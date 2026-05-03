#include "transform/merge_reg.hpp"
#include "transform/scalar_memory_pack.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace wolvrix::lib::grh;

namespace wolvrix::lib::transform
{
    namespace
    {
        constexpr std::size_t kMinShiftChainMembers = 4;

        template <typename T>
        std::optional<T> getAttr(const Operation &op, std::string_view key)
        {
            const auto attr = op.attr(key);
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

        struct RegisterInfo
        {
            std::string symbol;
            OperationId regOp;
            int64_t width = 0;
            bool isSigned = false;
            std::optional<std::string> initValue;
            std::vector<OperationId> readOps;
            std::vector<ValueId> readValues;
            std::vector<OperationId> writeOps;
        };

        struct IndexedMember
        {
            int64_t index = 0;
            RegisterInfo reg;
        };

        struct BundlePipelineField
        {
            std::string field;
            std::vector<RegisterInfo> stages;
        };

        struct BundlePipelineCluster
        {
            std::string prefix;
            std::vector<BundlePipelineField> fields;
            std::size_t stageCount = 0;
        };

        struct BundleEntryMember
        {
            std::string field;
            RegisterInfo reg;
        };

        struct BundleEntryCluster
        {
            std::string prefix;
            int64_t index = 0;
            std::vector<BundleEntryMember> members;
        };

        struct ParsedTrailingIndex
        {
            std::string prefix;
            int64_t index = 0;
        };

        bool isZeroInitLiteral(std::string_view text)
        {
            if (text.empty() || text == "$random")
            {
                return false;
            }
            const std::size_t quote = text.find('\'');
            if (quote != std::string_view::npos)
            {
                std::size_t digitStart = quote + 1;
                if (digitStart < text.size() &&
                    (text[digitStart] == 'b' || text[digitStart] == 'B' ||
                     text[digitStart] == 'd' || text[digitStart] == 'D' ||
                     text[digitStart] == 'h' || text[digitStart] == 'H' ||
                     text[digitStart] == 'o' || text[digitStart] == 'O'))
                {
                    ++digitStart;
                }
                bool sawZero = false;
                for (std::size_t i = digitStart; i < text.size(); ++i)
                {
                    const unsigned char ch = static_cast<unsigned char>(text[i]);
                    if (ch == '_' || std::isspace(ch))
                    {
                        continue;
                    }
                    if (ch != '0')
                    {
                        return false;
                    }
                    sawZero = true;
                }
                return sawZero;
            }
            bool sawZero = false;
            for (unsigned char ch : text)
            {
                if (ch >= '1' && ch <= '9')
                {
                    return false;
                }
                if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == 'x' || ch == 'X' ||
                    ch == 'z' || ch == 'Z' || ch == '?')
                {
                    return false;
                }
                sawZero = sawZero || ch == '0';
            }
            return sawZero;
        }

        std::optional<ParsedTrailingIndex> parseTrailingIndex(std::string_view symbol)
        {
            if (symbol.empty() || !std::isdigit(static_cast<unsigned char>(symbol.back())))
            {
                return std::nullopt;
            }
            std::size_t firstDigit = symbol.size();
            while (firstDigit > 0 && std::isdigit(static_cast<unsigned char>(symbol[firstDigit - 1])))
            {
                --firstDigit;
            }
            if (firstDigit == 0 || symbol[firstDigit - 1] != '_')
            {
                return std::nullopt;
            }
            int64_t index = 0;
            for (std::size_t i = firstDigit; i < symbol.size(); ++i)
            {
                index = index * 10 + static_cast<int64_t>(symbol[i] - '0');
            }
            return ParsedTrailingIndex{std::string(symbol.substr(0, firstDigit)), index};
        }

        struct ParsedIndexedBundleEntrySymbol
        {
            std::string prefix;
            int64_t index = 0;
            std::string field;
        };

        std::optional<ParsedIndexedBundleEntrySymbol> parseIndexedBundleEntrySymbol(std::string_view symbol)
        {
            for (std::size_t sep = 0; sep + 2 < symbol.size(); ++sep)
            {
                if (symbol[sep] != '_' || !std::isdigit(static_cast<unsigned char>(symbol[sep + 1])))
                {
                    continue;
                }
                std::size_t digitEnd = sep + 1;
                int64_t index = 0;
                while (digitEnd < symbol.size() && std::isdigit(static_cast<unsigned char>(symbol[digitEnd])))
                {
                    index = index * 10 + static_cast<int64_t>(symbol[digitEnd] - '0');
                    ++digitEnd;
                }
                if (digitEnd >= symbol.size() || symbol[digitEnd] != '_' || digitEnd + 1 >= symbol.size())
                {
                    continue;
                }
                return ParsedIndexedBundleEntrySymbol{
                    .prefix = std::string(symbol.substr(0, sep + 1)),
                    .index = index,
                    .field = std::string(symbol.substr(digitEnd + 1)),
                };
            }
            return std::nullopt;
        }

        struct ParsedBundlePipelineSymbol
        {
            std::string prefix;
            std::string field;
            int64_t stage = 0;
        };

        std::optional<ParsedBundlePipelineSymbol> parseBundlePipelineSymbol(std::string_view symbol)
        {
            const std::string_view marker = "$REG";
            const std::size_t markerPos = symbol.rfind(marker);
            if (markerPos == std::string_view::npos)
            {
                return std::nullopt;
            }
            std::string_view suffix = symbol.substr(markerPos + marker.size());
            if (suffix.empty() || suffix.front() != '_')
            {
                return std::nullopt;
            }
            suffix.remove_prefix(1);
            if (suffix.empty())
            {
                return std::nullopt;
            }

            int64_t stage = 0;
            std::string_view field = suffix;
            if (std::isdigit(static_cast<unsigned char>(suffix.front())))
            {
                std::size_t digitEnd = 0;
                while (digitEnd < suffix.size() && std::isdigit(static_cast<unsigned char>(suffix[digitEnd])))
                {
                    stage = stage * 10 + static_cast<int64_t>(suffix[digitEnd] - '0');
                    ++digitEnd;
                }
                if (digitEnd >= suffix.size() || suffix[digitEnd] != '_')
                {
                    return std::nullopt;
                }
                field = suffix.substr(digitEnd + 1);
                if (field.empty())
                {
                    return std::nullopt;
                }
            }

            return ParsedBundlePipelineSymbol{
                .prefix = std::string(symbol.substr(0, markerPos)),
                .field = std::string(field),
                .stage = stage,
            };
        }

        bool sameAttr(const Operation &lhs, const Operation &rhs, std::string_view key)
        {
            return lhs.attr(key) == rhs.attr(key);
        }

        bool compatibleForWideRegister(Graph &graph, const RegisterInfo &lhs, const RegisterInfo &rhs)
        {
            if (lhs.width <= 0 || lhs.width != rhs.width || lhs.isSigned != rhs.isSigned)
            {
                return false;
            }
            if (lhs.readOps.size() != 1 || rhs.readOps.size() != 1 ||
                lhs.writeOps.size() != 1 || rhs.writeOps.size() != 1)
            {
                return false;
            }
            const Operation lhsWrite = graph.getOperation(lhs.writeOps.front());
            const Operation rhsWrite = graph.getOperation(rhs.writeOps.front());
            if (lhsWrite.operands().size() < 3 || rhsWrite.operands().size() != lhsWrite.operands().size())
            {
                return false;
            }
            if (!sameAttr(lhsWrite, rhsWrite, "eventEdge"))
            {
                return false;
            }
            if (lhsWrite.operands()[0] != rhsWrite.operands()[0])
            {
                return false;
            }
            for (std::size_t i = 3; i < lhsWrite.operands().size(); ++i)
            {
                if (lhsWrite.operands()[i] != rhsWrite.operands()[i])
                {
                    return false;
                }
            }
            if (lhs.initValue.has_value() != rhs.initValue.has_value())
            {
                return false;
            }
            if (lhs.initValue && (!isZeroInitLiteral(*lhs.initValue) || !isZeroInitLiteral(*rhs.initValue)))
            {
                return false;
            }
            return true;
        }

        bool valueConeUses(Graph &graph,
                           ValueId root,
                           ValueId target,
                           std::unordered_set<ValueId, ValueIdHash> &visited,
                           std::size_t depth = 0)
        {
            if (root == target)
            {
                return true;
            }
            if (depth > 8 || !root.valid() || !visited.insert(root).second)
            {
                return false;
            }
            const OperationId def = graph.valueDef(root);
            if (!def.valid())
            {
                return false;
            }
            const Operation op = graph.getOperation(def);
            switch (op.kind())
            {
            case OperationKind::kAssign:
            case OperationKind::kMux:
            case OperationKind::kConcat:
            case OperationKind::kSliceStatic:
            case OperationKind::kSliceDynamic:
            case OperationKind::kAnd:
            case OperationKind::kOr:
            case OperationKind::kXor:
            case OperationKind::kNot:
            case OperationKind::kLogicAnd:
            case OperationKind::kLogicOr:
            case OperationKind::kLogicNot:
                break;
            default:
                return false;
            }
            for (const ValueId operand : op.operands())
            {
                if (valueConeUses(graph, operand, target, visited, depth + 1))
                {
                    return true;
                }
            }
            return false;
        }

        bool valueConeUses(Graph &graph, ValueId root, ValueId target)
        {
            std::unordered_set<ValueId, ValueIdHash> visited;
            return valueConeUses(graph, root, target, visited);
        }

        ValueId createValueForOp(Graph &graph,
                                 OperationKind kind,
                                 std::span<const ValueId> operands,
                                 int32_t width,
                                 bool isSigned,
                                 std::string_view note)
        {
            const ValueId result = graph.createValue(graph.makeInternalValSym(),
                                                     width > 0 ? width : 1,
                                                     isSigned,
                                                     ValueType::Logic);
            const OperationId op = graph.createOperation(kind, graph.makeInternalOpSym());
            for (ValueId operand : operands)
            {
                graph.addOperand(op, operand);
            }
            graph.addResult(op, result);
            const SrcLoc loc = makeTransformSrcLoc("merge-reg", note);
            graph.setOpSrcLoc(op, loc);
            graph.setValueSrcLoc(result, loc);
            return result;
        }

        ValueId createConstant(Graph &graph,
                               int32_t width,
                               bool isSigned,
                               std::string literal,
                               std::string_view note)
        {
            const ValueId result = graph.createValue(graph.makeInternalValSym(),
                                                     width > 0 ? width : 1,
                                                     isSigned,
                                                     ValueType::Logic);
            const OperationId op = graph.createOperation(OperationKind::kConstant, graph.makeInternalOpSym());
            graph.addResult(op, result);
            graph.setAttr(op, "constValue", std::move(literal));
            const SrcLoc loc = makeTransformSrcLoc("merge-reg", note);
            graph.setOpSrcLoc(op, loc);
            graph.setValueSrcLoc(result, loc);
            return result;
        }

        std::string makeWideSymbol(Graph &graph, std::string_view firstSymbol, std::string_view lastSymbol)
        {
            std::string base = std::string(firstSymbol);
            if (base.size() > 80)
            {
                base.resize(80);
            }
            base += "$merge_reg$";
            base += Graph::normalizeComponent(lastSymbol);
            base += "$wide";
            std::string candidate = base;
            std::size_t suffix = 0;
            while (graph.lookupSymbol(candidate).valid())
            {
                candidate = base + "_" + std::to_string(++suffix);
            }
            return candidate;
        }

        ValueId createConcat(Graph &graph,
                             const std::vector<ValueId> &lsbFirstValues,
                             int32_t width,
                             bool isSigned,
                             std::string_view note)
        {
            const ValueId result = graph.createValue(graph.makeInternalValSym(), width, isSigned, ValueType::Logic);
            const OperationId op = graph.createOperation(OperationKind::kConcat, graph.makeInternalOpSym());
            for (auto it = lsbFirstValues.rbegin(); it != lsbFirstValues.rend(); ++it)
            {
                graph.addOperand(op, *it);
            }
            graph.addResult(op, result);
            const SrcLoc loc = makeTransformSrcLoc("merge-reg", note);
            graph.setOpSrcLoc(op, loc);
            graph.setValueSrcLoc(result, loc);
            return result;
        }

        ValueId createSlice(Graph &graph,
                            ValueId wideValue,
                            int64_t start,
                            int64_t end,
                            int32_t width,
                            bool isSigned)
        {
            const ValueId result = graph.createValue(graph.makeInternalValSym(), width, isSigned, ValueType::Logic);
            const OperationId op = graph.createOperation(OperationKind::kSliceStatic, graph.makeInternalOpSym());
            graph.addOperand(op, wideValue);
            graph.addResult(op, result);
            graph.setAttr(op, "sliceStart", start);
            graph.setAttr(op, "sliceEnd", end);
            const SrcLoc loc = makeTransformSrcLoc("merge-reg", "wide_register_slice");
            graph.setOpSrcLoc(op, loc);
            graph.setValueSrcLoc(result, loc);
            return result;
        }

