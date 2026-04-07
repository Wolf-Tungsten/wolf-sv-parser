#include "core/grh.hpp"
#include "core/transform.hpp"
#include "emit/grhsim_cpp.hpp"
#include "transform/activity_schedule.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace wolvrix::lib::emit;
using namespace wolvrix::lib::grh;
using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_grhsim_cpp] " << message << '\n';
        return 1;
    }

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    ValueId makeLogicValue(Graph &graph, std::string_view name, int32_t width, bool isSigned = false)
    {
        return graph.createValue(graph.internSymbol(std::string(name)), width, isSigned, ValueType::Logic);
    }

    ValueId makeStringValue(Graph &graph, std::string_view name)
    {
        return graph.createValue(graph.internSymbol(std::string(name)), 0, false, ValueType::String);
    }

    ValueId addConstant(Graph &graph,
                        std::string_view opName,
                        std::string_view valueName,
                        int32_t width,
                        std::string literal,
                        ValueType type = ValueType::Logic)
    {
        ValueId value = graph.createValue(graph.internSymbol(std::string(valueName)), width, false, type);
        OperationId op = graph.createOperation(OperationKind::kConstant,
                                               graph.internSymbol(std::string(opName)));
        graph.addResult(op, value);
        graph.setAttr(op, "constValue", std::move(literal));
        return value;
    }

    std::string allOnesLiteral(int32_t width)
    {
        return std::to_string(width) + "'b" + std::string(static_cast<std::size_t>(width), '1');
    }

    Design buildDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");
        design.markAsTop(graph.symbol());

        ValueId clk = makeLogicValue(graph, "clk", 1);
        ValueId en = makeLogicValue(graph, "en", 1);
        ValueId a = makeLogicValue(graph, "a", 8);
        ValueId comb = makeLogicValue(graph, "comb", 8);
        ValueId b = makeLogicValue(graph, "b", 8);
        ValueId sh = makeLogicValue(graph, "sh", 3);
        ValueId rep2 = makeLogicValue(graph, "rep2", 2);
        ValueId sa = makeLogicValue(graph, "sa", 8, true);
        ValueId wideIn = makeLogicValue(graph, "wide_in", 130);
        ValueId wideAddr = makeLogicValue(graph, "wide_addr", 2);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("a", a);
        graph.bindInputPort("comb", comb);
        graph.bindInputPort("b", b);
        graph.bindInputPort("sh", sh);
        graph.bindInputPort("rep2", rep2);
        graph.bindInputPort("sa", sa);
        graph.bindInputPort("wide_in", wideIn);
        graph.bindInputPort("wide_addr", wideAddr);

        OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol("reg_q"));
        graph.setAttr(reg, "width", static_cast<int64_t>(8));
        graph.setAttr(reg, "isSigned", false);

        ValueId regQ = makeLogicValue(graph, "reg_q_read", 8);
        OperationId regRead = graph.createOperation(OperationKind::kRegisterReadPort,
                                                    graph.internSymbol("reg_q_read_op"));
        graph.addResult(regRead, regQ);
        graph.setAttr(regRead, "regSymbol", std::string("reg_q"));

        ValueId mask = addConstant(graph, "const_mask", "mask", 8, "8'hFF");
        ValueId wideZero = addConstant(graph, "const_wide_zero", "wide_zero", 130, "130'h0");
        ValueId wideOne = addConstant(graph, "const_wide_one", "wide_one", 130, "130'h1");
        ValueId wideTwo = addConstant(graph, "const_wide_two", "wide_two", 130, "130'h2");
        ValueId smallTwo = addConstant(graph, "const_small_two", "small_two", 2, "2'd2");
        ValueId wideMask = addConstant(graph, "const_wide_mask", "wide_mask", 130, allOnesLiteral(130));
        ValueId fmt = addConstant(graph, "const_fmt", "fmt", 0, "\"q=%0d\"", ValueType::String);

        ValueId sum = makeLogicValue(graph, "sum", 8);
        OperationId add = graph.createOperation(OperationKind::kAdd, graph.internSymbol("sum_add"));
        graph.addOperand(add, regQ);
        graph.addOperand(add, a);
        graph.addResult(add, sum);
        graph.bindOutputPort("y", sum);

        ValueId mulY = makeLogicValue(graph, "mul_y", 8);
        OperationId mul = graph.createOperation(OperationKind::kMul, graph.internSymbol("mul_op"));
        graph.addOperand(mul, comb);
        graph.addOperand(mul, b);
        graph.addResult(mul, mulY);
        graph.bindOutputPort("mul_y", mulY);

        ValueId divY = makeLogicValue(graph, "div_y", 8);
        OperationId div = graph.createOperation(OperationKind::kDiv, graph.internSymbol("div_op"));
        graph.addOperand(div, comb);
        graph.addOperand(div, b);
        graph.addResult(div, divY);
        graph.bindOutputPort("div_y", divY);

        ValueId modY = makeLogicValue(graph, "mod_y", 8);
        OperationId mod = graph.createOperation(OperationKind::kMod, graph.internSymbol("mod_op"));
        graph.addOperand(mod, comb);
        graph.addOperand(mod, b);
        graph.addResult(mod, modY);
        graph.bindOutputPort("mod_y", modY);

        ValueId shlY = makeLogicValue(graph, "shl_y", 8);
        OperationId shlOp = graph.createOperation(OperationKind::kShl, graph.internSymbol("shl_op"));
        graph.addOperand(shlOp, comb);
        graph.addOperand(shlOp, sh);
        graph.addResult(shlOp, shlY);
        graph.bindOutputPort("shl_y", shlY);

        ValueId lshrY = makeLogicValue(graph, "lshr_y", 8);
        OperationId lshrOp = graph.createOperation(OperationKind::kLShr, graph.internSymbol("lshr_op"));
        graph.addOperand(lshrOp, comb);
        graph.addOperand(lshrOp, sh);
        graph.addResult(lshrOp, lshrY);
        graph.bindOutputPort("lshr_y", lshrY);

        ValueId ashrY = makeLogicValue(graph, "ashr_y", 8, true);
        OperationId ashrOp = graph.createOperation(OperationKind::kAShr, graph.internSymbol("ashr_op"));
        graph.addOperand(ashrOp, sa);
        graph.addOperand(ashrOp, sh);
        graph.addResult(ashrOp, ashrY);
        graph.bindOutputPort("ashr_y", ashrY);

        ValueId redOrY = makeLogicValue(graph, "red_or_y", 1);
        OperationId redOrOp = graph.createOperation(OperationKind::kReduceOr, graph.internSymbol("red_or_op"));
        graph.addOperand(redOrOp, comb);
        graph.addResult(redOrOp, redOrY);
        graph.bindOutputPort("red_or_y", redOrY);

        ValueId redXorY = makeLogicValue(graph, "red_xor_y", 1);
        OperationId redXorOp = graph.createOperation(OperationKind::kReduceXor, graph.internSymbol("red_xor_op"));
        graph.addOperand(redXorOp, comb);
        graph.addResult(redXorOp, redXorY);
        graph.bindOutputPort("red_xor_y", redXorY);

        ValueId sliceY = makeLogicValue(graph, "slice_y", 3);
        OperationId sliceOp = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("slice_dyn_op"));
        graph.addOperand(sliceOp, comb);
        graph.addOperand(sliceOp, sh);
        graph.addResult(sliceOp, sliceY);
        graph.setAttr(sliceOp, "sliceWidth", static_cast<int64_t>(3));
        graph.bindOutputPort("slice_y", sliceY);

        ValueId repY = makeLogicValue(graph, "rep_y", 8);
        OperationId repOp = graph.createOperation(OperationKind::kReplicate, graph.internSymbol("rep_op"));
        graph.addOperand(repOp, rep2);
        graph.addResult(repOp, repY);
        graph.setAttr(repOp, "rep", static_cast<int64_t>(4));
        graph.bindOutputPort("rep_y", repY);

        OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("reg_q_write"));
        graph.addOperand(write, en);
        graph.addOperand(write, sum);
        graph.addOperand(write, mask);
        graph.addOperand(write, clk);
        graph.setAttr(write, "regSymbol", std::string("reg_q"));
        graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});

        OperationId wideReg = graph.createOperation(OperationKind::kRegister, graph.internSymbol("wide_reg_q"));
        graph.setAttr(wideReg, "width", static_cast<int64_t>(130));
        graph.setAttr(wideReg, "isSigned", false);

        ValueId wideRegQ = makeLogicValue(graph, "wide_reg_q_read", 130);
        OperationId wideRegRead = graph.createOperation(OperationKind::kRegisterReadPort,
                                                        graph.internSymbol("wide_reg_q_read_op"));
        graph.addResult(wideRegRead, wideRegQ);
        graph.setAttr(wideRegRead, "regSymbol", std::string("wide_reg_q"));
        graph.bindOutputPort("wide_y", wideRegQ);

        OperationId wideRegWrite = graph.createOperation(OperationKind::kRegisterWritePort,
                                                         graph.internSymbol("wide_reg_q_write"));
        graph.addOperand(wideRegWrite, en);
        graph.addOperand(wideRegWrite, wideIn);
        graph.addOperand(wideRegWrite, wideMask);
        graph.addOperand(wideRegWrite, clk);
        graph.setAttr(wideRegWrite, "regSymbol", std::string("wide_reg_q"));
        graph.setAttr(wideRegWrite, "eventEdge", std::vector<std::string>{"posedge"});

        OperationId wideMem = graph.createOperation(OperationKind::kMemory, graph.internSymbol("wide_mem"));
        graph.setAttr(wideMem, "width", static_cast<int64_t>(130));
        graph.setAttr(wideMem, "row", static_cast<int64_t>(4));
        graph.setAttr(wideMem, "isSigned", false);

        ValueId wideMemQ = makeLogicValue(graph, "wide_mem_q", 130);
        OperationId wideMemRead = graph.createOperation(OperationKind::kMemoryReadPort,
                                                        graph.internSymbol("wide_mem_read"));
        graph.addOperand(wideMemRead, wideAddr);
        graph.addResult(wideMemRead, wideMemQ);
        graph.setAttr(wideMemRead, "memSymbol", std::string("wide_mem"));
        graph.bindOutputPort("wide_mem_y", wideMemQ);

        OperationId wideMemWrite = graph.createOperation(OperationKind::kMemoryWritePort,
                                                         graph.internSymbol("wide_mem_write"));
        graph.addOperand(wideMemWrite, en);
        graph.addOperand(wideMemWrite, wideAddr);
        graph.addOperand(wideMemWrite, wideIn);
        graph.addOperand(wideMemWrite, wideMask);
        graph.addOperand(wideMemWrite, clk);
        graph.setAttr(wideMemWrite, "memSymbol", std::string("wide_mem"));
        graph.setAttr(wideMemWrite, "eventEdge", std::vector<std::string>{"posedge"});

        ValueId wideAddY = makeLogicValue(graph, "wide_add_y", 130);
        OperationId wideAdd = graph.createOperation(OperationKind::kAdd, graph.internSymbol("wide_add_op"));
        graph.addOperand(wideAdd, wideIn);
        graph.addOperand(wideAdd, wideOne);
        graph.addResult(wideAdd, wideAddY);
        graph.bindOutputPort("wide_add_y", wideAddY);

        ValueId wideSubY = makeLogicValue(graph, "wide_sub_y", 130);
        OperationId wideSub = graph.createOperation(OperationKind::kSub, graph.internSymbol("wide_sub_op"));
        graph.addOperand(wideSub, wideIn);
        graph.addOperand(wideSub, wideOne);
        graph.addResult(wideSub, wideSubY);
        graph.bindOutputPort("wide_sub_y", wideSubY);

        ValueId wideMulY = makeLogicValue(graph, "wide_mul_y", 132);
        OperationId wideMul = graph.createOperation(OperationKind::kMul, graph.internSymbol("wide_mul_op"));
        graph.addOperand(wideMul, wideIn);
        graph.addOperand(wideMul, smallTwo);
        graph.addResult(wideMul, wideMulY);
        graph.bindOutputPort("wide_mul_y", wideMulY);

        ValueId wideDivY = makeLogicValue(graph, "wide_div_y", 130);
        OperationId wideDiv = graph.createOperation(OperationKind::kDiv, graph.internSymbol("wide_div_op"));
        graph.addOperand(wideDiv, wideIn);
        graph.addOperand(wideDiv, wideTwo);
        graph.addResult(wideDiv, wideDivY);
        graph.bindOutputPort("wide_div_y", wideDivY);

        ValueId wideModY = makeLogicValue(graph, "wide_mod_y", 130);
        OperationId wideMod = graph.createOperation(OperationKind::kMod, graph.internSymbol("wide_mod_op"));
        graph.addOperand(wideMod, wideIn);
        graph.addOperand(wideMod, wideTwo);
        graph.addResult(wideMod, wideModY);
        graph.bindOutputPort("wide_mod_y", wideModY);

        ValueId wideAndY = makeLogicValue(graph, "wide_and_y", 130);
        OperationId wideAnd = graph.createOperation(OperationKind::kAnd, graph.internSymbol("wide_and_op"));
        graph.addOperand(wideAnd, wideIn);
        graph.addOperand(wideAnd, wideMask);
        graph.addResult(wideAnd, wideAndY);
        graph.bindOutputPort("wide_and_y", wideAndY);

        ValueId wideOrY = makeLogicValue(graph, "wide_or_y", 130);
        OperationId wideOr = graph.createOperation(OperationKind::kOr, graph.internSymbol("wide_or_op"));
        graph.addOperand(wideOr, wideIn);
        graph.addOperand(wideOr, wideZero);
        graph.addResult(wideOr, wideOrY);
        graph.bindOutputPort("wide_or_y", wideOrY);

        ValueId wideXorY = makeLogicValue(graph, "wide_xor_y", 130);
        OperationId wideXor = graph.createOperation(OperationKind::kXor, graph.internSymbol("wide_xor_op"));
        graph.addOperand(wideXor, wideIn);
        graph.addOperand(wideXor, wideZero);
        graph.addResult(wideXor, wideXorY);
        graph.bindOutputPort("wide_xor_y", wideXorY);

        ValueId wideXnorY = makeLogicValue(graph, "wide_xnor_y", 130);
        OperationId wideXnor = graph.createOperation(OperationKind::kXnor, graph.internSymbol("wide_xnor_op"));
        graph.addOperand(wideXnor, wideIn);
        graph.addOperand(wideXnor, wideMask);
        graph.addResult(wideXnor, wideXnorY);
        graph.bindOutputPort("wide_xnor_y", wideXnorY);

        ValueId wideNotY = makeLogicValue(graph, "wide_not_y", 130);
        OperationId wideNot = graph.createOperation(OperationKind::kNot, graph.internSymbol("wide_not_op"));
        graph.addOperand(wideNot, wideIn);
        graph.addResult(wideNot, wideNotY);
        graph.bindOutputPort("wide_not_y", wideNotY);

        ValueId wideEqY = makeLogicValue(graph, "wide_eq_y", 1);
        OperationId wideEq = graph.createOperation(OperationKind::kEq, graph.internSymbol("wide_eq_op"));
        graph.addOperand(wideEq, wideIn);
        graph.addOperand(wideEq, wideIn);
        graph.addResult(wideEq, wideEqY);
        graph.bindOutputPort("wide_eq_y", wideEqY);

        ValueId wideLtY = makeLogicValue(graph, "wide_lt_y", 1);
        OperationId wideLt = graph.createOperation(OperationKind::kLt, graph.internSymbol("wide_lt_op"));
        graph.addOperand(wideLt, wideOne);
        graph.addOperand(wideLt, wideIn);
        graph.addResult(wideLt, wideLtY);
        graph.bindOutputPort("wide_lt_y", wideLtY);

        ValueId wideLogicAndY = makeLogicValue(graph, "wide_logic_and_y", 1);
        OperationId wideLogicAnd = graph.createOperation(OperationKind::kLogicAnd, graph.internSymbol("wide_logic_and_op"));
        graph.addOperand(wideLogicAnd, wideIn);
        graph.addOperand(wideLogicAnd, wideOne);
        graph.addResult(wideLogicAnd, wideLogicAndY);
        graph.bindOutputPort("wide_logic_and_y", wideLogicAndY);

        ValueId wideReduceOrY = makeLogicValue(graph, "wide_reduce_or_y", 1);
        OperationId wideReduceOr = graph.createOperation(OperationKind::kReduceOr, graph.internSymbol("wide_reduce_or_op"));
        graph.addOperand(wideReduceOr, wideIn);
        graph.addResult(wideReduceOr, wideReduceOrY);
        graph.bindOutputPort("wide_reduce_or_y", wideReduceOrY);

        ValueId wideShlY = makeLogicValue(graph, "wide_shl_y", 130);
        OperationId wideShl = graph.createOperation(OperationKind::kShl, graph.internSymbol("wide_shl_op"));
        graph.addOperand(wideShl, wideIn);
        graph.addOperand(wideShl, wideAddr);
        graph.addResult(wideShl, wideShlY);
        graph.bindOutputPort("wide_shl_y", wideShlY);

        ValueId wideLshrY = makeLogicValue(graph, "wide_lshr_y", 130);
        OperationId wideLshr = graph.createOperation(OperationKind::kLShr, graph.internSymbol("wide_lshr_op"));
        graph.addOperand(wideLshr, wideIn);
        graph.addOperand(wideLshr, wideAddr);
        graph.addResult(wideLshr, wideLshrY);
        graph.bindOutputPort("wide_lshr_y", wideLshrY);

        ValueId wideAshrY = makeLogicValue(graph, "wide_ashr_y", 130);
        OperationId wideAshr = graph.createOperation(OperationKind::kAShr, graph.internSymbol("wide_ashr_op"));
        graph.addOperand(wideAshr, wideMask);
        graph.addOperand(wideAshr, wideAddr);
        graph.addResult(wideAshr, wideAshrY);
        graph.bindOutputPort("wide_ashr_y", wideAshrY);

        ValueId wideMuxY = makeLogicValue(graph, "wide_mux_y", 130);
        OperationId wideMux = graph.createOperation(OperationKind::kMux, graph.internSymbol("wide_mux_op"));
        graph.addOperand(wideMux, en);
        graph.addOperand(wideMux, wideIn);
        graph.addOperand(wideMux, wideZero);
        graph.addResult(wideMux, wideMuxY);
        graph.bindOutputPort("wide_mux_y", wideMuxY);

        ValueId wideConcatY = makeLogicValue(graph, "wide_concat_y", 132);
        OperationId wideConcat = graph.createOperation(OperationKind::kConcat, graph.internSymbol("wide_concat_op"));
        graph.addOperand(wideConcat, wideAddr);
        graph.addOperand(wideConcat, wideIn);
        graph.addResult(wideConcat, wideConcatY);
        graph.bindOutputPort("wide_concat_y", wideConcatY);

        ValueId wideRepY = makeLogicValue(graph, "wide_rep_y", 260);
        OperationId wideRep = graph.createOperation(OperationKind::kReplicate, graph.internSymbol("wide_rep_op"));
        graph.addOperand(wideRep, wideIn);
        graph.addResult(wideRep, wideRepY);
        graph.setAttr(wideRep, "rep", static_cast<int64_t>(2));
        graph.bindOutputPort("wide_rep_y", wideRepY);

        ValueId wideSliceStaticY = makeLogicValue(graph, "wide_slice_static_y", 65);
        OperationId wideSliceStatic = graph.createOperation(OperationKind::kSliceStatic, graph.internSymbol("wide_slice_static_op"));
        graph.addOperand(wideSliceStatic, wideIn);
        graph.addResult(wideSliceStatic, wideSliceStaticY);
        graph.setAttr(wideSliceStatic, "sliceStart", static_cast<int64_t>(5));
        graph.setAttr(wideSliceStatic, "sliceEnd", static_cast<int64_t>(69));
        graph.bindOutputPort("wide_slice_static_y", wideSliceStaticY);

        ValueId wideSliceDynY = makeLogicValue(graph, "wide_slice_dyn_y", 65);
        OperationId wideSliceDyn = graph.createOperation(OperationKind::kSliceDynamic, graph.internSymbol("wide_slice_dyn_op"));
        graph.addOperand(wideSliceDyn, wideIn);
        graph.addOperand(wideSliceDyn, wideAddr);
        graph.addResult(wideSliceDyn, wideSliceDynY);
        graph.setAttr(wideSliceDyn, "sliceWidth", static_cast<int64_t>(65));
        graph.bindOutputPort("wide_slice_dyn_y", wideSliceDynY);

        OperationId display = graph.createOperation(OperationKind::kSystemTask,
                                                    graph.internSymbol("display_task"));
        graph.addOperand(display, en);
        graph.addOperand(display, fmt);
        graph.addOperand(display, sum);
        graph.addOperand(display, clk);
        graph.setAttr(display, "name", std::string("display"));
        graph.setAttr(display, "procKind", std::string("always_ff"));
        graph.setAttr(display, "hasTiming", false);
        graph.setAttr(display, "eventEdge", std::vector<std::string>{"posedge"});

        OperationId dpiImport = graph.createOperation(OperationKind::kDpicImport,
                                                      graph.internSymbol("trace_sum"));
        graph.setAttr(dpiImport, "argsDirection", std::vector<std::string>{"input"});
        graph.setAttr(dpiImport, "argsWidth", std::vector<int64_t>{8});
        graph.setAttr(dpiImport, "argsName", std::vector<std::string>{"value"});
        graph.setAttr(dpiImport, "argsSigned", std::vector<bool>{false});
        graph.setAttr(dpiImport, "argsType", std::vector<std::string>{"logic"});
        graph.setAttr(dpiImport, "hasReturn", false);

        OperationId dpi = graph.createOperation(OperationKind::kDpicCall,
                                                graph.internSymbol("trace_dpi_call"));
        graph.addOperand(dpi, en);
        graph.addOperand(dpi, sum);
        graph.addOperand(dpi, clk);
        graph.setAttr(dpi, "targetImportSymbol", std::string("trace_sum"));
        graph.setAttr(dpi, "inArgName", std::vector<std::string>{"value"});
        graph.setAttr(dpi, "outArgName", std::vector<std::string>{});
        graph.setAttr(dpi, "hasReturn", false);
        graph.setAttr(dpi, "eventEdge", std::vector<std::string>{"posedge"});

        return design;
    }

    bool runActivitySchedule(Design &design, SessionStore &session)
    {
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{
            .path = "top",
            .enableReplication = true,
        }));
        PassDiagnostics diags;
        PassManagerResult result = manager.run(design, diags);
        return result.success && !diags.hasError();
    }

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

