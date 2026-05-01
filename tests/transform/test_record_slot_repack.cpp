#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/record_slot_repack.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace wolvrix::lib::grh;
using namespace wolvrix::lib::transform;

namespace
{
    int fail(const std::string &message)
    {
        std::cerr << "[transform-record-slot-repack] " << message << '\n';
        return 1;
    }

    ValueId makeLogicValue(Graph &graph, std::string_view name, int32_t width, bool isSigned = false)
    {
        return graph.createValue(graph.internSymbol(std::string(name)), width, isSigned, ValueType::Logic);
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
                        std::string literal,
                        bool isSigned = false)
    {
        const ValueId value = makeLogicValue(graph, valueName, width, isSigned);
        const OperationId op = graph.createOperation(OperationKind::kConstant,
                                                     graph.internSymbol(std::string(opName)));
        graph.addResult(op, value);
        graph.setAttr(op, "constValue", std::move(literal));
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

    Design buildSlotRecordDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId dataA = makeLogicValue(graph, "data_a", 8);
        const ValueId dataB = makeLogicValue(graph, "data_b", 1);
        const ValueId fillA = makeLogicValue(graph, "fill_a", 8);
        const ValueId fillB = makeLogicValue(graph, "fill_b", 1);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("data_a", dataA);
        graph.bindInputPort("data_b", dataB);
        graph.bindInputPort("fill_a", fillA);
        graph.bindInputPort("fill_b", fillB);

        const ValueId maskA = addConstant(graph, "mask_a_op", "mask_a", 8, "8'hff");
        const ValueId maskB = addConstant(graph, "mask_b_op", "mask_b", 1, "1'b1");
        const ValueId c0 = addConstant(graph, "c0_op", "c0", 2, "2'd0");
        const ValueId c1 = addConstant(graph, "c1_op", "c1", 2, "2'd1");
        const ValueId c2 = addConstant(graph, "c2_op", "c2", 2, "2'd2");

        std::vector<ValueId> fieldAReads;
        std::vector<ValueId> fieldBReads;
        for (int slot = 0; slot < 4; ++slot)
        {
            const std::string regAName = "slot_" + std::to_string(slot) + "_field_a";
            const std::string regBName = "slot_" + std::to_string(slot) + "_field_b";

            const OperationId regA = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regAName));
            graph.setAttr(regA, "width", static_cast<int64_t>(8));
            graph.setAttr(regA, "isSigned", false);
            graph.setAttr(regA, "initValue", std::string("8'h00"));

