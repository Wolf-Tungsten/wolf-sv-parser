#ifndef WOLF_SV_PASS_OUTPUT_ASSIGN_INLINE_HPP
#define WOLF_SV_PASS_OUTPUT_ASSIGN_INLINE_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class OutputAssignInlinePass : public Pass
    {
    public:
        OutputAssignInlinePass();

        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_OUTPUT_ASSIGN_INLINE_HPP