int main()
{
    try
    {
        Design design = buildDesign();
        SessionStore session;
        if (!runActivitySchedule(design, session))
        {
            return fail("activity-schedule pass failed");
        }

        EmitDiagnostics diag;
        EmitGrhSimCpp emitter(&diag);
        EmitOptions options;
        options.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR) + "/grhsim_cpp";
        options.session = &session;
        options.sessionPathPrefix = std::string("top");

        EmitResult result = emitter.emit(design, options);
        if (!result.success)
        {
            return fail("EmitGrhSimCpp failed");
        }
        if (diag.hasError())
        {
            return fail("EmitGrhSimCpp reported diagnostics errors");
        }
        if (result.artifacts.size() != 6)
        {
            return fail("EmitGrhSimCpp should report six artifacts");
        }

        const std::filesystem::path outDir = std::filesystem::path(*options.outputDir);
        const std::filesystem::path headerPath = outDir / "grhsim_top.hpp";
        const std::filesystem::path statePath = outDir / "grhsim_top_state.cpp";
        const std::filesystem::path evalPath = outDir / "grhsim_top_eval.cpp";
        const std::filesystem::path schedPath = outDir / "grhsim_top_sched_0.cpp";
        const std::filesystem::path makefilePath = outDir / "Makefile";
        if (!std::filesystem::exists(headerPath) || !std::filesystem::exists(statePath) || !std::filesystem::exists(evalPath) ||
            !std::filesystem::exists(schedPath) || !std::filesystem::exists(makefilePath))
        {
            return fail("Expected generated grhsim artifacts to exist");
        }

        const std::string header = readFile(headerPath);
        const std::string state = readFile(statePath);
        const std::string eval = readFile(evalPath);
        const std::string sched = readFile(schedPath);
        const std::string makefile = readFile(makefilePath);

    if (header.find("class GrhSIM_top") == std::string::npos)
    {
        return fail("Missing simulator class declaration");
    }
    if (header.find("kEventPrecomputeMaxOps = 128") == std::string::npos)
    {
        return fail("Missing default event_precompute_max_ops emission");
    }
    if (header.find("void set_clk(bool value);") == std::string::npos)
    {
        return fail("Missing input setter declaration");
    }
    if (header.find("std::uint8_t get_y() const;") == std::string::npos)
    {
        return fail("Missing output getter declaration");
    }
    if (header.find("std::array<std::uint64_t, 3> out_wide_y") == std::string::npos)
    {
        return fail("Missing wide output field declaration");
    }
    if (sched.find("grhsim_assign_words") == std::string::npos ||
        state.find("grhsim_merge_words_masked") == std::string::npos)
    {
        return fail("Missing wide runtime helper usage");
    }
    if (sched.find("grhsim_add_words") == std::string::npos ||
        sched.find("grhsim_mul_words") == std::string::npos ||
        sched.find("grhsim_udiv_words") == std::string::npos ||
        sched.find("grhsim_and_words") == std::string::npos ||
        sched.find("grhsim_compare_unsigned_words") == std::string::npos ||
        sched.find("grhsim_reduce_or_words") == std::string::npos ||
        sched.find("grhsim_shl_words") == std::string::npos ||
        sched.find("grhsim_slice_words") == std::string::npos ||
        sched.find("grhsim_insert_words") == std::string::npos)
    {
        return fail("Missing emitted wide combinational helper calls");
    }
    if (eval.find("seed_head_eval") == std::string::npos || eval.find("event_domain_hit_") == std::string::npos)
    {
        return fail("Missing eval precompute / head activation logic");
    }
    if (sched.find("grhsim_test_bit") == std::string::npos || sched.find("supernode_") == std::string::npos)
    {
        return fail("Missing emitted supernode scheduling code");
    }
    if (sched.find("display_task") == std::string::npos || sched.find("trace_dpi_call") == std::string::npos)
    {
        return fail("Missing side-effect op anchors in schedule file");
    }
    if (header.find("extern \"C\" void trace_sum") == std::string::npos)
    {
        return fail("Missing DPI import declaration");
    }
    if (makefile.find("AR ?= ar") == std::string::npos || makefile.find("all: $(LIB)") == std::string::npos)
    {
        return fail("Missing Makefile skeleton");
    }

        const std::string buildCmd = "make -C " + outDir.string();
        if (std::system(buildCmd.c_str()) != 0)
        {
            return fail("Generated Makefile failed to build grhsim archive");
        }
        if (!std::filesystem::exists(outDir / "libgrhsim_top.a"))
        {
            return fail("Generated grhsim archive missing after make");
        }

        const std::filesystem::path harnessPath = outDir / "grhsim_top_harness.cpp";
    {
        std::ofstream harness(harnessPath);
        if (!harness.is_open())
        {
            return fail("Failed to create grhsim harness");
        }
        harness << "#include \"grhsim_top.hpp\"\n";
        harness << "#include <array>\n";
        harness << "#include <cstddef>\n";
        harness << "#include <cstdint>\n";
        harness << "#include <iostream>\n\n";
        harness << "static std::uint8_t g_last_trace = 0;\n";
        harness << "template <std::size_t N>\n";
        harness << "static bool same_words(const std::array<std::uint64_t, N>& lhs,\n";
        harness << "                       const std::array<std::uint64_t, N>& rhs)\n";
        harness << "{\n";
        harness << "    return lhs == rhs;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static bool get_bit(const std::array<std::uint64_t, N>& value, std::size_t index)\n";
        harness << "{\n";
        harness << "    if (index / 64u >= N) return false;\n";
        harness << "    return (value[index / 64u] & (UINT64_C(1) << (index & 63u))) != 0;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static void put_bit(std::array<std::uint64_t, N>& value, std::size_t index, bool bit)\n";
        harness << "{\n";
        harness << "    if (index / 64u >= N) return;\n";
        harness << "    const std::uint64_t mask = UINT64_C(1) << (index & 63u);\n";
        harness << "    if (bit) value[index / 64u] |= mask;\n";
        harness << "    else value[index / 64u] &= ~mask;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static void trunc_words(std::array<std::uint64_t, N>& value, std::size_t width)\n";
        harness << "{\n";
        harness << "    const std::size_t live_words = (width + 63u) / 64u;\n";
        harness << "    for (std::size_t i = live_words; i < N; ++i) value[i] = 0;\n";
        harness << "    if (live_words != 0) {\n";
        harness << "        const std::size_t tail = width - (live_words - 1u) * 64u;\n";
        harness << "        if (tail < 64u) value[live_words - 1u] &= ((UINT64_C(1) << tail) - 1u);\n";
        harness << "    }\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static std::array<std::uint64_t, N> add_one(std::array<std::uint64_t, N> value, std::size_t width)\n";
        harness << "{\n";
        harness << "    std::uint64_t carry = 1;\n";
        harness << "    for (std::size_t i = 0; i < N && carry != 0; ++i) {\n";
        harness << "        const std::uint64_t next = value[i] + carry;\n";
        harness << "        carry = next < value[i] ? 1 : 0;\n";
        harness << "        value[i] = next;\n";
        harness << "    }\n";
        harness << "    trunc_words(value, width);\n";
        harness << "    return value;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static std::array<std::uint64_t, N> sub_one(std::array<std::uint64_t, N> value, std::size_t width)\n";
        harness << "{\n";
        harness << "    std::uint64_t borrow = 1;\n";
        harness << "    for (std::size_t i = 0; i < N && borrow != 0; ++i) {\n";
        harness << "        const std::uint64_t prev = value[i];\n";
        harness << "        value[i] = prev - borrow;\n";
        harness << "        borrow = prev < borrow ? 1 : 0;\n";
        harness << "    }\n";
        harness << "    trunc_words(value, width);\n";
        harness << "    return value;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static std::array<std::uint64_t, N> not_words(std::array<std::uint64_t, N> value, std::size_t width)\n";
        harness << "{\n";
        harness << "    for (auto& word : value) word = ~word;\n";
        harness << "    trunc_words(value, width);\n";
        harness << "    return value;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static std::array<std::uint64_t, N> shl_words(const std::array<std::uint64_t, N>& value, std::size_t amount, std::size_t width)\n";
        harness << "{\n";
        harness << "    std::array<std::uint64_t, N> out{};\n";
        harness << "    for (std::size_t bit = 0; bit + amount < width; ++bit) if (get_bit(value, bit)) put_bit(out, bit + amount, true);\n";
        harness << "    trunc_words(out, width);\n";
        harness << "    return out;\n";
        harness << "}\n\n";
        harness << "template <std::size_t N>\n";
        harness << "static std::array<std::uint64_t, N> lshr_words(const std::array<std::uint64_t, N>& value, std::size_t amount, std::size_t width)\n";
        harness << "{\n";
        harness << "    std::array<std::uint64_t, N> out{};\n";
        harness << "    for (std::size_t bit = amount; bit < width; ++bit) if (get_bit(value, bit)) put_bit(out, bit - amount, true);\n";
        harness << "    trunc_words(out, width);\n";
        harness << "    return out;\n";
        harness << "}\n\n";
        harness << "template <std::size_t DestN, std::size_t SrcN>\n";
        harness << "static std::array<std::uint64_t, DestN> slice_words(const std::array<std::uint64_t, SrcN>& value, std::size_t start, std::size_t width)\n";
        harness << "{\n";
        harness << "    std::array<std::uint64_t, DestN> out{};\n";
        harness << "    for (std::size_t bit = 0; bit < width; ++bit) if (get_bit(value, start + bit)) put_bit(out, bit, true);\n";
        harness << "    trunc_words(out, width);\n";
        harness << "    return out;\n";
        harness << "}\n\n";
        harness << "template <std::size_t DestN, std::size_t HiN, std::size_t LoN>\n";
        harness << "static std::array<std::uint64_t, DestN> concat_words(const std::array<std::uint64_t, HiN>& hi,\n";
        harness << "                                                      std::size_t hi_width,\n";
        harness << "                                                      const std::array<std::uint64_t, LoN>& lo,\n";
        harness << "                                                      std::size_t lo_width)\n";
        harness << "{\n";
        harness << "    std::array<std::uint64_t, DestN> out{};\n";
        harness << "    for (std::size_t bit = 0; bit < lo_width; ++bit) if (get_bit(lo, bit)) put_bit(out, bit, true);\n";
        harness << "    for (std::size_t bit = 0; bit < hi_width; ++bit) if (get_bit(hi, bit)) put_bit(out, lo_width + bit, true);\n";
        harness << "    trunc_words(out, hi_width + lo_width);\n";
        harness << "    return out;\n";
        harness << "}\n\n";
        harness << "template <std::size_t DestN, std::size_t SrcN>\n";
        harness << "static std::array<std::uint64_t, DestN> replicate_words(const std::array<std::uint64_t, SrcN>& value,\n";
        harness << "                                                         std::size_t value_width,\n";
        harness << "                                                         std::size_t rep)\n";
        harness << "{\n";
        harness << "    std::array<std::uint64_t, DestN> out{};\n";
        harness << "    for (std::size_t r = 0; r < rep; ++r)\n";
        harness << "        for (std::size_t bit = 0; bit < value_width; ++bit)\n";
        harness << "            if (get_bit(value, bit)) put_bit(out, r * value_width + bit, true);\n";
        harness << "    trunc_words(out, value_width * rep);\n";
        harness << "    return out;\n";
        harness << "}\n\n";
        harness << "extern \"C\" void trace_sum(std::uint8_t value)\n";
        harness << "{\n";
        harness << "    g_last_trace = value;\n";
        harness << "}\n\n";
        harness << "int main()\n";
        harness << "{\n";
        harness << "    GrhSIM_top sim;\n";
        harness << "    const std::array<std::uint64_t, 3> wide_value_a{UINT64_C(0x0123456789ABCDEF), UINT64_C(0x0FEDCBA987654321), UINT64_C(0x2)};\n";
        harness << "    const std::array<std::uint64_t, 3> wide_zero{};\n";
        harness << "    const std::array<std::uint64_t, 1> two_bit_one{UINT64_C(1)};\n";
        harness << "    const std::array<std::uint64_t, 3> wide_one = add_one(wide_zero, 130);\n";
        harness << "    const std::array<std::uint64_t, 3> wide_two = add_one(wide_one, 130);\n";
        harness << "    sim.set_en(true);\n";
        harness << "    sim.set_a(static_cast<std::uint8_t>(3));\n";
        harness << "    sim.set_comb(static_cast<std::uint8_t>(0xB6));\n";
        harness << "    sim.set_b(static_cast<std::uint8_t>(3));\n";
        harness << "    sim.set_sh(static_cast<std::uint8_t>(2));\n";
        harness << "    sim.set_rep2(static_cast<std::uint8_t>(2));\n";
        harness << "    sim.set_sa(static_cast<std::uint8_t>(0xF0));\n";
        harness << "    sim.set_wide_in(wide_value_a);\n";
        harness << "    sim.set_wide_addr(static_cast<std::uint8_t>(1));\n";
        harness << "    sim.set_clk(false);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(3)) return 1;\n";
        harness << "    if (sim.get_mul_y() != static_cast<std::uint8_t>(34)) return 11;\n";
        harness << "    if (sim.get_div_y() != static_cast<std::uint8_t>(60)) return 12;\n";
        harness << "    if (sim.get_mod_y() != static_cast<std::uint8_t>(2)) return 13;\n";
        harness << "    if (sim.get_shl_y() != static_cast<std::uint8_t>(0xD8)) return 14;\n";
        harness << "    if (sim.get_lshr_y() != static_cast<std::uint8_t>(0x2D)) return 15;\n";
        harness << "    if (sim.get_ashr_y() != static_cast<std::uint8_t>(0xFC)) return 16;\n";
        harness << "    if (!sim.get_red_or_y()) return 17;\n";
        harness << "    if (!sim.get_red_xor_y()) return 18;\n";
        harness << "    if (sim.get_slice_y() != static_cast<std::uint8_t>(5)) return 19;\n";
        harness << "    if (sim.get_rep_y() != static_cast<std::uint8_t>(0xAA)) return 20;\n";
        harness << "    if (!same_words(sim.get_wide_y(), wide_zero)) return 21;\n";
        harness << "    if (!same_words(sim.get_wide_mem_y(), wide_zero)) return 22;\n";
        harness << "    if (!same_words(sim.get_wide_add_y(), add_one(wide_value_a, 130))) return 31;\n";
        harness << "    if (!same_words(sim.get_wide_sub_y(), sub_one(wide_value_a, 130))) return 32;\n";
        harness << "    if (!same_words(sim.get_wide_mul_y(), shl_words(wide_value_a, 1, 132))) return 33;\n";
        harness << "    if (!same_words(sim.get_wide_div_y(), lshr_words(wide_value_a, 1, 130))) return 34;\n";
        harness << "    if (!same_words(sim.get_wide_mod_y(), wide_one)) return 35;\n";
        harness << "    if (!same_words(sim.get_wide_and_y(), wide_value_a)) return 36;\n";
        harness << "    if (!same_words(sim.get_wide_or_y(), wide_value_a)) return 37;\n";
        harness << "    if (!same_words(sim.get_wide_xor_y(), wide_value_a)) return 38;\n";
        harness << "    if (!same_words(sim.get_wide_xnor_y(), wide_value_a)) return 39;\n";
        harness << "    if (!same_words(sim.get_wide_not_y(), not_words(wide_value_a, 130))) return 40;\n";
        harness << "    if (!sim.get_wide_eq_y()) return 41;\n";
        harness << "    if (!sim.get_wide_lt_y()) return 42;\n";
        harness << "    if (!sim.get_wide_logic_and_y()) return 43;\n";
        harness << "    if (!sim.get_wide_reduce_or_y()) return 44;\n";
        harness << "    if (!same_words(sim.get_wide_shl_y(), shl_words(wide_value_a, 1, 130))) return 45;\n";
        harness << "    if (!same_words(sim.get_wide_lshr_y(), lshr_words(wide_value_a, 1, 130))) return 46;\n";
        harness << "    if (!same_words(sim.get_wide_ashr_y(), not_words(wide_zero, 130))) return 47;\n";
        harness << "    if (!same_words(sim.get_wide_mux_y(), wide_value_a)) return 48;\n";
        harness << "    if (!same_words(sim.get_wide_concat_y(), concat_words<3>(two_bit_one, 2, wide_value_a, 130))) return 49;\n";
        harness << "    if (!same_words(sim.get_wide_rep_y(), replicate_words<5>(wide_value_a, 130, 2))) return 50;\n";
        harness << "    if (!same_words(sim.get_wide_slice_static_y(), slice_words<2>(wide_value_a, 5, 65))) return 51;\n";
        harness << "    if (!same_words(sim.get_wide_slice_dyn_y(), slice_words<2>(wide_value_a, 1, 65))) return 52;\n";
        harness << "    sim.set_clk(true);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(3)) return 2;\n";
        harness << "    if (g_last_trace != static_cast<std::uint8_t>(3)) return 3;\n";
        harness << "    if (!same_words(sim.get_wide_y(), wide_zero)) return 23;\n";
        harness << "    if (!same_words(sim.get_wide_mem_y(), wide_zero)) return 24;\n";
        harness << "    sim.set_clk(false);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(6)) return 4;\n";
        harness << "    if (!same_words(sim.get_wide_y(), wide_value_a)) return 25;\n";
        harness << "    if (!same_words(sim.get_wide_mem_y(), wide_value_a)) return 26;\n";
        harness << "    sim.set_clk(true);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(6)) return 5;\n";
        harness << "    if (g_last_trace != static_cast<std::uint8_t>(6)) return 6;\n";
        harness << "    if (!same_words(sim.get_wide_y(), wide_value_a)) return 27;\n";
        harness << "    if (!same_words(sim.get_wide_mem_y(), wide_value_a)) return 28;\n";
        harness << "    return 0;\n";
        harness << "}\n";
    }

        const std::filesystem::path harnessExe = outDir / "grhsim_top_harness";
        const std::string compileHarnessCmd =
            "c++ -std=c++20 -O2 -I" + outDir.string() +
            " " + (outDir / "grhsim_top_state.cpp").string() +
            " " + (outDir / "grhsim_top_eval.cpp").string() +
            " " + (outDir / "grhsim_top_sched_0.cpp").string() +
            " " + harnessPath.string() +
            " -o " + harnessExe.string();
        if (std::system(compileHarnessCmd.c_str()) != 0)
        {
            return fail("Generated grhsim harness failed to compile");
        }

        const std::filesystem::path harnessLog = outDir / "grhsim_top_harness.log";
        const std::string runHarnessCmd = harnessExe.string() + " > " + harnessLog.string() + " 2>&1";
        if (std::system(runHarnessCmd.c_str()) != 0)
        {
            return fail("Generated grhsim harness failed to run");
        }
        const std::string harnessOutput = readFile(harnessLog);
        if (harnessOutput.find("[display]") == std::string::npos)
        {
            return fail("Generated grhsim harness missing system task output");
        }

        return 0;
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("unexpected exception: ") + ex.what());
    }
}
