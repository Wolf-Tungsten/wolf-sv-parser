#include "elaborate.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_smoke] " << message << '\n';
    return 1;
}

} // namespace

int main() {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    const std::filesystem::path sourcePath = std::filesystem::path(WOLF_SV_ELAB_SMOKE_INPUT_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing testcase file: " + sourcePath.string());
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-smoke");
    argStorage.emplace_back(sourcePath.string());
    std::vector<const char*> argv;
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        return fail("Failed to parse smoke test command line");
    }

    if (!driver.processOptions()) {
        return fail("Failed to process smoke test options");
    }

    if (!driver.parseAllSources()) {
        return fail("Failed to parse smoke test sources");
    }

    auto compilation = driver.createCompilation();
    if (!compilation) {
        return fail("Compilation creation failed");
    }
    driver.reportCompilation(*compilation, /* quiet */ true);
    driver.runAnalysis(*compilation);

    ElaborateDiagnostics diagnostics;
    Elaborate elaborator(&diagnostics);
    grh::Netlist netlist = elaborator.convert(compilation->getRoot());

    if (netlist.topGraphs().size() != 1) {
        return fail("Expected exactly one top graph");
    }

    const std::string& topName = netlist.topGraphs().front();
    if (topName.rfind("t0", 0) != 0) {
        return fail("Unexpected top graph name: " + topName);
    }

    const grh::Graph* graph = netlist.findGraph(topName);
    if (!graph) {
        return fail("Top graph lookup failed");
    }

    if (graph->inputPorts().size() != 1 || graph->inputPorts().count("i_port") == 0) {
        return fail("Input port i_port missing from graph");
    }
    if (graph->outputPorts().size() != 1 || graph->outputPorts().count("o_port") == 0) {
        return fail("Output port o_port missing from graph");
    }

    if (graph->operations().empty()) {
        return fail("Expected placeholder operation in graph");
    }
    const grh::Operation& op = *graph->operations().front();
    if (op.kind() != grh::OperationKind::kBlackbox) {
        return fail("Placeholder operation has unexpected kind");
    }

    if (diagnostics.messages().empty()) {
        return fail("Expected TODO diagnostic entry");
    }

    // Ensure at least one TODO message references the module body.
    bool foundTodo = false;
    for (const ElaborateDiagnostic& message : diagnostics.messages()) {
        if (message.kind == ElaborateDiagnosticKind::Todo) {
            foundTodo = true;
            break;
        }
    }
    if (!foundTodo) {
        return fail("Missing TODO diagnostic");
    }

    return 0;
}
