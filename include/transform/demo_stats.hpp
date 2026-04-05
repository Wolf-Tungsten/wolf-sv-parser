#ifndef WOLVRIX_TRANSFORM_DEMO_STATS_HPP
#define WOLVRIX_TRANSFORM_DEMO_STATS_HPP

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    struct StatsOptions
    {
        std::string outputKey;
    };

    class StatsPass : public Pass
    {
    public:
        StatsPass();
        explicit StatsPass(StatsOptions options);

        PassResult run() override;

    private:
        StatsOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_DEMO_STATS_HPP
