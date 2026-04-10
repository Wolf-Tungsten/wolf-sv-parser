#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/activity_schedule.hpp"

#include <iostream>
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
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{.path = "top"}));

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

        if (supernodeToOps->size() != 3 || topoOrder->size() != 3)
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
        if (writeSupernode == addSupernode || writeSupernode == maskSupernode)
        {
            return fail("Register write supernode should stay isolated");
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
        if ((*valueFanout)[sumValue.index - 1].empty() ||
            std::find((*valueFanout)[sumValue.index - 1].begin(), (*valueFanout)[sumValue.index - 1].end(), writeSupernode) ==
                (*valueFanout)[sumValue.index - 1].end())
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
            ActivityScheduleOptions{.path = "top2", .supernodeMaxSize = 2, .enableReplication = false}));

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
        if (supernodeToOps->size() != 2)
        {
            return fail("Expected DP partition to produce two supernodes under max-size=2");
        }
        for (const auto &ops : *supernodeToOps)
        {
            if (ops.size() > 2)
            {
                return fail("DP partition violated supernode-max-size");
            }
        }
        if ((*opToSupernode)[op0.index - 1] != (*opToSupernode)[op1.index - 1])
        {
            return fail("Expected leading chain ops to share one supernode");
        }
        if ((*opToSupernode)[op2.index - 1] != (*opToSupernode)[op3.index - 1])
        {
            return fail("Expected trailing chain ops to share one supernode");
        }
        if ((*opToSupernode)[op1.index - 1] == (*opToSupernode)[op2.index - 1])
        {
            return fail("Expected cut between the two bounded supernodes");
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

    return 0;
}
