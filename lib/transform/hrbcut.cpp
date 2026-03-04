#include "transform/hrbcut.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <utility>
#include <vector>

namespace wolvrix::lib::transform
{

    namespace
    {
        constexpr double kBalanceEpsilon = 1e-9;

        struct SinkRef
        {
            enum class Kind
            {
                Operation,
                OutputValue
            };

            Kind kind = Kind::Operation;
            wolvrix::lib::grh::OperationId op = wolvrix::lib::grh::OperationId::invalid();
            wolvrix::lib::grh::ValueId value = wolvrix::lib::grh::ValueId::invalid();
        };

        struct ConeInfo
        {
            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> combOps;
            std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> values;
            std::unordered_set<std::string> memSymbols;
        };

        struct AscInfo
        {
            std::vector<std::size_t> sinks;
            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> combOps;
            double weight = 0.0;
        };

        struct PartitionSolution
        {
            std::vector<std::size_t> a;
            std::vector<std::size_t> b;
            double balance = 0.0;
            double overlap = 0.0;
        };

        class DisjointSet
        {
        public:
            explicit DisjointSet(std::size_t count)
                : parent_(count), rank_(count, 0)
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    parent_[i] = i;
                }
            }

            std::size_t find(std::size_t value)
            {
                std::size_t root = value;
                while (parent_[root] != root)
                {
                    root = parent_[root];
                }
                while (parent_[value] != value)
                {
                    std::size_t next = parent_[value];
                    parent_[value] = root;
                    value = next;
                }
                return root;
            }

            void unite(std::size_t lhs, std::size_t rhs)
            {
                lhs = find(lhs);
                rhs = find(rhs);
                if (lhs == rhs)
                {
                    return;
                }
                if (rank_[lhs] < rank_[rhs])
                {
                    parent_[lhs] = rhs;
                }
                else if (rank_[lhs] > rank_[rhs])
                {
                    parent_[rhs] = lhs;
                }
                else
                {
                    parent_[rhs] = lhs;
                    ++rank_[lhs];
                }
            }

