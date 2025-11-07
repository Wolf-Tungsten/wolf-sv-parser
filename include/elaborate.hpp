#pragma once

#include "grh.hpp"

namespace slang::ast
{
    class RootSymbol;
}

namespace wolf_sv
{

    class Elaborate
    {
    public:
        grh::Netlist convert(const slang::ast::RootSymbol &root);
    };

} // namespace wolf_sv
