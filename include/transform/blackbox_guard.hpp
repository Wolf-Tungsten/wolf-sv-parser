#pragma once

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{
    class BlackboxGuardPass final : public Pass
    {
    public:
        BlackboxGuardPass();
        PassResult run() override;
    };
} // namespace wolvrix::lib::transform
