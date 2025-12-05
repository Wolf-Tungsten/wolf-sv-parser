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

        using ConstantStore = std::unordered_map<const grh::Value *, ConstantValue>;

        bool isFoldable(grh::OperationKind kind)
        {
            switch (kind)
            {
            case grh::OperationKind::kAdd:
            case grh::OperationKind::kSub:
            case grh::OperationKind::kMul:
            case grh::OperationKind::kDiv:
            case grh::OperationKind::kMod:
            case grh::OperationKind::kEq:
            case grh::OperationKind::kNe:
            case grh::OperationKind::kLt:
            case grh::OperationKind::kLe:
            case grh::OperationKind::kGt:
            case grh::OperationKind::kGe:
            case grh::OperationKind::kAnd:
            case grh::OperationKind::kOr:
            case grh::OperationKind::kXor:
            case grh::OperationKind::kXnor:
            case grh::OperationKind::kNot:
            case grh::OperationKind::kLogicAnd:
            case grh::OperationKind::kLogicOr:
            case grh::OperationKind::kLogicNot:
            case grh::OperationKind::kReduceAnd:
            case grh::OperationKind::kReduceOr:
            case grh::OperationKind::kReduceXor:
            case grh::OperationKind::kReduceNor:
            case grh::OperationKind::kReduceNand:
            case grh::OperationKind::kReduceXnor:
            case grh::OperationKind::kShl:
            case grh::OperationKind::kLShr:
            case grh::OperationKind::kAShr:
            case grh::OperationKind::kMux:
            case grh::OperationKind::kAssign:
            case grh::OperationKind::kConcat:
            case grh::OperationKind::kReplicate:
            case grh::OperationKind::kSliceStatic:
            case grh::OperationKind::kSliceDynamic:
            case grh::OperationKind::kSliceArray:
                return true;
            default:
                return false;
            }
        }

        std::optional<std::string> getStringAttr(const grh::Operation &op, std::string_view key)
        {
            const auto &attrs = op.attributes();
            auto it = attrs.find(std::string(key));
            if (it == attrs.end())
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<std::string>(&it->second))
            {
                return *value;
            }
            return std::nullopt;
        }

        std::optional<int64_t> getIntAttr(const grh::Operation &op, std::string_view key)
        {
            const auto &attrs = op.attributes();
            auto it = attrs.find(std::string(key));
            if (it == attrs.end())
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<int64_t>(&it->second))
            {
                return *value;
            }
            return std::nullopt;
        }

        std::optional<ConstantValue> parseConstLiteral(const grh::Graph &graph, const grh::Operation &op, const grh::Value &value, const std::string &literal, const std::function<void(std::string)> &onError)
        {
            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(literal);
                if (value.width() <= 0)
                {
                    onError("Value width must be positive for constant propagation: " + value.symbol());
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

        std::optional<ConstantValue> parseConstValue(const grh::Graph &graph, const grh::Operation &op, const grh::Value &value, const std::function<void(std::string)> &onError)
        {
            auto literalOpt = getStringAttr(op, "constValue");
            if (!literalOpt)
            {
                onError("kConstant missing constValue attribute");
                return std::nullopt;
            }
            return parseConstLiteral(graph, op, value, *literalOpt, onError);
        }

        bool operandsAreConstant(const grh::Operation &op, const ConstantStore &store)
        {
            const auto operands = op.operands();
            for (std::size_t i = 0; i < operands.size(); ++i)
            {
                const grh::Value *val = operands[i];
                if (!val)
                {
                    return false;
                }
                if (store.find(val) == store.end())
                {
                    return false;
                }
            }
            return true;
        }

        slang::SVInt normalizeToValue(const grh::Value &value, const slang::SVInt &raw)
        {
            slang::SVInt adjusted = raw;
            adjusted.setSigned(value.isSigned());
            adjusted = adjusted.resize(static_cast<slang::bitwidth_t>(value.width()));
            adjusted.setSigned(value.isSigned());
            return adjusted;
        }

        std::optional<slang::SVInt> foldBinary(const grh::Operation &op, grh::OperationKind kind, const std::vector<slang::SVInt> &operands)
        {
            const slang::SVInt &lhs = operands[0];
            const slang::SVInt &rhs = operands[1];
            switch (kind)
            {
            case grh::OperationKind::kAdd:
                return lhs + rhs;
            case grh::OperationKind::kSub:
                return lhs - rhs;
            case grh::OperationKind::kMul:
                return lhs * rhs;
            case grh::OperationKind::kDiv:
                return lhs / rhs;
            case grh::OperationKind::kMod:
                return lhs % rhs;
            case grh::OperationKind::kAnd:
                return lhs & rhs;
            case grh::OperationKind::kOr:
                return lhs | rhs;
            case grh::OperationKind::kXor:
                return lhs ^ rhs;
            case grh::OperationKind::kXnor:
                return ~(lhs ^ rhs);
            case grh::OperationKind::kEq:
                return slang::SVInt(lhs == rhs);
            case grh::OperationKind::kNe:
                return slang::SVInt(lhs != rhs);
            case grh::OperationKind::kLt:
                return slang::SVInt(lhs < rhs);
            case grh::OperationKind::kLe:
                return slang::SVInt(lhs <= rhs);
            case grh::OperationKind::kGt:
                return slang::SVInt(lhs > rhs);
            case grh::OperationKind::kGe:
                return slang::SVInt(lhs >= rhs);
            case grh::OperationKind::kLogicAnd:
                return slang::SVInt(lhs && rhs);
            case grh::OperationKind::kLogicOr:
                return slang::SVInt(lhs || rhs);
            case grh::OperationKind::kShl:
                return lhs.shl(rhs);
            case grh::OperationKind::kLShr:
                return lhs.lshr(rhs);
            case grh::OperationKind::kAShr:
                return lhs.ashr(rhs);
            default:
                (void)op;
                return std::nullopt;
            }
        }

        std::optional<slang::SVInt> foldUnary(const grh::Operation &op, grh::OperationKind kind, const slang::SVInt &operand)
        {
            switch (kind)
            {
            case grh::OperationKind::kNot:
                return ~operand;
            case grh::OperationKind::kLogicNot:
                return slang::SVInt(!operand);
            case grh::OperationKind::kReduceAnd:
                return slang::SVInt(operand.reductionAnd());
            case grh::OperationKind::kReduceOr:
                return slang::SVInt(operand.reductionOr());
            case grh::OperationKind::kReduceXor:
                return slang::SVInt(operand.reductionXor());
            case grh::OperationKind::kReduceNor:
                return slang::SVInt(!operand.reductionOr());
            case grh::OperationKind::kReduceNand:
                return slang::SVInt(!operand.reductionAnd());
            case grh::OperationKind::kReduceXnor:
                return slang::SVInt(!operand.reductionXor());
            default:
                (void)op;
                return std::nullopt;
            }
        }

        std::optional<std::vector<slang::SVInt>> foldOperation(const grh::Graph &graph, const grh::Operation &op, const ConstantStore &store, const FoldOptions options, const std::function<void(std::string)> &onError, const std::function<void(std::string)> &onWarning)
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
                const grh::Value *val = op.operands()[i];
                if (!val)
                {
                    onError("Operand missing during constant propagation");
                    return std::nullopt;
                }
                auto it = store.find(val);
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
            case grh::OperationKind::kAdd:
            case grh::OperationKind::kSub:
            case grh::OperationKind::kMul:
            case grh::OperationKind::kDiv:
            case grh::OperationKind::kMod:
            case grh::OperationKind::kEq:
            case grh::OperationKind::kNe:
            case grh::OperationKind::kLt:
            case grh::OperationKind::kLe:
            case grh::OperationKind::kGt:
            case grh::OperationKind::kGe:
            case grh::OperationKind::kAnd:
            case grh::OperationKind::kOr:
            case grh::OperationKind::kXor:
            case grh::OperationKind::kXnor:
            case grh::OperationKind::kLogicAnd:
            case grh::OperationKind::kLogicOr:
            case grh::OperationKind::kShl:
            case grh::OperationKind::kLShr:
            case grh::OperationKind::kAShr:
                folded = foldBinary(op, op.kind(), operands);
                break;
            case grh::OperationKind::kNot:
            case grh::OperationKind::kLogicNot:
            case grh::OperationKind::kReduceAnd:
            case grh::OperationKind::kReduceOr:
            case grh::OperationKind::kReduceXor:
            case grh::OperationKind::kReduceNor:
            case grh::OperationKind::kReduceNand:
            case grh::OperationKind::kReduceXnor:
                folded = foldUnary(op, op.kind(), operands[0]);
                break;
            case grh::OperationKind::kAssign:
                folded = operands[0];
                break;
            case grh::OperationKind::kConcat:
                folded = slang::SVInt::concat(operands);
                break;
            case grh::OperationKind::kReplicate:
            {
                auto repOpt = getIntAttr(op, "rep");
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
            case grh::OperationKind::kMux:
                folded = slang::SVInt::conditional(operands[0], operands[1], operands[2]);
                break;
            case grh::OperationKind::kSliceStatic:
            {
                auto startOpt = getIntAttr(op, "sliceStart");
                auto endOpt = getIntAttr(op, "sliceEnd");
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
            case grh::OperationKind::kSliceDynamic:
            case grh::OperationKind::kSliceArray:
            {
                auto widthOpt = getIntAttr(op, "sliceWidth");
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
                const grh::Value *res = op.results()[i];
                if (!res)
                {
                    onError("Result missing during constant propagation");
                    return std::nullopt;
                }
                results.push_back(normalizeToValue(*res, *folded));
            }
            return results;
        }

        std::string makeUniqueSymbol(const grh::Graph &graph, std::string base, bool isOperation)
        {
            std::string candidate = std::move(base);
            int counter = 0;
            const auto exists = [&](const std::string &symbol)
            {
                return isOperation ? graph.findOperation(symbol) != nullptr : graph.findValue(symbol) != nullptr;
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

        grh::Value &createConstant(grh::Graph &graph, const grh::Operation &sourceOp, std::size_t resultIndex, const slang::SVInt &value)
        {
            std::ostringstream base;
            base << "__constfold_" << sourceOp.symbol() << "_" << resultIndex;
            std::string valueName = makeUniqueSymbol(graph, base.str(), /*isOperation=*/false);

            std::ostringstream opBase;
            opBase << "__constfold_op_" << sourceOp.symbol() << "_" << resultIndex;
            std::string opName = makeUniqueSymbol(graph, opBase.str(), /*isOperation=*/true);

            grh::Value &newValue = graph.createValue(valueName, static_cast<int64_t>(value.getBitWidth()), value.isSigned());
            grh::Operation &constOp = graph.createOperation(grh::OperationKind::kConstant, opName);
            constOp.addResult(newValue);
            constOp.setAttribute("constValue", formatConstLiteral(value));
            return newValue;
        }

        void replaceUsers(grh::Graph &graph, grh::Value &oldValue, grh::Value &newValue, const std::function<void(std::string)> &onError)
        {
            std::vector<grh::ValueUser> users = oldValue.users();
            for (const auto &user : users)
            {
                grh::Operation *op = user.operationPtr ? user.operationPtr : graph.findOperation(user.operationSymbol);
                if (!op)
                {
                    continue;
                }
                if (user.operandIndex >= op->operands().size())
                {
                    std::ostringstream oss;
                    oss << "Operand index " << user.operandIndex << " out of range for op " << op->symbol();
                    onError(oss.str());
                    continue;
                }
                try
                {
                    op->replaceOperand(user.operandIndex, newValue);
                }
                catch (const std::exception &ex)
                {
                    std::ostringstream oss;
                    oss << "Failed to replace operand " << user.operandIndex << " in op " << op->symbol() << ": " << ex.what();
                    onError(oss.str());
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
            const grh::Graph &graph = *graphEntry.second;
            for (const auto &opSymbol : graph.operationOrder())
            {
                const grh::Operation &op = graph.getOperation(opSymbol);
                if (op.kind() != grh::OperationKind::kConstant)
                {
                    continue;
                }
                for (std::size_t i = 0; i < op.results().size(); ++i)
                {
                    const grh::Value *res = op.results()[i];
                    if (!res)
                    {
                        error(graph, op, "kConstant missing result");
                        failed = true;
                        continue;
                    }
                    if (constants.find(res) != constants.end())
                    {
                        continue;
                    }
                    auto reportError = [&](const std::string &msg)
                    { this->error(graph, op, msg); failed = true; };
                    auto parsed = parseConstValue(graph, op, *res, reportError);
                    if (!parsed)
                    {
                        continue;
                    }
                    constants.emplace(res, *parsed);
                }
            }
        }

        std::unordered_set<std::string> foldedOps;
        FoldOptions foldOpts{options_.allowXPropagation};

        for (int iter = 0; iter < options_.maxIterations; ++iter)
        {
            bool iterationChanged = false;

            for (const auto &graphEntry : netlist().graphs())
            {
                grh::Graph &graph = *graphEntry.second;
                std::vector<std::string> opOrder = graph.operationOrder();
                std::vector<std::string> opsToErase;
                for (const auto &opSymbol : opOrder)
                {
                    grh::Operation &op = graph.getOperation(opSymbol);
                    if (op.kind() == grh::OperationKind::kConstant || !isFoldable(op.kind()))
                    {
                        continue;
                    }
                    if (foldedOps.find(op.symbol()) != foldedOps.end())
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
                        grh::Value *res = op.results()[idx];
                        if (!res)
                        {
                            error(graph, op, "Result missing during folding");
                            failed = true;
                            createdAllResults = false;
                            continue;
                        }
                        grh::Value &newValue = createConstant(graph, op, idx, sv);
                        replaceUsers(graph, *res, newValue, [&](const std::string &msg)
                                     { this->error(graph, op, msg); failed = true; });
                        graph.replaceOutputValue(*res, newValue);
                        constants[&newValue] = ConstantValue{sv, sv.hasUnknown()};
                        iterationChanged = true;
                    }

                    if (createdAllResults)
                    {
                        foldedOps.insert(op.symbol());
                        opsToErase.push_back(op.symbol());
                    }
                }

                for (const auto &symbol : opsToErase)
                {
                    graph.removeOperation(symbol);
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
