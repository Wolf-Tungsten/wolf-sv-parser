#include "pass/const_fold.hpp"

#include "grh.hpp"

#include "slang/numeric/SVInt.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
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

    // Definitions for forward-declared types from the header
    struct ConstantValue
    {
        slang::SVInt value;
        bool hasUnknown = false;
    };

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

    namespace
    {
        struct FoldOptions
        {
            bool allowXPropagation = false;
        };

        using ConstantStore = std::unordered_map<grh::ir::ValueId, ConstantValue, grh::ir::ValueIdHash>;
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

            // Warn when allowing X propagation and folding produces X/Z
            if (options.allowXPropagation && folded->hasUnknown())
            {
                onWarning("Folding produced X/Z result while allowXPropagation=true");
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
                                        std::atomic<int> &symbolCounter)
        {
            ConstantKey key = makeConstantKey(resultValue, value);
            if (auto it = pool.find(key); it != pool.end())
            {
                return it->second;
            }

            std::ostringstream base;
            base << "__constfold_" << sourceOp.symbolText() << "_" << resultIndex;
            std::string valueName = makeUniqueSymbol(graph, base.str(), /*isOperation=*/false, symbolCounter);

            std::ostringstream opBase;
            opBase << "__constfold_op_" << sourceOp.symbolText() << "_" << resultIndex;
            std::string opName = makeUniqueSymbol(graph, opBase.str(), /*isOperation=*/true, symbolCounter);

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
            graph.setAttr(constOp, "constValue", formatConstLiteral(value));
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

    bool ConstantFoldPass::collectConstants(GraphFoldContext &ctx)
    {
        bool dedupedConstants = false;

        for (const auto opId : ctx.graph.operations())
        {
            const grh::ir::Operation op = ctx.graph.getOperation(opId);
            if (op.kind() != grh::ir::OperationKind::kConstant)
            {
                continue;
            }
            for (const auto resId : op.results())
            {
                if (!resId.valid())
                {
                    error(ctx.graph, op, "kConstant missing result");
                    ctx.failed = true;
                    continue;
                }
                if (ctx.constants.find(resId) != ctx.constants.end())
                {
                    continue;
                }
                auto reportError = [&](const std::string &msg)
                { this->error(ctx.graph, op, msg); ctx.failed = true; };
                grh::ir::Value res = ctx.graph.getValue(resId);
                auto parsed = parseConstValue(ctx.graph, op, res, reportError);
                if (!parsed)
                {
                    continue;
                }
                ctx.constants.emplace(resId, *parsed);
                ConstantKey key = makeConstantKey(res, parsed->value);
                if (auto it = ctx.pool->find(key); it != ctx.pool->end())
                {
                    if (it->second != resId && !res.isInput() && !res.isInout())
                    {
                        replaceUsers(ctx.graph, resId, it->second, reportError);
                        dedupedConstants = true;
                    }
                    continue;
                }
                ctx.pool->emplace(std::move(key), resId);
            }
        }

        return dedupedConstants;
    }

    bool ConstantFoldPass::iterativeFolding(GraphFoldContext &ctx)
    {
        bool anyChanged = false;
        size_t totalFolded = 0;
        FoldOptions foldOpts{options_.allowXPropagation};

        for (int iter = 0; iter < options_.maxIterations; ++iter)
        {
            bool iterationChanged = false;
            std::vector<grh::ir::OperationId> opOrder(ctx.graph.operations().begin(), ctx.graph.operations().end());
            std::vector<grh::ir::OperationId> opsToErase;

            for (const auto opId : opOrder)
            {
                const grh::ir::Operation op = ctx.graph.getOperation(opId);
                if (op.kind() == grh::ir::OperationKind::kConstant || !isFoldable(op.kind()))
                {
                    continue;
                }
                if (ctx.foldedOps.find(opId) != ctx.foldedOps.end())
                {
                    continue;
                }
                if (!operandsAreConstant(op, ctx.constants))
                {
                    continue;
                }

                auto onError = [&](const std::string &msg)
                { this->error(ctx.graph, op, msg); ctx.failed = true; };
                auto onWarning = [&](const std::string &msg)
                { this->warning(ctx.graph, op, msg); };

                std::optional<std::vector<slang::SVInt>> folded = foldOperation(ctx.graph, op, ctx.constants, foldOpts, onError, onWarning);
                if (!folded)
                {
                    continue;
                }

                bool createdAllResults = true;
                for (std::size_t idx = 0; idx < folded->size(); ++idx)
                {
                    const slang::SVInt &sv = (*folded)[idx];
                    const auto resId = op.results()[idx];
                    if (!resId.valid())
                    {
                        error(ctx.graph, op, "Result missing during folding");
                        ctx.failed = true;
                        createdAllResults = false;
                        continue;
                    }
                    const grh::ir::Value resValue = ctx.graph.getValue(resId);
                    grh::ir::ValueId newValue = createConstant(ctx.graph, *ctx.pool, op, idx, resValue, sv, ctx.symbolCounter);
                    replaceUsers(ctx.graph, resId, newValue, [&](const std::string &msg)
                                 { this->error(ctx.graph, op, msg); ctx.failed = true; });
                    ctx.constants[newValue] = ConstantValue{sv, sv.hasUnknown()};
                    iterationChanged = true;
                }

                if (createdAllResults)
                {
                    ctx.foldedOps.insert(opId);
                    opsToErase.push_back(opId);
                    ++totalFolded;
                }
            }

            for (const auto opId : opsToErase)
            {
                if (!ctx.graph.eraseOp(opId))
                {
                    const grh::ir::Operation op = ctx.graph.getOperation(opId);
                    error(ctx.graph, op, "Failed to erase folded operation");
                    ctx.failed = true;
                }
            }

            anyChanged = anyChanged || iterationChanged;
            if (!iterationChanged)
            {
                break;
            }
        }

        return anyChanged;
    }

    bool ConstantFoldPass::simplifySlices(GraphFoldContext &ctx)
    {
        bool simplifiedSlices = false;
        std::vector<grh::ir::OperationId> opOrder(ctx.graph.operations().begin(), ctx.graph.operations().end());
        std::vector<grh::ir::OperationId> opsToErase;

        for (const auto opId : opOrder)
        {
            const grh::ir::Operation op = ctx.graph.getOperation(opId);
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
            auto sliceStart = getIntAttr(ctx.graph, op, "sliceStart");
            auto sliceEnd = getIntAttr(ctx.graph, op, "sliceEnd");
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
            const grh::ir::Value baseValue = ctx.graph.getValue(baseValueId);
            const grh::ir::OperationId baseDefId = baseValue.definingOp();
            if (!baseDefId.valid())
            {
                continue;
            }
            const grh::ir::Operation baseDef = ctx.graph.getOperation(baseDefId);
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
                const int64_t width = ctx.graph.getValue(operandId).width();
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
            const grh::ir::Value resultValue = ctx.graph.getValue(resultId);
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
                const grh::ir::Value operandValue = ctx.graph.getValue(operandId);
                if (operandValue.width() != resultValue.width() ||
                    operandValue.isSigned() != resultValue.isSigned())
                {
                    break;
                }
                auto onError = [&](const std::string &msg)
                { this->error(ctx.graph, op, msg); ctx.failed = true; };
                replaceUsers(ctx.graph, resultId, operandId, onError);
                opsToErase.push_back(opId);
                simplifiedSlices = true;
                break;
            }
        }

        for (const auto opId : opsToErase)
        {
            const grh::ir::Operation op = ctx.graph.getOperation(opId);
            if (!ctx.graph.eraseOp(opId))
            {
                error(ctx.graph, op, "Failed to erase simplified kSliceStatic op");
                ctx.failed = true;
            }
        }

        return simplifiedSlices;
    }

    bool ConstantFoldPass::eliminateDeadConstants(GraphFoldContext &ctx)
    {
        bool removedDeadConstants = false;
        std::vector<grh::ir::OperationId> deadConstOps;

        for (const auto opId : ctx.graph.operations())
        {
            const grh::ir::Operation op = ctx.graph.getOperation(opId);
            if (op.kind() != grh::ir::OperationKind::kConstant)
            {
                continue;
            }
            const auto &results = op.results();
            if (results.empty())
            {
                error(ctx.graph, op, "kConstant missing result");
                ctx.failed = true;
                continue;
            }
            bool live = false;
            for (const auto resId : results)
            {
                if (!resId.valid())
                {
                    error(ctx.graph, op, "kConstant missing result");
                    ctx.failed = true;
                    live = true;
                    break;
                }
                if (isValuePortBound(ctx.graph, resId))
                {
                    live = true;
                    break;
                }
                if (!ctx.graph.getValue(resId).users().empty())
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
            const grh::ir::Operation op = ctx.graph.getOperation(opId);
            if (!ctx.graph.eraseOp(opId))
            {
                error(ctx.graph, op, "Failed to erase dead kConstant op");
                ctx.failed = true;
            }
            else
            {
                removedDeadConstants = true;
            }
        }

        return removedDeadConstants;
    }

    bool ConstantFoldPass::simplifyUnsignedComparisons(GraphFoldContext &ctx)
    {
        bool simplified = false;
        std::vector<grh::ir::OperationId> opOrder(ctx.graph.operations().begin(), ctx.graph.operations().end());
        std::vector<grh::ir::OperationId> opsToErase;

        for (const auto opId : opOrder)
        {
            const grh::ir::Operation op = ctx.graph.getOperation(opId);
            const auto kind = op.kind();
            
            // Handle unsigned >= 0 (always true) and unsigned <= max (always true)
            bool isGe = (kind == grh::ir::OperationKind::kGe);
            bool isLe = (kind == grh::ir::OperationKind::kLe);
            
            if (!isGe && !isLe)
            {
                continue;
            }
            
            const auto &operands = op.operands();
            const auto &results = op.results();
            if (operands.size() < 2 || results.empty())
            {
                continue;
            }
            
            const grh::ir::ValueId lhsId = operands[0];
            const grh::ir::ValueId rhsId = operands[1];
            const grh::ir::ValueId resultId = results[0];
            
            if (!lhsId.valid() || !rhsId.valid() || !resultId.valid())
            {
                continue;
            }
            
            const grh::ir::Value lhsValue = ctx.graph.getValue(lhsId);
            const grh::ir::Value rhsValue = ctx.graph.getValue(rhsId);
            
            // Check for unsigned >= 0 (always true for unsigned)
            // LHS must be unsigned, RHS must be constant 0
            if (isGe && !lhsValue.isSigned())
            {
                auto rhsConstIt = ctx.constants.find(rhsId);
                if (rhsConstIt != ctx.constants.end())
                {
                    const auto &rhsSv = rhsConstIt->second.value;
                    // Check if RHS is zero
                    if (rhsSv.getBitWidth() > 0 && rhsSv.getActiveBits() == 0)
                    {
                        // Replace with constant 1'b1
                        slang::SVInt trueValue(1, 1, false);
                        auto onError = [&](const std::string &msg)
                        { this->error(ctx.graph, op, msg); ctx.failed = true; };
                        grh::ir::ValueId newValue = createConstant(ctx.graph, *ctx.pool, op, 0, 
                            ctx.graph.getValue(resultId), trueValue, ctx.symbolCounter);
                        replaceUsers(ctx.graph, resultId, newValue, onError);
                        ctx.constants[newValue] = ConstantValue{trueValue, false};
                        opsToErase.push_back(opId);
                        simplified = true;
                        continue;
                    }
                }
            }
            
            // Check for unsigned <= max_value (always true for unsigned)
            // LHS must be unsigned, RHS must be constant with all 1s at LHS width
            if (isLe && !lhsValue.isSigned())
            {
                auto rhsConstIt = ctx.constants.find(rhsId);
                if (rhsConstIt != ctx.constants.end())
                {
                    const auto &rhsSv = rhsConstIt->second.value;
                    int64_t lhsWidth = lhsValue.width();
                    
                    if (lhsWidth > 0 && rhsSv.getBitWidth() > 0)
                    {
                        // Check if RHS is all 1s up to LHS width
                        // Resize RHS to LHS width and check if all bits are 1
                        slang::SVInt resizedRhs = rhsSv.resize(static_cast<slang::bitwidth_t>(lhsWidth));
                        bool allOnes = true;
                        for (int i = 0; i < lhsWidth; ++i)
                        {
                            if (!resizedRhs[i])
                            {
                                allOnes = false;
                                break;
                            }
                        }
                        
                        if (allOnes)
                        {
                            // Replace with constant 1'b1
                            slang::SVInt trueValue(1, 1, false);
                            auto onError = [&](const std::string &msg)
                            { this->error(ctx.graph, op, msg); ctx.failed = true; };
                            grh::ir::ValueId newValue = createConstant(ctx.graph, *ctx.pool, op, 0,
                                ctx.graph.getValue(resultId), trueValue, ctx.symbolCounter);
                            replaceUsers(ctx.graph, resultId, newValue, onError);
                            ctx.constants[newValue] = ConstantValue{trueValue, false};
                            opsToErase.push_back(opId);
                            simplified = true;
                        }
                    }
                }
            }
        }
        
        for (const auto opId : opsToErase)
        {
            if (!ctx.graph.eraseOp(opId))
            {
                const grh::ir::Operation op = ctx.graph.getOperation(opId);
                error(ctx.graph, op, "Failed to erase simplified unsigned comparison op");
                ctx.failed = true;
            }
        }
        
        return simplified;
    }

    bool ConstantFoldPass::processSingleGraph(GraphFoldContext &ctx)
    {
        bool graphChanged = false;

        // Phase 1: Collect constants from existing kConstant ops and dedupe
        graphChanged = collectConstants(ctx) || graphChanged;

        // Phase 2: Iterative folding until convergence or max iterations
        graphChanged = iterativeFolding(ctx) || graphChanged;

        // Phase 3: Simplify slice operations
        graphChanged = simplifySlices(ctx) || graphChanged;

        // Phase 4: Eliminate dead constants
        graphChanged = eliminateDeadConstants(ctx) || graphChanged;

        // Phase 5: Simplify unsigned comparisons (e.g., x >= 0, x <= MAX)
        graphChanged = simplifyUnsignedComparisons(ctx) || graphChanged;

        return graphChanged;
    }

    PassResult ConstantFoldPass::run()
    {
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
            // Iterate over all graphs
            for (const auto &graphEntry : netlist().graphs())
            {
                grh::ir::Graph &graph = *graphEntry.second;

                // Create per-graph context with fresh state
                GraphFoldContext ctx{
                    graph,
                    constants,  // Shared across graphs (constants can reference values from other graphs)
                    std::make_unique<ConstantPool>(),  // Fresh constant pool for this graph
                    {},         // Fresh symbol counter
                    {},         // Fresh folded operations set
                    failed
                };

                // Process the graph
                bool graphChanged = processSingleGraph(ctx);
                result.changed = result.changed || graphChanged;
            }

            if (failed)
            {
                result.failed = true;
            }

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
