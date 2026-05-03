#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/merge_reg.hpp"

#include <array>
#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace wolvrix::lib::grh;
using namespace wolvrix::lib::transform;

namespace
{
    int runMergeReg(Design &design, bool expectChanged, std::string_view label = "");
    int runMergeRegWithOptions(Design &design,
                               MergeRegOptions options,
                               bool expectChanged,
                               std::string_view label = "");

    int fail(const std::string &message)
    {
        std::cerr << "[transform-merge-reg] " << message << '\n';
        return 1;
    }

    ValueId makeLogicValue(Graph &graph, std::string_view name, int32_t width)
    {
        return graph.createValue(graph.internSymbol(std::string(name)), width, false, ValueType::Logic);
    }

    template <typename T>
    std::optional<T> getAttr(const Operation &op, std::string_view key)
    {
        const auto attr = op.attr(key);
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

    ValueId addConstant(Graph &graph,
                        std::string_view opName,
                        std::string_view valueName,
                        int32_t width,
                        std::string literal)
    {
        const ValueId value = makeLogicValue(graph, valueName, width);
        const OperationId op = graph.createOperation(OperationKind::kConstant,
                                                     graph.internSymbol(std::string(opName)));
        graph.addResult(op, value);
        graph.setAttr(op, "constValue", std::move(literal));
        return value;
    }

    ValueId addOp(Graph &graph,
                  OperationKind kind,
                  std::string_view opName,
                  std::string_view valueName,
                  int32_t width,
                  std::span<const ValueId> operands)
    {
        const ValueId value = makeLogicValue(graph, valueName, width);
        const OperationId op = graph.createOperation(kind, graph.internSymbol(std::string(opName)));
        for (ValueId operand : operands)
        {
            graph.addOperand(op, operand);
        }
        graph.addResult(op, value);
        return value;
    }

    std::size_t countOps(const Graph &graph, OperationKind kind)
    {
        std::size_t count = 0;
        for (const OperationId opId : graph.operations())
        {
            if (graph.getOperation(opId).kind() == kind)
            {
                ++count;
            }
        }
        return count;
    }

    bool storagePortsHaveTargets(const Graph &graph, std::string &error)
    {
        std::vector<std::string> registers;
        for (const OperationId opId : graph.operations())
        {
            const Operation op = graph.getOperation(opId);
            if (op.kind() == OperationKind::kRegister)
            {
                registers.push_back(std::string(op.symbolText()));
            }
        }
        for (const OperationId opId : graph.operations())
        {
            const Operation op = graph.getOperation(opId);
            if (op.kind() != OperationKind::kRegisterReadPort &&
                op.kind() != OperationKind::kRegisterWritePort)
            {
                continue;
            }
            const auto regSymbol = getAttr<std::string>(op, "regSymbol");
            if (!regSymbol)
            {
                error = "register port missing regSymbol";
                return false;
            }
            if (std::find(registers.begin(), registers.end(), *regSymbol) == registers.end())
            {
                error = "register port targets missing register: " + *regSymbol;
                return false;
            }
        }
        return true;
    }

    std::string diagSummary(const PassDiagnostics &diags)
    {
        std::string out;
        for (const auto &diag : diags.messages())
        {
            if (!out.empty())
            {
                out += " | ";
            }
            out += diag.passName.empty() ? diag.message : (diag.passName + ": " + diag.message);
        }
        return out.empty() ? std::string("<none>") : out;
    }

    Design buildSelfHoldDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId en = makeLogicValue(graph, "en", 1);
        const ValueId one = addConstant(graph, "one_op", "one", 1, "1'b1");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);

        for (int i = 0; i < 4; ++i)
        {
            const std::string name = "REG_" + std::to_string(i);
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(name));
            graph.setAttr(reg, "width", static_cast<int64_t>(1));
            graph.setAttr(reg, "isSigned", false);

