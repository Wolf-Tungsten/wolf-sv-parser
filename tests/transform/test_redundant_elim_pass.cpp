#include "grh.hpp"
#include "transform/redundant_elim.hpp"
#include "transform.hpp"

#include <iostream>
#include <string>

using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[redundant-elim-tests] " << message << '\n';
        return 1;
    }

} // namespace

int main()
{
    wolvrix::lib::grh::Netlist netlist;
    wolvrix::lib::grh::Graph &graph = netlist.createGraph("g");

    const wolvrix::lib::grh::SymbolId resetSym = graph.internSymbol("reset");
    wolvrix::lib::grh::ValueId reset = graph.createValue(resetSym, 1, false);
    graph.bindInputPort(resetSym, reset);

    wolvrix::lib::grh::ValueId notReset = graph.createValue(graph.internSymbol("not_reset"), 1, false);
    wolvrix::lib::grh::OperationId notOp =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kLogicNot,
                              graph.internSymbol("not_op"));
    graph.addOperand(notOp, reset);
    graph.addResult(notOp, notReset);

    wolvrix::lib::grh::ValueId guard = graph.createValue(graph.internSymbol("guard"), 1, false);
    wolvrix::lib::grh::OperationId orOp =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kLogicOr,
                              graph.internSymbol("or_op"));
    graph.addOperand(orOp, reset);
    graph.addOperand(orOp, notReset);
    graph.addResult(orOp, guard);

    const wolvrix::lib::grh::SymbolId outSym = graph.internSymbol("out");
    graph.bindOutputPort(outSym, guard);

    PassManager manager;
    manager.addPass(std::make_unique<RedundantElimPass>());

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
        return fail("Expected redundant elimination to succeed");
    }
    if (!res.changed)
    {
        return fail("Expected redundant elimination to report changes");
    }

    if (graph.findOperation("or_op").valid())
    {
        return fail("or_op should be removed");
    }

    wolvrix::lib::grh::ValueId outValueId = wolvrix::lib::grh::ValueId::invalid();
    for (const auto &port : graph.outputPorts())
    {
        if (port.name == outSym)
        {
            outValueId = port.value;
            break;
        }
    }
    if (!outValueId.valid())
    {
        return fail("Output port not found");
    }

    wolvrix::lib::grh::Value outValue = graph.getValue(outValueId);
    wolvrix::lib::grh::OperationId defOpId = outValue.definingOp();
    if (!defOpId.valid())
    {
        return fail("Output should be driven by a constant");
    }
    wolvrix::lib::grh::Operation defOp = graph.getOperation(defOpId);
    if (defOp.kind() != wolvrix::lib::grh::OperationKind::kConstant)
    {
        return fail("Output should be driven by kConstant");
    }
    auto constAttr = defOp.attr("constValue");
    if (!constAttr)
    {
        return fail("Constant is missing constValue attribute");
    }
    const auto *literal = std::get_if<std::string>(&*constAttr);
    if (!literal || *literal != "1'b1")
    {
        return fail("Expected constValue to be 1'b1");
    }

    return 0;
}
