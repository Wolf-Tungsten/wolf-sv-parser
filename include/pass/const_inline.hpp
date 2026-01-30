#ifndef WOLF_SV_PASS_CONST_INLINE_HPP
#define WOLF_SV_PASS_CONST_INLINE_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class ConstInlinePass : public Pass
    {
    public:
        ConstInlinePass();

        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_CONST_INLINE_HPP
