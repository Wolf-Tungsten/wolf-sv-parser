#ifndef WOLF_SV_PASS_CONST_FOLD_HPP
#define WOLF_SV_PASS_CONST_FOLD_HPP

#include "grh.hpp"
#include "transform.hpp"

#include <atomic>
#include <memory>
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

    // Forward declarations for internal types
    struct ConstantValue;
    struct ConstantKey;
    struct ConstantKeyHash;

    class ConstantFoldPass : public Pass
    {
    public:
        ConstantFoldPass();
        explicit ConstantFoldPass(ConstantFoldOptions options);

        PassResult run() override;

    private:
        using ConstantStore = std::unordered_map<grh::ir::ValueId, ConstantValue, grh::ir::ValueIdHash>;
        using ConstantPool = std::unordered_map<ConstantKey, grh::ir::ValueId, ConstantKeyHash>;

        // Per-graph folding context
        struct GraphFoldContext
        {
            grh::ir::Graph &graph;
            ConstantStore &constants;  // Shared across graphs (values can reference constants from other graphs)
            std::unique_ptr<ConstantPool> pool;  // Per-graph constant pool for deduplication
            std::atomic<int> symbolCounter{0};  // Per-graph counter for unique symbol generation
            std::unordered_set<grh::ir::OperationId, grh::ir::OperationIdHash> foldedOps;  // Per-graph folded operations
            bool &failed;
            std::size_t dedupedConstants = 0;
            std::size_t foldedOpsCount = 0;
            std::size_t simplifiedSlices = 0;
            std::size_t deadConstantsRemoved = 0;
            std::size_t unsignedCmpSimplified = 0;
            std::size_t opsErased = 0;
        };

        // Process a single graph for constant folding
        bool processSingleGraph(GraphFoldContext &ctx);

        // Individual optimization passes
        bool collectConstants(GraphFoldContext &ctx);
        bool iterativeFolding(GraphFoldContext &ctx);
        bool simplifySlices(GraphFoldContext &ctx);
        bool eliminateDeadConstants(GraphFoldContext &ctx);
        bool simplifyUnsignedComparisons(GraphFoldContext &ctx);

        ConstantFoldOptions options_;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_CONST_FOLD_HPP