        void replaceOutputPortBindings(Graph &graph, ValueId from, ValueId to)
        {
            const std::vector<Port> ports(graph.outputPorts().begin(), graph.outputPorts().end());
            for (const Port &port : ports)
            {
                if (port.value == from)
                {
                    graph.bindOutputPort(port.name, to);
                }
            }
        }

        void remapCachedValues(std::vector<ValueId> &values,
                               std::span<const ValueId> fromValues,
                               std::span<const ValueId> toValues)
        {
            if (fromValues.size() != toValues.size())
            {
                return;
            }
            for (ValueId &value : values)
            {
                for (std::size_t i = 0; i < fromValues.size(); ++i)
                {
                    if (value == fromValues[i])
                    {
                        value = toValues[i];
                        break;
                    }
                }
            }
        }

        bool isShiftChainCandidate(Graph &graph, const std::vector<IndexedMember> &members)
        {
            if (members.size() < kMinShiftChainMembers)
            {
                return false;
            }
            if (!compatibleForWideRegister(graph, members.front().reg, members.front().reg))
            {
                return false;
            }
            for (std::size_t i = 1; i < members.size(); ++i)
            {
                if (members[i].index != members[i - 1].index + 1)
                {
                    return false;
                }
                if (!compatibleForWideRegister(graph, members.front().reg, members[i].reg))
                {
                    return false;
                }
                const Operation write = graph.getOperation(members[i].reg.writeOps.front());
                if (!valueConeUses(graph, write.operands()[1], members[i - 1].reg.readValues.front()))
                {
                    return false;
                }
            }
            return true;
        }

        struct AddressMatch
        {
            ValueId enable;
            ValueId addr;
        };

        struct ZeroWrapper
        {
            ValueId inner;
            ValueId cond;
            bool trueSelectsInner = true;
        };

