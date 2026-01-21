#ifndef WOLF_SV_GRH_IR_LOAD_HPP
#define WOLF_SV_GRH_IR_LOAD_HPP

#include "grh.hpp"

#include <string_view>

namespace wolf_sv::grh::ir::load
{

    class Load
    {
    public:
        virtual ~Load() = default;

        virtual grh::ir::Netlist load(std::string_view data) = 0;
    };

    class LoadJson final : public Load
    {
    public:
        grh::ir::Netlist load(std::string_view data) override;
    };

} // namespace wolf_sv::grh::ir::load

#endif // WOLF_SV_GRH_IR_LOAD_HPP
