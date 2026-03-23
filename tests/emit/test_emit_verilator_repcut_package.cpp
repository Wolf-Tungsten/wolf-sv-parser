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

    const std::filesystem::path manifestPath = artifactRoot / "manifest.json";
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

    for (const auto &path : {manifestPath, debugSvPath, part0SvPath, part1SvPath, topSvPath,
                             debugFileList, part0FileList, part1FileList, wrapperHeaderPath, wrapperSourcePath,
                             smokeMainPath, unitsMkPath, makefilePath})
    {
        if (!std::filesystem::exists(path))
        {
            return fail("expected package artifact is missing: " + path.string());
        }
    }

    const std::string manifest = readFile(manifestPath);
    if (manifest.empty())
    {
        return fail("failed to read manifest.json");
    }
    if (!contains(manifest, "\"top_module\":\"SimTop\""))
    {
        return fail("manifest missing top_module");
    }
    if (!contains(manifest, "\"top_inputs\":[{\"name\":\"clock\",\"width\":1,\"signed\":false},{\"name\":\"reset\",\"width\":1,\"signed\":false},{\"name\":\"in_data\",\"width\":8,\"signed\":false}]"))
    {
        return fail("manifest missing top_inputs");
    }
    if (contains(manifest, "\"clock_port\"") || contains(manifest, "\"reset_port\""))
    {
        return fail("manifest should not special-case clock/reset ports");
    }
    if (!contains(manifest, "\"serial_eval_order\":[\"debug_part\",\"part_0\",\"part_1\"]"))
    {
        return fail("manifest missing serial_eval_order");
    }
    if (!contains(manifest, "\"instance_name\":\"part_0\"") ||
        !contains(manifest, "\"instance_name\":\"part_1\"") ||
        !contains(manifest, "\"instance_name\":\"debug_part\""))
    {
        return fail("manifest missing unit list entries");
    }
    if (!contains(manifest, "\"kind\":\"top_to_unit\"") ||
        !contains(manifest, "\"kind\":\"unit_to_unit\"") ||
        !contains(manifest, "\"kind\":\"unit_to_top\"") ||
        !contains(manifest, "\"kind\":\"const_to_unit\""))
    {
        return fail("manifest missing expected connection kinds");
    }
    if (!contains(manifest, "\"signal\":\"mid\"") ||
        !contains(manifest, "\"driver\":{\"type\":\"unit\",\"instance\":\"part_0\",\"port\":\"out_0\"}") ||
        !contains(manifest, "\"sinks\":[{\"type\":\"unit\",\"instance\":\"part_1\",\"port\":\"in_2\"}]"))
    {
        return fail("manifest missing expected unit_to_unit connection details");
    }
    if (!contains(manifest, "\"signal\":\"out_alias\"") ||
        !contains(manifest, "\"driver\":{\"type\":\"unit\",\"instance\":\"part_1\",\"port\":\"out_0\"}") ||
        !contains(manifest, "\"sinks\":[{\"type\":\"top\",\"port\":\"out\"}]"))
    {
        return fail("manifest missing expected unit_to_top connection details");
    }
    if (!contains(manifest, "\"signal\":\"sel_const\"") ||
        !contains(manifest, "\"driver\":{\"type\":\"const\",\"value\":\"1'b1\"}") ||
        !contains(manifest, "\"port\":\"in_3\""))
    {
        return fail("manifest missing expected const_to_unit connection details");
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
    if (!contains(wrapperHeader, "std::vector<std::function<void()>> logic_eval_fns_;") ||
        !contains(wrapperHeader, "void run_part_eval_workers_();"))
    {
        return fail("wrapper header missing expected parallel eval declarations");
    }
    if (!contains(wrapperHeader, "void set_clock(CData value) { top_in_clock_ = value; }") ||
        !contains(wrapperHeader, "void set_in_data(CData value) { top_in_in_data_ = value; }") ||
        !contains(wrapperHeader, "CData get_out() const { return top_out_out_; }"))
    {
        return fail("wrapper header missing expected top port accessors");
    }
    if (!contains(wrapperSource, "unit_debug_part_->in_2 = top_in_in_data_;") ||
        !contains(wrapperSource, "unit_part_0_->in_2 = top_in_in_data_;") ||
        !contains(wrapperSource, "unit_part_1_->in_2 = signal_mid_;") ||
        !contains(wrapperSource, "unit_part_1_->in_3 = const_sel_const_;"))
    {
        return fail("wrapper source missing expected scatter code");
    }
    if (!contains(wrapperSource, "unit_debug_part_->eval();") ||
        !contains(wrapperSource, "logic_eval_fns_.emplace_back([this]() { unit_part_0_->eval(); });") ||
        !contains(wrapperSource, "logic_eval_fns_.emplace_back([this]() { unit_part_1_->eval(); });") ||
        !contains(wrapperSource, "run_part_eval_workers_();"))
    {
        return fail("wrapper source missing expected eval calls");
    }
    if (!contains(wrapperSource, "std::getenv(\"XS_EMU_THREADS\")") ||
        !contains(wrapperSource, "assert(requestedWorkers <= logic_eval_fns_.size() && \"XS_EMU_THREADS must not exceed repcut partition count\")") ||
        !contains(wrapperSource, "#if defined(__linux__)"))
    {
        return fail("wrapper source missing expected runtime thread-pool guards");
    }
    if (!contains(wrapperSource, "Evaluate debug_part first so DPI/device responses are available before logic eval.") ||
        !contains(wrapperSource, "Evaluate all non-debug units after every cross-partition input has been loaded."))
    {
        return fail("wrapper source missing expected debug_part scheduling comments");
    }
    if (!contains(wrapperSource, "signal_mid_ = unit_part_0_->out_0;") ||
        !contains(wrapperSource, "top_out_out_ = unit_part_1_->out_0;"))
    {
        return fail("wrapper source missing expected gather code");
    }
    const std::size_t part1InputPos = wrapperSource.find("unit_part_1_->in_2 = signal_mid_;");
    const std::size_t debugEvalPos = wrapperSource.find("unit_debug_part_->eval();");
    const std::size_t partEvalPos = wrapperSource.find("run_part_eval_workers_();");
    const std::size_t gatherPos = wrapperSource.find("signal_mid_ = unit_part_0_->out_0;");
    if (part1InputPos == std::string::npos || debugEvalPos == std::string::npos ||
        partEvalPos == std::string::npos || gatherPos == std::string::npos)
    {
        return fail("wrapper source missing phase-order markers");
    }
    if (!(part1InputPos < debugEvalPos && debugEvalPos < partEvalPos && partEvalPos < gatherPos))
    {
        return fail("wrapper source should scatter all inputs, eval debug_part first, then run parallel part eval, then gather outputs");
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
