#pragma once

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class RedundantElimPass : public Pass
    {
    public:
        RedundantElimPass();
        PassResult run() override;
    };

} // namespace wolvrix::lib::transform
