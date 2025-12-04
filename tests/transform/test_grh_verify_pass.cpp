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
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g");
        grh::Value &val = graph.createValue("v0", 1, false);
        grh::Operation &op = graph.createOperation(grh::OperationKind::kConstant, "c0");
        op.addResult(val); // Missing constValue on purpose

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());

        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (result.success || !diags.hasError())
        {
            return fail("Missing attribute should be reported as error and fail the pass");
        }
    }

    // Case 2: operand symbol that cannot be resolved triggers error
    {
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g");
        grh::Value &lhs = graph.createValue("a", 1, false);
        grh::Value &rhs = graph.createValue("b", 1, false);
        grh::Value &out = graph.createValue("out", 1, false);
        grh::Operation &op = graph.createOperation(grh::OperationKind::kAdd, "add0");
        op.addOperand(lhs);
        op.addOperand(rhs);
        op.addResult(out);

        auto &operandSymbols = const_cast<std::vector<grh::ValueId> &>(op.operandSymbols());
        operandSymbols[1] = "missing_b";

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());
        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (result.success || !diags.hasError())
        {
            return fail("Unresolvable operand symbol should be reported as error");
        }
    }

    // Case 3: user lists are rebuilt from operand references when missing
    {
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g");
        grh::Value &lhs = graph.createValue("a", 1, false);
        grh::Value &rhs = graph.createValue("b", 1, false);
        grh::Value &out = graph.createValue("out", 1, false);
        grh::Operation &op = graph.createOperation(grh::OperationKind::kAdd, "add0");
        op.addOperand(lhs);
        op.addOperand(rhs);
        op.addResult(out);

        const_cast<std::vector<grh::ValueUser> &>(lhs.users()).clear();
        const_cast<std::vector<grh::ValueUser> &>(rhs.users()).clear();

        PassManager manager;
        manager.addPass(std::make_unique<GRHVerifyPass>());
        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (!result.success || diags.hasError())
        {
            return fail("User list rebuild should succeed without errors");
        }
        if (!result.changed)
        {
            return fail("Expected pass to mark changes after rebuilding users");
        }
        if (lhs.users().size() != 1 || rhs.users().size() != 1)
        {
            return fail("User lists were not rebuilt as expected");
        }
    }

    // Case 4: unexpected attribute is surfaced as info, not error
    {
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g");
        grh::Value &in = graph.createValue("in", 1, false);
        grh::Value &out = graph.createValue("out", 1, false);
        grh::Operation &op = graph.createOperation(grh::OperationKind::kAssign, "assign0");
        op.addOperand(in);
        op.addResult(out);
        op.setAttribute("extra", 42);

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