            const OperationId regB = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regBName));
            graph.setAttr(regB, "width", static_cast<int64_t>(1));
            graph.setAttr(regB, "isSigned", false);
            graph.setAttr(regB, "initValue", std::string("1'b0"));

            const ValueId qA = makeLogicValue(graph, "q_a_" + std::to_string(slot), 8);
            const OperationId readA = graph.createOperation(OperationKind::kRegisterReadPort,
                                                            graph.internSymbol("read_a_" + std::to_string(slot)));
            graph.setAttr(readA, "regSymbol", regAName);
            graph.addResult(readA, qA);
            fieldAReads.push_back(qA);

            const ValueId qB = makeLogicValue(graph, "q_b_" + std::to_string(slot), 1);
            const OperationId readB = graph.createOperation(OperationKind::kRegisterReadPort,
                                                            graph.internSymbol("read_b_" + std::to_string(slot)));
            graph.setAttr(readB, "regSymbol", regBName);
            graph.addResult(readB, qB);
            fieldBReads.push_back(qB);

            ValueId slotConst = c0;
            if (slot == 1)
            {
                slotConst = c1;
            }
            else if (slot == 2)
            {
                slotConst = c2;
            }
            else if (slot == 3)
            {
                slotConst = addConstant(graph, "c3_op", "c3", 2, "2'd3");
            }

            const ValueId hit = makeLogicValue(graph, "hit_" + std::to_string(slot), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("eq_" + std::to_string(slot)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, slotConst);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "point_cond_" + std::to_string(slot), 1);
            const OperationId andOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("point_and_" + std::to_string(slot)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const OperationId writeA = graph.createOperation(OperationKind::kRegisterWritePort,
                                                             graph.internSymbol("write_a_" + std::to_string(slot)));
            graph.setAttr(writeA, "regSymbol", regAName);
            graph.setAttr(writeA, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(writeA, pointCond);
            graph.addOperand(writeA, dataA);
            graph.addOperand(writeA, maskA);
            graph.addOperand(writeA, clk);

            const OperationId writeB = graph.createOperation(OperationKind::kRegisterWritePort,
                                                             graph.internSymbol("write_b_" + std::to_string(slot)));
            graph.setAttr(writeB, "regSymbol", regBName);
            graph.setAttr(writeB, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(writeB, pointCond);
            graph.addOperand(writeB, dataB);
            graph.addOperand(writeB, maskB);
            graph.addOperand(writeB, clk);

            const OperationId fillWriteA = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("fill_write_a_" + std::to_string(slot)));
            graph.setAttr(fillWriteA, "regSymbol", regAName);
            graph.setAttr(fillWriteA, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWriteA, fillEn);
            graph.addOperand(fillWriteA, fillA);
            graph.addOperand(fillWriteA, maskA);
            graph.addOperand(fillWriteA, clk);

            const OperationId fillWriteB = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("fill_write_b_" + std::to_string(slot)));
            graph.setAttr(fillWriteB, "regSymbol", regBName);
            graph.setAttr(fillWriteB, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWriteB, fillEn);
            graph.addOperand(fillWriteB, fillB);
            graph.addOperand(fillWriteB, maskB);
            graph.addOperand(fillWriteB, clk);
        }

        const ValueId muxA2 = makeLogicValue(graph, "mux_a_2", 8);
        const OperationId muxA2Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("mux_a_2_op"));
        graph.addOperand(muxA2Op, graph.findValue("hit_2"));
        graph.addOperand(muxA2Op, fieldAReads[2]);
        graph.addOperand(muxA2Op, fieldAReads[3]);
        graph.addResult(muxA2Op, muxA2);

        const ValueId muxA1 = makeLogicValue(graph, "mux_a_1", 8);
        const OperationId muxA1Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("mux_a_1_op"));
        graph.addOperand(muxA1Op, graph.findValue("hit_1"));
        graph.addOperand(muxA1Op, fieldAReads[1]);
        graph.addOperand(muxA1Op, muxA2);
        graph.addResult(muxA1Op, muxA1);

        const ValueId outA = makeLogicValue(graph, "out_a", 8);
        const OperationId muxA0Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("mux_a_0_op"));
        graph.addOperand(muxA0Op, graph.findValue("hit_0"));
        graph.addOperand(muxA0Op, fieldAReads[0]);
        graph.addOperand(muxA0Op, muxA1);
        graph.addResult(muxA0Op, outA);
        graph.bindOutputPort("out_a", outA);

        const ValueId muxB2 = makeLogicValue(graph, "mux_b_2", 1);
        const OperationId muxB2Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("mux_b_2_op"));
        graph.addOperand(muxB2Op, graph.findValue("hit_2"));
        graph.addOperand(muxB2Op, fieldBReads[2]);
        graph.addOperand(muxB2Op, fieldBReads[3]);
        graph.addResult(muxB2Op, muxB2);

        const ValueId muxB1 = makeLogicValue(graph, "mux_b_1", 1);
        const OperationId muxB1Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("mux_b_1_op"));
        graph.addOperand(muxB1Op, graph.findValue("hit_1"));
        graph.addOperand(muxB1Op, fieldBReads[1]);
        graph.addOperand(muxB1Op, muxB2);
        graph.addResult(muxB1Op, muxB1);

        const ValueId outB = makeLogicValue(graph, "out_b", 1);
        const OperationId muxB0Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("mux_b_0_op"));
        graph.addOperand(muxB0Op, graph.findValue("hit_0"));
        graph.addOperand(muxB0Op, fieldBReads[0]);
        graph.addOperand(muxB0Op, muxB1);
        graph.addResult(muxB0Op, outB);
        graph.bindOutputPort("out_b", outB);

        return design;
    }

    Design buildConcatDynamicRecordDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId data = makeLogicValue(graph, "data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("data", data);

        const ValueId mask = addConstant(graph, "mask_op", "mask", 8, "8'hff");
        const ValueId shift3 = addConstant(graph, "shift3_op", "shift3", 3, "3'd3");
        const ValueId c0 = addConstant(graph, "c0_op", "c0", 2, "2'd0");
        const ValueId c1 = addConstant(graph, "c1_op", "c1", 2, "2'd1");
        const ValueId c2 = addConstant(graph, "c2_op", "c2", 2, "2'd2");
        const ValueId c3 = addConstant(graph, "c3_op", "c3", 2, "2'd3");

        std::vector<ValueId> reads;
        for (int slot = 0; slot < 4; ++slot)
        {
            const std::string regName = "lane_" + std::to_string(slot) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "lane_q_" + std::to_string(slot), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("lane_read_" + std::to_string(slot)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            reads.push_back(q);

            ValueId slotConst = c0;
            if (slot == 1)
            {
                slotConst = c1;
            }
            else if (slot == 2)
            {
                slotConst = c2;
            }
            else if (slot == 3)
            {
                slotConst = c3;
            }

            const ValueId hit = makeLogicValue(graph, "lane_hit_" + std::to_string(slot), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("lane_eq_" + std::to_string(slot)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, slotConst);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "lane_point_cond_" + std::to_string(slot), 1);
            const OperationId andOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("lane_point_and_" + std::to_string(slot)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("lane_write_" + std::to_string(slot)));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, pointCond);
            graph.addOperand(write, data);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        const ValueId packed = makeLogicValue(graph, "packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("packed_concat"));
        graph.addOperand(concat, reads[3]);
        graph.addOperand(concat, reads[2]);
        graph.addOperand(concat, reads[1]);
        graph.addOperand(concat, reads[0]);
        graph.addResult(concat, packed);

        const ValueId bitAddr = makeLogicValue(graph, "bit_addr", 5);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitAddr);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("packed_slice_dyn"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, bitAddr);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        return design;
    }

    Design buildMultiPointWriteRecordDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wr0 = makeLogicValue(graph, "wr0", 1);
        const ValueId wr1 = makeLogicValue(graph, "wr1", 1);
        const ValueId idx0 = makeLogicValue(graph, "idx0", 2);
        const ValueId idx1 = makeLogicValue(graph, "idx1", 2);
        const ValueId dataA0 = makeLogicValue(graph, "data_a0", 8);
        const ValueId dataA1 = makeLogicValue(graph, "data_a1", 8);
        const ValueId dataB0 = makeLogicValue(graph, "data_b0", 1);
        const ValueId dataB1 = makeLogicValue(graph, "data_b1", 1);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr0", wr0);
        graph.bindInputPort("wr1", wr1);
        graph.bindInputPort("idx0", idx0);
        graph.bindInputPort("idx1", idx1);
        graph.bindInputPort("data_a0", dataA0);
        graph.bindInputPort("data_a1", dataA1);
        graph.bindInputPort("data_b0", dataB0);
        graph.bindInputPort("data_b1", dataB1);

        const ValueId maskA = addConstant(graph, "mp_mask_a_op", "mp_mask_a", 8, "8'hff");
        const ValueId maskB = addConstant(graph, "mp_mask_b_op", "mp_mask_b", 1, "1'b1");
        const ValueId c0 = addConstant(graph, "mp_c0_op", "mp_c0", 2, "2'd0");
        const ValueId c1 = addConstant(graph, "mp_c1_op", "mp_c1", 2, "2'd1");
        const ValueId c2 = addConstant(graph, "mp_c2_op", "mp_c2", 2, "2'd2");
        const ValueId c3 = addConstant(graph, "mp_c3_op", "mp_c3", 2, "2'd3");

        std::vector<ValueId> fieldAReads;
        std::vector<ValueId> fieldBReads;
        for (int slot = 0; slot < 4; ++slot)
        {
            const std::string regAName = "multi_slot_" + std::to_string(slot) + "_field_a";
            const std::string regBName = "multi_slot_" + std::to_string(slot) + "_field_b";

            const OperationId regA = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regAName));
            graph.setAttr(regA, "width", static_cast<int64_t>(8));
            graph.setAttr(regA, "isSigned", false);
            graph.setAttr(regA, "initValue", std::string("8'h00"));

            const OperationId regB = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regBName));
            graph.setAttr(regB, "width", static_cast<int64_t>(1));
            graph.setAttr(regB, "isSigned", false);
            graph.setAttr(regB, "initValue", std::string("1'b0"));

            const ValueId qA = makeLogicValue(graph, "multi_q_a_" + std::to_string(slot), 8);
            const OperationId readA = graph.createOperation(OperationKind::kRegisterReadPort,
                                                            graph.internSymbol("multi_read_a_" + std::to_string(slot)));
            graph.setAttr(readA, "regSymbol", regAName);
            graph.addResult(readA, qA);
            fieldAReads.push_back(qA);

            const ValueId qB = makeLogicValue(graph, "multi_q_b_" + std::to_string(slot), 1);
            const OperationId readB = graph.createOperation(OperationKind::kRegisterReadPort,
                                                            graph.internSymbol("multi_read_b_" + std::to_string(slot)));
            graph.setAttr(readB, "regSymbol", regBName);
            graph.addResult(readB, qB);
            fieldBReads.push_back(qB);

            ValueId slotConst = c0;
            if (slot == 1)
            {
                slotConst = c1;
            }
            else if (slot == 2)
            {
                slotConst = c2;
            }
            else if (slot == 3)
            {
                slotConst = c3;
            }

            const ValueId hit0 = makeLogicValue(graph, "multi_hit0_" + std::to_string(slot), 1);
            const OperationId eq0 = graph.createOperation(OperationKind::kEq, graph.internSymbol("multi_eq0_" + std::to_string(slot)));
            graph.addOperand(eq0, idx0);
            graph.addOperand(eq0, slotConst);
            graph.addResult(eq0, hit0);

            const ValueId hit1 = makeLogicValue(graph, "multi_hit1_" + std::to_string(slot), 1);
            const OperationId eq1 = graph.createOperation(OperationKind::kEq, graph.internSymbol("multi_eq1_" + std::to_string(slot)));
            graph.addOperand(eq1, idx1);
            graph.addOperand(eq1, slotConst);
            graph.addResult(eq1, hit1);

            const ValueId pointCond0 = makeLogicValue(graph, "multi_point_cond0_" + std::to_string(slot), 1);
            const OperationId and0 =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("multi_and0_" + std::to_string(slot)));
            graph.addOperand(and0, wr0);
            graph.addOperand(and0, hit0);
            graph.addResult(and0, pointCond0);

            const ValueId pointCond1 = makeLogicValue(graph, "multi_point_cond1_" + std::to_string(slot), 1);
            const OperationId and1 =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("multi_and1_" + std::to_string(slot)));
            graph.addOperand(and1, wr1);
            graph.addOperand(and1, hit1);
            graph.addResult(and1, pointCond1);

            const ValueId writeCond = makeLogicValue(graph, "multi_write_cond_" + std::to_string(slot), 1);
            const OperationId writeCondOp =
                graph.createOperation(OperationKind::kLogicOr, graph.internSymbol("multi_write_cond_op_" + std::to_string(slot)));
            graph.addOperand(writeCondOp, pointCond0);
            graph.addOperand(writeCondOp, pointCond1);
            graph.addResult(writeCondOp, writeCond);

            const ValueId nextA1 = makeLogicValue(graph, "multi_next_a1_" + std::to_string(slot), 8);
            const OperationId nextA1Op =
                graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_next_a1_op_" + std::to_string(slot)));
            graph.addOperand(nextA1Op, pointCond1);
            graph.addOperand(nextA1Op, dataA1);
            graph.addOperand(nextA1Op, qA);
            graph.addResult(nextA1Op, nextA1);

            const ValueId nextA = makeLogicValue(graph, "multi_next_a_" + std::to_string(slot), 8);
            const OperationId nextAOp =
                graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_next_a_op_" + std::to_string(slot)));
            graph.addOperand(nextAOp, pointCond0);
            graph.addOperand(nextAOp, dataA0);
            graph.addOperand(nextAOp, nextA1);
            graph.addResult(nextAOp, nextA);

            const ValueId nextB1 = makeLogicValue(graph, "multi_next_b1_" + std::to_string(slot), 1);
            const OperationId nextB1Op =
                graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_next_b1_op_" + std::to_string(slot)));
            graph.addOperand(nextB1Op, pointCond1);
            graph.addOperand(nextB1Op, dataB1);
            graph.addOperand(nextB1Op, qB);
            graph.addResult(nextB1Op, nextB1);

            const ValueId nextB = makeLogicValue(graph, "multi_next_b_" + std::to_string(slot), 1);
            const OperationId nextBOp =
                graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_next_b_op_" + std::to_string(slot)));
            graph.addOperand(nextBOp, pointCond0);
            graph.addOperand(nextBOp, dataB0);
            graph.addOperand(nextBOp, nextB1);
            graph.addResult(nextBOp, nextB);

            const OperationId writeA = graph.createOperation(OperationKind::kRegisterWritePort,
                                                             graph.internSymbol("multi_write_a_" + std::to_string(slot)));
            graph.setAttr(writeA, "regSymbol", regAName);
            graph.setAttr(writeA, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(writeA, writeCond);
            graph.addOperand(writeA, nextA);
            graph.addOperand(writeA, maskA);
            graph.addOperand(writeA, clk);

            const OperationId writeB = graph.createOperation(OperationKind::kRegisterWritePort,
                                                             graph.internSymbol("multi_write_b_" + std::to_string(slot)));
            graph.setAttr(writeB, "regSymbol", regBName);
            graph.setAttr(writeB, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(writeB, writeCond);
            graph.addOperand(writeB, nextB);
            graph.addOperand(writeB, maskB);
            graph.addOperand(writeB, clk);
        }

        const ValueId muxA2 = makeLogicValue(graph, "multi_mux_a_2", 8);
        const OperationId muxA2Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_mux_a_2_op"));
        graph.addOperand(muxA2Op, graph.findValue("multi_hit0_2"));
        graph.addOperand(muxA2Op, fieldAReads[2]);
        graph.addOperand(muxA2Op, fieldAReads[3]);
        graph.addResult(muxA2Op, muxA2);

        const ValueId muxA1 = makeLogicValue(graph, "multi_mux_a_1", 8);
        const OperationId muxA1Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_mux_a_1_op"));
        graph.addOperand(muxA1Op, graph.findValue("multi_hit0_1"));
        graph.addOperand(muxA1Op, fieldAReads[1]);
        graph.addOperand(muxA1Op, muxA2);
        graph.addResult(muxA1Op, muxA1);

        const ValueId outA = makeLogicValue(graph, "multi_out_a", 8);
        const OperationId muxA0Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_mux_a_0_op"));
        graph.addOperand(muxA0Op, graph.findValue("multi_hit0_0"));
        graph.addOperand(muxA0Op, fieldAReads[0]);
        graph.addOperand(muxA0Op, muxA1);
        graph.addResult(muxA0Op, outA);
        graph.bindOutputPort("multi_out_a", outA);

        const ValueId muxB2 = makeLogicValue(graph, "multi_mux_b_2", 1);
        const OperationId muxB2Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_mux_b_2_op"));
        graph.addOperand(muxB2Op, graph.findValue("multi_hit0_2"));
        graph.addOperand(muxB2Op, fieldBReads[2]);
        graph.addOperand(muxB2Op, fieldBReads[3]);
        graph.addResult(muxB2Op, muxB2);

        const ValueId muxB1 = makeLogicValue(graph, "multi_mux_b_1", 1);
        const OperationId muxB1Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_mux_b_1_op"));
        graph.addOperand(muxB1Op, graph.findValue("multi_hit0_1"));
        graph.addOperand(muxB1Op, fieldBReads[1]);
        graph.addOperand(muxB1Op, muxB2);
        graph.addResult(muxB1Op, muxB1);

        const ValueId outB = makeLogicValue(graph, "multi_out_b", 1);
        const OperationId muxB0Op = graph.createOperation(OperationKind::kMux, graph.internSymbol("multi_mux_b_0_op"));
        graph.addOperand(muxB0Op, graph.findValue("multi_hit0_0"));
        graph.addOperand(muxB0Op, fieldBReads[0]);
        graph.addOperand(muxB0Op, muxB1);
        graph.addResult(muxB0Op, outB);
        graph.bindOutputPort("multi_out_b", outB);

        return design;
    }

} // namespace

