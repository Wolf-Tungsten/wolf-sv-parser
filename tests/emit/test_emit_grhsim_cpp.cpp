#include "core/grh.hpp"
#include "core/transform.hpp"
#include "emit/grhsim_cpp.hpp"
#include "transform/activity_schedule.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
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

    Design buildDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");
        design.markAsTop(graph.symbol());

        ValueId clk = makeLogicValue(graph, "clk", 1);
        ValueId en = makeLogicValue(graph, "en", 1);
        ValueId a = makeLogicValue(graph, "a", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("en", en);
        graph.bindInputPort("a", a);

        OperationId reg = graph.createOperation(OperationKind::kRegister, graph.internSymbol("reg_q"));
        graph.setAttr(reg, "width", static_cast<int64_t>(8));
        graph.setAttr(reg, "isSigned", false);

        ValueId regQ = makeLogicValue(graph, "reg_q_read", 8);
        OperationId regRead = graph.createOperation(OperationKind::kRegisterReadPort,
                                                    graph.internSymbol("reg_q_read_op"));
        graph.addResult(regRead, regQ);
        graph.setAttr(regRead, "regSymbol", std::string("reg_q"));

        ValueId mask = addConstant(graph, "const_mask", "mask", 8, "8'hFF");
        ValueId fmt = addConstant(graph, "const_fmt", "fmt", 0, "\"q=%0d\"", ValueType::String);

        ValueId sum = makeLogicValue(graph, "sum", 8);
        OperationId add = graph.createOperation(OperationKind::kAdd, graph.internSymbol("sum_add"));
        graph.addOperand(add, regQ);
        graph.addOperand(add, a);
        graph.addResult(add, sum);
        graph.bindOutputPort("y", sum);

        OperationId write = graph.createOperation(OperationKind::kRegisterWritePort,
                                                  graph.internSymbol("reg_q_write"));
        graph.addOperand(write, en);
        graph.addOperand(write, sum);
        graph.addOperand(write, mask);
        graph.addOperand(write, clk);
        graph.setAttr(write, "regSymbol", std::string("reg_q"));
        graph.setAttr(write, "eventEdge", std::vector<std::string>{"posedge"});

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
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{.path = "top"}));
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
    const std::filesystem::path evalPath = outDir / "grhsim_top_eval.cpp";
    const std::filesystem::path schedPath = outDir / "grhsim_top_sched_0.cpp";
    const std::filesystem::path makefilePath = outDir / "Makefile";
    if (!std::filesystem::exists(headerPath) || !std::filesystem::exists(evalPath) ||
        !std::filesystem::exists(schedPath) || !std::filesystem::exists(makefilePath))
    {
        return fail("Expected generated grhsim artifacts to exist");
    }

    const std::string header = readFile(headerPath);
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
        harness << "#include <cstdint>\n";
        harness << "#include <iostream>\n\n";
        harness << "static std::uint8_t g_last_trace = 0;\n";
        harness << "extern \"C\" void trace_sum(std::uint8_t value)\n";
        harness << "{\n";
        harness << "    g_last_trace = value;\n";
        harness << "}\n\n";
        harness << "int main()\n";
        harness << "{\n";
        harness << "    GrhSIM_top sim;\n";
        harness << "    sim.set_en(true);\n";
        harness << "    sim.set_a(static_cast<std::uint8_t>(3));\n";
        harness << "    sim.set_clk(false);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(3)) return 1;\n";
        harness << "    sim.set_clk(true);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(3)) return 2;\n";
        harness << "    if (g_last_trace != static_cast<std::uint8_t>(3)) return 3;\n";
        harness << "    sim.set_clk(false);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(6)) return 4;\n";
        harness << "    sim.set_clk(true);\n";
        harness << "    sim.eval();\n";
        harness << "    if (sim.get_y() != static_cast<std::uint8_t>(6)) return 5;\n";
        harness << "    if (g_last_trace != static_cast<std::uint8_t>(6)) return 6;\n";
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
