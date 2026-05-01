#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/merge_reg.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace wolvrix::lib::grh;
using namespace wolvrix::lib::transform;

namespace
{
    int runMergeReg(Design &design, bool expectChanged, std::string_view label = "");

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

    Design buildShiftChainDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId en = makeLogicValue(graph, "en", 1);
        const ValueId in = makeLogicValue(graph, "in", 1);
        const ValueId one = addConstant(graph, "one_op", "one", 1, "1'b1");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("in", in);

        std::vector<std::string> names{"REG_9", "REG_10", "REG_11", "REG_12"};
        std::vector<ValueId> reads;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(names[i]));
            graph.setAttr(reg, "width", static_cast<int64_t>(1));
            graph.setAttr(reg, "isSigned", false);
            graph.setAttr(reg, "initValue", std::string("1'b0"));

            const ValueId q = makeLogicValue(graph, "q_" + std::to_string(i), 1);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", names[i]);
            graph.addResult(read, q);
            reads.push_back(q);
        }
        graph.bindOutputPort("tap", reads[2]);

        for (std::size_t i = 0; i < names.size(); ++i)
        {
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("write_" + std::to_string(i)));
            graph.setAttr(write, "regSymbol", names[i]);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, en);
            graph.addOperand(write, i == 0 ? in : reads[i - 1]);
            graph.addOperand(write, one);
            graph.addOperand(write, clk);
        }

        return design;
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

    Design buildBitsetDesign(bool wrapWithResetMux = false)
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId en = makeLogicValue(graph, "en", 1);
        const ValueId resetDone = makeLogicValue(graph, "reset_done", 1);
        const ValueId set = makeLogicValue(graph, "set", 1);
        const ValueId clear = makeLogicValue(graph, "clear", 1);
        const ValueId setAddr = makeLogicValue(graph, "set_addr", 2);
        const ValueId clearAddr = makeLogicValue(graph, "clear_addr", 2);
        const ValueId one = addConstant(graph, "one_op", "one", 1, "1'b1");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("reset_done", resetDone);
        graph.bindInputPort("set", set);
        graph.bindInputPort("clear", clear);
        graph.bindInputPort("set_addr", setAddr);
        graph.bindInputPort("clear_addr", clearAddr);
        const ValueId resetZero = wrapWithResetMux
                                      ? addConstant(graph, "reset_zero_op", "reset_zero", 1, "1'b0")
                                      : ValueId::invalid();

        std::vector<ValueId> reads;
        std::vector<std::string> names;
        for (int i = 0; i < 4; ++i)
        {
            const std::string name = "flag_" + std::to_string(i);
            names.push_back(name);
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(name));
            graph.setAttr(reg, "width", static_cast<int64_t>(1));
            graph.setAttr(reg, "isSigned", false);

            const ValueId q = makeLogicValue(graph, "q_" + std::to_string(i), 1);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", name);
            graph.addResult(read, q);
            reads.push_back(q);
        }

        const ValueId any01 = addOp(graph,
                                    OperationKind::kOr,
                                    "any01_op",
                                    "any01",
                                    1,
                                    std::array<ValueId, 2>{reads[0], reads[1]});
        const ValueId any23 = addOp(graph,
                                    OperationKind::kOr,
                                    "any23_op",
                                    "any23",
                                    1,
                                    std::array<ValueId, 2>{reads[2], reads[3]});
        graph.bindOutputPort("any01", any01);
        graph.bindOutputPort("any23", any23);
        graph.bindOutputPort("tap2", reads[2]);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId idx = addConstant(graph,
                                            "idx_op_" + suffix,
                                            "idx_" + suffix,
                                            2,
                                            "2'd" + suffix);
            const ValueId setEq = addOp(graph,
                                        OperationKind::kEq,
                                        "set_eq_op_" + suffix,
                                        "set_eq_" + suffix,
                                        1,
                                        std::array<ValueId, 2>{setAddr, idx});
            const ValueId setHit = addOp(graph,
                                         OperationKind::kAnd,
                                         "set_hit_op_" + suffix,
                                         "set_hit_" + suffix,
                                         1,
                                         std::array<ValueId, 2>{set, setEq});
            const ValueId clearEq = addOp(graph,
                                          OperationKind::kEq,
                                          "clear_eq_op_" + suffix,
                                          "clear_eq_" + suffix,
                                          1,
                                          std::array<ValueId, 2>{clearAddr, idx});
            const ValueId clearHit = addOp(graph,
                                           OperationKind::kAnd,
                                           "clear_hit_op_" + suffix,
                                           "clear_hit_" + suffix,
                                           1,
                                           std::array<ValueId, 2>{clear, clearEq});
            const ValueId notClear = addOp(graph,
                                           OperationKind::kNot,
                                           "not_clear_op_" + suffix,
                                           "not_clear_" + suffix,
                                           1,
                                           std::array<ValueId, 1>{clearHit});
            const ValueId setOrHold = addOp(graph,
                                            OperationKind::kOr,
                                            "set_or_hold_op_" + suffix,
                                            "set_or_hold_" + suffix,
                                            1,
                                            std::array<ValueId, 2>{setHit, reads[i]});
            ValueId next = addOp(graph,
                                 OperationKind::kAnd,
                                 "next_op_" + suffix,
                                 "next_" + suffix,
                                 1,
                                 std::array<ValueId, 2>{notClear, setOrHold});
            if (wrapWithResetMux)
            {
                next = addOp(graph,
                             OperationKind::kMux,
                             "reset_mux_op_" + suffix,
                             "reset_mux_" + suffix,
                             1,
                             std::array<ValueId, 3>{resetDone, next, resetZero});
            }

            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("write_" + suffix));
            graph.setAttr(write, "regSymbol", names[i]);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, en);
            graph.addOperand(write, next);
            graph.addOperand(write, one);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildIndexedBankDesign(bool wrapWithResetMux = false)
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId en = makeLogicValue(graph, "en", 1);
        const ValueId resetDone = makeLogicValue(graph, "reset_done", 1);
        const ValueId wr = makeLogicValue(graph, "wr", 1);
        const ValueId wrAddr = makeLogicValue(graph, "wr_addr", 2);
        const ValueId wrData = makeLogicValue(graph, "wr_data", 8);
        const ValueId one = addConstant(graph, "one_op", "one", 8, "8'hff");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("reset_done", resetDone);
        graph.bindInputPort("wr", wr);
        graph.bindInputPort("wr_addr", wrAddr);
        graph.bindInputPort("wr_data", wrData);
        const ValueId resetZero = wrapWithResetMux
                                      ? addConstant(graph, "reset_zero_op", "reset_zero", 8, "8'h00")
                                      : ValueId::invalid();

        std::vector<ValueId> reads;
        std::vector<std::string> names;
        for (int i = 0; i < 4; ++i)
        {
            const std::string name = "bank_" + std::to_string(i);
            names.push_back(name);
            const OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol(name));
            graph.setAttr(reg, "width", static_cast<int64_t>(8));
            graph.setAttr(reg, "isSigned", false);

            const ValueId q = makeLogicValue(graph, "bank_q_" + std::to_string(i), 8);
            const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                           graph.internSymbol("bank_read_" + std::to_string(i)));
            graph.setAttr(read, "regSymbol", name);
            graph.addResult(read, q);
            reads.push_back(q);
        }
        graph.bindOutputPort("tap2", reads[2]);

        for (int i = 0; i < 4; ++i)
        {
            const std::string suffix = std::to_string(i);
            const ValueId idx = addConstant(graph,
                                            "bank_idx_op_" + suffix,
                                            "bank_idx_" + suffix,
                                            2,
                                            "2'd" + suffix);
            const ValueId addrEq = addOp(graph,
                                         OperationKind::kEq,
                                         "bank_eq_op_" + suffix,
                                         "bank_eq_" + suffix,
                                         1,
                                         std::array<ValueId, 2>{wrAddr, idx});
            const ValueId hit = addOp(graph,
                                      OperationKind::kAnd,
                                      "bank_hit_op_" + suffix,
                                      "bank_hit_" + suffix,
                                      1,
                                      std::array<ValueId, 2>{wr, addrEq});
            ValueId next = addOp(graph,
                                 OperationKind::kMux,
                                 "bank_next_op_" + suffix,
                                 "bank_next_" + suffix,
                                 8,
                                 std::array<ValueId, 3>{hit, wrData, reads[i]});
            if (wrapWithResetMux)
            {
                next = addOp(graph,
                             OperationKind::kMux,
                             "bank_reset_mux_op_" + suffix,
                             "bank_reset_mux_" + suffix,
                             8,
                             std::array<ValueId, 3>{resetDone, next, resetZero});
            }

            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("bank_write_" + suffix));
            graph.setAttr(write, "regSymbol", names[i]);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, en);
            graph.addOperand(write, next);
            graph.addOperand(write, one);
            graph.addOperand(write, clk);
        }

        return design;
    }

    Design buildBundlePipelineDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId en = makeLogicValue(graph, "en", 1);
        const ValueId validIn = makeLogicValue(graph, "valid_in", 1);
        const ValueId dataIn = makeLogicValue(graph, "data_in", 8);
        const ValueId validMask = addConstant(graph, "valid_mask_op", "valid_mask", 1, "1'b1");
        const ValueId dataMask = addConstant(graph, "data_mask_op", "data_mask", 8, "8'hff");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("valid_in", validIn);
        graph.bindInputPort("data_in", dataIn);

        const std::vector<std::pair<std::string, int32_t>> fields{{"valid", 1}, {"data", 8}};
        std::vector<std::vector<ValueId>> reads(fields.size());
        for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
        {
            const auto &[field, width] = fields[fieldIndex];
            for (int stage = 0; stage < 3; ++stage)
            {
                const std::string regName = stage == 0
                                                ? "pipe$REG_" + field
                                                : "pipe$REG_" + std::to_string(stage) + "_" + field;
                const OperationId reg = graph.createOperation(OperationKind::kRegister,
                                                              graph.internSymbol(regName));
                graph.setAttr(reg, "width", static_cast<int64_t>(width));
                graph.setAttr(reg, "isSigned", false);
                graph.setAttr(reg, "initValue", std::string(width == 1 ? "1'b0" : "8'h00"));

                const ValueId q = makeLogicValue(graph,
                                                 "pipe_q_" + std::to_string(stage) + "_" + field,
                                                 width);
                const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                               graph.internSymbol("pipe_read_" + std::to_string(stage) + "_" + field));
                graph.setAttr(read, "regSymbol", regName);
                graph.addResult(read, q);
                reads[fieldIndex].push_back(q);
            }
        }
        graph.bindOutputPort("valid_out", reads[0][2]);
        graph.bindOutputPort("data_out", reads[1][2]);

        for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
        {
            const auto &[field, width] = fields[fieldIndex];
            for (int stage = 0; stage < 3; ++stage)
            {
                const std::string regName = stage == 0
                                                ? "pipe$REG_" + field
                                                : "pipe$REG_" + std::to_string(stage) + "_" + field;
                const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("pipe_write_" + std::to_string(stage) + "_" + field));
                graph.setAttr(write, "regSymbol", regName);
                graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
                graph.addOperand(write, en);
                graph.addOperand(write,
                                 stage == 0
                                     ? (field == "valid" ? validIn : dataIn)
                                     : reads[fieldIndex][static_cast<std::size_t>(stage - 1)]);
                graph.addOperand(write, width == 1 ? validMask : dataMask);
                graph.addOperand(write, clk);
            }
        }

        return design;
    }

    Design buildPackedMuxBundlePipelineDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId en = makeLogicValue(graph, "en", 1);
        const ValueId resetDone = makeLogicValue(graph, "reset_done", 1);
        const ValueId aIn = makeLogicValue(graph, "a_in", 8);
        const ValueId bIn = makeLogicValue(graph, "b_in", 8);
        const ValueId dataMask = addConstant(graph, "data_mask_op", "data_mask", 8, "8'hff");
        const ValueId zero16 = addConstant(graph, "zero16_op", "zero16", 16, "16'h0");
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("reset_done", resetDone);
        graph.bindInputPort("a_in", aIn);
        graph.bindInputPort("b_in", bIn);

        const std::vector<std::string> fields{"a", "b"};
        std::vector<std::vector<ValueId>> reads(fields.size());
        for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
        {
            const std::string &field = fields[fieldIndex];
            for (int stage = 0; stage < 3; ++stage)
            {
                const std::string regName = stage == 0
                                                ? "packed$REG_" + field
                                                : "packed$REG_" + std::to_string(stage) + "_" + field;
                const OperationId reg = graph.createOperation(OperationKind::kRegister,
                                                              graph.internSymbol(regName));
                graph.setAttr(reg, "width", static_cast<int64_t>(8));
                graph.setAttr(reg, "isSigned", false);
                graph.setAttr(reg, "initValue", std::string("8'h00"));

                const ValueId q = makeLogicValue(graph,
                                                 "packed_q_" + std::to_string(stage) + "_" + field,
                                                 8);
                const OperationId read = graph.createOperation(OperationKind::kRegisterReadPort,
                                                               graph.internSymbol("packed_read_" + std::to_string(stage) + "_" + field));
                graph.setAttr(read, "regSymbol", regName);
                graph.addResult(read, q);
                reads[fieldIndex].push_back(q);
            }
        }
        graph.bindOutputPort("a_out", reads[0][2]);
        graph.bindOutputPort("b_out", reads[1][2]);

        for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
        {
            const std::string &field = fields[fieldIndex];
            const std::string regName = "packed$REG_" + field;
            const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                            graph.internSymbol("packed_write_0_" + field));
            graph.setAttr(write, "regSymbol", regName);
            graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
            graph.addOperand(write, en);
            graph.addOperand(write, field == "a" ? aIn : bIn);
            graph.addOperand(write, dataMask);
            graph.addOperand(write, clk);
        }

        for (int stage = 1; stage < 3; ++stage)
        {
            const ValueId packedPrev = addOp(graph,
                                             OperationKind::kConcat,
                                             "packed_concat_" + std::to_string(stage),
                                             "packed_prev_" + std::to_string(stage),
                                             16,
                                             std::array<ValueId, 2>{reads[1][static_cast<std::size_t>(stage - 1)],
                                                                    reads[0][static_cast<std::size_t>(stage - 1)]});
            const ValueId resetByte = addOp(graph,
                                            OperationKind::kReplicate,
                                            "packed_repl_" + std::to_string(stage),
                                            "packed_reset_byte_" + std::to_string(stage),
                                            8,
                                            std::array<ValueId, 1>{resetDone});
            OperationId replDef = graph.valueDef(resetByte);
            graph.setAttr(replDef, "rep", static_cast<int64_t>(8));
            const ValueId packedMask = addOp(graph,
                                             OperationKind::kConcat,
                                             "packed_mask_concat_" + std::to_string(stage),
                                             "packed_mask_" + std::to_string(stage),
                                             16,
                                             std::array<ValueId, 2>{resetByte, resetByte});
            const ValueId invMask = addOp(graph,
                                          OperationKind::kNot,
                                          "packed_not_" + std::to_string(stage),
                                          "packed_inv_mask_" + std::to_string(stage),
                                          16,
                                          std::array<ValueId, 1>{packedMask});
            const ValueId trueData = addOp(graph,
                                           OperationKind::kAnd,
                                           "packed_true_" + std::to_string(stage),
                                           "packed_true_data_" + std::to_string(stage),
                                           16,
                                           std::array<ValueId, 2>{packedPrev, packedMask});
            const ValueId falseData = addOp(graph,
                                            OperationKind::kAnd,
                                            "packed_false_" + std::to_string(stage),
                                            "packed_false_data_" + std::to_string(stage),
                                            16,
                                            std::array<ValueId, 2>{zero16, invMask});
            const ValueId packedNext = addOp(graph,
                                             OperationKind::kOr,
                                             "packed_or_" + std::to_string(stage),
                                             "packed_next_" + std::to_string(stage),
                                             16,
                                             std::array<ValueId, 2>{trueData, falseData});
            const ValueId aData = addOp(graph,
                                        OperationKind::kSliceStatic,
                                        "packed_slice_a_" + std::to_string(stage),
                                        "packed_a_data_" + std::to_string(stage),
                                        8,
                                        std::array<ValueId, 1>{packedNext});
            graph.setAttr(graph.valueDef(aData), "sliceStart", static_cast<int64_t>(0));
            graph.setAttr(graph.valueDef(aData), "sliceEnd", static_cast<int64_t>(7));
            const ValueId bData = addOp(graph,
                                        OperationKind::kSliceStatic,
                                        "packed_slice_b_" + std::to_string(stage),
                                        "packed_b_data_" + std::to_string(stage),
                                        8,
                                        std::array<ValueId, 1>{packedNext});
            graph.setAttr(graph.valueDef(bData), "sliceStart", static_cast<int64_t>(8));
            graph.setAttr(graph.valueDef(bData), "sliceEnd", static_cast<int64_t>(15));

            for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
            {
                const std::string &field = fields[fieldIndex];
                const std::string regName = "packed$REG_" + std::to_string(stage) + "_" + field;
                const OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                                graph.internSymbol("packed_write_" + std::to_string(stage) + "_" + field));
                graph.setAttr(write, "regSymbol", regName);
                graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});
                graph.addOperand(write, en);
                graph.addOperand(write, fieldIndex == 0 ? aData : bData);
                graph.addOperand(write, dataMask);
                graph.addOperand(write, clk);
            }
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

    int checkBitsetRewrite(Design &design, std::string_view label)
    {
        if (const int status = runMergeReg(design, true, label); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail(std::string(label) + ": missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 1 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 1 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 1)
        {
            return fail(std::string(label) + ": bitset should become one wide register/read/write");
        }
        if (countOps(*graph, OperationKind::kReduceOr) != 2)
        {
            return fail(std::string(label) + ": bitset OR chains should become two reduce-or ops");
        }
        const ValueId tap2 = graph->outputPortValue("tap2");
        if (!tap2.valid() || graph->getOperation(graph->valueDef(tap2)).kind() != OperationKind::kSliceStatic)
        {
            return fail(std::string(label) + ": bitset scalar tap should be rebound to a static slice");
        }
        return 0;
    }

    int checkIndexedBankRewrite(Design &design, std::string_view label)
    {
        if (const int status = runMergeReg(design, true, label); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail(std::string(label) + ": missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 1 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 1 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 1)
        {
            return fail(std::string(label) + ": indexed bank should become one wide register/read/write");
        }
        OperationId wideReg;
        for (const OperationId opId : graph->operations())
        {
            if (graph->getOperation(opId).kind() == OperationKind::kRegister)
            {
                wideReg = opId;
                break;
            }
        }
        if (!wideReg.valid() || getAttr<int64_t>(graph->getOperation(wideReg), "width").value_or(0) != 32)
        {
            return fail(std::string(label) + ": indexed bank wide register should be 32 bits");
        }
        const ValueId tap2 = graph->outputPortValue("tap2");
        if (!tap2.valid() || graph->getOperation(graph->valueDef(tap2)).kind() != OperationKind::kSliceStatic)
        {
            return fail(std::string(label) + ": indexed bank scalar tap should be rebound to a static slice");
        }
        return 0;
    }

    int checkBundlePipelineRewrite(Design &design)
    {
        if (const int status = runMergeReg(design, true, "bundle pipeline"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("bundle pipeline: missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 1 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 1 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 1)
        {
            return fail("bundle pipeline should become one wide register/read/write");
        }
        OperationId wideReg;
        for (const OperationId opId : graph->operations())
        {
            if (graph->getOperation(opId).kind() == OperationKind::kRegister)
            {
                wideReg = opId;
                break;
            }
        }
        if (!wideReg.valid() || getAttr<int64_t>(graph->getOperation(wideReg), "width").value_or(0) != 27)
        {
            return fail("bundle pipeline wide register should be 27 bits");
        }
        const ValueId validOut = graph->outputPortValue("valid_out");
        const ValueId dataOut = graph->outputPortValue("data_out");
        if (!validOut.valid() || !dataOut.valid() ||
            graph->getOperation(graph->valueDef(validOut)).kind() != OperationKind::kSliceStatic ||
            graph->getOperation(graph->valueDef(dataOut)).kind() != OperationKind::kSliceStatic)
        {
            return fail("bundle pipeline outputs should be rebound to wide-register slices");
        }
        return 0;
    }

    int checkPackedMuxBundlePipelineRewrite(Design &design)
    {
        if (const int status = runMergeReg(design, true, "packed mux bundle pipeline"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("packed mux bundle pipeline: missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 1 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 1 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 1)
        {
            return fail("packed mux bundle pipeline should become one wide register/read/write");
        }
        OperationId wideReg;
        for (const OperationId opId : graph->operations())
        {
            if (graph->getOperation(opId).kind() == OperationKind::kRegister)
            {
                wideReg = opId;
                break;
            }
        }
        if (!wideReg.valid() || getAttr<int64_t>(graph->getOperation(wideReg), "width").value_or(0) != 48)
        {
            return fail("packed mux bundle pipeline wide register should be 48 bits");
        }
        return 0;
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
        PassManager manager;
        manager.addPass(std::make_unique<MergeRegPass>());
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
        Design design = buildShiftChainDesign();
        if (const int status = runMergeReg(design, true, "shift chain"); status != 0)
        {
            return status;
        }
        Graph *graph = design.findGraph("top");
        if (graph == nullptr)
        {
            return fail("missing graph");
        }
        if (countOps(*graph, OperationKind::kRegister) != 1 ||
            countOps(*graph, OperationKind::kRegisterReadPort) != 1 ||
            countOps(*graph, OperationKind::kRegisterWritePort) != 1)
        {
            return fail("shift chain should become one wide register/read/write");
        }
        if (countOps(*graph, OperationKind::kSliceStatic) != 4)
        {
            return fail("shift chain rewrite should expose old scalar reads as static slices");
        }

        OperationId wideReg;
        for (const OperationId opId : graph->operations())
        {
            if (graph->getOperation(opId).kind() == OperationKind::kRegister)
            {
                wideReg = opId;
                break;
            }
        }
        if (!wideReg.valid())
        {
            return fail("missing rewritten wide register");
        }
        const Operation reg = graph->getOperation(wideReg);
        if (getAttr<int64_t>(reg, "width").value_or(0) != 4)
        {
            return fail("wide register width should equal packed member count");
        }
        const ValueId tap = graph->outputPortValue("tap");
        if (!tap.valid())
        {
            return fail("output tap should still be bound");
        }
        const OperationId tapDef = graph->valueDef(tap);
        if (!tapDef.valid())
        {
            return fail("output tap should have a defining slice op");
        }
        if (graph->getOperation(tapDef).kind() != OperationKind::kSliceStatic)
        {
            return fail("output tap should be rebound to a slice of the wide register read");
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("shift-chain case threw: ") + ex.what());
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
        Design design = buildBitsetDesign();
        if (const int status = checkBitsetRewrite(design, "bitset"); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("bitset case threw: ") + ex.what());
    }

    try
    {
        Design design = buildBitsetDesign(true);
        if (const int status = checkBitsetRewrite(design, "bitset reset mux"); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("bitset reset mux case threw: ") + ex.what());
    }

    try
    {
        Design design = buildIndexedBankDesign();
        if (const int status = checkIndexedBankRewrite(design, "indexed bank"); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("indexed bank case threw: ") + ex.what());
    }

    try
    {
        Design design = buildIndexedBankDesign(true);
        if (const int status = checkIndexedBankRewrite(design, "indexed bank reset mux"); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("indexed bank reset mux case threw: ") + ex.what());
    }

    try
    {
        Design design = buildBundlePipelineDesign();
        if (const int status = checkBundlePipelineRewrite(design); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("bundle pipeline case threw: ") + ex.what());
    }

    try
    {
        Design design = buildPackedMuxBundlePipelineDesign();
        if (const int status = checkPackedMuxBundlePipelineRewrite(design); status != 0)
        {
            return status;
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("packed mux bundle pipeline case threw: ") + ex.what());
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
