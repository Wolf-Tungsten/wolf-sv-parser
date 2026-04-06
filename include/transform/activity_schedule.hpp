#ifndef WOLVRIX_TRANSFORM_ACTIVITY_SCHEDULE_HPP
#define WOLVRIX_TRANSFORM_ACTIVITY_SCHEDULE_HPP

#include "core/grh.hpp"
#include "core/transform.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace wolvrix::lib::transform
{

    struct ActivityScheduleOptions
    {
        std::string path;
        std::size_t supernodeMaxSize = 64;
        bool enableCoarsen = true;
        bool enableChainMerge = true;
        bool enableSiblingMerge = true;
        bool enableForwardMerge = true;
        bool enableRefine = true;
        std::size_t refineMaxIter = 4;
        bool enableReplication = true;
        std::size_t replicationMaxCost = 2;
        std::size_t replicationMaxTargets = 8;
        std::string costModel = "edge-cut";
    };

    struct ActivityScheduleSymbolIdHash
    {
        std::size_t operator()(wolvrix::lib::grh::SymbolId id) const noexcept
        {
            return static_cast<std::size_t>(id.value);
        }
    };

    struct ActivityScheduleEventTerm
    {
        wolvrix::lib::grh::ValueId value;
        std::string edge;

        friend bool operator==(const ActivityScheduleEventTerm &lhs,
                               const ActivityScheduleEventTerm &rhs) noexcept
        {
            return lhs.value == rhs.value && lhs.edge == rhs.edge;
        }
    };

    struct ActivityScheduleEventTermHash
    {
        std::size_t operator()(const ActivityScheduleEventTerm &term) const noexcept
        {
            const std::size_t valueHash = wolvrix::lib::grh::ValueIdHash{}(term.value);
            const std::size_t edgeHash = std::hash<std::string>{}(term.edge);
            return valueHash * 1315423911u + edgeHash;
        }
    };

    struct ActivityScheduleEventDomainSignature
    {
        std::vector<ActivityScheduleEventTerm> terms;

        [[nodiscard]] bool empty() const noexcept { return terms.empty(); }

        friend bool operator==(const ActivityScheduleEventDomainSignature &lhs,
                               const ActivityScheduleEventDomainSignature &rhs) noexcept
        {
            return lhs.terms == rhs.terms;
        }
    };

    struct ActivityScheduleEventDomainSignatureHash
    {
        std::size_t operator()(const ActivityScheduleEventDomainSignature &signature) const noexcept
        {
            std::size_t seed = 0;
            for (const auto &term : signature.terms)
            {
                seed = seed * 1315423911u + ActivityScheduleEventTermHash{}(term);
            }
            return seed;
        }
    };

    struct ActivityScheduleEventDomainSink
    {
        wolvrix::lib::grh::OperationId sinkOp;
        ActivityScheduleEventDomainSignature signature;
    };

    struct ActivityScheduleEventDomainSinkGroup
    {
        ActivityScheduleEventDomainSignature signature;
        std::vector<wolvrix::lib::grh::OperationId> sinkOps;
    };

    using ActivityScheduleSupernodes = std::vector<uint32_t>;
    using ActivityScheduleSupernodeToOps = std::vector<std::vector<wolvrix::lib::grh::OperationId>>;
    using ActivityScheduleSupernodeToOpSymbols = std::vector<std::vector<wolvrix::lib::grh::SymbolId>>;
    using ActivityScheduleOpToSupernode = std::vector<uint32_t>;
    using ActivityScheduleOpSymbolToSupernode =
        std::unordered_map<wolvrix::lib::grh::SymbolId, uint32_t, ActivityScheduleSymbolIdHash>;
    using ActivityScheduleDag = std::vector<std::vector<uint32_t>>;
    using ActivityScheduleTopoOrder = std::vector<uint32_t>;
    using ActivityScheduleHeadEvalSupernodes = std::vector<uint32_t>;
    using ActivityScheduleOpEventDomains =
        std::vector<std::vector<ActivityScheduleEventDomainSignature>>;
    using ActivityScheduleValueEventDomains =
        std::vector<std::vector<ActivityScheduleEventDomainSignature>>;
    using ActivityScheduleSupernodeEventDomains =
        std::vector<std::vector<ActivityScheduleEventDomainSignature>>;
    using ActivityScheduleEventDomainSinks = std::vector<ActivityScheduleEventDomainSink>;
    using ActivityScheduleEventDomainSinkGroups = std::vector<ActivityScheduleEventDomainSinkGroup>;

    inline constexpr uint32_t kInvalidActivitySupernodeId = std::numeric_limits<uint32_t>::max();

    class ActivitySchedulePass : public Pass
    {
    public:
        ActivitySchedulePass();
        explicit ActivitySchedulePass(ActivityScheduleOptions options);

        PassResult run() override;

    private:
        ActivityScheduleOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_ACTIVITY_SCHEDULE_HPP
