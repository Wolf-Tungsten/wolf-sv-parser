#include "grh.hpp"
#include "pass/const_inline.hpp"
#include "transform.hpp"

#include <iostream>
#include <string>

using namespace wolf_sv_parser;
using namespace wolf_sv_parser::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[const-inline-tests] " << message << '\n';
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
    // Case 1: inline const assign to output
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g_inline");
        grh::ir::ValueId c1 = makeConst(graph, "c1", "c1_op", 1, false, "1'b1");

        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
        graph.bindOutputPort(graph.internSymbol("out"), out);
        grh::ir::OperationId assign = graph.createOperation(grh::ir::OperationKind::kAssign,
                                                            graph.internSymbol("assign_out"));
        graph.addOperand(assign, c1);
        graph.addResult(assign, out);

        PassManager manager;
        manager.addPass(std::make_unique<ConstInlinePass>());
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
            return fail("Expected const-inline to succeed");
        }
        if (!res.changed)
        {
            return fail("Expected const-inline to report changes");
        }
        if (graph.findOperation("assign_out").valid())
        {
            return fail("assign_out should be removed");
        }
        grh::ir::Value outValue = graph.getValue(out);
        if (!outValue.definingOp().valid() ||
            graph.getOperation(outValue.definingOp()).kind() != grh::ir::OperationKind::kConstant)
        {
            return fail("Output should be driven by constant after inline");
        }
    }

    // Case 2: shared constant should be cloned, not stolen
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g_shared");
        grh::ir::ValueId c1 = makeConst(graph, "c1", "c1_op", 1, false, "1'b1");

        grh::ir::ValueId mid = graph.createValue(graph.internSymbol("mid"), 1, false);
        grh::ir::OperationId notOp = graph.createOperation(grh::ir::OperationKind::kNot,
                                                           graph.internSymbol("not_mid"));
        graph.addOperand(notOp, c1);
        graph.addResult(notOp, mid);

        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
        graph.bindOutputPort(graph.internSymbol("out"), out);
        grh::ir::OperationId assign = graph.createOperation(grh::ir::OperationKind::kAssign,
                                                            graph.internSymbol("assign_out"));
        graph.addOperand(assign, c1);
        graph.addResult(assign, out);

        PassManager manager;
        manager.addPass(std::make_unique<ConstInlinePass>());
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
            return fail("Expected const-inline to succeed on shared const");
        }
        if (!res.changed)
        {
            return fail("Expected const-inline to report changes on shared const");
        }
        if (graph.findOperation("assign_out").valid())
        {
            return fail("assign_out should be removed in shared const case");
        }
        if (!graph.findOperation("c1_op").valid())
        {
            return fail("shared constant op should remain");
        }
        grh::ir::Value outValue = graph.getValue(out);
        if (!outValue.definingOp().valid() ||
            graph.getOperation(outValue.definingOp()).kind() != grh::ir::OperationKind::kConstant)
        {
            return fail("Output should be driven by constant after inline (shared)");
        }
    }

    // Case 3: output port bound directly to const value should be renamed to port name
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g_port_bind");
        grh::ir::ValueId c1 = makeConst(graph, "c1", "c1_op", 1, false, "1'b1");
        graph.bindOutputPort(graph.internSymbol("out"), c1);

        PassManager manager;
        manager.addPass(std::make_unique<ConstInlinePass>());
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
            return fail("Expected const-inline to succeed on port-bound const");
        }
        if (!res.changed)
        {
            return fail("Expected const-inline to report changes on port-bound const");
        }
        if (!graph.findValue("out").valid())
        {
            return fail("Expected output value to be renamed to port symbol");
        }
    }

    return 0;
}
