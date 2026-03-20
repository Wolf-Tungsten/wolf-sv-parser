#include "grh.hpp"
#include "transform.hpp"
#include "transform/instance_inline.hpp"

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
        std::cerr << "[transform-instance-inline] " << message << '\n';
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

    wolvrix::lib::grh::OperationId findInstanceByName(const wolvrix::lib::grh::Graph &graph,
                                                      std::string_view instanceName)
    {
        for (const auto opId : graph.operations())
        {
            if (!opId.valid())
            {
                continue;
            }
            const auto op = graph.getOperation(opId);
            if (op.kind() != wolvrix::lib::grh::OperationKind::kInstance)
            {
                continue;
            }
            const auto name = getAttrString(op, "instanceName");
            if (name && *name == instanceName)
            {
                return opId;
            }
        }
        return wolvrix::lib::grh::OperationId::invalid();
    }

    bool runInlinePass(wolvrix::lib::grh::Design &design,
                       std::string path,
                       PassDiagnostics &diags,
                       bool expectSuccess)
    {
        PassManager manager;
        manager.addPass(std::make_unique<InstanceInlinePass>(InstanceInlineOptions{std::move(path)}));
        const PassManagerResult result = manager.run(design, diags);
        if (expectSuccess)
        {
            return result.success && !diags.hasError();
        }
        return !result.success || diags.hasError();
    }

} // namespace

