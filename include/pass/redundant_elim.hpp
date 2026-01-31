#pragma once

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class RedundantElimPass : public Pass
    {
    public:
        RedundantElimPass();
        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform
