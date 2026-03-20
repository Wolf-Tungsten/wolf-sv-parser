#pragma once

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    class MultiDrivenGuardPass : public Pass
    {
    public:
        MultiDrivenGuardPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform
