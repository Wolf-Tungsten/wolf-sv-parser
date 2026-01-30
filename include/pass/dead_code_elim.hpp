#ifndef WOLF_SV_PASS_DEAD_CODE_ELIM_HPP
#define WOLF_SV_PASS_DEAD_CODE_ELIM_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class DeadCodeElimPass : public Pass
    {
    public:
        DeadCodeElimPass();

        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_DEAD_CODE_ELIM_HPP
