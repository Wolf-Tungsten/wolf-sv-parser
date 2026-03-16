#ifndef WOLVRIX_TRANSFORM_MEM_TO_REG_HPP
#define WOLVRIX_TRANSFORM_MEM_TO_REG_HPP

#include "transform.hpp"

#include <cstdint>

namespace wolvrix::lib::transform
{

    struct MemToRegOptions
    {
        int64_t rowLimit = 32;
        bool strictInit = false;
    };

    class MemToRegPass : public Pass
    {
    public:
        MemToRegPass();
        explicit MemToRegPass(MemToRegOptions options);

        PassResult run() override;

    private:
        MemToRegOptions options_;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_MEM_TO_REG_HPP
