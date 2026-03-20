#ifndef WOLVRIX_TRANSFORM_INSTANCE_INLINE_HPP
#define WOLVRIX_TRANSFORM_INSTANCE_INLINE_HPP

#include "core/transform.hpp"

#include <string>

namespace wolvrix::lib::transform
{

    struct InstanceInlineOptions
    {
        std::string path;
    };

    class InstanceInlinePass : public Pass
    {
    public:
        InstanceInlinePass();
        explicit InstanceInlinePass(InstanceInlineOptions options);

        PassResult run() override;

    private:
        InstanceInlineOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_INSTANCE_INLINE_HPP
