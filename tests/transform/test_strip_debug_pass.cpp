#include "core/grh.hpp"
#include "transform/strip_debug.hpp"
#include "core/transform.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[strip-debug-tests] " << message << '\n';
        return 1;
    }

    wolvrix::lib::grh::ValueId makeConst(wolvrix::lib::grh::Graph &graph,
                                         const std::string &valueName,
                                         const std::string &opName,
                                         int64_t width,
                                         bool isSigned,
                                         const std::string &literal)
    {
        const wolvrix::lib::grh::SymbolId valueSym = graph.internSymbol(valueName);
        const wolvrix::lib::grh::SymbolId opSym = graph.internSymbol(opName);
        const wolvrix::lib::grh::ValueId val = graph.createValue(valueSym, static_cast<int32_t>(width), isSigned);
        const wolvrix::lib::grh::OperationId op = graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant, opSym);
        graph.addResult(op, val);
        graph.setAttr(op, "constValue", literal);
        return val;
    }

    std::optional<std::string> getAttrString(const wolvrix::lib::grh::Operation &op, std::string_view key)
    {
        auto attr = op.attr(key);
        if (!attr)
        {
            return std::nullopt;
        }
        if (auto value = std::get_if<std::string>(&*attr))
        {
            return *value;
        }
        return std::nullopt;
    }

} // namespace

int runBasicTest()
{
    wolvrix::lib::grh::Design design;
    wolvrix::lib::grh::Graph &graph = design.createGraph("top");

    auto makeValue = [&](const std::string &name) {
        wolvrix::lib::grh::SymbolId sym = graph.internSymbol(name);
        return graph.createValue(sym, 1, false);
    };

    wolvrix::lib::grh::ValueId inA = makeValue("a");
    wolvrix::lib::grh::ValueId inB = makeValue("b");
    graph.bindInputPort("a", inA);
    graph.bindInputPort("b", inB);

    wolvrix::lib::grh::ValueId outY = makeValue("y");
    graph.bindOutputPort("y", outY);

    wolvrix::lib::grh::ValueId c0 = makeConst(graph, "c0", "c0_op", 1, false, "1'b1");

    wolvrix::lib::grh::OperationId notOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kNot, graph.internSymbol("not_op"));
    wolvrix::lib::grh::ValueId notVal = makeValue("not_val");
    graph.addOperand(notOp, c0);
    graph.addResult(notOp, notVal);

    wolvrix::lib::grh::OperationId dpiImport =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kDpicImport, graph.internSymbol("dpi_func"));
    (void)dpiImport;

    wolvrix::lib::grh::OperationId dpiCall =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kDpicCall, graph.internSymbol("dpi_call"));
    graph.addOperand(dpiCall, inA);
    graph.addOperand(dpiCall, c0);
    graph.addOperand(dpiCall, inB);
    wolvrix::lib::grh::ValueId dpiRes = makeValue("dpi_res");
    graph.addResult(dpiCall, dpiRes);
    graph.setAttr(dpiCall, "targetImportSymbol", std::string("dpi_func"));

    wolvrix::lib::grh::OperationId sysTask =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kSystemTask, graph.internSymbol("sys_task"));
    graph.addOperand(sysTask, inB);
    graph.setAttr(sysTask, "name", std::string("$display"));

    wolvrix::lib::grh::OperationId inst =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kInstance, graph.internSymbol("u_child"));
    graph.setAttr(inst, "moduleName", std::string("child"));
    graph.setAttr(inst, "inputPortName", std::vector<std::string>{"in1"});
    graph.setAttr(inst, "outputPortName", std::vector<std::string>{"out1"});
    wolvrix::lib::grh::ValueId instOut = makeValue("inst_out");
    graph.addOperand(inst, inA);
    graph.addResult(inst, instOut);

    wolvrix::lib::grh::OperationId assign =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kAssign, graph.internSymbol("assign_y"));
    graph.addOperand(assign, dpiRes);
    graph.addResult(assign, outY);

    design.markAsTop("top");

    PassManager manager;
    manager.addPass(std::make_unique<StripDebugPass>(StripDebugOptions{.path = "top"}));

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
        return fail("Expected strip-debug to succeed");
    }
    if (!res.changed)
    {
        return fail("Expected strip-debug to report changes");
    }

    auto *newTop = design.findGraph("top");
    if (!newTop)
    {
        return fail("Expected new top graph to exist");
    }
    auto *topInt = design.findGraph("top_logic_part");
    auto *topExt = design.findGraph("top_debug_part");
    if (!topInt || !topExt)
    {
        return fail("Expected top_logic_part and top_debug_part graphs");
    }

    if (topInt->findOperation("dpi_call").valid() ||
        topInt->findOperation("sys_task").valid() ||
        topInt->findOperation("u_child").valid())
    {
        return fail("Expected strip ops to be removed from top_int");
    }

    if (!topExt->findOperation("dpi_call").valid() ||
        !topExt->findOperation("sys_task").valid() ||
        !topExt->findOperation("u_child").valid() ||
        !topExt->findOperation("dpi_func").valid())
    {
        return fail("Expected strip ops to be present in top_ext");
    }

    if (!topExt->findOperation("c0_op").valid())
    {
        return fail("Expected constant clone in top_ext");
    }
    if (!topInt->findOperation("c0_op").valid())
    {
        return fail("Expected constant to remain in top_int");
    }

    bool hasExtInA = false;
    bool hasExtInB = false;
    bool hasExtOut = false;
    for (const auto &port : topExt->inputPorts())
    {
        if (port.name == "ext_in_a")
        {
            hasExtInA = true;
        }
        if (port.name == "ext_in_b")
        {
            hasExtInB = true;
        }
    }
    for (const auto &port : topExt->outputPorts())
    {
        if (port.name == "ext_out_dpi_res")
        {
            hasExtOut = true;
        }
    }
    if (!hasExtInA || !hasExtInB || !hasExtOut)
    {
        return fail("Expected boundary ports on top_ext");
    }

    bool hasIntOutA = false;
    bool hasIntOutB = false;
    bool hasIntIn = false;
    for (const auto &port : topInt->outputPorts())
    {
        if (port.name == "int_out_a")
        {
            hasIntOutA = true;
        }
        if (port.name == "int_out_b")
        {
            hasIntOutB = true;
        }
    }
    for (const auto &port : topInt->inputPorts())
    {
        if (port.name == "int_in_dpi_res")
        {
            hasIntIn = true;
        }
    }
    if (!hasIntOutA || !hasIntOutB || !hasIntIn)
    {
        return fail("Expected boundary ports on top_int");
    }

    return 0;
}

