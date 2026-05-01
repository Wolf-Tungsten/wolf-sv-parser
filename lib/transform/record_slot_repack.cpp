#include "transform/record_slot_repack.hpp"

#include "core/grh.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
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
                const char *value = std::getenv("WOLVRIX_RECORD_SLOT_REPACK_TRACE_SUBSTR");
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
            for (ValueId operand : op.operands())
            {
                const OperationId defOpId = graph.valueDef(operand);
                if (!defOpId.valid())
                {
                    continue;
                }
                const Operation defOp = graph.getOperation(defOpId);
                if (matchesTraceFilter(defOp.symbolText()))
                {
                    return true;
                }
                if (auto reg = getAttr<std::string>(defOp, "regSymbol"))
                {
                    if (matchesTraceFilter(*reg))
                    {
                        return true;
                    }
                }
                for (ValueId result : defOp.results())
                {
                    if (matchesTraceFilter(graph.getValue(result).symbolText()))
                    {
                        return true;
                    }
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

        struct ValueIdHash
        {
            std::size_t operator()(ValueId value) const noexcept { return hashValueId(value); }
        };

        struct EqConstInfo
        {
            ValueId addr;
            int64_t constIndex = -1;
        };

        struct PointCondInfo
        {
            struct ExcludeGuard
            {
                std::vector<ValueId> baseTerms;
                ValueId addr;

                bool operator==(const ExcludeGuard &) const = default;
            };

            std::vector<ValueId> baseTerms;
            ValueId addr;
            int64_t constIndex = -1;
            std::vector<ExcludeGuard> excludeGuards;

            bool operator==(const PointCondInfo &) const = default;
        };

        struct SlotMember
        {
            int64_t slotIndex = -1;
            std::string regSymbol;
            OperationId regOp;
            OperationId readOp;
            ValueId readValue;
        };

        struct ReadConsumer
        {
            OperationId opId;
            ValueId addr;
            bool reverseAddr = false;
            int64_t rowOffset = 0;
        };

        struct ReadPlan
        {
            enum class Kind
            {
                kMuxRoot,
                kConcatSlices,
            };

            Kind kind = Kind::kMuxRoot;
            ValueId rootValue;
            OperationId rootOp;
            ValueId readAddr;
            std::vector<ReadConsumer> consumers;
            std::vector<OperationId> concatOps;
        };

        struct WriteShape
        {
            std::vector<PointCondInfo::ExcludeGuard> excludeGuards;
            std::vector<ValueId> baseTerms;
            ValueId addr;
            ValueId data;
            ValueId mask;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;
        };

        struct FillShape
        {
            ValueId cond;
            ValueId data;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;
        };

        struct CandidateFamily
        {
            int32_t width = 0;
            bool isSigned = false;
            std::vector<SlotMember> members;
            ReadPlan readPlan;
            std::vector<WriteShape> pointWrites;
            std::optional<FillShape> fillWrite;
        };

        struct PointAccessKey
        {
            std::vector<PointCondInfo::ExcludeGuard> excludeGuards;
            std::vector<ValueId> baseTerms;
            ValueId addr;
            std::vector<ValueId> eventValues;
            std::vector<std::string> eventEdges;

            bool operator==(const PointAccessKey &) const = default;
        };

        struct GroupKey
        {
            std::vector<PointAccessKey> pointWrites;
            std::optional<ValueId> fillCond;
            std::vector<ValueId> fillEvents;
            std::vector<std::string> fillEdges;
            std::size_t rowCount = 0;

            bool operator==(const GroupKey &) const = default;
        };

        struct GroupKeyHash
        {
            std::size_t operator()(const GroupKey &key) const noexcept
            {
                std::size_t seed = 0;
                for (const PointAccessKey &point : key.pointWrites)
                {
                    for (const auto &guard : point.excludeGuards)
                    {
                        for (ValueId term : guard.baseTerms)
                        {
                            hashCombine(seed, hashValueId(term));
                        }
                        hashCombine(seed, hashValueId(guard.addr));
                    }
                    for (ValueId term : point.baseTerms)
                    {
                        hashCombine(seed, hashValueId(term));
                    }
                    hashCombine(seed, hashValueId(point.addr));
                    for (ValueId value : point.eventValues)
                    {
                        hashCombine(seed, hashValueId(value));
                    }
                    for (const auto &edge : point.eventEdges)
                    {
                        hashCombine(seed, edge);
                    }
                }
                hashCombine(seed, key.fillCond ? hashValueId(*key.fillCond) : 0u);
                for (ValueId value : key.fillEvents)
                {
                    hashCombine(seed, hashValueId(value));
                }
                for (const auto &edge : key.fillEdges)
                {
                    hashCombine(seed, edge);
                }
                hashCombine(seed, key.rowCount);
                return seed;
            }
        };

        struct ReadWriteRefs
        {
            std::unordered_map<std::string, std::vector<OperationId>> readPortsBySymbol;
            std::unordered_map<std::string, std::vector<OperationId>> writePortsBySymbol;
        };

        bool isContainedInWriteCone(const Graph &graph,
                                    OperationId opId,
                                    const std::unordered_set<OperationId, OperationIdHash> &writeOps,
                                    std::unordered_map<OperationId, bool, OperationIdHash> &memo,
                                    std::unordered_set<OperationId, OperationIdHash> &visiting);

        ReadWriteRefs collectReadWriteRefs(const Graph &graph)
        {
            ReadWriteRefs refs;
            for (const OperationId opId : graph.operations())
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

        std::optional<int64_t> parseConstIntLiteral(std::string_view literal)
        {
            if (literal.empty())
            {
                return std::nullopt;
            }
            if (literal.front() == '"' || literal.front() == '$')
            {
                return std::nullopt;
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
                return std::nullopt;
            }

            int base = 10;
            std::string digits;
            const std::size_t tick = cleaned.find('\'');
            if (tick != std::string::npos)
            {
                if (tick + 2 >= cleaned.size())
                {
                    return std::nullopt;
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
                    return std::nullopt;
                }
                digits = cleaned.substr(tick + 2);
            }
            else
            {
                digits = cleaned;
            }
            if (digits.empty())
            {
                return std::nullopt;
            }
            for (char ch : digits)
            {
                if (ch == 'x' || ch == 'z' || ch == '?')
                {
                    return std::nullopt;
                }
            }
            try
            {
                return std::stoll(digits, nullptr, base);
            }
            catch (const std::exception &)
            {
                return std::nullopt;
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
            return parseConstIntLiteral(*literal);
        }

        ValueId stripAssigns(const Graph &graph, ValueId value)
        {
            ValueId current = value;
            while (current.valid())
            {
                const OperationId defOpId = graph.valueDef(current);
                if (!defOpId.valid())
                {
                    return current;
                }
                const Operation defOp = graph.getOperation(defOpId);
                if (defOp.kind() != OperationKind::kAssign || defOp.operands().size() != 1)
                {
                    return current;
                }
                current = defOp.operands()[0];
            }
            return current;
        }

        void flattenAndTerms(const Graph &graph, ValueId value, std::vector<ValueId> &terms)
        {
            const ValueId stripped = stripAssigns(graph, value);
            const OperationId defOpId = graph.valueDef(stripped);
            if (!defOpId.valid())
            {
                terms.push_back(stripped);
                return;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if ((defOp.kind() == OperationKind::kAnd || defOp.kind() == OperationKind::kLogicAnd) &&
                defOp.operands().size() == 2)
            {
                flattenAndTerms(graph, defOp.operands()[0], terms);
                flattenAndTerms(graph, defOp.operands()[1], terms);
                return;
            }
            terms.push_back(stripped);
        }

        void canonicalizeValues(std::vector<ValueId> &values)
        {
            std::sort(values.begin(), values.end(), [](ValueId lhs, ValueId rhs) {
                return hashValueId(lhs) < hashValueId(rhs);
            });
            values.erase(std::unique(values.begin(), values.end()), values.end());
        }

        std::optional<EqConstInfo> parseEqAddrConst(const Graph &graph, ValueId cond)
        {
            const ValueId stripped = stripAssigns(graph, cond);
            const OperationId defOpId = graph.valueDef(stripped);
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
                return EqConstInfo{.addr = defOp.operands()[0], .constIndex = *rhsConst};
            }
            if (auto lhsConst = constantIntValue(graph, defOp.operands()[0]))
            {
                return EqConstInfo{.addr = defOp.operands()[1], .constIndex = *lhsConst};
            }
            return std::nullopt;
        }

        std::optional<PointCondInfo> parsePointCond(const Graph &graph, ValueId cond)
        {
            std::vector<ValueId> terms;
            flattenAndTerms(graph, cond, terms);
            std::optional<EqConstInfo> eqInfo;
            std::vector<ValueId> baseTerms;
            std::vector<PointCondInfo::ExcludeGuard> excludeGuards;
            for (ValueId term : terms)
            {
                if (auto eq = parseEqAddrConst(graph, term))
                {
                    if (eqInfo)
                    {
                        return std::nullopt;
                    }
                    eqInfo = *eq;
                    continue;
                }
                const ValueId stripped = stripAssigns(graph, term);
                const OperationId defOpId = graph.valueDef(stripped);
                if (defOpId.valid())
                {
                    const Operation defOp = graph.getOperation(defOpId);
                    if ((defOp.kind() == OperationKind::kLogicNot || defOp.kind() == OperationKind::kNot) &&
                        defOp.operands().size() == 1)
                    {
                        if (auto guard = parsePointCond(graph, defOp.operands()[0]))
                        {
                            excludeGuards.push_back(PointCondInfo::ExcludeGuard{
                                .baseTerms = guard->baseTerms,
                                .addr = guard->addr,
                            });
                            continue;
                        }
                    }
                }
                baseTerms.push_back(term);
            }
            if (!eqInfo)
            {
                return std::nullopt;
            }
            canonicalizeValues(baseTerms);
            std::sort(excludeGuards.begin(), excludeGuards.end(), [](const auto &lhs, const auto &rhs) {
                if (lhs.baseTerms.size() != rhs.baseTerms.size())
                {
                    return lhs.baseTerms.size() < rhs.baseTerms.size();
                }
                for (std::size_t i = 0; i < lhs.baseTerms.size(); ++i)
                {
                    if (hashValueId(lhs.baseTerms[i]) != hashValueId(rhs.baseTerms[i]))
                    {
                        return hashValueId(lhs.baseTerms[i]) < hashValueId(rhs.baseTerms[i]);
                    }
                }
                return hashValueId(lhs.addr) < hashValueId(rhs.addr);
            });
            return PointCondInfo{
                .baseTerms = std::move(baseTerms),
                .addr = eqInfo->addr,
                .constIndex = eqInfo->constIndex,
                .excludeGuards = std::move(excludeGuards),
            };
        }

        std::optional<SlotMember> parseLeafRead(const Graph &graph, ValueId value)
        {
            const ValueId stripped = stripAssigns(graph, value);
            const OperationId defOpId = graph.valueDef(stripped);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kRegisterReadPort || defOp.results().size() != 1)
            {
                return std::nullopt;
            }
            const auto regSymbol = getAttr<std::string>(defOp, "regSymbol");
            if (!regSymbol)
            {
                return std::nullopt;
            }
            const OperationId regOpId = graph.findOperation(*regSymbol);
            if (!regOpId.valid() || graph.getOperation(regOpId).kind() != OperationKind::kRegister)
            {
                return std::nullopt;
            }
            return SlotMember{
                .slotIndex = -1,
                .regSymbol = *regSymbol,
                .regOp = regOpId,
                .readOp = defOpId,
                .readValue = stripped,
            };
        }

        bool isTopLevelMuxRoot(const Graph &graph, OperationId opId)
        {
            const Operation op = graph.getOperation(opId);
            if (op.kind() != OperationKind::kMux || op.results().size() != 1)
            {
                return false;
            }
            const Value result = graph.getValue(op.results().front());
            if (result.users().empty())
            {
                return true;
            }
            if (result.users().size() != 1)
            {
                return true;
            }
            const ValueUser user = result.users().front();
            const Operation userOp = graph.getOperation(user.operation);
            return userOp.kind() != OperationKind::kMux || user.operandIndex != 2;
        }

        bool isTopLevelConcatRoot(const Graph &graph, OperationId opId)
        {
            const Operation op = graph.getOperation(opId);
            if (op.kind() != OperationKind::kConcat || op.results().size() != 1)
            {
                return false;
            }
            const Value result = graph.getValue(op.results().front());
            for (const ValueUser &user : result.users())
            {
                if (graph.getOperation(user.operation).kind() == OperationKind::kConcat)
                {
                    return false;
                }
            }
            return true;
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

            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return parseLinearElementIndex(graph, value, rowCount);
            }
            const Operation defOp = graph.getOperation(defOpId);
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
            return parseLinearElementIndex(graph, value, rowCount);
        }

        bool parseSlotReadMuxRec(const Graph &graph,
                                 ValueId value,
                                 ValueId *addrOut,
                                 std::vector<SlotMember> &membersOut,
                                 std::vector<OperationId> &muxOpsOut)
        {
            if (auto leaf = parseLeafRead(graph, value))
            {
                membersOut.push_back(std::move(*leaf));
                return true;
            }

            const ValueId stripped = stripAssigns(graph, value);
            const OperationId defOpId = graph.valueDef(stripped);
            if (!defOpId.valid())
            {
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kMux || defOp.operands().size() != 3 || defOp.results().size() != 1)
            {
                return false;
            }
            const auto point = parsePointCond(graph, defOp.operands()[0]);
            if (!point || !point->baseTerms.empty())
            {
                return false;
            }
            if (!addrOut->valid())
            {
                *addrOut = point->addr;
            }
            else if (*addrOut != point->addr)
            {
                return false;
            }

            auto whenTrue = parseLeafRead(graph, defOp.operands()[1]);
            if (!whenTrue)
            {
                return false;
            }
            whenTrue->slotIndex = point->constIndex;
            membersOut.push_back(std::move(*whenTrue));
            muxOpsOut.push_back(defOpId);
            return parseSlotReadMuxRec(graph, defOp.operands()[2], addrOut, membersOut, muxOpsOut);
        }

        std::optional<CandidateFamily> parseReadCandidate(const Graph &graph,
                                                          OperationId rootMuxOpId,
                                                          const ReadWriteRefs &refs,
                                                          std::string *reasonOut = nullptr)
        {
            const Operation rootOp = graph.getOperation(rootMuxOpId);
            if (rootOp.results().size() != 1)
            {
                setRejectReason(reasonOut, "root mux missing single result");
                return std::nullopt;
            }

            ValueId addr = ValueId::invalid();
            std::vector<SlotMember> members;
            std::vector<OperationId> muxOps;
            if (!parseSlotReadMuxRec(graph, rootOp.results().front(), &addr, members, muxOps))
            {
                setRejectReason(reasonOut, "read tree is not a pure slot-select mux chain");
                return std::nullopt;
            }

            if (members.size() < 4)
            {
                setRejectReason(reasonOut, "slot family too small");
                return std::nullopt;
            }

            int64_t maxResolvedIndex = -1;
            std::optional<std::size_t> unresolvedLeaf;
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                if (members[i].slotIndex < 0)
                {
                    if (unresolvedLeaf)
                    {
                        setRejectReason(reasonOut, "multiple unresolved default leaves");
                        return std::nullopt;
                    }
                    unresolvedLeaf = i;
                }
                else
                {
                    maxResolvedIndex = std::max(maxResolvedIndex, members[i].slotIndex);
                }
            }

            if (maxResolvedIndex < 0)
            {
                setRejectReason(reasonOut, "no resolved slot index found");
                return std::nullopt;
            }
            const std::size_t rowCount = members.size();
            std::vector<bool> seen(rowCount, false);
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                if (members[i].slotIndex < 0)
                {
                    continue;
                }
                const std::size_t slot = static_cast<std::size_t>(members[i].slotIndex);
                if (slot >= rowCount || seen[slot])
                {
                    setRejectReason(reasonOut, "slot indices are duplicated or out of range");
                    return std::nullopt;
                }
                seen[slot] = true;
            }
            if (!unresolvedLeaf && maxResolvedIndex != static_cast<int64_t>(rowCount - 1))
            {
                setRejectReason(reasonOut, "slot indices are not dense");
                return std::nullopt;
            }
            if (unresolvedLeaf)
            {
                std::optional<int64_t> missingSlot;
                for (std::size_t slot = 0; slot < rowCount; ++slot)
                {
                    if (!seen[slot])
                    {
                        if (missingSlot)
                        {
                            setRejectReason(reasonOut, "multiple missing slots for default leaf");
                            return std::nullopt;
                        }
                        missingSlot = static_cast<int64_t>(slot);
                    }
                }
                if (!missingSlot)
                {
                    setRejectReason(reasonOut, "default leaf but no missing slot");
                    return std::nullopt;
                }
                members[*unresolvedLeaf].slotIndex = *missingSlot;
            }
            std::sort(members.begin(), members.end(), [](const SlotMember &lhs, const SlotMember &rhs) {
                return lhs.slotIndex < rhs.slotIndex;
            });
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                if (members[i].slotIndex != static_cast<int64_t>(i))
                {
                    setRejectReason(reasonOut, "sorted slots still not dense from zero");
                    return std::nullopt;
                }
            }

            const Operation firstReg = graph.getOperation(members.front().regOp);
            const int32_t width = static_cast<int32_t>(getAttr<int64_t>(firstReg, "width").value_or(0));
            const bool isSigned = getAttr<bool>(firstReg, "isSigned").value_or(false);
            if (width <= 0)
            {
                setRejectReason(reasonOut, "register width invalid");
                return std::nullopt;
            }
            std::unordered_set<OperationId, OperationIdHash> muxOpSet(muxOps.begin(), muxOps.end());
            std::unordered_set<OperationId, OperationIdHash> writeOps;
            for (const SlotMember &member : members)
            {
                if (const auto it = refs.writePortsBySymbol.find(member.regSymbol); it != refs.writePortsBySymbol.end())
                {
                    writeOps.insert(it->second.begin(), it->second.end());
                }
            }
            std::unordered_map<OperationId, bool, OperationIdHash> writeConeMemo;
            std::unordered_set<OperationId, OperationIdHash> writeConeVisiting;
            for (const SlotMember &member : members)
            {
                const Operation regOp = graph.getOperation(member.regOp);
                if (static_cast<int32_t>(getAttr<int64_t>(regOp, "width").value_or(0)) != width ||
                    getAttr<bool>(regOp, "isSigned").value_or(false) != isSigned)
                {
                    setRejectReason(reasonOut, "slot registers disagree on width or signedness");
                    return std::nullopt;
                }
                const auto readIt = refs.readPortsBySymbol.find(member.regSymbol);
                if (readIt == refs.readPortsBySymbol.end() || readIt->second.size() != 1 || readIt->second.front() != member.readOp)
                {
                    setRejectReason(reasonOut, "slot register has extra read ports");
                    return std::nullopt;
                }
                const Value memberReadValue = graph.getValue(member.readValue);
                for (const ValueUser &user : memberReadValue.users())
                {
                    if (muxOpSet.contains(user.operation))
                    {
                        continue;
                    }
                    if (!isContainedInWriteCone(graph, user.operation, writeOps, writeConeMemo, writeConeVisiting))
                    {
                        setRejectReason(reasonOut, "register read has non-read-tree users");
                        return std::nullopt;
                    }
                }
            }

            return CandidateFamily{
                .width = width,
                .isSigned = isSigned,
                .members = std::move(members),
                .readPlan =
                    ReadPlan{
                        .kind = ReadPlan::Kind::kMuxRoot,
                        .rootValue = rootOp.results().front(),
                        .rootOp = rootMuxOpId,
                        .readAddr = addr,
                    },
            };
        }

        std::optional<CandidateFamily> parseConcatReadCandidate(const Graph &graph,
                                                                OperationId rootConcatOpId,
                                                                const ReadWriteRefs &refs,
                                                                std::string *reasonOut = nullptr)
        {
            const bool trace = traceFilter().has_value();
            const Operation rootOp = graph.getOperation(rootConcatOpId);
            if (rootOp.results().size() != 1)
            {
                setRejectReason(reasonOut, "root concat missing single result");
                return std::nullopt;
            }

            std::vector<ValueId> leaves;
            std::vector<OperationId> concatOps;
            try
            {
                flattenConcatLeaves(graph, rootConcatOpId, leaves, concatOps);
            }
            catch (const std::runtime_error &ex)
            {
                if (trace)
                {
                    throw std::runtime_error(std::string("flattenConcatLeaves: ") + ex.what());
                }
                throw;
            }
            if (leaves.size() < 4)
            {
                setRejectReason(reasonOut, "slot family too small");
                return std::nullopt;
            }

            std::vector<SlotMember> members;
            members.reserve(leaves.size());
            for (std::size_t i = 0; i < leaves.size(); ++i)
            {
                std::optional<SlotMember> member;
                try
                {
                    member = parseLeafRead(graph, leaves[i]);
                }
                catch (const std::runtime_error &ex)
                {
                    if (trace)
                    {
                        throw std::runtime_error("parseLeafRead leaf=" + std::to_string(i) + ": " + ex.what());
                    }
                    throw;
                }
                if (!member)
                {
                    setRejectReason(reasonOut, "concat leaves are not all register reads");
                    return std::nullopt;
                }
                members.push_back(std::move(*member));
            }

            {
                std::unordered_set<std::string> distinctSymbols;
                for (const SlotMember &member : members)
                {
                    distinctSymbols.insert(member.regSymbol);
                }
                if (distinctSymbols.size() != members.size())
                {
                    std::unordered_set<std::string> keptSymbols;
                    std::vector<SlotMember> trimmedReversed;
                    trimmedReversed.reserve(distinctSymbols.size());
                    for (auto it = members.rbegin(); it != members.rend(); ++it)
                    {
                        if (keptSymbols.insert(it->regSymbol).second)
                        {
                            trimmedReversed.push_back(*it);
                        }
                    }
                    if (trimmedReversed.size() < 4)
                    {
                        setRejectReason(reasonOut, "concat leaves do not map to distinct register symbols");
                        return std::nullopt;
                    }
                    members.assign(trimmedReversed.rbegin(), trimmedReversed.rend());
                }
            }
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                members[i].slotIndex = static_cast<int64_t>(members.size() - 1u - i);
            }

            std::optional<Operation> firstReg;
            try
            {
                firstReg = graph.getOperation(members.front().regOp);
            }
            catch (const std::runtime_error &ex)
            {
                if (trace)
                {
                    throw std::runtime_error(std::string("firstReg: ") + ex.what());
                }
                throw;
            }
            const int32_t width = static_cast<int32_t>(getAttr<int64_t>(*firstReg, "width").value_or(0));
            const bool isSigned = getAttr<bool>(*firstReg, "isSigned").value_or(false);
            if (width <= 0)
            {
                setRejectReason(reasonOut, "register width invalid");
                return std::nullopt;
            }

            std::unordered_set<OperationId, OperationIdHash> concatOpSet(concatOps.begin(), concatOps.end());
            std::unordered_set<OperationId, OperationIdHash> writeOps;
            for (const SlotMember &member : members)
            {
                if (const auto it = refs.writePortsBySymbol.find(member.regSymbol); it != refs.writePortsBySymbol.end())
                {
                    writeOps.insert(it->second.begin(), it->second.end());
                }
            }
            std::unordered_map<OperationId, bool, OperationIdHash> writeConeMemo;
            std::unordered_set<OperationId, OperationIdHash> writeConeVisiting;
            for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex)
            {
                const SlotMember &member = members[memberIndex];
                std::optional<Operation> regOp;
                try
                {
                    regOp = graph.getOperation(member.regOp);
                }
                catch (const std::runtime_error &ex)
                {
                    if (trace)
                    {
                        throw std::runtime_error("memberReg member=" + std::to_string(memberIndex) +
                                                 " symbol=" + member.regSymbol + ": " + ex.what());
                    }
                    throw;
                }
                if (static_cast<int32_t>(getAttr<int64_t>(*regOp, "width").value_or(0)) != width ||
                    getAttr<bool>(*regOp, "isSigned").value_or(false) != isSigned)
                {
                    setRejectReason(reasonOut, "slot registers disagree on width or signedness");
                    return std::nullopt;
                }
                const auto readIt = refs.readPortsBySymbol.find(member.regSymbol);
                if (readIt == refs.readPortsBySymbol.end() || readIt->second.size() != 1 || readIt->second.front() != member.readOp)
                {
                    setRejectReason(reasonOut, "slot register has extra read ports");
                    return std::nullopt;
                }
                const Value memberReadValue = graph.getValue(member.readValue);
                std::size_t userIndex = 0;
                for (const ValueUser &user : memberReadValue.users())
                {
                    if (concatOpSet.contains(user.operation))
                    {
                        ++userIndex;
                        continue;
                    }
                    try
                    {
                        if (!isContainedInWriteCone(graph, user.operation, writeOps, writeConeMemo, writeConeVisiting))
                        {
                            setRejectReason(reasonOut, "register read has non-concat users");
                            return std::nullopt;
                        }
                    }
                    catch (const std::runtime_error &ex)
                    {
                        if (trace)
                        {
                            throw std::runtime_error("memberValidate member=" + std::to_string(memberIndex) +
                                                     " symbol=" + member.regSymbol +
                                                     " user=" + std::to_string(userIndex) +
                                                     " op_index=" + std::to_string(user.operation.index) + ": " +
                                                     ex.what());
                        }
                        throw;
                    }
                    ++userIndex;
                }
            }

            std::vector<ReadConsumer> consumers;
            const Value concatResult = graph.getValue(rootOp.results().front());
            if (concatResult.users().empty())
            {
                setRejectReason(reasonOut, "concat result has no users");
                return std::nullopt;
            }
            std::size_t consumerIndex = 0;
            for (const ValueUser &user : concatResult.users())
            {
                std::optional<Operation> sliceOp;
                try
                {
                    sliceOp = graph.getOperation(user.operation);
                }
                catch (const std::runtime_error &ex)
                {
                    if (trace)
                    {
                        throw std::runtime_error("consumerOp consumer=" + std::to_string(consumerIndex) +
                                                 ": " + ex.what());
                    }
                    throw;
                }
                if (sliceOp->operands().size() != 2 ||
                    sliceOp->results().size() != 1 ||
                    sliceOp->operands()[0] != rootOp.results().front())
                {
                    setRejectReason(reasonOut, "concat user is not a 2-operand single-result slice");
                    return std::nullopt;
                }
                if (sliceOp->kind() == OperationKind::kSliceArray)
                {
                    if (getAttr<int64_t>(*sliceOp, "sliceWidth").value_or(0) != width)
                    {
                        setRejectReason(reasonOut, "kSliceArray user width does not match member width");
                        return std::nullopt;
                    }
                    consumers.push_back(ReadConsumer{
                        .opId = user.operation,
                        .addr = sliceOp->operands()[1],
                    });
                    ++consumerIndex;
                    continue;
                }
                if (sliceOp->kind() == OperationKind::kSliceDynamic)
                {
                    if (getAttr<int64_t>(*sliceOp, "sliceWidth").value_or(0) != width)
                    {
                        setRejectReason(reasonOut, "kSliceDynamic user width does not match member width");
                        return std::nullopt;
                    }
                    const auto indexInfo = parseSliceDynamicStart(graph, sliceOp->operands()[1], width, members.size());
                    if (!indexInfo)
                    {
                        setRejectReason(reasonOut, "kSliceDynamic start is not a supported element index expression");
                        return std::nullopt;
                    }
                    consumers.push_back(ReadConsumer{
                        .opId = user.operation,
                        .addr = indexInfo->addr,
                        .reverseAddr = indexInfo->reverseAddr,
                        .rowOffset = indexInfo->rowOffset,
                    });
                    ++consumerIndex;
                    continue;
                }
                setRejectReason(reasonOut, "concat user is neither kSliceArray nor kSliceDynamic");
                return std::nullopt;
            }

            return CandidateFamily{
                .width = width,
                .isSigned = isSigned,
                .members = std::move(members),
                .readPlan =
                    ReadPlan{
                        .kind = ReadPlan::Kind::kConcatSlices,
                        .rootValue = rootOp.results().front(),
                        .rootOp = rootConcatOpId,
                        .consumers = std::move(consumers),
                        .concatOps = std::move(concatOps),
                    },
            };
        }

        bool isAllOnesMaskValue(const Graph &graph, ValueId value, int32_t width)
        {
            const auto parsed = constantIntValue(graph, value);
            if (!parsed || width <= 0 || width >= 63)
            {
                return false;
            }
            return *parsed == ((int64_t{1} << width) - 1);
        }

        bool sameValueVector(std::span<const ValueId> lhs, std::span<const ValueId> rhs)
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

        bool isAndKind(OperationKind kind) noexcept
        {
            return kind == OperationKind::kAnd || kind == OperationKind::kLogicAnd;
        }

        bool isOrKind(OperationKind kind) noexcept
        {
            return kind == OperationKind::kOr || kind == OperationKind::kLogicOr;
        }

        void flattenBoolTree(const Graph &graph,
                             ValueId value,
                             std::vector<ValueId> &leaves,
                             bool (*matches)(OperationKind) noexcept)
        {
            const ValueId stripped = stripAssigns(graph, value);
            const OperationId defOpId = graph.valueDef(stripped);
            if (!defOpId.valid())
            {
                leaves.push_back(stripped);
                return;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (!matches(defOp.kind()) || defOp.operands().size() != 2)
            {
                leaves.push_back(stripped);
                return;
            }
            flattenBoolTree(graph, defOp.operands()[0], leaves, matches);
            flattenBoolTree(graph, defOp.operands()[1], leaves, matches);
        }

        void flattenOrTerms(const Graph &graph, ValueId value, std::vector<ValueId> &terms)
        {
            flattenBoolTree(graph, value, terms, isOrKind);
        }

        bool isAllZeroValue(const Graph &graph, ValueId value)
        {
            const auto parsed = constantIntValue(graph, value);
            return parsed && *parsed == 0;
        }

        struct PointCondBranch
        {
            ValueId cond;
            PointCondInfo info;
        };

        bool samePointAccessKey(const WriteShape &lhs, const WriteShape &rhs)
        {
            return lhs.addr == rhs.addr && lhs.excludeGuards == rhs.excludeGuards &&
                   sameValueVector(lhs.baseTerms, rhs.baseTerms) &&
                   sameValueVector(lhs.eventValues, rhs.eventValues) && lhs.eventEdges == rhs.eventEdges;
        }

        bool samePointWriteFamily(const WriteShape &lhs, const WriteShape &rhs)
        {
            return samePointAccessKey(lhs, rhs) && lhs.data == rhs.data && lhs.mask == rhs.mask;
        }

        bool candidateMatchesTraceFilter(const CandidateFamily &candidate)
        {
            if (!traceFilter())
            {
                return false;
            }
            for (const SlotMember &member : candidate.members)
            {
                if (matchesTraceFilter(member.regSymbol))
                {
                    return true;
                }
            }
            return false;
        }

        bool matchesCondMaskExpr(const Graph &graph,
                                 ValueId value,
                                 int32_t width,
                                 ValueId cond,
                                 bool expectTrue)
        {
            const ValueId stripped = stripAssigns(graph, value);
            if (stripped == cond)
            {
                return expectTrue && width == 1;
            }
            const OperationId defOpId = graph.valueDef(stripped);
            if (!defOpId.valid())
            {
                return false;
            }
            const Operation defOp = graph.getOperation(defOpId);
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

        bool matchesEquivalentPointCondMask(const Graph &graph,
                                            ValueId maskValue,
                                            int32_t width,
                                            const PointCondBranch &branch,
                                            bool expectTrue)
        {
            if (matchesCondMaskExpr(graph, maskValue, width, branch.cond, expectTrue))
            {
                return true;
            }
            if (!expectTrue)
            {
                return false;
            }
            if (graph.valueWidth(maskValue) != 1)
            {
                return false;
            }
            if (auto parsed = parsePointCond(graph, maskValue))
            {
                return *parsed == branch.info;
            }
            return false;
        }

        std::optional<std::pair<ValueId, ValueId>> parseMuxArms(const Graph &graph, ValueId value, ValueId cond)
        {
            const ValueId stripped = stripAssigns(graph, value);
            const OperationId defOpId = graph.valueDef(stripped);
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
            const ValueId muxCond = stripAssigns(graph, defOp.operands()[0]);
            const OperationId muxCondDefId = graph.valueDef(muxCond);
            if (muxCondDefId.valid())
            {
                const Operation muxCondDef = graph.getOperation(muxCondDefId);
                if ((muxCondDef.kind() == OperationKind::kLogicNot || muxCondDef.kind() == OperationKind::kNot) &&
                    muxCondDef.operands().size() == 1 && stripAssigns(graph, muxCondDef.operands()[0]) == cond)
                {
                    return std::pair<ValueId, ValueId>{defOp.operands()[2], defOp.operands()[1]};
                }
            }
            return std::nullopt;
        }

        std::optional<ValueId> parseMaskedPointDataTerm(const Graph &graph,
                                                        ValueId term,
                                                        const PointCondBranch &branch,
                                                        int32_t width)
        {
            const ValueId stripped = stripAssigns(graph, term);
            const OperationId defOpId = graph.valueDef(stripped);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (!isAndKind(defOp.kind()) || defOp.operands().size() != 2)
            {
                if (defOp.kind() == OperationKind::kMux && defOp.operands().size() == 3)
                {
                    if (matchesEquivalentPointCondMask(graph, defOp.operands()[0], width, branch, true) &&
                        isAllZeroValue(graph, defOp.operands()[2]))
                    {
                        return defOp.operands()[1];
                    }
                }
                return std::nullopt;
            }
            if (matchesEquivalentPointCondMask(graph, defOp.operands()[0], width, branch, true))
            {
                return defOp.operands()[1];
            }
            if (matchesEquivalentPointCondMask(graph, defOp.operands()[1], width, branch, true))
            {
                return defOp.operands()[0];
            }
            return std::nullopt;
        }

        bool parsePointDataMaskedOrTree(const Graph &graph,
                                        ValueId value,
                                        std::span<const PointCondBranch> branches,
                                        int32_t width,
                                        std::vector<WriteShape> &pointsOut,
                                        const std::vector<ValueId> &eventValues,
                                        const std::vector<std::string> &eventEdges,
                                        ValueId mask)
        {
            std::vector<ValueId> terms;
            flattenOrTerms(graph, value, terms);
            if (terms.size() != branches.size())
            {
                return false;
            }

            std::vector<bool> used(terms.size(), false);
            for (const PointCondBranch &branch : branches)
            {
                bool matched = false;
                for (std::size_t i = 0; i < terms.size(); ++i)
                {
                    if (used[i])
                    {
                        continue;
                    }
                    if (auto data = parseMaskedPointDataTerm(graph, terms[i], branch, width))
                    {
                        used[i] = true;
                        pointsOut.push_back(WriteShape{
                            .excludeGuards = branch.info.excludeGuards,
                            .baseTerms = branch.info.baseTerms,
                            .addr = branch.info.addr,
                            .data = *data,
                            .mask = mask,
                            .eventValues = eventValues,
                            .eventEdges = eventEdges,
                        });
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                {
                    return false;
                }
            }
            return true;
        }

        bool parsePointDataTree(const Graph &graph,
                                ValueId value,
                                std::span<const PointCondBranch> branches,
                                int32_t width,
                                std::vector<WriteShape> &pointsOut,
                                const std::vector<ValueId> &eventValues,
                                const std::vector<std::string> &eventEdges,
                                ValueId mask,
                                bool allowImplicitLastPointArm)
        {
            if (branches.empty())
            {
                return true;
            }
            if (branches.size() > 1 &&
                parsePointDataMaskedOrTree(graph, value, branches, width, pointsOut, eventValues, eventEdges, mask))
            {
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

                pointsOut.push_back(WriteShape{
                    .excludeGuards = branches[i].info.excludeGuards,
                    .baseTerms = branches[i].info.baseTerms,
                    .addr = branches[i].info.addr,
                    .data = muxArms->first,
                    .mask = mask,
                    .eventValues = eventValues,
                    .eventEdges = eventEdges,
                });
                if (parsePointDataTree(graph,
                                       muxArms->second,
                                       std::span<const PointCondBranch>(remaining.data(), remaining.size()),
                                       width,
                                       pointsOut,
                                       eventValues,
                                       eventEdges,
                                       mask,
                                       allowImplicitLastPointArm))
                {
                    return true;
                }
                pointsOut.pop_back();
            }
            if (allowImplicitLastPointArm && branches.size() == 1)
            {
                pointsOut.push_back(WriteShape{
                    .excludeGuards = branches.front().info.excludeGuards,
                    .baseTerms = branches.front().info.baseTerms,
                    .addr = branches.front().info.addr,
                    .data = value,
                    .mask = mask,
                    .eventValues = eventValues,
                    .eventEdges = eventEdges,
                });
                return true;
            }
            return false;
        }

        bool parseWritesForCandidate(const Graph &graph,
                                     const ReadWriteRefs &refs,
                                     CandidateFamily &candidate,
                                     std::string *reasonOut = nullptr)
        {
            const bool trace = candidateMatchesTraceFilter(candidate);
            std::optional<FillShape> fill;
            std::vector<WriteShape> pointFamilies;

            for (const SlotMember &member : candidate.members)
            {
                const auto writeIt = refs.writePortsBySymbol.find(member.regSymbol);
                if (writeIt == refs.writePortsBySymbol.end())
                {
                    setRejectReason(reasonOut, "slot register has no write ports");
                    return false;
                }

                std::optional<OperationId> fillOp;
                std::vector<WriteShape> memberPoints;
                for (OperationId writeOpId : writeIt->second)
                {
                    const Operation writeOp = graph.getOperation(writeOpId);
                    if (writeOp.kind() != OperationKind::kRegisterWritePort || writeOp.operands().size() < 4)
                    {
                        setRejectReason(reasonOut, "write op shape is not register write");
                        return false;
                    }
                    const auto eventEdges =
                        getAttr<std::vector<std::string>>(writeOp, "eventEdge").value_or(std::vector<std::string>{});
                    const std::vector<ValueId> eventValues(writeOp.operands().begin() + 3, writeOp.operands().end());
                    std::vector<ValueId> condBranches;
                    flattenOrTerms(graph, writeOp.operands()[0], condBranches);
                    std::vector<PointCondBranch> pointBranches;
                    std::vector<ValueId> fillBranches;
                    for (ValueId condBranch : condBranches)
                    {
                        if (auto cond = parsePointCond(graph, condBranch))
                        {
                            pointBranches.push_back(PointCondBranch{.cond = condBranch, .info = std::move(*cond)});
                        }
                        else
                        {
                            fillBranches.push_back(condBranch);
                        }
                    }

                    if (trace && matchesTraceFilter(member.regSymbol))
                    {
                        std::cerr << "[record-slot-repack] write_probe symbol=" << member.regSymbol
                                  << " write_op=" << std::string(writeOp.symbolText())
                                  << " point_branches=" << pointBranches.size()
                                  << " fill_branches=" << fillBranches.size();
                        if (!pointBranches.empty())
                        {
                            std::cerr << " point_slots=";
                            for (std::size_t i = 0; i < pointBranches.size(); ++i)
                            {
                                if (i != 0)
                                {
                                    std::cerr << ',';
                                }
                                std::cerr << pointBranches[i].info.constIndex;
                            }
                        }
                        if (!fillBranches.empty())
                        {
                            std::cerr << " fill_kinds=";
                            for (std::size_t i = 0; i < fillBranches.size(); ++i)
                            {
                                if (i != 0)
                                {
                                    std::cerr << ',';
                                }
                                const ValueId fillValue = fillBranches[i];
                                const OperationId fillDef = graph.valueDef(stripAssigns(graph, fillValue));
                                if (!fillDef.valid())
                                {
                                    std::cerr << "input";
                                    continue;
                                }
                                const Operation fillDefOp = graph.getOperation(fillDef);
                                std::cerr << std::string(toString(fillDefOp.kind()));
                            }
                        }
                        std::cerr << '\n';
                        if (pointBranches.empty())
                        {
                            for (std::size_t i = 0; i < fillBranches.size(); ++i)
                            {
                                std::vector<ValueId> fillTerms;
                                flattenAndTerms(graph, fillBranches[i], fillTerms);
                                std::cerr << "[record-slot-repack] write_probe_terms symbol=" << member.regSymbol
                                          << " branch=" << i << " term_count=" << fillTerms.size() << " terms=";
                                for (std::size_t termIndex = 0; termIndex < fillTerms.size(); ++termIndex)
                                {
                                    if (termIndex != 0)
                                    {
                                        std::cerr << ';';
                                    }
                                    const ValueId term = stripAssigns(graph, fillTerms[termIndex]);
                                    const OperationId termDef = graph.valueDef(term);
                                    if (!termDef.valid())
                                    {
                                        std::cerr << "input";
                                        continue;
                                    }
                                    const Operation termOp = graph.getOperation(termDef);
                                    std::cerr << std::string(toString(termOp.kind()));
                                    if (const auto eq = parseEqAddrConst(graph, term))
                                    {
                                        std::cerr << "(eq=" << eq->constIndex << ')';
                                    }
                                }
                                std::cerr << '\n';
                            }
                        }
                    }

                    if (!pointBranches.empty())
                    {
                        for (const PointCondBranch &branch : pointBranches)
                        {
                            if (branch.info.constIndex != member.slotIndex)
                            {
                                setRejectReason(reasonOut, "point-write slot index mismatch");
                                return false;
                            }
                        }
                        if (!fillBranches.empty())
                        {
                            setRejectReason(reasonOut, "mixed fill/point write is not yet supported");
                            return false;
                        }
                        if (pointBranches.size() == 1)
                        {
                            if (pointBranches.front().info.constIndex != member.slotIndex)
                            {
                                setRejectReason(reasonOut, "point-write slot index mismatch");
                                return false;
                            }
                            memberPoints.push_back(WriteShape{
                                .excludeGuards = pointBranches.front().info.excludeGuards,
                                .baseTerms = pointBranches.front().info.baseTerms,
                                .addr = pointBranches.front().info.addr,
                                .data = writeOp.operands()[1],
                                .mask = writeOp.operands()[2],
                                .eventValues = eventValues,
                                .eventEdges = eventEdges,
                            });
                        }
                        else
                        {
                            const bool ok =
                                parsePointDataTree(graph,
                                                   writeOp.operands()[1],
                                                   std::span<const PointCondBranch>(pointBranches.data(), pointBranches.size()),
                                                   candidate.width,
                                                   memberPoints,
                                                   eventValues,
                                                   eventEdges,
                                                   writeOp.operands()[2],
                                                   true);
                            if (trace && matchesTraceFilter(member.regSymbol))
                            {
                                std::cerr << "[record-slot-repack] write_probe_data_tree symbol="
                                          << member.regSymbol
                                          << " write_op=" << std::string(writeOp.symbolText())
                                          << " ok=" << (ok ? 1 : 0)
                                          << " member_points=" << memberPoints.size() << '\n';
                            }
                            if (!ok)
                            {
                                setRejectReason(reasonOut, "multiple point writes do not encode data as a mux/and-or tree");
                                return false;
                            }
                        }
                        continue;
                    }

                    if (fillOp)
                    {
                        setRejectReason(reasonOut, "multiple non-point writes for one slot");
                        return false;
                    }
                    fillOp = writeOpId;
                    if (!isAllOnesMaskValue(graph, writeOp.operands()[2], candidate.width))
                    {
                        setRejectReason(reasonOut, "bulk write mask is not all-ones");
                        return false;
                    }
                    if (!fill)
                    {
                        fill = FillShape{
                            .cond = writeOp.operands()[0],
                            .data = writeOp.operands()[1],
                            .eventValues = eventValues,
                            .eventEdges = eventEdges,
                        };
                    }
                    else if (fill->cond != writeOp.operands()[0] ||
                             fill->data != writeOp.operands()[1] ||
                             !sameValueVector(fill->eventValues, eventValues) ||
                             fill->eventEdges != eventEdges)
                    {
                        setRejectReason(reasonOut, "fill-write family disagrees across slots");
                        return false;
                    }
                }

                if (memberPoints.empty())
                {
                    setRejectReason(reasonOut,
                                    "missing point write for slot=" + std::to_string(member.slotIndex) +
                                        " symbol=" + member.regSymbol);
                    return false;
                }
                for (const WriteShape &point : memberPoints)
                {
                    if (point.data == member.readValue)
                    {
                        continue;
                    }
                    if (point.addr == member.readValue)
                    {
                        setRejectReason(reasonOut, "point-write address aliases read value unexpectedly");
                        return false;
                    }
                }

                if (pointFamilies.empty())
                {
                    pointFamilies = memberPoints;
                    for (const WriteShape &point : pointFamilies)
                    {
                        if (point.addr == member.readValue)
                        {
                            setRejectReason(reasonOut, "point-write address aliases read value unexpectedly");
                            return false;
                        }
                    }
                }
                else
                {
                    std::vector<bool> matched(pointFamilies.size(), false);
                    for (const WriteShape &point : memberPoints)
                    {
                        bool found = false;
                        for (std::size_t i = 0; i < pointFamilies.size(); ++i)
                        {
                            if (matched[i] || !samePointWriteFamily(pointFamilies[i], point))
                            {
                                continue;
                            }
                            matched[i] = true;
                            found = true;
                            break;
                        }
                        if (!found)
                        {
                            setRejectReason(reasonOut, "point-write family disagrees across slots");
                            return false;
                        }
                    }
                    if (std::find(matched.begin(), matched.end(), false) != matched.end())
                    {
                        setRejectReason(reasonOut, "point-write family count disagrees across slots");
                        return false;
                    }
                }
            }

            if (pointFamilies.empty())
            {
                setRejectReason(reasonOut, "no point-write family found");
                return false;
            }
            candidate.pointWrites = std::move(pointFamilies);
            candidate.fillWrite = std::move(fill);
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
            const OperationId defOpId = graph.valueDef(value);
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
                const Value resultValue = graph.getValue(result);
                if (isValueBoundToPort(graph, result) || !resultValue.users().empty())
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

        ValueId createConstantValue(Graph &graph,
                                    int32_t width,
                                    bool isSigned,
                                    std::string literal,
                                    std::string_view note)
        {
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId valueId = graph.createValue(valueSym, width > 0 ? width : 1, isSigned, ValueType::Logic);
            const OperationId opId = graph.createOperation(OperationKind::kConstant, opSym);
            graph.addResult(opId, valueId);
            graph.setAttr(opId, "constValue", std::move(literal));
            const SrcLoc srcLoc = makeTransformSrcLoc("record-slot-repack", note);
            graph.setValueSrcLoc(valueId, srcLoc);
            graph.setOpSrcLoc(opId, srcLoc);
            return valueId;
        }

        ValueId createLogicTree(Graph &graph,
                                std::span<const ValueId> values,
                                OperationKind kind,
                                std::string_view note,
                                bool emptyAsTrue)
        {
            if (values.empty())
            {
                return createConstantValue(graph, 1, false, emptyAsTrue ? "1'b1" : "1'b0", note);
            }
            if (values.size() == 1)
            {
                return values.front();
            }
            ValueId acc = values.front();
            for (std::size_t i = 1; i < values.size(); ++i)
            {
                const SymbolId valueSym = graph.makeInternalValSym();
                const SymbolId opSym = graph.makeInternalOpSym();
                const ValueId out = graph.createValue(valueSym, 1, false, ValueType::Logic);
                const OperationId op = graph.createOperation(kind, opSym);
                graph.addOperand(op, acc);
                graph.addOperand(op, values[i]);
                graph.addResult(op, out);
                const SrcLoc srcLoc = makeTransformSrcLoc("record-slot-repack", note);
                graph.setValueSrcLoc(out, srcLoc);
                graph.setOpSrcLoc(op, srcLoc);
                acc = out;
            }
            return acc;
        }

        ValueId createBoolBinaryOp(Graph &graph, OperationKind kind, ValueId lhs, ValueId rhs, std::string_view note)
        {
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId out = graph.createValue(valueSym, 1, false, ValueType::Logic);
            const OperationId op = graph.createOperation(kind, opSym);
            graph.addOperand(op, lhs);
            graph.addOperand(op, rhs);
            graph.addResult(op, out);
            const SrcLoc srcLoc = makeTransformSrcLoc("record-slot-repack", note);
            graph.setValueSrcLoc(out, srcLoc);
            graph.setOpSrcLoc(op, srcLoc);
            return out;
        }

        ValueId createBoolNot(Graph &graph, ValueId operand, std::string_view note)
        {
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId out = graph.createValue(valueSym, 1, false, ValueType::Logic);
            const OperationId op = graph.createOperation(OperationKind::kLogicNot, opSym);
            graph.addOperand(op, operand);
            graph.addResult(op, out);
            const SrcLoc srcLoc = makeTransformSrcLoc("record-slot-repack", note);
            graph.setValueSrcLoc(out, srcLoc);
            graph.setOpSrcLoc(op, srcLoc);
            return out;
        }

        ValueId materializePointWriteCond(Graph &graph, const WriteShape &point)
        {
            std::vector<ValueId> condTerms(point.baseTerms.begin(), point.baseTerms.end());
            for (const auto &guard : point.excludeGuards)
            {
                std::vector<ValueId> guardTerms(guard.baseTerms.begin(), guard.baseTerms.end());
                guardTerms.push_back(createBoolBinaryOp(graph, OperationKind::kEq, guard.addr, point.addr, "record_point_guard_eq"));
                const ValueId guardHit =
                    createLogicTree(graph,
                                    std::span<const ValueId>(guardTerms.data(), guardTerms.size()),
                                    OperationKind::kLogicAnd,
                                    "record_point_guard_hit",
                                    true);
                condTerms.push_back(createBoolNot(graph, guardHit, "record_point_guard_not"));
            }
            return createLogicTree(graph,
                                   std::span<const ValueId>(condTerms.data(), condTerms.size()),
                                   OperationKind::kLogicAnd,
                                   "record_point_cond",
                                   true);
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
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId out = graph.createValue(valueSym, width, false, ValueType::Logic);
            const OperationId op = graph.createOperation(rowDelta < 0 ? OperationKind::kSub : OperationKind::kAdd, opSym);
            graph.addOperand(op, baseAddr);
            graph.addOperand(op, deltaConst);
            graph.addResult(op, out);
            const SrcLoc srcLoc = makeTransformSrcLoc("record-slot-repack", note);
            graph.setValueSrcLoc(out, srcLoc);
            graph.setOpSrcLoc(op, srcLoc);
            return out;
        }

        ValueId createReverseIndex(Graph &graph, ValueId originalIndex, std::size_t rowCount)
        {
            const int32_t indexWidth = graph.valueWidth(originalIndex);
            const ValueId maxIndex =
                createConstantValue(graph,
                                    indexWidth,
                                    false,
                                    std::to_string(indexWidth) + "'d" + std::to_string(rowCount - 1u),
                                    "reverse_index_const");
            const SymbolId valueSym = graph.makeInternalValSym();
            const SymbolId opSym = graph.makeInternalOpSym();
            const ValueId out = graph.createValue(valueSym, indexWidth, false, ValueType::Logic);
            const OperationId op = graph.createOperation(OperationKind::kSub, opSym);
            graph.addOperand(op, maxIndex);
            graph.addOperand(op, originalIndex);
            graph.addResult(op, out);
            const SrcLoc srcLoc = makeTransformSrcLoc("record-slot-repack", "reverse_index");
            graph.setValueSrcLoc(out, srcLoc);
            graph.setOpSrcLoc(op, srcLoc);
            return out;
        }

        std::string makeUniqueMemoryName(const Graph &graph, std::string_view regSymbol)
        {
            std::string base(regSymbol);
            base += "$record_mem";
            std::string candidate = base;
            std::size_t suffix = 0;
            while (graph.findOperation(candidate).valid() || graph.findValue(candidate).valid())
            {
                ++suffix;
                candidate = base + "$" + std::to_string(suffix);
            }
            return candidate;
        }

        void setMemoryInitAttrs(Graph &graph, OperationId memoryOp, const CandidateFamily &candidate)
        {
            std::vector<std::string> initKind;
            std::vector<std::string> initFile;
            std::vector<std::string> initValue;
            std::vector<int64_t> initStart;
            std::vector<int64_t> initLen;

            for (const SlotMember &member : candidate.members)
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
                initStart.push_back(member.slotIndex);
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

        void eraseWriteOpAndPrune(Graph &graph, OperationId writeOpId)
        {
            const Operation op = graph.getOperation(writeOpId);
            const std::vector<ValueId> operands(op.operands().begin(), op.operands().end());
            graph.eraseOp(writeOpId);
            for (ValueId operand : operands)
            {
                pruneDeadValueDef(graph, operand);
            }
        }

        GroupKey makeGroupKey(const CandidateFamily &candidate)
        {
            std::vector<PointAccessKey> pointWrites;
            pointWrites.reserve(candidate.pointWrites.size());
            for (const WriteShape &point : candidate.pointWrites)
            {
                pointWrites.push_back(PointAccessKey{
                    .excludeGuards = point.excludeGuards,
                    .baseTerms = point.baseTerms,
                    .addr = point.addr,
                    .eventValues = point.eventValues,
                    .eventEdges = point.eventEdges,
                });
            }
            GroupKey key{
                .pointWrites = std::move(pointWrites),
                .fillCond = candidate.fillWrite ? std::optional<ValueId>(candidate.fillWrite->cond) : std::nullopt,
                .fillEvents = candidate.fillWrite ? candidate.fillWrite->eventValues : std::vector<ValueId>{},
                .fillEdges = candidate.fillWrite ? candidate.fillWrite->eventEdges : std::vector<std::string>{},
                .rowCount = candidate.members.size(),
            };
            return key;
        }

        void rewriteCandidate(Graph &graph,
                              const CandidateFamily &candidate,
                              const std::string &groupId)
        {
            const std::string memoryName = makeUniqueMemoryName(graph, candidate.members.front().regSymbol);
            const OperationId memoryOp = graph.createOperation(OperationKind::kMemory, graph.internSymbol(memoryName));
            graph.setAttr(memoryOp, "width", static_cast<int64_t>(candidate.width));
            graph.setAttr(memoryOp, "row", static_cast<int64_t>(candidate.members.size()));
            graph.setAttr(memoryOp, "isSigned", candidate.isSigned);
            graph.setAttr(memoryOp, "recordGroupId", groupId);
            graph.setOpSrcLoc(memoryOp, makeTransformSrcLoc("record-slot-repack", "record_memory_decl"));
            setMemoryInitAttrs(graph, memoryOp, candidate);

            bool preserveDeclared = false;
            for (const SlotMember &member : candidate.members)
            {
                if (graph.isDeclaredSymbol(graph.operationSymbol(member.regOp)))
                {
                    preserveDeclared = true;
                    break;
                }
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(graph.operationSymbol(memoryOp));
            }

            if (candidate.readPlan.kind == ReadPlan::Kind::kMuxRoot)
            {
                const OperationId readOp = graph.createOperation(OperationKind::kMemoryReadPort, graph.makeInternalOpSym());
                graph.setAttr(readOp, "memSymbol", memoryName);
                graph.setAttr(readOp, "recordGroupId", groupId);
                graph.addOperand(readOp, candidate.readPlan.readAddr);
                const ValueId readValue =
                    graph.createValue(graph.makeInternalValSym(), candidate.width, candidate.isSigned, ValueType::Logic);
                graph.addResult(readOp, readValue);
                const SrcLoc readLoc = makeTransformSrcLoc("record-slot-repack", "record_memory_read");
                graph.setOpSrcLoc(readOp, readLoc);
                graph.setValueSrcLoc(readValue, readLoc);
                replacePortBinding(graph, candidate.readPlan.rootValue, readValue);
                graph.replaceAllUses(candidate.readPlan.rootValue, readValue);
                pruneDeadValueDef(graph, candidate.readPlan.rootValue);
            }
            else
            {
                for (const ReadConsumer &consumer : candidate.readPlan.consumers)
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
                    graph.setAttr(readOp, "recordGroupId", groupId);
                    graph.addOperand(readOp, addr);
                    const ValueId readValue =
                        graph.createValue(graph.makeInternalValSym(), candidate.width, candidate.isSigned, ValueType::Logic);
                    graph.addResult(readOp, readValue);
                    const SrcLoc readLoc = makeTransformSrcLoc("record-slot-repack", "record_memory_read");
                    graph.setOpSrcLoc(readOp, readLoc);
                    graph.setValueSrcLoc(readValue, readLoc);
                    replacePortBinding(graph, oldResult, readValue);
                    graph.eraseOp(consumer.opId, std::array<ValueId, 1>{readValue});
                }
                for (OperationId concatOpId : candidate.readPlan.concatOps)
                {
                    graph.eraseOp(concatOpId);
                }
                for (const SlotMember &member : candidate.members)
                {
                    graph.eraseOp(member.readOp);
                }
            }

            for (const WriteShape &point : candidate.pointWrites)
            {
                const OperationId pointWrite =
                    graph.createOperation(OperationKind::kMemoryWritePort, graph.makeInternalOpSym());
                graph.setAttr(pointWrite, "memSymbol", memoryName);
                graph.setAttr(pointWrite, "eventEdge", point.eventEdges);
                graph.setAttr(pointWrite, "recordGroupId", groupId);
                graph.addOperand(pointWrite, materializePointWriteCond(graph, point));
                graph.addOperand(pointWrite, point.addr);
                graph.addOperand(pointWrite, point.data);
                graph.addOperand(pointWrite, point.mask);
                for (ValueId eventValue : point.eventValues)
                {
                    graph.addOperand(pointWrite, eventValue);
                }
                graph.setOpSrcLoc(pointWrite, makeTransformSrcLoc("record-slot-repack", "record_memory_write"));
            }

            if (candidate.fillWrite)
            {
                const OperationId fillOp = graph.createOperation(OperationKind::kMemoryFillPort, graph.makeInternalOpSym());
                graph.setAttr(fillOp, "memSymbol", memoryName);
                graph.setAttr(fillOp, "eventEdge", candidate.fillWrite->eventEdges);
                graph.setAttr(fillOp, "recordGroupId", groupId);
                graph.addOperand(fillOp, candidate.fillWrite->cond);
                graph.addOperand(fillOp, candidate.fillWrite->data);
                for (ValueId eventValue : candidate.fillWrite->eventValues)
                {
                    graph.addOperand(fillOp, eventValue);
                }
                graph.setOpSrcLoc(fillOp, makeTransformSrcLoc("record-slot-repack", "record_memory_fill"));
            }
        }
    } // namespace

    RecordSlotRepackPass::RecordSlotRepackPass()
        : Pass("record-slot-repack",
               "record-slot-repack",
               "Repack slot-selected scalar register records into per-field memories")
    {
    }

    PassResult RecordSlotRepackPass::run()
    {
        PassResult result;
        std::size_t graphCount = 0;
        std::size_t candidateCount = 0;
        std::size_t rewrittenFamilyCount = 0;
        std::unordered_map<GroupKey, std::string, GroupKeyHash> groupIds;
        std::size_t nextGroupId = 0;

        for (const auto &entry : design().graphs())
        {
            Graph &graph = *entry.second;
            ++graphCount;
            const ReadWriteRefs refs = collectReadWriteRefs(graph);
            std::unordered_set<OperationId, OperationIdHash> claimedRegs;
            struct PendingCandidate
            {
                std::string rootLabel;
                bool traceRoot = false;
                CandidateFamily candidate;
            };
            std::vector<PendingCandidate> pendingCandidates;
            std::vector<OperationId> muxRoots;
            std::vector<OperationId> concatRoots;
            for (const OperationId opId : graph.operations())
            {
                if (isTopLevelMuxRoot(graph, opId))
                {
                    muxRoots.push_back(opId);
                }
                else if (isTopLevelConcatRoot(graph, opId))
                {
                    concatRoots.push_back(opId);
                }
            }

            auto processCandidate = [&](OperationId rootOpId,
                                        auto &&readParser) {
                try
                {
                    std::string rootLabel;
                    bool traceRoot = false;
                    try
                    {
                        const Operation rootOp = graph.getOperation(rootOpId);
                        rootLabel = std::string(rootOp.symbolText());
                        traceRoot = matchesTraceFilter(graph, rootOpId);
                    }
                    catch (const std::runtime_error &)
                    {
                        return;
                    }
                    std::string readRejectReason;
                    std::optional<CandidateFamily> candidate;
                    try
                    {
                        candidate = readParser(graph, rootOpId, refs, &readRejectReason);
                    }
                    catch (const std::runtime_error &ex)
                    {
                        if (traceRoot)
                        {
                            std::cerr << "[record-slot-repack] exception phase=readParser root=" << rootLabel
                                      << " what=" << ex.what() << '\n';
                        }
                        return;
                    }
                    if (!candidate)
                    {
                        if (traceRoot)
                        {
                            std::cerr << "[record-slot-repack] reject_read root=" << rootLabel << " reason="
                                      << (readRejectReason.empty() ? "unknown" : readRejectReason) << '\n';
                            logInfo("record-slot-repack trace reject_read root=" +
                                    rootLabel +
                                    " reason=" + (readRejectReason.empty() ? "unknown" : readRejectReason));
                        }
                        return;
                    }
                    std::string writeRejectReason;
                    bool writesOk = false;
                    try
                    {
                        writesOk = parseWritesForCandidate(graph, refs, *candidate, &writeRejectReason);
                    }
                    catch (const std::runtime_error &ex)
                    {
                        if (traceRoot || candidateMatchesTraceFilter(*candidate))
                        {
                            std::cerr << "[record-slot-repack] exception phase=writeParser root=" << rootLabel
                                      << " what=" << ex.what() << '\n';
                        }
                        return;
                    }
                    if (!writesOk)
                    {
                        if (traceRoot || candidateMatchesTraceFilter(*candidate))
                        {
                            std::cerr << "[record-slot-repack] reject_write root=" << rootLabel << " reason="
                                      << (writeRejectReason.empty() ? "unknown" : writeRejectReason) << '\n';
                            logInfo("record-slot-repack trace reject_write root=" +
                                    rootLabel +
                                    " reason=" + (writeRejectReason.empty() ? "unknown" : writeRejectReason));
                        }
                        return;
                    }
                    bool overlap = false;
                    for (const SlotMember &member : candidate->members)
                    {
                        if (claimedRegs.contains(member.regOp))
                        {
                            overlap = true;
                            break;
                        }
                    }
                    if (overlap)
                    {
                        if (traceRoot || candidateMatchesTraceFilter(*candidate))
                        {
                            std::cerr << "[record-slot-repack] overlap root=" << rootLabel << '\n';
                        }
                        return;
                    }
                        if (traceRoot || candidateMatchesTraceFilter(*candidate))
                        {
                            std::cerr << "[record-slot-repack] parsed root=" << rootLabel << " members="
                                      << candidate->members.size() << " width=" << candidate->width << '\n';
                        }
                        pendingCandidates.push_back(PendingCandidate{
                            .rootLabel = std::move(rootLabel),
                            .traceRoot = traceRoot,
                            .candidate = std::move(*candidate),
                        });
                    }
                    catch (const std::runtime_error &ex)
                    {
                        try
                        {
                        const Operation rootOp = graph.getOperation(rootOpId);
                        if (matchesTraceFilter(graph, rootOpId))
                        {
                            std::cerr << "[record-slot-repack] exception root="
                                      << std::string(rootOp.symbolText()) << " what=" << ex.what() << '\n';
                        }
                    }
                    catch (const std::runtime_error &)
                    {
                    }
                    return;
                }
            };

            for (OperationId rootMuxOpId : muxRoots)
            {
                processCandidate(rootMuxOpId, parseReadCandidate);
            }
            for (OperationId rootConcatOpId : concatRoots)
            {
                processCandidate(rootConcatOpId, parseConcatReadCandidate);
            }

            for (PendingCandidate &pending : pendingCandidates)
            {
                CandidateFamily &candidate = pending.candidate;
                bool overlap = false;
                for (const SlotMember &member : candidate.members)
                {
                    if (claimedRegs.contains(member.regOp))
                    {
                        overlap = true;
                        break;
                    }
                }
                if (overlap)
                {
                    if (pending.traceRoot || candidateMatchesTraceFilter(candidate))
                    {
                        std::cerr << "[record-slot-repack] overlap root=" << pending.rootLabel << '\n';
                    }
                    continue;
                }

                if (pending.traceRoot || candidateMatchesTraceFilter(candidate))
                {
                    std::cerr << "[record-slot-repack] accept root=" << pending.rootLabel << " members="
                              << candidate.members.size() << " width=" << candidate.width << '\n';
                    logInfo("record-slot-repack trace accept root=" +
                            pending.rootLabel +
                            " members=" + std::to_string(candidate.members.size()) +
                            " width=" + std::to_string(candidate.width));
                }
                ++candidateCount;
                for (const SlotMember &member : candidate.members)
                {
                    claimedRegs.insert(member.regOp);
                }

                GroupKey key = makeGroupKey(candidate);
                auto it = groupIds.find(key);
                if (it == groupIds.end())
                {
                    it = groupIds.emplace(std::move(key), "record_group_" + std::to_string(nextGroupId++)).first;
                }

                if (pending.traceRoot || candidateMatchesTraceFilter(candidate))
                {
                    std::cerr << "[record-slot-repack] rewrite_begin root=" << pending.rootLabel << '\n';
                }
                try
                {
                    rewriteCandidate(graph, candidate, it->second);
                }
                catch (const std::runtime_error &ex)
                {
                    if (pending.traceRoot || candidateMatchesTraceFilter(candidate))
                    {
                        std::cerr << "[record-slot-repack] exception phase=rewrite root=" << pending.rootLabel
                                  << " what=" << ex.what() << '\n';
                    }
                    continue;
                }
                if (pending.traceRoot || candidateMatchesTraceFilter(candidate))
                {
                    std::cerr << "[record-slot-repack] rewrite_done root=" << pending.rootLabel << '\n';
                }
                for (const SlotMember &member : candidate.members)
                {
                    const auto writeIt = refs.writePortsBySymbol.find(member.regSymbol);
                    if (writeIt != refs.writePortsBySymbol.end())
                    {
                        for (OperationId writeOpId : writeIt->second)
                        {
                            try
                            {
                                eraseWriteOpAndPrune(graph, writeOpId);
                            }
                            catch (const std::runtime_error &)
                            {
                            }
                        }
                    }
                    try
                    {
                        graph.eraseOp(member.regOp);
                    }
                    catch (const std::runtime_error &)
                    {
                    }
                }

                result.changed = true;
                ++rewrittenFamilyCount;
            }
        }

        logInfo("record-slot-repack: graphs=" + std::to_string(graphCount) +
                " candidates=" + std::to_string(candidateCount) +
                " rewritten_families=" + std::to_string(rewrittenFamilyCount) +
                " mode=slot_mux_to_field_memories");
        return result;
    }

} // namespace wolvrix::lib::transform
