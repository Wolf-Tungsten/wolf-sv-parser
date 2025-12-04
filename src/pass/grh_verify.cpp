#include "pass/grh_verify.hpp"

#include "grh.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
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

        enum class AttrValueKind
        {
            Bool,
            Int,
            Double,
            String,
            BoolArray,
            IntArray,
            DoubleArray,
            StringArray,
            Unknown
        };

        AttrValueKind classifyAttr(const grh::AttributeValue &value)
        {
            if (std::holds_alternative<bool>(value))
            {
                return AttrValueKind::Bool;
            }
            if (std::holds_alternative<int64_t>(value))
            {
                return AttrValueKind::Int;
            }
            if (std::holds_alternative<double>(value))
            {
                return AttrValueKind::Double;
            }
            if (std::holds_alternative<std::string>(value))
            {
                return AttrValueKind::String;
            }
            if (std::holds_alternative<std::vector<bool>>(value))
            {
                return AttrValueKind::BoolArray;
            }
            if (std::holds_alternative<std::vector<int64_t>>(value))
            {
                return AttrValueKind::IntArray;
            }
            if (std::holds_alternative<std::vector<double>>(value))
            {
                return AttrValueKind::DoubleArray;
            }
            if (std::holds_alternative<std::vector<std::string>>(value))
            {
                return AttrValueKind::StringArray;
            }
            return AttrValueKind::Unknown;
        }

        struct AttributeRule
        {
            std::string name;
            AttrValueKind kind = AttrValueKind::Unknown;
            std::vector<std::string> allowedStrings;
            bool optional = false;
        };

        struct Range
        {
            std::size_t min = 0;
            std::size_t max = std::numeric_limits<std::size_t>::max();
        };

        struct OperationSpec
        {
            Range operands;
            Range results;
            std::vector<AttributeRule> required;
            std::vector<AttributeRule> optional;
        };

        Range exact(std::size_t count)
        {
            return Range{count, count};
        }

        AttributeRule makeStringAttr(std::string name, std::vector<std::string> allowed, bool optional = false)
        {
            return AttributeRule{std::move(name), AttrValueKind::String, std::move(allowed), optional};
        }

        AttributeRule makeIntAttr(std::string name, bool optional = false)
        {
            return AttributeRule{std::move(name), AttrValueKind::Int, {}, optional};
        }

        AttributeRule makeBoolAttr(std::string name, bool optional = false)
        {
            return AttributeRule{std::move(name), AttrValueKind::Bool, {}, optional};
        }

        AttributeRule makeStringArrayAttr(std::string name, bool optional = false)
        {
            return AttributeRule{std::move(name), AttrValueKind::StringArray, {}, optional};
        }

        AttributeRule makeIntArrayAttr(std::string name, bool optional = false)
        {
            return AttributeRule{std::move(name), AttrValueKind::IntArray, {}, optional};
        }

        OperationSpec makeSpec(Range operands, Range results, std::vector<AttributeRule> required, std::vector<AttributeRule> optional = {})
        {
            OperationSpec spec;
            spec.operands = operands;
            spec.results = results;
            spec.required = std::move(required);
            spec.optional = std::move(optional);
            return spec;
        }

        const std::unordered_map<grh::OperationKind, OperationSpec> &operationSpecs()
        {
            static const std::unordered_map<grh::OperationKind, OperationSpec> specs = []()
            {
                std::unordered_map<grh::OperationKind, OperationSpec> map;

                OperationSpec binary = makeSpec(exact(2), exact(1), {});
                OperationSpec unary = makeSpec(exact(1), exact(1), {});

                const std::vector binaryKinds = {
                    grh::OperationKind::kAdd, grh::OperationKind::kSub, grh::OperationKind::kMul, grh::OperationKind::kDiv, grh::OperationKind::kMod,
                    grh::OperationKind::kEq, grh::OperationKind::kNe, grh::OperationKind::kLt, grh::OperationKind::kLe, grh::OperationKind::kGt, grh::OperationKind::kGe,
                    grh::OperationKind::kAnd, grh::OperationKind::kOr, grh::OperationKind::kXor, grh::OperationKind::kXnor, grh::OperationKind::kLogicAnd, grh::OperationKind::kLogicOr,
                    grh::OperationKind::kShl, grh::OperationKind::kLShr, grh::OperationKind::kAShr};
                for (auto kind : binaryKinds)
                {
                    map.emplace(kind, binary);
                }

                const std::vector unaryKinds = {
                    grh::OperationKind::kNot, grh::OperationKind::kLogicNot, grh::OperationKind::kReduceAnd, grh::OperationKind::kReduceOr,
                    grh::OperationKind::kReduceXor, grh::OperationKind::kReduceNor, grh::OperationKind::kReduceNand, grh::OperationKind::kReduceXnor};
                for (auto kind : unaryKinds)
                {
                    map.emplace(kind, unary);
                }

                map.emplace(grh::OperationKind::kConstant, makeSpec(exact(0), exact(1), {makeStringAttr("constValue", {})}));
                map.emplace(grh::OperationKind::kMux, makeSpec(exact(3), exact(1), {}));
                map.emplace(grh::OperationKind::kAssign, makeSpec(exact(1), exact(1), {}));
                map.emplace(grh::OperationKind::kConcat, makeSpec(Range{2, std::numeric_limits<std::size_t>::max()}, exact(1), {}));
                map.emplace(grh::OperationKind::kReplicate, makeSpec(exact(1), exact(1), {makeIntAttr("rep")}));
                map.emplace(grh::OperationKind::kSliceStatic, makeSpec(exact(1), exact(1), {makeIntAttr("sliceStart"), makeIntAttr("sliceEnd")}));
                map.emplace(grh::OperationKind::kSliceDynamic, makeSpec(exact(2), exact(1), {makeIntAttr("sliceWidth")}));
                map.emplace(grh::OperationKind::kSliceArray, makeSpec(exact(2), exact(1), {makeIntAttr("sliceWidth")}));

                map.emplace(grh::OperationKind::kLatch, makeSpec(exact(2), exact(1), {makeStringAttr("enLevel", {"high", "low"})}));
                map.emplace(grh::OperationKind::kLatchArst, makeSpec(exact(4), exact(1), {makeStringAttr("enLevel", {"high", "low"}), makeStringAttr("rstPolarity", {"high", "low"})}));

                auto clkPolarity = makeStringAttr("clkPolarity", {"posedge", "negedge"});
                auto enLevel = makeStringAttr("enLevel", {"high", "low"});
                auto rstPolarity = makeStringAttr("rstPolarity", {"high", "low"});
                map.emplace(grh::OperationKind::kRegister, makeSpec(exact(2), exact(1), {clkPolarity}));
                map.emplace(grh::OperationKind::kRegisterEn, makeSpec(exact(3), exact(1), {clkPolarity, enLevel}));
                map.emplace(grh::OperationKind::kRegisterRst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity}));
                map.emplace(grh::OperationKind::kRegisterEnRst, makeSpec(exact(5), exact(1), {clkPolarity, rstPolarity, enLevel}));
                map.emplace(grh::OperationKind::kRegisterArst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity}));
                map.emplace(grh::OperationKind::kRegisterEnArst, makeSpec(exact(5), exact(1), {clkPolarity, rstPolarity, enLevel}));

                map.emplace(grh::OperationKind::kMemory, makeSpec(exact(0), exact(0), {makeIntAttr("width"), makeIntAttr("row"), makeBoolAttr("isSigned")}));
                map.emplace(grh::OperationKind::kMemoryAsyncReadPort, makeSpec(exact(1), exact(1), {makeStringAttr("memSymbol", {})}));
                map.emplace(grh::OperationKind::kMemorySyncReadPort, makeSpec(exact(3), exact(1), {clkPolarity, makeStringAttr("memSymbol", {}), enLevel}));
                map.emplace(grh::OperationKind::kMemorySyncReadPortRst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity, enLevel, makeStringAttr("memSymbol", {})}));
                map.emplace(grh::OperationKind::kMemorySyncReadPortArst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity, enLevel, makeStringAttr("memSymbol", {})}));

                map.emplace(grh::OperationKind::kMemoryWritePort, makeSpec(exact(4), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, enLevel}));
                map.emplace(grh::OperationKind::kMemoryWritePortRst, makeSpec(exact(5), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));
                map.emplace(grh::OperationKind::kMemoryWritePortArst, makeSpec(exact(5), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));

                map.emplace(grh::OperationKind::kMemoryMaskWritePort, makeSpec(exact(5), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, enLevel}));
                map.emplace(grh::OperationKind::kMemoryMaskWritePortRst, makeSpec(exact(6), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));
                map.emplace(grh::OperationKind::kMemoryMaskWritePortArst, makeSpec(exact(6), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));

                map.emplace(grh::OperationKind::kInstance, makeSpec(Range{0, std::numeric_limits<std::size_t>::max()}, Range{0, std::numeric_limits<std::size_t>::max()},
                                                                     {makeStringAttr("moduleName", {}), makeStringArrayAttr("inputPortName"), makeStringArrayAttr("outputPortName"), makeStringAttr("instanceName", {})}));

                map.emplace(grh::OperationKind::kBlackbox, makeSpec(Range{0, std::numeric_limits<std::size_t>::max()}, Range{0, std::numeric_limits<std::size_t>::max()},
                                                                     {makeStringAttr("moduleName", {}), makeStringArrayAttr("inputPortName"), makeStringArrayAttr("outputPortName"),
                                                                      makeStringArrayAttr("parameterNames"), makeStringArrayAttr("parameterValues"), makeStringAttr("instanceName", {})}));

                map.emplace(grh::OperationKind::kDisplay, makeSpec(Range{2, std::numeric_limits<std::size_t>::max()}, exact(0),
                                                                    {clkPolarity, makeStringAttr("formatString", {}), makeStringAttr("displayKind", {"display", "write", "strobe"})}));
                map.emplace(grh::OperationKind::kAssert, makeSpec(exact(2), exact(0),
                                                                   {clkPolarity},
                                                                   {makeStringAttr("message", {}, true), makeStringAttr("severity", {}, true)}));

                map.emplace(grh::OperationKind::kDpicImport, makeSpec(exact(0), exact(0),
                                                                      {makeStringArrayAttr("argsDirection"), makeIntArrayAttr("argsWidth"), makeStringArrayAttr("argsName")}));

                map.emplace(grh::OperationKind::kDpicCall, makeSpec(Range{2, std::numeric_limits<std::size_t>::max()}, Range{0, std::numeric_limits<std::size_t>::max()},
                                                                     {clkPolarity, makeStringAttr("targetImportSymbol", {}), makeStringArrayAttr("inArgName"), makeStringArrayAttr("outArgName")}));

                return map;
            }();

            return specs;
        }

        bool matchesAllowedStrings(const grh::AttributeValue &value, const AttributeRule &rule)
        {
            if (rule.allowedStrings.empty() || rule.kind != AttrValueKind::String)
            {
                return true;
            }
            const auto &text = std::get<std::string>(value);
            return std::find(rule.allowedStrings.begin(), rule.allowedStrings.end(), text) != rule.allowedStrings.end();
        }

        template <class T>
        bool lengthEquals(const grh::AttributeValue &value, std::size_t expected)
        {
            if (const auto *arr = std::get_if<std::vector<T>>(&value))
            {
                return arr->size() == expected;
            }
            return false;
        }

        grh::Operation *findOperationInNetlist(grh::Netlist &netlist, std::string_view symbol)
        {
            for (const auto &graphEntry : netlist.graphs())
            {
                if (auto *op = graphEntry.second->findOperation(symbol))
                {
                    return op;
                }
            }
            return nullptr;
        }

        std::vector<std::pair<std::string, std::size_t>> normalizeUsers(const std::vector<grh::ValueUser> &users)
        {
            std::vector<std::pair<std::string, std::size_t>> norm;
            norm.reserve(users.size());
            for (const auto &user : users)
            {
                norm.emplace_back(user.operationSymbol, user.operandIndex);
            }
            std::sort(norm.begin(), norm.end());
            return norm;
        }

    } // namespace

    GRHVerifyPass::GRHVerifyPass() : Pass("grh-verify", "grh-verify", "Verify GRH structural integrity and pointer caches") {}
    GRHVerifyPass::GRHVerifyPass(GRHVerifyOptions opts) : Pass("grh-verify", "grh-verify", "Verify GRH structural integrity and pointer caches"), options_(opts) {}

    PassResult GRHVerifyPass::run()
    {
        PassResult result;
        grh::Netlist &netlistRef = netlist();
        PassDiagnostics &diagsRef = diags();

        std::unordered_map<grh::Value *, std::vector<grh::ValueUser>> expectedUsers;
        std::unordered_map<grh::Value *, std::string> expectedDefiningOps;

        for (const auto &graphEntry : netlistRef.graphs())
        {
            grh::Graph &graph = *graphEntry.second;
            expectedUsers.clear();
            expectedDefiningOps.clear();

            for (const auto &[sym, valuePtr] : graph.values())
            {
                if (valuePtr)
                {
                    expectedUsers[valuePtr.get()] = {};
                }
            }

            for (const auto &opSym : graph.operationOrder())
            {
                grh::Operation *op = graph.findOperation(opSym);
                if (!op)
                {
                    error(graph, "Operation symbol missing from graph: " + opSym);
                    result.failed = true;
                    continue;
                }

                const auto &specIt = operationSpecs().find(op->kind());
                if (specIt == operationSpecs().end())
                {
                    error(graph, *op, "Unknown operation kind encountered");
                    result.failed = true;
                }

                const OperationSpec *spec = specIt == operationSpecs().end() ? nullptr : &specIt->second;
                const auto operandsCount = op->operandSymbols().size();
                const auto resultsCount = op->resultSymbols().size();

                if (spec)
                {
                    if (operandsCount < spec->operands.min || operandsCount > spec->operands.max)
                    {
                        std::ostringstream oss;
                        oss << "Operand count " << operandsCount << " out of range [" << spec->operands.min << ", " << spec->operands.max << "]";
                        error(graph, *op, oss.str());
                    }

                    if (resultsCount < spec->results.min || resultsCount > spec->results.max)
                    {
                        std::ostringstream oss;
                        oss << "Result count " << resultsCount << " out of range [" << spec->results.min << ", " << spec->results.max << "]";
                        error(graph, *op, oss.str());
                    }

                    std::unordered_set<std::string> allowedAttrs;
                    for (const auto &rule : spec->required)
                    {
                        allowedAttrs.insert(rule.name);
                        auto it = op->attributes().find(rule.name);
                        if (it == op->attributes().end())
                        {
                            error(graph, *op, "Missing required attribute: " + rule.name);
                            continue;
                        }
                        AttrValueKind kind = classifyAttr(it->second);
                        if (kind != rule.kind)
                        {
                            error(graph, *op, "Attribute '" + rule.name + "' has incorrect type");
                            continue;
                        }
                        if (!matchesAllowedStrings(it->second, rule))
                        {
                            error(graph, *op, "Attribute '" + rule.name + "' has unsupported value");
                        }
                    }
                    for (const auto &rule : spec->optional)
                    {
                        allowedAttrs.insert(rule.name);
                        auto it = op->attributes().find(rule.name);
                        if (it == op->attributes().end())
                        {
                            continue;
                        }
                        AttrValueKind kind = classifyAttr(it->second);
                        if (kind != rule.kind)
                        {
                            error(graph, *op, "Attribute '" + rule.name + "' has incorrect type");
                            continue;
                        }
                        if (!matchesAllowedStrings(it->second, rule))
                        {
                            error(graph, *op, "Attribute '" + rule.name + "' has unsupported value");
                        }
                    }

                    for (const auto &[attrName, attrVal] : op->attributes())
                    {
                        if (!allowedAttrs.contains(attrName))
                        {
                            info(graph, *op, "Unexpected attribute (kept): " + attrName);
                        }
                    }
                }

                auto verifyStringAttr = [&](std::string_view name) -> const std::string *
                {
                    auto it = op->attributes().find(std::string(name));
                    if (it == op->attributes().end())
                    {
                        return nullptr;
                    }
                    if (!std::holds_alternative<std::string>(it->second))
                    {
                        return nullptr;
                    }
                    return &std::get<std::string>(it->second);
                };

                // Operand/result presence and expected users
                for (std::size_t i = 0; i < operandsCount; ++i)
                {
                    const auto &operandSym = op->operandSymbols()[i];
                    grh::Value *value = graph.findValue(operandSym);
                    if (!value)
                    {
                        error(graph, *op, "Operand references missing value: " + operandSym);
                        continue;
                    }
                    expectedUsers[value].push_back(grh::ValueUser{op->symbol(), op, i});
                    if (options_.autoFixPointers)
                    {
                        try
                        {
                            op->operandValue(i);
                        }
                        catch (const std::exception &)
                        {
                            error(graph, *op, "Failed to resolve operand pointer for: " + operandSym);
                        }
                    }
                }

                for (std::size_t i = 0; i < resultsCount; ++i)
                {
                    const auto &resultSym = op->resultSymbols()[i];
                    grh::Value *value = graph.findValue(resultSym);
                    if (!value)
                    {
                        error(graph, *op, "Result references missing value: " + resultSym);
                        continue;
                    }
                    expectedDefiningOps[value] = op->symbol();
                    if (options_.autoFixPointers)
                    {
                        try
                        {
                            op->resultValue(i);
                        }
                        catch (const std::exception &)
                        {
                            error(graph, *op, "Failed to resolve result pointer for: " + resultSym);
                        }
                    }
                }

                // Per-kind referential checks
                switch (op->kind())
                {
                case grh::OperationKind::kMemoryAsyncReadPort:
                case grh::OperationKind::kMemorySyncReadPort:
                case grh::OperationKind::kMemorySyncReadPortRst:
                case grh::OperationKind::kMemorySyncReadPortArst:
                case grh::OperationKind::kMemoryWritePort:
                case grh::OperationKind::kMemoryWritePortRst:
                case grh::OperationKind::kMemoryWritePortArst:
                case grh::OperationKind::kMemoryMaskWritePort:
                case grh::OperationKind::kMemoryMaskWritePortRst:
                case grh::OperationKind::kMemoryMaskWritePortArst:
                {
                    const std::string *memSymbol = verifyStringAttr("memSymbol");
                    if (!memSymbol)
                    {
                        break;
                    }
                    grh::Operation *target = graph.findOperation(*memSymbol);
                    if (!target)
                    {
                        error(graph, *op, "memSymbol does not resolve to an operation: " + *memSymbol);
                    }
                    else if (target->kind() != grh::OperationKind::kMemory)
                    {
                        error(graph, *op, "memSymbol must point to kMemory, got " + std::string(grh::toString(target->kind())));
                    }
                    break;
                }
                case grh::OperationKind::kInstance:
                {
                    const std::string *moduleName = verifyStringAttr("moduleName");
                    auto inputNames = op->attributes().find("inputPortName");
                    auto outputNames = op->attributes().find("outputPortName");
                    if (moduleName && !netlistRef.findGraph(*moduleName))
                    {
                        error(graph, *op, "Instance moduleName not found in netlist: " + *moduleName);
                    }
                    if (inputNames != op->attributes().end() && !lengthEquals<std::string>(inputNames->second, operandsCount))
                    {
                        error(graph, *op, "inputPortName size must match operand count");
                    }
                    if (outputNames != op->attributes().end() && !lengthEquals<std::string>(outputNames->second, resultsCount))
                    {
                        error(graph, *op, "outputPortName size must match result count");
                    }
                    break;
                }
                case grh::OperationKind::kBlackbox:
                {
                    auto inputNames = op->attributes().find("inputPortName");
                    auto outputNames = op->attributes().find("outputPortName");
                    auto paramNames = op->attributes().find("parameterNames");
                    auto paramValues = op->attributes().find("parameterValues");
                    if (inputNames != op->attributes().end() && !lengthEquals<std::string>(inputNames->second, operandsCount))
                    {
                        error(graph, *op, "inputPortName size must match operand count");
                    }
                    if (outputNames != op->attributes().end() && !lengthEquals<std::string>(outputNames->second, resultsCount))
                    {
                        error(graph, *op, "outputPortName size must match result count");
                    }
                    if (paramNames != op->attributes().end() && paramValues != op->attributes().end())
                    {
                        const auto *namesVec = std::get_if<std::vector<std::string>>(&paramNames->second);
                        const auto *valuesVec = std::get_if<std::vector<std::string>>(&paramValues->second);
                        if (namesVec && valuesVec)
                        {
                            if (namesVec->size() != valuesVec->size())
                            {
                                error(graph, *op, "parameterNames size must match parameterValues size");
                            }
                        }
                        else
                        {
                            error(graph, *op, "parameterNames/parameterValues must both be string arrays");
                        }
                    }
                    break;
                }
                case grh::OperationKind::kDpicImport:
                {
                    const auto &attrs = op->attributes();
                    auto dirIt = attrs.find("argsDirection");
                    auto widthIt = attrs.find("argsWidth");
                    auto nameIt = attrs.find("argsName");
                    if (dirIt != attrs.end() && widthIt != attrs.end() && nameIt != attrs.end())
                    {
                        const auto *dirs = std::get_if<std::vector<std::string>>(&dirIt->second);
                        const auto *widths = std::get_if<std::vector<int64_t>>(&widthIt->second);
                        const auto *names = std::get_if<std::vector<std::string>>(&nameIt->second);
                        if (dirs && widths && names)
                        {
                            if (!(dirs->size() == widths->size() && dirs->size() == names->size()))
                            {
                                error(graph, *op, "argsDirection/argsWidth/argsName sizes must match");
                            }
                        }
                        else
                        {
                            error(graph, *op, "argsDirection/argsWidth/argsName must all be arrays");
                        }
                    }
                    break;
                }
                case grh::OperationKind::kDpicCall:
                {
                    const auto *targetImport = verifyStringAttr("targetImportSymbol");
                    if (targetImport)
                    {
                        grh::Operation *target = findOperationInNetlist(netlistRef, *targetImport);
                        if (!target)
                        {
                            error(graph, *op, "targetImportSymbol not found: " + *targetImport);
                        }
                        else if (target->kind() != grh::OperationKind::kDpicImport)
                        {
                            error(graph, *op, "targetImportSymbol must reference kDpicImport");
                        }
                    }

                    const auto &attrs = op->attributes();
                    auto inNamesIt = attrs.find("inArgName");
                    auto outNamesIt = attrs.find("outArgName");
                    if (inNamesIt != attrs.end())
                    {
                        std::size_t expectedInputs = operandsCount >= 2 ? operandsCount - 2 : 0;
                        if (!lengthEquals<std::string>(inNamesIt->second, expectedInputs))
                        {
                            error(graph, *op, "inArgName size must match input argument count");
                        }
                    }
                    if (outNamesIt != attrs.end())
                    {
                        if (!lengthEquals<std::string>(outNamesIt->second, resultsCount))
                        {
                            error(graph, *op, "outArgName size must match output argument count");
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }

            // Validate value definitions and user lists
            for (const auto &[sym, valuePtr] : graph.values())
            {
                grh::Value &value = *valuePtr;
                const auto expectedUserIter = expectedUsers.find(&value);
                const auto expectedDefIter = expectedDefiningOps.find(&value);

                if (value.definingOpSymbol())
                {
                    grh::Operation *defOp = graph.findOperation(*value.definingOpSymbol());
                    if (!defOp)
                    {
                        error(graph, value, "Value defining operation not found: " + *value.definingOpSymbol());
                    }
                    else if (expectedDefIter != expectedDefiningOps.end() && expectedDefIter->second != *value.definingOpSymbol())
                    {
                        error(graph, value, "Value defining op mismatch (symbol vs result owner)");
                    }
                    else if (options_.autoFixPointers)
                    {
                        value.definingOp();
                    }
                }
                else if (expectedDefIter != expectedDefiningOps.end())
                {
                    warning(graph, value, "Value is produced by operation but definingOpSymbol is not set");
                }

                const auto &actualUsers = value.users();
                std::vector<std::pair<std::string, std::size_t>> actualNorm = normalizeUsers(actualUsers);
                std::vector<std::pair<std::string, std::size_t>> expectedNorm;
                if (expectedUserIter != expectedUsers.end())
                {
                    expectedNorm = normalizeUsers(expectedUserIter->second);
                }

                bool usersMatch = actualNorm == expectedNorm;
                if (!usersMatch)
                {
                    warning(graph, value, "Value users list does not match operand references");
                    if (options_.autoFixPointers && expectedUserIter != expectedUsers.end())
                    {
                        auto &mutableUsers = const_cast<std::vector<grh::ValueUser> &>(value.users());
                        mutableUsers = expectedUserIter->second;
                        usersMatch = true;
                        result.changed = true;
                        info(graph, value, "Value users list rebuilt from operations");
                    }
                }

                auto &mutableUsers = const_cast<std::vector<grh::ValueUser> &>(value.users());
                for (auto &user : mutableUsers)
                {
                    if (user.operationSymbol.empty())
                    {
                        error(graph, value, "User entry missing operation symbol");
                        continue;
                    }
                    grh::Operation *op = graph.findOperation(user.operationSymbol);
                    if (!op)
                    {
                        error(graph, value, "User entry references unknown operation: " + user.operationSymbol);
                        continue;
                    }
                    if (options_.autoFixPointers && user.operationPtr != op)
                    {
                        user.operationPtr = op;
                        result.changed = true;
                        info(graph, value, "User pointer rehydrated for value");
                    }
                }
            }
        }

        result.failed = result.failed || diagsRef.hasError();
        return result;
    }

} // namespace wolf_sv::transform
