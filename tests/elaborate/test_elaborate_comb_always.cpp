#include "elaborate.hpp"
#include "emit.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv_parser;
using namespace grh::emit;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_comb_always] " << message << '\n';
    return 1;
}

const SignalMemoEntry* findEntry(std::span<const SignalMemoEntry> memo,
                                 std::string_view name) {
    for (const SignalMemoEntry& entry : memo) {
        if (entry.symbol && entry.symbol->name == name) {
            return &entry;
        }
    }
    return nullptr;
}

ValueId findPort(const grh::ir::Graph& graph, std::string_view name, bool isInput) {
    const auto ports = isInput ? graph.inputPorts() : graph.outputPorts();
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
}

bool writeArtifact(const grh::ir::Netlist& netlist) {
    const std::filesystem::path artifactPath(WOLF_SV_ELAB_COMB_ALWAYS_ARTIFACT_PATH);
    if (artifactPath.empty()) {
        return true;
    }

    if (const std::filesystem::path dir = artifactPath.parent_path(); !dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(dir) &&
            !std::filesystem::create_directories(dir, ec) && ec) {
            std::cerr << "[elaborate_comb_always] Failed to create artifact dir: "
                      << ec.message() << '\n';
            return false;
        }
    }

    std::ofstream os(artifactPath, std::ios::trunc);
    if (!os.is_open()) {
        std::cerr << "[elaborate_comb_always] Failed to open artifact file: "
                  << artifactPath.string() << '\n';
        return false;
    }

    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    auto jsonOpt = emitter.emitToString(netlist, options);
    if (!jsonOpt || diagnostics.hasError()) {
        std::cerr << "[elaborate_comb_always] Failed to emit JSON artifact\n";
        return false;
    }

    os << *jsonOpt;
    return true;
}

} // namespace

#ifndef WOLF_SV_ELAB_COMB_ALWAYS_DATA_PATH
#error "WOLF_SV_ELAB_COMB_ALWAYS_DATA_PATH must be defined"
#endif
#ifndef WOLF_SV_ELAB_COMB_ALWAYS_ARTIFACT_PATH
#error "WOLF_SV_ELAB_COMB_ALWAYS_ARTIFACT_PATH must be defined"
#endif

