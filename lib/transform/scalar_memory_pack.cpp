#include "transform/scalar_memory_pack.hpp"

#include "core/grh.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolvrix::lib::transform
{

    namespace
    {
        using namespace wolvrix::lib::grh;

        template <typename T>
        std::optional<T> getAttr(const Operation &op, std::string_view key)
        {
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *value = std::get_if<T>(&*attr))
            {
                return *value;
            }
            return std::nullopt;
        }

        void setRejectReason(std::string *reasonOut, std::string reason)
        {
            if (reasonOut && reasonOut->empty())
            {
                *reasonOut = std::move(reason);
            }
        }

        const std::optional<std::string> &traceFilter()
        {
            static const std::optional<std::string> filter = []() -> std::optional<std::string> {
                const char *value = std::getenv("WOLVRIX_SCALAR_MEMORY_PACK_TRACE_SUBSTR");
                if (!value || *value == '\0')
                {
                    return std::nullopt;
                }
                return std::string(value);
            }();
            return filter;
        }

        bool matchesTraceFilter(std::string_view text)
        {
            const auto &filter = traceFilter();
            return filter && text.find(*filter) != std::string_view::npos;
        }

        bool matchesTraceFilter(const Graph &graph, OperationId opId)
        {
            if (!traceFilter())
            {
                return false;
            }
            const Operation op = graph.getOperation(opId);
            if (matchesTraceFilter(op.symbolText()))
            {
                return true;
            }
            for (ValueId result : op.results())
            {
                if (matchesTraceFilter(graph.getValue(result).symbolText()))
                {
                    return true;
                }
            }
            return false;
        }

        std::string jsonEscape(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            for (const unsigned char ch : text)
            {
                switch (ch)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (ch < 0x20)
                    {
                        constexpr char digits[] = "0123456789abcdef";
                        out += "\\u00";
                        out.push_back(digits[(ch >> 4) & 0xf]);
                        out.push_back(digits[ch & 0xf]);
                    }
                    else
                    {
                        out.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }
            return out;
        }

        template <typename T>
        void hashCombine(std::size_t &seed, const T &value)
        {
            seed ^= std::hash<T>{}(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        }

        std::size_t hashValueId(ValueId value) noexcept
        {
            std::size_t seed = static_cast<std::size_t>(value.index);
            hashCombine(seed, value.generation);
            hashCombine(seed, value.graph.index);
            hashCombine(seed, value.graph.generation);
            return seed;
        }

        struct ClusterKey
        {
            std::string prefix;
            std::string suffix;
            int32_t width = 0;
            bool isSigned = false;

            bool operator==(const ClusterKey &) const = default;
        };

        struct ClusterKeyHash
        {
            std::size_t operator()(const ClusterKey &key) const noexcept
            {
                std::size_t seed = std::hash<std::string>{}(key.prefix);
                hashCombine(seed, key.suffix);
                hashCombine(seed, key.width);
                hashCombine(seed, key.isSigned);
                return seed;
            }
        };

        struct ClusterMember
        {
            std::string symbol;
            OperationId regOp;
            OperationId readOp;
            ValueId readResult;
            int64_t index = -1;
        };

        struct ReadRewritePlan
        {
            struct Consumer
            {
                OperationId eraseOp;
                ValueId oldResult;
                ValueId addr;
                int64_t constIndex = -1;
                bool reverseAddr = false;
                int64_t rowOffset = 0;
            };

            OperationId concatOp;
            ValueId concatResult;
            std::vector<OperationId> concatOps;
            std::vector<Consumer> consumers;
        };

        struct FillGroupKey
        {
            std::vector<ValueId> condBranches;
            struct DataRef
            {
                ValueId value;
                int32_t width = 0;
                bool isSigned = false;
                std::string literal;
                int64_t sliceStart = -1;

                bool operator==(const DataRef &) const = default;
            } data;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;
            int64_t writeOrder = 0;

            bool operator==(const FillGroupKey &) const = default;
        };

        struct FillGroupKeyHash
        {
            std::size_t operator()(const FillGroupKey &key) const noexcept
            {
                std::size_t seed = 0;
                for (ValueId value : key.condBranches)
                {
                    hashCombine(seed, hashValueId(value));
                }
                if (key.data.value.valid())
                {
                    hashCombine(seed, hashValueId(key.data.value));
                }
                else
                {
                    hashCombine(seed, key.data.width);
                    hashCombine(seed, key.data.isSigned);
                    hashCombine(seed, key.data.literal);
                    hashCombine(seed, key.data.sliceStart);
                }
                hashCombine(seed, hashValueId(key.mask));
                for (ValueId value : key.eventValues)
                {
                    hashCombine(seed, hashValueId(value));
                }
                for (const std::string &edge : key.eventEdges)
                {
                    hashCombine(seed, edge);
                }
                hashCombine(seed, key.writeOrder);
                return seed;
            }
        };

        struct FillFamilyKey
        {
            std::vector<ValueId> condBranches;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;

            bool operator==(const FillFamilyKey &) const = default;
        };

        struct FillFamilyKeyHash
        {
            std::size_t operator()(const FillFamilyKey &key) const noexcept
            {
                std::size_t seed = 0;
                for (ValueId value : key.condBranches)
                {
                    hashCombine(seed, hashValueId(value));
                }
                hashCombine(seed, hashValueId(key.mask));
                for (ValueId value : key.eventValues)
                {
                    hashCombine(seed, hashValueId(value));
                }
                for (const std::string &edge : key.eventEdges)
                {
                    hashCombine(seed, edge);
                }
                return seed;
            }
        };

        struct PointWriteKey
        {
            std::vector<ValueId> baseTerms;
            ValueId addr;
            FillGroupKey::DataRef data;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;
            int64_t writeOrder = 0;

            bool operator==(const PointWriteKey &) const = default;
        };

        struct PointWriteKeyHash
        {
            std::size_t operator()(const PointWriteKey &key) const noexcept
            {
                std::size_t seed = 0;
                for (ValueId value : key.baseTerms)
                {
                    hashCombine(seed, hashValueId(value));
                }
                hashCombine(seed, hashValueId(key.addr));
                if (key.data.value.valid())
                {
                    hashCombine(seed, hashValueId(key.data.value));
                }
                else
                {
                    hashCombine(seed, key.data.width);
                    hashCombine(seed, key.data.isSigned);
                    hashCombine(seed, key.data.literal);
                    hashCombine(seed, key.data.sliceStart);
                }
                hashCombine(seed, hashValueId(key.mask));
                for (ValueId value : key.eventValues)
                {
                    hashCombine(seed, hashValueId(value));
                }
                for (const std::string &edge : key.eventEdges)
                {
                    hashCombine(seed, edge);
                }
                hashCombine(seed, key.writeOrder);
                return seed;
            }
        };

        struct PointWriteInfo
        {
            PointWriteKey key;
            int64_t constIndex = -1;
            OperationId opId;
            std::string memberSymbol;
        };

        struct ParsedFillArm
        {
            std::vector<ValueId> condBranches;
            FillGroupKey::DataRef data;
            int64_t writeOrder = 0;
        };

        struct ParsedPointArm
        {
            std::vector<ValueId> baseTerms;
            ValueId addr;
            int64_t constIndex = -1;
            FillGroupKey::DataRef data;
            int64_t writeOrder = 0;
        };

        struct ParsedWritePattern
        {
            OperationId opId;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;
            std::vector<ParsedFillArm> fills;
            std::vector<ParsedPointArm> points;
        };

        constexpr int64_t kWriteOrderStride = 1000000;

        void assignWriteOrders(ParsedWritePattern &pattern)
        {
            int64_t localOrder = 0;

            for (auto it = pattern.fills.rbegin(); it != pattern.fills.rend(); ++it)
            {
                it->writeOrder = localOrder;
                ++localOrder;
            }
            for (auto it = pattern.points.rbegin(); it != pattern.points.rend(); ++it)
            {
                it->writeOrder = localOrder;
                ++localOrder;
            }
        }

        struct CandidateCluster
        {
            ClusterKey key;
            int64_t indexBase = 0;
            std::vector<ClusterMember> members;
            ReadRewritePlan readPlan;
            std::vector<std::pair<FillGroupKey, std::vector<OperationId>>> fillGroups;
            std::vector<std::pair<PointWriteKey, std::vector<OperationId>>> pointWriteGroups;
        };

        struct OrderedWriteGroupRef
        {
            enum class Kind
            {
                Point,
                Fill,
            };

            Kind kind = Kind::Point;
            std::size_t index = 0;
            int64_t order = 0;
        };

        struct FlatConcatInfo
        {
            OperationId concatOpId;
            std::vector<ValueId> flattenedLeaves;
            std::vector<OperationId> concatOps;
            std::vector<ClusterMember> members;
        };

        struct NamedSegment
        {
            ClusterKey key;
            int64_t indexBase = 0;
            std::vector<ClusterMember> members;
        };

        struct RejectAggregate
        {
            std::size_t rejectCount = 0;
            std::size_t memberCount = 0;
            std::vector<std::string> examples;
        };

        struct IndexedSymbolSplit
        {
            std::string prefix;
            std::string suffix;
            int64_t index = -1;
        };

        struct ReadWriteRefs
        {
            std::unordered_map<std::string, std::vector<OperationId>> readPortsBySymbol;
            std::unordered_map<std::string, std::vector<OperationId>> writePortsBySymbol;
        };

        class ScalarMemoryPackReport
        {
        public:
            ScalarMemoryPackReport()
            {
                const char *path = std::getenv("WOLVRIX_SCALAR_MEMORY_PACK_REPORT_JSON");
                if (!path || *path == '\0')
                {
                    return;
                }

                path_ = path;
                const std::filesystem::path reportPath(path_);
                if (reportPath.has_parent_path())
                {
                    std::filesystem::create_directories(reportPath.parent_path());
                }
                out_.open(reportPath, std::ios::out | std::ios::trunc);
                if (!out_)
                {
                    throw std::runtime_error("failed to open scalar-memory-pack report: " + path_);
                }
                out_ << "{\n  \"records\": [\n";
            }

            ~ScalarMemoryPackReport()
            {
                if (out_.is_open())
                {
                    finish(0, 0, 0, 0);
                }
            }

            bool enabled() const noexcept { return out_.is_open(); }

            void addCluster(const Graph &graph,
                            const CandidateCluster &candidate,
                            std::string_view memoryName,
                            std::size_t clusterIndex)
            {
                if (!enabled())
                {
                    return;
                }

                for (const ClusterMember &member : candidate.members)
                {
                    if (!firstRecord_)
                    {
                        out_ << ",\n";
                    }
                    firstRecord_ = false;
                    ++recordCount_;
                    out_ << "    {"
                         << "\"cluster_index\":" << clusterIndex
                         << ",\"graph\":\"" << jsonEscape(graph.symbol()) << "\""
                         << ",\"memory\":\"" << jsonEscape(memoryName) << "\""
                         << ",\"register\":\"" << jsonEscape(member.symbol) << "\""
                         << ",\"row\":" << member.index
                         << ",\"row_count\":" << candidate.members.size()
                         << ",\"width\":" << candidate.key.width
                         << ",\"is_signed\":" << (candidate.key.isSigned ? "true" : "false")
                         << "}";
                }
            }

            void finish(std::size_t graphCount,
                        std::size_t candidateClusterCount,
                        std::size_t candidateMemberCount,
                        std::size_t rewrittenClusterCount)
            {
                if (!enabled())
                {
                    return;
                }
                out_ << "\n  ],\n"
                     << "  \"summary\": {"
                     << "\"graphs\":" << graphCount
                     << ",\"candidate_clusters\":" << candidateClusterCount
                     << ",\"candidate_members\":" << candidateMemberCount
                     << ",\"rewritten_clusters\":" << rewrittenClusterCount
                     << ",\"rewritten_members\":" << recordCount_
                     << "}\n"
                     << "}\n";
                out_.close();
            }

        private:
            std::string path_;
            std::ofstream out_;
            bool firstRecord_ = true;
            std::size_t recordCount_ = 0;
        };

        ReadWriteRefs collectReadWriteRefs(const Graph &graph)
        {
            ReadWriteRefs refs;
            for (const auto opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.kind() == OperationKind::kRegisterReadPort)
                {
                    if (auto reg = getAttr<std::string>(op, "regSymbol"))
                    {
                        refs.readPortsBySymbol[*reg].push_back(opId);
                    }
                }
                else if (op.kind() == OperationKind::kRegisterWritePort)
                {
                    if (auto reg = getAttr<std::string>(op, "regSymbol"))
                    {
                        refs.writePortsBySymbol[*reg].push_back(opId);
                    }
                }
            }
            return refs;
        }

        bool isGeneratedRegisterSymbol(const Graph &graph, OperationId regOpId)
        {
            const std::string_view symbol = graph.getOperation(regOpId).symbolText();
            return symbol.empty() || symbol.front() == '_';
        }

        bool isIndexDelimiter(char ch) noexcept
        {
            return ch == '_' || ch == '$';
        }

        std::vector<IndexedSymbolSplit> enumerateIndexedSymbolSplits(std::string_view symbol)
        {
            std::vector<IndexedSymbolSplit> splits;
            for (std::size_t pos = 0; pos < symbol.size(); ++pos)
            {
                if (!std::isdigit(static_cast<unsigned char>(symbol[pos])))
                {
                    continue;
                }
                const std::size_t start = pos;
                while (pos < symbol.size() &&
                       std::isdigit(static_cast<unsigned char>(symbol[pos])))
                {
                    ++pos;
                }
                const std::size_t end = pos;
                if (start == 0 || !isIndexDelimiter(symbol[start - 1]))
                {
                    continue;
                }
                if (end < symbol.size() && !isIndexDelimiter(symbol[end]))
                {
                    continue;
                }
                try
                {
                    splits.push_back(IndexedSymbolSplit{
                        .prefix = std::string(symbol.substr(0, start)),
                        .suffix = std::string(symbol.substr(end)),
                        .index = std::stoll(std::string(symbol.substr(start, end - start)))});
                }
                catch (const std::exception &)
                {
                }
            }
            return splits;
        }

        bool isAllOnesLiteral(std::string_view literal, int32_t width)
        {
            if (literal.empty() || width <= 0)
            {
                return false;
            }
            std::string cleaned;
            cleaned.reserve(literal.size());
            for (char ch : literal)
            {
                if (ch != '_' && !std::isspace(static_cast<unsigned char>(ch)))
                {
                    cleaned.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }
            }
            const std::size_t tick = cleaned.find('\'');
            if (tick == std::string::npos || tick + 2 >= cleaned.size())
            {
                return false;
            }
            const int parsedWidth = std::stoi(cleaned.substr(0, tick));
            if (parsedWidth != width)
            {
                return false;
            }
            const char base = cleaned[tick + 1];
            const std::string digits = cleaned.substr(tick + 2);
            if (base == 'b')
            {
                return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == '1'; });
            }
            if (base == 'h')
            {
                return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == 'f'; });
            }
            return false;
        }

        bool isAllOnesMaskValue(const Graph &graph, ValueId value, int32_t width)
        {
            if (!value.valid())
            {
                return false;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kConstant)
            {
                return false;
            }
            const auto literal = getAttr<std::string>(defOp, "constValue");
            return literal.has_value() && isAllOnesLiteral(*literal, width);
        }

        bool isAllZeroLiteral(std::string_view literal, int32_t width)
        {
            if (literal.empty() || width <= 0)
            {
                return false;
            }
            std::string cleaned;
            cleaned.reserve(literal.size());
            for (char ch : literal)
            {
                if (ch != '_' && !std::isspace(static_cast<unsigned char>(ch)))
                {
                    cleaned.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }
            }
            const std::size_t tick = cleaned.find('\'');
            if (tick == std::string::npos || tick + 2 >= cleaned.size())
            {
                return false;
            }
            const int parsedWidth = std::stoi(cleaned.substr(0, tick));
            if (parsedWidth != width)
            {
                return false;
            }
            const char base = cleaned[tick + 1];
            const std::string digits = cleaned.substr(tick + 2);
            if (base == 'b')
            {
                return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == '0'; });
            }
            if (base == 'h')
            {
                return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == '0'; });
            }
            if (base == 'd')
            {
                return digits == "0";
            }
            return false;
        }

        FillGroupKey::DataRef makeDataRefFromValue(const Graph &graph, ValueId value)
        {
            FillGroupKey::DataRef data;
            data.width = value.valid() ? graph.valueWidth(value) : 0;
            data.isSigned = value.valid() ? graph.valueSigned(value) : false;
            if (!value.valid())
            {
                return data;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (defOpId.valid())
            {
                const Operation defOp = graph.getOperation(defOpId);
                if (defOp.kind() == OperationKind::kConstant)
                {
                    if (const auto literal = getAttr<std::string>(defOp, "constValue"))
                    {
                        data.literal = *literal;
                        return data;
                    }
                }
            }
            data.value = value;
            return data;
        }

        FillGroupKey::DataRef makeDataRefFromSlice(const Graph &graph,
                                                   ValueId value,
                                                   int32_t sliceWidth,
                                                   int64_t sliceStart,
                                                   bool isSigned)
        {
            FillGroupKey::DataRef data;
            data.value = value;
            data.width = sliceWidth;
            data.isSigned = isSigned;
            data.sliceStart = sliceStart;
            return data;
        }

        std::optional<ValueId> projectStaticSliceLeaf(const Graph &graph,
                                                      ValueId value,
                                                      int64_t sliceStart,
                                                      int32_t sliceWidth)
        {
            if (!value.valid() || sliceStart < 0 || sliceWidth <= 0)
            {
                return std::nullopt;
            }
            if (sliceStart == 0 && graph.valueWidth(value) == sliceWidth)
            {
                return value;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
            {
                return projectStaticSliceLeaf(graph, defOp.operands()[0], sliceStart, sliceWidth);
            }
            if (defOp.kind() == OperationKind::kSliceStatic && defOp.operands().size() == 1)
            {
                const int64_t innerStart = getAttr<int64_t>(defOp, "sliceStart").value_or(-1);
                if (innerStart >= 0)
                {
                    return projectStaticSliceLeaf(graph, defOp.operands()[0], innerStart + sliceStart, sliceWidth);
                }
            }
            if (defOp.kind() == OperationKind::kConcat)
            {
                int64_t offset = 0;
                for (auto it = defOp.operands().rbegin(); it != defOp.operands().rend(); ++it)
                {
                    const ValueId operand = *it;
                    const int32_t operandWidth = graph.valueWidth(operand);
                    if (sliceStart >= offset && sliceStart + sliceWidth <= offset + operandWidth)
                    {
                        return projectStaticSliceLeaf(graph, operand, sliceStart - offset, sliceWidth);
                    }
                    offset += operandWidth;
                }
            }
            return std::nullopt;
        }

        FillGroupKey::DataRef makeProjectedDataRef(const Graph &graph,
                                                   ValueId value,
                                                   int64_t sliceStart,
                                                   int32_t sliceWidth,
                                                   bool isSigned)
        {
            if (auto projected = projectStaticSliceLeaf(graph, value, sliceStart, sliceWidth))
            {
                return makeDataRefFromValue(graph, *projected);
            }
            const OperationId defOpId = graph.valueDef(value);
            if (defOpId.valid())
            {
                const Operation defOp = graph.getOperation(defOpId);
                if (defOp.kind() == OperationKind::kConstant)
                {
                    if (const auto literal = getAttr<std::string>(defOp, "constValue"))
                    {
                        if (isAllZeroLiteral(*literal, graph.valueWidth(value)))
                        {
                            return FillGroupKey::DataRef{
                                .value = ValueId::invalid(),
                                .width = sliceWidth,
                                .isSigned = isSigned,
                                .literal = std::to_string(sliceWidth) + "'d0",
                                .sliceStart = -1};
                        }
                    }
                }
            }
            return makeDataRefFromSlice(graph, value, sliceWidth, sliceStart, isSigned);
        }

        bool matchesCondMaskExpr(const Graph &graph,
                                 ValueId value,
                                 int32_t width,
                                 ValueId cond,
                                 bool expectTrue)
        {
            if (value == cond)
            {
                return expectTrue && width == 1;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
            {
                return matchesCondMaskExpr(graph, defOp.operands()[0], width, cond, expectTrue);
            }
            if ((defOp.kind() == OperationKind::kLogicNot || defOp.kind() == OperationKind::kNot) &&
                defOp.operands().size() == 1)
            {
                return matchesCondMaskExpr(graph, defOp.operands()[0], width, cond, !expectTrue);
            }
            if (defOp.kind() == OperationKind::kReplicate &&
                defOp.operands().size() == 1 &&
                getAttr<int64_t>(defOp, "rep").value_or(0) * graph.valueWidth(defOp.operands()[0]) == width)
            {
                return matchesCondMaskExpr(graph, defOp.operands()[0], graph.valueWidth(defOp.operands()[0]), cond, expectTrue);
            }
            return false;
        }

        bool matchesProjectedCondMask(const Graph &graph,
                                      ValueId maskValue,
                                      int64_t sliceStart,
                                      int32_t sliceWidth,
                                      ValueId cond,
                                      bool expectTrue)
        {
            const OperationId defOpId = graph.valueDef(maskValue);
            if (defOpId.valid())
            {
                const Operation defOp = graph.getOperation(defOpId);
                if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
                {
                    return matchesProjectedCondMask(graph,
                                                    defOp.operands()[0],
                                                    sliceStart,
                                                    sliceWidth,
                                                    cond,
                                                    expectTrue);
                }
                if ((defOp.kind() == OperationKind::kLogicNot || defOp.kind() == OperationKind::kNot) &&
                    defOp.operands().size() == 1)
                {
                    return matchesProjectedCondMask(graph,
                                                    defOp.operands()[0],
                                                    sliceStart,
                                                    sliceWidth,
                                                    cond,
                                                    !expectTrue);
                }
            }
            auto projected = projectStaticSliceLeaf(graph, maskValue, sliceStart, sliceWidth);
            if (!projected)
            {
                return false;
            }
            return matchesCondMaskExpr(graph, *projected, sliceWidth, cond, expectTrue);
        }

        bool parseConstIntLiteral(std::string_view literal, int64_t &valueOut)
        {
            if (literal.empty())
            {
                return false;
            }
            std::string cleaned;
            cleaned.reserve(literal.size());
            for (char ch : literal)
            {
                if (ch == '_' || std::isspace(static_cast<unsigned char>(ch)))
                {
                    continue;
                }
                cleaned.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            if (cleaned.empty())
            {
                return false;
            }
            if (cleaned.front() == '"' || cleaned.front() == '$')
            {
                return false;
            }
            int base = 10;
            std::string digits;
            const std::size_t tick = cleaned.find('\'');
            if (tick != std::string::npos)
            {
                if (tick + 2 >= cleaned.size())
                {
                    return false;
                }
                switch (cleaned[tick + 1])
                {
                case 'b':
                    base = 2;
                    break;
                case 'o':
                    base = 8;
                    break;
                case 'd':
                    base = 10;
                    break;
                case 'h':
                    base = 16;
                    break;
                default:
                    return false;
                }
                digits = cleaned.substr(tick + 2);
            }
            else
            {
                digits = cleaned;
            }
            if (digits.empty())
            {
                return false;
            }
            if (digits.find_first_of("xz?") != std::string::npos)
            {
                return false;
            }
            try
            {
                valueOut = std::stoll(digits, nullptr, base);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }

        std::optional<int64_t> constantIntValue(const Graph &graph, ValueId value)
        {
            if (!value.valid())
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
            const auto literal = getAttr<std::string>(defOp, "constValue");
            if (!literal)
            {
                return std::nullopt;
            }
            int64_t valueOut = 0;
            if (!parseConstIntLiteral(*literal, valueOut))
            {
                return std::nullopt;
            }
            return valueOut;
        }

        bool isAndKind(OperationKind kind) noexcept
        {
            return kind == OperationKind::kAnd || kind == OperationKind::kLogicAnd;
        }

        bool isOrKind(OperationKind kind) noexcept
        {
            return kind == OperationKind::kOr || kind == OperationKind::kLogicOr;
        }

        bool valueIdLess(ValueId lhs, ValueId rhs) noexcept
        {
            if (lhs.graph.index != rhs.graph.index)
            {
                return lhs.graph.index < rhs.graph.index;
            }
            if (lhs.graph.generation != rhs.graph.generation)
            {
                return lhs.graph.generation < rhs.graph.generation;
            }
            if (lhs.index != rhs.index)
            {
                return lhs.index < rhs.index;
            }
            return lhs.generation < rhs.generation;
        }

        void canonicalizeValues(std::vector<ValueId> &values)
        {
            std::sort(values.begin(), values.end(), valueIdLess);
        }

        void flattenBoolTree(const Graph &graph,
                             ValueId value,
                             std::vector<ValueId> &leaves,
                             bool (*matches)(OperationKind) noexcept)
        {
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                leaves.push_back(value);
                return;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (!matches(defOp.kind()) || defOp.operands().size() != 2)
            {
                leaves.push_back(value);
                return;
            }
            flattenBoolTree(graph, defOp.operands()[0], leaves, matches);
            flattenBoolTree(graph, defOp.operands()[1], leaves, matches);
        }

        void flattenAndTerms(const Graph &graph, ValueId value, std::vector<ValueId> &terms)
        {
            flattenBoolTree(graph, value, terms, isAndKind);
        }

        void flattenOrTerms(const Graph &graph, ValueId value, std::vector<ValueId> &terms)
        {
            flattenBoolTree(graph, value, terms, isOrKind);
        }

        std::optional<std::pair<ValueId, int64_t>> parseEqAddrConst(const Graph &graph, ValueId cond)
        {
            if (!cond.valid())
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(cond);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kEq || defOp.operands().size() != 2)
            {
                return std::nullopt;
            }
            if (auto rhsConst = constantIntValue(graph, defOp.operands()[1]))
            {
                return std::pair<ValueId, int64_t>{defOp.operands()[0], *rhsConst};
            }
            if (auto lhsConst = constantIntValue(graph, defOp.operands()[0]))
            {
                return std::pair<ValueId, int64_t>{defOp.operands()[1], *lhsConst};
            }
            return std::nullopt;
        }

        std::optional<std::pair<ValueId, int64_t>> parseAllOnesAddrMatch(const Graph &graph,
                                                                          ValueId cond,
                                                                          std::size_t rowCount)
        {
            if (!cond.valid() || rowCount == 0)
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(cond);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kReduceAnd || defOp.operands().size() != 1)
            {
                return std::nullopt;
            }
            const ValueId addr = defOp.operands()[0];
            const int32_t width = graph.valueWidth(addr);
            if (width <= 1 || width >= 63)
            {
                return std::nullopt;
            }
            if ((uint64_t{1} << width) != rowCount)
            {
                return std::nullopt;
            }
            return std::pair<ValueId, int64_t>{addr, static_cast<int64_t>(rowCount - 1u)};
        }

        std::optional<std::pair<ValueId, int64_t>> parseAllZeroAddrMatch(const Graph &graph, ValueId cond)
        {
            if (!cond.valid())
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(cond);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if ((defOp.kind() != OperationKind::kNot && defOp.kind() != OperationKind::kLogicNot) ||
                defOp.operands().size() != 1)
            {
                return std::nullopt;
            }
            const OperationId innerDefId = graph.valueDef(defOp.operands()[0]);
            if (!innerDefId.valid())
            {
                return std::nullopt;
            }
            const Operation innerDef = graph.getOperation(innerDefId);
            if (innerDef.kind() != OperationKind::kReduceOr || innerDef.operands().size() != 1)
            {
                return std::nullopt;
            }
            return std::pair<ValueId, int64_t>{innerDef.operands()[0], 0};
        }

        struct PointCondInfo
        {
            std::vector<ValueId> baseTerms;
            ValueId addr;
            int64_t constIndex = -1;

            bool operator==(const PointCondInfo &) const = default;
        };

        std::optional<PointCondInfo> parsePointCondTerms(const Graph &graph,
                                                         std::vector<ValueId> terms,
                                                         std::size_t clusterRows)
        {
            std::optional<std::pair<ValueId, int64_t>> eqInfo;
            std::vector<ValueId> baseTerms;
            for (ValueId term : terms)
            {
                if (auto eq = parseEqAddrConst(graph, term))
                {
                    if (eqInfo)
                    {
                        return std::nullopt;
                    }
                    eqInfo = *eq;
                }
                else if (auto allOnes = parseAllOnesAddrMatch(graph, term, clusterRows))
                {
                    if (eqInfo)
                    {
                        return std::nullopt;
                    }
                    eqInfo = *allOnes;
                }
                else if (auto allZero = parseAllZeroAddrMatch(graph, term))
                {
                    if (eqInfo)
                    {
                        return std::nullopt;
                    }
                    eqInfo = *allZero;
                }
                else
                {
                    baseTerms.push_back(term);
                }
            }
            if (!eqInfo)
            {
                return std::nullopt;
            }
            canonicalizeValues(baseTerms);
            return PointCondInfo{
                .baseTerms = std::move(baseTerms),
                .addr = eqInfo->first,
                .constIndex = eqInfo->second};
        }

        bool flattenPointOrBundleTerms(const Graph &graph, ValueId value, std::vector<ValueId> &leaves)
        {
            if (!value.valid())
            {
                return false;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
            {
                return flattenPointOrBundleTerms(graph, defOp.operands()[0], leaves);
            }
            if (isOrKind(defOp.kind()) && defOp.operands().size() == 2)
            {
                return flattenPointOrBundleTerms(graph, defOp.operands()[0], leaves) &&
                       flattenPointOrBundleTerms(graph, defOp.operands()[1], leaves);
            }
            if (defOp.kind() != OperationKind::kReduceOr || defOp.operands().size() != 1)
            {
                return false;
            }
            const OperationId concatDefId = graph.valueDef(defOp.operands()[0]);
            if (!concatDefId.valid())
            {
                return false;
            }
            const Operation concatDef = graph.getOperation(concatDefId);
            if (concatDef.kind() != OperationKind::kConcat || concatDef.operands().empty())
            {
                return false;
            }
            for (ValueId operand : concatDef.operands())
            {
                if (graph.valueWidth(operand) != 1)
                {
                    return false;
                }
                leaves.push_back(operand);
            }
            return true;
        }

        struct PointCondBundle
        {
            std::vector<ValueId> globalBaseTerms;
            std::vector<std::pair<ValueId, PointCondInfo>> leaves;
        };

        std::optional<PointCondBundle> parsePointCondBundle(const Graph &graph,
                                                            ValueId cond,
                                                            std::size_t clusterRows)
        {
            std::vector<ValueId> terms;
            flattenAndTerms(graph, cond, terms);

            std::vector<ValueId> globalBaseTerms;
            std::vector<ValueId> bundleLeaves;
            bool sawBundle = false;
            for (ValueId term : terms)
            {
                std::vector<ValueId> currentLeaves;
                if (flattenPointOrBundleTerms(graph, term, currentLeaves) && currentLeaves.size() > 1)
                {
                    if (sawBundle)
                    {
                        return std::nullopt;
                    }
                    bundleLeaves = std::move(currentLeaves);
                    sawBundle = true;
                }
                else
                {
                    globalBaseTerms.push_back(term);
                }
            }
            if (!sawBundle)
            {
                return std::nullopt;
            }

            PointCondBundle bundle;
            bundle.globalBaseTerms = std::move(globalBaseTerms);
            canonicalizeValues(bundle.globalBaseTerms);
            for (ValueId leaf : bundleLeaves)
            {
                std::vector<ValueId> leafExpandedTerms;
                flattenAndTerms(graph, leaf, leafExpandedTerms);
                auto parsed = parsePointCondTerms(graph, std::move(leafExpandedTerms), clusterRows);
                if (!parsed)
                {
                    return std::nullopt;
                }
                bundle.leaves.push_back({leaf, std::move(*parsed)});
            }
            return bundle;
        }

        std::optional<PointCondInfo> parsePointCond(const Graph &graph,
                                                    ValueId cond,
                                                    std::size_t clusterRows)
        {
            if (!cond.valid())
            {
                return std::nullopt;
            }

            std::vector<ValueId> terms;
            flattenAndTerms(graph, cond, terms);
            return parsePointCondTerms(graph, std::move(terms), clusterRows);
        }

        bool sameOperands(std::span<const ValueId> lhs, std::span<const ValueId> rhs) noexcept
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < lhs.size(); ++i)
            {
                if (lhs[i] != rhs[i])
                {
                    return false;
                }
            }
            return true;
        }

        void replacePortBinding(Graph &graph, ValueId from, ValueId to)
        {
            for (const auto &port : graph.outputPorts())
            {
                if (port.value == from)
                {
                    graph.bindOutputPort(port.name, to);
                }
            }
            for (const auto &port : graph.inputPorts())
            {
                if (port.value == from)
                {
                    graph.bindInputPort(port.name, to);
                }
            }
            for (const auto &port : graph.inoutPorts())
            {
                if (port.in == from || port.out == from || port.oe == from)
                {
                    graph.bindInoutPort(port.name,
                                        port.in == from ? to : port.in,
                                        port.out == from ? to : port.out,
                                        port.oe == from ? to : port.oe);
                }
            }
        }

        bool isValueBoundToPort(const Graph &graph, ValueId value)
        {
            for (const auto &port : graph.outputPorts())
            {
                if (port.value == value)
                {
                    return true;
                }
            }
            for (const auto &port : graph.inputPorts())
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

        bool isContainedInWriteCone(const Graph &graph,
                                    OperationId opId,
                                    const std::unordered_set<OperationId, OperationIdHash> &writeOps,
                                    std::unordered_map<OperationId, bool, OperationIdHash> &memo,
                                    std::unordered_set<OperationId, OperationIdHash> &visiting)
        {
            if (writeOps.contains(opId))
            {
                return true;
            }
            if (const auto it = memo.find(opId); it != memo.end())
            {
                return it->second;
            }
            if (!visiting.insert(opId).second)
            {
                memo[opId] = false;
                return false;
            }

            bool ok = true;
            const Operation op = graph.getOperation(opId);
            if (op.results().empty())
            {
                ok = false;
            }
            else
            {
                for (ValueId result : op.results())
                {
                    if (isValueBoundToPort(graph, result))
                    {
                        ok = false;
                        break;
                    }
                    const Value value = graph.getValue(result);
                    for (const ValueUser &user : value.users())
                    {
                        if (!isContainedInWriteCone(graph, user.operation, writeOps, memo, visiting))
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok)
                    {
                        break;
                    }
                }
            }

            visiting.erase(opId);
            memo[opId] = ok;
            return ok;
        }

        void pruneDeadValueDef(Graph &graph, ValueId value)
        {
            OperationId defOpId = OperationId::invalid();
            try
            {
                defOpId = graph.valueDef(value);
            }
            catch (const std::runtime_error &)
            {
                return;
            }
            if (!defOpId.valid())
            {
                return;
            }

            std::optional<Operation> op;
            try
            {
                op = graph.getOperation(defOpId);
            }
            catch (const std::runtime_error &)
            {
                return;
            }

            for (ValueId result : op->results())
            {
                if (isValueBoundToPort(graph, result))
                {
                    return;
                }
                if (!graph.getValue(result).users().empty())
                {
                    return;
                }
            }

            const std::vector<ValueId> operands(op->operands().begin(), op->operands().end());
            graph.eraseOp(defOpId);
            for (ValueId operand : operands)
            {
                pruneDeadValueDef(graph, operand);
            }
        }

        bool operationExists(const Graph &graph, OperationId opId)
        {
            if (!opId.valid())
            {
                return false;
            }
            try
            {
                (void)graph.getOperation(opId);
                return true;
            }
            catch (const std::runtime_error &)
            {
                return false;
            }
        }

        bool valueDependsOnAnyRoot(const Graph &graph,
                                   ValueId value,
                                   const std::unordered_set<ValueId, ValueIdHash> &roots,
                                   std::unordered_set<ValueId, ValueIdHash> &visiting)
        {
            if (!value.valid())
            {
                return false;
            }
            if (roots.contains(value))
            {
                return true;
            }
            if (!visiting.insert(value).second)
            {
                return false;
            }

            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                visiting.erase(value);
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
            for (ValueId operand : defOp.operands())
            {
                if (valueDependsOnAnyRoot(graph, operand, roots, visiting))
                {
                    visiting.erase(value);
                    return true;
                }
            }

            visiting.erase(value);
            return false;
        }

        bool dataRefDependsOnAnyRoot(const Graph &graph,
                                     const FillGroupKey::DataRef &data,
                                     const std::unordered_set<ValueId, ValueIdHash> &roots)
        {
            std::unordered_set<ValueId, ValueIdHash> visiting;
            return data.value.valid() && valueDependsOnAnyRoot(graph, data.value, roots, visiting);
        }

        std::unordered_set<ValueId, ValueIdHash> collectPackedReadRoots(const Graph &graph,
                                                                        const CandidateCluster &candidate)
        {
            std::unordered_set<ValueId, ValueIdHash> roots;
            for (const ClusterMember &member : candidate.members)
            {
                if (member.readResult.valid())
                {
                    roots.insert(member.readResult);
                }
            }
            if (candidate.readPlan.concatResult.valid())
            {
                roots.insert(candidate.readPlan.concatResult);
            }
            for (OperationId concatOpId : candidate.readPlan.concatOps)
            {
                const Operation concatOp = graph.getOperation(concatOpId);
                for (ValueId result : concatOp.results())
                {
                    roots.insert(result);
                }
            }
            return roots;
        }

        bool pointWriteDataDependsOnPackedReads(const Graph &graph, const CandidateCluster &candidate)
        {
            const auto roots = collectPackedReadRoots(graph, candidate);
            for (const auto &[key, opIds] : candidate.pointWriteGroups)
            {
                (void)opIds;
                if (dataRefDependsOnAnyRoot(graph, key.data, roots))
                {
                    return true;
                }
            }
            return false;
        }

        std::string makeUniqueMemoryName(const Graph &graph, const ClusterKey &key)
        {
            std::string base = key.prefix;
            if (!base.empty() && base.back() == '_')
            {
                base.pop_back();
            }
            if (base.empty())
            {
                base = "scalar_cluster";
            }
            base += "$packed_mem";
            if (!key.suffix.empty() && key.suffix != "_value")
            {
                base += key.suffix;
            }
            std::string candidate = base;
            std::size_t suffix = 0;
            while (graph.findOperation(candidate).valid() || graph.findValue(candidate).valid())
            {
                ++suffix;
                candidate = base + "$" + std::to_string(suffix);
            }
            return candidate;
        }

        ValueId createConstantValue(Graph &graph,
                                    int32_t width,
                                    bool isSigned,
                                    std::string literal,
                                    std::string_view note)
        {
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId valueId = graph.createValue(valueSym,
                                                      width > 0 ? width : 1,
                                                      isSigned,
                                                      ValueType::Logic);
            const OperationId opId = graph.createOperation(OperationKind::kConstant, opSym);
            graph.addResult(opId, valueId);
            graph.setAttr(opId, "constValue", std::move(literal));
            const SrcLoc srcLoc = makeTransformSrcLoc("scalar-memory-pack", note);
            graph.setValueSrcLoc(valueId, srcLoc);
            graph.setOpSrcLoc(opId, srcLoc);
            return valueId;
        }

        ValueId createBinaryOp(Graph &graph,
                               OperationKind kind,
                               ValueId lhs,
                               ValueId rhs,
                               int32_t outWidth,
                               bool outSigned,
                               std::string_view note)
        {
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId out = graph.createValue(valueSym,
                                                  outWidth > 0 ? outWidth : 1,
                                                  outSigned,
                                                  ValueType::Logic);
            const OperationId op = graph.createOperation(kind, opSym);
            graph.addOperand(op, lhs);
            graph.addOperand(op, rhs);
            graph.addResult(op, out);
            const SrcLoc srcLoc = makeTransformSrcLoc("scalar-memory-pack", note);
            graph.setValueSrcLoc(out, srcLoc);
            graph.setOpSrcLoc(op, srcLoc);
            return out;
        }

        ValueId createLogicTree(Graph &graph,
                                std::span<const ValueId> values,
                                OperationKind kind,
                                std::string_view note,
                                bool emptyAsTrue)
        {
            if (values.empty())
            {
                return emptyAsTrue
                           ? createConstantValue(graph, 1, false, "1'b1", std::string(note) + "_true")
                           : createConstantValue(graph, 1, false, "1'b0", std::string(note) + "_false");
            }
            ValueId acc = values.front();
            for (std::size_t i = 1; i < values.size(); ++i)
            {
                acc = createBinaryOp(graph, kind, acc, values[i], 1, false, note);
            }
            return acc;
        }

        bool isConcatLikeTree(const Graph &graph,
                              ValueId value,
                              const std::unordered_set<OperationId, OperationIdHash> &treeOps)
        {
            if (!value.valid())
            {
                return false;
            }
            const OperationId defOpId = graph.valueDef(value);
            return defOpId.valid() && treeOps.contains(defOpId);
        }

        void flattenConcatLeaves(const Graph &graph,
                                 OperationId opId,
                                 std::vector<ValueId> &leaves,
                                 std::vector<OperationId> &concatOps)
        {
            concatOps.push_back(opId);
            const Operation op = graph.getOperation(opId);
            for (ValueId operand : op.operands())
            {
                const OperationId defOpId = graph.valueDef(operand);
                if (defOpId.valid() && graph.getOperation(defOpId).kind() == OperationKind::kConcat)
                {
                    const Value concatValue = graph.getValue(operand);
                    if (concatValue.users().size() == 1 && concatValue.users().front().operation == opId)
                    {
                        flattenConcatLeaves(graph, defOpId, leaves, concatOps);
                        continue;
                    }
                }
                leaves.push_back(operand);
            }
        }

        std::string makeMemberSequenceKey(std::span<const ClusterMember> members)
        {
            std::size_t totalSize = 0;
            for (const ClusterMember &member : members)
            {
                totalSize += member.symbol.size() + 1;
            }

            std::string key;
            key.reserve(totalSize);
            for (const ClusterMember &member : members)
            {
                key.append(member.symbol);
                key.push_back('\n');
            }
            return key;
        }

        std::vector<NamedSegment> collectNamedSegments(const Graph &graph, const ReadWriteRefs &refs)
        {
            std::unordered_map<ClusterKey, std::vector<ClusterMember>, ClusterKeyHash> families;
            for (const OperationId opId : graph.operations())
            {
                const Operation regOp = graph.getOperation(opId);
                if (regOp.kind() != OperationKind::kRegister || isGeneratedRegisterSymbol(graph, opId))
                {
                    continue;
                }

                const std::string symbol(regOp.symbolText());
                if (symbol.empty() ||
                    !refs.readPortsBySymbol.contains(symbol) ||
                    !refs.writePortsBySymbol.contains(symbol))
                {
                    continue;
                }

                const int32_t width = static_cast<int32_t>(getAttr<int64_t>(regOp, "width").value_or(0));
                const bool isSigned = getAttr<bool>(regOp, "isSigned").value_or(false);
                if (width <= 0)
                {
                    continue;
                }

                std::unordered_set<ClusterKey, ClusterKeyHash> seenKeys;
                for (const IndexedSymbolSplit &split : enumerateIndexedSymbolSplits(symbol))
                {
                    ClusterKey key{
                        .prefix = split.prefix,
                        .suffix = split.suffix,
                        .width = width,
                        .isSigned = isSigned};
                    if (!seenKeys.insert(key).second)
                    {
                        continue;
                    }
                    families[key].push_back(ClusterMember{
                        .symbol = symbol,
                        .regOp = opId,
                        .readOp = OperationId::invalid(),
                        .readResult = ValueId::invalid(),
                        .index = split.index});
                }
            }

            std::vector<NamedSegment> segments;
            for (auto &[key, members] : families)
            {
                if (members.size() < 4)
                {
                    continue;
                }

                std::sort(members.begin(),
                          members.end(),
                          [](const ClusterMember &lhs, const ClusterMember &rhs) {
                              if (lhs.index != rhs.index)
                              {
                                  return lhs.index < rhs.index;
                              }
                              return lhs.symbol < rhs.symbol;
                          });

                auto flushSegment = [&](std::size_t start, std::size_t end) {
                    if (end <= start || end - start < 4)
                    {
                        return;
                    }
                    NamedSegment segment;
                    segment.key = key;
                    segment.indexBase = members[start].index;
                    segment.members.reserve(end - start);
                    for (std::size_t i = start; i < end; ++i)
                    {
                        segment.members.push_back(members[i]);
                    }
                    segments.push_back(std::move(segment));
                };

                std::size_t start = 0;
                for (std::size_t i = 1; i < members.size(); ++i)
                {
                    const bool duplicateIndex = members[i].index == members[i - 1].index;
                    const bool nonContiguous = members[i].index != members[i - 1].index + 1;
                    if (duplicateIndex || nonContiguous)
                    {
                        flushSegment(start, i);
                        start = i;
                    }
                }
                flushSegment(start, members.size());
            }
            return segments;
        }

        void addRejectExample(std::vector<std::string> &examples, std::string example, std::size_t cap = 5)
        {
            if (std::find(examples.begin(), examples.end(), example) != examples.end())
            {
                return;
            }
            if (examples.size() < cap)
            {
                examples.push_back(std::move(example));
            }
        }

        std::string formatRejectExamples(const std::vector<std::string> &examples)
        {
            if (examples.empty())
            {
                return "[]";
            }
            std::string out = "[";
            for (std::size_t i = 0; i < examples.size(); ++i)
            {
                if (i != 0)
                {
                    out += ", ";
                }
                out += examples[i];
            }
            out += "]";
            return out;
        }

        bool replaceReasonPrefix(std::string &reason, std::string_view prefix, std::string replacement)
        {
            if (!reason.starts_with(prefix))
            {
                return false;
            }
            const std::size_t payloadStart = reason.find(": ", prefix.size());
            if (payloadStart == std::string::npos)
            {
                reason = std::move(replacement);
                return true;
            }
            reason = std::move(replacement) + reason.substr(payloadStart);
            return true;
        }

        std::string normalizeRejectReason(std::string reason)
        {
            if (replaceReasonPrefix(reason,
                                    "failed to parse register write pattern for ",
                                    "failed to parse register write pattern"))
            {
                return reason;
            }
            if (replaceReasonPrefix(reason,
                                    "missing register write port for member ",
                                    "missing register write port for member"))
            {
                return reason;
            }
            return reason;
        }

        struct SliceDynamicIndexInfo
        {
            ValueId addr;
            bool reverseAddr = false;
            int64_t rowOffset = 0;
        };

        std::optional<SliceDynamicIndexInfo> parseLinearElementIndex(const Graph &graph,
                                                                     ValueId value,
                                                                     std::size_t rowCount)
        {
            if (!value.valid())
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return SliceDynamicIndexInfo{.addr = value, .reverseAddr = false, .rowOffset = 0};
            }
            const Operation defOp = graph.getOperation(defOpId);
            if ((defOp.kind() == OperationKind::kAdd || defOp.kind() == OperationKind::kSub) &&
                defOp.operands().size() == 2)
            {
                if (auto rhsConst = constantIntValue(graph, defOp.operands()[1]))
                {
                    if (auto base = parseLinearElementIndex(graph, defOp.operands()[0], rowCount))
                    {
                        base->rowOffset += defOp.kind() == OperationKind::kAdd ? *rhsConst : -*rhsConst;
                        return base;
                    }
                }
                if (defOp.kind() == OperationKind::kAdd)
                {
                    if (auto lhsConst = constantIntValue(graph, defOp.operands()[0]))
                    {
                        if (auto base = parseLinearElementIndex(graph, defOp.operands()[1], rowCount))
                        {
                            base->rowOffset += *lhsConst;
                            return base;
                        }
                    }
                }
            }
            if (defOp.kind() == OperationKind::kSub && defOp.operands().size() == 2)
            {
                if (auto lhsConst = constantIntValue(graph, defOp.operands()[0]))
                {
                    if (*lhsConst == static_cast<int64_t>(rowCount - 1u))
                    {
                        if (auto base = parseLinearElementIndex(graph, defOp.operands()[1], rowCount))
                        {
                            base->reverseAddr = !base->reverseAddr;
                            base->rowOffset = -base->rowOffset;
                            return base;
                        }
                    }
                }
            }
            return SliceDynamicIndexInfo{.addr = value, .reverseAddr = false, .rowOffset = 0};
        }

        int64_t requiredBitIndexWidth(int32_t elementWidth, std::size_t rowCount)
        {
            if (elementWidth <= 0 || rowCount <= 1)
            {
                return 1;
            }
            uint64_t maxStart = static_cast<uint64_t>(elementWidth) *
                                static_cast<uint64_t>(rowCount - 1u);
            int64_t width = 1;
            while (maxStart >= (uint64_t{1} << width))
            {
                ++width;
            }
            return width;
        }

        std::optional<ValueId> stripIndexWidthWrapper(const Graph &graph,
                                                      ValueId value,
                                                      int64_t requiredWidth)
        {
            if (!value.valid())
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return value;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kSliceStatic || defOp.operands().size() != 1)
            {
                return value;
            }
            const int64_t sliceStart = getAttr<int64_t>(defOp, "sliceStart").value_or(-1);
            const int64_t sliceEnd = getAttr<int64_t>(defOp, "sliceEnd").value_or(-1);
            if (sliceStart != 0 || sliceEnd < requiredWidth - 1)
            {
                return value;
            }
            return defOp.operands()[0];
        }

        std::optional<SliceDynamicIndexInfo> parseSliceDynamicStart(const Graph &graph,
                                                                    ValueId value,
                                                                    int32_t elementWidth,
                                                                    std::size_t rowCount)
        {
            if (elementWidth <= 0)
            {
                return std::nullopt;
            }
            if (elementWidth == 1)
            {
                return parseLinearElementIndex(graph, value, rowCount);
            }

            if (const auto stripped =
                    stripIndexWidthWrapper(graph, value, requiredBitIndexWidth(elementWidth, rowCount));
                stripped && *stripped != value)
            {
                return parseSliceDynamicStart(graph, *stripped, elementWidth, rowCount);
            }

            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);

            if ((defOp.kind() == OperationKind::kAdd || defOp.kind() == OperationKind::kSub) &&
                defOp.operands().size() == 2)
            {
                auto applyBitOffset = [&](ValueId nonConstOperand, int64_t bitDelta) -> std::optional<SliceDynamicIndexInfo> {
                    if (bitDelta % elementWidth != 0)
                    {
                        return std::nullopt;
                    }
                    if (auto base = parseSliceDynamicStart(graph, nonConstOperand, elementWidth, rowCount))
                    {
                        base->rowOffset += bitDelta / elementWidth;
                        return base;
                    }
                    return std::nullopt;
                };
                if (auto rhsConst = constantIntValue(graph, defOp.operands()[1]))
                {
                    const int64_t bitDelta = defOp.kind() == OperationKind::kAdd ? *rhsConst : -*rhsConst;
                    if (auto parsed = applyBitOffset(defOp.operands()[0], bitDelta))
                    {
                        return parsed;
                    }
                }
                if (defOp.kind() == OperationKind::kAdd)
                {
                    if (auto lhsConst = constantIntValue(graph, defOp.operands()[0]))
                    {
                        if (auto parsed = applyBitOffset(defOp.operands()[1], *lhsConst))
                        {
                            return parsed;
                        }
                    }
                }
            }

            if (defOp.kind() == OperationKind::kMul && defOp.operands().size() == 2)
            {
                if (auto lhsConst = constantIntValue(graph, defOp.operands()[0]))
                {
                    if (*lhsConst == elementWidth)
                    {
                        return parseLinearElementIndex(graph, defOp.operands()[1], rowCount);
                    }
                }
                if (auto rhsConst = constantIntValue(graph, defOp.operands()[1]))
                {
                    if (*rhsConst == elementWidth)
                    {
                        return parseLinearElementIndex(graph, defOp.operands()[0], rowCount);
                    }
                }
            }

            if ((elementWidth & (elementWidth - 1)) == 0 &&
                defOp.kind() == OperationKind::kShl &&
                defOp.operands().size() == 2)
            {
                if (auto shiftConst = constantIntValue(graph, defOp.operands()[1]))
                {
                    int32_t shift = 0;
                    while ((1 << shift) < elementWidth)
                    {
                        ++shift;
                    }
                    if (*shiftConst == shift)
                    {
                        return parseLinearElementIndex(graph, defOp.operands()[0], rowCount);
                    }
                }
            }

            return std::nullopt;
        }

        ValueId createAdjustedAddress(Graph &graph,
                                      ValueId baseAddr,
                                      int64_t rowDelta,
                                      std::string_view note)
        {
            if (rowDelta == 0)
            {
                return baseAddr;
            }
            const int32_t width = graph.valueWidth(baseAddr);
            const ValueId deltaConst =
                createConstantValue(graph,
                                    width,
                                    false,
                                    std::to_string(width) + "'d" + std::to_string(rowDelta < 0 ? -rowDelta : rowDelta),
                                    std::string(note) + "_const");
            return createBinaryOp(graph,
                                  rowDelta < 0 ? OperationKind::kSub : OperationKind::kAdd,
                                  baseAddr,
                                  deltaConst,
                                  width,
                                  false,
                                  note);
        }

        ReadRewritePlan analyzeReadPlan(const Graph &graph,
                                        const CandidateCluster &candidate,
                                        const ReadWriteRefs &refs,
                                        OperationId concatOpId,
                                        std::span<const ValueId> flattenedLeaves,
                                        std::span<const OperationId> concatOps,
                                        std::string *reasonOut)
        {
            ReadRewritePlan plan{};
            if (candidate.members.empty())
            {
                setRejectReason(reasonOut, "candidate has no members");
                return plan;
            }

            std::vector<ValueId> rowValues;
            rowValues.reserve(candidate.members.size());
            for (const ClusterMember &member : candidate.members)
            {
                if (!member.readResult.valid())
                {
                    setRejectReason(reasonOut, "member read result is invalid");
                    return {};
                }
                rowValues.push_back(member.readResult);
            }
            const Operation concatOp = graph.getOperation(concatOpId);
            if (concatOp.kind() != OperationKind::kConcat || concatOp.results().size() != 1)
            {
                setRejectReason(reasonOut, "candidate op is not a single-result kConcat");
                return {};
            }
            if (flattenedLeaves.size() != rowValues.size())
            {
                setRejectReason(reasonOut, "flattened concat leaf count does not match member count");
                return {};
            }

            std::vector<ValueId> expectedLeaves(rowValues.rbegin(), rowValues.rend());
            if (!sameOperands(std::span<const ValueId>(flattenedLeaves.data(), flattenedLeaves.size()),
                              std::span<const ValueId>(expectedLeaves.data(), expectedLeaves.size())))
            {
                setRejectReason(reasonOut, "flattened concat leaves do not match register read order");
                return {};
            }

            std::unordered_set<OperationId, OperationIdHash> concatOpSet(concatOps.begin(), concatOps.end());
            std::unordered_set<OperationId, OperationIdHash> writeOps;
            for (const ClusterMember &member : candidate.members)
            {
                if (const auto it = refs.writePortsBySymbol.find(member.symbol); it != refs.writePortsBySymbol.end())
                {
                    writeOps.insert(it->second.begin(), it->second.end());
                }
            }
            std::unordered_map<OperationId, bool, OperationIdHash> writeConeMemo;
            std::unordered_set<OperationId, OperationIdHash> writeConeVisiting;
            for (ValueId readValue : rowValues)
            {
                const Value value = graph.getValue(readValue);
                if (value.users().empty())
                {
                    setRejectReason(reasonOut, "register read has non-concat users");
                    return {};
                }
                for (const ValueUser &user : value.users())
                {
                    if (concatOpSet.contains(user.operation))
                    {
                        continue;
                    }
                    if (!isContainedInWriteCone(graph,
                                                user.operation,
                                                writeOps,
                                                writeConeMemo,
                                                writeConeVisiting))
                    {
                        setRejectReason(reasonOut, "register read has non-concat users");
                        return {};
                    }
                }
            }

            const ValueId concatResult = concatOp.results().front();
            const Value concatResultValue = graph.getValue(concatResult);
            if (concatResultValue.users().empty())
            {
                setRejectReason(reasonOut, "concat result has no users");
                return {};
            }
            for (const ValueUser &user : concatResultValue.users())
            {
                const Operation sliceOp = graph.getOperation(user.operation);
                if (sliceOp.operands().size() != 2 ||
                    sliceOp.results().size() != 1 ||
                    sliceOp.operands()[0] != concatResult)
                {
                    setRejectReason(reasonOut, "concat user is not a 2-operand single-result slice");
                    return {};
                }
                if (sliceOp.kind() == OperationKind::kSliceArray)
                {
                    if (getAttr<int64_t>(sliceOp, "sliceWidth").value_or(0) != candidate.key.width)
                    {
                        setRejectReason(reasonOut, "kSliceArray user width does not match member width");
                        return {};
                    }
                    plan.consumers.push_back(ReadRewritePlan::Consumer{
                        .eraseOp = user.operation,
                        .oldResult = sliceOp.results().front(),
                        .addr = sliceOp.operands()[1],
                        .constIndex = -1,
                        .reverseAddr = false,
                        .rowOffset = 0});
                    continue;
                }
                if (sliceOp.kind() == OperationKind::kSliceDynamic)
                {
                    if (getAttr<int64_t>(sliceOp, "sliceWidth").value_or(0) != candidate.key.width)
                    {
                        setRejectReason(reasonOut, "kSliceDynamic user width does not match member width");
                        return {};
                    }
                    const auto indexInfo =
                        parseSliceDynamicStart(graph, sliceOp.operands()[1], candidate.key.width, candidate.members.size());
                    if (!indexInfo)
                    {
                        setRejectReason(reasonOut, "kSliceDynamic start is not a supported element index expression");
                        return {};
                    }
                    plan.consumers.push_back(ReadRewritePlan::Consumer{
                        .eraseOp = user.operation,
                        .oldResult = sliceOp.results().front(),
                        .addr = indexInfo->addr,
                        .constIndex = -1,
                        .reverseAddr = indexInfo->reverseAddr,
                        .rowOffset = indexInfo->rowOffset});
                    continue;
                }
                setRejectReason(reasonOut, "concat user is neither kSliceArray nor kSliceDynamic");
                return {};
            }

            plan.concatOp = concatOpId;
            plan.concatResult = concatResult;
            plan.concatOps.assign(concatOps.begin(), concatOps.end());
            return plan;
        }

        std::optional<std::pair<FillGroupKey::DataRef, FillGroupKey::DataRef>>
        parseMuxArms(const Graph &graph,
                     ValueId value,
                     ValueId cond,
                     int64_t projectedSliceStart = -1,
                     int32_t projectedWidth = -1,
                     bool projectedSigned = false)
        {
            const int32_t targetWidth = projectedWidth > 0 ? projectedWidth : graph.valueWidth(value);
            const bool targetSigned = projectedWidth > 0 ? projectedSigned : graph.valueSigned(value);
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kMux || defOp.operands().size() != 3)
            {
                if (defOp.kind() == OperationKind::kSliceStatic &&
                    defOp.operands().size() == 1 &&
                    getAttr<int64_t>(defOp, "sliceStart").has_value() &&
                    getAttr<int64_t>(defOp, "sliceEnd").has_value())
                {
                    const int64_t sliceStart = getAttr<int64_t>(defOp, "sliceStart").value();
                    const int64_t absoluteSliceStart =
                        projectedSliceStart >= 0 ? projectedSliceStart + sliceStart : sliceStart;
                    return parseMuxArms(graph,
                                        defOp.operands()[0],
                                        cond,
                                        absoluteSliceStart,
                                        targetWidth,
                                        targetSigned);
                }

                if (defOp.kind() == OperationKind::kOr && defOp.operands().size() == 2)
                {
                    const int64_t sliceStart = projectedSliceStart >= 0 ? projectedSliceStart : 0;
                    auto parseMaskedData = [&](ValueId masked, bool expectTrueMask) -> std::optional<FillGroupKey::DataRef> {
                        const OperationId maskedDefId = graph.valueDef(masked);
                        if (!maskedDefId.valid())
                        {
                            return std::nullopt;
                        }
                        const Operation maskedDef = graph.getOperation(maskedDefId);
                        if (maskedDef.kind() != OperationKind::kAnd || maskedDef.operands().size() != 2)
                        {
                            return std::nullopt;
                        }
                        if (matchesProjectedCondMask(graph,
                                                     maskedDef.operands()[1],
                                                     sliceStart,
                                                     targetWidth,
                                                     cond,
                                                     expectTrueMask))
                        {
                            return makeProjectedDataRef(graph,
                                                        maskedDef.operands()[0],
                                                        sliceStart,
                                                        targetWidth,
                                                        targetSigned);
                        }
                        if (matchesProjectedCondMask(graph,
                                                     maskedDef.operands()[0],
                                                     sliceStart,
                                                     targetWidth,
                                                     cond,
                                                     expectTrueMask))
                        {
                            return makeProjectedDataRef(graph,
                                                        maskedDef.operands()[1],
                                                        sliceStart,
                                                        targetWidth,
                                                        targetSigned);
                        }
                        return std::nullopt;
                    };

                    auto trueData = parseMaskedData(defOp.operands()[0], true);
                    auto falseData = parseMaskedData(defOp.operands()[1], false);
                    if (trueData && falseData)
                    {
                        return std::pair<FillGroupKey::DataRef, FillGroupKey::DataRef>{*trueData, *falseData};
                    }
                    trueData = parseMaskedData(defOp.operands()[1], true);
                    falseData = parseMaskedData(defOp.operands()[0], false);
                    if (trueData && falseData)
                    {
                        return std::pair<FillGroupKey::DataRef, FillGroupKey::DataRef>{*trueData, *falseData};
                    }
                }

                return std::nullopt;
            }
            if (defOp.operands()[0] == cond)
            {
                auto makeLeafDataRef = [&](ValueId arm) -> FillGroupKey::DataRef {
                    if (projectedSliceStart >= 0)
                    {
                        if (projectedSliceStart == 0 && targetWidth == graph.valueWidth(arm))
                        {
                            return makeDataRefFromValue(graph, arm);
                        }
                        return makeDataRefFromSlice(graph, arm, targetWidth, projectedSliceStart, targetSigned);
                    }
                    return makeDataRefFromValue(graph, arm);
                };
                auto normalizeArm = [&](ValueId arm, bool trueWhenCond) -> FillGroupKey::DataRef {
                    if (auto nested = parseMuxArms(graph, arm, cond, projectedSliceStart, targetWidth, targetSigned))
                    {
                        if (trueWhenCond &&
                            !nested->second.literal.empty() &&
                            isAllZeroLiteral(nested->second.literal, nested->second.width))
                        {
                            return nested->first;
                        }
                        if (!trueWhenCond &&
                            !nested->first.literal.empty() &&
                            isAllZeroLiteral(nested->first.literal, nested->first.width))
                        {
                            return nested->second;
                        }
                    }
                    return makeLeafDataRef(arm);
                };
                return std::pair<FillGroupKey::DataRef, FillGroupKey::DataRef>{
                    normalizeArm(defOp.operands()[1], true),
                    normalizeArm(defOp.operands()[2], false)};
            }
            const OperationId condDefId = graph.valueDef(defOp.operands()[0]);
            if (condDefId.valid())
            {
                const Operation condDef = graph.getOperation(condDefId);
                if (condDef.kind() == OperationKind::kLogicNot &&
                    condDef.operands().size() == 1 &&
                    condDef.operands()[0] == cond)
                {
                    auto makeLeafDataRef = [&](ValueId arm) -> FillGroupKey::DataRef {
                        if (projectedSliceStart >= 0)
                        {
                            if (projectedSliceStart == 0 && targetWidth == graph.valueWidth(arm))
                            {
                                return makeDataRefFromValue(graph, arm);
                            }
                            return makeDataRefFromSlice(graph, arm, targetWidth, projectedSliceStart, targetSigned);
                        }
                        return makeDataRefFromValue(graph, arm);
                    };
                    auto normalizeArm = [&](ValueId arm, bool trueWhenCond) -> FillGroupKey::DataRef {
                        if (auto nested = parseMuxArms(graph, arm, cond, projectedSliceStart, targetWidth, targetSigned))
                        {
                            if (trueWhenCond &&
                                !nested->second.literal.empty() &&
                                isAllZeroLiteral(nested->second.literal, nested->second.width))
                            {
                                return nested->first;
                            }
                            if (!trueWhenCond &&
                                !nested->first.literal.empty() &&
                                isAllZeroLiteral(nested->first.literal, nested->first.width))
                            {
                                return nested->second;
                            }
                        }
                        return makeLeafDataRef(arm);
                    };
                    return std::pair<FillGroupKey::DataRef, FillGroupKey::DataRef>{
                        normalizeArm(defOp.operands()[2], true),
                        normalizeArm(defOp.operands()[1], false)};
                }
            }
            return std::nullopt;
        }

        struct PointCondBranch
        {
            ValueId cond;
            PointCondInfo info;
            std::vector<ValueId> extraBaseTerms;
            std::size_t clusterRows = 0;
        };

        std::vector<ValueId> mergedPointBaseTerms(const PointCondBranch &branch)
        {
            std::vector<ValueId> merged = branch.info.baseTerms;
            merged.insert(merged.end(), branch.extraBaseTerms.begin(), branch.extraBaseTerms.end());
            canonicalizeValues(merged);
            merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
            return merged;
        }

        bool matchesEquivalentPointCondMask(const Graph &graph,
                                            ValueId maskValue,
                                            int64_t sliceStart,
                                            int32_t sliceWidth,
                                            const PointCondBranch &branch,
                                            bool expectTrue)
        {
            if (matchesProjectedCondMask(graph, maskValue, sliceStart, sliceWidth, branch.cond, expectTrue))
            {
                return true;
            }
            if (!expectTrue)
            {
                return false;
            }

            const OperationId defOpId = graph.valueDef(maskValue);
            if (defOpId.valid())
            {
                const Operation defOp = graph.getOperation(defOpId);
                if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
                {
                    return matchesEquivalentPointCondMask(graph,
                                                          defOp.operands()[0],
                                                          sliceStart,
                                                          sliceWidth,
                                                          branch,
                                                          expectTrue);
                }
            }

            if (graph.valueWidth(maskValue) == 1)
            {
                if (auto parsed = parsePointCond(graph, maskValue, branch.clusterRows))
                {
                    return *parsed == branch.info;
                }
            }

            if (sliceWidth == 1)
            {
                auto projected = projectStaticSliceLeaf(graph, maskValue, sliceStart, sliceWidth);
                if (!projected)
                {
                    return false;
                }
                if (auto parsed = parsePointCond(graph, *projected, branch.clusterRows))
                {
                    return *parsed == branch.info;
                }
            }
            return false;
        }

        std::optional<FillGroupKey::DataRef> parseMaskedPointDataTerm(const Graph &graph,
                                                                      ValueId term,
                                                                      const PointCondBranch &branch,
                                                                      int32_t width,
                                                                      bool isSigned)
        {
            const OperationId defOpId = graph.valueDef(term);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
            {
                return parseMaskedPointDataTerm(graph, defOp.operands()[0], branch, width, isSigned);
            }
            if (!isAndKind(defOp.kind()) || defOp.operands().size() != 2)
            {
                if (defOp.kind() == OperationKind::kMux && defOp.operands().size() == 3)
                {
                    auto tryMaskedMux = [&](ValueId condValue,
                                            ValueId trueValue,
                                            ValueId falseValue) -> std::optional<FillGroupKey::DataRef> {
                        if (!matchesEquivalentPointCondMask(graph, condValue, 0, width, branch, true))
                        {
                            return std::nullopt;
                        }
                        const auto falseData = makeProjectedDataRef(graph, falseValue, 0, width, isSigned);
                        if (falseData.literal.empty() || !isAllZeroLiteral(falseData.literal, falseData.width))
                        {
                            return std::nullopt;
                        }
                        return makeProjectedDataRef(graph, trueValue, 0, width, isSigned);
                    };

                    if (auto data = tryMaskedMux(defOp.operands()[0], defOp.operands()[1], defOp.operands()[2]))
                    {
                        return data;
                    }
                }
                return std::nullopt;
            }
            if (matchesEquivalentPointCondMask(graph, defOp.operands()[0], 0, width, branch, true))
            {
                return makeProjectedDataRef(graph, defOp.operands()[1], 0, width, isSigned);
            }
            if (matchesEquivalentPointCondMask(graph, defOp.operands()[1], 0, width, branch, true))
            {
                return makeProjectedDataRef(graph, defOp.operands()[0], 0, width, isSigned);
            }
            return std::nullopt;
        }

        bool isSelfRegisterReadValue(const Graph &graph, ValueId value, std::string_view regSymbol)
        {
            if (!value.valid())
            {
                return false;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() == OperationKind::kAssign && defOp.operands().size() == 1)
            {
                return isSelfRegisterReadValue(graph, defOp.operands()[0], regSymbol);
            }
            if (defOp.kind() != OperationKind::kRegisterReadPort)
            {
                return false;
            }
            const auto readRegSymbol = getAttr<std::string>(defOp, "regSymbol");
            return readRegSymbol && *readRegSymbol == regSymbol;
        }

        bool isSelfRegisterDataRef(const Graph &graph,
                                   const FillGroupKey::DataRef &data,
                                   std::string_view regSymbol)
        {
            if (!data.value.valid() || data.sliceStart >= 0)
            {
                return false;
            }
            return isSelfRegisterReadValue(graph, data.value, regSymbol);
        }

        bool samePointBranchDescriptor(const PointCondBranch &lhs, const PointCondBranch &rhs)
        {
            return lhs.info == rhs.info && lhs.extraBaseTerms == rhs.extraBaseTerms;
        }

        std::optional<std::vector<std::size_t>> parsePointBranchSubset(const Graph &graph,
                                                                       ValueId cond,
                                                                       std::span<const PointCondBranch> branches,
                                                                       bool allowFullMatch = false)
        {
            if (branches.empty())
            {
                return std::nullopt;
            }
            const auto bundle = parsePointCondBundle(graph, cond, branches.front().clusterRows);
            if (!bundle)
            {
                return std::nullopt;
            }

            std::vector<PointCondBranch> parsed;
            parsed.reserve(bundle->leaves.size());
            for (const auto &[leafCond, leafInfo] : bundle->leaves)
            {
                parsed.push_back(PointCondBranch{
                    .cond = leafCond,
                    .info = leafInfo,
                    .extraBaseTerms = bundle->globalBaseTerms,
                    .clusterRows = branches.front().clusterRows,
                });
            }

            std::vector<std::size_t> matched;
            std::vector<bool> used(branches.size(), false);
            for (const PointCondBranch &want : parsed)
            {
                bool found = false;
                for (std::size_t i = 0; i < branches.size(); ++i)
                {
                    if (used[i])
                    {
                        continue;
                    }
                    if (!samePointBranchDescriptor(want, branches[i]))
                    {
                        continue;
                    }
                    used[i] = true;
                    matched.push_back(i);
                    found = true;
                    break;
                }
                if (!found)
                {
                    return std::nullopt;
                }
            }
            if (matched.empty())
            {
                return std::nullopt;
            }
            if (!allowFullMatch && matched.size() == branches.size())
            {
                return std::nullopt;
            }
            std::sort(matched.begin(), matched.end());
            return matched;
        }

        void appendBroadcastPointArms(std::span<const PointCondBranch> branches,
                                      const FillGroupKey::DataRef &data,
                                      std::vector<ParsedPointArm> &pointsOut)
        {
            for (const PointCondBranch &branch : branches)
            {
                pointsOut.push_back(ParsedPointArm{
                    .baseTerms = mergedPointBaseTerms(branch),
                    .addr = branch.info.addr,
                    .constIndex = branch.info.constIndex,
                    .data = data});
            }
        }

        bool parsePointDataMaskedOrTree(const Graph &graph,
                                        const FillGroupKey::DataRef &value,
                                        std::span<const PointCondBranch> branches,
                                        std::vector<ParsedPointArm> &pointsOut,
                                        FillGroupKey::DataRef &defaultDataOut)
        {
            if (!value.value.valid() || branches.empty())
            {
                return false;
            }

            std::vector<ValueId> terms;
            flattenOrTerms(graph, value.value, terms);
            if (terms.size() != branches.size())
            {
                return false;
            }

            std::vector<bool> used(terms.size(), false);
            std::vector<ParsedPointArm> parsedPoints;
            parsedPoints.reserve(branches.size());
            for (const PointCondBranch &branch : branches)
            {
                bool matched = false;
                for (std::size_t i = 0; i < terms.size(); ++i)
                {
                    if (used[i])
                    {
                        continue;
                    }
                    const auto data = parseMaskedPointDataTerm(graph, terms[i], branch, value.width, value.isSigned);
                    if (!data)
                    {
                        continue;
                    }
                    used[i] = true;
                    parsedPoints.push_back(ParsedPointArm{
                        .baseTerms = mergedPointBaseTerms(branch),
                        .addr = branch.info.addr,
                        .constIndex = branch.info.constIndex,
                        .data = *data});
                    matched = true;
                    break;
                }
                if (!matched)
                {
                    return false;
                }
            }

            pointsOut = std::move(parsedPoints);
            defaultDataOut = FillGroupKey::DataRef{};
            return true;
        }

        bool parsePointDataTree(const Graph &graph,
                                const FillGroupKey::DataRef &value,
                                std::span<const PointCondBranch> branches,
                                std::vector<ParsedPointArm> &pointsOut,
                                FillGroupKey::DataRef &defaultDataOut,
                                bool allowImplicitLastPointArm)
        {
            if (branches.empty())
            {
                defaultDataOut = value;
                return true;
            }

            const OperationId valueDefId = value.value.valid() ? graph.valueDef(value.value) : OperationId::invalid();
            const bool structurallySimpleValue =
                !value.value.valid() ||
                !valueDefId.valid() ||
                ([&]() {
                    const Operation defOp = graph.getOperation(valueDefId);
                    return defOp.kind() != OperationKind::kMux &&
                           defOp.kind() != OperationKind::kOr &&
                           defOp.kind() != OperationKind::kLogicOr;
                })();
            if (branches.size() > 1 && structurallySimpleValue)
            {
                appendBroadcastPointArms(branches, value, pointsOut);
                defaultDataOut = FillGroupKey::DataRef{};
                return true;
            }

            if (allowImplicitLastPointArm && branches.size() == 1)
            {
                pointsOut.push_back(ParsedPointArm{
                    .baseTerms = mergedPointBaseTerms(branches.front()),
                    .addr = branches.front().info.addr,
                    .constIndex = branches.front().info.constIndex,
                    .data = value});
                defaultDataOut = FillGroupKey::DataRef{};
                return true;
            }

            if (branches.size() > 1 &&
                parsePointDataMaskedOrTree(graph, value, branches, pointsOut, defaultDataOut))
            {
                return true;
            }

            if (value.value.valid() && valueDefId.valid())
            {
                const Operation valueDef = graph.getOperation(valueDefId);
                if (valueDef.kind() == OperationKind::kMux && valueDef.operands().size() == 3)
                {
                    if (const auto subset = parsePointBranchSubset(graph,
                                                                   valueDef.operands()[0],
                                                                   std::span<const PointCondBranch>(branches.data(),
                                                                                                   branches.size()),
                                                                   true))
                    {
                        std::vector<PointCondBranch> selected;
                        std::vector<PointCondBranch> remaining;
                        selected.reserve(subset->size());
                        remaining.reserve(branches.size() - subset->size());
                        std::size_t nextSelected = 0;
                        for (std::size_t i = 0; i < branches.size(); ++i)
                        {
                            if (nextSelected < subset->size() && (*subset)[nextSelected] == i)
                            {
                                selected.push_back(branches[i]);
                                ++nextSelected;
                            }
                            else
                            {
                                remaining.push_back(branches[i]);
                            }
                        }

                        std::vector<ParsedPointArm> truePoints;
                        std::vector<ParsedPointArm> falsePoints;
                        FillGroupKey::DataRef trueDefault;
                        FillGroupKey::DataRef falseDefault;
                        if (parsePointDataTree(graph,
                                               makeDataRefFromValue(graph, valueDef.operands()[1]),
                                               std::span<const PointCondBranch>(selected.data(), selected.size()),
                                               truePoints,
                                               trueDefault,
                                               true) &&
                            parsePointDataTree(graph,
                                               makeDataRefFromValue(graph, valueDef.operands()[2]),
                                               std::span<const PointCondBranch>(remaining.data(), remaining.size()),
                                               falsePoints,
                                               falseDefault,
                                               true) &&
                            !trueDefault.value.valid() && trueDefault.literal.empty())
                        {
                            if (remaining.empty())
                            {
                                pointsOut = std::move(truePoints);
                                defaultDataOut = falseDefault;
                                return true;
                            }
                            if (!falseDefault.value.valid() && falseDefault.literal.empty())
                            {
                                pointsOut = std::move(truePoints);
                                pointsOut.insert(pointsOut.end(), falsePoints.begin(), falsePoints.end());
                                defaultDataOut = FillGroupKey::DataRef{};
                                return true;
                            }
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < branches.size(); ++i)
            {
                if (!value.value.valid())
                {
                    continue;
                }
                const auto muxArms = parseMuxArms(graph, value.value, branches[i].cond);
                if (!muxArms)
                {
                    continue;
                }

                std::vector<PointCondBranch> remaining;
                remaining.reserve(branches.size() - 1);
                for (std::size_t j = 0; j < branches.size(); ++j)
                {
                    if (j != i)
                    {
                        remaining.push_back(branches[j]);
                    }
                }

                std::vector<ParsedPointArm> tailPoints;
                FillGroupKey::DataRef tailDefault;
                if (!parsePointDataTree(graph,
                                        muxArms->second,
                                        std::span<const PointCondBranch>(remaining.data(), remaining.size()),
                                        tailPoints,
                                        tailDefault,
                                        allowImplicitLastPointArm))
                {
                    continue;
                }

                pointsOut.push_back(ParsedPointArm{
                    .baseTerms = mergedPointBaseTerms(branches[i]),
                    .addr = branches[i].info.addr,
                    .constIndex = branches[i].info.constIndex,
                    .data = muxArms->first});
                pointsOut.insert(pointsOut.end(), tailPoints.begin(), tailPoints.end());
                defaultDataOut = tailDefault;
                return true;
            }

            return false;
        }

        bool parseFillDataTree(const Graph &graph,
                               const FillGroupKey::DataRef &value,
                               std::span<const ValueId> branches,
                               std::vector<ParsedFillArm> &fillsOut,
                               bool allowImplicitLastFillArm)
        {
            if (branches.empty())
            {
                return true;
            }

            if (allowImplicitLastFillArm && branches.size() == 1)
            {
                fillsOut.push_back(ParsedFillArm{
                    .condBranches = {branches.front()},
                    .data = value});
                return true;
            }

            for (std::size_t i = 0; i < branches.size(); ++i)
            {
                if (!value.value.valid())
                {
                    continue;
                }
                const auto muxArms = parseMuxArms(graph, value.value, branches[i]);
                if (!muxArms)
                {
                    continue;
                }

                std::vector<ValueId> remaining;
                remaining.reserve(branches.size() - 1);
                for (std::size_t j = 0; j < branches.size(); ++j)
                {
                    if (j != i)
                    {
                        remaining.push_back(branches[j]);
                    }
                }

                std::vector<ParsedFillArm> tailFills;
                if (!parseFillDataTree(graph,
                                       muxArms->second,
                                       std::span<const ValueId>(remaining.data(), remaining.size()),
                                       tailFills,
                                       allowImplicitLastFillArm))
                {
                    continue;
                }

                fillsOut.push_back(ParsedFillArm{
                    .condBranches = {branches[i]},
                    .data = muxArms->first});
                fillsOut.insert(fillsOut.end(), tailFills.begin(), tailFills.end());
                return true;
            }

            return false;
        }

        std::optional<ParsedWritePattern> parseWritePattern(const Graph &graph,
                                                            OperationId opId,
                                                            std::size_t clusterRows,
                                                            std::string *reasonOut)
        {
            const Operation op = graph.getOperation(opId);
            const auto tracedRegSymbol = getAttr<std::string>(op, "regSymbol");
            if (op.kind() != OperationKind::kRegisterWritePort || op.operands().size() < 4)
            {
                setRejectReason(reasonOut, "write op is not a register write port with mask and event operands");
                return std::nullopt;
            }

            ParsedWritePattern pattern{
                .opId = opId,
                .mask = op.operands()[2],
                .eventValues = std::vector<ValueId>(op.operands().begin() + 3, op.operands().end()),
                .eventEdges = getAttr<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}),
            };

            std::vector<ValueId> condBranches;
            flattenOrTerms(graph, op.operands()[0], condBranches);
            std::vector<ValueId> fillBranches;
            std::vector<PointCondBranch> pointBranches;
            for (ValueId condBranch : condBranches)
            {
                if (auto parsed = parsePointCond(graph, condBranch, clusterRows))
                {
                    pointBranches.push_back(PointCondBranch{
                        .cond = condBranch,
                        .info = std::move(*parsed),
                        .extraBaseTerms = {},
                        .clusterRows = clusterRows,
                    });
                }
                else if (auto bundle = parsePointCondBundle(graph, condBranch, clusterRows))
                {
                    for (auto &[leafCond, leafInfo] : bundle->leaves)
                    {
                        pointBranches.push_back(PointCondBranch{
                            .cond = leafCond,
                            .info = std::move(leafInfo),
                            .extraBaseTerms = bundle->globalBaseTerms,
                            .clusterRows = clusterRows,
                        });
                    }
                }
                else
                {
                    fillBranches.push_back(condBranch);
                }
            }

            if (pointBranches.empty())
            {
                canonicalizeValues(condBranches);
                pattern.fills.push_back(ParsedFillArm{
                    .condBranches = std::move(condBranches),
                    .data = makeDataRefFromValue(graph, op.operands()[1])});
                assignWriteOrders(pattern);
                if (tracedRegSymbol && matchesTraceFilter(*tracedRegSymbol))
                {
                    std::cerr << "[scalar-memory-pack] write-pattern reg=" << *tracedRegSymbol
                              << " points=0 fills=" << pattern.fills.size()
                              << " mode=fill-only\n";
                }
                return pattern;
            }

            if (pointBranches.size() == 1 && fillBranches.empty())
            {
                pattern.points.push_back(ParsedPointArm{
                    .baseTerms = std::move(pointBranches.front().info.baseTerms),
                    .addr = pointBranches.front().info.addr,
                    .constIndex = pointBranches.front().info.constIndex,
                    .data = makeDataRefFromValue(graph, op.operands()[1])});
                assignWriteOrders(pattern);
                if (tracedRegSymbol && matchesTraceFilter(*tracedRegSymbol))
                {
                    std::cerr << "[scalar-memory-pack] write-pattern reg=" << *tracedRegSymbol
                              << " points=1 fills=0 mode=single-point\n";
                }
                return pattern;
            }

            std::vector<ParsedPointArm> parsedPoints;
            FillGroupKey::DataRef defaultData;
            if (!parsePointDataTree(graph,
                                    makeDataRefFromValue(graph, op.operands()[1]),
                                    std::span<const PointCondBranch>(pointBranches.data(), pointBranches.size()),
                                    parsedPoints,
                                    defaultData,
                                    fillBranches.empty()))
            {
                if (!fillBranches.empty())
                {
                    const OperationId dataDefId = graph.valueDef(op.operands()[1]);
                    if (dataDefId.valid())
                    {
                        const Operation dataDef = graph.getOperation(dataDefId);
                        if (dataDef.kind() == OperationKind::kMux &&
                            dataDef.operands().size() == 3 &&
                            std::find(fillBranches.begin(), fillBranches.end(), dataDef.operands()[0]) != fillBranches.end())
                        {
                            setRejectReason(reasonOut,
                                            "mixed write top-level mux selects on a non-address member-local branch");
                            return std::nullopt;
                        }
                    }
                }
                setRejectReason(reasonOut,
                                fillBranches.empty()
                                    ? "multiple point-update branches do not encode nextValue as a mux tree over point conditions"
                                    : "mixed fill/point write does not encode nextValue as a mux tree over point conditions");
                return std::nullopt;
            }

            pattern.points = std::move(parsedPoints);
            if (!fillBranches.empty())
            {
                if (tracedRegSymbol && isSelfRegisterDataRef(graph, defaultData, *tracedRegSymbol))
                {
                    if (tracedRegSymbol && matchesTraceFilter(*tracedRegSymbol))
                    {
                        std::cerr << "[scalar-memory-pack] write-pattern reg=" << *tracedRegSymbol
                                  << " dropping fill branches because default data is self-read\n";
                    }
                    assignWriteOrders(pattern);
                    return pattern;
                }
                std::vector<ParsedFillArm> parsedFills;
                if (fillBranches.size() > 1 &&
                    parseFillDataTree(graph,
                                      defaultData,
                                      std::span<const ValueId>(fillBranches.data(), fillBranches.size()),
                                      parsedFills,
                                      true))
                {
                    pattern.fills = std::move(parsedFills);
                }
                else
                {
                    canonicalizeValues(fillBranches);
                    pattern.fills.push_back(ParsedFillArm{
                        .condBranches = std::move(fillBranches),
                        .data = defaultData});
                }
            }
            assignWriteOrders(pattern);
            if (tracedRegSymbol && matchesTraceFilter(*tracedRegSymbol))
            {
                std::cerr << "[scalar-memory-pack] write-pattern reg=" << *tracedRegSymbol
                          << " points=" << pattern.points.size()
                          << " fills=" << pattern.fills.size()
                          << " mode=mixed\n";
            }
            return pattern;
        }

        ValueId materializeDataRef(Graph &graph,
                                   const FillGroupKey::DataRef &data,
                                   std::string_view note)
        {
            if (data.value.valid())
            {
                if (data.sliceStart >= 0)
                {
                    const SymbolId valueSym = graph.makeInternalValSym();
                    const SymbolId opSym = graph.makeInternalOpSym();
                    const ValueId out = graph.createValue(valueSym,
                                                          data.width > 0 ? data.width : 1,
                                                          data.isSigned,
                                                          ValueType::Logic);
                    const OperationId op = graph.createOperation(OperationKind::kSliceStatic, opSym);
                    graph.addOperand(op, data.value);
                    graph.addResult(op, out);
                    graph.setAttr(op, "sliceStart", data.sliceStart);
                    graph.setAttr(op, "sliceEnd", data.sliceStart + data.width - 1);
                    const SrcLoc srcLoc = makeTransformSrcLoc("scalar-memory-pack", note);
                    graph.setValueSrcLoc(out, srcLoc);
                    graph.setOpSrcLoc(op, srcLoc);
                    return out;
                }
                return data.value;
            }
            return createConstantValue(graph, data.width, data.isSigned, data.literal, note);
        }

        bool analyzeFillGroups(const Graph &graph,
                               const CandidateCluster &candidate,
                               const ReadWriteRefs &refs,
                               std::vector<std::pair<FillGroupKey, std::vector<OperationId>>> &groupsOut,
                               std::string *reasonOut)
        {
            std::unordered_map<FillGroupKey, std::vector<std::pair<std::string, OperationId>>, FillGroupKeyHash> groups;
            std::unordered_map<FillFamilyKey,
                               std::vector<std::pair<std::string, FillGroupKey::DataRef>>,
                               FillFamilyKeyHash>
                familyGroups;
            bool sawAnyFillArm = false;
            for (const ClusterMember &member : candidate.members)
            {
                const auto writeIt = refs.writePortsBySymbol.find(member.symbol);
                if (writeIt == refs.writePortsBySymbol.end())
                {
                    setRejectReason(reasonOut, "missing register write port for member " + member.symbol);
                    return false;
                }
                for (OperationId opId : writeIt->second)
                {
                    std::string writeReason;
                    const auto parsed = parseWritePattern(graph, opId, candidate.members.size(), &writeReason);
                    if (!parsed)
                    {
                        setRejectReason(reasonOut,
                                        "failed to parse register write pattern for " +
                                            std::string(graph.getOperation(opId).symbolText()) +
                                            ": " + writeReason);
                        return false;
                    }
                    if (parsed->fills.empty())
                    {
                        continue;
                    }
                    for (const ParsedFillArm &fill : parsed->fills)
                    {
                        sawAnyFillArm = true;
                        FillGroupKey key{
                            .condBranches = fill.condBranches,
                            .data = fill.data,
                            .mask = parsed->mask,
                            .eventValues = parsed->eventValues,
                            .eventEdges = parsed->eventEdges,
                            .writeOrder = fill.writeOrder};
                        groups[key].push_back({member.symbol, opId});

                        FillFamilyKey familyKey{
                            .condBranches = fill.condBranches,
                            .mask = parsed->mask,
                            .eventValues = parsed->eventValues,
                            .eventEdges = parsed->eventEdges};
                        familyGroups[familyKey].push_back({member.symbol, fill.data});
                    }
                }
            }

            for (const auto &[key, items] : groups)
            {
                (void)key;
                if (items.size() != candidate.members.size())
                {
                    continue;
                }
                if (!isAllOnesMaskValue(graph, key.mask, candidate.key.width))
                {
                    setRejectReason(reasonOut, "fill group mask is not an all-ones literal");
                    return false;
                }
                std::unordered_set<std::string> seenSymbols;
                bool complete = true;
                std::vector<OperationId> opIds;
                opIds.reserve(items.size());
                for (const auto &[symbol, opId] : items)
                {
                    if (!seenSymbols.insert(symbol).second)
                    {
                        complete = false;
                        break;
                    }
                    opIds.push_back(opId);
                }
                if (!complete)
                {
                    continue;
                }
                groupsOut.push_back({key, std::move(opIds)});
            }
            if (groupsOut.empty())
            {
                if (!sawAnyFillArm)
                {
                    return true;
                }
                for (const auto &[familyKey, items] : familyGroups)
                {
                    if (items.size() != candidate.members.size())
                    {
                        continue;
                    }
                    std::unordered_set<std::string> seenSymbols;
                    bool complete = true;
                    std::optional<FillGroupKey::DataRef> firstData;
                    bool allSameData = true;
                    for (const auto &[symbol, data] : items)
                    {
                        if (!seenSymbols.insert(symbol).second)
                        {
                            complete = false;
                            break;
                        }
                        if (!firstData)
                        {
                            firstData = data;
                        }
                        else if (*firstData != data)
                        {
                            allSameData = false;
                        }
                    }
                    if (!complete)
                    {
                        continue;
                    }
                    if (!allSameData)
                    {
                        setRejectReason(reasonOut,
                                        "cluster has row-varying bulk branch not representable as kMemoryFillPort");
                        return false;
                    }
                    (void)familyKey;
                }
                setRejectReason(reasonOut, "no complete fill group covers the full cluster");
            }
            return true;
        }

        bool analyzePointWriteGroups(const Graph &graph,
                                     const CandidateCluster &candidate,
                                     const ReadWriteRefs &refs,
                                     std::vector<std::pair<PointWriteKey, std::vector<OperationId>>> &groupsOut,
                                     std::string *reasonOut)
        {
            std::unordered_map<std::string, int64_t> memberIndexBySymbol;
            for (const ClusterMember &member : candidate.members)
            {
                memberIndexBySymbol.emplace(member.symbol, member.index);
            }

            std::unordered_map<PointWriteKey, std::vector<PointWriteInfo>, PointWriteKeyHash> groups;
            for (const ClusterMember &member : candidate.members)
            {
                const auto writeIt = refs.writePortsBySymbol.find(member.symbol);
                if (writeIt == refs.writePortsBySymbol.end())
                {
                    setRejectReason(reasonOut, "missing register write port for member " + member.symbol);
                    return false;
                }
                for (OperationId opId : writeIt->second)
                {
                    std::string writeReason;
                    const auto parsed = parseWritePattern(graph, opId, candidate.members.size(), &writeReason);
                    if (!parsed)
                    {
                        setRejectReason(reasonOut,
                                        "failed to parse register write pattern for " +
                                            std::string(graph.getOperation(opId).symbolText()) +
                                            ": " + writeReason);
                        return false;
                    }
                    if (parsed->points.empty())
                    {
                        continue;
                    }
                    for (const ParsedPointArm &point : parsed->points)
                    {
                        PointWriteKey key{
                            .baseTerms = point.baseTerms,
                            .addr = point.addr,
                            .data = point.data,
                            .mask = parsed->mask,
                            .eventValues = parsed->eventValues,
                            .eventEdges = parsed->eventEdges,
                            .writeOrder = point.writeOrder};
                        groups[key].push_back(PointWriteInfo{
                            .key = key,
                            .constIndex = point.constIndex,
                            .opId = opId,
                            .memberSymbol = member.symbol});
                    }
                }
            }

            for (const auto &[key, infos] : groups)
            {
                if (infos.size() != candidate.members.size())
                {
                    continue;
                }
                std::unordered_set<std::string> seenSymbols;
                bool complete = true;
                std::vector<OperationId> opIds;
                opIds.reserve(infos.size());
                for (const PointWriteInfo &info : infos)
                {
                    const auto memberIndexIt = memberIndexBySymbol.find(info.memberSymbol);
                    if (memberIndexIt == memberIndexBySymbol.end() ||
                        memberIndexIt->second + candidate.indexBase != info.constIndex ||
                        !seenSymbols.insert(info.memberSymbol).second)
                    {
                        complete = false;
                        break;
                    }
                    opIds.push_back(info.opId);
                }
                if (!complete)
                {
                    continue;
                }
                groupsOut.push_back({key, std::move(opIds)});
            }
            if (groupsOut.empty())
            {
                setRejectReason(reasonOut, "no complete point-write group covers the full cluster");
            }
            return true;
        }

        int64_t earliestOpOrder(std::span<const OperationId> opIds)
        {
            int64_t order = std::numeric_limits<int64_t>::max();
            for (OperationId opId : opIds)
            {
                order = std::min(order, static_cast<int64_t>(opId.index));
            }
            return order;
        }

        std::vector<OrderedWriteGroupRef> buildOrderedWriteGroups(const CandidateCluster &candidate)
        {
            std::vector<OrderedWriteGroupRef> groups;
            groups.reserve(candidate.pointWriteGroups.size() + candidate.fillGroups.size());

            for (std::size_t i = 0; i < candidate.pointWriteGroups.size(); ++i)
            {
                const auto &[key, opIds] = candidate.pointWriteGroups[i];
                groups.push_back(OrderedWriteGroupRef{
                    .kind = OrderedWriteGroupRef::Kind::Point,
                    .index = i,
                    .order = earliestOpOrder(std::span<const OperationId>(opIds.data(), opIds.size())) * kWriteOrderStride +
                             key.writeOrder});
            }
            for (std::size_t i = 0; i < candidate.fillGroups.size(); ++i)
            {
                const auto &[key, opIds] = candidate.fillGroups[i];
                groups.push_back(OrderedWriteGroupRef{
                    .kind = OrderedWriteGroupRef::Kind::Fill,
                    .index = i,
                    .order = earliestOpOrder(std::span<const OperationId>(opIds.data(), opIds.size())) * kWriteOrderStride +
                             key.writeOrder});
            }

            std::sort(groups.begin(), groups.end(), [](const OrderedWriteGroupRef &lhs, const OrderedWriteGroupRef &rhs) {
                if (lhs.order != rhs.order)
                {
                    return lhs.order < rhs.order;
                }
                return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
            });
            return groups;
        }

        std::optional<FlatConcatInfo> analyzeConcatReadShape(const Graph &graph,
                                                             OperationId concatOpId,
                                                             std::string *reasonOut)
        {
            const Operation concatOp = graph.getOperation(concatOpId);
            if (concatOp.kind() != OperationKind::kConcat || concatOp.results().size() != 1)
            {
                setRejectReason(reasonOut, "op is not a single-result kConcat");
                return std::nullopt;
            }

            std::vector<ValueId> flattenedLeaves;
            std::vector<OperationId> concatOps;
            flattenConcatLeaves(graph, concatOpId, flattenedLeaves, concatOps);
            if (flattenedLeaves.size() < 4)
            {
                setRejectReason(reasonOut, "concat has fewer than 4 flattened leaves");
                return std::nullopt;
            }

            FlatConcatInfo readShape;
            readShape.concatOpId = concatOpId;
            readShape.flattenedLeaves = flattenedLeaves;
            readShape.concatOps = concatOps;
            std::unordered_set<std::string> seenSymbols;
            readShape.members.reserve(flattenedLeaves.size());
            int32_t clusterWidth = 0;
            bool clusterSigned = false;
            for (auto it = flattenedLeaves.rbegin(); it != flattenedLeaves.rend(); ++it)
            {
                const ValueId readValue = *it;
                const OperationId readOpId = graph.valueDef(readValue);
                if (!readOpId.valid())
                {
                    setRejectReason(reasonOut, "concat leaf has no defining operation");
                    return std::nullopt;
                }
                const Operation readOp = graph.getOperation(readOpId);
                if (readOp.kind() != OperationKind::kRegisterReadPort || readOp.results().size() != 1)
                {
                    setRejectReason(reasonOut, "concat leaf is not a single-result register read port");
                    return std::nullopt;
                }
                const auto regSymbol = getAttr<std::string>(readOp, "regSymbol");
                if (!regSymbol || !seenSymbols.insert(*regSymbol).second)
                {
                    setRejectReason(reasonOut, "concat leaves do not map to distinct register symbols");
                    return std::nullopt;
                }
                const OperationId regOpId = graph.findOperation(*regSymbol);
                if (!regOpId.valid())
                {
                    setRejectReason(reasonOut, "register symbol does not resolve to an operation");
                    return std::nullopt;
                }
                const Operation regOp = graph.getOperation(regOpId);
                if (regOp.kind() != OperationKind::kRegister)
                {
                    setRejectReason(reasonOut, "register read target is not a kRegister op");
                    return std::nullopt;
                }
                const int32_t width = static_cast<int32_t>(getAttr<int64_t>(regOp, "width").value_or(0));
                const bool isSigned = getAttr<bool>(regOp, "isSigned").value_or(false);
                if (width <= 0)
                {
                    setRejectReason(reasonOut, "register width is not positive");
                    return std::nullopt;
                }
                if (readShape.members.empty())
                {
                    clusterWidth = width;
                    clusterSigned = isSigned;
                }
                else if (clusterWidth != width || clusterSigned != isSigned)
                {
                    setRejectReason(reasonOut, "register cluster width/sign does not match across leaves");
                    return std::nullopt;
                }
                readShape.members.push_back(ClusterMember{
                    .symbol = *regSymbol,
                    .regOp = regOpId,
                    .readOp = readOpId,
                    .readResult = readValue,
                    .index = static_cast<int64_t>(readShape.members.size())});
            }
            return readShape;
        }

        int32_t addressWidthForRows(std::size_t rowCount)
        {
            int32_t width = 1;
            while ((uint64_t{1} << width) < std::max<std::size_t>(rowCount, 1))
            {
                ++width;
            }
            return width;
        }

        std::optional<CandidateCluster> analyzeNamedSegmentCandidate(const Graph &graph,
                                                                     const NamedSegment &segment,
                                                                     const FlatConcatInfo &readShape,
                                                                     const ReadWriteRefs &refs,
                                                                     std::string *reasonOut)
        {
            if (segment.members.size() < 4)
            {
                setRejectReason(reasonOut, "named segment has fewer than 4 members");
                return std::nullopt;
            }
            if (segment.members.size() != readShape.members.size())
            {
                setRejectReason(reasonOut, "named segment size does not match concat-driven read sequence");
                return std::nullopt;
            }
            if (makeMemberSequenceKey(segment.members) != makeMemberSequenceKey(readShape.members))
            {
                setRejectReason(reasonOut, "named segment does not match concat-driven read sequence");
                return std::nullopt;
            }

            CandidateCluster candidate{
                .key = segment.key,
                .indexBase = segment.indexBase,
                .members = readShape.members,
            };
            candidate.readPlan =
                analyzeReadPlan(graph,
                                candidate,
                                refs,
                                readShape.concatOpId,
                                readShape.flattenedLeaves,
                                readShape.concatOps,
                                reasonOut);
            if (!candidate.readPlan.concatOp.valid())
            {
                return std::nullopt;
            }
            if (!analyzeFillGroups(graph, candidate, refs, candidate.fillGroups, reasonOut))
            {
                return std::nullopt;
            }
            if (!analyzePointWriteGroups(graph, candidate, refs, candidate.pointWriteGroups, reasonOut))
            {
                return std::nullopt;
            }
            if (candidate.pointWriteGroups.size() > 1)
            {
                setRejectReason(reasonOut, "multiple dynamic point-write groups require ordered same-memory commits");
                return std::nullopt;
            }
            if (!candidate.pointWriteGroups.empty() && pointWriteDataDependsOnPackedReads(graph, candidate))
            {
                setRejectReason(reasonOut, "dynamic point-write data depends on packed register reads");
                return std::nullopt;
            }
            if (candidate.fillGroups.empty() && candidate.pointWriteGroups.empty())
            {
                setRejectReason(reasonOut, "cluster has neither complete fill groups nor complete point-write groups");
                return std::nullopt;
            }
            return candidate;
        }

        void setMemoryInitAttrs(Graph &graph, OperationId memoryOp, const CandidateCluster &candidate)
        {
            std::vector<std::string> initKind;
            std::vector<std::string> initFile;
            std::vector<std::string> initValue;
            std::vector<int64_t> initStart;
            std::vector<int64_t> initLen;

            for (const ClusterMember &member : candidate.members)
            {
                const Operation regOp = graph.getOperation(member.regOp);
                const auto init = getAttr<std::string>(regOp, "initValue");
                if (!init)
                {
                    continue;
                }
                initKind.push_back("literal");
                initFile.emplace_back();
                initValue.push_back(*init);
                initStart.push_back(member.index);
                initLen.push_back(1);
            }

            if (!initKind.empty())
            {
                graph.setAttr(memoryOp, "initKind", initKind);
                graph.setAttr(memoryOp, "initFile", initFile);
                graph.setAttr(memoryOp, "initValue", initValue);
                graph.setAttr(memoryOp, "initStart", initStart);
                graph.setAttr(memoryOp, "initLen", initLen);
            }
        }

        ValueId createReverseIndex(Graph &graph, ValueId originalIndex, std::size_t rowCount)
        {
            const int32_t indexWidth = graph.valueWidth(originalIndex);
            const ValueId maxIndex = createConstantValue(graph,
                                                         indexWidth,
                                                         false,
                                                         std::to_string(indexWidth) + "'d" + std::to_string(rowCount - 1u),
                                                         "reverse_index_const");
            return createBinaryOp(graph,
                                  OperationKind::kSub,
                                  maxIndex,
                                  originalIndex,
                                  indexWidth,
                                  false,
                                  "reverse_index");
        }

        std::string rewriteCandidate(Graph &graph, const CandidateCluster &candidate)
        {
            const std::string memoryName = makeUniqueMemoryName(graph, candidate.key);
            const SymbolId memorySym = graph.internSymbol(memoryName);
            const OperationId memoryOp = graph.createOperation(OperationKind::kMemory, memorySym);
            graph.setAttr(memoryOp, "width", static_cast<int64_t>(candidate.key.width));
            graph.setAttr(memoryOp, "row", static_cast<int64_t>(candidate.members.size()));
            graph.setAttr(memoryOp, "isSigned", candidate.key.isSigned);
            graph.setOpSrcLoc(memoryOp, makeTransformSrcLoc("scalar-memory-pack", "packed_memory_decl"));
            setMemoryInitAttrs(graph, memoryOp, candidate);

            bool preserveDeclared = false;
            for (const ClusterMember &member : candidate.members)
            {
                if (graph.isDeclaredSymbol(graph.operationSymbol(member.regOp)))
                {
                    preserveDeclared = true;
                    break;
                }
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(memorySym);
            }

            auto rewriteReadConsumers = [&]() {
                for (const ReadRewritePlan::Consumer &consumer : candidate.readPlan.consumers)
                {
                    if (!operationExists(graph, consumer.eraseOp))
                    {
                        continue;
                    }
                    ValueId addr = consumer.addr;
                    if (consumer.constIndex >= 0)
                    {
                        const int32_t addrWidth = addressWidthForRows(candidate.members.size());
                        addr = createConstantValue(graph,
                                                   addrWidth,
                                                   false,
                                                   std::to_string(addrWidth) + "'d" + std::to_string(consumer.constIndex),
                                                   "direct_memory_read_index");
                    }
                    if (!addr.valid())
                    {
                        throw std::runtime_error("scalar-memory-pack internal error: memory read address is invalid");
                    }
                    if (consumer.reverseAddr)
                    {
                        addr = createReverseIndex(graph, addr, candidate.members.size());
                    }
                    addr = createAdjustedAddress(graph,
                                                 addr,
                                                 consumer.reverseAddr ? -consumer.rowOffset : consumer.rowOffset,
                                                 "read_row_offset");
                    const OperationId readOp = graph.createOperation(OperationKind::kMemoryReadPort, graph.makeInternalOpSym());
                    graph.setAttr(readOp, "memSymbol", memoryName);
                    graph.addOperand(readOp, addr);
                    const ValueId newResult = graph.createValue(graph.makeInternalValSym(),
                                                                candidate.key.width,
                                                                candidate.key.isSigned,
                                                                ValueType::Logic);
                    graph.addResult(readOp, newResult);
                    const SrcLoc srcLoc = makeTransformSrcLoc("scalar-memory-pack", "dynamic_memory_read");
                    graph.setOpSrcLoc(readOp, srcLoc);
                    graph.setValueSrcLoc(newResult, srcLoc);
                    replacePortBinding(graph, consumer.oldResult, newResult);
                    graph.eraseOp(consumer.eraseOp, std::array<ValueId, 1>{newResult});
                }
            };

            const bool concatDrivenRead = candidate.readPlan.concatOp.valid();
            if (concatDrivenRead)
            {
                rewriteReadConsumers();

                for (OperationId concatOpId : candidate.readPlan.concatOps)
                {
                    if (operationExists(graph, concatOpId))
                    {
                        graph.eraseOp(concatOpId);
                    }
                }
                if (!candidate.readPlan.concatOps.empty())
                {
                    for (const ClusterMember &member : candidate.members)
                    {
                        if (operationExists(graph, member.readOp))
                        {
                            graph.eraseOp(member.readOp);
                        }
                    }
                }
            }

            const auto orderedWriteGroups = buildOrderedWriteGroups(candidate);
            for (const OrderedWriteGroupRef &group : orderedWriteGroups)
            {
                if (group.kind == OrderedWriteGroupRef::Kind::Point)
                {
                    const auto &[key, opIds] = candidate.pointWriteGroups[group.index];
                    (void)opIds;
                    const OperationId writeOp =
                        graph.createOperation(OperationKind::kMemoryWritePort, graph.makeInternalOpSym());
                    graph.setAttr(writeOp, "memSymbol", memoryName);
                    graph.setAttr(writeOp, "eventEdge", key.eventEdges);
                    graph.addOperand(writeOp,
                                     createLogicTree(graph, key.baseTerms, OperationKind::kLogicAnd, "point_write_cond", true));
                    graph.addOperand(writeOp,
                                     createAdjustedAddress(graph,
                                                           key.addr,
                                                           -candidate.indexBase,
                                                           "point_write_index_base"));
                    graph.addOperand(writeOp, materializeDataRef(graph, key.data, "point_write_data"));
                    graph.addOperand(writeOp, key.mask);
                    for (ValueId eventValue : key.eventValues)
                    {
                        graph.addOperand(writeOp, eventValue);
                    }
                    graph.setOpSrcLoc(writeOp, makeTransformSrcLoc("scalar-memory-pack", "dynamic_memory_write"));
                    continue;
                }

                const auto &[key, opIds] = candidate.fillGroups[group.index];
                (void)opIds;
                const OperationId fillOp = graph.createOperation(OperationKind::kMemoryFillPort, graph.makeInternalOpSym());
                graph.setAttr(fillOp, "memSymbol", memoryName);
                graph.setAttr(fillOp, "eventEdge", key.eventEdges);
                graph.addOperand(fillOp, createLogicTree(graph, key.condBranches, OperationKind::kLogicOr, "fill_cond", false));
                graph.addOperand(fillOp, materializeDataRef(graph, key.data, "fill_data"));
                for (ValueId eventValue : key.eventValues)
                {
                    graph.addOperand(fillOp, eventValue);
                }
                graph.setOpSrcLoc(fillOp, makeTransformSrcLoc("scalar-memory-pack", "memory_fill"));
            }

            std::unordered_set<OperationId, OperationIdHash> erasedWrites;
            std::vector<ValueId> pruneSeeds;
            for (const auto &[key, opIds] : candidate.pointWriteGroups)
            {
                (void)key;
                for (OperationId oldWrite : opIds)
                {
                    erasedWrites.insert(oldWrite);
                }
            }
            for (const auto &[key, opIds] : candidate.fillGroups)
            {
                (void)key;
                for (OperationId oldWrite : opIds)
                {
                    erasedWrites.insert(oldWrite);
                }
            }
            for (OperationId oldWrite : erasedWrites)
            {
                if (!operationExists(graph, oldWrite))
                {
                    continue;
                }
                const Operation oldWriteOp = graph.getOperation(oldWrite);
                pruneSeeds.insert(pruneSeeds.end(), oldWriteOp.operands().begin(), oldWriteOp.operands().end());
                graph.eraseOp(oldWrite);
            }
            if (!concatDrivenRead)
            {
                rewriteReadConsumers();
            }
            for (ValueId seed : pruneSeeds)
            {
                pruneDeadValueDef(graph, seed);
            }
            for (const ClusterMember &member : candidate.members)
            {
                if (operationExists(graph, member.regOp))
                {
                    graph.eraseOp(member.regOp);
                }
            }
            return memoryName;
        }
    } // namespace

    ScalarMemoryPackPass::ScalarMemoryPackPass()
        : Pass("scalar-memory-pack",
               "scalar-memory-pack",
               "Recover scalar register clusters into indexed memory access plus fill")
    {
    }

        PassResult ScalarMemoryPackPass::run()
        {
        PassResult result;
        std::size_t graphCount = 0;
        std::size_t candidateClusterCount = 0;
        std::size_t candidateMemberCount = 0;
        std::size_t rewrittenClusterCount = 0;
        std::size_t rewrittenMemberCount = 0;
        std::unordered_map<std::string, RejectAggregate> rejectAggregates;
        ScalarMemoryPackReport report;

        for (const auto &entry : design().graphs())
        {
            ++graphCount;
            Graph &graph = *entry.second;
            const ReadWriteRefs refs = collectReadWriteRefs(graph);
            std::unordered_set<OperationId, OperationIdHash> claimedRegs;
            std::unordered_map<std::string, std::vector<FlatConcatInfo>> readShapesBySequence;
            for (const OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.kind() != OperationKind::kConcat)
                {
                    continue;
                }

                std::string readShapeReason;
                auto readShape = analyzeConcatReadShape(graph, opId, &readShapeReason);
                if (!readShape)
                {
                    continue;
                }
                readShapesBySequence[makeMemberSequenceKey(readShape->members)].push_back(std::move(*readShape));
            }

            std::vector<CandidateCluster> acceptedCandidates;
            const std::vector<NamedSegment> segments = collectNamedSegments(graph, refs);
            for (const NamedSegment &segment : segments)
            {
                bool overlap = false;
                for (const ClusterMember &member : segment.members)
                {
                    if (claimedRegs.contains(member.regOp))
                    {
                        overlap = true;
                        break;
                    }
                }
                if (overlap)
                {
                    continue;
                }

                const std::string sequenceKey = makeMemberSequenceKey(segment.members);
                const auto readShapeIt = readShapesBySequence.find(sequenceKey);
                if (readShapeIt == readShapesBySequence.end())
                {
                    RejectAggregate &aggregate = rejectAggregates["no matching concat-driven packed read sequence"];
                    ++aggregate.rejectCount;
                    aggregate.memberCount += segment.members.size();
                    addRejectExample(aggregate.examples, segment.members.front().symbol + " ...");
                    continue;
                }

                std::optional<CandidateCluster> accepted;
                std::string lastRejectReason;
                for (const FlatConcatInfo &readShape : readShapeIt->second)
                {
                    std::string rejectReason;
                    auto candidate = analyzeNamedSegmentCandidate(graph, segment, readShape, refs, &rejectReason);
                    if (candidate)
                    {
                        accepted = std::move(candidate);
                        break;
                    }
                    lastRejectReason = rejectReason;
                }
                if (!accepted)
                {
                    const std::string rawReason = lastRejectReason.empty() ? std::string("unknown") : lastRejectReason;
                    const std::string reason = normalizeRejectReason(rawReason);
                    RejectAggregate &aggregate = rejectAggregates[reason];
                    ++aggregate.rejectCount;
                    aggregate.memberCount += segment.members.size();
                    addRejectExample(aggregate.examples, segment.members.front().symbol + " => " + rawReason);
                    continue;
                }

                for (const ClusterMember &member : accepted->members)
                {
                    claimedRegs.insert(member.regOp);
                }
                ++candidateClusterCount;
                candidateMemberCount += accepted->members.size();
                acceptedCandidates.push_back(std::move(*accepted));
            }

            for (const CandidateCluster &candidate : acceptedCandidates)
            {
                const std::string memoryName = rewriteCandidate(graph, candidate);
                result.changed = true;
                ++rewrittenClusterCount;
                rewrittenMemberCount += candidate.members.size();
                report.addCluster(graph, candidate, memoryName, rewrittenClusterCount - 1);
            }
        }

        logInfo("scalar-memory-pack: graphs=" + std::to_string(graphCount) +
                " candidate_clusters=" + std::to_string(candidateClusterCount) +
                " candidate_members=" + std::to_string(candidateMemberCount) +
                " rewritten_clusters=" + std::to_string(rewrittenClusterCount) +
                " rewritten_members=" + std::to_string(rewrittenMemberCount) +
                " mode=named_segment_plus_concat_driven_dynamic_read_write_plus_fill");
        if (!rejectAggregates.empty())
        {
            std::size_t totalRejects = 0;
            std::size_t totalRejectedMembers = 0;
            std::vector<std::pair<std::string, RejectAggregate *>> sortedRejects;
            sortedRejects.reserve(rejectAggregates.size());
            for (auto &[reason, aggregate] : rejectAggregates)
            {
                totalRejects += aggregate.rejectCount;
                totalRejectedMembers += aggregate.memberCount;
                sortedRejects.push_back({reason, &aggregate});
            }
            std::sort(sortedRejects.begin(),
                      sortedRejects.end(),
                      [](const auto &lhs, const auto &rhs) {
                          if (lhs.second->rejectCount != rhs.second->rejectCount)
                          {
                              return lhs.second->rejectCount > rhs.second->rejectCount;
                          }
                          if (lhs.second->memberCount != rhs.second->memberCount)
                          {
                              return lhs.second->memberCount > rhs.second->memberCount;
                          }
                          return lhs.first < rhs.first;
                      });
            logInfo("scalar-memory-pack reject-summary: unique_reasons=" + std::to_string(sortedRejects.size()) +
                    " total_rejects=" + std::to_string(totalRejects) +
                    " total_rejected_members=" + std::to_string(totalRejectedMembers));
            constexpr std::size_t kRejectSummaryLimit = 20;
            const std::size_t limit = std::min(kRejectSummaryLimit, sortedRejects.size());
            for (std::size_t i = 0; i < limit; ++i)
            {
                const auto &[reason, aggregate] = sortedRejects[i];
                logInfo("scalar-memory-pack reject-summary[" + std::to_string(i) +
                        "]: rejects=" + std::to_string(aggregate->rejectCount) +
                        " members=" + std::to_string(aggregate->memberCount) +
                        " reason=" + reason +
                        " examples=" + formatRejectExamples(aggregate->examples));
            }
        }
        report.finish(graphCount, candidateClusterCount, candidateMemberCount, rewrittenClusterCount);
        return result;
    }

} // namespace wolvrix::lib::transform