int main()
{
    {
        Design design = buildSlotRecordDesign();
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing graph");
        }

        PassManager manager;
        manager.addPass(std::make_unique<RecordSlotRepackPass>());
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            std::string summary;
            for (const auto &diag : diags.messages())
            {
                if (!summary.empty())
                {
                    summary += " | ";
                }
                summary += diag.passName + ": " + diag.message;
            }
            return fail("record-slot-repack should succeed: " + summary);
        }
        if (!result.changed)
        {
            return fail("record-slot-repack should rewrite the slot-record family");
        }

        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0 ||
            countOps(*graph, OperationKind::kMux) != 0)
        {
            return fail("slot-record repack should remove scalar storage/mux nodes");
        }

        if (countOps(*graph, OperationKind::kMemory) != 2 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 2 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 2 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 2)
        {
            return fail("slot-record repack should create two memories with matching read/write/fill ports");
        }

        std::vector<std::string> memoryGroupIds;
        std::size_t writesFromA = 0;
        std::size_t writesFromB = 0;
        std::size_t fillsFromA = 0;
        std::size_t fillsFromB = 0;
        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (op.kind() == OperationKind::kMemory)
            {
                if (getAttr<int64_t>(op, "row").value_or(0) != 4)
                {
                    return fail("record memory should have 4 rows");
                }
                const auto groupId = getAttr<std::string>(op, "recordGroupId");
                if (!groupId || groupId->empty())
                {
                    return fail("record memory should carry recordGroupId");
                }
                memoryGroupIds.push_back(*groupId);
            }
            else if (op.kind() == OperationKind::kMemoryWritePort)
            {
                if (op.operands().size() != 5 ||
                    op.operands()[0] != graph->inputPortValue("wr_en") ||
                    op.operands()[1] != graph->inputPortValue("idx"))
                {
                    return fail("memory write should normalize to wr_en + idx + data + mask + clk");
                }
                if (op.operands()[2] == graph->inputPortValue("data_a"))
                {
                    ++writesFromA;
                }
                if (op.operands()[2] == graph->inputPortValue("data_b"))
                {
                    ++writesFromB;
                }
            }
            else if (op.kind() == OperationKind::kMemoryFillPort)
            {
                if (op.operands().size() != 3 || op.operands()[0] != graph->inputPortValue("fill_en"))
                {
                    return fail("memory fill should preserve fill_en + fill_data + clk");
                }
                if (op.operands()[1] == graph->inputPortValue("fill_a"))
                {
                    ++fillsFromA;
                }
                if (op.operands()[1] == graph->inputPortValue("fill_b"))
                {
                    ++fillsFromB;
                }
            }
        }

        if (memoryGroupIds.size() != 2 || memoryGroupIds[0] != memoryGroupIds[1])
        {
            return fail("both field memories should share the same recordGroupId");
        }
        if (writesFromA != 1 || writesFromB != 1 || fillsFromA != 1 || fillsFromB != 1)
        {
            return fail("repacked memories should preserve per-field point-write and fill families");
        }

        const ValueId outA = graph->outputPortValue("out_a");
        const ValueId outB = graph->outputPortValue("out_b");
        if (!outA.valid() || !outB.valid())
        {
            return fail("outputs should remain bound");
        }
        const OperationId outADef = graph->valueDef(outA);
        const OperationId outBDef = graph->valueDef(outB);
        if (!outADef.valid() || !outBDef.valid())
        {
            return fail("outputs should be driven by memory reads");
        }
        const Operation outAOp = graph->getOperation(outADef);
        const Operation outBOp = graph->getOperation(outBDef);
        if (outAOp.kind() != OperationKind::kMemoryReadPort ||
            outBOp.kind() != OperationKind::kMemoryReadPort ||
            outAOp.operands().size() != 1 ||
            outBOp.operands().size() != 1 ||
            outAOp.operands()[0] != graph->inputPortValue("idx") ||
            outBOp.operands()[0] != graph->inputPortValue("idx"))
        {
            return fail("outputs should be normalized to memory reads on idx");
        }
    }

    {
        Design design = buildConcatDynamicRecordDesign();
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing concat graph");
        }

        PassManager manager;
        manager.addPass(std::make_unique<RecordSlotRepackPass>());
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            std::string summary;
            for (const auto &diag : diags.messages())
            {
                if (!summary.empty())
                {
                    summary += " | ";
                }
                summary += diag.passName + ": " + diag.message;
            }
            return fail("concat dynamic record case should succeed: " + summary);
        }
        if (!result.changed)
        {
            return fail("concat dynamic record case should be rewritten");
        }
        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0 ||
            countOps(*graph, OperationKind::kConcat) != 0 ||
            countOps(*graph, OperationKind::kSliceDynamic) != 0)
        {
            return fail("concat dynamic record case should remove scalar storage/concat/slice nodes");
        }
        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 1)
        {
            return fail("concat dynamic record case should create one memory/read/write");
        }
        const ValueId out = graph->outputPortValue("out");
        if (!out.valid())
        {
            return fail("concat dynamic output missing");
        }
        const OperationId outDef = graph->valueDef(out);
        if (!outDef.valid() || graph->getOperation(outDef).kind() != OperationKind::kMemoryReadPort)
        {
            return fail("concat dynamic output should be driven by memory read");
        }
    }

    {
        Design design = buildMultiPointWriteRecordDesign();
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing multi-point graph");
        }

        PassManager manager;
        manager.addPass(std::make_unique<RecordSlotRepackPass>());
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            std::string summary;
            for (const auto &diag : diags.messages())
            {
                if (!summary.empty())
                {
                    summary += " | ";
                }
                summary += diag.passName + ": " + diag.message;
            }
            return fail("multi-point write record case should succeed: " + summary);
        }
        if (!result.changed)
        {
            return fail("multi-point write record case should be rewritten");
        }

        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0)
        {
            return fail("multi-point write record case should remove scalar storage");
        }
        if (countOps(*graph, OperationKind::kMemory) != 2 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 2 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 4 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("multi-point write record case should create two memories with two write families each");
        }

        std::size_t writeA0 = 0;
        std::size_t writeA1 = 0;
        std::size_t writeB0 = 0;
        std::size_t writeB1 = 0;
        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (op.kind() != OperationKind::kMemoryWritePort)
            {
                continue;
            }
            if (op.operands().size() != 5)
            {
                return fail("multi-point memory write should preserve cond + addr + data + mask + clk");
            }
            const ValueId cond = op.operands()[0];
            const ValueId addr = op.operands()[1];
            const ValueId data = op.operands()[2];
            if (cond == graph->inputPortValue("wr0") && addr == graph->inputPortValue("idx0"))
            {
                if (data == graph->inputPortValue("data_a0"))
                {
                    ++writeA0;
                }
                if (data == graph->inputPortValue("data_b0"))
                {
                    ++writeB0;
                }
            }
            if (cond == graph->inputPortValue("wr1") && addr == graph->inputPortValue("idx1"))
            {
                if (data == graph->inputPortValue("data_a1"))
                {
                    ++writeA1;
                }
                if (data == graph->inputPortValue("data_b1"))
                {
                    ++writeB1;
                }
            }
        }

        if (writeA0 != 1 || writeA1 != 1 || writeB0 != 1 || writeB1 != 1)
        {
            return fail("multi-point write record case should preserve both point-write families per field");
        }
    }

    return 0;
}
