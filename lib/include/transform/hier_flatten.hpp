#ifndef WOLVRIX_TRANSFORM_HIER_FLATTEN_HPP
#define WOLVRIX_TRANSFORM_HIER_FLATTEN_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    struct HierFlattenOptions
    {
        bool preserveFlattenedModules = false;
        enum class SymProtectMode
        {
            All,
            Hierarchy,
            Stateful,
            None
        };
        SymProtectMode symProtect = SymProtectMode::All;
    };

    class HierFlattenPass : public Pass
    {
    public:
        HierFlattenPass();
        explicit HierFlattenPass(HierFlattenOptions options);

        PassResult run() override;

    private:
        HierFlattenOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_HIER_FLATTEN_HPP
