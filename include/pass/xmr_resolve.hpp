#ifndef WOLF_SV_PASS_XMR_RESOLVE_HPP
#define WOLF_SV_PASS_XMR_RESOLVE_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class XmrResolvePass : public Pass
    {
    public:
        XmrResolvePass();

        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_XMR_RESOLVE_HPP
