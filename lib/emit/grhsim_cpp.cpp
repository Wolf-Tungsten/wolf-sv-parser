#include "emit/grhsim_cpp.hpp"

#include "transform/activity_schedule.hpp"

#include "slang/numeric/SVInt.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolvrix::lib::emit
{

    namespace
    {

        using wolvrix::lib::grh::Graph;
        using wolvrix::lib::grh::Operation;
        using wolvrix::lib::grh::OperationId;
        using wolvrix::lib::grh::OperationIdHash;
        using wolvrix::lib::grh::OperationKind;
        using wolvrix::lib::grh::Value;
        using wolvrix::lib::grh::ValueId;
        using wolvrix::lib::grh::ValueIdHash;
        using wolvrix::lib::grh::ValueType;
        using wolvrix::lib::transform::ActivityScheduleEventDomainSignature;
        using wolvrix::lib::transform::ActivityScheduleEventDomainSignatureHash;
        using wolvrix::lib::transform::ActivityScheduleEventDomainSinkGroups;
        using wolvrix::lib::transform::ActivityScheduleEventDomainSinks;
        using wolvrix::lib::transform::ActivityScheduleEventTerm;
        using wolvrix::lib::transform::ActivityScheduleEventTermHash;
        using wolvrix::lib::transform::ActivityScheduleHeadEvalSupernodes;
        using wolvrix::lib::transform::ActivityScheduleSupernodeEventDomains;
        using wolvrix::lib::transform::ActivityScheduleSupernodeToOpSymbols;
        using wolvrix::lib::transform::ActivityScheduleSupernodeToOps;
        using wolvrix::lib::transform::ActivityScheduleTopoOrder;
        using wolvrix::lib::transform::ActivityScheduleDag;

        template <typename T>
        std::optional<T> getAttribute(const Operation &op, std::string_view key)
        {
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *ptr = std::get_if<T>(&*attr))
            {
                return *ptr;
            }
            return std::nullopt;
        }

        std::string sanitizeIdentifier(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            if (text.empty() || (!std::isalpha(static_cast<unsigned char>(text.front())) && text.front() != '_'))
            {
                out.push_back('_');
            }
            for (unsigned char ch : text)
            {
                if (std::isalnum(ch) || ch == '_')
                {
                    out.push_back(static_cast<char>(ch));
                }
                else
                {
                    out.push_back('_');
                }
            }
            return out;
        }

        std::string escapeCppString(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            for (unsigned char ch : text)
            {
                switch (ch)
                {
                case '\\':
                    out.append("\\\\");
                    break;
                case '"':
                    out.append("\\\"");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    out.push_back(static_cast<char>(ch));
                    break;
                }
            }
            return out;
        }

        std::string joinStrings(const std::vector<std::string> &items, std::string_view sep)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (i != 0)
                {
                    out << sep;
                }
                out << items[i];
            }
            return out.str();
        }

        std::size_t parseEventPrecomputeMaxOps(const EmitOptions &options)
        {
            auto it = options.attributes.find("event_precompute_max_ops");
            if (it == options.attributes.end())
            {
                return 128;
            }
            try
            {
                return static_cast<std::size_t>(std::stoull(it->second));
            }
            catch (const std::exception &)
            {
                return 128;
            }
        }

        bool isScalarLogicValue(const Graph &graph, ValueId value) noexcept
        {
            return graph.valueType(value) == ValueType::Logic &&
                   graph.valueWidth(value) > 0 &&
                   graph.valueWidth(value) <= 64;
        }

        std::string cppTypeForValue(const Graph &graph, ValueId value)
        {
            switch (graph.valueType(value))
            {
            case ValueType::Real:
                return "double";
            case ValueType::String:
                return "std::string";
            case ValueType::Logic:
            default:
                break;
            }

            const int32_t width = graph.valueWidth(value);
            if (width <= 1)
            {
                return "bool";
            }
            if (width <= 8)
            {
                return "std::uint8_t";
            }
            if (width <= 16)
            {
                return "std::uint16_t";
            }
            if (width <= 32)
            {
                return "std::uint32_t";
            }
            if (width <= 64)
            {
                return "std::uint64_t";
            }
            std::ostringstream out;
            out << "std::array<std::uint64_t, " << ((static_cast<std::size_t>(width) + 63u) / 64u) << ">";
            return out.str();
        }

        std::string defaultInitExpr(const Graph &graph, ValueId value)
        {
            switch (graph.valueType(value))
            {
            case ValueType::Real:
                return "0.0";
            case ValueType::String:
                return "{}";
            case ValueType::Logic:
            default:
                break;
            }
            if (graph.valueWidth(value) <= 1)
            {
                return "false";
            }
            if (graph.valueWidth(value) <= 64)
            {
                return "0";
            }
            return "{}";
        }

        std::string valueDebugName(const Graph &graph, ValueId value)
        {
            const std::string_view symbol = graph.symbolText(graph.valueSymbol(value));
            if (!symbol.empty())
            {
                return sanitizeIdentifier(symbol);
            }
            return "v" + std::to_string(value.index);
        }

        std::string opDebugName(const Graph &graph, OperationId op)
        {
            const std::string_view symbol = graph.symbolText(graph.operationSymbol(op));
            if (!symbol.empty())
            {
                return sanitizeIdentifier(symbol);
            }
            return "op" + std::to_string(op.index);
        }

        std::optional<std::string> constantExpr(const Graph &graph, const Operation &op, ValueId resultValue)
        {
            auto literal = getAttribute<std::string>(op, "constValue");
            if (!literal)
            {
                return std::nullopt;
            }
            switch (graph.valueType(resultValue))
            {
            case ValueType::String:
            {
                if (literal->size() >= 2 && literal->front() == '"' && literal->back() == '"')
                {
                    return *literal;
                }
                return "\"" + escapeCppString(*literal) + "\"";
            }
            case ValueType::Real:
                return *literal;
            case ValueType::Logic:
            default:
                break;
            }
            if (!isScalarLogicValue(graph, resultValue))
            {
                return std::nullopt;
            }
            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(*literal);
                parsed = parsed.resize(static_cast<slang::bitwidth_t>(graph.valueWidth(resultValue)));
                const auto raw = parsed.as<uint64_t>();
                if (!raw)
                {
                    return std::nullopt;
                }
                std::ostringstream out;
                out << "UINT64_C(" << *raw << ")";
                return out.str();
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        struct ScheduleRefs
        {
            const ActivityScheduleSupernodeToOps *supernodeToOps = nullptr;
            const ActivityScheduleSupernodeToOpSymbols *supernodeToOpSymbols = nullptr;
            const ActivityScheduleDag *dag = nullptr;
            const ActivityScheduleTopoOrder *topoOrder = nullptr;
            const ActivityScheduleHeadEvalSupernodes *headEvalSupernodes = nullptr;
            const ActivityScheduleSupernodeEventDomains *supernodeEventDomains = nullptr;
            const wolvrix::lib::transform::ActivityScheduleValueEventDomains *valueEventDomains = nullptr;
            const ActivityScheduleEventDomainSinks *eventDomainSinks = nullptr;
            const ActivityScheduleEventDomainSinkGroups *eventDomainSinkGroups = nullptr;
        };

        struct StateDecl
        {
            enum class Kind
            {
                Register,
                Latch,
                Memory
            };

            Kind kind = Kind::Register;
            std::string symbol;
            std::string fieldName;
            std::string cppType;
            int32_t width = 0;
            bool isSigned = false;
            int64_t rowCount = 0;
        };

        struct WriteDecl
        {
            OperationId opId;
            StateDecl::Kind kind = StateDecl::Kind::Register;
            std::string symbol;
            std::string pendingValidField;
            std::string pendingDataField;
            std::string pendingMaskField;
            std::string pendingAddrField;
        };

        struct DpiImportDecl
        {
            std::string symbol;
            std::vector<std::string> argsDirection;
            std::vector<int64_t> argsWidth;
            std::vector<std::string> argsName;
            std::vector<bool> argsSigned;
            std::vector<std::string> argsType;
            bool hasReturn = false;
            int64_t returnWidth = 0;
            bool returnSigned = false;
            std::string returnType;
        };

        struct EmitModel
        {
            std::unordered_map<ValueId, std::string, ValueIdHash> inputFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> prevInputFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> valueFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> prevEventFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> eventCurrentVarByValue;
            std::unordered_map<std::string, StateDecl> stateBySymbol;
            std::unordered_map<OperationId, WriteDecl, OperationIdHash> writeByOp;
            std::unordered_map<std::string, DpiImportDecl> dpiImportBySymbol;
            std::unordered_map<ActivityScheduleEventTerm, std::size_t, ActivityScheduleEventTermHash> eventTermIndex;
            std::unordered_map<ActivityScheduleEventDomainSignature, std::size_t, ActivityScheduleEventDomainSignatureHash>
                eventDomainIndex;
            std::vector<ActivityScheduleEventTerm> eventTerms;
            std::vector<bool> eventDomainPrecomputable;
            std::vector<WriteDecl> writes;
            std::vector<DpiImportDecl> dpiImports;
            std::vector<std::string> valueFieldDecls;
            std::vector<std::string> stateFieldDecls;
            std::vector<std::string> writeFieldDecls;
            std::vector<std::string> dpiDecls;
            std::vector<std::string> inputSetDecls;
            std::vector<std::string> outputGetDecls;
            std::vector<std::string> inputSetImpls;
            std::vector<std::string> outputGetImpls;
            std::vector<std::string> inputFieldDecls;
            std::vector<std::string> outputFieldDecls;
        };

        std::string dpiCppType(std::string_view typeName, int64_t width)
        {
            std::string lowered;
            lowered.reserve(typeName.size());
            for (unsigned char ch : typeName)
            {
                lowered.push_back(static_cast<char>(std::tolower(ch)));
            }
            if (lowered == "real" || lowered == "shortreal")
            {
                return "double";
            }
            if (lowered == "string")
            {
                return "std::string";
            }
            if (width <= 1)
            {
                return "bool";
            }
            if (width <= 8)
            {
                return "std::uint8_t";
            }
            if (width <= 16)
            {
                return "std::uint16_t";
            }
            if (width <= 32)
            {
                return "std::uint32_t";
            }
            return "std::uint64_t";
        }

        bool buildModel(const Graph &graph,
                        const ScheduleRefs &schedule,
                        EmitModel &model,
                        std::string &error)
        {
            for (const auto &port : graph.inputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                const std::string typeName = cppTypeForValue(graph, port.value);
                model.inputFieldByValue.emplace(port.value, "in_" + name);
                model.prevInputFieldByValue.emplace(port.value, "prev_in_" + name);
                model.inputFieldDecls.push_back("    " + typeName + " in_" + name + " = " + defaultInitExpr(graph, port.value) + ";");
                model.inputFieldDecls.push_back("    " + typeName + " prev_in_" + name + " = " + defaultInitExpr(graph, port.value) + ";");
                model.inputSetDecls.push_back("    void set_" + name + "(" + typeName + " value);");
                model.inputSetImpls.push_back("void CLASS::set_" + name + "(" + typeName + " value)\n{\n    in_" + name + " = value;\n}\n");
            }

            for (const auto &port : graph.outputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                const std::string typeName = cppTypeForValue(graph, port.value);
                model.outputFieldDecls.push_back("    " + typeName + " out_" + name + " = " + defaultInitExpr(graph, port.value) + ";");
                model.outputGetDecls.push_back("    [[nodiscard]] " + typeName + " get_" + name + "() const;");
                model.outputGetImpls.push_back(typeName + " CLASS::get_" + name + "() const\n{\n    return out_" + name + ";\n}\n");
            }

            for (ValueId valueId : graph.values())
            {
                if (model.inputFieldByValue.find(valueId) != model.inputFieldByValue.end())
                {
                    continue;
                }
                const std::string fieldName = "val_" + valueDebugName(graph, valueId) + "_" + std::to_string(valueId.index);
                model.valueFieldByValue.emplace(valueId, fieldName);
                model.valueFieldDecls.push_back("    " + cppTypeForValue(graph, valueId) + " " + fieldName + " = " + defaultInitExpr(graph, valueId) + ";");
            }

            for (OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                switch (op.kind())
                {
                case OperationKind::kRegister:
                case OperationKind::kLatch:
                case OperationKind::kMemory:
                {
                    auto width = getAttribute<int64_t>(op, "width");
                    auto isSigned = getAttribute<bool>(op, "isSigned");
                    if (!width || !isSigned)
                    {
                        error = "storage declaration missing width/isSigned: " + std::string(op.symbolText());
                        return false;
                    }
                    StateDecl state;
                    state.kind = op.kind() == OperationKind::kRegister
                                     ? StateDecl::Kind::Register
                                     : (op.kind() == OperationKind::kLatch ? StateDecl::Kind::Latch : StateDecl::Kind::Memory);
                    state.symbol = std::string(op.symbolText());
                    state.width = static_cast<int32_t>(*width);
                    state.isSigned = *isSigned;
                    if (state.kind == StateDecl::Kind::Memory)
                    {
                        auto row = getAttribute<int64_t>(op, "row");
                        if (!row || *row <= 0)
                        {
                            error = "memory declaration missing row: " + state.symbol;
                            return false;
                        }
                        state.rowCount = *row;
                        const std::string elemType =
                            dpiCppType("logic", state.width);
                        state.cppType = "std::vector<" + elemType + ">";
                        state.fieldName = "state_mem_" + sanitizeIdentifier(state.symbol);
                        std::ostringstream decl;
                        decl << "    " << state.cppType << " " << state.fieldName << ";";
                        model.stateFieldDecls.push_back(decl.str());
                    }
                    else
                    {
                        if (state.width <= 0 || state.width > 64)
                        {
                            error = "storage width >64 not implemented yet: " + state.symbol;
                            return false;
                        }
                        const std::string cppType = dpiCppType("logic", state.width);
                        state.cppType = cppType;
                        state.fieldName = (state.kind == StateDecl::Kind::Register ? "state_reg_" : "state_latch_") +
                                          sanitizeIdentifier(state.symbol);
                        model.stateFieldDecls.push_back("    " + cppType + " " + state.fieldName + " = 0;");
                    }
                    model.stateBySymbol.insert_or_assign(state.symbol, state);
                    break;
                }
                case OperationKind::kDpicImport:
                {
                    DpiImportDecl decl;
                    decl.symbol = std::string(op.symbolText());
                    decl.argsDirection = getAttribute<std::vector<std::string>>(op, "argsDirection").value_or(std::vector<std::string>{});
                    decl.argsWidth = getAttribute<std::vector<int64_t>>(op, "argsWidth").value_or(std::vector<int64_t>{});
                    decl.argsName = getAttribute<std::vector<std::string>>(op, "argsName").value_or(std::vector<std::string>{});
                    decl.argsSigned = getAttribute<std::vector<bool>>(op, "argsSigned").value_or(std::vector<bool>{});
                    decl.argsType = getAttribute<std::vector<std::string>>(op, "argsType").value_or(std::vector<std::string>{});
                    decl.hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);
                    decl.returnWidth = getAttribute<int64_t>(op, "returnWidth").value_or(64);
                    decl.returnSigned = getAttribute<bool>(op, "returnSigned").value_or(false);
                    decl.returnType = getAttribute<std::string>(op, "returnType").value_or(std::string("logic"));
                    model.dpiImportBySymbol.insert_or_assign(decl.symbol, decl);
                    model.dpiImports.push_back(decl);
                    break;
                }
                default:
                    break;
                }
            }

            for (OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.kind() == OperationKind::kRegisterWritePort ||
                    op.kind() == OperationKind::kLatchWritePort ||
                    op.kind() == OperationKind::kMemoryWritePort)
                {
                    WriteDecl write;
                    write.opId = opId;
                    write.kind = op.kind() == OperationKind::kRegisterWritePort
                                     ? StateDecl::Kind::Register
                                     : (op.kind() == OperationKind::kLatchWritePort ? StateDecl::Kind::Latch : StateDecl::Kind::Memory);
                    const char *symbolAttr = write.kind == StateDecl::Kind::Memory ? "memSymbol"
                                                                                    : (write.kind == StateDecl::Kind::Register ? "regSymbol" : "latchSymbol");
                    auto targetSymbol = getAttribute<std::string>(op, symbolAttr);
                    if (!targetSymbol)
                    {
                        error = std::string(symbolAttr) + " missing on write op: " + std::string(op.symbolText());
                        return false;
                    }
                    if (model.stateBySymbol.find(*targetSymbol) == model.stateBySymbol.end())
                    {
                        error = "write target declaration missing: " + *targetSymbol;
                        return false;
                    }
                    write.symbol = *targetSymbol;
                    const std::string base = "pending_" + opDebugName(graph, opId) + "_" + std::to_string(opId.index);
                    write.pendingValidField = base + "_valid";
                    write.pendingDataField = base + "_data";
                    write.pendingMaskField = base + "_mask";
                    write.pendingAddrField = base + "_addr";
                    model.writeByOp.emplace(opId, write);
                    model.writes.push_back(write);

                    const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                    model.writeFieldDecls.push_back("    bool " + write.pendingValidField + " = false;");
                    if (write.kind == StateDecl::Kind::Memory)
                    {
                        model.writeFieldDecls.push_back("    std::size_t " + write.pendingAddrField + " = 0;");
                    }
                    model.writeFieldDecls.push_back("    " + (write.kind == StateDecl::Kind::Memory ? dpiCppType("logic", state.width) : state.cppType) +
                                                    " " + write.pendingDataField +
                                                    (write.kind == StateDecl::Kind::Memory ? " = 0;" : " = 0;"));
                    model.writeFieldDecls.push_back("    " + (write.kind == StateDecl::Kind::Memory ? dpiCppType("logic", state.width) : state.cppType) +
                                                    " " + write.pendingMaskField +
                                                    (write.kind == StateDecl::Kind::Memory ? " = 0;" : " = 0;"));
                }
            }

            for (const auto &group : *schedule.eventDomainSinkGroups)
            {
                const std::size_t domainIndex = model.eventDomainIndex.size();
                model.eventDomainIndex.emplace(group.signature, domainIndex);
                model.eventDomainPrecomputable.push_back(true);
                for (const auto &term : group.signature.terms)
                {
                    if (model.eventTermIndex.find(term) == model.eventTermIndex.end())
                    {
                        const std::size_t termIndex = model.eventTerms.size();
                        model.eventTerms.push_back(term);
                        model.eventTermIndex.emplace(term, termIndex);
                        const std::string prevField = "prev_evt_" + valueDebugName(graph, term.value) + "_" + std::to_string(term.value.index);
                        model.prevEventFieldByValue.emplace(term.value, prevField);
                        model.eventCurrentVarByValue.emplace(term.value, "curr_evt_" + std::to_string(termIndex));
                    }
                }
            }

            for (const auto &[value, prevField] : model.prevEventFieldByValue)
            {
                model.valueFieldDecls.push_back("    " + cppTypeForValue(graph, value) + " " + prevField + " = " +
                                                defaultInitExpr(graph, value) + ";");
            }

            for (const auto &decl : model.dpiImports)
            {
                std::vector<std::string> args;
                const std::size_t argCount = decl.argsDirection.size();
                for (std::size_t i = 0; i < argCount; ++i)
                {
                    const std::string baseType =
                        dpiCppType(i < decl.argsType.size() ? decl.argsType[i] : std::string_view("logic"),
                                   i < decl.argsWidth.size() ? decl.argsWidth[i] : 64);
                    const std::string &direction = decl.argsDirection[i];
                    if (direction == "output" || direction == "inout")
                    {
                        args.push_back(baseType + " &" + sanitizeIdentifier(i < decl.argsName.size() ? decl.argsName[i] : ("arg" + std::to_string(i))));
                    }
                    else
                    {
                        args.push_back(baseType + " " + sanitizeIdentifier(i < decl.argsName.size() ? decl.argsName[i] : ("arg" + std::to_string(i))));
                    }
                }
                const std::string returnType = decl.hasReturn ? dpiCppType(decl.returnType, decl.returnWidth) : "void";
                model.dpiDecls.push_back("extern \"C\" " + returnType + " " + sanitizeIdentifier(decl.symbol) + "(" +
                                         joinStrings(args, ", ") + ");");
            }
            return true;
        }

        std::string valueRef(const EmitModel &model, ValueId value)
        {
            if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
            {
                return it->second;
            }
            if (auto it = model.valueFieldByValue.find(value); it != model.valueFieldByValue.end())
            {
                return it->second;
            }
            return "/*missing_value_ref*/";
        }

        std::optional<std::string> pureExprForValue(const Graph &graph,
                                                    const EmitModel &model,
                                                    ValueId value,
                                                    std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> &cache,
                                                    std::unordered_map<ValueId, std::size_t, ValueIdHash> &costCache,
                                                    std::size_t &totalOps)
        {
            if (auto it = cache.find(value); it != cache.end())
            {
                totalOps += costCache[value];
                return it->second;
            }

            std::optional<std::string> expr;
            std::size_t cost = 0;
            if (auto inputIt = model.inputFieldByValue.find(value); inputIt != model.inputFieldByValue.end())
            {
                expr = inputIt->second;
            }
            else
            {
                const Value val = graph.getValue(value);
                const OperationId defOpId = val.definingOp();
                if (!defOpId.valid())
                {
                    expr = valueRef(model, value);
                }
                else
                {
                    const Operation op = graph.getOperation(defOpId);
                    switch (op.kind())
                    {
                    case OperationKind::kConstant:
                        expr = constantExpr(graph, op, value);
                        break;
                    case OperationKind::kRegisterReadPort:
                    {
                        auto regSymbol = getAttribute<std::string>(op, "regSymbol");
                        if (regSymbol)
                        {
                            if (auto it = model.stateBySymbol.find(*regSymbol); it != model.stateBySymbol.end())
                            {
                                expr = it->second.fieldName;
                            }
                        }
                        break;
                    }
                    case OperationKind::kLatchReadPort:
                    {
                        auto latchSymbol = getAttribute<std::string>(op, "latchSymbol");
                        if (latchSymbol)
                        {
                            if (auto it = model.stateBySymbol.find(*latchSymbol); it != model.stateBySymbol.end())
                            {
                                expr = it->second.fieldName;
                            }
                        }
                        break;
                    }
                    case OperationKind::kAssign:
                    case OperationKind::kAdd:
                    case OperationKind::kSub:
                    case OperationKind::kAnd:
                    case OperationKind::kOr:
                    case OperationKind::kXor:
                    case OperationKind::kEq:
                    case OperationKind::kNe:
                    case OperationKind::kLt:
                    case OperationKind::kLe:
                    case OperationKind::kGt:
                    case OperationKind::kGe:
                    case OperationKind::kLogicAnd:
                    case OperationKind::kLogicOr:
                    case OperationKind::kNot:
                    case OperationKind::kLogicNot:
                    case OperationKind::kMux:
                    case OperationKind::kConcat:
                    case OperationKind::kSliceStatic:
                    {
                        if (!isScalarLogicValue(graph, value))
                        {
                            break;
                        }
                        const auto operands = op.operands();
                        std::vector<std::string> operandExprs;
                        operandExprs.reserve(operands.size());
                        for (ValueId operand : operands)
                        {
                            std::size_t operandCost = 0;
                            auto operandExpr = pureExprForValue(graph, model, operand, cache, costCache, operandCost);
                            if (!operandExpr)
                            {
                                operandExprs.clear();
                                break;
                            }
                            cost += operandCost;
                            operandExprs.push_back(*operandExpr);
                        }
                        if (operandExprs.size() != operands.size())
                        {
                            break;
                        }
                        cost += 1;
                        switch (op.kind())
                        {
                        case OperationKind::kAssign:
                            expr = "(" + operandExprs[0] + ")";
                            break;
                        case OperationKind::kAdd:
                            expr = "(" + operandExprs[0] + " + " + operandExprs[1] + ")";
                            break;
                        case OperationKind::kSub:
                            expr = "(" + operandExprs[0] + " - " + operandExprs[1] + ")";
                            break;
                        case OperationKind::kAnd:
                            expr = "(" + operandExprs[0] + " & " + operandExprs[1] + ")";
                            break;
                        case OperationKind::kOr:
                            expr = "(" + operandExprs[0] + " | " + operandExprs[1] + ")";
                            break;
                        case OperationKind::kXor:
                            expr = "(" + operandExprs[0] + " ^ " + operandExprs[1] + ")";
                            break;
                        case OperationKind::kEq:
                            expr = "((" + operandExprs[0] + ") == (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kNe:
                            expr = "((" + operandExprs[0] + ") != (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kLt:
                            expr = "((" + operandExprs[0] + ") < (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kLe:
                            expr = "((" + operandExprs[0] + ") <= (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kGt:
                            expr = "((" + operandExprs[0] + ") > (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kGe:
                            expr = "((" + operandExprs[0] + ") >= (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kLogicAnd:
                            expr = "((" + operandExprs[0] + ") && (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kLogicOr:
                            expr = "((" + operandExprs[0] + ") || (" + operandExprs[1] + "))";
                            break;
                        case OperationKind::kNot:
                            expr = "(~(" + operandExprs[0] + "))";
                            break;
                        case OperationKind::kLogicNot:
                            expr = "(!(" + operandExprs[0] + "))";
                            break;
                        case OperationKind::kMux:
                            expr = "((" + operandExprs[0] + ") ? (" + operandExprs[1] + ") : (" + operandExprs[2] + "))";
                            break;
                        case OperationKind::kConcat:
                        {
                            if (operands.size() != 2 || !isScalarLogicValue(graph, operands[1]))
                            {
                                break;
                            }
                            expr = "((" + operandExprs[0] + " << " + std::to_string(graph.valueWidth(operands[1])) +
                                   ") | (" + operandExprs[1] + "))";
                            break;
                        }
                        case OperationKind::kSliceStatic:
                        {
                            auto sliceStart = getAttribute<int64_t>(op, "sliceStart");
                            auto sliceEnd = getAttribute<int64_t>(op, "sliceEnd");
                            if (!sliceStart || !sliceEnd)
                            {
                                break;
                            }
                            const int64_t start = *sliceStart;
                            const int64_t width = *sliceEnd - *sliceStart + 1;
                            expr = "((" + operandExprs[0] + " >> " + std::to_string(start) + "))";
                            if (width < 64)
                            {
                                expr = "(((" + operandExprs[0] + " >> " + std::to_string(start) + ") & UINT64_C(" +
                                       std::to_string((UINT64_C(1) << width) - 1) + ")))";
                            }
                            break;
                        }
                        default:
                            break;
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }

            cache.emplace(value, expr);
            costCache.emplace(value, cost);
            totalOps += cost;
            return expr;
        }

        std::string scalarAssignmentExpr(OperationKind kind,
                                         const std::vector<std::string> &operands,
                                         const Operation &op,
                                         const Graph &graph)
        {
            switch (kind)
            {
            case OperationKind::kAssign:
                return operands[0];
            case OperationKind::kAdd:
                return "(" + operands[0] + " + " + operands[1] + ")";
            case OperationKind::kSub:
                return "(" + operands[0] + " - " + operands[1] + ")";
            case OperationKind::kAnd:
                return "(" + operands[0] + " & " + operands[1] + ")";
            case OperationKind::kOr:
                return "(" + operands[0] + " | " + operands[1] + ")";
            case OperationKind::kXor:
                return "(" + operands[0] + " ^ " + operands[1] + ")";
            case OperationKind::kNot:
                return "(~(" + operands[0] + "))";
            case OperationKind::kEq:
                return "((" + operands[0] + ") == (" + operands[1] + "))";
            case OperationKind::kNe:
                return "((" + operands[0] + ") != (" + operands[1] + "))";
            case OperationKind::kLt:
                return "((" + operands[0] + ") < (" + operands[1] + "))";
            case OperationKind::kLe:
                return "((" + operands[0] + ") <= (" + operands[1] + "))";
            case OperationKind::kGt:
                return "((" + operands[0] + ") > (" + operands[1] + "))";
            case OperationKind::kGe:
                return "((" + operands[0] + ") >= (" + operands[1] + "))";
            case OperationKind::kLogicAnd:
                return "((" + operands[0] + ") && (" + operands[1] + "))";
            case OperationKind::kLogicOr:
                return "((" + operands[0] + ") || (" + operands[1] + "))";
            case OperationKind::kLogicNot:
                return "(!(" + operands[0] + "))";
            case OperationKind::kMux:
                return "((" + operands[0] + ") ? (" + operands[1] + ") : (" + operands[2] + "))";
            case OperationKind::kConcat:
                return "((" + operands[0] + " << " + std::to_string(graph.valueWidth(op.operands()[1])) + ") | (" + operands[1] + "))";
            case OperationKind::kSliceStatic:
            {
                const auto sliceStart = getAttribute<int64_t>(op, "sliceStart").value_or(0);
                const auto sliceEnd = getAttribute<int64_t>(op, "sliceEnd").value_or(sliceStart);
                const int64_t width = sliceEnd - sliceStart + 1;
                if (width >= 64)
                {
                    return "((" + operands[0] + ") >> " + std::to_string(sliceStart) + ")";
                }
                std::uint64_t mask = width <= 0 ? 0u : ((UINT64_C(1) << width) - 1u);
                return "(((" + operands[0] + ") >> " + std::to_string(sliceStart) + ") & UINT64_C(" + std::to_string(mask) + "))";
            }
            default:
                break;
            }
            return {};
        }

        std::string exactEventExpr(const Graph &graph,
                                   const EmitModel &model,
                                   const Operation &op)
        {
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            const auto operands = op.operands();
            if (!eventEdges || eventEdges->empty())
            {
                return "true";
            }
            std::size_t eventStart = operands.size() - eventEdges->size();
            if (op.kind() == OperationKind::kRegisterWritePort)
            {
                eventStart = 3;
            }
            else if (op.kind() == OperationKind::kMemoryWritePort)
            {
                eventStart = 4;
            }

            std::vector<std::string> parts;
            for (std::size_t i = 0; i < eventEdges->size(); ++i)
            {
                if (eventStart + i >= operands.size())
                {
                    continue;
                }
                const ValueId value = operands[eventStart + i];
                std::string current = valueRef(model, value);
                auto prevIt = model.prevEventFieldByValue.find(value);
                if (prevIt == model.prevEventFieldByValue.end())
                {
                    parts.push_back("true");
                    continue;
                }
                const std::string &edge = (*eventEdges)[i];
                if (edge == "posedge")
                {
                    parts.push_back("grhsim_event_posedge(" + current + ", " + prevIt->second + ")");
                }
                else if (edge == "negedge")
                {
                    parts.push_back("grhsim_event_negedge(" + current + ", " + prevIt->second + ")");
                }
                else
                {
                    parts.push_back("((" + current + ") != (" + prevIt->second + "))");
                }
            }
            if (parts.empty())
            {
                return "true";
            }
            return "(" + joinStrings(parts, " || ") + ")";
        }

    } // namespace

    EmitResult EmitGrhSimCpp::emitImpl(const wolvrix::lib::grh::Design & /*design*/,
                                       std::span<const Graph *const> topGraphs,
                                       const EmitOptions &options)
    {
        EmitResult result;
        if (topGraphs.size() != 1)
        {
            reportError("emit grhsim-cpp expects exactly one top graph");
            result.success = false;
            return result;
        }
        if (!options.outputDir || options.outputDir->empty())
        {
            reportError("emit grhsim-cpp requires outputDir");
            result.success = false;
            return result;
        }
        if (options.session == nullptr)
        {
            reportError("emit grhsim-cpp requires session data");
            result.success = false;
            return result;
        }

        const auto &graph = *topGraphs.front();
        const std::string sessionPrefix = resolveSessionPathPrefix(graph, options) + ".activity_schedule.";
        ScheduleRefs schedule;
        schedule.supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(options, sessionPrefix + "supernode_to_ops");
        schedule.supernodeToOpSymbols =
            getSessionValue<ActivityScheduleSupernodeToOpSymbols>(options, sessionPrefix + "supernode_to_op_symbols");
        schedule.dag =
            getSessionValue<ActivityScheduleDag>(options, sessionPrefix + "dag");
        schedule.topoOrder =
            getSessionValue<ActivityScheduleTopoOrder>(options, sessionPrefix + "topo_order");
        schedule.headEvalSupernodes =
            getSessionValue<ActivityScheduleHeadEvalSupernodes>(options, sessionPrefix + "head_eval_supernodes");
        schedule.supernodeEventDomains =
            getSessionValue<ActivityScheduleSupernodeEventDomains>(options, sessionPrefix + "supernode_event_domains");
        schedule.valueEventDomains =
            getSessionValue<wolvrix::lib::transform::ActivityScheduleValueEventDomains>(options,
                                                                                        sessionPrefix + "value_event_domains");
        schedule.eventDomainSinks =
            getSessionValue<ActivityScheduleEventDomainSinks>(options, sessionPrefix + "event_domain_sinks");
        schedule.eventDomainSinkGroups =
            getSessionValue<ActivityScheduleEventDomainSinkGroups>(options, sessionPrefix + "event_domain_sink_groups");
        if (schedule.supernodeToOps == nullptr || schedule.dag == nullptr || schedule.topoOrder == nullptr ||
            schedule.headEvalSupernodes == nullptr || schedule.supernodeEventDomains == nullptr ||
            schedule.valueEventDomains == nullptr || schedule.eventDomainSinks == nullptr ||
            schedule.eventDomainSinkGroups == nullptr)
        {
            reportError("missing activity-schedule session data", sessionPrefix);
            result.success = false;
            return result;
        }

        EmitModel model;
        std::string buildError;
        if (!buildModel(graph, schedule, model, buildError))
        {
            reportError(buildError, graph.symbol());
            result.success = false;
            return result;
        }

        const std::size_t eventPrecomputeMaxOps = parseEventPrecomputeMaxOps(options);
        std::vector<uint32_t> sourceSupernodeList;
        {
            std::vector<std::size_t> indegree(schedule.dag->size(), 0);
            for (std::size_t supernodeId = 0; supernodeId < schedule.dag->size(); ++supernodeId)
            {
                for (uint32_t succ : (*schedule.dag)[supernodeId])
                {
                    if (succ < indegree.size())
                    {
                        ++indegree[succ];
                    }
                }
            }
            for (std::size_t supernodeId = 0; supernodeId < indegree.size(); ++supernodeId)
            {
                if (indegree[supernodeId] == 0)
                {
                    sourceSupernodeList.push_back(static_cast<uint32_t>(supernodeId));
                }
            }
        }
        for (const auto &[signature, index] : model.eventDomainIndex)
        {
            bool precomputable = true;
            for (const auto &term : signature.terms)
            {
                std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> cache;
                std::unordered_map<ValueId, std::size_t, ValueIdHash> costCache;
                std::size_t totalOps = 0;
                auto expr = pureExprForValue(graph, model, term.value, cache, costCache, totalOps);
                if (!expr || totalOps > eventPrecomputeMaxOps)
                {
                    precomputable = false;
                    break;
                }
            }
            model.eventDomainPrecomputable[index] = precomputable;
        }

        const std::filesystem::path outDir = resolveOutputDir(options);
        const std::string normalizedTop = sanitizeIdentifier(graph.symbol());
        const std::string className = "GrhSIM_" + normalizedTop;
        const std::string prefix = "grhsim_" + normalizedTop;
        const std::filesystem::path headerPath = outDir / (prefix + ".hpp");
        const std::filesystem::path runtimePath = outDir / (prefix + "_runtime.hpp");
        const std::filesystem::path statePath = outDir / (prefix + "_state.cpp");
        const std::filesystem::path evalPath = outDir / (prefix + "_eval.cpp");
        const std::filesystem::path schedPath = outDir / (prefix + "_sched_0.cpp");
        const std::filesystem::path makefilePath = outDir / "Makefile";

        auto replaceClassMarker = [&](std::string text) -> std::string
        {
            std::size_t pos = 0;
            while ((pos = text.find("CLASS", pos)) != std::string::npos)
            {
                text.replace(pos, 5, className);
                pos += className.size();
            }
            return text;
        };

        {
            auto stream = openOutputFile(runtimePath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "#pragma once\n\n";
            *stream << "#include <cstddef>\n";
            *stream << "#include <cstdint>\n";
            *stream << "#include <vector>\n\n";
            *stream << "inline std::uint64_t grhsim_mask(std::size_t width)\n{\n";
            *stream << "    if (width == 0) return UINT64_C(0);\n";
            *stream << "    if (width >= 64) return ~UINT64_C(0);\n";
            *stream << "    return (UINT64_C(1) << width) - UINT64_C(1);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_trunc_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return value & grhsim_mask(width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_event_posedge(std::uint64_t curr, std::uint64_t prev)\n{\n";
            *stream << "    return curr != 0 && prev == 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_event_negedge(std::uint64_t curr, std::uint64_t prev)\n{\n";
            *stream << "    return curr == 0 && prev != 0;\n";
            *stream << "}\n\n";
            *stream << "inline void grhsim_set_bit(std::vector<std::uint64_t> &bits, std::size_t index)\n{\n";
            *stream << "    bits[index >> 6] |= (UINT64_C(1) << (index & 63u));\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_test_bit(const std::vector<std::uint64_t> &bits, std::size_t index)\n{\n";
            *stream << "    return (bits[index >> 6] & (UINT64_C(1) << (index & 63u))) != 0;\n";
            *stream << "}\n";
        }

        {
            auto stream = openOutputFile(headerPath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "#pragma once\n\n";
            *stream << "#include <array>\n";
            *stream << "#include <cstddef>\n";
            *stream << "#include <cstdint>\n";
            *stream << "#include <string>\n";
            *stream << "#include <vector>\n\n";
            *stream << "#include \"" << runtimePath.filename().string() << "\"\n\n";
            for (const auto &decl : model.dpiDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.dpiDecls.empty())
            {
                *stream << '\n';
            }
            *stream << "class " << className << " {\n";
            *stream << "public:\n";
            *stream << "    static constexpr std::size_t kSupernodeCount = " << schedule.supernodeToOps->size() << ";\n";
            *stream << "    static constexpr std::size_t kEventTermCount = " << model.eventTerms.size() << ";\n";
            *stream << "    static constexpr std::size_t kEventDomainCount = " << schedule.eventDomainSinkGroups->size() << ";\n";
            *stream << "    static constexpr std::size_t kEventPrecomputeMaxOps = " << eventPrecomputeMaxOps << ";\n\n";
            *stream << "    " << className << "();\n";
            for (const auto &decl : model.inputSetDecls)
            {
                *stream << decl << '\n';
            }
            *stream << "    void eval();\n";
            for (const auto &decl : model.outputGetDecls)
            {
                *stream << decl << '\n';
            }
            *stream << "\nprivate:\n";
            *stream << "    void eval_batch_0();\n";
            *stream << "    void commit_state_updates();\n";
            *stream << "    void refresh_outputs();\n\n";
            *stream << "    bool first_eval_ = true;\n";
            *stream << "    bool state_feedback_pending_ = true;\n";
            *stream << "    bool side_effect_feedback_ = false;\n";
            *stream << "    std::vector<std::uint64_t> supernode_active_curr_;\n";
            *stream << "    std::vector<bool> event_term_hit_;\n";
            *stream << "    std::vector<bool> event_domain_hit_;\n\n";
            for (const auto &decl : model.inputFieldDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.inputFieldDecls.empty())
            {
                *stream << '\n';
            }
            for (const auto &decl : model.outputFieldDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.outputFieldDecls.empty())
            {
                *stream << '\n';
            }
            for (const auto &decl : model.valueFieldDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.valueFieldDecls.empty())
            {
                *stream << '\n';
            }
            for (const auto &decl : model.stateFieldDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.stateFieldDecls.empty())
            {
                *stream << '\n';
            }
            for (const auto &decl : model.writeFieldDecls)
            {
                *stream << decl << '\n';
            }
            *stream << "};\n";
        }

        {
            auto stream = openOutputFile(statePath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            *stream << className << "::" << className << "()\n";
            *stream << "    : supernode_active_curr_((kSupernodeCount + 63u) / 64u, 0),\n";
            *stream << "      event_term_hit_(kEventTermCount, false),\n";
            *stream << "      event_domain_hit_(kEventDomainCount, true)\n";
            *stream << "{\n";
            for (const auto &[symbol, state] : model.stateBySymbol)
            {
                if (state.kind == StateDecl::Kind::Memory)
                {
                    *stream << "    " << state.fieldName << ".assign(" << state.rowCount << ", 0);\n";
                }
            }
            *stream << "}\n\n";
            for (const auto &impl : model.inputSetImpls)
            {
                *stream << replaceClassMarker(impl) << '\n';
            }
            for (const auto &impl : model.outputGetImpls)
            {
                *stream << replaceClassMarker(impl) << '\n';
            }
            *stream << "void " << className << "::commit_state_updates()\n{\n";
            *stream << "    bool any_state_change = false;\n";
            for (const auto &write : model.writes)
            {
                const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                *stream << "    if (" << write.pendingValidField << ") {\n";
                if (state.kind == StateDecl::Kind::Memory)
                {
                    *stream << "        const std::size_t row = " << write.pendingAddrField << " % " << state.rowCount << ";\n";
                    *stream << "        const auto merged = static_cast<" << dpiCppType("logic", state.width) << ">((" << state.fieldName
                            << "[row] & ~" << write.pendingMaskField << ") | (" << write.pendingDataField << " & " << write.pendingMaskField << "));\n";
                    *stream << "        if (" << state.fieldName << "[row] != merged) {\n";
                    *stream << "            " << state.fieldName << "[row] = merged;\n";
                    *stream << "            any_state_change = true;\n";
                    *stream << "        }\n";
                }
                else
                {
                    *stream << "        const auto merged = static_cast<" << state.cppType << ">((" << state.fieldName
                            << " & ~" << write.pendingMaskField << ") | (" << write.pendingDataField << " & " << write.pendingMaskField << "));\n";
                    *stream << "        if (" << state.fieldName << " != merged) {\n";
                    *stream << "            " << state.fieldName << " = merged;\n";
                    *stream << "            any_state_change = true;\n";
                    *stream << "        }\n";
                }
                *stream << "        " << write.pendingValidField << " = false;\n";
                *stream << "    }\n";
            }
            *stream << "    state_feedback_pending_ = state_feedback_pending_ || any_state_change;\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::refresh_outputs()\n{\n";
            for (const auto &port : graph.outputPorts())
            {
                *stream << "    out_" << sanitizeIdentifier(port.name) << " = " << valueRef(model, port.value) << ";\n";
            }
            *stream << "}\n";
        }

        {
            auto stream = openOutputFile(evalPath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            *stream << "void " << className << "::eval()\n{\n";
            *stream << "    const bool initial_eval = first_eval_;\n";
            *stream << "    bool seed_head_eval = initial_eval || state_feedback_pending_ || side_effect_feedback_;\n";
            for (const auto &port : graph.inputPorts())
            {
                const std::string current = model.inputFieldByValue.at(port.value);
                const std::string prev = model.prevInputFieldByValue.at(port.value);
                *stream << "    if (" << current << " != " << prev << ") {\n";
                *stream << "        seed_head_eval = true;\n";
                *stream << "    }\n";
            }
            *stream << "    std::fill(supernode_active_curr_.begin(), supernode_active_curr_.end(), 0);\n";
            *stream << "    std::fill(event_term_hit_.begin(), event_term_hit_.end(), false);\n";
            *stream << "    std::fill(event_domain_hit_.begin(), event_domain_hit_.end(), true);\n";
            *stream << "    first_eval_ = false;\n";
            *stream << "    state_feedback_pending_ = false;\n";
            *stream << "    side_effect_feedback_ = false;\n\n";

            for (std::size_t termIndex = 0; termIndex < model.eventTerms.size(); ++termIndex)
            {
                const auto &term = model.eventTerms[termIndex];
                std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> cache;
                std::unordered_map<ValueId, std::size_t, ValueIdHash> costCache;
                std::size_t totalOps = 0;
                auto expr = pureExprForValue(graph, model, term.value, cache, costCache, totalOps);
                const std::string currVar = model.eventCurrentVarByValue.at(term.value);
                if (expr && totalOps <= eventPrecomputeMaxOps)
                {
                    *stream << "    const auto " << currVar << " = " << *expr << ";\n";
                    if (term.edge == "posedge")
                    {
                        *stream << "    event_term_hit_[" << termIndex << "] = grhsim_event_posedge(" << currVar << ", "
                                << model.prevEventFieldByValue.at(term.value) << ");\n";
                    }
                    else if (term.edge == "negedge")
                    {
                        *stream << "    event_term_hit_[" << termIndex << "] = grhsim_event_negedge(" << currVar << ", "
                                << model.prevEventFieldByValue.at(term.value) << ");\n";
                    }
                    else
                    {
                        *stream << "    event_term_hit_[" << termIndex << "] = (" << currVar << " != "
                                << model.prevEventFieldByValue.at(term.value) << ");\n";
                    }
                }
                else
                {
                    *stream << "    const auto " << currVar << " = " << valueRef(model, term.value) << ";\n";
                    *stream << "    event_term_hit_[" << termIndex << "] = false;\n";
                }
            }
            *stream << '\n';

            for (const auto &group : *schedule.eventDomainSinkGroups)
            {
                const std::size_t domainIndex = model.eventDomainIndex.at(group.signature);
                if (group.signature.empty())
                {
                    *stream << "    event_domain_hit_[" << domainIndex << "] = true;\n";
                    continue;
                }
                if (!model.eventDomainPrecomputable[domainIndex])
                {
                    *stream << "    event_domain_hit_[" << domainIndex << "] = true;\n";
                    continue;
                }
                std::vector<std::string> parts;
                for (const auto &term : group.signature.terms)
                {
                    parts.push_back("event_term_hit_[" + std::to_string(model.eventTermIndex.at(term)) + "]");
                }
                *stream << "    event_domain_hit_[" << domainIndex << "] = " << joinStrings(parts, " || ") << ";\n";
            }
            *stream << '\n';

            *stream << "    if (seed_head_eval) {\n";
            for (uint32_t supernodeId : *schedule.headEvalSupernodes)
            {
                *stream << "        grhsim_set_bit(supernode_active_curr_, " << supernodeId << ");\n";
            }
            for (uint32_t supernodeId : sourceSupernodeList)
            {
                *stream << "        grhsim_set_bit(supernode_active_curr_, " << supernodeId << ");\n";
            }
            *stream << "    }\n\n";
            *stream << "    eval_batch_0();\n";
            *stream << "    commit_state_updates();\n";
            *stream << "    refresh_outputs();\n\n";
            for (const auto &port : graph.inputPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.value) << " = " << model.inputFieldByValue.at(port.value) << ";\n";
            }
            for (const auto &[value, prevField] : model.prevEventFieldByValue)
            {
                *stream << "    " << prevField << " = " << model.eventCurrentVarByValue.at(value) << ";\n";
            }
            *stream << "}\n";
        }

        {
            auto stream = openOutputFile(schedPath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            *stream << "#include <cstdlib>\n";
            *stream << "#include <iostream>\n\n";
            *stream << "void " << className << "::eval_batch_0()\n{\n";
            for (uint32_t supernodeId : *schedule.topoOrder)
            {
                std::vector<std::string> domainParts;
                for (const auto &signature : (*schedule.supernodeEventDomains)[supernodeId])
                {
                    if (signature.empty())
                    {
                        domainParts.push_back("true");
                    }
                    else
                    {
                        const std::size_t domainIndex = model.eventDomainIndex.at(signature);
                        if (!model.eventDomainPrecomputable[domainIndex])
                        {
                            domainParts.push_back("true");
                        }
                        else
                        {
                            domainParts.push_back("event_domain_hit_[" + std::to_string(domainIndex) + "]");
                        }
                    }
                }
                const std::string guardExpr = domainParts.empty() ? "true" : "(" + joinStrings(domainParts, " || ") + ")";
                *stream << "    if (!grhsim_test_bit(supernode_active_curr_, " << supernodeId << ") || !(" << guardExpr << ")) {\n";
                *stream << "        goto supernode_" << supernodeId << "_end;\n";
                *stream << "    }\n";
                *stream << "    {\n";
                *stream << "        bool supernode_changed = false;\n";
                for (const auto opId : (*schedule.supernodeToOps)[supernodeId])
                {
                    const Operation op = graph.getOperation(opId);
                    const auto operands = op.operands();
                    *stream << "        // op " << op.symbolText() << "\n";
                    switch (op.kind())
                    {
                    case OperationKind::kConstant:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        const ValueId resultValue = op.results().front();
                        const auto expr = constantExpr(graph, op, resultValue);
                        if (!expr)
                        {
                            reportError("unsupported constant emit", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        const std::string lhs = valueRef(model, resultValue);
                        if (graph.valueType(resultValue) == ValueType::Logic)
                        {
                            *stream << "        {\n";
                            *stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                                    << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << *expr << "), " << graph.valueWidth(resultValue) << "));\n";
                            *stream << "            if (" << lhs << " != next_value) { " << lhs << " = next_value; supernode_changed = true; }\n";
                            *stream << "        }\n";
                        }
                        else
                        {
                            *stream << "        { const auto next_value = " << *expr << "; if (" << lhs
                                    << " != next_value) { " << lhs << " = next_value; supernode_changed = true; } }\n";
                        }
                        break;
                    }
                    case OperationKind::kRegisterReadPort:
                    case OperationKind::kLatchReadPort:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        auto targetSymbol = getAttribute<std::string>(op, op.kind() == OperationKind::kRegisterReadPort ? "regSymbol" : "latchSymbol");
                        if (!targetSymbol)
                        {
                            reportError("storage read missing symbol", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        const auto stateIt = model.stateBySymbol.find(*targetSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            reportError("storage read target missing", *targetSymbol);
                            result.success = false;
                            return result;
                        }
                        const std::string lhs = valueRef(model, op.results().front());
                        *stream << "        if (" << lhs << " != " << stateIt->second.fieldName << ") { "
                                << lhs << " = " << stateIt->second.fieldName << "; supernode_changed = true; }\n";
                        break;
                    }
                    case OperationKind::kMemoryReadPort:
                    {
                        if (op.results().empty() || operands.empty())
                        {
                            break;
                        }
                        auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                        if (!memSymbol)
                        {
                            reportError("memory read missing memSymbol", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        const auto stateIt = model.stateBySymbol.find(*memSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            reportError("memory read target missing", *memSymbol);
                            result.success = false;
                            return result;
                        }
                        const StateDecl &state = stateIt->second;
                        const std::string lhs = valueRef(model, op.results().front());
                        const std::string addrExpr = valueRef(model, operands[0]);
                        *stream << "        {\n";
                        *stream << "            const std::size_t row = static_cast<std::size_t>(" << addrExpr << ") % " << state.rowCount << ";\n";
                        *stream << "            const auto next_value = " << state.fieldName << "[row];\n";
                        *stream << "            if (" << lhs << " != next_value) { " << lhs << " = next_value; supernode_changed = true; }\n";
                        *stream << "        }\n";
                        break;
                    }
                    case OperationKind::kAssign:
                    case OperationKind::kAdd:
                    case OperationKind::kSub:
                    case OperationKind::kAnd:
                    case OperationKind::kOr:
                    case OperationKind::kXor:
                    case OperationKind::kNot:
                    case OperationKind::kEq:
                    case OperationKind::kNe:
                    case OperationKind::kLt:
                    case OperationKind::kLe:
                    case OperationKind::kGt:
                    case OperationKind::kGe:
                    case OperationKind::kLogicAnd:
                    case OperationKind::kLogicOr:
                    case OperationKind::kLogicNot:
                    case OperationKind::kMux:
                    case OperationKind::kConcat:
                    case OperationKind::kSliceStatic:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        const ValueId resultValue = op.results().front();
                        if (!isScalarLogicValue(graph, resultValue))
                        {
                            reportError("non-scalar op emit not implemented", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        std::vector<std::string> operandExprs;
                        operandExprs.reserve(operands.size());
                        for (ValueId operand : operands)
                        {
                            operandExprs.push_back(valueRef(model, operand));
                        }
                        const std::string rhs = scalarAssignmentExpr(op.kind(), operandExprs, op, graph);
                        if (rhs.empty())
                        {
                            reportError("unsupported scalar expression emit", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        const std::string lhs = valueRef(model, resultValue);
                        *stream << "        {\n";
                        *stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                                << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << rhs << "), " << graph.valueWidth(resultValue) << "));\n";
                        *stream << "            if (" << lhs << " != next_value) { " << lhs << " = next_value; supernode_changed = true; }\n";
                        *stream << "        }\n";
                        break;
                    }
                    case OperationKind::kRegisterWritePort:
                    case OperationKind::kLatchWritePort:
                    case OperationKind::kMemoryWritePort:
                    {
                        const auto writeIt = model.writeByOp.find(opId);
                        if (writeIt == model.writeByOp.end())
                        {
                            reportError("write metadata missing", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        const WriteDecl &write = writeIt->second;
                        const std::string condExpr = operands.empty() ? "true" : valueRef(model, operands[0]);
                        const std::string eventExpr = exactEventExpr(graph, model, op);
                        *stream << "        if ((" << condExpr << ") && (" << eventExpr << ")) {\n";
                        if (write.kind == StateDecl::Kind::Memory)
                        {
                            *stream << "            " << write.pendingAddrField << " = static_cast<std::size_t>(" << valueRef(model, operands[1]) << ");\n";
                            *stream << "            " << write.pendingDataField << " = " << valueRef(model, operands[2]) << ";\n";
                            *stream << "            " << write.pendingMaskField << " = " << valueRef(model, operands[3]) << ";\n";
                        }
                        else
                        {
                            *stream << "            " << write.pendingDataField << " = " << valueRef(model, operands[1]) << ";\n";
                            *stream << "            " << write.pendingMaskField << " = " << valueRef(model, operands[2]) << ";\n";
                        }
                        *stream << "            " << write.pendingValidField << " = true;\n";
                        *stream << "        }\n";
                        break;
                    }
                    case OperationKind::kSystemTask:
                    {
                        if (operands.empty())
                        {
                            break;
                        }
                        const std::string condExpr = valueRef(model, operands[0]);
                        const std::string eventExpr = exactEventExpr(graph, model, op);
                        const auto name = getAttribute<std::string>(op, "name").value_or(std::string("display"));
                        const std::size_t eventCount = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}).size();
                        const std::size_t argEnd = operands.size() >= eventCount ? operands.size() - eventCount : operands.size();
                        const std::string streamName = (name == "error" || name == "warning") ? "std::cerr" : "std::cout";
                        *stream << "        if ((" << condExpr << ") && (" << eventExpr << ")) {\n";
                        *stream << "            " << streamName << " << \"[" << name << "]\"";
                        for (std::size_t i = 1; i < argEnd; ++i)
                        {
                            *stream << " << \" \" << " << valueRef(model, operands[i]);
                        }
                        *stream << " << std::endl;\n";
                        if (name == "fatal" || name == "finish")
                        {
                            *stream << "            std::abort();\n";
                        }
                        *stream << "        }\n";
                        break;
                    }
                    case OperationKind::kDpicCall:
                    {
                        const auto targetImport = getAttribute<std::string>(op, "targetImportSymbol");
                        const auto inArgName = getAttribute<std::vector<std::string>>(op, "inArgName").value_or(std::vector<std::string>{});
                        const auto outArgName = getAttribute<std::vector<std::string>>(op, "outArgName").value_or(std::vector<std::string>{});
                        const auto inoutArgName = getAttribute<std::vector<std::string>>(op, "inoutArgName").value_or(std::vector<std::string>{});
                        const bool hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);
                        if (!targetImport)
                        {
                            reportError("kDpicCall missing targetImportSymbol", std::string(op.symbolText()));
                            result.success = false;
                            return result;
                        }
                        auto importIt = model.dpiImportBySymbol.find(*targetImport);
                        if (importIt == model.dpiImportBySymbol.end())
                        {
                            reportError("kDpicCall target import not found", *targetImport);
                            result.success = false;
                            return result;
                        }
                        const DpiImportDecl &decl = importIt->second;
                        const std::size_t eventCount = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}).size();
                        const std::size_t inputCount = inArgName.size();
                        const std::size_t inoutCount = inoutArgName.size();
                        const std::string condExpr = operands.empty() ? "true" : valueRef(model, operands[0]);
                        const std::string eventExpr = exactEventExpr(graph, model, op);
                        *stream << "        if ((" << condExpr << ") && (" << eventExpr << ")) {\n";
                        std::vector<std::string> callArgs;
                        std::size_t operandCursor = 1;
                        std::size_t resultCursor = 0;
                        if (hasReturn)
                        {
                            const ValueId returnValue = op.results()[0];
                            *stream << "            auto dpi_ret = " << sanitizeIdentifier(decl.symbol) << "(";
                            std::vector<std::string> deferredArgs;
                            operandCursor = 1;
                            resultCursor = 1;
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                const std::string argType = dpiCppType(i < decl.argsType.size() ? decl.argsType[i] : std::string_view("logic"),
                                                                       i < decl.argsWidth.size() ? decl.argsWidth[i] : 64);
                                if (direction == "input")
                                {
                                    deferredArgs.push_back(valueRef(model, operands[operandCursor++]));
                                }
                                else if (direction == "output")
                                {
                                    const std::string tempName = "dpi_out_" + std::to_string(i);
                                    *stream << "\n            " << argType << " " << tempName << "{};";
                                    deferredArgs.push_back(tempName);
                                }
                                else
                                {
                                    const std::string tempName = "dpi_inout_" + std::to_string(i);
                                    *stream << "\n            " << argType << " " << tempName << " = " << valueRef(model, operands[operandCursor++]) << ";";
                                    deferredArgs.push_back(tempName);
                                }
                            }
                            *stream << joinStrings(deferredArgs, ", ") << ");\n";
                            *stream << "            if (" << valueRef(model, returnValue) << " != dpi_ret) { "
                                    << valueRef(model, returnValue) << " = dpi_ret; supernode_changed = true; }\n";
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                if (direction == "output" || direction == "inout")
                                {
                                    const std::string tempName = (direction == "output" ? "dpi_out_" : "dpi_inout_") + std::to_string(i);
                                    if (resultCursor >= op.results().size())
                                    {
                                        continue;
                                    }
                                    const std::string lhs = valueRef(model, op.results()[resultCursor++]);
                                    *stream << "            if (" << lhs << " != " << tempName << ") { " << lhs
                                            << " = " << tempName << "; supernode_changed = true; }\n";
                                }
                            }
                        }
                        else
                        {
                            std::vector<std::string> deferredArgs;
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                const std::string argType = dpiCppType(i < decl.argsType.size() ? decl.argsType[i] : std::string_view("logic"),
                                                                       i < decl.argsWidth.size() ? decl.argsWidth[i] : 64);
                                if (direction == "input")
                                {
                                    deferredArgs.push_back(valueRef(model, operands[operandCursor++]));
                                }
                                else if (direction == "output")
                                {
                                    const std::string tempName = "dpi_out_" + std::to_string(i);
                                    *stream << "            " << argType << " " << tempName << "{};\n";
                                    deferredArgs.push_back(tempName);
                                }
                                else
                                {
                                    const std::string tempName = "dpi_inout_" + std::to_string(i);
                                    *stream << "            " << argType << " " << tempName << " = " << valueRef(model, operands[operandCursor++]) << ";\n";
                                    deferredArgs.push_back(tempName);
                                }
                            }
                            *stream << "            " << sanitizeIdentifier(decl.symbol) << "(" << joinStrings(deferredArgs, ", ") << ");\n";
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                if (direction == "output" || direction == "inout")
                                {
                                    const std::string tempName = (direction == "output" ? "dpi_out_" : "dpi_inout_") + std::to_string(i);
                                    if (resultCursor >= op.results().size())
                                    {
                                        continue;
                                    }
                                    const std::string lhs = valueRef(model, op.results()[resultCursor++]);
                                    *stream << "            if (" << lhs << " != " << tempName << ") { " << lhs
                                            << " = " << tempName << "; supernode_changed = true; }\n";
                                }
                            }
                        }
                        (void)inputCount;
                        (void)inoutCount;
                        (void)eventCount;
                        *stream << "        }\n";
                        break;
                    }
                    case OperationKind::kRegister:
                    case OperationKind::kLatch:
                    case OperationKind::kMemory:
                    case OperationKind::kDpicImport:
                        break;
                    default:
                        reportError("unsupported op kind in grhsim-cpp emit", std::string(op.symbolText()));
                        result.success = false;
                        return result;
                    }
                }
                *stream << "        if (supernode_changed) {\n";
                for (uint32_t succ : (*schedule.dag)[supernodeId])
                {
                    *stream << "            grhsim_set_bit(supernode_active_curr_, " << succ << ");\n";
                }
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "supernode_" << supernodeId << "_end:\n";
            }
            *stream << "    return;\n";
            *stream << "}\n";
        }

        {
            auto stream = openOutputFile(makefilePath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "CXX ?= c++\n";
            *stream << "AR ?= ar\n";
            *stream << "ARFLAGS ?= rcs\n";
            *stream << "CXXFLAGS ?= -std=c++20 -O3\n";
            *stream << "LIB := lib" << prefix << ".a\n";
            *stream << "SRCS := " << statePath.filename().string() << ' ' << evalPath.filename().string() << ' ' << schedPath.filename().string() << "\n";
            *stream << "OBJS := $(SRCS:.cpp=.o)\n\n";
            *stream << "all: $(LIB)\n\n";
            *stream << "$(LIB): $(OBJS)\n";
            *stream << "\t$(AR) $(ARFLAGS) $@ $(OBJS)\n\n";
            *stream << "%.o: %.cpp\n";
            *stream << "\t$(CXX) $(CXXFLAGS) -I. -c $< -o $@\n\n";
            *stream << "clean:\n";
            *stream << "\t$(RM) $(OBJS) $(LIB)\n";
        }

        result.artifacts = {
            headerPath.string(),
            runtimePath.string(),
            statePath.string(),
            evalPath.string(),
            schedPath.string(),
            makefilePath.string(),
        };
        return result;
    }

} // namespace wolvrix::lib::emit
