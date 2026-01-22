#ifndef WOLF_SV_PASS_CONST_FOLD_HPP
#define WOLF_SV_PASS_CONST_FOLD_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
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

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_CONST_FOLD_HPP
