#ifndef WOLVRIX_TRANSFORM_STRIP_DEBUG_HPP
#define WOLVRIX_TRANSFORM_STRIP_DEBUG_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class StripDebugPass : public Pass
    {
    public:
        StripDebugPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_STRIP_DEBUG_HPP
