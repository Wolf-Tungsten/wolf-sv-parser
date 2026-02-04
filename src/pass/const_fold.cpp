#include "pass/const_fold.hpp"

#include "grh.hpp"

#include "slang/numeric/SVInt.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolf_sv_parser::transform
{

    // Timing utilities for profiling
    struct Timer {
        using Clock = std::chrono::high_resolution_clock;
        using Duration = std::chrono::duration<double, std::milli>;
        using TimePoint = Clock::time_point;
        
        std::string name;
        TimePoint start;
        mutable Duration elapsed{};
        mutable size_t count = 0;
        
        explicit Timer(std::string n) : name(std::move(n)), start(Clock::now()) {}
        
        void stop() const {
            elapsed += Clock::now() - start;
            ++count;
        }
        
        void restart() {
            start = Clock::now();
        }
        
        double ms() const { return elapsed.count(); }
    };
    
    struct TimerCollector {
        std::unordered_map<std::string, Timer> timers;
        
        Timer& get(const std::string& name) {
            auto it = timers.find(name);
            if (it == timers.end()) {
                it = timers.emplace(name, Timer(name)).first;
            }
            return it->second;
        }
        
        void report() const {
            std::cerr << "\n=== ConstFoldPass Timing Report ===\n";
            std::vector<const Timer*> sorted;
            for (const auto& [name, timer] : timers) {
                sorted.push_back(&timer);
            }
            std::sort(sorted.begin(), sorted.end(), [](const Timer* a, const Timer* b) {
                return a->ms() > b->ms();
            });
            
            std::cerr << std::setw(40) << std::left << "Phase/Function" 
                      << std::setw(15) << "Time (ms)" 
                      << std::setw(10) << "Count" 
                      << std::setw(15) << "Avg (ms)" << "\n";
            std::cerr << std::string(80, '-') << "\n";
            
            double total = 0;
            for (const auto* t : sorted) {
                // Only sum top-level phases (names starting with 'phase') to avoid double counting
                // nested function timers
                if (t->name.rfind("phase", 0) == 0) {
                    total += t->ms();
                }
                std::cerr << std::setw(40) << t->name 
                          << std::setw(15) << std::fixed << std::setprecision(3) << t->ms()
                          << std::setw(10) << t->count
                          << std::setw(15) << (t->count > 0 ? t->ms() / t->count : 0) << "\n";
            }
            std::cerr << std::string(80, '-') << "\n";
            std::cerr << std::setw(40) << "TOTAL" << std::setw(15) << std::fixed << std::setprecision(3) << total << "\n";
            std::cerr << "===================================\n\n";
        }
    };

    namespace
    {

        struct ConstantValue
        {
            slang::SVInt value;
            bool hasUnknown = false;
        };

        struct FoldOptions
        {
            bool allowXPropagation = false;
        };

        using ConstantStore = std::unordered_map<grh::ir::ValueId, ConstantValue, grh::ir::ValueIdHash>;
        struct ConstantKey
        {
            std::string literal;
            int32_t width = 0;
            bool isSigned = false;

            bool operator==(const ConstantKey &other) const
            {
                return width == other.width &&
                       isSigned == other.isSigned &&
                       literal == other.literal;
            }
        };

        struct ConstantKeyHash
        {
            std::size_t operator()(const ConstantKey &key) const
            {
                std::size_t seed = std::hash<std::string>{}(key.literal);
                seed ^= std::hash<int32_t>{}(key.width) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= std::hash<bool>{}(key.isSigned) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                return seed;
            }
        };

        using ConstantPool = std::unordered_map<ConstantKey, grh::ir::ValueId, ConstantKeyHash>;

        bool isFoldable(grh::ir::OperationKind kind)
        {
            switch (kind)
            {
            case grh::ir::OperationKind::kAdd:
            case grh::ir::OperationKind::kSub:
            case grh::ir::OperationKind::kMul:
            case grh::ir::OperationKind::kDiv:
            case grh::ir::OperationKind::kMod:
            case grh::ir::OperationKind::kEq:
            case grh::ir::OperationKind::kNe:
            case grh::ir::OperationKind::kCaseEq:
            case grh::ir::OperationKind::kCaseNe:
            case grh::ir::OperationKind::kWildcardEq:
            case grh::ir::OperationKind::kWildcardNe:
            case grh::ir::OperationKind::kLt:
            case grh::ir::OperationKind::kLe:
            case grh::ir::OperationKind::kGt:
            case grh::ir::OperationKind::kGe:
            case grh::ir::OperationKind::kAnd:
            case grh::ir::OperationKind::kOr:
            case grh::ir::OperationKind::kXor:
            case grh::ir::OperationKind::kXnor:
            case grh::ir::OperationKind::kNot:
            case grh::ir::OperationKind::kLogicAnd:
            case grh::ir::OperationKind::kLogicOr:
            case grh::ir::OperationKind::kLogicNot:
            case grh::ir::OperationKind::kReduceAnd:
            case grh::ir::OperationKind::kReduceOr:
            case grh::ir::OperationKind::kReduceXor:
            case grh::ir::OperationKind::kReduceNor:
            case grh::ir::OperationKind::kReduceNand:
            case grh::ir::OperationKind::kReduceXnor:
            case grh::ir::OperationKind::kShl:
            case grh::ir::OperationKind::kLShr:
            case grh::ir::OperationKind::kAShr:
            case grh::ir::OperationKind::kMux:
            case grh::ir::OperationKind::kAssign:
            case grh::ir::OperationKind::kConcat:
            case grh::ir::OperationKind::kReplicate:
            case grh::ir::OperationKind::kSliceStatic:
            case grh::ir::OperationKind::kSliceDynamic:
            case grh::ir::OperationKind::kSliceArray:
                return true;
            default:
                return false;
            }
        }

        std::optional<std::string> getStringAttr(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string_view key)
        {
            (void)graph;
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<std::string>(&*attr))
            {
                return *value;
            }
            return std::nullopt;
        }

        std::optional<int64_t> getIntAttr(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string_view key)
        {
            (void)graph;
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<int64_t>(&*attr))
            {
                return *value;
            }
            return std::nullopt;
        }

        bool isValuePortBound(const grh::ir::Graph &graph, grh::ir::ValueId value)
        {
            for (const auto &port : graph.inputPorts())
            {
                if (port.value == value)
                {
                    return true;
                }
            }
            for (const auto &port : graph.outputPorts())
            {
                if (port.value == value)
                {
                    return true;
                }
            }
            for (const auto &port : graph.inoutPorts())
            {
                if (port.in == value || port.out == value || port.oe == value)
                {
                    return true;
                }
            }
            return false;
        }

        std::optional<ConstantValue> parseConstLiteral(const grh::ir::Graph &graph, const grh::ir::Operation &op, const grh::ir::Value &value, const std::string &literal, const std::function<void(std::string)> &onError)
        {
            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(literal);
                if (value.width() <= 0)
                {
                    onError("Value width must be positive for constant propagation: " + std::string(value.symbolText()));
                    return std::nullopt;
                }
                parsed.setSigned(value.isSigned());
                parsed = parsed.resize(static_cast<slang::bitwidth_t>(value.width()));
                ConstantValue result{parsed, parsed.hasUnknown()};
                return result;
            }
            catch (const std::exception &ex)
            {
                std::ostringstream oss;
                oss << "Failed to parse constValue '" << literal << "': " << ex.what();
                onError(oss.str());
                return std::nullopt;
            }
        }

        std::optional<ConstantValue> parseConstValue(const grh::ir::Graph &graph, const grh::ir::Operation &op, const grh::ir::Value &value, const std::function<void(std::string)> &onError)
        {
            auto literalOpt = getStringAttr(graph, op, "constValue");
            if (!literalOpt)
            {
                onError("kConstant missing constValue attribute");
                return std::nullopt;
            }
            return parseConstLiteral(graph, op, value, *literalOpt, onError);
        }

        bool operandsAreConstant(const grh::ir::Operation &op, const ConstantStore &store)
        {
            const auto operands = op.operands();
            for (std::size_t i = 0; i < operands.size(); ++i)
            {
                const auto valId = operands[i];
                if (!valId.valid())
                {
                    return false;
                }
                if (store.find(valId) == store.end())
                {
                    return false;
                }
            }
            return true;
        }

        slang::SVInt normalizeToValue(const grh::ir::Value &value, const slang::SVInt &raw)
        {
            slang::SVInt adjusted = raw;
            adjusted.setSigned(value.isSigned());
            adjusted = adjusted.resize(static_cast<slang::bitwidth_t>(value.width()));
            adjusted.setSigned(value.isSigned());
            return adjusted;
        }

        std::optional<slang::SVInt> foldBinary(const grh::ir::Operation &op, grh::ir::OperationKind kind, const std::vector<slang::SVInt> &operands)
        {
            const slang::SVInt &lhs = operands[0];
            const slang::SVInt &rhs = operands[1];
            switch (kind)
            {
            case grh::ir::OperationKind::kAdd:
                return lhs + rhs;
            case grh::ir::OperationKind::kSub:
                return lhs - rhs;
            case grh::ir::OperationKind::kMul:
                return lhs * rhs;
            case grh::ir::OperationKind::kDiv:
                return lhs / rhs;
            case grh::ir::OperationKind::kMod:
                return lhs % rhs;
            case grh::ir::OperationKind::kAnd:
                return lhs & rhs;
            case grh::ir::OperationKind::kOr:
                return lhs | rhs;
            case grh::ir::OperationKind::kXor:
                return lhs ^ rhs;
            case grh::ir::OperationKind::kXnor:
                return ~(lhs ^ rhs);
            case grh::ir::OperationKind::kEq:
                return slang::SVInt(lhs == rhs);
            case grh::ir::OperationKind::kNe:
                return slang::SVInt(lhs != rhs);
            case grh::ir::OperationKind::kCaseEq:
                return slang::SVInt(exactlyEqual(lhs, rhs));
            case grh::ir::OperationKind::kCaseNe:
                return slang::SVInt(!exactlyEqual(lhs, rhs));
            case grh::ir::OperationKind::kWildcardEq:
                return slang::SVInt(caseXWildcardEqual(lhs, rhs));
            case grh::ir::OperationKind::kWildcardNe:
                return slang::SVInt(!caseXWildcardEqual(lhs, rhs));
            case grh::ir::OperationKind::kLt:
                return slang::SVInt(lhs < rhs);
            case grh::ir::OperationKind::kLe:
                return slang::SVInt(lhs <= rhs);
            case grh::ir::OperationKind::kGt:
                return slang::SVInt(lhs > rhs);
            case grh::ir::OperationKind::kGe:
                return slang::SVInt(lhs >= rhs);
            case grh::ir::OperationKind::kLogicAnd:
                return slang::SVInt(lhs && rhs);
            case grh::ir::OperationKind::kLogicOr:
                return slang::SVInt(lhs || rhs);
            case grh::ir::OperationKind::kShl:
                return lhs.shl(rhs);
            case grh::ir::OperationKind::kLShr:
                return lhs.lshr(rhs);
            case grh::ir::OperationKind::kAShr:
                return lhs.ashr(rhs);
            default:
                (void)op;
                return std::nullopt;
            }
        }

        std::optional<slang::SVInt> foldUnary(const grh::ir::Operation &op, grh::ir::OperationKind kind, const slang::SVInt &operand)
        {
            switch (kind)
            {
            case grh::ir::OperationKind::kNot:
                return ~operand;
            case grh::ir::OperationKind::kLogicNot:
                return slang::SVInt(!operand);
            case grh::ir::OperationKind::kReduceAnd:
                return slang::SVInt(operand.reductionAnd());
            case grh::ir::OperationKind::kReduceOr:
                return slang::SVInt(operand.reductionOr());
            case grh::ir::OperationKind::kReduceXor:
                return slang::SVInt(operand.reductionXor());
            case grh::ir::OperationKind::kReduceNor:
                return slang::SVInt(!operand.reductionOr());
            case grh::ir::OperationKind::kReduceNand:
                return slang::SVInt(!operand.reductionAnd());
            case grh::ir::OperationKind::kReduceXnor:
                return slang::SVInt(!operand.reductionXor());
            default:
                (void)op;
                return std::nullopt;
            }
        }

        std::optional<std::vector<slang::SVInt>> foldOperation(const grh::ir::Graph &graph, const grh::ir::Operation &op, const ConstantStore &store, const FoldOptions options, const std::function<void(std::string)> &onError, const std::function<void(std::string)> &onWarning)
        {
            if (op.results().empty())
            {
                return std::nullopt;
            }

            std::vector<slang::SVInt> operands;
            operands.reserve(op.operands().size());
            bool hasUnknown = false;
            for (std::size_t i = 0; i < op.operands().size(); ++i)
            {
                const auto valId = op.operands()[i];
                if (!valId.valid())
                {
                    onError("Operand missing during constant propagation");
                    return std::nullopt;
                }
                auto it = store.find(valId);
                if (it == store.end())
                {
                    return std::nullopt;
                }
                hasUnknown = hasUnknown || it->second.hasUnknown;
                operands.push_back(it->second.value);
            }

            if (!options.allowXPropagation && hasUnknown)
            {
                onWarning("Skip folding due to X/Z operand when allowXPropagation=false");
                return std::nullopt;
            }

            std::optional<slang::SVInt> folded;
            switch (op.kind())
            {
            case grh::ir::OperationKind::kAdd:
            case grh::ir::OperationKind::kSub:
            case grh::ir::OperationKind::kMul:
            case grh::ir::OperationKind::kDiv:
            case grh::ir::OperationKind::kMod:
            case grh::ir::OperationKind::kEq:
            case grh::ir::OperationKind::kNe:
            case grh::ir::OperationKind::kCaseEq:
            case grh::ir::OperationKind::kCaseNe:
            case grh::ir::OperationKind::kWildcardEq:
            case grh::ir::OperationKind::kWildcardNe:
            case grh::ir::OperationKind::kLt:
            case grh::ir::OperationKind::kLe:
            case grh::ir::OperationKind::kGt:
            case grh::ir::OperationKind::kGe:
            case grh::ir::OperationKind::kAnd:
            case grh::ir::OperationKind::kOr:
            case grh::ir::OperationKind::kXor:
            case grh::ir::OperationKind::kXnor:
            case grh::ir::OperationKind::kLogicAnd:
            case grh::ir::OperationKind::kLogicOr:
            case grh::ir::OperationKind::kShl:
            case grh::ir::OperationKind::kLShr:
            case grh::ir::OperationKind::kAShr:
                folded = foldBinary(op, op.kind(), operands);
                break;
            case grh::ir::OperationKind::kNot:
            case grh::ir::OperationKind::kLogicNot:
            case grh::ir::OperationKind::kReduceAnd:
            case grh::ir::OperationKind::kReduceOr:
            case grh::ir::OperationKind::kReduceXor:
            case grh::ir::OperationKind::kReduceNor:
            case grh::ir::OperationKind::kReduceNand:
            case grh::ir::OperationKind::kReduceXnor:
                folded = foldUnary(op, op.kind(), operands[0]);
                break;
            case grh::ir::OperationKind::kAssign:
                folded = operands[0];
                break;
            case grh::ir::OperationKind::kConcat:
                folded = slang::SVInt::concat(operands);
                break;
            case grh::ir::OperationKind::kReplicate:
            {
                auto repOpt = getIntAttr(graph, op, "rep");
                if (!repOpt)
                {
                    onError("kReplicate missing required 'rep' attribute");
                    return std::nullopt;
                }
                if (*repOpt <= 0)
                {
                    onError("kReplicate requires positive rep attribute");
                    return std::nullopt;
                }
                slang::SVInt times(static_cast<uint64_t>(*repOpt));
                folded = operands[0].replicate(times);
                break;
            }
            case grh::ir::OperationKind::kMux:
                folded = slang::SVInt::conditional(operands[0], operands[1], operands[2]);
                break;
            case grh::ir::OperationKind::kSliceStatic:
            {
                auto startOpt = getIntAttr(graph, op, "sliceStart");
                auto endOpt = getIntAttr(graph, op, "sliceEnd");
                if (!startOpt || !endOpt)
                {
                    onError("kSliceStatic missing sliceStart/sliceEnd attributes");
                    return std::nullopt;
                }
                int64_t start = *startOpt;
                int64_t end = *endOpt;
                if (start < 0 || end < start)
                {
                    onError("kSliceStatic has invalid slice range");
                    return std::nullopt;
                }
                uint64_t width = static_cast<uint64_t>(end - start + 1);
                folded = operands[0].lshr(static_cast<slang::bitwidth_t>(start)).trunc(static_cast<slang::bitwidth_t>(width));
                break;
            }
            case grh::ir::OperationKind::kSliceDynamic:
            case grh::ir::OperationKind::kSliceArray:
            {
                auto widthOpt = getIntAttr(graph, op, "sliceWidth");
                if (!widthOpt)
                {
                    onError("Slice operation missing sliceWidth attribute");
                    return std::nullopt;
                }
                if (*widthOpt <= 0)
                {
                    onError("sliceWidth must be positive");
                    return std::nullopt;
                }
                uint64_t width = static_cast<uint64_t>(*widthOpt);
                const slang::SVInt &input = operands[0];
                const slang::SVInt &offset = operands[1];
                slang::SVInt shifted = input.lshr(offset);
                folded = shifted.trunc(static_cast<slang::bitwidth_t>(width));
                break;
            }
            default:
                return std::nullopt;
            }

            if (!folded)
            {
                return std::nullopt;
            }

            std::vector<slang::SVInt> results;
            results.reserve(op.results().size());
            for (std::size_t i = 0; i < op.results().size(); ++i)
            {
                const auto resId = op.results()[i];
                if (!resId.valid())
                {
                    onError("Result missing during constant propagation");
                    return std::nullopt;
                }
                results.push_back(normalizeToValue(graph.getValue(resId), *folded));
            }
            return results;
        }

        std::string makeUniqueSymbol(const grh::ir::Graph &graph, std::string base, bool isOperation, std::atomic<int> &counter)
        {
            std::string candidate = std::move(base);
            // Fast path: assume no collision, append atomic counter
            int id = counter.fetch_add(1, std::memory_order_relaxed);
            candidate.append("_").append(std::to_string(id));
            
            // Slow path: handle collision (rare)
            const auto exists = [&](const std::string &symbol)
            {
                return isOperation ? graph.findOperation(symbol).valid() : graph.findValue(symbol).valid();
            };
            while (exists(candidate))
            {
                id = counter.fetch_add(1, std::memory_order_relaxed);
                candidate = base + "_" + std::to_string(id);
            }
            return candidate;
        }

        std::string formatConstLiteral(const slang::SVInt &value)
        {
            const slang::bitwidth_t width = value.getBitWidth();
            return value.toString(slang::LiteralBase::Hex, true, width);
        }

        ConstantKey makeConstantKey(const grh::ir::Value &value, const slang::SVInt &sv)
        {
            ConstantKey key;
            key.width = value.width();
            key.isSigned = value.isSigned();
            key.literal = formatConstLiteral(sv);
            return key;
        }

        grh::ir::ValueId createConstant(grh::ir::Graph &graph, ConstantPool &pool,
                                        const grh::ir::Operation &sourceOp, std::size_t resultIndex,
                                        const grh::ir::Value &resultValue,
                                        const slang::SVInt &value,
                                        TimerCollector* timers = nullptr,
                                        std::atomic<int> *symbolCounter = nullptr)
        {
            auto T = [&](const std::string& name) -> Timer* {
                return timers ? &timers->get(name) : nullptr;
            };
            
            ConstantKey key;
            {
                auto* t = T("_cc_make_key");
                Timer::TimePoint start = t ? Timer::Clock::now() : Timer::TimePoint();
                key = makeConstantKey(resultValue, value);
                if (t) { t->elapsed += Timer::Clock::now() - start; ++t->count; }
            }
            {
                auto* t = T("_cc_pool_find");
                Timer::TimePoint start = t ? Timer::Clock::now() : Timer::TimePoint();
                if (auto it = pool.find(key); it != pool.end())
                {
                    return it->second;
                }
                if (t) { t->elapsed += Timer::Clock::now() - start; ++t->count; }
            }

            std::string valueName;
            {
                auto* t = T("_cc_make_value_symbol");
                Timer::TimePoint start = t ? Timer::Clock::now() : Timer::TimePoint();
                std::ostringstream base;
                base << "__constfold_" << sourceOp.symbolText() << "_" << resultIndex;
                static std::atomic<int> dummyCounter{0};
                valueName = makeUniqueSymbol(graph, base.str(), /*isOperation=*/false, symbolCounter ? *symbolCounter : dummyCounter);
                if (t) { t->elapsed += Timer::Clock::now() - start; ++t->count; }
            }

            std::string opName;
            {
                auto* t = T("_cc_make_op_symbol");
                Timer::TimePoint start = t ? Timer::Clock::now() : Timer::TimePoint();
                std::ostringstream opBase;
                opBase << "__constfold_op_" << sourceOp.symbolText() << "_" << resultIndex;
                static std::atomic<int> dummyCounter2{0};
                opName = makeUniqueSymbol(graph, opBase.str(), /*isOperation=*/true, symbolCounter ? *symbolCounter : dummyCounter2);
                if (t) { t->elapsed += Timer::Clock::now() - start; ++t->count; }
            }

            const grh::ir::SymbolId valueSym = graph.internSymbol(valueName);
            const grh::ir::SymbolId opSym = graph.internSymbol(opName);
            int32_t width = resultValue.width();
            if (width <= 0)
            {
                width = static_cast<int32_t>(value.getBitWidth());
            }
            const grh::ir::ValueId newValue = graph.createValue(valueSym, width, resultValue.isSigned());
            const grh::ir::OperationId constOp = graph.createOperation(grh::ir::OperationKind::kConstant, opSym);
            graph.addResult(constOp, newValue);
            {
                auto* t = T("_cc_format_literal");
                Timer::TimePoint start = t ? Timer::Clock::now() : Timer::TimePoint();
                graph.setAttr(constOp, "constValue", formatConstLiteral(value));
                if (t) { t->elapsed += Timer::Clock::now() - start; ++t->count; }
            }
            pool.emplace(std::move(key), newValue);
            return newValue;
        }

        void replaceUsers(grh::ir::Graph &graph, grh::ir::ValueId oldValue, grh::ir::ValueId newValue, const std::function<void(std::string)> &onError)
        {
            try
            {
                graph.replaceAllUses(oldValue, newValue);
            }
            catch (const std::exception &ex)
            {
                onError(std::string("Failed to replace operands for constant folding: ") + ex.what());
            }

            std::vector<grh::ir::SymbolId> outputPortsToUpdate;
            for (const auto &port : graph.outputPorts())
            {
                if (port.value == oldValue)
                {
                    outputPortsToUpdate.push_back(port.name);
                }
            }

            for (const auto portName : outputPortsToUpdate)
            {
                try
                {
                    graph.bindOutputPort(portName, newValue);
                }
                catch (const std::exception &ex)
                {
                    onError(std::string("Failed to rebind output port during constant folding: ") + ex.what());
                }
            }
        }

    } // namespace

    ConstantFoldPass::ConstantFoldPass()
        : Pass("const-fold", "const-fold"), options_({})
    {
    }

    ConstantFoldPass::ConstantFoldPass(ConstantFoldOptions options)
        : Pass("const-fold", "const-fold"), options_(options)
    {
    }
    
    // Scoped timer helper
    struct ScopedTimer {
        Timer& timer;
        explicit ScopedTimer(Timer& t) : timer(t) { timer.restart(); }
        ~ScopedTimer() { timer.stop(); }
    };

    PassResult ConstantFoldPass::run()
    {
        TimerCollector timers;
        auto T = [&](const std::string& name) -> ScopedTimer { 
            return ScopedTimer(timers.get(name)); 
        };
        
        PassResult result;
        ConstantStore constants;
        bool failed = false;

        auto handleException = [&](const std::exception &ex)
        {
            this->error(std::string("Unhandled exception: ") + ex.what());
            failed = true;
        };

        try
        {

        // Seed constant table from existing kConstant ops and dedupe identical constants.
        bool dedupedConstants = false;
        std::unordered_map<const grh::ir::Graph *, ConstantPool> pools;
        {
            auto _t = T("phase1_collect_constants");
        for (const auto &graphEntry : netlist().graphs())
        {
            grh::ir::Graph &graph = *graphEntry.second;
            ConstantPool &pool = pools[&graph];
            for (const auto opId : graph.operations())
            {
                const grh::ir::Operation op = graph.getOperation(opId);
                if (op.kind() != grh::ir::OperationKind::kConstant)
                {
                    continue;
                }
                for (const auto resId : op.results())
                {
                    if (!resId.valid())
                    {
                        error(graph, op, "kConstant missing result");
                        failed = true;
                        continue;
                    }
                    if (constants.find(resId) != constants.end())
                    {
                        continue;
                    }
                    auto reportError = [&](const std::string &msg)
                    { this->error(graph, op, msg); failed = true; };
                    grh::ir::Value res = graph.getValue(resId);
                    auto parsed = parseConstValue(graph, op, res, reportError);
                    if (!parsed)
                    {
                        continue;
                    }
                    constants.emplace(resId, *parsed);
                    ConstantKey key = makeConstantKey(res, parsed->value);
                    if (auto it = pool.find(key); it != pool.end())
                    {
                        if (it->second != resId && !res.isInput() && !res.isInout())
                        {
                            auto _t2 = T("replace_users");
                            replaceUsers(graph, resId, it->second, reportError);
                            dedupedConstants = true;
                        }
                        continue;
                    }
                    pool.emplace(std::move(key), resId);
                }
            }
        }
        } // phase1_collect_constants timer scope
        result.changed = result.changed || dedupedConstants;

        std::unordered_set<grh::ir::OperationId, grh::ir::OperationIdHash> foldedOps;
        FoldOptions foldOpts{options_.allowXPropagation};
        size_t totalFolded = 0;

        {
            auto _t = T("phase2_iterative_folding");
            // Per-graph atomic counter for unique symbol generation
            std::unordered_map<const grh::ir::Graph*, std::unique_ptr<std::atomic<int>>> symbolCounters;
            
            for (int iter = 0; iter < options_.maxIterations; ++iter)
            {
                bool iterationChanged = false;

                for (const auto &graphEntry : netlist().graphs())
                {
                    grh::ir::Graph &graph = *graphEntry.second;
                    // Get or create counter for this graph
                    std::atomic<int> *counter = nullptr;
                    auto counterIt = symbolCounters.find(&graph);
                    if (counterIt == symbolCounters.end()) {
                        auto newCounter = std::make_unique<std::atomic<int>>(0);
                        counter = newCounter.get();
                        symbolCounters[&graph] = std::move(newCounter);
                    } else {
                        counter = counterIt->second.get();
                    }
                    
                    std::vector<grh::ir::OperationId> opOrder(graph.operations().begin(), graph.operations().end());
                    std::vector<grh::ir::OperationId> opsToErase;
                    for (const auto opId : opOrder)
                    {
                        const grh::ir::Operation op = graph.getOperation(opId);
                        if (op.kind() == grh::ir::OperationKind::kConstant || !isFoldable(op.kind()))
                        {
                            continue;
                        }
                        if (foldedOps.find(opId) != foldedOps.end())
                        {
                            continue;
                        }
                        {
                            auto _t2 = T("operands_are_constant");
                            if (!operandsAreConstant(op, constants))
                            {
                                continue;
                            }
                        }
                        auto onError = [&](const std::string &msg)
                        { this->error(graph, op, msg); failed = true; };
                        auto onWarning = [&](const std::string &msg)
                        { this->warning(graph, op, msg); };
                        std::optional<std::vector<slang::SVInt>> folded;
                        {
                            auto _t3 = T("fold_operation");
                            folded = foldOperation(graph, op, constants, foldOpts, onError, onWarning);
                        }
                        if (!folded)
                        {
                            continue;
                        }

                        bool createdAllResults = true;
                        ConstantPool &pool = pools[&graph];
                        for (std::size_t idx = 0; idx < folded->size(); ++idx)
                        {
                            const slang::SVInt &sv = (*folded)[idx];
                            const auto resId = op.results()[idx];
                            if (!resId.valid())
                            {
                                error(graph, op, "Result missing during folding");
                                failed = true;
                                createdAllResults = false;
                                continue;
                            }
                            const grh::ir::Value resValue = graph.getValue(resId);
                            grh::ir::ValueId newValue;
                            {
                                auto _t4 = T("create_constant");
                                newValue = createConstant(graph, pool, op, idx, resValue, sv, &timers, counter);
                            }
                            {
                                auto _t5 = T("replace_users");
                                replaceUsers(graph, resId, newValue, [&](const std::string &msg)
                                             { this->error(graph, op, msg); failed = true; });
                            }
                            constants[newValue] = ConstantValue{sv, sv.hasUnknown()};
                            iterationChanged = true;
                        }

                        if (createdAllResults)
                        {
                            foldedOps.insert(opId);
                            opsToErase.push_back(opId);
                            ++totalFolded;
                        }
                    }

                    for (const auto opId : opsToErase)
                    {
                        auto _t6 = T("erase_op");
                        if (!graph.eraseOp(opId))
                        {
                            const grh::ir::Operation op = graph.getOperation(opId);
                            error(graph, op, "Failed to erase folded operation");
                            failed = true;
                        }
                    }
                }

                result.changed = result.changed || iterationChanged;
                if (!iterationChanged)
                {
                    break;
                }
            }
        }

        bool simplifiedSlices = false;
        {
            auto _t = T("phase3_slice_simplify");
            for (const auto &graphEntry : netlist().graphs())
            {
                grh::ir::Graph &graph = *graphEntry.second;
                std::vector<grh::ir::OperationId> opOrder(graph.operations().begin(),
                                                          graph.operations().end());
                std::vector<grh::ir::OperationId> opsToErase;
                for (const auto opId : opOrder)
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (op.kind() != grh::ir::OperationKind::kSliceStatic)
                    {
                        continue;
                    }
                    const auto &operands = op.operands();
                    const auto &results = op.results();
                    if (operands.size() != 1 || results.size() != 1)
                    {
                        continue;
                    }
                    auto sliceStart = getIntAttr(graph, op, "sliceStart");
                    auto sliceEnd = getIntAttr(graph, op, "sliceEnd");
                    if (!sliceStart || !sliceEnd)
                    {
                        continue;
                    }
                    const int64_t low = *sliceStart;
                    const int64_t high = *sliceEnd;
                    if (low < 0 || high < low)
                    {
                        continue;
                    }
                    const grh::ir::ValueId baseValueId = operands[0];
                    if (!baseValueId.valid())
                    {
                        continue;
                    }
                    const grh::ir::Value baseValue = graph.getValue(baseValueId);
                    const grh::ir::OperationId baseDefId = baseValue.definingOp();
                    if (!baseDefId.valid())
                    {
                        continue;
                    }
                    const grh::ir::Operation baseDef = graph.getOperation(baseDefId);
                    if (baseDef.kind() != grh::ir::OperationKind::kConcat)
                    {
                        continue;
                    }
                    const auto &concatOperands = baseDef.operands();
                    if (concatOperands.empty())
                    {
                        continue;
                    }
                    std::vector<int64_t> widths;
                    widths.reserve(concatOperands.size());
                    int64_t totalWidth = 0;
                    bool widthsOk = true;
                    for (const auto operandId : concatOperands)
                    {
                        if (!operandId.valid())
                        {
                            widthsOk = false;
                            break;
                        }
                        const int64_t width = graph.getValue(operandId).width();
                        if (width <= 0)
                        {
                            widthsOk = false;
                            break;
                        }
                        widths.push_back(width);
                        totalWidth += width;
                    }
                    if (!widthsOk || totalWidth <= 0)
                    {
                        continue;
                    }
                    if (high >= totalWidth)
                    {
                        continue;
                    }
                    const grh::ir::ValueId resultId = results[0];
                    if (!resultId.valid())
                    {
                        continue;
                    }
                    const grh::ir::Value resultValue = graph.getValue(resultId);
                    int64_t cursor = totalWidth;
                    for (std::size_t i = 0; i < concatOperands.size(); ++i)
                    {
                        const int64_t width = widths[i];
                        const int64_t hi = cursor - 1;
                        const int64_t lo = cursor - width;
                        cursor = lo;
                        if (lo != low || hi != high)
                        {
                            continue;
                        }
                        const grh::ir::ValueId operandId = concatOperands[i];
                        if (!operandId.valid())
                        {
                            break;
                        }
                        const grh::ir::Value operandValue = graph.getValue(operandId);
                        if (operandValue.width() != resultValue.width() ||
                            operandValue.isSigned() != resultValue.isSigned())
                        {
                            break;
                        }
                        auto onError = [&](const std::string &msg)
                        { this->error(graph, op, msg); failed = true; };
                        replaceUsers(graph, resultId, operandId, onError);
                        opsToErase.push_back(opId);
                        simplifiedSlices = true;
                        break;
                    }
                }

                for (const auto opId : opsToErase)
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (!graph.eraseOp(opId))
                    {
                        error(graph, op, "Failed to erase simplified kSliceStatic op");
                        failed = true;
                    }
                }
            }
        }
        result.changed = result.changed || simplifiedSlices;

        bool removedDeadConstants = false;
        {
            auto _t = T("phase4_dead_code_elim");
            for (const auto &graphEntry : netlist().graphs())
            {
                grh::ir::Graph &graph = *graphEntry.second;
                std::vector<grh::ir::OperationId> deadConstOps;
                for (const auto opId : graph.operations())
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (op.kind() != grh::ir::OperationKind::kConstant)
                    {
                        continue;
                    }
                    const auto &results = op.results();
                    if (results.empty())
                    {
                        error(graph, op, "kConstant missing result");
                        failed = true;
                        continue;
                    }
                    bool live = false;
                    for (const auto resId : results)
                    {
                        if (!resId.valid())
                        {
                            error(graph, op, "kConstant missing result");
                            failed = true;
                            live = true;
                            break;
                        }
                        {
                            auto _t2 = T("is_value_port_bound");
                            if (isValuePortBound(graph, resId))
                            {
                                live = true;
                                break;
                            }
                        }
                        if (!graph.getValue(resId).users().empty())
                        {
                            live = true;
                            break;
                        }
                    }
                    if (!live)
                    {
                        deadConstOps.push_back(opId);
                    }
                }
                for (const auto opId : deadConstOps)
                {
                    const grh::ir::Operation op = graph.getOperation(opId);
                    if (!graph.eraseOp(opId))
                    {
                        error(graph, op, "Failed to erase dead kConstant op");
                        failed = true;
                    }
                    else
                    {
                        removedDeadConstants = true;
                    }
                }
            }
        }
        result.changed = result.changed || removedDeadConstants;

        if (failed)
        {
            result.failed = true;
        }
        
        // Add statistics to timing report (elapsed=0 for stats entries, count holds the value)
        timers.get("_total_folded_ops").elapsed = Timer::Duration::zero();
        timers.get("_total_folded_ops").count = totalFolded;
        timers.report();

        return result;
        }
        catch (const std::exception &ex)
        {
            handleException(ex);
            result.failed = true;
            return result;
        }
    }

} // namespace wolf_sv_parser::transform
