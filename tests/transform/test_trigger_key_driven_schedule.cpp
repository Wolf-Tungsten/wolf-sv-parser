#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/trigger_key_driven_schedule.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

    using wolvrix::lib::grh::AttributeValue;
    using wolvrix::lib::grh::Graph;
    using wolvrix::lib::grh::Operation;
    using wolvrix::lib::grh::OperationId;
    using wolvrix::lib::grh::OperationKind;
    using wolvrix::lib::grh::ValueId;
    using wolvrix::lib::grh::ValueType;

    int fail(const std::string &message)
    {
        std::cerr << "[trigger-key-driven-schedule-tests] " << message << '\n';
        return 1;
    }

    template <typename T>
    std::optional<T> getAttr(const Operation &op, std::string_view key)
    {
        const std::optional<AttributeValue> attr = op.attr(key);
        if (!attr)
        {
            return std::nullopt;
        }
        if (const auto *value = std::get_if<T>(&*attr))
        {
            return *value;
        }
        return std::nullopt;
    }

    ValueId makeValue(Graph &graph,
                      const std::string &name,
                      int32_t width = 8,
                      bool isSigned = false,
                      ValueType type = ValueType::Logic)
    {
        return graph.createValue(graph.internSymbol(name), width, isSigned, type);
    }

    OperationId makeBinaryOp(Graph &graph,
                             OperationKind kind,
                             const std::string &name,
                             ValueId lhs,
                             ValueId rhs,
                             ValueId out)
    {
        const OperationId op = graph.createOperation(kind, graph.internSymbol(name));
        graph.addOperand(op, lhs);
        graph.addOperand(op, rhs);
        graph.addResult(op, out);
        return op;
    }

    struct TestIds
    {
        OperationId sharedAdd;
        OperationId clkLogicAnd;
        OperationId regData0;
        OperationId regData1;
        OperationId outExpr;
        OperationId internalUse;
        OperationId deadXor;
        OperationId regDecl0;
        OperationId regDecl1;
        OperationId regWrite0;
        OperationId regWrite1;
        OperationId dpiImport;
        OperationId systemTask;
        ValueId clkLogicValue;
        ValueId rstNValue;
    };

    TestIds populateDesign(wolvrix::lib::grh::Design &design)
    {
        Graph &graph = design.createGraph("top");
        design.markAsTop("top");

        const ValueId clk = makeValue(graph, "clk", 1, false);
        const ValueId rstN = makeValue(graph, "rstN", 1, false);
        const ValueId a = makeValue(graph, "a");
        const ValueId b = makeValue(graph, "b");
        const ValueId en = makeValue(graph, "en", 1, false);
        const ValueId mask = makeValue(graph, "mask");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("rstN", rstN);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("en", en);
        graph.bindInputPort("mask", mask);

        const ValueId sharedValue = makeValue(graph, "shared");
        const OperationId sharedAdd =
            makeBinaryOp(graph, OperationKind::kAdd, "shared_add", a, b, sharedValue);

        const ValueId clkLogicValue = makeValue(graph, "clk_logic", 1, false);
        const OperationId clkLogicAnd =
            makeBinaryOp(graph, OperationKind::kAnd, "clk_logic_and", clk, en, clkLogicValue);

        const ValueId regData0Value = makeValue(graph, "reg_data0");
        const OperationId regData0 =
            makeBinaryOp(graph, OperationKind::kAdd, "reg_data0_op", sharedValue, a, regData0Value);

        const ValueId regData1Value = makeValue(graph, "reg_data1");
        const OperationId regData1 =
            makeBinaryOp(graph, OperationKind::kSub, "reg_data1_op", sharedValue, b, regData1Value);

        const ValueId outExprValue = makeValue(graph, "out_expr");
        const OperationId outExpr =
            makeBinaryOp(graph, OperationKind::kAdd, "out_expr_op", sharedValue, a, outExprValue);
        graph.bindOutputPort("y", outExprValue);

        const ValueId internalUseValue = makeValue(graph, "internal_use");
        const OperationId internalUse =
            makeBinaryOp(graph, OperationKind::kAnd, "internal_use_op", outExprValue, mask, internalUseValue);

        const ValueId deadXorValue = makeValue(graph, "dead_xor");
        const OperationId deadXor =
            makeBinaryOp(graph, OperationKind::kXor, "dead_xor_op", a, b, deadXorValue);

        const OperationId regDecl0 = graph.createOperation(OperationKind::kRegister, graph.internSymbol("reg0"));
        graph.setAttr(regDecl0, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl0, "isSigned", false);

        const OperationId regDecl1 = graph.createOperation(OperationKind::kRegister, graph.internSymbol("reg1"));
        graph.setAttr(regDecl1, "width", static_cast<int64_t>(8));
        graph.setAttr(regDecl1, "isSigned", false);

        const OperationId regWrite0 =
            graph.createOperation(OperationKind::kRegisterWritePort, graph.internSymbol("reg0_wr"));
        graph.addOperand(regWrite0, en);
        graph.addOperand(regWrite0, regData0Value);
        graph.addOperand(regWrite0, mask);
        graph.addOperand(regWrite0, clkLogicValue);
        graph.addOperand(regWrite0, rstN);
        graph.setAttr(regWrite0, "regSymbol", std::string("reg0"));
        graph.setAttr(regWrite0, "eventEdge", std::vector<std::string>{"posedge", "negedge"});

        const OperationId regWrite1 =
            graph.createOperation(OperationKind::kRegisterWritePort, graph.internSymbol("reg1_wr"));
        graph.addOperand(regWrite1, en);
        graph.addOperand(regWrite1, regData1Value);
        graph.addOperand(regWrite1, mask);
        graph.addOperand(regWrite1, rstN);
        graph.addOperand(regWrite1, clkLogicValue);
        graph.addOperand(regWrite1, clkLogicValue);
        graph.setAttr(regWrite1, "regSymbol", std::string("reg1"));
        graph.setAttr(regWrite1, "eventEdge", std::vector<std::string>{"negedge", "posedge", "posedge"});

        const OperationId dpiImport =
            graph.createOperation(OperationKind::kDpicImport, graph.internSymbol("dpi_decl"));
        graph.setAttr(dpiImport, "hasReturn", false);

        const OperationId systemTask =
            graph.createOperation(OperationKind::kSystemTask, graph.internSymbol("display_task"));
        graph.addOperand(systemTask, en);
        graph.addOperand(systemTask, outExprValue);
        graph.addOperand(systemTask, sharedValue);
        graph.setAttr(systemTask, "name", std::string("display"));

        return TestIds{
            sharedAdd,
            clkLogicAnd,
            regData0,
            regData1,
            outExpr,
            internalUse,
            deadXor,
            regDecl0,
            regDecl1,
            regWrite0,
            regWrite1,
            dpiImport,
            systemTask,
            clkLogicValue,
            rstN,
        };
    }

    class CheckTriggerKeyDrivenSchedulePass : public Pass
    {
    public:
        explicit CheckTriggerKeyDrivenSchedulePass(const TestIds &ids)
            : Pass("check-trigger-key-driven-schedule", "check-trigger-key-driven-schedule"), ids_(ids)
        {
        }

        PassResult run() override
        {
            auto failPass = [&](const std::string &message) {
                diags().error(id(), message);
                return PassResult{false, true, {}};
            };

            const std::string base = "tkds/top/";
            const auto *meta = getScratchpad<TkdScheduleMeta>(base + "meta");
            const auto *triggerKeys = getScratchpad<std::vector<TkdTriggerKeyRecord>>(base + "pools/triggerKeys");
            const auto *affectedSinkSets =
                getScratchpad<std::vector<TkdAffectedSinkSetRecord>>(base + "pools/affectedSinkSets");
            const auto *sinkGroups = getScratchpad<std::vector<TkdSinkGroupRecord>>(base + "groups/sink");
            const auto *triggerGroups =
                getScratchpad<std::vector<TkdTriggerGroupRecord>>(base + "groups/trigger");
            const auto *simpleGroups = getScratchpad<std::vector<TkdSimpleGroupRecord>>(base + "groups/simple");
            const auto *opToGroup = getScratchpad<TkdOpToGroupIndex>(base + "index/opToTkdGroup");
            const auto *edges = getScratchpad<std::vector<TkdGroupEdge>>(base + "plan/edges");
            const auto *topoOrder = getScratchpad<std::vector<TkdGroupId>>(base + "plan/topoOrder");
            if (!meta || !triggerKeys || !affectedSinkSets || !sinkGroups || !triggerGroups || !simpleGroups ||
                !opToGroup || !edges || !topoOrder)
            {
                return failPass("scratchpad entries missing");
            }

            auto *graph = design().findGraph("top");
            if (graph == nullptr)
            {
                return failPass("top graph missing");
            }

            const ValueId outputValue = graph->outputPortValue("y");
            if (!outputValue.valid())
            {
                return failPass("output port y missing after pass");
            }
            const OperationId outputDef = graph->valueDef(outputValue);
            if (!outputDef.valid() || graph->opKind(outputDef) != OperationKind::kAssign)
            {
                return failPass("top-level output should be rebound through a dedicated kAssign sink");
            }
            const Operation outputAssign = graph->getOperation(outputDef);
            if (outputAssign.operands().size() != 1)
            {
                return failPass("normalized output assign must have one operand");
            }
            const OperationId outExprDef = graph->valueDef(outputAssign.operands()[0]);
            if (outExprDef != ids_.outExpr)
            {
                return failPass("normalized output assign must read the original output expression");
            }

            if (meta->normalizedTopLevelSinkAssignCount != 1)
            {
                return failPass("expected exactly one normalized top-level sink assign");
            }
            if (meta->modulePath != "top" || meta->graphSymbol != "top")
            {
                return failPass("meta path or graph symbol mismatch");
            }
            if (meta->sinkGroupCount != 2)
            {
                return failPass("expected exactly two sink groups");
            }
            if (meta->simpleGroupCount != 4)
            {
                return failPass("expected four simple groups");
            }
            if (triggerGroups->size() != 1)
            {
                return failPass("expected one trigger group");
            }

            const auto emptySinkGroupIt = std::find_if(
                sinkGroups->begin(),
                sinkGroups->end(),
                [&](const TkdSinkGroupRecord &record) { return record.triggerKeyId == meta->emptyTriggerKeyId; });
            const auto nonEmptySinkGroupIt = std::find_if(
                sinkGroups->begin(),
                sinkGroups->end(),
                [&](const TkdSinkGroupRecord &record) { return record.triggerKeyId != meta->emptyTriggerKeyId; });
            if (emptySinkGroupIt == sinkGroups->end() || nonEmptySinkGroupIt == sinkGroups->end())
            {
                return failPass("expected one empty-key sink group and one non-empty-key sink group");
            }
            if (emptySinkGroupIt->memberOps.size() != 2)
            {
                return failPass("empty-key sink group should contain system task and output sink assign");
            }
            if (nonEmptySinkGroupIt->memberOps.size() != 2)
            {
                return failPass("non-empty sink group should contain both register writes");
            }

            const auto &triggerKey = (*triggerKeys)[nonEmptySinkGroupIt->triggerKeyId];
            if (triggerKey.items.size() != 2)
            {
                return failPass("normalized trigger key should contain two unique event items");
            }
            bool sawClkPosedge = false;
            bool sawRstNegedge = false;
            for (const auto &item : triggerKey.items)
            {
                sawClkPosedge = sawClkPosedge ||
                                (item.valueId == ids_.clkLogicValue && item.eventEdge == TriggerEventEdge::kPosedge);
                sawRstNegedge = sawRstNegedge ||
                                (item.valueId == ids_.rstNValue && item.eventEdge == TriggerEventEdge::kNegedge);
            }
            if (!sawClkPosedge || !sawRstNegedge)
            {
                return failPass("normalized trigger key items mismatch");
            }

            const TkdTriggerGroupRecord &triggerGroup = triggerGroups->front();
            if (triggerGroup.rootValues.size() != 2)
            {
                return failPass("trigger group should have two unique root values");
            }
            if (std::find(triggerGroup.memberOps.begin(), triggerGroup.memberOps.end(), ids_.clkLogicAnd) ==
                triggerGroup.memberOps.end())
            {
                return failPass("trigger group should contain the clock-tree driving op");
            }
            const auto &triggerAffectedSet = (*affectedSinkSets)[triggerGroup.affectedSinkSetId].sinkGroupIds;
            if (triggerAffectedSet.size() != 1 || triggerAffectedSet.front() != nonEmptySinkGroupIt->sinkGroupId)
            {
                return failPass("trigger group affected sink set should only include the non-empty-key sink group");
            }

            auto groupForSet = [&](const std::vector<SinkTKDGroupId> &targetSet) -> const TkdSimpleGroupRecord * {
                for (const auto &group : *simpleGroups)
                {
                    const auto &set = (*affectedSinkSets)[group.affectedSinkSetId].sinkGroupIds;
                    if (set == targetSet)
                    {
                        return &group;
                    }
                }
                return nullptr;
            };

            const TkdSimpleGroupRecord *emptySetGroup = groupForSet({});
            const TkdSimpleGroupRecord *emptySinkOnlyGroup = groupForSet({emptySinkGroupIt->sinkGroupId});
            const TkdSimpleGroupRecord *nonEmptySinkOnlyGroup = groupForSet({nonEmptySinkGroupIt->sinkGroupId});
            const TkdSimpleGroupRecord *bothSinkGroup =
                groupForSet({std::min(emptySinkGroupIt->sinkGroupId, nonEmptySinkGroupIt->sinkGroupId),
                             std::max(emptySinkGroupIt->sinkGroupId, nonEmptySinkGroupIt->sinkGroupId)});
            if (!emptySetGroup || !emptySinkOnlyGroup || !nonEmptySinkOnlyGroup || !bothSinkGroup)
            {
                return failPass("missing expected simple groups");
            }

            auto containsOp = [](const std::vector<OperationId> &ops, OperationId target) {
                return std::find(ops.begin(), ops.end(), target) != ops.end();
            };
            if (!containsOp(emptySetGroup->memberOps, ids_.deadXor) ||
                !containsOp(emptySetGroup->memberOps, ids_.internalUse))
            {
                return failPass("empty affected-sink-set group should contain dead executable ops");
            }
            if (!containsOp(emptySinkOnlyGroup->memberOps, ids_.outExpr))
            {
                return failPass("empty-key simple group should contain the output expression");
            }
            if (!containsOp(nonEmptySinkOnlyGroup->memberOps, ids_.regData0) ||
                !containsOp(nonEmptySinkOnlyGroup->memberOps, ids_.regData1))
            {
                return failPass("non-empty-key simple group should contain both register data ops");
            }
            if (!containsOp(bothSinkGroup->memberOps, ids_.sharedAdd))
            {
                return failPass("union affected-sink-set group should contain the shared upstream op");
            }

            auto requireInvalidGroup = [&](OperationId opId, std::string_view name) -> std::optional<PassResult> {
                if (opId.index >= opToGroup->groupIdByOpIndex.size())
                {
                    return failPass(std::string(name) + " missing from opToTkdGroup index");
                }
                if (opToGroup->groupIdByOpIndex[opId.index] != opToGroup->invalidTkdGroupId)
                {
                    return failPass(std::string(name) + " should stay outside all TKD groups");
                }
                return std::nullopt;
            };
            if (auto failed = requireInvalidGroup(ids_.regDecl0, "regDecl0"))
            {
                return *failed;
            }
            if (auto failed = requireInvalidGroup(ids_.regDecl1, "regDecl1"))
            {
                return *failed;
            }
            if (auto failed = requireInvalidGroup(ids_.dpiImport, "dpiImport"))
            {
                return *failed;
            }

            std::unordered_set<TkdGroupId> directTriggerTargets;
            for (const auto &edge : *edges)
            {
                if (edge.srcGroupId == triggerGroup.tkdGroupId)
                {
                    directTriggerTargets.insert(edge.dstGroupId);
                }
            }
            for (TkdGroupId groupId = 0; groupId < meta->tkdGroupCount; ++groupId)
            {
                if (groupId == triggerGroup.tkdGroupId)
                {
                    continue;
                }
                if (!directTriggerTargets.contains(groupId))
                {
                    return failPass("trigger group should have a direct edge to every other TKD group");
                }
            }

            if (topoOrder->size() != meta->tkdGroupCount)
            {
                return failPass("topo order size mismatch");
            }
            std::unordered_map<TkdGroupId, std::size_t> topoIndex;
            topoIndex.reserve(topoOrder->size());
            for (std::size_t i = 0; i < topoOrder->size(); ++i)
            {
                topoIndex.emplace((*topoOrder)[i], i);
            }
            if (topoIndex.size() != topoOrder->size())
            {
                return failPass("topo order should not contain duplicate group ids");
            }
            for (const auto &edge : *edges)
            {
                const auto srcIt = topoIndex.find(edge.srcGroupId);
                const auto dstIt = topoIndex.find(edge.dstGroupId);
                if (srcIt == topoIndex.end() || dstIt == topoIndex.end() || srcIt->second >= dstIt->second)
                {
                    return failPass("topo order violates a TKD group dependency edge");
                }
            }

            return {};
        }

    private:
        TestIds ids_;
    };

} // namespace

int main()
{
    wolvrix::lib::grh::Design design;
    const TestIds ids = populateDesign(design);

    std::string makePassError;
    const std::vector<std::string_view> passArgs = {"-path", "top"};
    auto schedulePass = makePass("trigger-key-driven-schedule", passArgs, makePassError);
    if (!schedulePass)
    {
        return fail("failed to create trigger-key-driven-schedule pass: " + makePassError);
    }

    PassManager manager;
    manager.addPass(std::move(schedulePass));
    manager.addPass(std::make_unique<CheckTriggerKeyDrivenSchedulePass>(ids));

    PassDiagnostics diags;
    const PassManagerResult result = manager.run(design, diags);
    if (!result.success)
    {
        if (!diags.messages().empty())
        {
            return fail(diags.messages().front().message);
        }
        return fail("pipeline failed");
    }
    if (diags.hasError())
    {
        return fail("unexpected diagnostics errors");
    }

    return 0;
}