            const ValueId q = makeLogicValue(graph, "q_" + std::to_string(i), 1);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", name);
            graph.addResult(read, q);

            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("write_" + std::to_string(i)));
            graph.setAttr(write, "regSymbol", name);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, en);
            graph.addOperand(write, q);
            graph.addOperand(write, one);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildScalarMemoryPackDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId data = makeLogicValue(graph, "data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("data", data);

        const ValueId mask = addConstant(graph, "mask_op", "mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "c0_op", "c0", 2, "2'd0"),
            addConstant(graph, "c1_op", "c1", 2, "2'd1"),
            addConstant(graph, "c2_op", "c2", 2, "2'd2"),
            addConstant(graph, "c3_op", "c3", 2, "2'd3"),
        };

        std::vector<ValueId> reads;
        std::vector<std::string> names;
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "arr_" + std::to_string(i) + "_value";
            names.push_back(regName);
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "arr_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("arr_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            reads.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("packed_concat"));
        graph.addOperand(concat, reads[3]);
        graph.addOperand(concat, reads[2]);
        graph.addOperand(concat, reads[1]);
        graph.addOperand(concat, reads[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("packed_idx"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idx);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId hit = addOp(graph,
                                      OperationKind::kEq,
                                      "eq_op_" + suffix,
                                      "eq_" + suffix,
                                      1,
                                      std::array<ValueId, 2>{idx, indices[static_cast<std::size_t>(i)]});
            const ValueId pointCond = addOp(graph,
                                            OperationKind::kAnd,
                                            "point_and_op_" + suffix,
                                            "point_cond_" + suffix,
                                            1,
                                            std::array<ValueId, 2>{wrEn, hit});

            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("point_write_" + suffix));
            graph.setAttr(pointWrite, "regSymbol", names[static_cast<std::size_t>(i)]);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("fill_write_" + suffix));
            graph.setAttr(fillWrite, "regSymbol", names[static_cast<std::size_t>(i)]);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    Design buildIndexedBundleEntryDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId enA = makeLogicValue(graph, "en_a", 1);
        const ValueId enB = makeLogicValue(graph, "en_b", 1);
        const ValueId mask1 = addConstant(graph, "mask1_op", "mask1", 1, "1'b1");
        const ValueId mask4 = addConstant(graph, "mask4_op", "mask4", 4, "4'hf");
        const ValueId mask8 = addConstant(graph, "mask8_op", "mask8", 8, "8'hff");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en_a", enA);
        graph.bindInputPort("en_b", enB);

        const std::vector<std::tuple<std::string, int32_t, bool>> fields{
            {"valid", 1, false},
            {"isRVC", 1, false},
            {"fixedTaken", 1, false},
            {"predTaken", 1, false},
            {"taken", 1, true},
            {"instr", 8, true},
            {"pc", 8, true},
            {"foldpc", 4, true},
        };
        for (int entry = 0; entry < 2; ++entry)
        {
            for (const auto &[field, width, useEnB] : fields)
            {
                const std::string regName = "bundle_" + std::to_string(entry) + "_" + field;
                const OperationId reg = graph.createOperation(OperationKind::kRegister,
                                                              graph.internSymbol(regName));
                graph.setAttr(reg, "width", static_cast<int64_t>(width));
                graph.setAttr(reg, "isSigned", false);
                graph.setAttr(reg, "initValue", width == 1 ? std::string("1'b0") : std::string("8'h00"));

                const ValueId q = makeLogicValue(graph,
                                                 "bundle_q_" + std::to_string(entry) + "_" + field,
                                                 width);
                const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                               graph.internSymbol("bundle_read_" + std::to_string(entry) + "_" + field));
                graph.setAttr(read, "regSymbol", regName);
                graph.addResult(read, q);

                const ValueId data = makeLogicValue(graph,
                                                    "bundle_d_" + std::to_string(entry) + "_" + field,
                                                    width);
                const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("bundle_write_" + std::to_string(entry) + "_" + field));
                graph.setAttr(write, "regSymbol", regName);
                graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
                graph.addOperand(write, useEnB ? enB : enA);
                graph.addOperand(write, data);
                graph.addOperand(write, width == 1 ? mask1 : (width == 4 ? mask4 : mask8));
                graph.addOperand(write, clk);
            }
        }

        return design;
    }

    Design buildMaskedLaneBundleEntryDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId rst = makeLogicValue(graph, "rst", 1);
        const ValueId mask8 = addConstant(graph, "lane_mask_op", "lane_mask", 8, "8'hff");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("rst", rst);

        for (int lane = 0; lane < 8; ++lane)
        {
            const std::string regName = "lane_bundle_0_" + std::to_string(lane);
            const OperationId reg = graph.createOperation(OperationKind::kRegister,
                                                          graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "lane_q_" + std::to_string(lane), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("lane_read_" + std::to_string(lane)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);

            const ValueId en = makeLogicValue(graph, "lane_en_" + std::to_string(lane), 1);
            const ValueId data = makeLogicValue(graph, "lane_d_" + std::to_string(lane), 8);
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("lane_write_" + std::to_string(lane)));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge", "posedge"});
            graph.addOperand(write, en);
            graph.addOperand(write, data);
            graph.addOperand(write, mask8);
            graph.addOperand(write, clk);
            graph.addOperand(write, rst);
        }

        return design;
    }

    int checkIndexedBundleEntryRewrite(Design &design)
    {
        if (const int status = runMergeReg(design, true, "indexed bundle entry"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("indexed bundle entry: missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 4 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 4 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 4)
        {
            return fail("indexed bundle entry should become four wide register groups, got regs=" +
                        std::to_string(countOps(*graph, OperationKind::kRegister)) +
                        " reads=" + std::to_string(countOps(*graph, OperationKind::kRegisterReadPort)) +
                        " writes=" + std::to_string(countOps(*graph, OperationKind::kRegisterWritePort)));
        }

        std::vector<int64_t> widths;
        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (op.kind() == OperationKind::kRegister)
            {
                widths.push_back(getAttr<int64_t>(op, "width").value_or(0));
            }
        }
        std::sort(widths.begin(), widths.end());
        if (widths != std::vector<int64_t>{4, 4, 21, 21})
        {
            return fail("indexed bundle entry wide register widths should preserve write-enable groups");
        }
        return 0;
    }

    int checkMaskedLaneBundleEntryRewrite(Design &design)
    {
        if (const int status = runMergeReg(design, true, "masked lane bundle entry"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("masked lane bundle entry: missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 1 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 1 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 1)
        {
            return fail("masked lane bundle entry should become one wide register group");
        }
        OperationId wideReg;
        OperationId wideWrite;
        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (op.kind() == OperationKind::kRegister)
            {
                wideReg = opId;
            }
            if (op.kind() == OperationKind::kRegisterWritePort)
            {
                wideWrite = opId;
            }
        }
        if (!wideReg.valid() || getAttr<int64_t>(graph->getOperation(wideReg), "width").value_or(0) != 64)
        {
            return fail("masked lane bundle entry wide register should be 64 bits");
        }
        if (!wideWrite.valid() || graph->getOperation(wideWrite).operands().size() != 5)
        {
            return fail("masked lane bundle entry should preserve two event operands");
        }
        return 0;
    }

    int runMergeReg(Design &design, bool expectChanged, std::string_view label)
    {
        return runMergeRegWithOptions(design, MergeRegOptions{}, expectChanged, label);
    }

    int runMergeRegWithOptions(Design &design,
                               MergeRegOptions options,
                               bool expectChanged,
                               std::string_view label)
    {
        PassManager manager;
        manager.addPass(std::make_unique<MergeRegPass>(options));
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            return fail("merge-reg pass should succeed: " + diagSummary(diags));
        }
        if (result.changed != expectChanged)
        {
            const std::string prefix = label.empty() ? std::string() : (std::string(label) + ": ");
            return fail(prefix + (expectChanged ? "merge-reg should rewrite the design"
                                                : "merge-reg should leave the design unchanged"));
        }
        for (const auto &entry : design.graphs())
        {
            std::string error;
            if (!storagePortsHaveTargets(*entry.second, error))
            {
                const std::string prefix = label.empty() ? std::string() : (std::string(label) + ": ");
                return fail(prefix + error);
            }
        }
        return 0;
    }
} // namespace

int main()
{
    try
    {
        Design design = buildScalarMemoryPackDesign();
        if (const int status = runMergeReg(design, true, "scalar-to-memory"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("scalar-to-memory: missing graph");
        }
        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 1 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 1)
        {
            return fail("scalar-to-memory should become one memory/read/write/fill");
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("scalar-to-memory case threw: ") + ex.what());
    }

    try
    {
        Design design = buildScalarMemoryPackDesign();
        MergeRegOptions options;
        options.enableScalarToMemory = false;
        if (const int status = runMergeRegWithOptions(design, options, false, "scalar-to-memory disabled"); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("scalar-to-memory disabled case threw: ") + ex.what());
    }

    try
    {
        Design design = buildSelfHoldDesign();
        if (const int status = runMergeReg(design, false, "self hold"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 4 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 4 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 4)
        {
            return fail("self-hold direct reads should not be packed");
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("self-hold case threw: ") + ex.what());
    }

    try
    {
        Design design = buildIndexedBundleEntryDesign();
        if (const int status = checkIndexedBundleEntryRewrite(design); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("indexed bundle entry case threw: ") + ex.what());
    }

    try
    {
        Design design = buildIndexedBundleEntryDesign();
        MergeRegOptions options;
        options.enableIndexedBundleEntryToWideRegister = false;
        if (const int status = runMergeRegWithOptions(design, options, false, "indexed bundle entry disabled"); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("indexed bundle entry disabled case threw: ") + ex.what());
    }

    try
    {
        Design design = buildMaskedLaneBundleEntryDesign();
        if (const int status = checkMaskedLaneBundleEntryRewrite(design); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("masked lane bundle entry case threw: ") + ex.what());
    }

    return 0;
}