int main() {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    const std::filesystem::path sourcePath(WOLF_SV_ELAB_COMB_ALWAYS_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing comb always testcase file: " + sourcePath.string());
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-comb-always");
    argStorage.emplace_back(sourcePath.string());

    std::vector<const char*> argv;
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        return fail("Failed to parse command line");
    }
    if (!driver.processOptions()) {
        return fail("Failed to process options");
    }
    if (!driver.parseAllSources()) {
        return fail("Failed to parse sources");
    }

    auto compilation = driver.createCompilation();
    if (!compilation) {
        return fail("Failed to create compilation");
    }
    driver.reportCompilation(*compilation, /* quiet */ true);
    driver.runAnalysis(*compilation);

    ElaborateDiagnostics diagnostics;
    ElaborateOptions elaborateOptions;
    elaborateOptions.abortOnError = false;
    Elaborate elaborator(&diagnostics, elaborateOptions);
    grh::ir::Netlist netlist = elaborator.convert(compilation->getRoot());

    if (!writeArtifact(netlist)) {
        return fail("Failed to write comb always artifact");
    }

    auto findInstanceByName = [&](std::string_view name) -> const slang::ast::InstanceSymbol* {
        for (const slang::ast::InstanceSymbol* inst : compilation->getRoot().topInstances) {
            if (inst && inst->name == name) {
                return inst;
            }
        }
        return nullptr;
    };

    auto fetchBody = [](const slang::ast::InstanceSymbol& inst) -> const slang::ast::InstanceBodySymbol& {
        const slang::ast::InstanceBodySymbol* canonical = inst.getCanonicalBody();
        return canonical ? *canonical : inst.body;
    };

    auto fetchNetMemoForInstance = [&](const slang::ast::InstanceSymbol& inst) {
        return elaborator.peekNetMemo(fetchBody(inst));
    };

    auto fetchGraphByName = [&](std::string_view name) -> grh::ir::Graph* {
        return netlist.findGraph(name);
    };

    auto getAssignSource = [](const grh::ir::Graph& graph, const SignalMemoEntry& entry) -> ValueId {
        if (!entry.value) {
            return ValueId::invalid();
        }
        OperationId assignId = graph.getValue(entry.value).definingOp();
        if (!assignId) {
            return ValueId::invalid();
        }
        const grh::ir::Operation assign = graph.getOperation(assignId);
        if (assign.kind() != grh::ir::OperationKind::kAssign || assign.operands().size() != 1) {
            return ValueId::invalid();
        }
        return assign.operands().front();
    };

    const slang::ast::InstanceSymbol* instStage12 = findInstanceByName("comb_always_stage12_case");
    if (!instStage12) {
        return fail("comb_always_stage12_case top instance not found");
    }

    grh::ir::Graph* graph = fetchGraphByName("comb_always_stage12_case");
    if (!graph) {
        return fail("GRH graph comb_always_stage12_case not found");
    }

    std::span<const SignalMemoEntry> netMemo = fetchNetMemoForInstance(*instStage12);
    if (netMemo.empty()) {
        return fail("Net memo is empty for comb_always_stage12_case");
    }

    ValueId portInA = findPort(*graph, "in_a", /*isInput=*/true);
    ValueId portInB = findPort(*graph, "in_b", /*isInput=*/true);
    if (!portInA || !portInB) {
        return fail("Input ports in_a/in_b not registered in graph");
    }

    const SignalMemoEntry* captureA = findEntry(netMemo, "capture_a");
    const SignalMemoEntry* captureB = findEntry(netMemo, "capture_b");
    const SignalMemoEntry* orValue = findEntry(netMemo, "or_value");
    if (!captureA || !captureB || !orValue) {
        return fail("Failed to locate capture_a/capture_b/or_value memo entries");
    }

    auto verifyAssignOperand = [&](const grh::ir::Graph& graphRef, const SignalMemoEntry& entry,
                                   ValueId expected) -> bool {
        if (!entry.value) {
            fail(std::string(entry.symbol->name) + " memo missing GRH value");
            return false;
        }
        OperationId assignId = graphRef.getValue(entry.value).definingOp();
        if (!assignId) {
            fail(std::string(entry.symbol->name) + " is not driven by kAssign");
            return false;
        }
        const grh::ir::Operation assign = graphRef.getOperation(assignId);
        if (assign.kind() != grh::ir::OperationKind::kAssign) {
            fail(std::string(entry.symbol->name) + " is not driven by kAssign");
            return false;
        }
        if (assign.operands().size() != 1 || assign.operands().front() != expected) {
            fail(std::string(entry.symbol->name) + " assign operand mismatch");
            return false;
        }
        return true;
    };

    if (!verifyAssignOperand(*graph, *captureA, portInA)) {
        return 1;
    }
    if (!verifyAssignOperand(*graph, *captureB, portInB)) {
        return 1;
    }

    if (!orValue->value) {
        return fail("or_value memo entry missing value");
    }
    OperationId orAssignId = graph->getValue(orValue->value).definingOp();
    if (!orAssignId) {
        return fail("or_value is not driven by assign as expected");
    }
    const grh::ir::Operation orAssign = graph->getOperation(orAssignId);
    if (orAssign.kind() != grh::ir::OperationKind::kAssign ||
        orAssign.operands().empty()) {
        return fail("or_value is not driven by assign as expected");
    }
    ValueId orResult = orAssign.operands().front();
    OperationId orOpId = graph->getValue(orResult).definingOp();
    if (!orOpId) {
        return fail("or_value assign is expected to originate from kOr");
    }
    const grh::ir::Operation orOp = graph->getOperation(orOpId);
    if (orOp.kind() != grh::ir::OperationKind::kOr || orOp.operands().size() != 2) {
        return fail("or_value assign is expected to originate from kOr");
    }
    if ((orOp.operands()[0] != portInA || orOp.operands()[1] != portInB) &&
        (orOp.operands()[0] != portInB || orOp.operands()[1] != portInA)) {
        return fail("or_value kOr operands do not reference in_a/in_b");
    }

    // Stage13 if tests
    const slang::ast::InstanceSymbol* instIf = findInstanceByName("comb_always_stage13_if");
    if (!instIf) {
        return fail("comb_always_stage13_if top instance not found");
    }
    grh::ir::Graph* graphIf = fetchGraphByName("comb_always_stage13_if");
    if (!graphIf) {
        return fail("GRH graph comb_always_stage13_if not found");
    }
    std::span<const SignalMemoEntry> netMemoIf = fetchNetMemoForInstance(*instIf);
    if (netMemoIf.empty()) {
        return fail("Net memo is empty for comb_always_stage13_if");
    }

    const SignalMemoEntry* outIf = findEntry(netMemoIf, "out_if");
    const SignalMemoEntry* outNested = findEntry(netMemoIf, "out_nested");
    if (!outIf || !outNested) {
        return fail("Failed to locate out_if/out_nested memo entries");
    }
    auto verifyDrivenByMux = [&](const grh::ir::Graph& graphRef, const SignalMemoEntry& entry,
                                 std::string_view label) -> bool {
        ValueId driver = getAssignSource(graphRef, entry);
        if (!driver) {
            std::cerr << "[elaborate_comb_always] " << label << " missing assign driver\n";
            return false;
        }
        OperationId muxId = graphRef.getValue(driver).definingOp();
        if (!muxId) {
            std::cerr << "[elaborate_comb_always] " << label << " missing mux op\n";
            return false;
        }
        const grh::ir::Operation mux = graphRef.getOperation(muxId);
        if (mux.kind() != grh::ir::OperationKind::kMux) {
            std::cerr << "[elaborate_comb_always] " << label << " is not driven by kMux\n";
            return false;
        }
        return true;
    };
    auto verifyDirectWithoutMux = [&](const grh::ir::Graph& graphRef, const SignalMemoEntry& entry,
                                      ValueId expected, std::string_view label) -> bool {
        if (!verifyAssignOperand(graphRef, entry, expected)) {
            return false;
        }
        ValueId driver = getAssignSource(graphRef, entry);
        if (!driver) {
            std::cerr << "[elaborate_comb_always] " << label << " missing assign driver\n";
            return false;
        }
        OperationId opId = graphRef.getValue(driver).definingOp();
        if (opId) {
            const grh::ir::Operation op = graphRef.getOperation(opId);
            if (op.kind() == grh::ir::OperationKind::kMux) {
                std::cerr << "[elaborate_comb_always] " << label
                          << " unexpectedly driven by kMux under static condition\n";
                return false;
            }
        }
        return true;
    };
    if (!verifyDrivenByMux(*graphIf, *outIf, "out_if")) {
        return 1;
    }
    if (!verifyDrivenByMux(*graphIf, *outNested, "out_nested")) {
        return 1;
    }

    // Stage13 case tests
    const slang::ast::InstanceSymbol* instCase = findInstanceByName("comb_always_stage13_case");
    if (!instCase) {
        return fail("comb_always_stage13_case top instance not found");
    }
    grh::ir::Graph* graphCase = fetchGraphByName("comb_always_stage13_case");
    if (!graphCase) {
        return fail("GRH graph comb_always_stage13_case not found");
    }
    std::span<const SignalMemoEntry> netMemoCase = fetchNetMemoForInstance(*instCase);
    if (netMemoCase.empty()) {
        return fail("Net memo is empty for comb_always_stage13_case");
    }
    const SignalMemoEntry* outCase = findEntry(netMemoCase, "out_case");
    if (!outCase) {
        return fail("Failed to locate out_case memo entry");
    }
    ValueId caseDriver = getAssignSource(*graphCase, *outCase);
    if (!caseDriver) {
        return fail("out_case missing assign driver");
    }
    OperationId caseMuxId = graphCase->getValue(caseDriver).definingOp();
    if (!caseMuxId) {
        return fail("out_case missing mux op");
    }
    const grh::ir::Operation caseMux = graphCase->getOperation(caseMuxId);
    if (caseMux.kind() != grh::ir::OperationKind::kMux) {
        return fail("out_case is not driven by outer kMux");
    }
    bool hasNestedMux = false;
    if (caseMux.operands().size() >= 3) {
        for (std::size_t idx = 1; idx < caseMux.operands().size(); ++idx) {
            const ValueId branch = caseMux.operands()[idx];
            OperationId branchOpId = branch ? graphCase->getValue(branch).definingOp()
                                            : OperationId::invalid();
            if (branchOpId) {
                const grh::ir::Operation branchOp = graphCase->getOperation(branchOpId);
                if (branchOp.kind() == grh::ir::OperationKind::kMux) {
                    hasNestedMux = true;
                    break;
                }
            }
        }
    }
    if (!hasNestedMux) {
        return fail("case mux chain is expected to contain nested mux nodes");
    }

    // Stage13 default-if tests (default assignment before if acts as implicit else)
    const slang::ast::InstanceSymbol* instDefault =
        findInstanceByName("comb_always_stage13_default_if");
    if (!instDefault) {
        return fail("comb_always_stage13_default_if top instance not found");
    }
    grh::ir::Graph* graphDefault = fetchGraphByName("comb_always_stage13_default_if");
    if (!graphDefault) {
        return fail("GRH graph comb_always_stage13_default_if not found");
    }
    std::span<const SignalMemoEntry> netMemoDefault = fetchNetMemoForInstance(*instDefault);
    if (netMemoDefault.empty()) {
        return fail("Net memo is empty for comb_always_stage13_default_if");
    }
    const SignalMemoEntry* outDefault = findEntry(netMemoDefault, "out_default");
    if (!outDefault) {
        return fail("Failed to locate out_default memo entry");
    }
    ValueId defaultDriver = getAssignSource(*graphDefault, *outDefault);
    if (!defaultDriver) {
        return fail("out_default missing assign driver");
    }
    OperationId defaultMuxId = graphDefault->getValue(defaultDriver).definingOp();
    if (!defaultMuxId) {
        return fail("out_default missing mux op");
    }
    const grh::ir::Operation defaultMux = graphDefault->getOperation(defaultMuxId);
    if (defaultMux.kind() != grh::ir::OperationKind::kMux ||
        defaultMux.operands().size() != 3) {
        return fail("out_default is expected to be driven by a 2-way kMux");
    }
    ValueId condPort = findPort(*graphDefault, "cond", /*isInput=*/true);
    ValueId defPort = findPort(*graphDefault, "in_default", /*isInput=*/true);
    ValueId overridePort = findPort(*graphDefault, "in_override", /*isInput=*/true);
    if (!condPort || !defPort || !overridePort) {
        return fail("comb_always_stage13_default_if missing input ports");
    }
    if (defaultMux.operands()[0] != condPort) {
        return fail("out_default mux condition does not reference cond input");
    }
    ValueId muxTrue = defaultMux.operands()[1];
    ValueId muxFalse = defaultMux.operands()[2];
    auto matchesPorts = [&](ValueId value, ValueId expected) {
        return value == expected;
    };
    if (!(matchesPorts(muxTrue, overridePort) && matchesPorts(muxFalse, defPort)) &&
        !(matchesPorts(muxTrue, defPort) && matchesPorts(muxFalse, overridePort))) {
        return fail("out_default mux operands do not tie to in_default/in_override");
    }

    const auto validateMuxOutput = [&](std::string_view instanceName,
                                       std::string_view signalName) -> bool {
        const slang::ast::InstanceSymbol* inst = findInstanceByName(instanceName);
        if (!inst) {
            fail(std::string(instanceName) + " top instance not found");
            return false;
        }
        grh::ir::Graph* g = fetchGraphByName(instanceName);
        if (!g) {
            fail(std::string("GRH graph ") + std::string(instanceName) + " not found");
            return false;
        }
        std::span<const SignalMemoEntry> memo = fetchNetMemoForInstance(*inst);
        if (memo.empty()) {
            fail(std::string("Net memo is empty for ") + std::string(instanceName));
            return false;
        }
        const SignalMemoEntry* entry = findEntry(memo, signalName);
        if (!entry) {
            fail(std::string("Failed to locate ") + std::string(signalName) + " memo entry");
            return false;
        }
        if (!verifyDrivenByMux(*g, *entry, signalName)) {
            return false;
        }
        return true;
    };

    auto collectLeavesForOp = [&](const grh::ir::Graph& graph, ValueId root,
                                  grh::ir::OperationKind foldKind,
                                  std::unordered_set<ValueId, grh::ir::ValueIdHash>& leaves) {
        if (!root) {
            return;
        }
        std::vector<ValueId> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            ValueId node = stack.back();
            stack.pop_back();
            OperationId opId = graph.getValue(node).definingOp();
            if (!opId) {
                leaves.insert(node);
                continue;
            }
            const grh::ir::Operation op = graph.getOperation(opId);
            if (op.kind() != foldKind) {
                leaves.insert(node);
                continue;
            }
            for (ValueId operand : op.operands()) {
                stack.push_back(operand);
            }
        }
    };

    if (!validateMuxOutput("comb_always_stage13_case_defaultless", "out_case_implicit")) {
        return 1;
    }
    if (!validateMuxOutput("comb_always_stage13_casex", "out_casex")) {
        return 1;
    }
    if (!validateMuxOutput("comb_always_stage13_casez", "out_casez")) {
        return 1;
    }

    // Stage14 static if tests
    const slang::ast::InstanceSymbol* instStaticIf =
        findInstanceByName("comb_always_stage14_static_if");
    if (!instStaticIf) {
        return fail("comb_always_stage14_static_if top instance not found");
    }
    grh::ir::Graph* graphStaticIf = fetchGraphByName("comb_always_stage14_static_if");
    if (!graphStaticIf) {
        return fail("GRH graph comb_always_stage14_static_if not found");
    }
    std::span<const SignalMemoEntry> netMemoStaticIf = fetchNetMemoForInstance(*instStaticIf);
    if (netMemoStaticIf.empty()) {
        return fail("Net memo is empty for comb_always_stage14_static_if");
    }
    const SignalMemoEntry* outStaticTrue = findEntry(netMemoStaticIf, "out_static_true");
    const SignalMemoEntry* outStaticFalse = findEntry(netMemoStaticIf, "out_static_false");
    const SignalMemoEntry* outMixed = findEntry(netMemoStaticIf, "out_mixed");
    if (!outStaticTrue || !outStaticFalse || !outMixed) {
        return fail("Failed to locate stage14 static-if memo entries");
    }
    ValueId portInTrue = findPort(*graphStaticIf, "in_true", /*isInput=*/true);
    ValueId portInFalse = findPort(*graphStaticIf, "in_false", /*isInput=*/true);
    ValueId portDynA = findPort(*graphStaticIf, "dyn_a", /*isInput=*/true);
    ValueId portDynB = findPort(*graphStaticIf, "dyn_b", /*isInput=*/true);
    ValueId portSelect = findPort(*graphStaticIf, "select", /*isInput=*/true);
    if (!portInTrue || !portInFalse || !portDynA || !portDynB || !portSelect) {
        return fail("comb_always_stage14_static_if inputs missing in graph");
    }
    if (!verifyDirectWithoutMux(*graphStaticIf, *outStaticTrue, portInTrue, "out_static_true")) {
        return 1;
    }
    if (!verifyDirectWithoutMux(*graphStaticIf, *outStaticFalse, portInFalse,
                                "out_static_false")) {
        return 1;
    }
    if (!verifyDrivenByMux(*graphStaticIf, *outMixed, "out_mixed")) {
        return 1;
    }
    ValueId mixedDriver = getAssignSource(*graphStaticIf, *outMixed);
    if (!mixedDriver) {
        return fail("out_mixed missing assign driver");
    }
    OperationId mixedMuxId = graphStaticIf->getValue(mixedDriver).definingOp();
    if (!mixedMuxId) {
        return fail("out_mixed missing mux op");
    }
    const grh::ir::Operation mixedMux = graphStaticIf->getOperation(mixedMuxId);
    if (mixedMux.operands().size() != 3) {
        return fail("out_mixed mux expected to have 3 operands");
    }
    if (mixedMux.operands()[0] != portSelect) {
        return fail("out_mixed mux condition does not reference select input");
    }
    ValueId mixedTrue = mixedMux.operands()[1];
    ValueId mixedFalse = mixedMux.operands()[2];
    if (!((mixedTrue == portDynA && mixedFalse == portDynB) ||
          (mixedTrue == portDynB && mixedFalse == portDynA))) {
        return fail("out_mixed mux operands do not reference dyn_a/dyn_b inputs");
    }

    // Stage14 static case tests
    const slang::ast::InstanceSymbol* instStaticCase =
        findInstanceByName("comb_always_stage14_static_case");
    if (!instStaticCase) {
        return fail("comb_always_stage14_static_case top instance not found");
    }
    grh::ir::Graph* graphStaticCase = fetchGraphByName("comb_always_stage14_static_case");
    if (!graphStaticCase) {
        return fail("GRH graph comb_always_stage14_static_case not found");
    }
    std::span<const SignalMemoEntry> netMemoStaticCase = fetchNetMemoForInstance(*instStaticCase);
    if (netMemoStaticCase.empty()) {
        return fail("Net memo is empty for comb_always_stage14_static_case");
    }
    const SignalMemoEntry* outCaseConst = findEntry(netMemoStaticCase, "out_case_const");
    const SignalMemoEntry* outCaseDefault = findEntry(netMemoStaticCase, "out_case_default");
    const SignalMemoEntry* outCaseNested = findEntry(netMemoStaticCase, "out_case_nested");
    if (!outCaseConst || !outCaseDefault || !outCaseNested) {
        return fail("Failed to locate stage14 static-case memo entries");
    }
    ValueId portIn0 = findPort(*graphStaticCase, "in0", /*isInput=*/true);
    ValueId portIn1 = findPort(*graphStaticCase, "in1", /*isInput=*/true);
    ValueId portIn2 = findPort(*graphStaticCase, "in2", /*isInput=*/true);
    ValueId portIn3 = findPort(*graphStaticCase, "in3", /*isInput=*/true);
    ValueId portDynCaseA = findPort(*graphStaticCase, "dyn_a", /*isInput=*/true);
    ValueId portDynCaseB = findPort(*graphStaticCase, "dyn_b", /*isInput=*/true);
    ValueId portCaseSelect = findPort(*graphStaticCase, "select", /*isInput=*/true);
    if (!portIn0 || !portIn1 || !portIn2 || !portIn3 || !portDynCaseA || !portDynCaseB ||
        !portCaseSelect) {
        return fail("comb_always_stage14_static_case inputs missing in graph");
    }
    (void)portIn0;
    (void)portIn1;
    if (!verifyDirectWithoutMux(*graphStaticCase, *outCaseConst, portIn2,
                                "out_case_const")) {
        return 1;
    }
    if (!verifyDirectWithoutMux(*graphStaticCase, *outCaseDefault, portIn3,
                                "out_case_default")) {
        return 1;
    }
    if (!verifyDrivenByMux(*graphStaticCase, *outCaseNested, "out_case_nested")) {
        return 1;
    }
    ValueId nestedDriver = getAssignSource(*graphStaticCase, *outCaseNested);
    if (!nestedDriver) {
        return fail("out_case_nested missing assign driver");
    }
    OperationId nestedMuxId = graphStaticCase->getValue(nestedDriver).definingOp();
    if (!nestedMuxId) {
        return fail("out_case_nested missing mux op");
    }
    const grh::ir::Operation nestedMux = graphStaticCase->getOperation(nestedMuxId);
    if (nestedMux.operands().size() != 3) {
        return fail("out_case_nested mux expected to have 3 operands");
    }
    if (nestedMux.operands()[0] != portCaseSelect) {
        return fail("out_case_nested mux condition does not reference select input");
    }
    ValueId nestedTrue = nestedMux.operands()[1];
    ValueId nestedFalse = nestedMux.operands()[2];
    if (!((nestedTrue == portDynCaseA && nestedFalse == portDynCaseB) ||
          (nestedTrue == portDynCaseB && nestedFalse == portDynCaseA))) {
        return fail("out_case_nested mux operands do not reference dyn_a/dyn_b inputs");
    }

    // Stage15 for-loop reductions
    {
        const slang::ast::InstanceSymbol* instStage15For =
            findInstanceByName("comb_always_stage15_for");
        if (!instStage15For) {
            return fail("comb_always_stage15_for top instance not found");
        }
        grh::ir::Graph* graphStage15For = fetchGraphByName("comb_always_stage15_for");
        if (!graphStage15For) {
            return fail("GRH graph comb_always_stage15_for not found");
        }
        std::span<const SignalMemoEntry> netMemoStage15For =
            fetchNetMemoForInstance(*instStage15For);
        const SignalMemoEntry* outStage15For = findEntry(netMemoStage15For, "out_for");
        if (!outStage15For) {
            return fail("Failed to locate out_for memo entry");
        }
        ValueId portEven = findPort(*graphStage15For, "data_even", /*isInput=*/true);
        ValueId portOdd = findPort(*graphStage15For, "data_odd", /*isInput=*/true);
        if (!portEven || !portOdd) {
            return fail("comb_always_stage15_for missing data_even/data_odd ports");
        }
        ValueId forDriver = getAssignSource(*graphStage15For, *outStage15For);
        if (!forDriver) {
            return fail("out_for missing assign driver");
        }
        std::unordered_set<ValueId, grh::ir::ValueIdHash> orLeaves;
        collectLeavesForOp(*graphStage15For, forDriver, grh::ir::OperationKind::kOr, orLeaves);
        if (!orLeaves.count(portEven) || !orLeaves.count(portOdd)) {
            return fail("out_for kOr tree does not reference both data_even and data_odd inputs");
        }
    }

    // Stage15 foreach XOR
    {
        const slang::ast::InstanceSymbol* instStage15Foreach =
            findInstanceByName("comb_always_stage15_foreach");
        if (!instStage15Foreach) {
            return fail("comb_always_stage15_foreach top instance not found");
        }
        grh::ir::Graph* graphStage15Foreach = fetchGraphByName("comb_always_stage15_foreach");
        if (!graphStage15Foreach) {
            return fail("GRH graph comb_always_stage15_foreach not found");
        }
        std::span<const SignalMemoEntry> netMemoStage15Foreach =
            fetchNetMemoForInstance(*instStage15Foreach);
        const SignalMemoEntry* outStage15Foreach =
            findEntry(netMemoStage15Foreach, "out_foreach");
        if (!outStage15Foreach) {
            return fail("Failed to locate out_foreach memo entry");
        }
        ValueId portSrc0 = findPort(*graphStage15Foreach, "src0", /*isInput=*/true);
        ValueId portSrc1 = findPort(*graphStage15Foreach, "src1", /*isInput=*/true);
        if (!portSrc0 || !portSrc1) {
            return fail("comb_always_stage15_foreach missing src ports");
        }
        ValueId foreachDriver = getAssignSource(*graphStage15Foreach, *outStage15Foreach);
        if (!foreachDriver) {
            return fail("out_foreach missing assign driver");
        }
        std::unordered_set<ValueId, grh::ir::ValueIdHash> xorLeaves;
        collectLeavesForOp(*graphStage15Foreach, foreachDriver, grh::ir::OperationKind::kXor,
                           xorLeaves);
        if (!xorLeaves.count(portSrc0) || !xorLeaves.count(portSrc1)) {
            return fail("out_foreach kXor tree does not reference src0/src1 inputs");
        }
    }

    // Stage15 break/continue semantics
    {
        const slang::ast::InstanceSymbol* instStage15Break =
            findInstanceByName("comb_always_stage15_break");
        if (!instStage15Break) {
            return fail("comb_always_stage15_break top instance not found");
        }
        grh::ir::Graph* graphStage15Break = fetchGraphByName("comb_always_stage15_break");
        if (!graphStage15Break) {
            return fail("GRH graph comb_always_stage15_break not found");
        }
        std::span<const SignalMemoEntry> netMemoStage15Break =
            fetchNetMemoForInstance(*instStage15Break);
        const SignalMemoEntry* outStage15Break =
            findEntry(netMemoStage15Break, "out_break");
        if (!outStage15Break) {
            return fail("Failed to locate out_break memo entry");
        }
        ValueId portBreakB = findPort(*graphStage15Break, "break_b", /*isInput=*/true);
        if (!portBreakB) {
            return fail("comb_always_stage15_break missing break_b port");
        }
        if (!verifyDirectWithoutMux(*graphStage15Break, *outStage15Break, portBreakB,
                                    "out_break")) {
            return 1;
        }

        const slang::ast::InstanceSymbol* instStage15Continue =
            findInstanceByName("comb_always_stage15_continue");
        if (!instStage15Continue) {
            return fail("comb_always_stage15_continue top instance not found");
        }
        grh::ir::Graph* graphStage15Continue = fetchGraphByName("comb_always_stage15_continue");
        if (!graphStage15Continue) {
            return fail("GRH graph comb_always_stage15_continue not found");
        }
        std::span<const SignalMemoEntry> netMemoStage15Continue =
            fetchNetMemoForInstance(*instStage15Continue);
        const SignalMemoEntry* outStage15Continue =
            findEntry(netMemoStage15Continue, "out_continue");
        if (!outStage15Continue) {
            return fail("Failed to locate out_continue memo entry");
        }
        ValueId portContB = findPort(*graphStage15Continue, "cont_b", /*isInput=*/true);
        if (!portContB) {
            return fail("comb_always_stage15_continue missing cont_b port");
        }
        if (!verifyDirectWithoutMux(*graphStage15Continue, *outStage15Continue, portContB,
                                    "out_continue")) {
            return 1;
        }
    }

    bool sawExpectedLatchDiag = false;
    bool sawUniqueCaseDiag = false;
    std::vector<std::string> unexpectedDiags;
    for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
        if (diag.message == "Module body elaboration pending") {
            continue;
        }
        if (diag.message.find("comb always branch coverage incomplete") != std::string::npos ||
            diag.message.find("Latch inferred for procedural block") != std::string::npos) {
            sawExpectedLatchDiag = true;
            continue;
        }
        if (diag.message.find("unique case items overlap") != std::string::npos) {
            sawUniqueCaseDiag = true;
            continue;
        }
        unexpectedDiags.push_back(diag.message);
    }
    if (!sawExpectedLatchDiag) {
        return fail("Expected latch diagnostic for comb_always_stage13_incomplete was not emitted");
    }
    if (!sawUniqueCaseDiag) {
        return fail("Expected unique case overlap diagnostic was not emitted");
    }
    if (!unexpectedDiags.empty()) {
        for (const std::string& msg : unexpectedDiags) {
            std::cerr << "[elaborate_comb_always] unexpected diagnostic: " << msg << '\n';
        }
        return fail("Unexpected diagnostics emitted during comb always elaboration");
    }

    return 0;
}
