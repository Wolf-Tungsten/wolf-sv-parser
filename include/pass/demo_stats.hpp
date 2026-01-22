#ifndef WOLF_SV_PASS_DEMO_STATS_HPP
#define WOLF_SV_PASS_DEMO_STATS_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class StatsPass : public Pass
    {
    public:
        StatsPass();

        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_DEMO_STATS_HPP
