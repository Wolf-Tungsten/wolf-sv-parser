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

    const slang::ast::InstanceSymbol* topInstance = nullptr;
    for (const slang::ast::InstanceSymbol* inst : compilation->getRoot().topInstances) {
        if (inst && inst->name == "comb_always_stage12_case") {
            topInstance = inst;
            break;
        }
    }
    if (!topInstance) {
        return fail("comb_always_stage12_case top instance not found");
    }

    grh::Graph* graph = netlist.findGraph("comb_always_stage12_case");
    if (!graph) {
        return fail("GRH graph comb_always_stage12_case not found");
    }

    const slang::ast::InstanceBodySymbol* canonicalBody = topInstance->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body = canonicalBody ? *canonicalBody : topInstance->body;
    std::span<const SignalMemoEntry> netMemo = elaborator.peekNetMemo(body);
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

    auto hasRealDiag = std::any_of(diagnostics.messages().begin(), diagnostics.messages().end(),
                                   [](const ElaborateDiagnostic& diag) {
                                       return diag.message != "Module body elaboration pending";
                                   });
    if (hasRealDiag) {
        for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
            std::cerr << "[elaborate_comb_always] unexpected diagnostic: " << diag.message << '\n';
        }
        return fail("Diagnostics emitted during comb always elaboration");
    }

    return 0;
}
