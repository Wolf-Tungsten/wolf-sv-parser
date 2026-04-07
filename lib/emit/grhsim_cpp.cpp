#include "emit/grhsim_cpp.hpp"

#include "transform/activity_schedule.hpp"

#include "slang/numeric/SVInt.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
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

        std::size_t logicWordCount(int32_t width) noexcept
        {
            if (width <= 0)
            {
                return 0;
            }
            return (static_cast<std::size_t>(width) + 63u) / 64u;
        }

        bool isWideLogicWidth(int32_t width) noexcept
        {
            return width > 64;
        }

        bool isWideLogicValue(const Graph &graph, ValueId value) noexcept
        {
            return graph.valueType(value) == ValueType::Logic &&
                   isWideLogicWidth(graph.valueWidth(value));
        }

        std::string logicCppType(int32_t width)
        {
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
            out << "std::array<std::uint64_t, " << logicWordCount(width) << ">";
            return out.str();
        }

        std::string defaultInitExprForLogicWidth(int32_t width)
        {
            if (width <= 1)
            {
                return "false";
            }
            if (width <= 64)
            {
                return "0";
            }
            return "{}";
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
            return logicCppType(graph.valueWidth(value));
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
            return defaultInitExprForLogicWidth(graph.valueWidth(value));
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
            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(*literal);
                parsed = parsed.resize(static_cast<slang::bitwidth_t>(graph.valueWidth(resultValue)));
                if (parsed.hasUnknown())
                {
                    return std::nullopt;
                }
                if (!isWideLogicValue(graph, resultValue))
                {
                    const auto raw = parsed.as<uint64_t>();
                    if (!raw)
                    {
                        return std::nullopt;
                    }
                    std::ostringstream out;
                    out << "UINT64_C(" << *raw << ")";
                    return out.str();
                }

                std::ostringstream out;
                const std::size_t words = logicWordCount(graph.valueWidth(resultValue));
                out << "std::array<std::uint64_t, " << words << ">{";
                const std::uint64_t *rawWords = parsed.getRawPtr();
                for (std::size_t i = 0; i < words; ++i)
                {
                    if (i != 0)
                    {
                        out << ", ";
                    }
                    out << "UINT64_C(" << rawWords[i] << ")";
                }
                out << "}";
                return out.str();
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        std::optional<slang::SVInt> parseConstLiteral(std::string_view literal)
        {
            std::string compact;
            compact.reserve(literal.size());
            for (char ch : literal)
            {
                if (ch == '_' || std::isspace(static_cast<unsigned char>(ch)))
                {
                    continue;
                }
                compact.push_back(ch);
            }
            if (compact.empty() || compact.front() == '"' || compact.front() == '$')
            {
                return std::nullopt;
            }

            bool negative = false;
            if (compact.front() == '-' || compact.front() == '+')
            {
                negative = compact.front() == '-';
                compact.erase(compact.begin());
            }
            if (compact.empty())
            {
                return std::nullopt;
            }

            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(compact);
                if (negative)
                {
                    parsed = -parsed;
                }
                return parsed;
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        std::optional<std::string> logicValueToCppExpr(const slang::SVInt &value, int32_t width)
        {
            if (width <= 0)
            {
                return std::nullopt;
            }
            slang::SVInt sized = value.resize(static_cast<slang::bitwidth_t>(width));
            if (sized.hasUnknown())
            {
                return std::nullopt;
            }
            if (!isWideLogicWidth(width))
            {
                const auto raw = sized.as<std::uint64_t>();
                if (!raw)
                {
                    return std::nullopt;
                }
                return "static_cast<" + logicCppType(width) + ">(UINT64_C(" + std::to_string(*raw) + "))";
            }

            std::ostringstream out;
            const std::size_t words = logicWordCount(width);
            out << "std::array<std::uint64_t, " << words << ">{";
            const std::uint64_t *rawWords = sized.getRawPtr();
            for (std::size_t i = 0; i < words; ++i)
            {
                if (i != 0)
                {
                    out << ", ";
                }
                out << "UINT64_C(" << rawWords[i] << ")";
            }
            out << "}";
            return out.str();
        }

        std::optional<std::string> logicLiteralToCppExpr(std::string_view literal, int32_t width)
        {
            auto parsed = parseConstLiteral(literal);
            if (!parsed)
            {
                return std::nullopt;
            }
            return logicValueToCppExpr(*parsed, width);
        }

        bool hasAnyMemoryInitAttrs(const Operation &op)
        {
            return op.attr("initKind").has_value() ||
                   op.attr("initFile").has_value() ||
                   op.attr("initValue").has_value() ||
                   op.attr("initStart").has_value() ||
                   op.attr("initLen").has_value();
        }

        bool tokenizeReadmemText(std::string_view text,
                                 std::vector<std::string> &tokens,
                                 std::string &error)
        {
            tokens.clear();
            for (std::size_t i = 0; i < text.size();)
            {
                const char ch = text[i];
                if (std::isspace(static_cast<unsigned char>(ch)))
                {
                    ++i;
                    continue;
                }
                if (ch == '/' && i + 1 < text.size())
                {
                    if (text[i + 1] == '/')
                    {
                        i += 2;
                        while (i < text.size() && text[i] != '\n')
                        {
                            ++i;
                        }
                        continue;
                    }
                    if (text[i + 1] == '*')
                    {
                        const std::size_t end = text.find("*/", i + 2);
                        if (end == std::string_view::npos)
                        {
                            error = "unterminated block comment in readmem file";
                            return false;
                        }
                        i = end + 2;
                        continue;
                    }
                }

                const std::size_t begin = i;
                while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
                {
                    if (text[i] == '/' && i + 1 < text.size() &&
                        (text[i + 1] == '/' || text[i + 1] == '*'))
                    {
                        break;
                    }
                    ++i;
                }
                if (i > begin)
                {
                    tokens.emplace_back(text.substr(begin, i - begin));
                }
            }
            return true;
        }

        std::optional<std::size_t> parseReadmemAddress(std::string_view token)
        {
            if (token.empty())
            {
                return std::nullopt;
            }
            std::size_t value = 0;
            for (unsigned char ch : token)
            {
                if (ch == '_')
                {
                    continue;
                }
                value <<= 4;
                if (ch >= '0' && ch <= '9')
                {
                    value |= static_cast<std::size_t>(ch - '0');
                }
                else if (ch >= 'a' && ch <= 'f')
                {
                    value |= static_cast<std::size_t>(ch - 'a' + 10);
                }
                else if (ch >= 'A' && ch <= 'F')
                {
                    value |= static_cast<std::size_t>(ch - 'A' + 10);
                }
                else
                {
                    return std::nullopt;
                }
            }
            return value;
        }

        struct InitExprCode
        {
            std::string expr;
            bool requiresRuntime = false;
        };

        InitExprCode randomInitExprForWidth(int32_t width);

        bool buildMemoryInitRowExprs(const Operation &memoryOp,
                                     int32_t width,
                                     int64_t rowCount,
                                     std::vector<std::optional<InitExprCode>> &rowExprs,
                                     std::string &error)
        {
            rowExprs.assign(static_cast<std::size_t>(rowCount), std::nullopt);
            if (!hasAnyMemoryInitAttrs(memoryOp))
            {
                return true;
            }

            auto kindsOpt = getAttribute<std::vector<std::string>>(memoryOp, "initKind");
            auto filesOpt = getAttribute<std::vector<std::string>>(memoryOp, "initFile");
            auto startsOpt = getAttribute<std::vector<int64_t>>(memoryOp, "initStart");
            auto lensOpt = getAttribute<std::vector<int64_t>>(memoryOp, "initLen");
            auto values = getAttribute<std::vector<std::string>>(memoryOp, "initValue").value_or(std::vector<std::string>{});

            if (!kindsOpt || !filesOpt || !startsOpt || !lensOpt)
            {
                error = "memory init attrs incomplete: " + std::string(memoryOp.symbolText());
                return false;
            }

            const auto &kinds = *kindsOpt;
            const auto &files = *filesOpt;
            const auto &starts = *startsOpt;
            const auto &lens = *lensOpt;
            if (kinds.size() != files.size() ||
                kinds.size() != starts.size() ||
                kinds.size() != lens.size())
            {
                error = "memory init attr size mismatch: " + std::string(memoryOp.symbolText());
                return false;
            }

            for (std::size_t i = 0; i < kinds.size(); ++i)
            {
                const int64_t start = starts[i];
                const int64_t len = lens[i];
                const std::size_t lower = start < 0 ? 0u : static_cast<std::size_t>(std::max<int64_t>(0, start));
                const std::size_t upper = start < 0
                                              ? static_cast<std::size_t>(rowCount)
                                              : static_cast<std::size_t>(std::min<int64_t>(rowCount,
                                                                                            len <= 0 ? rowCount : start + len));

                if (kinds[i] == "literal")
                {
                    const std::string literal = (i < values.size() && !values[i].empty()) ? values[i] : "0";
                    std::optional<InitExprCode> initExpr;
                    if (literal == "$random")
                    {
                        initExpr = randomInitExprForWidth(width);
                    }
                    else if (auto expr = logicLiteralToCppExpr(literal, width))
                    {
                        initExpr = InitExprCode{.expr = *expr, .requiresRuntime = false};
                    }
                    if (!initExpr)
                    {
                        error = "memory literal init is not statically evaluable: " + std::string(memoryOp.symbolText());
                        return false;
                    }
                    for (std::size_t row = lower; row < upper; ++row)
                    {
                        rowExprs[row] = *initExpr;
                    }
                    continue;
                }

                if (kinds[i] != "readmemh" && kinds[i] != "readmemb")
                {
                    error = "unsupported memory initKind on " + std::string(memoryOp.symbolText()) + ": " + kinds[i];
                    return false;
                }
                if (files[i].empty())
                {
                    error = "memory initFile missing on " + std::string(memoryOp.symbolText());
                    return false;
                }

                std::filesystem::path initPath(files[i]);
                if (initPath.is_relative())
                {
                    initPath = std::filesystem::current_path() / initPath;
                }
                std::ifstream stream(initPath, std::ios::in | std::ios::binary);
                if (!stream.is_open())
                {
                    error = "failed to open memory initFile: " + initPath.string();
                    return false;
                }
                const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
                std::vector<std::string> tokens;
                if (!tokenizeReadmemText(text, tokens, error))
                {
                    error = initPath.string() + ": " + error;
                    return false;
                }

                const bool isHex = kinds[i] == "readmemh";
                std::size_t row = lower;
                for (const std::string &token : tokens)
                {
                    if (!token.empty() && token.front() == '@')
                    {
                        auto addr = parseReadmemAddress(std::string_view(token).substr(1));
                        if (!addr)
                        {
                            error = "invalid readmem address token in " + initPath.string() + ": " + token;
                            return false;
                        }
                        row = *addr;
                        continue;
                    }

                    if (row >= static_cast<std::size_t>(rowCount))
                    {
                        ++row;
                        continue;
                    }
                    if (row >= lower && row < upper)
                    {
                        std::string literal = std::to_string(width) + (isHex ? "'h" : "'b") + token;
                        auto expr = logicLiteralToCppExpr(literal, width);
                        if (!expr)
                        {
                            error = "invalid readmem data token in " + initPath.string() + ": " + token;
                            return false;
                        }
                        rowExprs[row] = InitExprCode{.expr = *expr, .requiresRuntime = false};
                    }
                    ++row;
                }
            }

            return true;
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
            std::optional<InitExprCode> initExpr;
            std::vector<std::optional<InitExprCode>> memoryInitRowExprs;
        };

        InitExprCode randomInitExprForWidth(int32_t width)
        {
            InitExprCode init;
            init.requiresRuntime = true;
            if (isWideLogicWidth(width))
            {
                std::ostringstream out;
                out << "grhsim_random_words<" << logicWordCount(width) << ">(random_state_, " << width << ")";
                init.expr = out.str();
            }
            else
            {
                init.expr = "static_cast<" + logicCppType(width) + ">(grhsim_random_u64(random_state_, " +
                            std::to_string(width) + "))";
            }
            return init;
        }

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
            std::vector<std::string> stateOrder;
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
            return logicCppType(static_cast<int32_t>(width));
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
                        const std::string elemType = logicCppType(state.width);
                        state.cppType = "std::vector<" + elemType + ">";
                        state.fieldName = "state_mem_" + sanitizeIdentifier(state.symbol);
                        if (!buildMemoryInitRowExprs(op, state.width, state.rowCount, state.memoryInitRowExprs, error))
                        {
                            return false;
                        }
                        std::ostringstream decl;
                        decl << "    " << state.cppType << " " << state.fieldName << ";";
                        model.stateFieldDecls.push_back(decl.str());
                    }
                    else
                    {
                        if (state.width <= 0)
                        {
                            error = "storage width must be positive: " + state.symbol;
                            return false;
                        }
                        const std::string cppType = logicCppType(state.width);
                        state.cppType = cppType;
                        state.fieldName = (state.kind == StateDecl::Kind::Register ? "state_reg_" : "state_latch_") +
                                          sanitizeIdentifier(state.symbol);
                        if (auto initValue = getAttribute<std::string>(op, "initValue"))
                        {
                            if (*initValue == "$random")
                            {
                                state.initExpr = randomInitExprForWidth(state.width);
                            }
                            else if (auto expr = logicLiteralToCppExpr(*initValue, state.width))
                            {
                                state.initExpr = InitExprCode{.expr = *expr, .requiresRuntime = false};
                            }
                            else
                            {
                                error = "storage initValue is not statically evaluable: " + state.symbol;
                                return false;
                            }
                        }
                        model.stateFieldDecls.push_back("    " + cppType + " " + state.fieldName + " = " +
                                                        defaultInitExprForLogicWidth(state.width) + ";");
                    }
                    model.stateOrder.push_back(state.symbol);
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
                    const std::string fieldType = write.kind == StateDecl::Kind::Memory ? logicCppType(state.width) : state.cppType;
                    const std::string initExpr = defaultInitExprForLogicWidth(state.width);
                    model.writeFieldDecls.push_back("    " + fieldType + " " + write.pendingDataField + " = " + initExpr + ";");
                    model.writeFieldDecls.push_back("    " + fieldType + " " + write.pendingMaskField + " = " + initExpr + ";");
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

        std::string wordsArrayTypeForWidth(int32_t width)
        {
            std::ostringstream out;
            out << "std::array<std::uint64_t, " << logicWordCount(width) << ">";
            return out.str();
        }

        bool opNeedsWordLogicEmit(const Graph &graph, const Operation &op) noexcept
        {
            for (ValueId value : op.operands())
            {
                if (isWideLogicValue(graph, value))
                {
                    return true;
                }
            }
            for (ValueId value : op.results())
            {
                if (isWideLogicValue(graph, value))
                {
                    return true;
                }
            }
            return false;
        }

        std::string wordsExprForValue(const Graph &graph,
                                      const EmitModel &model,
                                      ValueId value,
                                      int32_t destWidth)
        {
            std::ostringstream out;
            out << "grhsim_cast_words<" << logicWordCount(destWidth) << ">("
                << valueRef(model, value) << ", "
                << graph.valueWidth(value) << ", "
                << destWidth << ")";
            return out.str();
        }

        void emitLogicAssignFromWordsExpr(std::ostream &stream,
                                          const Graph &graph,
                                          const EmitModel &model,
                                          ValueId resultValue,
                                          const std::string &wordsExpr)
        {
            const std::string lhs = valueRef(model, resultValue);
            const int32_t resultWidth = graph.valueWidth(resultValue);
            stream << "        {\n";
            stream << "            const auto next_words = " << wordsExpr << ";\n";
            if (isWideLogicValue(graph, resultValue))
            {
                stream << "            if (grhsim_assign_words(" << lhs << ", next_words, " << resultWidth
                       << ")) { supernode_changed = true; }\n";
            }
            else
            {
                stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                       << ">(grhsim_trunc_u64(next_words[0], " << resultWidth << "));\n";
                stream << "            if (" << lhs << " != next_value) { " << lhs
                       << " = next_value; supernode_changed = true; }\n";
            }
            stream << "        }\n";
        }

        void emitLogicAssignFromBoolExpr(std::ostream &stream,
                                         const Graph &graph,
                                         const EmitModel &model,
                                         ValueId resultValue,
                                         const std::string &boolExpr)
        {
            const std::string lhs = valueRef(model, resultValue);
            stream << "        {\n";
            stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                   << ">(" << boolExpr << ");\n";
            stream << "            if (" << lhs << " != next_value) { " << lhs
                   << " = next_value; supernode_changed = true; }\n";
            stream << "        }\n";
        }

        std::string scalarAssignmentExpr(OperationKind kind,
                                         const std::vector<std::string> &operands,
                                         const Operation &op,
                                         const Graph &graph);

        bool emitWordLogicOperation(std::ostream &stream,
                                    const Graph &graph,
                                    const EmitModel &model,
                                    const Operation &op,
                                    std::string &error)
        {
            if (op.results().empty())
            {
                return true;
            }
            const auto operands = op.operands();
            const ValueId resultValue = op.results().front();
            const int32_t resultWidth = graph.valueWidth(resultValue);
            const std::size_t resultWords = logicWordCount(resultWidth);

            auto unaryWords = [&](ValueId operand, int32_t width) -> std::string
            {
                return wordsExprForValue(graph, model, operand, width);
            };
            auto binaryWords = [&](ValueId lhs, ValueId rhs, int32_t width, std::string_view helper) -> std::string
            {
                std::ostringstream out;
                out << helper << "("
                    << wordsExprForValue(graph, model, lhs, width) << ", "
                    << wordsExprForValue(graph, model, rhs, width) << ", "
                    << width << ")";
                return out.str();
            };
            auto compareWidth = [&]() -> int32_t
            {
                int32_t width = 1;
                for (ValueId value : operands)
                {
                    width = std::max(width, graph.valueWidth(value));
                }
                return width;
            };

            switch (op.kind())
            {
            case OperationKind::kAssign:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, unaryWords(operands[0], resultWidth));
                return true;
            case OperationKind::kAdd:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_add_words"));
                return true;
            case OperationKind::kSub:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_sub_words"));
                return true;
            case OperationKind::kMul:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_mul_words"));
                return true;
            case OperationKind::kDiv:
            {
                const bool signedMode =
                    graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth,
                                                         signedMode ? "grhsim_sdiv_words" : "grhsim_udiv_words"));
                return true;
            }
            case OperationKind::kMod:
            {
                const bool signedMode =
                    graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth,
                                                         signedMode ? "grhsim_smod_words" : "grhsim_umod_words"));
                return true;
            }
            case OperationKind::kAnd:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_and_words"));
                return true;
            case OperationKind::kOr:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_or_words"));
                return true;
            case OperationKind::kXor:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_xor_words"));
                return true;
            case OperationKind::kXnor:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_xnor_words"));
                return true;
            case OperationKind::kNot:
            {
                std::ostringstream out;
                out << "grhsim_not_words(" << unaryWords(operands[0], resultWidth) << ", " << resultWidth << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kEq:
            {
                const int32_t width = compareWidth();
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_compare_unsigned_words(" +
                                                wordsExprForValue(graph, model, operands[0], width) + ", " +
                                                wordsExprForValue(graph, model, operands[1], width) + ") == 0");
                return true;
            }
            case OperationKind::kNe:
            {
                const int32_t width = compareWidth();
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_compare_unsigned_words(" +
                                                wordsExprForValue(graph, model, operands[0], width) + ", " +
                                                wordsExprForValue(graph, model, operands[1], width) + ") != 0");
                return true;
            }
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            {
                const int32_t width = compareWidth();
                const bool signedMode =
                    graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string cmpExpr = signedMode
                                                ? ("grhsim_compare_signed_words(" +
                                                   wordsExprForValue(graph, model, operands[0], width) + ", " +
                                                   wordsExprForValue(graph, model, operands[1], width) + ", " +
                                                   std::to_string(width) + ")")
                                                : ("grhsim_compare_unsigned_words(" +
                                                   wordsExprForValue(graph, model, operands[0], width) + ", " +
                                                   wordsExprForValue(graph, model, operands[1], width) + ")");
                std::string predicate;
                switch (op.kind())
                {
                case OperationKind::kLt:
                    predicate = cmpExpr + " < 0";
                    break;
                case OperationKind::kLe:
                    predicate = cmpExpr + " <= 0";
                    break;
                case OperationKind::kGt:
                    predicate = cmpExpr + " > 0";
                    break;
                case OperationKind::kGe:
                    predicate = cmpExpr + " >= 0";
                    break;
                default:
                    break;
                }
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue, predicate);
                return true;
            }
            case OperationKind::kLogicAnd:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_any_bits_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ") && grhsim_any_bits_words(" +
                                                unaryWords(operands[1], graph.valueWidth(operands[1])) + ", " +
                                                std::to_string(graph.valueWidth(operands[1])) + ")");
                return true;
            case OperationKind::kLogicOr:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_any_bits_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ") || grhsim_any_bits_words(" +
                                                unaryWords(operands[1], graph.valueWidth(operands[1])) + ", " +
                                                std::to_string(graph.valueWidth(operands[1])) + ")");
                return true;
            case OperationKind::kLogicNot:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "!grhsim_any_bits_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kReduceAnd:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_reduce_and_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kReduceNand:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_reduce_nand_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kReduceOr:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_reduce_or_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kReduceNor:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_reduce_nor_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kReduceXor:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_reduce_xor_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kReduceXnor:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_reduce_xnor_words(" +
                                                unaryWords(operands[0], graph.valueWidth(operands[0])) + ", " +
                                                std::to_string(graph.valueWidth(operands[0])) + ")");
                return true;
            case OperationKind::kShl:
            {
                std::ostringstream out;
                out << "grhsim_shl_words("
                    << unaryWords(operands[0], resultWidth) << ", "
                    << valueRef(model, operands[1]) << ", "
                    << resultWidth << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kLShr:
            {
                std::ostringstream out;
                out << "grhsim_lshr_words("
                    << unaryWords(operands[0], resultWidth) << ", "
                    << valueRef(model, operands[1]) << ", "
                    << resultWidth << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kAShr:
            {
                std::ostringstream out;
                out << "grhsim_ashr_words("
                    << unaryWords(operands[0], resultWidth) << ", "
                    << valueRef(model, operands[1]) << ", "
                    << resultWidth << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kMux:
            {
                std::ostringstream out;
                out << "((" << valueRef(model, operands[0]) << ") ? "
                    << wordsExprForValue(graph, model, operands[1], resultWidth) << " : "
                    << wordsExprForValue(graph, model, operands[2], resultWidth) << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kConcat:
            {
                stream << "        {\n";
                stream << "            " << wordsArrayTypeForWidth(resultWidth) << " next_words{};\n";
                stream << "            std::size_t concat_cursor = " << resultWidth << ";\n";
                for (ValueId operand : operands)
                {
                    const int32_t operandWidth = graph.valueWidth(operand);
                    stream << "            concat_cursor -= " << operandWidth << ";\n";
                    stream << "            grhsim_insert_words(next_words, concat_cursor, "
                           << wordsExprForValue(graph, model, operand, operandWidth) << ", "
                           << operandWidth << ");\n";
                }
                if (isWideLogicValue(graph, resultValue))
                {
                    stream << "            if (grhsim_assign_words(" << valueRef(model, resultValue)
                           << ", next_words, " << resultWidth << ")) { supernode_changed = true; }\n";
                }
                else
                {
                    stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                           << ">(grhsim_trunc_u64(next_words[0], " << resultWidth << "));\n";
                    stream << "            if (" << valueRef(model, resultValue) << " != next_value) { "
                           << valueRef(model, resultValue) << " = next_value; supernode_changed = true; }\n";
                }
                stream << "        }\n";
                return true;
            }
            case OperationKind::kReplicate:
            {
                const auto rep = getAttribute<int64_t>(op, "rep").value_or(1);
                const int32_t operandWidth = graph.valueWidth(operands[0]);
                stream << "        {\n";
                stream << "            " << wordsArrayTypeForWidth(resultWidth) << " next_words{};\n";
                stream << "            for (std::size_t rep_index = 0; rep_index < " << rep << "; ++rep_index) {\n";
                stream << "                grhsim_insert_words(next_words, rep_index * " << operandWidth << ", "
                       << wordsExprForValue(graph, model, operands[0], operandWidth) << ", "
                       << operandWidth << ");\n";
                stream << "            }\n";
                stream << "            grhsim_trunc_words(next_words, " << resultWidth << ");\n";
                if (isWideLogicValue(graph, resultValue))
                {
                    stream << "            if (grhsim_assign_words(" << valueRef(model, resultValue)
                           << ", next_words, " << resultWidth << ")) { supernode_changed = true; }\n";
                }
                else
                {
                    stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                           << ">(grhsim_trunc_u64(next_words[0], " << resultWidth << "));\n";
                    stream << "            if (" << valueRef(model, resultValue) << " != next_value) { "
                           << valueRef(model, resultValue) << " = next_value; supernode_changed = true; }\n";
                }
                stream << "        }\n";
                return true;
            }
            case OperationKind::kSliceStatic:
            {
                const auto sliceStart = getAttribute<int64_t>(op, "sliceStart").value_or(0);
                std::ostringstream out;
                out << "grhsim_slice_words<" << resultWords << ">("
                    << wordsExprForValue(graph, model, operands[0], graph.valueWidth(operands[0])) << ", "
                    << sliceStart << ", "
                    << resultWidth << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kSliceDynamic:
            {
                std::ostringstream out;
                out << "grhsim_slice_words<" << resultWords << ">("
                    << wordsExprForValue(graph, model, operands[0], graph.valueWidth(operands[0])) << ", "
                    << valueRef(model, operands[1]) << ", "
                    << graph.valueWidth(operands[0]) << ", "
                    << resultWidth << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            default:
                error = "unsupported wide logic emit op: " + std::string(op.symbolText());
                return false;
            }
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
                    case OperationKind::kMul:
                    case OperationKind::kDiv:
                    case OperationKind::kMod:
                    case OperationKind::kAnd:
                    case OperationKind::kOr:
                    case OperationKind::kXor:
                    case OperationKind::kXnor:
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
                    case OperationKind::kReduceAnd:
                    case OperationKind::kReduceNand:
                    case OperationKind::kReduceOr:
                    case OperationKind::kReduceNor:
                    case OperationKind::kReduceXor:
                    case OperationKind::kReduceXnor:
                    case OperationKind::kShl:
                    case OperationKind::kLShr:
                    case OperationKind::kAShr:
                    case OperationKind::kMux:
                    case OperationKind::kConcat:
                    case OperationKind::kReplicate:
                    case OperationKind::kSliceStatic:
                    case OperationKind::kSliceDynamic:
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
                        expr = scalarAssignmentExpr(op.kind(), operandExprs, op, graph);
                        if (expr && expr->empty())
                        {
                            expr.reset();
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
            const auto resultWidth =
                op.results().empty() ? 64 : static_cast<std::size_t>(graph.valueWidth(op.results().front()));
            switch (kind)
            {
            case OperationKind::kAssign:
                return operands[0];
            case OperationKind::kAdd:
                return "(" + operands[0] + " + " + operands[1] + ")";
            case OperationKind::kSub:
                return "(" + operands[0] + " - " + operands[1] + ")";
            case OperationKind::kMul:
                return "(" + operands[0] + " * " + operands[1] + ")";
            case OperationKind::kDiv:
            {
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                return std::string(signedMode ? "grhsim_sdiv_u64(" : "grhsim_udiv_u64(") +
                       operands[0] + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            }
            case OperationKind::kMod:
            {
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                return std::string(signedMode ? "grhsim_smod_u64(" : "grhsim_umod_u64(") +
                       operands[0] + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            }
            case OperationKind::kAnd:
                return "(" + operands[0] + " & " + operands[1] + ")";
            case OperationKind::kOr:
                return "(" + operands[0] + " | " + operands[1] + ")";
            case OperationKind::kXor:
                return "(" + operands[0] + " ^ " + operands[1] + ")";
            case OperationKind::kXnor:
                return "(~(" + operands[0] + " ^ " + operands[1] + "))";
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
            case OperationKind::kReduceAnd:
                return "grhsim_reduce_and_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            case OperationKind::kReduceNand:
                return "grhsim_reduce_nand_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            case OperationKind::kReduceOr:
                return "grhsim_reduce_or_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            case OperationKind::kReduceNor:
                return "grhsim_reduce_nor_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            case OperationKind::kReduceXor:
                return "grhsim_reduce_xor_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            case OperationKind::kReduceXnor:
                return "grhsim_reduce_xnor_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            case OperationKind::kShl:
                return "grhsim_shl_u64(" + operands[0] + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            case OperationKind::kLShr:
                return "grhsim_lshr_u64(" + operands[0] + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            case OperationKind::kAShr:
                return "grhsim_ashr_u64(" + operands[0] + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            case OperationKind::kMux:
                return "((" + operands[0] + ") ? (" + operands[1] + ") : (" + operands[2] + "))";
            case OperationKind::kConcat:
                return "grhsim_concat_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) +
                       ", " + operands[1] + ", " + std::to_string(graph.valueWidth(op.operands()[1])) + ")";
            case OperationKind::kReplicate:
            {
                const auto rep = getAttribute<int64_t>(op, "rep").value_or(1);
                return "grhsim_replicate_u64(" + operands[0] + ", " +
                       std::to_string(graph.valueWidth(op.operands()[0])) + ", " + std::to_string(rep) + ")";
            }
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
            case OperationKind::kSliceDynamic:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth").value_or(1);
                return "grhsim_slice_dynamic_u64(" + operands[0] + ", " + operands[1] + ", " +
                       std::to_string(sliceWidth) + ")";
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
            *stream << "#include <array>\n";
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
            *stream << "inline std::int64_t grhsim_sign_extend_i64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    if (width == 0) return 0;\n";
            *stream << "    value = grhsim_trunc_u64(value, width);\n";
            *stream << "    if (width >= 64) return static_cast<std::int64_t>(value);\n";
            *stream << "    const std::uint64_t sign = UINT64_C(1) << (width - 1u);\n";
            *stream << "    if ((value & sign) == 0) return static_cast<std::int64_t>(value);\n";
            *stream << "    return static_cast<std::int64_t>(value | ~grhsim_mask(width));\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_concat_u64(std::uint64_t lhs, std::size_t lhsWidth, std::uint64_t rhs, std::size_t rhsWidth)\n{\n";
            *stream << "    const std::uint64_t lhsBits = grhsim_trunc_u64(lhs, lhsWidth);\n";
            *stream << "    const std::uint64_t rhsBits = grhsim_trunc_u64(rhs, rhsWidth);\n";
            *stream << "    if (rhsWidth >= 64) return rhsBits;\n";
            *stream << "    return grhsim_trunc_u64((lhsBits << rhsWidth) | rhsBits, lhsWidth + rhsWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_replicate_u64(std::uint64_t value, std::size_t elemWidth, std::size_t rep)\n{\n";
            *stream << "    if (elemWidth == 0 || rep == 0) return 0;\n";
            *stream << "    const std::uint64_t elem = grhsim_trunc_u64(value, elemWidth);\n";
            *stream << "    std::uint64_t out = 0;\n";
            *stream << "    for (std::size_t i = 0; i < rep; ++i) {\n";
            *stream << "        const std::size_t shift = i * elemWidth;\n";
            *stream << "        if (shift >= 64) break;\n";
            *stream << "        out |= (elem << shift);\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(out, elemWidth * rep);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_slice_dynamic_u64(std::uint64_t value, std::uint64_t start, std::size_t width)\n{\n";
            *stream << "    if (start >= 64) return 0;\n";
            *stream << "    return grhsim_trunc_u64(value >> start, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_shl_u64(std::uint64_t value, std::uint64_t shift, std::size_t width)\n{\n";
            *stream << "    if (shift >= 64) return 0;\n";
            *stream << "    return grhsim_trunc_u64(value << shift, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_lshr_u64(std::uint64_t value, std::uint64_t shift, std::size_t width)\n{\n";
            *stream << "    if (shift >= 64) return 0;\n";
            *stream << "    return grhsim_trunc_u64(grhsim_trunc_u64(value, width) >> shift, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_ashr_u64(std::uint64_t value, std::uint64_t shift, std::size_t width)\n{\n";
            *stream << "    if (width == 0) return 0;\n";
            *stream << "    const std::uint64_t bounded = shift >= 64 ? 63 : shift;\n";
            *stream << "    const std::int64_t signedValue = grhsim_sign_extend_i64(value, width);\n";
            *stream << "    return grhsim_trunc_u64(static_cast<std::uint64_t>(signedValue >> bounded), width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_udiv_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::uint64_t divisor = grhsim_trunc_u64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    return grhsim_trunc_u64(grhsim_trunc_u64(lhs, width) / divisor, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_umod_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::uint64_t divisor = grhsim_trunc_u64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    return grhsim_trunc_u64(grhsim_trunc_u64(lhs, width) % divisor, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_sdiv_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::int64_t divisor = grhsim_sign_extend_i64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    const std::int64_t dividend = grhsim_sign_extend_i64(lhs, width);\n";
            *stream << "    return grhsim_trunc_u64(static_cast<std::uint64_t>(dividend / divisor), width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_smod_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::int64_t divisor = grhsim_sign_extend_i64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    const std::int64_t dividend = grhsim_sign_extend_i64(lhs, width);\n";
            *stream << "    return grhsim_trunc_u64(static_cast<std::uint64_t>(dividend % divisor), width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_and_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    if (width == 0) return false;\n";
            *stream << "    return grhsim_trunc_u64(value, width) == grhsim_mask(width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_nand_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return !grhsim_reduce_and_u64(value, width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_or_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return grhsim_trunc_u64(value, width) != 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_nor_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return !grhsim_reduce_or_u64(value, width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_xor_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return (__builtin_popcountll(grhsim_trunc_u64(value, width)) & 1) != 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_xnor_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return !grhsim_reduce_xor_u64(value, width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_event_posedge(std::uint64_t curr, std::uint64_t prev)\n{\n";
            *stream << "    return curr != 0 && prev == 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_event_negedge(std::uint64_t curr, std::uint64_t prev)\n{\n";
            *stream << "    return curr == 0 && prev != 0;\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_splitmix64_next(std::uint64_t &state)\n{\n";
            *stream << "    std::uint64_t z = (state += UINT64_C(0x9E3779B97F4A7C15));\n";
            *stream << "    z = (z ^ (z >> 30u)) * UINT64_C(0xBF58476D1CE4E5B9);\n";
            *stream << "    z = (z ^ (z >> 27u)) * UINT64_C(0x94D049BB133111EB);\n";
            *stream << "    return z ^ (z >> 31u);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_random_u64(std::uint64_t &state, std::size_t width)\n{\n";
            *stream << "    return grhsim_trunc_u64(grhsim_splitmix64_next(state), width);\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline void grhsim_trunc_words(std::array<std::uint64_t, N> &value, std::size_t width)\n{\n";
            *stream << "    const std::size_t liveWords = (width + 63u) / 64u;\n";
            *stream << "    for (std::size_t i = liveWords; i < N; ++i) {\n";
            *stream << "        value[i] = 0;\n";
            *stream << "    }\n";
            *stream << "    if constexpr (N > 0) {\n";
            *stream << "        if (liveWords != 0) {\n";
            *stream << "            const std::size_t tailWidth = width - ((liveWords - 1u) * 64u);\n";
            *stream << "            value[liveWords - 1u] = grhsim_trunc_u64(value[liveWords - 1u], tailWidth);\n";
            *stream << "        }\n";
            *stream << "    }\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_equal_words(const std::array<std::uint64_t, N> &lhs, const std::array<std::uint64_t, N> &rhs)\n{\n";
            *stream << "    return lhs == rhs;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_assign_words(std::array<std::uint64_t, N> &dst, std::array<std::uint64_t, N> src, std::size_t width)\n{\n";
            *stream << "    grhsim_trunc_words(src, width);\n";
            *stream << "    if (dst == src) {\n";
            *stream << "        return false;\n";
            *stream << "    }\n";
            *stream << "    dst = src;\n";
            *stream << "    return true;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline std::array<std::uint64_t, N> grhsim_merge_words_masked(const std::array<std::uint64_t, N> &base,\n";
            *stream << "                                                           const std::array<std::uint64_t, N> &data,\n";
            *stream << "                                                           const std::array<std::uint64_t, N> &mask,\n";
            *stream << "                                                           std::size_t width)\n{\n";
            *stream << "    std::array<std::uint64_t, N> out{};\n";
            *stream << "    for (std::size_t i = 0; i < N; ++i) {\n";
            *stream << "        out[i] = (base[i] & ~mask[i]) | (data[i] & mask[i]);\n";
            *stream << "    }\n";
            *stream << "    grhsim_trunc_words(out, width);\n";
            *stream << "    return out;\n";
            *stream << "}\n\n";
            *stream << R"CPP(
template <std::size_t DestN, typename T>
inline std::array<std::uint64_t, DestN> grhsim_cast_words(T value, std::size_t srcWidth, std::size_t destWidth)
{
    std::array<std::uint64_t, DestN> out{};
    if constexpr (DestN > 0) {
        out[0] = grhsim_trunc_u64(static_cast<std::uint64_t>(value), srcWidth);
    }
    grhsim_trunc_words(out, destWidth);
    return out;
}

template <std::size_t DestN, std::size_t SrcN>
inline std::array<std::uint64_t, DestN> grhsim_cast_words(const std::array<std::uint64_t, SrcN> &value,
                                                          std::size_t srcWidth,
                                                          std::size_t destWidth)
{
    std::array<std::uint64_t, DestN> out{};
    const std::size_t srcWords = (srcWidth + 63u) / 64u;
    const std::size_t limit = srcWords < SrcN ? srcWords : SrcN;
    for (std::size_t i = 0; i < limit && i < DestN; ++i) {
        out[i] = value[i];
    }
    grhsim_trunc_words(out, destWidth);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_random_words(std::uint64_t &state, std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = grhsim_splitmix64_next(state);
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline bool grhsim_any_bits_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::uint64_t word = (i + 1u == liveWords) ? grhsim_trunc_u64(value[i], width - i * 64u) : value[i];
        if (word != 0) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
inline bool grhsim_get_bit_words(const std::array<std::uint64_t, N> &value, std::size_t index)
{
    if (index / 64u >= N) {
        return false;
    }
    return (value[index / 64u] & (UINT64_C(1) << (index & 63u))) != 0;
}

template <std::size_t N>
inline void grhsim_put_bit_words(std::array<std::uint64_t, N> &value, std::size_t index, bool bit)
{
    if (index / 64u >= N) {
        return;
    }
    const std::uint64_t mask = UINT64_C(1) << (index & 63u);
    if (bit) {
        value[index / 64u] |= mask;
    }
    else {
        value[index / 64u] &= ~mask;
    }
}

template <std::size_t DestN, std::size_t SrcN>
inline void grhsim_insert_words(std::array<std::uint64_t, DestN> &dest,
                                std::size_t destLsb,
                                const std::array<std::uint64_t, SrcN> &src,
                                std::size_t srcWidth)
{
    for (std::size_t bit = 0; bit < srcWidth; ++bit) {
        grhsim_put_bit_words(dest, destLsb + bit, grhsim_get_bit_words(src, bit));
    }
}

template <std::size_t DestN, std::size_t SrcN>
inline std::array<std::uint64_t, DestN> grhsim_slice_words(const std::array<std::uint64_t, SrcN> &src,
                                                           std::size_t start,
                                                           std::size_t width)
{
    std::array<std::uint64_t, DestN> out{};
    for (std::size_t bit = 0; bit < width; ++bit) {
        grhsim_put_bit_words(out, bit, grhsim_get_bit_words(src, start + bit));
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <typename T>
inline std::size_t grhsim_index_words(T value, std::size_t cap)
{
    const std::uint64_t raw = static_cast<std::uint64_t>(value);
    if (raw >= cap) {
        return cap;
    }
    return static_cast<std::size_t>(raw);
}

template <std::size_t N>
inline std::size_t grhsim_index_words(const std::array<std::uint64_t, N> &value, std::size_t cap)
{
    for (std::size_t i = 1; i < N; ++i) {
        if (value[i] != 0) {
            return cap;
        }
    }
    if (value[0] >= cap) {
        return cap;
    }
    return static_cast<std::size_t>(value[0]);
}

template <std::size_t DestN, std::size_t SrcN, typename ShiftT>
inline std::array<std::uint64_t, DestN> grhsim_slice_words(const std::array<std::uint64_t, SrcN> &src,
                                                           const ShiftT &start,
                                                           std::size_t srcWidth,
                                                           std::size_t width)
{
    return grhsim_slice_words<DestN>(src, grhsim_index_words(start, srcWidth), width);
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_not_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = ~value[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_and_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lhs[i] & rhs[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_or_words(const std::array<std::uint64_t, N> &lhs,
                                                    const std::array<std::uint64_t, N> &rhs,
                                                    std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lhs[i] | rhs[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_xor_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lhs[i] ^ rhs[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_xnor_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    return grhsim_not_words(grhsim_xor_words(lhs, rhs, width), width);
}

template <std::size_t N>
inline bool grhsim_sign_bit_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    if (width == 0) {
        return false;
    }
    return grhsim_get_bit_words(value, width - 1u);
}

template <std::size_t N>
inline int grhsim_compare_unsigned_words(const std::array<std::uint64_t, N> &lhs,
                                         const std::array<std::uint64_t, N> &rhs)
{
    for (std::size_t i = N; i-- > 0;) {
        if (lhs[i] < rhs[i]) {
            return -1;
        }
        if (lhs[i] > rhs[i]) {
            return 1;
        }
    }
    return 0;
}

template <std::size_t N>
inline int grhsim_compare_signed_words(const std::array<std::uint64_t, N> &lhs,
                                       const std::array<std::uint64_t, N> &rhs,
                                       std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, width);
    if (lhsNeg != rhsNeg) {
        return lhsNeg ? -1 : 1;
    }
    const int cmp = grhsim_compare_unsigned_words(lhs, rhs);
    return lhsNeg ? -cmp : cmp;
}

template <std::size_t N>
inline bool grhsim_reduce_and_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    if (width == 0) {
        return false;
    }
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        if (grhsim_trunc_u64(value[i], wordWidth) != grhsim_mask(wordWidth)) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
inline bool grhsim_reduce_nand_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return !grhsim_reduce_and_words(value, width);
}

template <std::size_t N>
inline bool grhsim_reduce_or_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return grhsim_any_bits_words(value, width);
}

template <std::size_t N>
inline bool grhsim_reduce_nor_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return !grhsim_reduce_or_words(value, width);
}

template <std::size_t N>
inline bool grhsim_reduce_xor_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    unsigned parity = 0;
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        parity ^= static_cast<unsigned>(__builtin_popcountll(grhsim_trunc_u64(value[i], wordWidth)) & 1u);
    }
    return (parity & 1u) != 0;
}

template <std::size_t N>
inline bool grhsim_reduce_xnor_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return !grhsim_reduce_xor_words(value, width);
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_add_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    unsigned __int128 carry = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const unsigned __int128 sum =
            static_cast<unsigned __int128>(lhs[i]) + static_cast<unsigned __int128>(rhs[i]) + carry;
        out[i] = static_cast<std::uint64_t>(sum);
        carry = sum >> 64u;
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_sub_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t rhsWord = rhs[i] + borrow;
        borrow = (rhsWord < rhs[i] || lhs[i] < rhsWord) ? 1 : 0;
        out[i] = lhs[i] - rhsWord;
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_mul_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        unsigned __int128 carry = 0;
        for (std::size_t j = 0; j + i < N; ++j) {
            const unsigned __int128 accum =
                static_cast<unsigned __int128>(out[i + j]) +
                static_cast<unsigned __int128>(lhs[i]) * static_cast<unsigned __int128>(rhs[j]) +
                carry;
            out[i + j] = static_cast<std::uint64_t>(accum);
            carry = accum >> 64u;
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_negate_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    std::array<std::uint64_t, N> out = grhsim_not_words(value, width);
    std::array<std::uint64_t, N> one{};
    if constexpr (N > 0) {
        one[0] = 1;
    }
    return grhsim_add_words(out, one, width);
}

template <std::size_t N>
inline void grhsim_shl1_words_inplace(std::array<std::uint64_t, N> &value, std::size_t width)
{
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t nextCarry = value[i] >> 63u;
        value[i] = (value[i] << 1u) | carry;
        carry = nextCarry;
    }
    grhsim_trunc_words(value, width);
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_udiv_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    if (!grhsim_any_bits_words(rhs, width)) {
        return {};
    }
    std::array<std::uint64_t, N> quotient{};
    std::array<std::uint64_t, N> remainder{};
    for (std::size_t bit = width; bit-- > 0;) {
        grhsim_shl1_words_inplace(remainder, width);
        if (grhsim_get_bit_words(lhs, bit)) {
            remainder[0] |= 1;
        }
        if (grhsim_compare_unsigned_words(remainder, rhs) >= 0) {
            remainder = grhsim_sub_words(remainder, rhs, width);
            grhsim_put_bit_words(quotient, bit, true);
        }
    }
    grhsim_trunc_words(quotient, width);
    return quotient;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_umod_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    if (!grhsim_any_bits_words(rhs, width)) {
        return {};
    }
    std::array<std::uint64_t, N> remainder{};
    for (std::size_t bit = width; bit-- > 0;) {
        grhsim_shl1_words_inplace(remainder, width);
        if (grhsim_get_bit_words(lhs, bit)) {
            remainder[0] |= 1;
        }
        if (grhsim_compare_unsigned_words(remainder, rhs) >= 0) {
            remainder = grhsim_sub_words(remainder, rhs, width);
        }
    }
    grhsim_trunc_words(remainder, width);
    return remainder;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_sdiv_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, width);
    const auto lhsAbs = lhsNeg ? grhsim_negate_words(lhs, width) : lhs;
    const auto rhsAbs = rhsNeg ? grhsim_negate_words(rhs, width) : rhs;
    auto quotient = grhsim_udiv_words(lhsAbs, rhsAbs, width);
    if (lhsNeg != rhsNeg) {
        quotient = grhsim_negate_words(quotient, width);
    }
    grhsim_trunc_words(quotient, width);
    return quotient;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_smod_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, width);
    const auto lhsAbs = lhsNeg ? grhsim_negate_words(lhs, width) : lhs;
    const auto rhsAbs = rhsNeg ? grhsim_negate_words(rhs, width) : rhs;
    auto remainder = grhsim_umod_words(lhsAbs, rhsAbs, width);
    if (lhsNeg) {
        remainder = grhsim_negate_words(remainder, width);
    }
    grhsim_trunc_words(remainder, width);
    return remainder;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_shl_words(const std::array<std::uint64_t, N> &value,
                                                     const ShiftT &shift,
                                                     std::size_t width)
{
    const std::size_t amount = grhsim_index_words(shift, width);
    if (amount >= width) {
        return {};
    }
    std::array<std::uint64_t, N> out{};
    for (std::size_t bit = 0; bit + amount < width; ++bit) {
        if (grhsim_get_bit_words(value, bit)) {
            grhsim_put_bit_words(out, bit + amount, true);
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_lshr_words(const std::array<std::uint64_t, N> &value,
                                                      const ShiftT &shift,
                                                      std::size_t width)
{
    const std::size_t amount = grhsim_index_words(shift, width);
    if (amount >= width) {
        return {};
    }
    std::array<std::uint64_t, N> out{};
    for (std::size_t bit = amount; bit < width; ++bit) {
        if (grhsim_get_bit_words(value, bit)) {
            grhsim_put_bit_words(out, bit - amount, true);
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_ashr_words(const std::array<std::uint64_t, N> &value,
                                                      const ShiftT &shift,
                                                      std::size_t width)
{
    const std::size_t amount = grhsim_index_words(shift, width);
    const bool sign = grhsim_sign_bit_words(value, width);
    if (amount >= width) {
        std::array<std::uint64_t, N> fill{};
        if (sign) {
            for (std::size_t bit = 0; bit < width; ++bit) {
                grhsim_put_bit_words(fill, bit, true);
            }
        }
        grhsim_trunc_words(fill, width);
        return fill;
    }
    std::array<std::uint64_t, N> out{};
    for (std::size_t bit = amount; bit < width; ++bit) {
        if (grhsim_get_bit_words(value, bit)) {
            grhsim_put_bit_words(out, bit - amount, true);
        }
    }
    if (sign) {
        for (std::size_t bit = width - amount; bit < width; ++bit) {
            grhsim_put_bit_words(out, bit, true);
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

)CPP";
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
            *stream << "    void init();\n";
            *stream << "    void set_random_seed(std::uint64_t seed);\n";
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
            *stream << "    std::uint64_t random_seed_ = UINT64_C(0);\n";
            *stream << "    std::uint64_t random_state_ = UINT64_C(0);\n";
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
            bool emittedInitNamespace = false;
            for (const std::string &stateSymbol : model.stateOrder)
            {
                const StateDecl &state = model.stateBySymbol.at(stateSymbol);
                if (state.kind != StateDecl::Kind::Memory || state.memoryInitRowExprs.empty())
                {
                    continue;
                }
                std::vector<std::size_t> initRows;
                initRows.reserve(state.memoryInitRowExprs.size());
                for (std::size_t row = 0; row < state.memoryInitRowExprs.size(); ++row)
                {
                    if (state.memoryInitRowExprs[row].has_value() && !state.memoryInitRowExprs[row]->requiresRuntime)
                    {
                        initRows.push_back(row);
                    }
                }
                if (initRows.empty())
                {
                    continue;
                }
                if (!emittedInitNamespace)
                {
                    *stream << "namespace {\n";
                    emittedInitNamespace = true;
                }
                const std::string base = "k_mem_init_" + sanitizeIdentifier(stateSymbol);
                *stream << "constexpr std::size_t " << base << "_rows[] = {";
                for (std::size_t i = 0; i < initRows.size(); ++i)
                {
                    if (i != 0)
                    {
                        *stream << ", ";
                    }
                    *stream << initRows[i];
                }
                *stream << "};\n";
                *stream << "constexpr " << logicCppType(state.width) << " " << base << "_data[] = {\n";
                for (std::size_t row : initRows)
                {
                    *stream << "    " << state.memoryInitRowExprs[row]->expr << ",\n";
                }
                *stream << "};\n\n";
            }
            if (emittedInitNamespace)
            {
                *stream << "} // namespace\n\n";
            }
            *stream << className << "::" << className << "()\n";
            *stream << "    : supernode_active_curr_((kSupernodeCount + 63u) / 64u, 0),\n";
            *stream << "      event_term_hit_(kEventTermCount, false),\n";
            *stream << "      event_domain_hit_(kEventDomainCount, true)\n";
            *stream << "{\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::init()\n{\n";
            for (const auto &port : graph.outputPorts())
            {
                *stream << "    out_" << sanitizeIdentifier(port.name) << " = " << defaultInitExpr(graph, port.value) << ";\n";
            }
            if (!graph.outputPorts().empty())
            {
                *stream << '\n';
            }
            for (ValueId valueId : graph.values())
            {
                auto it = model.valueFieldByValue.find(valueId);
                if (it == model.valueFieldByValue.end())
                {
                    continue;
                }
                *stream << "    " << it->second << " = " << defaultInitExpr(graph, valueId) << ";\n";
            }
            if (!graph.values().empty())
            {
                *stream << '\n';
            }
            *stream << "    random_state_ = random_seed_;\n";
            for (const std::string &stateSymbol : model.stateOrder)
            {
                const StateDecl &state = model.stateBySymbol.at(stateSymbol);
                if (state.kind == StateDecl::Kind::Memory)
                {
                    *stream << "    " << state.fieldName << ".assign(" << state.rowCount << ", "
                            << logicCppType(state.width) << "{});\n";
                    std::size_t initCount = 0;
                    for (const auto &rowExpr : state.memoryInitRowExprs)
                    {
                        if (rowExpr.has_value() && !rowExpr->requiresRuntime)
                        {
                            ++initCount;
                        }
                    }
                    if (initCount != 0)
                    {
                        const std::string base = "k_mem_init_" + sanitizeIdentifier(stateSymbol);
                        *stream << "    for (std::size_t i = 0; i < " << initCount << "; ++i) {\n";
                        *stream << "        " << state.fieldName << "[" << base << "_rows[i]] = " << base << "_data[i];\n";
                        *stream << "    }\n";
                    }
                    for (std::size_t row = 0; row < state.memoryInitRowExprs.size(); ++row)
                    {
                        if (!state.memoryInitRowExprs[row].has_value() || !state.memoryInitRowExprs[row]->requiresRuntime)
                        {
                            continue;
                        }
                        *stream << "    " << state.fieldName << "[" << row << "] = "
                                << state.memoryInitRowExprs[row]->expr << ";\n";
                    }
                }
                else if (state.initExpr)
                {
                    *stream << "    " << state.fieldName << " = " << state.initExpr->expr << ";\n";
                }
            }
            if (!model.stateOrder.empty())
            {
                *stream << '\n';
            }
            for (const auto &write : model.writes)
            {
                const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                *stream << "    " << write.pendingValidField << " = false;\n";
                if (state.kind == StateDecl::Kind::Memory)
                {
                    *stream << "    " << write.pendingAddrField << " = 0;\n";
                }
                *stream << "    " << write.pendingDataField << " = " << defaultInitExprForLogicWidth(state.width) << ";\n";
                *stream << "    " << write.pendingMaskField << " = " << defaultInitExprForLogicWidth(state.width) << ";\n";
            }
            if (!model.writes.empty())
            {
                *stream << '\n';
            }
            for (const auto &port : graph.inputPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.value) << " = " << model.inputFieldByValue.at(port.value) << ";\n";
            }
            if (!graph.inputPorts().empty())
            {
                *stream << '\n';
            }
            for (const auto &[value, prevField] : model.prevEventFieldByValue)
            {
                std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> cache;
                std::unordered_map<ValueId, std::size_t, ValueIdHash> costCache;
                std::size_t totalOps = 0;
                auto expr = pureExprForValue(graph, model, value, cache, costCache, totalOps);
                *stream << "    " << prevField << " = " << (expr ? *expr : valueRef(model, value)) << ";\n";
            }
            if (!model.prevEventFieldByValue.empty())
            {
                *stream << '\n';
            }
            *stream << "    std::fill(supernode_active_curr_.begin(), supernode_active_curr_.end(), 0);\n";
            *stream << "    std::fill(event_term_hit_.begin(), event_term_hit_.end(), false);\n";
            *stream << "    std::fill(event_domain_hit_.begin(), event_domain_hit_.end(), true);\n";
            *stream << "    first_eval_ = true;\n";
            *stream << "    state_feedback_pending_ = true;\n";
            *stream << "    side_effect_feedback_ = false;\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::set_random_seed(std::uint64_t seed)\n{\n";
            *stream << "    random_seed_ = seed;\n";
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
                    if (isWideLogicWidth(state.width))
                    {
                        *stream << "        const auto merged = grhsim_merge_words_masked(" << state.fieldName << "[row], "
                                << write.pendingDataField << ", " << write.pendingMaskField << ", " << state.width << ");\n";
                        *stream << "        if (grhsim_assign_words(" << state.fieldName << "[row], merged, " << state.width << ")) {\n";
                        *stream << "            any_state_change = true;\n";
                        *stream << "        }\n";
                    }
                    else
                    {
                        *stream << "        const auto merged = static_cast<" << logicCppType(state.width) << ">((" << state.fieldName
                                << "[row] & ~" << write.pendingMaskField << ") | (" << write.pendingDataField
                                << " & " << write.pendingMaskField << "));\n";
                        *stream << "        if (" << state.fieldName << "[row] != merged) {\n";
                        *stream << "            " << state.fieldName << "[row] = merged;\n";
                        *stream << "            any_state_change = true;\n";
                        *stream << "        }\n";
                    }
                }
                else
                {
                    if (isWideLogicWidth(state.width))
                    {
                        *stream << "        const auto merged = grhsim_merge_words_masked(" << state.fieldName << ", "
                                << write.pendingDataField << ", " << write.pendingMaskField << ", " << state.width << ");\n";
                        *stream << "        if (grhsim_assign_words(" << state.fieldName << ", merged, " << state.width << ")) {\n";
                        *stream << "            any_state_change = true;\n";
                        *stream << "        }\n";
                    }
                    else
                    {
                        *stream << "        const auto merged = static_cast<" << state.cppType << ">((" << state.fieldName
                                << " & ~" << write.pendingMaskField << ") | (" << write.pendingDataField
                                << " & " << write.pendingMaskField << "));\n";
                        *stream << "        if (" << state.fieldName << " != merged) {\n";
                        *stream << "            " << state.fieldName << " = merged;\n";
                        *stream << "            any_state_change = true;\n";
                        *stream << "        }\n";
                    }
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
                            if (isWideLogicValue(graph, resultValue))
                            {
                                *stream << "        {\n";
                                *stream << "            const auto next_value = " << *expr << ";\n";
                                *stream << "            if (grhsim_assign_words(" << lhs << ", next_value, "
                                        << graph.valueWidth(resultValue) << ")) { supernode_changed = true; }\n";
                                *stream << "        }\n";
                            }
                            else
                            {
                                *stream << "        {\n";
                                *stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                                        << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << *expr << "), " << graph.valueWidth(resultValue) << "));\n";
                                *stream << "            if (" << lhs << " != next_value) { " << lhs << " = next_value; supernode_changed = true; }\n";
                                *stream << "        }\n";
                            }
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
                        if (isWideLogicValue(graph, op.results().front()))
                        {
                            *stream << "        if (grhsim_assign_words(" << lhs << ", " << stateIt->second.fieldName << ", "
                                    << graph.valueWidth(op.results().front()) << ")) { supernode_changed = true; }\n";
                        }
                        else
                        {
                            *stream << "        if (" << lhs << " != " << stateIt->second.fieldName << ") { "
                                    << lhs << " = " << stateIt->second.fieldName << "; supernode_changed = true; }\n";
                        }
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
                        if (isWideLogicValue(graph, op.results().front()))
                        {
                            *stream << "            if (grhsim_assign_words(" << lhs << ", next_value, "
                                    << graph.valueWidth(op.results().front()) << ")) { supernode_changed = true; }\n";
                        }
                        else
                        {
                            *stream << "            if (" << lhs << " != next_value) { " << lhs
                                    << " = next_value; supernode_changed = true; }\n";
                        }
                        *stream << "        }\n";
                        break;
                    }
                    case OperationKind::kAssign:
                    case OperationKind::kAdd:
                    case OperationKind::kSub:
                    case OperationKind::kMul:
                    case OperationKind::kDiv:
                    case OperationKind::kMod:
                    case OperationKind::kAnd:
                    case OperationKind::kOr:
                    case OperationKind::kXor:
                    case OperationKind::kXnor:
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
                    case OperationKind::kReduceAnd:
                    case OperationKind::kReduceNand:
                    case OperationKind::kReduceOr:
                    case OperationKind::kReduceNor:
                    case OperationKind::kReduceXor:
                    case OperationKind::kReduceXnor:
                    case OperationKind::kShl:
                    case OperationKind::kLShr:
                    case OperationKind::kAShr:
                    case OperationKind::kMux:
                    case OperationKind::kConcat:
                    case OperationKind::kReplicate:
                    case OperationKind::kSliceStatic:
                    case OperationKind::kSliceDynamic:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        const ValueId resultValue = op.results().front();
                        if (opNeedsWordLogicEmit(graph, op))
                        {
                            std::string emitError;
                            if (!emitWordLogicOperation(*stream, graph, model, op, emitError))
                            {
                                reportError(emitError, std::string(op.symbolText()));
                                result.success = false;
                                return result;
                            }
                            break;
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
