#pragma once

#include "transform.hpp"

namespace wolf_sv::transform
{

    class StatsPass : public Pass
    {
    public:
        StatsPass();

        PassResult run() override;
    };

} // namespace wolf_sv::transform
