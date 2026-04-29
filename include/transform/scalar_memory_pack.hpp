#ifndef WOLVRIX_TRANSFORM_SCALAR_MEMORY_PACK_HPP
#define WOLVRIX_TRANSFORM_SCALAR_MEMORY_PACK_HPP

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    class ScalarMemoryPackPass : public Pass
    {
    public:
        ScalarMemoryPackPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_SCALAR_MEMORY_PACK_HPP
