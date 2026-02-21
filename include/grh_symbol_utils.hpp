#ifndef WOLF_SV_GRH_SYMBOL_UTILS_HPP
#define WOLF_SV_GRH_SYMBOL_UTILS_HPP

#include "grh.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace grh::ir::symbol_utils
{

    inline std::string normalizeComponent(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (unsigned char ch : text)
        {
            if (std::isalnum(ch) || ch == '_')
            {
                out.push_back(static_cast<char>(ch));
            }
            else
            {
                out.push_back('_');
            }
        }
        return out;
    }

    inline std::string makeInternalBase(std::string_view kind,
                                        std::string_view pass,
                                        std::string_view purpose)
    {
        (void)pass;
        (void)purpose;
        std::string base;
        base.reserve(kind.size() + 2);
        base.push_back('_');
        base.append(kind);
        return base;
    }

    inline std::string makeInternalSymbolText(std::string_view kind,
                                              std::string_view pass,
                                              std::string_view purpose,
                                              uint32_t counter)
    {
        std::string base = makeInternalBase(kind, pass, purpose);
        base.push_back('_');
        base.append(std::to_string(counter));
        return base;
    }

    inline SymbolId makeInternalSymbol(Graph &graph,
                                       std::string_view kind,
                                       std::string_view pass,
                                       std::string_view purpose,
                                       uint32_t &counter)
    {
        std::string base = makeInternalBase(kind, pass, purpose);
        for (;;)
        {
            std::string candidate = base;
            candidate.push_back('_');
            candidate.append(std::to_string(counter++));
            if (!graph.symbols().contains(candidate))
            {
                return graph.internSymbol(candidate);
            }
        }
    }

} // namespace grh::ir::symbol_utils

#endif // WOLF_SV_GRH_SYMBOL_UTILS_HPP
