#ifndef WOLVRIX_TRANSFORM_DEAD_CODE_ELIM_HPP
#define WOLVRIX_TRANSFORM_DEAD_CODE_ELIM_HPP

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    struct DeadCodeElimOptions
    {
        std::string outputKey;
        std::size_t sampleLimit = 16;
    };

    class DeadCodeElimPass : public Pass
    {
    public:
        DeadCodeElimPass();
        explicit DeadCodeElimPass(DeadCodeElimOptions options);

        PassResult run() override;

    private:
        DeadCodeElimOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_DEAD_CODE_ELIM_HPP
