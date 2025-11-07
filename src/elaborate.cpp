#include "elaborate.hpp"

#include "slang/ast/symbols/CompilationUnitSymbols.h"

namespace wolf_sv {

grh::Netlist Elaborate::convert(const slang::ast::RootSymbol&) {
    // Stage 1 focuses on GRH data structures; elaboration will be implemented in later stages.
    return grh::Netlist{};
}

} // namespace wolf_sv
