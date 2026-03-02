#include "transform/demo_stats.hpp"

#include "grh.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wolvrix::lib::transform
{

    StatsPass::StatsPass() : Pass("stats", "operation-value-stats", "Count graphs, operations, and values for diagnostics") {}

    PassResult StatsPass::run()
    {
        std::size_t graphCount = 0;
        std::size_t opCount = 0;
        std::size_t valueCount = 0;
        uint64_t valueBitwidthTotal = 0;
        std::map<int32_t, std::size_t> valueWidthCounts;
        std::map<std::string, std::size_t> opKindCounts;
        std::map<int64_t, std::size_t> registerWidthCounts;
        std::map<int64_t, std::size_t> latchWidthCounts;
        std::map<int64_t, std::size_t> memoryWidthCounts;
        std::map<uint64_t, std::size_t> memoryCapacityCounts;
        std::map<uint64_t, std::size_t> coneDepthCounts;
        std::map<uint64_t, std::size_t> coneSizeCounts;
        std::map<uint64_t, std::size_t> coneFaninCounts;
        std::map<uint64_t, std::size_t> combFanoutStatefulCounts;
        struct MaxSymbolEntry
        {
            bool valid = false;
            uint64_t value = 0;
            std::vector<std::string> symbols;
        };
        MaxSymbolEntry maxValueWidth;
        MaxSymbolEntry maxRegisterWidth;
        MaxSymbolEntry maxLatchWidth;
        MaxSymbolEntry maxMemoryWidth;
        MaxSymbolEntry maxMemoryCapacity;
        MaxSymbolEntry maxWriteportConeDepth;
        MaxSymbolEntry maxWriteportConeSize;
        MaxSymbolEntry maxWriteportConeFanin;
        MaxSymbolEntry maxCombFanout;
        MaxSymbolEntry maxReadportFanout;
        auto getIntAttr = [](const std::optional<wolvrix::lib::grh::AttributeValue> &attr) -> std::optional<int64_t>
        {
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<int64_t>(&(*attr)))
            {
                return *value;
            }
            return std::nullopt;
        };

        auto isCombinationalOp = [](wolvrix::lib::grh::OperationKind kind) -> bool
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegister:
            case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatch:
            case wolvrix::lib::grh::OperationKind::kLatchReadPort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kMemory:
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
            case wolvrix::lib::grh::OperationKind::kInstance:
            case wolvrix::lib::grh::OperationKind::kBlackbox:
            case wolvrix::lib::grh::OperationKind::kSystemFunction:
            case wolvrix::lib::grh::OperationKind::kSystemTask:
            case wolvrix::lib::grh::OperationKind::kDpicImport:
            case wolvrix::lib::grh::OperationKind::kDpicCall:
            case wolvrix::lib::grh::OperationKind::kXMRRead:
            case wolvrix::lib::grh::OperationKind::kXMRWrite:
                return false;
            default:
                return true;
            }
        };
        auto isStatefulWritePort = [](wolvrix::lib::grh::OperationKind kind) -> bool
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
                return true;
            default:
                return false;
            }
        };
        auto qualifyGraphSymbol = [](const wolvrix::lib::grh::Graph *graph) -> std::string
        {
            if (!graph)
            {
                return "graph";
            }
            const auto &symbol = graph->symbol();
            if (!symbol.empty())
            {
                return symbol;
            }
            return "graph#" + std::to_string(graph->id().index);
        };
        auto qualifyValueSymbol = [&](const wolvrix::lib::grh::Graph *graph,
                                      const wolvrix::lib::grh::Value &value,
                                      const wolvrix::lib::grh::ValueId &valueId) -> std::string
        {
            std::string symbol(value.symbolText());
            if (symbol.empty())
            {
                symbol = "value#" + std::to_string(valueId.index);
            }
            return qualifyGraphSymbol(graph) + "::" + symbol;
        };
        auto qualifyOpSymbol = [&](const wolvrix::lib::grh::Graph *graph,
                                   const wolvrix::lib::grh::Operation &op,
                                   const wolvrix::lib::grh::OperationId &opId) -> std::string
        {
            std::string symbol(op.symbolText());
            if (symbol.empty())
            {
                symbol = "op#" + std::to_string(opId.index);
            }
            return qualifyGraphSymbol(graph) + "::" + symbol;
        };
        auto updateMax = [](MaxSymbolEntry &entry, uint64_t value, const std::string &symbol)
        {
            if (!entry.valid || value > entry.value)
            {
                entry.valid = true;
                entry.value = value;
                entry.symbols.clear();
                entry.symbols.push_back(symbol);
                return;
            }
            if (value == entry.value)
            {
                if (std::find(entry.symbols.begin(), entry.symbols.end(), symbol) == entry.symbols.end())
                {
                    entry.symbols.push_back(symbol);
                }
            }
        };
        auto mergeMax = [&](MaxSymbolEntry &dst, const MaxSymbolEntry &src)
        {
            if (!src.valid)
            {
                return;
            }
            if (!dst.valid || src.value > dst.value)
            {
                dst = src;
                return;
            }
            if (src.value == dst.value)
            {
                for (const auto &symbol : src.symbols)
                {
                    if (std::find(dst.symbols.begin(), dst.symbols.end(), symbol) == dst.symbols.end())
                    {
                        dst.symbols.push_back(symbol);
                    }
                }
            }
        };

        struct ConeTask
        {
            const wolvrix::lib::grh::Graph *graph = nullptr;
            wolvrix::lib::grh::ValueId root{};
        };
        struct GraphContext
        {
            const wolvrix::lib::grh::Graph *graph = nullptr;
            std::unordered_map<wolvrix::lib::grh::ValueId, std::size_t,
                               wolvrix::lib::grh::ValueIdHash>
                outputPortCounts;
            std::unordered_map<wolvrix::lib::grh::ValueId, std::size_t,
                               wolvrix::lib::grh::ValueIdHash>
                declaredValueCounts;
        };
        struct FanoutTask
        {
            const GraphContext *ctx = nullptr;
            wolvrix::lib::grh::OperationId op{};
        };
        struct ConeLocal
        {
            std::unordered_map<uint64_t, std::size_t> depthCounts;
            std::unordered_map<uint64_t, std::size_t> sizeCounts;
            std::unordered_map<uint64_t, std::size_t> faninCounts;
            MaxSymbolEntry maxDepth;
            MaxSymbolEntry maxSize;
            MaxSymbolEntry maxFanin;
            std::unordered_map<wolvrix::lib::grh::ValueId, uint64_t,
                               wolvrix::lib::grh::ValueIdHash>
                depthMemo;
            std::unordered_map<wolvrix::lib::grh::ValueId, uint8_t,
                               wolvrix::lib::grh::ValueIdHash>
                depthState;
        };
        struct FanoutLocal
        {
            std::unordered_map<uint64_t, std::size_t> counts;
            MaxSymbolEntry maxFanout;
        };

        std::vector<GraphContext> graphContexts;
        graphContexts.reserve(design().graphs().size());
        for (const auto &entry : design().graphs())
        {
            GraphContext ctx;
            ctx.graph = entry.second.get();
            for (const auto &port : ctx.graph->outputPorts())
            {
                ++ctx.outputPortCounts[port.value];
            }
            for (const auto &port : ctx.graph->inoutPorts())
            {
                ++ctx.outputPortCounts[port.out];
            }
            for (const auto &symbol : ctx.graph->declaredSymbols())
            {
                const auto valueId = ctx.graph->findValue(symbol);
                if (valueId.valid())
                {
                    ++ctx.declaredValueCounts[valueId];
                }
            }
            graphContexts.push_back(std::move(ctx));
        }
        for (const auto &ctx : graphContexts)
        {
            (void)ctx.graph->operations();
            (void)ctx.graph->values();
        }

        std::vector<ConeTask> coneTasks;
        std::vector<FanoutTask> combFanoutTasks;
        std::vector<FanoutTask> readportFanoutTasks;

        for (const auto &ctx : graphContexts)
        {
            ++graphCount;
            const auto *graph = ctx.graph;
            for (const auto &opId : graph->operations())
            {
                ++opCount;
                const auto op = graph->getOperation(opId);
                const std::string kindText(toString(op.kind()));
                ++opKindCounts[kindText];
                const auto width = getIntAttr(op.attr("width"));
                const auto rows = getIntAttr(op.attr("row"));
                switch (op.kind())
                {
                case wolvrix::lib::grh::OperationKind::kRegister:
                    if (width && *width > 0)
                    {
                        ++registerWidthCounts[*width];
                        const uint64_t widthValue = static_cast<uint64_t>(*width);
                        if (!maxRegisterWidth.valid || widthValue >= maxRegisterWidth.value)
                        {
                            updateMax(maxRegisterWidth, widthValue, qualifyOpSymbol(graph, op, opId));
                        }
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kLatch:
                    if (width && *width > 0)
                    {
                        ++latchWidthCounts[*width];
                        const uint64_t widthValue = static_cast<uint64_t>(*width);
                        if (!maxLatchWidth.valid || widthValue >= maxLatchWidth.value)
                        {
                            updateMax(maxLatchWidth, widthValue, qualifyOpSymbol(graph, op, opId));
                        }
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kMemory:
                    if (width && *width > 0)
                    {
                        ++memoryWidthCounts[*width];
                        const uint64_t widthValue = static_cast<uint64_t>(*width);
                        if (!maxMemoryWidth.valid || widthValue >= maxMemoryWidth.value)
                        {
                            updateMax(maxMemoryWidth, widthValue, qualifyOpSymbol(graph, op, opId));
                        }
                    }
                    if (width && rows && *width > 0 && *rows > 0)
                    {
                        const uint64_t w = static_cast<uint64_t>(*width);
                        const uint64_t r = static_cast<uint64_t>(*rows);
                        if (w <= std::numeric_limits<uint64_t>::max() / r)
                        {
                            const uint64_t capacity = w * r;
                            ++memoryCapacityCounts[capacity];
                            if (!maxMemoryCapacity.valid || capacity >= maxMemoryCapacity.value)
                            {
                                updateMax(maxMemoryCapacity, capacity,
                                          qualifyOpSymbol(graph, op, opId));
                            }
                        }
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
                case wolvrix::lib::grh::OperationKind::kLatchWritePort:
                    if (op.operands().size() >= 2)
                    {
                        coneTasks.push_back({graph, op.operands()[0]});
                        coneTasks.push_back({graph, op.operands()[1]});
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
                    if (op.operands().size() >= 3)
                    {
                        coneTasks.push_back({graph, op.operands()[0]});
                        coneTasks.push_back({graph, op.operands()[2]});
                    }
                    break;
                default:
                    break;
                }

                if (isCombinationalOp(op.kind()))
                {
                    combFanoutTasks.push_back({&ctx, opId});
                }
                switch (op.kind())
                {
                case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
                case wolvrix::lib::grh::OperationKind::kLatchReadPort:
                case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                    readportFanoutTasks.push_back({&ctx, opId});
                    break;
                default:
                    break;
                }
            }
            for (const auto &valueId : graph->values())
            {
                ++valueCount;
                const auto value = graph->getValue(valueId);
                const int32_t width = value.width();
                ++valueWidthCounts[width];
                if (width > 0)
                {
                    valueBitwidthTotal += static_cast<uint64_t>(width);
                    const uint64_t widthValue = static_cast<uint64_t>(width);
                    if (!maxValueWidth.valid || widthValue >= maxValueWidth.value)
                    {
                        updateMax(maxValueWidth, widthValue,
                                  qualifyValueSymbol(graph, value, valueId));
                    }
                }
            }
        }

        const std::size_t maxThreads =
            std::max<std::size_t>(1, static_cast<std::size_t>(std::thread::hardware_concurrency()));
        auto runParallel = [&](const auto &tasks, auto &locals, auto &&func)
        {
            if (tasks.empty())
            {
                return;
            }
            const std::size_t threadCount = std::min<std::size_t>(maxThreads, tasks.size());
            locals.resize(threadCount);
            std::atomic<std::size_t> next{0};
            std::vector<std::thread> threads;
            threads.reserve(threadCount);
            for (std::size_t t = 0; t < threadCount; ++t)
            {
                threads.emplace_back([&, t]() {
                    for (;;)
                    {
                        const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                        if (index >= tasks.size())
                        {
                            return;
                        }
                        func(tasks[index], locals[t]);
                    }
                });
            }
            for (auto &thread : threads)
            {
                thread.join();
            }
        };
        auto mergeCounts = [](auto &dst, const auto &src)
        {
            for (const auto &entry : src)
            {
                dst[entry.first] += entry.second;
            }
        };

        std::vector<ConeLocal> coneLocals;
        runParallel(
            coneTasks,
            coneLocals,
            [&](const ConeTask &task, ConeLocal &local) {
                if (!task.root.valid())
                {
                    return;
                }
                auto computeDepth = [&](auto &&self, wolvrix::lib::grh::ValueId valueId) -> uint64_t
                {
                    if (!valueId.valid())
                    {
                        return 0;
                    }
                    auto memoIt = local.depthMemo.find(valueId);
                    if (memoIt != local.depthMemo.end())
                    {
                        return memoIt->second;
                    }
                    auto stateIt = local.depthState.find(valueId);
                    if (stateIt != local.depthState.end() && stateIt->second == 1)
                    {
                        local.depthState[valueId] = 2;
                        local.depthMemo[valueId] = 0;
                        return 0;
                    }
                    local.depthState[valueId] = 1;
                    uint64_t depth = 0;
                    const auto value = task.graph->getValue(valueId);
                    const auto defOpId = value.definingOp();
                    if (defOpId.valid())
                    {
                        const auto defOp = task.graph->getOperation(defOpId);
                        if (isCombinationalOp(defOp.kind()))
                        {
                            uint64_t maxDepth = 0;
                            for (const auto &operand : defOp.operands())
                            {
                                maxDepth = std::max(maxDepth, self(self, operand));
                            }
                            depth = maxDepth + 1;
                        }
                    }
                    local.depthState[valueId] = 2;
                    local.depthMemo[valueId] = depth;
                    return depth;
                };

                std::unordered_set<wolvrix::lib::grh::OperationId,
                                   wolvrix::lib::grh::OperationIdHash>
                    visitedOps;
                std::unordered_set<wolvrix::lib::grh::ValueId,
                                   wolvrix::lib::grh::ValueIdHash>
                    visitedValues;
                std::unordered_set<wolvrix::lib::grh::ValueId,
                                   wolvrix::lib::grh::ValueIdHash>
                    leafValues;
                std::vector<wolvrix::lib::grh::ValueId> stack;
                stack.push_back(task.root);
                while (!stack.empty())
                {
                    const auto valueId = stack.back();
                    stack.pop_back();
                    if (!valueId.valid())
                    {
                        continue;
                    }
                    if (!visitedValues.insert(valueId).second)
                    {
                        continue;
                    }
                    const auto value = task.graph->getValue(valueId);
                    const auto defOpId = value.definingOp();
                    if (!defOpId.valid())
                    {
                        leafValues.insert(valueId);
                        continue;
                    }
                    const auto defOp = task.graph->getOperation(defOpId);
                    if (!isCombinationalOp(defOp.kind()))
                    {
                        leafValues.insert(valueId);
                        continue;
                    }
                    if (!visitedOps.insert(defOpId).second)
                    {
                        continue;
                    }
                    for (const auto &operand : defOp.operands())
                    {
                        stack.push_back(operand);
                    }
                }
                const uint64_t depth = computeDepth(computeDepth, task.root);
                const uint64_t size = static_cast<uint64_t>(visitedOps.size());
                const uint64_t fanin = static_cast<uint64_t>(leafValues.size());
                ++local.depthCounts[depth];
                ++local.sizeCounts[size];
                ++local.faninCounts[fanin];
                const auto rootValue = task.graph->getValue(task.root);
                const auto rootSymbol = qualifyValueSymbol(task.graph, rootValue, task.root);
                updateMax(local.maxDepth, depth, rootSymbol);
                updateMax(local.maxSize, size, rootSymbol);
                updateMax(local.maxFanin, fanin, rootSymbol);
            });
        for (const auto &local : coneLocals)
        {
            mergeCounts(coneDepthCounts, local.depthCounts);
            mergeCounts(coneSizeCounts, local.sizeCounts);
            mergeCounts(coneFaninCounts, local.faninCounts);
            mergeMax(maxWriteportConeDepth, local.maxDepth);
            mergeMax(maxWriteportConeSize, local.maxSize);
            mergeMax(maxWriteportConeFanin, local.maxFanin);
        }

        auto computeSinkFanout = [&](const GraphContext *ctx,
                                     std::span<const wolvrix::lib::grh::ValueId> roots) -> uint64_t
        {
            const auto *graph = ctx->graph;
            std::unordered_set<wolvrix::lib::grh::OperationId,
                               wolvrix::lib::grh::OperationIdHash>
                drivenSinks;
            std::unordered_set<wolvrix::lib::grh::ValueId,
                               wolvrix::lib::grh::ValueIdHash>
                visitedValues;
            std::unordered_set<wolvrix::lib::grh::OperationId,
                               wolvrix::lib::grh::OperationIdHash>
                visitedOps;
            std::unordered_set<wolvrix::lib::grh::ValueId,
                               wolvrix::lib::grh::ValueIdHash>
                sinkSeen;
            std::vector<wolvrix::lib::grh::ValueId> stack;
            stack.reserve(roots.size());
            for (const auto &root : roots)
            {
                if (root.valid())
                {
                    stack.push_back(root);
                }
            }
            uint64_t outputCount = 0;
            while (!stack.empty())
            {
                const auto valueId = stack.back();
                stack.pop_back();
                if (!valueId.valid())
                {
                    continue;
                }
                if (!visitedValues.insert(valueId).second)
                {
                    continue;
                }
                const auto outputIt = ctx->outputPortCounts.find(valueId);
                const auto declaredIt = ctx->declaredValueCounts.find(valueId);
                if (outputIt != ctx->outputPortCounts.end() ||
                    declaredIt != ctx->declaredValueCounts.end())
                {
                    if (sinkSeen.insert(valueId).second)
                    {
                        if (outputIt != ctx->outputPortCounts.end())
                        {
                            outputCount += static_cast<uint64_t>(outputIt->second);
                        }
                        else if (declaredIt != ctx->declaredValueCounts.end())
                        {
                            outputCount += static_cast<uint64_t>(declaredIt->second);
                        }
                    }
                }
                const auto value = graph->getValue(valueId);
                for (const auto &user : value.users())
                {
                    const auto userOpId = user.operation;
                    const auto userOp = graph->getOperation(userOpId);
                    if (!isCombinationalOp(userOp.kind()))
                    {
                        drivenSinks.insert(userOpId);
                        continue;
                    }
                    if (!visitedOps.insert(userOpId).second)
                    {
                        continue;
                    }
                    for (const auto &res : userOp.results())
                    {
                        if (res.valid())
                        {
                            stack.push_back(res);
                        }
                    }
                }
            }
            return static_cast<uint64_t>(drivenSinks.size()) + outputCount;
        };

        std::vector<FanoutLocal> combFanoutLocals;
        runParallel(
            combFanoutTasks,
            combFanoutLocals,
            [&](const FanoutTask &task, FanoutLocal &local) {
                if (!task.ctx || !task.op.valid())
                {
                    return;
                }
                const auto op = task.ctx->graph->getOperation(task.op);
                const uint64_t fanout = computeSinkFanout(task.ctx, op.results());
                ++local.counts[fanout];
                updateMax(local.maxFanout, fanout,
                          qualifyOpSymbol(task.ctx->graph, op, task.op));
            });
        for (const auto &local : combFanoutLocals)
        {
            mergeCounts(combFanoutStatefulCounts, local.counts);
            mergeMax(maxCombFanout, local.maxFanout);
        }

        std::map<uint64_t, std::size_t> readportFanoutCounts;
        std::vector<FanoutLocal> readportFanoutLocals;
        runParallel(
            readportFanoutTasks,
            readportFanoutLocals,
            [&](const FanoutTask &task, FanoutLocal &local) {
                if (!task.ctx || !task.op.valid())
                {
                    return;
                }
                const auto op = task.ctx->graph->getOperation(task.op);
                const uint64_t fanout = computeSinkFanout(task.ctx, op.results());
                ++local.counts[fanout];
                updateMax(local.maxFanout, fanout,
                          qualifyOpSymbol(task.ctx->graph, op, task.op));
            });
        for (const auto &local : readportFanoutLocals)
        {
            mergeCounts(readportFanoutCounts, local.counts);
            mergeMax(maxReadportFanout, local.maxFanout);
        }

        std::ostringstream oss;
        oss << "{";
        bool firstField = true;
        auto appendScalar = [&](std::string_view name, uint64_t value)
        {
            if (!firstField)
            {
                oss << ",";
            }
            oss << "\"" << name << "\":" << value;
            firstField = false;
        };
        auto appendJsonMap = [&](std::string_view name, const auto &entries)
        {
            if (!firstField)
            {
                oss << ",";
            }
            oss << "\"" << name << "\":{";
            bool first = true;
            for (const auto &entry : entries)
            {
                if (!first)
                {
                    oss << ",";
                }
                oss << "\"" << entry.first << "\":" << entry.second;
                first = false;
            }
            oss << "}";
            firstField = false;
        };
        auto appendEscapedString = [&](std::string_view text)
        {
            for (char ch : text)
            {
                switch (ch)
                {
                case '\\':
                    oss << "\\\\";
                    break;
                case '"':
                    oss << "\\\"";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                case '\r':
                    oss << "\\r";
                    break;
                case '\t':
                    oss << "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20)
                    {
                        oss << "\\u00";
                        const char *digits = "0123456789abcdef";
                        oss << digits[(ch >> 4) & 0x0f];
                        oss << digits[ch & 0x0f];
                    }
                    else
                    {
                        oss << ch;
                    }
                    break;
                }
            }
        };
        auto appendMaxSymbolEntry = [&](std::string_view name, const MaxSymbolEntry &entry, bool &first)
        {
            if (!first)
            {
                oss << ",";
            }
            oss << "\"" << name << "\":{";
            oss << "\"max\":" << (entry.valid ? entry.value : 0) << ",\"symbols\":[";
            bool firstSymbol = true;
            for (const auto &symbol : entry.symbols)
            {
                if (!firstSymbol)
                {
                    oss << ",";
                }
                oss << "\"";
                appendEscapedString(symbol);
                oss << "\"";
                firstSymbol = false;
            }
            oss << "]}";
            first = false;
        };

        appendScalar("graph_count", graphCount);
        appendScalar("operation_count", opCount);
        appendScalar("value_count", valueCount);
        appendScalar("value_bitwidth_total", valueBitwidthTotal);
        appendJsonMap("value_widths", valueWidthCounts);
        appendJsonMap("operation_kinds", opKindCounts);
        appendJsonMap("register_widths", registerWidthCounts);
        appendJsonMap("latch_widths", latchWidthCounts);
        appendJsonMap("memory_widths", memoryWidthCounts);
        appendJsonMap("memory_capacity_bits", memoryCapacityCounts);
        appendJsonMap("writeport_cone_depths", coneDepthCounts);
        appendJsonMap("writeport_cone_sizes", coneSizeCounts);
        appendJsonMap("writeport_cone_fanins", coneFaninCounts);
        appendJsonMap("comb_op_fanout_sinks", combFanoutStatefulCounts);
        appendJsonMap("readport_fanout_sinks", readportFanoutCounts);
        if (!firstField)
        {
            oss << ",";
        }
        oss << "\"max_symbols\":{";
        bool firstMax = true;
        appendMaxSymbolEntry("value_widths", maxValueWidth, firstMax);
        appendMaxSymbolEntry("register_widths", maxRegisterWidth, firstMax);
        appendMaxSymbolEntry("latch_widths", maxLatchWidth, firstMax);
        appendMaxSymbolEntry("memory_widths", maxMemoryWidth, firstMax);
        appendMaxSymbolEntry("memory_capacity_bits", maxMemoryCapacity, firstMax);
        appendMaxSymbolEntry("writeport_cone_depths", maxWriteportConeDepth, firstMax);
        appendMaxSymbolEntry("writeport_cone_sizes", maxWriteportConeSize, firstMax);
        appendMaxSymbolEntry("writeport_cone_fanins", maxWriteportConeFanin, firstMax);
        appendMaxSymbolEntry("comb_op_fanout_sinks", maxCombFanout, firstMax);
        appendMaxSymbolEntry("readport_fanout_sinks", maxReadportFanout, firstMax);
        oss << "}";
        firstField = false;
        oss << "}";

        std::string message = oss.str();

        info(message);

        PassResult result;
        result.changed = false;
        result.failed = false;
        result.artifacts.push_back(message);

        return result;
    }

} // namespace wolvrix::lib::transform
