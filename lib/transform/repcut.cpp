#include "transform/repcut.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

namespace wolvrix::lib::transform
{

    namespace
    {
        using NodeId = uint32_t;
        using AscId = uint32_t;
        using PieceId = uint32_t;
        constexpr NodeId kInvalidNode = std::numeric_limits<NodeId>::max();
        constexpr PieceId kInvalidPiece = std::numeric_limits<PieceId>::max();
        constexpr std::size_t kMaxConeCollectThreads = 8;
        constexpr std::size_t kConeCollectChunkSize = 64;

        template <typename T>
        std::optional<T> getAttr(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            std::optional<wolvrix::lib::grh::AttributeValue> attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (auto ptr = std::get_if<T>(&*attr))
            {
                return *ptr;
            }
            return std::nullopt;
        }

        std::optional<std::vector<std::string>> getAttrStrings(const wolvrix::lib::grh::Operation &op,
                                                               std::string_view key)
        {
            return getAttr<std::vector<std::string>>(op, key);
        }

        std::optional<std::string> getAttrString(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            return getAttr<std::string>(op, key);
        }

        std::optional<bool> getAttrBool(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            return getAttr<bool>(op, key);
        }

        bool opHasEvents(const wolvrix::lib::grh::Operation &op)
        {
            auto edges = getAttrStrings(op, "eventEdge");
            return edges && !edges->empty();
        }

        bool isSinkOpKind(wolvrix::lib::grh::OperationKind kind)
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
        }

