#include "grh.hpp"
#include "pass/const_fold.hpp"
#include "transform.hpp"

#include "slang/numeric/SVInt.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace wolf_sv;
using namespace wolf_sv::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[const-fold-tests] " << message << '\n';
        return 1;
    }

    grh::Value &makeConst(grh::Graph &graph, const std::string &valueName, const std::string &opName, int64_t width, bool isSigned, const std::string &literal)
    {
        grh::Value &val = graph.createValue(valueName, width, isSigned);
        grh::Operation &op = graph.createOperation(grh::OperationKind::kConstant, opName);
        op.addResult(val);
        op.setAttribute("constValue", literal);
        return val;
    }

    std::optional<slang::SVInt> getConstLiteral(const grh::Operation &op)
    {
        auto it = op.attributes().find("constValue");
        if (it == op.attributes().end())
        {
            return std::nullopt;
        }
        const auto *val = std::get_if<std::string>(&it->second);
        if (!val)
        {
            return std::nullopt;
        }
        try
        {
            return slang::SVInt::fromString(*val);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

} // namespace

int main()
{
    // Case 1: multi-iteration folding rewires downstream users
    {
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g");
        grh::Value &c0 = makeConst(graph, "c0", "c0_op", 4, false, "4'h3");
        grh::Value &c1 = makeConst(graph, "c1", "c1_op", 4, false, "4'h1");

        grh::Value &sum = graph.createValue("sum", 4, false);
        grh::Operation &add = graph.createOperation(grh::OperationKind::kAdd, "add0");
        add.addOperand(c0);
        add.addOperand(c1);
        add.addResult(sum);

        grh::Value &pass = graph.createValue("pass", 4, false);
        grh::Operation &assign = graph.createOperation(grh::OperationKind::kAssign, "assign0");
        assign.addOperand(sum);
        assign.addResult(pass);

        grh::Value &neg = graph.createValue("neg", 4, false);
        grh::Operation &invert = graph.createOperation(grh::OperationKind::kNot, "not0");
        invert.addOperand(pass);
        invert.addResult(neg);

        grh::Value &finalSum = graph.createValue("finalSum", 4, false);
        grh::Operation &add2 = graph.createOperation(grh::OperationKind::kAdd, "add1");
        add2.addOperand(neg);
        add2.addOperand(c1);
        add2.addResult(finalSum);

        grh::Value &out = graph.createValue("out", 4, false);
        graph.bindOutputPort("out", out);
        grh::Operation &assignOut = graph.createOperation(grh::OperationKind::kAssign, "assign1");
        assignOut.addOperand(finalSum);
        assignOut.addResult(graph.getValue("out"));

        PassManager manager;
        manager.addPass(std::make_unique<ConstantFoldPass>());

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
            return fail("Expected constant propagation to succeed");
        }
        if (!res.changed)
        {
            return fail("Expected constant propagation to mark changes");
        }

        // Downstream user should be rewired to a constant.
        if (graph.findOperation("assign1"))
        {
            return fail("assign1 should be removed after folding");
        }
        const grh::Value *outVal = graph.outputPortValue("out");
        if (!outVal || !outVal->definingOp() || outVal->definingOp()->kind() != grh::OperationKind::kConstant)
        {
            return fail("Output port was not rewired to a constant");
        }
        auto literal = getConstLiteral(*outVal->definingOp());
        slang::SVInt expected = slang::SVInt::fromString("4'hc").resize(4);
        if (!literal || !static_cast<bool>((*literal == expected)))
        {
            return fail("Final constant value mismatch");
        }
        if (!neg.users().empty())
        {
            return fail("Expected users of intermediate value to be rewired");
        }
    }

    // Case 2: X operand blocks folding when allowXPropagation=false
    {
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g2");
        grh::Value &xval = makeConst(graph, "cx", "cx_op", 1, false, "1'bx");
        grh::Value &one = makeConst(graph, "c1", "c1_op", 1, false, "1'b1");

        grh::Value &andOut = graph.createValue("andOut", 1, false);
        grh::Operation &op = graph.createOperation(grh::OperationKind::kAnd, "and0");
        op.addOperand(xval);
        op.addOperand(one);
        op.addResult(andOut);

        PassManager manager;
        manager.addPass(std::make_unique<ConstantFoldPass>());
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
            return fail("Unexpected failure when X propagation is disabled");
        }
        if (res.changed)
        {
            return fail("Pass should not change graph when blocked by X");
        }
        if (op.operands()[0] != &xval || op.operands()[1] != &one)
        {
            return fail("Operands should remain unchanged when folding is skipped");
        }
    }

    // Case 3: missing attribute triggers failure
    {
        grh::Netlist netlist;
        grh::Graph &graph = netlist.createGraph("g3");
        grh::Value &c = makeConst(graph, "c", "c_op", 2, false, "2'h1");

        grh::Value &repOut = graph.createValue("repOut", 4, false);
        grh::Operation &rep = graph.createOperation(grh::OperationKind::kReplicate, "rep0");
        rep.addOperand(c);
        rep.addResult(repOut);
        // Intentionally omit the "rep" attribute.

        PassManager manager;
        manager.addPass(std::make_unique<ConstantFoldPass>());
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
        if (res.success || !diags.hasError())
        {
            return fail("Missing attribute should fail the pass and emit an error");
        }
    }

    return 0;
}
