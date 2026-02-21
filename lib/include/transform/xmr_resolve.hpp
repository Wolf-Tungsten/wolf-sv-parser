#ifndef WOLVRIX_TRANSFORM_XMR_RESOLVE_HPP
#define WOLVRIX_TRANSFORM_XMR_RESOLVE_HPP

#include "transform.hpp"

namespace wolvrix::lib::transform
{

    class XmrResolvePass : public Pass
    {
    public:
        XmrResolvePass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_XMR_RESOLVE_HPP
