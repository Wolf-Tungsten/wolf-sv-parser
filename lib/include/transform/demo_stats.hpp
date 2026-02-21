#ifndef WOLVRIX_TRANSFORM_DEMO_STATS_HPP
#define WOLVRIX_TRANSFORM_DEMO_STATS_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class StatsPass : public Pass
    {
    public:
        StatsPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_DEMO_STATS_HPP
