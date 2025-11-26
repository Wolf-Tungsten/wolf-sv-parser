#include "elaborate.hpp"
#include "emit.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;
using namespace wolf_sv::emit;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_latch] " << message << '\n';
    return 1;
}

bool writeArtifact(const grh::Netlist& netlist) {
    const std::filesystem::path artifactPath(WOLF_SV_ELAB_LATCH_ARTIFACT_PATH);
    if (artifactPath.empty()) {
        return true;
    }
    if (const std::filesystem::path dir = artifactPath.parent_path(); !dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(dir) &&
            !std::filesystem::create_directories(dir, ec) && ec) {
            std::cerr << "[elaborate_latch] Failed to create artifact dir: " << ec.message()
                      << '\n';
            return false;
        }
    }

    EmitDiagnostics emitDiag;
    EmitJSON emitter(&emitDiag);
    EmitOptions opts;
    auto json = emitter.emitToString(netlist, opts);
    if (!json || emitDiag.hasError()) {
        std::cerr << "[elaborate_latch] Failed to emit JSON artifact\n";
        return false;
    }

    std::ofstream os(artifactPath, std::ios::trunc);
    if (!os.is_open()) {
        std::cerr << "[elaborate_latch] Failed to open artifact file: " << artifactPath << '\n';
        return false;
    }
    os << *json;
    return true;
}

const grh::Graph* findGraph(const grh::Netlist& netlist, std::string_view name) {
    return netlist.findGraph(name);
}

const grh::Value* findValue(const grh::Graph& graph, std::string_view name) {
    return graph.findValue(std::string(name));
}

const grh::Operation* findOpByKind(const grh::Graph& graph, grh::OperationKind kind) {
    for (const auto& sym : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(sym);
        if (op.kind() == kind) {
            return &op;
        }
    }
    return nullptr;
}

} // namespace

#ifndef WOLF_SV_ELAB_LATCH_DATA_PATH
#error "WOLF_SV_ELAB_LATCH_DATA_PATH must be defined"
#endif
#ifndef WOLF_SV_ELAB_LATCH_ARTIFACT_PATH
#error "WOLF_SV_ELAB_LATCH_ARTIFACT_PATH must be defined"
#endif

int main() {
    std::filesystem::path sourcePath(WOLF_SV_ELAB_LATCH_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing latch testcase file: " + sourcePath.string());
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-latch");
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
        return fail("Failed to write latch artifact JSON");
    }

    auto expectLatchOp = [&](std::string_view graphName, grh::OperationKind kind,
                             std::optional<std::string_view> enName,
                             std::optional<std::string_view> dataName,
                             std::optional<std::string_view> rstName,
                             std::optional<std::string_view> resetValName) -> bool {
        const grh::Graph* graph = findGraph(netlist, graphName);
        if (!graph) {
            fail("Graph not found: " + std::string(graphName));
            return false;
        }
        const grh::Operation* latch = findOpByKind(*graph, kind);
        if (!latch) {
            fail("Latch op missing in graph: " + std::string(graphName));
            return false;
        }
        const auto& operands = latch->operands();
        const auto& results = latch->results();
        if (results.size() != 1) {
            return fail("Latch results size mismatch in graph: " + std::string(graphName));
        }
        if (kind == grh::OperationKind::kLatch) {
            if (operands.size() != 2) {
                return fail("kLatch operand count mismatch");
            }
        }
        else {
            if (operands.size() != 4) {
                return fail("kLatchArst operand count mismatch");
            }
        }

        const grh::Value* en = operands[0];
        const grh::Value* d = operands.back();
        if (!en || en->width() != 1) {
            fail("Latch enable mismatch");
            return false;
        }
        if (enName && en->symbol() != *enName) {
            fail("Latch enable symbol mismatch");
            return false;
        }
        if (!d) {
            fail("Latch data missing");
            return false;
        }
        if (dataName && d->symbol() != *dataName) {
            fail("Latch data symbol mismatch");
            return false;
        }
        if (rstName) {
            const grh::Value* rst = operands[1];
            const grh::Value* resetVal = operands[2];
            if (!rst || rst->symbol() != *rstName || rst->width() != 1) {
                fail("Latch reset signal mismatch");
                return false;
            }
            if (!resetVal) {
                fail("Latch resetValue missing");
                return false;
            }
            if (resetValName && resetVal->symbol() != *resetValName) {
                fail("Latch resetValue symbol mismatch");
                return false;
            }
            if (resetVal->width() != d->width()) {
                fail("Latch resetValue width mismatch");
                return false;
            }
            if (const grh::Operation* def = resetVal->definingOp()) {
                if (def->kind() != grh::OperationKind::kConstant) {
                    fail("Latch resetValue is not driven by constant");
                    return false;
                }
            }
        }
        return true;
    };

    if (!expectLatchOp("latch_always_latch", grh::OperationKind::kLatch,
                       std::optional<std::string_view>("en"),
                       std::optional<std::string_view>("d"), std::nullopt, std::nullopt)) {
        return 1;
    }
    if (!expectLatchOp("latch_inferred", grh::OperationKind::kLatch,
                       std::optional<std::string_view>("en"),
                       std::optional<std::string_view>("d"), std::nullopt, std::nullopt)) {
        return 1;
    }
    if (!expectLatchOp("latch_inferred_arst", grh::OperationKind::kLatchArst,
                       std::optional<std::string_view>("en"),
                       std::optional<std::string_view>("d"),
                       std::optional<std::string_view>("rst"), std::nullopt)) {
        return 1;
    }
    if (!expectLatchOp("latch_inferred_case", grh::OperationKind::kLatch, std::nullopt,
                       std::optional<std::string_view>("a"), std::nullopt, std::nullopt)) {
        return 1;
    }

    bool sawLatchWarning = false;
    for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
        if (diag.kind == ElaborateDiagnosticKind::Warning &&
            diag.message.find("Latch inferred") != std::string::npos) {
            sawLatchWarning = true;
            break;
        }
    }
    if (!sawLatchWarning) {
        return fail("Expected latch warning was not emitted");
    }

    return 0;
}
