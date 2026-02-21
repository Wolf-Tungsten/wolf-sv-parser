#ifndef WOLVRIX_TRANSFORM_DEAD_CODE_ELIM_HPP
#define WOLVRIX_TRANSFORM_DEAD_CODE_ELIM_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class DeadCodeElimPass : public Pass
    {
    public:
        DeadCodeElimPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_DEAD_CODE_ELIM_HPP
