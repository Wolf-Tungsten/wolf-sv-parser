#ifndef WOLF_SV_PASS_CONST_FOLD_HPP
#define WOLF_SV_PASS_CONST_FOLD_HPP

#include "grh.hpp"
#include "transform.hpp"

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wolf_sv_parser::transform
{

    struct ConstantFoldOptions
    {
        int maxIterations = 8;
        bool allowXPropagation = false;
    };

    class ConstantFoldPass : public Pass
    {
    public:
        ConstantFoldPass();
        explicit ConstantFoldPass(ConstantFoldOptions options);

        PassResult run() override;

    private:
        using ConstantStore = std::unordered_map<grh::ir::ValueId, struct ConstantValue, grh::ir::ValueIdHash>;
        using ConstantPool = std::unordered_map<struct ConstantKey, grh::ir::ValueId, struct ConstantKeyHash>;

        // Per-graph folding context
        struct GraphFoldContext
        {
            grh::ir::Graph &graph;
            ConstantStore &constants;
            ConstantPool &pool;
            std::atomic<int> &symbolCounter;
            std::unordered_set<grh::ir::OperationId, grh::ir::OperationIdHash> &foldedOps;
            bool &failed;
        };

        // Process a single graph for constant folding
        bool processSingleGraph(GraphFoldContext &ctx);

        // Individual optimization passes
        bool collectConstants(GraphFoldContext &ctx);
        bool iterativeFolding(GraphFoldContext &ctx);
        bool simplifySlices(GraphFoldContext &ctx);
        bool eliminateDeadConstants(GraphFoldContext &ctx);

        ConstantFoldOptions options_;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_CONST_FOLD_HPP
