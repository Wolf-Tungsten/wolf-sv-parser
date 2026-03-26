#include "emit/verilator_repcut_package.hpp"
#include "core/grh.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
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

    std::string diagnosticsSummary(const EmitDiagnostics &diagnostics)
    {
        std::string summary;
        for (const auto &diag : diagnostics.messages())
        {
            if (!summary.empty())
            {
                summary += " | ";
            }
            summary += diag.message;
            if (!diag.context.empty())
            {
                summary += " [";
                summary += diag.context;
                summary += "]";
            }
        }
        return summary;
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
        Graph &effect = design.createGraph("SimTop_effect_part");
        const auto effectClock = effect.createValue(effect.internSymbol("clock"), 1, false);
        const auto effectReset = effect.createValue(effect.internSymbol("reset"), 1, false);
        const auto effectInput = effect.createValue(effect.internSymbol("in__data"), 8, false);
        const auto effectOutput = effect.createValue(effect.internSymbol("effect__out"), 8, false);
        const auto effectCond = effect.createValue(effect.internSymbol("effect_cond"), 1, false);
        effect.bindInputPort("clock", effectClock);
        effect.bindInputPort("reset", effectReset);
        effect.bindInputPort("in__data", effectInput);
        effect.bindOutputPort("effect__out", effectOutput);
        const auto condConst =
            effect.createOperation(OperationKind::kConstant, effect.internSymbol("const_true"));
        effect.setAttr(condConst, "constValue", std::string("1'b1"));
        effect.addResult(condConst, effectCond);
        const auto dpiImport =
            effect.createOperation(OperationKind::kDpicImport, effect.internSymbol("dpi_func"));
        effect.setAttr(dpiImport, "argsDirection", std::vector<std::string>{"input"});
        effect.setAttr(dpiImport, "argsWidth", std::vector<int64_t>{8});
        effect.setAttr(dpiImport, "argsName", std::vector<std::string>{"in_val"});
        effect.setAttr(dpiImport, "argsSigned", std::vector<bool>{false});
        effect.setAttr(dpiImport, "argsType", std::vector<std::string>{"logic"});
        effect.setAttr(dpiImport, "hasReturn", true);
        effect.setAttr(dpiImport, "returnWidth", static_cast<int64_t>(8));
        effect.setAttr(dpiImport, "returnSigned", false);
        effect.setAttr(dpiImport, "returnType", std::string("logic"));
        (void)dpiImport;
        const auto dpiCall =
            effect.createOperation(OperationKind::kDpicCall, effect.internSymbol("dpi_call"));
        effect.addOperand(dpiCall, effectCond);
        effect.addOperand(dpiCall, effectInput);
        effect.addOperand(dpiCall, effectClock);
        effect.addResult(dpiCall, effectOutput);
        effect.setAttr(dpiCall, "targetImportSymbol", std::string("dpi_func"));
        effect.setAttr(dpiCall, "eventEdge", std::vector<std::string>{"posedge"});
        effect.setAttr(dpiCall, "inArgName", std::vector<std::string>{"in_val"});
        effect.setAttr(dpiCall, "outArgName", std::vector<std::string>{});
        effect.setAttr(dpiCall, "hasReturn", true);

        const auto dpiImportVoid =
            effect.createOperation(OperationKind::kDpicImport, effect.internSymbol("dpi_void_func"));
        effect.setAttr(dpiImportVoid, "argsDirection", std::vector<std::string>{"input"});
        effect.setAttr(dpiImportVoid, "argsWidth", std::vector<int64_t>{8});
        effect.setAttr(dpiImportVoid, "argsName", std::vector<std::string>{"in_val"});
        effect.setAttr(dpiImportVoid, "argsSigned", std::vector<bool>{false});
        effect.setAttr(dpiImportVoid, "argsType", std::vector<std::string>{"logic"});
        effect.setAttr(dpiImportVoid, "hasReturn", false);
        const auto dpiCallVoid =
            effect.createOperation(OperationKind::kDpicCall, effect.internSymbol("dpi_call_void"));
        effect.addOperand(dpiCallVoid, effectCond);
        effect.addOperand(dpiCallVoid, effectInput);
        effect.addOperand(dpiCallVoid, effectClock);
        effect.setAttr(dpiCallVoid, "targetImportSymbol", std::string("dpi_void_func"));
        effect.setAttr(dpiCallVoid, "eventEdge", std::vector<std::string>{"posedge"});
        effect.setAttr(dpiCallVoid, "inArgName", std::vector<std::string>{"in_val"});
        effect.setAttr(dpiCallVoid, "outArgName", std::vector<std::string>{});
        effect.setAttr(dpiCallVoid, "hasReturn", false);
        buildUnitGraph(design,
                       "SimTop_logic_part_repcut_part0",
                       {{"clock", 1}, {"reset", 1}, {"effect__out", 8}},
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
        const auto effectOut = top.createValue(top.internSymbol("effect__out"), 8, false);
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
                    "effect_part",
                    "SimTop_effect_part",
                    {linkClock, reset, inData},
                    {effectOut},
                    {"clock", "reset", "in__data"},
                    {"effect__out"});
        addInstance(top,
                    "part_0",
                    "SimTop_logic_part_repcut_part0",
                    {linkClock, reset, effectOut},
                    {mid},
                    {"clock", "reset", "effect__out"},
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
        return fail("package emit reported failure: " + diagnosticsSummary(diagnostics));
    }
    if (diagnostics.hasError())
    {
        return fail("package emit reported diagnostics errors: " + diagnosticsSummary(diagnostics));
    }

    const std::filesystem::path effectSvPath = artifactRoot / "sv" / "SimTop_effect_part.sv";
    const std::filesystem::path part0SvPath = artifactRoot / "sv" / "SimTop_logic_part_repcut_part0.sv";
    const std::filesystem::path part1SvPath = artifactRoot / "sv" / "SimTop_logic_part_repcut_part1.sv";
    const std::filesystem::path topSvPath = artifactRoot / "sv" / "SimTop.sv";
    const std::filesystem::path effectFileList = artifactRoot / "verilate" / "effect_part.f";
    const std::filesystem::path part0FileList = artifactRoot / "verilate" / "part_0.f";
    const std::filesystem::path part1FileList = artifactRoot / "verilate" / "part_1.f";
    const std::filesystem::path wrapperHeaderPath = artifactRoot / "wolvi_repcut_verilator_sim.h";
    const std::filesystem::path smokeMainPath = artifactRoot / "partitioned_smoke_main.cpp";
    const std::filesystem::path unitsMkPath = artifactRoot / "units.mk";
    const std::filesystem::path makefilePath = artifactRoot / "Makefile";
    std::vector<std::filesystem::path> wrapperSourcePaths;
    for (const auto &entry : std::filesystem::directory_iterator(artifactRoot))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto filename = entry.path().filename().string();
        if (entry.path().extension() == ".cpp" && filename.rfind("wolvi_repcut_verilator_sim", 0) == 0)
        {
            wrapperSourcePaths.push_back(entry.path());
        }
    }
    std::sort(wrapperSourcePaths.begin(), wrapperSourcePaths.end());
    std::unordered_map<std::string, std::string> wrapperSourceByName;

    for (const auto &path : {effectSvPath, part0SvPath, part1SvPath, topSvPath,
                             effectFileList, part0FileList, part1FileList, wrapperHeaderPath,
                             smokeMainPath, unitsMkPath, makefilePath})
    {
        if (!std::filesystem::exists(path))
        {
            return fail("expected package artifact is missing: " + path.string());
        }
    }
    if (wrapperSourcePaths.empty())
    {
        return fail("expected split wrapper sources are missing");
    }

    if (readFile(effectFileList) !=
        (artifactRoot / "sv" / "WolviRepCutUnit_effect_part.sv").generic_string() + "\n" +
            (artifactRoot / "sv" / "SimTop_effect_part.sv").generic_string() + "\n")
    {
        return fail("unexpected effect_part file list content");
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
    const std::string smokeMain = readFile(smokeMainPath);
    const std::string unitsMk = readFile(unitsMkPath);
    const std::string makefile = readFile(makefilePath);
    std::string wrapperSource;
    for (const auto &path : wrapperSourcePaths)
    {
        const std::string text = readFile(path);
        wrapperSourceByName.emplace(path.filename().string(), text);
        wrapperSource += text;
        wrapperSource += "\n";
    }
    if (wrapperHeader.empty() || wrapperSource.empty() || smokeMain.empty() || unitsMk.empty() || makefile.empty())
    {
        return fail("failed to read generated package build files");
    }
    if (!contains(wrapperHeader, "class VWolviRepCutUnit_effect_part;") ||
        !contains(wrapperHeader, "class VWolviRepCutUnit_part_0;") ||
        !contains(wrapperHeader, "class VWolviRepCutUnit_part_1;"))
    {
        return fail("wrapper header missing expected model forward declarations");
    }
    if (!contains(wrapperHeader, "std::unique_ptr<VWolviRepCutUnit_effect_part> unit_effect_part_;") ||
        !contains(wrapperHeader, "std::unique_ptr<VWolviRepCutUnit_part_0> unit_part_0_;") ||
        !contains(wrapperHeader, "std::unique_ptr<VWolviRepCutUnit_part_1> unit_part_1_;"))
    {
        return fail("wrapper header missing expected unit members");
    }
    if (!contains(wrapperHeader, "using StepFn = void (WolviRepCutVerilatorSim::*)(std::size_t);") ||
        !contains(wrapperHeader, "std::vector<StepFn> normal_step_fns_;") ||
        !contains(wrapperHeader, "void run_part_eval_phase_(const std::vector<StepFn>& phaseFns);") ||
        !contains(wrapperHeader, "void run_normal_step_workers_();"))
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
    if (!contains(wrapperHeader, "CData signal_effect__out_snapshot_{};") ||
        !contains(wrapperHeader, "CData signal_effect__out_writeback_{};") ||
        !contains(wrapperHeader, "CData signal_mid_snapshot_{};") ||
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
    if (!contains(wrapperSource, "#include \"VWolviRepCutUnit_effect_part.h\"") ||
        !contains(wrapperSource, "#include \"VWolviRepCutUnit_part_0.h\"") ||
        !contains(wrapperSource, "#include \"VWolviRepCutUnit_part_1.h\""))
    {
        return fail("wrapper sources missing expected model includes");
    }
    if (!contains(wrapperSource, "unit_effect_part_->in_2 = top_in_in_data_;") ||
        !contains(wrapperSource, "unit_part_0_->in_2 = signal_effect__out_snapshot_;") ||
        !contains(wrapperSource, "unit_part_1_->in_2 = signal_mid_snapshot_;") ||
        !contains(wrapperSource, "unit_part_1_->in_3 = const_sel_const_;"))
    {
        return fail("wrapper source missing expected scatter code");
    }
    if (!contains(wrapperSource, "normal_step_fns_.push_back(&WolviRepCutVerilatorSim::"))
    {
        return fail("wrapper source missing expected static task generation");
    }
    if (!contains(wrapperSource, "unit_effect_part_->eval();") ||
        !contains(wrapperSource, "// eval part_0") ||
        !contains(wrapperSource, "// eval part_1") ||
        !contains(wrapperSource, "run_normal_step_workers_();"))
    {
        return fail("wrapper source missing expected eval calls");
    }
    if (!contains(wrapperSource, "signal_effect__out_writeback_ = unit_effect_part_->out_0;") ||
        !contains(wrapperSource, "signal_mid_writeback_ = unit_part_0_->out_0;") ||
        !contains(wrapperSource, "signal_effect__out_snapshot_ = signal_effect__out_writeback_;") ||
        !contains(wrapperSource, "signal_mid_snapshot_ = signal_mid_writeback_;"))
    {
        return fail("wrapper source missing expected snapshot/writeback publish and commit code");
    }
    if (!contains(wrapperSource, "std::getenv(\"XS_EMU_THREADS\")") ||
        !contains(wrapperSource, "assert(requestedWorkers <= normal_step_fns_.size() && \"XS_EMU_THREADS must not exceed repcut partition count\")") ||
        !contains(wrapperSource, "#if defined(__linux__)"))
    {
        return fail("wrapper source missing expected runtime thread-pool guards");
    }
    const auto commonSourceIt = wrapperSourceByName.find("wolvi_repcut_verilator_sim_common.cpp");
    if (commonSourceIt == wrapperSourceByName.end())
    {
        return fail("missing common wrapper source");
    }
    const std::string &commonSource = commonSourceIt->second;
    if (!contains(wrapperHeader, "using WolviClock = std::chrono::steady_clock;") ||
        !contains(wrapperSource, "void WolviRepCutVerilatorSim::report_step_timing_() const {") ||
        !contains(wrapperSource, "[WOLVI][step-timing] steps=%llu total=%.3f ms avg=%.3f us\\n") ||
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
        !contains(wrapperSource, "step_timing_.part_eval_ns +=") ||
        !contains(wrapperSource, "step_timing_.writeback_ns +=") ||
        !contains(wrapperSource, "step_timing_.total_ns +=") ||
        !contains(wrapperSource, "partTimingStats->scatter_ns +=") ||
        !contains(wrapperSource, "partTimingStats->eval_ns +=") ||
        !contains(wrapperSource, "partTimingStats->gather_ns +=") ||
        !contains(wrapperSource, "partTimingStats->total_ns +=") ||
        !contains(wrapperSource, "partTimingStats = &part_timing_worker_stats_[workerIndex][") ||
        !contains(wrapperSource, "PartTimingStats* partTimingStats = &part_timing_stats_[0];") ||
        !contains(wrapperSource, "PartTimingStats* partTimingStats = &part_timing_stats_[1];") ||
        !contains(wrapperSource, "PartTimingStats* partTimingStats = &part_timing_stats_[2];"))
    {
        return fail("wrapper source missing expected timing instrumentation");
    }
    if (contains(wrapperHeader, "void run_early_phase_();") ||
        contains(wrapperHeader, "early_scatter_ns") ||
        contains(wrapperHeader, "early_eval_ns") ||
        contains(wrapperHeader, "early_publish_ns") ||
        contains(wrapperSource, "run_early_phase_();") ||
        contains(wrapperSource, "printPhase(\"early_scatter\"") ||
        contains(wrapperSource, "printPhase(\"early_eval\"") ||
        contains(wrapperSource, "printPhase(\"early_publish\"") ||
        contains(wrapperSource, "dumpPhase(\"early_scatter\"") ||
        contains(wrapperSource, "dumpPhase(\"early_eval\"") ||
        contains(wrapperSource, "dumpPhase(\"early_publish\""))
    {
        return fail("wrapper source should not contain early-phase scheduling artifacts");
    }
    if (contains(wrapperSource, "XS_REPCUT_STEP_TIMING") ||
        contains(wrapperSource, "step_timing_enabled_") ||
        contains(wrapperSource, "wolvi_env_flag_enabled("))
    {
        return fail("wrapper source should not contain legacy step timing instrumentation");
    }
    if (!contains(commonSource, "Evaluate all units with fused local scatter/eval/gather."))
    {
        return fail("wrapper source missing expected normal scheduling comment");
    }
    const std::size_t dtorPos = commonSource.find("WolviRepCutVerilatorSim::~WolviRepCutVerilatorSim()");
    const std::size_t reportStepPos = commonSource.find("report_step_timing_();", dtorPos);
    const std::size_t reportPartPos = commonSource.find("report_part_timing_();", dtorPos);
    const std::size_t dumpJsonPos = commonSource.find("dump_timing_jsonl_();", dtorPos);
    const std::size_t shutdownPos = commonSource.find("shutdown_part_eval_workers_();", dtorPos);
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
    std::string normalChunkSource;
    for (const auto &[name, text] : wrapperSourceByName)
    {
        if (name.rfind("wolvi_repcut_verilator_sim_normal_", 0) == 0 &&
            text.find("unit_effect_part_->in_2 = top_in_in_data_;") != std::string::npos)
        {
            normalChunkSource = text;
            break;
        }
    }
    if (normalChunkSource.empty())
    {
        return fail("wrapper source missing normal chunk for effect_part");
    }
    const std::size_t commonPartEvalPos = commonSource.find("run_normal_step_workers_();");
    const std::size_t commonWritebackPos = commonSource.find("commit_writeback_();");
    const std::size_t normalInputPos = normalChunkSource.find("unit_effect_part_->in_2 = top_in_in_data_;");
    const std::size_t normalEvalPos = normalChunkSource.find("unit_effect_part_->eval();");
    const std::size_t normalPublishPos =
        normalChunkSource.find("signal_effect__out_writeback_ = unit_effect_part_->out_0;");
    if (commonPartEvalPos == std::string::npos || commonWritebackPos == std::string::npos ||
        normalInputPos == std::string::npos || normalEvalPos == std::string::npos ||
        normalPublishPos == std::string::npos)
    {
        return fail("wrapper source missing phase-order markers");
    }
    if (!(commonPartEvalPos < commonWritebackPos &&
          normalInputPos < normalEvalPos && normalEvalPos < normalPublishPos))
    {
        return fail("wrapper source should run normal workers before writeback and keep per-unit local ordering");
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
    if (!contains(unitsMk, "PARTITIONED_UNITS := effect_part part_0 part_1") ||
        !contains(unitsMk, "PARTITIONED_UNIT_MAKE_J ?= $(if $(strip $(VM_BUILD_JOBS)),-j $(VM_BUILD_JOBS),)") ||
        !contains(unitsMk, "PARTITIONED_VM_PARALLEL_BUILDS ?= $(VM_PARALLEL_BUILDS)") ||
        !contains(unitsMk, "UNIT_effect_part_MODULE := WolviRepCutUnit_effect_part") ||
        !contains(unitsMk, "UNIT_part_0_MODULE := WolviRepCutUnit_part_0") ||
        !contains(unitsMk, "UNIT_part_1_MODULE := WolviRepCutUnit_part_1") ||
        !contains(unitsMk, "$(VERILATOR) --cc -f $(UNIT_effect_part_FILELIST)") ||
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
        !contains(makefile, "WRAPPER_SRC_NAMES :=") ||
        !contains(makefile, "WRAPPER_SRCS := $(addprefix $(PACKAGE_ROOT)/,$(WRAPPER_SRC_NAMES))") ||
        !contains(makefile, "WRAPPER_OBJS := $(addprefix $(BUILD_DIR)/,$(WRAPPER_SRC_NAMES:.cpp=.o))") ||
        !contains(makefile, "VERILATED_DPI_OBJ := $(BUILD_DIR)/verilated_dpi.o") ||
        !contains(makefile, "VERILATED_THREADS_OBJ := $(BUILD_DIR)/verilated_threads.o") ||
        !contains(makefile, "$(BUILD_DIR)/%.o: $(PACKAGE_ROOT)/%.cpp $(PACKAGE_ROOT)/wolvi_repcut_verilator_sim.h $(UNIT_ARCHIVES) | $(BUILD_DIR)") ||
        !contains(makefile, "$(TARGET): $(VERILATED_OBJ) $(VERILATED_DPI_OBJ) $(VERILATED_THREADS_OBJ) $(WRAPPER_OBJS) $(SMOKE_MAIN_OBJ) $(UNIT_ARCHIVES)") ||
        !contains(makefile, "$(CXX) $(LDFLAGS) -o $@ $(VERILATED_OBJ) $(VERILATED_DPI_OBJ) $(VERILATED_THREADS_OBJ) $(WRAPPER_OBJS) $(SMOKE_MAIN_OBJ) $(UNIT_ARCHIVES) $(LDLIBS) -ldl -pthread") ||
        !contains(makefile, "run: $(TARGET)"))
    {
        return fail("Makefile missing expected top-level build rules");
    }

    return 0;
}
