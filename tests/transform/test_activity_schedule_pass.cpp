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
        const auto *supernodes =
            getSessionValue<ActivityScheduleSupernodes>(session, keyPrefix + "supernodes");
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *supernodeToOpSymbols =
            getSessionValue<ActivityScheduleSupernodeToOpSymbols>(session, keyPrefix + "supernode_to_op_symbols");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        const auto *opSymbolToSupernode =
            getSessionValue<ActivityScheduleOpSymbolToSupernode>(session, keyPrefix + "op_symbol_to_supernode");
        const auto *dag =
            getSessionValue<ActivityScheduleDag>(session, keyPrefix + "dag");
        const auto *topoOrder =
            getSessionValue<ActivityScheduleTopoOrder>(session, keyPrefix + "topo_order");
        const auto *headEval =
            getSessionValue<ActivityScheduleHeadEvalSupernodes>(session, keyPrefix + "head_eval_supernodes");
        const auto *opEventDomains =
            getSessionValue<ActivityScheduleOpEventDomains>(session, keyPrefix + "op_event_domains");
        const auto *valueEventDomains =
            getSessionValue<ActivityScheduleValueEventDomains>(session, keyPrefix + "value_event_domains");
        const auto *supernodeEventDomains =
            getSessionValue<ActivityScheduleSupernodeEventDomains>(session, keyPrefix + "supernode_event_domains");
        const auto *eventDomainSinks =
            getSessionValue<ActivityScheduleEventDomainSinks>(session, keyPrefix + "event_domain_sinks");
        const auto *eventDomainSinkGroups =
            getSessionValue<ActivityScheduleEventDomainSinkGroups>(session, keyPrefix + "event_domain_sink_groups");

        if (supernodes == nullptr || supernodeToOps == nullptr || supernodeToOpSymbols == nullptr ||
            opToSupernode == nullptr || opSymbolToSupernode == nullptr || dag == nullptr ||
            topoOrder == nullptr || headEval == nullptr || opEventDomains == nullptr ||
            valueEventDomains == nullptr || supernodeEventDomains == nullptr ||
            eventDomainSinks == nullptr || eventDomainSinkGroups == nullptr)
        {
            return fail("Expected all activity-schedule session outputs to exist");
        }

        if (supernodes->size() != 3 || supernodeToOps->size() != 3 || supernodeToOpSymbols->size() != 3 ||
            dag->size() != 3 || topoOrder->size() != 3 || supernodeEventDomains->size() != 3)
        {
            return fail("Unexpected activity-schedule supernode shape after coarsen/partition");
        }

        for (std::size_t i = 0; i < supernodes->size(); ++i)
        {
            if ((*supernodes)[i] != i)
            {
                return fail("Expected dense supernode ids");
            }
            if ((*supernodeToOps)[i].empty() || (*supernodeToOpSymbols)[i].empty())
            {
                return fail("Expected non-empty supernodes");
            }
            if ((*supernodeToOps)[i].size() != (*supernodeToOpSymbols)[i].size())
            {
                return fail("Expected op/op-symbol cardinality match per supernode");
            }
        }

        if (eventDomainSinks->size() != 2)
        {
            return fail("Expected exactly two sink entries");
        }

        bool foundOutputSink = false;
        bool foundWriteSink = false;
        for (const auto &sink : *eventDomainSinks)
        {
            if (sink.sinkOp == addOp)
            {
                if (!sink.signature.empty())
                {
                    return fail("Output sink should have empty event-domain");
                }
                foundOutputSink = true;
            }
            if (sink.sinkOp == writeOp)
            {
                if (sink.signature.terms.size() != 1 ||
                    sink.signature.terms[0].value != clk ||
                    sink.signature.terms[0].edge != "posedge")
                {
                    return fail("Register write sink should carry clk posedge event-domain");
                }
                foundWriteSink = true;
            }
        }
        if (!foundOutputSink || !foundWriteSink)
        {
            return fail("Missing expected sink entries");
        }
        if (eventDomainSinkGroups->size() != 2)
        {
            return fail("Expected two grouped event-domain sink entries");
        }
        bool foundEmptyGroup = false;
        bool foundPosedgeGroup = false;
        for (const auto &group : *eventDomainSinkGroups)
        {
            if (group.signature.empty())
            {
                foundEmptyGroup = group.sinkOps.size() == 1 && group.sinkOps.front() == addOp;
                continue;
            }
            if (group.signature.terms.size() == 1 &&
                group.signature.terms[0].value == clk &&
                group.signature.terms[0].edge == "posedge")
            {
                foundPosedgeGroup = group.sinkOps.size() == 1 && group.sinkOps.front() == writeOp;
            }
        }
        if (!foundEmptyGroup || !foundPosedgeGroup)
        {
            return fail("Expected grouped event-domain sinks to match sink signatures");
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

        const auto addSym = graph.operationSymbol(addOp);
        const auto writeSym = graph.operationSymbol(writeOp);
        if (!addSym.valid() || !writeSym.valid())
        {
            return fail("Expected final op symbols to be valid");
        }
        auto addIt = opSymbolToSupernode->find(addSym);
        auto writeIt = opSymbolToSupernode->find(writeSym);
        if (addIt == opSymbolToSupernode->end() || writeIt == opSymbolToSupernode->end() ||
            addIt->second != addSupernode || writeIt->second != writeSupernode)
        {
            return fail("Expected op_symbol_to_supernode mapping to match op_to_supernode");
        }

        auto containsSupernode = [](const auto &nodes, uint32_t id) {
            return std::find(nodes.begin(), nodes.end(), id) != nodes.end();
        };
        if (!containsSupernode(*headEval, addSupernode) || !containsSupernode(*headEval, writeSupernode))
        {
            return fail("Expected add/write supernodes to be marked head-eval");
        }

        if ((*supernodeEventDomains)[writeSupernode].size() != 1 ||
            (*supernodeEventDomains)[writeSupernode][0].terms.size() != 1)
        {
            return fail("Write supernode should have one non-empty event-domain");
        }
        if ((*supernodeEventDomains)[maskSupernode].size() != 1)
        {
            return fail("Mask supernode should inherit the register-write event-domain");
        }
        if ((*supernodeEventDomains)[addSupernode].size() != 2)
        {
            return fail("Merged read/add supernode should carry both empty and non-empty event-domains");
        }

        const auto &addDomains = (*supernodeEventDomains)[addSupernode];
        bool hasEmpty = false;
        bool hasPosedge = false;
        for (const auto &domain : addDomains)
        {
            if (domain.empty())
            {
                hasEmpty = true;
                continue;
            }
            if (domain.terms.size() == 1 && domain.terms[0].value == clk && domain.terms[0].edge == "posedge")
            {
                hasPosedge = true;
            }
        }
        if (!hasEmpty || !hasPosedge)
        {
            return fail("Merged read/add supernode should contain both output and write sink domains");
        }
        if (addOp.index == 0 || writeOp.index == 0 || sumValue.index == 0 || clk.index == 0)
        {
            return fail("Expected valid op/value ids");
        }
        if ((*opEventDomains)[addOp.index - 1].size() != 2)
        {
            return fail("Add op should carry both output and write event-domains");
        }
        if ((*opEventDomains)[writeOp.index - 1].size() != 1 ||
            (*opEventDomains)[writeOp.index - 1][0].terms.size() != 1)
        {
            return fail("Write op should carry one non-empty event-domain");
        }
        if ((*valueEventDomains)[sumValue.index - 1].size() != 2)
        {
            return fail("Sum value should carry both output and write event-domains");
        }
        if ((*valueEventDomains)[clk.index - 1].size() != 1 ||
            (*valueEventDomains)[clk.index - 1][0].terms.size() != 1 ||
            (*valueEventDomains)[clk.index - 1][0].terms[0].value != clk)
        {
            return fail("Clock value should carry the write event-domain");
        }

        if ((*dag)[addSupernode].empty() ||
            std::find((*dag)[addSupernode].begin(), (*dag)[addSupernode].end(), writeSupernode) ==
                (*dag)[addSupernode].end())
        {
            return fail("Expected DAG edge from read/add supernode to write supernode");
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
        const auto *supernodes =
            getSessionValue<ActivityScheduleSupernodes>(session, keyPrefix + "supernodes");
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opToSupernode =
            getSessionValue<ActivityScheduleOpToSupernode>(session, keyPrefix + "op_to_supernode");
        if (supernodes == nullptr || supernodeToOps == nullptr || opToSupernode == nullptr)
        {
            return fail("Expected bounded partition session outputs");
        }
        if (supernodes->size() != 2 || supernodeToOps->size() != 2)
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
        const auto *supernodes =
            getSessionValue<ActivityScheduleSupernodes>(session, keyPrefix + "supernodes");
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(session, keyPrefix + "supernode_to_ops");
        const auto *opSymbolToSupernode =
            getSessionValue<ActivityScheduleOpSymbolToSupernode>(session, keyPrefix + "op_symbol_to_supernode");
        const auto *dag =
            getSessionValue<ActivityScheduleDag>(session, keyPrefix + "dag");
        if (supernodes == nullptr || supernodeToOps == nullptr || opSymbolToSupernode == nullptr || dag == nullptr)
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

        if (supernodes->size() != 2 || supernodeToOps->size() != 2)
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
        if (!dag->empty() && (!(*dag)[0].empty() || !(*dag)[1].empty()))
        {
            return fail("Expected no residual DAG edge after replication removes shared dependency");
        }

        const auto andSym = graph.lookupSymbol("use_and");
        const auto xorSym = graph.lookupSymbol("use_xor");
        auto andIt = opSymbolToSupernode->find(andSym);
        auto xorIt = opSymbolToSupernode->find(xorSym);
        if (andIt == opSymbolToSupernode->end() || xorIt == opSymbolToSupernode->end())
        {
            return fail("Expected consumer symbols to remain mapped to supernodes");
        }
        if (andIt->second == xorIt->second)
        {
            return fail("Expected replicated consumers to stay in separate supernodes");
        }
    }

    return 0;
}
