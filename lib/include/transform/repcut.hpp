#ifndef WOLVRIX_TRANSFORM_REPCUT_HPP
#define WOLVRIX_TRANSFORM_REPCUT_HPP

#include "transform.hpp"

#include <cstddef>
#include <string>

namespace wolvrix::lib::transform
{

    struct RepcutOptions
    {
        std::string targetGraphSymbol;
        std::size_t partitionCount = 2;
        double imbalanceFactor = 0.015;
        std::string workDir = ".";
        std::string kaHyParPath = "KaHyPar";
        bool keepIntermediateFiles = false;
    };

    class RepcutPass : public Pass
    {
    public:
        RepcutPass();
        explicit RepcutPass(RepcutOptions options);

        PassResult run() override;

    private:
        RepcutOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_REPCUT_HPP
