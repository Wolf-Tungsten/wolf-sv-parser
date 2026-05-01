#ifndef WOLVRIX_TRANSFORM_MERGE_REG_HPP
#define WOLVRIX_TRANSFORM_MERGE_REG_HPP

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    class MergeRegPass : public Pass
    {
    public:
        MergeRegPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_MERGE_REG_HPP
