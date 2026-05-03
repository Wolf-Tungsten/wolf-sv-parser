#ifndef WOLVRIX_TRANSFORM_MERGE_REG_HPP
#define WOLVRIX_TRANSFORM_MERGE_REG_HPP

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    struct MergeRegOptions
    {
        bool enableScalarToMemory = true;
        bool enableIndexedBundleEntryToWideRegister = true;
    };

    class MergeRegPass : public Pass
    {
    public:
        MergeRegPass();
        explicit MergeRegPass(MergeRegOptions options);

        PassResult run() override;

    private:
        MergeRegOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_MERGE_REG_HPP
