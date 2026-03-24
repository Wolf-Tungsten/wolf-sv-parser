#include "emit/verilator_repcut_package.hpp"
#include "core/grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace wolvrix::lib::emit;
using namespace wolvrix::lib::grh;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_verilator_repcut_package] " << message << '\n';
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

    bool contains(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    Graph &buildUnitGraph(Design &design,
                          std::string_view name,
                          std::vector<std::pair<std::string, int32_t>> inputs,
                          std::vector<std::pair<std::string, int32_t>> outputs)
    {
        Graph &graph = design.createGraph(std::string(name));
        for (const auto &[portName, width] : inputs)
        {
            const auto value = graph.createValue(graph.internSymbol(portName), width, false);
            graph.bindInputPort(portName, value);
        }
        for (const auto &[portName, width] : outputs)
        {
            const auto value = graph.createValue(graph.internSymbol(portName), width, false);
            graph.bindOutputPort(portName, value);
        }
        return graph;
    }

    void addInstance(Graph &graph,
                     std::string_view instanceName,
                     std::string_view moduleName,
                     std::vector<ValueId> operands,
                     std::vector<ValueId> results,
                     std::vector<std::string> inputPortNames,
                     std::vector<std::string> outputPortNames)
    {
        const auto op = graph.createOperation(OperationKind::kInstance, graph.internSymbol(std::string(instanceName)));
        graph.setAttr(op, "instanceName", std::string(instanceName));
        graph.setAttr(op, "moduleName", std::string(moduleName));
        graph.setAttr(op, "inputPortName", std::move(inputPortNames));
        graph.setAttr(op, "outputPortName", std::move(outputPortNames));
        graph.setAttr(op, "inoutPortName", std::vector<std::string>{});
        for (const auto operand : operands)
        {
            graph.addOperand(op, operand);
        }
        for (const auto result : results)
        {
            graph.addResult(op, result);
        }
    }

    Design buildDesign()
    {
        Design design;
        buildUnitGraph(design,
                       "SimTop_debug_part",
                       {{"clock", 1}, {"reset", 1}, {"in__data", 8}},
                       {{"dbg__out", 8}});
        buildUnitGraph(design,
                       "SimTop_logic_part_repcut_part0",
                       {{"clock", 1}, {"reset", 1}, {"in_data", 8}},
                       {{"mid__val", 8}});
        buildUnitGraph(design,
                       "SimTop_logic_part_repcut_part1",
                       {{"clock", 1}, {"reset", 1}, {"mid__val", 8}, {"sel", 1}},
                       {{"out__data", 8}});

        Graph &top = design.createGraph("SimTop");
        const auto clock = top.createValue(top.internSymbol("clock"), 1, false);
        const auto linkClock = top.createValue(top.internSymbol("link_clock"), 1, false);
        const auto reset = top.createValue(top.internSymbol("reset"), 1, false);
        const auto inData = top.createValue(top.internSymbol("in_data"), 8, false);
        const auto dbgOut = top.createValue(top.internSymbol("dbg__out"), 8, false);
        const auto mid = top.createValue(top.internSymbol("mid"), 8, false);
        const auto outData = top.createValue(top.internSymbol("out_data"), 8, false);
        const auto outAlias = top.createValue(top.internSymbol("out_alias"), 8, false);
        const auto selConst = top.createValue(top.internSymbol("sel_const"), 1, false);

        top.bindInputPort("clock", clock);
        top.bindInputPort("reset", reset);
        top.bindInputPort("in_data", inData);
        top.bindOutputPort("out", outAlias);

        const auto clockAssign = top.createOperation(OperationKind::kAssign, top.internSymbol("assign_clock_alias"));
        top.addOperand(clockAssign, clock);
        top.addResult(clockAssign, linkClock);

        const auto constOp = top.createOperation(OperationKind::kConstant, top.internSymbol("const_sel"));
        top.setAttr(constOp, "constValue", std::string("1'b1"));
        top.addResult(constOp, selConst);

        const auto outAssign = top.createOperation(OperationKind::kAssign, top.internSymbol("assign_out_alias"));
        top.addOperand(outAssign, outData);
        top.addResult(outAssign, outAlias);

        addInstance(top,
                    "debug_part",
                    "SimTop_debug_part",
                    {linkClock, reset, inData},
                    {dbgOut},
                    {"clock", "reset", "in__data"},
                    {"dbg__out"});
        addInstance(top,
                    "part_0",
                    "SimTop_logic_part_repcut_part0",
                    {linkClock, reset, inData},
                    {mid},
                    {"clock", "reset", "in_data"},
                    {"mid__val"});
        addInstance(top,
                    "part_1",
                    "SimTop_logic_part_repcut_part1",
                    {linkClock, reset, mid, selConst},
                    {outData},
                    {"clock", "reset", "mid__val", "sel"},
                    {"out__data"});

        design.markAsTop("SimTop");
        return design;
    }

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

int main()
{
    const Design design = buildDesign();
    const std::filesystem::path artifactRoot = std::filesystem::path(WOLF_SV_EMIT_ARTIFACT_DIR) / "verilator_repcut_package";

    std::error_code ec;
    std::filesystem::remove_all(artifactRoot, ec);

    EmitDiagnostics diagnostics;
    EmitVerilatorRepCutPackage emitter(&diagnostics);
    EmitOptions options;
    options.outputDir = artifactRoot.string();
    options.topOverrides = {"SimTop"};

    const EmitResult result = emitter.emit(design, options);
    if (!result.success)
    {
        return fail("package emit reported failure");
    }
    if (diagnostics.hasError())
    {
        return fail("package emit reported diagnostics errors");
    }

    const std::filesystem::path debugSvPath = artifactRoot / "sv" / "SimTop_debug_part.sv";
    const std::filesystem::path part0SvPath = artifactRoot / "sv" / "SimTop_logic_part_repcut_part0.sv";
    const std::filesystem::path part1SvPath = artifactRoot / "sv" / "SimTop_logic_part_repcut_part1.sv";
    const std::filesystem::path topSvPath = artifactRoot / "sv" / "SimTop.sv";
    const std::filesystem::path debugFileList = artifactRoot / "verilate" / "debug_part.f";
    const std::filesystem::path part0FileList = artifactRoot / "verilate" / "part_0.f";
    const std::filesystem::path part1FileList = artifactRoot / "verilate" / "part_1.f";
    const std::filesystem::path wrapperHeaderPath = artifactRoot / "wolvi_repcut_verilator_sim.h";
    const std::filesystem::path wrapperSourcePath = artifactRoot / "wolvi_repcut_verilator_sim.cpp";
    const std::filesystem::path smokeMainPath = artifactRoot / "partitioned_smoke_main.cpp";
    const std::filesystem::path unitsMkPath = artifactRoot / "units.mk";
    const std::filesystem::path makefilePath = artifactRoot / "Makefile";

    for (const auto &path : {debugSvPath, part0SvPath, part1SvPath, topSvPath,
                             debugFileList, part0FileList, part1FileList, wrapperHeaderPath, wrapperSourcePath,
                             smokeMainPath, unitsMkPath, makefilePath})
    {
        if (!std::filesystem::exists(path))
        {
            return fail("expected package artifact is missing: " + path.string());
        }
    }

    if (readFile(debugFileList) !=
        (artifactRoot / "sv" / "WolviRepCutUnit_debug_part.sv").generic_string() + "\n" +
            (artifactRoot / "sv" / "SimTop_debug_part.sv").generic_string() + "\n")
    {
        return fail("unexpected debug_part file list content");
    }
    if (readFile(part0FileList) !=
        (artifactRoot / "sv" / "WolviRepCutUnit_part_0.sv").generic_string() + "\n" +
            (artifactRoot / "sv" / "SimTop_logic_part_repcut_part0.sv").generic_string() + "\n")
    {
        return fail("unexpected part_0 file list content");
    }
    if (readFile(part1FileList) !=
        (artifactRoot / "sv" / "WolviRepCutUnit_part_1.sv").generic_string() + "\n" +
            (artifactRoot / "sv" / "SimTop_logic_part_repcut_part1.sv").generic_string() + "\n")
    {
        return fail("unexpected part_1 file list content");
    }

    const std::string wrapperHeader = readFile(wrapperHeaderPath);
    const std::string wrapperSource = readFile(wrapperSourcePath);
    const std::string smokeMain = readFile(smokeMainPath);
    const std::string unitsMk = readFile(unitsMkPath);
    const std::string makefile = readFile(makefilePath);
    if (wrapperHeader.empty() || wrapperSource.empty() || smokeMain.empty() || unitsMk.empty() || makefile.empty())
    {
        return fail("failed to read generated package build files");
    }
    if (!contains(wrapperHeader, "#include \"VWolviRepCutUnit_debug_part.h\"") ||
        !contains(wrapperHeader, "#include \"VWolviRepCutUnit_part_0.h\"") ||
        !contains(wrapperHeader, "#include \"VWolviRepCutUnit_part_1.h\""))
    {
        return fail("wrapper header missing expected model includes");
    }
    if (!contains(wrapperHeader, "std::unique_ptr<VWolviRepCutUnit_debug_part> unit_debug_part_;") ||
        !contains(wrapperHeader, "std::unique_ptr<VWolviRepCutUnit_part_0> unit_part_0_;") ||
        !contains(wrapperHeader, "std::unique_ptr<VWolviRepCutUnit_part_1> unit_part_1_;"))
    {
        return fail("wrapper header missing expected unit members");
    }
    if (!contains(wrapperHeader, "std::vector<std::function<void(std::size_t)>> non_debug_step_fns_;") ||
        !contains(wrapperHeader, "void run_part_eval_phase_(const std::vector<std::function<void(std::size_t)>>& phaseFns);") ||
        !contains(wrapperHeader, "void run_non_debug_step_workers_();"))
    {
        return fail("wrapper header missing expected parallel eval declarations");
    }
    if (!contains(wrapperHeader, "struct alignas(64) PartTimingStats {") ||
        !contains(wrapperHeader, "std::uint64_t scatter_ns{};") ||
        !contains(wrapperHeader, "std::uint64_t eval_ns{};") ||
        !contains(wrapperHeader, "std::uint64_t gather_ns{};") ||
        !contains(wrapperHeader, "struct StepTimingStats {") ||
        !contains(wrapperHeader, "PartTimingStats collect_part_timing_stats_(std::size_t partIndex) const;") ||
        !contains(wrapperHeader, "void report_step_timing_() const;") ||
        !contains(wrapperHeader, "void report_part_timing_() const;") ||
        !contains(wrapperHeader, "void dump_timing_jsonl_() const;") ||
        !contains(wrapperHeader, "std::uint64_t step_count_{};") ||
        !contains(wrapperHeader, "StepTimingStats step_timing_{};") ||
        !contains(wrapperHeader, "part_timing_stats_{};") ||
        !contains(wrapperHeader, "part_timing_worker_stats_{};"))
    {
        return fail("wrapper header missing expected timing declarations");
    }
    if (!contains(wrapperHeader, "CData signal_mid_snapshot_{};") ||
        !contains(wrapperHeader, "CData signal_mid_writeback_{};"))
    {
        return fail("wrapper header missing expected snapshot/writeback cache members");
    }
    if (!contains(wrapperHeader, "void set_clock(CData value) { top_in_clock_ = value; }") ||
        !contains(wrapperHeader, "void set_in_data(CData value) { top_in_in_data_ = value; }") ||
        !contains(wrapperHeader, "CData get_out() const { return top_out_out_; }"))
    {
        return fail("wrapper header missing expected top port accessors");
    }
    if (!contains(wrapperSource, "unit_debug_part_->in_2 = top_in_in_data_;") ||
        !contains(wrapperSource, "unit_part_0_->in_2 = top_in_in_data_;") ||
        !contains(wrapperSource, "unit_part_1_->in_2 = signal_mid_snapshot_;") ||
        !contains(wrapperSource, "unit_part_1_->in_3 = const_sel_const_;"))
    {
        return fail("wrapper source missing expected scatter code");
    }
    if (!contains(wrapperSource, "non_debug_step_fns_.emplace_back([this](std::size_t workerIndex) {"))
    {
        return fail("wrapper source missing expected static task generation");
    }
    if (!contains(wrapperSource, "unit_debug_part_->eval();") ||
        !contains(wrapperSource, "// eval part_0") ||
        !contains(wrapperSource, "// eval part_1") ||
        !contains(wrapperSource, "run_non_debug_step_workers_();"))
    {
        return fail("wrapper source missing expected eval calls");
    }
    if (!contains(wrapperSource, "Publish debug_part outputs into both snapshot and writeback before worker launch.") ||
        !contains(wrapperSource, "signal_mid_writeback_ = unit_part_0_->out_0;") ||
        !contains(wrapperSource, "signal_mid_snapshot_ = signal_mid_writeback_;"))
    {
        return fail("wrapper source missing expected snapshot/writeback publish and commit code");
    }
    if (!contains(wrapperSource, "std::getenv(\"XS_EMU_THREADS\")") ||
        !contains(wrapperSource, "assert(requestedWorkers <= non_debug_step_fns_.size() && \"XS_EMU_THREADS must not exceed repcut partition count\")") ||
        !contains(wrapperSource, "#if defined(__linux__)"))
    {
        return fail("wrapper source missing expected runtime thread-pool guards");
    }
    if (!contains(wrapperSource, "using WolviClock = std::chrono::steady_clock;") ||
        !contains(wrapperSource, "void WolviRepCutVerilatorSim::report_step_timing_() const {") ||
        !contains(wrapperSource, "[WOLVI][step-timing] steps=%llu total=%.3f ms avg=%.3f us\\n") ||
        !contains(wrapperSource, "printPhase(\"debug_scatter\", step_timing_.debug_scatter_ns);") ||
        !contains(wrapperSource, "printPhase(\"debug_eval\", step_timing_.debug_eval_ns);") ||
        !contains(wrapperSource, "printPhase(\"debug_publish\", step_timing_.debug_publish_ns);") ||
        !contains(wrapperSource, "printPhase(\"part_eval\", step_timing_.part_eval_ns);") ||
        !contains(wrapperSource, "printPhase(\"writeback\", step_timing_.writeback_ns);") ||
        !contains(wrapperSource, "WolviRepCutVerilatorSim::PartTimingStats WolviRepCutVerilatorSim::collect_part_timing_stats_(std::size_t partIndex) const {") ||
        !contains(wrapperSource, "void WolviRepCutVerilatorSim::report_part_timing_() const {") ||
        !contains(wrapperSource, "void WolviRepCutVerilatorSim::dump_timing_jsonl_() const {") ||
        !contains(wrapperSource, "std::getenv(\"WOLVI_REPCUT_TIMING_JSONL\")") ||
        !contains(wrapperSource, "\"{\\\"record_type\\\":\\\"part_timing\\\"") ||
        !contains(wrapperSource, "\\\"scatter_total_ms\\\":%.3f") ||
        !contains(wrapperSource, "\\\"eval_total_ms\\\":%.3f") ||
        !contains(wrapperSource, "\\\"gather_total_ms\\\":%.3f") ||
        !contains(wrapperSource, "[WOLVI][part-timing] part=%s steps=%llu total=%.3f ms avg=%.3f us scatter=%.3f us eval=%.3f us gather=%.3f us\\n") ||
        !contains(wrapperSource, "++step_timing_.steps;") ||
        !contains(wrapperSource, "step_timing_.debug_scatter_ns +=") ||
        !contains(wrapperSource, "step_timing_.debug_eval_ns +=") ||
        !contains(wrapperSource, "step_timing_.debug_publish_ns +=") ||
        !contains(wrapperSource, "step_timing_.part_eval_ns +=") ||
        !contains(wrapperSource, "step_timing_.writeback_ns +=") ||
        !contains(wrapperSource, "step_timing_.total_ns +=") ||
        !contains(wrapperSource, "partTimingStats->scatter_ns +=") ||
        !contains(wrapperSource, "partTimingStats->eval_ns +=") ||
        !contains(wrapperSource, "partTimingStats->gather_ns +=") ||
        !contains(wrapperSource, "partTimingStats = &part_timing_worker_stats_[workerIndex][") ||
        !contains(wrapperSource, "part_timing_stats_[0].total_ns +=") ||
        !contains(wrapperSource, "part_timing_stats_[0].scatter_ns +=") ||
        !contains(wrapperSource, "part_timing_stats_[0].eval_ns +=") ||
        !contains(wrapperSource, "part_timing_stats_[0].gather_ns +="))
    {
        return fail("wrapper source missing expected timing instrumentation");
    }
    if (contains(wrapperSource, "XS_REPCUT_STEP_TIMING") ||
        contains(wrapperSource, "step_timing_enabled_") ||
        contains(wrapperSource, "wolvi_env_flag_enabled("))
    {
        return fail("wrapper source should not contain legacy step timing instrumentation");
    }
    if (!contains(wrapperSource, "Evaluate debug_part first so DPI/device responses are available before logic eval.") ||
        !contains(wrapperSource, "Evaluate all non-debug units with fused local scatter/eval/gather."))
    {
        return fail("wrapper source missing expected debug_part scheduling comments");
    }
    const std::size_t dtorPos = wrapperSource.find("WolviRepCutVerilatorSim::~WolviRepCutVerilatorSim()");
    const std::size_t reportStepPos = wrapperSource.find("report_step_timing_();", dtorPos);
    const std::size_t reportPartPos = wrapperSource.find("report_part_timing_();", dtorPos);
    const std::size_t dumpJsonPos = wrapperSource.find("dump_timing_jsonl_();", dtorPos);
    const std::size_t shutdownPos = wrapperSource.find("shutdown_part_eval_workers_();", dtorPos);
    if (dtorPos == std::string::npos || reportStepPos == std::string::npos || reportPartPos == std::string::npos ||
        dumpJsonPos == std::string::npos || shutdownPos == std::string::npos)
    {
        return fail("wrapper source missing destructor timing/shutdown sequence");
    }
    if (!(reportStepPos < shutdownPos && reportPartPos < shutdownPos && dumpJsonPos < shutdownPos))
    {
        return fail("wrapper destructor should report and dump timing before clearing worker timing state");
    }
    if (!contains(wrapperSource, "signal_mid_writeback_ = unit_part_0_->out_0;") ||
        !contains(wrapperSource, "top_out_out_ = unit_part_1_->out_0;"))
    {
        return fail("wrapper source missing expected gather code");
    }
    const std::size_t debugInputPos = wrapperSource.find("unit_debug_part_->in_2 = top_in_in_data_;");
    const std::size_t debugEvalPos = wrapperSource.find("unit_debug_part_->eval();");
    const std::size_t debugPublishPos =
        wrapperSource.find("Publish debug_part outputs into both snapshot and writeback before worker launch.");
    const std::size_t partEvalPos = wrapperSource.find("run_non_debug_step_workers_();");
    const std::size_t gatherPhasePos = wrapperSource.find("signal_mid_snapshot_ = signal_mid_writeback_;");
    if (debugInputPos == std::string::npos || debugEvalPos == std::string::npos ||
        debugPublishPos == std::string::npos || partEvalPos == std::string::npos ||
        gatherPhasePos == std::string::npos)
    {
        return fail("wrapper source missing phase-order markers");
    }
    if (!(debugInputPos < debugEvalPos && debugEvalPos < debugPublishPos && debugPublishPos < partEvalPos &&
          partEvalPos < gatherPhasePos))
    {
        return fail("wrapper source should load debug inputs, eval debug_part, publish debug outputs, run fused non-debug worker phase, then commit writeback");
    }
    if (!contains(wrapperSource, "const CData WolviRepCutVerilatorSim::const_sel_const_ = static_cast<CData>(0x1ULL);"))
    {
        return fail("wrapper source missing expected constant definition");
    }
    if (!contains(smokeMain, "WolviRepCutVerilatorSim sim;") ||
        !contains(smokeMain, "sim.step();"))
    {
        return fail("smoke main missing expected simulation bootstrap");
    }
    if (!contains(unitsMk, "PARTITIONED_UNITS := debug_part part_0 part_1") ||
        !contains(unitsMk, "PARTITIONED_UNIT_MAKE_J ?= $(if $(strip $(VM_BUILD_JOBS)),-j $(VM_BUILD_JOBS),)") ||
        !contains(unitsMk, "PARTITIONED_VM_PARALLEL_BUILDS ?= $(VM_PARALLEL_BUILDS)") ||
        !contains(unitsMk, "UNIT_debug_part_MODULE := WolviRepCutUnit_debug_part") ||
        !contains(unitsMk, "UNIT_part_0_MODULE := WolviRepCutUnit_part_0") ||
        !contains(unitsMk, "UNIT_part_1_MODULE := WolviRepCutUnit_part_1") ||
        !contains(unitsMk, "$(VERILATOR) --cc -f $(UNIT_debug_part_FILELIST)") ||
        !contains(unitsMk, "$(MAKE) $(PARTITIONED_UNIT_MAKE_J) -C $(UNIT_part_1_MDIR) VM_PARALLEL_BUILDS=$(PARTITIONED_VM_PARALLEL_BUILDS) OBJCACHE= -f VWolviRepCutUnit_part_1.mk VWolviRepCutUnit_part_1__ALL.a"))
    {
        return fail("units.mk missing expected unit build rules");
    }
    if (!contains(makefile, "include units.mk") ||
        !contains(makefile, ".DEFAULT_GOAL := all") ||
        !contains(makefile, "PARTITIONED_VERILATOR_FLAGS ?= --no-timing -Wno-STMTDLY -Wno-WIDTH -Wno-WIDTHTRUNC --output-split 30000 --output-split-cfuncs 30000") ||
        !contains(makefile, "VM_BUILD_JOBS ?=") ||
        !contains(makefile, "VM_PARALLEL_BUILDS ?= 1") ||
        !contains(makefile, "verilate-units: $(UNIT_ARCHIVES)") ||
        !contains(makefile, "VERILATED_DPI_OBJ := $(BUILD_DIR)/verilated_dpi.o") ||
        !contains(makefile, "VERILATED_THREADS_OBJ := $(BUILD_DIR)/verilated_threads.o") ||
        !contains(makefile, "$(TARGET): $(VERILATED_OBJ) $(VERILATED_DPI_OBJ) $(VERILATED_THREADS_OBJ) $(WRAPPER_OBJ) $(SMOKE_MAIN_OBJ) $(UNIT_ARCHIVES)") ||
        !contains(makefile, "$(CXX) $(LDFLAGS) -o $@ $(VERILATED_OBJ) $(VERILATED_DPI_OBJ) $(VERILATED_THREADS_OBJ) $(WRAPPER_OBJ) $(SMOKE_MAIN_OBJ) $(UNIT_ARCHIVES) $(LDLIBS) -ldl -pthread") ||
        !contains(makefile, "run: $(TARGET)"))
    {
        return fail("Makefile missing expected top-level build rules");
    }

    return 0;
}
