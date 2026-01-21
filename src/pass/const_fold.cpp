#include "pass/const_fold.hpp"

#include "grh.hpp"

#include "slang/numeric/SVInt.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolf_sv::transform
{

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

        std::string makeUniqueSymbol(const grh::ir::Graph &graph, std::string base, bool isOperation)
        {
            std::string candidate = std::move(base);
            int counter = 0;
            const auto exists = [&](const std::string &symbol)
            {
                return isOperation ? graph.findOperation(symbol).valid() : graph.findValue(symbol).valid();
            };
            while (exists(candidate))
            {
                ++counter;
                candidate.append("_").append(std::to_string(counter));
            }
            return candidate;
        }

        std::string formatConstLiteral(const slang::SVInt &value)
        {
            const slang::bitwidth_t width = value.getBitWidth();
            return value.toString(slang::LiteralBase::Hex, true, width);
        }

        grh::ir::ValueId createConstant(grh::ir::Graph &graph, const grh::ir::Operation &sourceOp, std::size_t resultIndex, const slang::SVInt &value)
        {
            std::ostringstream base;
            base << "__constfold_" << sourceOp.symbolText() << "_" << resultIndex;
            std::string valueName = makeUniqueSymbol(graph, base.str(), /*isOperation=*/false);

            std::ostringstream opBase;
            opBase << "__constfold_op_" << sourceOp.symbolText() << "_" << resultIndex;
            std::string opName = makeUniqueSymbol(graph, opBase.str(), /*isOperation=*/true);

            const grh::ir::SymbolId valueSym = graph.internSymbol(valueName);
            const grh::ir::SymbolId opSym = graph.internSymbol(opName);
            const grh::ir::ValueId newValue = graph.createValue(valueSym, static_cast<int32_t>(value.getBitWidth()), value.isSigned());
            const grh::ir::OperationId constOp = graph.createOperation(grh::ir::OperationKind::kConstant, opSym);
            graph.addResult(constOp, newValue);
            graph.setAttr(constOp, "constValue", formatConstLiteral(value));
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

        // Seed constant table from existing kConstant ops.
        for (const auto &graphEntry : netlist().graphs())
        {
            const grh::ir::Graph &graph = *graphEntry.second;
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
                }
            }
        }

        std::unordered_set<grh::ir::OperationId, grh::ir::OperationIdHash> foldedOps;
        FoldOptions foldOpts{options_.allowXPropagation};

        for (int iter = 0; iter < options_.maxIterations; ++iter)
        {
            bool iterationChanged = false;

            for (const auto &graphEntry : netlist().graphs())
            {
                grh::ir::Graph &graph = *graphEntry.second;
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
                    if (!operandsAreConstant(op, constants))
                    {
                        continue;
                    }
                    auto onError = [&](const std::string &msg)
                    { this->error(graph, op, msg); failed = true; };
                    auto onWarning = [&](const std::string &msg)
                    { this->warning(graph, op, msg); };
                    auto folded = foldOperation(graph, op, constants, foldOpts, onError, onWarning);
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
                            error(graph, op, "Result missing during folding");
                            failed = true;
                            createdAllResults = false;
                            continue;
                        }
                        const grh::ir::ValueId newValue = createConstant(graph, op, idx, sv);
                        replaceUsers(graph, resId, newValue, [&](const std::string &msg)
                                     { this->error(graph, op, msg); failed = true; });
                        constants[newValue] = ConstantValue{sv, sv.hasUnknown()};
                        iterationChanged = true;
                    }

                    if (createdAllResults)
                    {
                        foldedOps.insert(opId);
                        opsToErase.push_back(opId);
                    }
                }

                for (const auto opId : opsToErase)
                {
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

} // namespace wolf_sv::transform
