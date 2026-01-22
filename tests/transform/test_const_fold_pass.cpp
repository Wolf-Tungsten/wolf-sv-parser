#include "grh.hpp"
#include "pass/const_fold.hpp"
#include "transform.hpp"

#include "slang/numeric/SVInt.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace wolf_sv_parser;
using namespace wolf_sv_parser::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[const-fold-tests] " << message << '\n';
        return 1;
    }

    grh::ir::ValueId makeConst(grh::ir::Graph &graph, const std::string &valueName, const std::string &opName, int64_t width, bool isSigned, const std::string &literal)
    {
        const grh::ir::SymbolId valueSym = graph.internSymbol(valueName);
        const grh::ir::SymbolId opSym = graph.internSymbol(opName);
        const grh::ir::ValueId val = graph.createValue(valueSym, static_cast<int32_t>(width), isSigned);
        const grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kConstant, opSym);
        graph.addResult(op, val);
        graph.setAttr(op, "constValue", literal);
        return val;
    }

    std::optional<slang::SVInt> getConstLiteral(const grh::ir::Graph &graph, const grh::ir::Operation &op)
    {
        (void)graph;
        auto attr = op.attr("constValue");
        if (!attr)
        {
            return std::nullopt;
        }
        const auto *val = std::get_if<std::string>(&*attr);
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
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g");
        grh::ir::ValueId c0 = makeConst(graph, "c0", "c0_op", 4, false, "4'h3");
        grh::ir::ValueId c1 = makeConst(graph, "c1", "c1_op", 4, false, "4'h1");

        grh::ir::ValueId sum = graph.createValue(graph.internSymbol("sum"), 4, false);
        grh::ir::OperationId add = graph.createOperation(grh::ir::OperationKind::kAdd, graph.internSymbol("add0"));
        graph.addOperand(add, c0);
        graph.addOperand(add, c1);
        graph.addResult(add, sum);

        grh::ir::ValueId pass = graph.createValue(graph.internSymbol("pass"), 4, false);
        grh::ir::OperationId assign = graph.createOperation(grh::ir::OperationKind::kAssign, graph.internSymbol("assign0"));
        graph.addOperand(assign, sum);
        graph.addResult(assign, pass);

        grh::ir::ValueId neg = graph.createValue(graph.internSymbol("neg"), 4, false);
        grh::ir::OperationId invert = graph.createOperation(grh::ir::OperationKind::kNot, graph.internSymbol("not0"));
        graph.addOperand(invert, pass);
        graph.addResult(invert, neg);

        grh::ir::ValueId finalSum = graph.createValue(graph.internSymbol("finalSum"), 4, false);
        grh::ir::OperationId add2 = graph.createOperation(grh::ir::OperationKind::kAdd, graph.internSymbol("add1"));
        graph.addOperand(add2, neg);
        graph.addOperand(add2, c1);
        graph.addResult(add2, finalSum);

        grh::ir::ValueId out = graph.createValue(graph.internSymbol("out"), 4, false);
        graph.bindOutputPort(graph.internSymbol("out"), out);
        grh::ir::OperationId assignOut = graph.createOperation(grh::ir::OperationKind::kAssign, graph.internSymbol("assign1"));
        graph.addOperand(assignOut, finalSum);
        graph.addResult(assignOut, out);

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
        if (graph.findOperation("assign1").valid())
        {
            return fail("assign1 should be removed after folding");
        }
        grh::ir::ValueId outVal = graph.outputPortValue(graph.internSymbol("out"));
        if (!outVal.valid())
        {
            return fail("Output port was not rewired to a constant");
        }
        grh::ir::Value outValue = graph.getValue(outVal);
        if (!outValue.definingOp().valid() ||
            graph.getOperation(outValue.definingOp()).kind() != grh::ir::OperationKind::kConstant)
        {
            return fail("Output port was not rewired to a constant");
        }
        auto literal = getConstLiteral(graph, graph.getOperation(outValue.definingOp()));
        slang::SVInt expected = slang::SVInt::fromString("4'hc").resize(4);
        if (!literal || !static_cast<bool>((*literal == expected)))
        {
            return fail("Final constant value mismatch");
        }
        if (!graph.getValue(neg).users().empty())
        {
            return fail("Expected users of intermediate value to be rewired");
        }
    }

    // Case 2: X operand blocks folding when allowXPropagation=false
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g2");
        grh::ir::ValueId xval = makeConst(graph, "cx", "cx_op", 1, false, "1'bx");
        grh::ir::ValueId one = makeConst(graph, "c1", "c1_op", 1, false, "1'b1");

        grh::ir::ValueId andOut = graph.createValue(graph.internSymbol("andOut"), 1, false);
        grh::ir::OperationId op = graph.createOperation(grh::ir::OperationKind::kAnd, graph.internSymbol("and0"));
        graph.addOperand(op, xval);
        graph.addOperand(op, one);
        graph.addResult(op, andOut);

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
        const grh::ir::Operation opView = graph.getOperation(op);
        if (opView.operands()[0] != xval || opView.operands()[1] != one)
        {
            return fail("Operands should remain unchanged when folding is skipped");
        }
    }

    // Case 3: missing attribute triggers failure
    {
        grh::ir::Netlist netlist;
        grh::ir::Graph &graph = netlist.createGraph("g3");
        grh::ir::ValueId c = makeConst(graph, "c", "c_op", 2, false, "2'h1");

        grh::ir::ValueId repOut = graph.createValue(graph.internSymbol("repOut"), 4, false);
        grh::ir::OperationId rep = graph.createOperation(grh::ir::OperationKind::kReplicate, graph.internSymbol("rep0"));
        graph.addOperand(rep, c);
        graph.addResult(rep, repOut);
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
