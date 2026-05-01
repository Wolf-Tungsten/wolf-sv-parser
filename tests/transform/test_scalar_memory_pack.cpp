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

    std::string opCountSummary(const Graph &graph)
    {
        return "memory=" + std::to_string(countOps(graph, OperationKind::kMemory)) +
               " read=" + std::to_string(countOps(graph, OperationKind::kMemoryReadPort)) +
               " write=" + std::to_string(countOps(graph, OperationKind::kMemoryWritePort)) +
               " fill=" + std::to_string(countOps(graph, OperationKind::kMemoryFillPort)) +
               " reg=" + std::to_string(countOps(graph, OperationKind::kRegister)) +
               " regread=" + std::to_string(countOps(graph, OperationKind::kRegisterReadPort)) +
               " regwrite=" + std::to_string(countOps(graph, OperationKind::kRegisterWritePort)) +
               " concat=" + std::to_string(countOps(graph, OperationKind::kConcat)) +
               " slicearray=" + std::to_string(countOps(graph, OperationKind::kSliceArray)) +
               " slicedyn=" + std::to_string(countOps(graph, OperationKind::kSliceDynamic));
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

    Design buildDirectReadFamilyDesign()
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

        const ValueId mask = addConstant(graph, "direct_mask_op", "direct_mask", 8, "8'hff");
        const ValueId c0 = addConstant(graph, "direct_c0_op", "direct_c0", 2, "2'd0");
        const ValueId c1 = addConstant(graph, "direct_c1_op", "direct_c1", 2, "2'd1");
        const ValueId c2 = addConstant(graph, "direct_c2_op", "direct_c2", 2, "2'd2");
        const ValueId c3 = addConstant(graph, "direct_c3_op", "direct_c3", 2, "2'd3");
        const std::array<ValueId, 4> indices{c0, c1, c2, c3};

        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "arr_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);

            const ValueId q = makeLogicValue(graph, "direct_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("direct_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            graph.bindOutputPort("out_" + std::to_string(i), q);

            const ValueId hit = makeLogicValue(graph, "direct_hit_" + std::to_string(i), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq,
                                                         graph.internSymbol("direct_eq_" + std::to_string(i)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "direct_point_cond_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("direct_point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("direct_point_write_" +
                                                                                    std::to_string(i)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("direct_fill_write_" +
                                                                                   std::to_string(i)));
            graph.setAttr(fillWrite, "regSymbol", regName);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    std::optional<int64_t> constantValue(const Graph &graph, ValueId value)
    {
        const OperationId defOpId = graph.valueDef(value);
        if (!defOpId.valid())
        {
            return std::nullopt;
        }
        const Operation defOp = graph.getOperation(defOpId);
        if (defOp.kind() != OperationKind::kConstant)
        {
            return std::nullopt;
        }
        const auto literal = getAttr<std::string>(defOp, "constValue");
        if (!literal)
        {
            return std::nullopt;
        }
        const std::size_t tick = literal->find('d');
        if (tick == std::string::npos)
        {
            return std::nullopt;
        }
        return std::stoll(literal->substr(tick + 1));
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

    Design buildInternalGeneratedLikeDesign()
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

        const ValueId mask = addConstant(graph, "internal_mask_op", "internal_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "internal_c0_op", "internal_c0", 2, "2'd0"),
            addConstant(graph, "internal_c1_op", "internal_c1", 2, "2'd1"),
            addConstant(graph, "internal_c2_op", "internal_c2", 2, "2'd2"),
            addConstant(graph, "internal_c3_op", "internal_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "_reg_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "internal_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "internal_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("_packed_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("_packed_idx"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idx);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const ValueId hit = makeLogicValue(graph, "internal_hit_" + std::to_string(i), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq,
                                                         graph.internSymbol("_eq_" + std::to_string(i)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "internal_point_cond_" + std::to_string(i), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("_point_and_" + std::to_string(i)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const std::string regName = "_reg_" + std::to_string(i) + "_value";
            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("_point_write_" + std::to_string(i)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("_fill_write_" + std::to_string(i)));
            graph.setAttr(fillWrite, "regSymbol", regName);
            graph.setAttr(fillWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(fillWrite, fillEn);
            graph.addOperand(fillWrite, data);
            graph.addOperand(fillWrite, mask);
            graph.addOperand(fillWrite, clk);
        }

        return design;
    }

    Design buildShiftedIndexDesign()
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

        const ValueId shift3 = addConstant(graph, "shifted_shift3_op", "shifted_shift3", 3, "3'd3");
        const ValueId sub32 = addConstant(graph, "shifted_sub32_op", "shifted_sub32", 6, "6'd32");
        const ValueId mask = addConstant(graph, "shifted_mask_op", "shifted_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "shifted_c4_op", "shifted_c4", 3, "3'd4"),
            addConstant(graph, "shifted_c5_op", "shifted_c5", 3, "3'd5"),
            addConstant(graph, "shifted_c6_op", "shifted_c6", 3, "3'd6"),
            addConstant(graph, "shifted_c7_op", "shifted_c7", 3, "3'd7")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const int regIndex = i + 4;
            const std::string regName = "shifted_" + std::to_string(regIndex) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "shifted_q_" + std::to_string(regIndex), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("shifted_read_" + std::to_string(regIndex)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "shifted_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("shifted_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId bitBase = makeLogicValue(graph, "shifted_bit_base", 6);
        const OperationId shl = graph.createOperation(OperationKind::kShl, graph.internSymbol("shifted_bit_addr_shl"));
        graph.addOperand(shl, idx);
        graph.addOperand(shl, shift3);
        graph.addResult(shl, bitBase);

        const ValueId bitAddr = makeLogicValue(graph, "shifted_bit_addr", 6);
        const OperationId sub = graph.createOperation(OperationKind::kSub, graph.internSymbol("shifted_bit_addr_sub"));
        graph.addOperand(sub, bitBase);
        graph.addOperand(sub, sub32);
        graph.addResult(sub, bitAddr);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("shifted_slice_dyn"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, bitAddr);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const int regIndex = i + 4;
            const ValueId hit = makeLogicValue(graph, "shifted_hit_" + std::to_string(regIndex), 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq,
                                                         graph.internSymbol("shifted_eq_" + std::to_string(regIndex)));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "shifted_point_cond_" + std::to_string(regIndex), 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("shifted_point_and_" + std::to_string(regIndex)));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const std::string regName = "shifted_" + std::to_string(regIndex) + "_value";
            const OperationId pointWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                 graph.internSymbol("shifted_point_write_" + std::to_string(regIndex)));
            graph.setAttr(pointWrite, "regSymbol", regName);
            graph.setAttr(pointWrite, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(pointWrite, pointCond);
            graph.addOperand(pointWrite, data);
            graph.addOperand(pointWrite, mask);
            graph.addOperand(pointWrite, clk);

            const OperationId fillWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("shifted_fill_write_" + std::to_string(regIndex)));
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

    Design buildMultiplePointWriteWithHoldReadUsersDesign()
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

        const ValueId mask = addConstant(graph, "hold_mask_op", "hold_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "hold_c0_op", "hold_c0", 2, "2'd0"),
            addConstant(graph, "hold_c1_op", "hold_c1", 2, "2'd1"),
            addConstant(graph, "hold_c2_op", "hold_c2", 2, "2'd2"),
            addConstant(graph, "hold_c3_op", "hold_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "hold_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "hold_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("hold_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "hold_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("hold_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("hold_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idxA);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);

            const ValueId hitA = makeLogicValue(graph, "hold_hit_a_" + suffix, 1);
            const OperationId eqA = graph.createOperation(OperationKind::kEq, graph.internSymbol("hold_eq_a_" + suffix));
            graph.addOperand(eqA, idxA);
            graph.addOperand(eqA, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqA, hitA);

            const ValueId pointA = makeLogicValue(graph, "hold_point_a_" + suffix, 1);
            const OperationId andA = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("hold_and_a_" + suffix));
            graph.addOperand(andA, wrEnA);
            graph.addOperand(andA, hitA);
            graph.addResult(andA, pointA);

            const ValueId hitB = makeLogicValue(graph, "hold_hit_b_" + suffix, 1);
            const OperationId eqB = graph.createOperation(OperationKind::kEq, graph.internSymbol("hold_eq_b_" + suffix));
            graph.addOperand(eqB, idxB);
            graph.addOperand(eqB, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqB, hitB);

            const ValueId pointB = makeLogicValue(graph, "hold_point_b_" + suffix, 1);
            const OperationId andB = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("hold_and_b_" + suffix));
            graph.addOperand(andB, wrEnB);
            graph.addOperand(andB, hitB);
            graph.addResult(andB, pointB);

            const ValueId writeCond = makeLogicValue(graph, "hold_cond_" + suffix, 1);
            const OperationId orOp = graph.createOperation(OperationKind::kLogicOr,
                                                           graph.internSymbol("hold_or_" + suffix));
            graph.addOperand(orOp, pointA);
            graph.addOperand(orOp, pointB);
            graph.addResult(orOp, writeCond);

            const ValueId tailData = makeLogicValue(graph, "hold_tail_data_" + suffix, 8);
            const OperationId tailMux = graph.createOperation(OperationKind::kMux,
                                                              graph.internSymbol("hold_tail_mux_" + suffix));
            graph.addOperand(tailMux, pointB);
            graph.addOperand(tailMux, dataB);
            graph.addOperand(tailMux, readValues[static_cast<std::size_t>(i)]);
            graph.addResult(tailMux, tailData);

            const ValueId mergedData = makeLogicValue(graph, "hold_data_" + suffix, 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("hold_mux_" + suffix));
            graph.addOperand(mux, pointA);
            graph.addOperand(mux, dataA);
            graph.addOperand(mux, tailData);
            graph.addResult(mux, mergedData);

            const std::string regName = "hold_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("hold_write_" + suffix));
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

    Design buildLoweredTriplePointWriteDesign()
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

        const ValueId mask = addConstant(graph, "lowered_triple_mask_op", "lowered_triple_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "lowered_triple_c0_op", "lowered_triple_c0", 2, "2'd0"),
            addConstant(graph, "lowered_triple_c1_op", "lowered_triple_c1", 2, "2'd1"),
            addConstant(graph, "lowered_triple_c2_op", "lowered_triple_c2", 2, "2'd2"),
            addConstant(graph, "lowered_triple_c3_op", "lowered_triple_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "lowered_triple_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "lowered_triple_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("lowered_triple_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "lowered_triple_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("lowered_triple_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("lowered_triple_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idxA);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);

            const ValueId hitA = makeLogicValue(graph, "lowered_triple_hit_a_" + suffix, 1);
            const OperationId eqA =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("lowered_triple_eq_a_" + suffix));
            graph.addOperand(eqA, idxA);
            graph.addOperand(eqA, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqA, hitA);
            const ValueId pointA = makeLogicValue(graph, "lowered_triple_point_a_" + suffix, 1);
            const OperationId andA = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("lowered_triple_and_a_" + suffix));
            graph.addOperand(andA, wrEnA);
            graph.addOperand(andA, hitA);
            graph.addResult(andA, pointA);

            const ValueId hitB = makeLogicValue(graph, "lowered_triple_hit_b_" + suffix, 1);
            const OperationId eqB =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("lowered_triple_eq_b_" + suffix));
            graph.addOperand(eqB, idxB);
            graph.addOperand(eqB, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqB, hitB);
            const ValueId pointB = makeLogicValue(graph, "lowered_triple_point_b_" + suffix, 1);
            const OperationId andB = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("lowered_triple_and_b_" + suffix));
            graph.addOperand(andB, wrEnB);
            graph.addOperand(andB, hitB);
            graph.addResult(andB, pointB);

            const ValueId hitC = makeLogicValue(graph, "lowered_triple_hit_c_" + suffix, 1);
            const OperationId eqC =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("lowered_triple_eq_c_" + suffix));
            graph.addOperand(eqC, idxC);
            graph.addOperand(eqC, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqC, hitC);
            const ValueId pointC = makeLogicValue(graph, "lowered_triple_point_c_" + suffix, 1);
            const OperationId andC = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("lowered_triple_and_c_" + suffix));
            graph.addOperand(andC, wrEnC);
            graph.addOperand(andC, hitC);
            graph.addResult(andC, pointC);

            const ValueId orAB = makeLogicValue(graph, "lowered_triple_or_ab_" + suffix, 1);
            const OperationId orABOp = graph.createOperation(OperationKind::kLogicOr,
                                                             graph.internSymbol("lowered_triple_or_ab_op_" + suffix));
            graph.addOperand(orABOp, pointA);
            graph.addOperand(orABOp, pointB);
            graph.addResult(orABOp, orAB);

            const ValueId writeCond = makeLogicValue(graph, "lowered_triple_cond_" + suffix, 1);
            const OperationId orABCOp = graph.createOperation(OperationKind::kLogicOr,
                                                              graph.internSymbol("lowered_triple_or_abc_op_" + suffix));
            graph.addOperand(orABCOp, orAB);
            graph.addOperand(orABCOp, pointC);
            graph.addResult(orABCOp, writeCond);

            const ValueId maskA = makeLogicValue(graph, "lowered_triple_mask_a_" + suffix, 8);
            const OperationId repA = graph.createOperation(OperationKind::kReplicate,
                                                           graph.internSymbol("lowered_triple_rep_a_" + suffix));
            graph.addOperand(repA, pointA);
            graph.addResult(repA, maskA);
            graph.setAttr(repA, "rep", static_cast<int64_t>(8));

            const ValueId maskB = makeLogicValue(graph, "lowered_triple_mask_b_" + suffix, 8);
            const OperationId repB = graph.createOperation(OperationKind::kReplicate,
                                                           graph.internSymbol("lowered_triple_rep_b_" + suffix));
            graph.addOperand(repB, pointB);
            graph.addResult(repB, maskB);
            graph.setAttr(repB, "rep", static_cast<int64_t>(8));

            const ValueId maskC = makeLogicValue(graph, "lowered_triple_mask_c_" + suffix, 8);
            const OperationId repC = graph.createOperation(OperationKind::kReplicate,
                                                           graph.internSymbol("lowered_triple_rep_c_" + suffix));
            graph.addOperand(repC, pointC);
            graph.addResult(repC, maskC);
            graph.setAttr(repC, "rep", static_cast<int64_t>(8));

            const ValueId maskedA = makeLogicValue(graph, "lowered_triple_masked_a_" + suffix, 8);
            const OperationId andDataA = graph.createOperation(OperationKind::kAnd,
                                                               graph.internSymbol("lowered_triple_data_and_a_" + suffix));
            graph.addOperand(andDataA, dataA);
            graph.addOperand(andDataA, maskA);
            graph.addResult(andDataA, maskedA);

            const ValueId maskedB = makeLogicValue(graph, "lowered_triple_masked_b_" + suffix, 8);
            const OperationId andDataB = graph.createOperation(OperationKind::kAnd,
                                                               graph.internSymbol("lowered_triple_data_and_b_" + suffix));
            graph.addOperand(andDataB, dataB);
            graph.addOperand(andDataB, maskB);
            graph.addResult(andDataB, maskedB);

            const ValueId maskedC = makeLogicValue(graph, "lowered_triple_masked_c_" + suffix, 8);
            const OperationId andDataC = graph.createOperation(OperationKind::kAnd,
                                                               graph.internSymbol("lowered_triple_data_and_c_" + suffix));
            graph.addOperand(andDataC, dataC);
            graph.addOperand(andDataC, maskC);
            graph.addResult(andDataC, maskedC);

            const ValueId orDataAB = makeLogicValue(graph, "lowered_triple_or_data_ab_" + suffix, 8);
            const OperationId orDataABOp = graph.createOperation(OperationKind::kOr,
                                                                 graph.internSymbol("lowered_triple_or_data_ab_op_" + suffix));
            graph.addOperand(orDataABOp, maskedA);
            graph.addOperand(orDataABOp, maskedB);
            graph.addResult(orDataABOp, orDataAB);

            const ValueId mergedData = makeLogicValue(graph, "lowered_triple_data_" + suffix, 8);
            const OperationId orDataABCOp = graph.createOperation(OperationKind::kOr,
                                                                  graph.internSymbol("lowered_triple_or_data_abc_op_" + suffix));
            graph.addOperand(orDataABCOp, orDataAB);
            graph.addOperand(orDataABCOp, maskedC);
            graph.addResult(orDataABCOp, mergedData);

            const std::string regName = "lowered_triple_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("lowered_triple_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildDuplicatedPointMaskWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEnA = makeLogicValue(graph, "wr_en_a", 1);
        const ValueId wrEnB = makeLogicValue(graph, "wr_en_b", 1);
        const ValueId idxA = makeLogicValue(graph, "idx_a", 2);
        const ValueId idxB = makeLogicValue(graph, "idx_b", 2);
        const ValueId dataA = makeLogicValue(graph, "data_a", 1);
        const ValueId dataB = makeLogicValue(graph, "data_b", 1);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en_a", wrEnA);
        graph.bindInputPort("wr_en_b", wrEnB);
        graph.bindInputPort("idx_a", idxA);
        graph.bindInputPort("idx_b", idxB);
        graph.bindInputPort("data_a", dataA);
        graph.bindInputPort("data_b", dataB);

        const ValueId mask = addConstant(graph, "dup_mask_op", "dup_mask", 1, "1'h1");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "dup_c0_op", "dup_c0", 2, "2'd0"),
            addConstant(graph, "dup_c1_op", "dup_c1", 2, "2'd1"),
            addConstant(graph, "dup_c2_op", "dup_c2", 2, "2'd2"),
            addConstant(graph, "dup_c3_op", "dup_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "dup_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(1));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("1'h0"));

            const ValueId q = makeLogicValue(graph, "dup_q_" + std::to_string(i), 1);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("dup_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "dup_packed", 4);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("dup_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 1);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("dup_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idxA);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(1));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);

            const ValueId eqAWrite = makeLogicValue(graph, "dup_eq_a_write_" + suffix, 1);
            const OperationId eqAWriteOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_eq_a_write_op_" + suffix));
            graph.addOperand(eqAWriteOp, idxA);
            graph.addOperand(eqAWriteOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqAWriteOp, eqAWrite);

            const ValueId pointAWrite = makeLogicValue(graph, "dup_point_a_write_" + suffix, 1);
            const OperationId pointAWriteOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_point_a_write_op_" + suffix));
            graph.addOperand(pointAWriteOp, wrEnA);
            graph.addOperand(pointAWriteOp, eqAWrite);
            graph.addResult(pointAWriteOp, pointAWrite);

            const ValueId eqBWrite = makeLogicValue(graph, "dup_eq_b_write_" + suffix, 1);
            const OperationId eqBWriteOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_eq_b_write_op_" + suffix));
            graph.addOperand(eqBWriteOp, idxB);
            graph.addOperand(eqBWriteOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqBWriteOp, eqBWrite);

            const ValueId pointBWrite = makeLogicValue(graph, "dup_point_b_write_" + suffix, 1);
            const OperationId pointBWriteOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_point_b_write_op_" + suffix));
            graph.addOperand(pointBWriteOp, wrEnB);
            graph.addOperand(pointBWriteOp, eqBWrite);
            graph.addResult(pointBWriteOp, pointBWrite);

            const ValueId writeCond = makeLogicValue(graph, "dup_write_cond_" + suffix, 1);
            const OperationId writeCondOp =
                graph.createOperation(OperationKind::kLogicOr, graph.internSymbol("dup_write_cond_op_" + suffix));
            graph.addOperand(writeCondOp, pointAWrite);
            graph.addOperand(writeCondOp, pointBWrite);
            graph.addResult(writeCondOp, writeCond);

            const ValueId eqAData = makeLogicValue(graph, "dup_eq_a_data_" + suffix, 1);
            const OperationId eqADataOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_eq_a_data_op_" + suffix));
            graph.addOperand(eqADataOp, idxA);
            graph.addOperand(eqADataOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqADataOp, eqAData);

            const ValueId pointAData = makeLogicValue(graph, "dup_point_a_data_" + suffix, 1);
            const OperationId pointADataOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_point_a_data_op_" + suffix));
            graph.addOperand(pointADataOp, wrEnA);
            graph.addOperand(pointADataOp, eqAData);
            graph.addResult(pointADataOp, pointAData);

            const ValueId eqBData = makeLogicValue(graph, "dup_eq_b_data_" + suffix, 1);
            const OperationId eqBDataOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_eq_b_data_op_" + suffix));
            graph.addOperand(eqBDataOp, idxB);
            graph.addOperand(eqBDataOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqBDataOp, eqBData);

            const ValueId pointBData = makeLogicValue(graph, "dup_point_b_data_" + suffix, 1);
            const OperationId pointBDataOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_point_b_data_op_" + suffix));
            graph.addOperand(pointBDataOp, wrEnB);
            graph.addOperand(pointBDataOp, eqBData);
            graph.addResult(pointBDataOp, pointBData);

            const ValueId maskedA = makeLogicValue(graph, "dup_masked_a_" + suffix, 1);
            const OperationId maskedAOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_masked_a_op_" + suffix));
            graph.addOperand(maskedAOp, pointAData);
            graph.addOperand(maskedAOp, dataA);
            graph.addResult(maskedAOp, maskedA);

            const ValueId maskedB = makeLogicValue(graph, "dup_masked_b_" + suffix, 1);
            const OperationId maskedBOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_masked_b_op_" + suffix));
            graph.addOperand(maskedBOp, pointBData);
            graph.addOperand(maskedBOp, dataB);
            graph.addResult(maskedBOp, maskedB);

            const ValueId mergedData = makeLogicValue(graph, "dup_merged_data_" + suffix, 1);
            const OperationId mergedDataOp =
                graph.createOperation(OperationKind::kOr, graph.internSymbol("dup_merged_data_op_" + suffix));
            graph.addOperand(mergedDataOp, maskedA);
            graph.addOperand(mergedDataOp, maskedB);
            graph.addResult(mergedDataOp, mergedData);

            const std::string regName = "dup_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("dup_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildDuplicatedPointMuxMaskWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEnA = makeLogicValue(graph, "wr_en_a", 1);
        const ValueId wrEnB = makeLogicValue(graph, "wr_en_b", 1);
        const ValueId idxA = makeLogicValue(graph, "idx_a", 2);
        const ValueId idxB = makeLogicValue(graph, "idx_b", 2);
        const ValueId dataA = makeLogicValue(graph, "data_a", 3);
        const ValueId dataB = makeLogicValue(graph, "data_b", 3);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en_a", wrEnA);
        graph.bindInputPort("wr_en_b", wrEnB);
        graph.bindInputPort("idx_a", idxA);
        graph.bindInputPort("idx_b", idxB);
        graph.bindInputPort("data_a", dataA);
        graph.bindInputPort("data_b", dataB);

        const ValueId mask = addConstant(graph, "dup_mux_mask_op", "dup_mux_mask", 3, "3'b111");
        const ValueId zero3 = addConstant(graph, "dup_mux_zero_op", "dup_mux_zero", 3, "3'b000");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "dup_mux_c0_op", "dup_mux_c0", 2, "2'd0"),
            addConstant(graph, "dup_mux_c1_op", "dup_mux_c1", 2, "2'd1"),
            addConstant(graph, "dup_mux_c2_op", "dup_mux_c2", 2, "2'd2"),
            addConstant(graph, "dup_mux_c3_op", "dup_mux_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "dup_mux_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(3));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("3'b000"));

            const ValueId q = makeLogicValue(graph, "dup_mux_q_" + std::to_string(i), 3);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("dup_mux_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "dup_mux_packed", 12);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("dup_mux_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 3);
        const OperationId slice =
            graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("dup_mux_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idxA);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(3));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);

            const ValueId eqAWrite = makeLogicValue(graph, "dup_mux_eq_a_write_" + suffix, 1);
            const OperationId eqAWriteOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_mux_eq_a_write_op_" + suffix));
            graph.addOperand(eqAWriteOp, idxA);
            graph.addOperand(eqAWriteOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqAWriteOp, eqAWrite);

            const ValueId pointAWrite = makeLogicValue(graph, "dup_mux_point_a_write_" + suffix, 1);
            const OperationId pointAWriteOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_mux_point_a_write_op_" + suffix));
            graph.addOperand(pointAWriteOp, wrEnA);
            graph.addOperand(pointAWriteOp, eqAWrite);
            graph.addResult(pointAWriteOp, pointAWrite);

            const ValueId eqBWrite = makeLogicValue(graph, "dup_mux_eq_b_write_" + suffix, 1);
            const OperationId eqBWriteOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_mux_eq_b_write_op_" + suffix));
            graph.addOperand(eqBWriteOp, idxB);
            graph.addOperand(eqBWriteOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqBWriteOp, eqBWrite);

            const ValueId pointBWrite = makeLogicValue(graph, "dup_mux_point_b_write_" + suffix, 1);
            const OperationId pointBWriteOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_mux_point_b_write_op_" + suffix));
            graph.addOperand(pointBWriteOp, wrEnB);
            graph.addOperand(pointBWriteOp, eqBWrite);
            graph.addResult(pointBWriteOp, pointBWrite);

            const ValueId writeCond = makeLogicValue(graph, "dup_mux_write_cond_" + suffix, 1);
            const OperationId writeCondOp =
                graph.createOperation(OperationKind::kLogicOr, graph.internSymbol("dup_mux_write_cond_op_" + suffix));
            graph.addOperand(writeCondOp, pointAWrite);
            graph.addOperand(writeCondOp, pointBWrite);
            graph.addResult(writeCondOp, writeCond);

            const ValueId eqAData = makeLogicValue(graph, "dup_mux_eq_a_data_" + suffix, 1);
            const OperationId eqADataOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_mux_eq_a_data_op_" + suffix));
            graph.addOperand(eqADataOp, idxA);
            graph.addOperand(eqADataOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqADataOp, eqAData);

            const ValueId pointAData = makeLogicValue(graph, "dup_mux_point_a_data_" + suffix, 1);
            const OperationId pointADataOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_mux_point_a_data_op_" + suffix));
            graph.addOperand(pointADataOp, wrEnA);
            graph.addOperand(pointADataOp, eqAData);
            graph.addResult(pointADataOp, pointAData);

            const ValueId eqBData = makeLogicValue(graph, "dup_mux_eq_b_data_" + suffix, 1);
            const OperationId eqBDataOp =
                graph.createOperation(OperationKind::kEq, graph.internSymbol("dup_mux_eq_b_data_op_" + suffix));
            graph.addOperand(eqBDataOp, idxB);
            graph.addOperand(eqBDataOp, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eqBDataOp, eqBData);

            const ValueId pointBData = makeLogicValue(graph, "dup_mux_point_b_data_" + suffix, 1);
            const OperationId pointBDataOp =
                graph.createOperation(OperationKind::kAnd, graph.internSymbol("dup_mux_point_b_data_op_" + suffix));
            graph.addOperand(pointBDataOp, wrEnB);
            graph.addOperand(pointBDataOp, eqBData);
            graph.addResult(pointBDataOp, pointBData);

            const ValueId muxA = makeLogicValue(graph, "dup_mux_a_" + suffix, 3);
            const OperationId muxAOp =
                graph.createOperation(OperationKind::kMux, graph.internSymbol("dup_mux_a_op_" + suffix));
            graph.addOperand(muxAOp, pointAData);
            graph.addOperand(muxAOp, dataA);
            graph.addOperand(muxAOp, zero3);
            graph.addResult(muxAOp, muxA);

            const ValueId muxB = makeLogicValue(graph, "dup_mux_b_" + suffix, 3);
            const OperationId muxBOp =
                graph.createOperation(OperationKind::kMux, graph.internSymbol("dup_mux_b_op_" + suffix));
            graph.addOperand(muxBOp, pointBData);
            graph.addOperand(muxBOp, dataB);
            graph.addOperand(muxBOp, zero3);
            graph.addResult(muxBOp, muxB);

            const ValueId mergedData = makeLogicValue(graph, "dup_mux_merged_data_" + suffix, 3);
            const OperationId mergedDataOp =
                graph.createOperation(OperationKind::kOr, graph.internSymbol("dup_mux_merged_data_op_" + suffix));
            graph.addOperand(mergedDataOp, muxA);
            graph.addOperand(mergedDataOp, muxB);
            graph.addResult(mergedDataOp, mergedData);

            const std::string regName = "dup_mux_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("dup_mux_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildLoweredMixedWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId pointData = makeLogicValue(graph, "point_data", 8);
        const ValueId fillData = makeLogicValue(graph, "fill_data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("point_data", pointData);
        graph.bindInputPort("fill_data", fillData);

        const ValueId mask = addConstant(graph, "lowered_mask_op", "lowered_mask", 8, "8'hff");
        const ValueId zero8 = addConstant(graph, "lowered_zero8_op", "lowered_zero8", 8, "8'h00");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "lowered_c0_op", "lowered_c0", 2, "2'd0"),
            addConstant(graph, "lowered_c1_op", "lowered_c1", 2, "2'd1"),
            addConstant(graph, "lowered_c2_op", "lowered_c2", 2, "2'd2"),
            addConstant(graph, "lowered_c3_op", "lowered_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string regName = "lowered_" + std::to_string(i) + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "lowered_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("lowered_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "lowered_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("lowered_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("lowered_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idx);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId hit = makeLogicValue(graph, "lowered_hit_" + suffix, 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("lowered_eq_" + suffix));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "lowered_point_cond_" + suffix, 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("lowered_point_and_" + suffix));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const ValueId writeCond = makeLogicValue(graph, "lowered_write_cond_" + suffix, 1);
            const OperationId orCond = graph.createOperation(OperationKind::kLogicOr,
                                                             graph.internSymbol("lowered_cond_or_" + suffix));
            graph.addOperand(orCond, fillEn);
            graph.addOperand(orCond, pointCond);
            graph.addResult(orCond, writeCond);

            const ValueId pointMaskWide = makeLogicValue(graph, "lowered_point_mask_wide_" + suffix, 8);
            const OperationId pointMaskRep = graph.createOperation(OperationKind::kReplicate,
                                                                   graph.internSymbol("lowered_point_mask_rep_" + suffix));
            graph.addOperand(pointMaskRep, pointCond);
            graph.addResult(pointMaskRep, pointMaskWide);
            graph.setAttr(pointMaskRep, "rep", static_cast<int64_t>(8));

            const ValueId pointTrue = makeLogicValue(graph, "lowered_point_true_" + suffix, 8);
            const OperationId pointAnd = graph.createOperation(OperationKind::kAnd,
                                                               graph.internSymbol("lowered_point_true_and_" + suffix));
            graph.addOperand(pointAnd, pointData);
            graph.addOperand(pointAnd, pointMaskWide);
            graph.addResult(pointAnd, pointTrue);

            const ValueId pointMaskNot = makeLogicValue(graph, "lowered_point_mask_not_" + suffix, 8);
            const OperationId pointNot = graph.createOperation(OperationKind::kNot,
                                                               graph.internSymbol("lowered_point_mask_not_op_" + suffix));
            graph.addOperand(pointNot, pointMaskWide);
            graph.addResult(pointNot, pointMaskNot);

            const ValueId pointFalse = makeLogicValue(graph, "lowered_point_false_" + suffix, 8);
            const OperationId zeroAnd = graph.createOperation(OperationKind::kAnd,
                                                              graph.internSymbol("lowered_point_false_and_" + suffix));
            graph.addOperand(zeroAnd, zero8);
            graph.addOperand(zeroAnd, pointMaskNot);
            graph.addResult(zeroAnd, pointFalse);

            const ValueId loweredPointData = makeLogicValue(graph, "lowered_point_data_" + suffix, 8);
            const OperationId pointOr = graph.createOperation(OperationKind::kOr,
                                                              graph.internSymbol("lowered_point_or_" + suffix));
            graph.addOperand(pointOr, pointTrue);
            graph.addOperand(pointOr, pointFalse);
            graph.addResult(pointOr, loweredPointData);

            const ValueId mergedData = makeLogicValue(graph, "lowered_merged_data_" + suffix, 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("lowered_fill_point_mux_" + suffix));
            graph.addOperand(mux, pointCond);
            graph.addOperand(mux, loweredPointData);
            graph.addOperand(mux, fillData);
            graph.addResult(mux, mergedData);

            const std::string regName = "lowered_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("lowered_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeCond);
            graph.addOperand(write, mergedData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildLoweredMixedWritePackedWideDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId pointData = makeLogicValue(graph, "point_data", 8);
        const ValueId fillData = makeLogicValue(graph, "fill_data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("point_data", pointData);
        graph.bindInputPort("fill_data", fillData);

        const ValueId mask = addConstant(graph, "packed_mask_op", "packed_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "packed_c0_op", "packed_c0", 2, "2'd0"),
            addConstant(graph, "packed_c1_op", "packed_c1", 2, "2'd1"),
            addConstant(graph, "packed_c2_op", "packed_c2", 2, "2'd2"),
            addConstant(graph, "packed_c3_op", "packed_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        std::vector<ValueId> pointConds;
        pointConds.reserve(4);
        std::vector<ValueId> writeConds;
        writeConds.reserve(4);
        std::vector<ValueId> maskLanes;
        maskLanes.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const std::string regName = "packed_" + suffix + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "packed_q_" + suffix, 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("packed_read_" + suffix));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);

            const ValueId hit = makeLogicValue(graph, "packed_hit_" + suffix, 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("packed_eq_" + suffix));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "packed_point_cond_" + suffix, 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("packed_point_and_" + suffix));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);
            pointConds.push_back(pointCond);

            const ValueId writeCond = makeLogicValue(graph, "packed_write_cond_" + suffix, 1);
            const OperationId orCond = graph.createOperation(OperationKind::kLogicOr,
                                                             graph.internSymbol("packed_cond_or_" + suffix));
            graph.addOperand(orCond, fillEn);
            graph.addOperand(orCond, pointCond);
            graph.addResult(orCond, writeCond);
            writeConds.push_back(writeCond);

            const ValueId maskLane = makeLogicValue(graph, "packed_mask_lane_" + suffix, 8);
            const OperationId maskRep = graph.createOperation(OperationKind::kReplicate,
                                                              graph.internSymbol("packed_mask_rep_" + suffix));
            graph.addOperand(maskRep, pointCond);
            graph.addResult(maskRep, maskLane);
            graph.setAttr(maskRep, "rep", static_cast<int64_t>(8));
            maskLanes.push_back(maskLane);
        }

        const ValueId packed = makeLogicValue(graph, "packed_storage", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("packed_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("packed_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idx);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        const ValueId pointWide = makeLogicValue(graph, "packed_point_wide", 32);
        const OperationId pointConcat = graph.createOperation(OperationKind::kConcat,
                                                              graph.internSymbol("packed_point_concat"));
        for (int i = 0; i < 4; ++i)
        {
            graph.addOperand(pointConcat, pointData);
        }
        graph.addResult(pointConcat, pointWide);

        const ValueId fillWide = makeLogicValue(graph, "packed_fill_wide", 32);
        const OperationId fillConcat = graph.createOperation(OperationKind::kConcat,
                                                             graph.internSymbol("packed_fill_concat"));
        for (int i = 0; i < 4; ++i)
        {
            graph.addOperand(fillConcat, fillData);
        }
        graph.addResult(fillConcat, fillWide);

        const ValueId maskWide = makeLogicValue(graph, "packed_mask_wide", 32);
        const OperationId maskConcat = graph.createOperation(OperationKind::kConcat,
                                                             graph.internSymbol("packed_mask_concat"));
        graph.addOperand(maskConcat, maskLanes[3]);
        graph.addOperand(maskConcat, maskLanes[2]);
        graph.addOperand(maskConcat, maskLanes[1]);
        graph.addOperand(maskConcat, maskLanes[0]);
        graph.addResult(maskConcat, maskWide);

        const ValueId notMaskWide = makeLogicValue(graph, "packed_not_mask_wide", 32);
        const OperationId notMask = graph.createOperation(OperationKind::kNot,
                                                          graph.internSymbol("packed_not_mask"));
        graph.addOperand(notMask, maskWide);
        graph.addResult(notMask, notMaskWide);

        const ValueId pointTrueWide = makeLogicValue(graph, "packed_point_true_wide", 32);
        const OperationId pointAnd = graph.createOperation(OperationKind::kAnd,
                                                           graph.internSymbol("packed_point_true_and"));
        graph.addOperand(pointAnd, pointWide);
        graph.addOperand(pointAnd, maskWide);
        graph.addResult(pointAnd, pointTrueWide);

        const ValueId fillFalseWide = makeLogicValue(graph, "packed_fill_false_wide", 32);
        const OperationId fillAnd = graph.createOperation(OperationKind::kAnd,
                                                          graph.internSymbol("packed_fill_false_and"));
        graph.addOperand(fillAnd, fillWide);
        graph.addOperand(fillAnd, notMaskWide);
        graph.addResult(fillAnd, fillFalseWide);

        const ValueId mergedWide = makeLogicValue(graph, "packed_merged_wide", 32);
        const OperationId mergedOr = graph.createOperation(OperationKind::kOr,
                                                           graph.internSymbol("packed_merged_or"));
        graph.addOperand(mergedOr, pointTrueWide);
        graph.addOperand(mergedOr, fillFalseWide);
        graph.addResult(mergedOr, mergedWide);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId laneData = makeLogicValue(graph, "packed_lane_data_" + suffix, 8);
            const OperationId laneSlice = graph.createOperation(OperationKind::kSliceStatic,
                                                                graph.internSymbol("packed_lane_slice_" + suffix));
            graph.addOperand(laneSlice, mergedWide);
            graph.addResult(laneSlice, laneData);
            graph.setAttr(laneSlice, "sliceStart", static_cast<int64_t>(i * 8));
            graph.setAttr(laneSlice, "sliceEnd", static_cast<int64_t>(i * 8 + 7));

            const std::string regName = "packed_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("packed_write_" + suffix));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, writeConds[static_cast<std::size_t>(i)]);
            graph.addOperand(write, laneData);
            graph.addOperand(write, mask);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildMultiFillMixedWriteDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId wrEn = makeLogicValue(graph, "wr_en", 1);
        const ValueId fillAEn = makeLogicValue(graph, "fill_a_en", 1);
        const ValueId fillBEn = makeLogicValue(graph, "fill_b_en", 1);
        const ValueId idx = makeLogicValue(graph, "idx", 2);
        const ValueId pointData = makeLogicValue(graph, "point_data", 8);
        const ValueId fillAData = makeLogicValue(graph, "fill_a_data", 8);
        const ValueId fillBData = makeLogicValue(graph, "fill_b_data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("wr_en", wrEn);
        graph.bindInputPort("fill_a_en", fillAEn);
        graph.bindInputPort("fill_b_en", fillBEn);
        graph.bindInputPort("idx", idx);
        graph.bindInputPort("point_data", pointData);
        graph.bindInputPort("fill_a_data", fillAData);
        graph.bindInputPort("fill_b_data", fillBData);

        const ValueId mask = addConstant(graph, "multifill_mask_op", "multifill_mask", 8, "8'hff");
        const std::array<ValueId, 4> indices{
            addConstant(graph, "multifill_c0_op", "multifill_c0", 2, "2'd0"),
            addConstant(graph, "multifill_c1_op", "multifill_c1", 2, "2'd1"),
            addConstant(graph, "multifill_c2_op", "multifill_c2", 2, "2'd2"),
            addConstant(graph, "multifill_c3_op", "multifill_c3", 2, "2'd3")};

        std::vector<ValueId> readValues;
        readValues.reserve(4);
        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const std::string regName = "multifill_" + suffix + "_value";
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(regName));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("8'h00"));

            const ValueId q = makeLogicValue(graph, "multifill_q_" + suffix, 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("multifill_read_" + suffix));
            graph.setAttr(read, "regSymbol", regName);
            graph.addResult(read, q);
            readValues.push_back(q);
        }

        const ValueId packed = makeLogicValue(graph, "multifill_packed", 32);
        const OperationId concat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("multifill_concat"));
        graph.addOperand(concat, readValues[3]);
        graph.addOperand(concat, readValues[2]);
        graph.addOperand(concat, readValues[1]);
        graph.addOperand(concat, readValues[0]);
        graph.addResult(concat, packed);

        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId slice = graph.createOperation(OperationKind::kSliceArray, graph.internSymbol("multifill_slice_array"));
        graph.addOperand(slice, packed);
        graph.addOperand(slice, idx);
        graph.addResult(slice, out);
        graph.setAttr(slice, "sliceWidth", static_cast<int64_t>(8));
        graph.bindOutputPort("out", out);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId hit = makeLogicValue(graph, "multifill_hit_" + suffix, 1);
            const OperationId eq = graph.createOperation(OperationKind::kEq, graph.internSymbol("multifill_eq_" + suffix));
            graph.addOperand(eq, idx);
            graph.addOperand(eq, indices[static_cast<std::size_t>(i)]);
            graph.addResult(eq, hit);

            const ValueId pointCond = makeLogicValue(graph, "multifill_point_cond_" + suffix, 1);
            const OperationId andOp = graph.createOperation(OperationKind::kAnd,
                                                            graph.internSymbol("multifill_point_and_" + suffix));
            graph.addOperand(andOp, wrEn);
            graph.addOperand(andOp, hit);
            graph.addResult(andOp, pointCond);

            const ValueId fillCond = makeLogicValue(graph, "multifill_fill_cond_" + suffix, 1);
            const OperationId fillOr = graph.createOperation(OperationKind::kLogicOr,
                                                             graph.internSymbol("multifill_fill_or_" + suffix));
            graph.addOperand(fillOr, fillAEn);
            graph.addOperand(fillOr, fillBEn);
            graph.addResult(fillOr, fillCond);

            const ValueId writeCond = makeLogicValue(graph, "multifill_write_cond_" + suffix, 1);
            const OperationId writeOr = graph.createOperation(OperationKind::kLogicOr,
                                                              graph.internSymbol("multifill_write_or_" + suffix));
            graph.addOperand(writeOr, pointCond);
            graph.addOperand(writeOr, fillCond);
            graph.addResult(writeOr, writeCond);

            const ValueId fillTail = makeLogicValue(graph, "multifill_fill_tail_" + suffix, 8);
            const OperationId fillTailMux = graph.createOperation(OperationKind::kMux,
                                                                  graph.internSymbol("multifill_fill_tail_mux_" + suffix));
            graph.addOperand(fillTailMux, fillAEn);
            graph.addOperand(fillTailMux, fillAData);
            graph.addOperand(fillTailMux, fillBData);
            graph.addResult(fillTailMux, fillTail);

            const ValueId mergedData = makeLogicValue(graph, "multifill_merged_data_" + suffix, 8);
            const OperationId mux = graph.createOperation(OperationKind::kMux,
                                                          graph.internSymbol("multifill_point_mux_" + suffix));
            graph.addOperand(mux, pointCond);
            graph.addOperand(mux, pointData);
            graph.addOperand(mux, fillTail);
            graph.addResult(mux, mergedData);

            const std::string regName = "multifill_" + suffix + "_value";
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("multifill_write_" + suffix));
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
                          std::string_view primaryOutputName = "out",
                          std::size_t expectedConcatCount = 0,
                          std::size_t expectedFillPorts = 1)
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
            return fail("scalar-memory-pack pass should succeed: " + diagSummary(diags));
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the cluster: " + diagSummary(diags));
        }

        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0 ||
            countOps(*graph, OperationKind::kConcat) != expectedConcatCount ||
            countOps(*graph, OperationKind::kSliceArray) != 0 ||
            (expectNoSliceDynamic && countOps(*graph, OperationKind::kSliceDynamic) != 0))
        {
            return fail("scalarized storage/read/write nodes should be removed: " + opCountSummary(*graph));
        }
        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != expectedReadPorts ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 1 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != expectedFillPorts)
        {
            return fail("expected packed memory op counts after packing: " + opCountSummary(*graph));
        }

        const OperationId memoryOpId = findSingleOp(*graph, OperationKind::kMemory);
        const OperationId writeOpId = findSingleOp(*graph, OperationKind::kMemoryWritePort);
        if (!memoryOpId.valid() || !writeOpId.valid())
        {
            return fail("expected unique packed memory/write ops");
        }
        if (expectedFillPorts == 1)
        {
            const OperationId fillOpId = findSingleOp(*graph, OperationKind::kMemoryFillPort);
            if (!fillOpId.valid())
            {
                return fail("expected unique packed fill op");
            }
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

        if (expectedFillPorts == 1)
        {
            const OperationId fillOpId = findSingleOp(*graph, OperationKind::kMemoryFillPort);
            const Operation fillOp = graph->getOperation(fillOpId);
            if (fillOp.operands().size() != 3 ||
                fillOp.operands()[0] != graph->inputPortValue("fill_en"))
            {
                return fail("memory fill should preserve fill cond");
            }
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
        Design design = buildDirectReadFamilyDesign();
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
            return fail("scalar-memory-pack pass should succeed on direct-read family");
        }
        if (result.changed)
        {
            return fail("direct-read family should stay scalar because it does not reduce read ports");
        }
        if (countOps(*graph, OperationKind::kRegister) != 4 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 4 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 8)
        {
            return fail("direct-read family should remain unchanged: " + opCountSummary(*graph));
        }
        if (countOps(*graph, OperationKind::kMemory) != 0 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 0 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 0 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("direct-read family should not be packed into memory form: " + opCountSummary(*graph));
        }
    }

    {
        Design design = buildInternalGeneratedLikeDesign();
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
            return fail("scalar-memory-pack pass should succeed on internal-generated-like family");
        }
        if (result.changed)
        {
            return fail("internal-generated-like family should stay scalar because generated registers must be filtered");
        }
        if (countOps(*graph, OperationKind::kMemory) != 0 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 0 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 0 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("internal-generated-like family should not be packed into memory form: " +
                        opCountSummary(*graph));
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
        Design design = buildShiftedIndexDesign();
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
            return fail("scalar-memory-pack pass should succeed on shifted-index family");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the shifted-index family");
        }
        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 1 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 1)
        {
            return fail("shifted-index family should pack into one memory/read/write/fill set: " +
                        opCountSummary(*graph));
        }

        const Operation readOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryReadPort));
        const OperationId readAddrDef = graph->valueDef(readOp.operands().front());
        if (!readAddrDef.valid() || graph->getOperation(readAddrDef).kind() != OperationKind::kSub)
        {
            return fail("shifted-index packed read should normalize to idx - baseIndex");
        }

        const Operation writeOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryWritePort));
        if (writeOp.operands().size() != 5)
        {
            return fail("shifted-index packed write should preserve cond/addr/data/mask/clock operands");
        }
        const OperationId writeAddrDef = graph->valueDef(writeOp.operands()[1]);
        if (!writeAddrDef.valid() || graph->getOperation(writeAddrDef).kind() != OperationKind::kSub)
        {
            return fail("shifted-index packed write should normalize to idx - baseIndex");
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
        Design design = buildMultiplePointWriteWithHoldReadUsersDesign();
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
            return fail("scalar-memory-pack pass should succeed on multiple-point writes with hold read users");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the hold-read-user cluster");
        }

        if (countOps(*graph, OperationKind::kRegister) != 0 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 0 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 0 ||
            countOps(*graph, OperationKind::kConcat) != 0 ||
            countOps(*graph, OperationKind::kSliceArray) != 0 ||
            countOps(*graph, OperationKind::kSliceDynamic) != 0)
        {
            return fail("hold-read-user cluster should not leave scalar storage/read/write nodes: " +
                        opCountSummary(*graph));
        }

        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 1 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("hold-read-user cluster should still pack into one memory: " +
                        opCountSummary(*graph));
        }

        const OperationId memoryOpId = findSingleOp(*graph, OperationKind::kMemory);
        if (!memoryOpId.valid())
        {
            return fail("hold-read-user cluster should produce one packed memory op");
        }
        const Operation memoryOp = graph->getOperation(memoryOpId);
        if (getAttr<int64_t>(memoryOp, "row").value_or(0) != 4 ||
            getAttr<int64_t>(memoryOp, "width").value_or(0) != 8)
        {
            return fail("hold-read-user packed memory shape mismatch");
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

    {
        Design design = buildLoweredTriplePointWriteDesign();
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
            return fail("scalar-memory-pack pass should succeed on lowered triple-point writes");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the lowered triple-point cluster");
        }

        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 3 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("lowered triple-point packed cluster should become one memory with three write ports");
        }
    }

    {
        Design design = buildDuplicatedPointMaskWriteDesign();
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
            return fail("scalar-memory-pack pass should succeed on duplicated point-mask writes");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the duplicated point-mask cluster");
        }

        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 2 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("duplicated point-mask packed cluster should become one memory with two write ports");
        }
    }

    {
        Design design = buildDuplicatedPointMuxMaskWriteDesign();
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
            return fail("scalar-memory-pack pass should succeed on duplicated point-mux writes");
        }
        if (!result.changed)
        {
            return fail("scalar-memory-pack should rewrite the duplicated point-mux cluster");
        }

        if (countOps(*graph, OperationKind::kMemory) != 1 ||
            countOps(*graph, OperationKind::kMemoryReadPort) != 1 ||
            countOps(*graph, OperationKind::kMemoryWritePort) != 2 ||
            countOps(*graph, OperationKind::kMemoryFillPort) != 0)
        {
            return fail("duplicated point-mux packed cluster should become one memory with two write ports");
        }
    }

    {
        Design design = buildLoweredMixedWriteDesign();
        if (const int status = verifyPackedShape(design, true, 4, 1, "out"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation writeOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryWritePort));
        const Operation fillOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryFillPort));
        if (writeOp.operands().size() != 5 || writeOp.operands()[2] != graph->inputPortValue("point_data"))
        {
            return fail("lowered mixed-write point update should recover point_data");
        }
        if (fillOp.operands().size() != 3 || fillOp.operands()[1] != graph->inputPortValue("fill_data"))
        {
            return fail("lowered mixed-write fill should recover fill_data");
        }
    }

    {
        Design design = buildLoweredMixedWritePackedWideDesign();
        if (const int status = verifyPackedShape(design, true, 4, 1, "out"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation writeOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryWritePort));
        const Operation fillOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryFillPort));
        if (writeOp.operands().size() != 5 || writeOp.operands()[2] != graph->inputPortValue("point_data"))
        {
            return fail("packed-wide lowered mixed-write point update should recover point_data");
        }
        if (fillOp.operands().size() != 3 || fillOp.operands()[1] != graph->inputPortValue("fill_data"))
        {
            return fail("packed-wide lowered mixed-write fill should recover fill_data");
        }
    }

    {
        Design design = buildMultiFillMixedWriteDesign();
        if (const int status = verifyPackedShape(design, true, 4, 1, "out", 0, 2); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        const Operation writeOp = graph->getOperation(findSingleOp(*graph, OperationKind::kMemoryWritePort));
        if (writeOp.operands().size() != 5 || writeOp.operands()[2] != graph->inputPortValue("point_data"))
        {
            return fail("multi-fill mixed-write point update should recover point_data");
        }
        std::size_t fillASeen = 0;
        std::size_t fillBSeen = 0;
        for (const auto opId : graph->operations())
        {
            const Operation op = graph->getOperation(opId);
            if (op.kind() != OperationKind::kMemoryFillPort || op.operands().size() != 3)
            {
                continue;
            }
            if (op.operands()[0] == graph->inputPortValue("fill_a_en") &&
                op.operands()[1] == graph->inputPortValue("fill_a_data"))
            {
                ++fillASeen;
            }
            if (op.operands()[0] == graph->inputPortValue("fill_b_en") &&
                op.operands()[1] == graph->inputPortValue("fill_b_data"))
            {
                ++fillBSeen;
            }
        }
        if (fillASeen != 1 || fillBSeen != 1)
        {
            return fail("multi-fill mixed-write should materialize one fill port per fill arm");
        }
    }

    return 0;
}