        private:
            std::vector<std::size_t> parent_;
            std::vector<uint32_t> rank_;
        };

        std::size_t maxParallelThreads()
        {
            return std::max<std::size_t>(1, static_cast<std::size_t>(std::thread::hardware_concurrency()));
        }

        template <typename Local, typename Func>
        void runParallelTasks(std::size_t taskCount, std::vector<Local> &locals, Func func)
        {
            if (taskCount == 0)
            {
                return;
            }
            const std::size_t threadCount = std::min<std::size_t>(maxParallelThreads(), taskCount);
            locals.clear();
            locals.resize(threadCount);
            if (threadCount == 1)
            {
                for (std::size_t index = 0; index < taskCount; ++index)
                {
                    func(index, locals[0]);
                }
                return;
            }
            std::atomic<std::size_t> next{0};
            std::vector<std::thread> threads;
            threads.reserve(threadCount);
            for (std::size_t t = 0; t < threadCount; ++t)
            {
                threads.emplace_back([&, t]() {
                    for (;;)
                    {
                        const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                        if (index >= taskCount)
                        {
                            return;
                        }
                        func(index, locals[t]);
                    }
                });
            }
            for (auto &thread : threads)
            {
                thread.join();
            }
        }

        template <typename T>
        std::optional<T> getAttr(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            std::optional<wolvrix::lib::grh::AttributeValue> attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (auto ptr = std::get_if<T>(&*attr))
            {
                return *ptr;
            }
            return std::nullopt;
        }

        std::optional<std::string> getAttrString(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            return getAttr<std::string>(op, key);
        }

        std::optional<bool> getAttrBool(const wolvrix::lib::grh::Operation &op, std::string_view key)
        {
            return getAttr<bool>(op, key);
        }

        std::optional<std::vector<std::string>> getAttrStrings(const wolvrix::lib::grh::Operation &op,
                                                               std::string_view key)
        {
            return getAttr<std::vector<std::string>>(op, key);
        }

        bool opHasEvents(const wolvrix::lib::grh::Operation &op)
        {
            auto edges = getAttrStrings(op, "eventEdge");
            return edges && !edges->empty();
        }

        bool isSinkOpKind(wolvrix::lib::grh::OperationKind kind)
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegisterWritePort:
            case wolvrix::lib::grh::OperationKind::kLatchWritePort:
            case wolvrix::lib::grh::OperationKind::kMemoryWritePort:
                return true;
            default:
                return false;
            }
        }

        bool isSourceOpKind(wolvrix::lib::grh::OperationKind kind)
        {
            switch (kind)
            {
            case wolvrix::lib::grh::OperationKind::kRegisterReadPort:
            case wolvrix::lib::grh::OperationKind::kLatchReadPort:
                return true;
            default:
                return false;
            }
        }

        bool isCombOp(const wolvrix::lib::grh::Operation &op)
        {
            if (opHasEvents(op))
            {
                return false;
            }
            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
            case wolvrix::lib::grh::OperationKind::kAdd:
            case wolvrix::lib::grh::OperationKind::kSub:
            case wolvrix::lib::grh::OperationKind::kMul:
            case wolvrix::lib::grh::OperationKind::kDiv:
            case wolvrix::lib::grh::OperationKind::kMod:
            case wolvrix::lib::grh::OperationKind::kEq:
            case wolvrix::lib::grh::OperationKind::kNe:
            case wolvrix::lib::grh::OperationKind::kCaseEq:
            case wolvrix::lib::grh::OperationKind::kCaseNe:
            case wolvrix::lib::grh::OperationKind::kWildcardEq:
            case wolvrix::lib::grh::OperationKind::kWildcardNe:
            case wolvrix::lib::grh::OperationKind::kLt:
            case wolvrix::lib::grh::OperationKind::kLe:
            case wolvrix::lib::grh::OperationKind::kGt:
            case wolvrix::lib::grh::OperationKind::kGe:
            case wolvrix::lib::grh::OperationKind::kAnd:
            case wolvrix::lib::grh::OperationKind::kOr:
            case wolvrix::lib::grh::OperationKind::kXor:
            case wolvrix::lib::grh::OperationKind::kXnor:
            case wolvrix::lib::grh::OperationKind::kNot:
            case wolvrix::lib::grh::OperationKind::kLogicAnd:
            case wolvrix::lib::grh::OperationKind::kLogicOr:
            case wolvrix::lib::grh::OperationKind::kLogicNot:
            case wolvrix::lib::grh::OperationKind::kReduceAnd:
            case wolvrix::lib::grh::OperationKind::kReduceOr:
            case wolvrix::lib::grh::OperationKind::kReduceXor:
            case wolvrix::lib::grh::OperationKind::kReduceNor:
            case wolvrix::lib::grh::OperationKind::kReduceNand:
            case wolvrix::lib::grh::OperationKind::kReduceXnor:
            case wolvrix::lib::grh::OperationKind::kShl:
            case wolvrix::lib::grh::OperationKind::kLShr:
            case wolvrix::lib::grh::OperationKind::kAShr:
            case wolvrix::lib::grh::OperationKind::kMux:
            case wolvrix::lib::grh::OperationKind::kAssign:
            case wolvrix::lib::grh::OperationKind::kConcat:
            case wolvrix::lib::grh::OperationKind::kReplicate:
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
            case wolvrix::lib::grh::OperationKind::kSliceArray:
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                return true;
            case wolvrix::lib::grh::OperationKind::kSystemFunction: {
                auto sideEffects = getAttrBool(op, "hasSideEffects");
                return !sideEffects || !*sideEffects;
            }
            default:
                return false;
            }
        }

        bool isPowerOfTwo(std::size_t value)
        {
            return value && ((value & (value - 1)) == 0);
        }

        std::size_t log2Exact(std::size_t value)
        {
            std::size_t result = 0;
            while (value > 1)
            {
                value >>= 1;
                ++result;
            }
            return result;
        }

        double combOpWeight(const wolvrix::lib::grh::Graph &graph,
                            const wolvrix::lib::grh::Operation &op)
        {
            if (!isCombOp(op))
            {
                return 0.0;
            }
            double base = 1.0;
            switch (op.kind())
            {
            case wolvrix::lib::grh::OperationKind::kConstant:
                base = 0.2;
                break;
            case wolvrix::lib::grh::OperationKind::kAdd:
            case wolvrix::lib::grh::OperationKind::kSub:
                base = 1.5;
                break;
            case wolvrix::lib::grh::OperationKind::kMul:
                base = 4.0;
                break;
            case wolvrix::lib::grh::OperationKind::kDiv:
            case wolvrix::lib::grh::OperationKind::kMod:
                base = 6.0;
                break;
            case wolvrix::lib::grh::OperationKind::kMux:
                base = 2.5;
                break;
            case wolvrix::lib::grh::OperationKind::kConcat:
                base = 1.0 + static_cast<double>(op.operands().size()) * 0.5;
                break;
            case wolvrix::lib::grh::OperationKind::kReplicate: {
                auto rep = getAttr<int64_t>(op, "rep");
                base = 1.0 + (rep ? static_cast<double>(*rep) * 0.2 : 0.5);
                break;
            }
            case wolvrix::lib::grh::OperationKind::kSliceStatic:
            case wolvrix::lib::grh::OperationKind::kSliceDynamic:
            case wolvrix::lib::grh::OperationKind::kSliceArray: {
                auto width = getAttr<int64_t>(op, "sliceWidth");
                base = 1.0 + (width ? static_cast<double>(*width) / 128.0 : 0.25);
                break;
            }
            case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
                base = 4.0;
                break;
            default:
                break;
            }
            int32_t width = 0;
            if (!op.results().empty())
            {
                width = graph.valueWidth(op.results().front());
            }
            if (width > 0)
            {
                base += static_cast<double>(width) / 64.0;
            }
            return base;
        }

        double computeBalance(const std::vector<AscInfo> &ascs,
                              const std::vector<std::size_t> &a,
                              const std::vector<std::size_t> &b)
        {
            double wA = 0.0;
            double wB = 0.0;
            for (std::size_t id : a)
            {
                wA += ascs[id].weight;
            }
            for (std::size_t id : b)
            {
                wB += ascs[id].weight;
            }
            return wB / (wA + wB + kBalanceEpsilon);
        }

        double computeOverlap(const std::vector<AscInfo> &ascs,
                              const std::vector<std::size_t> &a,
                              const std::vector<std::size_t> &b,
                              const std::unordered_map<wolvrix::lib::grh::OperationId, double,
                                                       wolvrix::lib::grh::OperationIdHash> &weights)
        {
            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> opsA;
            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> opsB;
            for (std::size_t id : a)
            {
                opsA.insert(ascs[id].combOps.begin(), ascs[id].combOps.end());
            }
            for (std::size_t id : b)
            {
                opsB.insert(ascs[id].combOps.begin(), ascs[id].combOps.end());
            }
            if (opsA.empty() || opsB.empty())
            {
                return 0.0;
            }
            const auto &smaller = opsA.size() <= opsB.size() ? opsA : opsB;
            const auto &larger = opsA.size() <= opsB.size() ? opsB : opsA;
            double overlap = 0.0;
            for (const auto &opId : smaller)
            {
                if (larger.find(opId) != larger.end())
                {
                    auto it = weights.find(opId);
                    overlap += (it != weights.end()) ? it->second : 0.0;
                }
            }
            return overlap;
        }

        std::vector<std::size_t> randomHalf(std::vector<std::size_t> items, std::mt19937 &rng)
        {
            if (items.empty())
            {
                return items;
            }
            std::shuffle(items.begin(), items.end(), rng);
            const std::size_t half = items.size() / 2;
            items.resize(half);
            return items;
        }

        std::pair<std::vector<std::size_t>, std::vector<std::size_t>> randomSplitEven(std::vector<std::size_t> items,
                                                                                       std::mt19937 &rng)
        {
            std::shuffle(items.begin(), items.end(), rng);
            const std::size_t half = items.size() / 2;
            std::vector<std::size_t> a(items.begin(), items.begin() + half);
            std::vector<std::size_t> b(items.begin() + half, items.end());
            return {std::move(a), std::move(b)};
        }

        std::pair<std::vector<std::size_t>, std::vector<std::size_t>> randomSplitRatio(
            const std::vector<std::size_t> &items, double pA, double pB, std::mt19937 &rng)
        {
            std::vector<std::size_t> a;
            std::vector<std::size_t> b;
            a.reserve(items.size());
            b.reserve(items.size());
            const double total = pA + pB;
            const double probA = total > 0.0 ? pA / total : 0.5;
            std::bernoulli_distribution dist(probA);
            for (std::size_t item : items)
            {
                (dist(rng) ? a : b).push_back(item);
            }
            if (items.size() > 1)
            {
                if (a.empty())
                {
                    a.push_back(b.back());
                    b.pop_back();
                }
                if (b.empty())
                {
                    b.push_back(a.back());
                    a.pop_back();
                }
            }
            return {std::move(a), std::move(b)};
        }

        PartitionSolution splitRecursiveBalance(const std::vector<AscInfo> &ascs,
                                                const std::vector<std::size_t> &ascIds,
                                                std::size_t splitStopThreshold,
                                                std::mt19937 &rng)
        {
            PartitionSolution sol;
            if (ascIds.size() <= 1)
            {
                sol.a = ascIds;
                return sol;
            }

            std::vector<std::size_t> H = randomHalf(ascIds, rng);
            std::unordered_set<std::size_t> halfSet(H.begin(), H.end());
            std::vector<std::size_t> R;
            R.reserve(ascIds.size() - H.size());
            for (std::size_t id : ascIds)
            {
                if (halfSet.find(id) == halfSet.end())
                {
                    R.push_back(id);
                }
            }

            auto split = randomSplitEven(std::move(H), rng);
            std::vector<std::size_t> A = std::move(split.first);
            std::vector<std::size_t> B = std::move(split.second);

            while (R.size() > splitStopThreshold)
            {
                const double balance = computeBalance(ascs, A, B);
                const double pB = balance;
                const double pA = 1.0 - pB;
                std::vector<std::size_t> C = randomHalf(R, rng);
                std::unordered_set<std::size_t> cSet(C.begin(), C.end());
                std::vector<std::size_t> nextR;
                nextR.reserve(R.size() - C.size());
                for (std::size_t id : R)
                {
                    if (cSet.find(id) == cSet.end())
                    {
                        nextR.push_back(id);
                    }
                }
                auto cSplit = randomSplitRatio(C, pA, pB, rng);
                A.insert(A.end(), cSplit.first.begin(), cSplit.first.end());
                B.insert(B.end(), cSplit.second.begin(), cSplit.second.end());
                R = std::move(nextR);
            }

            const double balance = computeBalance(ascs, A, B);
            const double pB = balance;
            const double pA = 1.0 - pB;
            auto rSplit = randomSplitRatio(R, pA, pB, rng);
            A.insert(A.end(), rSplit.first.begin(), rSplit.first.end());
            B.insert(B.end(), rSplit.second.begin(), rSplit.second.end());

            sol.a = std::move(A);
            sol.b = std::move(B);
            sol.balance = computeBalance(ascs, sol.a, sol.b);
            return sol;
        }

        PartitionSolution multiSampleSelect(const std::vector<AscInfo> &ascs,
                                            const std::vector<std::size_t> &ascIds,
                                            double balanceThreshold,
                                            std::size_t targetCandidateCount,
                                            std::size_t maxTrials,
                                            std::size_t splitStopThreshold,
                                            const std::unordered_map<wolvrix::lib::grh::OperationId, double,
                                                                     wolvrix::lib::grh::OperationIdHash> &weights,
                                            std::mt19937 &rng,
                                            const std::function<void(std::string)> &log)
        {
            PartitionSolution best;
            bool bestSet = false;
            std::vector<PartitionSolution> accepted;
            accepted.reserve(targetCandidateCount);

            if (maxTrials == 0)
            {
                return best;
            }

            if (log)
            {
                log("hrbcut: sampling start asc=" + std::to_string(ascIds.size()) +
                    " trials=" + std::to_string(maxTrials) +
                    " target=" + std::to_string(targetCandidateCount) +
                    " split_stop=" + std::to_string(splitStopThreshold));
            }

            const uint64_t baseSeed = (static_cast<uint64_t>(rng()) << 32) ^ static_cast<uint64_t>(rng());
            std::vector<PartitionSolution> trials(maxTrials);
            struct TrialLocal
            {
            };
            std::vector<TrialLocal> trialLocals;
            runParallelTasks(maxTrials, trialLocals, [&](std::size_t index, TrialLocal &) {
                const uint64_t seed = baseSeed ^
                    (0x9e3779b97f4a7c15ULL + static_cast<uint64_t>(index) * 0xbf58476d1ce4e5b9ULL);
                std::seed_seq seq{
                    static_cast<uint32_t>(seed),
                    static_cast<uint32_t>(seed >> 32),
                    static_cast<uint32_t>(index)};
                std::mt19937 localRng(seq);
                trials[index] = splitRecursiveBalance(ascs, ascIds, splitStopThreshold, localRng);
            });

            for (auto &trial : trials)
            {
                trial.balance = computeBalance(ascs, trial.a, trial.b);
                if (!bestSet || std::abs(trial.balance - 0.5) < std::abs(best.balance - 0.5))
                {
                    best = trial;
                    bestSet = true;
                }
                if (std::abs(trial.balance - 0.5) <= balanceThreshold)
                {
                    accepted.push_back(trial);
                    if (accepted.size() >= targetCandidateCount)
                    {
                        break;
                    }
                }
            }

            if (log)
            {
                log("hrbcut: sampling done accepted=" + std::to_string(accepted.size()) +
                    " best_balance=" + std::to_string(best.balance));
            }

            if (accepted.size() < targetCandidateCount && bestSet)
            {
                accepted.push_back(best);
            }

            if (accepted.empty())
            {
                return best;
            }

            if (log)
            {
                log("hrbcut: overlap eval candidates=" + std::to_string(accepted.size()));
            }

            for (auto &sol : accepted)
            {
                sol.overlap = computeOverlap(ascs, sol.a, sol.b, weights);
            }

            auto it = std::min_element(accepted.begin(), accepted.end(),
                                       [](const PartitionSolution &lhs, const PartitionSolution &rhs) {
                                           return lhs.overlap < rhs.overlap;
                                       });
            return (it != accepted.end()) ? *it : best;
        }

        std::string escapeJson(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (char ch : text)
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
                    if (static_cast<unsigned char>(ch) < 0x20)
                    {
                        const char *digits = "0123456789abcdef";
                        out.append("\\u00");
                        out.push_back(digits[(ch >> 4) & 0x0f]);
                        out.push_back(digits[ch & 0x0f]);
                    }
                    else
                    {
                        out.push_back(ch);
                    }
                    break;
                }
            }
            return out;
        }

        struct ValueInfo
        {
            std::string symbol;
            int32_t width = 0;
            bool isSigned = false;
            wolvrix::lib::grh::ValueType type = wolvrix::lib::grh::ValueType::Logic;
            std::optional<wolvrix::lib::grh::SrcLoc> srcLoc;
        };

        ValueInfo captureValueInfo(const wolvrix::lib::grh::Graph &graph, wolvrix::lib::grh::ValueId valueId)
        {
            const auto value = graph.getValue(valueId);
            ValueInfo info;
            info.symbol = std::string(value.symbolText());
            info.width = value.width();
            info.isSigned = value.isSigned();
            info.type = value.type();
            info.srcLoc = value.srcLoc();
            return info;
        }

        std::string normalizePortBase(std::string_view text, std::string_view fallback)
        {
            std::string normalized = wolvrix::lib::grh::Graph::normalizeComponent(text);
            if (normalized.empty())
            {
                normalized = std::string(fallback);
            }
            if (!normalized.empty() && normalized.front() >= '0' && normalized.front() <= '9')
            {
                normalized.insert(normalized.begin(), '_');
            }
            return normalized;
        }

        std::string uniquePortName(std::unordered_set<std::string> &used, std::string base, bool &conflict)
        {
            conflict = false;
            std::string candidate = std::move(base);
            if (used.insert(candidate).second)
            {
                return candidate;
            }
            conflict = true;
            std::string root = candidate;
            int suffix = 0;
            while (true)
            {
                candidate = root + "_" + std::to_string(++suffix);
                if (used.insert(candidate).second)
                {
                    return candidate;
                }
            }
        }

        std::string uniqueGraphName(wolvrix::lib::grh::Design &design, std::string base)
        {
            std::string candidate = std::move(base);
            int suffix = 0;
            while (design.findGraph(candidate) != nullptr)
            {
                candidate = base + "_" + std::to_string(++suffix);
            }
            return candidate;
        }

        wolvrix::lib::grh::SymbolId internUniqueSymbol(wolvrix::lib::grh::Graph &graph, std::string base)
        {
            if (base.empty())
            {
                base = "value";
            }
            std::string candidate = std::move(base);
            int suffix = 0;
            while (true)
            {
                wolvrix::lib::grh::SymbolId sym = graph.internSymbol(candidate);
                if (!sym.valid())
                {
                    candidate = base + "_" + std::to_string(++suffix);
                    continue;
                }
                if (!graph.findValue(sym).valid() && !graph.findOperation(sym).valid())
                {
                    return sym;
                }
                candidate = base + "_" + std::to_string(++suffix);
            }
        }

        wolvrix::lib::grh::OperationId buildInstance(
            wolvrix::lib::grh::Graph &parent,
            std::string_view moduleName,
            std::string_view instanceBase,
            const wolvrix::lib::grh::Graph &target,
            const std::unordered_map<std::string, wolvrix::lib::grh::ValueId> &inputMapping,
            const std::unordered_map<std::string, wolvrix::lib::grh::ValueId> &outputMapping)
        {
            std::string opBase = std::string("inst_") + std::string(instanceBase);
            wolvrix::lib::grh::SymbolId opSym = internUniqueSymbol(parent, opBase);
            wolvrix::lib::grh::OperationId inst = parent.createOperation(wolvrix::lib::grh::OperationKind::kInstance, opSym);
            parent.setAttr(inst, "moduleName", std::string(moduleName));

            std::vector<std::string> inputNames;
            inputNames.reserve(target.inputPorts().size());
            for (const auto &port : target.inputPorts())
            {
                inputNames.push_back(port.name);
            }
            std::vector<std::string> outputNames;
            outputNames.reserve(target.outputPorts().size());
            for (const auto &port : target.outputPorts())
            {
                outputNames.push_back(port.name);
            }
            parent.setAttr(inst, "inputPortName", inputNames);
            parent.setAttr(inst, "outputPortName", outputNames);
            parent.setAttr(inst, "instanceName", std::string(instanceBase));

            for (const auto &portName : inputNames)
            {
                auto it = inputMapping.find(portName);
                if (it != inputMapping.end())
                {
                    parent.addOperand(inst, it->second);
                }
            }
            for (const auto &portName : outputNames)
            {
                auto it = outputMapping.find(portName);
                if (it != outputMapping.end())
                {
                    parent.addResult(inst, it->second);
                }
            }
            return inst;
        }

    } // namespace

    HrbcutPass::HrbcutPass()
        : Pass("hrbcut", "hrbcut", "Partition a single graph via hierarchical balance cutting"),
          options_({})
    {
    }

    HrbcutPass::HrbcutPass(HrbcutOptions options)
        : Pass("hrbcut", "hrbcut", "Partition a single graph via hierarchical balance cutting"),
          options_(std::move(options))
    {
    }

    PassResult HrbcutPass::run()
    {
        PassResult result;
        const auto totalStart = std::chrono::steady_clock::now();

        if (options_.targetGraphSymbol.empty())
        {
            error("hrbcut requires -target-graph to select a graph symbol");
            result.failed = true;
            return result;
        }
        if (!isPowerOfTwo(options_.partitionCount))
        {
            error("hrbcut partition_count must be a power of two");
            result.failed = true;
            return result;
        }
        if (options_.targetCandidateCount == 0 || options_.maxTrials == 0)
        {
            error("hrbcut target_candidate_count and max_trials must be non-zero");
            result.failed = true;
            return result;
        }
        if (options_.balanceThreshold < 0.0 || options_.balanceThreshold > 0.5)
        {
            error("hrbcut balance_threshold must be within [0, 0.5]");
            result.failed = true;
            return result;
        }

        wolvrix::lib::grh::Graph *graph = design().findGraph(options_.targetGraphSymbol);
        if (!graph)
        {
            error("hrbcut target graph not found: " + options_.targetGraphSymbol);
            result.failed = true;
            return result;
        }
        logInfo("hrbcut: start graph=" + graph->symbol());

        const auto ops = graph->operations();
        const auto values = graph->values();

        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> inoutInputs;
        std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> inoutOutputs;
        for (const auto &port : graph->inoutPorts())
        {
            inoutInputs.insert(port.in);
            inoutOutputs.insert(port.out);
            inoutOutputs.insert(port.oe);
        }

        for (const auto opId : ops)
        {
            const auto op = graph->getOperation(opId);
            if (op.kind() == wolvrix::lib::grh::OperationKind::kSystemTask ||
                op.kind() == wolvrix::lib::grh::OperationKind::kDpicCall)
            {
                error(*graph, op, "strip-debug should remove system tasks/dpi calls before hrbcut");
                result.failed = true;
            }
        }
        if (result.failed)
        {
            return result;
        }

        const auto sinkStart = std::chrono::steady_clock::now();
        std::vector<SinkRef> sinks;
        sinks.reserve(ops.size());
        std::unordered_map<wolvrix::lib::grh::OperationId, std::size_t, wolvrix::lib::grh::OperationIdHash> sinkIndexByOp;
        std::size_t sinkOpCount = 0;
        std::size_t sinkOutputCount = 0;

        for (const auto opId : ops)
        {
            const auto op = graph->getOperation(opId);
            if (isSinkOpKind(op.kind()))
            {
                SinkRef sink;
                sink.kind = SinkRef::Kind::Operation;
                sink.op = opId;
                sinks.push_back(sink);
                sinkIndexByOp.emplace(opId, sinks.size() - 1);
                ++sinkOpCount;
            }
        }
        for (const auto valueId : values)
        {
            if (graph->valueIsOutput(valueId) || inoutOutputs.find(valueId) != inoutOutputs.end())
            {
                SinkRef sink;
                sink.kind = SinkRef::Kind::OutputValue;
                sink.value = valueId;
                sinks.push_back(sink);
                ++sinkOutputCount;
            }
        }

        if (sinks.empty())
        {
            logInfo("hrbcut: no sinks found, skipping");
            return result;
        }
        logInfo("hrbcut: sink discovery done ops=" + std::to_string(sinkOpCount) +
                " outputs=" + std::to_string(sinkOutputCount) +
                " total=" + std::to_string(sinks.size()) +
                " in " +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - sinkStart)
                                   .count()) +
                "ms");

        std::unordered_map<std::string, std::vector<std::size_t>> regWriteSinks;
        std::unordered_map<std::string, std::vector<std::size_t>> latchWriteSinks;
        std::unordered_map<std::string, std::vector<std::size_t>> memWriteSinks;

        for (const auto &entry : sinkIndexByOp)
        {
            const auto op = graph->getOperation(entry.first);
            std::optional<std::string> sym;
            if (op.kind() == wolvrix::lib::grh::OperationKind::kRegisterWritePort)
            {
                sym = getAttrString(op, "regSymbol");
                if (!sym)
                {
                    warning(*graph, op, "kRegisterWritePort missing regSymbol");
                    continue;
                }
                regWriteSinks[*sym].push_back(entry.second);
            }
            else if (op.kind() == wolvrix::lib::grh::OperationKind::kLatchWritePort)
            {
                sym = getAttrString(op, "latchSymbol");
                if (!sym)
                {
                    warning(*graph, op, "kLatchWritePort missing latchSymbol");
                    continue;
                }
                latchWriteSinks[*sym].push_back(entry.second);
            }
            else if (op.kind() == wolvrix::lib::grh::OperationKind::kMemoryWritePort)
            {
                sym = getAttrString(op, "memSymbol");
                if (!sym)
                {
                    warning(*graph, op, "kMemoryWritePort missing memSymbol");
                    continue;
                }
                memWriteSinks[*sym].push_back(entry.second);
            }
        }

        const auto coneStart = std::chrono::steady_clock::now();
        std::vector<ConeInfo> sinkCones(sinks.size());
        struct ConeLocal
        {
            std::vector<wolvrix::lib::grh::OperationId> missingMemSymbols;
        };

        auto traverseValue = [&](auto &&self,
                                 ConeInfo &cone,
                                 wolvrix::lib::grh::ValueId valueId,
                                 ConeLocal *local) -> void {
            if (!valueId.valid())
            {
                return;
            }
            if (!cone.values.insert(valueId).second)
            {
                return;
            }
            if (graph->valueIsInput(valueId) || inoutInputs.find(valueId) != inoutInputs.end())
            {
                return;
            }
            const wolvrix::lib::grh::OperationId defOpId = graph->valueDef(valueId);
            if (!defOpId.valid())
            {
                return;
            }
            const auto defOp = graph->getOperation(defOpId);
            if (isSourceOpKind(defOp.kind()))
            {
                return;
            }
            if (!isCombOp(defOp))
            {
                return;
            }
            if (!cone.combOps.insert(defOpId).second)
            {
                return;
            }
            if (defOp.kind() == wolvrix::lib::grh::OperationKind::kMemoryReadPort)
            {
                auto memSym = getAttrString(defOp, "memSymbol");
                if (!memSym)
                {
                    if (local)
                    {
                        local->missingMemSymbols.push_back(defOpId);
                    }
                }
                else
                {
                    cone.memSymbols.insert(*memSym);
                }
            }
            for (const auto operand : defOp.operands())
            {
                self(self, cone, operand, local);
            }
        };

        std::vector<ConeLocal> coneLocals;
        runParallelTasks(sinks.size(), coneLocals, [&](std::size_t index, ConeLocal &local) {
            ConeInfo &cone = sinkCones[index];
            const SinkRef &sink = sinks[index];
            if (sink.kind == SinkRef::Kind::Operation)
            {
                const auto op = graph->getOperation(sink.op);
                for (const auto operand : op.operands())
                {
                    traverseValue(traverseValue, cone, operand, &local);
                }
            }
            else
            {
                traverseValue(traverseValue, cone, sink.value, &local);
            }
        });

        std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> missingMemOps;
        for (const auto &local : coneLocals)
        {
            for (const auto &opId : local.missingMemSymbols)
            {
                missingMemOps.insert(opId);
            }
        }
        for (const auto &opId : missingMemOps)
        {
            if (opId.valid())
            {
                warning(*graph, graph->getOperation(opId), "kMemoryReadPort missing memSymbol");
            }
        }
        std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> coneCombOps;
        coneCombOps.reserve(ops.size());
        for (const auto &cone : sinkCones)
        {
            coneCombOps.insert(cone.combOps.begin(), cone.combOps.end());
        }
        logInfo("hrbcut: cone build done comb_ops=" + std::to_string(coneCombOps.size()) + " in " +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - coneStart)
                                   .count()) +
                "ms");

        const auto ascStart = std::chrono::steady_clock::now();
        DisjointSet dsu(sinks.size());

        auto unionGroup = [&](const std::unordered_map<std::string, std::vector<std::size_t>> &groups) {
            for (const auto &entry : groups)
            {
                const auto &indices = entry.second;
                if (indices.size() < 2)
                {
                    continue;
                }
                const std::size_t root = indices.front();
                for (std::size_t i = 1; i < indices.size(); ++i)
                {
                    dsu.unite(root, indices[i]);
                }
            }
        };

        unionGroup(regWriteSinks);
        unionGroup(latchWriteSinks);
        unionGroup(memWriteSinks);

        for (std::size_t i = 0; i < sinks.size(); ++i)
        {
            for (const auto &memSym : sinkCones[i].memSymbols)
            {
                auto it = memWriteSinks.find(memSym);
                if (it == memWriteSinks.end())
                {
                    continue;
                }
                const auto &indices = it->second;
                for (std::size_t idx : indices)
                {
                    dsu.unite(i, idx);
                }
            }
        }

        std::unordered_map<std::size_t, std::size_t> ascIdByRoot;
        std::vector<AscInfo> ascs;
        ascs.reserve(sinks.size());

        for (std::size_t i = 0; i < sinks.size(); ++i)
        {
            const std::size_t root = dsu.find(i);
            auto it = ascIdByRoot.find(root);
            if (it == ascIdByRoot.end())
            {
                const std::size_t newId = ascs.size();
                ascIdByRoot.emplace(root, newId);
                ascs.push_back(AscInfo{});
                it = ascIdByRoot.find(root);
            }
            ascs[it->second].sinks.push_back(i);
        }
        logInfo("hrbcut: ASC build done count=" + std::to_string(ascs.size()) + " in " +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - ascStart)
                                   .count()) +
                "ms");

        std::unordered_map<wolvrix::lib::grh::OperationId, double, wolvrix::lib::grh::OperationIdHash> opWeights;
        opWeights.reserve(ops.size());

        for (auto &asc : ascs)
        {
            for (std::size_t sinkIdx : asc.sinks)
            {
                const auto &cone = sinkCones[sinkIdx];
                asc.combOps.insert(cone.combOps.begin(), cone.combOps.end());
            }
            double weight = 0.0;
            for (const auto &opId : asc.combOps)
            {
                auto it = opWeights.find(opId);
                if (it == opWeights.end())
                {
                    const auto op = graph->getOperation(opId);
                    const double w = combOpWeight(*graph, op);
                    it = opWeights.emplace(opId, w).first;
                }
                weight += it->second;
            }
            asc.weight = weight;
        }

        if (options_.partitionCount > ascs.size())
        {
            error(*graph, "hrbcut partition_count exceeds ASC count");
            result.failed = true;
            return result;
        }

        const std::size_t depth = log2Exact(options_.partitionCount);
        std::vector<std::vector<std::size_t>> partitions;
        partitions.reserve(options_.partitionCount);

        std::vector<std::size_t> ascIds;
        ascIds.reserve(ascs.size());
        for (std::size_t i = 0; i < ascs.size(); ++i)
        {
            ascIds.push_back(i);
        }

        std::mt19937 rng(static_cast<uint32_t>(std::hash<std::string>{}(graph->symbol())) ^ 0x9e3779b9U);

        const auto splitStart = std::chrono::steady_clock::now();
        std::function<void(const std::vector<std::size_t> &, std::size_t)> recurse;
        recurse = [&](const std::vector<std::size_t> &current, std::size_t remainingDepth) {
            if (remainingDepth == 0 || current.size() <= 1)
            {
                partitions.push_back(current);
                return;
            }
            logInfo("hrbcut: split depth=" + std::to_string(remainingDepth) +
                    " asc=" + std::to_string(current.size()));
            PartitionSolution split = multiSampleSelect(
                ascs,
                current,
                options_.balanceThreshold,
                options_.targetCandidateCount,
                options_.maxTrials,
                options_.splitStopThreshold,
                opWeights,
                rng,
                [&](std::string msg) { logInfo(std::move(msg)); });
            logInfo("hrbcut: split result depth=" + std::to_string(remainingDepth) +
                    " balance=" + std::to_string(split.balance) +
                    " overlap=" + std::to_string(split.overlap) +
                    " A=" + std::to_string(split.a.size()) +
                    " B=" + std::to_string(split.b.size()));
            if (split.a.empty() || split.b.empty())
            {
                warning(*graph, "hrbcut produced empty partition during split; stopping recursion");
                partitions.push_back(current);
                return;
            }
            recurse(split.a, remainingDepth - 1);
            recurse(split.b, remainingDepth - 1);
        };

        recurse(ascIds, depth);
        logInfo("hrbcut: partitioning done parts=" + std::to_string(partitions.size()) + " in " +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - splitStart)
                                   .count()) +
                "ms");

        std::vector<double> partitionWeights;
        std::vector<std::size_t> partitionAscCounts;
        std::vector<std::size_t> partitionCombCounts;
        std::vector<std::size_t> partitionSinkOpCounts;
        std::vector<std::size_t> partitionOpCountsEst;
        std::vector<std::size_t> partitionOpCountsActual;
        partitionWeights.reserve(partitions.size());
        partitionAscCounts.reserve(partitions.size());
        partitionCombCounts.reserve(partitions.size());
        partitionSinkOpCounts.reserve(partitions.size());
        partitionOpCountsEst.reserve(partitions.size());
        partitionOpCountsActual.reserve(partitions.size());

        std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> combOpsAll;
        for (const auto &asc : ascs)
        {
            combOpsAll.insert(asc.combOps.begin(), asc.combOps.end());
        }

        std::size_t combOpsAfter = 0;
        for (const auto &part : partitions)
        {
            double weight = 0.0;
            std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> combOps;
            std::size_t sinkOps = 0;
            for (std::size_t id : part)
            {
                weight += ascs[id].weight;
                combOps.insert(ascs[id].combOps.begin(), ascs[id].combOps.end());
                for (std::size_t sinkIdx : ascs[id].sinks)
                {
                    if (sinks[sinkIdx].kind == SinkRef::Kind::Operation)
                    {
                        ++sinkOps;
                    }
                }
            }
            combOpsAfter += combOps.size();
            partitionWeights.push_back(weight);
            partitionAscCounts.push_back(part.size());
            partitionCombCounts.push_back(combOps.size());
            partitionSinkOpCounts.push_back(sinkOps);
            partitionOpCountsEst.push_back(combOps.size() + sinkOps);
        }

        double maxWeight = 0.0;
        double meanWeight = 0.0;
        std::size_t maxPartitionOpsEst = 0;
        if (!partitionWeights.empty())
        {
            for (std::size_t i = 0; i < partitionWeights.size(); ++i)
            {
                const double weight = partitionWeights[i];
                meanWeight += weight;
                if (weight > maxWeight)
                {
                    maxWeight = weight;
                }
                if (partitionOpCountsEst[i] > maxPartitionOpsEst)
                {
                    maxPartitionOpsEst = partitionOpCountsEst[i];
                }
            }
            meanWeight /= static_cast<double>(partitionWeights.size());
        }

        const std::size_t originalOpCount = ops.size();
        const std::size_t combOpsUnique = combOpsAll.size();
        const std::size_t statefulOps = originalOpCount > combOpsUnique ? (originalOpCount - combOpsUnique) : 0U;
        const std::size_t estimatedTotalOps = statefulOps + combOpsAfter;
        const std::size_t estimatedIncrease = estimatedTotalOps > originalOpCount ? (estimatedTotalOps - originalOpCount) : 0U;

        std::ostringstream oss;
        oss << "{";
        oss << "\"graph\":\"" << escapeJson(graph->symbol()) << "\"";
        oss << ",\"partition_count\":" << partitions.size();
        oss << ",\"asc_count\":" << ascs.size();
        oss << ",\"sink_count\":" << sinks.size();
        oss << ",\"weight_max\":" << maxWeight;
        oss << ",\"weight_mean\":" << meanWeight;
        oss << ",\"weight_skew\":" << (meanWeight > 0.0 ? (maxWeight / meanWeight) : 0.0);
        oss << ",\"comb_ops_unique\":" << combOpsUnique;
        oss << ",\"comb_ops_after\":" << combOpsAfter;
        oss << ",\"op_count_original\":" << originalOpCount;
        oss << ",\"op_count_after_est\":" << estimatedTotalOps;
        oss << ",\"op_count_increase_est\":" << estimatedIncrease;
        oss << ",\"partition_op_count_max_est\":" << maxPartitionOpsEst;

        auto appendArray = [&](std::string_view name, const auto &valuesVec) {
            oss << ",\"" << name << "\":[";
            for (std::size_t i = 0; i < valuesVec.size(); ++i)
            {
                if (i)
                {
                    oss << ",";
                }
                oss << valuesVec[i];
            }
            oss << "]";
        };

        appendArray("partition_weights", partitionWeights);
        appendArray("partition_asc_counts", partitionAscCounts);
        appendArray("partition_comb_ops", partitionCombCounts);
        appendArray("partition_sink_ops", partitionSinkOpCounts);
        appendArray("partition_op_count_est", partitionOpCountsEst);

        std::size_t maxPartitionOpCountActual = 0;

        if (options_.partitionCount > 1)
        {
            logInfo("hrbcut: graph splitting start");

            struct StorageInfo
            {
                wolvrix::lib::grh::OperationId declOp = wolvrix::lib::grh::OperationId::invalid();
                std::vector<wolvrix::lib::grh::OperationId> readPorts;
                std::vector<wolvrix::lib::grh::OperationId> writePorts;
            };

            std::unordered_map<std::string, StorageInfo> regInfos;
            std::unordered_map<std::string, StorageInfo> latchInfos;
            std::unordered_map<std::string, StorageInfo> memInfos;

            auto ensureStorage = [](auto &map, const std::string &key) -> StorageInfo & {
                auto it = map.find(key);
                if (it == map.end())
                {
                    it = map.emplace(key, StorageInfo{}).first;
                }
                return it->second;
            };

            for (const auto opId : ops)
            {
                const auto op = graph->getOperation(opId);
                switch (op.kind())
                {
                case wolvrix::lib::grh::OperationKind::kRegister: {
                    std::string sym = std::string(op.symbolText());
                    if (!sym.empty())
                    {
                        ensureStorage(regInfos, sym).declOp = opId;
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kLatch: {
                    std::string sym = std::string(op.symbolText());
                    if (!sym.empty())
                    {
                        ensureStorage(latchInfos, sym).declOp = opId;
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kMemory: {
                    std::string sym = std::string(op.symbolText());
                    if (!sym.empty())
                    {
                        ensureStorage(memInfos, sym).declOp = opId;
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kRegisterReadPort: {
                    auto sym = getAttrString(op, "regSymbol");
                    if (sym)
                    {
                        ensureStorage(regInfos, *sym).readPorts.push_back(opId);
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kRegisterWritePort: {
                    auto sym = getAttrString(op, "regSymbol");
                    if (sym)
                    {
                        ensureStorage(regInfos, *sym).writePorts.push_back(opId);
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kLatchReadPort: {
                    auto sym = getAttrString(op, "latchSymbol");
                    if (sym)
                    {
                        ensureStorage(latchInfos, *sym).readPorts.push_back(opId);
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kLatchWritePort: {
                    auto sym = getAttrString(op, "latchSymbol");
                    if (sym)
                    {
                        ensureStorage(latchInfos, *sym).writePorts.push_back(opId);
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kMemoryReadPort: {
                    auto sym = getAttrString(op, "memSymbol");
                    if (sym)
                    {
                        ensureStorage(memInfos, *sym).readPorts.push_back(opId);
                    }
                    break;
                }
                case wolvrix::lib::grh::OperationKind::kMemoryWritePort: {
                    auto sym = getAttrString(op, "memSymbol");
                    if (sym)
                    {
                        ensureStorage(memInfos, *sym).writePorts.push_back(opId);
                    }
                    break;
                }
                default:
                    break;
                }
            }

            std::unordered_map<std::string, std::size_t> regPartition;
            std::unordered_map<std::string, std::size_t> latchPartition;
            std::unordered_map<std::string, std::size_t> memPartition;

            std::unordered_map<wolvrix::lib::grh::ValueId, ValueInfo, wolvrix::lib::grh::ValueIdHash> valueInfos;
            valueInfos.reserve(values.size());
            for (const auto valueId : values)
            {
                valueInfos.emplace(valueId, captureValueInfo(*graph, valueId));
            }

            auto assignPartition = [&](auto &map, const std::string &sym, std::size_t part,
                                       const wolvrix::lib::grh::Operation &op) {
                auto it = map.find(sym);
                if (it == map.end())
                {
                    map.emplace(sym, part);
                }
                else if (it->second != part)
                {
                    warning(*graph, op, "hrbcut: storage symbol appears in multiple partitions: " + sym);
                }
            };

            for (std::size_t p = 0; p < partitions.size(); ++p)
            {
                for (std::size_t ascId : partitions[p])
                {
                    for (std::size_t sinkIdx : ascs[ascId].sinks)
                    {
                        const SinkRef &sink = sinks[sinkIdx];
                        if (sink.kind != SinkRef::Kind::Operation)
                        {
                            continue;
                        }
                        const auto op = graph->getOperation(sink.op);
                        if (op.kind() == wolvrix::lib::grh::OperationKind::kRegisterWritePort)
                        {
                            if (auto sym = getAttrString(op, "regSymbol"))
                            {
                                assignPartition(regPartition, *sym, p, op);
                            }
                        }
                        else if (op.kind() == wolvrix::lib::grh::OperationKind::kLatchWritePort)
                        {
                            if (auto sym = getAttrString(op, "latchSymbol"))
                            {
                                assignPartition(latchPartition, *sym, p, op);
                            }
                        }
                        else if (op.kind() == wolvrix::lib::grh::OperationKind::kMemoryWritePort)
                        {
                            if (auto sym = getAttrString(op, "memSymbol"))
                            {
                                assignPartition(memPartition, *sym, p, op);
                            }
                        }
                    }
                }
            }

            std::vector<std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash>>
                partitionOps(partitions.size());

            for (std::size_t p = 0; p < partitions.size(); ++p)
            {
                auto &opSet = partitionOps[p];
                for (std::size_t ascId : partitions[p])
                {
                    opSet.insert(ascs[ascId].combOps.begin(), ascs[ascId].combOps.end());
                    for (std::size_t sinkIdx : ascs[ascId].sinks)
                    {
                        if (sinks[sinkIdx].kind == SinkRef::Kind::Operation)
                        {
                            opSet.insert(sinks[sinkIdx].op);
                        }
                    }
                }
            }

            auto addStorageOps = [&](const auto &storageInfos, const auto &partitionMap) {
                for (const auto &entry : partitionMap)
                {
                    const std::string &sym = entry.first;
                    const std::size_t part = entry.second;
                    auto it = storageInfos.find(sym);
                    if (it == storageInfos.end())
                    {
                        continue;
                    }
                    auto &opSet = partitionOps[part];
                    if (it->second.declOp.valid())
                    {
                        opSet.insert(it->second.declOp);
                    }
                    for (const auto opId : it->second.readPorts)
                    {
                        opSet.insert(opId);
                    }
                    for (const auto opId : it->second.writePorts)
                    {
                        opSet.insert(opId);
                    }
                }
            };

            addStorageOps(regInfos, regPartition);
            addStorageOps(latchInfos, latchPartition);
            addStorageOps(memInfos, memPartition);

            struct DefInfo
            {
                uint32_t owner = std::numeric_limits<uint32_t>::max();
                uint32_t count = 0;
            };

            std::unordered_map<wolvrix::lib::grh::ValueId, DefInfo, wolvrix::lib::grh::ValueIdHash> defInfo;
            defInfo.reserve(values.size());

            for (std::size_t p = 0; p < partitionOps.size(); ++p)
            {
                for (const auto opId : partitionOps[p])
                {
                    const auto op = graph->getOperation(opId);
                    for (const auto result : op.results())
                    {
                        if (!result.valid())
                        {
                            continue;
                        }
                        auto &info = defInfo[result];
                        if (info.count == 0)
                        {
                            info.owner = static_cast<uint32_t>(p);
                            info.count = 1;
                        }
                        else if (info.owner != p)
                        {
                            info.owner = std::numeric_limits<uint32_t>::max();
                            info.count++;
                        }
                    }
                }
            }

            std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> origInputs;
            std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> origOutputs;
            for (const auto &port : graph->inputPorts())
            {
                origInputs.insert(port.value);
            }
            for (const auto &port : graph->outputPorts())
            {
                origOutputs.insert(port.value);
            }
            for (const auto &port : graph->inoutPorts())
            {
                origInputs.insert(port.in);
                origOutputs.insert(port.out);
                origOutputs.insert(port.oe);
            }

            std::unordered_map<wolvrix::lib::grh::ValueId, bool, wolvrix::lib::grh::ValueIdHash> usedByOther;
            for (std::size_t p = 0; p < partitionOps.size(); ++p)
            {
                for (const auto opId : partitionOps[p])
                {
                    const auto op = graph->getOperation(opId);
                    for (const auto operand : op.operands())
                    {
                        auto it = defInfo.find(operand);
                        if (it == defInfo.end())
                        {
                            continue;
                        }
                        if (it->second.owner != std::numeric_limits<uint32_t>::max() &&
                            it->second.owner != p)
                        {
                            usedByOther[operand] = true;
                        }
                    }
                }
            }

            struct PartitionGraphInfo
            {
                wolvrix::lib::grh::Graph *graph = nullptr;
                std::string name;
                std::unordered_map<wolvrix::lib::grh::ValueId, std::string, wolvrix::lib::grh::ValueIdHash>
                    inputPortByValue;
                std::unordered_map<wolvrix::lib::grh::ValueId, std::string, wolvrix::lib::grh::ValueIdHash>
                    outputPortByValue;
            };

            std::vector<PartitionGraphInfo> partInfos;
            partInfos.reserve(partitions.size());

            for (std::size_t p = 0; p < partitions.size(); ++p)
            {
                std::string partName = uniqueGraphName(design(), graph->symbol() + "_p" + std::to_string(p));
                wolvrix::lib::grh::Graph &partGraph = design().cloneGraph(graph->symbol(), partName);

                std::unordered_map<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueId,
                                   wolvrix::lib::grh::ValueIdHash>
                    valueMap;
                std::unordered_map<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationId,
                                   wolvrix::lib::grh::OperationIdHash>
                    opMap;

                auto srcValues = graph->values();
                auto dstValues = partGraph.values();
                for (std::size_t i = 0; i < srcValues.size() && i < dstValues.size(); ++i)
                {
                    valueMap.emplace(srcValues[i], dstValues[i]);
                }
                auto srcOps = graph->operations();
                auto dstOps = partGraph.operations();
                for (std::size_t i = 0; i < srcOps.size() && i < dstOps.size(); ++i)
                {
                    opMap.emplace(srcOps[i], dstOps[i]);
                }

                for (const auto &port : partGraph.inputPorts())
                {
                    partGraph.removeInputPort(port.name);
                }
                for (const auto &port : partGraph.outputPorts())
                {
                    partGraph.removeOutputPort(port.name);
                }
                for (const auto &port : partGraph.inoutPorts())
                {
                    partGraph.removeInoutPort(port.name);
                }

                std::unordered_set<std::string> usedPortNames;
                std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> inputValues;
                std::unordered_set<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueIdHash> outputValues;

                for (const auto opId : partitionOps[p])
                {
                    const auto op = graph->getOperation(opId);
                    for (const auto operand : op.operands())
                    {
                        if (!operand.valid())
                        {
                            continue;
                        }
                        if (origInputs.find(operand) != origInputs.end())
                        {
                            inputValues.insert(operand);
                            continue;
                        }
                        auto it = defInfo.find(operand);
                        if (it == defInfo.end())
                        {
                            inputValues.insert(operand);
                            continue;
                        }
                        if (it->second.owner != std::numeric_limits<uint32_t>::max() &&
                            it->second.owner != p)
                        {
                            inputValues.insert(operand);
                        }
                    }
                    for (const auto resultVal : op.results())
                    {
                        if (!resultVal.valid())
                        {
                            continue;
                        }
                        auto it = defInfo.find(resultVal);
                        if (it == defInfo.end())
                        {
                            continue;
                        }
                        if (it->second.owner == p)
                        {
                            if (origOutputs.find(resultVal) != origOutputs.end() ||
                                usedByOther.find(resultVal) != usedByOther.end())
                            {
                                outputValues.insert(resultVal);
                            }
                        }
                    }
                }

                PartitionGraphInfo info;
                info.graph = &partGraph;
                info.name = partName;

                for (const auto valueId : inputValues)
                {
                    auto it = valueMap.find(valueId);
                    if (it == valueMap.end())
                    {
                        continue;
                    }
                    ValueInfo vinfo = valueInfos.at(valueId);
                    std::string fallback = std::string("value") + std::to_string(valueId.index);
                    std::string base = normalizePortBase(vinfo.symbol, fallback);
                    bool conflict = false;
                    std::string portName = uniquePortName(usedPortNames, "in_" + base, conflict);
                    partGraph.bindInputPort(portName, it->second);
                    info.inputPortByValue.emplace(valueId, portName);
                }

                for (const auto valueId : outputValues)
                {
                    auto it = valueMap.find(valueId);
                    if (it == valueMap.end())
                    {
                        continue;
                    }
                    ValueInfo vinfo = valueInfos.at(valueId);
                    std::string fallback = std::string("value") + std::to_string(valueId.index);
                    std::string base = normalizePortBase(vinfo.symbol, fallback);
                    bool conflict = false;
                    std::string portName = uniquePortName(usedPortNames, "out_" + base, conflict);
                    partGraph.bindOutputPort(portName, it->second);
                    info.outputPortByValue.emplace(valueId, portName);
                }

                std::unordered_set<wolvrix::lib::grh::OperationId, wolvrix::lib::grh::OperationIdHash> keepCloneOps;
                keepCloneOps.reserve(partitionOps[p].size());
                for (const auto opId : partitionOps[p])
                {
                    auto it = opMap.find(opId);
                    if (it != opMap.end())
                    {
                        keepCloneOps.insert(it->second);
                    }
                }
                for (const auto opId : partGraph.operations())
                {
                    if (keepCloneOps.find(opId) == keepCloneOps.end())
                    {
                        partGraph.eraseOpUnchecked(opId);
                    }
                }

                partInfos.push_back(std::move(info));
            }

            struct PortSnapshot
            {
                std::string name;
                wolvrix::lib::grh::ValueId value;
                ValueInfo info;
            };
            struct InoutSnapshot
            {
                std::string name;
                wolvrix::lib::grh::ValueId in;
                wolvrix::lib::grh::ValueId out;
                wolvrix::lib::grh::ValueId oe;
                ValueInfo infoIn;
                ValueInfo infoOut;
                ValueInfo infoOe;
            };

            std::vector<PortSnapshot> inputSnapshot;
            std::vector<PortSnapshot> outputSnapshot;
            std::vector<InoutSnapshot> inoutSnapshot;

            inputSnapshot.reserve(graph->inputPorts().size());
            outputSnapshot.reserve(graph->outputPorts().size());
            inoutSnapshot.reserve(graph->inoutPorts().size());

            for (const auto &port : graph->inputPorts())
            {
                inputSnapshot.push_back(PortSnapshot{port.name, port.value, captureValueInfo(*graph, port.value)});
            }
            for (const auto &port : graph->outputPorts())
            {
                outputSnapshot.push_back(PortSnapshot{port.name, port.value, captureValueInfo(*graph, port.value)});
            }
            for (const auto &port : graph->inoutPorts())
            {
                InoutSnapshot snap;
                snap.name = port.name;
                snap.in = port.in;
                snap.out = port.out;
                snap.oe = port.oe;
                snap.infoIn = captureValueInfo(*graph, port.in);
                snap.infoOut = captureValueInfo(*graph, port.out);
                snap.infoOe = captureValueInfo(*graph, port.oe);
                inoutSnapshot.push_back(std::move(snap));
            }

            for (const auto &part : partInfos)
            {
                if (!part.graph)
                {
                    partitionOpCountsActual.push_back(0);
                    continue;
                }
                const std::size_t count = part.graph->operations().size();
                partitionOpCountsActual.push_back(count);
                maxPartitionOpCountActual = std::max(maxPartitionOpCountActual, count);
            }

            std::vector<std::string> topAliases = design().aliasesForGraph(graph->symbol());
            bool wasTop = false;
            for (const auto &topName : design().topGraphs())
            {
                if (topName == graph->symbol())
                {
                    wasTop = true;
                    break;
                }
            }

            std::string topName = graph->symbol();
            design().deleteGraph(topName);
            wolvrix::lib::grh::Graph &newTop = design().createGraph(topName);

            std::unordered_map<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueId,
                               wolvrix::lib::grh::ValueIdHash>
                topValueBySource;

            for (const auto &port : inputSnapshot)
            {
                std::string base = normalizePortBase(port.info.symbol, "in");
                wolvrix::lib::grh::SymbolId sym = internUniqueSymbol(newTop, base);
                wolvrix::lib::grh::ValueId value = newTop.createValue(sym, port.info.width, port.info.isSigned, port.info.type);
                if (port.info.srcLoc)
                {
                    newTop.setValueSrcLoc(value, *port.info.srcLoc);
                }
                newTop.bindInputPort(port.name, value);
                topValueBySource.emplace(port.value, value);
            }

            std::unordered_map<wolvrix::lib::grh::ValueId, wolvrix::lib::grh::ValueId,
                               wolvrix::lib::grh::ValueIdHash>
                linkValues;
            for (const auto &entry : defInfo)
            {
                const auto valueId = entry.first;
                const auto owner = entry.second.owner;
                if (owner == std::numeric_limits<uint32_t>::max())
                {
                    continue;
                }
                if (usedByOther.find(valueId) == usedByOther.end())
                {
                    continue;
                }
                ValueInfo info = valueInfos.at(valueId);
                std::string base = normalizePortBase(info.symbol, "link");
                wolvrix::lib::grh::SymbolId sym = internUniqueSymbol(newTop, "link_" + base);
                wolvrix::lib::grh::ValueId linkVal = newTop.createValue(sym, info.width, info.isSigned, info.type);
                linkValues.emplace(valueId, linkVal);
            }

            for (const auto &port : outputSnapshot)
            {
                auto itLink = linkValues.find(port.value);
                if (itLink != linkValues.end())
                {
                    newTop.bindOutputPort(port.name, itLink->second);
                    topValueBySource.emplace(port.value, itLink->second);
                    continue;
                }
                auto it = topValueBySource.find(port.value);
                if (it != topValueBySource.end())
                {
                    newTop.bindOutputPort(port.name, it->second);
                    continue;
                }
                std::string base = normalizePortBase(port.info.symbol, "out");
                wolvrix::lib::grh::SymbolId sym = internUniqueSymbol(newTop, base);
                wolvrix::lib::grh::ValueId value = newTop.createValue(sym, port.info.width, port.info.isSigned, port.info.type);
                if (port.info.srcLoc)
                {
                    newTop.setValueSrcLoc(value, *port.info.srcLoc);
                }
                newTop.bindOutputPort(port.name, value);
                topValueBySource.emplace(port.value, value);
            }

            for (const auto &port : inoutSnapshot)
            {
                wolvrix::lib::grh::SymbolId inSym = internUniqueSymbol(newTop, normalizePortBase(port.infoIn.symbol, "in"));
                wolvrix::lib::grh::ValueId inVal =
                    newTop.createValue(inSym, port.infoIn.width, port.infoIn.isSigned, port.infoIn.type);
                if (port.infoIn.srcLoc)
                {
                    newTop.setValueSrcLoc(inVal, *port.infoIn.srcLoc);
                }

                wolvrix::lib::grh::ValueId outVal;
                auto outLink = linkValues.find(port.out);
                if (outLink != linkValues.end())
                {
                    outVal = outLink->second;
                }
                else
                {
                    wolvrix::lib::grh::SymbolId outSym = internUniqueSymbol(newTop, normalizePortBase(port.infoOut.symbol, "out"));
                    outVal = newTop.createValue(outSym, port.infoOut.width, port.infoOut.isSigned, port.infoOut.type);
                    if (port.infoOut.srcLoc)
                    {
                        newTop.setValueSrcLoc(outVal, *port.infoOut.srcLoc);
                    }
                }

                wolvrix::lib::grh::ValueId oeVal;
                auto oeLink = linkValues.find(port.oe);
                if (oeLink != linkValues.end())
                {
                    oeVal = oeLink->second;
                }
                else
                {
                    wolvrix::lib::grh::SymbolId oeSym = internUniqueSymbol(newTop, normalizePortBase(port.infoOe.symbol, "oe"));
                    oeVal = newTop.createValue(oeSym, port.infoOe.width, port.infoOe.isSigned, port.infoOe.type);
                    if (port.infoOe.srcLoc)
                    {
                        newTop.setValueSrcLoc(oeVal, *port.infoOe.srcLoc);
                    }
                }

                newTop.bindInoutPort(port.name, inVal, outVal, oeVal);
                topValueBySource.emplace(port.in, inVal);
                topValueBySource.emplace(port.out, outVal);
                topValueBySource.emplace(port.oe, oeVal);
            }

            for (std::size_t p = 0; p < partInfos.size(); ++p)
            {
                PartitionGraphInfo &part = partInfos[p];
                std::unordered_map<std::string, wolvrix::lib::grh::ValueId> inputMapping;
                std::unordered_map<std::string, wolvrix::lib::grh::ValueId> outputMapping;

                for (const auto &entry : part.inputPortByValue)
                {
                    wolvrix::lib::grh::ValueId src = entry.first;
                    const std::string &portName = entry.second;
                    auto itTop = topValueBySource.find(src);
                    if (itTop != topValueBySource.end())
                    {
                        inputMapping.emplace(portName, itTop->second);
                        continue;
                    }
                    auto itLink = linkValues.find(src);
                    if (itLink != linkValues.end())
                    {
                        inputMapping.emplace(portName, itLink->second);
                        continue;
                    }
                }
                for (const auto &entry : part.outputPortByValue)
                {
                    wolvrix::lib::grh::ValueId src = entry.first;
                    const std::string &portName = entry.second;
                    auto itTop = topValueBySource.find(src);
                    if (itTop != topValueBySource.end())
                    {
                        outputMapping.emplace(portName, itTop->second);
                        continue;
                    }
                    auto itLink = linkValues.find(src);
                    if (itLink != linkValues.end())
                    {
                        outputMapping.emplace(portName, itLink->second);
                        continue;
                    }
                }

                if (inputMapping.size() != part.graph->inputPorts().size())
                {
                    logWarn("hrbcut: instance input mapping incomplete for " + part.graph->symbol());
                }
                if (outputMapping.size() != part.graph->outputPorts().size())
                {
                    logWarn("hrbcut: instance output mapping incomplete for " + part.graph->symbol());
                }

                buildInstance(newTop, part.graph->symbol(), "part_" + std::to_string(p),
                              *part.graph, inputMapping, outputMapping);
            }

            for (const auto &alias : topAliases)
            {
                design().registerGraphAlias(alias, newTop);
            }
            if (wasTop)
            {
                design().markAsTop(topName);
            }

            result.changed = true;
            logInfo("hrbcut: graph splitting done");
        }

        if (options_.partitionCount <= 1)
        {
            maxPartitionOpCountActual = originalOpCount;
            partitionOpCountsActual.assign(1, originalOpCount);
        }

        oss << ",\"op_count_input\":" << originalOpCount;
        oss << ",\"partition_op_count_max\":" << maxPartitionOpCountActual;
        appendArray("partition_op_count", partitionOpCountsActual);
        oss << "}";

        const std::string message = oss.str();
        info(message);
        result.artifacts.push_back(message);
        logInfo("hrbcut: stats emitted in " +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - totalStart)
                                   .count()) +
                "ms");

        if (options_.partitionCount <= 1)
        {
            result.changed = false;
        }
        result.failed = false;
        return result;
    }

} // namespace wolvrix::lib::transform
