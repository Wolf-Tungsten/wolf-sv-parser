#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/activity_schedule.hpp"

#include <algorithm>
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
        return typed == nullptr ? nullptr : &typed->value;
    }

    wolvrix::lib::grh::ValueId makeValue(wolvrix::lib::grh::Graph &graph,
                                         const std::string &name,
                                         int32_t width,
                                         bool isSigned = false)
    {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
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

    struct ScheduleView
    {
        const ActivityScheduleSupernodeToOps *supernodeToOps = nullptr;
        const ActivityScheduleOpToSupernode *opToSupernode = nullptr;
        const ActivityScheduleDag *dag = nullptr;
        const ActivityScheduleValueFanout *valueFanout = nullptr;
        const ActivityScheduleTopoOrder *topoOrder = nullptr;
        const ActivityScheduleStateReadSupernodes *stateReadSupernodes = nullptr;
        const ActivityScheduleSupernodeKinds *supernodeKinds = nullptr;
        const std::string *summaryStats = nullptr;
    };

    ScheduleView loadSchedule(const SessionStore &session, const std::string &graphName)
    {
        const std::string prefix = graphName + ".activity_schedule.";
        return ScheduleView{
            getSessionValue<ActivityScheduleSupernodeToOps>(session, prefix + "supernode_to_ops"),
            getSessionValue<ActivityScheduleOpToSupernode>(session, prefix + "op_to_supernode"),
            getSessionValue<ActivityScheduleDag>(session, prefix + "dag"),
            getSessionValue<ActivityScheduleValueFanout>(session, prefix + "value_fanout"),
            getSessionValue<ActivityScheduleTopoOrder>(session, prefix + "topo_order"),
            getSessionValue<ActivityScheduleStateReadSupernodes>(session, prefix + "state_read_supernodes"),
            getSessionValue<ActivityScheduleSupernodeKinds>(session, prefix + "supernode_kind"),
            getSessionValue<std::string>(session, prefix + "summary_stats"),
        };
    }

    bool hasFanoutTo(const ActivityScheduleValueFanout &fanout,
                     wolvrix::lib::grh::ValueId value,
                     uint32_t supernode)
    {
        if (value.index == 0 || value.index > fanout.size())
        {
            return false;
        }
        const auto &succs = fanout[value.index - 1];
        return std::find(succs.begin(), succs.end(), supernode) != succs.end();
    }

    int validateCommonScheduleShape(const wolvrix::lib::grh::Graph &graph,
                                    const ScheduleView &schedule)
    {
        if (schedule.supernodeToOps == nullptr || schedule.opToSupernode == nullptr ||
            schedule.dag == nullptr || schedule.valueFanout == nullptr ||
            schedule.topoOrder == nullptr || schedule.stateReadSupernodes == nullptr ||
            schedule.supernodeKinds == nullptr || schedule.summaryStats == nullptr)
        {
            return fail("Expected all activity-schedule session outputs to exist");
        }
        if (schedule.supernodeToOps->size() != schedule.supernodeKinds->size() ||
            schedule.supernodeToOps->size() != schedule.topoOrder->size())
        {
            return fail("Expected supernode outputs to have matching sizes");
        }
        for (uint32_t supernodeId = 0; supernodeId < schedule.supernodeToOps->size(); ++supernodeId)
        {
            const bool commitKind =
                (*schedule.supernodeKinds)[supernodeId] == ActivityScheduleSupernodeKind::Commit;
            for (const auto opId : (*schedule.supernodeToOps)[supernodeId])
            {
                const bool commitOp = isCommitPhaseOp(graph.getOperation(opId));
                if (commitKind != commitOp)
                {
                    return fail("Expected explicit supernode_kind to match contained ops");
                }
            }
        }
        return 0;
    }

} // namespace

