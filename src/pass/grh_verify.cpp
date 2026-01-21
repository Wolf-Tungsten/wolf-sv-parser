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
#include <tuple>
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

        struct OperationRef
        {
            grh::Graph *graph = nullptr;
            grh::ir::OperationId op = grh::ir::OperationId::invalid();
        };

        struct UserKey
        {
            grh::ir::OperationId op = grh::ir::OperationId::invalid();
            uint32_t operandIndex = 0;
        };

        bool operator==(const UserKey &lhs, const UserKey &rhs)
        {
            return lhs.op == rhs.op && lhs.operandIndex == rhs.operandIndex;
        }

        bool operator<(const UserKey &lhs, const UserKey &rhs)
        {
            auto lhsTuple = std::tie(lhs.op.graph.index, lhs.op.graph.generation, lhs.op.index, lhs.op.generation, lhs.operandIndex);
            auto rhsTuple = std::tie(rhs.op.graph.index, rhs.op.graph.generation, rhs.op.index, rhs.op.generation, rhs.operandIndex);
            return lhsTuple < rhsTuple;
        }

        std::optional<OperationRef> findOperationInNetlist(grh::Netlist &netlist, std::string_view symbol)
        {
            for (const auto &graphEntry : netlist.graphs())
            {
                grh::Graph *graph = graphEntry.second.get();
                if (!graph)
                {
                    continue;
                }
                const grh::ir::OperationId opId = graph->findOperation(symbol);
                if (opId.valid())
                {
                    return OperationRef{graph, opId};
                }
            }
            return std::nullopt;
        }

        std::vector<UserKey> normalizeUsers(std::span<const grh::ir::ValueUser> users)
        {
            std::vector<UserKey> norm;
            norm.reserve(users.size());
            for (const auto &user : users)
            {
                norm.push_back(UserKey{user.operation, user.operandIndex});
            }
            std::sort(norm.begin(), norm.end());
            return norm;
        }

        std::optional<grh::AttributeValue> findAttr(const grh::Graph &graph, const grh::Operation &op, std::string_view name)
        {
            const grh::ir::SymbolId key = graph.lookupSymbol(name);
            if (!key.valid())
            {
                return std::nullopt;
            }
            return op.attr(key);
        }

    } // namespace

    GRHVerifyPass::GRHVerifyPass() : Pass("grh-verify", "grh-verify", "Verify GRH structural integrity and pointer caches") {}
    GRHVerifyPass::GRHVerifyPass(GRHVerifyOptions opts) : Pass("grh-verify", "grh-verify", "Verify GRH structural integrity and pointer caches"), options_(opts) {}

    PassResult GRHVerifyPass::run()
    {
        PassResult result;
        grh::Netlist &netlistRef = netlist();
        PassDiagnostics &diagsRef = diags();

        std::unordered_map<grh::ir::ValueId, std::vector<grh::ir::ValueUser>, grh::ir::ValueIdHash> expectedUsers;
        std::unordered_map<grh::ir::ValueId, grh::ir::OperationId, grh::ir::ValueIdHash> expectedDefiningOps;

        for (const auto &graphEntry : netlistRef.graphs())
        {
            grh::Graph &graph = *graphEntry.second;
            expectedUsers.clear();
            expectedDefiningOps.clear();

            for (const auto valueId : graph.values())
            {
                expectedUsers.emplace(valueId, std::vector<grh::ir::ValueUser>{});
            }

            for (const auto opId : graph.operations())
            {
                const grh::Operation op = graph.getOperation(opId);

                const auto &specIt = operationSpecs().find(op.kind());
                if (specIt == operationSpecs().end())
                {
                    error(graph, op, "Unknown operation kind encountered");
                    result.failed = true;
                }

                const OperationSpec *spec = specIt == operationSpecs().end() ? nullptr : &specIt->second;
                const auto operandsCount = op.operands().size();
                const auto resultsCount = op.results().size();

                if (spec)
                {
                    if (operandsCount < spec->operands.min || operandsCount > spec->operands.max)
                    {
                        std::ostringstream oss;
                        oss << "Operand count " << operandsCount << " out of range [" << spec->operands.min << ", " << spec->operands.max << "]";
                        error(graph, op, oss.str());
                    }

                    if (resultsCount < spec->results.min || resultsCount > spec->results.max)
                    {
                        std::ostringstream oss;
                        oss << "Result count " << resultsCount << " out of range [" << spec->results.min << ", " << spec->results.max << "]";
                        error(graph, op, oss.str());
                    }

                    std::unordered_set<std::string_view> allowedAttrs;
                    for (const auto &rule : spec->required)
                    {
                        allowedAttrs.insert(rule.name);
                        auto attr = findAttr(graph, op, rule.name);
                        if (!attr)
                        {
                            error(graph, op, "Missing required attribute: " + rule.name);
                            continue;
                        }
                        AttrValueKind kind = classifyAttr(*attr);
                        if (kind != rule.kind)
                        {
                            error(graph, op, "Attribute '" + rule.name + "' has incorrect type");
                            continue;
                        }
                        if (!matchesAllowedStrings(*attr, rule))
                        {
                            error(graph, op, "Attribute '" + rule.name + "' has unsupported value");
                        }
                    }
                    for (const auto &rule : spec->optional)
                    {
                        allowedAttrs.insert(rule.name);
                        auto attr = findAttr(graph, op, rule.name);
                        if (!attr)
                        {
                            continue;
                        }
                        AttrValueKind kind = classifyAttr(*attr);
                        if (kind != rule.kind)
                        {
                            error(graph, op, "Attribute '" + rule.name + "' has incorrect type");
                            continue;
                        }
                        if (!matchesAllowedStrings(*attr, rule))
                        {
                            error(graph, op, "Attribute '" + rule.name + "' has unsupported value");
                        }
                    }

                    for (const auto &attr : op.attrs())
                    {
                        const std::string_view attrName = graph.symbolText(attr.key);
                        if (!allowedAttrs.contains(attrName))
                        {
                            info(graph, op, "Unexpected attribute (kept): " + std::string(attrName));
                        }
                    }
                }

                auto verifyStringAttr = [&](std::string_view name) -> std::optional<std::string>
                {
                    auto attr = findAttr(graph, op, name);
                    if (!attr)
                    {
                        return std::nullopt;
                    }
                    if (const auto *value = std::get_if<std::string>(&*attr))
                    {
                        return *value;
                    }
                    return std::nullopt;
                };

                // Operand/result presence and expected users
                for (std::size_t i = 0; i < operandsCount; ++i)
                {
                    const auto operandId = op.operands()[i];
                    if (!operandId.valid())
                    {
                        error(graph, op, "Operand references invalid value id");
                        continue;
                    }
                    try
                    {
                        graph.getValue(operandId);
                    }
                    catch (const std::exception &)
                    {
                        error(graph, op, "Operand references missing value");
                        continue;
                    }
                    expectedUsers[operandId].push_back(grh::ir::ValueUser{opId, static_cast<uint32_t>(i)});
                }

                for (std::size_t i = 0; i < resultsCount; ++i)
                {
                    const auto resultId = op.results()[i];
                    if (!resultId.valid())
                    {
                        error(graph, op, "Result references invalid value id");
                        continue;
                    }
                    try
                    {
                        graph.getValue(resultId);
                    }
                    catch (const std::exception &)
                    {
                        error(graph, op, "Result references missing value");
                        continue;
                    }
                    expectedDefiningOps[resultId] = opId;
                }

                // Per-kind referential checks
                switch (op.kind())
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
                    const auto memSymbol = verifyStringAttr("memSymbol");
                    if (!memSymbol)
                    {
                        break;
                    }
                    const grh::ir::OperationId targetId = graph.findOperation(*memSymbol);
                    if (!targetId.valid())
                    {
                        error(graph, op, "memSymbol does not resolve to an operation: " + *memSymbol);
                    }
                    else if (graph.getOperation(targetId).kind() != grh::OperationKind::kMemory)
                    {
                        error(graph, op, "memSymbol must point to kMemory");
                    }
                    break;
                }
                case grh::OperationKind::kInstance:
                {
                    const auto moduleName = verifyStringAttr("moduleName");
                    auto inputNames = findAttr(graph, op, "inputPortName");
                    auto outputNames = findAttr(graph, op, "outputPortName");
                    if (moduleName && !netlistRef.findGraph(*moduleName))
                    {
                        error(graph, op, "Instance moduleName not found in netlist: " + *moduleName);
                    }
                    if (inputNames && !lengthEquals<std::string>(*inputNames, operandsCount))
                    {
                        error(graph, op, "inputPortName size must match operand count");
                    }
                    if (outputNames && !lengthEquals<std::string>(*outputNames, resultsCount))
                    {
                        error(graph, op, "outputPortName size must match result count");
                    }
                    break;
                }
                case grh::OperationKind::kBlackbox:
                {
                    auto inputNames = findAttr(graph, op, "inputPortName");
                    auto outputNames = findAttr(graph, op, "outputPortName");
                    auto paramNames = findAttr(graph, op, "parameterNames");
                    auto paramValues = findAttr(graph, op, "parameterValues");
                    if (inputNames && !lengthEquals<std::string>(*inputNames, operandsCount))
                    {
                        error(graph, op, "inputPortName size must match operand count");
                    }
                    if (outputNames && !lengthEquals<std::string>(*outputNames, resultsCount))
                    {
                        error(graph, op, "outputPortName size must match result count");
                    }
                    if (paramNames && paramValues)
                    {
                        const auto *namesVec = std::get_if<std::vector<std::string>>(&*paramNames);
                        const auto *valuesVec = std::get_if<std::vector<std::string>>(&*paramValues);
                        if (namesVec && valuesVec)
                        {
                            if (namesVec->size() != valuesVec->size())
                            {
                                error(graph, op, "parameterNames size must match parameterValues size");
                            }
                        }
                        else
                        {
                            error(graph, op, "parameterNames/parameterValues must both be string arrays");
                        }
                    }
                    break;
                }
                case grh::OperationKind::kDpicImport:
                {
                    auto dirIt = findAttr(graph, op, "argsDirection");
                    auto widthIt = findAttr(graph, op, "argsWidth");
                    auto nameIt = findAttr(graph, op, "argsName");
                    if (dirIt && widthIt && nameIt)
                    {
                        const auto *dirs = std::get_if<std::vector<std::string>>(&*dirIt);
                        const auto *widths = std::get_if<std::vector<int64_t>>(&*widthIt);
                        const auto *names = std::get_if<std::vector<std::string>>(&*nameIt);
                        if (dirs && widths && names)
                        {
                            if (!(dirs->size() == widths->size() && dirs->size() == names->size()))
                            {
                                error(graph, op, "argsDirection/argsWidth/argsName sizes must match");
                            }
                        }
                        else
                        {
                            error(graph, op, "argsDirection/argsWidth/argsName must all be arrays");
                        }
                    }
                    break;
                }
                case grh::OperationKind::kDpicCall:
                {
                    const auto targetImport = verifyStringAttr("targetImportSymbol");
                    if (targetImport)
                    {
                        auto target = findOperationInNetlist(netlistRef, *targetImport);
                        if (!target)
                        {
                            error(graph, op, "targetImportSymbol not found: " + *targetImport);
                        }
                        else if (target->graph->getOperation(target->op).kind() != grh::OperationKind::kDpicImport)
                        {
                            error(graph, op, "targetImportSymbol must reference kDpicImport");
                        }
                    }

                    auto inNamesIt = findAttr(graph, op, "inArgName");
                    auto outNamesIt = findAttr(graph, op, "outArgName");
                    if (inNamesIt)
                    {
                        std::size_t expectedInputs = operandsCount >= 2 ? operandsCount - 2 : 0;
                        if (!lengthEquals<std::string>(*inNamesIt, expectedInputs))
                        {
                            error(graph, op, "inArgName size must match input argument count");
                        }
                    }
                    if (outNamesIt)
                    {
                        if (!lengthEquals<std::string>(*outNamesIt, resultsCount))
                        {
                            error(graph, op, "outArgName size must match output argument count");
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }

            // Validate value definitions and user lists
            for (const auto valueId : graph.values())
            {
                grh::Value value = graph.getValue(valueId);
                const auto expectedUserIter = expectedUsers.find(valueId);
                const auto expectedDefIter = expectedDefiningOps.find(valueId);

                if (value.definingOp().valid())
                {
                    try
                    {
                        graph.getOperation(value.definingOp());
                    }
                    catch (const std::exception &)
                    {
                        error(graph, value, "Value defining operation not found");
                    }
                    if (expectedDefIter != expectedDefiningOps.end() && expectedDefIter->second != value.definingOp())
                    {
                        error(graph, value, "Value defining op mismatch (id vs result owner)");
                    }
                }
                else if (expectedDefIter != expectedDefiningOps.end())
                {
                    warning(graph, value, "Value is produced by operation but definingOp is not set");
                }

                std::vector<UserKey> actualNorm = normalizeUsers(value.users());
                std::vector<UserKey> expectedNorm;
                if (expectedUserIter != expectedUsers.end())
                {
                    expectedNorm = normalizeUsers(std::span<const grh::ir::ValueUser>(expectedUserIter->second));
                }

                if (actualNorm != expectedNorm)
                {
                    warning(graph, value, "Value users list does not match operand references");
                }

                for (const auto &user : value.users())
                {
                    if (!user.operation.valid())
                    {
                        error(graph, value, "User entry missing operation id");
                        continue;
                    }
                    try
                    {
                        grh::Operation userOp = graph.getOperation(user.operation);
                        if (user.operandIndex >= userOp.operands().size())
                        {
                            error(graph, value, "User operand index out of range");
                        }
                    }
                    catch (const std::exception &)
                    {
                        error(graph, value, "User entry references unknown operation");
                    }
                }
            }
        }

        result.failed = result.failed || diagsRef.hasError();
        return result;
    }

} // namespace wolf_sv::transform
