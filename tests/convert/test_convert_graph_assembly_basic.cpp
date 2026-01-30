#include "convert.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[convert-graph-assembly-basic] " << message << '\n';
    return 1;
}

struct CompilationBundle {
    slang::driver::Driver driver;
    std::unique_ptr<slang::ast::Compilation> compilation;
};

std::unique_ptr<CompilationBundle> compileInput(const std::filesystem::path& sourcePath,
                                                std::string_view topModule) {
    auto bundle = std::make_unique<CompilationBundle>();
    auto& driver = bundle->driver;
    driver.addStandardArgs();
    driver.languageVersion = slang::LanguageVersion::v1800_2023;
    if (!topModule.empty()) {
        driver.options.topModules.emplace_back(topModule);
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("convert-graph-assembly-basic");
    argStorage.emplace_back(sourcePath.string());
    std::vector<const char*> argv;
    argv.reserve(argStorage.size());
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        return nullptr;
    }
    if (!driver.processOptions()) {
        return nullptr;
    }
    if (!driver.parseAllSources()) {
        return nullptr;
    }

    bundle->compilation = driver.createCompilation();
    if (!bundle->compilation) {
        return nullptr;
    }
    driver.reportCompilation(*bundle->compilation, /* quiet */ true);
    driver.runAnalysis(*bundle->compilation);
    return bundle;
}

bool hasPort(std::span<const grh::ir::Port> ports, const grh::ir::Graph& graph,
             std::string_view name) {
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return true;
        }
    }
    return false;
}

int testGraphAssemblyBasic(const std::filesystem::path& sourcePath) {
    auto bundle = compileInput(sourcePath, "graph_assembly_basic");
    if (!bundle || !bundle->compilation) {
        return fail("Failed to compile " + sourcePath.string());
    }

    wolf_sv_parser::ConvertDriver driver;
    grh::ir::Netlist netlist = driver.convert(bundle->compilation->getRoot());

    if (netlist.topGraphs().size() != 1) {
        return fail("Expected exactly one top graph");
    }
    if (netlist.topGraphs().front() != "graph_assembly_basic") {
        return fail("Unexpected top graph name");
    }

    const grh::ir::Graph* graph = netlist.findGraph("graph_assembly_basic");
    if (!graph) {
        return fail("Missing graph_assembly_basic graph");
    }

    if (!hasPort(graph->inputPorts(), *graph, "clk") ||
        !hasPort(graph->inputPorts(), *graph, "a") ||
        !hasPort(graph->inputPorts(), *graph, "b") ||
        !hasPort(graph->inputPorts(), *graph, "en")) {
        return fail("Missing expected input ports");
    }

    if (!hasPort(graph->outputPorts(), *graph, "y") ||
        !hasPort(graph->outputPorts(), *graph, "q") ||
        !hasPort(graph->outputPorts(), *graph, "l")) {
        return fail("Missing expected output ports");
    }

    bool hasAssign = false;
    bool hasRegister = false;
    bool hasLatch = false;

    for (grh::ir::OperationId opId : graph->operations()) {
        grh::ir::Operation op = graph->getOperation(opId);
        switch (op.kind()) {
        case grh::ir::OperationKind::kAssign:
            hasAssign = true;
            break;
        case grh::ir::OperationKind::kRegister:
            hasRegister = true;
            break;
        case grh::ir::OperationKind::kLatch:
            hasLatch = true;
            break;
        default:
            break;
        }
    }

    if (!hasAssign || !hasRegister || !hasLatch) {
        return fail("Missing expected assign/register/latch operations");
    }

    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath = WOLF_SV_CONVERT_GRAPH_ASSEMBLY_DATA_PATH;
    return testGraphAssemblyBasic(sourcePath);
}
