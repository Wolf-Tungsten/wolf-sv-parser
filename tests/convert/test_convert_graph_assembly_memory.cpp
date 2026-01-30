#include "convert.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[convert-graph-assembly-memory] " << message << '\n';
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
    argStorage.emplace_back("convert-graph-assembly-memory");
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

std::optional<int64_t> getAttrInt(const grh::ir::Operation& op, std::string_view key) {
    auto attr = op.attr(key);
    if (!attr) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<int64_t>(&*attr)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<bool> getAttrBool(const grh::ir::Operation& op, std::string_view key) {
    auto attr = op.attr(key);
    if (!attr) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<bool>(&*attr)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<std::string> getAttrString(const grh::ir::Operation& op, std::string_view key) {
    auto attr = op.attr(key);
    if (!attr) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<std::string>(&*attr)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> getAttrStrings(const grh::ir::Operation& op,
                                                       std::string_view key) {
    auto attr = op.attr(key);
    if (!attr) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<std::vector<std::string>>(&*attr)) {
        return *value;
    }
    return std::nullopt;
}

int testGraphAssemblyMemory(const std::filesystem::path& sourcePath) {
    auto bundle = compileInput(sourcePath, "graph_assembly_memory");
    if (!bundle || !bundle->compilation) {
        return fail("Failed to compile " + sourcePath.string());
    }

    wolf_sv_parser::ConvertDriver driver;
    grh::ir::Netlist netlist = driver.convert(bundle->compilation->getRoot());

    if (netlist.topGraphs().size() != 1) {
        return fail("Expected exactly one top graph");
    }
    if (netlist.topGraphs().front() != "graph_assembly_memory") {
        return fail("Unexpected top graph name");
    }

    const grh::ir::Graph* graph = netlist.findGraph("graph_assembly_memory");
    if (!graph) {
        return fail("Missing graph_assembly_memory graph");
    }

    int memoryOps = 0;
    int readOps = 0;
    int writeOps = 0;
    bool memoryAttrsOk = false;

    for (grh::ir::OperationId opId : graph->operations()) {
        grh::ir::Operation op = graph->getOperation(opId);
        switch (op.kind()) {
        case grh::ir::OperationKind::kMemory: {
            ++memoryOps;
            auto width = getAttrInt(op, "width");
            auto rows = getAttrInt(op, "row");
            auto isSigned = getAttrBool(op, "isSigned");
            if (!width || !rows || !isSigned) {
                return fail("kMemory missing width/row/isSigned attributes");
            }
            if (*width != 8 || *rows != 16 || *isSigned) {
                return fail("kMemory attributes do not match expected width/row/isSigned");
            }
            memoryAttrsOk = true;
            break;
        }
        case grh::ir::OperationKind::kMemoryReadPort: {
            ++readOps;
            auto memSymbol = getAttrString(op, "memSymbol");
            if (!memSymbol || *memSymbol != "mem") {
                return fail("kMemoryReadPort missing or unexpected memSymbol");
            }
            break;
        }
        case grh::ir::OperationKind::kMemoryWritePort: {
            ++writeOps;
            auto memSymbol = getAttrString(op, "memSymbol");
            if (!memSymbol || *memSymbol != "mem") {
                return fail("kMemoryWritePort missing or unexpected memSymbol");
            }
            auto edges = getAttrStrings(op, "eventEdge");
            if (!edges || edges->empty() || (*edges)[0] != "posedge") {
                return fail("kMemoryWritePort missing eventEdge attribute");
            }
            break;
        }
        default:
            break;
        }
    }

    if (!memoryAttrsOk || memoryOps != 1) {
        return fail("Expected exactly one kMemory op with valid attributes");
    }
    if (readOps < 2) {
        return fail("Expected at least two kMemoryReadPort ops");
    }
    if (writeOps != 1) {
        return fail("Expected exactly one kMemoryWritePort op");
    }

    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath = WOLF_SV_CONVERT_GRAPH_ASSEMBLY_MEMORY_DATA_PATH;
    return testGraphAssemblyMemory(sourcePath);
}
