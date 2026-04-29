#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/scalar_memory_pack.hpp"

#include <array>
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
        std::cerr << "[transform-scalar-memory-pack] " << message << '\n';
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

    OperationId findSingleOp(const Graph &graph, OperationKind kind)
    {
        OperationId found = OperationId::invalid();
        for (const OperationId opId : graph.operations())
        {
            if (graph.getOperation(opId).kind() != kind)
            {
                continue;
            }
            if (found.valid())
            {
                return OperationId::invalid();
            }
            found = opId;
        }
        return found;
    }

    Design buildDesign()
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
        const ValueId c0 = addConstant(graph, "c0_op", "c0", 2, "2'd0");
        const ValueId c1 = addConstant(graph, "c1_op", "c1", 2, "2'd1");
        const ValueId c2 = addConstant(graph, "c2_op", "c2", 2, "2'd2");
        const ValueId c3 = addConstant(graph, "c3_op", "c3", 2, "2'd3");

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        const std::vector<std::string> initLiterals{"8'h11", "8'h22", "8'h33", "8'h44"};
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "arr_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", initLiterals[static_cast<std::size_t>(i)]);

            const ValueId q = makeLogicValue(graph, "arr_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("arr_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("packed_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("packed_idx"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idx);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        const std::array<ValueId, 4> indices{c0, c1, c2, c3};
        for (int i = 0; i < 4; ++i)
        {
            const ValueId hit = makeLogicValue(graph, "hit_" + std::to_string(i), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("eq_" + std::to_string(i)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "point_cond_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const std::string regName = "arr_" + std::to_string(i) + "_value";
            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("point_write_" + std::to_string(i)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("fill_write_" + std::to_string(i)));
            graph.setAttr(fillWrite, "regSymbol", regName);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    Design buildDynamicTreeDesign()
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

        const ValueId shift3 = addConstant(graph, "shift3_op", "shift3", 3, "3'd3");
        const ValueId mask = addConstant(graph, "mask_op", "mask", 8, "8'hff");
        const ValueId c0 = addConstant(graph, "c0_op", "c0", 2, "2'd0");
        const ValueId c1 = addConstant(graph, "c1_op", "c1", 2, "2'd1");
        const ValueId c2 = addConstant(graph, "c2_op", "c2", 2, "2'd2");
        const ValueId c3 = addConstant(graph, "c3_op", "c3", 2, "2'd3");

        std::vector<ValueId> readValues;
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "tree_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "tree_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("tree_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId lo = makeLogicValue(graph, "lo", 16);
        const OperationId loCat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("lo_cat"));
        graph.addOperand(loCat, readValues[1]);
        graph.addOperand(loCat, readValues[0]);
        graph.addResult(loCat, lo);

        const ValueId hi = makeLogicValue(graph, "hi", 16);
        const OperationId hiCat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("hi_cat"));
        graph.addOperand(hiCat, readValues[3]);
        graph.addOperand(hiCat, readValues[2]);
        graph.addResult(hiCat, hi);

        const ValueId packed = makeLogicValue(graph, "packed", 32);
        const OperationId rootCat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("root_cat"));
        graph.addOperand(rootCat, hi);
        graph.addOperand(rootCat, lo);
        graph.addResult(rootCat, packed);

        const ValueId bitAddr = makeLogicValue(graph, "bit_addr", 5);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitAddr);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("tree_slice_dyn"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, bitAddr);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        const std::array<ValueId, 4> indices{c0, c1, c2, c3};
        for (int i = 0; i < 4; ++i)
        {
            const ValueId hit = makeLogicValue(graph, "hit_" + std::to_string(i), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("eq_" + std::to_string(i)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "point_cond_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const std::string regName = "tree_" + std::to_string(i) + "_value";
            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("point_write_" + std::to_string(i)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("fill_write_" + std::to_string(i)));
            graph.setAttr(fillWrite, "regSymbol", regName);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    Design buildOffsetDynamicDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 3);
        const ValueId data = makeLogicValue(graph, "data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("data", data);

        const ValueId shift3 = addConstant(graph, "shift3_op", "shift3", 3, "3'd3");
        const ValueId add8 = addConstant(graph, "add8_op", "add8", 6, "6'd8");
        const ValueId mask = addConstant(graph, "mask_op", "mask", 8, "8'hff");
        const std::array<ValueId, 5> indices{
            addConstant(graph, "c0_op", "c0", 3, "3'd0"),
            addConstant(graph, "c1_op", "c1", 3, "3'd1"),
            addConstant(graph, "c2_op", "c2", 3, "3'd2"),
            addConstant(graph, "c3_op", "c3", 3, "3'd3"),
            addConstant(graph, "c4_op", "c4", 3, "3'd4")};

        std::vector<ValueId> readValues;
        for (int i = 0; i < 5; ++i)
        {
            const std::string regName = "off_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "off_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("off_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "packed", 40);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("packed_concat"));
        for (int i = 4; i >= 0; --i)
        {
            graph.addOperand(concat, readValues[static_cast<std::size_t>(i)]);
        }
        graph.addResult(concat, packed);

        const ValueId bitBase = makeLogicValue(graph, "bit_base", 6);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitBase);

        const ValueId bitAddr = makeLogicValue(graph, "bit_addr", 6);
        const OperationId add = graph.createOperation(OperationKind::kAdd, graph.internSymbol("bit_addr_add"));
        graph.addOperand(add, bitBase);
        graph.addOperand(add, add8);
        graph.addResult(add, bitAddr);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("off_slice_dyn"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, bitAddr);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 5; ++i)
        {
            const ValueId hit = makeLogicValue(graph, "hit_" + std::to_string(i), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("eq_" + std::to_string(i)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "point_cond_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const std::string regName = "off_" + std::to_string(i) + "_value";
            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("point_write_" + std::to_string(i)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("fill_write_" + std::to_string(i)));
            graph.setAttr(fillWrite, "regSymbol", regName);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    Design buildTruncatedDynamicDesign()
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

        const ValueId shift3 = addConstant(graph, "trunc_shift3_op", "trunc_shift3", 3, "3'd3");
        const ValueId mask = addConstant(graph, "trunc_mask_op", "trunc_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "trunc_c0_op", "trunc_c0", 2, "2'd0"),
            addConstant(graph, "trunc_c1_op", "trunc_c1", 2, "2'd1"),
            addConstant(graph, "trunc_c2_op", "trunc_c2", 2, "2'd2"),
            addConstant(graph, "trunc_c3_op", "trunc_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "trunc_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "trunc_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("trunc_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "trunc_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("trunc_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId bitAddrWide = makeLogicValue(graph, "trunc_bit_addr_wide", 6);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("trunc_bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitAddrWide);

        const ValueId bitAddr = makeLogicValue(graph, "trunc_bit_addr", 5);
        const OperationId lowSlice = graph.createOperation(OperationKind::kSliceStatic,
                                                           graph.internSymbol("trunc_bit_addr_lo"));
        graph.addOperand(lowSlice, bitAddrWide);
        graph.addResult(lowSlice, bitAddr);
        graph.setAttr(lowSlice, "sliceStart", static_cast<int64_t>(0));
        graph.setAttr(lowSlice, "sliceEnd", static_cast<int64_t>(4));

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("trunc_slice_dyn"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, bitAddr);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const ValueId hit = makeLogicValue(graph, "trunc_hit_" + std::to_string(i), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("trunc_eq_" + std::to_string(i)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "trunc_point_cond_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("trunc_point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const std::string regName = "trunc_" + std::to_string(i) + "_value";
            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("trunc_point_write_" + std::to_string(i)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("trunc_fill_write_" + std::to_string(i)));
            graph.setAttr(fillWrite, "regSymbol", regName);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    Design buildMergedWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId fillData = makeLogicValue(graph, "fill_data", 8);
        const ValueId pointData = makeLogicValue(graph, "point_data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("fill_data", fillData);
        graph.bindInputPort("point_data", pointData);

        const ValueId shift3 = addConstant(graph, "merged_shift3_op", "merged_shift3", 3, "3'd3");
        const ValueId mask = addConstant(graph, "merged_mask_op", "merged_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "merged_c0_op", "merged_c0", 2, "2'd0"),
            addConstant(graph, "merged_c1_op", "merged_c1", 2, "2'd1"),
            addConstant(graph, "merged_c2_op", "merged_c2", 2, "2'd2"),
            addConstant(graph, "merged_c3_op", "merged_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "laneBank_" + std::to_string(i);
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "lane_bank_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("lane_bank_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "merged_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("merged_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId arrayOut = makeLogicValue(graph, "array_out", 8);
        const OperationId arraySlice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("merged_slice_array"));
        graph.addOperand(arraySlice, packed);
        graph.addOperand(arraySlice, idx);
        graph.addResult(arraySlice, arrayOut);
        graph.setAttr(arraySlice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("array_out", arrayOut);

        const ValueId bitAddr = makeLogicValue(graph, "merged_bit_addr", 5);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("merged_bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitAddr);

        const ValueId dynOut = makeLogicValue(graph, "dyn_out", 8);
        const OperationId dynSlice = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("merged_slice_dyn"));
        graph.addOperand(dynSlice, packed);
        graph.addOperand(dynSlice, bitAddr);
        graph.addResult(dynSlice, dynOut);
        graph.setAttr(dynSlice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("dyn_out", dynOut);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId hit = makeLogicValue(graph, "merged_hit_" + suffix, 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("merged_eq_" + suffix));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointGate = makeLogicValue(graph, "merged_point_gate_" + suffix, 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("merged_point_and_" + suffix));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointGate);

            const ValueId mergedCond = makeLogicValue(graph, "merged_cond_" + suffix, 1);
            const OperationId orOp = graph.createOperation(OperationKind::kLogicOr,
                                                           graph.internSymbol("merged_or_" + suffix));
            graph.addOperand(orOp, fillEn);
            graph.addOperand(orOp, pointGate);
            graph.addResult(orOp, mergedCond);

            const ValueId mergedData = makeLogicValue(graph, "merged_data_" + suffix, 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("merged_mux_" + suffix));
            graph.addOperand(mux, pointGate);
            graph.addOperand(mux, pointData);
            graph.addOperand(mux, fillData);
            graph.addResult(mux, mergedData);

            const std::string regName = "laneBank_" + suffix;
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("merged_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, mergedCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildReduceAndMaxIndexDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId fillData = makeLogicValue(graph, "fill_data", 8);
        const ValueId pointData = makeLogicValue(graph, "point_data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("fill_data", fillData);
        graph.bindInputPort("point_data", pointData);

        const ValueId shift3 = addConstant(graph, "reduce_and_shift3_op", "reduce_and_shift3", 3, "3'd3");
        const ValueId mask = addConstant(graph, "reduce_and_mask_op", "reduce_and_mask", 8, "8'hff");
        const std::array<ValueId, 3> indices{
            addConstant(graph, "reduce_and_c0_op", "reduce_and_c0", 2, "2'd0"),
            addConstant(graph, "reduce_and_c1_op", "reduce_and_c1", 2, "2'd1"),
            addConstant(graph, "reduce_and_c2_op", "reduce_and_c2", 2, "2'd2")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "reduce_and_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "reduce_and_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("reduce_and_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "reduce_and_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("reduce_and_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId bitAddr = makeLogicValue(graph, "reduce_and_bit_addr", 5);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("reduce_and_bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitAddr);

        const ValueId dynOut = makeLogicValue(graph, "dyn_out", 8);
        const OperationId dynSlice = graph.createOperation(OperationKind::kSliceDynamic,
                                                           graph.internSymbol("reduce_and_slice_dyn"));
        graph.addOperand(dynSlice, packed);
        graph.addOperand(dynSlice, bitAddr);
        graph.addResult(dynSlice, dynOut);
        graph.setAttr(dynSlice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("dyn_out", dynOut);

        const ValueId maxHit = makeLogicValue(graph, "reduce_and_hit_max", 1);
        const OperationId maxReduceAnd =
            graph.createOperation(OperationKind::kReduceAnd, graph.internSymbol("reduce_and_max_hit"));
        graph.addOperand(maxReduceAnd, idx);
        graph.addResult(maxReduceAnd, maxHit);

        for (int i = 0; i < 4; ++i)
        {
            ValueId hit = maxHit;
            if (i < 3)
            {
                hit = makeLogicValue(graph, "reduce_and_hit_" + std::to_string(i), 1);
                const OperationId eq = graph.createOperation(OperationKind::kEq,
                                                             graph.internSymbol("reduce_and_eq_" + std::to_string(i)));
                graph.addOperand(eq, idx);
                graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
                graph.addResult(eq, hit);
            }

            const ValueId pointGate = makeLogicValue(graph, "reduce_and_point_gate_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("reduce_and_point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointGate);

            const ValueId mergedCond = makeLogicValue(graph, "reduce_and_cond_" + std::to_string(i), 1);
            const OperationId orOp = graph.createOperation(OperationKind::kLogicOr,
                                                           graph.internSymbol("reduce_and_or_" + std::to_string(i)));
            graph.addOperand(orOp, fillEn);
            graph.addOperand(orOp, pointGate);
            graph.addResult(orOp, mergedCond);

            const ValueId mergedData = makeLogicValue(graph, "reduce_and_data_" + std::to_string(i), 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("reduce_and_mux_" + std::to_string(i)));
            graph.addOperand(mux, pointGate);
            graph.addOperand(mux, pointData);
            graph.addOperand(mux, fillData);
            graph.addResult(mux, mergedData);

            const std::string regName = "reduce_and_" + std::to_string(i) + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("reduce_and_write_" + std::to_string(i)));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, mergedCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildMultiplePointWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEnA = makeLogicValue(graph, "wr_en_a", 1);
        const ValueId wrEnB = makeLogicValue(graph, "wr_en_b", 1);
        const ValueId idxA = makeLogicValue(graph, "idx_a", 2);
        const ValueId idxB = makeLogicValue(graph, "idx_b", 2);
        const ValueId dataA = makeLogicValue(graph, "data_a", 8);
        const ValueId dataB = makeLogicValue(graph, "data_b", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en_a", wrEnA);
        graph.bindInputPort("wr_en_b", wrEnB);
        graph.bindInputPort("idx_a", idxA);
        graph.bindInputPort("idx_b", idxB);
        graph.bindInputPort("data_a", dataA);
        graph.bindInputPort("data_b", dataB);

        const ValueId mask = addConstant(graph, "multi_mask_op", "multi_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "multi_c0_op", "multi_c0", 2, "2'd0"),
            addConstant(graph, "multi_c1_op", "multi_c1", 2, "2'd1"),
            addConstant(graph, "multi_c2_op", "multi_c2", 2, "2'd2"),
            addConstant(graph, "multi_c3_op", "multi_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "multi_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "multi_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("multi_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "multi_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("multi_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("multi_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idxA);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);

            const ValueId hitA = makeLogicValue(graph, "multi_hit_a_" + suffix, 1);
            const OperationId eqA = graph.createOperation(OperationKind::kEq, graph.internSymbol("multi_eq_a_" + suffix));
            graph.addOperand(eqA, idxA);
            graph.addOperand(eqA, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqA, hitA);

            const ValueId pointA = makeLogicValue(graph, "multi_point_a_" + suffix, 1);
            const OperationId andA = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("multi_and_a_" + suffix));
            graph.addOperand(andA, wrEnA);
            graph.addOperand(andA, hitA);
            graph.addResult(andA, pointA);

            const ValueId hitB = makeLogicValue(graph, "multi_hit_b_" + suffix, 1);
            const OperationId eqB = graph.createOperation(OperationKind::kEq, graph.internSymbol("multi_eq_b_" + suffix));
            graph.addOperand(eqB, idxB);
            graph.addOperand(eqB, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqB, hitB);

            const ValueId pointB = makeLogicValue(graph, "multi_point_b_" + suffix, 1);
            const OperationId andB = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("multi_and_b_" + suffix));
            graph.addOperand(andB, wrEnB);
            graph.addOperand(andB, hitB);
            graph.addResult(andB, pointB);

            const ValueId writeCond = makeLogicValue(graph, "multi_cond_" + suffix, 1);
            const OperationId orOp = graph.createOperation(OperationKind::kLogicOr,
                                                           graph.internSymbol("multi_or_" + suffix));
            graph.addOperand(orOp, pointA);
            graph.addOperand(orOp, pointB);
            graph.addResult(orOp, writeCond);

            const ValueId mergedData = makeLogicValue(graph, "multi_data_" + suffix, 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("multi_mux_" + suffix));
            graph.addOperand(mux, pointB);
            graph.addOperand(mux, dataB);
            graph.addOperand(mux, dataA);
            graph.addResult(mux, mergedData);

            const std::string regName = "multi_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("multi_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildTriplePointWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEnA = makeLogicValue(graph, "wr_en_a", 1);
        const ValueId wrEnB = makeLogicValue(graph, "wr_en_b", 1);
        const ValueId wrEnC = makeLogicValue(graph, "wr_en_c", 1);
        const ValueId idxA = makeLogicValue(graph, "idx_a", 2);
        const ValueId idxB = makeLogicValue(graph, "idx_b", 2);
        const ValueId idxC = makeLogicValue(graph, "idx_c", 2);
        const ValueId dataA = makeLogicValue(graph, "data_a", 8);
        const ValueId dataB = makeLogicValue(graph, "data_b", 8);
        const ValueId dataC = makeLogicValue(graph, "data_c", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en_a", wrEnA);
        graph.bindInputPort("wr_en_b", wrEnB);
        graph.bindInputPort("wr_en_c", wrEnC);
        graph.bindInputPort("idx_a", idxA);
        graph.bindInputPort("idx_b", idxB);
        graph.bindInputPort("idx_c", idxC);
        graph.bindInputPort("data_a", dataA);
        graph.bindInputPort("data_b", dataB);
        graph.bindInputPort("data_c", dataC);

        const ValueId mask = addConstant(graph, "triple_mask_op", "triple_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "triple_c0_op", "triple_c0", 2, "2'd0"),
            addConstant(graph, "triple_c1_op", "triple_c1", 2, "2'd1"),
            addConstant(graph, "triple_c2_op", "triple_c2", 2, "2'd2"),
            addConstant(graph, "triple_c3_op", "triple_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "triple_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "triple_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("triple_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "triple_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("triple_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("triple_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idxA);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);

            const ValueId hitA = makeLogicValue(graph, "triple_hit_a_" + suffix, 1);
            const OperationId eqA = graph.createOperation(OperationKind::kEq, graph.internSymbol("triple_eq_a_" + suffix));
            graph.addOperand(eqA, idxA);
            graph.addOperand(eqA, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqA, hitA);
            const ValueId pointA = makeLogicValue(graph, "triple_point_a_" + suffix, 1);
            const OperationId andA = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("triple_and_a_" + suffix));
            graph.addOperand(andA, wrEnA);
            graph.addOperand(andA, hitA);
            graph.addResult(andA, pointA);

            const ValueId hitB = makeLogicValue(graph, "triple_hit_b_" + suffix, 1);
            const OperationId eqB = graph.createOperation(OperationKind::kEq, graph.internSymbol("triple_eq_b_" + suffix));
            graph.addOperand(eqB, idxB);
            graph.addOperand(eqB, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqB, hitB);
            const ValueId pointB = makeLogicValue(graph, "triple_point_b_" + suffix, 1);
            const OperationId andB = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("triple_and_b_" + suffix));
            graph.addOperand(andB, wrEnB);
            graph.addOperand(andB, hitB);
            graph.addResult(andB, pointB);

            const ValueId hitC = makeLogicValue(graph, "triple_hit_c_" + suffix, 1);
            const OperationId eqC = graph.createOperation(OperationKind::kEq, graph.internSymbol("triple_eq_c_" + suffix));
            graph.addOperand(eqC, idxC);
            graph.addOperand(eqC, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqC, hitC);
            const ValueId pointC = makeLogicValue(graph, "triple_point_c_" + suffix, 1);
            const OperationId andC = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("triple_and_c_" + suffix));
            graph.addOperand(andC, wrEnC);
            graph.addOperand(andC, hitC);
            graph.addResult(andC, pointC);

            const ValueId orAB = makeLogicValue(graph, "triple_or_ab_" + suffix, 1);
            const OperationId orABOp = graph.createOperation(OperationKind::kLogicOr,
                                                             graph.internSymbol("triple_or_ab_op_" + suffix));
            graph.addOperand(orABOp, pointA);
            graph.addOperand(orABOp, pointB);
            graph.addResult(orABOp, orAB);

            const ValueId writeCond = makeLogicValue(graph, "triple_cond_" + suffix, 1);
            const OperationId orABCOp = graph.createOperation(OperationKind::kLogicOr,
                                                              graph.internSymbol("triple_or_abc_op_" + suffix));
            graph.addOperand(orABCOp, orAB);
            graph.addOperand(orABCOp, pointC);
            graph.addResult(orABCOp, writeCond);

            const ValueId tailData = makeLogicValue(graph, "triple_tail_data_" + suffix, 8);
            const OperationId tailMux = graph.createOperation(OperationKind::kMux,
                                                              graph.internSymbol("triple_tail_mux_" + suffix));
            graph.addOperand(tailMux, pointB);
            graph.addOperand(tailMux, dataB);
            graph.addOperand(tailMux, dataA);
            graph.addResult(tailMux, tailData);

            const ValueId mergedData = makeLogicValue(graph, "triple_data_" + suffix, 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("triple_mux_" + suffix));
            graph.addOperand(mux, pointC);
            graph.addOperand(mux, dataC);
            graph.addOperand(mux, tailData);
            graph.addResult(mux, mergedData);

            const std::string regName = "triple_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("triple_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    int verifyPackedShape(Design &design,
                          bool expectNoSliceDynamic,
                          int64_t expectedRows,
                          std::size_t expectedReadPorts = 1,
                          std::string_view primaryOutputName = "out")
    {
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing graph");
        }

        PassManager manager;
        manager.addPass(std::make_unique<ScalarMemoryPackPass>());
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            return fail("scalar-memory-pack pass should succeed");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the cluster");
        }

        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0 ||
            countOps(*graph, OperationKind::kConcat) != 0 ||
            countOps(*graph, OperationKind::kSliceArray) != 0 ||
            (expectNoSliceDynamic && countOps(*graph, OperationKind::kSliceDynamic) != 0))
        {
            return fail("scalarized storage/read/write nodes should be removed");
        }
        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != expectedReadPorts ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 1 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 1)
        {
            return fail("expected packed memory op counts after packing");
        }

        const OperationId memoryOpId = findSingleOp(*graph, OperationKind::kMemory);
        const OperationId writeOpId = findSingleOp(*graph, OperationKind::kMemoryWritePort);
        const OperationId fillOpId = findSingleOp(*graph, OperationKind::kMemoryFillPort);
        if (!memoryOpId.valid() || !writeOpId.valid() || !fillOpId.valid())
        {
            return fail("expected unique packed memory/write/fill ops");
        }

        const Operation memoryOp = graph->getOperation(memoryOpId);
        if (getAttr<int64_t>(memoryOp, "row").value_or(0) != expectedRows ||
            getAttr<int64_t>(memoryOp, "width").value_or(0) != 8)
        {
            return fail("packed memory shape mismatch");
        }

        const ValueId primaryOutValue = graph->outputPortValue(primaryOutputName);
        if (!primaryOutValue.valid())
        {
            return fail("primary output should exist");
        }
        const OperationId primaryReadOpId = graph->valueDef(primaryOutValue);
        if (!primaryReadOpId.valid())
        {
            return fail("primary output should be driven by an operation");
        }
        const Operation primaryReadOp = graph->getOperation(primaryReadOpId);
        if (primaryReadOp.kind() != OperationKind::kMemoryReadPort ||
            getAttr<std::string>(primaryReadOp, "memSymbol").value_or(std::string()).empty() ||
            primaryReadOp.operands().size() != 1)
        {
            return fail("primary output should be driven by a rewritten memory read");
        }

        const Operation writeOp = graph->getOperation(writeOpId);
        if (writeOp.operands().size() != 5 ||
            writeOp.operands()[0] != graph->inputPortValue("wr_en") ||
            writeOp.operands()[1] != graph->inputPortValue("idx"))
        {
            return fail("memory write should recover baseCond + dynamic addr");
        }

        const Operation fillOp = graph->getOperation(fillOpId);
        if (fillOp.operands().size() != 3 ||
            fillOp.operands()[0] != graph->inputPortValue("fill_en"))
        {
            return fail("memory fill should preserve fill cond");
        }

        return 0;
    }
} // namespace

int main()
{
    {
        Design design = buildDesign();
        if (const int status = verifyPackedShape(design, true, 4); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const OperationId memoryOpId = findSingleOp(*graph, OperationKind::kMemory);
        const Operation memoryOp = graph->getOperation(memoryOpId);
        const auto initKind = getAttr<std::vector<std::string>>(memoryOp, "initKind");
        const auto initStart = getAttr<std::vector<int64_t>>(memoryOp, "initStart");
        if (!initKind || !initStart || initKind->size() != 4 || initStart->size() != 4)
        {
            return fail("packed memory should preserve per-row register init");
        }
        const Operation readOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryReadPort));
        if (readOp.operands().size() != 1 || readOp.operands()[0] != graph->inputPortValue("idx"))
        {
            return fail("basic packed memory read should use the original dynamic index");
        }
    }

    {
        Design design = buildDynamicTreeDesign();
        if (const int status = verifyPackedShape(design, true, 4); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation readOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryReadPort));
        if (readOp.operands().size() != 1 || readOp.operands()[0] != graph->inputPortValue("idx"))
        {
            return fail("concat-tree + slice-dynamic packed memory read should normalize to the original index");
        }
    }

    {
        Design design = buildOffsetDynamicDesign();
        if (const int status = verifyPackedShape(design, true, 5); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation readOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryReadPort));
        if (readOp.operands().size() != 1)
        {
            return fail("offset packed memory read should have exactly one addr operand");
        }
        const OperationId addrDef = graph->valueDef(readOp.operands()[0]);
        if (!addrDef.valid() || graph->getOperation(addrDef).kind() != OperationKind::kAdd)
        {
            return fail("offset packed memory read should materialize idx + rowOffset");
        }
    }

    {
        Design design = buildTruncatedDynamicDesign();
        if (const int status = verifyPackedShape(design, true, 4); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation readOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryReadPort));
        if (readOp.operands().size() != 1 || readOp.operands()[0] != graph->inputPortValue("idx"))
        {
            return fail("low-bit slice wrapper around dynamic bit index should normalize to the original index");
        }
    }

    {
        Design design = buildMergedWriteDesign();
        if (const int status = verifyPackedShape(design, true, 4, 2, "array_out"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const ValueId dynOut = graph->outputPortValue("dyn_out");
        const OperationId dynReadOpId = graph->valueDef(dynOut);
        if (!dynReadOpId.valid() || graph->getOperation(dynReadOpId).kind() != OperationKind::kMemoryReadPort)
        {
            return fail("merged-write dynamic output should be rewritten to memory read");
        }
        const Operation dynReadOp = graph->getOperation(dynReadOpId);
        if (dynReadOp.operands().size() != 1 || dynReadOp.operands()[0] != graph->inputPortValue("idx"))
        {
            return fail("merged-write dynamic read should normalize back to idx");
        }

        const Operation writeOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryWritePort));
        if (writeOp.operands().size() != 5 || writeOp.operands()[2] != graph->inputPortValue("point_data"))
        {
            return fail("merged-write point update should preserve mux true-arm as memory write data");
        }
        const Operation fillOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryFillPort));
        if (fillOp.operands().size() != 3 || fillOp.operands()[1] != graph->inputPortValue("fill_data"))
        {
            return fail("merged-write fill should preserve mux false-arm as fill data");
        }
    }

    {
        Design design = buildReduceAndMaxIndexDesign();
        if (const int status = verifyPackedShape(design, true, 4, 1, "dyn_out"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation writeOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryWritePort));
        if (writeOp.operands().size() != 5 || writeOp.operands()[1] != graph->inputPortValue("idx"))
        {
            return fail("reduce-and max-index point update should still normalize to the original dynamic addr");
        }
    }

    {
        Design design = buildMultiplePointWriteDesign();
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing graph");
        }

        PassManager manager;
        manager.addPass(std::make_unique<ScalarMemoryPackPass>());
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            return fail("scalar-memory-pack pass should succeed on multiple-point writes");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the multiple-point cluster");
        }

        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0 ||
            countOps(*graph, OperationKind::kConcat) != 0 ||
            countOps(*graph, OperationKind::kSliceArray) != 0)
        {
            return fail("multiple-point packed cluster should remove scalar storage/read/write nodes");
        }

        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 2 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("multiple-point packed cluster should become one memory with two write ports");
        }

        std::size_t writesFromA = 0;
        std::size_t writesFromB = 0;
        for (const OperationId opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (op.kind() != OperationKind::kMemoryWritePort)
            {
                continue;
            }
            if (op.operands().size() != 5)
            {
                return fail("multiple-point memory write should preserve cond/addr/data/mask/clock operands");
            }
            if (op.operands()[0] == graph->inputPortValue("wr_en_a") &&
                op.operands()[1] == graph->inputPortValue("idx_a") &&
                op.operands()[2] == graph->inputPortValue("data_a"))
            {
                ++writesFromA;
            }
            if (op.operands()[0] == graph->inputPortValue("wr_en_b") &&
                op.operands()[1] == graph->inputPortValue("idx_b") &&
                op.operands()[2] == graph->inputPortValue("data_b"))
            {
                ++writesFromB;
            }
        }
        if (writesFromA != 1 || writesFromB != 1)
        {
            return fail("multiple-point memory writes should recover both point-update families");
        }
    }

    {
        Design design = buildTriplePointWriteDesign();
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing graph");
        }

        PassManager manager;
        manager.addPass(std::make_unique<ScalarMemoryPackPass>());
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        if (!result.success || diags.hasError())
        {
            return fail("scalar-memory-pack pass should succeed on triple-point writes");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the triple-point cluster");
        }

        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 3 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("triple-point packed cluster should become one memory with three write ports");
        }
    }

    return 0;
}