int main()
{
    std::string currentCase;
    try
    {
    {
        currentCase = "top";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("top");
        design.markAsTop("top");

        const auto clk = makeValue(graph, "clk", 1);
        const auto en = makeValue(graph, "en", 1);
        const auto a = makeValue(graph, "a", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("a", a);

        const auto regDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol("q"));
        graph.setAttr(regDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl, "isSigned", false);

        const auto qReadValue = makeValue(graph, "q_read", 8);
        const auto qReadOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                                   graph.internSymbol("q_read_op"));
        graph.addResult(qReadOp, qReadValue);
        graph.setAttr(qReadOp, "regSymbol", std::string("q"));

        const auto maskValue = makeValue(graph, "mask_all", 8);
        const auto maskOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant,
                                                  graph.internSymbol("mask_const"));
        graph.addResult(maskOp, maskValue);
        graph.setAttr(maskOp, "constValue", std::string("8'hFF"));

        const auto sumValue = makeValue(graph, "sum", 8);
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
            ActivityScheduleOptions{.path = "top", .maxOpInComputeSupernode = 4}));

        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected activity-schedule pass to succeed");
        }
        const auto schedule = loadSchedule(session, "top");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }

        const uint32_t addSupernode = (*schedule.opToSupernode)[addOp.index - 1];
        const uint32_t writeSupernode = (*schedule.opToSupernode)[writeOp.index - 1];
        if (addSupernode == kInvalidActivitySupernodeId || writeSupernode == kInvalidActivitySupernodeId ||
            addSupernode == writeSupernode)
        {
            return fail("Expected compute and commit ops to map to distinct supernodes");
        }
        if ((*schedule.opToSupernode)[regDecl.index - 1] != kInvalidActivitySupernodeId)
        {
            return fail("Expected declaration op to stay out of schedule");
        }
        if (!hasFanoutTo(*schedule.valueFanout, sumValue, writeSupernode))
        {
            return fail("Expected compute value fanout into commit supernode");
        }
        if (!hasFanoutTo(*schedule.valueFanout, maskValue, writeSupernode))
        {
            return fail("Expected direct source value dependency into commit supernode");
        }
        if (schedule.summaryStats->find("\"compute_commit_value_pairs\":2") == std::string::npos)
        {
            return fail("Expected summary_stats to report two compute->commit value pairs in top case");
        }
        if (schedule.summaryStats->find("\"compute_compute_value_pairs\":0") == std::string::npos)
        {
            return fail("Expected summary_stats to report zero compute->compute value pairs in top case");
        }
        if (schedule.summaryStats->find("\"state_read_activation_edges\":0") == std::string::npos)
        {
            return fail("Expected top case to avoid cross-supernode state-read propagation");
        }
        if (schedule.summaryStats->find("\"memory_read_activation_edges\":0") == std::string::npos)
        {
            return fail("Expected top case to report zero memory-read propagation");
        }
        if (schedule.summaryStats->find("\"constant_activation_edges\":1") == std::string::npos)
        {
            return fail("Expected top case to report one constant propagation edge");
        }
        if (schedule.summaryStats->find("\"other_compute_activation_edges\":1") == std::string::npos)
        {
            return fail("Expected top case to report one compute propagation edge");
        }
        const auto readersIt = schedule.stateReadSupernodes->find("q");
        if (readersIt == schedule.stateReadSupernodes->end() || readersIt->second.empty())
        {
            return fail("Expected register read state mapping to compute supernode");
        }
    }

    {
        currentCase = "essent_mffc_chain_and_shared";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_mffc_chain_and_shared");
        design.markAsTop("essent_mffc_chain_and_shared");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto shared = makeValue(graph, "shared", 8);
        const auto sharedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("shared_op"));
        graph.addOperand(sharedOp, a);
        graph.addOperand(sharedOp, b);
        graph.addResult(sharedOp, shared);

        const auto chain0 = makeValue(graph, "chain0", 8);
        const auto chain0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                    graph.internSymbol("chain0_op"));
        graph.addOperand(chain0Op, shared);
        graph.addOperand(chain0Op, c);
        graph.addResult(chain0Op, chain0);

        const auto y0 = makeValue(graph, "y0", 8);
        const auto chain1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                    graph.internSymbol("chain1_op"));
        graph.addOperand(chain1Op, chain0);
        graph.addOperand(chain1Op, d);
        graph.addResult(chain1Op, y0);
        graph.bindOutputPort("y0", y0);

        const auto y1 = makeValue(graph, "y1", 8);
        const auto siblingOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                                     graph.internSymbol("sibling_op"));
        graph.addOperand(siblingOp, shared);
        graph.addOperand(siblingOp, d);
        graph.addResult(siblingOp, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_mffc_chain_and_shared",
            .maxOpInComputeSupernode = 1,
            .maxOpInComputeNode = 16,
            .enableCoarsen = false,
            .enableEssentMffcBuild = true,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT MFFC schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_mffc_chain_and_shared");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }

        const uint32_t sharedSupernode = (*schedule.opToSupernode)[sharedOp.index - 1];
        const uint32_t chain0Supernode = (*schedule.opToSupernode)[chain0Op.index - 1];
        const uint32_t chain1Supernode = (*schedule.opToSupernode)[chain1Op.index - 1];
        const uint32_t siblingSupernode = (*schedule.opToSupernode)[siblingOp.index - 1];
        if (sharedSupernode == kInvalidActivitySupernodeId ||
            chain0Supernode == kInvalidActivitySupernodeId ||
            chain1Supernode == kInvalidActivitySupernodeId ||
            siblingSupernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected all compute ops to map to ESSENT MFFC supernodes");
        }
        if (chain0Supernode != chain1Supernode)
        {
            return fail("Expected single-use chain to be absorbed into one ESSENT MFFC node");
        }
        if (sharedSupernode == chain0Supernode || sharedSupernode == siblingSupernode)
        {
            return fail("Expected shared expression to remain a separate ESSENT MFFC root");
        }
        if (!hasFanoutTo(*schedule.valueFanout, shared, chain0Supernode) ||
            !hasFanoutTo(*schedule.valueFanout, shared, siblingSupernode))
        {
            return fail("Expected shared expression fanout to both ESSENT MFFC consumers");
        }
        if (hasFanoutTo(*schedule.valueFanout, chain0, chain1Supernode))
        {
            return fail("Expected absorbed single-use chain value to stay local");
        }
        if (schedule.summaryStats->find("\"initial_compute_supernodes\":3") == std::string::npos)
        {
            return fail("Expected ESSENT MFFC summary to report three initial compute supernodes");
        }
        if (schedule.summaryStats->find("\"initial_boundary_activation_edges\":2") == std::string::npos)
        {
            return fail("Expected ESSENT MFFC summary to report two initial boundary activation edges");
        }
    }

    {
        currentCase = "essent_coarsen_single_parent";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_coarsen_single_parent");
        design.markAsTop("essent_coarsen_single_parent");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto root = makeValue(graph, "root", 8);
        const auto rootOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                  graph.internSymbol("essent_root_op"));
        graph.addOperand(rootOp, a);
        graph.addOperand(rootOp, b);
        graph.addResult(rootOp, root);

        const auto child0 = makeValue(graph, "child0", 8);
        const auto child0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                    graph.internSymbol("essent_child0_op"));
        graph.addOperand(child0Op, root);
        graph.addOperand(child0Op, c);
        graph.addResult(child0Op, child0);
        graph.bindOutputPort("child0", child0);

        const auto child1 = makeValue(graph, "child1", 8);
        const auto child1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                    graph.internSymbol("essent_child1_op"));
        graph.addOperand(child1Op, root);
        graph.addOperand(child1Op, d);
        graph.addResult(child1Op, child1);
        graph.bindOutputPort("child1", child1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_coarsen_single_parent",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 8,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSmallSiblingMerge = false,
            .enableEssentSmallOverlapMerge = false,
            .enableEssentDownMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT single-parent coarsen schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_coarsen_single_parent");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }

        const uint32_t rootSupernode = (*schedule.opToSupernode)[rootOp.index - 1];
        const uint32_t child0Supernode = (*schedule.opToSupernode)[child0Op.index - 1];
        const uint32_t child1Supernode = (*schedule.opToSupernode)[child1Op.index - 1];
        if (rootSupernode == kInvalidActivitySupernodeId ||
            child0Supernode == kInvalidActivitySupernodeId ||
            child1Supernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected ESSENT single-parent coarsen ops to map to supernodes");
        }
        if (rootSupernode != child0Supernode || rootSupernode != child1Supernode)
        {
            return fail("Expected ESSENT single-parent coarsen to merge shared parent and children");
        }
        if (schedule.summaryStats->find("\"initial_compute_supernodes\":3") == std::string::npos)
        {
            return fail("Expected ESSENT single-parent summary to report three initial compute supernodes");
        }
        if (schedule.summaryStats->find("\"essent_single_parent_merges\":2") == std::string::npos)
        {
            return fail("Expected ESSENT single-parent summary to report two single-parent merges");
        }
        if (schedule.summaryStats->find("\"clusters_after_essent_coarsen\":1") == std::string::npos)
        {
            return fail("Expected ESSENT single-parent summary to report one final cluster");
        }
    }

    {
        currentCase = "essent_coarsen_small_siblings";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_coarsen_small_siblings");
        design.markAsTop("essent_coarsen_small_siblings");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto chainRoot = makeValue(graph, "chain_root", 8);
        const auto chainRootOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                       graph.internSymbol("essent_chain_root_op"));
        graph.addOperand(chainRootOp, a);
        graph.addOperand(chainRootOp, b);
        graph.addResult(chainRootOp, chainRoot);

        const auto chainChild = makeValue(graph, "chain_child", 8);
        const auto chainChildOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                        graph.internSymbol("essent_chain_child_op"));
        graph.addOperand(chainChildOp, chainRoot);
        graph.addOperand(chainChildOp, c);
        graph.addResult(chainChildOp, chainChild);
        graph.bindOutputPort("chain_child", chainChild);

        const auto parent0 = makeValue(graph, "parent0", 8);
        const auto parent0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                                     graph.internSymbol("essent_parent0_op"));
        graph.addOperand(parent0Op, a);
        graph.addOperand(parent0Op, c);
        graph.addResult(parent0Op, parent0);

        const auto parent1 = makeValue(graph, "parent1", 8);
        const auto parent1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                     graph.internSymbol("essent_parent1_op"));
        graph.addOperand(parent1Op, b);
        graph.addOperand(parent1Op, d);
        graph.addResult(parent1Op, parent1);

        const auto child0 = makeValue(graph, "child0", 8);
        const auto child0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                    graph.internSymbol("essent_child0_op"));
        graph.addOperand(child0Op, parent0);
        graph.addOperand(child0Op, parent1);
        graph.addResult(child0Op, child0);
        graph.bindOutputPort("child0", child0);

        const auto child1 = makeValue(graph, "child1", 8);
        const auto child1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("essent_child1_op"));
        graph.addOperand(child1Op, parent0);
        graph.addOperand(child1Op, parent1);
        graph.addResult(child1Op, child1);
        graph.bindOutputPort("child1", child1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_coarsen_small_siblings",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 8,
            .essentSmallSiblingMaxPreds = 8,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSingleParentMerge = false,
            .enableEssentSmallOverlapMerge = false,
            .enableEssentDownMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT small-sibling coarsen schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_coarsen_small_siblings");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }

        const uint32_t chainRootSupernode = (*schedule.opToSupernode)[chainRootOp.index - 1];
        const uint32_t chainChildSupernode = (*schedule.opToSupernode)[chainChildOp.index - 1];
        const uint32_t parent0Supernode = (*schedule.opToSupernode)[parent0Op.index - 1];
        const uint32_t parent1Supernode = (*schedule.opToSupernode)[parent1Op.index - 1];
        const uint32_t child0Supernode = (*schedule.opToSupernode)[child0Op.index - 1];
        const uint32_t child1Supernode = (*schedule.opToSupernode)[child1Op.index - 1];
        if (chainRootSupernode == kInvalidActivitySupernodeId ||
            chainChildSupernode == kInvalidActivitySupernodeId ||
            parent0Supernode == kInvalidActivitySupernodeId ||
            parent1Supernode == kInvalidActivitySupernodeId ||
            child0Supernode == kInvalidActivitySupernodeId ||
            child1Supernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected ESSENT coarsen ops to map to supernodes");
        }
        if (schedule.summaryStats->find("\"initial_compute_supernodes\":5") == std::string::npos)
        {
            return fail("Expected ESSENT small-sibling summary to report five initial compute supernodes");
        }
        if (schedule.summaryStats->find("\"essent_single_parent_merges\":0") == std::string::npos)
        {
            return fail("Expected ESSENT small-sibling summary to report zero single-parent merges");
        }
        if (schedule.summaryStats->find("\"essent_small_sibling_merges\":1") == std::string::npos)
        {
            return fail("Expected ESSENT small-sibling summary to report one small-sibling merge");
        }
        if (schedule.summaryStats->find("\"clusters_after_essent_coarsen\":4") == std::string::npos)
        {
            return fail("Expected ESSENT small-sibling summary to report four final clusters");
        }
    }

    {
        currentCase = "essent_coarsen_small_siblings_budgeted";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_coarsen_small_siblings_budgeted");
        design.markAsTop("essent_coarsen_small_siblings_budgeted");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto parent0 = makeValue(graph, "parent0", 8);
        const auto parent0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                                     graph.internSymbol("budget_parent0_op"));
        graph.addOperand(parent0Op, a);
        graph.addOperand(parent0Op, c);
        graph.addResult(parent0Op, parent0);

        const auto parent1 = makeValue(graph, "parent1", 8);
        const auto parent1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                     graph.internSymbol("budget_parent1_op"));
        graph.addOperand(parent1Op, b);
        graph.addOperand(parent1Op, d);
        graph.addResult(parent1Op, parent1);

        const auto child0 = makeValue(graph, "child0", 8);
        const auto child0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                    graph.internSymbol("budget_child0_op"));
        graph.addOperand(child0Op, parent0);
        graph.addOperand(child0Op, parent1);
        graph.addResult(child0Op, child0);
        graph.bindOutputPort("child0", child0);

        const auto child1 = makeValue(graph, "child1", 8);
        const auto child1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("budget_child1_op"));
        graph.addOperand(child1Op, parent0);
        graph.addOperand(child1Op, parent1);
        graph.addResult(child1Op, child1);
        graph.bindOutputPort("child1", child1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_coarsen_small_siblings_budgeted",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 8,
            .essentSmallSiblingMaxPreds = 8,
            .essentSmallSiblingCandidateBudget = 1,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSingleParentMerge = false,
            .enableEssentSmallOverlapMerge = false,
            .enableEssentDownMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected budgeted ESSENT small-sibling schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_coarsen_small_siblings_budgeted");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if (schedule.summaryStats->find("\"essent_small_sibling_merges\":0") == std::string::npos)
        {
            return fail("Expected budgeted small-sibling summary to report zero merges");
        }
    }

    {
        currentCase = "essent_coarsen_small_overlap";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_coarsen_small_overlap");
        design.markAsTop("essent_coarsen_small_overlap");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto p0 = makeValue(graph, "p0", 8);
        const auto p0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                graph.internSymbol("essent_overlap_p0_op"));
        graph.addOperand(p0Op, a);
        graph.addOperand(p0Op, b);
        graph.addResult(p0Op, p0);

        const auto p1 = makeValue(graph, "p1", 8);
        const auto p1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("essent_overlap_p1_op"));
        graph.addOperand(p1Op, a);
        graph.addOperand(p1Op, c);
        graph.addResult(p1Op, p1);

        const auto p2 = makeValue(graph, "p2", 8);
        const auto p2Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                                graph.internSymbol("essent_overlap_p2_op"));
        graph.addOperand(p2Op, b);
        graph.addOperand(p2Op, d);
        graph.addResult(p2Op, p2);

        const auto child0 = makeValue(graph, "child0", 8);
        const auto child0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                    graph.internSymbol("essent_overlap_child0_op"));
        graph.addOperand(child0Op, p0);
        graph.addOperand(child0Op, p1);
        graph.addResult(child0Op, child0);
        graph.bindOutputPort("child0", child0);

        const auto child1 = makeValue(graph, "child1", 8);
        const auto child1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("essent_overlap_child1_op"));
        graph.addOperand(child1Op, p0);
        graph.addOperand(child1Op, p2);
        graph.addResult(child1Op, child1);
        graph.bindOutputPort("child1", child1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_coarsen_small_overlap",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 8,
            .essentOverlapThreshold1 = 0.5,
            .essentOverlapThreshold2 = 0.0,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSingleParentMerge = false,
            .enableEssentSmallSiblingMerge = false,
            .enableEssentDownMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT small-overlap coarsen schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_coarsen_small_overlap");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if (schedule.summaryStats->find("\"essent_small_overlap_merges\":1") == std::string::npos)
        {
            return fail("Expected ESSENT small-overlap summary to report one merge");
        }
        if (schedule.summaryStats->find("\"clusters_after_essent_coarsen\":2") == std::string::npos)
        {
            return fail("Expected ESSENT small-overlap summary to report two final clusters");
        }
    }

    {
        currentCase = "essent_coarsen_down";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_coarsen_down");
        design.markAsTop("essent_coarsen_down");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto p0 = makeValue(graph, "p0", 8);
        const auto p0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                graph.internSymbol("essent_down_p0_op"));
        graph.addOperand(p0Op, a);
        graph.addOperand(p0Op, b);
        graph.addResult(p0Op, p0);

        const auto p1 = makeValue(graph, "p1", 8);
        const auto p1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("essent_down_p1_op"));
        graph.addOperand(p1Op, a);
        graph.addOperand(p1Op, c);
        graph.addResult(p1Op, p1);

        const auto child = makeValue(graph, "child", 8);
        const auto childOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                   graph.internSymbol("essent_down_child_op"));
        graph.addOperand(childOp, p0);
        graph.addOperand(childOp, p1);
        graph.addResult(childOp, child);
        graph.bindOutputPort("child", child);

        const auto keep0 = makeValue(graph, "keep0", 8);
        const auto keep0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kOr,
                                                   graph.internSymbol("essent_down_keep0_op"));
        graph.addOperand(keep0Op, p0);
        graph.addOperand(keep0Op, d);
        graph.addResult(keep0Op, keep0);
        graph.bindOutputPort("keep0", keep0);

        const auto keep1 = makeValue(graph, "keep1", 8);
        const auto keep1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                   graph.internSymbol("essent_down_keep1_op"));
        graph.addOperand(keep1Op, p1);
        graph.addOperand(keep1Op, d);
        graph.addResult(keep1Op, keep1);
        graph.bindOutputPort("keep1", keep1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_coarsen_down",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 8,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSingleParentMerge = false,
            .enableEssentSmallSiblingMerge = false,
            .enableEssentSmallOverlapMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT down coarsen schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_coarsen_down");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if (schedule.summaryStats->find("\"essent_down_merges\":") == std::string::npos ||
            schedule.summaryStats->find("\"essent_down_merges\":0") != std::string::npos)
        {
            return fail("Expected ESSENT down summary to report at least one merge");
        }
    }

    {
        currentCase = "essent_coarsen_small_overlap_large_sibling";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_coarsen_small_overlap_large_sibling");
        design.markAsTop("essent_coarsen_small_overlap_large_sibling");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        const auto e = makeValue(graph, "e", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);
        graph.bindInputPort("e", e);

        const auto p0 = makeValue(graph, "large_overlap_p0", 8);
        const auto p0Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                graph.internSymbol("large_overlap_p0_op"));
        graph.addOperand(p0Op, a);
        graph.addOperand(p0Op, b);
        graph.addResult(p0Op, p0);

        const auto p1 = makeValue(graph, "large_overlap_p1", 8);
        const auto p1Op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                graph.internSymbol("large_overlap_p1_op"));
        graph.addOperand(p1Op, a);
        graph.addOperand(p1Op, c);
        graph.addResult(p1Op, p1);

        auto cursor = p1;
        for (int i = 0; i < 3; ++i)
        {
            const auto next = makeValue(graph, "large_overlap_sibling_" + std::to_string(i), 8);
            const auto op = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                  graph.internSymbol("large_overlap_sibling_op_" + std::to_string(i)));
            graph.addOperand(op, cursor);
            graph.addOperand(op, i == 0 ? d : e);
            graph.addResult(op, next);
            cursor = next;
        }
        graph.bindOutputPort("large_sibling", cursor);

        const auto smallChild = makeValue(graph, "large_overlap_small_child", 8);
        const auto smallChildOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                       graph.internSymbol("large_overlap_small_child_op"));
        graph.addOperand(smallChildOp, p0);
        graph.addOperand(smallChildOp, p1);
        graph.addResult(smallChildOp, smallChild);
        graph.bindOutputPort("small_child", smallChild);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_coarsen_small_overlap_large_sibling",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 4,
            .essentOverlapThreshold1 = 0.5,
            .essentOverlapThreshold2 = 0.0,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSingleParentMerge = false,
            .enableEssentSmallSiblingMerge = false,
            .enableEssentDownMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT small-overlap with large sibling to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_coarsen_small_overlap_large_sibling");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if (schedule.summaryStats->find("\"essent_small_overlap_merges\":1") == std::string::npos)
        {
            return fail("Expected small-overlap to merge a non-small sibling candidate: " +
                        *schedule.summaryStats);
        }
    }

    {
        currentCase = "essent_cycle_guard_bounded_reject";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_cycle_guard_bounded_reject");
        design.markAsTop("essent_cycle_guard_bounded_reject");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);

        const auto root = makeValue(graph, "root", 8);
        const auto rootOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                  graph.internSymbol("essent_guard_root_op"));
        graph.addOperand(rootOp, a);
        graph.addOperand(rootOp, b);
        graph.addResult(rootOp, root);

        const auto child = makeValue(graph, "child", 8);
        const auto childOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                   graph.internSymbol("essent_guard_child_op"));
        graph.addOperand(childOp, root);
        graph.addOperand(childOp, c);
        graph.addResult(childOp, child);
        graph.bindOutputPort("child", child);

        const auto sibling = makeValue(graph, "sibling", 8);
        const auto siblingOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                     graph.internSymbol("essent_guard_sibling_op"));
        graph.addOperand(siblingOp, root);
        graph.addOperand(siblingOp, d);
        graph.addResult(siblingOp, sibling);
        graph.bindOutputPort("sibling", sibling);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_cycle_guard_bounded_reject",
            .maxOpInComputeSupernode = 8,
            .maxOpInComputeNode = 16,
            .essentSmallPartCutoff = 8,
            .essentCycleGuardMaxVisits = 0,
            .enableCoarsen = true,
            .enableEssentMffcBuild = true,
            .enableEssentCoarsen = true,
            .enableEssentSmallSiblingMerge = false,
            .enableEssentSmallOverlapMerge = false,
            .enableEssentDownMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected ESSENT cycle guard schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_cycle_guard_bounded_reject");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if (schedule.summaryStats->find("\"essent_single_parent_merges\":0") == std::string::npos)
        {
            return fail("Expected ESSENT guard summary to report zero single-parent merges: " +
                        *schedule.summaryStats);
        }
        if (schedule.summaryStats->find("\"essent_merge_rejected_bounded\":") == std::string::npos ||
            schedule.summaryStats->find("\"essent_merge_rejected_bounded\":0") != std::string::npos)
        {
            return fail("Expected ESSENT guard summary to report bounded rejects: " +
                        *schedule.summaryStats);
        }
    }

    {
        currentCase = "source_compute";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("source_compute");
        design.markAsTop("source_compute");

        const auto a = makeValue(graph, "a", 8);
        graph.bindInputPort("a", a);
        const auto c = makeValue(graph, "c", 8);
        const auto cOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kConstant,
                                               graph.internSymbol("const_source"));
        graph.addResult(cOp, c);
        graph.setAttr(cOp, "constValue", std::string("8'h01"));
        const auto y = makeValue(graph, "y", 8);
        const auto addOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                 graph.internSymbol("add"));
        graph.addOperand(addOp, a);
        graph.addOperand(addOp, c);
        graph.addResult(addOp, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "source_compute",
            .maxOpInComputeSupernode = 1,
            .enableCoarsen = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected source-to-compute schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "source_compute");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        const uint32_t addSupernode = (*schedule.opToSupernode)[addOp.index - 1];
        if (addSupernode == kInvalidActivitySupernodeId)
        {
            return fail("Expected compute op to map to a supernode");
        }
        bool foundLocalConstClone = false;
        wolvrix::lib::grh::OperationId clonedConstOp = wolvrix::lib::grh::OperationId::invalid();
        for (const auto opId : (*schedule.supernodeToOps)[addSupernode])
        {
            if (opId != cOp && graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kConstant)
            {
                foundLocalConstClone = true;
                clonedConstOp = opId;
            }
        }
        if (!foundLocalConstClone)
        {
            for (const auto opId : graph.operations())
            {
                if (opId != cOp && graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kConstant)
                {
                    const auto supernode = (*schedule.opToSupernode)[opId.index - 1];
                    if (supernode != kInvalidActivitySupernodeId)
                    {
                        foundLocalConstClone = true;
                        clonedConstOp = opId;
                        break;
                    }
                }
            }
        }
        if (!foundLocalConstClone || !clonedConstOp.valid())
        {
            return fail("Expected source constant clone to enter compute scheduling");
        }
        if (graph.opOperands(addOp).size() < 2 ||
            graph.valueDef(graph.opOperands(addOp)[1]) != clonedConstOp)
        {
            return fail("Expected compute op operand to be rewritten to source clone");
        }
    }

    {
        currentCase = "mem_read";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("mem_read");
        design.markAsTop("mem_read");

        const auto addr = makeValue(graph, "addr", 2);
        const auto a = makeValue(graph, "a", 8);
        graph.bindInputPort("addr", addr);
        graph.bindInputPort("a", a);
        const auto memDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kMemory,
                                                   graph.internSymbol("m"));
        graph.setAttr(memDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(memDecl, "row", static_cast<int64_t>(4));
        graph.setAttr(memDecl, "isSigned", false);
        const auto readValue = makeValue(graph, "r", 8);
        const auto readOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kMemoryReadPort,
                                                  graph.internSymbol("read"));
        graph.setAttr(readOp, "memSymbol", std::string("m"));
        graph.addOperand(readOp, addr);
        graph.addResult(readOp, readValue);
        const auto y = makeValue(graph, "y", 8);
        const auto xorOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("xor"));
        graph.addOperand(xorOp, readValue);
        graph.addOperand(xorOp, a);
        graph.addResult(xorOp, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "mem_read",
            .maxOpInComputeSupernode = 2,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected memory-read schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "mem_read");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if ((*schedule.opToSupernode)[readOp.index - 1] == kInvalidActivitySupernodeId)
        {
            return fail("Expected memory read to be scheduled as compute op");
        }
        std::size_t memoryReadCount = 0;
        for (const auto opId : graph.operations())
        {
            if (graph.opKind(opId) == wolvrix::lib::grh::OperationKind::kMemoryReadPort)
            {
                ++memoryReadCount;
            }
        }
        if (memoryReadCount != 1)
        {
            return fail("Expected memory read not to be cloned as source");
        }
        if (schedule.summaryStats->find("\"memory_read_activation_edges\":0") == std::string::npos)
        {
            return fail("Expected mem_read case to avoid cross-supernode memory-read propagation");
        }
    }

    {
        currentCase = "common_expr";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("common_expr");
        design.markAsTop("common_expr");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        const auto d = makeValue(graph, "d", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);
        graph.bindInputPort("d", d);
        const auto shared = makeValue(graph, "shared", 8);
        const auto sharedOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                    graph.internSymbol("shared_op"));
        graph.addOperand(sharedOp, a);
        graph.addOperand(sharedOp, b);
        graph.addResult(sharedOp, shared);
        const auto y0 = makeValue(graph, "y0", 8);
        const auto andOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kAnd,
                                                 graph.internSymbol("and"));
        graph.addOperand(andOp, shared);
        graph.addOperand(andOp, c);
        graph.addResult(andOp, y0);
        graph.bindOutputPort("y0", y0);
        const auto y1 = makeValue(graph, "y1", 8);
        const auto xorOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                 graph.internSymbol("xor"));
        graph.addOperand(xorOp, shared);
        graph.addOperand(xorOp, d);
        graph.addResult(xorOp, y1);
        graph.bindOutputPort("y1", y1);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "common_expr",
            .maxOpInComputeSupernode = 1,
            .enableCoarsen = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected common expr schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "common_expr");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        const uint32_t sharedSupernode = (*schedule.opToSupernode)[sharedOp.index - 1];
        if (sharedSupernode == (*schedule.opToSupernode)[andOp.index - 1] ||
            sharedSupernode == (*schedule.opToSupernode)[xorOp.index - 1])
        {
            return fail("Expected shared expression to remain an independent compute node");
        }
        if (!hasFanoutTo(*schedule.valueFanout, shared, (*schedule.opToSupernode)[andOp.index - 1]) ||
            !hasFanoutTo(*schedule.valueFanout, shared, (*schedule.opToSupernode)[xorOp.index - 1]))
        {
            return fail("Expected shared expression fanout to both consumers");
        }
        if (schedule.summaryStats->find("\"other_compute_activation_edges\":2") == std::string::npos)
        {
            return fail("Expected common_expr case to report two compute propagation edges");
        }
        if (schedule.summaryStats->find("\"other_compute_multi_target_values\":1") == std::string::npos)
        {
            return fail("Expected common_expr case to report one multi-target compute value");
        }
    }

    {
        currentCase = "declared_value_local_compute";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("declared_value_local_compute");
        design.markAsTop("declared_value_local_compute");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        const auto c = makeValue(graph, "c", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("c", c);

        const auto wireSym = graph.internSymbol("declared_wire");
        graph.addDeclaredSymbol(wireSym);
        const auto declaredWire = graph.createValue(wireSym, 8, false);
        const auto declaredProducer = graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd,
                                                            graph.internSymbol("declared_producer"));
        graph.addOperand(declaredProducer, a);
        graph.addOperand(declaredProducer, b);
        graph.addResult(declaredProducer, declaredWire);

        const auto y = makeValue(graph, "y", 8);
        const auto consumer = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                    graph.internSymbol("declared_consumer"));
        graph.addOperand(consumer, declaredWire);
        graph.addOperand(consumer, c);
        graph.addResult(consumer, y);
        graph.bindOutputPort("y", y);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "declared_value_local_compute",
            .maxOpInComputeSupernode = 2,
            .enableCoarsen = false,
            .enableChainMerge = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected declared-value-local-compute schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "declared_value_local_compute");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        const uint32_t producerSupernode = (*schedule.opToSupernode)[declaredProducer.index - 1];
        const uint32_t consumerSupernode = (*schedule.opToSupernode)[consumer.index - 1];
        if (producerSupernode == kInvalidActivitySupernodeId ||
            consumerSupernode == kInvalidActivitySupernodeId ||
            producerSupernode != consumerSupernode)
        {
            return fail("Expected declared compute value producer and single consumer to stay local");
        }
        if (hasFanoutTo(*schedule.valueFanout, declaredWire, consumerSupernode))
        {
            return fail("Expected local declared compute value to avoid cross-supernode fanout");
        }
        if (schedule.summaryStats->find("\"compute_compute_value_pairs\":0") == std::string::npos)
        {
            return fail("Expected declared_value_local_compute case to report zero compute->compute value pairs");
        }
    }

    {
        currentCase = "essent_split_oversize_compute_node";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("essent_split_oversize_compute_node");
        design.markAsTop("essent_split_oversize_compute_node");

        const auto a = makeValue(graph, "a", 8);
        const auto b = makeValue(graph, "b", 8);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);

        std::vector<wolvrix::lib::grh::OperationId> ops;
        wolvrix::lib::grh::ValueId cursor = a;
        for (int i = 0; i < 5; ++i)
        {
            const auto result = makeValue(graph, "chain_" + std::to_string(i), 8);
            const auto op = graph.createOperation(wolvrix::lib::grh::OperationKind::kXor,
                                                  graph.internSymbol("chain_op_" + std::to_string(i)));
            graph.addOperand(op, cursor);
            graph.addOperand(op, b);
            graph.addResult(op, result);
            ops.push_back(op);
            cursor = result;
        }
        graph.bindOutputPort("y", cursor);

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "essent_split_oversize_compute_node",
            .maxOpInComputeSupernode = 2,
            .maxOpInComputeNode = 16,
            .enableCoarsen = false,
            .enableEssentMffcBuild = true,
            .splitOversizeComputeNodes = true,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected oversize MFFC compute-node split schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "essent_split_oversize_compute_node");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        const uint32_t firstSupernode = (*schedule.opToSupernode)[ops.front().index - 1];
        const uint32_t lastSupernode = (*schedule.opToSupernode)[ops.back().index - 1];
        if (firstSupernode == kInvalidActivitySupernodeId ||
            lastSupernode == kInvalidActivitySupernodeId ||
            firstSupernode == lastSupernode)
        {
            return fail("Expected oversize MFFC compute node to split into multiple final supernodes");
        }
        bool splitReachable = false;
        if (firstSupernode < schedule.dag->size())
        {
            std::vector<uint32_t> stack{firstSupernode};
            std::vector<uint8_t> seen(schedule.dag->size(), 0);
            seen[firstSupernode] = 1;
            while (!stack.empty())
            {
                const uint32_t node = stack.back();
                stack.pop_back();
                if (node == lastSupernode)
                {
                    splitReachable = true;
                    break;
                }
                for (const uint32_t succ : (*schedule.dag)[node])
                {
                    if (succ < seen.size() && seen[succ] == 0)
                    {
                        seen[succ] = 1;
                        stack.push_back(succ);
                    }
                }
            }
        }
        if (!splitReachable)
        {
            return fail("Expected split chunks from the same MFFC compute node to stay reachable in DAG");
        }
        for (const auto &supernodeOps : *schedule.supernodeToOps)
        {
            if (supernodeOps.size() > 2)
            {
                return fail("Expected split final compute supernodes to obey maxOpInComputeSupernode");
            }
        }
    }

    {
        currentCase = "commit_chunk";
        wolvrix::lib::grh::Design design;
        auto &graph = design.createGraph("commit_chunk");
        design.markAsTop("commit_chunk");

        const auto clk = makeValue(graph, "clk", 1);
        const auto en = makeValue(graph, "en", 1);
        const auto mask = makeValue(graph, "mask", 8);
        const auto d0 = makeValue(graph, "d0", 8);
        const auto d1 = makeValue(graph, "d1", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);
        graph.bindInputPort("d0", d0);
        graph.bindInputPort("d1", d1);
        for (const char *name : {"q0", "q1"})
        {
            const auto reg = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                                   graph.internSymbol(name));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
        }
        const auto write0 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("w0"));
        graph.addOperand(write0, en);
        graph.addOperand(write0, d0);
        graph.addOperand(write0, mask);
        graph.addOperand(write0, clk);
        graph.setAttr(write0, "regSymbol", std::string("q0"));
        graph.setAttr(write0, "eventEdge", std::vector<std::string>{"posedge"});
        const auto write1 = graph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("w1"));
        graph.addOperand(write1, en);
        graph.addOperand(write1, d1);
        graph.addOperand(write1, mask);
        graph.addOperand(write1, clk);
        graph.setAttr(write1, "regSymbol", std::string("q1"));
        graph.setAttr(write1, "eventEdge", std::vector<std::string>{"posedge"});

        SessionStore session;
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "commit_chunk",
            .maxOpInComputeSupernode = 1,
            .maxOpInCommitSupernode = 1,
            .enableCoarsen = false,
        }));
        PassDiagnostics diags;
        const PassManagerResult runResult = manager.run(design, diags);
        if (!runResult.success || diags.hasError())
        {
            return fail("Expected commit chunk schedule to succeed");
        }
        const auto schedule = loadSchedule(session, "commit_chunk");
        if (const int rc = validateCommonScheduleShape(graph, schedule); rc != 0)
        {
            return rc;
        }
        if ((*schedule.opToSupernode)[write0.index - 1] == (*schedule.opToSupernode)[write1.index - 1])
        {
            return fail("Expected maxOpInCommitSupernode to split commit supernodes");
        }
    }

    return 0;
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Unhandled exception in ") + currentCase + ": " + ex.what());
    }
}
