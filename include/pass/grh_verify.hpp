#pragma once

#include "transform.hpp"

namespace wolf_sv::transform
{

    struct GRHVerifyOptions
    {
        bool autoFixPointers = true;
        bool stopOnError = true;
    };

    class GRHVerifyPass : public Pass
    {
    public:
        GRHVerifyPass();
        explicit GRHVerifyPass(GRHVerifyOptions opts);
        PassResult run() override;

    private:
        GRHVerifyOptions options_;
    };

} // namespace wolf_sv::transform
