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
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
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

        std::size_t parseScheduleBatchMaxOps(const EmitOptions &options)
        {
            auto it = options.attributes.find("sched_batch_max_ops");
            if (it == options.attributes.end())
            {
                return 512;
            }
            try
            {
                return static_cast<std::size_t>(std::stoull(it->second));
            }
            catch (const std::exception &)
            {
                return 512;
            }
        }

        std::size_t parseScheduleBatchMaxEstimatedLines(const EmitOptions &options)
        {
            auto it = options.attributes.find("sched_batch_max_estimated_lines");
            if (it == options.attributes.end())
            {
                return 4096;
            }
            try
            {
                return static_cast<std::size_t>(std::stoull(it->second));
            }
            catch (const std::exception &)
            {
                return 4096;
            }
        }

        std::size_t parseEmitParallelism(const EmitOptions &options)
        {
            auto fallback = []() -> std::size_t
            {
                const unsigned value = std::thread::hardware_concurrency();
                return value == 0 ? 1u : static_cast<std::size_t>(value);
            };

            auto it = options.attributes.find("emit_parallelism");
            if (it == options.attributes.end())
            {
                return fallback();
            }
            try
            {
                const std::size_t parsed = static_cast<std::size_t>(std::stoull(it->second));
                return parsed == 0 ? fallback() : parsed;
            }
            catch (const std::exception &)
            {
                return fallback();
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

        struct ScheduleBatch
        {
            std::size_t index = 0;
            std::size_t estimatedLines = 0;
            std::size_t opCount = 0;
            std::vector<uint32_t> supernodeIds;
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
            enum class MemoryMaskMode
            {
                kDynamic,
                kConstZero,
                kConstAllOnes,
            };

            enum class MemoryAddrMode
            {
                kGeneric,
                kInRange,
                kPow2Wrap,
            };

            OperationId opId;
            StateDecl::Kind kind = StateDecl::Kind::Register;
            std::string symbol;
            std::string pendingValidField;
            std::string pendingDataField;
            std::string pendingMaskField;
            std::string pendingAddrField;
            MemoryMaskMode memoryMaskMode = MemoryMaskMode::kDynamic;
            MemoryAddrMode memoryAddrMode = MemoryAddrMode::kGeneric;
            std::size_t memoryRowMask = 0;
        };

        struct RegisterWriteGroup
        {
            std::string symbol;
            std::vector<std::size_t> writeIndices;
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
            std::unordered_map<ValueId, bool, ValueIdHash> eventValuePrecomputable;
            std::unordered_map<std::string, StateDecl> stateBySymbol;
            std::unordered_map<OperationId, WriteDecl, OperationIdHash> writeByOp;
            std::unordered_map<std::string, DpiImportDecl> dpiImportBySymbol;
            std::unordered_map<ActivityScheduleEventTerm, std::size_t, ActivityScheduleEventTermHash> eventTermIndex;
            std::unordered_map<ActivityScheduleEventDomainSignature, std::size_t, ActivityScheduleEventDomainSignatureHash>
                eventDomainIndex;
            std::vector<ActivityScheduleEventTerm> eventTerms;
            std::vector<bool> eventDomainPrecomputable;
            std::vector<WriteDecl> writes;
            std::vector<RegisterWriteGroup> registerWriteGroups;
            std::vector<DpiImportDecl> dpiImports;
            std::vector<std::string> stateOrder;
            std::vector<std::string> valueFieldDecls;
            std::vector<std::string> stateFieldDecls;
            std::vector<std::string> writeFieldDecls;
            std::vector<std::string> dpiDecls;
            std::vector<std::string> publicPortDecls;
            std::vector<std::string> inputFieldDecls;
            std::vector<std::string> outputFieldDecls;
            bool needsSystemTaskRuntime = false;
        };

        bool opNeedsWordLogicEmit(const Graph &graph, const Operation &op) noexcept;
        std::string valueRef(const EmitModel &model, ValueId value);

        std::optional<slang::SVInt> constLogicValue(const Graph &graph, ValueId value, int32_t width)
        {
            if (graph.valueType(value) != ValueType::Logic || width <= 0)
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kConstant)
            {
                return std::nullopt;
            }
            auto literal = getAttribute<std::string>(defOp, "constValue");
            if (!literal)
            {
                return std::nullopt;
            }
            auto parsed = parseConstLiteral(*literal);
            if (!parsed)
            {
                return std::nullopt;
            }
            parsed = parsed->resize(static_cast<slang::bitwidth_t>(width));
            if (parsed->hasUnknown())
            {
                return std::nullopt;
            }
            return parsed;
        }

        bool isConstLogicAllZero(const Graph &graph, ValueId value, int32_t width)
        {
            auto parsed = constLogicValue(graph, value, width);
            if (!parsed)
            {
                return false;
            }
            const std::size_t words = logicWordCount(width);
            const std::uint64_t *rawWords = parsed->getRawPtr();
            for (std::size_t i = 0; i < words; ++i)
            {
                if (rawWords[i] != 0)
                {
                    return false;
                }
            }
            return true;
        }

        bool isConstLogicAllOnes(const Graph &graph, ValueId value, int32_t width)
        {
            auto parsed = constLogicValue(graph, value, width);
            if (!parsed)
            {
                return false;
            }
            const std::size_t words = logicWordCount(width);
            const std::uint64_t *rawWords = parsed->getRawPtr();
            for (std::size_t i = 0; i < words; ++i)
            {
                const std::size_t wordWidth =
                    (i + 1u == words) ? (static_cast<std::size_t>(width) - i * 64u) : 64u;
                const std::uint64_t expected = wordWidth >= 64u ? ~UINT64_C(0) : ((UINT64_C(1) << wordWidth) - 1u);
                if (rawWords[i] != expected)
                {
                    return false;
                }
            }
            return true;
        }

        bool isPowerOfTwoU64(std::uint64_t value) noexcept
        {
            return value != 0 && (value & (value - 1u)) == 0;
        }

        std::size_t minUnsignedBitsForRange(std::uint64_t exclusiveUpperBound) noexcept
        {
            std::size_t bits = 0;
            std::uint64_t limit = 1;
            while (limit < exclusiveUpperBound)
            {
                limit <<= 1u;
                ++bits;
            }
            return bits;
        }

        bool validateRegisterWritePort(const Graph &graph,
                                       const Operation &op,
                                       const StateDecl &state,
                                       std::string &error)
        {
            const auto operands = op.operands();
            const std::string opName = std::string(op.symbolText());
            if (!op.results().empty())
            {
                error = "kRegisterWritePort must not have results: " + opName;
                return false;
            }
            if (operands.size() < 4)
            {
                error = "kRegisterWritePort missing operands: " + opName;
                return false;
            }
            if (graph.valueType(operands[0]) != ValueType::Logic || graph.valueWidth(operands[0]) != 1)
            {
                error = "kRegisterWritePort updateCond must be 1-bit logic: " + opName;
                return false;
            }
            if (graph.valueType(operands[1]) != ValueType::Logic || graph.valueWidth(operands[1]) != state.width)
            {
                error = "kRegisterWritePort nextValue width/type mismatch: " + opName;
                return false;
            }
            if (graph.valueType(operands[2]) != ValueType::Logic || graph.valueWidth(operands[2]) != state.width)
            {
                error = "kRegisterWritePort mask width/type mismatch: " + opName;
                return false;
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            if (!eventEdges)
            {
                error = "kRegisterWritePort missing eventEdge: " + opName;
                return false;
            }
            const std::size_t eventCount = operands.size() - 3;
            if (eventEdges->size() != eventCount)
            {
                error = "kRegisterWritePort eventEdge size mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                const ValueId eventValue = operands[3 + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kRegisterWritePort event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }
            return true;
        }

        bool validateMemoryWritePort(const Graph &graph,
                                     const Operation &op,
                                     const StateDecl &state,
                                     std::string &error)
        {
            const auto operands = op.operands();
            const std::string opName = std::string(op.symbolText());
            if (!op.results().empty())
            {
                error = "kMemoryWritePort must not have results: " + opName;
                return false;
            }
            if (operands.size() < 5)
            {
                error = "kMemoryWritePort missing operands: " + opName;
                return false;
            }
            if (graph.valueType(operands[0]) != ValueType::Logic || graph.valueWidth(operands[0]) != 1)
            {
                error = "kMemoryWritePort updateCond must be 1-bit logic: " + opName;
                return false;
            }
            if (graph.valueType(operands[1]) != ValueType::Logic || graph.valueWidth(operands[1]) <= 0)
            {
                error = "kMemoryWritePort addr must be logic: " + opName;
                return false;
            }
            if (graph.valueType(operands[2]) != ValueType::Logic || graph.valueWidth(operands[2]) != state.width)
            {
                error = "kMemoryWritePort data width/type mismatch: " + opName;
                return false;
            }
            if (graph.valueType(operands[3]) != ValueType::Logic || graph.valueWidth(operands[3]) != state.width)
            {
                error = "kMemoryWritePort mask width/type mismatch: " + opName;
                return false;
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            if (!eventEdges)
            {
                error = "kMemoryWritePort missing eventEdge: " + opName;
                return false;
            }
            const std::size_t eventCount = operands.size() - 4;
            if (eventEdges->size() != eventCount)
            {
                error = "kMemoryWritePort eventEdge size mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                const ValueId eventValue = operands[4 + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kMemoryWritePort event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }
            return true;
        }

        bool validateSystemTask(const Graph &graph,
                                const Operation &op,
                                std::string &error)
        {
            const auto operands = op.operands();
            const std::string opName = std::string(op.symbolText());
            if (!op.results().empty())
            {
                error = "kSystemTask must not have results: " + opName;
                return false;
            }
            if (operands.empty())
            {
                error = "kSystemTask missing callCond operand: " + opName;
                return false;
            }
            if (graph.valueType(operands[0]) != ValueType::Logic || graph.valueWidth(operands[0]) != 1)
            {
                error = "kSystemTask callCond must be 1-bit logic: " + opName;
                return false;
            }
            auto name = getAttribute<std::string>(op, "name");
            if (!name || name->empty())
            {
                error = "kSystemTask missing name: " + opName;
                return false;
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            const std::size_t eventCount = eventEdges ? eventEdges->size() : 0;
            if (operands.size() < 1 + eventCount)
            {
                error = "kSystemTask eventEdge size mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                const ValueId eventValue = operands[operands.size() - eventCount + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kSystemTask event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }
            return true;
        }

        bool systemTaskIsFinal(const Operation &op)
        {
            return getAttribute<std::string>(op, "procKind").value_or(std::string()) == "final";
        }

        bool systemTaskRunsOnlyOnInitialEval(const Operation &op)
        {
            const std::string procKind = getAttribute<std::string>(op, "procKind").value_or(std::string());
            const bool hasTiming = getAttribute<bool>(op, "hasTiming").value_or(false);
            return procKind == "initial" && !hasTiming;
        }

        bool systemFunctionRunsOnlyOnInitialEval(const Operation &op)
        {
            const std::string procKind = getAttribute<std::string>(op, "procKind").value_or(std::string());
            const bool hasTiming = getAttribute<bool>(op, "hasTiming").value_or(false);
            return procKind == "initial" && !hasTiming;
        }

        std::string systemTaskArgExpr(const Graph &graph,
                                      const EmitModel &model,
                                      ValueId value)
        {
            auto ref = [&]() -> std::string
            {
                if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
                {
                    return it->second;
                }
                if (auto it = model.valueFieldByValue.find(value); it != model.valueFieldByValue.end())
                {
                    return it->second;
                }
                return "/*missing_system_task_arg*/";
            };
            const std::string signedText = graph.valueSigned(value) ? "true" : "false";
            switch (graph.valueType(value))
            {
            case ValueType::String:
                return "grhsim_make_task_arg(" + ref() + ")";
            case ValueType::Real:
                return "grhsim_make_task_arg(static_cast<double>(" + ref() + "))";
            case ValueType::Logic:
            default:
                break;
            }
            if (isWideLogicValue(graph, value))
            {
                std::ostringstream out;
                out << "grhsim_make_task_arg(" << ref() << ", "
                    << graph.valueWidth(value) << ", "
                    << signedText << ")";
                return out.str();
            }
            std::ostringstream out;
            out << "grhsim_make_task_arg(static_cast<std::uint64_t>(" << ref() << "), "
                << graph.valueWidth(value) << ", "
                << signedText << ")";
            return out.str();
        }

        std::string dpiLoweredTypeName(std::string_view typeName)
        {
            std::string lowered;
            lowered.reserve(typeName.size());
            for (unsigned char ch : typeName)
            {
                lowered.push_back(static_cast<char>(std::tolower(ch)));
            }
            return lowered;
        }

        bool dpiTypeIsString(std::string_view typeName)
        {
            return dpiLoweredTypeName(typeName) == "string";
        }

        bool dpiTypeIsReal(std::string_view typeName)
        {
            const std::string lowered = dpiLoweredTypeName(typeName);
            return lowered == "real" || lowered == "shortreal";
        }

        bool dpiTypeIsShortReal(std::string_view typeName)
        {
            return dpiLoweredTypeName(typeName) == "shortreal";
        }

        bool dpiTypeIsLogicLike(std::string_view typeName)
        {
            return !dpiTypeIsString(typeName) && !dpiTypeIsReal(typeName);
        }

        int64_t dpiEffectiveWidth(std::string_view typeName, int64_t width)
        {
            const std::string lowered = dpiLoweredTypeName(typeName);
            if (lowered == "byte")
            {
                return 8;
            }
            if (lowered == "shortint")
            {
                return 16;
            }
            if (lowered == "int" || lowered == "integer")
            {
                return 32;
            }
            if (lowered == "longint" || lowered == "time")
            {
                return 64;
            }
            return width > 0 ? width : 1;
        }

        std::string dpiScalarLogicCppType(int64_t width, bool isSigned)
        {
            if (width <= 1)
            {
                return "bool";
            }
            if (width <= 8)
            {
                return isSigned ? "std::int8_t" : "std::uint8_t";
            }
            if (width <= 16)
            {
                return isSigned ? "std::int16_t" : "std::uint16_t";
            }
            if (width <= 32)
            {
                return isSigned ? "std::int32_t" : "std::uint32_t";
            }
            return isSigned ? "std::int64_t" : "std::uint64_t";
        }

        std::string dpiBaseCppType(std::string_view typeName, int64_t width, bool isSigned)
        {
            const std::string lowered = dpiLoweredTypeName(typeName);
            if (dpiTypeIsShortReal(typeName))
            {
                return "float";
            }
            if (lowered == "real" || lowered == "shortreal")
            {
                return "double";
            }
            if (lowered == "string")
            {
                return "std::string";
            }
            const int64_t effectiveWidth = dpiEffectiveWidth(typeName, width);
            if (effectiveWidth <= 64)
            {
                return dpiScalarLogicCppType(effectiveWidth, isSigned);
            }
            return logicCppType(static_cast<int32_t>(effectiveWidth));
        }

        std::string dpiCppType(std::string_view typeName,
                               int64_t width,
                               bool isSigned,
                               bool isOutputRef)
        {
            const std::string baseType = dpiBaseCppType(typeName, width, isSigned);
            if (isOutputRef)
            {
                return baseType + " &";
            }
            if (dpiTypeIsString(typeName) || dpiEffectiveWidth(typeName, width) > 64)
            {
                return "const " + baseType + " &";
            }
            return baseType;
        }

        int findNameIndex(const std::vector<std::string> &names, std::string_view needle)
        {
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == needle)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        ValueType dpiExpectedValueType(std::string_view typeName)
        {
            if (dpiTypeIsString(typeName))
            {
                return ValueType::String;
            }
            if (dpiTypeIsReal(typeName))
            {
                return ValueType::Real;
            }
            return ValueType::Logic;
        }

        bool validateDpiValueType(const Graph &graph,
                                  ValueId value,
                                  std::string_view typeName,
                                  int64_t width,
                                  std::string_view context,
                                  std::string &error)
        {
            const ValueType expectedType = dpiExpectedValueType(typeName);
            if (graph.valueType(value) != expectedType)
            {
                error = std::string(context) + " value type mismatch";
                return false;
            }
            if (expectedType == ValueType::Logic)
            {
                const int64_t expectedWidth = dpiEffectiveWidth(typeName, width);
                if (graph.valueWidth(value) != expectedWidth)
                {
                    error = std::string(context) + " width mismatch";
                    return false;
                }
            }
            return true;
        }

        std::string dpiValueExpr(const Graph &graph,
                                 const EmitModel &model,
                                 ValueId value,
                                 std::string_view typeName,
                                 int64_t width,
                                 bool isSigned)
        {
            const std::string ref = valueRef(model, value);
            if (dpiTypeIsShortReal(typeName))
            {
                return "static_cast<float>(" + ref + ")";
            }
            if (dpiTypeIsReal(typeName) || dpiTypeIsString(typeName))
            {
                return ref;
            }
            const int64_t effectiveWidth = dpiEffectiveWidth(typeName, width);
            if (effectiveWidth > 64)
            {
                return ref;
            }
            return "static_cast<" + dpiBaseCppType(typeName, width, isSigned) + ">(" + ref + ")";
        }

        bool validateDpicCall(const Graph &graph,
                              const Operation &op,
                              const EmitModel &model,
                              std::string &error)
        {
            const std::string opName = std::string(op.symbolText());
            const auto operands = op.operands();
            if (operands.empty())
            {
                error = "kDpicCall missing callCond operand: " + opName;
                return false;
            }
            if (graph.valueType(operands[0]) != ValueType::Logic || graph.valueWidth(operands[0]) != 1)
            {
                error = "kDpicCall callCond must be 1-bit logic: " + opName;
                return false;
            }

            const auto targetImport = getAttribute<std::string>(op, "targetImportSymbol");
            if (!targetImport)
            {
                error = "kDpicCall missing targetImportSymbol: " + opName;
                return false;
            }
            auto importIt = model.dpiImportBySymbol.find(*targetImport);
            if (importIt == model.dpiImportBySymbol.end())
            {
                error = "kDpicCall target import not found: " + *targetImport;
                return false;
            }
            const DpiImportDecl &decl = importIt->second;

            const std::vector<std::string> inArgName =
                getAttribute<std::vector<std::string>>(op, "inArgName").value_or(std::vector<std::string>{});
            const std::vector<std::string> outArgName =
                getAttribute<std::vector<std::string>>(op, "outArgName").value_or(std::vector<std::string>{});
            const std::vector<std::string> inoutArgName =
                getAttribute<std::vector<std::string>>(op, "inoutArgName").value_or(std::vector<std::string>{});
            const std::vector<std::string> eventEdges =
                getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{});
            const bool hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);

            if (!inoutArgName.empty())
            {
                error = "kDpicCall inout args are not supported in grhsim-cpp emit: " + opName;
                return false;
            }
            for (const std::string &direction : decl.argsDirection)
            {
                if (direction == "inout")
                {
                    error = "kDpicImport with inout args is not supported in grhsim-cpp emit: " + decl.symbol;
                    return false;
                }
            }
            if (hasReturn != decl.hasReturn)
            {
                error = "kDpicCall hasReturn mismatch: " + opName;
                return false;
            }
            if (operands.size() != 1 + inArgName.size() + eventEdges.size())
            {
                error = "kDpicCall operand count mismatch: " + opName;
                return false;
            }
            const std::size_t expectedResults = (hasReturn ? 1u : 0u) + outArgName.size();
            if (op.results().size() != expectedResults)
            {
                error = "kDpicCall result count mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventEdges.size(); ++i)
            {
                const ValueId eventValue = operands[1 + inArgName.size() + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kDpicCall event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }

            auto validateUniqueNames = [&](const std::vector<std::string> &names, std::string_view kind) -> bool
            {
                std::unordered_set<std::string> seen;
                for (const std::string &name : names)
                {
                    if (!seen.insert(name).second)
                    {
                        error = "kDpicCall duplicate " + std::string(kind) + " name: " + opName;
                        return false;
                    }
                }
                return true;
            };
            if (!validateUniqueNames(inArgName, "input arg") || !validateUniqueNames(outArgName, "output arg"))
            {
                return false;
            }

            std::size_t importInputCount = 0;
            std::size_t importOutputCount = 0;
            for (const std::string &direction : decl.argsDirection)
            {
                if (direction == "input")
                {
                    ++importInputCount;
                }
                else if (direction == "output")
                {
                    ++importOutputCount;
                }
            }
            if (inArgName.size() != importInputCount || outArgName.size() != importOutputCount)
            {
                error = "kDpicCall arg group size mismatch: " + opName;
                return false;
            }

            std::size_t resultBase = 0;
            if (hasReturn)
            {
                if (!validateDpiValueType(graph,
                                          op.results()[0],
                                          decl.returnType,
                                          decl.returnWidth,
                                          "kDpicCall return",
                                          error))
                {
                    error += ": " + opName;
                    return false;
                }
                resultBase = 1;
            }

            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
            {
                const std::string &direction = decl.argsDirection[i];
                const std::string formalName =
                    i < decl.argsName.size() ? decl.argsName[i] : ("arg" + std::to_string(i));
                const std::string_view typeName =
                    i < decl.argsType.size() ? std::string_view(decl.argsType[i]) : std::string_view("logic");
                const int64_t width = i < decl.argsWidth.size() ? decl.argsWidth[i] : 1;
                if (direction == "input")
                {
                    const int inputIndex = findNameIndex(inArgName, formalName);
                    if (inputIndex < 0)
                    {
                        error = "kDpicCall missing matching input arg " + formalName + ": " + opName;
                        return false;
                    }
                    if (!validateDpiValueType(graph,
                                              operands[1 + static_cast<std::size_t>(inputIndex)],
                                              typeName,
                                              width,
                                              "kDpicCall input arg",
                                              error))
                    {
                        error += ": " + opName + " formal=" + formalName;
                        return false;
                    }
                }
                else if (direction == "output")
                {
                    const int outputIndex = findNameIndex(outArgName, formalName);
                    if (outputIndex < 0)
                    {
                        error = "kDpicCall missing matching output arg " + formalName + ": " + opName;
                        return false;
                    }
                    if (!validateDpiValueType(graph,
                                              op.results()[resultBase + static_cast<std::size_t>(outputIndex)],
                                              typeName,
                                              width,
                                              "kDpicCall output arg",
                                              error))
                    {
                        error += ": " + opName + " formal=" + formalName;
                        return false;
                    }
                }
                else
                {
                    error = "kDpicImport has unsupported arg direction for grhsim-cpp emit: " + decl.symbol;
                    return false;
                }
            }
            return true;
        }

        bool buildModel(const Graph &graph,
                        const ScheduleRefs &schedule,
                        EmitModel &model,
                        std::string &error)
        {
            auto registerInputEndpoint = [&](ValueId valueId, const std::string &fieldStem, const std::string &apiStem) {
                if (model.inputFieldByValue.find(valueId) != model.inputFieldByValue.end())
                {
                    return;
                }
                const std::string typeName = cppTypeForValue(graph, valueId);
                model.inputFieldByValue.emplace(valueId, fieldStem);
                model.prevInputFieldByValue.emplace(valueId, "prev_" + fieldStem);
                model.inputFieldDecls.push_back("    " + typeName + " " + fieldStem + " = " + defaultInitExpr(graph, valueId) + ";");
                model.inputFieldDecls.push_back("    " + typeName + " prev_" + fieldStem + " = " + defaultInitExpr(graph, valueId) + ";");
                model.publicPortDecls.push_back("    " + typeName + " " + apiStem + " = " + defaultInitExpr(graph, valueId) + ";");
            };

            auto registerReadableEndpoint =
                [&](ValueId valueId, const std::string &fieldStem, const std::string &apiStem, bool initializeField) {
                    const std::string typeName = cppTypeForValue(graph, valueId);
                    const std::string initExpr = initializeField ? defaultInitExpr(graph, valueId) : typeName + "{}";
                    model.outputFieldDecls.push_back("    " + typeName + " " + fieldStem + " = " + initExpr + ";");
                    model.publicPortDecls.push_back("    " + typeName + " " + apiStem + " = " + initExpr + ";");
                };

            for (const auto &port : graph.inputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                registerInputEndpoint(port.value, "in_" + name, name);
            }

            for (const auto &port : graph.outputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                registerReadableEndpoint(port.value, "out_" + name, name, true);
            }

            for (const auto &port : graph.inoutPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                const std::string inType = cppTypeForValue(graph, port.in);
                const std::string outType = cppTypeForValue(graph, port.out);
                const std::string oeType = cppTypeForValue(graph, port.oe);
                model.inputFieldByValue.emplace(port.in, "inout_" + name + "_in");
                model.prevInputFieldByValue.emplace(port.in, "prev_inout_" + name + "_in");
                model.inputFieldDecls.push_back("    " + inType + " inout_" + name + "_in = " + defaultInitExpr(graph, port.in) + ";");
                model.inputFieldDecls.push_back("    " + inType + " prev_inout_" + name + "_in = " + defaultInitExpr(graph, port.in) + ";");
                model.outputFieldDecls.push_back("    " + outType + " inout_" + name + "_out = " + defaultInitExpr(graph, port.out) + ";");
                model.outputFieldDecls.push_back("    " + oeType + " inout_" + name + "_oe = " + defaultInitExpr(graph, port.oe) + ";");
                model.publicPortDecls.push_back("    struct Inout_" + name + " {\n"
                                                "        " + inType + " in = " + defaultInitExpr(graph, port.in) + ";\n"
                                                "        " + outType + " out = " + defaultInitExpr(graph, port.out) + ";\n"
                                                "        " + oeType + " oe = " + defaultInitExpr(graph, port.oe) + ";\n"
                                                "    } " + name + ";");
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
                    decl.argsName.resize(decl.argsDirection.size());
                    decl.argsWidth.resize(decl.argsDirection.size(), 1);
                    decl.argsSigned.resize(decl.argsDirection.size(), false);
                    decl.argsType.resize(decl.argsDirection.size(), std::string("logic"));
                    for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                    {
                        if (decl.argsName[i].empty())
                        {
                            decl.argsName[i] = "arg" + std::to_string(i);
                        }
                        decl.argsWidth[i] = dpiEffectiveWidth(decl.argsType[i], decl.argsWidth[i]);
                    }
                    decl.returnWidth = dpiEffectiveWidth(decl.returnType, decl.returnWidth);
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
                if (op.kind() == OperationKind::kSystemTask)
                {
                    model.needsSystemTaskRuntime = true;
                    if (!validateSystemTask(graph, op, error))
                    {
                        return false;
                    }
                    continue;
                }
                if (op.kind() == OperationKind::kSystemFunction)
                {
                    const std::string name = getAttribute<std::string>(op, "name").value_or(std::string());
                    if (name == "fopen" || name == "ferror")
                    {
                        model.needsSystemTaskRuntime = true;
                    }
                    continue;
                }
                if (op.kind() == OperationKind::kDpicCall)
                {
                    if (!validateDpicCall(graph, op, model, error))
                    {
                        return false;
                    }
                    continue;
                }
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
                    const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                    if (write.kind == StateDecl::Kind::Register)
                    {
                        if (state.kind != StateDecl::Kind::Register)
                        {
                            error = "kRegisterWritePort target is not a register: " + write.symbol;
                            return false;
                        }
                        if (!validateRegisterWritePort(graph, op, state, error))
                        {
                            return false;
                        }
                    }
                    else if (write.kind == StateDecl::Kind::Memory)
                    {
                        if (state.kind != StateDecl::Kind::Memory)
                        {
                            error = "kMemoryWritePort target is not a memory: " + write.symbol;
                            return false;
                        }
                        if (!validateMemoryWritePort(graph, op, state, error))
                        {
                            return false;
                        }
                        const auto operands = op.operands();
                        const ValueId addrValue = operands[1];
                        const ValueId maskValue = operands[3];
                        if (isConstLogicAllZero(graph, maskValue, state.width))
                        {
                            write.memoryMaskMode = WriteDecl::MemoryMaskMode::kConstZero;
                        }
                        else if (isConstLogicAllOnes(graph, maskValue, state.width))
                        {
                            write.memoryMaskMode = WriteDecl::MemoryMaskMode::kConstAllOnes;
                        }
                        if (state.rowCount > 0)
                        {
                            const std::uint64_t rowCount = static_cast<std::uint64_t>(state.rowCount);
                            if (isPowerOfTwoU64(rowCount))
                            {
                                write.memoryAddrMode = WriteDecl::MemoryAddrMode::kPow2Wrap;
                                write.memoryRowMask = static_cast<std::size_t>(rowCount - 1u);
                            }
                            else
                            {
                                const int32_t addrWidth = graph.valueWidth(addrValue);
                                if (addrWidth > 0 && addrWidth < 64 &&
                                    (UINT64_C(1) << static_cast<std::size_t>(addrWidth)) <= rowCount)
                                {
                                    write.memoryAddrMode = WriteDecl::MemoryAddrMode::kInRange;
                                }
                            }
                        }
                    }
                    const std::string base = "pending_" + opDebugName(graph, opId) + "_" + std::to_string(opId.index);
                    write.pendingValidField = base + "_valid";
                    write.pendingDataField = base + "_data";
                    write.pendingMaskField = base + "_mask";
                    write.pendingAddrField = base + "_addr";
                    model.writeByOp.emplace(opId, write);
                    model.writes.push_back(write);

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

            std::unordered_map<std::string, std::size_t> registerWriteGroupIndex;
            for (std::size_t writeIndex = 0; writeIndex < model.writes.size(); ++writeIndex)
            {
                const WriteDecl &write = model.writes[writeIndex];
                if (write.kind != StateDecl::Kind::Register)
                {
                    continue;
                }
                auto [it, inserted] = registerWriteGroupIndex.emplace(write.symbol, model.registerWriteGroups.size());
                if (inserted)
                {
                    RegisterWriteGroup group;
                    group.symbol = write.symbol;
                    model.registerWriteGroups.push_back(std::move(group));
                }
                model.registerWriteGroups[it->second].writeIndices.push_back(writeIndex);
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
            for (const auto &[value, currField] : model.eventCurrentVarByValue)
            {
                model.valueFieldDecls.push_back("    " + cppTypeForValue(graph, value) + " " + currField + " = " +
                                                defaultInitExpr(graph, value) + ";");
            }

                for (const auto &decl : model.dpiImports)
                {
                    std::vector<std::string> args;
                    const std::size_t argCount = decl.argsDirection.size();
                    for (std::size_t i = 0; i < argCount; ++i)
                    {
                    const std::string argType =
                        dpiCppType(i < decl.argsType.size() ? std::string_view(decl.argsType[i]) : std::string_view("logic"),
                                   i < decl.argsWidth.size() ? decl.argsWidth[i] : 64,
                                   i < decl.argsSigned.size() ? decl.argsSigned[i] : false,
                                   decl.argsDirection[i] == "output" || decl.argsDirection[i] == "inout");
                    const std::string &direction = decl.argsDirection[i];
                    (void)direction;
                    args.push_back(argType + " " + sanitizeIdentifier(i < decl.argsName.size() ? decl.argsName[i] : ("arg" + std::to_string(i))));
                    }
                const std::string returnType =
                    decl.hasReturn ? dpiBaseCppType(decl.returnType, decl.returnWidth, decl.returnSigned) : "void";
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

        std::string boolLiteral(bool value)
        {
            return value ? "true" : "false";
        }

        std::string wordsArrayTypeForWidth(int32_t width)
        {
            std::ostringstream out;
            out << "std::array<std::uint64_t, " << logicWordCount(width) << ">";
            return out.str();
        }

        std::size_t estimateOperationEmitLines(const Graph &graph, const Operation &op)
        {
            switch (op.kind())
            {
            case OperationKind::kConstant:
            case OperationKind::kRegisterReadPort:
            case OperationKind::kLatchReadPort:
                return 4;
            case OperationKind::kMemoryReadPort:
                return 6;
            case OperationKind::kRegisterWritePort:
            case OperationKind::kLatchWritePort:
            case OperationKind::kMemoryWritePort:
                return 7;
            case OperationKind::kSystemTask:
            case OperationKind::kDpicCall:
                return 10;
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
            case OperationKind::kCaseEq:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardEq:
            case OperationKind::kWildcardNe:
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
            case OperationKind::kSliceArray:
            case OperationKind::kSystemFunction:
                return opNeedsWordLogicEmit(graph, op) ? 8 : 5;
            default:
                return 2;
            }
        }

        std::size_t estimateSupernodeEmitLines(const Graph &graph,
                                               const ActivityScheduleSupernodeToOps &supernodeToOps,
                                               uint32_t supernodeId)
        {
            std::size_t total = 8;
            if (supernodeId >= supernodeToOps.size())
            {
                return total;
            }
            for (OperationId opId : supernodeToOps[supernodeId])
            {
                total += estimateOperationEmitLines(graph, graph.getOperation(opId));
            }
            return total;
        }

        std::vector<ScheduleBatch> buildScheduleBatches(const Graph &graph,
                                                        const ScheduleRefs &schedule,
                                                        std::size_t batchMaxOps,
                                                        std::size_t batchMaxEstimatedLines)
        {
            std::vector<ScheduleBatch> batches;
            ScheduleBatch current;
            auto flushCurrent = [&]()
            {
                if (current.supernodeIds.empty())
                {
                    return;
                }
                current.index = batches.size();
                batches.push_back(std::move(current));
                current = ScheduleBatch{};
            };

            for (uint32_t supernodeId : *schedule.topoOrder)
            {
                const std::size_t supernodeOps =
                    supernodeId < schedule.supernodeToOps->size() ? (*schedule.supernodeToOps)[supernodeId].size() : 0;
                const std::size_t supernodeLines =
                    estimateSupernodeEmitLines(graph, *schedule.supernodeToOps, supernodeId);
                const bool hitOpBudget =
                    batchMaxOps != 0 &&
                    !current.supernodeIds.empty() &&
                    current.opCount + supernodeOps > batchMaxOps;
                const bool hitLineBudget =
                    batchMaxEstimatedLines != 0 &&
                    !current.supernodeIds.empty() &&
                    current.estimatedLines + supernodeLines > batchMaxEstimatedLines;
                if (hitOpBudget || hitLineBudget)
                {
                    flushCurrent();
                }
                current.supernodeIds.push_back(supernodeId);
                current.opCount += supernodeOps;
                current.estimatedLines += supernodeLines;
            }
            flushCurrent();
            if (batches.empty())
            {
                batches.push_back(ScheduleBatch{});
            }
            return batches;
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
                << destWidth << ", "
                << boolLiteral(graph.valueSigned(value)) << ")";
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
            case OperationKind::kCaseEq:
            case OperationKind::kWildcardEq:
            {
                const int32_t width = compareWidth();
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "grhsim_compare_unsigned_words(" +
                                                wordsExprForValue(graph, model, operands[0], width) + ", " +
                                                wordsExprForValue(graph, model, operands[1], width) + ") == 0");
                return true;
            }
            case OperationKind::kNe:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardNe:
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
            case OperationKind::kSliceArray:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth");
                if (!sliceWidth)
                {
                    error = "kSliceArray missing sliceWidth";
                    return false;
                }
                std::ostringstream out;
                out << "grhsim_slice_words<" << resultWords << ">("
                    << wordsExprForValue(graph, model, operands[0], graph.valueWidth(operands[0])) << ", "
                    << "(" << valueRef(model, operands[1]) << ") * " << *sliceWidth << ", "
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

        std::string eventWordsCastExpr(const std::string &expr,
                                       int32_t srcWidth,
                                       int32_t destWidth,
                                       bool isSigned)
        {
            std::ostringstream out;
            out << "grhsim_cast_words<" << logicWordCount(destWidth) << ">("
                << expr << ", "
                << srcWidth << ", "
                << destWidth << ", "
                << boolLiteral(isSigned) << ")";
            return out.str();
        }

        std::string eventLogicExprFromWordsExpr(const Graph &graph,
                                                ValueId resultValue,
                                                const std::string &wordsExpr)
        {
            if (isWideLogicValue(graph, resultValue))
            {
                return wordsExpr;
            }
            std::ostringstream out;
            out << "static_cast<" << cppTypeForValue(graph, resultValue) << ">(grhsim_trunc_u64(("
                << wordsExpr << ")[0], " << graph.valueWidth(resultValue) << "))";
            return out.str();
        }

        std::optional<std::string> eventWordLogicExprForOp(const Graph &graph,
                                                           const EmitModel &model,
                                                           const Operation &op,
                                                           ValueId resultValue,
                                                           const std::vector<std::string> &operandExprs)
        {
            const auto operands = op.operands();
            const int32_t resultWidth = graph.valueWidth(resultValue);
            const std::size_t resultWords = logicWordCount(resultWidth);
            auto operandWords = [&](std::size_t index, int32_t width) -> std::string
            {
                const ValueId value = operands[index];
                return eventWordsCastExpr(operandExprs[index], graph.valueWidth(value), width, graph.valueSigned(value));
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
            auto logicBoolExpr = [&](const std::string &expr) -> std::string
            {
                return "static_cast<" + cppTypeForValue(graph, resultValue) + ">(" + expr + ")";
            };

            switch (op.kind())
            {
            case OperationKind::kAssign:
                return eventLogicExprFromWordsExpr(graph, resultValue, operandWords(0, resultWidth));
            case OperationKind::kAdd:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_add_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kSub:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_sub_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kMul:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_mul_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kDiv:
            {
                const bool signedMode = graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string helper = signedMode ? "grhsim_sdiv_words" : "grhsim_udiv_words";
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   helper + "(" + operandWords(0, resultWidth) + ", " +
                                                       operandWords(1, resultWidth) + ", " +
                                                       std::to_string(resultWidth) + ")");
            }
            case OperationKind::kMod:
            {
                const bool signedMode = graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string helper = signedMode ? "grhsim_smod_words" : "grhsim_umod_words";
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   helper + "(" + operandWords(0, resultWidth) + ", " +
                                                       operandWords(1, resultWidth) + ", " +
                                                       std::to_string(resultWidth) + ")");
            }
            case OperationKind::kAnd:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_and_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kOr:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_or_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kXor:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_xor_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kXnor:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_xnor_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kNot:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_not_words(" + operandWords(0, resultWidth) + ", " + std::to_string(resultWidth) + ")");
            case OperationKind::kEq:
            case OperationKind::kCaseEq:
            case OperationKind::kWildcardEq:
            {
                const int32_t width = compareWidth();
                return logicBoolExpr("grhsim_compare_unsigned_words(" + operandWords(0, width) + ", " +
                                     operandWords(1, width) + ") == 0");
            }
            case OperationKind::kNe:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardNe:
            {
                const int32_t width = compareWidth();
                return logicBoolExpr("grhsim_compare_unsigned_words(" + operandWords(0, width) + ", " +
                                     operandWords(1, width) + ") != 0");
            }
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            {
                const int32_t width = compareWidth();
                const bool signedMode = graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string cmpExpr = signedMode
                                                ? ("grhsim_compare_signed_words(" + operandWords(0, width) + ", " +
                                                   operandWords(1, width) + ", " + std::to_string(width) + ")")
                                                : ("grhsim_compare_unsigned_words(" + operandWords(0, width) + ", " +
                                                   operandWords(1, width) + ")");
                if (op.kind() == OperationKind::kLt)
                {
                    return logicBoolExpr(cmpExpr + " < 0");
                }
                if (op.kind() == OperationKind::kLe)
                {
                    return logicBoolExpr(cmpExpr + " <= 0");
                }
                if (op.kind() == OperationKind::kGt)
                {
                    return logicBoolExpr(cmpExpr + " > 0");
                }
                return logicBoolExpr(cmpExpr + " >= 0");
            }
            case OperationKind::kLogicAnd:
                return logicBoolExpr("grhsim_any_bits_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ") && grhsim_any_bits_words(" +
                                     operandWords(1, graph.valueWidth(operands[1])) + ", " +
                                     std::to_string(graph.valueWidth(operands[1])) + ")");
            case OperationKind::kLogicOr:
                return logicBoolExpr("grhsim_any_bits_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ") || grhsim_any_bits_words(" +
                                     operandWords(1, graph.valueWidth(operands[1])) + ", " +
                                     std::to_string(graph.valueWidth(operands[1])) + ")");
            case OperationKind::kLogicNot:
                return logicBoolExpr("!grhsim_any_bits_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kReduceAnd:
                return logicBoolExpr("grhsim_reduce_and_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kReduceNand:
                return logicBoolExpr("grhsim_reduce_nand_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kReduceOr:
                return logicBoolExpr("grhsim_reduce_or_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kReduceNor:
                return logicBoolExpr("grhsim_reduce_nor_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kReduceXor:
                return logicBoolExpr("grhsim_reduce_xor_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kReduceXnor:
                return logicBoolExpr("grhsim_reduce_xnor_words(" + operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                     std::to_string(graph.valueWidth(operands[0])) + ")");
            case OperationKind::kShl:
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   "grhsim_shl_words(" + operandWords(0, resultWidth) + ", " +
                                                       operandExprs[1] + ", " + std::to_string(resultWidth) + ")");
            case OperationKind::kLShr:
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   "grhsim_lshr_words(" + operandWords(0, resultWidth) + ", " +
                                                       operandExprs[1] + ", " + std::to_string(resultWidth) + ")");
            case OperationKind::kAShr:
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   "grhsim_ashr_words(" + operandWords(0, resultWidth) + ", " +
                                                       operandExprs[1] + ", " + std::to_string(resultWidth) + ")");
            case OperationKind::kMux:
            {
                const ValueId condValue = operands[0];
                const std::string condExpr =
                    isWideLogicValue(graph, condValue)
                        ? ("grhsim_any_bits_words(" + operandWords(0, graph.valueWidth(condValue)) + ", " +
                           std::to_string(graph.valueWidth(condValue)) + ")")
                        : ("(" + operandExprs[0] + ") != 0");
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   "((" + condExpr + ") ? " + operandWords(1, resultWidth) + " : " +
                                                       operandWords(2, resultWidth) + ")");
            }
            case OperationKind::kConcat:
            {
                std::ostringstream out;
                out << "([&]() -> " << wordsArrayTypeForWidth(resultWidth) << " { ";
                out << wordsArrayTypeForWidth(resultWidth) << " next_words{}; ";
                out << "std::size_t concat_cursor = " << resultWidth << "; ";
                for (std::size_t operandIndex = 0; operandIndex < operands.size(); ++operandIndex)
                {
                    const ValueId operand = operands[operandIndex];
                    const int32_t operandWidth = graph.valueWidth(operand);
                    out << "concat_cursor -= " << operandWidth << "; ";
                    out << "grhsim_insert_words(next_words, concat_cursor, "
                        << operandWords(operandIndex, operandWidth) << ", " << operandWidth << "); ";
                }
                out << "return next_words; }())";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            case OperationKind::kReplicate:
            {
                const auto rep = getAttribute<int64_t>(op, "rep").value_or(1);
                const int32_t operandWidth = graph.valueWidth(operands[0]);
                std::ostringstream out;
                out << "([&]() -> " << wordsArrayTypeForWidth(resultWidth) << " { ";
                out << wordsArrayTypeForWidth(resultWidth) << " next_words{}; ";
                out << "for (std::size_t rep_index = 0; rep_index < " << rep << "; ++rep_index) { ";
                out << "grhsim_insert_words(next_words, rep_index * " << operandWidth << ", "
                    << operandWords(0, operandWidth) << ", " << operandWidth << "); ";
                out << "} grhsim_trunc_words(next_words, " << resultWidth << "); return next_words; }())";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            case OperationKind::kSliceStatic:
            {
                const auto sliceStart = getAttribute<int64_t>(op, "sliceStart").value_or(0);
                std::ostringstream out;
                out << "grhsim_slice_words<" << resultWords << ">("
                    << operandWords(0, graph.valueWidth(operands[0])) << ", "
                    << sliceStart << ", "
                    << resultWidth << ")";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            case OperationKind::kSliceDynamic:
            {
                std::ostringstream out;
                out << "grhsim_slice_words<" << resultWords << ">("
                    << operandWords(0, graph.valueWidth(operands[0])) << ", "
                    << operandExprs[1] << ", "
                    << graph.valueWidth(operands[0]) << ", "
                    << resultWidth << ")";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            case OperationKind::kSliceArray:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth");
                if (!sliceWidth)
                {
                    return std::nullopt;
                }
                std::ostringstream out;
                out << "grhsim_slice_words<" << resultWords << ">("
                    << operandWords(0, graph.valueWidth(operands[0])) << ", "
                    << "((" << operandExprs[1] << ") * " << *sliceWidth << "), "
                    << graph.valueWidth(operands[0]) << ", "
                    << resultWidth << ")";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            default:
                break;
            }
            return std::nullopt;
        }

        bool eventExprTopoCollect(const Graph &graph,
                                  ValueId value,
                                  std::unordered_set<ValueId, ValueIdHash> &visiting,
                                  std::unordered_set<ValueId, ValueIdHash> &visited,
                                  std::vector<ValueId> &ordered)
        {
            if (visited.contains(value))
            {
                return true;
            }
            if (visiting.contains(value))
            {
                return false;
            }
            visiting.insert(value);

            bool ok = true;
            const OperationId defOpId = graph.valueDef(value);
            if (defOpId.valid())
            {
                const Operation op = graph.getOperation(defOpId);
                switch (op.kind())
                {
                case OperationKind::kConstant:
                case OperationKind::kRegisterReadPort:
                case OperationKind::kLatchReadPort:
                    break;
                case OperationKind::kMemoryReadPort:
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
                case OperationKind::kCaseEq:
                case OperationKind::kCaseNe:
                case OperationKind::kWildcardEq:
                case OperationKind::kWildcardNe:
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
                case OperationKind::kSliceArray:
                case OperationKind::kSystemFunction:
                    for (ValueId operand : op.operands())
                    {
                        if (!eventExprTopoCollect(graph, operand, visiting, visited, ordered))
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                    {
                        ordered.push_back(value);
                    }
                    break;
                default:
                    ok = false;
                    break;
                }
            }

            visiting.erase(value);
            if (ok)
            {
                visited.insert(value);
            }
            return ok;
        }

        std::optional<std::string> eventExprLeafRefForValue(const Graph &graph,
                                                            const EmitModel &model,
                                                            ValueId value)
        {
            if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
            {
                return it->second;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return valueRef(model, value);
            }
            const Operation op = graph.getOperation(defOpId);
            switch (op.kind())
            {
            case OperationKind::kConstant:
                return constantExpr(graph, op, value);
            case OperationKind::kRegisterReadPort:
            {
                auto regSymbol = getAttribute<std::string>(op, "regSymbol");
                if (!regSymbol)
                {
                    return std::nullopt;
                }
                auto it = model.stateBySymbol.find(*regSymbol);
                if (it == model.stateBySymbol.end())
                {
                    return std::nullopt;
                }
                return it->second.fieldName;
            }
            case OperationKind::kLatchReadPort:
            {
                auto latchSymbol = getAttribute<std::string>(op, "latchSymbol");
                if (!latchSymbol)
                {
                    return std::nullopt;
                }
                auto it = model.stateBySymbol.find(*latchSymbol);
                if (it == model.stateBySymbol.end())
                {
                    return std::nullopt;
                }
                return it->second.fieldName;
            }
            default:
                break;
            }
            return std::nullopt;
        }

        std::optional<std::string> eventExprRefForValue(const Graph &graph,
                                                        const EmitModel &model,
                                                        ValueId value,
                                                        const std::unordered_map<ValueId, std::string, ValueIdHash> &materializedRefs)
        {
            if (auto it = materializedRefs.find(value); it != materializedRefs.end())
            {
                return it->second;
            }
            return eventExprLeafRefForValue(graph, model, value);
        }

        std::optional<std::string> eventExprMaterializedBodyForValue(
            const Graph &graph,
            const EmitModel &model,
            ValueId value,
            const std::unordered_map<ValueId, std::string, ValueIdHash> &materializedRefs)
        {
            if (auto leaf = eventExprLeafRefForValue(graph, model, value))
            {
                return leaf;
            }

            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return valueRef(model, value);
            }
            const Operation op = graph.getOperation(defOpId);
            const auto operands = op.operands();
            std::vector<std::string> operandExprs;
            operandExprs.reserve(operands.size());
            for (ValueId operand : operands)
            {
                auto operandExpr = eventExprRefForValue(graph, model, operand, materializedRefs);
                if (!operandExpr)
                {
                    return std::nullopt;
                }
                operandExprs.push_back(*operandExpr);
            }

            if (op.kind() == OperationKind::kMemoryReadPort)
            {
                auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                if (!memSymbol || operandExprs.empty() || graph.valueType(value) != ValueType::Logic)
                {
                    return std::nullopt;
                }
                auto stateIt = model.stateBySymbol.find(*memSymbol);
                if (stateIt == model.stateBySymbol.end())
                {
                    return std::nullopt;
                }
                std::ostringstream out;
                out << "([&]() -> " << cppTypeForValue(graph, value) << " { ";
                out << "const std::size_t row = grhsim_index_words(" << operandExprs[0] << ", "
                    << stateIt->second.rowCount << "); ";
                out << "if (row >= " << stateIt->second.rowCount << ") return "
                    << defaultInitExprForLogicWidth(graph.valueWidth(value)) << "; ";
                out << "return " << stateIt->second.fieldName << "[row]; }())";
                return out.str();
            }

            if (graph.valueType(value) != ValueType::Logic)
            {
                return std::nullopt;
            }
            if (opNeedsWordLogicEmit(graph, op))
            {
                return eventWordLogicExprForOp(graph, model, op, value, operandExprs);
            }
            const std::string rhs = scalarAssignmentExpr(op.kind(), operandExprs, op, graph);
            if (rhs.empty())
            {
                return std::nullopt;
            }
            return "static_cast<" + cppTypeForValue(graph, value) +
                   ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" + rhs + "), " +
                   std::to_string(graph.valueWidth(value)) + "))";
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
                    case OperationKind::kMemoryReadPort:
                    {
                        const auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                        const auto operands = op.operands();
                        if (!memSymbol || operands.empty() || graph.valueType(value) != ValueType::Logic)
                        {
                            break;
                        }
                        auto stateIt = model.stateBySymbol.find(*memSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            break;
                        }
                        std::size_t operandCost = 0;
                        auto addrExpr = pureExprForValue(graph, model, operands[0], cache, costCache, operandCost);
                        if (!addrExpr)
                        {
                            break;
                        }
                        cost += operandCost + 1;
                        std::ostringstream out;
                        out << "([&]() -> " << cppTypeForValue(graph, value) << " { ";
                        out << "const std::size_t row = grhsim_index_words(" << *addrExpr << ", "
                            << stateIt->second.rowCount << "); ";
                        out << "if (row >= " << stateIt->second.rowCount << ") return "
                            << defaultInitExprForLogicWidth(graph.valueWidth(value)) << "; ";
                        out << "return " << stateIt->second.fieldName << "[row]; }())";
                        expr = out.str();
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
                    case OperationKind::kCaseEq:
                    case OperationKind::kCaseNe:
                    case OperationKind::kWildcardEq:
                    case OperationKind::kWildcardNe:
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
                    case OperationKind::kSliceArray:
                    case OperationKind::kSystemFunction:
                    {
                        if (graph.valueType(value) != ValueType::Logic)
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
                        if (opNeedsWordLogicEmit(graph, op))
                        {
                            expr = eventWordLogicExprForOp(graph, model, op, value, operandExprs);
                        }
                        else
                        {
                            const std::string rhs = scalarAssignmentExpr(op.kind(), operandExprs, op, graph);
                            if (!rhs.empty())
                            {
                                expr = "static_cast<" + cppTypeForValue(graph, value) +
                                       ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" + rhs + "), " +
                                       std::to_string(graph.valueWidth(value)) + "))";
                            }
                        }
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
            auto operandExpr = [&](std::size_t index, std::size_t width) -> std::string
            {
                const ValueId value = op.operands()[index];
                return "grhsim_cast_u64(static_cast<std::uint64_t>(" + operands[index] + "), " +
                       std::to_string(graph.valueWidth(value)) + ", " + std::to_string(width) + ", " +
                       boolLiteral(graph.valueSigned(value)) + ")";
            };
            auto compareWidth = [&]() -> std::size_t
            {
                std::size_t width = 1;
                for (ValueId value : op.operands())
                {
                    width = std::max(width, static_cast<std::size_t>(graph.valueWidth(value)));
                }
                return width;
            };
            switch (kind)
            {
            case OperationKind::kAssign:
                return operandExpr(0, resultWidth);
            case OperationKind::kAdd:
                return "(" + operandExpr(0, resultWidth) + " + " + operandExpr(1, resultWidth) + ")";
            case OperationKind::kSub:
                return "(" + operandExpr(0, resultWidth) + " - " + operandExpr(1, resultWidth) + ")";
            case OperationKind::kMul:
                return "(" + operandExpr(0, resultWidth) + " * " + operandExpr(1, resultWidth) + ")";
            case OperationKind::kDiv:
            {
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                return std::string(signedMode ? "grhsim_sdiv_u64(" : "grhsim_udiv_u64(") +
                       operandExpr(0, resultWidth) + ", " + operandExpr(1, resultWidth) + ", " +
                       std::to_string(resultWidth) + ")";
            }
            case OperationKind::kMod:
            {
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                return std::string(signedMode ? "grhsim_smod_u64(" : "grhsim_umod_u64(") +
                       operandExpr(0, resultWidth) + ", " + operandExpr(1, resultWidth) + ", " +
                       std::to_string(resultWidth) + ")";
            }
            case OperationKind::kAnd:
                return "(" + operandExpr(0, resultWidth) + " & " + operandExpr(1, resultWidth) + ")";
            case OperationKind::kOr:
                return "(" + operandExpr(0, resultWidth) + " | " + operandExpr(1, resultWidth) + ")";
            case OperationKind::kXor:
                return "(" + operandExpr(0, resultWidth) + " ^ " + operandExpr(1, resultWidth) + ")";
            case OperationKind::kXnor:
                return "(~(" + operandExpr(0, resultWidth) + " ^ " + operandExpr(1, resultWidth) + "))";
            case OperationKind::kNot:
                return "(~(" + operandExpr(0, resultWidth) + "))";
            case OperationKind::kEq:
            case OperationKind::kCaseEq:
            case OperationKind::kWildcardEq:
            {
                const std::size_t width = compareWidth();
                return "((" + operandExpr(0, width) + ") == (" + operandExpr(1, width) + "))";
            }
            case OperationKind::kNe:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardNe:
            {
                const std::size_t width = compareWidth();
                return "((" + operandExpr(0, width) + ") != (" + operandExpr(1, width) + "))";
            }
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            {
                const std::size_t width = compareWidth();
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                const std::string cmpExpr = signedMode
                                                ? "grhsim_compare_signed_u64(" + operandExpr(0, width) + ", " +
                                                      operandExpr(1, width) + ", " + std::to_string(width) + ")"
                                                : "grhsim_compare_unsigned_u64(" + operandExpr(0, width) + ", " +
                                                      operandExpr(1, width) + ", " + std::to_string(width) + ")";
                switch (kind)
                {
                case OperationKind::kLt:
                    return "(" + cmpExpr + " < 0)";
                case OperationKind::kLe:
                    return "(" + cmpExpr + " <= 0)";
                case OperationKind::kGt:
                    return "(" + cmpExpr + " > 0)";
                case OperationKind::kGe:
                    return "(" + cmpExpr + " >= 0)";
                default:
                    break;
                }
                return {};
            }
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
                return "grhsim_shl_u64(" + operandExpr(0, resultWidth) + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            case OperationKind::kLShr:
                return "grhsim_lshr_u64(" + operandExpr(0, resultWidth) + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            case OperationKind::kAShr:
                return "grhsim_ashr_u64(" + operandExpr(0, resultWidth) + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")";
            case OperationKind::kMux:
                return "((" + operands[0] + ") ? (" + operandExpr(1, resultWidth) + ") : (" + operandExpr(2, resultWidth) + "))";
            case OperationKind::kConcat:
            {
                if (operands.empty())
                {
                    return {};
                }
                std::string expr = operands[0];
                int32_t accumWidth = graph.valueWidth(op.operands()[0]);
                for (std::size_t i = 1; i < operands.size(); ++i)
                {
                    expr = "grhsim_concat_u64(" + expr + ", " + std::to_string(accumWidth) + ", " +
                           operands[i] + ", " + std::to_string(graph.valueWidth(op.operands()[i])) + ")";
                    accumWidth += graph.valueWidth(op.operands()[i]);
                }
                return expr;
            }
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
            case OperationKind::kSliceArray:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth");
                if (!sliceWidth)
                {
                    return {};
                }
                return "grhsim_slice_dynamic_u64(" + operands[0] + ", ((" + operands[1] + ") * " +
                       std::to_string(*sliceWidth) + "), " + std::to_string(*sliceWidth) + ")";
            }
            case OperationKind::kSystemFunction:
            {
                const auto name = getAttribute<std::string>(op, "name");
                if (!name || *name != "clog2" || operands.size() != 1)
                {
                    return {};
                }
                return "grhsim_clog2_u64(" + operands[0] + ", " +
                       std::to_string(graph.valueWidth(op.operands()[0])) + ")";
            }
            default:
                break;
            }
            return {};
        }

        std::optional<std::string> exactEventExpr(const Graph &graph,
                                                  const EmitModel &model,
                                                  const Operation &op)
        {
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            const auto operands = op.operands();
            if (!eventEdges || eventEdges->empty())
            {
                return std::string("true");
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
                std::string current;
                if (auto currentIt = model.eventCurrentVarByValue.find(value); currentIt != model.eventCurrentVarByValue.end())
                {
                    current = currentIt->second;
                }
                else
                {
                    std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> cache;
                    std::unordered_map<ValueId, std::size_t, ValueIdHash> costCache;
                    std::size_t totalOps = 0;
                    auto currentExpr = pureExprForValue(graph, model, value, cache, costCache, totalOps);
                    if (!currentExpr)
                    {
                        return std::nullopt;
                    }
                    current = *currentExpr;
                }
                auto prevIt = model.prevEventFieldByValue.find(value);
                if (prevIt == model.prevEventFieldByValue.end())
                {
                    parts.push_back("true");
                    continue;
                }
                const std::string &edge = (*eventEdges)[i];
                if (isWideLogicValue(graph, value))
                {
                    if (edge == "posedge")
                    {
                        parts.push_back("grhsim_event_posedge_words(" + current + ", " + prevIt->second + ", " +
                                        std::to_string(graph.valueWidth(value)) + ")");
                    }
                    else if (edge == "negedge")
                    {
                        parts.push_back("grhsim_event_negedge_words(" + current + ", " + prevIt->second + ", " +
                                        std::to_string(graph.valueWidth(value)) + ")");
                    }
                    else
                    {
                        parts.push_back("(grhsim_compare_unsigned_words(" + current + ", " + prevIt->second + ") != 0)");
                    }
                }
                else if (edge == "posedge")
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
                return std::string("true");
            }
            return "(" + joinStrings(parts, " || ") + ")";
        }

        std::string memoryWriteRowExpr(const Graph &graph,
                                       const EmitModel &model,
                                       const WriteDecl &write,
                                       ValueId addrValue,
                                       const StateDecl &state)
        {
            const std::string addrExpr = valueRef(model, addrValue);
            switch (write.memoryAddrMode)
            {
            case WriteDecl::MemoryAddrMode::kPow2Wrap:
                return "grhsim_index_pow2_words(" + addrExpr + ", " + std::to_string(write.memoryRowMask) + ")";
            case WriteDecl::MemoryAddrMode::kInRange:
                return "grhsim_index_in_range_words(" + addrExpr + ")";
            case WriteDecl::MemoryAddrMode::kGeneric:
            default:
                break;
            }
            return "grhsim_index_words(" + addrExpr + ", " + std::to_string(state.rowCount) + ")";
        }

        std::optional<std::string> emitSchedBatchFile(const std::filesystem::path &schedPath,
                                                      const std::filesystem::path &headerPath,
                                                      const std::string &className,
                                                      const Graph &graph,
                                                      const EmitModel &model,
                                                      const ScheduleRefs &schedule,
                                                      const ScheduleBatch &batch)
        {
            std::ofstream stream(schedPath);
            if (!stream.is_open())
            {
                return "failed to open output file: " + schedPath.string();
            }

            auto emitError = [](std::string_view message, std::string_view detail) -> std::optional<std::string>
            {
                if (detail.empty())
                {
                    return std::string(message);
                }
                return std::string(message) + ": " + std::string(detail);
            };

            stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            stream << "#include <cstdlib>\n";
            stream << "#include <iostream>\n\n";
            stream << "void " << className << "::eval_batch_" << batch.index << "()\n{\n";
            for (uint32_t supernodeId : batch.supernodeIds)
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
                stream << "    if (!grhsim_test_bit(supernode_active_curr_, " << supernodeId << ") || !(" << guardExpr << ")) {\n";
                stream << "        goto supernode_" << supernodeId << "_end;\n";
                stream << "    }\n";
                stream << "    {\n";
                stream << "        bool supernode_changed = false;\n";
                for (const auto opId : (*schedule.supernodeToOps)[supernodeId])
                {
                    const Operation op = graph.getOperation(opId);
                    const auto operands = op.operands();
                    stream << "        // op " << op.symbolText() << "\n";
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
                            return emitError("unsupported constant emit", std::string(op.symbolText()));
                        }
                        const std::string lhs = valueRef(model, resultValue);
                        if (graph.valueType(resultValue) == ValueType::Logic)
                        {
                            if (isWideLogicValue(graph, resultValue))
                            {
                                stream << "        {\n";
                                stream << "            const auto next_value = " << *expr << ";\n";
                                stream << "            if (grhsim_assign_words(" << lhs << ", next_value, "
                                       << graph.valueWidth(resultValue) << ")) { supernode_changed = true; }\n";
                                stream << "        }\n";
                            }
                            else
                            {
                                stream << "        {\n";
                                stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                                       << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << *expr << "), " << graph.valueWidth(resultValue) << "));\n";
                                stream << "            if (" << lhs << " != next_value) { " << lhs << " = next_value; supernode_changed = true; }\n";
                                stream << "        }\n";
                            }
                        }
                        else
                        {
                            stream << "        { const auto next_value = " << *expr << "; if (" << lhs
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
                            return emitError("storage read missing symbol", std::string(op.symbolText()));
                        }
                        const auto stateIt = model.stateBySymbol.find(*targetSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            return emitError("storage read target missing", *targetSymbol);
                        }
                        const std::string lhs = valueRef(model, op.results().front());
                        if (isWideLogicValue(graph, op.results().front()))
                        {
                            stream << "        if (grhsim_assign_words(" << lhs << ", " << stateIt->second.fieldName << ", "
                                   << graph.valueWidth(op.results().front()) << ")) { supernode_changed = true; }\n";
                        }
                        else
                        {
                            stream << "        if (" << lhs << " != " << stateIt->second.fieldName << ") { "
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
                            return emitError("memory read missing memSymbol", std::string(op.symbolText()));
                        }
                        const auto stateIt = model.stateBySymbol.find(*memSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            return emitError("memory read target missing", *memSymbol);
                        }
                        const StateDecl &state = stateIt->second;
                        const std::string lhs = valueRef(model, op.results().front());
                        stream << "        {\n";
                        stream << "            const std::size_t row = grhsim_index_words(" << valueRef(model, operands[0])
                               << ", " << state.rowCount << ");\n";
                        if (isWideLogicValue(graph, op.results().front()))
                        {
                            stream << "            if (row >= " << state.rowCount << ") {\n";
                            stream << "                if (grhsim_assign_words(" << lhs << ", "
                                   << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())) << ", "
                                   << graph.valueWidth(op.results().front()) << ")) { supernode_changed = true; }\n";
                            stream << "            } else {\n";
                            stream << "                const auto next_value = " << state.fieldName << "[row];\n";
                            stream << "            if (grhsim_assign_words(" << lhs << ", next_value, "
                                   << graph.valueWidth(op.results().front()) << ")) { supernode_changed = true; }\n";
                            stream << "            }\n";
                        }
                        else
                        {
                            stream << "            if (row >= " << state.rowCount << ") {\n";
                            stream << "                if (" << lhs << " != " << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front()))
                                   << ") { " << lhs << " = " << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front()))
                                   << "; supernode_changed = true; }\n";
                            stream << "            } else {\n";
                            stream << "                const auto next_value = " << state.fieldName << "[row];\n";
                            stream << "            if (" << lhs << " != next_value) { " << lhs
                                   << " = next_value; supernode_changed = true; }\n";
                            stream << "            }\n";
                        }
                        stream << "        }\n";
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
                    case OperationKind::kCaseEq:
                    case OperationKind::kCaseNe:
                    case OperationKind::kWildcardEq:
                    case OperationKind::kWildcardNe:
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
                    case OperationKind::kSliceArray:
                    case OperationKind::kSystemFunction:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        const ValueId resultValue = op.results().front();
                        if (op.kind() == OperationKind::kSystemFunction &&
                            graph.valueType(resultValue) != ValueType::Logic)
                        {
                            return emitError("unsupported kSystemFunction result type in grhsim-cpp emit",
                                             std::string(op.symbolText()));
                        }
                        if (op.kind() == OperationKind::kSystemFunction)
                        {
                            const std::string name = getAttribute<std::string>(op, "name").value_or(std::string());
                            if (name == "fopen")
                            {
                                if (isWideLogicValue(graph, resultValue))
                                {
                                    return emitError("unsupported wide kSystemFunction fopen result in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                if (operands.empty() || operands.size() > 2)
                                {
                                    return emitError("unsupported kSystemFunction fopen arity in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                const std::string lhs = valueRef(model, resultValue);
                                const std::string pathExpr =
                                    "grhsim_task_default_arg_text(" + systemTaskArgExpr(graph, model, operands[0]) + ")";
                                const std::string modeExpr =
                                    operands.size() >= 2
                                        ? ("grhsim_task_default_arg_text(" +
                                           systemTaskArgExpr(graph, model, operands[1]) + ")")
                                        : std::string("std::string(\"r\")");
                                std::string procGuard = "true";
                                if (systemFunctionRunsOnlyOnInitialEval(op))
                                {
                                    procGuard = "first_eval_";
                                }
                                stream << "        if (" << procGuard << ") {\n";
                                stream << "            const auto next_value = static_cast<"
                                       << cppTypeForValue(graph, resultValue) << ">(grhsim_trunc_u64(open_file_handle("
                                       << pathExpr << ", " << modeExpr << "), "
                                       << graph.valueWidth(resultValue) << "));\n";
                                stream << "            if (" << lhs << " != next_value) { " << lhs
                                       << " = next_value; supernode_changed = true; }\n";
                                stream << "        }\n";
                                break;
                            }
                            if (name == "ferror")
                            {
                                if (isWideLogicValue(graph, resultValue))
                                {
                                    return emitError("unsupported wide kSystemFunction ferror result in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                if (operands.size() != 1)
                                {
                                    return emitError("unsupported kSystemFunction ferror arity in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                const std::string lhs = valueRef(model, resultValue);
                                stream << "        {\n";
                                stream << "            const auto next_value = static_cast<"
                                       << cppTypeForValue(graph, resultValue)
                                       << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(file_error_status(grhsim_task_arg_u64("
                                       << systemTaskArgExpr(graph, model, operands[0]) << "))), "
                                       << graph.valueWidth(resultValue) << "));\n";
                                stream << "            if (" << lhs << " != next_value) { " << lhs
                                       << " = next_value; supernode_changed = true; }\n";
                                stream << "        }\n";
                                break;
                            }
                        }
                        if (opNeedsWordLogicEmit(graph, op))
                        {
                            std::string emitErrorText;
                            if (!emitWordLogicOperation(stream, graph, model, op, emitErrorText))
                            {
                                return emitError(emitErrorText, std::string(op.symbolText()));
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
                            return emitError("unsupported scalar expression emit", std::string(op.symbolText()));
                        }
                        const std::string lhs = valueRef(model, resultValue);
                        stream << "        {\n";
                        stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                               << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << rhs << "), " << graph.valueWidth(resultValue) << "));\n";
                        stream << "            if (" << lhs << " != next_value) { " << lhs << " = next_value; supernode_changed = true; }\n";
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kRegisterWritePort:
                    case OperationKind::kLatchWritePort:
                    case OperationKind::kMemoryWritePort:
                    {
                        const auto writeIt = model.writeByOp.find(opId);
                        if (writeIt == model.writeByOp.end())
                        {
                            return emitError("write metadata missing", std::string(op.symbolText()));
                        }
                        const WriteDecl &write = writeIt->second;
                        const std::string condExpr = operands.empty() ? "true" : valueRef(model, operands[0]);
                        const auto eventExpr = exactEventExpr(graph, model, op);
                        if (!eventExpr)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        stream << "        if ((" << condExpr << ") && (" << *eventExpr << ")) {\n";
                        if (write.kind == StateDecl::Kind::Memory)
                        {
                            const StateDecl &state = model.stateBySymbol.at(write.symbol);
                            if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kConstZero)
                            {
                                stream << "            // constant zero mask: no memory update\n";
                            }
                            else
                            {
                                stream << "            " << write.pendingAddrField << " = "
                                       << memoryWriteRowExpr(graph, model, write, operands[1], state) << ";\n";
                                stream << "            " << write.pendingDataField << " = " << valueRef(model, operands[2]) << ";\n";
                                if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kDynamic)
                                {
                                    stream << "            " << write.pendingMaskField << " = " << valueRef(model, operands[3]) << ";\n";
                                }
                                stream << "            " << write.pendingValidField << " = true;\n";
                            }
                        }
                        else
                        {
                            stream << "            " << write.pendingDataField << " = " << valueRef(model, operands[1]) << ";\n";
                            stream << "            " << write.pendingMaskField << " = " << valueRef(model, operands[2]) << ";\n";
                            stream << "            " << write.pendingValidField << " = true;\n";
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kSystemTask:
                    {
                        if (operands.empty())
                        {
                            break;
                        }
                        const std::string condExpr = valueRef(model, operands[0]);
                        const auto name = getAttribute<std::string>(op, "name").value_or(std::string("display"));
                        const std::size_t eventCount = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}).size();
                        const std::size_t argEnd = operands.size() >= eventCount ? operands.size() - eventCount : operands.size();
                        if (systemTaskIsFinal(op))
                        {
                            break;
                        }
                        const auto eventExpr = exactEventExpr(graph, model, op);
                        if (!eventExpr)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        std::string procGuard = "true";
                        if (systemTaskRunsOnlyOnInitialEval(op))
                        {
                            procGuard = "first_eval_";
                        }
                        stream << "        if ((" << condExpr << ") && (" << *eventExpr << ") && (" << procGuard << ")) {\n";
                        if (argEnd <= 1)
                        {
                            stream << "            execute_system_task(\"" << escapeCppString(name) << "\", {});\n";
                        }
                        else
                        {
                            stream << "            execute_system_task(\"" << escapeCppString(name) << "\", {";
                            for (std::size_t i = 1; i < argEnd; ++i)
                            {
                                if (i != 1)
                                {
                                    stream << ", ";
                                }
                                stream << systemTaskArgExpr(graph, model, operands[i]);
                            }
                            stream << "});\n";
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kDpicCall:
                    {
                        const auto targetImport = getAttribute<std::string>(op, "targetImportSymbol");
                        const auto inArgName = getAttribute<std::vector<std::string>>(op, "inArgName").value_or(std::vector<std::string>{});
                        const auto outArgName = getAttribute<std::vector<std::string>>(op, "outArgName").value_or(std::vector<std::string>{});
                        const bool hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);
                        if (!targetImport)
                        {
                            return emitError("kDpicCall missing targetImportSymbol", std::string(op.symbolText()));
                        }
                        auto importIt = model.dpiImportBySymbol.find(*targetImport);
                        if (importIt == model.dpiImportBySymbol.end())
                        {
                            return emitError("kDpicCall target import not found", *targetImport);
                        }
                        const DpiImportDecl &decl = importIt->second;
                        const std::string condExpr = operands.empty() ? "true" : valueRef(model, operands[0]);
                        const auto eventExpr = exactEventExpr(graph, model, op);
                        if (!eventExpr)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        stream << "        if ((" << condExpr << ") && (" << *eventExpr << ")) {\n";
                        std::vector<std::string> deferredArgs;
                        std::size_t resultBase = hasReturn ? 1u : 0u;
                        for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                        {
                            const std::string &direction = decl.argsDirection[i];
                            const std::string &formalName = decl.argsName[i];
                            const std::string_view argType =
                                i < decl.argsType.size() ? std::string_view(decl.argsType[i]) : std::string_view("logic");
                            const int64_t argWidth = i < decl.argsWidth.size() ? decl.argsWidth[i] : 1;
                            const bool argSigned = i < decl.argsSigned.size() ? decl.argsSigned[i] : false;
                            if (direction == "input")
                            {
                                const int inputIndex = findNameIndex(inArgName, formalName);
                                if (inputIndex < 0)
                                {
                                    return emitError("kDpicCall missing matching input arg", formalName);
                                }
                                deferredArgs.push_back(dpiValueExpr(graph,
                                                                    model,
                                                                    operands[1 + static_cast<std::size_t>(inputIndex)],
                                                                    argType,
                                                                    argWidth,
                                                                    argSigned));
                            }
                            else if (direction == "output")
                            {
                                const int outputIndex = findNameIndex(outArgName, formalName);
                                if (outputIndex < 0)
                                {
                                    return emitError("kDpicCall missing matching output arg", formalName);
                                }
                                const std::string tempName = "dpi_out_" + std::to_string(i);
                                stream << "            "
                                       << dpiBaseCppType(argType, argWidth, argSigned)
                                       << " " << tempName << "{};\n";
                                deferredArgs.push_back(tempName);
                            }
                            else
                            {
                                return emitError("kDpicCall inout args are not supported in grhsim-cpp emit",
                                                 std::string(op.symbolText()));
                            }
                        }
                        if (hasReturn)
                        {
                            const ValueId returnValue = op.results()[0];
                            stream << "            auto dpi_ret = " << sanitizeIdentifier(decl.symbol)
                                   << "(" << joinStrings(deferredArgs, ", ") << ");\n";
                            stream << "            if (" << valueRef(model, returnValue) << " != dpi_ret) { "
                                   << valueRef(model, returnValue) << " = dpi_ret; supernode_changed = true; }\n";
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                if (direction == "output")
                                {
                                    const int outputIndex = findNameIndex(outArgName, decl.argsName[i]);
                                    if (outputIndex < 0)
                                    {
                                        continue;
                                    }
                                    const std::string tempName = "dpi_out_" + std::to_string(i);
                                    const std::string lhs =
                                        valueRef(model, op.results()[resultBase + static_cast<std::size_t>(outputIndex)]);
                                    stream << "            if (" << lhs << " != " << tempName << ") { " << lhs
                                           << " = " << tempName << "; supernode_changed = true; }\n";
                                }
                            }
                        }
                        else
                        {
                            stream << "            " << sanitizeIdentifier(decl.symbol) << "(" << joinStrings(deferredArgs, ", ") << ");\n";
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                if (direction == "output")
                                {
                                    const int outputIndex = findNameIndex(outArgName, decl.argsName[i]);
                                    if (outputIndex < 0)
                                    {
                                        continue;
                                    }
                                    const std::string tempName = "dpi_out_" + std::to_string(i);
                                    const std::string lhs =
                                        valueRef(model, op.results()[resultBase + static_cast<std::size_t>(outputIndex)]);
                                    stream << "            if (" << lhs << " != " << tempName << ") { " << lhs
                                           << " = " << tempName << "; supernode_changed = true; }\n";
                                }
                            }
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kRegister:
                    case OperationKind::kLatch:
                    case OperationKind::kMemory:
                    case OperationKind::kDpicImport:
                        break;
                    default:
                        return emitError("unsupported op kind in grhsim-cpp emit", std::string(op.symbolText()));
                    }
                }
                stream << "        if (supernode_changed) {\n";
                for (uint32_t succ : (*schedule.dag)[supernodeId])
                {
                    stream << "            grhsim_set_bit(supernode_active_curr_, " << succ << ");\n";
                }
                stream << "        }\n";
                stream << "    }\n";
                stream << "supernode_" << supernodeId << "_end:\n";
            }
            stream << "    return;\n";
            stream << "}\n";
            return std::nullopt;
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
        for (OperationId opId : graph.operations())
        {
            const Operation op = graph.getOperation(opId);
            if (op.kind() != OperationKind::kSystemFunction)
            {
                continue;
            }
            const std::string name = getAttribute<std::string>(op, "name").value_or(std::string());
            if (name == "fopen" && !systemFunctionRunsOnlyOnInitialEval(op))
            {
                reportWarning("$fopen may execute multiple times; use initial without timing control for one-shot open semantics",
                              std::string(op.symbolText()));
            }
        }

        const std::size_t eventPrecomputeMaxOps = parseEventPrecomputeMaxOps(options);
        const std::size_t schedBatchMaxOps = parseScheduleBatchMaxOps(options);
        const std::size_t schedBatchMaxEstimatedLines = parseScheduleBatchMaxEstimatedLines(options);
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
        for (const auto &[value, prevField] : model.prevEventFieldByValue)
        {
            (void)prevField;
            std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> cache;
            std::unordered_map<ValueId, std::size_t, ValueIdHash> costCache;
            std::size_t totalOps = 0;
            auto expr = pureExprForValue(graph, model, value, cache, costCache, totalOps);
            model.eventValuePrecomputable[value] = expr.has_value() && totalOps <= eventPrecomputeMaxOps;
        }
        for (const auto &[signature, index] : model.eventDomainIndex)
        {
            bool precomputable = true;
            for (const auto &term : signature.terms)
            {
                auto it = model.eventValuePrecomputable.find(term.value);
                if (it == model.eventValuePrecomputable.end() || !it->second)
                {
                    precomputable = false;
                    break;
                }
            }
            model.eventDomainPrecomputable[index] = precomputable;
        }

        const std::vector<ScheduleBatch> scheduleBatches =
            buildScheduleBatches(graph, schedule, schedBatchMaxOps, schedBatchMaxEstimatedLines);

        const std::filesystem::path outDir = resolveOutputDir(options);
        const std::string normalizedTop = sanitizeIdentifier(graph.symbol());
        const std::string className = "GrhSIM_" + normalizedTop;
        const std::string prefix = "grhsim_" + normalizedTop;
        const std::filesystem::path headerPath = outDir / (prefix + ".hpp");
        const std::filesystem::path runtimePath = outDir / (prefix + "_runtime.hpp");
        const std::filesystem::path statePath = outDir / (prefix + "_state.cpp");
        const std::filesystem::path evalPath = outDir / (prefix + "_eval.cpp");
        std::vector<std::filesystem::path> schedPaths;
        schedPaths.reserve(scheduleBatches.size());
        for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
        {
            schedPaths.push_back(outDir / (prefix + "_sched_" + std::to_string(batchIndex) + ".cpp"));
        }
        const std::filesystem::path makefilePath = outDir / "Makefile";

        {
            auto stream = openOutputFile(runtimePath);
            if (!stream)
            {
                result.success = false;
                return result;
            }
            *stream << "#pragma once\n\n";
            *stream << "#include <algorithm>\n";
            *stream << "#include <array>\n";
            *stream << "#include <cmath>\n";
            *stream << "#include <cstddef>\n";
            *stream << "#include <cstdlib>\n";
            *stream << "#include <cstdint>\n";
            *stream << "#include <limits>\n";
            *stream << "#include <string>\n";
            *stream << "#include <vector>\n\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "#include <fstream>\n";
                *stream << "#include <iomanip>\n";
                *stream << "#include <initializer_list>\n";
                *stream << "#include <iostream>\n";
                *stream << "#include <sstream>\n";
                *stream << "#include <string_view>\n";
                *stream << "#include <unordered_map>\n";
                *stream << "#include <utility>\n\n";
            }
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
            *stream << "inline std::uint64_t grhsim_cast_u64(std::uint64_t value,\n";
            *stream << "                                   std::size_t srcWidth,\n";
            *stream << "                                   std::size_t destWidth,\n";
            *stream << "                                   bool srcSigned)\n{\n";
            *stream << "    value = grhsim_trunc_u64(value, srcWidth);\n";
            *stream << "    if (srcSigned) {\n";
            *stream << "        value = static_cast<std::uint64_t>(grhsim_sign_extend_i64(value, srcWidth));\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(value, destWidth);\n";
            *stream << "}\n\n";
            *stream << "inline int grhsim_compare_unsigned_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    lhs = grhsim_trunc_u64(lhs, width);\n";
            *stream << "    rhs = grhsim_trunc_u64(rhs, width);\n";
            *stream << "    if (lhs < rhs) return -1;\n";
            *stream << "    if (lhs > rhs) return 1;\n";
            *stream << "    return 0;\n";
            *stream << "}\n\n";
            *stream << "inline int grhsim_compare_signed_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::int64_t lhsSigned = grhsim_sign_extend_i64(lhs, width);\n";
            *stream << "    const std::int64_t rhsSigned = grhsim_sign_extend_i64(rhs, width);\n";
            *stream << "    if (lhsSigned < rhsSigned) return -1;\n";
            *stream << "    if (lhsSigned > rhsSigned) return 1;\n";
            *stream << "    return 0;\n";
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
            *stream << "inline std::uint64_t grhsim_clog2_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    value = grhsim_trunc_u64(value, width);\n";
            *stream << "    if (value <= 1) return 0;\n";
            *stream << "    std::uint64_t result = 0;\n";
            *stream << "    value -= 1;\n";
            *stream << "    while (value != 0) {\n";
            *stream << "        value >>= 1u;\n";
            *stream << "        ++result;\n";
            *stream << "    }\n";
            *stream << "    return result;\n";
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
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_event_posedge_words(const std::array<std::uint64_t, N> &curr,\n";
            *stream << "                                     const std::array<std::uint64_t, N> &prev,\n";
            *stream << "                                     std::size_t width)\n{\n";
            *stream << "    const std::size_t live_words = (width + 63u) / 64u;\n";
            *stream << "    bool currAny = false;\n";
            *stream << "    bool prevAny = false;\n";
            *stream << "    for (std::size_t i = 0; i < live_words; ++i) {\n";
            *stream << "        const std::size_t bits = (i + 1u == live_words) ? (width - i * 64u) : 64u;\n";
            *stream << "        const std::uint64_t mask = bits < 64u ? ((UINT64_C(1) << bits) - 1u) : ~UINT64_C(0);\n";
            *stream << "        currAny = currAny || ((curr[i] & mask) != 0);\n";
            *stream << "        prevAny = prevAny || ((prev[i] & mask) != 0);\n";
            *stream << "    }\n";
            *stream << "    return currAny && !prevAny;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_event_negedge_words(const std::array<std::uint64_t, N> &curr,\n";
            *stream << "                                     const std::array<std::uint64_t, N> &prev,\n";
            *stream << "                                     std::size_t width)\n{\n";
            *stream << "    const std::size_t live_words = (width + 63u) / 64u;\n";
            *stream << "    bool currAny = false;\n";
            *stream << "    bool prevAny = false;\n";
            *stream << "    for (std::size_t i = 0; i < live_words; ++i) {\n";
            *stream << "        const std::size_t bits = (i + 1u == live_words) ? (width - i * 64u) : 64u;\n";
            *stream << "        const std::uint64_t mask = bits < 64u ? ((UINT64_C(1) << bits) - 1u) : ~UINT64_C(0);\n";
            *stream << "        currAny = currAny || ((curr[i] & mask) != 0);\n";
            *stream << "        prevAny = prevAny || ((prev[i] & mask) != 0);\n";
            *stream << "    }\n";
            *stream << "    return !currAny && prevAny;\n";
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
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_apply_masked_words_inplace(std::array<std::uint64_t, N> &dst,\n";
            *stream << "                                             const std::array<std::uint64_t, N> &data,\n";
            *stream << "                                             const std::array<std::uint64_t, N> &mask,\n";
            *stream << "                                             std::size_t width)\n{\n";
            *stream << "    bool changed = false;\n";
            *stream << "    const std::size_t liveWords = (width + 63u) / 64u;\n";
            *stream << "    for (std::size_t i = 0; i < liveWords && i < N; ++i) {\n";
            *stream << "        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;\n";
            *stream << "        const std::uint64_t wordMask = grhsim_trunc_u64(mask[i], wordWidth);\n";
            *stream << "        const std::uint64_t next = (dst[i] & ~wordMask) | (grhsim_trunc_u64(data[i], wordWidth) & wordMask);\n";
            *stream << "        changed = changed || (dst[i] != next);\n";
            *stream << "        dst[i] = next;\n";
            *stream << "    }\n";
            *stream << "    for (std::size_t i = liveWords; i < N; ++i) {\n";
            *stream << "        changed = changed || (dst[i] != 0);\n";
            *stream << "        dst[i] = 0;\n";
            *stream << "    }\n";
            *stream << "    return changed;\n";
            *stream << "}\n\n";
            *stream << R"CPP(
template <std::size_t DestN, typename T>
inline std::array<std::uint64_t, DestN> grhsim_cast_words(T value,
                                                          std::size_t srcWidth,
                                                          std::size_t destWidth,
                                                          bool srcSigned)
{
    std::array<std::uint64_t, DestN> out{};
    if constexpr (DestN > 0) {
        out[0] = grhsim_trunc_u64(static_cast<std::uint64_t>(value), srcWidth);
        if (srcSigned && srcWidth != 0 &&
            ((out[0] >> ((srcWidth >= 64u ? 63u : srcWidth - 1u))) & UINT64_C(1)) != 0) {
            if (srcWidth < 64u) {
                out[0] |= ~grhsim_mask(srcWidth);
            }
            for (std::size_t i = 1; i < DestN; ++i) {
                out[i] = ~UINT64_C(0);
            }
        }
    }
    grhsim_trunc_words(out, destWidth);
    return out;
}

template <std::size_t DestN, std::size_t SrcN>
inline std::array<std::uint64_t, DestN> grhsim_cast_words(const std::array<std::uint64_t, SrcN> &value,
                                                          std::size_t srcWidth,
                                                          std::size_t destWidth,
                                                          bool srcSigned)
{
    std::array<std::uint64_t, DestN> out{};
    const std::size_t srcWords = (srcWidth + 63u) / 64u;
    const std::size_t limit = srcWords < SrcN ? srcWords : SrcN;
    for (std::size_t i = 0; i < limit && i < DestN; ++i) {
        out[i] = value[i];
    }
    if (srcSigned && srcWidth != 0) {
        const std::size_t signWord = (srcWidth - 1u) / 64u;
        const std::size_t signBit = (srcWidth - 1u) & 63u;
        const bool neg = signWord < DestN && ((out[signWord] >> signBit) & UINT64_C(1)) != 0;
        if (neg) {
            if (signWord < DestN && signBit != 63u) {
                out[signWord] |= (~UINT64_C(0)) << (signBit + 1u);
            }
            for (std::size_t i = signWord + 1u; i < DestN; ++i) {
                out[i] = ~UINT64_C(0);
            }
        }
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

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_shl_words(const std::array<std::uint64_t, N> &value,
                                                     const ShiftT &shift,
                                                     std::size_t width);

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_lshr_words(const std::array<std::uint64_t, N> &value,
                                                      const ShiftT &shift,
                                                      std::size_t width);

template <std::size_t N>
inline bool grhsim_try_u128_words(const std::array<std::uint64_t, N> &value,
                                  std::size_t width,
                                  unsigned __int128 &out);

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_from_u128_words(unsigned __int128 value, std::size_t width);

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

template <std::size_t N>
inline void grhsim_clear_range_words(std::array<std::uint64_t, N> &value, std::size_t start, std::size_t width)
{
    if (width == 0 || N == 0) {
        return;
    }
    const std::size_t totalBits = N * 64u;
    if (start >= totalBits) {
        return;
    }
    const std::size_t end = std::min(totalBits, start + width);
    if (end <= start) {
        return;
    }
    const std::size_t startWord = start / 64u;
    const std::size_t endWord = (end - 1u) / 64u;
    const std::size_t startBit = start & 63u;
    const std::size_t endBits = ((end - 1u) & 63u) + 1u;
    if (startWord == endWord) {
        const std::uint64_t lowMask = startBit == 0 ? UINT64_C(0) : grhsim_mask(startBit);
        const std::uint64_t highMask = endBits >= 64 ? UINT64_C(0) : ~grhsim_mask(endBits);
        value[startWord] &= (lowMask | highMask);
        return;
    }
    value[startWord] &= (startBit == 0 ? UINT64_C(0) : grhsim_mask(startBit));
    for (std::size_t i = startWord + 1u; i < endWord && i < N; ++i) {
        value[i] = 0;
    }
    if (endWord < N) {
        value[endWord] &= (endBits >= 64 ? UINT64_C(0) : ~grhsim_mask(endBits));
    }
}

template <std::size_t N>
inline void grhsim_fill_range_words(std::array<std::uint64_t, N> &value, std::size_t start, std::size_t width)
{
    if (width == 0 || N == 0) {
        return;
    }
    const std::size_t totalBits = N * 64u;
    if (start >= totalBits) {
        return;
    }
    const std::size_t end = std::min(totalBits, start + width);
    if (end <= start) {
        return;
    }
    const std::size_t startWord = start / 64u;
    const std::size_t endWord = (end - 1u) / 64u;
    const std::size_t startBit = start & 63u;
    const std::size_t endBits = ((end - 1u) & 63u) + 1u;
    if (startWord == endWord) {
        const std::uint64_t lowMask = startBit == 0 ? ~UINT64_C(0) : ~grhsim_mask(startBit);
        const std::uint64_t highMask = endBits >= 64 ? ~UINT64_C(0) : grhsim_mask(endBits);
        value[startWord] |= (lowMask & highMask);
        return;
    }
    value[startWord] |= (startBit == 0 ? ~UINT64_C(0) : ~grhsim_mask(startBit));
    for (std::size_t i = startWord + 1u; i < endWord && i < N; ++i) {
        value[i] = ~UINT64_C(0);
    }
    if (endWord < N) {
        value[endWord] |= (endBits >= 64 ? ~UINT64_C(0) : grhsim_mask(endBits));
    }
}

template <std::size_t DestN, std::size_t SrcN>
inline void grhsim_insert_words(std::array<std::uint64_t, DestN> &dest,
                                std::size_t destLsb,
                                const std::array<std::uint64_t, SrcN> &src,
                                std::size_t srcWidth)
{
    if (srcWidth == 0 || DestN == 0 || SrcN == 0) {
        return;
    }
    grhsim_clear_range_words(dest, destLsb, srcWidth);
    const std::size_t srcWords = (srcWidth + 63u) / 64u;
    const std::size_t destWord = destLsb / 64u;
    const std::size_t bitShift = destLsb & 63u;
    for (std::size_t i = 0; i < srcWords && i < SrcN; ++i) {
        std::uint64_t word = src[i];
        const std::size_t wordWidth = (i + 1u == srcWords) ? (srcWidth - i * 64u) : 64u;
        word = grhsim_trunc_u64(word, wordWidth);
        if (destWord + i < DestN) {
            dest[destWord + i] |= (bitShift == 0 ? word : (word << bitShift));
        }
        if (bitShift != 0 && destWord + i + 1u < DestN) {
            dest[destWord + i + 1u] |= (word >> (64u - bitShift));
        }
    }
}

template <std::size_t DestN, std::size_t SrcN>
inline std::array<std::uint64_t, DestN> grhsim_slice_words(const std::array<std::uint64_t, SrcN> &src,
                                                           std::size_t start,
                                                           std::size_t width)
{
    std::array<std::uint64_t, DestN> out{};
    if (width == 0 || DestN == 0 || SrcN == 0) {
        return out;
    }
    const std::size_t srcWord = start / 64u;
    const std::size_t bitShift = start & 63u;
    const std::size_t outWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < outWords && i < DestN; ++i) {
        const std::uint64_t low = (srcWord + i < SrcN) ? src[srcWord + i] : UINT64_C(0);
        if (bitShift == 0) {
            out[i] = low;
        }
        else {
            const std::uint64_t high = (srcWord + i + 1u < SrcN) ? src[srcWord + i + 1u] : UINT64_C(0);
            out[i] = (low >> bitShift) | (high << (64u - bitShift));
        }
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

template <typename T>
inline std::size_t grhsim_index_in_range_words(T value)
{
    return static_cast<std::size_t>(static_cast<std::uint64_t>(value));
}

template <std::size_t N>
inline std::size_t grhsim_index_in_range_words(const std::array<std::uint64_t, N> &value)
{
    return N == 0 ? 0 : static_cast<std::size_t>(value[0]);
}

template <typename T>
inline std::size_t grhsim_index_pow2_words(T value, std::size_t mask)
{
    return static_cast<std::size_t>(static_cast<std::uint64_t>(value)) & mask;
}

template <std::size_t N>
inline std::size_t grhsim_index_pow2_words(const std::array<std::uint64_t, N> &value, std::size_t mask)
{
    return (N == 0 ? 0 : static_cast<std::size_t>(value[0])) & mask;
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
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, N * 64u, lhs128) && grhsim_try_u128_words(rhs, N * 64u, rhs128)) {
        if (lhs128 < rhs128) {
            return -1;
        }
        if (lhs128 > rhs128) {
            return 1;
        }
        return 0;
    }
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
inline bool grhsim_try_u64_words(const std::array<std::uint64_t, N> &value,
                                 std::size_t width,
                                 std::uint64_t &out)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    if (liveWords == 0 || N == 0) {
        out = 0;
        return true;
    }
    const std::size_t limit = liveWords < N ? liveWords : N;
    for (std::size_t i = 1; i < limit; ++i) {
        if (value[i] != 0) {
            return false;
        }
    }
    out = grhsim_trunc_u64(value[0], width >= 64 ? 64u : width);
    return true;
}

template <std::size_t N>
inline bool grhsim_try_u128_words(const std::array<std::uint64_t, N> &value,
                                  std::size_t width,
                                  unsigned __int128 &out)
{
    if (width > 128u) {
        return false;
    }
    const std::size_t liveWords = (width + 63u) / 64u;
    if (liveWords == 0 || N == 0) {
        out = 0;
        return true;
    }
    if (liveWords > 2u || liveWords > N) {
        return false;
    }
    const std::uint64_t lo = value[0];
    const std::uint64_t hi = liveWords >= 2u ? value[1] : UINT64_C(0);
    out = static_cast<unsigned __int128>(lo) | (static_cast<unsigned __int128>(hi) << 64u);
    return true;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_from_u128_words(unsigned __int128 value, std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    if constexpr (N > 0) {
        out[0] = static_cast<std::uint64_t>(value);
    }
    if constexpr (N > 1) {
        out[1] = static_cast<std::uint64_t>(value >> 64u);
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline bool grhsim_try_single_bit_words(const std::array<std::uint64_t, N> &value,
                                        std::size_t width,
                                        std::size_t &bitIndex)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    bool found = false;
    bitIndex = 0;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        const std::uint64_t word = grhsim_trunc_u64(value[i], wordWidth);
        if (word == 0) {
            continue;
        }
        if ((word & (word - 1u)) != 0) {
            return false;
        }
        if (found) {
            return false;
        }
        found = true;
        bitIndex = i * 64u + static_cast<std::size_t>(__builtin_ctzll(word));
    }
    return found;
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
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        return grhsim_from_u128_words<N>(lhs128 + rhs128, width);
    }
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
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        return grhsim_from_u128_words<N>(lhs128 - rhs128, width);
    }
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
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        return grhsim_from_u128_words<N>(lhs128 * rhs128, width);
    }

    auto mulByWord = [&](const std::array<std::uint64_t, N> &value,
                         std::uint64_t rhsWord) -> std::array<std::uint64_t, N>
    {
        std::array<std::uint64_t, N> out{};
        unsigned __int128 carry = 0;
        for (std::size_t i = 0; i < N; ++i) {
            const unsigned __int128 accum =
                static_cast<unsigned __int128>(value[i]) * static_cast<unsigned __int128>(rhsWord) + carry;
            out[i] = static_cast<std::uint64_t>(accum);
            carry = accum >> 64u;
        }
        grhsim_trunc_words(out, width);
        return out;
    };

    std::uint64_t rhsWord = 0;
    if (grhsim_try_u64_words(rhs, width, rhsWord)) {
        return mulByWord(lhs, rhsWord);
    }
    std::uint64_t lhsWord = 0;
    if (grhsim_try_u64_words(lhs, width, lhsWord)) {
        return mulByWord(rhs, lhsWord);
    }
    std::size_t rhsBitIndex = 0;
    if (grhsim_try_single_bit_words(rhs, width, rhsBitIndex)) {
        return grhsim_shl_words(lhs, rhsBitIndex, width);
    }
    std::size_t lhsBitIndex = 0;
    if (grhsim_try_single_bit_words(lhs, width, lhsBitIndex)) {
        return grhsim_shl_words(rhs, lhsBitIndex, width);
    }

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
inline std::size_t grhsim_highest_bit_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = liveWords; i-- > 0;) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        const std::uint64_t word = grhsim_trunc_u64(value[i], wordWidth);
        if (word != 0) {
            return i * 64u + (63u - static_cast<std::size_t>(__builtin_clzll(word)));
        }
    }
    return 0;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_udiv_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        if (rhs128 == 0) {
            return {};
        }
        return grhsim_from_u128_words<N>(lhs128 / rhs128, width);
    }

    std::uint64_t rhsWord = 0;
    if (grhsim_try_u64_words(rhs, width, rhsWord)) {
        if (rhsWord == 0) {
            return {};
        }
        std::array<std::uint64_t, N> quotient{};
        unsigned __int128 remainder = 0;
        const std::size_t liveWords = (width + 63u) / 64u;
        for (std::size_t i = liveWords; i-- > 0;) {
            const unsigned __int128 dividend =
                (remainder << 64u) | static_cast<unsigned __int128>(lhs[i]);
            quotient[i] = static_cast<std::uint64_t>(dividend / rhsWord);
            remainder = dividend % rhsWord;
        }
        grhsim_trunc_words(quotient, width);
        return quotient;
    }
    std::size_t rhsBitIndex = 0;
    if (grhsim_try_single_bit_words(rhs, width, rhsBitIndex)) {
        return grhsim_lshr_words(lhs, rhsBitIndex, width);
    }
    if (!grhsim_any_bits_words(rhs, width)) {
        return {};
    }
    std::array<std::uint64_t, N> quotient{};
    std::array<std::uint64_t, N> remainder = lhs;
    const std::size_t rhsHighestBit = grhsim_highest_bit_words(rhs, width);
    while (grhsim_compare_unsigned_words(remainder, rhs) >= 0) {
        const std::size_t remainderHighestBit = grhsim_highest_bit_words(remainder, width);
        std::size_t shift = remainderHighestBit - rhsHighestBit;
        auto shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        if (grhsim_compare_unsigned_words(remainder, shiftedDivisor) < 0) {
            if (shift == 0) {
                break;
            }
            --shift;
            shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        }
        remainder = grhsim_sub_words(remainder, shiftedDivisor, width);
        grhsim_put_bit_words(quotient, shift, true);
    }
    grhsim_trunc_words(quotient, width);
    return quotient;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_umod_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        if (rhs128 == 0) {
            return {};
        }
        return grhsim_from_u128_words<N>(lhs128 % rhs128, width);
    }

    std::uint64_t rhsWord = 0;
    if (grhsim_try_u64_words(rhs, width, rhsWord)) {
        if (rhsWord == 0) {
            return {};
        }
        unsigned __int128 remainder = 0;
        const std::size_t liveWords = (width + 63u) / 64u;
        for (std::size_t i = liveWords; i-- > 0;) {
            const unsigned __int128 dividend =
                (remainder << 64u) | static_cast<unsigned __int128>(lhs[i]);
            remainder = dividend % rhsWord;
        }
        std::array<std::uint64_t, N> out{};
        if constexpr (N > 0) {
            out[0] = static_cast<std::uint64_t>(remainder);
        }
        grhsim_trunc_words(out, width);
        return out;
    }
    std::size_t rhsBitIndex = 0;
    if (grhsim_try_single_bit_words(rhs, width, rhsBitIndex)) {
        std::array<std::uint64_t, N> out{};
        if (rhsBitIndex != 0) {
            out = grhsim_slice_words<N>(lhs, 0, rhsBitIndex);
        }
        grhsim_trunc_words(out, width);
        return out;
    }
    if (!grhsim_any_bits_words(rhs, width)) {
        return {};
    }
    std::array<std::uint64_t, N> remainder = lhs;
    const std::size_t rhsHighestBit = grhsim_highest_bit_words(rhs, width);
    while (grhsim_compare_unsigned_words(remainder, rhs) >= 0) {
        const std::size_t remainderHighestBit = grhsim_highest_bit_words(remainder, width);
        std::size_t shift = remainderHighestBit - rhsHighestBit;
        auto shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        if (grhsim_compare_unsigned_words(remainder, shiftedDivisor) < 0) {
            if (shift == 0) {
                break;
            }
            --shift;
            shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        }
        remainder = grhsim_sub_words(remainder, shiftedDivisor, width);
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
    const std::size_t wordShift = amount / 64u;
    const std::size_t bitShift = amount & 63u;
    for (std::size_t i = N; i-- > 0;) {
        if (i < wordShift) {
            continue;
        }
        const std::uint64_t low = value[i - wordShift];
        out[i] = (bitShift == 0 ? low : (low << bitShift));
        if (bitShift != 0 && i > wordShift) {
            out[i] |= (value[i - wordShift - 1u] >> (64u - bitShift));
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
    const std::size_t wordShift = amount / 64u;
    const std::size_t bitShift = amount & 63u;
    for (std::size_t i = 0; i < N; ++i) {
        if (i + wordShift >= N) {
            break;
        }
        const std::uint64_t high = value[i + wordShift];
        out[i] = (bitShift == 0 ? high : (high >> bitShift));
        if (bitShift != 0 && i + wordShift + 1u < N) {
            out[i] |= (value[i + wordShift + 1u] << (64u - bitShift));
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
            grhsim_fill_range_words(fill, 0, width);
        }
        grhsim_trunc_words(fill, width);
        return fill;
    }
    std::array<std::uint64_t, N> out = grhsim_lshr_words(value, shift, width);
    if (sign) {
        grhsim_fill_range_words(out, width - amount, amount);
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
            if (model.needsSystemTaskRuntime)
            {
                *stream << R"CPP(

enum class grhsim_task_arg_kind {
    Logic,
    Real,
    String,
};

struct grhsim_task_arg {
    grhsim_task_arg_kind kind = grhsim_task_arg_kind::Logic;
    std::size_t width = 0;
    bool isSigned = false;
    bool isWide = false;
    std::uint64_t scalarValue = 0;
    std::vector<std::uint64_t> words;
    double realValue = 0.0;
    std::string stringValue;
};

inline grhsim_task_arg grhsim_make_task_arg(std::uint64_t value, std::size_t width, bool isSigned)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::Logic;
    arg.width = width;
    arg.isSigned = isSigned;
    arg.isWide = false;
    arg.scalarValue = grhsim_trunc_u64(value, width == 0 ? 64u : width);
    return arg;
}

template <std::size_t N>
inline grhsim_task_arg grhsim_make_task_arg(const std::array<std::uint64_t, N> &value,
                                            std::size_t width,
                                            bool isSigned)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::Logic;
    arg.width = width;
    arg.isSigned = isSigned;
    arg.isWide = true;
    arg.words.assign(value.begin(), value.end());
    const std::size_t liveWords = (width + 63u) / 64u;
    if (arg.words.size() < liveWords) {
        arg.words.resize(liveWords, 0);
    }
    if (width != 0 && !arg.words.empty()) {
        const std::size_t tailWidth = width - ((liveWords - 1u) * 64u);
        arg.words[liveWords - 1u] = grhsim_trunc_u64(arg.words[liveWords - 1u], tailWidth);
    }
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(double value)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::Real;
    arg.realValue = value;
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(const std::string &value)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::String;
    arg.stringValue = value;
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(std::string &&value)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::String;
    arg.stringValue = std::move(value);
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(const char *value)
{
    return grhsim_make_task_arg(std::string(value == nullptr ? "" : value));
}

inline void grhsim_task_trunc_words(std::vector<std::uint64_t> &words, std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    if (words.size() < liveWords) {
        words.resize(liveWords, 0);
    }
    if (liveWords == 0) {
        words.clear();
        return;
    }
    words.resize(liveWords);
    const std::size_t tailWidth = width - ((liveWords - 1u) * 64u);
    words[liveWords - 1u] = grhsim_trunc_u64(words[liveWords - 1u], tailWidth);
}

inline bool grhsim_task_words_is_zero(const std::vector<std::uint64_t> &words)
{
    for (std::uint64_t word : words) {
        if (word != 0) {
            return false;
        }
    }
    return true;
}

inline bool grhsim_task_sign_bit(const std::vector<std::uint64_t> &words, std::size_t width)
{
    if (width == 0 || words.empty()) {
        return false;
    }
    const std::size_t wordIndex = (width - 1u) / 64u;
    const std::size_t bitIndex = (width - 1u) & 63u;
    if (wordIndex >= words.size()) {
        return false;
    }
    return ((words[wordIndex] >> bitIndex) & UINT64_C(1)) != 0;
}

inline void grhsim_task_negate_words(std::vector<std::uint64_t> &words, std::size_t width)
{
    for (std::uint64_t &word : words) {
        word = ~word;
    }
    std::uint64_t carry = 1;
    for (std::uint64_t &word : words) {
        const std::uint64_t next = word + carry;
        carry = (next < word) ? 1 : 0;
        word = next;
        if (carry == 0) {
            break;
        }
    }
    grhsim_task_trunc_words(words, width);
}

inline std::uint32_t grhsim_task_divmod_words(std::vector<std::uint64_t> &words, std::uint32_t base)
{
    unsigned __int128 rem = 0;
    for (std::size_t i = words.size(); i-- > 0;) {
        const unsigned __int128 cur = (rem << 64u) | words[i];
        words[i] = static_cast<std::uint64_t>(cur / base);
        rem = cur % base;
    }
    return static_cast<std::uint32_t>(rem);
}

inline std::string grhsim_task_unsigned_words_to_base(std::vector<std::uint64_t> words,
                                                      std::size_t width,
                                                      std::uint32_t base,
                                                      bool uppercase)
{
    grhsim_task_trunc_words(words, width);
    if (words.empty() || grhsim_task_words_is_zero(words)) {
        return "0";
    }
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string out;
    while (!grhsim_task_words_is_zero(words)) {
        const std::uint32_t rem = grhsim_task_divmod_words(words, base);
        out.push_back(digits[rem]);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

inline std::string grhsim_task_logic_to_base(const grhsim_task_arg &arg,
                                             std::uint32_t base,
                                             bool uppercase)
{
    if (!arg.isWide) {
        if (base == 10u) {
            return std::to_string(grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width));
        }
        if (base == 16u) {
            std::ostringstream out;
            out << std::hex << (uppercase ? std::uppercase : std::nouppercase)
                << grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width);
            return out.str();
        }
        if (base == 8u) {
            std::ostringstream out;
            out << std::oct << grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width);
            return out.str();
        }
        if (base == 2u) {
            const std::size_t width = arg.width == 0 ? 1u : arg.width;
            std::string out;
            out.reserve(width);
            const std::uint64_t value = grhsim_trunc_u64(arg.scalarValue, width >= 64u ? 64u : width);
            for (std::size_t i = 0; i < width; ++i) {
                out.push_back(((value >> (width - i - 1u)) & UINT64_C(1)) != 0 ? '1' : '0');
            }
            const std::size_t pos = out.find_first_not_of('0');
            return pos == std::string::npos ? "0" : out.substr(pos);
        }
        return std::to_string(grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width));
    }
    return grhsim_task_unsigned_words_to_base(arg.words, arg.width, base, uppercase);
}

inline std::string grhsim_task_logic_to_decimal(const grhsim_task_arg &arg, bool signedMode)
{
    if (!signedMode) {
        return grhsim_task_logic_to_base(arg, 10u, false);
    }
    if (!arg.isWide) {
        return std::to_string(grhsim_sign_extend_i64(arg.scalarValue, arg.width == 0 ? 64u : arg.width));
    }
    std::vector<std::uint64_t> words = arg.words;
    grhsim_task_trunc_words(words, arg.width);
    const bool negative = grhsim_task_sign_bit(words, arg.width);
    if (negative) {
        grhsim_task_negate_words(words, arg.width);
    }
    std::string out = grhsim_task_unsigned_words_to_base(words, arg.width, 10u, false);
    if (negative && out != "0") {
        out.insert(out.begin(), '-');
    }
    return out;
}

inline std::string grhsim_task_default_arg_text(const grhsim_task_arg &arg)
{
    switch (arg.kind) {
    case grhsim_task_arg_kind::String:
        return arg.stringValue;
    case grhsim_task_arg_kind::Real: {
        std::ostringstream out;
        out << std::defaultfloat << arg.realValue;
        return out.str();
    }
    case grhsim_task_arg_kind::Logic:
    default:
        return grhsim_task_logic_to_decimal(arg, arg.isSigned);
    }
}

inline std::uint64_t grhsim_task_arg_u64(const grhsim_task_arg &arg)
{
    switch (arg.kind) {
    case grhsim_task_arg_kind::Real:
        return static_cast<std::uint64_t>(arg.realValue);
    case grhsim_task_arg_kind::String:
        return arg.stringValue.empty() ? 0 : static_cast<std::uint64_t>(static_cast<unsigned char>(arg.stringValue.front()));
    case grhsim_task_arg_kind::Logic:
    default:
        return arg.isWide ? (arg.words.empty() ? 0 : arg.words.front())
                          : grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width);
    }
}

inline std::string grhsim_task_apply_width(std::string text,
                                           int width,
                                           bool leftJustify,
                                           bool zeroPad)
{
    if (width <= 0 || static_cast<int>(text.size()) >= width) {
        return text;
    }
    const std::size_t padCount = static_cast<std::size_t>(width - static_cast<int>(text.size()));
    const char pad = zeroPad && !leftJustify ? '0' : ' ';
    if (leftJustify) {
        text.append(padCount, pad);
        return text;
    }
    if (pad == '0' && !text.empty() && text.front() == '-') {
        return std::string("-") + std::string(padCount, '0') + text.substr(1);
    }
    return std::string(padCount, pad) + text;
}

inline std::string grhsim_task_format_one(const grhsim_task_arg &arg,
                                          char spec,
                                          int width,
                                          int precision,
                                          bool leftJustify,
                                          bool zeroPad)
{
    std::string text;
    switch (spec) {
    case 'd':
    case 'i':
        if (arg.kind == grhsim_task_arg_kind::Logic) {
            text = grhsim_task_logic_to_decimal(arg, arg.isSigned);
        }
        else if (arg.kind == grhsim_task_arg_kind::Real) {
            text = std::to_string(static_cast<long long>(arg.realValue));
        }
        else {
            text = grhsim_task_default_arg_text(arg);
        }
        break;
    case 'u':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_decimal(arg, false)
                   : std::to_string(grhsim_task_arg_u64(arg));
        break;
    case 'h':
    case 'x':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 16u, false)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'H':
    case 'X':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 16u, true)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'b':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 2u, false)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'o':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 8u, false)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'c':
        text.assign(1, static_cast<char>(grhsim_task_arg_u64(arg) & UINT64_C(0xff)));
        break;
    case 's':
        text = (arg.kind == grhsim_task_arg_kind::String) ? arg.stringValue : grhsim_task_default_arg_text(arg);
        break;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G': {
        const double value = (arg.kind == grhsim_task_arg_kind::Real)
                                 ? arg.realValue
                                 : static_cast<double>(grhsim_task_arg_u64(arg));
        std::ostringstream out;
        if (precision >= 0) {
            out << std::setprecision(precision);
        }
        switch (spec) {
        case 'e':
            out << std::scientific << std::nouppercase;
            break;
        case 'E':
            out << std::scientific << std::uppercase;
            break;
        case 'f':
            out << std::fixed << std::nouppercase;
            break;
        case 'F':
            out << std::fixed << std::uppercase;
            break;
        case 'G':
            out << std::uppercase;
            [[fallthrough]];
        case 'g':
        default:
            break;
        }
        out << value;
        text = out.str();
        break;
    }
    case 't':
        text = std::to_string(grhsim_task_arg_u64(arg));
        break;
    case 'v':
        text = grhsim_task_default_arg_text(arg);
        break;
    default:
        text = grhsim_task_default_arg_text(arg);
        break;
    }
    return grhsim_task_apply_width(std::move(text), width, leftJustify, zeroPad);
}

inline std::string grhsim_format_task_message(const std::vector<grhsim_task_arg> &items)
{
    if (items.empty()) {
        return {};
    }
    if (items.front().kind != grhsim_task_arg_kind::String) {
        std::string out;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0) {
                out.push_back(' ');
            }
            out += grhsim_task_default_arg_text(items[i]);
        }
        return out;
    }

    std::string out;
    const std::string &fmt = items.front().stringValue;
    std::size_t argIndex = 1;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        if (i + 1u >= fmt.size()) {
            out.push_back('%');
            break;
        }
        if (fmt[i + 1u] == '%') {
            out.push_back('%');
            ++i;
            continue;
        }
        ++i;
        bool leftJustify = false;
        bool zeroPad = false;
        while (i < fmt.size()) {
            if (fmt[i] == '-') {
                leftJustify = true;
                ++i;
                continue;
            }
            if (fmt[i] == '0') {
                zeroPad = true;
                ++i;
                continue;
            }
            break;
        }
        int fieldWidth = 0;
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
            fieldWidth = fieldWidth * 10 + static_cast<int>(fmt[i] - '0');
            ++i;
        }
        int precision = -1;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i;
            precision = 0;
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
                precision = precision * 10 + static_cast<int>(fmt[i] - '0');
                ++i;
            }
        }
        while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'L' || fmt[i] == 'z')) {
            ++i;
        }
        if (i >= fmt.size()) {
            break;
        }
        const char spec = fmt[i];
        if (spec == 'm') {
            out += "top";
            continue;
        }
        if (argIndex >= items.size()) {
            out.push_back('%');
            out.push_back(spec);
            continue;
        }
        out += grhsim_task_format_one(items[argIndex++],
                                      spec,
                                      fieldWidth,
                                      precision,
                                      leftJustify,
                                      zeroPad);
    }
    return out;
}

inline std::string grhsim_format_task_message(std::initializer_list<grhsim_task_arg> args)
{
    return grhsim_format_task_message(std::vector<grhsim_task_arg>(args.begin(), args.end()));
}
)CPP";
            }
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
            *stream << "    static constexpr std::size_t kBatchCount = " << scheduleBatches.size() << ";\n";
            *stream << "    static constexpr std::size_t kEventTermCount = " << model.eventTerms.size() << ";\n";
            *stream << "    static constexpr std::size_t kEventDomainCount = " << schedule.eventDomainSinkGroups->size() << ";\n";
            *stream << "    static constexpr std::size_t kEventPrecomputeMaxOps = " << eventPrecomputeMaxOps << ";\n\n";
            *stream << "    " << className << "();\n";
            *stream << "    ~" << className << "();\n";
            *stream << "    void init();\n";
            *stream << "    void set_random_seed(std::uint64_t seed);\n";
            *stream << "    [[nodiscard]] bool had_register_write_conflict() const;\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    [[nodiscard]] bool finish_requested() const;\n";
                *stream << "    [[nodiscard]] bool stop_requested() const;\n";
                *stream << "    [[nodiscard]] bool fatal_requested() const;\n";
                *stream << "    [[nodiscard]] int system_exit_code() const;\n";
                *stream << "    [[nodiscard]] const std::string &dumpfile_path() const;\n";
                *stream << "    [[nodiscard]] bool dumpvars_enabled() const;\n";
            }
            *stream << "    void eval();\n";
            for (const auto &decl : model.publicPortDecls)
            {
                *stream << decl << '\n';
            }
            *stream << "\nprivate:\n";
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                *stream << "    void eval_batch_" << batchIndex << "();\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    struct PendingSystemTaskText {\n";
                *stream << "        bool useHandle = false;\n";
                *stream << "        std::uint64_t handle = 0;\n";
                *stream << "        bool useStderr = false;\n";
                *stream << "        bool newline = true;\n";
                *stream << "        std::string text;\n";
                *stream << "    };\n";
                *stream << "    struct FileHandleEntry {\n";
                *stream << "        std::fstream stream;\n";
                *stream << "        bool canRead = false;\n";
                *stream << "        bool canWrite = false;\n";
                *stream << "        int errorCode = 0;\n";
                *stream << "    };\n";
            }
            *stream << "    void commit_state_updates();\n";
            *stream << "    void refresh_outputs();\n\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    static bool parse_file_open_mode(std::string_view mode,\n";
                *stream << "                                     std::ios::openmode &openMode,\n";
                *stream << "                                     bool &canRead,\n";
                *stream << "                                     bool &canWrite);\n";
                *stream << "    std::uint64_t open_file_handle(const std::string &path, const std::string &mode);\n";
                *stream << "    int file_error_status(std::uint64_t handle) const;\n";
                *stream << "    void close_file_handle(std::uint64_t handle);\n";
                *stream << "    void clear_file_error(std::uint64_t handle);\n";
                *stream << "    void set_file_error(std::uint64_t handle, int errorCode);\n";
                *stream << "    void execute_system_task(std::string_view name, std::initializer_list<grhsim_task_arg> args);\n";
                *stream << "    void flush_deferred_system_task_texts();\n";
                *stream << "    void finalize();\n";
                *stream << "    [[noreturn]] void terminate_host_process(int exitCode);\n";
                *stream << "    std::ostream *resolve_output_stream(std::uint64_t handle, bool useStderr);\n";
                *stream << "    void emit_system_task_text(bool useHandle,\n";
                *stream << "                               std::uint64_t handle,\n";
                *stream << "                               bool useStderr,\n";
                *stream << "                               bool newline,\n";
                *stream << "                               std::string text,\n";
                *stream << "                               bool deferred);\n\n";
            }
            *stream << "    bool first_eval_ = true;\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    bool finalized_ = false;\n";
            }
            *stream << "    bool state_feedback_pending_ = true;\n";
            *stream << "    bool side_effect_feedback_ = false;\n";
            *stream << "    bool register_write_conflict_ = false;\n";
            *stream << "    std::uint64_t random_seed_ = UINT64_C(0);\n";
            *stream << "    std::uint64_t random_state_ = UINT64_C(0);\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    bool finish_requested_ = false;\n";
                *stream << "    bool stop_requested_ = false;\n";
                *stream << "    bool fatal_requested_ = false;\n";
                *stream << "    int system_exit_code_ = 0;\n";
                *stream << "    std::string dumpfile_path_;\n";
                *stream << "    bool dumpvars_enabled_ = false;\n";
                *stream << "    std::uint64_t next_file_handle_ = UINT64_C(3);\n";
                *stream << "    std::unordered_map<std::uint64_t, FileHandleEntry> file_handles_;\n";
                *stream << "    std::vector<PendingSystemTaskText> deferred_system_task_texts_;\n";
            }
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
            *stream << className << "::~" << className << "()\n{\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    finalize();\n";
            }
            *stream << "}\n\n";
            *stream << "void " << className << "::init()\n{\n";
            for (const auto &port : graph.inputPorts())
            {
                *stream << "    " << sanitizeIdentifier(port.name) << " = " << defaultInitExpr(graph, port.value) << ";\n";
                *stream << "    " << model.inputFieldByValue.at(port.value) << " = " << defaultInitExpr(graph, port.value) << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                *stream << "    " << name << ".in = " << defaultInitExpr(graph, port.in) << ";\n";
                *stream << "    " << name << ".out = " << defaultInitExpr(graph, port.out) << ";\n";
                *stream << "    " << name << ".oe = " << defaultInitExpr(graph, port.oe) << ";\n";
                *stream << "    " << model.inputFieldByValue.at(port.in) << " = " << defaultInitExpr(graph, port.in) << ";\n";
            }
            if (!graph.inputPorts().empty() || !graph.inoutPorts().empty())
            {
                *stream << '\n';
            }
            for (const auto &port : graph.outputPorts())
            {
                *stream << "    " << sanitizeIdentifier(port.name) << " = " << defaultInitExpr(graph, port.value) << ";\n";
                *stream << "    out_" << sanitizeIdentifier(port.name) << " = " << defaultInitExpr(graph, port.value) << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                *stream << "    inout_" << name << "_out = " << defaultInitExpr(graph, port.out) << ";\n";
                *stream << "    inout_" << name << "_oe = " << defaultInitExpr(graph, port.oe) << ";\n";
            }
            if (!graph.outputPorts().empty() || !graph.inoutPorts().empty())
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
            for (const auto &port : graph.inoutPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.in) << " = " << model.inputFieldByValue.at(port.in) << ";\n";
            }
            if (!graph.inputPorts().empty() || !graph.inoutPorts().empty())
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
            for (const auto &[value, currField] : model.eventCurrentVarByValue)
            {
                *stream << "    " << currField << " = " << defaultInitExpr(graph, value) << ";\n";
            }
            if (!model.eventCurrentVarByValue.empty())
            {
                *stream << '\n';
            }
            *stream << "    std::fill(supernode_active_curr_.begin(), supernode_active_curr_.end(), 0);\n";
            *stream << "    std::fill(event_term_hit_.begin(), event_term_hit_.end(), false);\n";
            *stream << "    std::fill(event_domain_hit_.begin(), event_domain_hit_.end(), true);\n";
            *stream << "    first_eval_ = true;\n";
            *stream << "    state_feedback_pending_ = true;\n";
            *stream << "    side_effect_feedback_ = false;\n";
            *stream << "    register_write_conflict_ = false;\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    finalized_ = false;\n";
                *stream << "    finish_requested_ = false;\n";
                *stream << "    stop_requested_ = false;\n";
                *stream << "    fatal_requested_ = false;\n";
                *stream << "    system_exit_code_ = 0;\n";
                *stream << "    dumpfile_path_.clear();\n";
                *stream << "    dumpvars_enabled_ = false;\n";
                *stream << "    next_file_handle_ = UINT64_C(3);\n";
                *stream << "    file_handles_.clear();\n";
                *stream << "    deferred_system_task_texts_.clear();\n";
            }
            *stream << "}\n\n";
            *stream << "void " << className << "::set_random_seed(std::uint64_t seed)\n{\n";
            *stream << "    random_seed_ = seed;\n";
            *stream << "}\n\n";
            *stream << "bool " << className << "::had_register_write_conflict() const\n{\n";
            *stream << "    return register_write_conflict_;\n";
            *stream << "}\n\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "bool " << className << "::parse_file_open_mode(std::string_view mode,\n";
                *stream << "                                 std::ios::openmode &openMode,\n";
                *stream << "                                 bool &canRead,\n";
                *stream << "                                 bool &canWrite)\n{\n";
                *stream << "    openMode = std::ios::openmode{};\n";
                *stream << "    canRead = false;\n";
                *stream << "    canWrite = false;\n";
                *stream << "    if (mode.empty()) {\n";
                *stream << "        mode = \"r\";\n";
                *stream << "    }\n";
                *stream << "    char base = '\\0';\n";
                *stream << "    bool binary = false;\n";
                *stream << "    bool plus = false;\n";
                *stream << "    for (char ch : mode) {\n";
                *stream << "        switch (ch) {\n";
                *stream << "        case 'r':\n";
                *stream << "        case 'w':\n";
                *stream << "        case 'a':\n";
                *stream << "            if (base != '\\0') {\n";
                *stream << "                return false;\n";
                *stream << "            }\n";
                *stream << "            base = ch;\n";
                *stream << "            break;\n";
                *stream << "        case 'b':\n";
                *stream << "            binary = true;\n";
                *stream << "            break;\n";
                *stream << "        case '+':\n";
                *stream << "            plus = true;\n";
                *stream << "            break;\n";
                *stream << "        default:\n";
                *stream << "            return false;\n";
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "    switch (base) {\n";
                *stream << "    case 'r':\n";
                *stream << "        openMode = std::ios::in;\n";
                *stream << "        canRead = true;\n";
                *stream << "        break;\n";
                *stream << "    case 'w':\n";
                *stream << "        openMode = std::ios::out | std::ios::trunc;\n";
                *stream << "        canWrite = true;\n";
                *stream << "        break;\n";
                *stream << "    case 'a':\n";
                *stream << "        openMode = std::ios::out | std::ios::app;\n";
                *stream << "        canWrite = true;\n";
                *stream << "        break;\n";
                *stream << "    default:\n";
                *stream << "        return false;\n";
                *stream << "    }\n";
                *stream << "    if (plus) {\n";
                *stream << "        openMode |= std::ios::in | std::ios::out;\n";
                *stream << "        canRead = true;\n";
                *stream << "        canWrite = true;\n";
                *stream << "    }\n";
                *stream << "    if (binary) {\n";
                *stream << "        openMode |= std::ios::binary;\n";
                *stream << "    }\n";
                *stream << "    return true;\n";
                *stream << "}\n\n";
                *stream << "std::uint64_t " << className << "::open_file_handle(const std::string &path,\n";
                *stream << "                                       const std::string &mode)\n{\n";
                *stream << "    std::ios::openmode openMode{};\n";
                *stream << "    bool canRead = false;\n";
                *stream << "    bool canWrite = false;\n";
                *stream << "    if (!parse_file_open_mode(mode, openMode, canRead, canWrite)) {\n";
                *stream << "        return UINT64_C(0);\n";
                *stream << "    }\n";
                *stream << "    FileHandleEntry entry;\n";
                *stream << "    entry.canRead = canRead;\n";
                *stream << "    entry.canWrite = canWrite;\n";
                *stream << "    entry.stream.open(path, openMode);\n";
                *stream << "    if (!entry.stream.is_open()) {\n";
                *stream << "        return UINT64_C(0);\n";
                *stream << "    }\n";
                *stream << "    std::uint64_t handle = next_file_handle_ <= UINT64_C(2) ? UINT64_C(3) : next_file_handle_;\n";
                *stream << "    const std::uint64_t start = handle;\n";
                *stream << "    while (file_handles_.find(handle) != file_handles_.end()) {\n";
                *stream << "        ++handle;\n";
                *stream << "        if (handle <= UINT64_C(2)) {\n";
                *stream << "            handle = UINT64_C(3);\n";
                *stream << "        }\n";
                *stream << "        if (handle == start) {\n";
                *stream << "            return UINT64_C(0);\n";
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "    file_handles_.emplace(handle, std::move(entry));\n";
                *stream << "    next_file_handle_ = handle + UINT64_C(1);\n";
                *stream << "    if (next_file_handle_ <= UINT64_C(2)) {\n";
                *stream << "        next_file_handle_ = UINT64_C(3);\n";
                *stream << "    }\n";
                *stream << "    return handle;\n";
                *stream << "}\n\n";
                *stream << "int " << className << "::file_error_status(std::uint64_t handle) const\n{\n";
                *stream << "    if (handle <= UINT64_C(2)) {\n";
                *stream << "        return 0;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        return it->second.errorCode;\n";
                *stream << "    }\n";
                *stream << "    return 1;\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::close_file_handle(std::uint64_t handle)\n{\n";
                *stream << "    if (handle == UINT64_C(1)) {\n";
                *stream << "        std::cout.flush();\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (handle == UINT64_C(2)) {\n";
                *stream << "        std::cerr.flush();\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        if (it->second.canWrite) {\n";
                *stream << "            it->second.stream.flush();\n";
                *stream << "        }\n";
                *stream << "        it->second.stream.close();\n";
                *stream << "        file_handles_.erase(it);\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::clear_file_error(std::uint64_t handle)\n{\n";
                *stream << "    if (handle <= UINT64_C(2)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        it->second.errorCode = 0;\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::set_file_error(std::uint64_t handle, int errorCode)\n{\n";
                *stream << "    if (handle <= UINT64_C(2)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        it->second.errorCode = errorCode;\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::finish_requested() const\n{\n";
                *stream << "    return finish_requested_;\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::stop_requested() const\n{\n";
                *stream << "    return stop_requested_;\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::fatal_requested() const\n{\n";
                *stream << "    return fatal_requested_;\n";
                *stream << "}\n\n";
                *stream << "int " << className << "::system_exit_code() const\n{\n";
                *stream << "    return system_exit_code_;\n";
                *stream << "}\n\n";
                *stream << "const std::string &" << className << "::dumpfile_path() const\n{\n";
                *stream << "    return dumpfile_path_;\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::dumpvars_enabled() const\n{\n";
                *stream << "    return dumpvars_enabled_;\n";
                *stream << "}\n\n";
                *stream << "std::ostream *" << className << "::resolve_output_stream(std::uint64_t handle,\n";
                *stream << "                                        bool useStderr)\n{\n";
                *stream << "    (void)useStderr;\n";
                *stream << "    if (handle == 1) {\n";
                *stream << "        return &std::cout;\n";
                *stream << "    }\n";
                *stream << "    if (handle == 2) {\n";
                *stream << "        return &std::cerr;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        if (!it->second.canWrite || !it->second.stream.is_open()) {\n";
                *stream << "            it->second.errorCode = 1;\n";
                *stream << "            return nullptr;\n";
                *stream << "        }\n";
                *stream << "        it->second.errorCode = 0;\n";
                *stream << "        return &it->second.stream;\n";
                *stream << "    }\n";
                *stream << "    return nullptr;\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::emit_system_task_text(bool useHandle,\n";
                *stream << "                                std::uint64_t handle,\n";
                *stream << "                                bool useStderr,\n";
                *stream << "                                bool newline,\n";
                *stream << "                                std::string text,\n";
                *stream << "                                bool deferred)\n{\n";
                *stream << "    if (deferred) {\n";
                *stream << "        deferred_system_task_texts_.push_back(PendingSystemTaskText{\n";
                *stream << "            .useHandle = useHandle,\n";
                *stream << "            .handle = handle,\n";
                *stream << "            .useStderr = useStderr,\n";
                *stream << "            .newline = newline,\n";
                *stream << "            .text = std::move(text),\n";
                *stream << "        });\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    std::ostream *out = nullptr;\n";
                *stream << "    if (useHandle) {\n";
                *stream << "        out = resolve_output_stream(handle, useStderr);\n";
                *stream << "    }\n";
                *stream << "    else {\n";
                *stream << "        out = useStderr ? &std::cerr : &std::cout;\n";
                *stream << "    }\n";
                *stream << "    if (out == nullptr) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    (*out) << text;\n";
                *stream << "    if (newline) {\n";
                *stream << "        (*out) << '\\n';\n";
                *stream << "    }\n";
                *stream << "    if (useHandle && handle > UINT64_C(2)) {\n";
                *stream << "        clear_file_error(handle);\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::flush_deferred_system_task_texts()\n{\n";
                *stream << "    for (auto &item : deferred_system_task_texts_) {\n";
                *stream << "        emit_system_task_text(item.useHandle,\n";
                *stream << "                              item.handle,\n";
                *stream << "                              item.useStderr,\n";
                *stream << "                              item.newline,\n";
                *stream << "                              std::move(item.text),\n";
                *stream << "                              false);\n";
                *stream << "    }\n";
                *stream << "    deferred_system_task_texts_.clear();\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::execute_system_task(std::string_view name,\n";
                *stream << "                               std::initializer_list<grhsim_task_arg> args)\n{\n";
                *stream << "    const std::vector<grhsim_task_arg> items(args.begin(), args.end());\n";
                *stream << "    if (name == \"display\" || name == \"write\" || name == \"strobe\") {\n";
                *stream << "        emit_system_task_text(false,\n";
                *stream << "                              0,\n";
                *stream << "                              false,\n";
                *stream << "                              name != \"write\",\n";
                *stream << "                              grhsim_format_task_message(items),\n";
                *stream << "                              name == \"strobe\");\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fdisplay\" || name == \"fwrite\") {\n";
                *stream << "        if (items.empty()) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        const std::uint64_t handle = grhsim_task_arg_u64(items.front());\n";
                *stream << "        const std::vector<grhsim_task_arg> msgArgs(items.begin() + 1, items.end());\n";
                *stream << "        emit_system_task_text(true,\n";
                *stream << "                              handle,\n";
                *stream << "                              false,\n";
                *stream << "                              name == \"fdisplay\",\n";
                *stream << "                              grhsim_format_task_message(msgArgs),\n";
                *stream << "                              false);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fflush\") {\n";
                *stream << "        if (items.empty()) {\n";
                *stream << "            std::cout.flush();\n";
                *stream << "            std::cerr.flush();\n";
                *stream << "            for (auto &[handle, entry] : file_handles_) {\n";
                *stream << "                if (entry.canWrite) {\n";
                *stream << "                    entry.stream.flush();\n";
                *stream << "                    entry.errorCode = 0;\n";
                *stream << "                }\n";
                *stream << "            }\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        const std::uint64_t handle = grhsim_task_arg_u64(items.front());\n";
                *stream << "        if (handle == UINT64_C(1)) {\n";
                *stream << "            std::cout.flush();\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        if (handle == UINT64_C(2)) {\n";
                *stream << "            std::cerr.flush();\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "            if (it->second.canWrite && it->second.stream.is_open()) {\n";
                *stream << "                it->second.stream.flush();\n";
                *stream << "                it->second.errorCode = 0;\n";
                *stream << "            }\n";
                *stream << "            else {\n";
                *stream << "                it->second.errorCode = 1;\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "        else {\n";
                *stream << "            set_file_error(handle, 1);\n";
                *stream << "        }\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fclose\") {\n";
                *stream << "        if (!items.empty()) {\n";
                *stream << "            close_file_handle(grhsim_task_arg_u64(items.front()));\n";
                *stream << "        }\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"dumpfile\") {\n";
                *stream << "        if (!items.empty()) {\n";
                *stream << "            dumpfile_path_ = grhsim_task_default_arg_text(items.front());\n";
                *stream << "        }\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"dumpvars\") {\n";
                *stream << "        dumpvars_enabled_ = true;\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"info\" || name == \"warning\" || name == \"error\") {\n";
                *stream << "        const bool useStderr = (name == \"warning\" || name == \"error\");\n";
                *stream << "        const std::string prefix = \"[\" + std::string(name) + \"] \";\n";
                *stream << "        emit_system_task_text(false,\n";
                *stream << "                              0,\n";
                *stream << "                              useStderr,\n";
                *stream << "                              true,\n";
                *stream << "                              prefix + grhsim_format_task_message(items),\n";
                *stream << "                              false);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fatal\") {\n";
                *stream << "        std::size_t msgStart = 0;\n";
                *stream << "        int exitCode = 1;\n";
                *stream << "        if (!items.empty() && items.front().kind == grhsim_task_arg_kind::Logic) {\n";
                *stream << "            exitCode = static_cast<int>(grhsim_task_arg_u64(items.front()));\n";
                *stream << "            msgStart = 1;\n";
                *stream << "        }\n";
                *stream << "        const std::vector<grhsim_task_arg> msgArgs(items.begin() + msgStart, items.end());\n";
                *stream << "        fatal_requested_ = true;\n";
                *stream << "        system_exit_code_ = exitCode;\n";
                *stream << "        emit_system_task_text(false,\n";
                *stream << "                              0,\n";
                *stream << "                              true,\n";
                *stream << "                              true,\n";
                *stream << "                              std::string(\"[fatal] \") + grhsim_format_task_message(msgArgs),\n";
                *stream << "                              false);\n";
                *stream << "        terminate_host_process(exitCode);\n";
                *stream << "    }\n";
                *stream << "    if (name == \"finish\" || name == \"stop\") {\n";
                *stream << "        std::size_t msgStart = 0;\n";
                *stream << "        int exitCode = 0;\n";
                *stream << "        if (!items.empty() && items.front().kind == grhsim_task_arg_kind::Logic) {\n";
                *stream << "            exitCode = static_cast<int>(grhsim_task_arg_u64(items.front()));\n";
                *stream << "            msgStart = 1;\n";
                *stream << "        }\n";
                *stream << "        if (name == \"finish\") {\n";
                *stream << "            finish_requested_ = true;\n";
                *stream << "        }\n";
                *stream << "        else {\n";
                *stream << "            stop_requested_ = true;\n";
                *stream << "        }\n";
                *stream << "        system_exit_code_ = exitCode;\n";
                *stream << "        const std::vector<grhsim_task_arg> msgArgs(items.begin() + msgStart, items.end());\n";
                *stream << "        if (!msgArgs.empty()) {\n";
                *stream << "            emit_system_task_text(false,\n";
                *stream << "                                  0,\n";
                *stream << "                                  false,\n";
                *stream << "                                  true,\n";
                *stream << "                                  std::string(\"[\") + std::string(name) + \"] \" + grhsim_format_task_message(msgArgs),\n";
                *stream << "                                  false);\n";
                *stream << "        }\n";
                *stream << "        terminate_host_process(exitCode);\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::finalize()\n{\n";
                *stream << "    if (finalized_) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    finalized_ = true;\n";
                for (OperationId opId : graph.operations())
                {
                    const Operation op = graph.getOperation(opId);
                    if (op.kind() != OperationKind::kSystemTask || !systemTaskIsFinal(op))
                    {
                        continue;
                    }
                    const auto operands = op.operands();
                    const std::size_t eventCount = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}).size();
                    const std::size_t argEnd = operands.size() >= eventCount ? operands.size() - eventCount : operands.size();
                    const std::string name = getAttribute<std::string>(op, "name").value_or(std::string("display"));
                    *stream << "    if (" << valueRef(model, operands[0]) << ") {\n";
                    if (argEnd <= 1)
                    {
                        *stream << "        execute_system_task(\"" << escapeCppString(name) << "\", {});\n";
                    }
                    else
                    {
                        *stream << "        execute_system_task(\"" << escapeCppString(name) << "\", {";
                        for (std::size_t i = 1; i < argEnd; ++i)
                        {
                            if (i != 1)
                            {
                                *stream << ", ";
                            }
                            *stream << systemTaskArgExpr(graph, model, operands[i]);
                        }
                        *stream << "});\n";
                    }
                    *stream << "    }\n";
                }
                *stream << "    flush_deferred_system_task_texts();\n";
                *stream << "    for (auto &[handle, entry] : file_handles_) {\n";
                *stream << "        (void)handle;\n";
                *stream << "        if (entry.canWrite) {\n";
                *stream << "            entry.stream.flush();\n";
                *stream << "        }\n";
                *stream << "        entry.stream.close();\n";
                *stream << "    }\n";
                *stream << "    file_handles_.clear();\n";
                *stream << "}\n\n";
                *stream << "[[noreturn]] void " << className << "::terminate_host_process(int exitCode)\n{\n";
                *stream << "    finalize();\n";
                *stream << "    std::cout.flush();\n";
                *stream << "    std::cerr.flush();\n";
                *stream << "    std::exit(exitCode);\n";
                *stream << "}\n\n";
            }
            auto emitCommitWrite = [&](const WriteDecl &write)
            {
                const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                *stream << "    if (" << write.pendingValidField << ") {\n";
                if (state.kind == StateDecl::Kind::Memory)
                {
                    *stream << "        const std::size_t row = " << write.pendingAddrField << ";\n";
                    *stream << "        if (row < " << state.rowCount << ") {\n";
                    if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kConstAllOnes)
                    {
                        if (isWideLogicWidth(state.width))
                        {
                            *stream << "            if (grhsim_assign_words(" << state.fieldName << "[row], "
                                    << write.pendingDataField << ", " << state.width << ")) {\n";
                            *stream << "                any_state_change = true;\n";
                            *stream << "            }\n";
                        }
                        else
                        {
                            *stream << "            const auto next_value = static_cast<" << logicCppType(state.width)
                                    << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << write.pendingDataField
                                    << "), " << state.width << "));\n";
                            *stream << "            if (" << state.fieldName << "[row] != next_value) {\n";
                            *stream << "                " << state.fieldName << "[row] = next_value;\n";
                            *stream << "                any_state_change = true;\n";
                            *stream << "            }\n";
                        }
                    }
                    else
                    {
                        if (isWideLogicWidth(state.width))
                        {
                            *stream << "            if (grhsim_apply_masked_words_inplace(" << state.fieldName << "[row], "
                                    << write.pendingDataField << ", " << write.pendingMaskField << ", " << state.width << ")) {\n";
                            *stream << "                any_state_change = true;\n";
                            *stream << "            }\n";
                        }
                        else
                        {
                            *stream << "            const auto mask = static_cast<" << logicCppType(state.width)
                                    << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << write.pendingMaskField
                                    << "), " << state.width << "));\n";
                            *stream << "            const auto merged = static_cast<" << logicCppType(state.width)
                                    << ">((" << state.fieldName << "[row] & ~mask) | (static_cast<" << logicCppType(state.width)
                                    << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << write.pendingDataField
                                    << "), " << state.width << ")) & mask));\n";
                            *stream << "            if (" << state.fieldName << "[row] != merged) {\n";
                            *stream << "                " << state.fieldName << "[row] = merged;\n";
                            *stream << "                any_state_change = true;\n";
                            *stream << "            }\n";
                        }
                    }
                    *stream << "        }\n";
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
            };
            *stream << "void " << className << "::commit_state_updates()\n{\n";
            *stream << "    bool any_state_change = false;\n";
            *stream << "    register_write_conflict_ = false;\n";
            for (const auto &group : model.registerWriteGroups)
            {
                const std::string groupName = sanitizeIdentifier(group.symbol);
                *stream << "    {\n";
                *stream << "        bool saw_write_" << groupName << " = false;\n";
                for (std::size_t writeIndex : group.writeIndices)
                {
                    const WriteDecl &write = model.writes[writeIndex];
                    *stream << "        if (" << write.pendingValidField << ") {\n";
                    *stream << "            if (saw_write_" << groupName << ") {\n";
                    *stream << "                register_write_conflict_ = true;\n";
                    *stream << "            }\n";
                    *stream << "            saw_write_" << groupName << " = true;\n";
                    *stream << "        }\n";
                }
                for (std::size_t writeIndex : group.writeIndices)
                {
                    emitCommitWrite(model.writes[writeIndex]);
                }
                *stream << "    }\n";
            }
            for (const auto &write : model.writes)
            {
                if (write.kind == StateDecl::Kind::Register)
                {
                    continue;
                }
                emitCommitWrite(write);
            }
            *stream << "    state_feedback_pending_ = state_feedback_pending_ || any_state_change;\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::refresh_outputs()\n{\n";
            for (const auto &port : graph.outputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                *stream << "    out_" << name << " = " << valueRef(model, port.value) << ";\n";
                *stream << "    " << name << " = out_" << name << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                *stream << "    inout_" << name << "_out = " << valueRef(model, port.out) << ";\n";
                *stream << "    inout_" << name << "_oe = " << valueRef(model, port.oe) << ";\n";
                *stream << "    " << name << ".out = inout_" << name << "_out;\n";
                *stream << "    " << name << ".oe = inout_" << name << "_oe;\n";
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
            for (const auto &port : graph.inputPorts())
            {
                *stream << "    " << model.inputFieldByValue.at(port.value) << " = " << sanitizeIdentifier(port.name) << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                *stream << "    " << model.inputFieldByValue.at(port.in) << " = " << sanitizeIdentifier(port.name) << ".in;\n";
            }
            if (!graph.inputPorts().empty() || !graph.inoutPorts().empty())
            {
                *stream << '\n';
            }
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
            for (const auto &port : graph.inoutPorts())
            {
                const std::string current = model.inputFieldByValue.at(port.in);
                const std::string prev = model.prevInputFieldByValue.at(port.in);
                *stream << "    if (" << current << " != " << prev << ") {\n";
                *stream << "        seed_head_eval = true;\n";
                *stream << "    }\n";
            }
            *stream << "    std::fill(supernode_active_curr_.begin(), supernode_active_curr_.end(), 0);\n";
            *stream << "    std::fill(event_term_hit_.begin(), event_term_hit_.end(), false);\n";
            *stream << "    std::fill(event_domain_hit_.begin(), event_domain_hit_.end(), true);\n";
            *stream << "    state_feedback_pending_ = false;\n";
            *stream << "    side_effect_feedback_ = false;\n\n";

            {
                std::vector<ValueId> orderedEventExprValues;
                std::unordered_set<ValueId, ValueIdHash> visiting;
                std::unordered_set<ValueId, ValueIdHash> visited;
                bool eventExprProgramValid = true;
                for (const auto &[value, prevField] : model.prevEventFieldByValue)
                {
                    (void)prevField;
                    if (!eventExprTopoCollect(graph, value, visiting, visited, orderedEventExprValues))
                    {
                        eventExprProgramValid = false;
                        break;
                    }
                }
                std::unordered_map<ValueId, std::string, ValueIdHash> materializedRefs;
                if (eventExprProgramValid)
                {
                    for (ValueId value : orderedEventExprValues)
                    {
                        const bool isEventValue = model.eventCurrentVarByValue.contains(value);
                        if (!isEventValue)
                        {
                            auto expr = eventExprMaterializedBodyForValue(graph, model, value, materializedRefs);
                            if (!expr)
                            {
                                eventExprProgramValid = false;
                                break;
                            }
                            const std::string tempName = "evt_tmp_" + valueDebugName(graph, value) + "_" + std::to_string(value.index);
                            *stream << "    const auto " << tempName << " = " << *expr << ";\n";
                            materializedRefs.emplace(value, tempName);
                            continue;
                        }
                        auto expr = eventExprMaterializedBodyForValue(graph, model, value, materializedRefs);
                        if (!expr)
                        {
                            eventExprProgramValid = false;
                            break;
                        }
                        const std::string &currField = model.eventCurrentVarByValue.at(value);
                        *stream << "    " << currField << " = " << *expr << ";\n";
                        materializedRefs.emplace(value, currField);
                    }
                    if (eventExprProgramValid)
                    {
                        for (const auto &[value, currField] : model.eventCurrentVarByValue)
                        {
                            if (materializedRefs.contains(value))
                            {
                                continue;
                            }
                            auto expr = eventExprMaterializedBodyForValue(graph, model, value, materializedRefs);
                            if (!expr)
                            {
                                eventExprProgramValid = false;
                                break;
                            }
                            *stream << "    " << currField << " = " << *expr << ";\n";
                            materializedRefs.emplace(value, currField);
                        }
                    }
                }
                if (!eventExprProgramValid)
                {
                    for (const auto &[value, prevField] : model.prevEventFieldByValue)
                    {
                        (void)prevField;
                        std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> cache;
                        std::unordered_map<ValueId, std::size_t, ValueIdHash> costCache;
                        std::size_t totalOps = 0;
                        auto expr = pureExprForValue(graph, model, value, cache, costCache, totalOps);
                        if (expr)
                        {
                            *stream << "    " << model.eventCurrentVarByValue.at(value) << " = " << *expr << ";\n";
                        }
                        else
                        {
                            *stream << "    " << model.eventCurrentVarByValue.at(value) << " = " << valueRef(model, value) << ";\n";
                        }
                    }
                }
            }
            if (!model.prevEventFieldByValue.empty())
            {
                *stream << '\n';
            }

            for (std::size_t termIndex = 0; termIndex < model.eventTerms.size(); ++termIndex)
            {
                const auto &term = model.eventTerms[termIndex];
                const std::string currVar = model.eventCurrentVarByValue.at(term.value);
                const bool precomputable =
                    model.eventValuePrecomputable.contains(term.value) && model.eventValuePrecomputable.at(term.value);
                if (precomputable)
                {
                    if (isWideLogicValue(graph, term.value))
                    {
                        if (term.edge == "posedge")
                        {
                            *stream << "    event_term_hit_[" << termIndex << "] = grhsim_event_posedge_words(" << currVar << ", "
                                    << model.prevEventFieldByValue.at(term.value) << ", "
                                    << graph.valueWidth(term.value) << ");\n";
                        }
                        else if (term.edge == "negedge")
                        {
                            *stream << "    event_term_hit_[" << termIndex << "] = grhsim_event_negedge_words(" << currVar << ", "
                                    << model.prevEventFieldByValue.at(term.value) << ", "
                                    << graph.valueWidth(term.value) << ");\n";
                        }
                        else
                        {
                            *stream << "    event_term_hit_[" << termIndex << "] = (grhsim_compare_unsigned_words(" << currVar
                                    << ", " << model.prevEventFieldByValue.at(term.value) << ") != 0);\n";
                        }
                    }
                    else if (term.edge == "posedge")
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
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                *stream << "    eval_batch_" << batchIndex << "();\n";
            }
            *stream << "    commit_state_updates();\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    flush_deferred_system_task_texts();\n";
            }
            *stream << "    if (state_feedback_pending_) {\n";
            *stream << "        std::fill(supernode_active_curr_.begin(), supernode_active_curr_.end(), 0);\n";
            *stream << "        std::fill(event_term_hit_.begin(), event_term_hit_.end(), false);\n";
            *stream << "        std::fill(event_domain_hit_.begin(), event_domain_hit_.end(), false);\n";
            for (const auto &group : *schedule.eventDomainSinkGroups)
            {
                const std::size_t domainIndex = model.eventDomainIndex.at(group.signature);
                if (group.signature.empty())
                {
                    *stream << "        event_domain_hit_[" << domainIndex << "] = true;\n";
                }
            }
            for (uint32_t supernodeId : sourceSupernodeList)
            {
                *stream << "        grhsim_set_bit(supernode_active_curr_, " << supernodeId << ");\n";
            }
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                *stream << "        eval_batch_" << batchIndex << "();\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "        flush_deferred_system_task_texts();\n";
            }
            *stream << "        state_feedback_pending_ = false;\n";
            *stream << "    }\n";
            *stream << "    refresh_outputs();\n\n";
            for (const auto &port : graph.inputPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.value) << " = " << model.inputFieldByValue.at(port.value) << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.in) << " = " << model.inputFieldByValue.at(port.in) << ";\n";
            }
            for (const auto &[value, prevField] : model.prevEventFieldByValue)
            {
                *stream << "    " << prevField << " = " << model.eventCurrentVarByValue.at(value) << ";\n";
            }
            *stream << "    first_eval_ = false;\n";
            *stream << "}\n";
        }

        const std::size_t emitParallelism =
            std::min(parseEmitParallelism(options), scheduleBatches.empty() ? std::size_t{1} : scheduleBatches.size());
        if (emitParallelism <= 1 || scheduleBatches.size() <= 1)
        {
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                if (auto error = emitSchedBatchFile(schedPaths[batchIndex],
                                                    headerPath,
                                                    className,
                                                    graph,
                                                    model,
                                                    schedule,
                                                    scheduleBatches[batchIndex]))
                {
                    reportError(*error, graph.symbol());
                    result.success = false;
                    return result;
                }
            }
        }
        else
        {
            struct PendingSchedEmit
            {
                std::size_t batchIndex = 0;
                std::future<std::optional<std::string>> future;
            };

            std::vector<PendingSchedEmit> pending;
            pending.reserve(emitParallelism);

            auto launchBatch = [&](std::size_t batchIndex)
            {
                pending.push_back(PendingSchedEmit{
                    .batchIndex = batchIndex,
                    .future = std::async(std::launch::async,
                                         [&, batchIndex]() -> std::optional<std::string>
                                         {
                                             return emitSchedBatchFile(schedPaths[batchIndex],
                                                                       headerPath,
                                                                       className,
                                                                       graph,
                                                                       model,
                                                                       schedule,
                                                                       scheduleBatches[batchIndex]);
                                         }),
                });
            };

            std::size_t launched = 0;
            while (launched < scheduleBatches.size() || !pending.empty())
            {
                while (launched < scheduleBatches.size() && pending.size() < emitParallelism)
                {
                    launchBatch(launched++);
                }

                PendingSchedEmit task = std::move(pending.front());
                pending.erase(pending.begin());
                if (auto error = task.future.get())
                {
                    reportError(*error, graph.symbol());
                    result.success = false;
                    return result;
                }
            }
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
            *stream << "SRCS := " << statePath.filename().string() << ' ' << evalPath.filename().string();
            for (const auto &schedPath : schedPaths)
            {
                *stream << ' ' << schedPath.filename().string();
            }
            *stream << "\n";
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
        };
        for (const auto &schedPath : schedPaths)
        {
            result.artifacts.push_back(schedPath.string());
        }
        result.artifacts.push_back(makefilePath.string());
        return result;
    }

} // namespace wolvrix::lib::emit
