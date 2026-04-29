#include "core/grh.hpp"
#include "core/transform.hpp"
#include "emit/grhsim_cpp.hpp"
#include "transform/activity_schedule.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace wolvrix::lib::emit;
using namespace wolvrix::lib::grh;
using namespace wolvrix::lib::transform;

namespace
{
    int fail(const std::string &message)
    {
        std::cerr << "[emit_grhsim_cpp_memory_fill] " << message << '\n';
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

    Design buildDesign()
    {
        Design design;
        Graph &graph = design.createGraph("top");
        design.markAsTop(graph.symbol());

        const ValueId clk = makeLogicValue(graph, "clk", 1);
        const ValueId fillEn = makeLogicValue(graph, "fill_en", 1);
        const ValueId data = makeLogicValue(graph, "data", 8);
        graph.bindInputPort("clk", clk);
        graph.bindInputPort("fill_en", fillEn);
        graph.bindInputPort("data", data);

        const OperationId mem = graph.createOperation(OperationKind::kMemory, graph.internSymbol("mem"));
        graph.setAttr(mem, "width", static_cast<int64_t>(8));
        graph.setAttr(mem, "row", static_cast<int64_t>(4));
        graph.setAttr(mem, "isSigned", false);

        const OperationId fill = graph.createOperation(OperationKind::kMemoryFillPort,
                                                       graph.internSymbol("mem_fill"));
        graph.setAttr(fill, "memSymbol", std::string("mem"));
        graph.setAttr(fill, "eventEdge", std::vector<std::string>{"posedge"});
        graph.addOperand(fill, fillEn);
        graph.addOperand(fill, data);
        graph.addOperand(fill, clk);

        const ValueId addr0 = addConstant(graph, "addr0_op", "addr0", 2, "2'd0");
        const ValueId out = makeLogicValue(graph, "out", 8);
        const OperationId read = graph.createOperation(OperationKind::kMemoryReadPort,
                                                       graph.internSymbol("mem_read"));
        graph.setAttr(read, "memSymbol", std::string("mem"));
        graph.addOperand(read, addr0);
        graph.addResult(read, out);
        graph.bindOutputPort("out", out);

        return design;
    }

    bool runActivitySchedule(Design &design, SessionStore &session)
    {
        PassManager manager;
        manager.options().session = &session;
        manager.addPass(std::make_unique<ActivitySchedulePass>(ActivityScheduleOptions{.path = "top"}));
        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
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
        return fail("activity schedule failed");
    }

    const std::filesystem::path outDir = std::filesystem::path(WOLF_SV_EMIT_ARTIFACT_DIR) / "grhsim_cpp_memory_fill";
    std::filesystem::create_directories(outDir);

    EmitDiagnostics diag;
    EmitGrhSimCpp emitter(&diag);
    EmitOptions options;
    options.outputDir = outDir.string();
    options.session = &session;
    options.sessionPathPrefix = std::string("top");
    options.attributes["sched_batch_max_ops"] = "8";
    options.attributes["sched_batch_max_estimated_lines"] = "96";
    options.attributes["emit_parallelism"] = "1";

    const EmitResult result = emitter.emit(design, options);
    if (!result.success || diag.hasError())
    {
        return fail("grhsim cpp emit failed");
    }

    const std::string header = readFile(outDir / "grhsim_top.hpp");
    const std::string sched = readFile(outDir / "grhsim_top_sched_0.cpp");
    if (header.empty() || sched.empty())
    {
        return fail("missing generated grhsim files");
    }
    if (sched.find("for (std::size_t fill_row = 0; fill_row < 4u; ++fill_row)") == std::string::npos ||
        sched.find("any_row_changed = true;") == std::string::npos ||
        sched.find("state_mem_mem_") == std::string::npos)
    {
        return fail("memory fill emit body is missing from generated schedule");
    }

    return 0;
}
