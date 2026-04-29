#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/activity_schedule.hpp"

#include <algorithm>
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
        std::cerr << "[activity-schedule-tests] " << message << '\n';
        return 1;
    }

    template <typename T>
    const T *getSessionValue(const SessionStore &session, std::string_view key)
    {
        auto it = session.find(std::string(key));
        if (it == session.end())
        {
            return nullptr;
        }
        auto *typed = dynamic_cast<const SessionSlotValue<T> *>(it->second.get());
        if (typed == nullptr)
        {
            return nullptr;
        }
        return &typed->value;
    }

    wolvrix::lib::grh::ValueId makeValue(wolvrix::lib::grh::Graph &graph,
                                         const std::string &name,
                                         int32_t width,
                                         bool isSigned = false)
    {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
    }

    std::size_t supernodeSizeForOp(const ActivityScheduleSupernodeToOps &supernodeToOps,
                                   const ActivityScheduleOpToSupernode &opToSupernode,
                                   wolvrix::lib::grh::OperationId opId)
    {
        if (opId.index == 0 || opId.index > opToSupernode.size())
        {
            return 0;
        }
        const uint32_t supernodeId = opToSupernode[opId.index - 1];
        if (supernodeId == kInvalidActivitySupernodeId || supernodeId >= supernodeToOps.size())
        {
            return 0;
        }
        return supernodeToOps[supernodeId].size();
    }

    bool isCommitPhaseOp(const wolvrix::lib::grh::Operation &op)
    {
        using wolvrix::lib::grh::OperationKind;
        switch (op.kind())
        {
        case OperationKind::kRegisterWritePort:
        case OperationKind::kLatchWritePort:
        case OperationKind::kMemoryWritePort:
        case OperationKind::kMemoryFillPort:
            return true;
        default:
            return false;
        }
    }

} // namespace

