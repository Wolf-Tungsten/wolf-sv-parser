#ifndef WOLVRIX_TRANSFORM_SLICE_INDEX_CONST_HPP
#define WOLVRIX_TRANSFORM_SLICE_INDEX_CONST_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class SliceIndexConstPass : public Pass
    {
    public:
        SliceIndexConstPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_SLICE_INDEX_CONST_HPP
