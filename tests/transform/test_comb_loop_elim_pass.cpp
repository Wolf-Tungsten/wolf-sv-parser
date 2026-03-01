#include "grh.hpp"
#include "transform/comb_loop_elim.hpp"
#include "transform.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[comb-loop-elim-tests] " << message << '\n';
        return 1;
    }

    wolvrix::lib::grh::ValueId makeValue(wolvrix::lib::grh::Graph &graph,
                                         const std::string &name,
                                         int32_t width)
    {
        const wolvrix::lib::grh::SymbolId sym = graph.internSymbol(name);
        return graph.createValue(sym, width, false);
    }

    wolvrix::lib::grh::OperationId makeSliceStatic(wolvrix::lib::grh::Graph &graph,
                                                   wolvrix::lib::grh::ValueId base,
                                                   wolvrix::lib::grh::ValueId result,
                                                   int64_t low,
                                                   int64_t high,
                                                   const std::string &name)
    {
        const wolvrix::lib::grh::SymbolId sym = graph.internSymbol(name);
        const wolvrix::lib::grh::OperationId op =
            graph.createOperation(wolvrix::lib::grh::OperationKind::kSliceStatic, sym);
        graph.addOperand(op, base);
        graph.addResult(op, result);
        graph.setAttr(op, "sliceStart", low);
        graph.setAttr(op, "sliceEnd", high);
        return op;
    }

    wolvrix::lib::grh::OperationId makeConcat(wolvrix::lib::grh::Graph &graph,
                                              const std::string &name,
                                              const std::vector<wolvrix::lib::grh::ValueId> &operands,
                                              wolvrix::lib::grh::ValueId result)
    {
        const wolvrix::lib::grh::SymbolId sym = graph.internSymbol(name);
        const wolvrix::lib::grh::OperationId op =
            graph.createOperation(wolvrix::lib::grh::OperationKind::kConcat, sym);
        for (const auto operand : operands)
        {
            graph.addOperand(op, operand);
        }
        graph.addResult(op, result);
        return op;
    }

    int test_false_loop_fixed()
    {
        wolvrix::lib::grh::Design design;
        wolvrix::lib::grh::Graph &graph = design.createGraph("g_false");

        auto a_high = makeValue(graph, "a_high", 4);
        auto b_high = makeValue(graph, "b_high", 4);

        auto a_low_from_b = makeValue(graph, "a_low_from_b", 4);
        auto a = makeValue(graph, "a", 8);
        auto b_low_from_a = makeValue(graph, "b_low_from_a", 4);
        auto b = makeValue(graph, "b", 8);

        makeSliceStatic(graph, b, a_low_from_b, 0, 3, "slice_b_low");
        makeConcat(graph, "concat_a", {a_high, a_low_from_b}, a);

        makeSliceStatic(graph, a, b_low_from_a, 4, 7, "slice_a_high");
        makeConcat(graph, "concat_b", {b_high, b_low_from_a}, b);

        const std::size_t valuesBefore = graph.values().size();

        PassManager manager;
        manager.addPass(std::make_unique<CombLoopElimPass>());

        PassDiagnostics diags;
        PassManagerResult res{};
        try
        {
            res = manager.run(design, diags);
        }
        catch (const std::exception &ex)
        {
            return fail(std::string("Exception during run: ") + ex.what());
        }
        if (!res.success || diags.hasError())
        {
            return fail("Expected comb-loop-elim to succeed");
        }
        if (!res.changed)
        {
            return fail("Expected comb-loop-elim to report changes for false loop");
        }
        if (!diags.messages().empty())
        {
            return fail("Expected no diagnostics for fixed false loop");
        }

        if (graph.values().size() <= valuesBefore)
        {
            return fail("Expected comb-loop-elim to split values for false loop");
        }

        CombLoopElimOptions verifyOptions;
        verifyOptions.fixFalseLoops = false;
        PassManager verifyManager;
        verifyManager.addPass(std::make_unique<CombLoopElimPass>(verifyOptions));

        PassDiagnostics verifyDiags;
        PassManagerResult verifyRes{};
        try
        {
            verifyRes = verifyManager.run(design, verifyDiags);
        }
        catch (const std::exception &ex)
        {
            return fail(std::string("Exception during verify run: ") + ex.what());
        }
        if (!verifyRes.success || verifyDiags.hasError())
        {
            return fail("Expected comb-loop-elim to succeed during verify run");
        }
        for (const auto &msg : verifyDiags.messages())
        {
            if (msg.passName == "comb-loop-elim" &&
                msg.message.find("comb loop detected") != std::string::npos)
            {
                return fail("Expected no comb-loop warnings after split");
            }
        }

        return 0;
    }

    int test_true_loop_reported()
    {
        wolvrix::lib::grh::Design design;
        wolvrix::lib::grh::Graph &graph = design.createGraph("g_true");

        auto a = makeValue(graph, "a", 1);
        auto x = makeValue(graph, "x", 1);
        auto y = makeValue(graph, "y", 1);

        const auto xorOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("xor_xy"));
        graph.addOperand(xorOp, a);
        graph.addOperand(xorOp, y);
        graph.addResult(xorOp, x);

        const auto notOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kNot,
                                                 graph.internSymbol("not_x"));
        graph.addOperand(notOp, x);
        graph.addResult(notOp, y);

        CombLoopElimOptions options;
        options.fixFalseLoops = false;
        PassManager manager;
        manager.addPass(std::make_unique<CombLoopElimPass>(options));

        PassDiagnostics diags;
        PassManagerResult res{};
        try
        {
            res = manager.run(design, diags);
        }
        catch (const std::exception &ex)
        {
            return fail(std::string("Exception during run: ") + ex.what());
        }
        if (!res.success || diags.hasError())
        {
            return fail("Expected comb-loop-elim to succeed with warnings");
        }
        if (res.changed)
        {
            return fail("Expected comb-loop-elim to avoid changes for true loop");
        }

        bool foundWarning = false;
        for (const auto &msg : diags.messages())
        {
            if (msg.passName == "comb-loop-elim" &&
                msg.message.find("comb loop detected") != std::string::npos)
            {
                foundWarning = true;
                break;
            }
        }
        if (!foundWarning)
        {
            return fail("Expected comb-loop-elim warning for true loop");
        }
        return 0;
    }

} // namespace

int main()
{
    if (int rc = test_false_loop_fixed(); rc != 0)
    {
        return rc;
    }
    if (int rc = test_true_loop_reported(); rc != 0)
    {
        return rc;
    }
    return 0;
}