int runImportRenameTest()
{
    wolvrix::lib::grh::Design design;
    wolvrix::lib::grh::Graph &graph = design.createGraph("top");

    auto makeValue = [&](const std::string &name) {
        wolvrix::lib::grh::SymbolId sym = graph.internSymbol(name);
        return graph.createValue(sym, 1, false);
    };

    wolvrix::lib::grh::ValueId inDpi = makeValue("in_dpi");
    graph.bindInputPort("in_dpi", inDpi);

    wolvrix::lib::grh::ValueId outY = makeValue("y");
    graph.bindOutputPort("y", outY);

    wolvrix::lib::grh::OperationId dpiImport =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kDpicImport, graph.internSymbol("dpi_func"));
    (void)dpiImport;

    wolvrix::lib::grh::OperationId dpiCall =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kDpicCall, graph.internSymbol("dpi_call"));
    graph.addOperand(dpiCall, inDpi);
    wolvrix::lib::grh::ValueId dpiRes = makeValue("dpi_res");
    graph.addResult(dpiCall, dpiRes);
    graph.setAttr(dpiCall, "targetImportSymbol", std::string("dpi_func"));

    wolvrix::lib::grh::OperationId assign =
        graph.createOperation(wolvrix::lib::grh::OperationKind::kAssign, graph.internSymbol("assign_y"));
    graph.addOperand(assign, dpiRes);
    graph.addResult(assign, outY);

    design.markAsTop("top");

    PassManager manager;
    manager.addPass(std::make_unique<StripDebugPass>(StripDebugOptions{.path = "top"}));

    PassDiagnostics diags;
    PassManagerResult res{};
    try
    {
        res = manager.run(design, diags);
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Exception during run (rename test): ") + ex.what());
    }
    if (!res.success || diags.hasError())
    {
        return fail("Expected strip-debug to succeed (rename test)");
    }
    if (!res.changed)
    {
        return fail("Expected strip-debug to report changes (rename test)");
    }

    auto *topExt = design.findGraph("top_debug_part");
    if (!topExt)
    {
        return fail("Expected top_debug_part graph to exist (rename test)");
    }

    std::optional<std::string> importSym;
    std::optional<std::string> callTarget;
    for (const auto opId : topExt->operations())
    {
        const auto op = topExt->getOperation(opId);
        if (op.kind() == wolvrix::lib::grh::OperationKind::kDpicImport)
        {
            importSym = std::string(op.symbolText());
        }
        if (op.kind() == wolvrix::lib::grh::OperationKind::kDpicCall)
        {
            callTarget = getAttrString(op, "targetImportSymbol");
        }
    }

    if (!importSym || !callTarget)
    {
        return fail("Expected DPI import and call in top_debug_part (rename test)");
    }
    if (*importSym != *callTarget)
    {
        return fail("Expected DPI call targetImportSymbol to match imported symbol (rename test)");
    }

    return 0;
}

