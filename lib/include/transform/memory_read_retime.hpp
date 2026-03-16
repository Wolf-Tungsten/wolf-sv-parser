#ifndef WOLVRIX_TRANSFORM_MEMORY_READ_RETIME_HPP
#define WOLVRIX_TRANSFORM_MEMORY_READ_RETIME_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class MemoryReadRetimePass : public Pass
    {
    public:
        MemoryReadRetimePass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_MEMORY_READ_RETIME_HPP
