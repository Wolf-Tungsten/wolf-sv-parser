#ifndef WOLVRIX_LOAD_HPP
#define WOLVRIX_LOAD_HPP

#include "grh.hpp"

#include <string_view>

namespace wolvrix::lib::load
{

    class Load
    {
    public:
        virtual ~Load() = default;

        virtual wolvrix::lib::grh::Netlist load(std::string_view data) = 0;
    };

    class LoadJson final : public Load
    {
    public:
        wolvrix::lib::grh::Netlist load(std::string_view data) override;
    };

} // namespace wolvrix::lib::load

#endif // WOLVRIX_LOAD_HPP
