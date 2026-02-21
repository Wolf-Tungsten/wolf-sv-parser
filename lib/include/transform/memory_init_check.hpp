#ifndef WOLVRIX_TRANSFORM_MEMORY_INIT_CHECK_HPP
#define WOLVRIX_TRANSFORM_MEMORY_INIT_CHECK_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class MemoryInitCheckPass : public Pass
    {
    public:
        MemoryInitCheckPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_MEMORY_INIT_CHECK_HPP
