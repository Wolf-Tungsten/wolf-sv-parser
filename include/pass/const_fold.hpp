#pragma once

#include "transform.hpp"

namespace wolf_sv::transform
{

    struct ConstantFoldOptions
    {
        int maxIterations = 8;
        bool allowXPropagation = false;
    };

    class ConstantFoldPass : public Pass
    {
    public:
        ConstantFoldPass();
        explicit ConstantFoldPass(ConstantFoldOptions options);

        PassResult run() override;

    private:
        ConstantFoldOptions options_;
    };

} // namespace wolf_sv::transform
