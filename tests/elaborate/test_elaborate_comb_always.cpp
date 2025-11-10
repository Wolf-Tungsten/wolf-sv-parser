#include "elaborate.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;

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

const grh::Value* findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    auto it = ports.find(std::string(name));
    if (it != ports.end()) {
        return it->second;
    }
    return nullptr;
}

bool writeArtifact(const grh::Netlist& netlist) {
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

    os << netlist.toJsonString(true);
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
    Elaborate elaborator(&diagnostics);
    grh::Netlist netlist = elaborator.convert(compilation->getRoot());

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

    auto fetchGraphByName = [&](std::string_view name) -> grh::Graph* {
        return netlist.findGraph(name);
    };

    auto getAssignSource = [&](const SignalMemoEntry& entry) -> const grh::Value* {
        if (!entry.value) {
            return nullptr;
        }
        const grh::Operation* assign = entry.value->definingOp();
        if (!assign || assign->kind() != grh::OperationKind::kAssign ||
            assign->operands().size() != 1) {
            return nullptr;
        }
        return assign->operands().front();
    };

    const slang::ast::InstanceSymbol* instStage12 = findInstanceByName("comb_always_stage12_case");
    if (!instStage12) {
        return fail("comb_always_stage12_case top instance not found");
    }

    grh::Graph* graph = fetchGraphByName("comb_always_stage12_case");
    if (!graph) {
        return fail("GRH graph comb_always_stage12_case not found");
    }

    std::span<const SignalMemoEntry> netMemo = fetchNetMemoForInstance(*instStage12);
    if (netMemo.empty()) {
        return fail("Net memo is empty for comb_always_stage12_case");
    }

    const grh::Value* portInA = findPort(*graph, "in_a", /*isInput=*/true);
    const grh::Value* portInB = findPort(*graph, "in_b", /*isInput=*/true);
    if (!portInA || !portInB) {
        return fail("Input ports in_a/in_b not registered in graph");
    }

    const SignalMemoEntry* captureA = findEntry(netMemo, "capture_a");
    const SignalMemoEntry* captureB = findEntry(netMemo, "capture_b");
    const SignalMemoEntry* orValue = findEntry(netMemo, "or_value");
    if (!captureA || !captureB || !orValue) {
        return fail("Failed to locate capture_a/capture_b/or_value memo entries");
    }

    auto verifyAssignOperand = [&](const SignalMemoEntry& entry, const grh::Value* expected) -> bool {
        if (!entry.value) {
            fail(std::string(entry.symbol->name) + " memo missing GRH value");
            return false;
        }
        const grh::Operation* assign = entry.value->definingOp();
        if (!assign || assign->kind() != grh::OperationKind::kAssign) {
            fail(std::string(entry.symbol->name) + " is not driven by kAssign");
            return false;
        }
        if (assign->operands().size() != 1 || assign->operands().front() != expected) {
            fail(std::string(entry.symbol->name) + " assign operand mismatch");
            return false;
        }
        return true;
    };

    if (!verifyAssignOperand(*captureA, portInA)) {
        return 1;
    }
    if (!verifyAssignOperand(*captureB, portInB)) {
        return 1;
    }

    if (!orValue->value) {
        return fail("or_value memo entry missing value");
    }
    const grh::Operation* orAssign = orValue->value->definingOp();
    if (!orAssign || orAssign->kind() != grh::OperationKind::kAssign ||
        orAssign->operands().empty()) {
        return fail("or_value is not driven by assign as expected");
    }
    const grh::Value* orResult = orAssign->operands().front();
    const grh::Operation* orOp = orResult->definingOp();
    if (!orOp || orOp->kind() != grh::OperationKind::kOr || orOp->operands().size() != 2) {
        return fail("or_value assign is expected to originate from kOr");
    }
    if ((orOp->operands()[0] != portInA || orOp->operands()[1] != portInB) &&
        (orOp->operands()[0] != portInB || orOp->operands()[1] != portInA)) {
        return fail("or_value kOr operands do not reference in_a/in_b");
    }

    // Stage13 if tests
    const slang::ast::InstanceSymbol* instIf = findInstanceByName("comb_always_stage13_if");
    if (!instIf) {
        return fail("comb_always_stage13_if top instance not found");
    }
    if (!fetchGraphByName("comb_always_stage13_if")) {
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
    auto verifyDrivenByMux = [&](const SignalMemoEntry& entry, std::string_view label) -> bool {
        const grh::Value* driver = getAssignSource(entry);
        if (!driver) {
            std::cerr << "[elaborate_comb_always] " << label << " missing assign driver\n";
            return false;
        }
        const grh::Operation* mux = driver->definingOp();
        if (!mux || mux->kind() != grh::OperationKind::kMux) {
            std::cerr << "[elaborate_comb_always] " << label << " is not driven by kMux\n";
            return false;
        }
        return true;
    };
    if (!verifyDrivenByMux(*outIf, "out_if")) {
        return 1;
    }
    if (!verifyDrivenByMux(*outNested, "out_nested")) {
        return 1;
    }

    // Stage13 case tests
    const slang::ast::InstanceSymbol* instCase = findInstanceByName("comb_always_stage13_case");
    if (!instCase) {
        return fail("comb_always_stage13_case top instance not found");
    }
    if (!fetchGraphByName("comb_always_stage13_case")) {
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
    const grh::Value* caseDriver = getAssignSource(*outCase);
    if (!caseDriver) {
        return fail("out_case missing assign driver");
    }
    const grh::Operation* caseMux = caseDriver->definingOp();
    if (!caseMux || caseMux->kind() != grh::OperationKind::kMux) {
        return fail("out_case is not driven by outer kMux");
    }
    bool hasNestedMux = false;
    if (caseMux->operands().size() >= 3) {
        for (std::size_t idx = 1; idx < caseMux->operands().size(); ++idx) {
            const grh::Value* branch = caseMux->operands()[idx];
            if (branch && branch->definingOp() &&
                branch->definingOp()->kind() == grh::OperationKind::kMux) {
                hasNestedMux = true;
                break;
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
    grh::Graph* graphDefault = fetchGraphByName("comb_always_stage13_default_if");
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
    const grh::Value* defaultDriver = getAssignSource(*outDefault);
    if (!defaultDriver) {
        return fail("out_default missing assign driver");
    }
    const grh::Operation* defaultMux = defaultDriver->definingOp();
    if (!defaultMux || defaultMux->kind() != grh::OperationKind::kMux ||
        defaultMux->operands().size() != 3) {
        return fail("out_default is expected to be driven by a 2-way kMux");
    }
    const grh::Value* condPort = findPort(*graphDefault, "cond", /*isInput=*/true);
    const grh::Value* defPort = findPort(*graphDefault, "in_default", /*isInput=*/true);
    const grh::Value* overridePort = findPort(*graphDefault, "in_override", /*isInput=*/true);
    if (!condPort || !defPort || !overridePort) {
        return fail("comb_always_stage13_default_if missing input ports");
    }
    if (defaultMux->operands()[0] != condPort) {
        return fail("out_default mux condition does not reference cond input");
    }
    const grh::Value* muxTrue = defaultMux->operands()[1];
    const grh::Value* muxFalse = defaultMux->operands()[2];
    auto matchesPorts = [&](const grh::Value* value, const grh::Value* expected) {
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
        if (!fetchGraphByName(instanceName)) {
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
        if (!verifyDrivenByMux(*entry, signalName)) {
            return false;
        }
        return true;
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

    bool sawExpectedLatchDiag = false;
    bool sawUniqueCaseDiag = false;
    std::vector<std::string> unexpectedDiags;
    for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
        if (diag.message == "Module body elaboration pending") {
            continue;
        }
        if (diag.message.find("comb always branch coverage incomplete") != std::string::npos) {
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
