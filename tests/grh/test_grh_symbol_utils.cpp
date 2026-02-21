#include "grh.hpp"
#include "grh_symbol_utils.hpp"

#include <cctype>
#include <iostream>
#include <string>

using namespace grh::ir;

namespace
{

int fail(const std::string &message)
{
    std::cerr << "[grh_symbol_utils_tests] " << message << '\n';
    return 1;
}

bool isIdentifier(std::string_view text)
{
    if (text.empty())
    {
        return false;
    }
    unsigned char first = static_cast<unsigned char>(text.front());
    if (!(std::isalpha(first) || first == '_'))
    {
        return false;
    }
    for (std::size_t i = 1; i < text.size(); ++i)
    {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (!(std::isalnum(ch) || ch == '_'))
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    try
    {
        using grh::ir::symbol_utils::makeInternalBase;
        using grh::ir::symbol_utils::makeInternalSymbol;
        using grh::ir::symbol_utils::makeInternalSymbolText;
        using grh::ir::symbol_utils::normalizeComponent;

        const std::string normalized = normalizeComponent("a-b.c$");
        if (normalized != "a_b_c_")
        {
            return fail("normalizeComponent should replace non-identifier chars");
        }

        const std::string base = makeInternalBase("op", "const-fold", "tmp*value");
        if (base != "_op")
        {
            return fail("makeInternalBase should ignore pass/purpose");
        }
        if (!isIdentifier(base))
        {
            return fail("makeInternalBase should generate a valid identifier");
        }

        const std::string baseVal = makeInternalBase("val", "convert", "tmp");
        const std::string textVal = makeInternalSymbolText("val", "convert", "tmp", 7);
        if (textVal != baseVal + "_7")
        {
            return fail("makeInternalSymbolText format mismatch");
        }
        if (!isIdentifier(textVal))
        {
            return fail("makeInternalSymbolText should generate a valid identifier");
        }

        Netlist netlist;
        Graph &graph = netlist.createGraph("g");
        const std::string uniqBase = makeInternalBase("val", "convert", "uniq");
        const auto sym0 = graph.internSymbol(uniqBase + "_0");
        const auto sym1 = graph.internSymbol(uniqBase + "_1");

        uint32_t counter = 0;
        const auto sym2 = makeInternalSymbol(graph, "val", "convert", "uniq", counter);
        const std::string symText = std::string(graph.symbolText(sym2));
        if (symText != uniqBase + "_2")
        {
            return fail("makeInternalSymbol should skip existing names");
        }
        if (counter != 3)
        {
            return fail("makeInternalSymbol should advance counter correctly");
        }
        if (sym2 == sym0 || sym2 == sym1)
        {
            return fail("makeInternalSymbol should return a unique symbol");
        }
        if (!isIdentifier(symText))
        {
            return fail("makeInternalSymbol should generate a valid identifier");
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Unhandled exception: ") + ex.what());
    }

    return 0;
}