int runInstancePathTest()
{
    wolvrix::lib::grh::Design design;
    wolvrix::lib::grh::Graph &child = design.createGraph("child");
    wolvrix::lib::grh::Graph &top = design.createGraph("top");

    auto makeChildValue = [&](const std::string &name) {
        wolvrix::lib::grh::SymbolId sym = child.internSymbol(name);
        return child.createValue(sym, 1, false);
    };
    auto makeTopValue = [&](const std::string &name) {
        wolvrix::lib::grh::SymbolId sym = top.internSymbol(name);
        return top.createValue(sym, 1, false);
    };

    wolvrix::lib::grh::ValueId childIn = makeChildValue("a");
    wolvrix::lib::grh::ValueId childOut = makeChildValue("y");
    child.bindInputPort("a", childIn);
    child.bindOutputPort("y", childOut);

    wolvrix::lib::grh::OperationId sysTask =
        child.createOperation(wolvrix::lib::grh::OperationKind::kSystemTask, child.internSymbol("child_debug_task"));
    child.addOperand(sysTask, childIn);
    child.setAttr(sysTask, "name", std::string("$display"));

    wolvrix::lib::grh::OperationId assign =
        child.createOperation(wolvrix::lib::grh::OperationKind::kAssign, child.internSymbol("assign_y"));
    child.addOperand(assign, childIn);
    child.addResult(assign, childOut);

    wolvrix::lib::grh::ValueId topIn = makeTopValue("a");
    wolvrix::lib::grh::ValueId topOut = makeTopValue("y");
    top.bindInputPort("a", topIn);
    top.bindOutputPort("y", topOut);
    wolvrix::lib::grh::OperationId inst =
        top.createOperation(wolvrix::lib::grh::OperationKind::kInstance, top.internSymbol("u_child"));
    top.addOperand(inst, topIn);
    top.addResult(inst, topOut);
    top.setAttr(inst, "moduleName", std::string("child"));
    top.setAttr(inst, "instanceName", std::string("u_child"));
    top.setAttr(inst, "inputPortName", std::vector<std::string>{"a"});
    top.setAttr(inst, "outputPortName", std::vector<std::string>{"y"});

    design.markAsTop("top");

    PassManager manager;
    manager.addPass(std::make_unique<StripDebugPass>(StripDebugOptions{.path = "top.u_child"}));

    PassDiagnostics diags;
    PassManagerResult res{};
    try
    {
        res = manager.run(design, diags);
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Exception during run (instance path test): ") + ex.what());
    }
    if (!res.success || diags.hasError())
    {
        return fail("Expected strip-debug to succeed (instance path test)");
    }

    auto *childWrapper = design.findGraph("child");
    auto *childLogic = design.findGraph("child_logic_part");
    auto *childDebug = design.findGraph("child_debug_part");
    if (!childWrapper || !childLogic || !childDebug)
    {
        return fail("Expected child wrapper and split parts to exist");
    }

    if (childLogic->findOperation("child_debug_task").valid())
    {
        return fail("Expected debug ops to be removed from child_logic_part");
    }
    if (!childDebug->findOperation("child_debug_task").valid())
    {
        return fail("Expected debug ops to move into child_debug_part");
    }

    return 0;
}

int main()
{
    if (int rc = runBasicTest())
    {
        return rc;
    }
    if (int rc = runImportRenameTest())
    {
        return rc;
    }
    if (int rc = runInstancePathTest())
    {
        return rc;
    }
    return 0;
}
