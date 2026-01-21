#include "grh.hpp"
#include "transform.hpp"
#include "pass/grh_verify.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace wolf_sv;
using namespace wolf_sv::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[grh-verify-tests] " << message << '\n';
        return 1;
    }

} // namespace

int main()
{
    // Case 1: schema validation catches missing required attribute
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g");
        grh::ir::ValueId val = graph.createValue(graph.internSymbol("v0"), 1, false);
        grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kConstant, graph.internSymbol("c0"));
        graph.addResult(op, val); // Missing constValue on purpose

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());

        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (result.success || !diags.hasError())
        {
            return fail("Missing attribute should be reported as error and fail the pass");
        }
    }

    // Case 2: operand count mismatch triggers error
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g");
        grh::ir::ValueId lhs = graph.createValue(graph.internSymbol("a"), 1, false);
        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
        grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kAdd, graph.internSymbol("add0"));
        graph.addOperand(op, lhs); // Missing second operand on purpose
        graph.addResult(op, out);

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());
        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (result.success || !diags.hasError())
        {
            return fail("Operand count mismatch should be reported as error");
        }
    }

    // Case 3: well-formed graph passes verification
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g");
        grh::ir::ValueId lhs = graph.createValue(graph.internSymbol("a"), 1, false);
        grh::ir::ValueId rhs = graph.createValue(graph.internSymbol("b"), 1, false);
        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
        grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kAdd, graph.internSymbol("add0"));
        graph.addOperand(op, lhs);
        graph.addOperand(op, rhs);
        graph.addResult(op, out);

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());
        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (!result.success || diags.hasError())
        {
            return fail("User list rebuild should succeed without errors");
        }
        if (result.changed)
        {
            return fail("Well-formed graph should not report changes");
        }
    }

    // Case 4: unexpected attribute is surfaced as info, not error
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g");
        grh::ir::ValueId in = graph.createValue(graph.internSymbol("in"), 1, false);
        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 1, false);
        grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kAssign, graph.internSymbol("assign0"));
        graph.addOperand(op, in);
        graph.addResult(op, out);
        graph.setAttr(op, "extra", int64_t{42});

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());
        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (!result.success || diags.hasError())
        {
            return fail("Extra attribute should not cause an error");
        }
#if WOLF_SV_TRANSFORM_ENABLE_INFO_DIAGNOSTICS
        bool hasInfo = false;
        for (const auto &msg : diags.messages())
        {
            if (msg.kind == PassDiagnosticKind::Info && msg.passName == "grh-verify")
            {
                hasInfo = true;
                break;
            }
        }
        if (!hasInfo)
        {
            return fail("Extra attribute should be reported as info");
        }
#endif
    }

    return 0;
}
