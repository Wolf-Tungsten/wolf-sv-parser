#include "grh.hpp"

#include <cctype>
#include <iostream>
#include <string>

using namespace wolvrix::lib::grh;

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
        using wolvrix::lib::grh::symbol_utils::makeInternalBase;
        using wolvrix::lib::grh::symbol_utils::normalizeComponent;

        const std::string normalized = normalizeComponent("a-b.c$");
        if (normalized != "a_b_c_")
        {
            return fail("normalizeComponent should replace non-identifier chars");
        }

        const std::string base = makeInternalBase("op");
        if (base != "_op")
        {
            return fail("makeInternalBase should use kind");
        }
        if (!isIdentifier(base))
        {
            return fail("makeInternalBase should generate a valid identifier");
        }

        Netlist netlist;
        Graph &graph = netlist.createGraph("g");
        const std::string uniqBase = makeInternalBase("val");
        const auto sym0 = graph.internSymbol(uniqBase + "_0");
        const auto sym1 = graph.internSymbol(uniqBase + "_1");

        const auto sym2 = graph.makeInternalValSym();
        const std::string symText = std::string(graph.symbolText(sym2));
        if (symText != uniqBase + "_2")
        {
            return fail("makeInternalValSym should skip existing names");
        }
        if (sym2 == sym0 || sym2 == sym1)
        {
            return fail("makeInternalValSym should return a unique symbol");
        }
        if (!isIdentifier(symText))
        {
            return fail("makeInternalValSym should generate a valid identifier");
        }

        const auto opSym = graph.makeInternalOpSym();
        const std::string opText = std::string(graph.symbolText(opSym));
        if (opText.find("_op_") != 0)
        {
            return fail("makeInternalOpSym should generate op-prefixed symbols");
        }
        if (!isIdentifier(opText))
        {
            return fail("makeInternalOpSym should generate a valid identifier");
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Unhandled exception: ") + ex.what());
    }

    return 0;
}
