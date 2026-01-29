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

namespace wolf_sv_parser::transform
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

        AttrValueKind classifyAttr(const grh::ir::AttributeValue &value)
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

        const std::unordered_map<grh::ir::OperationKind, OperationSpec> &operationSpecs()
        {
            static const std::unordered_map<grh::ir::OperationKind, OperationSpec> specs = []()
            {
                std::unordered_map<grh::ir::OperationKind, OperationSpec> map;

                OperationSpec binary = makeSpec(exact(2), exact(1), {});
                OperationSpec unary = makeSpec(exact(1), exact(1), {});

                const std::vector binaryKinds = {
                    grh::ir::OperationKind::kAdd, grh::ir::OperationKind::kSub, grh::ir::OperationKind::kMul, grh::ir::OperationKind::kDiv, grh::ir::OperationKind::kMod,
                    grh::ir::OperationKind::kEq, grh::ir::OperationKind::kNe, grh::ir::OperationKind::kCaseEq, grh::ir::OperationKind::kCaseNe,
                    grh::ir::OperationKind::kWildcardEq, grh::ir::OperationKind::kWildcardNe,
                    grh::ir::OperationKind::kLt, grh::ir::OperationKind::kLe, grh::ir::OperationKind::kGt, grh::ir::OperationKind::kGe,
                    grh::ir::OperationKind::kAnd, grh::ir::OperationKind::kOr, grh::ir::OperationKind::kXor, grh::ir::OperationKind::kXnor, grh::ir::OperationKind::kLogicAnd, grh::ir::OperationKind::kLogicOr,
                    grh::ir::OperationKind::kShl, grh::ir::OperationKind::kLShr, grh::ir::OperationKind::kAShr};
                for (auto kind : binaryKinds)
                {
                    map.emplace(kind, binary);
                }

                const std::vector unaryKinds = {
                    grh::ir::OperationKind::kNot, grh::ir::OperationKind::kLogicNot, grh::ir::OperationKind::kReduceAnd, grh::ir::OperationKind::kReduceOr,
                    grh::ir::OperationKind::kReduceXor, grh::ir::OperationKind::kReduceNor, grh::ir::OperationKind::kReduceNand, grh::ir::OperationKind::kReduceXnor};
                for (auto kind : unaryKinds)
                {
                    map.emplace(kind, unary);
                }

                map.emplace(grh::ir::OperationKind::kConstant, makeSpec(exact(0), exact(1), {makeStringAttr("constValue", {})}));
                map.emplace(grh::ir::OperationKind::kMux, makeSpec(exact(3), exact(1), {}));
                map.emplace(grh::ir::OperationKind::kAssign, makeSpec(exact(1), exact(1), {}));
                map.emplace(grh::ir::OperationKind::kConcat, makeSpec(Range{2, std::numeric_limits<std::size_t>::max()}, exact(1), {}));
                map.emplace(grh::ir::OperationKind::kReplicate, makeSpec(exact(1), exact(1), {makeIntAttr("rep")}));
                map.emplace(grh::ir::OperationKind::kSliceStatic, makeSpec(exact(1), exact(1), {makeIntAttr("sliceStart"), makeIntAttr("sliceEnd")}));
                map.emplace(grh::ir::OperationKind::kSliceDynamic, makeSpec(exact(2), exact(1), {makeIntAttr("sliceWidth")}));
                map.emplace(grh::ir::OperationKind::kSliceArray, makeSpec(exact(2), exact(1), {makeIntAttr("sliceWidth")}));

                map.emplace(grh::ir::OperationKind::kLatch, makeSpec(exact(2), exact(1), {makeStringAttr("enLevel", {"high", "low"})}));
                map.emplace(grh::ir::OperationKind::kLatchArst, makeSpec(exact(4), exact(1), {makeStringAttr("enLevel", {"high", "low"}), makeStringAttr("rstPolarity", {"high", "low"})}));

                auto clkPolarity = makeStringAttr("clkPolarity", {"posedge", "negedge"});
                auto enLevel = makeStringAttr("enLevel", {"high", "low"});
                auto rstPolarity = makeStringAttr("rstPolarity", {"high", "low"});
                map.emplace(grh::ir::OperationKind::kRegister, makeSpec(exact(2), exact(1), {clkPolarity}));
                map.emplace(grh::ir::OperationKind::kRegisterEn, makeSpec(exact(3), exact(1), {clkPolarity, enLevel}));
                map.emplace(grh::ir::OperationKind::kRegisterRst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity}));
                map.emplace(grh::ir::OperationKind::kRegisterEnRst, makeSpec(exact(5), exact(1), {clkPolarity, rstPolarity, enLevel}));
                map.emplace(grh::ir::OperationKind::kRegisterArst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity}));
                map.emplace(grh::ir::OperationKind::kRegisterEnArst, makeSpec(exact(5), exact(1), {clkPolarity, rstPolarity, enLevel}));

                map.emplace(grh::ir::OperationKind::kMemory, makeSpec(exact(0), exact(0), {makeIntAttr("width"), makeIntAttr("row"), makeBoolAttr("isSigned")}));
                map.emplace(grh::ir::OperationKind::kMemoryAsyncReadPort, makeSpec(exact(1), exact(1), {makeStringAttr("memSymbol", {})}));
                map.emplace(grh::ir::OperationKind::kMemorySyncReadPort, makeSpec(exact(3), exact(1), {clkPolarity, makeStringAttr("memSymbol", {}), enLevel}));
                map.emplace(grh::ir::OperationKind::kMemorySyncReadPortRst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity, enLevel, makeStringAttr("memSymbol", {})}));
                map.emplace(grh::ir::OperationKind::kMemorySyncReadPortArst, makeSpec(exact(4), exact(1), {clkPolarity, rstPolarity, enLevel, makeStringAttr("memSymbol", {})}));

                map.emplace(grh::ir::OperationKind::kMemoryWritePort, makeSpec(exact(4), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, enLevel}));
                map.emplace(grh::ir::OperationKind::kMemoryWritePortRst, makeSpec(exact(5), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));
                map.emplace(grh::ir::OperationKind::kMemoryWritePortArst, makeSpec(exact(5), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));

                map.emplace(grh::ir::OperationKind::kMemoryMaskWritePort, makeSpec(exact(5), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, enLevel}));
                map.emplace(grh::ir::OperationKind::kMemoryMaskWritePortRst, makeSpec(exact(6), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));
                map.emplace(grh::ir::OperationKind::kMemoryMaskWritePortArst, makeSpec(exact(6), exact(0), {makeStringAttr("memSymbol", {}), clkPolarity, rstPolarity, enLevel}));

                map.emplace(grh::ir::OperationKind::kInstance, makeSpec(Range{0, std::numeric_limits<std::size_t>::max()}, Range{0, std::numeric_limits<std::size_t>::max()},
                                                                     {makeStringAttr("moduleName", {}), makeStringArrayAttr("inputPortName"), makeStringArrayAttr("outputPortName"),
                                                                      makeStringArrayAttr("inoutPortName", true), makeStringAttr("instanceName", {})}));

                map.emplace(grh::ir::OperationKind::kBlackbox, makeSpec(Range{0, std::numeric_limits<std::size_t>::max()}, Range{0, std::numeric_limits<std::size_t>::max()},
                                                                     {makeStringAttr("moduleName", {}), makeStringArrayAttr("inputPortName"), makeStringArrayAttr("outputPortName"),
                                                                      makeStringArrayAttr("inoutPortName", true), makeStringArrayAttr("parameterNames"),
                                                                      makeStringArrayAttr("parameterValues"), makeStringAttr("instanceName", {})}));

                map.emplace(grh::ir::OperationKind::kDisplay, makeSpec(Range{2, std::numeric_limits<std::size_t>::max()}, exact(0),
                                                                    {clkPolarity, makeStringAttr("formatString", {}), makeStringAttr("displayKind", {"display", "write", "strobe"})}));
                map.emplace(grh::ir::OperationKind::kAssert, makeSpec(exact(2), exact(0),
                                                                   {clkPolarity},
                                                                   {makeStringAttr("message", {}, true), makeStringAttr("severity", {}, true)}));

                map.emplace(grh::ir::OperationKind::kDpicImport, makeSpec(exact(0), exact(0),
                                                                      {makeStringArrayAttr("argsDirection"), makeIntArrayAttr("argsWidth"), makeStringArrayAttr("argsName")}));

                map.emplace(grh::ir::OperationKind::kDpicCall, makeSpec(Range{2, std::numeric_limits<std::size_t>::max()}, Range{0, std::numeric_limits<std::size_t>::max()},
                                                                     {clkPolarity, makeStringAttr("targetImportSymbol", {}), makeStringArrayAttr("inArgName"),
                                                                      makeStringArrayAttr("outArgName"), makeStringArrayAttr("inoutArgName", true)}));

                return map;
            }();

            return specs;
        }

        bool matchesAllowedStrings(const grh::ir::AttributeValue &value, const AttributeRule &rule)
        {
            if (rule.allowedStrings.empty() || rule.kind != AttrValueKind::String)
            {
                return true;
            }
            const auto &text = std::get<std::string>(value);
            return std::find(rule.allowedStrings.begin(), rule.allowedStrings.end(), text) != rule.allowedStrings.end();
        }

        template <class T>
        bool lengthEquals(const grh::ir::AttributeValue &value, std::size_t expected)
        {
            if (const auto *arr = std::get_if<std::vector<T>>(&value))
            {
                return arr->size() == expected;
            }
            return false;
        }

        struct OperationRef
        {
            grh::ir::Graph *graph = nullptr;
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

        std::optional<OperationRef> findOperationInNetlist(grh::ir::Netlist &netlist, std::string_view symbol)
        {
            for (const auto &graphEntry : netlist.graphs())
            {
                grh::ir::Graph *graph = graphEntry.second.get();
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

        std::optional<grh::ir::AttributeValue> findAttr(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string_view name)
        {
            (void)graph;
            return op.attr(name);
        }

    } // namespace

    GRHVerifyPass::GRHVerifyPass() : Pass("grh-verify", "grh-verify", "Verify GRH structural integrity and pointer caches") {}
    GRHVerifyPass::GRHVerifyPass(GRHVerifyOptions opts) : Pass("grh-verify", "grh-verify", "Verify GRH structural integrity and pointer caches"), options_(opts) {}

    PassResult GRHVerifyPass::run()
    {
        PassResult result;
        grh::ir::Netlist &netlistRef = netlist();
        PassDiagnostics &diagsRef = diags();

        std::unordered_map<grh::ir::ValueId, std::vector<grh::ir::ValueUser>, grh::ir::ValueIdHash> expectedUsers;
        std::unordered_map<grh::ir::ValueId, grh::ir::OperationId, grh::ir::ValueIdHash> expectedDefiningOps;

        for (const auto &graphEntry : netlistRef.graphs())
        {
            grh::ir::Graph &graph = *graphEntry.second;
            expectedUsers.clear();
            expectedDefiningOps.clear();

            for (const auto valueId : graph.values())
            {
                expectedUsers.emplace(valueId, std::vector<grh::ir::ValueUser>{});
            }

            for (const auto opId : graph.operations())
            {
                const grh::ir::Operation op = graph.getOperation(opId);

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
                        const std::string_view attrName = attr.key;
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
                case grh::ir::OperationKind::kMemoryAsyncReadPort:
                case grh::ir::OperationKind::kMemorySyncReadPort:
                case grh::ir::OperationKind::kMemorySyncReadPortRst:
                case grh::ir::OperationKind::kMemorySyncReadPortArst:
                case grh::ir::OperationKind::kMemoryWritePort:
                case grh::ir::OperationKind::kMemoryWritePortRst:
                case grh::ir::OperationKind::kMemoryWritePortArst:
                case grh::ir::OperationKind::kMemoryMaskWritePort:
                case grh::ir::OperationKind::kMemoryMaskWritePortRst:
                case grh::ir::OperationKind::kMemoryMaskWritePortArst:
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
                    else if (graph.getOperation(targetId).kind() != grh::ir::OperationKind::kMemory)
                    {
                        error(graph, op, "memSymbol must point to kMemory");
                    }
                    break;
                }
                case grh::ir::OperationKind::kInstance:
                {
                    const auto moduleName = verifyStringAttr("moduleName");
                    auto inputNames = findAttr(graph, op, "inputPortName");
                    auto outputNames = findAttr(graph, op, "outputPortName");
                    auto inoutNames = findAttr(graph, op, "inoutPortName");
                    if (moduleName && !netlistRef.findGraph(*moduleName))
                    {
                        error(graph, op, "Instance moduleName not found in netlist: " + *moduleName);
                    }
                    std::size_t inoutCount = 0;
                    if (inoutNames)
                    {
                        if (const auto *inoutVec = std::get_if<std::vector<std::string>>(&*inoutNames))
                        {
                            inoutCount = inoutVec->size();
                        }
                        else
                        {
                            error(graph, op, "inoutPortName must be a string array");
                        }
                    }
                    if (inputNames && !lengthEquals<std::string>(*inputNames, operandsCount - inoutCount * 2))
                    {
                        error(graph, op, "inputPortName size must match operand count");
                    }
                    if (outputNames && !lengthEquals<std::string>(*outputNames, resultsCount - inoutCount))
                    {
                        error(graph, op, "outputPortName size must match result count");
                    }
                    break;
                }
                case grh::ir::OperationKind::kBlackbox:
                {
                    auto inputNames = findAttr(graph, op, "inputPortName");
                    auto outputNames = findAttr(graph, op, "outputPortName");
                    auto inoutNames = findAttr(graph, op, "inoutPortName");
                    auto paramNames = findAttr(graph, op, "parameterNames");
                    auto paramValues = findAttr(graph, op, "parameterValues");
                    std::size_t inoutCount = 0;
                    if (inoutNames)
                    {
                        if (const auto *inoutVec = std::get_if<std::vector<std::string>>(&*inoutNames))
                        {
                            inoutCount = inoutVec->size();
                        }
                        else
                        {
                            error(graph, op, "inoutPortName must be a string array");
                        }
                    }
                    if (inputNames && !lengthEquals<std::string>(*inputNames, operandsCount - inoutCount * 2))
                    {
                        error(graph, op, "inputPortName size must match operand count");
                    }
                    if (outputNames && !lengthEquals<std::string>(*outputNames, resultsCount - inoutCount))
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
                case grh::ir::OperationKind::kDpicImport:
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
                            else
                            {
                                for (const auto &dir : *dirs)
                                {
                                    if (dir != "input" && dir != "output" && dir != "inout")
                                    {
                                        error(graph, op, "argsDirection must be input/output/inout");
                                        break;
                                    }
                                }
                            }
                        }
                        else
                        {
                            error(graph, op, "argsDirection/argsWidth/argsName must all be arrays");
                        }
                    }
                    break;
                }
                case grh::ir::OperationKind::kDpicCall:
                {
                    const auto targetImport = verifyStringAttr("targetImportSymbol");
                    if (targetImport)
                    {
                        auto target = findOperationInNetlist(netlistRef, *targetImport);
                        if (!target)
                        {
                            error(graph, op, "targetImportSymbol not found: " + *targetImport);
                        }
                        else if (target->graph->getOperation(target->op).kind() != grh::ir::OperationKind::kDpicImport)
                        {
                            error(graph, op, "targetImportSymbol must reference kDpicImport");
                        }
                    }

                    auto inNamesIt = findAttr(graph, op, "inArgName");
                    auto outNamesIt = findAttr(graph, op, "outArgName");
                    auto inoutNamesIt = findAttr(graph, op, "inoutArgName");
                    std::size_t inoutCount = 0;
                    if (inoutNamesIt)
                    {
                        if (const auto *vec = std::get_if<std::vector<std::string>>(&*inoutNamesIt))
                        {
                            inoutCount = vec->size();
                        }
                        else
                        {
                            error(graph, op, "inoutArgName must be a string array");
                        }
                    }
                    if (inNamesIt)
                    {
                        std::size_t expectedInputs = operandsCount >= 2 ? operandsCount - 2 - inoutCount : 0;
                        if (!lengthEquals<std::string>(*inNamesIt, expectedInputs))
                        {
                            error(graph, op, "inArgName size must match input argument count");
                        }
                    }
                    if (outNamesIt)
                    {
                        std::size_t expectedOutputs = resultsCount - inoutCount;
                        if (!lengthEquals<std::string>(*outNamesIt, expectedOutputs))
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
                grh::ir::Value value = graph.getValue(valueId);
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
                        grh::ir::Operation userOp = graph.getOperation(user.operation);
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

} // namespace wolf_sv_parser::transform
