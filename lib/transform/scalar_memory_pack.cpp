#include "transform/scalar_memory_pack.hpp"

#include "core/grh.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
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
                OperationId opId;
                ValueId addr;
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
            ValueId data;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;

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
                hashCombine(seed, hashValueId(key.data));
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
            ValueId data;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;

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
                hashCombine(seed, hashValueId(key.data));
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
            ValueId data;
        };

        struct ParsedPointArm
        {
            std::vector<ValueId> baseTerms;
            ValueId addr;
            int64_t constIndex = -1;
            ValueId data;
        };

        struct ParsedWritePattern
        {
            OperationId opId;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;
            std::optional<ParsedFillArm> fill;
            std::vector<ParsedPointArm> points;
        };

        struct CandidateCluster
        {
            ClusterKey key;
            std::vector<ClusterMember> members;
            ReadRewritePlan readPlan;
            std::vector<std::pair<FillGroupKey, std::vector<OperationId>>> fillGroups;
            std::vector<std::pair<PointWriteKey, std::vector<OperationId>>> pointWriteGroups;
        };

        struct RejectAggregate
        {
            std::size_t rejectCount = 0;
            std::size_t memberCount = 0;
            std::vector<std::string> examples;
        };

        struct ReadWriteRefs
        {
            std::unordered_map<std::string, std::vector<OperationId>> readPortsBySymbol;
            std::unordered_map<std::string, std::vector<OperationId>> writePortsBySymbol;
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

        struct PointCondInfo
        {
            std::vector<ValueId> baseTerms;
            ValueId addr;
            int64_t constIndex = -1;
        };

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

        std::size_t estimateFlattenedConcatLeafCount(const Graph &graph, OperationId opId)
        {
            try
            {
                if (graph.getOperation(opId).kind() != OperationKind::kConcat)
                {
                    return 0;
                }
            }
            catch (const std::runtime_error &)
            {
                return 0;
            }

            std::vector<ValueId> leaves;
            std::vector<OperationId> concatOps;
            flattenConcatLeaves(graph, opId, leaves, concatOps);
            return leaves.size();
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
            for (ValueId readValue : rowValues)
            {
                const Value value = graph.getValue(readValue);
                if (value.users().size() != 1 || !concatOpSet.contains(value.users().front().operation))
                {
                    setRejectReason(reasonOut, "register read has non-concat users");
                    return {};
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
                        .opId = user.operation,
                        .addr = sliceOp.operands()[1],
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
                        .opId = user.operation,
                        .addr = indexInfo->addr,
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

        std::optional<std::pair<ValueId, ValueId>> parseMuxArms(const Graph &graph, ValueId value, ValueId cond)
        {
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kMux || defOp.operands().size() != 3)
            {
                return std::nullopt;
            }
            if (defOp.operands()[0] == cond)
            {
                return std::pair<ValueId, ValueId>{defOp.operands()[1], defOp.operands()[2]};
            }
            const OperationId condDefId = graph.valueDef(defOp.operands()[0]);
            if (condDefId.valid())
            {
                const Operation condDef = graph.getOperation(condDefId);
                if (condDef.kind() == OperationKind::kLogicNot &&
                    condDef.operands().size() == 1 &&
                    condDef.operands()[0] == cond)
                {
                    return std::pair<ValueId, ValueId>{defOp.operands()[2], defOp.operands()[1]};
                }
            }
            return std::nullopt;
        }

        struct PointCondBranch
        {
            ValueId cond;
            PointCondInfo info;
        };

        bool parsePointDataTree(const Graph &graph,
                                ValueId value,
                                std::span<const PointCondBranch> branches,
                                std::vector<ParsedPointArm> &pointsOut,
                                ValueId &defaultDataOut,
                                bool allowImplicitLastPointArm)
        {
            if (branches.empty())
            {
                defaultDataOut = value;
                return true;
            }

            if (allowImplicitLastPointArm && branches.size() == 1)
            {
                pointsOut.push_back(ParsedPointArm{
                    .baseTerms = branches.front().info.baseTerms,
                    .addr = branches.front().info.addr,
                    .constIndex = branches.front().info.constIndex,
                    .data = value});
                defaultDataOut = ValueId::invalid();
                return true;
            }

            for (std::size_t i = 0; i < branches.size(); ++i)
            {
                const auto muxArms = parseMuxArms(graph, value, branches[i].cond);
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
                ValueId tailDefault = ValueId::invalid();
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
                    .baseTerms = branches[i].info.baseTerms,
                    .addr = branches[i].info.addr,
                    .constIndex = branches[i].info.constIndex,
                    .data = muxArms->first});
                pointsOut.insert(pointsOut.end(), tailPoints.begin(), tailPoints.end());
                defaultDataOut = tailDefault;
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
                    });
                }
                else
                {
                    fillBranches.push_back(condBranch);
                }
            }

            if (pointBranches.empty())
            {
                canonicalizeValues(condBranches);
                pattern.fill = ParsedFillArm{
                    .condBranches = std::move(condBranches),
                    .data = op.operands()[1]};
                return pattern;
            }

            if (pointBranches.size() == 1 && fillBranches.empty())
            {
                pattern.points.push_back(ParsedPointArm{
                    .baseTerms = std::move(pointBranches.front().info.baseTerms),
                    .addr = pointBranches.front().info.addr,
                    .constIndex = pointBranches.front().info.constIndex,
                    .data = op.operands()[1]});
                return pattern;
            }

            std::vector<ParsedPointArm> parsedPoints;
            ValueId defaultData = ValueId::invalid();
            if (!parsePointDataTree(graph,
                                    op.operands()[1],
                                    std::span<const PointCondBranch>(pointBranches.data(), pointBranches.size()),
                                    parsedPoints,
                                    defaultData,
                                    fillBranches.empty()))
            {
                setRejectReason(reasonOut,
                                fillBranches.empty()
                                    ? "multiple point-update branches do not encode nextValue as a mux tree over point conditions"
                                    : "mixed fill/point write does not encode nextValue as a mux tree over point conditions");
                return std::nullopt;
            }

            pattern.points = std::move(parsedPoints);
            if (!fillBranches.empty())
            {
                canonicalizeValues(fillBranches);
                pattern.fill = ParsedFillArm{
                    .condBranches = std::move(fillBranches),
                    .data = defaultData};
            }
            return pattern;
        }

        bool analyzeFillGroups(const Graph &graph,
                               const CandidateCluster &candidate,
                               const ReadWriteRefs &refs,
                               std::vector<std::pair<FillGroupKey, std::vector<OperationId>>> &groupsOut,
                               std::string *reasonOut)
        {
            std::unordered_map<FillGroupKey, std::vector<std::pair<std::string, OperationId>>, FillGroupKeyHash> groups;
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
                    if (!parsed->fill)
                    {
                        continue;
                    }
                    FillGroupKey key{
                        .condBranches = parsed->fill->condBranches,
                        .data = parsed->fill->data,
                        .mask = parsed->mask,
                        .eventValues = parsed->eventValues,
                        .eventEdges = parsed->eventEdges};
                    groups[key].push_back({member.symbol, opId});
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
                            .eventEdges = parsed->eventEdges};
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
                        memberIndexIt->second != info.constIndex ||
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

        std::optional<CandidateCluster> analyzeConcatCandidate(const Graph &graph,
                                                               OperationId concatOpId,
                                                               const ReadWriteRefs &refs,
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

            CandidateCluster candidate;
            candidate.key.prefix = std::string(concatOp.symbolText());
            candidate.key.suffix.clear();
            std::unordered_set<std::string> seenSymbols;
            candidate.members.reserve(flattenedLeaves.size());
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
                if (candidate.members.empty())
                {
                    candidate.key.width = width;
                    candidate.key.isSigned = isSigned;
                }
                else if (candidate.key.width != width || candidate.key.isSigned != isSigned)
                {
                    setRejectReason(reasonOut, "register cluster width/sign does not match across leaves");
                    return std::nullopt;
                }
                candidate.members.push_back(ClusterMember{
                    .symbol = *regSymbol,
                    .regOp = regOpId,
                    .readOp = readOpId,
                    .readResult = readValue,
                    .index = static_cast<int64_t>(candidate.members.size())});
            }

            candidate.readPlan = analyzeReadPlan(graph, candidate, concatOpId, flattenedLeaves, concatOps, reasonOut);
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

        void rewriteCandidate(Graph &graph, const CandidateCluster &candidate)
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

            for (const ReadRewritePlan::Consumer &consumer : candidate.readPlan.consumers)
            {
                const Operation sliceOp = graph.getOperation(consumer.opId);
                const ValueId oldResult = sliceOp.results().front();
                ValueId addr = consumer.addr;
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
                replacePortBinding(graph, oldResult, newResult);
                graph.eraseOp(consumer.opId, std::array<ValueId, 1>{newResult});
            }

            for (OperationId concatOpId : candidate.readPlan.concatOps)
            {
                graph.eraseOp(concatOpId);
            }
            for (const ClusterMember &member : candidate.members)
            {
                graph.eraseOp(member.readOp);
            }

            for (const auto &[key, opIds] : candidate.pointWriteGroups)
            {
                const OperationId writeOp = graph.createOperation(OperationKind::kMemoryWritePort, graph.makeInternalOpSym());
                graph.setAttr(writeOp, "memSymbol", memoryName);
                graph.setAttr(writeOp, "eventEdge", key.eventEdges);
                graph.addOperand(writeOp, createLogicTree(graph, key.baseTerms, OperationKind::kLogicAnd, "point_write_cond", true));
                graph.addOperand(writeOp, key.addr);
                graph.addOperand(writeOp, key.data);
                graph.addOperand(writeOp, key.mask);
                for (ValueId eventValue : key.eventValues)
                {
                    graph.addOperand(writeOp, eventValue);
                }
                graph.setOpSrcLoc(writeOp, makeTransformSrcLoc("scalar-memory-pack", "dynamic_memory_write"));
            }

            for (const auto &[key, opIds] : candidate.fillGroups)
            {
                const OperationId fillOp = graph.createOperation(OperationKind::kMemoryFillPort, graph.makeInternalOpSym());
                graph.setAttr(fillOp, "memSymbol", memoryName);
                graph.setAttr(fillOp, "eventEdge", key.eventEdges);
                graph.addOperand(fillOp, createLogicTree(graph, key.condBranches, OperationKind::kLogicOr, "fill_cond", false));
                graph.addOperand(fillOp, key.data);
                for (ValueId eventValue : key.eventValues)
                {
                    graph.addOperand(fillOp, eventValue);
                }
                graph.setOpSrcLoc(fillOp, makeTransformSrcLoc("scalar-memory-pack", "memory_fill"));
            }

            std::unordered_set<OperationId, OperationIdHash> erasedWrites;
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
                graph.eraseOp(oldWrite);
            }
            for (const ClusterMember &member : candidate.members)
            {
                graph.eraseOp(member.regOp);
            }
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

        for (const auto &entry : design().graphs())
        {
            ++graphCount;
            Graph &graph = *entry.second;
            const ReadWriteRefs refs = collectReadWriteRefs(graph);
            std::unordered_set<OperationId, OperationIdHash> claimedRegs;
            std::vector<OperationId> concatOps;

            for (const OperationId opId : graph.operations())
            {
                if (graph.getOperation(opId).kind() == OperationKind::kConcat)
                {
                    concatOps.push_back(opId);
                }
            }

            for (const OperationId opId : concatOps)
            {
                try
                {
                    (void)graph.getOperation(opId);
                }
                catch (const std::runtime_error &)
                {
                    continue;
                }
                std::string rejectReason;
                auto candidate = analyzeConcatCandidate(graph, opId, refs, &rejectReason);
                if (!candidate)
                {
                    const std::string rawReason =
                        rejectReason.empty() ? std::string("unknown") : std::move(rejectReason);
                    const std::string reason = normalizeRejectReason(rawReason);
                    RejectAggregate &aggregate = rejectAggregates[reason];
                    ++aggregate.rejectCount;
                    aggregate.memberCount += estimateFlattenedConcatLeafCount(graph, opId);
                    addRejectExample(aggregate.examples,
                                     std::string(graph.getOperation(opId).symbolText()) + " => " + rawReason);
                    if (matchesTraceFilter(graph, opId))
                    {
                        logInfo("scalar-memory-pack trace: graph=" + std::string(graph.symbol()) +
                                " concat=" + std::string(graph.getOperation(opId).symbolText()) +
                                " reject=" + rawReason);
                    }
                    continue;
                }
                if (matchesTraceFilter(graph, opId))
                {
                    logInfo("scalar-memory-pack trace: graph=" + std::string(graph.symbol()) +
                            " concat=" + std::string(graph.getOperation(opId).symbolText()) +
                            " accept members=" + std::to_string(candidate->members.size()) +
                            " fill_groups=" + std::to_string(candidate->fillGroups.size()) +
                            " point_groups=" + std::to_string(candidate->pointWriteGroups.size()));
                }
                bool overlap = false;
                for (const ClusterMember &member : candidate->members)
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
                for (const ClusterMember &member : candidate->members)
                {
                    claimedRegs.insert(member.regOp);
                }
                ++candidateClusterCount;
                candidateMemberCount += candidate->members.size();
                rewriteCandidate(graph, *candidate);
                result.changed = true;
                ++rewrittenClusterCount;
                rewrittenMemberCount += candidate->members.size();
            }
        }

        logInfo("scalar-memory-pack: graphs=" + std::to_string(graphCount) +
                " candidate_clusters=" + std::to_string(candidateClusterCount) +
                " candidate_members=" + std::to_string(candidateMemberCount) +
                " rewritten_clusters=" + std::to_string(rewrittenClusterCount) +
                " rewritten_members=" + std::to_string(rewrittenMemberCount) +
                " mode=dynamic_read_write_plus_fill");
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
        return result;
    }

} // namespace wolvrix::lib::transform
