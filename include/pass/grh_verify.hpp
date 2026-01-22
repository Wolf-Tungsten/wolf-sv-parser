#ifndef WOLF_SV_PASS_GRH_VERIFY_HPP
#define WOLF_SV_PASS_GRH_VERIFY_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
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

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_GRH_VERIFY_HPP
