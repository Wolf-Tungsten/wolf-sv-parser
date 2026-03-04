#ifndef WOLVRIX_TRANSFORM_HRBCUT_HPP
#define WOLVRIX_TRANSFORM_HRBCUT_HPP

#include "transform.hpp"

#include <cstddef>
#include <string>

namespace wolvrix::lib::transform
{

    struct HrbcutOptions
    {
        std::string targetGraphSymbol;
        std::size_t partitionCount = 2;
        double balanceThreshold = 0.05;
        std::size_t targetCandidateCount = 8;
        std::size_t maxTrials = 32;
        std::size_t splitStopThreshold = 8;
    };

    class HrbcutPass : public Pass
    {
    public:
        HrbcutPass();
        explicit HrbcutPass(HrbcutOptions options);

        PassResult run() override;

    private:
        HrbcutOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_HRBCUT_HPP
