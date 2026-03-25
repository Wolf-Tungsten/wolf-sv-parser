#ifndef WOLVRIX_TRANSFORM_REPCUT_PORT_MERGE_HPP
#define WOLVRIX_TRANSFORM_REPCUT_PORT_MERGE_HPP

#include "core/transform.hpp"

#include <string>

namespace wolvrix::lib::transform
{

    struct RepcutPortMergeOptions
    {
        std::string path;
    };

    class RepcutPortMergePass : public Pass
    {
    public:
        RepcutPortMergePass();
        explicit RepcutPortMergePass(RepcutPortMergeOptions options);

        PassResult run() override;

    private:
        RepcutPortMergeOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_REPCUT_PORT_MERGE_HPP
