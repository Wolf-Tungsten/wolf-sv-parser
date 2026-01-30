#include "grh.hpp"
#include "pass/output_assign_inline.hpp"
#include "transform.hpp"

#include <iostream>
#include <string>

using namespace wolf_sv_parser;
using namespace wolf_sv_parser::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[output-assign-inline-tests] " << message << '\n';
        return 1;
    }

} // namespace

int main()
{
    // Case 1: inline simple output assign.
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g");

        grh::ir::ValueId in = graph.createValue(graph.internSymbol("in"), 1, false);
        graph.bindInputPort(graph.internSymbol("in"), in);

        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
        graph.bindOutputPort(graph.internSymbol("out"), out);

        grh::ir::ValueId tmp = graph.createValue(graph.internSymbol("tmp"), 1, false);
        grh::ir::OperationId notOp = graph.createOperation(grh::ir::OperationKind::kNot,
                                                           graph.internSymbol("not0"));
        graph.addOperand(notOp, in);
        graph.addResult(notOp, tmp);

        grh::ir::OperationId assign = graph.createOperation(grh::ir::OperationKind::kAssign,
                                                            graph.internSymbol("assign_out"));
        graph.addOperand(assign, tmp);
        graph.addResult(assign, out);

        PassManager manager;
        manager.addPass(std::make_unique<OutputAssignInlinePass>());

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
            return fail("Expected output-assign-inline to succeed");
        }
        if (!res.changed)
        {
            return fail("Expected output-assign-inline to report changes");
        }
        if (graph.findOperation("assign_out").valid())
        {
            return fail("assign_out should be removed");
        }
        const grh::ir::Operation notOpAfter = graph.getOperation(notOp);
        if (notOpAfter.results().empty() || notOpAfter.results()[0] != out)
        {
            return fail("not0 should drive output value directly");
        }
        const grh::ir::Value outValue = graph.getValue(out);
        if (outValue.definingOp() != notOp)
        {
            return fail("output value should be defined by not0");
        }
    }

    // Case 2: skip when operand has multiple users.
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g2");

        grh::ir::ValueId in = graph.createValue(graph.internSymbol("in"), 1, false);
        graph.bindInputPort(graph.internSymbol("in"), in);

        grh::ir::ValueId out0 = graph.createValue(graph.internSymbol("out0"), 1, false);
        grh::ir::ValueId out1 = graph.createValue(graph.internSymbol("out1"), 1, false);
        graph.bindOutputPort(graph.internSymbol("out0"), out0);
        graph.bindOutputPort(graph.internSymbol("out1"), out1);

        grh::ir::ValueId tmp = graph.createValue(graph.internSymbol("tmp"), 1, false);
        grh::ir::OperationId notOp = graph.createOperation(grh::ir::OperationKind::kNot,
                                                           graph.internSymbol("not0"));
        graph.addOperand(notOp, in);
        graph.addResult(notOp, tmp);

        grh::ir::OperationId assign0 = graph.createOperation(grh::ir::OperationKind::kAssign,
                                                             graph.internSymbol("assign_out0"));
        graph.addOperand(assign0, tmp);
        graph.addResult(assign0, out0);

        grh::ir::OperationId assign1 = graph.createOperation(grh::ir::OperationKind::kAssign,
                                                             graph.internSymbol("assign_out1"));
        graph.addOperand(assign1, tmp);
        graph.addResult(assign1, out1);

        PassManager manager;
        manager.addPass(std::make_unique<OutputAssignInlinePass>());

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
            return fail("Expected output-assign-inline to succeed");
        }
        if (res.changed)
        {
            return fail("Expected output-assign-inline to skip multi-user operand");
        }
        if (!graph.findOperation("assign_out0").valid() || !graph.findOperation("assign_out1").valid())
        {
            return fail("assign ops should remain when operand has multiple users");
        }
    }

    return 0;
}