int main()
{
    {
        wolvrix::lib::grh::Design design;
        auto &leaf = design.createGraph("leaf");
        auto &child = design.createGraph("child");
        auto &mid = design.createGraph("mid");
        auto &top = design.createGraph("top");
        design.markAsTop("top");

        const auto leafIn = leaf.createValue(leaf.internSymbol("in"), 1, false);
        const auto leafOut = leaf.createValue(leaf.internSymbol("out"), 1, false);
        leaf.bindInputPort("in", leafIn);
        leaf.bindOutputPort("out", leafOut);
        const auto leafAssign = leaf.createOperation(wolvrix::lib::grh::OperationKind::kAssign, leaf.makeInternalOpSym());
        leaf.addOperand(leafAssign, leafIn);
        leaf.addResult(leafAssign, leafOut);

        const auto childA = child.createValue(child.internSymbol("a"), 1, false);
        const auto childY = child.createValue(child.internSymbol("y"), 1, false);
        child.bindInputPort("a", childA);
        child.bindOutputPort("y", childY);
        const auto stateSym = child.internSymbol("state");
        child.addDeclaredSymbol(stateSym);
        const auto childState = child.createOperation(wolvrix::lib::grh::OperationKind::kRegister, stateSym);
        child.setAttr(childState, "width", static_cast<int64_t>(1));
        child.setAttr(childState, "isSigned", false);

        const auto childLeafOut = child.createValue(child.makeInternalValSym(), 1, false);
        const auto childLeafInst = child.createOperation(wolvrix::lib::grh::OperationKind::kInstance, child.makeInternalOpSym());
        child.addOperand(childLeafInst, childA);
        child.addResult(childLeafInst, childLeafOut);
        child.setAttr(childLeafInst, "moduleName", std::string("leaf"));
        child.setAttr(childLeafInst, "instanceName", std::string("u_leaf"));
        child.setAttr(childLeafInst, "inputPortName", std::vector<std::string>{"in"});
        child.setAttr(childLeafInst, "outputPortName", std::vector<std::string>{"out"});

        const auto childAssign = child.createOperation(wolvrix::lib::grh::OperationKind::kAssign, child.makeInternalOpSym());
        child.addOperand(childAssign, childLeafOut);
        child.addResult(childAssign, childY);

        const auto midA = mid.createValue(mid.internSymbol("a"), 1, false);
        const auto midY = mid.createValue(mid.internSymbol("y"), 1, false);
        mid.bindInputPort("a", midA);
        mid.bindOutputPort("y", midY);
        const auto midChildInst = mid.createOperation(wolvrix::lib::grh::OperationKind::kInstance, mid.makeInternalOpSym());
        mid.addOperand(midChildInst, midA);
        mid.addResult(midChildInst, midY);
        mid.setAttr(midChildInst, "moduleName", std::string("child"));
        mid.setAttr(midChildInst, "instanceName", std::string("u_child"));
        mid.setAttr(midChildInst, "inputPortName", std::vector<std::string>{"a"});
        mid.setAttr(midChildInst, "outputPortName", std::vector<std::string>{"y"});

        const auto topA = top.createValue(top.internSymbol("a"), 1, false);
        const auto topY = top.createValue(top.internSymbol("y"), 1, false);
        top.bindInputPort("a", topA);
        top.bindOutputPort("y", topY);
        const auto topMidInst = top.createOperation(wolvrix::lib::grh::OperationKind::kInstance, top.makeInternalOpSym());
        top.addOperand(topMidInst, topA);
        top.addResult(topMidInst, topY);
        top.setAttr(topMidInst, "moduleName", std::string("mid"));
        top.setAttr(topMidInst, "instanceName", std::string("u_mid"));
        top.setAttr(topMidInst, "inputPortName", std::vector<std::string>{"a"});
        top.setAttr(topMidInst, "outputPortName", std::vector<std::string>{"y"});

        PassDiagnostics diags;
        if (!runInlinePass(design, "top.u_mid.u_child", diags, true))
        {
            return fail("Expected instance-inline pass to succeed");
        }

        if (findInstanceByName(mid, "u_child").valid())
        {
            return fail("Expected target instance to be removed from parent graph");
        }
        const auto leafInstId = findInstanceByName(mid, "u_leaf");
        if (!leafInstId.valid())
        {
            return fail("Expected nested child instance to remain after one-level inline");
        }
        const auto leafInst = mid.getOperation(leafInstId);
        const auto moduleName = getAttrString(leafInst, "moduleName");
        if (!moduleName || *moduleName != "leaf")
        {
            return fail("Expected nested instance moduleName to stay intact");
        }

        if (!findInstanceByName(top, "u_mid").valid())
        {
            return fail("Expected upper-level instance to remain untouched");
        }

        const auto renamedReg = mid.findOperation("u_mid$u_child$state");
        if (!renamedReg.valid())
        {
            return fail("Expected child register to be cloned with prefixed symbol");
        }
        if (mid.getOperation(renamedReg).kind() != wolvrix::lib::grh::OperationKind::kRegister)
        {
            return fail("Prefixed cloned operation should be a register");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &child = design.createGraph("child");
        auto &top = design.createGraph("top");
        design.markAsTop("top");

        const auto childA = child.createValue(child.internSymbol("a"), 1, false);
        const auto childY = child.createValue(child.internSymbol("y"), 1, false);
        child.bindInputPort("a", childA);
        child.bindOutputPort("y", childY);
        const auto childAssign = child.createOperation(wolvrix::lib::grh::OperationKind::kAssign, child.makeInternalOpSym());
        child.addOperand(childAssign, childA);
        child.addResult(childAssign, childY);

        const auto topA = top.createValue(top.internSymbol("a"), 1, false);
        const auto topY = top.createValue(top.internSymbol("y"), 1, false);
        top.bindInputPort("a", topA);
        top.bindOutputPort("y", topY);
        const auto topChildInst = top.createOperation(wolvrix::lib::grh::OperationKind::kInstance, top.makeInternalOpSym());
        top.addOperand(topChildInst, topA);
        top.addResult(topChildInst, topY);
        top.setAttr(topChildInst, "moduleName", std::string("child"));
        top.setAttr(topChildInst, "instanceName", std::string("u_child"));
        top.setAttr(topChildInst, "inputPortName", std::vector<std::string>{"a"});
        top.setAttr(topChildInst, "outputPortName", std::vector<std::string>{"y"});

        const auto xmrVal = top.createValue(top.makeInternalValSym(), 1, false);
        const auto xmrOp = top.createOperation(wolvrix::lib::grh::OperationKind::kXMRRead, top.makeInternalOpSym());
        top.addResult(xmrOp, xmrVal);
        top.setAttr(xmrOp, "xmrPath", std::string("top.u_child.y"));

        PassDiagnostics diags;
        if (!runInlinePass(design, "top.u_child", diags, false))
        {
            return fail("Expected instance-inline to reject graphs with unresolved XMR");
        }
    }

    return 0;
}
