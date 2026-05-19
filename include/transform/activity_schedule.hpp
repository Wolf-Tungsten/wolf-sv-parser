#ifndef WOLVRIX_TRANSFORM_ACTIVITY_SCHEDULE_HPP
#define WOLVRIX_TRANSFORM_ACTIVITY_SCHEDULE_HPP

#include "core/grh.hpp"
#include "core/transform.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace wolvrix::lib::transform
{
    enum class ActivityOpClass : uint8_t
    {
        Source,
        Sink,
        Compute,
        Declaration,
        Unsupported,
    };

    enum class ActivityScheduleSupernodeKind : uint8_t
    {
        Compute = 0,
        Commit = 1,
    };

    struct ActivityScheduleOptions
    {
        std::string path;
        std::size_t maxOpInComputeSupernode = 128;
        std::size_t maxOpInComputeNode = 8192;
        std::size_t maxOpInCommitSupernode = 4096;
        std::size_t localSharedComputeMaxFanout = 4;
        std::size_t localSharedComputeMaxWidth = 256;
        std::size_t essentSmallPartCutoff = 20;
        std::size_t essentSmallSiblingMaxPreds = 1;
        std::size_t essentSmallSiblingCandidateBudget = 250000;
        std::size_t essentMaxClusterOps = 0;
        std::size_t splitOversizeComputeNodeMaxOps = 0;
        double essentOverlapThreshold1 = 0.5;
        double essentOverlapThreshold2 = 0.25;
        std::size_t essentCycleGuardMaxVisits = 4096;
        bool enableCoarsen = true;
        bool enableChainMerge = true;
        bool enableLocalSharedCompute = false;
        bool enableEssentMffcBuild = false;
        bool enableEssentCoarsen = false;
        bool enableEssentSingleParentMerge = true;
        bool enableEssentSmallSiblingMerge = true;
        bool enableEssentSmallOverlapMerge = true;
        bool enableEssentDownMerge = true;
        bool splitOversizeComputeNodes = false;
        bool dumpEssentDagStats = true;
        std::string costModel = "edge-cut";
    };

    struct ActivityScheduleSymbolIdHash
    {
        std::size_t operator()(wolvrix::lib::grh::SymbolId id) const noexcept
        {
            return static_cast<std::size_t>(id.value);
        }
    };

    using ActivityScheduleSupernodeToOps = std::vector<std::vector<wolvrix::lib::grh::OperationId>>;
    using ActivityScheduleOpToSupernode = std::vector<uint32_t>;
    using ActivityScheduleDag = std::vector<std::vector<uint32_t>>;
    using ActivityScheduleValueFanout = std::vector<std::vector<uint32_t>>;
    using ActivityScheduleTopoOrder = std::vector<uint32_t>;
    using ActivityScheduleStateReadSupernodes = std::unordered_map<std::string, std::vector<uint32_t>>;
    using ActivityScheduleSupernodeKinds = std::vector<ActivityScheduleSupernodeKind>;
    using ActivityScheduleComputeNodesBySupernode = std::vector<std::vector<uint32_t>>;

    struct ActivityScheduleSummaryStats
    {
        using KindCountMap = std::map<std::string, std::size_t>;

        std::size_t supernodes = 0;
        std::size_t computeSupernodes = 0;
        std::size_t commitSupernodes = 0;
        std::size_t dagEdges = 0;
        std::size_t boundaryValues = 0;
        std::size_t boundaryActivationEdges = 0;
        std::size_t computeComputeValuePairs = 0;
        std::size_t computeCommitValuePairs = 0;
        std::size_t stateReadActivationEdges = 0;
        std::size_t memoryReadActivationEdges = 0;
        std::size_t constantActivationEdges = 0;
        std::size_t otherComputeActivationEdges = 0;
        std::size_t otherComputeSingleTargetValues = 0;
        std::size_t otherComputeMultiTargetValues = 0;
        std::size_t otherComputeSingleTargetActivationEdges = 0;
        std::size_t otherComputeMultiTargetActivationEdges = 0;
        std::size_t otherComputeUniqueSupernodePairs = 0;
        std::size_t otherComputeDuplicateActivationEdges = 0;
        std::size_t computeNodes = 0;
        std::size_t computeNodeOpsTotal = 0;
        std::size_t initialComputeSupernodes = 0;
        std::size_t initialComputeSupernodeOpsTotal = 0;
        std::size_t initialComputeSupernodeDagEdges = 0;
        std::size_t initialBoundaryValues = 0;
        std::size_t initialBoundaryActivationEdges = 0;
        std::size_t essentClustersBeforeCoarsen = 0;
        std::size_t essentClustersAfterMffc = 0;
        std::size_t essentClustersAfterSingleParent = 0;
        std::size_t essentClustersAfterSmallSiblings = 0;
        std::size_t essentClustersAfterSmallOverlap = 0;
        std::size_t essentClustersAfterDown = 0;
        std::size_t clustersAfterEssentCoarsen = 0;
        std::size_t essentSingleParentMerges = 0;
        std::size_t essentSmallSiblingMerges = 0;
        std::size_t essentSmallOverlapMerges = 0;
        std::size_t essentDownMerges = 0;
        std::size_t essentMergeCandidates = 0;
        std::size_t essentMergeRejectedSize = 0;
        std::size_t essentMergeRejectedCycle = 0;
        std::size_t essentMergeRejectedBounded = 0;
        std::size_t essentMergeRejectedTopo = 0;
        std::size_t essentSingleParentCandidates = 0;
        std::size_t essentSingleParentRejectedSize = 0;
        std::size_t essentSingleParentRejectedCycle = 0;
        std::size_t essentSingleParentRejectedBounded = 0;
        std::size_t essentSingleParentRejectedTopo = 0;
        std::size_t essentSmallSiblingCandidates = 0;
        std::size_t essentSmallSiblingRejectedSize = 0;
        std::size_t essentSmallSiblingRejectedCycle = 0;
        std::size_t essentSmallSiblingRejectedBounded = 0;
        std::size_t essentSmallSiblingRejectedTopo = 0;
        std::size_t essentSmallOverlapCandidates = 0;
        std::size_t essentSmallOverlapRejectedSize = 0;
        std::size_t essentSmallOverlapRejectedCycle = 0;
        std::size_t essentSmallOverlapRejectedBounded = 0;
        std::size_t essentSmallOverlapRejectedTopo = 0;
        std::size_t essentDownCandidates = 0;
        std::size_t essentDownRejectedSize = 0;
        std::size_t essentDownRejectedCycle = 0;
        std::size_t essentDownRejectedBounded = 0;
        std::size_t essentDownRejectedTopo = 0;
        std::size_t sourceClonesInComputeNodes = 0;
        std::size_t localSharedComputeClonesInComputeNodes = 0;
        std::size_t directSourceInputsToCommitSupernodes = 0;
        std::size_t commonExprComputeNodes = 0;
        std::size_t computeNodeBoundaryInputsTotal = 0;
        std::size_t computeNodeBoundaryInputNoDef = 0;
        std::size_t computeNodeBoundaryInputDefOutOfRange = 0;
        std::size_t computeNodeBoundaryInputDeclared = 0;
        std::size_t computeNodeBoundaryInputSourceSpill = 0;
        std::size_t computeNodeBoundaryInputUnsupported = 0;
        std::size_t computeNodeBoundaryInputExistingOwner = 0;
        std::size_t computeNodeBoundaryInputExistingCommonOwner = 0;
        std::size_t computeNodeBoundaryInputShared = 0;
        std::size_t computeNodeBoundaryInputCapacity = 0;
        std::size_t computeNodeBoundaryValues = 0;
        std::size_t commitInputRootValues = 0;
        std::size_t commitSinkOps = 0;
        std::size_t commitEventKeyRuns = 0;
        std::size_t commitEventKeys = 0;
        std::size_t topoEdges = 0;
        std::size_t graphOps = 0;
        std::size_t graphValues = 0;
        KindCountMap activationEdgesBySourceKind;
        KindCountMap activationSourceValuesBySourceKind;
        KindCountMap computeNodeBoundaryExistingCommonOwnerByKind;
        KindCountMap computeNodeBoundaryExistingCommonOwnerByWidthBucket;
        KindCountMap computeNodeBoundaryExistingCommonOwnerByFanoutBucket;
    };

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