        std::optional<int64_t> parseConstInt(Graph &graph, ValueId value)
        {
            if (!value.valid())
            {
                return std::nullopt;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() != OperationKind::kConstant)
            {
                return std::nullopt;
            }
            const auto literal = getAttr<std::string>(def, "constValue");
            if (!literal)
            {
                return std::nullopt;
            }

            std::string cleaned;
            cleaned.reserve(literal->size());
            for (const unsigned char ch : *literal)
            {
                if (ch == '_' || std::isspace(ch))
                {
                    continue;
                }
                cleaned.push_back(static_cast<char>(std::tolower(ch)));
            }
            if (cleaned.empty() || cleaned.front() == '"' || cleaned.front() == '$')
            {
                return std::nullopt;
            }

            int base = 10;
            std::string digits = cleaned;
            if (const std::size_t tick = cleaned.find('\''); tick != std::string::npos)
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
            if (digits.empty() || digits.find_first_of("xz?") != std::string::npos)
            {
                return std::nullopt;
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

        std::optional<AddressMatch> parseAddressMatch(Graph &graph, ValueId value, int64_t index)
        {
            if (!value.valid())
            {
                return std::nullopt;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return parseAddressMatch(graph, def.operands()[0], index);
            }
            if ((def.kind() == OperationKind::kAnd || def.kind() == OperationKind::kLogicAnd) &&
                def.operands().size() == 2)
            {
                auto parseEq = [&](ValueId candidate, ValueId enable) -> std::optional<AddressMatch> {
                    const OperationId eqId = graph.valueDef(candidate);
                    if (!eqId.valid())
                    {
                        return std::nullopt;
                    }
                    const Operation eq = graph.getOperation(eqId);
                    if (eq.kind() != OperationKind::kEq || eq.operands().size() != 2)
                    {
                        return std::nullopt;
                    }
                    if (const auto rhs = parseConstInt(graph, eq.operands()[1]); rhs && *rhs == index)
                    {
                        return AddressMatch{enable, eq.operands()[0]};
                    }
                    if (const auto lhs = parseConstInt(graph, eq.operands()[0]); lhs && *lhs == index)
                    {
                        return AddressMatch{enable, eq.operands()[1]};
                    }
                    return std::nullopt;
                };
                if (auto parsed = parseEq(def.operands()[0], def.operands()[1]))
                {
                    return parsed;
                }
                if (auto parsed = parseEq(def.operands()[1], def.operands()[0]))
                {
                    return parsed;
                }
            }
            return std::nullopt;
        }

        std::optional<ValueId> parseClearTerm(Graph &graph, ValueId value, int64_t index)
        {
            if (!value.valid())
            {
                return std::nullopt;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return parseClearTerm(graph, def.operands()[0], index);
            }
            if ((def.kind() == OperationKind::kNot || def.kind() == OperationKind::kLogicNot) &&
                def.operands().size() == 1)
            {
                if (auto match = parseAddressMatch(graph, def.operands()[0], index))
                {
                    return match->enable;
                }
            }
            return std::nullopt;
        }

        struct BitsetMemberPattern
        {
            ValueId setEnable;
            ValueId setAddr;
            ValueId clearEnable;
            ValueId clearAddr;
            ValueId wrapperCond;
            bool wrapperTrueSelectsNext = true;
        };

        struct IndexedMuxMemberPattern
        {
            ValueId hit;
            ValueId enable;
            ValueId addr;
            ValueId data;
            ValueId wrapperCond;
            bool wrapperTrueSelectsNext = true;
        };

        bool isZeroConstant(Graph &graph, ValueId value)
        {
            if (!value.valid())
            {
                return false;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return false;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return isZeroConstant(graph, def.operands()[0]);
            }
            if (def.kind() != OperationKind::kConstant)
            {
                return false;
            }
            const auto literal = getAttr<std::string>(def, "constValue");
            if (!literal)
            {
                return false;
            }
            return isZeroInitLiteral(*literal);
        }

        bool isAllOnesConstant(Graph &graph, ValueId value)
        {
            if (!value.valid())
            {
                return false;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return false;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return isAllOnesConstant(graph, def.operands()[0]);
            }
            if (def.kind() != OperationKind::kConstant)
            {
                return false;
            }
            const auto literal = getAttr<std::string>(def, "constValue");
            if (!literal)
            {
                return false;
            }

            std::string cleaned;
            cleaned.reserve(literal->size());
            for (const unsigned char ch : *literal)
            {
                if (ch == '_' || std::isspace(ch))
                {
                    continue;
                }
                cleaned.push_back(static_cast<char>(std::tolower(ch)));
            }
            const std::size_t tick = cleaned.find('\'');
            if (tick != std::string::npos)
            {
                if (tick + 2 >= cleaned.size())
                {
                    return false;
                }
                const char base = cleaned[tick + 1];
                const std::string digits = cleaned.substr(tick + 2);
                if (digits.empty() || digits.find_first_of("xz?") != std::string::npos)
                {
                    return false;
                }
                if (base == 'b')
                {
                    return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == '1'; });
                }
                if (base == 'h')
                {
                    return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == 'f'; });
                }
                if (base == 'o')
                {
                    return std::all_of(digits.begin(), digits.end(), [](char ch) { return ch == '7'; });
                }
                if (base == 'd')
                {
                    const auto parsed = parseConstInt(graph, value);
                    if (!parsed)
                    {
                        return false;
                    }
                    const Value constant = graph.getValue(value);
                    if (constant.width() <= 0 || constant.width() >= 63)
                    {
                        return false;
                    }
                    return *parsed == ((int64_t{1} << constant.width()) - 1);
                }
                return false;
            }
            return !cleaned.empty() &&
                   std::all_of(cleaned.begin(), cleaned.end(), [](char ch) { return ch == '1'; });
        }

        ZeroWrapper peelZeroWrapper(Graph &graph, ValueId value)
        {
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return ZeroWrapper{value, ValueId::invalid(), true};
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                ZeroWrapper wrapped = peelZeroWrapper(graph, def.operands()[0]);
                return wrapped;
            }
            if (def.kind() == OperationKind::kMux && def.operands().size() == 3)
            {
                if (isZeroConstant(graph, def.operands()[2]))
                {
                    return ZeroWrapper{def.operands()[1], def.operands()[0], true};
                }
                if (isZeroConstant(graph, def.operands()[1]))
                {
                    return ZeroWrapper{def.operands()[2], def.operands()[0], false};
                }
            }
            return ZeroWrapper{value, ValueId::invalid(), true};
        }

        struct StaticSlice
        {
            int64_t low = 0;
            int64_t high = 0;
        };

        struct MaskSlice
        {
            ValueId cond;
            bool trueMeansOne = true;
        };

        std::optional<StaticSlice> getStaticSlice(const Operation &op)
        {
            if (op.kind() != OperationKind::kSliceStatic || op.operands().size() != 1)
            {
                return std::nullopt;
            }
            const auto low = getAttr<int64_t>(op, "sliceStart");
            const auto high = getAttr<int64_t>(op, "sliceEnd");
            if (!low || !high || *low < 0 || *high < *low)
            {
                return std::nullopt;
            }
            return StaticSlice{*low, *high};
        }

        bool valueEquivalent(Graph &graph, ValueId value, ValueId target, std::size_t depth = 0);

        bool sliceEquivalent(Graph &graph,
                             ValueId value,
                             int64_t low,
                             int64_t high,
                             ValueId target,
                             std::size_t depth = 0)
        {
            if (!value.valid() || !target.valid() || low < 0 || high < low || depth > 12)
            {
                return false;
            }
            const Value targetValue = graph.getValue(target);
            if (high - low + 1 != targetValue.width())
            {
                return false;
            }
            if (low == 0 && high == targetValue.width() - 1 && valueEquivalent(graph, value, target, depth + 1))
            {
                return true;
            }

            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return false;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return sliceEquivalent(graph, def.operands()[0], low, high, target, depth + 1);
            }
            if (def.kind() != OperationKind::kConcat || def.operands().empty())
            {
                return false;
            }

            int64_t totalWidth = 0;
            std::vector<int64_t> widths;
            widths.reserve(def.operands().size());
            for (const ValueId operand : def.operands())
            {
                if (!operand.valid())
                {
                    return false;
                }
                const int64_t width = graph.getValue(operand).width();
                if (width <= 0)
                {
                    return false;
                }
                widths.push_back(width);
                totalWidth += width;
            }
            if (high >= totalWidth)
            {
                return false;
            }

            int64_t cursor = totalWidth;
            for (std::size_t i = 0; i < def.operands().size(); ++i)
            {
                const int64_t width = widths[i];
                const int64_t laneHigh = cursor - 1;
                const int64_t laneLow = cursor - width;
                cursor = laneLow;
                if (laneLow == low && laneHigh == high)
                {
                    return valueEquivalent(graph, def.operands()[i], target, depth + 1);
                }
            }
            return false;
        }

        bool valueEquivalent(Graph &graph, ValueId value, ValueId target, std::size_t depth)
        {
            if (value == target)
            {
                return true;
            }
            if (!value.valid() || !target.valid() || depth > 12)
            {
                return false;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return false;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return valueEquivalent(graph, def.operands()[0], target, depth + 1);
            }
            if (const auto slice = getStaticSlice(def))
            {
                return sliceEquivalent(graph,
                                       def.operands()[0],
                                       slice->low,
                                       slice->high,
                                       target,
                                       depth + 1);
            }
            return false;
        }

        bool isZeroSlice(Graph &graph, ValueId value, int64_t low, int64_t high, std::size_t depth = 0)
        {
            if (!value.valid() || low < 0 || high < low || depth > 12)
            {
                return false;
            }
            if (isZeroConstant(graph, value))
            {
                return true;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return false;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return isZeroSlice(graph, def.operands()[0], low, high, depth + 1);
            }
            if (const auto slice = getStaticSlice(def))
            {
                return isZeroSlice(graph,
                                   def.operands()[0],
                                   slice->low + low,
                                   slice->low + high,
                                   depth + 1);
            }
            if (def.kind() == OperationKind::kAnd && def.operands().size() == 2)
            {
                return isZeroSlice(graph, def.operands()[0], low, high, depth + 1) ||
                       isZeroSlice(graph, def.operands()[1], low, high, depth + 1);
            }
            if (def.kind() == OperationKind::kConcat && !def.operands().empty())
            {
                int64_t totalWidth = 0;
                std::vector<int64_t> widths;
                widths.reserve(def.operands().size());
                for (const ValueId operand : def.operands())
                {
                    const int64_t width = graph.getValue(operand).width();
                    if (width <= 0)
                    {
                        return false;
                    }
                    widths.push_back(width);
                    totalWidth += width;
                }
                if (high >= totalWidth)
                {
                    return false;
                }
                int64_t cursor = totalWidth;
                for (std::size_t i = 0; i < def.operands().size(); ++i)
                {
                    const int64_t width = widths[i];
                    const int64_t laneHigh = cursor - 1;
                    const int64_t laneLow = cursor - width;
                    cursor = laneLow;
                    if (low >= laneLow && high <= laneHigh)
                    {
                        return isZeroSlice(graph,
                                           def.operands()[i],
                                           low - laneLow,
                                           high - laneLow,
                                           depth + 1);
                    }
                }
            }
            return false;
        }

        std::optional<ValueId> extractSliceValue(Graph &graph,
                                                 ValueId value,
                                                 int64_t low,
                                                 int64_t high,
                                                 std::size_t depth = 0)
        {
            if (!value.valid() || low < 0 || high < low || depth > 12)
            {
                return std::nullopt;
            }
            const int64_t width = graph.getValue(value).width();
            if (low == 0 && high == width - 1)
            {
                return value;
            }

            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return extractSliceValue(graph, def.operands()[0], low, high, depth + 1);
            }
            if (const auto slice = getStaticSlice(def))
            {
                return extractSliceValue(graph,
                                         def.operands()[0],
                                         slice->low + low,
                                         slice->low + high,
                                         depth + 1);
            }
            if (def.kind() != OperationKind::kConcat || def.operands().empty())
            {
                return std::nullopt;
            }

            int64_t totalWidth = 0;
            std::vector<int64_t> widths;
            widths.reserve(def.operands().size());
            for (const ValueId operand : def.operands())
            {
                const int64_t operandWidth = graph.getValue(operand).width();
                if (operandWidth <= 0)
                {
                    return std::nullopt;
                }
                widths.push_back(operandWidth);
                totalWidth += operandWidth;
            }
            if (high >= totalWidth)
            {
                return std::nullopt;
            }
            int64_t cursor = totalWidth;
            for (std::size_t i = 0; i < def.operands().size(); ++i)
            {
                const int64_t laneWidth = widths[i];
                const int64_t laneHigh = cursor - 1;
                const int64_t laneLow = cursor - laneWidth;
                cursor = laneLow;
                if (low == laneLow && high == laneHigh)
                {
                    return def.operands()[i];
                }
            }
            return std::nullopt;
        }

        std::optional<MaskSlice> matchConditionMaskSlice(Graph &graph,
                                                         ValueId value,
                                                         int64_t low,
                                                         int64_t high,
                                                         std::size_t depth = 0)
        {
            if (!value.valid() || low < 0 || high < low || depth > 12)
            {
                return std::nullopt;
            }
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                const int64_t width = graph.getValue(value).width();
                if (width == 1 && low == 0 && high == 0)
                {
                    return MaskSlice{value, true};
                }
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return matchConditionMaskSlice(graph, def.operands()[0], low, high, depth + 1);
            }
            if (def.kind() == OperationKind::kNot && def.operands().size() == 1)
            {
                auto inner = matchConditionMaskSlice(graph, def.operands()[0], low, high, depth + 1);
                if (inner)
                {
                    inner->trueMeansOne = !inner->trueMeansOne;
                }
                return inner;
            }
            if (def.kind() == OperationKind::kReplicate && def.operands().size() == 1)
            {
                const ValueId cond = def.operands()[0];
                if (graph.getValue(cond).width() == 1 && low >= 0 && high < graph.getValue(value).width())
                {
                    return MaskSlice{cond, true};
                }
                return std::nullopt;
            }
            if (def.kind() == OperationKind::kConcat && !def.operands().empty())
            {
                int64_t totalWidth = 0;
                std::vector<int64_t> widths;
                widths.reserve(def.operands().size());
                for (const ValueId operand : def.operands())
                {
                    const int64_t width = graph.getValue(operand).width();
                    if (width <= 0)
                    {
                        return std::nullopt;
                    }
                    widths.push_back(width);
                    totalWidth += width;
                }
                if (high >= totalWidth)
                {
                    return std::nullopt;
                }
                int64_t cursor = totalWidth;
                for (std::size_t i = 0; i < def.operands().size(); ++i)
                {
                    const int64_t laneWidth = widths[i];
                    const int64_t laneHigh = cursor - 1;
                    const int64_t laneLow = cursor - laneWidth;
                    cursor = laneLow;
                    if (low >= laneLow && high <= laneHigh)
                    {
                        return matchConditionMaskSlice(graph,
                                                       def.operands()[i],
                                                       low - laneLow,
                                                       high - laneLow,
                                                       depth + 1);
                    }
                }
            }
            return std::nullopt;
        }

        struct PackedMuxBranchSlice
        {
            std::optional<ValueId> payload;
            bool zero = false;
            ValueId cond;
            bool trueMeansEnabled = true;
        };

        std::optional<PackedMuxBranchSlice> matchPackedMuxBranchSlice(Graph &graph,
                                                                      ValueId value,
                                                                      int64_t low,
                                                                      int64_t high)
        {
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() != OperationKind::kAnd || def.operands().size() != 2)
            {
                return std::nullopt;
            }
            for (int payloadIndex = 0; payloadIndex < 2; ++payloadIndex)
            {
                const ValueId payloadOperand = def.operands()[static_cast<std::size_t>(payloadIndex)];
                const ValueId maskOperand = def.operands()[static_cast<std::size_t>(1 - payloadIndex)];
                const auto mask = matchConditionMaskSlice(graph, maskOperand, low, high);
                if (!mask)
                {
                    continue;
                }
                if (isZeroSlice(graph, payloadOperand, low, high))
                {
                    return PackedMuxBranchSlice{std::nullopt, true, mask->cond, mask->trueMeansOne};
                }
                if (auto payload = extractSliceValue(graph, payloadOperand, low, high))
                {
                    return PackedMuxBranchSlice{payload, false, mask->cond, mask->trueMeansOne};
                }
            }
            return std::nullopt;
        }

        std::optional<ZeroWrapper> peelPackedZeroWrapper(Graph &graph, ValueId value)
        {
            const OperationId sliceId = graph.valueDef(value);
            if (!sliceId.valid())
            {
                return std::nullopt;
            }
            const Operation sliceOp = graph.getOperation(sliceId);
            const auto slice = getStaticSlice(sliceOp);
            if (!slice)
            {
                return std::nullopt;
            }

            const OperationId rootId = graph.valueDef(sliceOp.operands()[0]);
            if (!rootId.valid())
            {
                return std::nullopt;
            }
            const Operation root = graph.getOperation(rootId);
            if (root.kind() != OperationKind::kOr || root.operands().size() != 2)
            {
                return std::nullopt;
            }

            const auto lhs = matchPackedMuxBranchSlice(graph, root.operands()[0], slice->low, slice->high);
            const auto rhs = matchPackedMuxBranchSlice(graph, root.operands()[1], slice->low, slice->high);
            if (!lhs || !rhs || lhs->cond != rhs->cond || lhs->trueMeansEnabled == rhs->trueMeansEnabled)
            {
                return std::nullopt;
            }
            if (lhs->payload && rhs->zero)
            {
                return ZeroWrapper{*lhs->payload, lhs->cond, lhs->trueMeansEnabled};
            }
            if (rhs->payload && lhs->zero)
            {
                return ZeroWrapper{*rhs->payload, rhs->cond, rhs->trueMeansEnabled};
            }
            return std::nullopt;
        }

        ZeroWrapper peelBundlePipelineData(Graph &graph, ValueId value)
        {
            const ZeroWrapper simple = peelZeroWrapper(graph, value);
            if (simple.cond.valid())
            {
                return simple;
            }
            if (auto packed = peelPackedZeroWrapper(graph, value))
            {
                return *packed;
            }
            return simple;
        }

        std::optional<BitsetMemberPattern> parseBitsetWriteData(Graph &graph,
                                                                 ValueId value,
                                                                 ValueId selfRead,
                                                                 int64_t index)
        {
            const ZeroWrapper wrapper = peelZeroWrapper(graph, value);
            if (wrapper.cond.valid())
            {
                auto parsed = parseBitsetWriteData(graph, wrapper.inner, selfRead, index);
                if (parsed)
                {
                    parsed->wrapperCond = wrapper.cond;
                    parsed->wrapperTrueSelectsNext = wrapper.trueSelectsInner;
                }
                return parsed;
            }

            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return parseBitsetWriteData(graph, def.operands()[0], selfRead, index);
            }
            if (def.kind() != OperationKind::kAnd || def.operands().size() != 2)
            {
                return std::nullopt;
            }

            auto parseSetOrSelf = [&](ValueId candidate) -> std::optional<AddressMatch> {
                const OperationId orId = graph.valueDef(candidate);
                if (!orId.valid())
                {
                    return std::nullopt;
                }
                const Operation orOp = graph.getOperation(orId);
                if (orOp.kind() != OperationKind::kOr || orOp.operands().size() != 2)
                {
                    return std::nullopt;
                }
                if (orOp.operands()[0] == selfRead)
                {
                    return parseAddressMatch(graph, orOp.operands()[1], index);
                }
                if (orOp.operands()[1] == selfRead)
                {
                    return parseAddressMatch(graph, orOp.operands()[0], index);
                }
                return std::nullopt;
            };

            auto parseSide = [&](ValueId clearCandidate, ValueId setCandidate) -> std::optional<BitsetMemberPattern> {
                auto set = parseSetOrSelf(setCandidate);
                if (!set)
                {
                    return std::nullopt;
                }
                const OperationId notId = graph.valueDef(clearCandidate);
                if (!notId.valid())
                {
                    return std::nullopt;
                }
                const Operation notOp = graph.getOperation(notId);
                if ((notOp.kind() != OperationKind::kNot && notOp.kind() != OperationKind::kLogicNot) ||
                    notOp.operands().size() != 1)
                {
                    return std::nullopt;
                }
                auto clear = parseAddressMatch(graph, notOp.operands()[0], index);
                if (!clear)
                {
                    return std::nullopt;
                }
                return BitsetMemberPattern{
                    .setEnable = set->enable,
                    .setAddr = set->addr,
                    .clearEnable = clear->enable,
                    .clearAddr = clear->addr,
                    .wrapperCond = ValueId::invalid(),
                    .wrapperTrueSelectsNext = true,
                };
            };

            if (auto parsed = parseSide(def.operands()[0], def.operands()[1]))
            {
                return parsed;
            }
            if (auto parsed = parseSide(def.operands()[1], def.operands()[0]))
            {
                return parsed;
            }
            return std::nullopt;
        }

        std::optional<IndexedMuxMemberPattern> parseIndexedMuxWriteData(Graph &graph,
                                                                        ValueId value,
                                                                        ValueId selfRead,
                                                                        int64_t index,
                                                                        int64_t memberWidth)
        {
            const ZeroWrapper wrapper = peelZeroWrapper(graph, value);
            if (wrapper.cond.valid())
            {
                auto parsed = parseIndexedMuxWriteData(graph, wrapper.inner, selfRead, index, memberWidth);
                if (parsed)
                {
                    parsed->wrapperCond = wrapper.cond;
                    parsed->wrapperTrueSelectsNext = wrapper.trueSelectsInner;
                }
                return parsed;
            }

            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::nullopt;
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return parseIndexedMuxWriteData(graph, def.operands()[0], selfRead, index, memberWidth);
            }
            if (def.kind() != OperationKind::kMux || def.operands().size() != 3)
            {
                return std::nullopt;
            }
            if (def.operands()[2] != selfRead)
            {
                return std::nullopt;
            }
            auto hit = parseAddressMatch(graph, def.operands()[0], index);
            if (!hit)
            {
                return std::nullopt;
            }
            const Value data = graph.getValue(def.operands()[1]);
            if (data.width() != memberWidth || data.type() != ValueType::Logic)
            {
                return std::nullopt;
            }
            return IndexedMuxMemberPattern{
                .hit = def.operands()[0],
                .enable = hit->enable,
                .addr = hit->addr,
                .data = def.operands()[1],
                .wrapperCond = ValueId::invalid(),
                .wrapperTrueSelectsNext = true,
            };
        }

        std::optional<std::vector<ValueId>> collectOrLeaves(Graph &graph,
                                                            ValueId value,
                                                            std::unordered_set<OperationId, OperationIdHash> &ops)
        {
            const OperationId defId = graph.valueDef(value);
            if (!defId.valid())
            {
                return std::vector<ValueId>{value};
            }
            const Operation def = graph.getOperation(defId);
            if (def.kind() == OperationKind::kAssign && def.operands().size() == 1)
            {
                return collectOrLeaves(graph, def.operands()[0], ops);
            }
            if (def.kind() != OperationKind::kOr || def.operands().size() != 2)
            {
                return std::vector<ValueId>{value};
            }
            auto lhs = collectOrLeaves(graph, def.operands()[0], ops);
            auto rhs = collectOrLeaves(graph, def.operands()[1], ops);
            if (!lhs || !rhs)
            {
                return std::nullopt;
            }
            ops.insert(defId);
            lhs->insert(lhs->end(), rhs->begin(), rhs->end());
            return lhs;
        }

        ValueId createDynamicOneHot(Graph &graph,
                                    ValueId enable,
                                    ValueId addr,
                                    int32_t width,
                                    std::string_view note)
        {
            const ValueId one = createConstant(graph, width, false, std::to_string(width) + "'d1", std::string(note) + "_one");
            const ValueId shifted = createValueForOp(graph, OperationKind::kShl, std::array<ValueId, 2>{one, addr}, width, false, note);
            const ValueId enableWide = createValueForOp(graph, OperationKind::kReplicate, std::array<ValueId, 1>{enable}, width, false, std::string(note) + "_enable");
            const OperationId enableDef = graph.valueDef(enableWide);
            if (enableDef.valid())
            {
                graph.setAttr(enableDef, "rep", static_cast<int64_t>(width));
            }
            return createValueForOp(graph, OperationKind::kAnd, std::array<ValueId, 2>{enableWide, shifted}, width, false, note);
        }

        ValueId createReplicate(Graph &graph,
                                ValueId value,
                                int32_t width,
                                std::string_view note)
        {
            if (width == 1)
            {
                return value;
            }
            const ValueId replicated = createValueForOp(graph,
                                                        OperationKind::kReplicate,
                                                        std::array<ValueId, 1>{value},
                                                        width,
                                                        false,
                                                        note);
            const OperationId def = graph.valueDef(replicated);
            if (def.valid())
            {
                graph.setAttr(def, "rep", static_cast<int64_t>(width));
            }
            return replicated;
        }

        std::string allOnesLiteral(int32_t width)
        {
            return std::to_string(width) + "'b" + std::string(static_cast<std::size_t>(width), '1');
        }

        std::string bundleEntryWriteSignature(const Operation &write)
        {
            std::string signature = "cond:" + std::to_string(write.operands()[0].index) + ";events:";
            for (std::size_t i = 3; i < write.operands().size(); ++i)
            {
                signature += std::to_string(write.operands()[i].index);
                signature += ",";
            }
            signature += ";edges:";
            if (const auto eventEdge = getAttr<std::vector<std::string>>(write, "eventEdge"))
            {
                for (const std::string &edge : *eventEdge)
                {
                    signature += edge;
                    signature += ",";
                }
            }
            return signature;
        }

        std::string bundleEntryEventSignature(const Operation &write)
        {
            std::string signature = "events:";
            for (std::size_t i = 3; i < write.operands().size(); ++i)
            {
                signature += std::to_string(write.operands()[i].index);
                signature += ",";
            }
            signature += ";edges:";
            if (const auto eventEdge = getAttr<std::vector<std::string>>(write, "eventEdge"))
            {
                for (const std::string &edge : *eventEdge)
                {
                    signature += edge;
                    signature += ",";
                }
            }
            return signature;
        }

        ValueId createOrTree(Graph &graph,
                             std::span<const ValueId> values,
                             int32_t width,
                             bool isSigned,
                             std::string_view note)
        {
            if (values.empty())
            {
                return createConstant(graph, width, isSigned, std::to_string(width) + "'d0", note);
            }
            ValueId current = values.front();
            for (std::size_t i = 1; i < values.size(); ++i)
            {
                current = createValueForOp(graph,
                                           OperationKind::kOr,
                                           std::array<ValueId, 2>{current, values[i]},
                                           width,
                                           isSigned,
                                           note);
            }
            return current;
        }

        bool operationStillExists(Graph &graph, OperationId opId)
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

        bool hasNonOrUser(Graph &graph, ValueId value)
        {
            for (const Port &port : graph.outputPorts())
            {
                if (port.value == value)
                {
                    return true;
                }
            }
            const Value current = graph.getValue(value);
            for (const ValueUser &user : current.users())
            {
                if (!operationStillExists(graph, user.operation))
                {
                    continue;
                }
                const Operation userOp = graph.getOperation(user.operation);
                if (userOp.kind() != OperationKind::kOr && userOp.kind() != OperationKind::kAssign)
                {
                    return true;
                }
            }
            return false;
        }

        bool compatibleBundleEntryMember(Graph &graph,
                                         const RegisterInfo &first,
                                         const RegisterInfo &reg,
                                         const Operation &firstWrite,
                                         const Operation &write,
                                         bool requireSameUpdateCond)
        {
            if (reg.width <= 0 || reg.isSigned ||
                reg.readOps.size() != 1 || reg.readValues.size() != 1 || reg.writeOps.size() != 1)
            {
                return false;
            }
            if (write.operands().size() != firstWrite.operands().size() ||
                !sameAttr(write, firstWrite, "eventEdge"))
            {
                return false;
            }
            if (requireSameUpdateCond && write.operands()[0] != firstWrite.operands()[0])
            {
                return false;
            }
            for (std::size_t i = 3; i < write.operands().size(); ++i)
            {
                if (write.operands()[i] != firstWrite.operands()[i])
                {
                    return false;
                }
            }
            if (first.initValue.has_value() != reg.initValue.has_value())
            {
                return false;
            }
            if (first.initValue && (!isZeroInitLiteral(*first.initValue) || !isZeroInitLiteral(*reg.initValue)))
            {
                return false;
            }
            const Value data = graph.getValue(write.operands()[1]);
            const Value mask = graph.getValue(write.operands()[2]);
            return data.width() == reg.width && data.type() == ValueType::Logic &&
                   mask.width() == reg.width && mask.type() == ValueType::Logic;
        }

        bool rewriteBundleEntryWideRegister(Graph &graph,
                                            std::span<const BundleEntryMember> members,
                                            bool foldUpdateCondsIntoMask)
        {
            if (members.size() < kMinShiftChainMembers)
            {
                return false;
            }
            const RegisterInfo &first = members.front().reg;
            if (first.width <= 0 || first.readOps.size() != 1 || first.readValues.size() != 1 ||
                first.writeOps.size() != 1)
            {
                return false;
            }
            const Operation firstWrite = graph.getOperation(first.writeOps.front());
            if (firstWrite.operands().size() < 4)
            {
                return false;
            }

            int64_t wideWidth64 = 0;
            std::vector<int64_t> offsets;
            std::vector<ValueId> dataValues;
            std::vector<ValueId> maskValues;
            std::vector<ValueId> updateConds;
            offsets.reserve(members.size());
            dataValues.reserve(members.size());
            maskValues.reserve(members.size());
            updateConds.reserve(members.size());
            for (const BundleEntryMember &member : members)
            {
                const Operation write = graph.getOperation(member.reg.writeOps.front());
                if (!compatibleBundleEntryMember(graph, first, member.reg, firstWrite, write, !foldUpdateCondsIntoMask))
                {
                    return false;
                }
                offsets.push_back(wideWidth64);
                wideWidth64 += member.reg.width;
                if (wideWidth64 > INT32_MAX)
                {
                    return false;
                }
                dataValues.push_back(write.operands()[1]);
                if (foldUpdateCondsIntoMask)
                {
                    const ValueId updateMask = createReplicate(graph,
                                                               write.operands()[0],
                                                               static_cast<int32_t>(member.reg.width),
                                                               "indexed_bundle_entry_update_mask");
                    maskValues.push_back(createValueForOp(graph,
                                                          OperationKind::kAnd,
                                                          std::array<ValueId, 2>{write.operands()[2], updateMask},
                                                          static_cast<int32_t>(member.reg.width),
                                                          false,
                                                          "indexed_bundle_entry_lane_mask"));
                    updateConds.push_back(write.operands()[0]);
                }
                else
                {
                    maskValues.push_back(write.operands()[2]);
                }
            }
            if (wideWidth64 <= 0)
            {
                return false;
            }
            const int32_t wideWidth = static_cast<int32_t>(wideWidth64);
            const std::string wideSymbol = makeWideSymbol(graph, first.symbol, members.back().reg.symbol);
            const SymbolId wideSym = graph.internSymbol(wideSymbol);

            const OperationId wideReg = graph.createOperation(OperationKind::kRegister, wideSym);
            graph.setAttr(wideReg, "width", static_cast<int64_t>(wideWidth));
            graph.setAttr(wideReg, "isSigned", false);
            if (first.initValue)
            {
                graph.setAttr(wideReg, "initValue", std::to_string(wideWidth) + "'h0");
            }
            graph.setOpSrcLoc(wideReg, makeTransformSrcLoc("merge-reg", "indexed_bundle_entry_decl"));

            bool preserveDeclared = false;
            for (const BundleEntryMember &member : members)
            {
                preserveDeclared = preserveDeclared || graph.isDeclaredSymbol(graph.operationSymbol(member.reg.regOp));
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(wideSym);
            }

            const OperationId wideRead = graph.createOperation(OperationKind::kRegisterReadPort, graph.makeInternalOpSym());
            graph.setAttr(wideRead, "regSymbol", wideSymbol);
            const ValueId wideReadValue = graph.createValue(graph.makeInternalValSym(), wideWidth, false, ValueType::Logic);
            graph.addResult(wideRead, wideReadValue);
            const SrcLoc readLoc = makeTransformSrcLoc("merge-reg", "indexed_bundle_entry_read");
            graph.setOpSrcLoc(wideRead, readLoc);
            graph.setValueSrcLoc(wideReadValue, readLoc);

            std::vector<ValueId> oldReadValues;
            std::vector<ValueId> replacementSlices;
            oldReadValues.reserve(members.size());
            replacementSlices.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                const BundleEntryMember &member = members[i];
                const int64_t start = offsets[i];
                const ValueId slice = createSlice(graph,
                                                  wideReadValue,
                                                  start,
                                                  start + member.reg.width - 1,
                                                  static_cast<int32_t>(member.reg.width),
                                                  member.reg.isSigned);
                oldReadValues.push_back(member.reg.readValues.front());
                replacementSlices.push_back(slice);
                replaceOutputPortBindings(graph, member.reg.readValues.front(), slice);
                graph.replaceAllUses(member.reg.readValues.front(), slice);
            }
            remapCachedValues(dataValues, oldReadValues, replacementSlices);
            remapCachedValues(maskValues, oldReadValues, replacementSlices);
            remapCachedValues(updateConds, oldReadValues, replacementSlices);

            const ValueId wideData = createConcat(graph, dataValues, wideWidth, false, "indexed_bundle_entry_data");
            const ValueId wideMask = createConcat(graph, maskValues, wideWidth, false, "indexed_bundle_entry_mask");
            const ValueId wideUpdateCond = foldUpdateCondsIntoMask
                                               ? createOrTree(graph,
                                                              updateConds,
                                                              1,
                                                              false,
                                                              "indexed_bundle_entry_update_or")
                                               : firstWrite.operands()[0];
            const OperationId wideWrite =
                graph.createOperation(OperationKind::kRegisterWritePort, graph.makeInternalOpSym());
            graph.setAttr(wideWrite, "regSymbol", wideSymbol);
            if (const auto eventEdge = getAttr<std::vector<std::string>>(firstWrite, "eventEdge"))
            {
                graph.setAttr(wideWrite, "eventEdge", *eventEdge);
            }
            graph.addOperand(wideWrite, wideUpdateCond);
            graph.addOperand(wideWrite, wideData);
            graph.addOperand(wideWrite, wideMask);
            for (std::size_t i = 3; i < firstWrite.operands().size(); ++i)
            {
                graph.addOperand(wideWrite, firstWrite.operands()[i]);
            }
            graph.setOpSrcLoc(wideWrite, makeTransformSrcLoc("merge-reg", "indexed_bundle_entry_write"));

            for (const BundleEntryMember &member : members)
            {
                graph.eraseOp(member.reg.writeOps.front());
            }
            for (const BundleEntryMember &member : members)
            {
                if (operationStillExists(graph, member.reg.readOps.front()))
                {
                    graph.eraseOp(member.reg.readOps.front());
                }
            }
            for (const BundleEntryMember &member : members)
            {
                graph.eraseOp(member.reg.regOp);
            }
            return true;
        }

        std::pair<std::size_t, std::size_t> rewriteIndexedBundleEntry(Graph &graph,
                                                                      const BundleEntryCluster &cluster)
        {
            if (cluster.members.size() < kMinShiftChainMembers)
            {
                return {0, 0};
            }
            std::unordered_map<std::string, std::vector<BundleEntryMember>> byWrite;
            for (const BundleEntryMember &member : cluster.members)
            {
                if (member.reg.writeOps.size() != 1)
                {
                    continue;
                }
                const Operation write = graph.getOperation(member.reg.writeOps.front());
                if (write.operands().size() < 4)
                {
                    continue;
                }
                byWrite[bundleEntryWriteSignature(write)].push_back(member);
            }

            std::vector<std::vector<BundleEntryMember>> groups;
            groups.reserve(byWrite.size());
            std::unordered_set<std::string> groupedSymbols;
            for (auto &[_, group] : byWrite)
            {
                if (group.size() < kMinShiftChainMembers)
                {
                    continue;
                }
                std::sort(group.begin(),
                          group.end(),
                          [](const BundleEntryMember &lhs, const BundleEntryMember &rhs) {
                              return lhs.field < rhs.field;
                          });
                for (const BundleEntryMember &member : group)
                {
                    groupedSymbols.insert(member.reg.symbol);
                }
                groups.push_back(std::move(group));
            }

            std::unordered_map<std::string, std::vector<BundleEntryMember>> byEvent;
            for (const BundleEntryMember &member : cluster.members)
            {
                if (groupedSymbols.find(member.reg.symbol) != groupedSymbols.end())
                {
                    continue;
                }
                if (member.reg.writeOps.size() != 1)
                {
                    continue;
                }
                const Operation write = graph.getOperation(member.reg.writeOps.front());
                if (write.operands().size() < 4)
                {
                    continue;
                }
                byEvent[bundleEntryEventSignature(write)].push_back(member);
            }
            for (auto &[_, group] : byEvent)
            {
                if (group.size() < kMinShiftChainMembers)
                {
                    continue;
                }
                std::sort(group.begin(),
                          group.end(),
                          [](const BundleEntryMember &lhs, const BundleEntryMember &rhs) {
                              return lhs.field < rhs.field;
                          });
                groups.push_back(std::move(group));
            }

            std::sort(groups.begin(),
                      groups.end(),
                      [](const auto &lhs, const auto &rhs) {
                          return lhs.front().reg.symbol < rhs.front().reg.symbol;
                      });

            std::size_t rewrittenClusters = 0;
            std::size_t rewrittenMembers = 0;
            for (const auto &group : groups)
            {
                const Operation firstWrite = graph.getOperation(group.front().reg.writeOps.front());
                bool sameUpdateCond = true;
                for (const BundleEntryMember &member : group)
                {
                    const Operation write = graph.getOperation(member.reg.writeOps.front());
                    sameUpdateCond = sameUpdateCond && write.operands()[0] == firstWrite.operands()[0];
                }
                if (!rewriteBundleEntryWideRegister(graph, group, !sameUpdateCond))
                {
                    continue;
                }
                ++rewrittenClusters;
                rewrittenMembers += group.size();
            }
            return {rewrittenClusters, rewrittenMembers};
        }

        bool compatibleIndexedBankMember(Graph &graph,
                                         const RegisterInfo &first,
                                         const RegisterInfo &reg,
                                         const Operation &firstWrite,
                                         const Operation &write)
        {
            if (reg.width <= 0 || reg.width != first.width || reg.isSigned != first.isSigned)
            {
                return false;
            }
            if (reg.readOps.size() != 1 || reg.readValues.size() != 1 || reg.writeOps.size() != 1)
            {
                return false;
            }
            if (write.operands().size() != firstWrite.operands().size() ||
                write.operands()[0] != firstWrite.operands()[0] ||
                !sameAttr(write, firstWrite, "eventEdge"))
            {
                return false;
            }
            if (!isAllOnesConstant(graph, write.operands()[2]))
            {
                return false;
            }
            for (std::size_t i = 3; i < write.operands().size(); ++i)
            {
                if (write.operands()[i] != firstWrite.operands()[i])
                {
                    return false;
                }
            }
            if (first.initValue.has_value() != reg.initValue.has_value())
            {
                return false;
            }
            if (first.initValue && (!isZeroInitLiteral(*first.initValue) || !isZeroInitLiteral(*reg.initValue)))
            {
                return false;
            }
            return true;
        }

        bool rewriteOneHotIndexedBank(Graph &graph, const std::vector<IndexedMember> &members)
        {
            if (members.size() < kMinShiftChainMembers)
            {
                return false;
            }
            const RegisterInfo &first = members.front().reg;
            if (first.width <= 0 || first.readOps.size() != 1 || first.readValues.size() != 1 ||
                first.writeOps.size() != 1)
            {
                return false;
            }
            const int64_t indexBase = members.front().index;
            if (indexBase != 0)
            {
                return false;
            }
            const Operation firstWrite = graph.getOperation(first.writeOps.front());
            if (firstWrite.operands().size() < 4)
            {
                return false;
            }

            std::vector<IndexedMuxMemberPattern> patterns;
            patterns.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                const IndexedMember &member = members[i];
                if (member.index != static_cast<int64_t>(i))
                {
                    return false;
                }
                const Operation write = graph.getOperation(member.reg.writeOps.front());
                if (!compatibleIndexedBankMember(graph, first, member.reg, firstWrite, write))
                {
                    return false;
                }
                auto parsed = parseIndexedMuxWriteData(graph,
                                                       write.operands()[1],
                                                       member.reg.readValues.front(),
                                                       member.index,
                                                       first.width);
                if (!parsed)
                {
                    return false;
                }
                if (!patterns.empty() &&
                    (parsed->enable != patterns.front().enable ||
                     parsed->addr != patterns.front().addr ||
                     parsed->data != patterns.front().data ||
                     parsed->wrapperCond != patterns.front().wrapperCond ||
                     parsed->wrapperTrueSelectsNext != patterns.front().wrapperTrueSelectsNext))
                {
                    return false;
                }
                patterns.push_back(*parsed);
            }

            const int32_t memberWidth = static_cast<int32_t>(first.width);
            const int32_t wideWidth = static_cast<int32_t>(first.width * static_cast<int64_t>(members.size()));
            const std::string wideSymbol = makeWideSymbol(graph, first.symbol, members.back().reg.symbol);
            const SymbolId wideSym = graph.internSymbol(wideSymbol);

            const OperationId wideReg = graph.createOperation(OperationKind::kRegister, wideSym);
            graph.setAttr(wideReg, "width", static_cast<int64_t>(wideWidth));
            graph.setAttr(wideReg, "isSigned", false);
            if (first.initValue)
            {
                graph.setAttr(wideReg, "initValue", std::to_string(wideWidth) + "'h0");
            }
            graph.setOpSrcLoc(wideReg, makeTransformSrcLoc("merge-reg", "onehot_indexed_bank_decl"));

            bool preserveDeclared = false;
            for (const IndexedMember &member : members)
            {
                preserveDeclared = preserveDeclared || graph.isDeclaredSymbol(graph.operationSymbol(member.reg.regOp));
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(wideSym);
            }

            const OperationId wideRead = graph.createOperation(OperationKind::kRegisterReadPort, graph.makeInternalOpSym());
            graph.setAttr(wideRead, "regSymbol", wideSymbol);
            const ValueId wideReadValue = graph.createValue(graph.makeInternalValSym(), wideWidth, false, ValueType::Logic);
            graph.addResult(wideRead, wideReadValue);
            const SrcLoc readLoc = makeTransformSrcLoc("merge-reg", "onehot_indexed_bank_read");
            graph.setOpSrcLoc(wideRead, readLoc);
            graph.setValueSrcLoc(wideReadValue, readLoc);

            std::vector<ValueId> replacementSlices;
            replacementSlices.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                const int64_t start = static_cast<int64_t>(i) * first.width;
                const ValueId slice = createSlice(graph,
                                                  wideReadValue,
                                                  start,
                                                  start + first.width - 1,
                                                  memberWidth,
                                                  first.isSigned);
                replacementSlices.push_back(slice);
                replaceOutputPortBindings(graph, members[i].reg.readValues.front(), slice);
                graph.replaceAllUses(members[i].reg.readValues.front(), slice);
            }

            std::vector<ValueId> wideDataSlots;
            std::vector<ValueId> wideMaskSlots;
            wideDataSlots.reserve(members.size());
            wideMaskSlots.reserve(members.size());
            const IndexedMuxMemberPattern &pattern = patterns.front();
            const ValueId oneHotMask = createDynamicOneHot(graph,
                                                           pattern.enable,
                                                           pattern.addr,
                                                           static_cast<int32_t>(members.size()),
                                                           "onehot_indexed_bank_onehot_mask");
            for (const IndexedMuxMemberPattern &pattern : patterns)
            {
                wideDataSlots.push_back(pattern.data);
                const ValueId slotHit = createSlice(graph,
                                                    oneHotMask,
                                                    static_cast<int64_t>(wideMaskSlots.size()),
                                                    static_cast<int64_t>(wideMaskSlots.size()),
                                                    1,
                                                    false);
                wideMaskSlots.push_back(createReplicate(graph,
                                                        slotHit,
                                                        memberWidth,
                                                        "onehot_indexed_bank_mask_slot"));
            }
            ValueId wideData = createConcat(graph, wideDataSlots, wideWidth, false, "onehot_indexed_bank_data");
            ValueId wideMask = createConcat(graph, wideMaskSlots, wideWidth, false, "onehot_indexed_bank_mask");

            if (pattern.wrapperCond.valid())
            {
                const ValueId zeroData = createConstant(graph, wideWidth, false, std::to_string(wideWidth) + "'d0", "onehot_indexed_bank_reset_zero_data");
                const ValueId allMask = createConstant(graph, wideWidth, false, allOnesLiteral(wideWidth), "onehot_indexed_bank_reset_mask");
                if (pattern.wrapperTrueSelectsNext)
                {
                    wideData = createValueForOp(graph,
                                                OperationKind::kMux,
                                                std::array<ValueId, 3>{pattern.wrapperCond, wideData, zeroData},
                                                wideWidth,
                                                false,
                                                "onehot_indexed_bank_data_mux");
                    wideMask = createValueForOp(graph,
                                                OperationKind::kMux,
                                                std::array<ValueId, 3>{pattern.wrapperCond, wideMask, allMask},
                                                wideWidth,
                                                false,
                                                "onehot_indexed_bank_mask_mux");
                }
                else
                {
                    wideData = createValueForOp(graph,
                                                OperationKind::kMux,
                                                std::array<ValueId, 3>{pattern.wrapperCond, zeroData, wideData},
                                                wideWidth,
                                                false,
                                                "onehot_indexed_bank_data_mux");
                    wideMask = createValueForOp(graph,
                                                OperationKind::kMux,
                                                std::array<ValueId, 3>{pattern.wrapperCond, allMask, wideMask},
                                                wideWidth,
                                                false,
                                                "onehot_indexed_bank_mask_mux");
                }
            }

            const OperationId wideWrite =
                graph.createOperation(OperationKind::kRegisterWritePort, graph.makeInternalOpSym());
            graph.setAttr(wideWrite, "regSymbol", wideSymbol);
            if (const auto eventEdge = getAttr<std::vector<std::string>>(firstWrite, "eventEdge"))
            {
                graph.setAttr(wideWrite, "eventEdge", *eventEdge);
            }
            graph.addOperand(wideWrite, firstWrite.operands()[0]);
            graph.addOperand(wideWrite, wideData);
            graph.addOperand(wideWrite, wideMask);
            for (std::size_t i = 3; i < firstWrite.operands().size(); ++i)
            {
                graph.addOperand(wideWrite, firstWrite.operands()[i]);
            }
            graph.setOpSrcLoc(wideWrite, makeTransformSrcLoc("merge-reg", "onehot_indexed_bank_write"));

            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.writeOps.front());
            }
            for (const IndexedMember &member : members)
            {
                if (operationStillExists(graph, member.reg.readOps.front()))
                {
                    graph.eraseOp(member.reg.readOps.front());
                }
            }
            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.regOp);
            }
            return true;
        }

        bool rewriteBitset(Graph &graph, const std::vector<IndexedMember> &members)
        {
            if (members.size() < kMinShiftChainMembers)
            {
                return false;
            }
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                const RegisterInfo &reg = members[i].reg;
                if (members[i].index != members.front().index + static_cast<int64_t>(i) ||
                    reg.width != 1 || reg.isSigned ||
                    reg.readOps.size() != 1 || reg.readValues.size() != 1 || reg.writeOps.size() != 1)
                {
                    return false;
                }
            }

            const Operation firstWrite = graph.getOperation(members.front().reg.writeOps.front());
            if (firstWrite.operands().size() < 4)
            {
                return false;
            }
            std::vector<BitsetMemberPattern> patterns;
            patterns.reserve(members.size());
            for (const IndexedMember &member : members)
            {
                const Operation write = graph.getOperation(member.reg.writeOps.front());
                if (write.operands().size() != firstWrite.operands().size() ||
                    write.operands()[0] != firstWrite.operands()[0] ||
                    write.operands()[2] != firstWrite.operands()[2] ||
                    !sameAttr(write, firstWrite, "eventEdge"))
                {
                    return false;
                }
                for (std::size_t i = 3; i < write.operands().size(); ++i)
                {
                    if (write.operands()[i] != firstWrite.operands()[i])
                    {
                        return false;
                    }
                }
                auto parsed = parseBitsetWriteData(graph,
                                                   write.operands()[1],
                                                   member.reg.readValues.front(),
                                                   member.index);
                if (!parsed)
                {
                    return false;
                }
                if (!patterns.empty() &&
                    (parsed->setEnable != patterns.front().setEnable ||
                     parsed->setAddr != patterns.front().setAddr ||
                     parsed->clearEnable != patterns.front().clearEnable ||
                     parsed->clearAddr != patterns.front().clearAddr ||
                     parsed->wrapperCond != patterns.front().wrapperCond ||
                     parsed->wrapperTrueSelectsNext != patterns.front().wrapperTrueSelectsNext))
                {
                    return false;
                }
                patterns.push_back(*parsed);
            }

            const int32_t wideWidth = static_cast<int32_t>(members.size());
            const int64_t indexBase = members.front().index;
            if (indexBase != 0)
            {
                return false;
            }

            std::unordered_map<OperationId, std::vector<ValueId>, OperationIdHash> reduceOrReplacements;
            std::unordered_set<OperationId, OperationIdHash> replaceableOrOps;
            for (const OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.results().size() != 1)
                {
                    continue;
                }
                if (!hasNonOrUser(graph, op.results().front()))
                {
                    continue;
                }
                std::unordered_set<OperationId, OperationIdHash> orOps;
                auto leaves = collectOrLeaves(graph, op.results().front(), orOps);
                if (!leaves || leaves->size() < 2)
                {
                    continue;
                }
                std::vector<ValueId> memberLeaves;
                for (const IndexedMember &member : members)
                {
                    memberLeaves.push_back(member.reg.readValues.front());
                }
                std::vector<int64_t> positions;
                bool allMemberReads = true;
                for (ValueId leaf : *leaves)
                {
                    const auto it = std::find(memberLeaves.begin(), memberLeaves.end(), leaf);
                    if (it == memberLeaves.end())
                    {
                        allMemberReads = false;
                        break;
                    }
                    positions.push_back(static_cast<int64_t>(std::distance(memberLeaves.begin(), it)));
                }
                if (!allMemberReads)
                {
                    continue;
                }
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                if (positions.size() != leaves->size())
                {
                    continue;
                }
                for (std::size_t i = 1; i < positions.size(); ++i)
                {
                    if (positions[i] != positions[i - 1] + 1)
                    {
                        allMemberReads = false;
                        break;
                    }
                }
                if (!allMemberReads)
                {
                    continue;
                }
                reduceOrReplacements.emplace(opId, std::vector<ValueId>{});
                replaceableOrOps.insert(orOps.begin(), orOps.end());
            }

            const std::string wideSymbol = makeWideSymbol(graph, members.front().reg.symbol, members.back().reg.symbol);
            const SymbolId wideSym = graph.internSymbol(wideSymbol);
            const OperationId wideReg = graph.createOperation(OperationKind::kRegister, wideSym);
            graph.setAttr(wideReg, "width", static_cast<int64_t>(wideWidth));
            graph.setAttr(wideReg, "isSigned", false);
            graph.setOpSrcLoc(wideReg, makeTransformSrcLoc("merge-reg", "bitset_wide_register_decl"));

            bool preserveDeclared = false;
            for (const IndexedMember &member : members)
            {
                preserveDeclared = preserveDeclared || graph.isDeclaredSymbol(graph.operationSymbol(member.reg.regOp));
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(wideSym);
            }

            const OperationId wideRead = graph.createOperation(OperationKind::kRegisterReadPort, graph.makeInternalOpSym());
            graph.setAttr(wideRead, "regSymbol", wideSymbol);
            const ValueId wideReadValue = graph.createValue(graph.makeInternalValSym(), wideWidth, false, ValueType::Logic);
            graph.addResult(wideRead, wideReadValue);
            graph.setOpSrcLoc(wideRead, makeTransformSrcLoc("merge-reg", "bitset_wide_register_read"));

            std::vector<ValueId> slices;
            slices.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                slices.push_back(createSlice(graph, wideReadValue, static_cast<int64_t>(i), static_cast<int64_t>(i), 1, false));
            }

            for (const auto &[orRoot, _] : reduceOrReplacements)
            {
                if (!operationStillExists(graph, orRoot))
                {
                    continue;
                }
                std::unordered_set<OperationId, OperationIdHash> orOps;
                auto leaves = collectOrLeaves(graph, graph.getOperation(orRoot).results().front(), orOps);
                if (!leaves || leaves->empty())
                {
                    continue;
                }
                int64_t first = wideWidth;
                int64_t last = -1;
                for (ValueId leaf : *leaves)
                {
                    for (std::size_t i = 0; i < members.size(); ++i)
                    {
                        if (leaf == members[i].reg.readValues.front())
                        {
                            first = std::min(first, static_cast<int64_t>(i));
                            last = std::max(last, static_cast<int64_t>(i));
                        }
                    }
                }
                if (first < 0 || last < first)
                {
                    continue;
                }
                const ValueId slice = createSlice(graph, wideReadValue, first, last, static_cast<int32_t>(last - first + 1), false);
                const ValueId reduced = createValueForOp(graph, OperationKind::kReduceOr, std::array<ValueId, 1>{slice}, 1, false, "bitset_reduce_or");
                replaceOutputPortBindings(graph, graph.getOperation(orRoot).results().front(), reduced);
                graph.eraseOp(orRoot, std::array<ValueId, 1>{reduced});
            }

            for (OperationId opId : replaceableOrOps)
            {
                if (!operationStillExists(graph, opId))
                {
                    continue;
                }
                const Operation op = graph.getOperation(opId);
                bool hasUsers = false;
                for (ValueId result : op.results())
                {
                    if (!graph.getValue(result).users().empty())
                    {
                        hasUsers = true;
                        break;
                    }
                }
                if (!hasUsers)
                {
                    graph.eraseOp(opId);
                }
            }

            for (std::size_t i = 0; i < members.size(); ++i)
            {
                replaceOutputPortBindings(graph, members[i].reg.readValues.front(), slices[i]);
                graph.replaceAllUses(members[i].reg.readValues.front(), slices[i]);
            }

            const BitsetMemberPattern &pattern = patterns.front();
            const ValueId setMask = createDynamicOneHot(graph, pattern.setEnable, pattern.setAddr, wideWidth, "bitset_set_mask");
            const ValueId clearMask = createDynamicOneHot(graph, pattern.clearEnable, pattern.clearAddr, wideWidth, "bitset_clear_mask");
            const ValueId setValue = createValueForOp(graph, OperationKind::kOr, std::array<ValueId, 2>{wideReadValue, setMask}, wideWidth, false, "bitset_set");
            const ValueId notClear = createValueForOp(graph, OperationKind::kNot, std::array<ValueId, 1>{clearMask}, wideWidth, false, "bitset_clear_not");
            ValueId next = createValueForOp(graph, OperationKind::kAnd, std::array<ValueId, 2>{setValue, notClear}, wideWidth, false, "bitset_next");
            if (pattern.wrapperCond.valid())
            {
                const ValueId zero = createConstant(graph, wideWidth, false, std::to_string(wideWidth) + "'d0", "bitset_reset_zero");
                next = pattern.wrapperTrueSelectsNext
                           ? createValueForOp(graph, OperationKind::kMux, std::array<ValueId, 3>{pattern.wrapperCond, next, zero}, wideWidth, false, "bitset_next_mux")
                           : createValueForOp(graph, OperationKind::kMux, std::array<ValueId, 3>{pattern.wrapperCond, zero, next}, wideWidth, false, "bitset_next_mux");
            }
            const ValueId mask = createConstant(graph, wideWidth, false, allOnesLiteral(wideWidth), "bitset_write_mask");

            const OperationId wideWrite = graph.createOperation(OperationKind::kRegisterWritePort, graph.makeInternalOpSym());
            graph.setAttr(wideWrite, "regSymbol", wideSymbol);
            if (const auto eventEdge = getAttr<std::vector<std::string>>(firstWrite, "eventEdge"))
            {
                graph.setAttr(wideWrite, "eventEdge", *eventEdge);
            }
            graph.addOperand(wideWrite, firstWrite.operands()[0]);
            graph.addOperand(wideWrite, next);
            graph.addOperand(wideWrite, mask);
            for (std::size_t i = 3; i < firstWrite.operands().size(); ++i)
            {
                graph.addOperand(wideWrite, firstWrite.operands()[i]);
            }
            graph.setOpSrcLoc(wideWrite, makeTransformSrcLoc("merge-reg", "bitset_wide_register_write"));

            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.writeOps.front());
            }
            for (const IndexedMember &member : members)
            {
                if (operationStillExists(graph, member.reg.readOps.front()))
                {
                    graph.eraseOp(member.reg.readOps.front());
                }
            }
            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.regOp);
            }
            return true;
        }

        bool rewriteShiftChain(Graph &graph, const std::vector<IndexedMember> &members)
        {
            if (!isShiftChainCandidate(graph, members))
            {
                return false;
            }

            const RegisterInfo &first = members.front().reg;
            const RegisterInfo &last = members.back().reg;
            const int32_t memberWidth = static_cast<int32_t>(first.width);
            const int32_t wideWidth = static_cast<int32_t>(first.width * static_cast<int64_t>(members.size()));
            const std::string wideSymbol = makeWideSymbol(graph, first.symbol, last.symbol);
            const SymbolId wideSym = graph.internSymbol(wideSymbol);

            std::vector<ValueId> oldReadValues;
            std::vector<ValueId> writeDataValues;
            std::vector<ValueId> maskValues;
            oldReadValues.reserve(members.size());
            writeDataValues.reserve(members.size());
            maskValues.reserve(members.size());
            for (const IndexedMember &member : members)
            {
                const Operation write = graph.getOperation(member.reg.writeOps.front());
                oldReadValues.push_back(member.reg.readValues.front());
                writeDataValues.push_back(write.operands()[1]);
                maskValues.push_back(write.operands()[2]);
            }

            const OperationId wideReg = graph.createOperation(OperationKind::kRegister, wideSym);
            graph.setAttr(wideReg, "width", static_cast<int64_t>(wideWidth));
            graph.setAttr(wideReg, "isSigned", false);
            if (first.initValue)
            {
                graph.setAttr(wideReg, "initValue", std::to_string(wideWidth) + "'h0");
            }
            graph.setOpSrcLoc(wideReg, makeTransformSrcLoc("merge-reg", "wide_register_decl"));

            bool preserveDeclared = false;
            for (const IndexedMember &member : members)
            {
                preserveDeclared = preserveDeclared || graph.isDeclaredSymbol(graph.operationSymbol(member.reg.regOp));
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(wideSym);
            }

            const OperationId wideRead = graph.createOperation(OperationKind::kRegisterReadPort, graph.makeInternalOpSym());
            graph.setAttr(wideRead, "regSymbol", wideSymbol);
            const ValueId wideReadValue = graph.createValue(graph.makeInternalValSym(), wideWidth, false, ValueType::Logic);
            graph.addResult(wideRead, wideReadValue);
            const SrcLoc readLoc = makeTransformSrcLoc("merge-reg", "wide_register_read");
            graph.setOpSrcLoc(wideRead, readLoc);
            graph.setValueSrcLoc(wideReadValue, readLoc);

            std::vector<ValueId> replacementSlices;
            replacementSlices.reserve(members.size());
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                const int64_t start = static_cast<int64_t>(i) * first.width;
                const ValueId slice = createSlice(graph,
                                                  wideReadValue,
                                                  start,
                                                  start + first.width - 1,
                                                  memberWidth,
                                                  first.isSigned);
                replacementSlices.push_back(slice);
                replaceOutputPortBindings(graph, oldReadValues[i], slice);
                graph.replaceAllUses(oldReadValues[i], slice);
            }

            for (ValueId &writeData : writeDataValues)
            {
                for (std::size_t i = 0; i < oldReadValues.size(); ++i)
                {
                    if (writeData == oldReadValues[i])
                    {
                        writeData = replacementSlices[i];
                        break;
                    }
                }
            }

            const ValueId wideData = createConcat(graph, writeDataValues, wideWidth, false, "wide_register_next");
            const ValueId wideMask = createConcat(graph, maskValues, wideWidth, false, "wide_register_mask");
            const Operation firstWrite = graph.getOperation(first.writeOps.front());
            const OperationId wideWrite =
                graph.createOperation(OperationKind::kRegisterWritePort, graph.makeInternalOpSym());
            graph.setAttr(wideWrite, "regSymbol", wideSymbol);
            if (const auto eventEdge = getAttr<std::vector<std::string>>(firstWrite, "eventEdge"))
            {
                graph.setAttr(wideWrite, "eventEdge", *eventEdge);
            }
            graph.addOperand(wideWrite, firstWrite.operands()[0]);
            graph.addOperand(wideWrite, wideData);
            graph.addOperand(wideWrite, wideMask);
            for (std::size_t i = 3; i < firstWrite.operands().size(); ++i)
            {
                graph.addOperand(wideWrite, firstWrite.operands()[i]);
            }
            graph.setOpSrcLoc(wideWrite, makeTransformSrcLoc("merge-reg", "wide_register_write"));

            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.writeOps.front());
            }
            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.readOps.front());
            }
            for (const IndexedMember &member : members)
            {
                graph.eraseOp(member.reg.regOp);
            }
            return true;
        }

        bool compatibleBundlePipelineReg(Graph &graph,
                                         const RegisterInfo &first,
                                         const RegisterInfo &reg,
                                         const Operation &firstWrite,
                                         const Operation &write)
        {
            if (reg.width <= 0 || reg.isSigned || reg.readOps.size() != 1 ||
                reg.readValues.size() != 1 || reg.writeOps.size() != 1)
            {
                return false;
            }
            if (write.operands().size() != firstWrite.operands().size() ||
                write.operands()[0] != firstWrite.operands()[0] ||
                !sameAttr(write, firstWrite, "eventEdge"))
            {
                return false;
            }
            if (!isAllOnesConstant(graph, write.operands()[2]))
            {
                return false;
            }
            for (std::size_t i = 3; i < write.operands().size(); ++i)
            {
                if (write.operands()[i] != firstWrite.operands()[i])
                {
                    return false;
                }
            }
            if (first.initValue.has_value() != reg.initValue.has_value())
            {
                return false;
            }
            if (first.initValue && (!isZeroInitLiteral(*first.initValue) || !isZeroInitLiteral(*reg.initValue)))
            {
                return false;
            }
            return true;
        }

        bool rewriteBundleShiftPipeline(Graph &graph, const BundlePipelineCluster &cluster)
        {
            if (cluster.fields.empty() || cluster.stageCount < 2)
            {
                return false;
            }

            const RegisterInfo &firstReg = cluster.fields.front().stages.front();
            if (firstReg.writeOps.size() != 1)
            {
                return false;
            }
            const Operation firstWrite = graph.getOperation(firstReg.writeOps.front());
            if (firstWrite.operands().size() < 4)
            {
                return false;
            }

            int64_t bundleWidth64 = 0;
            std::vector<int64_t> fieldOffsets;
            fieldOffsets.reserve(cluster.fields.size());
            ValueId wrapperCond = ValueId::invalid();
            bool wrapperTrueSelectsNext = true;
            for (const BundlePipelineField &field : cluster.fields)
            {
                if (field.stages.size() != cluster.stageCount)
                {
                    return false;
                }
                fieldOffsets.push_back(bundleWidth64);
                for (std::size_t stage = 0; stage < field.stages.size(); ++stage)
                {
                    const RegisterInfo &reg = field.stages[stage];
                    if (reg.width <= 0 || reg.isSigned || reg.readOps.size() != 1 ||
                        reg.readValues.size() != 1 || reg.writeOps.size() != 1)
                    {
                        return false;
                    }
                    const Operation write = graph.getOperation(reg.writeOps.front());
                    if (!compatibleBundlePipelineReg(graph, firstReg, reg, firstWrite, write))
                    {
                        return false;
                    }
                    const ZeroWrapper data = peelBundlePipelineData(graph, write.operands()[1]);
                    if (data.cond.valid())
                    {
                        if (!wrapperCond.valid())
                        {
                            wrapperCond = data.cond;
                            wrapperTrueSelectsNext = data.trueSelectsInner;
                        }
                        else if (wrapperCond != data.cond || wrapperTrueSelectsNext != data.trueSelectsInner)
                        {
                            return false;
                        }
                    }
                    if (stage > 0 &&
                        !valueEquivalent(graph, data.inner, field.stages[stage - 1].readValues.front()))
                    {
                        return false;
                    }
                }
                bundleWidth64 += field.stages.front().width;
            }
            if (bundleWidth64 <= 0 || bundleWidth64 > INT32_MAX)
            {
                return false;
            }
            const int32_t bundleWidth = static_cast<int32_t>(bundleWidth64);
            const int64_t wideWidth64 = bundleWidth64 * static_cast<int64_t>(cluster.stageCount);
            if (wideWidth64 <= 0 || wideWidth64 > INT32_MAX)
            {
                return false;
            }
            const int32_t wideWidth = static_cast<int32_t>(wideWidth64);

            std::vector<ValueId> inputFieldValues;
            inputFieldValues.reserve(cluster.fields.size());
            for (const BundlePipelineField &field : cluster.fields)
            {
                const Operation write = graph.getOperation(field.stages.front().writeOps.front());
                const ZeroWrapper data = peelBundlePipelineData(graph, write.operands()[1]);
                if (data.cond.valid())
                {
                    if (!wrapperCond.valid())
                    {
                        wrapperCond = data.cond;
                        wrapperTrueSelectsNext = data.trueSelectsInner;
                    }
                    else if (wrapperCond != data.cond || wrapperTrueSelectsNext != data.trueSelectsInner)
                    {
                        return false;
                    }
                }
                const ValueId inputValue = data.inner;
                const Value value = graph.getValue(inputValue);
                if (value.width() != field.stages.front().width || value.type() != ValueType::Logic)
                {
                    return false;
                }
                inputFieldValues.push_back(inputValue);
            }

            const std::string wideSymbol = makeWideSymbol(graph,
                                                          cluster.fields.front().stages.front().symbol,
                                                          cluster.fields.back().stages.back().symbol);
            const SymbolId wideSym = graph.internSymbol(wideSymbol);

            const OperationId wideReg = graph.createOperation(OperationKind::kRegister, wideSym);
            graph.setAttr(wideReg, "width", static_cast<int64_t>(wideWidth));
            graph.setAttr(wideReg, "isSigned", false);
            if (firstReg.initValue)
            {
                graph.setAttr(wideReg, "initValue", std::to_string(wideWidth) + "'h0");
            }
            graph.setOpSrcLoc(wideReg, makeTransformSrcLoc("merge-reg", "bundle_shift_pipeline_decl"));

            bool preserveDeclared = false;
            for (const BundlePipelineField &field : cluster.fields)
            {
                for (const RegisterInfo &reg : field.stages)
                {
                    preserveDeclared = preserveDeclared || graph.isDeclaredSymbol(graph.operationSymbol(reg.regOp));
                }
            }
            if (preserveDeclared)
            {
                graph.addDeclaredSymbol(wideSym);
            }

            const OperationId wideRead = graph.createOperation(OperationKind::kRegisterReadPort, graph.makeInternalOpSym());
            graph.setAttr(wideRead, "regSymbol", wideSymbol);
            const ValueId wideReadValue = graph.createValue(graph.makeInternalValSym(), wideWidth, false, ValueType::Logic);
            graph.addResult(wideRead, wideReadValue);
            const SrcLoc readLoc = makeTransformSrcLoc("merge-reg", "bundle_shift_pipeline_read");
            graph.setOpSrcLoc(wideRead, readLoc);
            graph.setValueSrcLoc(wideReadValue, readLoc);

            for (std::size_t fieldIndex = 0; fieldIndex < cluster.fields.size(); ++fieldIndex)
            {
                const BundlePipelineField &field = cluster.fields[fieldIndex];
                const int64_t fieldOffset = fieldOffsets[fieldIndex];
                for (std::size_t stage = 0; stage < field.stages.size(); ++stage)
                {
                    const RegisterInfo &reg = field.stages[stage];
                    const int64_t start = static_cast<int64_t>(stage) * bundleWidth + fieldOffset;
                    const ValueId slice = createSlice(graph,
                                                      wideReadValue,
                                                      start,
                                                      start + reg.width - 1,
                                                      static_cast<int32_t>(reg.width),
                                                      reg.isSigned);
                    replaceOutputPortBindings(graph, reg.readValues.front(), slice);
                    graph.replaceAllUses(reg.readValues.front(), slice);
                }
            }

            std::vector<ValueId> nextStageBundles;
            nextStageBundles.reserve(cluster.stageCount);
            nextStageBundles.push_back(createConcat(graph,
                                                    inputFieldValues,
                                                    bundleWidth,
                                                    false,
                                                    "bundle_shift_pipeline_input_bundle"));
            for (std::size_t stage = 0; stage + 1 < cluster.stageCount; ++stage)
            {
                const int64_t start = static_cast<int64_t>(stage) * bundleWidth;
                nextStageBundles.push_back(createSlice(graph,
                                                       wideReadValue,
                                                       start,
                                                       start + bundleWidth - 1,
                                                       bundleWidth,
                                                       false));
            }
            ValueId wideData = createConcat(graph,
                                            nextStageBundles,
                                            wideWidth,
                                            false,
                                            "bundle_shift_pipeline_next");
            if (wrapperCond.valid())
            {
                const ValueId zero = createConstant(graph,
                                                    wideWidth,
                                                    false,
                                                    std::to_string(wideWidth) + "'d0",
                                                    "bundle_shift_pipeline_reset_zero");
                wideData = wrapperTrueSelectsNext
                               ? createValueForOp(graph,
                                                  OperationKind::kMux,
                                                  std::array<ValueId, 3>{wrapperCond, wideData, zero},
                                                  wideWidth,
                                                  false,
                                                  "bundle_shift_pipeline_next_mux")
                               : createValueForOp(graph,
                                                  OperationKind::kMux,
                                                  std::array<ValueId, 3>{wrapperCond, zero, wideData},
                                                  wideWidth,
                                                  false,
                                                  "bundle_shift_pipeline_next_mux");
            }
            const ValueId wideMask = createConstant(graph,
                                                    wideWidth,
                                                    false,
                                                    allOnesLiteral(wideWidth),
                                                    "bundle_shift_pipeline_mask");

            const OperationId wideWrite =
                graph.createOperation(OperationKind::kRegisterWritePort, graph.makeInternalOpSym());
            graph.setAttr(wideWrite, "regSymbol", wideSymbol);
            if (const auto eventEdge = getAttr<std::vector<std::string>>(firstWrite, "eventEdge"))
            {
                graph.setAttr(wideWrite, "eventEdge", *eventEdge);
            }
            graph.addOperand(wideWrite, firstWrite.operands()[0]);
            graph.addOperand(wideWrite, wideData);
            graph.addOperand(wideWrite, wideMask);
            for (std::size_t i = 3; i < firstWrite.operands().size(); ++i)
            {
                graph.addOperand(wideWrite, firstWrite.operands()[i]);
            }
            graph.setOpSrcLoc(wideWrite, makeTransformSrcLoc("merge-reg", "bundle_shift_pipeline_write"));

            for (const BundlePipelineField &field : cluster.fields)
            {
                for (const RegisterInfo &reg : field.stages)
                {
                    graph.eraseOp(reg.writeOps.front());
                }
            }
            for (const BundlePipelineField &field : cluster.fields)
            {
                for (const RegisterInfo &reg : field.stages)
                {
                    if (operationStillExists(graph, reg.readOps.front()))
                    {
                        graph.eraseOp(reg.readOps.front());
                    }
                }
            }
            for (const BundlePipelineField &field : cluster.fields)
            {
                for (const RegisterInfo &reg : field.stages)
                {
                    graph.eraseOp(reg.regOp);
                }
            }
            return true;
        }

        std::unordered_map<std::string, RegisterInfo> collectRegisters(Graph &graph)
        {
            std::unordered_map<std::string, RegisterInfo> regs;
            for (const OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.kind() != OperationKind::kRegister)
                {
                    continue;
                }
                const auto width = getAttr<int64_t>(op, "width");
                const auto isSigned = getAttr<bool>(op, "isSigned");
                if (!width || !isSigned || *width <= 0)
                {
                    continue;
                }
                RegisterInfo info;
                info.symbol = std::string(op.symbolText());
                info.regOp = opId;
                info.width = *width;
                info.isSigned = *isSigned;
                info.initValue = getAttr<std::string>(op, "initValue");
                regs.emplace(info.symbol, std::move(info));
            }

            for (const OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.kind() != OperationKind::kRegisterReadPort && op.kind() != OperationKind::kRegisterWritePort)
                {
                    continue;
                }
                const auto regSymbol = getAttr<std::string>(op, "regSymbol");
                if (!regSymbol)
                {
                    continue;
                }
                auto it = regs.find(*regSymbol);
                if (it == regs.end())
                {
                    continue;
                }
                if (op.kind() == OperationKind::kRegisterReadPort)
                {
                    if (op.results().size() == 1)
                    {
                        it->second.readOps.push_back(opId);
                        it->second.readValues.push_back(op.results().front());
                    }
                }
                else
                {
                    it->second.writeOps.push_back(opId);
                }
            }

            return regs;
        }

        std::vector<std::vector<IndexedMember>> collectNaturalRegisterClusters(
            const std::unordered_map<std::string, RegisterInfo> &regs)
        {
            std::unordered_map<std::string, std::vector<IndexedMember>> groups;
            std::unordered_map<std::string, std::string> baseByGroup;
            for (const auto &[symbol, reg] : regs)
            {
                const auto parsed = parseTrailingIndex(symbol);
                if (!parsed)
                {
                    continue;
                }
                groups[parsed->prefix].push_back(IndexedMember{parsed->index, reg});
                if (!parsed->prefix.empty() && parsed->prefix.back() == '_')
                {
                    baseByGroup.emplace(parsed->prefix, parsed->prefix.substr(0, parsed->prefix.size() - 1));
                }
            }

            for (auto &[key, base] : baseByGroup)
            {
                auto groupIt = groups.find(key);
                auto baseIt = regs.find(base);
                if (groupIt == groups.end() || baseIt == regs.end())
                {
                    continue;
                }
                const bool hasIndexZero = std::any_of(groupIt->second.begin(),
                                                      groupIt->second.end(),
                                                      [](const IndexedMember &member) { return member.index == 0; });
                if (!hasIndexZero)
                {
                    groupIt->second.push_back(IndexedMember{0, baseIt->second});
                }
            }

            std::vector<std::vector<IndexedMember>> clusters;
            for (auto &[_, group] : groups)
            {
                std::sort(group.begin(),
                          group.end(),
                          [](const IndexedMember &lhs, const IndexedMember &rhs) {
                              if (lhs.index != rhs.index)
                              {
                                  return lhs.index < rhs.index;
                              }
                              return lhs.reg.symbol < rhs.reg.symbol;
                          });
                group.erase(std::unique(group.begin(),
                                        group.end(),
                                        [](const IndexedMember &lhs, const IndexedMember &rhs) {
                                            return lhs.index == rhs.index || lhs.reg.symbol == rhs.reg.symbol;
                                        }),
                            group.end());
                if (group.size() >= kMinShiftChainMembers)
                {
                    clusters.push_back(std::move(group));
                }
            }
            std::sort(clusters.begin(),
                      clusters.end(),
                      [](const auto &lhs, const auto &rhs) {
                          return lhs.front().reg.symbol < rhs.front().reg.symbol;
                      });
            return clusters;
        }

        std::vector<BundlePipelineCluster> collectBundlePipelineClusters(
            const std::unordered_map<std::string, RegisterInfo> &regs)
        {
            std::unordered_map<std::string, std::unordered_map<std::string, std::vector<RegisterInfo>>> grouped;
            for (const auto &[symbol, reg] : regs)
            {
                const auto parsed = parseBundlePipelineSymbol(symbol);
                if (!parsed)
                {
                    continue;
                }
                if (parsed->stage < 0)
                {
                    continue;
                }
                auto &stages = grouped[parsed->prefix][parsed->field];
                if (stages.size() <= static_cast<std::size_t>(parsed->stage))
                {
                    stages.resize(static_cast<std::size_t>(parsed->stage) + 1);
                }
                if (!stages[static_cast<std::size_t>(parsed->stage)].symbol.empty())
                {
                    continue;
                }
                stages[static_cast<std::size_t>(parsed->stage)] = reg;
            }

            std::vector<BundlePipelineCluster> clusters;
            for (auto &[prefix, fieldsByName] : grouped)
            {
                BundlePipelineCluster cluster;
                cluster.prefix = prefix;
                for (auto &[field, stages] : fieldsByName)
                {
                    if (stages.size() < 2)
                    {
                        continue;
                    }
                    bool complete = true;
                    for (const RegisterInfo &stage : stages)
                    {
                        if (stage.symbol.empty())
                        {
                            complete = false;
                            break;
                        }
                    }
                    if (!complete)
                    {
                        continue;
                    }
                    if (cluster.stageCount == 0)
                    {
                        cluster.stageCount = stages.size();
                    }
                    if (cluster.stageCount != stages.size())
                    {
                        continue;
                    }
                    cluster.fields.push_back(BundlePipelineField{field, std::move(stages)});
                }
                if (cluster.stageCount < 2 || cluster.fields.empty())
                {
                    continue;
                }
                std::sort(cluster.fields.begin(),
                          cluster.fields.end(),
                          [](const BundlePipelineField &lhs, const BundlePipelineField &rhs) {
                              return lhs.field < rhs.field;
                          });
                clusters.push_back(std::move(cluster));
            }
            std::sort(clusters.begin(),
                      clusters.end(),
                      [](const BundlePipelineCluster &lhs, const BundlePipelineCluster &rhs) {
                          return lhs.prefix < rhs.prefix;
                      });
            return clusters;
        }

        std::vector<BundleEntryCluster> collectIndexedBundleEntryClusters(
            const std::unordered_map<std::string, RegisterInfo> &regs)
        {
            std::unordered_map<std::string, std::unordered_map<int64_t, std::vector<BundleEntryMember>>> grouped;
            for (const auto &[symbol, reg] : regs)
            {
                const auto parsed = parseIndexedBundleEntrySymbol(symbol);
                if (!parsed)
                {
                    continue;
                }
                grouped[parsed->prefix][parsed->index].push_back(BundleEntryMember{parsed->field, reg});
            }

            std::vector<BundleEntryCluster> clusters;
            for (auto &[prefix, byIndex] : grouped)
            {
                for (auto &[index, members] : byIndex)
                {
                    if (members.size() < kMinShiftChainMembers)
                    {
                        continue;
                    }
                    std::sort(members.begin(),
                              members.end(),
                              [](const BundleEntryMember &lhs, const BundleEntryMember &rhs) {
                                  return lhs.field < rhs.field;
                              });
                    clusters.push_back(BundleEntryCluster{
                        .prefix = prefix,
                        .index = index,
                        .members = std::move(members),
                    });
                }
            }
            std::sort(clusters.begin(),
                      clusters.end(),
                      [](const BundleEntryCluster &lhs, const BundleEntryCluster &rhs) {
                          if (lhs.prefix != rhs.prefix)
                          {
                              return lhs.prefix < rhs.prefix;
                          }
                          return lhs.index < rhs.index;
                      });
            return clusters;
        }
    } // namespace

    MergeRegPass::MergeRegPass()
        : MergeRegPass(MergeRegOptions{})
    {
    }

    MergeRegPass::MergeRegPass(MergeRegOptions options)
        : Pass("merge-reg",
               "merge-reg",
               "Merge compatible scalar register clusters into wider register or memory shapes"),
          options_(std::move(options))
    {
    }

    PassResult MergeRegPass::run()
    {
        PassResult result;
        bool scalarToMemoryChanged = false;
        if (options_.enableScalarToMemory)
        {
            PassManagerOptions scalarOptions;
            scalarOptions.verbosity = verbosity();
            scalarOptions.logLevel = LogLevel::Info;
            scalarOptions.keepDeclaredSymbols = keepDeclaredSymbols();
            PassManager scalarManager(scalarOptions);
            scalarManager.addPass(std::make_unique<ScalarMemoryPackPass>());
            const PassManagerResult scalarResult = scalarManager.run(design(), diags());
            if (!scalarResult.success)
            {
                result.failed = true;
                return result;
            }
            scalarToMemoryChanged = scalarResult.changed;
            result.changed = result.changed || scalarResult.changed;
        }

        std::size_t graphCount = 0;
        std::size_t candidateClusters = 0;
        std::size_t candidateMembers = 0;
        std::size_t bundlePipelineClusters = 0;
        std::size_t bundlePipelineMembers = 0;
        std::size_t indexedBundleEntryClusters = 0;
        std::size_t indexedBundleEntryMembers = 0;
        std::size_t rewrittenClusters = 0;
        std::size_t rewrittenMembers = 0;
        std::vector<std::string> enabledStrategies;
        if (options_.enableScalarToMemory)
        {
            enabledStrategies.push_back("scalar-to-memory");
        }
        if (options_.enableBundleShiftPipelineToWideRegister)
        {
            enabledStrategies.push_back("bundle-shift-pipeline-to-wide-register");
        }
        if (options_.enableIndexedBundleEntryToWideRegister)
        {
            enabledStrategies.push_back("indexed-bundle-entry-to-wide-register");
        }
        if (options_.enableOneHotIndexedBankToWideRegister)
        {
            enabledStrategies.push_back("onehot-indexed-bank-to-wide-register");
        }
        if (options_.enableBitsetToWideRegister)
        {
            enabledStrategies.push_back("bitset-to-wide-register");
        }
        if (options_.enableShiftChainToWideRegister)
        {
            enabledStrategies.push_back("shift-chain-to-wide-register");
        }
        std::string strategiesText;
        for (const std::string &strategy : enabledStrategies)
        {
            if (!strategiesText.empty())
            {
                strategiesText.push_back(',');
            }
            strategiesText += strategy;
        }
        if (strategiesText.empty())
        {
            strategiesText = "none";
        }

        for (const auto &entry : design().graphs())
        {
            ++graphCount;
            Graph &graph = *entry.second;
            const auto regs = collectRegisters(graph);
            if (options_.enableBundleShiftPipelineToWideRegister)
            {
                const auto bundleClusters = collectBundlePipelineClusters(regs);
                bundlePipelineClusters += bundleClusters.size();
                for (const BundlePipelineCluster &cluster : bundleClusters)
                {
                    std::size_t memberCount = 0;
                    for (const BundlePipelineField &field : cluster.fields)
                    {
                        memberCount += field.stages.size();
                    }
                    bundlePipelineMembers += memberCount;
                    if (!rewriteBundleShiftPipeline(graph, cluster))
                    {
                        continue;
                    }
                    result.changed = true;
                    ++rewrittenClusters;
                    rewrittenMembers += memberCount;
                }
            }

            const auto postBundleRegs = collectRegisters(graph);
            if (options_.enableIndexedBundleEntryToWideRegister)
            {
                const auto bundleEntryClusters = collectIndexedBundleEntryClusters(postBundleRegs);
                indexedBundleEntryClusters += bundleEntryClusters.size();
                for (const BundleEntryCluster &cluster : bundleEntryClusters)
                {
                    indexedBundleEntryMembers += cluster.members.size();
                    const auto [clusterCount, memberCount] = rewriteIndexedBundleEntry(graph, cluster);
                    if (clusterCount == 0)
                    {
                        continue;
                    }
                    result.changed = true;
                    rewrittenClusters += clusterCount;
                    rewrittenMembers += memberCount;
                }
            }

            const auto postBundleEntryRegs = collectRegisters(graph);
            const auto clusters = collectNaturalRegisterClusters(postBundleEntryRegs);
            candidateClusters += clusters.size();
            for (const auto &cluster : clusters)
            {
                candidateMembers += cluster.size();
                if (options_.enableOneHotIndexedBankToWideRegister && rewriteOneHotIndexedBank(graph, cluster))
                {
                    result.changed = true;
                    ++rewrittenClusters;
                    rewrittenMembers += cluster.size();
                    continue;
                }
                if (options_.enableBitsetToWideRegister && rewriteBitset(graph, cluster))
                {
                    result.changed = true;
                    ++rewrittenClusters;
                    rewrittenMembers += cluster.size();
                    continue;
                }
                if (!options_.enableShiftChainToWideRegister || !rewriteShiftChain(graph, cluster))
                {
                    continue;
                }
                result.changed = true;
                ++rewrittenClusters;
                rewrittenMembers += cluster.size();
            }
        }

        logInfo("merge-reg: graphs=" + std::to_string(graphCount) +
                " candidate_clusters=" + std::to_string(candidateClusters) +
                " candidate_members=" + std::to_string(candidateMembers) +
                " bundle_pipeline_clusters=" + std::to_string(bundlePipelineClusters) +
                " bundle_pipeline_members=" + std::to_string(bundlePipelineMembers) +
                " indexed_bundle_entry_clusters=" + std::to_string(indexedBundleEntryClusters) +
                " indexed_bundle_entry_members=" + std::to_string(indexedBundleEntryMembers) +
                " rewritten_clusters=" + std::to_string(rewrittenClusters) +
                " rewritten_members=" + std::to_string(rewrittenMembers) +
                " scalar_to_memory_changed=" + std::string(scalarToMemoryChanged ? "true" : "false") +
                " strategies=" + strategiesText);
        return result;
    }

} // namespace wolvrix::lib::transform
