#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/hier_flatten.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

int fail(const std::string &message)
{
    std::cerr << "[transform-hier-flatten] " << message << '\n';
    return 1;
}

std::optional<std::string> getAttrString(const wolvrix::lib::grh::Operation &op,
                                         std::string_view key)
{
    auto attr = op.attr(key);
    if (!attr)
    {
        return std::nullopt;
    }
    if (const auto *value = std::get_if<std::string>(&*attr))
    {
        return *value;
    }
    return std::nullopt;
}

} // namespace

int main()
{
    wolvrix::lib::grh::Design design;
    wolvrix::lib::grh::Graph &child = design.createGraph("child");
    wolvrix::lib::grh::Graph &top = design.createGraph("top");
    design.markAsTop("top");

    const auto childA = child.createValue(child.internSymbol("a"), 1, false);
    const auto childAddr = child.createValue(child.internSymbol("addr"), 4, false);
    const auto childY = child.createValue(child.internSymbol("y"), 1, false);
    const auto childIoIn = child.createValue(child.internSymbol("io__in"), 1, false);
    const auto childIoOut = child.createValue(child.internSymbol("io__out"), 1, false);
    const auto childIoOe = child.createValue(child.internSymbol("io__oe"), 1, false);

    child.bindInputPort("a", childA);
    child.bindInputPort("addr", childAddr);
    child.bindOutputPort("y", childY);
    child.bindInoutPort("io", childIoIn, childIoOut, childIoOe);

    child.addDeclaredSymbol(child.getValue(childA).symbol());
    child.addDeclaredSymbol(child.getValue(childAddr).symbol());
    child.addDeclaredSymbol(child.getValue(childY).symbol());
    child.addDeclaredSymbol(child.getValue(childIoIn).symbol());
    child.addDeclaredSymbol(child.getValue(childIoOut).symbol());
    child.addDeclaredSymbol(child.getValue(childIoOe).symbol());

    const auto memSym = child.internSymbol("mem");
    child.addDeclaredSymbol(memSym);
    const auto memOp = child.createOperation(wolvrix::lib::grh::OperationKind::kMemory, memSym);
    child.setAttr(memOp, "width", static_cast<int64_t>(8));
    child.setAttr(memOp, "row", static_cast<int64_t>(16));
    child.setAttr(memOp, "isSigned", false);

    const auto memReadVal = child.createValue(child.makeInternalValSym(), 8, false);
    const auto memReadOp =
        child.createOperation(wolvrix::lib::grh::OperationKind::kMemoryReadPort,
                              child.makeInternalOpSym());
    child.addOperand(memReadOp, childAddr);
    child.addResult(memReadOp, memReadVal);
    child.setAttr(memReadOp, "memSymbol", std::string("mem"));

    const auto dpiSym = child.internSymbol("dpi_add");
    const auto dpiImport =
        child.createOperation(wolvrix::lib::grh::OperationKind::kDpicImport, dpiSym);
    child.setAttr(dpiImport, "argsDirection", std::vector<std::string>{"input"});
    child.setAttr(dpiImport, "argsWidth", std::vector<int64_t>{1});
    child.setAttr(dpiImport, "argsName", std::vector<std::string>{"x"});
    child.setAttr(dpiImport, "argsSigned", std::vector<bool>{false});
    child.setAttr(dpiImport, "argsType", std::vector<std::string>{"logic"});
    child.setAttr(dpiImport, "hasReturn", true);
    child.setAttr(dpiImport, "returnWidth", static_cast<int64_t>(1));
    child.setAttr(dpiImport, "returnSigned", false);

    const auto dpiCall =
        child.createOperation(wolvrix::lib::grh::OperationKind::kDpicCall,
                              child.makeInternalOpSym());
    child.addOperand(dpiCall, childA);
    const auto dpiRet = child.createValue(child.makeInternalValSym(), 1, false);
    child.addResult(dpiCall, dpiRet);
    child.setAttr(dpiCall, "targetImportSymbol", std::string("dpi_add"));
    child.setAttr(dpiCall, "inArgName", std::vector<std::string>{"x"});
    child.setAttr(dpiCall, "outArgName", std::vector<std::string>{});
    child.setAttr(dpiCall, "hasReturn", true);

    const auto assignOp =
        child.createOperation(wolvrix::lib::grh::OperationKind::kAssign,
                              child.makeInternalOpSym());
    child.addOperand(assignOp, childIoOut);
    child.addResult(assignOp, childY);

    const auto topA = top.createValue(top.internSymbol("a"), 1, false);
    const auto topAddr = top.createValue(top.internSymbol("addr"), 4, false);
    const auto topY = top.createValue(top.internSymbol("y"), 1, false);
    const auto topAddrInternal = top.createValue(top.makeInternalValSym(), 4, false);
    const auto topIoIn = top.createValue(top.internSymbol("io__in"), 1, false);
    const auto topIoOut = top.createValue(top.internSymbol("io__out"), 1, false);
    const auto topIoOe = top.createValue(top.internSymbol("io__oe"), 1, false);

    top.bindInputPort("a", topA);
    top.bindInputPort("addr", topAddr);
    top.bindOutputPort("y", topY);
    top.bindInoutPort("io", topIoIn, topIoOut, topIoOe);

    top.addDeclaredSymbol(top.getValue(topA).symbol());
    top.addDeclaredSymbol(top.getValue(topAddr).symbol());
    top.addDeclaredSymbol(top.getValue(topY).symbol());
    top.addDeclaredSymbol(top.getValue(topIoIn).symbol());
    top.addDeclaredSymbol(top.getValue(topIoOut).symbol());
    top.addDeclaredSymbol(top.getValue(topIoOe).symbol());

    const auto instOp =
        top.createOperation(wolvrix::lib::grh::OperationKind::kInstance,
                            top.internSymbol("_op_child"));
    top.addOperand(instOp, topA);
    top.addOperand(instOp, topAddrInternal);
    top.addOperand(instOp, topIoIn);
    top.addResult(instOp, topY);
    top.addResult(instOp, topIoOut);
    top.addResult(instOp, topIoOe);
    top.setAttr(instOp, "moduleName", std::string("child"));
    top.setAttr(instOp, "instanceName", std::string("u_child"));
    top.setAttr(instOp, "inputPortName", std::vector<std::string>{"a", "addr"});
    top.setAttr(instOp, "outputPortName", std::vector<std::string>{"y"});
    top.setAttr(instOp, "inoutPortName", std::vector<std::string>{"io"});

    PassManager manager;
    manager.addPass(std::make_unique<HierFlattenPass>());
    PassDiagnostics diags;
    const PassManagerResult result = manager.run(design, diags);
    if (!result.success || diags.hasError())
    {
        return fail("hier-flatten pass failed");
    }

    if (design.graphs().size() != 1)
    {
        return fail("Expected flattened design to contain one graph");
    }

    for (const auto opId : top.operations())
    {
        const wolvrix::lib::grh::Operation op = top.getOperation(opId);
        if (op.kind() == wolvrix::lib::grh::OperationKind::kInstance)
        {
            return fail("Expected no kInstance operations after flatten");
        }
    }

    const std::string expectedMem = "u_child$mem";
    wolvrix::lib::grh::OperationId foundMem = wolvrix::lib::grh::OperationId::invalid();
    wolvrix::lib::grh::OperationId foundRead = wolvrix::lib::grh::OperationId::invalid();
    wolvrix::lib::grh::OperationId foundDpiImport = wolvrix::lib::grh::OperationId::invalid();
    wolvrix::lib::grh::OperationId foundDpiCall = wolvrix::lib::grh::OperationId::invalid();
    bool foundAssign = false;

    for (const auto opId : top.operations())
    {
        const wolvrix::lib::grh::Operation op = top.getOperation(opId);
        switch (op.kind())
        {
        case wolvrix::lib::grh::OperationKind::kMemory:
            foundMem = opId;
            if (std::string(op.symbolText()) != expectedMem)
            {
                return fail("Flattened memory symbol mismatch");
            }
            break;
        case wolvrix::lib::grh::OperationKind::kMemoryReadPort:
            foundRead = opId;
            break;
        case wolvrix::lib::grh::OperationKind::kDpicImport:
            if (op.symbolText() == std::string_view("dpi_add"))
            {
                foundDpiImport = opId;
            }
            break;
        case wolvrix::lib::grh::OperationKind::kDpicCall:
            foundDpiCall = opId;
            break;
        case wolvrix::lib::grh::OperationKind::kAssign:
            if (!op.operands().empty() && !op.results().empty() &&
                op.operands()[0] == topIoOut &&
                op.results()[0] == topY)
            {
                foundAssign = true;
            }
            break;
        default:
            break;
        }
    }

    if (!foundMem.valid() || !foundRead.valid())
    {
        return fail("Flattened memory ops missing");
    }
    const auto memReadOpResolved = top.getOperation(foundRead);
    const auto memSymbol = getAttrString(memReadOpResolved, "memSymbol");
    if (!memSymbol || *memSymbol != expectedMem)
    {
        return fail("memSymbol not rewritten during flatten");
    }
    if (memReadOpResolved.operands().size() != 1 ||
        memReadOpResolved.operands()[0] != topAddrInternal)
    {
        return fail("Memory read port operand mismatch after flatten");
    }

    if (!foundDpiImport.valid() || !foundDpiCall.valid())
    {
        return fail("DPI ops missing after flatten");
    }
    const auto dpiCallOp = top.getOperation(foundDpiCall);
    const auto targetImport = getAttrString(dpiCallOp, "targetImportSymbol");
    if (!targetImport || *targetImport != "dpi_add")
    {
        return fail("DPI call targetImportSymbol mismatch after flatten");
    }
    if (dpiCallOp.operands().empty() || dpiCallOp.operands()[0] != topA)
    {
        return fail("DPI call operand not mapped to top input");
    }

    if (!foundAssign)
    {
        return fail("Inout-connected assign op missing after flatten");
    }

    if (std::string(top.getValue(topA).symbolText()) != "a")
    {
        return fail("Top input symbol was renamed unexpectedly");
    }
    if (std::string(top.getValue(topAddrInternal).symbolText()) != "u_child$addr")
    {
        return fail("Flattened internal port mapping symbol mismatch");
    }

    return 0;
}
