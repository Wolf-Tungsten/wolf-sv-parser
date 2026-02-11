#ifndef WOLF_SV_PASS_MEMORY_INIT_CHECK_HPP
#define WOLF_SV_PASS_MEMORY_INIT_CHECK_HPP

#include "transform.hpp"

namespace wolf_sv_parser::transform
{

    class MemoryInitCheckPass : public Pass
    {
    public:
        MemoryInitCheckPass();

        PassResult run() override;
    };

} // namespace wolf_sv_parser::transform

#endif // WOLF_SV_PASS_MEMORY_INIT_CHECK_HPP