int main()
{
    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top");
        design.markAsTop("top");

        const auto clk = makeValue(graph, "clk", 1, false);
        const auto en = makeValue(graph, "en", 1, false);
        const auto a = makeValue(graph, "a", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("a", a);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read", 8, false);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                                   graph.internSymbol("q_read_op"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "regSymbol", std::string("q"));

        const auto maskValue = makeValue(graph, "mask_all", 8, false);
        const auto maskOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant,
                                                  graph.internSymbol("mask_const"));
        graph.addResult(maskOp, maskValue);
        graph.setAttr(maskOp, "constValue", std::string("8'hFF"));

        const auto sumValue = makeValue(graph, "sum", 8, false);
        const auto addOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                 graph.internSymbol("sum_add"));
        graph.addOperand(addOp, qReadValue);
        graph.addOperand(addOp, a);
        graph.addResult(addOp, sumValue);
        graph.bindOutputPort("y", sumValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, sumValue);
        graph.addOperand(writeOp, maskValue);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(
            ActivityScheduleOptions{.path = "top", .enableReplication = false}));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected activity-schedule pass to succeed");
        }
        if (!graph.frozen())
        {
            return fail("Expected graph to be frozen after activity-schedule");
        }

        const std::string keyPrefix = "top.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        const auto *valueFanout =
            getSessionValue<ActivityScheduleValueFanout>(session, keyPrefix + "value_fanout");
        const auto *topoOrder =
            getSessionValue<ActivityScheduleTopoOrder>(session, keyPrefix + "topo_order");
        const auto *stateReadSupernodes =
            getSessionValue<ActivityScheduleStateReadSupernodes>(session, keyPrefix + "state_read_supernodes");

        if (supernodeToOps == nullptr || opToSupernode == nullptr || valueFanout == nullptr ||
            topoOrder == nullptr || stateReadSupernodes == nullptr)
        {
            return fail("Expected all activity-schedule session outputs to exist");
        }

        if (supernodeToOps->empty() || topoOrder->size() != supernodeToOps->size())
        {
            return fail("Unexpected activity-schedule supernode shape after coarsen/partition");
        }

        for (std::size_t i = 0; i < supernodeToOps->size(); ++i)
        {
            if ((*supernodeToOps)[i].empty())
            {
                return fail("Expected non-empty supernodes");
            }
        }

        const uint32_t addSupernode = (*opToSupernode)[addOp.index - 1];
        const uint32_t writeSupernode = (*opToSupernode)[writeOp.index - 1];
        const uint32_t readSupernode = (*opToSupernode)[qReadOp.index - 1];
        const uint32_t maskSupernode = (*opToSupernode)[maskOp.index - 1];
        if (addSupernode == kInvalidActivitySupernodeId || writeSupernode == kInvalidActivitySupernodeId ||
            readSupernode == kInvalidActivitySupernodeId || maskSupernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected partitionable ops to map to supernodes");
        }
        if ((*opToSupernode)[regDecl.index - 1] != kInvalidActivitySupernodeId)
        {
            return fail("Register declaration should not participate in partition");
        }
        if (addSupernode != readSupernode)
        {
            return fail("Expected read/add chain to be coarsened into one supernode");
        }
        if (addOp.index == 0 || writeOp.index == 0 || sumValue.index == 0 || clk.index == 0)
        {
            return fail("Expected valid op/value ids");
        }
        auto stateReadersIt = stateReadSupernodes->find("q");
        if (stateReadersIt == stateReadSupernodes->end())
        {
            return fail("Expected state_read_supernodes to contain q");
        }
        if (stateReadersIt->second.size() != 1 || stateReadersIt->second.front() != addSupernode)
        {
            return fail("Expected q readers to map to the merged read/add supernode");
        }

        if (sumValue.index == 0 || sumValue.index > valueFanout->size())
        {
            return fail("Expected valid sum value fanout entry");
        }
        if (writeSupernode != addSupernode &&
            ((*valueFanout)[sumValue.index - 1].empty() ||
             std::find((*valueFanout)[sumValue.index - 1].begin(), (*valueFanout)[sumValue.index - 1].end(), writeSupernode) ==
                 (*valueFanout)[sumValue.index - 1].end()))
        {
            return fail("Expected value fanout from sum value into write supernode");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top2");
        design.markAsTop("top2");

        const auto a = makeValue(graph, "a", 8, false);
        const auto b = makeValue(graph, "b", 8, false);
        const auto c = makeValue(graph, "c", 8, false);
        const auto d = makeValue(graph, "d", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto v0 = makeValue(graph, "v0", 8, false);
        const auto op0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                               graph.internSymbol("op0"));
        graph.addOperand(op0, a);
        graph.addOperand(op0, b);
        graph.addResult(op0, v0);

        const auto v1 = makeValue(graph, "v1", 8, false);
        const auto op1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                               graph.internSymbol("op1"));
        graph.addOperand(op1, v0);
        graph.addOperand(op1, b);
        graph.addResult(op1, v1);

        const auto v2 = makeValue(graph, "v2", 8, false);
        const auto op2 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                               graph.internSymbol("op2"));
        graph.addOperand(op2, v1);
        graph.addOperand(op2, c);
        graph.addResult(op2, v2);

        const auto y = makeValue(graph, "y", 8, false);
        const auto op3 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                               graph.internSymbol("op3"));
        graph.addOperand(op3, v2);
        graph.addOperand(op3, d);
        graph.addResult(op3, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(
            ActivityScheduleOptions{.path = "top2", .supernodeMaxSize = 4, .enableReplication = false}));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected bounded activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top2.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected bounded partition session outputs");
        }
        if (supernodeToOps->size() != 1)
        {
            return fail("Expected pure output chain to become one coarsened compute supernode");
        }
        if (supernodeToOps->front().size() != 4)
        {
            return fail("Expected coarsen to absorb the full four-op output chain");
        }
        if ((*opToSupernode)[op0.index - 1] != (*opToSupernode)[op1.index - 1])
        {
            return fail("Expected leading output-chain ops to share one supernode");
        }
        if ((*opToSupernode)[op2.index - 1] != (*opToSupernode)[op3.index - 1])
        {
            return fail("Expected trailing output-chain ops to share one supernode");
        }
        if ((*opToSupernode)[op1.index - 1] != (*opToSupernode)[op2.index - 1])
        {
            return fail("Expected output-chain middle ops to remain in the same compute supernode");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top3");
        design.markAsTop("top3");

        const auto a = makeValue(graph, "a", 8, false);
        const auto b = makeValue(graph, "b", 8, false);
        const auto c = makeValue(graph, "c", 8, false);
        const auto d = makeValue(graph, "d", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto sharedValue = makeValue(graph, "shared", 8, false);
        const auto sharedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("shared_add"));
        graph.addOperand(sharedOp, a);
        graph.addOperand(sharedOp, b);
        graph.addResult(sharedOp, sharedValue);

        const auto y0 = makeValue(graph, "y0", 8, false);
        const auto andOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                 graph.internSymbol("use_and"));
        graph.addOperand(andOp, sharedValue);
        graph.addOperand(andOp, c);
        graph.addResult(andOp, y0);
        graph.bindOutputPort("y0", y0);

        const auto y1 = makeValue(graph, "y1", 8, false);
        const auto xorOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("use_xor"));
        graph.addOperand(xorOp, sharedValue);
        graph.addOperand(xorOp, d);
        graph.addResult(xorOp, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top3",
            .supernodeMaxSize = 1,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableReplication = true,
            .replicationMaxCost = 2,
            .replicationMaxTargets = 4,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected replication-enabled activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top3.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        const auto *valueFanout =
            getSessionValue<ActivityScheduleValueFanout>(session, keyPrefix + "value_fanout");
        if (supernodeToOps == nullptr || opToSupernode == nullptr || valueFanout == nullptr)
        {
            return fail("Expected replication session outputs");
        }

        const auto sharedSym = graph.lookupSymbol("shared_add");
        if (!sharedSym.valid())
        {
            return fail("Expected original shared_add symbol to exist");
        }
        if (graph.findOperation(sharedSym).valid())
        {
            return fail("Expected original shared_add op to be removed after replication");
        }

        std::size_t addCount = 0;
        for (const auto opId : graph.operations())
        {
            if (graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kAdd)
            {
                ++addCount;
            }
        }
        if (addCount != 2)
        {
            return fail("Expected shared add to be replicated into two cloned add ops");
        }

        if (supernodeToOps->size() != 2)
        {
            return fail("Expected replication to collapse the empty source supernode");
        }
        for (const auto &ops : *supernodeToOps)
        {
            if (ops.size() != 2)
            {
                return fail("Expected each replicated consumer supernode to contain clone plus consumer");
            }
        }
        for (const auto &succs : *valueFanout)
        {
            if (!succs.empty())
            {
                return fail("Expected no residual cross-supernode value fanout after replication");
            }
        }

        std::optional<std::size_t> andCluster;
        std::optional<std::size_t> xorCluster;
        for (std::size_t i = 0; i < supernodeToOps->size(); ++i)
        {
            for (const auto opId : (*supernodeToOps)[i])
            {
                const auto symbol = std::string(graph.getOperation(opId).symbolText());
                if (symbol == "use_and")
                {
                    andCluster = i;
                }
                if (symbol == "use_xor")
                {
                    xorCluster = i;
                }
            }
        }
        if (!andCluster || !xorCluster || *andCluster == *xorCluster)
        {
            return fail("Expected replicated consumers to stay in separate supernodes");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top4");
        design.markAsTop("top4");

        const auto a = makeValue(graph, "a", 8, false);
        const auto b = makeValue(graph, "b", 8, false);
        const auto c = makeValue(graph, "c", 8, false);
        const auto d = makeValue(graph, "d", 8, false);
        const auto e = makeValue(graph, "e", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);
        graph.bindInputPort("e", e);

        const auto v0 = makeValue(graph, "v0_top4", 8, false);
        const auto op0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                               graph.internSymbol("top4_op0"));
        graph.addOperand(op0, a);
        graph.addOperand(op0, b);
        graph.addResult(op0, v0);

        const auto v1 = makeValue(graph, "v1_top4", 8, false);
        const auto op1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                               graph.internSymbol("top4_op1"));
        graph.addOperand(op1, v0);
        graph.addOperand(op1, c);
        graph.addResult(op1, v1);

        const auto v2 = makeValue(graph, "v2_top4", 8, false);
        const auto op2 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                               graph.internSymbol("top4_op2"));
        graph.addOperand(op2, v1);
        graph.addOperand(op2, d);
        graph.addResult(op2, v2);

        const auto y = makeValue(graph, "y_top4", 8, false);
        const auto op3 = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                               graph.internSymbol("top4_op3"));
        graph.addOperand(op3, v2);
        graph.addOperand(op3, e);
        graph.addResult(op3, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top4",
            .supernodeMaxSize = 4,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected tie-break activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top4.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected tie-break partition session outputs");
        }
        if (supernodeToOps->size() != 1)
        {
            return fail("Expected pure output chain to collapse into one coarsened compute supernode");
        }
        if ((*opToSupernode)[op0.index - 1] != (*opToSupernode)[op1.index - 1])
        {
            return fail("Expected output-chain head ops to share the same compute supernode");
        }
        if ((*opToSupernode)[op1.index - 1] != (*opToSupernode)[op2.index - 1] ||
            (*opToSupernode)[op2.index - 1] != (*opToSupernode)[op3.index - 1])
        {
            return fail("Expected output-chain trailing ops to stay in the same compute supernode");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top4b");
        design.markAsTop("top4b");

        const auto a = makeValue(graph, "a_top4b", 8, false);
        graph.bindInputPort("a", a);

        const auto v0 = makeValue(graph, "v0_top4b", 8, false);
        const auto op0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kNot,
                                               graph.internSymbol("top4b_op0"));
        graph.addOperand(op0, a);
        graph.addResult(op0, v0);

        const auto y = makeValue(graph, "y_top4b", 8, false);
        const auto op1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kNot,
                                               graph.internSymbol("top4b_op1"));
        graph.addOperand(op1, v0);
        graph.addResult(op1, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top4b",
            .supernodeMaxSize = 2,
            .enableChainMerge = false,
            .enableSiblingMerge = false,
            .enableForwardMerge = true,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected guaranteed-change singleton merge to coarsen a kNot chain");
        }

        const std::string keyPrefix = "top4b.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected kNot-chain session outputs");
        }
        if (supernodeToOps->size() != 1 || supernodeToOps->front().size() != 2)
        {
            return fail("Expected guaranteed-change merge to collapse the two-op kNot chain");
        }
        if ((*opToSupernode)[op0.index - 1] != (*opToSupernode)[op1.index - 1])
        {
            return fail("Expected both kNot ops to land in the same supernode");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top4c");
        design.markAsTop("top4c");

        const auto a = makeValue(graph, "a_top4c", 8, false);
        graph.bindInputPort("a", a);

        const auto v0 = makeValue(graph, "v0_top4c", 8, false);
        const auto op0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kNot,
                                               graph.internSymbol("top4c_op0"));
        graph.addOperand(op0, a);
        graph.addResult(op0, v0);

        const auto c1 = makeValue(graph, "c1_top4c", 8, false);
        const auto c1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant,
                                                graph.internSymbol("top4c_const"));
        graph.addResult(c1Op, c1);
        graph.setAttr(c1Op, "constValue", std::string("8'h01"));

        const auto y = makeValue(graph, "y_top4c", 8, false);
        const auto op1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                               graph.internSymbol("top4c_op1"));
        graph.addOperand(op1, v0);
        graph.addOperand(op1, c1);
        graph.addResult(op1, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top4c",
            .supernodeMaxSize = 2,
            .enableChainMerge = false,
            .enableSiblingMerge = false,
            .enableForwardMerge = true,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected guaranteed-change singleton merge to coarsen an add-const chain");
        }

        const std::string keyPrefix = "top4c.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected add-const-chain session outputs");
        }
        if (supernodeSizeForOp(*supernodeToOps, *opToSupernode, op1) != 2)
        {
            return fail("Expected guaranteed-change merge to absorb add-const into its predecessor");
        }
        if ((*opToSupernode)[op0.index - 1] != (*opToSupernode)[op1.index - 1])
        {
            return fail("Expected add-const chain to share one compute supernode");
        }
        if ((*opToSupernode)[c1Op.index - 1] == (*opToSupernode)[op1.index - 1])
        {
            return fail("Expected constant seed to remain outside the merged add-const compute supernode");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top5");
        design.markAsTop("top5");

        const auto clk = makeValue(graph, "clk_top5", 1, false);
        const auto en0 = makeValue(graph, "en0_top5", 1, false);
        const auto en1 = makeValue(graph, "en1_top5", 1, false);
        const auto en2 = makeValue(graph, "en2_top5", 1, false);
        const auto mask0 = makeValue(graph, "mask0_top5", 8, false);
        const auto mask1 = makeValue(graph, "mask1_top5", 8, false);
        const auto mask2 = makeValue(graph, "mask2_top5", 8, false);
        const auto d0 = makeValue(graph, "d0_top5", 8, false);
        const auto d1 = makeValue(graph, "d1_top5", 8, false);
        const auto d2 = makeValue(graph, "d2_top5", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en0", en0);
        graph.bindInputPort("en1", en1);
        graph.bindInputPort("en2", en2);
        graph.bindInputPort("mask0", mask0);
        graph.bindInputPort("mask1", mask1);
        graph.bindInputPort("mask2", mask2);
        graph.bindInputPort("d0", d0);
        graph.bindInputPort("d1", d1);
        graph.bindInputPort("d2", d2);

        const auto reg0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                graph.internSymbol("q0_top5"));
        const auto reg1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                graph.internSymbol("q1_top5"));
        const auto reg2 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                graph.internSymbol("q2_top5"));
        graph.setAttr(reg0, "width", static_cast<int64_t>(8));
        graph.setAttr(reg0, "isSigned", false);
        graph.setAttr(reg1, "width", static_cast<int64_t>(8));
        graph.setAttr(reg1, "isSigned", false);
        graph.setAttr(reg2, "width", static_cast<int64_t>(8));
        graph.setAttr(reg2, "isSigned", false);

        const auto write0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("q0_write_top5"));
        graph.addOperand(write0, en0);
        graph.addOperand(write0, d0);
        graph.addOperand(write0, mask0);
        graph.addOperand(write0, clk);
        graph.setAttr(write0, "regSymbol", std::string("q0_top5"));
        graph.setAttr(write0, "eventEdge", std::vector<std::string>{"posedge"});

        const auto write1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("q1_write_top5"));
        graph.addOperand(write1, en1);
        graph.addOperand(write1, d1);
        graph.addOperand(write1, mask1);
        graph.addOperand(write1, clk);
        graph.setAttr(write1, "regSymbol", std::string("q1_top5"));
        graph.setAttr(write1, "eventEdge", std::vector<std::string>{"posedge"});

        const auto write2 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("q2_write_top5"));
        graph.addOperand(write2, en2);
        graph.addOperand(write2, d2);
        graph.addOperand(write2, mask2);
        graph.addOperand(write2, clk);
        graph.setAttr(write2, "regSymbol", std::string("q2_top5"));
        graph.setAttr(write2, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top5",
            .supernodeMaxSize = 1,
            .maxSinkSupernodeOp = 3,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected sink-supernode activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top5.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected sink-supernode session outputs");
        }
        if (supernodeToOps->size() != 1 || supernodeToOps->front().size() != 3)
        {
            return fail("Expected sink ops to coarsen into one oversized sink supernode");
        }
        if (supernodeSizeForOp(*supernodeToOps, *opToSupernode, write0) <= 1)
        {
            return fail("Expected sink supernode to exceed normal supernode-max-size");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top5_fill");
        design.markAsTop("top5_fill");

        const auto clk = makeValue(graph, "clk_top5_fill", 1, false);
        const auto en = makeValue(graph, "en_top5_fill", 1, false);
        const auto fillEn = makeValue(graph, "fill_en_top5_fill", 1, false);
        const auto addr = makeValue(graph, "addr_top5_fill", 2, false);
        const auto data = makeValue(graph, "data_top5_fill", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("addr", addr);
        graph.bindInputPort("data", data);

        const auto mask = makeValue(graph, "mask_top5_fill", 8, false);
        const auto maskOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant,
                                                  graph.internSymbol("mask_const_top5_fill"));
        graph.addResult(maskOp, mask);
        graph.setAttr(maskOp, "constValue", std::string("8'hFF"));

        const auto memDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kMemory,
                                                   graph.internSymbol("mem_top5_fill"));
        graph.setAttr(memDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(memDecl, "row", static_cast<int64_t>(4));
        graph.setAttr(memDecl, "isSigned", false);

        const auto readValue = makeValue(graph, "read_value_top5_fill", 8, false);
        const auto readOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kMemoryReadPort,
                                                  graph.internSymbol("mem_read_top5_fill"));
        graph.setAttr(readOp, "memSymbol", std::string("mem_top5_fill"));
        graph.addOperand(readOp, addr);
        graph.addResult(readOp, readValue);
        graph.bindOutputPort("out", readValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kMemoryWritePort,
                                                   graph.internSymbol("mem_write_top5_fill"));
        graph.setAttr(writeOp, "memSymbol", std::string("mem_top5_fill"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, addr);
        graph.addOperand(writeOp, data);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);

        const auto fillOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kMemoryFillPort,
                                                  graph.internSymbol("mem_fill_top5_fill"));
        graph.setAttr(fillOp, "memSymbol", std::string("mem_top5_fill"));
        graph.setAttr(fillOp, "eventEdge", std::vector<std::string>{"posedge"});
        graph.addOperand(fillOp, fillEn);
        graph.addOperand(fillOp, data);
        graph.addOperand(fillOp, clk);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top5_fill",
            .supernodeMaxSize = 1,
            .maxSinkSupernodeOp = 4,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected memory-fill sink-supernode activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top5_fill.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        const auto *dag =
            getSessionValue<ActivityScheduleDag>(session, keyPrefix + "dag");
        const auto *topoOrder =
            getSessionValue<ActivityScheduleTopoOrder>(session, keyPrefix + "topo_order");
        if (supernodeToOps == nullptr || opToSupernode == nullptr || dag == nullptr || topoOrder == nullptr)
        {
            return fail("Expected memory-fill sink-supernode session outputs");
        }

        const uint32_t writeSupernode = (*opToSupernode)[writeOp.index - 1];
        const uint32_t fillSupernode = (*opToSupernode)[fillOp.index - 1];
        const uint32_t readSupernode = (*opToSupernode)[readOp.index - 1];
        if (writeSupernode == kInvalidActivitySupernodeId || fillSupernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected memory write/fill to map to sink supernodes");
        }
        if (readSupernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected memory read to map to a supernode");
        }
        if ((*dag)[fillSupernode].empty() == false)
        {
            return fail("Expected memory fill supernode to be a sink in the schedule DAG");
        }
        for (const auto opId : (*supernodeToOps)[fillSupernode])
        {
            if (!isCommitPhaseOp(graph.getOperation(opId)))
            {
                return fail("Expected memory fill supernode to contain only commit-phase sink ops");
            }
        }
        auto topoPosOf = [&](uint32_t supernodeId) -> std::size_t {
            for (std::size_t i = 0; i < topoOrder->size(); ++i)
            {
                if ((*topoOrder)[i] == supernodeId)
                {
                    return i;
                }
            }
            return topoOrder->size();
        };
        if (topoPosOf(fillSupernode) <= topoPosOf(readSupernode))
        {
            return fail("Expected memory fill sink supernode to be scheduled after the memory read supernode");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top6");
        design.markAsTop("top6");

        const auto clk = makeValue(graph, "clk_top6", 1, false);
        const auto en = makeValue(graph, "en_top6", 1, false);
        const auto mask = makeValue(graph, "mask_top6", 8, false);
        const auto a = makeValue(graph, "a_top6", 8, false);
        const auto b = makeValue(graph, "b_top6", 8, false);
        const auto c = makeValue(graph, "c_top6", 8, false);
        const auto d = makeValue(graph, "d_top6", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top6"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto v0 = makeValue(graph, "v0_top6", 8, false);
        const auto op0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                               graph.internSymbol("op0_top6"));
        graph.addOperand(op0, a);
        graph.addOperand(op0, b);
        graph.addResult(op0, v0);

        const auto v1 = makeValue(graph, "v1_top6", 8, false);
        const auto op1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                               graph.internSymbol("op1_top6"));
        graph.addOperand(op1, v0);
        graph.addOperand(op1, c);
        graph.addResult(op1, v1);

        const auto v2 = makeValue(graph, "v2_top6", 8, false);
        const auto op2 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                               graph.internSymbol("op2_top6"));
        graph.addOperand(op2, v1);
        graph.addOperand(op2, d);
        graph.addResult(op2, v2);

        const auto write = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                 graph.internSymbol("q_write_top6"));
        graph.addOperand(write, en);
        graph.addOperand(write, v2);
        graph.addOperand(write, mask);
        graph.addOperand(write, clk);
        graph.setAttr(write, "regSymbol", std::string("q_top6"));
        graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top6",
            .supernodeMaxSize = 3,
            .maxSinkSupernodeOp = 1,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected sink-fed compute chain activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top6.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected sink-fed chain session outputs");
        }
        if ((*opToSupernode)[op0.index - 1] != (*opToSupernode)[op1.index - 1] ||
            (*opToSupernode)[op1.index - 1] != (*opToSupernode)[op2.index - 1])
        {
            return fail("Expected sink-fed compute chain to coarsen into one supernode");
        }
        if ((*opToSupernode)[op2.index - 1] == (*opToSupernode)[write.index - 1])
        {
            return fail("Expected sink op to remain separate from the compute chain supernode");
        }
        if (supernodeSizeForOp(*supernodeToOps, *opToSupernode, op2) != 3)
        {
            return fail("Expected coarsen to keep the full three-op compute chain under the size limit");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top6b");
        design.markAsTop("top6b");

        const auto clk = makeValue(graph, "clk_top6b", 1, false);
        const auto en = makeValue(graph, "en_top6b", 1, false);
        const auto mask = makeValue(graph, "mask_top6b", 8, false);
        const auto a = makeValue(graph, "a_top6b", 8, false);
        const auto b = makeValue(graph, "b_top6b", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top6b"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read_top6b", 8, false);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                                   graph.internSymbol("q_read_op_top6b"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "regSymbol", std::string("q_top6b"));

        const auto sumValue = makeValue(graph, "sum_top6b", 8, false);
        const auto addOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                 graph.internSymbol("sum_add_top6b"));
        graph.addOperand(addOp, qReadValue);
        graph.addOperand(addOp, a);
        graph.addResult(addOp, sumValue);

        const auto xorValue = makeValue(graph, "xor_top6b", 8, false);
        const auto xorOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("xor_op_top6b"));
        graph.addOperand(xorOp, sumValue);
        graph.addOperand(xorOp, b);
        graph.addResult(xorOp, xorValue);
        graph.bindOutputPort("y", xorValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write_top6b"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, xorValue);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q_top6b"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top6b",
            .supernodeMaxSize = 8,
            .maxSinkSupernodeOp = 1,
            .enableCoarsen = true,
            .enableChainMerge = true,
            .enableSiblingMerge = true,
            .enableForwardMerge = true,
            .enableRefine = false,
            .enableStateReadTailAbsorb = false,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected pure-phase activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top6b.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected pure-phase session outputs");
        }
        if ((*opToSupernode)[writeOp.index - 1] == (*opToSupernode)[xorOp.index - 1])
        {
            return fail("Expected commit write to stay outside the compute chain supernode");
        }
        for (const auto &supernodeOps : *supernodeToOps)
        {
            bool hasCommitOp = false;
            bool hasComputeOp = false;
            for (const auto opId : supernodeOps)
            {
                const auto op = graph.getOperation(opId);
                if (isCommitPhaseOp(op))
                {
                    hasCommitOp = true;
                }
                else
                {
                    hasComputeOp = true;
                }
            }
            if (hasCommitOp && hasComputeOp)
            {
                return fail("Expected activity-schedule to emit pure compute/commit supernodes");
            }
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top7");
        design.markAsTop("top7");

        const auto clk = makeValue(graph, "clk_top7", 1, false);
        const auto en = makeValue(graph, "en_top7", 1, false);
        const auto mask = makeValue(graph, "mask_top7", 8, false);
        const auto a = makeValue(graph, "a_top7", 8, false);
        const auto b = makeValue(graph, "b_top7", 8, false);
        const auto c = makeValue(graph, "c_top7", 8, false);
        const auto d = makeValue(graph, "d_top7", 8, false);
        const auto e = makeValue(graph, "e_top7", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);
        graph.bindInputPort("e", e);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top7"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto shared = makeValue(graph, "shared_top7", 8, false);
        const auto sharedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("shared_top7_op"));
        graph.addOperand(sharedOp, a);
        graph.addOperand(sharedOp, b);
        graph.addResult(sharedOp, shared);

        const auto sinkData = makeValue(graph, "sink_data_top7", 8, false);
        const auto domOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("dom_top7_op"));
        graph.addOperand(domOp, shared);
        graph.addOperand(domOp, c);
        graph.addResult(domOp, sinkData);

        const auto sideValue = makeValue(graph, "side_top7", 8, false);
        const auto sideOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                  graph.internSymbol("side_top7_op"));
        graph.addOperand(sideOp, shared);
        graph.addOperand(sideOp, d);
        graph.addResult(sideOp, sideValue);

        const auto y = makeValue(graph, "y_top7", 8, false);
        const auto outOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                                 graph.internSymbol("out_top7_op"));
        graph.addOperand(outOp, sideValue);
        graph.addOperand(outOp, e);
        graph.addResult(outOp, y);
        graph.bindOutputPort("y", y);

        const auto write = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                 graph.internSymbol("q_write_top7"));
        graph.addOperand(write, en);
        graph.addOperand(write, sinkData);
        graph.addOperand(write, mask);
        graph.addOperand(write, clk);
        graph.setAttr(write, "regSymbol", std::string("q_top7"));
        graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top7",
            .supernodeMaxSize = 1,
            .maxSinkSupernodeOp = 1,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected shared-predecessor compute-chain activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top7.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected shared-predecessor session outputs");
        }
        if ((*opToSupernode)[sharedOp.index - 1] == (*opToSupernode)[domOp.index - 1])
        {
            return fail("Expected shared predecessor to stay outside the sink-fed compute chain supernode");
        }
        if (supernodeSizeForOp(*supernodeToOps, *opToSupernode, domOp) != 1)
        {
            return fail("Expected shared predecessor fanout to prevent premature coarsen growth");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top8");
        design.markAsTop("top8");

        const auto clk = makeValue(graph, "clk_top8", 1, false);
        const auto en = makeValue(graph, "en_top8", 1, false);
        const auto mask = makeValue(graph, "mask_top8", 8, false);
        const auto a = makeValue(graph, "a_top8", 8, false);
        const auto b = makeValue(graph, "b_top8", 8, false);
        const auto c = makeValue(graph, "c_top8", 8, false);
        const auto d = makeValue(graph, "d_top8", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top8"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto preValue = makeValue(graph, "pre_top8", 8, false);
        const auto preOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                 graph.internSymbol("pre_top8_op"));
        graph.addOperand(preOp, a);
        graph.addOperand(preOp, b);
        graph.addResult(preOp, preValue);

        const auto domValue = makeValue(graph, "dom_top8", 8, false);
        const auto domOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("dom_top8_op"));
        graph.addOperand(domOp, preValue);
        graph.addOperand(domOp, c);
        graph.addResult(domOp, domValue);

        const auto tailValue = makeValue(graph, "tail_top8", 8, false);
        const auto tailOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                  graph.internSymbol("tail_top8_op"));
        graph.addOperand(tailOp, domValue);
        graph.addOperand(tailOp, d);
        graph.addResult(tailOp, tailValue);
        graph.bindOutputPort("y", tailValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write_top8"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, domValue);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q_top8"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top8",
            .supernodeMaxSize = 2,
            .maxSinkSupernodeOp = 1,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected mixed sink/output users to avoid quotient cycles under coarsen");
        }

        const std::string keyPrefix = "top8.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        const auto *topoOrder =
            getSessionValue<ActivityScheduleTopoOrder>(session, keyPrefix + "topo_order");
        if (supernodeToOps == nullptr || opToSupernode == nullptr || topoOrder == nullptr)
        {
            return fail("Expected cycle-regression session outputs");
        }
        if (topoOrder->size() != supernodeToOps->size())
        {
            return fail("Expected final topo order to cover every cycle-regression supernode");
        }
        if ((*opToSupernode)[preOp.index - 1] == kInvalidActivitySupernodeId ||
            (*opToSupernode)[domOp.index - 1] == kInvalidActivitySupernodeId ||
            (*opToSupernode)[tailOp.index - 1] == kInvalidActivitySupernodeId ||
            (*opToSupernode)[writeOp.index - 1] == kInvalidActivitySupernodeId)
        {
            return fail("Expected cycle-regression ops to map to valid supernodes");
        }
        if ((*opToSupernode)[preOp.index - 1] != (*opToSupernode)[domOp.index - 1])
        {
            return fail("Expected shared sink/output predecessor seed to absorb its exclusive predecessor");
        }
        if ((*opToSupernode)[domOp.index - 1] == (*opToSupernode)[tailOp.index - 1] ||
            (*opToSupernode)[domOp.index - 1] == (*opToSupernode)[writeOp.index - 1])
        {
            return fail("Expected shared sink/output seed to remain separate from both consumers");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top9");
        design.markAsTop("top9");

        const auto clk = makeValue(graph, "clk_top9", 1, false);
        const auto en = makeValue(graph, "en_top9", 1, false);
        const auto mask = makeValue(graph, "mask_top9", 8, false);
        const auto a = makeValue(graph, "a_top9", 8, false);
        const auto b = makeValue(graph, "b_top9", 8, false);
        const auto c = makeValue(graph, "c_top9", 8, false);
        const auto d = makeValue(graph, "d_top9", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top9"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto preValue = makeValue(graph, "pre_top9", 8, false);
        const auto preOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                 graph.internSymbol("pre_top9_op"));
        graph.addOperand(preOp, a);
        graph.addOperand(preOp, b);
        graph.addResult(preOp, preValue);

        const auto seedValue = makeValue(graph, "seed_top9", 8, false);
        const auto seedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                  graph.internSymbol("seed_top9_op"));
        graph.addOperand(seedOp, preValue);
        graph.addOperand(seedOp, c);
        graph.addResult(seedOp, seedValue);

        const auto residualValue = makeValue(graph, "residual_top9", 8, false);
        const auto residualOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                      graph.internSymbol("residual_top9_op"));
        graph.addOperand(residualOp, seedValue);
        graph.addOperand(residualOp, d);
        graph.addResult(residualOp, residualValue);
        graph.bindOutputPort("y", residualValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write_top9"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, seedValue);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q_top9"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top9",
            .supernodeMaxSize = 2,
            .maxSinkSupernodeOp = 1,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected mixed sink/output users to preserve a coarsenable shared seed");
        }

        const std::string keyPrefix = "top9.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected mixed-user coarsen session outputs");
        }
        if ((*opToSupernode)[preOp.index - 1] != (*opToSupernode)[seedOp.index - 1])
        {
            return fail("Expected shared sink/output seed to absorb its exclusive predecessor during coarsen");
        }
        if ((*opToSupernode)[seedOp.index - 1] == (*opToSupernode)[residualOp.index - 1])
        {
            return fail("Expected shared sink/output seed to remain separate from the output tail consumer");
        }
        if ((*opToSupernode)[seedOp.index - 1] == (*opToSupernode)[writeOp.index - 1])
        {
            return fail("Expected sink op to remain separate from shared sink/output seed");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top10");
        design.markAsTop("top10");

        const auto clk = makeValue(graph, "clk_top10", 1, false);
        const auto en = makeValue(graph, "en_top10", 1, false);
        const auto mask = makeValue(graph, "mask_top10", 8, false);
        const auto a = makeValue(graph, "a_top10", 8, false);
        const auto b = makeValue(graph, "b_top10", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top10"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto dpiOut = makeValue(graph, "dpi_out_top10", 8, false);
        const auto dpiOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kDpicCall,
                                                 graph.internSymbol("dpi_top10"));
        graph.addOperand(dpiOp, en);
        graph.addOperand(dpiOp, a);
        graph.setAttr(dpiOp, "hasReturn", false);
        graph.addResult(dpiOp, dpiOut);

        const auto assignValue = makeValue(graph, "assign_top10", 8, false);
        const auto assignOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAssign,
                                                    graph.internSymbol("assign_top10_op"));
        graph.addOperand(assignOp, dpiOut);
        graph.addResult(assignOp, assignValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write_top10"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, assignValue);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q_top10"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        const auto residualValue = makeValue(graph, "residual_top10", 8, false);
        const auto residualOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                      graph.internSymbol("residual_top10_op"));
        graph.addOperand(residualOp, dpiOut);
        graph.addOperand(residualOp, b);
        graph.addResult(residualOp, residualValue);
        graph.bindOutputPort("y", residualValue);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top10",
            .supernodeMaxSize = 1,
            .maxSinkSupernodeOp = 8,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected no-return dpi with output result to stay out of sink partition");
        }

        const std::string keyPrefix = "top10.activity_schedule.";
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (opToSupernode == nullptr)
        {
            return fail("Expected no-return dpi session outputs");
        }
        if ((*opToSupernode)[dpiOp.index - 1] == (*opToSupernode)[writeOp.index - 1])
        {
            return fail("Expected no-return dpi with result to avoid sink-supernode classification");
        }
        if ((*opToSupernode)[assignOp.index - 1] == (*opToSupernode)[writeOp.index - 1])
        {
            return fail("Expected downstream assign to avoid sink-supernode classification");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top10b");
        design.markAsTop("top10b");

        const auto clk = makeValue(graph, "clk_top10b", 1, false);
        const auto en = makeValue(graph, "en_top10b", 1, false);
        const auto mask = makeValue(graph, "mask_top10b", 8, false);
        const auto a = makeValue(graph, "a_top10b", 8, false);
        const auto b = makeValue(graph, "b_top10b", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top10b"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto sumValue = makeValue(graph, "sum_top10b", 8, false);
        const auto addOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                 graph.internSymbol("sum_top10b_op"));
        graph.addOperand(addOp, a);
        graph.addOperand(addOp, b);
        graph.addResult(addOp, sumValue);
        graph.bindOutputPort("y", sumValue);

        const auto sysTask = graph.createOperation(wolvrix::lib::grh::OperationKind::kSystemTask,
                                                   graph.internSymbol("display_top10b"));
        graph.addOperand(sysTask, en);
        graph.addOperand(sysTask, sumValue);
        graph.setAttr(sysTask, "hasReturn", false);

        const auto dpiCall = graph.createOperation(wolvrix::lib::grh::OperationKind::kDpicCall,
                                                   graph.internSymbol("dpi_void_top10b"));
        graph.addOperand(dpiCall, en);
        graph.addOperand(dpiCall, sumValue);
        graph.setAttr(dpiCall, "hasReturn", false);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write_top10b"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, sumValue);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q_top10b"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top10b",
            .supernodeMaxSize = 1,
            .maxSinkSupernodeOp = 8,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected SystemTask/DpiCall compute-phase classification to succeed");
        }

        const std::string keyPrefix = "top10b.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected SystemTask/DpiCall session outputs");
        }

        const uint32_t sysTaskSupernode = (*opToSupernode)[sysTask.index - 1];
        const uint32_t dpiCallSupernode = (*opToSupernode)[dpiCall.index - 1];
        const uint32_t writeSupernode = (*opToSupernode)[writeOp.index - 1];
        if (sysTaskSupernode == writeSupernode || dpiCallSupernode == writeSupernode)
        {
            return fail("Expected SystemTask/DpiCall to stay out of sink-supernode classification");
        }
        for (const auto opId : (*supernodeToOps)[sysTaskSupernode])
        {
            if (isCommitPhaseOp(graph.getOperation(opId)))
            {
                return fail("Expected SystemTask supernode to stay in compute phase");
            }
        }
        for (const auto opId : (*supernodeToOps)[dpiCallSupernode])
        {
            if (isCommitPhaseOp(graph.getOperation(opId)))
            {
                return fail("Expected DpiCall supernode to stay in compute phase");
            }
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top11");
        design.markAsTop("top11");

        const auto clk = makeValue(graph, "clk_top11", 1, false);
        const auto en = makeValue(graph, "en_top11", 1, false);
        const auto mask = makeValue(graph, "mask_top11", 8, false);
        const auto a = makeValue(graph, "a_top11", 8, false);
        const auto b = makeValue(graph, "b_top11", 8, false);
        const auto c = makeValue(graph, "c_top11", 8, false);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top11"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto sharedValue = makeValue(graph, "shared_top11", 8, false);
        const auto sharedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("shared_top11_op"));
        graph.addOperand(sharedOp, a);
        graph.addOperand(sharedOp, b);
        graph.addResult(sharedOp, sharedValue);

        const auto midValue = makeValue(graph, "mid_top11", 8, false);
        const auto midOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("mid_top11_op"));
        graph.addOperand(midOp, sharedValue);
        graph.addOperand(midOp, c);
        graph.addResult(midOp, midValue);

        const auto seedValue = makeValue(graph, "seed_top11", 8, false);
        const auto seedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                  graph.internSymbol("seed_top11_op"));
        graph.addOperand(seedOp, midValue);
        graph.addOperand(seedOp, sharedValue);
        graph.addResult(seedOp, seedValue);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                   graph.internSymbol("q_write_top11"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, seedValue);
        graph.addOperand(writeOp, mask);
        graph.addOperand(writeOp, clk);
        graph.setAttr(writeOp, "regSymbol", std::string("q_top11"));
        graph.setAttr(writeOp, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top11",
            .supernodeMaxSize = 3,
            .maxSinkSupernodeOp = 1,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected shared predecessor to merge once coarsen collapses the downstream chain");
        }

        const std::string keyPrefix = "top11.activity_schedule.";
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (opToSupernode == nullptr)
        {
            return fail("Expected shared-predecessor coarsen session outputs");
        }
        if ((*opToSupernode)[midOp.index - 1] != (*opToSupernode)[seedOp.index - 1])
        {
            return fail("Expected single-user predecessor to join the coarsened downstream cluster");
        }
        if ((*opToSupernode)[sharedOp.index - 1] != (*opToSupernode)[seedOp.index - 1])
        {
            return fail("Expected predecessor shared only inside one coarsened cluster to be absorbed");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top12");
        design.markAsTop("top12");

        const auto a = makeValue(graph, "a_top12", 8, false);
        const auto b = makeValue(graph, "b_top12", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top12"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read_top12", 8, false);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                                   graph.internSymbol("q_read_top12_op"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "regSymbol", std::string("q_top12"));

        const auto y0 = makeValue(graph, "y0_top12", 8, false);
        const auto use0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                graph.internSymbol("use0_top12"));
        graph.addOperand(use0, qReadValue);
        graph.addOperand(use0, a);
        graph.addResult(use0, y0);
        graph.bindOutputPort("y0", y0);

        const auto y1 = makeValue(graph, "y1_top12", 8, false);
        const auto use1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("use1_top12"));
        graph.addOperand(use1, qReadValue);
        graph.addOperand(use1, b);
        graph.addResult(use1, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top12",
            .supernodeMaxSize = 1,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableStateReadTailAbsorb = true,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected register read tail absorb activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top12.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        const auto *valueFanout =
            getSessionValue<ActivityScheduleValueFanout>(session, keyPrefix + "value_fanout");
        const auto *stateReadSupernodes =
            getSessionValue<ActivityScheduleStateReadSupernodes>(session, keyPrefix + "state_read_supernodes");
        if (supernodeToOps == nullptr || opToSupernode == nullptr || valueFanout == nullptr ||
            stateReadSupernodes == nullptr)
        {
            return fail("Expected register read tail absorb session outputs");
        }

        const auto readSym = graph.lookupSymbol("q_read_top12_op");
        if (!readSym.valid())
        {
            return fail("Expected original register read symbol to exist");
        }
        if (graph.findOperation(readSym).valid())
        {
            return fail("Expected original shared register read op to be removed after tail absorb");
        }

        std::size_t readCount = 0;
        for (const auto opId : graph.operations())
        {
            if (graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kRegisterReadPort)
            {
                ++readCount;
            }
        }
        if (readCount != 2)
        {
            return fail("Expected shared register read to be cloned into two consumer-local reads");
        }
        if (supernodeToOps->size() != 2)
        {
            return fail("Expected register read absorb to collapse the empty source supernode");
        }
        for (const auto &ops : *supernodeToOps)
        {
            if (ops.size() != 2)
            {
                return fail("Expected each absorbed register-read consumer supernode to contain clone plus consumer");
            }
        }
        const auto use0Final = graph.findOperation("use0_top12");
        const auto use1Final = graph.findOperation("use1_top12");
        if (!use0Final.valid() || !use1Final.valid())
        {
            return fail("Expected absorbed register-read consumer symbols to remain resolvable");
        }
        const uint32_t use0Supernode = (*opToSupernode)[use0Final.index - 1];
        const uint32_t use1Supernode = (*opToSupernode)[use1Final.index - 1];
        if (use0Supernode == use1Supernode)
        {
            return fail("Expected absorbed register-read consumers to remain in separate supernodes");
        }
        const auto readersIt = stateReadSupernodes->find("q_top12");
        if (readersIt == stateReadSupernodes->end() || readersIt->second.size() != 2)
        {
            return fail("Expected register state readers to map directly to both consumer supernodes");
        }
        if (std::find(readersIt->second.begin(), readersIt->second.end(), use0Supernode) == readersIt->second.end() ||
            std::find(readersIt->second.begin(), readersIt->second.end(), use1Supernode) == readersIt->second.end())
        {
            return fail("Expected register state readers to target both absorbed consumer supernodes");
        }
        for (const auto &succs : *valueFanout)
        {
            if (!succs.empty())
            {
                return fail("Expected no residual cross-supernode value fanout after register read absorb");
            }
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top13");
        design.markAsTop("top13");

        const auto a = makeValue(graph, "a_top13", 8, false);
        const auto b = makeValue(graph, "b_top13", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto latchDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kLatch,
                                                     graph.internSymbol("q_top13"));
        graph.setAttr(latchDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(latchDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read_top13", 8, false);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kLatchReadPort,
                                                   graph.internSymbol("q_read_top13_op"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "latchSymbol", std::string("q_top13"));

        const auto y0 = makeValue(graph, "y0_top13", 8, false);
        const auto use0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                graph.internSymbol("use0_top13"));
        graph.addOperand(use0, qReadValue);
        graph.addOperand(use0, a);
        graph.addResult(use0, y0);
        graph.bindOutputPort("y0", y0);

        const auto y1 = makeValue(graph, "y1_top13", 8, false);
        const auto use1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("use1_top13"));
        graph.addOperand(use1, qReadValue);
        graph.addOperand(use1, b);
        graph.addResult(use1, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top13",
            .supernodeMaxSize = 1,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableStateReadTailAbsorb = true,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected latch read tail absorb activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top13.activity_schedule.";
        const auto *stateReadSupernodes =
            getSessionValue<ActivityScheduleStateReadSupernodes>(session, keyPrefix + "state_read_supernodes");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (stateReadSupernodes == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected latch read tail absorb session outputs");
        }

        const auto readSym = graph.lookupSymbol("q_read_top13_op");
        if (!readSym.valid())
        {
            return fail("Expected original latch read symbol to exist");
        }
        if (graph.findOperation(readSym).valid())
        {
            return fail("Expected original shared latch read op to be removed after tail absorb");
        }

        std::size_t readCount = 0;
        for (const auto opId : graph.operations())
        {
            if (graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kLatchReadPort)
            {
                ++readCount;
            }
        }
        if (readCount != 2)
        {
            return fail("Expected shared latch read to be cloned into two consumer-local reads");
        }

        const auto use0Final = graph.findOperation("use0_top13");
        const auto use1Final = graph.findOperation("use1_top13");
        if (!use0Final.valid() || !use1Final.valid())
        {
            return fail("Expected absorbed latch-read consumer symbols to remain resolvable");
        }
        const uint32_t use0Supernode = (*opToSupernode)[use0Final.index - 1];
        const uint32_t use1Supernode = (*opToSupernode)[use1Final.index - 1];
        const auto readersIt = stateReadSupernodes->find("q_top13");
        if (readersIt == stateReadSupernodes->end() || readersIt->second.size() != 2)
        {
            return fail("Expected latch state readers to map directly to both consumer supernodes");
        }
        if (std::find(readersIt->second.begin(), readersIt->second.end(), use0Supernode) == readersIt->second.end() ||
            std::find(readersIt->second.begin(), readersIt->second.end(), use1Supernode) == readersIt->second.end())
        {
            return fail("Expected latch state readers to target both absorbed consumer supernodes");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top14");
        design.markAsTop("top14");

        const auto a = makeValue(graph, "a_top14", 8, false);
        const auto b = makeValue(graph, "b_top14", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top14"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read_top14", 8, false);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                                   graph.internSymbol("q_read_top14_op"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "regSymbol", std::string("q_top14"));
        graph.bindOutputPort("y_raw", qReadValue);

        const auto y0 = makeValue(graph, "y0_top14", 8, false);
        const auto use0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                graph.internSymbol("use0_top14"));
        graph.addOperand(use0, qReadValue);
        graph.addOperand(use0, a);
        graph.addResult(use0, y0);
        graph.bindOutputPort("y0", y0);

        const auto y1 = makeValue(graph, "y1_top14", 8, false);
        const auto use1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("use1_top14"));
        graph.addOperand(use1, qReadValue);
        graph.addOperand(use1, b);
        graph.addResult(use1, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top14",
            .supernodeMaxSize = 1,
            .enableCoarsen = false,
            .enableRefine = false,
            .enableStateReadTailAbsorb = true,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected observable register read tail absorb activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top14.activity_schedule.";
        const auto *stateReadSupernodes =
            getSessionValue<ActivityScheduleStateReadSupernodes>(session, keyPrefix + "state_read_supernodes");
        if (stateReadSupernodes == nullptr)
        {
            return fail("Expected observable register read session outputs");
        }

        const auto readSym = graph.lookupSymbol("q_read_top14_op");
        if (!readSym.valid() || !graph.findOperation(readSym).valid())
        {
            return fail("Expected original observable register read op to remain after tail absorb");
        }

        std::size_t readCount = 0;
        for (const auto opId : graph.operations())
        {
            if (graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kRegisterReadPort)
            {
                ++readCount;
            }
        }
        if (readCount != 3)
        {
            return fail("Expected observable register read to keep original plus two consumer-local clones");
        }

        const auto readersIt = stateReadSupernodes->find("q_top14");
        if (readersIt == stateReadSupernodes->end() || readersIt->second.size() != 3)
        {
            return fail("Expected observable register read to keep original reader plus both absorbed consumers");
        }
    }

    {
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top15");
        design.markAsTop("top15");

        const auto a = makeValue(graph, "a_top15", 8, false);
        const auto b = makeValue(graph, "b_top15", 8, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q_top15"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read_top15", 8, false);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                                   graph.internSymbol("q_read_top15_op"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "regSymbol", std::string("q_top15"));

        const auto y0 = makeValue(graph, "y0_top15", 8, false);
        const auto use0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                graph.internSymbol("use0_top15"));
        graph.addOperand(use0, qReadValue);
        graph.addOperand(use0, a);
        graph.addResult(use0, y0);
        graph.bindOutputPort("y0", y0);

        const auto y1 = makeValue(graph, "y1_top15", 8, false);
        const auto use1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("use1_top15"));
        graph.addOperand(use1, qReadValue);
        graph.addOperand(use1, b);
        graph.addResult(use1, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top15",
            .supernodeMaxSize = 8,
            .enableCoarsen = true,
            .enableRefine = false,
            .enableStateReadTailAbsorb = true,
            .enableReplication = false,
        }));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected coarse-grain register read tail absorb activity-schedule pass to succeed");
        }

        const std::string keyPrefix = "top15.activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *stateReadSupernodes =
            getSessionValue<ActivityScheduleStateReadSupernodes>(session, keyPrefix + "state_read_supernodes");
        const auto *valueFanout =
            getSessionValue<ActivityScheduleValueFanout>(session, keyPrefix + "value_fanout");
        if (supernodeToOps == nullptr || stateReadSupernodes == nullptr || valueFanout == nullptr)
        {
            return fail("Expected coarse-grain register read tail absorb session outputs");
        }

        const auto readSym = graph.lookupSymbol("q_read_top15_op");
        if (!readSym.valid() || !graph.findOperation(readSym).valid())
        {
            return fail("Expected coarse-grain absorb to keep a single original register read op");
        }

        std::size_t readCount = 0;
        for (const auto opId : graph.operations())
        {
            if (graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kRegisterReadPort)
            {
                ++readCount;
            }
        }
        if (readCount != 1)
        {
            return fail("Expected coarse-grain absorb target partition to avoid over-cloning register reads");
        }

        if (supernodeToOps->size() != 1 || supernodeToOps->front().size() != 3)
        {
            return fail("Expected coarse-grain absorb to keep one merged supernode containing read plus both consumers");
        }

        const auto readersIt = stateReadSupernodes->find("q_top15");
        if (readersIt == stateReadSupernodes->end() || readersIt->second.size() != 1)
        {
            return fail("Expected coarse-grain absorb to keep one direct state-read supernode");
        }

        for (const auto &succs : *valueFanout)
        {
            if (!succs.empty())
            {
                return fail("Expected no residual cross-supernode value fanout after coarse-grain absorb");
            }
        }
    }

    return 0;
}