        bool isSourceOpKind(wolvrix::lib::grh::OperationKind kind)
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
            case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
            case wolvrix::lib::grh::OperationKind::kLatchReadPort:
                return true;
            default:
                return false;
            }
        }

        bool isCombOp(const wolvrix::lib::grh::Operation &op)
        {
            if (opHasEvents(op))
            {
                return false;
            }

            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
            case wolvrix::lib::grh::OperationKind::kAdd:
            case wolvrix::lib::grh::OperationKind::kSub:
            case wolvrix::lib::grh::OperationKind::kMul:
            case wolvrix::lib::grh::OperationKind::kDiv:
            case wolvrix::lib::grh::OperationKind::kMod:
            case wolvrix::lib::grh::OperationKind::kEq:
            case wolvrix::lib::grh::OperationKind::kNe:
            case wolvrix::lib::grh::OperationKind::kCaseEq:
            case wolvrix::lib::grh::OperationKind::kCaseNe:
            case wolvrix::lib::grh::OperationKind::kWildcardEq:
            case wolvrix::lib::grh::OperationKind::kWildcardNe:
            case wolvrix::lib::grh::OperationKind::kLt:
            case wolvrix::lib::grh::OperationKind::kLe:
            case wolvrix::lib::grh::OperationKind::kGt:
            case wolvrix::lib::grh::OperationKind::kGe:
            case wolvrix::lib::grh::OperationKind::kAnd:
            case wolvrix::lib::grh::OperationKind::kOr:
            case wolvrix::lib::grh::OperationKind::kXor:
            case wolvrix::lib::grh::OperationKind::kXnor:
            case wolvrix::lib::grh::OperationKind::kNot:
            case wolvrix::lib::grh::OperationKind::kLogicAnd:
            case wolvrix::lib::grh::OperationKind::kLogicOr:
            case wolvrix::lib::grh::OperationKind::kLogicNot:
            case wolvrix::lib::grh::OperationKind::kReduceAnd:
            case wolvrix::lib::grh::OperationKind::kReduceOr:
            case wolvrix::lib::grh::OperationKind::kReduceXor:
            case wolvrix::lib::grh::OperationKind::kReduceNor:
            case wolvrix::lib::grh::OperationKind::kReduceNand:
            case wolvrix::lib::grh::OperationKind::kReduceXnor:
            case wolvrix::lib::grh::OperationKind::kShl:
            case wolvrix::lib::grh::OperationKind::kLShr:
            case wolvrix::lib::grh::OperationKind::kAShr:
            case wolvrix::lib::grh::OperationKind::kMux:
            case wolvrix::lib::grh::OperationKind::kAssign:
            case wolvrix::lib::grh::OperationKind::kConcat:
            case wolvrix::lib::grh::OperationKind::kReplicate:
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
            case wolvrix::lib::grh::OperationKind::kSliceArray:
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                return true;
            case wolvrix::lib::grh::OperationKind::kSystemFunction:
            {
                auto sideEffects = getAttrBool(op, "hasSideEffects");
                return !sideEffects || !*sideEffects;
            }
            default:
                return false;
            }
        }

        bool isSourceValue(const wolvrix::lib::grh::Graph &graph,
                           wolvrix::lib::grh::ValueId value,
                           const std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> &inoutInputValues)
        {
            if (graph.valueIsInput(value) || inoutInputValues.find(value) != inoutInputValues.end())
            {
                return true;
            }

            const wolvrix::lib::grh::OperationId defOp = graph.valueDef(value);
            if (!defOp.valid())
            {
                return true;
            }

            const wolvrix::lib::grh::Operation op = graph.getOperation(defOp);
            return isSourceOpKind(op.kind());
        }

        struct PhaseAData
        {
            std::unordered_map<wolvrix::lib::grh::OperationId, NodeId, wolvrix::lib::grh::OperationIdHash> opToNode;
            std::vector<wolvrix::lib::grh::OperationId> nodeToOp;
            std::vector<std::vector<NodeId>> inNeighbors;
            std::vector<std::vector<NodeId>> outNeighbors;
        };

        struct SinkRef
        {
            enum class Kind
            {
                Operation,
                OutputValue
            };

            Kind kind = Kind::Operation;
            wolvrix::lib::grh::OperationId op = wolvrix::lib::grh::OperationId::invalid();
            wolvrix::lib::grh::ValueId value = wolvrix::lib::grh::ValueId::invalid();
        };

        struct AscInfo
        {
            std::vector<size_t> sinks;
            std::vector<NodeId> combOps;
        };

        struct PhaseBData
        {
            std::vector<SinkRef> sinks;
            std::vector<AscInfo> ascs;
            std::vector<AscId> sinkToAsc;
            std::vector<std::vector<AscId>> nodeToAscs;
            std::vector<std::unordered_set<NodeId>> pieces;
            std::vector<PieceId> nodeToPiece;
            std::vector<std::vector<AscId>> pieceToAscs;
        };

        using ProgressLogger = std::function<void(const std::string &)>;

        struct HyperGraph
        {
            struct HyperEdge
            {
                std::vector<AscId> nodes;
                uint32_t weight = 1;
            };

            std::vector<uint32_t> nodeWeights;
            std::vector<HyperEdge> edges;
        };

        struct StorageInfo
        {
            wolvrix::lib::grh::OperationId declOp = wolvrix::lib::grh::OperationId::invalid();
            std::vector<wolvrix::lib::grh::OperationId> readPorts;
            std::vector<wolvrix::lib::grh::OperationId> writePorts;
        };

        struct ValueInfo
        {
            std::string symbol;
            int32_t width = 0;
            bool isSigned = false;
            wolvrix::lib::grh::ValueType type = wolvrix::lib::grh::ValueType::Logic;
            std::optional<wolvrix::lib::grh::SrcLoc> srcLoc;
        };

        struct CrossPartitionValue
        {
            wolvrix::lib::grh::ValueId value = wolvrix::lib::grh::ValueId::invalid();
            uint32_t srcPart = 0;
            uint32_t dstPart = 0;
            bool requiresPort = false;
            bool allowed = true;
        };

        ValueInfo captureValueInfo(const wolvrix::lib::grh::Graph &graph, wolvrix::lib::grh::ValueId valueId)
        {
            const auto value = graph.getValue(valueId);
            ValueInfo info;
            info.symbol = std::string(value.symbolText());
            info.width = value.width();
            info.isSigned = value.isSigned();
            info.type = value.type();
            info.srcLoc = value.srcLoc();
            return info;
        }

        std::string normalizePortBase(std::string_view text, std::string_view fallback)
        {
            std::string normalized = wolvrix::lib::grh::Graph::normalizeComponent(text);
            if (normalized.empty())
            {
                normalized = std::string(fallback);
            }
            if (!normalized.empty() && normalized.front() >= '0' && normalized.front() <= '9')
            {
                normalized.insert(normalized.begin(), '_');
            }
            return normalized;
        }

        std::string uniquePortName(std::unordered_set<std::string> &used, std::string base)
        {
            std::string candidate = std::move(base);
            if (used.insert(candidate).second)
            {
                return candidate;
            }
            const std::string root = candidate;
            int suffix = 0;
            while (true)
            {
                candidate = root + "_" + std::to_string(++suffix);
                if (used.insert(candidate).second)
                {
                    return candidate;
                }
            }
        }

        std::string uniqueGraphName(wolvrix::lib::grh::Design &design, std::string base)
        {
            std::string candidate = std::move(base);
            const std::string root = candidate;
            int suffix = 0;
            while (design.findGraph(candidate) != nullptr)
            {
                candidate = root + "_" + std::to_string(++suffix);
            }
            return candidate;
        }

        wolvrix::lib::grh::SymbolId internUniqueSymbol(wolvrix::lib::grh::Graph &graph, std::string base)
        {
            if (base.empty())
            {
                base = "value";
            }
            std::string candidate = std::move(base);
            const std::string root = candidate;
            int suffix = 0;
            while (true)
            {
                const wolvrix::lib::grh::SymbolId sym = graph.internSymbol(candidate);
                if (sym.valid() && !graph.findValue(sym).valid() && !graph.findOperation(sym).valid())
                {
                    return sym;
                }
                candidate = root + "_" + std::to_string(++suffix);
            }
        }

        wolvrix::lib::grh::OperationId buildInstance(
            wolvrix::lib::grh::Graph &parent,
            std::string_view moduleName,
            std::string_view instanceBase,
            const wolvrix::lib::grh::Graph &target,
            const std::unordered_map<std::string, wolvrix::lib::grh::ValueId> &inputMapping,
            const std::unordered_map<std::string, wolvrix::lib::grh::ValueId> &outputMapping)
        {
            const std::string opBase = std::string("inst_") + std::string(instanceBase);
            const wolvrix::lib::grh::SymbolId opSym = internUniqueSymbol(parent, opBase);
            const wolvrix::lib::grh::OperationId inst = parent.createOperation(wolvrix::lib::grh::OperationKind::kInstance, opSym);
            parent.setAttr(inst, "moduleName", std::string(moduleName));

            std::vector<std::string> inputNames;
            inputNames.reserve(target.inputPorts().size());
            for (const auto &port : target.inputPorts())
            {
                inputNames.push_back(port.name);
            }
            std::vector<std::string> outputNames;
            outputNames.reserve(target.outputPorts().size());
            for (const auto &port : target.outputPorts())
            {
                outputNames.push_back(port.name);
            }
            parent.setAttr(inst, "inputPortName", inputNames);
            parent.setAttr(inst, "outputPortName", outputNames);
            parent.setAttr(inst, "instanceName", std::string(instanceBase));

            for (const auto &portName : inputNames)
            {
                auto it = inputMapping.find(portName);
                if (it != inputMapping.end())
                {
                    parent.addOperand(inst, it->second);
                }
            }
            for (const auto &portName : outputNames)
            {
                auto it = outputMapping.find(portName);
                if (it != outputMapping.end())
                {
                    parent.addResult(inst, it->second);
                }
            }
            return inst;
        }

        class DisjointSet
        {
        public:
            explicit DisjointSet(size_t count)
                : parent_(count), rank_(count, 0)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    parent_[i] = i;
                }
            }

            size_t find(size_t value)
            {
                size_t root = value;
                while (parent_[root] != root)
                {
                    root = parent_[root];
                }
                while (parent_[value] != value)
                {
                    const size_t next = parent_[value];
                    parent_[value] = root;
                    value = next;
                }
                return root;
            }

            void unite(size_t lhs, size_t rhs)
            {
                lhs = find(lhs);
                rhs = find(rhs);
                if (lhs == rhs)
                {
                    return;
                }
                if (rank_[lhs] < rank_[rhs])
                {
                    parent_[lhs] = rhs;
                }
                else if (rank_[lhs] > rank_[rhs])
                {
                    parent_[rhs] = lhs;
                }
                else
                {
                    parent_[rhs] = lhs;
                    ++rank_[lhs];
                }
            }

        private:
            std::vector<size_t> parent_;
            std::vector<uint32_t> rank_;
        };

        PhaseAData buildPhaseAData(const wolvrix::lib::grh::Graph &graph)
        {
            PhaseAData data;

            const std::span<const wolvrix::lib::grh::OperationId> ops = graph.operations();
            data.nodeToOp.reserve(ops.size());

            for (const auto opId : ops)
            {
                const NodeId node = static_cast<NodeId>(data.nodeToOp.size());
                data.nodeToOp.push_back(opId);
                data.opToNode.emplace(opId, node);
            }

            data.inNeighbors.resize(data.nodeToOp.size());
            data.outNeighbors.resize(data.nodeToOp.size());

            std::vector<std::unordered_set<NodeId>> inSets(data.nodeToOp.size());
            std::vector<std::unordered_set<NodeId>> outSets(data.nodeToOp.size());

            for (NodeId node = 0; node < data.nodeToOp.size(); ++node)
            {
                const wolvrix::lib::grh::Operation op = graph.getOperation(data.nodeToOp[node]);
                for (const auto operand : op.operands())
                {
                    const wolvrix::lib::grh::OperationId predOp = graph.valueDef(operand);
                    if (!predOp.valid())
                    {
                        continue;
                    }
                    auto predIt = data.opToNode.find(predOp);
                    if (predIt == data.opToNode.end())
                    {
                        continue;
                    }
                    const NodeId pred = predIt->second;
                    if (pred == node)
                    {
                        continue;
                    }
                    if (inSets[node].insert(pred).second)
                    {
                        data.inNeighbors[node].push_back(pred);
                    }
                    if (outSets[pred].insert(node).second)
                    {
                        data.outNeighbors[pred].push_back(node);
                    }
                }
            }

            for (NodeId node = 0; node < data.nodeToOp.size(); ++node)
            {
                std::sort(data.inNeighbors[node].begin(), data.inNeighbors[node].end());
                std::sort(data.outNeighbors[node].begin(), data.outNeighbors[node].end());
            }

            return data;
        }

        std::vector<SinkRef> collectSinks(const wolvrix::lib::grh::Graph &graph)
        {
            std::vector<SinkRef> sinks;
            sinks.reserve(graph.operations().size() + graph.outputPorts().size() + graph.inoutPorts().size() * 2);

            for (const auto opId : graph.operations())
            {
                const wolvrix::lib::grh::Operation op = graph.getOperation(opId);
                if (!isSinkOpKind(op.kind()))
                {
                    continue;
                }
                SinkRef sink;
                sink.kind = SinkRef::Kind::Operation;
                sink.op = opId;
                sinks.push_back(sink);
            }
            for (const auto &port : graph.outputPorts())
            {
                SinkRef sink;
                sink.kind = SinkRef::Kind::OutputValue;
                sink.value = port.value;
                sinks.push_back(sink);
            }
            for (const auto &port : graph.inoutPorts())
            {
                SinkRef outSink;
                outSink.kind = SinkRef::Kind::OutputValue;
                outSink.value = port.out;
                sinks.push_back(outSink);

                SinkRef oeSink;
                oeSink.kind = SinkRef::Kind::OutputValue;
                oeSink.value = port.oe;
                sinks.push_back(oeSink);
            }

            return sinks;
        }

        struct MemSymbolCollectStats
        {
            uint64_t visitedNodes = 0;
            uint64_t memSymbolHits = 0;
        };

        struct MemSymbolIntern
        {
            uint32_t intern(const std::string &symbol)
            {
                auto it = symbolToId.find(symbol);
                if (it != symbolToId.end())
                {
                    return it->second;
                }
                const uint32_t id = static_cast<uint32_t>(idToSymbol.size());
                idToSymbol.push_back(symbol);
                symbolToId.emplace(idToSymbol.back(), id);
                return id;
            }

            std::unordered_map<std::string, uint32_t> symbolToId;
            std::vector<std::string> idToSymbol;
        };

        struct MemSymbolMemo
        {
            std::vector<std::vector<uint32_t>> nodeToMemSymbolIds;
            std::vector<uint8_t> nodeState;
        };

        const std::vector<uint32_t> &collectNodeMemSymbolIds(const wolvrix::lib::grh::Graph &graph,
                                                              const PhaseAData &phaseA,
                                                              NodeId node,
                                                              const std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> &inoutInputValues,
                                                              MemSymbolIntern &intern,
                                                              MemSymbolMemo &memo,
                                                              MemSymbolCollectStats &stats)
        {
            static const std::vector<uint32_t> kEmpty;
            if (node >= memo.nodeToMemSymbolIds.size() || node >= memo.nodeState.size())
            {
                return kEmpty;
            }

            uint8_t &state = memo.nodeState[node];
            if (state == 2)
            {
                return memo.nodeToMemSymbolIds[node];
            }
            if (state == 1)
            {
                return kEmpty;
            }

            state = 1;
            ++stats.visitedNodes;
            std::vector<uint32_t> &result = memo.nodeToMemSymbolIds[node];
            result.clear();

            const wolvrix::lib::grh::OperationId opId = phaseA.nodeToOp[node];
            const wolvrix::lib::grh::Operation op = graph.getOperation(opId);
            if (op.kind() == wolvrix::lib::grh::OperationKind::kMemoryReadPort)
            {
                if (auto sym = getAttrString(op, "memSymbol"))
                {
                    ++stats.memSymbolHits;
                    result.push_back(intern.intern(*sym));
                }
            }

            if (isCombOp(op))
            {
                for (const auto operand : op.operands())
                {
                    if (isSourceValue(graph, operand, inoutInputValues))
                    {
                        continue;
                    }
                    const wolvrix::lib::grh::OperationId predOp = graph.valueDef(operand);
                    if (!predOp.valid())
                    {
                        continue;
                    }
                    auto it = phaseA.opToNode.find(predOp);
                    if (it == phaseA.opToNode.end())
                    {
                        continue;
                    }
                    const std::vector<uint32_t> &childIds = collectNodeMemSymbolIds(graph,
                                                                                    phaseA,
                                                                                    it->second,
                                                                                    inoutInputValues,
                                                                                    intern,
                                                                                    memo,
                                                                                    stats);
                    result.insert(result.end(), childIds.begin(), childIds.end());
                }
            }

            if (result.size() > 1)
            {
                std::sort(result.begin(), result.end());
                result.erase(std::unique(result.begin(), result.end()), result.end());
            }

            state = 2;
            return result;
        }

        void collectConeMemSymbolIds(const wolvrix::lib::grh::Graph &graph,
                                     const PhaseAData &phaseA,
                                     const SinkRef &sink,
                                     const std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> &inoutInputValues,
                                     MemSymbolIntern &intern,
                                     MemSymbolMemo &memo,
                                     std::unordered_set<uint32_t> &memSymbolIds,
                                     MemSymbolCollectStats &stats)
        {
            auto collectFromValue = [&](wolvrix::lib::grh::ValueId value) {
                if (isSourceValue(graph, value, inoutInputValues))
                {
                    return;
                }

                const wolvrix::lib::grh::OperationId defOp = graph.valueDef(value);
                if (!defOp.valid())
                {
                    return;
                }
                auto it = phaseA.opToNode.find(defOp);
                if (it == phaseA.opToNode.end())
                {
                    return;
                }

                const std::vector<uint32_t> &ids = collectNodeMemSymbolIds(graph,
                                                                            phaseA,
                                                                            it->second,
                                                                            inoutInputValues,
                                                                            intern,
                                                                            memo,
                                                                            stats);
                memSymbolIds.insert(ids.begin(), ids.end());
            };

            if (sink.kind == SinkRef::Kind::Operation)
            {
                const wolvrix::lib::grh::Operation op = graph.getOperation(sink.op);
                for (const auto operand : op.operands())
                {
                    collectFromValue(operand);
                }
            }
            else
            {
                collectFromValue(sink.value);
            }
        }

        void collectAscCone(const wolvrix::lib::grh::Graph &graph,
                            const PhaseAData &phaseA,
                            const std::vector<SinkRef> &sinks,
                            const std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> &inoutInputValues,
                            std::vector<uint32_t> &visitStamp,
                            const uint32_t visitEpoch,
                            AscInfo &asc)
        {
            asc.combOps.clear();

            auto pushFromValue = [&](std::vector<NodeId> &stack, wolvrix::lib::grh::ValueId value) {
                if (isSourceValue(graph, value, inoutInputValues))
                {
                    return;
                }

                const wolvrix::lib::grh::OperationId defOp = graph.valueDef(value);
                if (!defOp.valid())
                {
                    return;
                }
                auto it = phaseA.opToNode.find(defOp);
                if (it == phaseA.opToNode.end())
                {
                    return;
                }
                const NodeId node = it->second;
                if (node >= visitStamp.size() || visitStamp[node] == visitEpoch)
                {
                    return;
                }
                visitStamp[node] = visitEpoch;
                stack.push_back(node);
            };

            if (asc.sinks.size() >= 32)
            {
                const size_t reserveHint = std::min<size_t>(phaseA.nodeToOp.size(), asc.sinks.size() * 8);
                asc.combOps.reserve(reserveHint);
            }

            std::vector<NodeId> stack;
            stack.reserve(asc.sinks.size() * 2 + 4);
            for (const size_t sinkIndex : asc.sinks)
            {
                const SinkRef &sink = sinks[sinkIndex];
                if (sink.kind == SinkRef::Kind::Operation)
                {
                    const wolvrix::lib::grh::Operation op = graph.getOperation(sink.op);
                    for (const auto operand : op.operands())
                    {
                        pushFromValue(stack, operand);
                    }
                }
                else
                {
                    pushFromValue(stack, sink.value);
                }
            }

            while (!stack.empty())
            {
                const NodeId node = stack.back();
                stack.pop_back();

                const wolvrix::lib::grh::Operation op = graph.getOperation(phaseA.nodeToOp[node]);
                if (isSourceOpKind(op.kind()))
                {
                    continue;
                }
                if (!isCombOp(op))
                {
                    continue;
                }

                asc.combOps.push_back(node);
                for (const NodeId pred : phaseA.inNeighbors[node])
                {
                    if (pred >= visitStamp.size() || visitStamp[pred] == visitEpoch)
                    {
                        continue;
                    }
                    visitStamp[pred] = visitEpoch;
                    stack.push_back(pred);
                }
            }
        }

        PhaseBData buildAscs(const wolvrix::lib::grh::Graph &graph,
                             const PhaseAData &phaseA,
                             const std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> &inoutInputValues,
                             const ProgressLogger &progressLogger)
        {
            const auto phaseStart = std::chrono::steady_clock::now();
            auto msSince = [&](const std::chrono::steady_clock::time_point &start) -> uint64_t {
                return static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                        .count());
            };

            PhaseBData data;
            data.sinks = collectSinks(graph);
            data.sinkToAsc.assign(data.sinks.size(), 0);

            if (progressLogger)
            {
                progressLogger("repcut phase-b/ascs: sinks_collected=" + std::to_string(data.sinks.size()));
            }

            if (data.sinks.empty())
            {
                return data;
            }

            DisjointSet dsu(data.sinks.size());

            std::unordered_map<std::string, std::vector<size_t>> regWriteSinks;
            std::unordered_map<std::string, std::vector<size_t>> latchWriteSinks;
            std::unordered_map<uint32_t, std::vector<size_t>> memWriteSinks;
            MemSymbolIntern memSymbolIntern;

            const auto indexStart = std::chrono::steady_clock::now();
            const size_t sinkCount = data.sinks.size();
            const size_t indexProgressEvery = sinkCount < 50000 ? 10000 : 50000;
            for (size_t i = 0; i < data.sinks.size(); ++i)
            {
                const SinkRef &sink = data.sinks[i];
                if (sink.kind != SinkRef::Kind::Operation)
                {
                    if (progressLogger && i > 0 && (i % indexProgressEvery) == 0)
                    {
                        progressLogger("repcut phase-b/ascs: index_sink_progress=" + std::to_string(i) +
                                       "/" + std::to_string(sinkCount) +
                                       " elapsed_ms=" + std::to_string(msSince(indexStart)));
                    }
                    continue;
                }
                const wolvrix::lib::grh::Operation op = graph.getOperation(sink.op);
                if (auto sym = getAttrString(op, "regSymbol"))
                {
                    regWriteSinks[*sym].push_back(i);
                }
                else if (auto sym = getAttrString(op, "latchSymbol"))
                {
                    latchWriteSinks[*sym].push_back(i);
                }
                else if (auto sym = getAttrString(op, "memSymbol"))
                {
                    const uint32_t symbolId = memSymbolIntern.intern(*sym);
                    memWriteSinks[symbolId].push_back(i);
                }

                if (progressLogger && i > 0 && (i % indexProgressEvery) == 0)
                {
                    progressLogger("repcut phase-b/ascs: index_sink_progress=" + std::to_string(i) +
                                   "/" + std::to_string(sinkCount) +
                                   " elapsed_ms=" + std::to_string(msSince(indexStart)));
                }
            }
            if (progressLogger)
            {
                progressLogger("repcut phase-b/ascs: index_sink_done reg_groups=" +
                               std::to_string(regWriteSinks.size()) + " latch_groups=" +
                               std::to_string(latchWriteSinks.size()) + " mem_groups=" +
                               std::to_string(memWriteSinks.size()) +
                               " elapsed_ms=" + std::to_string(msSince(indexStart)));
            }

            auto unionGroup = [&](const auto &groups) {
                for (const auto &[symbol, indices] : groups)
                {
                    (void)symbol;
                    if (indices.size() < 2)
                    {
                        continue;
                    }
                    const size_t root = indices[0];
                    for (size_t i = 1; i < indices.size(); ++i)
                    {
                        dsu.unite(root, indices[i]);
                    }
                }
            };
            const auto unionStart = std::chrono::steady_clock::now();
            unionGroup(regWriteSinks);
            unionGroup(latchWriteSinks);
            unionGroup(memWriteSinks);
            if (progressLogger)
            {
                progressLogger("repcut phase-b/ascs: union_symbol_groups_done elapsed_ms=" +
                               std::to_string(msSince(unionStart)));
            }

            const auto memConeStart = std::chrono::steady_clock::now();
            const size_t memProgressEvery = sinkCount < 20000 ? 5000 : 20000;
            MemSymbolMemo memMemo;
            memMemo.nodeToMemSymbolIds.resize(phaseA.nodeToOp.size());
            memMemo.nodeState.resize(phaseA.nodeToOp.size(), 0);
            size_t batchStartIndex = 0;
            auto batchStart = memConeStart;
            uint64_t batchVisitedNodes = 0;
            uint64_t batchMemSymbolHits = 0;
            uint64_t batchDsuUnions = 0;
            std::unordered_set<uint32_t> batchUniqueMemSymbols;
            for (size_t i = 0; i < data.sinks.size(); ++i)
            {
                MemSymbolCollectStats sinkStats;
                std::unordered_set<uint32_t> sinkMemSymbolIds;
                collectConeMemSymbolIds(graph,
                                        phaseA,
                                        data.sinks[i],
                                        inoutInputValues,
                                        memSymbolIntern,
                                        memMemo,
                                        sinkMemSymbolIds,
                                        sinkStats);
                batchVisitedNodes += sinkStats.visitedNodes;
                batchMemSymbolHits += sinkStats.memSymbolHits;
                for (const uint32_t memSymbolId : sinkMemSymbolIds)
                {
                    batchUniqueMemSymbols.insert(memSymbolId);
                    auto it = memWriteSinks.find(memSymbolId);
                    if (it == memWriteSinks.end())
                    {
                        continue;
                    }
                    for (const size_t writeSinkIndex : it->second)
                    {
                        const size_t sinkRoot = dsu.find(i);
                        const size_t writeRoot = dsu.find(writeSinkIndex);
                        if (sinkRoot != writeRoot)
                        {
                            ++batchDsuUnions;
                            dsu.unite(sinkRoot, writeRoot);
                        }
                    }
                }

                if (progressLogger && ((i + 1) % memProgressEvery) == 0)
                {
                    const size_t processed = i + 1;
                    progressLogger("repcut phase-b/ascs: collect_mem_symbols_progress=" + std::to_string(processed) +
                                   "/" + std::to_string(sinkCount) +
                                   " elapsed_ms=" + std::to_string(msSince(memConeStart)) +
                                   " batch_elapsed_ms=" + std::to_string(msSince(batchStart)) +
                                   " visited_nodes=" + std::to_string(batchVisitedNodes) +
                                   " mem_symbol_hits=" + std::to_string(batchMemSymbolHits) +
                                   " unique_mem_symbols=" + std::to_string(batchUniqueMemSymbols.size()) +
                                   " dsu_unions=" + std::to_string(batchDsuUnions));
                    batchStartIndex = processed;
                    batchStart = std::chrono::steady_clock::now();
                    batchVisitedNodes = 0;
                    batchMemSymbolHits = 0;
                    batchDsuUnions = 0;
                    batchUniqueMemSymbols.clear();
                }
            }
            if (progressLogger && batchStartIndex < sinkCount)
            {
                progressLogger("repcut phase-b/ascs: collect_mem_symbols_progress=" + std::to_string(sinkCount) +
                               "/" + std::to_string(sinkCount) +
                               " elapsed_ms=" + std::to_string(msSince(memConeStart)) +
                               " batch_elapsed_ms=" + std::to_string(msSince(batchStart)) +
                               " visited_nodes=" + std::to_string(batchVisitedNodes) +
                               " mem_symbol_hits=" + std::to_string(batchMemSymbolHits) +
                               " unique_mem_symbols=" + std::to_string(batchUniqueMemSymbols.size()) +
                               " dsu_unions=" + std::to_string(batchDsuUnions));
            }
            if (progressLogger)
            {
                progressLogger("repcut phase-b/ascs: collect_mem_symbols_done elapsed_ms=" +
                               std::to_string(msSince(memConeStart)));
            }

            std::unordered_map<size_t, AscId> ascByRoot;
            const auto rootBuildStart = std::chrono::steady_clock::now();
            for (size_t i = 0; i < data.sinks.size(); ++i)
            {
                const size_t root = dsu.find(i);
                auto [it, inserted] = ascByRoot.emplace(root, static_cast<AscId>(data.ascs.size()));
                if (inserted)
                {
                    data.ascs.emplace_back();
                }
                const AscId aid = it->second;
                data.ascs[aid].sinks.push_back(i);
                data.sinkToAsc[i] = aid;
            }
            if (progressLogger)
            {
                progressLogger("repcut phase-b/ascs: build_asc_roots_done ascs=" +
                               std::to_string(data.ascs.size()) +
                               " elapsed_ms=" + std::to_string(msSince(rootBuildStart)));
            }

            const auto collectConeStart = std::chrono::steady_clock::now();
            const size_t ascProgressEvery = data.ascs.size() < 2000 ? 200 : 1000;
            const size_t totalAscs = data.ascs.size();
            const size_t hwThreads =
                std::max<size_t>(1, static_cast<size_t>(std::thread::hardware_concurrency()));
            const size_t threadCount = std::min<size_t>(
                totalAscs,
                std::min<size_t>(kMaxConeCollectThreads, hwThreads));

            if (threadCount <= 1 || totalAscs < kConeCollectChunkSize * 2)
            {
                std::vector<uint32_t> coneVisitStamp(phaseA.nodeToOp.size(), 0);
                uint32_t coneVisitEpoch = 1;
                for (AscId aid = 0; aid < totalAscs; ++aid)
                {
                    collectAscCone(graph,
                                   phaseA,
                                   data.sinks,
                                   inoutInputValues,
                                   coneVisitStamp,
                                   coneVisitEpoch,
                                   data.ascs[aid]);
                    ++coneVisitEpoch;
                    if (coneVisitEpoch == 0)
                    {
                        std::fill(coneVisitStamp.begin(), coneVisitStamp.end(), 0);
                        coneVisitEpoch = 1;
                    }

                    if (progressLogger && aid > 0 && (aid % ascProgressEvery) == 0)
                    {
                        progressLogger("repcut phase-b/ascs: collect_cones_progress=" +
                                       std::to_string(aid) + "/" + std::to_string(totalAscs) +
                                       " elapsed_ms=" + std::to_string(msSince(collectConeStart)));
                    }
                }
            }
            else
            {
                if (progressLogger)
                {
                    progressLogger("repcut phase-b/ascs: collect_cones_parallel threads=" +
                                   std::to_string(threadCount) +
                                   " chunk=" + std::to_string(kConeCollectChunkSize));
                }

                std::atomic<size_t> nextAsc{0};
                std::atomic<size_t> processedAscs{0};
                std::vector<std::thread> workers;
                workers.reserve(threadCount);

                for (size_t t = 0; t < threadCount; ++t)
                {
                    workers.emplace_back([&, t]() {
                        (void)t;
                        std::vector<uint32_t> visitStamp(phaseA.nodeToOp.size(), 0);
                        uint32_t visitEpoch = 1;
                        while (true)
                        {
                            const size_t begin = nextAsc.fetch_add(kConeCollectChunkSize, std::memory_order_relaxed);
                            if (begin >= totalAscs)
                            {
                                break;
                            }
                            const size_t end = std::min(totalAscs, begin + kConeCollectChunkSize);
                            for (size_t aid = begin; aid < end; ++aid)
                            {
                                collectAscCone(graph,
                                               phaseA,
                                               data.sinks,
                                               inoutInputValues,
                                               visitStamp,
                                               visitEpoch,
                                               data.ascs[aid]);
                                ++visitEpoch;
                                if (visitEpoch == 0)
                                {
                                    std::fill(visitStamp.begin(), visitStamp.end(), 0);
                                    visitEpoch = 1;
                                }
                            }
                            processedAscs.fetch_add(end - begin, std::memory_order_relaxed);
                        }
                    });
                }

                if (progressLogger)
                {
                    size_t nextProgress = ascProgressEvery;
                    while (nextProgress < totalAscs)
                    {
                        const size_t done = processedAscs.load(std::memory_order_relaxed);
                        if (done >= nextProgress)
                        {
                            progressLogger("repcut phase-b/ascs: collect_cones_progress=" +
                                           std::to_string(nextProgress) + "/" + std::to_string(totalAscs) +
                                           " elapsed_ms=" + std::to_string(msSince(collectConeStart)));
                            nextProgress += ascProgressEvery;
                            continue;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }

                for (auto &worker : workers)
                {
                    worker.join();
                }
            }

            if (progressLogger)
            {
                progressLogger("repcut phase-b/ascs: collect_cones_done elapsed_ms=" +
                               std::to_string(msSince(collectConeStart)) +
                               " total_elapsed_ms=" + std::to_string(msSince(phaseStart)));
            }

            return data;
        }

        void normalizeNodeToAscs(std::vector<std::vector<AscId>> &nodeToAscs)
        {
            for (auto &ascs : nodeToAscs)
            {
                std::sort(ascs.begin(), ascs.end());
                ascs.erase(std::unique(ascs.begin(), ascs.end()), ascs.end());
            }
        }

        NodeId chooseAscSeedNode(const AscInfo &asc,
                                 const std::vector<SinkRef> &sinks,
                                 const PhaseAData &phaseA)
        {
            for (const size_t sinkIndex : asc.sinks)
            {
                const SinkRef &sink = sinks[sinkIndex];
                if (sink.kind != SinkRef::Kind::Operation)
                {
                    continue;
                }
                auto it = phaseA.opToNode.find(sink.op);
                if (it != phaseA.opToNode.end())
                {
                    return it->second;
                }
            }
            if (!asc.combOps.empty())
            {
                return *asc.combOps.begin();
            }
            return kInvalidNode;
        }

        void findPiece(NodeId seed,
                       PieceId pid,
                       const PhaseAData &phaseA,
                       const std::vector<std::vector<AscId>> &nodeToAscs,
                       std::vector<std::unordered_set<NodeId>> &pieces,
                       std::vector<PieceId> &nodeToPiece,
                       std::vector<std::vector<AscId>> &pieceToAscs)
        {
            if (seed == kInvalidNode || seed >= nodeToPiece.size() || nodeToPiece[seed] != kInvalidPiece)
            {
                return;
            }

            const std::vector<AscId> targetAscs = nodeToAscs[seed];
            std::vector<NodeId> stack;
            stack.push_back(seed);

            while (!stack.empty())
            {
                const NodeId node = stack.back();
                stack.pop_back();

                if (nodeToPiece[node] != kInvalidPiece)
                {
                    continue;
                }
                if (nodeToAscs[node] != targetAscs)
                {
                    continue;
                }

                nodeToPiece[node] = pid;
                pieces[pid].insert(node);

                for (const NodeId pred : phaseA.inNeighbors[node])
                {
                    if (nodeToPiece[pred] == kInvalidPiece && nodeToAscs[pred] == targetAscs)
                    {
                        stack.push_back(pred);
                    }
                }
                for (const NodeId succ : phaseA.outNeighbors[node])
                {
                    if (nodeToPiece[succ] == kInvalidPiece && nodeToAscs[succ] == targetAscs)
                    {
                        stack.push_back(succ);
                    }
                }
            }

            pieceToAscs[pid] = targetAscs;
        }

        void buildPieces(PhaseBData &phaseB, const PhaseAData &phaseA, const ProgressLogger &progressLogger)
        {
            const auto phaseStart = std::chrono::steady_clock::now();
            auto msSince = [&](const std::chrono::steady_clock::time_point &start) -> uint64_t {
                return static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                        .count());
            };

            phaseB.nodeToAscs.clear();
            phaseB.nodeToAscs.resize(phaseA.nodeToOp.size());

            const auto nodeToAscsStart = std::chrono::steady_clock::now();
            const size_t ascProgressEvery = phaseB.ascs.size() < 2000 ? 200 : 1000;
            for (AscId aid = 0; aid < phaseB.ascs.size(); ++aid)
            {
                const AscInfo &asc = phaseB.ascs[aid];

                for (const size_t sinkIndex : asc.sinks)
                {
                    const SinkRef &sink = phaseB.sinks[sinkIndex];
                    if (sink.kind != SinkRef::Kind::Operation)
                    {
                        continue;
                    }
                    auto it = phaseA.opToNode.find(sink.op);
                    if (it != phaseA.opToNode.end())
                    {
                        phaseB.nodeToAscs[it->second].push_back(aid);
                    }
                }
                for (const NodeId node : asc.combOps)
                {
                    phaseB.nodeToAscs[node].push_back(aid);
                }

                if (progressLogger && aid > 0 && (aid % ascProgressEvery) == 0)
                {
                    progressLogger("repcut phase-b/pieces: build_node_to_ascs_progress=" +
                                   std::to_string(aid) + "/" + std::to_string(phaseB.ascs.size()) +
                                   " elapsed_ms=" + std::to_string(msSince(nodeToAscsStart)));
                }
            }
            if (progressLogger)
            {
                progressLogger("repcut phase-b/pieces: build_node_to_ascs_done elapsed_ms=" +
                               std::to_string(msSince(nodeToAscsStart)));
            }

            const auto normalizeStart = std::chrono::steady_clock::now();
            normalizeNodeToAscs(phaseB.nodeToAscs);
            if (progressLogger)
            {
                progressLogger("repcut phase-b/pieces: normalize_node_to_ascs_done elapsed_ms=" +
                               std::to_string(msSince(normalizeStart)));
            }

            phaseB.pieces.clear();
            phaseB.pieceToAscs.clear();
            phaseB.nodeToPiece.assign(phaseA.nodeToOp.size(), kInvalidPiece);

            const auto ascPieceStart = std::chrono::steady_clock::now();
            for (AscId aid = 0; aid < phaseB.ascs.size(); ++aid)
            {
                const PieceId pid = static_cast<PieceId>(phaseB.pieces.size());
                phaseB.pieces.emplace_back();
                phaseB.pieceToAscs.emplace_back(std::vector<AscId>{aid});

                const NodeId seed = chooseAscSeedNode(phaseB.ascs[aid], phaseB.sinks, phaseA);
                findPiece(seed, pid, phaseA, phaseB.nodeToAscs, phaseB.pieces, phaseB.nodeToPiece, phaseB.pieceToAscs);

                if (progressLogger && aid > 0 && (aid % ascProgressEvery) == 0)
                {
                    progressLogger("repcut phase-b/pieces: asc_piece_progress=" +
                                   std::to_string(aid) + "/" + std::to_string(phaseB.ascs.size()) +
                                   " elapsed_ms=" + std::to_string(msSince(ascPieceStart)));
                }
            }
            if (progressLogger)
            {
                progressLogger("repcut phase-b/pieces: asc_piece_done elapsed_ms=" +
                               std::to_string(msSince(ascPieceStart)) +
                               " pieces_now=" + std::to_string(phaseB.pieces.size()));
            }

            const auto residualStart = std::chrono::steady_clock::now();
            const size_t nodeCount = phaseB.nodeToPiece.size();
            const size_t nodeProgressEvery = nodeCount < 200000 ? 50000 : 200000;
            for (NodeId node = 0; node < phaseB.nodeToPiece.size(); ++node)
            {
                if (phaseB.nodeToPiece[node] != kInvalidPiece)
                {
                    if (progressLogger && node > 0 && (node % nodeProgressEvery) == 0)
                    {
                        progressLogger("repcut phase-b/pieces: residual_scan_progress=" +
                                       std::to_string(node) + "/" + std::to_string(nodeCount) +
                                       " elapsed_ms=" + std::to_string(msSince(residualStart)));
                    }
                    continue;
                }
                const PieceId pid = static_cast<PieceId>(phaseB.pieces.size());
                phaseB.pieces.emplace_back();
                phaseB.pieceToAscs.emplace_back();
                findPiece(node, pid, phaseA, phaseB.nodeToAscs, phaseB.pieces, phaseB.nodeToPiece, phaseB.pieceToAscs);

                if (progressLogger && node > 0 && (node % nodeProgressEvery) == 0)
                {
                    progressLogger("repcut phase-b/pieces: residual_scan_progress=" +
                                   std::to_string(node) + "/" + std::to_string(nodeCount) +
                                   " elapsed_ms=" + std::to_string(msSince(residualStart)));
                }
            }

            if (progressLogger)
            {
                progressLogger("repcut phase-b/pieces: residual_scan_done elapsed_ms=" +
                               std::to_string(msSince(residualStart)) +
                               " total_elapsed_ms=" + std::to_string(msSince(phaseStart)) +
                               " pieces_total=" + std::to_string(phaseB.pieces.size()));
            }
        }

        bool validatePhaseB(const PhaseAData &phaseA,
                            const PhaseBData &phaseB,
                            std::string &errorMessage)
        {
            for (size_t sinkIndex = 0; sinkIndex < phaseB.sinks.size(); ++sinkIndex)
            {
                const AscId aid = phaseB.sinkToAsc[sinkIndex];
                if (aid >= phaseB.ascs.size())
                {
                    errorMessage = "repcut phase-b: sink->asc mapping out of range";
                    return false;
                }
            }

            for (NodeId node = 0; node < phaseA.nodeToOp.size(); ++node)
            {
                if (phaseB.nodeToPiece[node] == kInvalidPiece)
                {
                    errorMessage = "repcut phase-b: node without piece assignment";
                    return false;
                }
                if (phaseB.nodeToPiece[node] >= phaseB.pieces.size())
                {
                    errorMessage = "repcut phase-b: node->piece mapping out of range";
                    return false;
                }
            }

            for (PieceId pid = 0; pid < phaseB.pieces.size(); ++pid)
            {
                const auto &piece = phaseB.pieces[pid];
                const auto &pieceAscs = phaseB.pieceToAscs[pid];
                for (const NodeId node : piece)
                {
                    if (node >= phaseB.nodeToAscs.size())
                    {
                        errorMessage = "repcut phase-b: piece contains invalid node";
                        return false;
                    }
                    if (phaseB.nodeToAscs[node] != pieceAscs)
                    {
                        errorMessage = "repcut phase-b: pieceToAscs mismatch with nodeToAscs";
                        return false;
                    }
                }
            }

            return true;
        }

        int32_t safeValueWidth(const wolvrix::lib::grh::Graph &graph, wolvrix::lib::grh::ValueId value)
        {
            if (!value.valid())
            {
                return 1;
            }
            const int32_t width = graph.valueWidth(value);
            return width > 0 ? width : 1;
        }

        int32_t getResultWidth(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op)
        {
            if (op.results().empty())
            {
                return 1;
            }
            int32_t maxWidth = 1;
            for (const auto value : op.results())
            {
                maxWidth = std::max(maxWidth, safeValueWidth(graph, value));
            }
            return maxWidth;
        }

        int32_t getMaxOperandWidth(const wolvrix::lib::grh::Graph &graph, const wolvrix::lib::grh::Operation &op)
        {
            int32_t maxWidth = 1;
            for (const auto operand : op.operands())
            {
                maxWidth = std::max(maxWidth, safeValueWidth(graph, operand));
            }
            return maxWidth;
        }

        int32_t getOperandWidth(const wolvrix::lib::grh::Graph &graph,
                                const wolvrix::lib::grh::Operation &op,
                                size_t operandIndex)
        {
            if (operandIndex >= op.operands().size())
            {
                return 1;
            }
            return safeValueWidth(graph, op.operands()[operandIndex]);
        }

        uint32_t calculateNodeWeight(const wolvrix::lib::grh::Graph &graph,
                                     const PhaseAData &phaseA,
                                     NodeId node,
                                     std::vector<uint32_t> &nodeWeights)
        {
            if (node >= phaseA.nodeToOp.size())
            {
                return 1;
            }
            if (nodeWeights[node] != std::numeric_limits<uint32_t>::max())
            {
                return nodeWeights[node];
            }

            const wolvrix::lib::grh::Operation op = graph.getOperation(phaseA.nodeToOp[node]);
            uint32_t weight = 1;
            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
                weight = 1;
                break;
            case wolvrix::lib::grh::OperationKind::kAdd:
            case wolvrix::lib::grh::OperationKind::kSub:
                weight = 2;
                break;
            case wolvrix::lib::grh::OperationKind::kMul:
                weight = 4;
                break;
            case wolvrix::lib::grh::OperationKind::kDiv:
            case wolvrix::lib::grh::OperationKind::kMod:
                weight = 6;
                break;
            case wolvrix::lib::grh::OperationKind::kEq:
            case wolvrix::lib::grh::OperationKind::kNe:
            case wolvrix::lib::grh::OperationKind::kCaseEq:
            case wolvrix::lib::grh::OperationKind::kCaseNe:
            case wolvrix::lib::grh::OperationKind::kWildcardEq:
            case wolvrix::lib::grh::OperationKind::kWildcardNe:
            case wolvrix::lib::grh::OperationKind::kLt:
            case wolvrix::lib::grh::OperationKind::kLe:
            case wolvrix::lib::grh::OperationKind::kGt:
            case wolvrix::lib::grh::OperationKind::kGe:
                weight = 2;
                break;
            case wolvrix::lib::grh::OperationKind::kShl:
            case wolvrix::lib::grh::OperationKind::kLShr:
            case wolvrix::lib::grh::OperationKind::kAShr:
                weight = 2;
                break;
            case wolvrix::lib::grh::OperationKind::kAnd:
            case wolvrix::lib::grh::OperationKind::kOr:
            case wolvrix::lib::grh::OperationKind::kXor:
            case wolvrix::lib::grh::OperationKind::kXnor:
            case wolvrix::lib::grh::OperationKind::kNot:
            case wolvrix::lib::grh::OperationKind::kLogicAnd:
            case wolvrix::lib::grh::OperationKind::kLogicOr:
            case wolvrix::lib::grh::OperationKind::kLogicNot:
            case wolvrix::lib::grh::OperationKind::kReduceAnd:
            case wolvrix::lib::grh::OperationKind::kReduceOr:
            case wolvrix::lib::grh::OperationKind::kReduceXor:
            case wolvrix::lib::grh::OperationKind::kReduceNor:
            case wolvrix::lib::grh::OperationKind::kReduceNand:
            case wolvrix::lib::grh::OperationKind::kReduceXnor:
                weight = (static_cast<uint32_t>(getResultWidth(graph, op)) + 63u) / 64u;
                weight = std::max(weight, 1u);
                break;
            case wolvrix::lib::grh::OperationKind::kMux:
            {
                const uint32_t nWords = (static_cast<uint32_t>(getResultWidth(graph, op)) + 63u) / 64u;
                weight = std::max(1u, nWords * 6u);
                break;
            }
            case wolvrix::lib::grh::OperationKind::kConcat:
                weight = std::max<uint32_t>(1u, static_cast<uint32_t>(op.operands().size()));
                break;
            case wolvrix::lib::grh::OperationKind::kReplicate:
                weight = 2;
                break;
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
            case wolvrix::lib::grh::OperationKind::kSliceArray:
                weight = 1;
                break;
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                weight = 4;
                break;
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            {
                const uint32_t nWords = (static_cast<uint32_t>(getOperandWidth(graph, op, 1)) + 63u) / 64u;
                weight = std::max(2u, nWords + 1u);
                break;
            }
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
            {
                const uint32_t wrEnWords = (static_cast<uint32_t>(getOperandWidth(graph, op, 0)) + 63u) / 64u;
                const uint32_t wrDataWords = (static_cast<uint32_t>(getOperandWidth(graph, op, 1)) + 63u) / 64u;
                weight = 1u + std::max(1u, wrEnWords) + std::max(1u, wrDataWords);
                break;
            }
            default:
            {
                if (isCombOp(op))
                {
                    const uint32_t widthScaled = (static_cast<uint32_t>(getMaxOperandWidth(graph, op)) + 63u) / 64u;
                    weight = std::max(1u, widthScaled);
                }
                else
                {
                    weight = 1;
                }
                break;
            }
            }

            nodeWeights[node] = std::max(1u, weight);
            return nodeWeights[node];
        }

        uint32_t calculatePieceWeight(const wolvrix::lib::grh::Graph &graph,
                                      const PhaseAData &phaseA,
                                      const PhaseBData &phaseB,
                                      PieceId pid,
                                      std::vector<uint32_t> &nodeWeights,
                                      std::vector<uint32_t> &pieceWeights)
        {
            if (pid >= phaseB.pieces.size())
            {
                return 1;
            }
            if (pieceWeights[pid] != std::numeric_limits<uint32_t>::max())
            {
                return pieceWeights[pid];
            }

            const auto &piece = phaseB.pieces[pid];
            if (piece.empty())
            {
                pieceWeights[pid] = 1;
                return pieceWeights[pid];
            }

            std::vector<NodeId> pieceSinks;
            pieceSinks.reserve(piece.size());
            for (const NodeId node : piece)
            {
                bool hasOutEdgeInPiece = false;
                for (const NodeId succ : phaseA.outNeighbors[node])
                {
                    if (piece.find(succ) != piece.end())
                    {
                        hasOutEdgeInPiece = true;
                        break;
                    }
                }
                if (!hasOutEdgeInPiece)
                {
                    pieceSinks.push_back(node);
                }
            }
            if (pieceSinks.empty())
            {
                for (const NodeId node : piece)
                {
                    pieceSinks.push_back(node);
                }
            }

            std::unordered_set<NodeId> visited;
            std::function<uint32_t(NodeId)> stmtWeight = [&](NodeId node) -> uint32_t {
                if (!visited.insert(node).second)
                {
                    return 0;
                }
                uint32_t w = calculateNodeWeight(graph, phaseA, node, nodeWeights);
                for (const NodeId pred : phaseA.inNeighbors[node])
                {
                    if (piece.find(pred) != piece.end())
                    {
                        w += stmtWeight(pred);
                    }
                }
                return w;
            };

            uint32_t totalWeight = 0;
            for (const NodeId sink : pieceSinks)
            {
                totalWeight += stmtWeight(sink);
            }

            pieceWeights[pid] = std::max(1u, totalWeight);
            return pieceWeights[pid];
        }

        HyperGraph buildHyperGraph(const wolvrix::lib::grh::Graph &graph,
                                   const PhaseAData &phaseA,
                                   const PhaseBData &phaseB,
                                   std::vector<uint32_t> &nodeWeights,
                                   std::vector<uint32_t> &pieceWeights)
        {
            HyperGraph hg;

            nodeWeights.assign(phaseA.nodeToOp.size(), std::numeric_limits<uint32_t>::max());
            pieceWeights.assign(phaseB.pieces.size(), std::numeric_limits<uint32_t>::max());

            for (PieceId pid = 0; pid < phaseB.pieces.size(); ++pid)
            {
                (void)calculatePieceWeight(graph, phaseA, phaseB, pid, nodeWeights, pieceWeights);
            }

            std::vector<uint32_t> piecePinCount(phaseB.pieces.size(), 0);
            for (PieceId pid = 0; pid < phaseB.pieces.size(); ++pid)
            {
                piecePinCount[pid] = static_cast<uint32_t>(phaseB.pieceToAscs[pid].size());
            }

            hg.nodeWeights.reserve(phaseB.ascs.size());
            for (AscId aid = 0; aid < phaseB.ascs.size(); ++aid)
            {
                uint32_t weight = (aid < pieceWeights.size()) ? pieceWeights[aid] : 1u;

                std::unordered_set<PieceId> connectPieces;
                for (const size_t sinkIndex : phaseB.ascs[aid].sinks)
                {
                    const SinkRef &sink = phaseB.sinks[sinkIndex];
                    if (sink.kind != SinkRef::Kind::Operation)
                    {
                        continue;
                    }
                    auto it = phaseA.opToNode.find(sink.op);
                    if (it != phaseA.opToNode.end())
                    {
                        const PieceId pid = phaseB.nodeToPiece[it->second];
                        if (pid != kInvalidPiece)
                        {
                            connectPieces.insert(pid);
                        }
                    }
                }
                for (const NodeId node : phaseB.ascs[aid].combOps)
                {
                    const PieceId pid = phaseB.nodeToPiece[node];
                    if (pid != kInvalidPiece)
                    {
                        connectPieces.insert(pid);
                    }
                }
                connectPieces.erase(aid);

                uint32_t sharedWeight = 0;
                for (const PieceId pid : connectPieces)
                {
                    const uint32_t pinCount = piecePinCount[pid];
                    if (pinCount == 0)
                    {
                        continue;
                    }
                    sharedWeight += pieceWeights[pid] / pinCount;
                }

                hg.nodeWeights.push_back(std::max(1u, weight + sharedWeight));
            }

            for (PieceId pid = static_cast<PieceId>(phaseB.ascs.size()); pid < phaseB.pieces.size(); ++pid)
            {
                HyperGraph::HyperEdge edge;
                edge.weight = std::max(1u, pieceWeights[pid]);
                edge.nodes = phaseB.pieceToAscs[pid];
                if (!edge.nodes.empty())
                {
                    hg.edges.push_back(std::move(edge));
                }
            }

            return hg;
        }

        bool validateHyperGraph(const PhaseBData &phaseB,
                                const HyperGraph &hg,
                                std::string &errorMessage)
        {
            if (hg.nodeWeights.size() != phaseB.ascs.size())
            {
                errorMessage = "repcut phase-c: hypergraph node count must match asc count";
                return false;
            }

            for (const uint32_t weight : hg.nodeWeights)
            {
                if (weight == 0)
                {
                    errorMessage = "repcut phase-c: hypergraph node weight must be >= 1";
                    return false;
                }
            }

            for (const auto &edge : hg.edges)
            {
                if (edge.weight == 0)
                {
                    errorMessage = "repcut phase-c: hyperedge weight must be >= 1";
                    return false;
                }
                if (edge.nodes.empty())
                {
                    errorMessage = "repcut phase-c: hyperedge must connect at least one asc";
                    return false;
                }
                for (const AscId aid : edge.nodes)
                {
                    if (aid >= phaseB.ascs.size())
                    {
                        errorMessage = "repcut phase-c: hyperedge references invalid asc id";
                        return false;
                    }
                }
            }

            return true;
        }

        std::string toFixedString(double value, int precision = 6)
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(precision);
            oss << value;
            return oss.str();
        }

        bool writeTextFile(const std::filesystem::path &path,
                           const std::string &content,
                           std::string &errorMessage)
        {
            std::ofstream out(path);
            if (!out)
            {
                errorMessage = "cannot open file for writing: " + path.string();
                return false;
            }
            out << content;
            out.flush();
            if (!out.good())
            {
                errorMessage = "failed writing file: " + path.string();
                return false;
            }
            return true;
        }

        bool writeHyperGraphToHmetis(const HyperGraph &hg,
                                     const std::filesystem::path &path,
                                     std::string &errorMessage)
        {
            std::ofstream out(path);
            if (!out)
            {
                errorMessage = "cannot open hMETIS file for writing: " + path.string();
                return false;
            }

            out << hg.edges.size() << " " << hg.nodeWeights.size() << " 11\n";
            for (const auto &edge : hg.edges)
            {
                out << edge.weight;
                for (const AscId aid : edge.nodes)
                {
                    out << " " << (aid + 1);
                }
                out << "\n";
            }
            for (const uint32_t weight : hg.nodeWeights)
            {
                out << weight << "\n";
            }
            out.flush();
            if (!out.good())
            {
                errorMessage = "failed writing hMETIS file: " + path.string();
                return false;
            }
            return true;
        }

        std::string escapeJson(std::string_view value)
        {
            std::string out;
            out.reserve(value.size() + 8);
            for (const char ch : value)
            {
                switch (ch)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out.push_back(ch);
                    break;
                }
            }
            return out;
        }

        std::string buildKaHyParConfig()
        {
            std::ostringstream cfg;
            cfg << "# Auto-generated by repcut pass\n";
            cfg << "# RepCut/Essent-style preset to avoid UNDEFINED defaults\n";
            cfg << "mode=direct\n";
            cfg << "objective=km1\n";
            cfg << "seed=-1\n";
            cfg << "cmaxnet=1000\n";
            cfg << "vcycles=0\n";
            cfg << "write_partition_file=true\n";

            cfg << "p-use-sparsifier=true\n";
            cfg << "p-sparsifier-min-median-he-size=28\n";
            cfg << "p-sparsifier-max-hyperedge-size=1200\n";
            cfg << "p-sparsifier-max-cluster-size=10\n";
            cfg << "p-sparsifier-min-cluster-size=2\n";
            cfg << "p-sparsifier-num-hash-func=5\n";
            cfg << "p-sparsifier-combined-num-hash-func=100\n";
            cfg << "p-detect-communities=true\n";
            cfg << "p-detect-communities-in-ip=true\n";
            cfg << "p-reuse-communities=false\n";
            cfg << "p-max-louvain-pass-iterations=100\n";
            cfg << "p-min-eps-improvement=0.0001\n";
            cfg << "p-louvain-edge-weight=hybrid\n";

            cfg << "c-type=ml_style\n";
            cfg << "c-s=1\n";
            cfg << "c-t=160\n";
            cfg << "c-rating-score=heavy_edge\n";
            cfg << "c-rating-use-communities=true\n";
            cfg << "c-rating-heavy_node_penalty=no_penalty\n";
            cfg << "c-rating-acceptance-criterion=best_prefer_unmatched\n";
            cfg << "c-fixed-vertex-acceptance-criterion=fixed_vertex_allowed\n";

            cfg << "i-mode=recursive\n";
            cfg << "i-technique=multi\n";
            cfg << "i-c-type=ml_style\n";
            cfg << "i-c-s=1\n";
            cfg << "i-c-t=150\n";
            cfg << "i-c-rating-score=heavy_edge\n";
            cfg << "i-c-rating-use-communities=true\n";
            cfg << "i-c-rating-heavy_node_penalty=no_penalty\n";
            cfg << "i-c-rating-acceptance-criterion=best_prefer_unmatched\n";
            cfg << "i-c-fixed-vertex-acceptance-criterion=fixed_vertex_allowed\n";
            cfg << "i-algo=pool\n";
            cfg << "i-runs=20\n";
            cfg << "i-bp-algorithm=worst_fit\n";
            cfg << "i-bp-heuristic-prepacking=false\n";
            cfg << "i-bp-early-restart=true\n";
            cfg << "i-bp-late-restart=true\n";
            cfg << "i-r-type=twoway_fm\n";
            cfg << "i-r-runs=-1\n";
            cfg << "i-r-fm-stop=simple\n";
            cfg << "i-r-fm-stop-i=50\n";

            cfg << "r-type=kway_fm_hyperflow_cutter_km1\n";
            cfg << "r-runs=-1\n";
            cfg << "r-fm-stop=adaptive_opt\n";
            cfg << "r-fm-stop-alpha=1\n";
            cfg << "r-fm-stop-i=350\n";
            cfg << "r-flow-execution-policy=exponential\n";
            cfg << "r-hfc-size-constraint=mf-style\n";
            cfg << "r-hfc-scaling=16\n";
            cfg << "r-hfc-distance-based-piercing=true\n";
            cfg << "r-hfc-mbc=true\n";
            return cfg.str();
        }

        bool isExecutableAvailable(const std::string &exe)
        {
            if (exe.empty())
            {
                return false;
            }
            if (exe.find('/') != std::string::npos)
            {
                std::error_code ec;
                return std::filesystem::exists(std::filesystem::path(exe), ec);
            }

            const std::string cmd = "command -v " + exe + " >/dev/null 2>&1";
            return std::system(cmd.c_str()) == 0;
        }

        std::string shellQuote(const std::string &value)
        {
            std::string out;
            out.reserve(value.size() + 2);
            out.push_back('\'');
            for (const char ch : value)
            {
                if (ch == '\'')
                {
                    out += "'\\''";
                }
                else
                {
                    out.push_back(ch);
                }
            }
            out.push_back('\'');
            return out;
        }

        int runKaHyParCommand(const std::string &exe,
                              const std::filesystem::path &hmetisFile,
                              const std::filesystem::path &configFile,
                              std::size_t partitionCount,
                              double imbalanceFactor,
                              const std::filesystem::path &logFile,
                              std::string &commandLine)
        {
            std::ostringstream cmd;
            cmd << shellQuote(exe)
                << " -h " << shellQuote(hmetisFile.string())
                << " -k " << partitionCount
                << " -e " << toFixedString(imbalanceFactor, 6)
                << " -p " << shellQuote(configFile.string())
                << " --seed -1"
                << " -w true"
                << " --mode direct"
                << " --objective km1"
                << " > " << shellQuote(logFile.string())
                << " 2>&1";
            commandLine = cmd.str();
            return std::system(commandLine.c_str());
        }

        std::string decodeSystemStatus(int status)
        {
            if (status == -1)
            {
                return "system() failed to launch process";
            }
#if defined(__unix__) || defined(__APPLE__)
            if (WIFEXITED(status))
            {
                return "exit_code=" + std::to_string(WEXITSTATUS(status));
            }
            if (WIFSIGNALED(status))
            {
                return "signal=" + std::to_string(WTERMSIG(status));
            }
#endif
            return "raw_status=" + std::to_string(status);
        }

        std::string readFileTail(const std::filesystem::path &path, std::size_t maxLines)
        {
            std::ifstream in(path);
            if (!in)
            {
                return {};
            }
            std::vector<std::string> lines;
            lines.reserve(maxLines + 1);
            std::string line;
            while (std::getline(in, line))
            {
                lines.push_back(line);
                if (lines.size() > maxLines)
                {
                    lines.erase(lines.begin());
                }
            }
            std::ostringstream out;
            for (const auto &l : lines)
            {
                out << l << "\n";
            }
            return out.str();
        }

        void cleanupStalePartitionOutputs(const std::filesystem::path &hmetisFile)
        {
            const std::filesystem::path parent = hmetisFile.parent_path();
            const std::string prefix = hmetisFile.filename().string() + ".part";
            std::error_code ec;
            if (!std::filesystem::exists(parent, ec) || ec)
            {
                return;
            }
            for (const auto &entry : std::filesystem::directory_iterator(parent, ec))
            {
                if (ec || !entry.is_regular_file())
                {
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (name.rfind(prefix, 0) == 0)
                {
                    std::filesystem::remove(entry.path(), ec);
                    ec.clear();
                }
            }
        }

        std::string summarizePartitionCandidates(const std::filesystem::path &hmetisFile)
        {
            const std::filesystem::path parent = hmetisFile.parent_path();
            const std::string prefix = hmetisFile.filename().string() + ".part";
            std::error_code ec;
            if (!std::filesystem::exists(parent, ec) || ec)
            {
                return "<none>";
            }
            std::vector<std::string> names;
            for (const auto &entry : std::filesystem::directory_iterator(parent, ec))
            {
                if (ec || !entry.is_regular_file())
                {
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (name.rfind(prefix, 0) == 0)
                {
                    names.push_back(name);
                }
            }
            if (names.empty())
            {
                return "<none>";
            }
            std::sort(names.begin(), names.end());
            std::ostringstream out;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i)
                {
                    out << ", ";
                }
                out << names[i];
            }
            return out.str();
        }

        std::filesystem::path findPartitionResultPath(const std::filesystem::path &hmetisFile,
                                                      std::size_t partitionCount,
                                                      double imbalanceFactor,
                                                      int seed)
        {
            const std::filesystem::path byK =
                std::filesystem::path(hmetisFile.string() + ".part" + std::to_string(partitionCount));
            std::error_code ec;
            if (std::filesystem::exists(byK, ec))
            {
                return byK;
            }

            const std::filesystem::path generic = std::filesystem::path(hmetisFile.string() + ".part");
            if (std::filesystem::exists(generic, ec))
            {
                return generic;
            }

            std::ostringstream exactSuffix;
            exactSuffix << ".part" << partitionCount
                        << ".epsilon" << toFixedString(imbalanceFactor, 6)
                        << ".seed" << seed
                        << ".KaHyPar";
            const std::filesystem::path exact = std::filesystem::path(hmetisFile.string() + exactSuffix.str());
            if (std::filesystem::exists(exact, ec))
            {
                return exact;
            }

            const std::filesystem::path parent = hmetisFile.parent_path();
            const std::string prefix = hmetisFile.filename().string() + ".part";
            std::vector<std::filesystem::path> candidates;
            if (std::filesystem::exists(parent, ec) && !ec)
            {
                for (const auto &entry : std::filesystem::directory_iterator(parent, ec))
                {
                    if (ec || !entry.is_regular_file())
                    {
                        continue;
                    }
                    const std::string name = entry.path().filename().string();
                    if (name.rfind(prefix, 0) == 0)
                    {
                        candidates.push_back(entry.path());
                    }
                }
            }

            if (!candidates.empty())
            {
                std::stable_sort(candidates.begin(), candidates.end(),
                                 [&](const auto &lhs, const auto &rhs) {
                                     const std::string l = lhs.filename().string();
                                     const std::string r = rhs.filename().string();
                                     const bool lByK = l == byK.filename().string();
                                     const bool rByK = r == byK.filename().string();
                                     if (lByK != rByK)
                                     {
                                         return lByK;
                                     }
                                     const bool lKahypar = l.find(".KaHyPar") != std::string::npos;
                                     const bool rKahypar = r.find(".KaHyPar") != std::string::npos;
                                     if (lKahypar != rKahypar)
                                     {
                                         return lKahypar;
                                     }
                                     return l.size() > r.size();
                                 });
                return candidates.front();
            }

            return byK;
        }

        std::vector<uint32_t> parsePartitionResult(const std::filesystem::path &resultFile,
                                                   std::size_t ascCount,
                                                   std::size_t maxPartCount,
                                                   bool &complete,
                                                   std::string &warningMessage)
        {
            std::vector<uint32_t> partition(ascCount, 0);
            complete = true;
            warningMessage.clear();

            std::ifstream in(resultFile);
            if (!in)
            {
                complete = false;
                warningMessage = "cannot open partition result: " + resultFile.string();
                return partition;
            }

            std::size_t index = 0;
            std::size_t invalidTokenCount = 0;
            std::size_t negativeCount = 0;
            std::size_t clampedToMaxCount = 0;
            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty())
                {
                    continue;
                }

                std::istringstream ls(line);
                std::string token;
                while (ls >> token)
                {
                    if (!token.empty() && token[0] == '#')
                    {
                        break;
                    }

                    std::size_t parsedChars = 0;
                    int64_t parsed = 0;
                    try
                    {
                        parsed = std::stoll(token, &parsedChars);
                    }
                    catch (const std::exception &)
                    {
                        complete = false;
                        ++invalidTokenCount;
                        continue;
                    }
                    if (parsedChars != token.size())
                    {
                        complete = false;
                        ++invalidTokenCount;
                        continue;
                    }

                    if (parsed < 0)
                    {
                        complete = false;
                        ++negativeCount;
                        parsed = 0;
                    }

                    if (maxPartCount > 0 && parsed >= static_cast<int64_t>(maxPartCount))
                    {
                        complete = false;
                        ++clampedToMaxCount;
                        parsed = static_cast<int64_t>(maxPartCount - 1);
                    }

                    if (index < ascCount)
                    {
                        partition[index] = static_cast<uint32_t>(parsed);
                    }
                    ++index;
                }
            }

            if (index < ascCount)
            {
                complete = false;
            }

            std::ostringstream warn;
            if (index < ascCount)
            {
                warn << "fewer entries than ASC count (" << index << " < " << ascCount
                     << "), missing entries default to 0";
            }
            else if (index > ascCount)
            {
                if (warn.tellp() > 0)
                {
                    warn << "; ";
                }
                warn << "extra entries in partition file (" << index << " > " << ascCount
                     << "), extras ignored";
            }
            if (invalidTokenCount > 0)
            {
                if (warn.tellp() > 0)
                {
                    warn << "; ";
                }
                warn << "invalid tokens=" << invalidTokenCount;
            }
            if (negativeCount > 0)
            {
                if (warn.tellp() > 0)
                {
                    warn << "; ";
                }
                warn << "negative ids clamped=" << negativeCount;
            }
            if (clampedToMaxCount > 0)
            {
                if (warn.tellp() > 0)
                {
                    warn << "; ";
                }
                warn << "ids >= max-part-count clamped=" << clampedToMaxCount;
            }
            warningMessage = warn.str();
            if (warningMessage.empty())
            {
                warningMessage.clear();
            }
            return partition;
        }

        void collectStorageInfos(const wolvrix::lib::grh::Graph &graph,
                                 std::unordered_map<std::string, StorageInfo> &regInfos,
                                 std::unordered_map<std::string, StorageInfo> &latchInfos,
                                 std::unordered_map<std::string, StorageInfo> &memInfos)
        {
            for (const auto opId : graph.operations())
            {
                const wolvrix::lib::grh::Operation op = graph.getOperation(opId);
                switch (op.kind())
                {
                case wolvrix::lib::grh::OperationKind::kRegister:
                {
                    const std::string symbol(op.symbolText());
                    if (!symbol.empty())
                    {
                        regInfos[symbol].declOp = opId;
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
                    if (auto sym = getAttrString(op, "regSymbol"))
                    {
                        regInfos[*sym].readPorts.push_back(opId);
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
                    if (auto sym = getAttrString(op, "regSymbol"))
                    {
                        regInfos[*sym].writePorts.push_back(opId);
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kLatch:
                {
                    const std::string symbol(op.symbolText());
                    if (!symbol.empty())
                    {
                        latchInfos[symbol].declOp = opId;
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kLatchReadPort:
                    if (auto sym = getAttrString(op, "latchSymbol"))
                    {
                        latchInfos[*sym].readPorts.push_back(opId);
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kLatchWritePort:
                    if (auto sym = getAttrString(op, "latchSymbol"))
                    {
                        latchInfos[*sym].writePorts.push_back(opId);
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kMemory:
                {
                    const std::string symbol(op.symbolText());
                    if (!symbol.empty())
                    {
                        memInfos[symbol].declOp = opId;
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                    if (auto sym = getAttrString(op, "memSymbol"))
                    {
                        memInfos[*sym].readPorts.push_back(opId);
                    }
                    break;
                case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
                    if (auto sym = getAttrString(op, "memSymbol"))
                    {
                        memInfos[*sym].writePorts.push_back(opId);
                    }
                    break;
                default:
                    break;
                }
            }
        }

        void assignStorageOpsToPartition(
            const std::unordered_map<std::string, StorageInfo> &infos,
            const std::unordered_map<std::string, uint32_t> &partitionBySymbol,
            std::unordered_map<wolvrix::lib::grh::OperationId, uint32_t, wolvrix::lib::grh::OperationIdHash> &opPartition)
        {
            for (const auto &[symbol, partId] : partitionBySymbol)
            {
                auto it = infos.find(symbol);
                if (it == infos.end())
                {
                    continue;
                }
                const StorageInfo &info = it->second;
                if (info.declOp.valid())
                {
                    opPartition[info.declOp] = partId;
                }
                for (const auto opId : info.readPorts)
                {
                    opPartition[opId] = partId;
                }
                for (const auto opId : info.writePorts)
                {
                    opPartition[opId] = partId;
                }
            }
        }

    } // namespace

    RepcutPass::RepcutPass()
        : Pass("repcut", "repcut", "Partition a single graph via RepCut hypergraph partitioning"),
          options_({})
    {
    }

    RepcutPass::RepcutPass(RepcutOptions options)
        : Pass("repcut", "repcut", "Partition a single graph via RepCut hypergraph partitioning"),
          options_(std::move(options))
    {
    }

    PassResult RepcutPass::run()
    {
        PassResult result;
        const auto totalStart = std::chrono::steady_clock::now();
        const auto msSince = [&](const std::chrono::steady_clock::time_point &start) -> uint64_t {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                    .count());
        };

        if (options_.targetGraphSymbol.empty())
        {
            error("repcut requires -target-graph to select a graph symbol");
            result.failed = true;
            return result;
        }
        if (options_.partitionCount < 2)
        {
            error("repcut partition_count must be >= 2");
            result.failed = true;
            return result;
        }
        if (options_.imbalanceFactor < 0.0)
        {
            error("repcut imbalance_factor must be >= 0");
            result.failed = true;
            return result;
        }

        wolvrix::lib::grh::Graph *graph = design().findGraph(options_.targetGraphSymbol);
        if (!graph)
        {
            error("repcut target graph not found: " + options_.targetGraphSymbol);
            result.failed = true;
            return result;
        }

        {
            std::ostringstream boot;
            boot << "repcut start: graph=" << graph->symbol()
                 << " partition_count=" << options_.partitionCount
                 << " imbalance_factor=" << toFixedString(options_.imbalanceFactor, 6)
                 << " work_dir=" << (options_.workDir.empty() ? std::string(".") : options_.workDir)
                 << " kahypar_path=" << options_.kaHyParPath
                 << " keep_intermediate=" << (options_.keepIntermediateFiles ? "true" : "false");
            logInfo(boot.str());
        }

        for (const auto opId : graph->operations())
        {
            const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
            if (op.kind() == wolvrix::lib::grh::OperationKind::kSystemTask ||
                op.kind() == wolvrix::lib::grh::OperationKind::kDpicCall)
            {
                error(*graph, op, "strip-debug should remove system tasks/dpi calls before repcut");
                result.failed = true;
            }
        }
        if (result.failed)
        {
            return result;
        }

        const auto phaseAStart = std::chrono::steady_clock::now();
        const PhaseAData data = buildPhaseAData(*graph);

        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> inoutInputValues;
        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> inoutOutputValues;
        for (const auto &port : graph->inoutPorts())
        {
            inoutInputValues.insert(port.in);
            inoutOutputValues.insert(port.out);
            inoutOutputValues.insert(port.oe);
        }

        std::size_t sinkOpCount = 0;
        std::size_t sinkOutputValueCount = 0;
        std::size_t sourceValueCount = 0;
        std::size_t combOpCount = 0;

        for (const auto opId : graph->operations())
        {
            const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
            if (isSinkOpKind(op.kind()))
            {
                ++sinkOpCount;
            }
            if (isCombOp(op))
            {
                ++combOpCount;
            }
        }
        for (const auto valueId : graph->values())
        {
            if (graph->valueIsOutput(valueId) || inoutOutputValues.find(valueId) != inoutOutputValues.end())
            {
                ++sinkOutputValueCount;
            }
            if (isSourceValue(*graph, valueId, inoutInputValues))
            {
                ++sourceValueCount;
            }
        }

        std::size_t edgeCount = 0;
        std::size_t maxInDegree = 0;
        std::size_t maxOutDegree = 0;
        for (NodeId node = 0; node < data.nodeToOp.size(); ++node)
        {
            edgeCount += data.inNeighbors[node].size();
            maxInDegree = std::max(maxInDegree, data.inNeighbors[node].size());
            maxOutDegree = std::max(maxOutDegree, data.outNeighbors[node].size());
        }

        std::ostringstream summary;
        summary << "repcut phase-a: graph=" << graph->symbol()
                << " nodes=" << data.nodeToOp.size()
                << " edges=" << edgeCount
                << " sink_ops=" << sinkOpCount
                << " sink_values=" << sinkOutputValueCount
                << " source_values=" << sourceValueCount
                << " comb_ops=" << combOpCount
                << " max_in_degree=" << maxInDegree
                << " max_out_degree=" << maxOutDegree;
            const uint64_t phaseAMs = msSince(phaseAStart);
            summary << " elapsed_ms=" << phaseAMs;
            logInfo(summary.str());

            const auto phaseBStart = std::chrono::steady_clock::now();
            logInfo("repcut phase-b: begin build_ascs");
            const auto buildAscsStart = std::chrono::steady_clock::now();
            PhaseBData phaseB = buildAscs(
                *graph,
                data,
                inoutInputValues,
                [&](const std::string &message) {
                    logInfo(message);
                });
            const uint64_t buildAscsMs = msSince(buildAscsStart);
            logInfo("repcut phase-b: build_ascs done elapsed_ms=" + std::to_string(buildAscsMs));

            logInfo("repcut phase-b: begin build_pieces");
            const auto buildPiecesStart = std::chrono::steady_clock::now();
            buildPieces(
                phaseB,
                data,
                [&](const std::string &message) {
                    logInfo(message);
                });
            const uint64_t buildPiecesMs = msSince(buildPiecesStart);
            logInfo("repcut phase-b: build_pieces done elapsed_ms=" + std::to_string(buildPiecesMs));

        std::string phaseBError;
        if (!validatePhaseB(data, phaseB, phaseBError))
        {
            error(phaseBError);
            result.failed = true;
            return result;
        }

        std::size_t opSinkCountInAsc = 0;
        std::size_t valueSinkCountInAsc = 0;
        std::size_t totalCombOpsInAsc = 0;
        std::size_t maxAscSinkCount = 0;
        std::size_t maxAscCombOpCount = 0;
        for (const auto &asc : phaseB.ascs)
        {
            maxAscSinkCount = std::max(maxAscSinkCount, asc.sinks.size());
            maxAscCombOpCount = std::max(maxAscCombOpCount, asc.combOps.size());
            totalCombOpsInAsc += asc.combOps.size();
            for (const size_t sinkIndex : asc.sinks)
            {
                if (phaseB.sinks[sinkIndex].kind == SinkRef::Kind::Operation)
                {
                    ++opSinkCountInAsc;
                }
                else
                {
                    ++valueSinkCountInAsc;
                }
            }
        }

        std::size_t nonAscPieceCount = 0;
        std::size_t maxPieceSize = 0;
        std::size_t emptyPieceCount = 0;
        for (PieceId pid = 0; pid < phaseB.pieces.size(); ++pid)
        {
            const auto &piece = phaseB.pieces[pid];
            if (piece.empty())
            {
                ++emptyPieceCount;
            }
            maxPieceSize = std::max(maxPieceSize, piece.size());
            if (pid >= phaseB.ascs.size())
            {
                ++nonAscPieceCount;
            }
        }

        std::ostringstream phaseBSummary;
        phaseBSummary << "repcut phase-b: graph=" << graph->symbol()
                      << " sinks=" << phaseB.sinks.size()
                      << " sink_ops=" << opSinkCountInAsc
                      << " sink_values=" << valueSinkCountInAsc
                      << " ascs=" << phaseB.ascs.size()
                      << " total_asc_comb_ops=" << totalCombOpsInAsc
                      << " max_asc_sinks=" << maxAscSinkCount
                      << " max_asc_comb_ops=" << maxAscCombOpCount
                      << " pieces=" << phaseB.pieces.size()
                      << " non_asc_pieces=" << nonAscPieceCount
                      << " empty_pieces=" << emptyPieceCount
                          << " max_piece_size=" << maxPieceSize;
                const uint64_t phaseBMs = msSince(phaseBStart);
                phaseBSummary << " build_ascs_ms=" << buildAscsMs
                          << " build_pieces_ms=" << buildPiecesMs
                          << " elapsed_ms=" << phaseBMs;
                logInfo(phaseBSummary.str());

                const auto phaseCStart = std::chrono::steady_clock::now();
        std::vector<uint32_t> nodeWeights;
        std::vector<uint32_t> pieceWeights;
                const auto hyperBuildStart = std::chrono::steady_clock::now();
        const HyperGraph hg = buildHyperGraph(*graph, data, phaseB, nodeWeights, pieceWeights);
                const uint64_t hyperBuildMs = msSince(hyperBuildStart);

        std::string phaseCError;
        if (!validateHyperGraph(phaseB, hg, phaseCError))
        {
            error(phaseCError);
            result.failed = true;
            return result;
        }

        std::size_t totalPieceWeight = 0;
        std::size_t maxPieceWeight = 0;
        for (const uint32_t pieceWeight : pieceWeights)
        {
            totalPieceWeight += pieceWeight;
            maxPieceWeight = std::max(maxPieceWeight, static_cast<std::size_t>(pieceWeight));
        }

        std::size_t totalNodeWeight = 0;
        std::size_t weightedNodeCount = 0;
        std::size_t maxNodeWeight = 0;
        for (const uint32_t nodeWeight : nodeWeights)
        {
            if (nodeWeight == std::numeric_limits<uint32_t>::max())
            {
                continue;
            }
            totalNodeWeight += nodeWeight;
            weightedNodeCount += 1;
            maxNodeWeight = std::max(maxNodeWeight, static_cast<std::size_t>(nodeWeight));
        }

        std::size_t maxHyperNodeWeight = 0;
        std::size_t maxHyperEdgeWeight = 0;
        for (const uint32_t w : hg.nodeWeights)
        {
            maxHyperNodeWeight = std::max(maxHyperNodeWeight, static_cast<std::size_t>(w));
        }
        for (const auto &edge : hg.edges)
        {
            maxHyperEdgeWeight = std::max(maxHyperEdgeWeight, static_cast<std::size_t>(edge.weight));
        }

        std::ostringstream phaseCSummary;
        phaseCSummary << "repcut phase-c: graph=" << graph->symbol()
                      << " weighted_nodes=" << weightedNodeCount
                      << " total_node_weight=" << totalNodeWeight
                      << " max_node_weight=" << maxNodeWeight
                      << " total_piece_weight=" << totalPieceWeight
                      << " max_piece_weight=" << maxPieceWeight
                      << " hyper_nodes=" << hg.nodeWeights.size()
                      << " hyper_edges=" << hg.edges.size()
                      << " max_hyper_node_weight=" << maxHyperNodeWeight
                      << " max_hyper_edge_weight=" << maxHyperEdgeWeight;
        const uint64_t phaseCMs = msSince(phaseCStart);
        phaseCSummary << " hyper_build_ms=" << hyperBuildMs
                      << " elapsed_ms=" << phaseCMs;
        logInfo(phaseCSummary.str());

        if (hg.nodeWeights.empty())
        {
            warning("repcut phase-d: hypergraph has no nodes, skipping partitioning");
            return result;
        }

        const auto phaseDStart = std::chrono::steady_clock::now();
        std::filesystem::path outputDir = options_.workDir.empty() ? std::filesystem::path(".")
                                        : std::filesystem::path(options_.workDir);
        std::error_code fsError;
        std::filesystem::create_directories(outputDir, fsError);
        if (fsError)
        {
            error("repcut phase-d: cannot create output directory: " + outputDir.string());
            result.failed = true;
            return result;
        }

        const std::string graphBase = wolvrix::lib::grh::Graph::normalizeComponent(graph->symbol());
        const std::string stem = graphBase + "_repcut_k" + std::to_string(options_.partitionCount);
        const std::filesystem::path hmetisPath = outputDir / (stem + ".hgr");
        const std::filesystem::path configPath = outputDir / (stem + ".kahypar.cfg");
        const std::filesystem::path kaHyParLogPath = outputDir / (stem + ".kahypar.log");

        std::string ioError;
        const auto writeHgrStart = std::chrono::steady_clock::now();
        if (!writeHyperGraphToHmetis(hg, hmetisPath, ioError))
        {
            error("repcut phase-d: " + ioError);
            result.failed = true;
            return result;
        }
        const uint64_t writeHgrMs = msSince(writeHgrStart);
        const auto writeCfgStart = std::chrono::steady_clock::now();
        const std::string configText = buildKaHyParConfig();
        if (!writeTextFile(configPath, configText, ioError))
        {
            error("repcut phase-d: " + ioError);
            result.failed = true;
            return result;
        }
        const uint64_t writeCfgMs = msSince(writeCfgStart);
        result.artifacts.push_back(hmetisPath.string());
        result.artifacts.push_back(configPath.string());
        result.artifacts.push_back(kaHyParLogPath.string());

        std::error_code sizeEc;
        const uintmax_t hgrBytes = std::filesystem::file_size(hmetisPath, sizeEc);
        sizeEc.clear();
        const uintmax_t cfgBytes = std::filesystem::file_size(configPath, sizeEc);

        {
            std::ostringstream dprep;
            dprep << "repcut phase-d prep: hgr_path=" << hmetisPath.string()
                  << " hgr_bytes=" << hgrBytes
                  << " cfg_path=" << configPath.string()
                  << " cfg_bytes=" << cfgBytes
                  << " write_hgr_ms=" << writeHgrMs
                  << " write_cfg_ms=" << writeCfgMs;
            logInfo(dprep.str());
        }

        cleanupStalePartitionOutputs(hmetisPath);

        if (!isExecutableAvailable(options_.kaHyParPath))
        {
            error("repcut phase-d: KaHyPar executable not found: " + options_.kaHyParPath);
            result.failed = true;
            return result;
        }

        std::string kaHyParCommand;
        const auto kaHyParRunStart = std::chrono::steady_clock::now();
        const int kaHyParResult = runKaHyParCommand(options_.kaHyParPath,
                                                    hmetisPath,
                                                    configPath,
                                                    options_.partitionCount,
                                                    options_.imbalanceFactor,
                                                    kaHyParLogPath,
                                                    kaHyParCommand);
        const uint64_t kaHyParRunMs = msSince(kaHyParRunStart);
        debug("repcut phase-d command: " + kaHyParCommand);
        if (kaHyParResult != 0)
        {
            const std::string logTail = readFileTail(kaHyParLogPath, 40);
            std::ostringstream diag;
            diag << "repcut phase-d: KaHyPar failed (" << decodeSystemStatus(kaHyParResult) << ")"
                 << "; graph=" << graph->symbol()
                 << "; hyper_nodes=" << hg.nodeWeights.size()
                 << "; hyper_edges=" << hg.edges.size()
                 << "; kahypar_run_ms=" << kaHyParRunMs
                 << "; hmetis=" << hmetisPath.string()
                 << "; config=" << configPath.string()
                 << "; log=" << kaHyParLogPath.string();
            if (hg.edges.empty())
            {
                diag << "; hint=hypergraph has zero hyper-edges (degenerate partitioning input)";
            }
            if (!logTail.empty())
            {
                diag << "\nKaHyPar log tail:\n" << logTail;
            }
            error(diag.str());
            result.failed = true;
            return result;
        }

            logInfo("repcut phase-d kahypar: run_ms=" + std::to_string(kaHyParRunMs) +
                " log=" + kaHyParLogPath.string());

            const auto parsePartStart = std::chrono::steady_clock::now();
        const std::filesystem::path partitionPath = findPartitionResultPath(hmetisPath,
                                            options_.partitionCount,
                                            options_.imbalanceFactor,
                                            -1);
        if (!std::filesystem::exists(partitionPath, fsError))
        {
            std::ostringstream msg;
            msg << "repcut phase-d: partition result file not found: " << partitionPath.string()
                << "; candidates=" << summarizePartitionCandidates(hmetisPath)
                << "; log=" << kaHyParLogPath.string();
            const std::string logTail = readFileTail(kaHyParLogPath, 20);
            if (!logTail.empty())
            {
                msg << "\nKaHyPar log tail:\n" << logTail;
            }
            error(msg.str());
            result.failed = true;
            return result;
        }
        result.artifacts.push_back(partitionPath.string());

        bool partitionComplete = true;
        std::string partitionWarning;
        const std::vector<uint32_t> ascPartition =
            parsePartitionResult(partitionPath,
                                 phaseB.ascs.size(),
                                 options_.partitionCount,
                                 partitionComplete,
                                 partitionWarning);
        if (!partitionComplete && !partitionWarning.empty())
        {
            warning("repcut phase-d: " + partitionWarning);
        }
        const uint64_t parsePartMs = msSince(parsePartStart);

        uint32_t maxPartId = 0;
        std::unordered_map<uint32_t, size_t> partSizes;
        for (const uint32_t part : ascPartition)
        {
            maxPartId = std::max(maxPartId, part);
            partSizes[part] += 1;
        }

        std::ostringstream phaseDSummary;
        phaseDSummary << "repcut phase-d: graph=" << graph->symbol()
                      << " hmetis=" << hmetisPath.string()
                      << " partition_file=" << partitionPath.string()
                      << " asc_count=" << ascPartition.size()
                      << " part_count_observed=" << (partSizes.empty() ? 0 : (maxPartId + 1))
                      << " partition_complete=" << (partitionComplete ? "true" : "false");
        const uint64_t phaseDMs = msSince(phaseDStart);
        phaseDSummary << " parse_partition_ms=" << parsePartMs
                      << " kahypar_run_ms=" << kaHyParRunMs
                      << " elapsed_ms=" << phaseDMs;
        logInfo(phaseDSummary.str());

        const auto phaseEStart = std::chrono::steady_clock::now();
        const auto phaseEMapStart = std::chrono::steady_clock::now();
        std::unordered_map<wolvrix::lib::grh::OperationId, uint32_t, wolvrix::lib::grh::OperationIdHash> opPartition;
        opPartition.reserve(data.nodeToOp.size());

        for (AscId aid = 0; aid < phaseB.ascs.size(); ++aid)
        {
            uint32_t partId = 0;
            if (aid < ascPartition.size())
            {
                partId = ascPartition[aid];
            }

            for (const size_t sinkIndex : phaseB.ascs[aid].sinks)
            {
                const SinkRef &sink = phaseB.sinks[sinkIndex];
                if (sink.kind == SinkRef::Kind::Operation && sink.op.valid())
                {
                    opPartition[sink.op] = partId;
                }
            }
            for (const NodeId node : phaseB.ascs[aid].combOps)
            {
                if (node < data.nodeToOp.size())
                {
                    opPartition[data.nodeToOp[node]] = partId;
                }
            }
        }

        std::unordered_map<std::string, uint32_t> regPartition;
        std::unordered_map<std::string, uint32_t> latchPartition;
        std::unordered_map<std::string, uint32_t> memPartition;
        for (AscId aid = 0; aid < phaseB.ascs.size(); ++aid)
        {
            const uint32_t partId = (aid < ascPartition.size()) ? ascPartition[aid] : 0u;
            for (const size_t sinkIndex : phaseB.ascs[aid].sinks)
            {
                const SinkRef &sink = phaseB.sinks[sinkIndex];
                if (sink.kind != SinkRef::Kind::Operation)
                {
                    continue;
                }
                const wolvrix::lib::grh::Operation op = graph->getOperation(sink.op);
                if (auto sym = getAttrString(op, "regSymbol"))
                {
                    regPartition[*sym] = partId;
                }
                else if (auto sym = getAttrString(op, "latchSymbol"))
                {
                    latchPartition[*sym] = partId;
                }
                else if (auto sym = getAttrString(op, "memSymbol"))
                {
                    memPartition[*sym] = partId;
                }
            }
        }

        std::unordered_map<std::string, StorageInfo> regInfos;
        std::unordered_map<std::string, StorageInfo> latchInfos;
        std::unordered_map<std::string, StorageInfo> memInfos;
        collectStorageInfos(*graph, regInfos, latchInfos, memInfos);

        assignStorageOpsToPartition(regInfos, regPartition, opPartition);
        assignStorageOpsToPartition(latchInfos, latchPartition, opPartition);
        assignStorageOpsToPartition(memInfos, memPartition, opPartition);
        const uint64_t phaseEMapMs = msSince(phaseEMapStart);

        std::unordered_map<wolvrix::lib::grh::ValueId, uint32_t, wolvrix::lib::grh::ValueIdHash> valueDefPartition;
        valueDefPartition.reserve(graph->values().size());
        for (const auto &[opId, pid] : opPartition)
        {
            const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
            for (const auto resultValue : op.results())
            {
                valueDefPartition[resultValue] = pid;
            }
        }

        std::vector<CrossPartitionValue> crossValues;
        bool hasForbiddenCross = false;
        std::size_t forbiddenMemoryReadCrossCount = 0;
        const auto phaseECrossStart = std::chrono::steady_clock::now();

        for (const auto &[value, defPart] : valueDefPartition)
        {
            const wolvrix::lib::grh::Value val = graph->getValue(value);
            const wolvrix::lib::grh::OperationId defOpId = val.definingOp();
            wolvrix::lib::grh::OperationKind defKind = wolvrix::lib::grh::OperationKind::kConstant;
            bool defKindKnown = false;
            if (defOpId.valid())
            {
                defKind = graph->getOperation(defOpId).kind();
                defKindKnown = true;
            }

            for (const auto &user : val.users())
            {
                auto itPart = opPartition.find(user.operation);
                if (itPart == opPartition.end())
                {
                    continue;
                }
                const uint32_t usePart = itPart->second;
                if (usePart == defPart)
                {
                    continue;
                }

                bool allowCross = false;
                bool requiresPort = false;
                if (defKindKnown)
                {
                    if (defKind == wolvrix::lib::grh::OperationKind::kRegisterReadPort ||
                        defKind == wolvrix::lib::grh::OperationKind::kLatchReadPort)
                    {
                        allowCross = true;
                        requiresPort = true;
                    }
                    else if (defKind == wolvrix::lib::grh::OperationKind::kMemoryReadPort)
                    {
                        allowCross = false;
                    }
                    else if (defKind == wolvrix::lib::grh::OperationKind::kConstant)
                    {
                        allowCross = true;
                        requiresPort = false;
                    }
                    else
                    {
                        allowCross = false;
                    }
                }
                else
                {
                    if (val.isInput() || inoutInputValues.find(value) != inoutInputValues.end())
                    {
                        allowCross = true;
                        requiresPort = true;
                    }
                }

                if (!allowCross)
                {
                    hasForbiddenCross = true;
                    if (defKindKnown && defKind == wolvrix::lib::grh::OperationKind::kMemoryReadPort)
                    {
                        ++forbiddenMemoryReadCrossCount;
                    }
                }

                CrossPartitionValue cv;
                cv.value = value;
                cv.srcPart = defPart;
                cv.dstPart = usePart;
                cv.allowed = allowCross;
                cv.requiresPort = requiresPort;
                crossValues.push_back(cv);
            }
        }
        const uint64_t phaseECrossMs = msSince(phaseECrossStart);

        std::unordered_map<wolvrix::lib::grh::ValueId, uint32_t, wolvrix::lib::grh::ValueIdHash> firstInputOwner;
        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> sourceInputValues =
            inoutInputValues;
        for (const auto &port : graph->inputPorts())
        {
            sourceInputValues.insert(port.value);
        }
        for (const auto valueId : sourceInputValues)
        {
            const wolvrix::lib::grh::Value val = graph->getValue(valueId);
            for (const auto &user : val.users())
            {
                auto itPart = opPartition.find(user.operation);
                if (itPart == opPartition.end())
                {
                    continue;
                }
                const uint32_t usePart = itPart->second;
                auto [it, inserted] = firstInputOwner.emplace(valueId, usePart);
                if (inserted || it->second == usePart)
                {
                    continue;
                }

                CrossPartitionValue cv;
                cv.value = valueId;
                cv.srcPart = it->second;
                cv.dstPart = usePart;
                cv.allowed = true;
                cv.requiresPort = true;
                crossValues.push_back(cv);
            }
        }

        if (hasForbiddenCross)
        {
            if (forbiddenMemoryReadCrossCount > 0)
            {
                error("repcut phase-e: detected memory-read cross-partition usage (" +
                      std::to_string(forbiddenMemoryReadCrossCount) + ")");
            }
            else
            {
                error("repcut phase-e: detected forbidden cross-partition values");
            }
            result.failed = true;
            return result;
        }

        std::size_t crossAllowedCount = 0;
        std::size_t crossNeedsPortCount = 0;
        for (const auto &cv : crossValues)
        {
            if (cv.allowed)
            {
                ++crossAllowedCount;
            }
            if (cv.allowed && cv.requiresPort)
            {
                ++crossNeedsPortCount;
            }
        }

        std::ostringstream phaseESummary;
        phaseESummary << "repcut phase-e: graph=" << graph->symbol()
                      << " op_partitioned=" << opPartition.size()
                      << " reg_partitions=" << regPartition.size()
                      << " latch_partitions=" << latchPartition.size()
                      << " mem_partitions=" << memPartition.size()
                      << " cross_values_total=" << crossValues.size()
                      << " cross_values_allowed=" << crossAllowedCount
                      << " cross_values_need_ports=" << crossNeedsPortCount
                      << " map_storage_ms=" << phaseEMapMs
                      << " cross_scan_ms=" << phaseECrossMs;
        logInfo(phaseESummary.str());

        const auto phaseERebuildStart = std::chrono::steady_clock::now();
        struct DefInfo
        {
            uint32_t owner = std::numeric_limits<uint32_t>::max();
            uint32_t count = 0;
        };

        const auto values = graph->values();
        std::unordered_map<wolvrix::lib::grh::ValueId, ValueInfo, wolvrix::lib::grh::ValueIdHash> valueInfos;
        valueInfos.reserve(values.size());
        for (const auto valueId : values)
        {
            valueInfos.emplace(valueId, captureValueInfo(*graph, valueId));
        }

        for (const auto opId : graph->operations())
        {
            if (opPartition.find(opId) != opPartition.end())
            {
                continue;
            }
            const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
            if (op.kind() != wolvrix::lib::grh::OperationKind::kConstant)
            {
                continue;
            }

            std::optional<uint32_t> owner;
            bool multiOwner = false;
            for (const auto resultValue : op.results())
            {
                const wolvrix::lib::grh::Value value = graph->getValue(resultValue);
                for (const auto &user : value.users())
                {
                    auto it = opPartition.find(user.operation);
                    if (it == opPartition.end())
                    {
                        continue;
                    }
                    if (!owner)
                    {
                        owner = it->second;
                    }
                    else if (*owner != it->second)
                    {
                        multiOwner = true;
                        break;
                    }
                }
                if (multiOwner)
                {
                    break;
                }
            }
            if (owner && !multiOwner)
            {
                opPartition.emplace(opId, *owner);
            }
        }

        std::size_t partitionCount = options_.partitionCount;
        for (const auto &[opId, pid] : opPartition)
        {
            (void)opId;
            partitionCount = std::max(partitionCount, static_cast<std::size_t>(pid) + 1);
        }

        std::vector<std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash>>
            partitionOps(partitionCount);
        for (const auto &[opId, pid] : opPartition)
        {
            if (pid >= partitionOps.size())
            {
                continue;
            }
            partitionOps[pid].insert(opId);
        }

        for (const auto opId : graph->operations())
        {
            const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
            if (op.kind() != wolvrix::lib::grh::OperationKind::kConstant)
            {
                continue;
            }
            for (const auto resultValue : op.results())
            {
                const wolvrix::lib::grh::Value value = graph->getValue(resultValue);
                for (const auto &user : value.users())
                {
                    auto it = opPartition.find(user.operation);
                    if (it == opPartition.end())
                    {
                        continue;
                    }
                    if (it->second < partitionOps.size())
                    {
                        partitionOps[it->second].insert(opId);
                    }
                }
            }
        }

        std::unordered_map<wolvrix::lib::grh::ValueId, DefInfo, wolvrix::lib::grh::ValueIdHash> defInfo;
        defInfo.reserve(values.size());
        for (std::size_t p = 0; p < partitionOps.size(); ++p)
        {
            for (const auto opId : partitionOps[p])
            {
                const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
                for (const auto resultValue : op.results())
                {
                    if (!resultValue.valid())
                    {
                        continue;
                    }
                    auto &info = defInfo[resultValue];
                    if (info.count == 0)
                    {
                        info.owner = static_cast<uint32_t>(p);
                        info.count = 1;
                    }
                    else if (info.owner != p)
                    {
                        info.owner = std::numeric_limits<uint32_t>::max();
                        info.count += 1;
                    }
                }
            }
        }

        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> origInputs;
        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> origOutputs;
        for (const auto &port : graph->inputPorts())
        {
            origInputs.insert(port.value);
        }
        for (const auto &port : graph->outputPorts())
        {
            origOutputs.insert(port.value);
        }
        for (const auto &port : graph->inoutPorts())
        {
            origInputs.insert(port.in);
            origOutputs.insert(port.out);
            origOutputs.insert(port.oe);
        }

        std::unordered_map<wolvrix::lib::grh::ValueId, bool, wolvrix::lib::grh::ValueIdHash> usedByOther;
        for (std::size_t p = 0; p < partitionOps.size(); ++p)
        {
            for (const auto opId : partitionOps[p])
            {
                const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
                for (const auto operand : op.operands())
                {
                    auto it = defInfo.find(operand);
                    if (it == defInfo.end())
                    {
                        continue;
                    }
                    if (it->second.owner != std::numeric_limits<uint32_t>::max() &&
                        it->second.owner != p)
                    {
                        usedByOther[operand] = true;
                    }
                }
            }
        }

        struct PartitionGraphInfo
        {
            wolvrix::lib::grh::Graph *graph = nullptr;
            std::string name;
            std::unordered_map<wolvrix::lib::grh::ValueId, std::string, wolvrix::lib::grh::ValueIdHash> inputPortByValue;
            std::unordered_map<wolvrix::lib::grh::ValueId, std::string, wolvrix::lib::grh::ValueIdHash> outputPortByValue;
        };

        std::vector<PartitionGraphInfo> partInfos;
        partInfos.reserve(partitionOps.size());

        for (std::size_t p = 0; p < partitionOps.size(); ++p)
        {
            const std::string partName = uniqueGraphName(design(), graph->symbol() + "_part" + std::to_string(p));
            wolvrix::lib::grh::Graph &partGraph = design().cloneGraph(graph->symbol(), partName);

            std::unordered_map<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash>
                valueMap;
            std::unordered_map<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash>
                opMap;

            const auto srcValues = graph->values();
            const auto dstValues = partGraph.values();
            for (std::size_t i = 0; i < srcValues.size() && i < dstValues.size(); ++i)
            {
                valueMap.emplace(srcValues[i], dstValues[i]);
            }
            const auto srcOps = graph->operations();
            const auto dstOps = partGraph.operations();
            for (std::size_t i = 0; i < srcOps.size() && i < dstOps.size(); ++i)
            {
                opMap.emplace(srcOps[i], dstOps[i]);
            }

            for (const auto &port : partGraph.inputPorts())
            {
                partGraph.removeInputPort(port.name);
            }
            for (const auto &port : partGraph.outputPorts())
            {
                partGraph.removeOutputPort(port.name);
            }
            for (const auto &port : partGraph.inoutPorts())
            {
                partGraph.removeInoutPort(port.name);
            }

            std::unordered_set<std::string> usedPortNames;
            std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> inputValues;
            std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> outputValues;

            for (const auto opId : partitionOps[p])
            {
                const wolvrix::lib::grh::Operation op = graph->getOperation(opId);
                for (const auto operand : op.operands())
                {
                    if (!operand.valid())
                    {
                        continue;
                    }
                    if (origInputs.find(operand) != origInputs.end())
                    {
                        inputValues.insert(operand);
                        continue;
                    }

                    auto it = defInfo.find(operand);
                    if (it == defInfo.end())
                    {
                        const wolvrix::lib::grh::OperationId operandDef = graph->valueDef(operand);
                        if (operandDef.valid() &&
                            graph->getOperation(operandDef).kind() == wolvrix::lib::grh::OperationKind::kConstant)
                        {
                            continue;
                        }
                        inputValues.insert(operand);
                        continue;
                    }
                    if (it->second.owner != std::numeric_limits<uint32_t>::max() && it->second.owner != p)
                    {
                        inputValues.insert(operand);
                    }
                }

                for (const auto resultValue : op.results())
                {
                    if (!resultValue.valid())
                    {
                        continue;
                    }
                    auto it = defInfo.find(resultValue);
                    if (it == defInfo.end())
                    {
                        continue;
                    }
                    if (it->second.owner == p &&
                        (origOutputs.find(resultValue) != origOutputs.end() ||
                         usedByOther.find(resultValue) != usedByOther.end()))
                    {
                        outputValues.insert(resultValue);
                    }
                }
            }

            PartitionGraphInfo info;
            info.graph = &partGraph;
            info.name = partName;

            for (const auto valueId : inputValues)
            {
                auto it = valueMap.find(valueId);
                if (it == valueMap.end())
                {
                    continue;
                }
                const auto vinfoIt = valueInfos.find(valueId);
                if (vinfoIt == valueInfos.end())
                {
                    continue;
                }
                const ValueInfo &vinfo = vinfoIt->second;
                const std::string fallback = std::string("value") + std::to_string(valueId.index);
                const std::string base = normalizePortBase(vinfo.symbol, fallback);
                const std::string portName = uniquePortName(usedPortNames, "in_" + base);
                partGraph.bindInputPort(portName, it->second);
                info.inputPortByValue.emplace(valueId, portName);
            }

            for (const auto valueId : outputValues)
            {
                auto it = valueMap.find(valueId);
                if (it == valueMap.end())
                {
                    continue;
                }
                const auto vinfoIt = valueInfos.find(valueId);
                if (vinfoIt == valueInfos.end())
                {
                    continue;
                }
                const ValueInfo &vinfo = vinfoIt->second;
                const std::string fallback = std::string("value") + std::to_string(valueId.index);
                const std::string base = normalizePortBase(vinfo.symbol, fallback);
                const std::string portName = uniquePortName(usedPortNames, "out_" + base);
                partGraph.bindOutputPort(portName, it->second);
                info.outputPortByValue.emplace(valueId, portName);
            }

            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> keepCloneOps;
            keepCloneOps.reserve(partitionOps[p].size());
            for (const auto opId : partitionOps[p])
            {
                auto it = opMap.find(opId);
                if (it != opMap.end())
                {
                    keepCloneOps.insert(it->second);
                }
            }
            for (const auto opId : partGraph.operations())
            {
                if (keepCloneOps.find(opId) == keepCloneOps.end())
                {
                    partGraph.eraseOpUnchecked(opId);
                }
            }

            partInfos.push_back(std::move(info));
        }

        struct PortSnapshot
        {
            std::string name;
            wolvrix::lib::grh::ValueId value;
            ValueInfo info;
        };
        struct InoutSnapshot
        {
            std::string name;
            wolvrix::lib::grh::ValueId in;
            wolvrix::lib::grh::ValueId out;
            wolvrix::lib::grh::ValueId oe;
            ValueInfo infoIn;
            ValueInfo infoOut;
            ValueInfo infoOe;
        };

        std::vector<PortSnapshot> inputSnapshot;
        std::vector<PortSnapshot> outputSnapshot;
        std::vector<InoutSnapshot> inoutSnapshot;
        inputSnapshot.reserve(graph->inputPorts().size());
        outputSnapshot.reserve(graph->outputPorts().size());
        inoutSnapshot.reserve(graph->inoutPorts().size());

        for (const auto &port : graph->inputPorts())
        {
            inputSnapshot.push_back(PortSnapshot{port.name, port.value, captureValueInfo(*graph, port.value)});
        }
        for (const auto &port : graph->outputPorts())
        {
            outputSnapshot.push_back(PortSnapshot{port.name, port.value, captureValueInfo(*graph, port.value)});
        }
        for (const auto &port : graph->inoutPorts())
        {
            InoutSnapshot snap;
            snap.name = port.name;
            snap.in = port.in;
            snap.out = port.out;
            snap.oe = port.oe;
            snap.infoIn = captureValueInfo(*graph, port.in);
            snap.infoOut = captureValueInfo(*graph, port.out);
            snap.infoOe = captureValueInfo(*graph, port.oe);
            inoutSnapshot.push_back(std::move(snap));
        }

        std::vector<std::string> topAliases = design().aliasesForGraph(graph->symbol());
        bool wasTop = false;
        for (const auto &topName : design().topGraphs())
        {
            if (topName == graph->symbol())
            {
                wasTop = true;
                break;
            }
        }

        const std::string topName = graph->symbol();
        design().deleteGraph(topName);
        wolvrix::lib::grh::Graph &newTop = design().createGraph(topName);

        std::unordered_map<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash>
            topValueBySource;

        for (const auto &port : inputSnapshot)
        {
            const std::string base = normalizePortBase(port.info.symbol, "in");
            const wolvrix::lib::grh::SymbolId sym = internUniqueSymbol(newTop, base);
            const wolvrix::lib::grh::ValueId value =
                newTop.createValue(sym, port.info.width, port.info.isSigned, port.info.type);
            if (port.info.srcLoc)
            {
                newTop.setValueSrcLoc(value, *port.info.srcLoc);
            }
            newTop.bindInputPort(port.name, value);
            topValueBySource.emplace(port.value, value);
        }

        std::unordered_map<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash>
            linkValues;
        for (const auto &[valueId, info] : defInfo)
        {
            if (info.owner == std::numeric_limits<uint32_t>::max())
            {
                continue;
            }
            if (usedByOther.find(valueId) == usedByOther.end())
            {
                continue;
            }
            auto vinfoIt = valueInfos.find(valueId);
            if (vinfoIt == valueInfos.end())
            {
                continue;
            }
            const ValueInfo &vinfo = vinfoIt->second;
            const std::string base = normalizePortBase(vinfo.symbol, "link");
            const wolvrix::lib::grh::SymbolId sym = internUniqueSymbol(newTop, "repcut_link_" + base);
            const wolvrix::lib::grh::ValueId linkVal =
                newTop.createValue(sym, vinfo.width, vinfo.isSigned, vinfo.type);
            if (vinfo.srcLoc)
            {
                newTop.setValueSrcLoc(linkVal, *vinfo.srcLoc);
            }
            linkValues.emplace(valueId, linkVal);
        }

        for (const auto &port : outputSnapshot)
        {
            auto itLink = linkValues.find(port.value);
            if (itLink != linkValues.end())
            {
                newTop.bindOutputPort(port.name, itLink->second);
                topValueBySource.emplace(port.value, itLink->second);
                continue;
            }
            auto itTop = topValueBySource.find(port.value);
            if (itTop != topValueBySource.end())
            {
                newTop.bindOutputPort(port.name, itTop->second);
                continue;
            }

            const std::string base = normalizePortBase(port.info.symbol, "out");
            const wolvrix::lib::grh::SymbolId sym = internUniqueSymbol(newTop, base);
            const wolvrix::lib::grh::ValueId value =
                newTop.createValue(sym, port.info.width, port.info.isSigned, port.info.type);
            if (port.info.srcLoc)
            {
                newTop.setValueSrcLoc(value, *port.info.srcLoc);
            }
            newTop.bindOutputPort(port.name, value);
            topValueBySource.emplace(port.value, value);
        }

        for (const auto &port : inoutSnapshot)
        {
            const wolvrix::lib::grh::SymbolId inSym = internUniqueSymbol(newTop, normalizePortBase(port.infoIn.symbol, "in"));
            const wolvrix::lib::grh::ValueId inVal =
                newTop.createValue(inSym, port.infoIn.width, port.infoIn.isSigned, port.infoIn.type);
            if (port.infoIn.srcLoc)
            {
                newTop.setValueSrcLoc(inVal, *port.infoIn.srcLoc);
            }

            wolvrix::lib::grh::ValueId outVal;
            auto outLink = linkValues.find(port.out);
            if (outLink != linkValues.end())
            {
                outVal = outLink->second;
            }
            else
            {
                const wolvrix::lib::grh::SymbolId outSym = internUniqueSymbol(newTop, normalizePortBase(port.infoOut.symbol, "out"));
                outVal = newTop.createValue(outSym, port.infoOut.width, port.infoOut.isSigned, port.infoOut.type);
                if (port.infoOut.srcLoc)
                {
                    newTop.setValueSrcLoc(outVal, *port.infoOut.srcLoc);
                }
            }

            wolvrix::lib::grh::ValueId oeVal;
            auto oeLink = linkValues.find(port.oe);
            if (oeLink != linkValues.end())
            {
                oeVal = oeLink->second;
            }
            else
            {
                const wolvrix::lib::grh::SymbolId oeSym = internUniqueSymbol(newTop, normalizePortBase(port.infoOe.symbol, "oe"));
                oeVal = newTop.createValue(oeSym, port.infoOe.width, port.infoOe.isSigned, port.infoOe.type);
                if (port.infoOe.srcLoc)
                {
                    newTop.setValueSrcLoc(oeVal, *port.infoOe.srcLoc);
                }
            }

            newTop.bindInoutPort(port.name, inVal, outVal, oeVal);
            topValueBySource.emplace(port.in, inVal);
            topValueBySource.emplace(port.out, outVal);
            topValueBySource.emplace(port.oe, oeVal);
        }

        for (std::size_t p = 0; p < partInfos.size(); ++p)
        {
            PartitionGraphInfo &part = partInfos[p];
            if (!part.graph)
            {
                continue;
            }

            std::unordered_map<std::string, wolvrix::lib::grh::ValueId> inputMapping;
            std::unordered_map<std::string, wolvrix::lib::grh::ValueId> outputMapping;

            for (const auto &[sourceValue, portName] : part.inputPortByValue)
            {
                auto itTop = topValueBySource.find(sourceValue);
                if (itTop != topValueBySource.end())
                {
                    inputMapping.emplace(portName, itTop->second);
                    continue;
                }
                auto itLink = linkValues.find(sourceValue);
                if (itLink != linkValues.end())
                {
                    inputMapping.emplace(portName, itLink->second);
                }
            }

            for (const auto &[sourceValue, portName] : part.outputPortByValue)
            {
                auto itTop = topValueBySource.find(sourceValue);
                if (itTop != topValueBySource.end())
                {
                    outputMapping.emplace(portName, itTop->second);
                    continue;
                }
                auto itLink = linkValues.find(sourceValue);
                if (itLink != linkValues.end())
                {
                    outputMapping.emplace(portName, itLink->second);
                }
            }

            if (inputMapping.size() != part.graph->inputPorts().size())
            {
                warning("repcut phase-e: incomplete instance input mapping for " + part.graph->symbol());
            }
            if (outputMapping.size() != part.graph->outputPorts().size())
            {
                warning("repcut phase-e: incomplete instance output mapping for " + part.graph->symbol());
            }

            buildInstance(newTop, part.graph->symbol(), "part_" + std::to_string(p), *part.graph, inputMapping, outputMapping);
        }

        for (const auto &alias : topAliases)
        {
            design().registerGraphAlias(alias, newTop);
        }
        if (wasTop)
        {
            design().markAsTop(topName);
        }

        result.changed = true;

        std::ostringstream phaseEReconstructSummary;
        phaseEReconstructSummary << "repcut phase-e reconstruct: graph=" << topName
                                 << " partition_graphs=" << partInfos.size()
                                 << " cross_links=" << linkValues.size()
                                 << " rebuild_ms=" << msSince(phaseERebuildStart);
        logInfo(phaseEReconstructSummary.str());
        const uint64_t phaseEMs = msSince(phaseEStart);

        if (!options_.keepIntermediateFiles)
        {
            std::error_code cleanupError;
            std::filesystem::remove(hmetisPath, cleanupError);
            cleanupError.clear();
            std::filesystem::remove(configPath, cleanupError);
            cleanupError.clear();
            std::filesystem::remove(partitionPath, cleanupError);
        }

        std::ostringstream stats;
        stats << "{"
              << "\"pass\":\"repcut\""
              << ",\"graph\":\"" << escapeJson(topName) << "\""
              << ",\"partition_count_requested\":" << options_.partitionCount
              << ",\"partition_count_observed\":" << partInfos.size()
              << ",\"asc_count\":" << phaseB.ascs.size()
              << ",\"piece_count\":" << phaseB.pieces.size()
              << ",\"hyper_edge_count\":" << hg.edges.size()
              << ",\"cross_values_total\":" << crossValues.size()
              << ",\"cross_values_need_ports\":" << crossNeedsPortCount
              << ",\"cross_links\":" << linkValues.size()
              << ",\"time_ms_total\":" << msSince(totalStart)
              << ",\"time_ms_phase_a\":" << phaseAMs
              << ",\"time_ms_phase_b\":" << phaseBMs
              << ",\"time_ms_phase_c\":" << phaseCMs
              << ",\"time_ms_phase_d\":" << phaseDMs
              << ",\"time_ms_phase_e\":" << phaseEMs
              << "}";
        const std::string statsMessage = stats.str();
        info(statsMessage);
        result.artifacts.push_back(statsMessage);
        logInfo("repcut: completed in " + std::to_string(msSince(totalStart)) + "ms");
        logInfo("repcut timing breakdown(ms): phase_a=" + std::to_string(phaseAMs) +
            " phase_b=" + std::to_string(phaseBMs) +
            " phase_c=" + std::to_string(phaseCMs) +
            " phase_d=" + std::to_string(phaseDMs) +
            " phase_e=" + std::to_string(phaseEMs));

        if (verbosity() == PassVerbosity::Debug)
        {
            debug("repcut phase-a: M0 index/adjacency ready");
            debug("repcut phase-a: M1 sink/source/comb classification ready");
            debug("repcut phase-b: ASC and piece initialization ready");
            debug("repcut phase-c: node/piece weights and hypergraph ready");
            debug("repcut phase-d: hmetis/config generation and KaHyPar invocation ready");
            debug("repcut phase-e: op partition mapping and cross-partition checks ready");
        }

        return result;
    }

} // namespace wolvrix::lib::transform
