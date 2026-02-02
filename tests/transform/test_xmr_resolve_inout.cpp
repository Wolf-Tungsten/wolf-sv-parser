#include "grh.hpp"
#include "transform.hpp"
#include "pass/xmr_resolve.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace wolf_sv_parser;
using namespace wolf_sv_parser::transform;

namespace
{

int fail(const std::string &message)
{
    std::cerr << "[transform-xmr-resolve-inout] " << message << '\n';
    return 1;
}

std::optional<std::vector<std::string>> getAttrStrings(const grh::ir::Operation &op,
                                                       std::string_view key)
{
    auto attr = op.attr(key);
    if (!attr)
    {
        return std::nullopt;
    }
    if (const auto *value = std::get_if<std::vector<std::string>>(&*attr))
    {
        return *value;
    }
    return std::nullopt;
}

bool startsWith(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

int main()
{
    grh::ir::Netlist netlist;
    grh::ir::Graph &child = netlist.createGraph("child");
    grh::ir::Graph &top = netlist.createGraph("top");
    netlist.markAsTop("top");

    const auto childA = child.createValue(child.internSymbol("a"), 1, false);
    const auto childY = child.createValue(child.internSymbol("y"), 1, false);
    const auto childIoIn = child.createValue(child.internSymbol("io__in"), 1, false);
    const auto childIoOut = child.createValue(child.internSymbol("io__out"), 1, false);
    const auto childIoOe = child.createValue(child.internSymbol("io__oe"), 1, false);
    child.bindInputPort(child.internSymbol("a"), childA);
    child.bindOutputPort(child.internSymbol("y"), childY);
    child.bindInoutPort(child.internSymbol("io"), childIoIn, childIoOut, childIoOe);
    child.createValue(child.internSymbol("leaf_r"), 1, false);
    child.createValue(child.internSymbol("leaf_w"), 1, false);

    const auto topA = top.createValue(top.internSymbol("a"), 1, false);
    const auto topY = top.createValue(top.internSymbol("y"), 1, false);
    const auto topIoOut = top.createValue(top.internSymbol("io__out"), 1, false);
    const auto topIoOe = top.createValue(top.internSymbol("io__oe"), 1, false);
    const auto topIoIn = top.createValue(top.internSymbol("io__in"), 1, false);

    grh::ir::OperationId instOp =
        top.createOperation(grh::ir::OperationKind::kInstance,
                            grh::ir::SymbolId::invalid());
    top.addOperand(instOp, topA);
    top.addOperand(instOp, topIoOut);
    top.addOperand(instOp, topIoOe);
    top.addResult(instOp, topY);
    top.addResult(instOp, topIoIn);
    top.setAttr(instOp, "moduleName", std::string("child"));
    top.setAttr(instOp, "instanceName", std::string("u_child"));
    top.setAttr(instOp, "inputPortName", std::vector<std::string>{"a"});
    top.setAttr(instOp, "outputPortName", std::vector<std::string>{"y"});
    top.setAttr(instOp, "inoutPortName", std::vector<std::string>{"io"});

    const auto readValue = top.createValue(top.internSymbol("xmr_read"), 1, false);
    grh::ir::OperationId xmrRead =
        top.createOperation(grh::ir::OperationKind::kXMRRead,
                            grh::ir::SymbolId::invalid());
    top.addResult(xmrRead, readValue);
    top.setAttr(xmrRead, "xmrPath", std::string("u_child.leaf_r"));

    const auto writeValue = top.createValue(top.internSymbol("xmr_write"), 1, false);
    grh::ir::OperationId xmrWrite =
        top.createOperation(grh::ir::OperationKind::kXMRWrite,
                            grh::ir::SymbolId::invalid());
    top.addOperand(xmrWrite, writeValue);
    top.setAttr(xmrWrite, "xmrPath", std::string("u_child.leaf_w"));

    PassManager manager;
    manager.addPass(std::make_unique<XmrResolvePass>());
    PassDiagnostics diags;
    const PassManagerResult result = manager.run(netlist, diags);
    if (!result.success || diags.hasError())
    {
        return fail("XMR resolve pass failed");
    }

    const grh::ir::Operation op = top.getOperation(instOp);
    const auto inputNames = getAttrStrings(op, "inputPortName");
    const auto outputNames = getAttrStrings(op, "outputPortName");
    const auto inoutNames = getAttrStrings(op, "inoutPortName");
    if (!inputNames || !outputNames || !inoutNames)
    {
        return fail("Instance missing port name attributes");
    }
    if (inputNames->size() != 2 || (*inputNames)[0] != "a")
    {
        return fail("Input port names not updated as expected");
    }
    if (outputNames->size() != 2 || (*outputNames)[0] != "y")
    {
        return fail("Output port names not updated as expected");
    }
    if (inoutNames->size() != 1 || (*inoutNames)[0] != "io")
    {
        return fail("Inout port names changed unexpectedly");
    }
    if (!startsWith((*inputNames)[1], "__xmr_w_"))
    {
        return fail("XMR write port not inserted into input names");
    }
    if (!startsWith((*outputNames)[1], "__xmr_r_"))
    {
        return fail("XMR read port not inserted into output names");
    }

    const auto operands = op.operands();
    const auto results = op.results();
    if (operands.size() != 4 || results.size() != 3)
    {
        return fail("Instance operand/result count mismatch after resolve");
    }
    if (operands[0] != topA || operands[2] != topIoOut || operands[3] != topIoOe)
    {
        return fail("Inout operands were reordered by XMR resolve");
    }
    if (results[0] != topY || results[2] != topIoIn)
    {
        return fail("Inout results were reordered by XMR resolve");
    }

    return 0;
}
