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
    std::cerr << "[convert-graph-assembly-slice] " << message << '\n';
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
    argStorage.emplace_back("convert-graph-assembly-slice");
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

int testGraphAssemblySlice(const std::filesystem::path& sourcePath) {
    auto bundle = compileInput(sourcePath, "graph_assembly_slice");
    if (!bundle || !bundle->compilation) {
        return fail("Failed to compile " + sourcePath.string());
    }

    wolf_sv_parser::ConvertDriver driver;
    grh::ir::Netlist netlist = driver.convert(bundle->compilation->getRoot());

    if (netlist.topGraphs().size() != 1) {
        return fail("Expected exactly one top graph");
    }
    if (netlist.topGraphs().front() != "graph_assembly_slice") {
        return fail("Unexpected top graph name");
    }

    const grh::ir::Graph* graph = netlist.findGraph("graph_assembly_slice");
    if (!graph) {
        return fail("Missing graph_assembly_slice graph");
    }

    bool hasConcat = false;
    bool hasSlice = false;
    bool hasConst = false;

    for (grh::ir::OperationId opId : graph->operations()) {
        grh::ir::Operation op = graph->getOperation(opId);
        switch (op.kind()) {
        case grh::ir::OperationKind::kConcat:
            hasConcat = true;
            break;
        case grh::ir::OperationKind::kSliceStatic:
            hasSlice = true;
            break;
        case grh::ir::OperationKind::kConstant:
            hasConst = true;
            break;
        default:
            break;
        }
    }

    if (!hasConcat) {
        return fail("Missing kConcat op in graph");
    }
    if (!hasSlice) {
        return fail("Missing kSliceStatic op in graph");
    }
    if (!hasConst) {
        return fail("Missing kConstant op in graph");
    }
    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath = WOLF_SV_CONVERT_GRAPH_ASSEMBLY_SLICE_DATA_PATH;
    return testGraphAssemblySlice(sourcePath);
}
