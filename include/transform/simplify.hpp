#ifndef WOLVRIX_TRANSFORM_SIMPLIFY_HPP
#define WOLVRIX_TRANSFORM_SIMPLIFY_HPP

#include "core/transform.hpp"
#include "transform/const_fold.hpp"

namespace wolvrix::lib::transform
{

    struct SimplifyOptions
    {
        int maxIterations = 8;
        ConstantFoldOptions::XFoldMode xFold = ConstantFoldOptions::XFoldMode::Known;
        ConstantFoldOptions::Semantics semantics = ConstantFoldOptions::Semantics::FourState;
    };

    class SimplifyPass : public Pass
    {
    public:
        SimplifyPass();
        explicit SimplifyPass(SimplifyOptions options);

        PassResult run() override;

    private:
        SimplifyOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_SIMPLIFY_HPP
