#include "grh.hpp"
#include "pass/dead_code_elim.hpp"
#include "transform.hpp"

#include <iostream>
#include <string>

using namespace wolf_sv_parser;
using namespace wolf_sv_parser::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[dead-code-elim-tests] " << message << '\n';
        return 1;
    }

    grh::ir::ValueId makeConst(grh::ir::Graph &graph,
                               const std::string &valueName,
                               const std::string &opName,
                               int64_t width,
                               bool isSigned,
                               const std::string &literal)
    {
        const grh::ir::SymbolId valueSym = graph.internSymbol(valueName);
        const grh::ir::SymbolId opSym = graph.internSymbol(opName);
        const grh::ir::ValueId val = graph.createValue(valueSym, static_cast<int32_t>(width), isSigned);
        const grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kConstant, opSym);
        graph.addResult(op, val);
        graph.setAttr(op, "constValue", literal);
        return val;
    }

} // namespace

int main()
{
    grh::ir::Netlist netlist;
    grh::ir::Graph &graph = netlist.createGraph("g");

    grh::ir::ValueId liveConst = makeConst(graph, "c_live", "c_live_op", 1, false, "1'b1");
    grh::ir::ValueId deadConst = makeConst(graph, "c_dead", "c_dead_op", 1, false, "1'b0");
    grh::ir::ValueId keptConst = makeConst(graph, "c_keep", "c_keep_op", 1, false, "1'b0");
    graph.addDeclaredSymbol(graph.internSymbol("c_keep"));
    (void)keptConst;

    grh::ir::ValueId deadTmp = graph.createValue(graph.internSymbol("dead_tmp"), 1, false);
    grh::ir::OperationId deadNot = graph.createOperation(grh::ir::OperationKind::kNot, graph.internSymbol("dead_not"));
    graph.addOperand(deadNot, deadConst);
    graph.addResult(deadNot, deadTmp);

    grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
    graph.bindOutputPort(graph.internSymbol("out"), out);
    grh::ir::OperationId assign = graph.createOperation(grh::ir::OperationKind::kAssign, graph.internSymbol("assign_out"));
    graph.addOperand(assign, liveConst);
    graph.addResult(assign, out);

    PassManager manager;
    manager.addPass(std::make_unique<DeadCodeElimPass>());

    PassDiagnostics diags;
    PassManagerResult res{};
    try
    {
        res = manager.run(netlist, diags);
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Exception during run: ") + ex.what());
    }
    if (!res.success || diags.hasError())
    {
        return fail("Expected DCE to succeed");
    }
    if (!res.changed)
    {
        return fail("Expected DCE to report changes");
    }

    if (graph.findOperation("dead_not").valid())
    {
        return fail("dead_not should be removed");
    }
    if (graph.findOperation("c_dead_op").valid())
    {
        return fail("c_dead_op should be removed");
    }
    if (graph.findValue("c_dead").valid())
    {
        return fail("c_dead value should be removed");
    }
    if (!graph.findOperation("c_live_op").valid())
    {
        return fail("c_live_op should remain");
    }
    if (!graph.findValue("c_live").valid())
    {
        return fail("c_live value should remain");
    }
    if (!graph.findOperation("assign_out").valid())
    {
        return fail("assign_out should remain");
    }
    if (!graph.findOperation("c_keep_op").valid())
    {
        return fail("c_keep_op should remain because it is declared");
    }
    if (!graph.findValue("c_keep").valid())
    {
        return fail("c_keep value should remain because it is declared");
    }

    return 0;
}
