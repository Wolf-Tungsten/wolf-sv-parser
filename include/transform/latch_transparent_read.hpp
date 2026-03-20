#pragma once

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    class LatchTransparentReadPass : public Pass
    {
    public:
        LatchTransparentReadPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform
