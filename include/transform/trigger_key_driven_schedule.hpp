#ifndef WOLVRIX_TRANSFORM_TRIGGER_KEY_DRIVEN_SCHEDULE_HPP
#define WOLVRIX_TRANSFORM_TRIGGER_KEY_DRIVEN_SCHEDULE_HPP

#include "core/grh.hpp"
#include "core/transform.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wolvrix::lib::transform
{

    using TriggerKeyId = uint32_t;
    using SinkTKDGroupId = uint32_t;
    using TriggerTKDGroupId = uint32_t;
    using SimpleTKDGroupId = uint32_t;
    using TkdGroupId = uint32_t;
    using AffectedSinkSetId = uint32_t;

    inline constexpr TriggerTKDGroupId kDefaultTriggerTkdGroupId = 0;
    inline constexpr TriggerKeyId kInvalidTriggerKeyId = std::numeric_limits<TriggerKeyId>::max();
    inline constexpr SinkTKDGroupId kInvalidSinkTkdGroupId = std::numeric_limits<SinkTKDGroupId>::max();
    inline constexpr SimpleTKDGroupId kInvalidSimpleTkdGroupId = std::numeric_limits<SimpleTKDGroupId>::max();
    inline constexpr TkdGroupId kInvalidTkdGroupId = std::numeric_limits<TkdGroupId>::max();
    inline constexpr AffectedSinkSetId kInvalidAffectedSinkSetId =
        std::numeric_limits<AffectedSinkSetId>::max();

    enum class TriggerEventEdge : uint8_t
    {
        kNegedge = 0,
        kPosedge = 1,
    };

    enum class TkdGroupEdgeKind : uint8_t
    {
        kDataDependency = 0,
        kTriggerPrecedence = 1,
    };

    struct TriggerKeyDrivenScheduleOptions
    {
        std::string path;
        std::string resultKey;
        std::string groupsKey;
        std::string metaKey;
    };

    struct TkdTriggerEventItem
    {
        wolvrix::lib::grh::ValueId valueId;
        TriggerEventEdge eventEdge = TriggerEventEdge::kPosedge;
    };

    struct TkdTriggerKeyRecord
    {
        TriggerKeyId triggerKeyId = kInvalidTriggerKeyId;
        std::vector<TkdTriggerEventItem> items;
    };

    struct TkdAffectedSinkSetRecord
    {
        AffectedSinkSetId affectedSinkSetId = kInvalidAffectedSinkSetId;
        std::vector<SinkTKDGroupId> sinkGroupIds;
    };

    struct TkdSinkGroupRecord
    {
        SinkTKDGroupId sinkGroupId = kInvalidSinkTkdGroupId;
        TkdGroupId tkdGroupId = kInvalidTkdGroupId;
        TriggerKeyId triggerKeyId = kInvalidTriggerKeyId;
        AffectedSinkSetId affectedSinkSetId = kInvalidAffectedSinkSetId;
        std::vector<wolvrix::lib::grh::OperationId> memberOps;
    };

    struct TkdTriggerGroupRecord
    {
        TriggerTKDGroupId triggerGroupId = kDefaultTriggerTkdGroupId;
        TkdGroupId tkdGroupId = kInvalidTkdGroupId;
        AffectedSinkSetId affectedSinkSetId = kInvalidAffectedSinkSetId;
        std::vector<wolvrix::lib::grh::ValueId> rootValues;
        std::vector<wolvrix::lib::grh::OperationId> memberOps;
    };

    struct TkdSimpleGroupRecord
    {
        SimpleTKDGroupId simpleGroupId = kInvalidSimpleTkdGroupId;
        TkdGroupId tkdGroupId = kInvalidTkdGroupId;
        AffectedSinkSetId affectedSinkSetId = kInvalidAffectedSinkSetId;
        std::vector<wolvrix::lib::grh::OperationId> memberOps;
    };

    struct TkdOpToGroupIndex
    {
        TkdGroupId invalidTkdGroupId = kInvalidTkdGroupId;
        std::vector<TkdGroupId> groupIdByOpIndex;
    };

    struct TkdGroupEdge
    {
        TkdGroupId srcGroupId = kInvalidTkdGroupId;
        TkdGroupId dstGroupId = kInvalidTkdGroupId;
        TkdGroupEdgeKind kind = TkdGroupEdgeKind::kDataDependency;
    };

    struct TkdScheduleMeta
    {
        uint32_t schemaVersion = 1;
        std::string modulePath;
        std::string graphSymbol;
        TriggerKeyId emptyTriggerKeyId = 0;
        AffectedSinkSetId emptyAffectedSinkSetId = 0;
        TkdGroupId invalidTkdGroupId = kInvalidTkdGroupId;
        std::size_t valueCount = 0;
        std::size_t operationCount = 0;
        std::size_t normalizedTopLevelSinkAssignCount = 0;
        std::size_t triggerKeyCount = 0;
        std::size_t affectedSinkSetCount = 0;
        std::size_t sinkGroupCount = 0;
        std::size_t simpleGroupCount = 0;
        std::size_t tkdGroupCount = 0;
        std::size_t edgeCount = 0;
    };

    struct TkdGroupBundle
    {
        std::vector<TkdSinkGroupRecord> sinkGroups;
        std::vector<TkdTriggerGroupRecord> triggerGroups;
        std::vector<TkdSimpleGroupRecord> simpleGroups;
    };

    struct TkdScheduleResult
    {
        TkdScheduleMeta meta;
        std::vector<TkdTriggerKeyRecord> triggerKeys;
        std::vector<TkdAffectedSinkSetRecord> affectedSinkSets;
        TkdGroupBundle groups;
        TkdOpToGroupIndex opToGroupIndex;
        std::vector<TkdGroupEdge> planEdges;
        std::vector<TkdGroupId> topoOrder;
    };

    class TriggerKeyDrivenSchedulePass : public Pass
    {
    public:
        TriggerKeyDrivenSchedulePass();
        explicit TriggerKeyDrivenSchedulePass(TriggerKeyDrivenScheduleOptions options);

        PassResult run() override;

    private:
        TriggerKeyDrivenScheduleOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_TRIGGER_KEY_DRIVEN_SCHEDULE_HPP
