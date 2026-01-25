#include "elaborate.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv_parser;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_mem] " << message << '\n';
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

const slang::ast::InstanceBodySymbol& fetchBody(const slang::ast::InstanceSymbol& inst) {
    const slang::ast::InstanceBodySymbol* canonical = inst.getCanonicalBody();
    return canonical ? *canonical : inst.body;
}

const slang::ast::InstanceSymbol*
findInstanceByName(std::span<const slang::ast::InstanceSymbol* const> instances,
                   std::string_view name) {
    for (const slang::ast::InstanceSymbol* inst : instances) {
        if (inst && inst->name == name) {
            return inst;
        }
    }
    return nullptr;
}

bool isMemoryWritePortKind(grh::ir::OperationKind kind) {
    switch (kind) {
    case grh::ir::OperationKind::kMemoryWritePort:
    case grh::ir::OperationKind::kMemoryWritePortRst:
    case grh::ir::OperationKind::kMemoryWritePortArst:
    case grh::ir::OperationKind::kMemoryMaskWritePort:
    case grh::ir::OperationKind::kMemoryMaskWritePortRst:
    case grh::ir::OperationKind::kMemoryMaskWritePortArst:
        return true;
    default:
        return false;
    }
}

int countMemoryWritePorts(const grh::ir::Graph& graph, std::string_view memSymbol) {
    int count = 0;
    for (const auto& opId : graph.operations()) {
        const grh::ir::Operation op = graph.getOperation(opId);
        if (!isMemoryWritePortKind(op.kind())) {
            continue;
        }
        auto attr = op.attr("memSymbol");
        const std::string* symbol = attr ? std::get_if<std::string>(&*attr) : nullptr;
        if (symbol && *symbol == memSymbol) {
            ++count;
        }
    }
    return count;
}

} // namespace

#ifndef WOLF_SV_ELAB_MEM_DATA_PATH
#error "WOLF_SV_ELAB_MEM_DATA_PATH must be defined"
#endif

int main() {
    const std::filesystem::path sourcePath(WOLF_SV_ELAB_MEM_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing testcase file: " + sourcePath.string());
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-mem");
    argStorage.emplace_back(sourcePath.string());

    std::vector<const char*> argv;
    argv.reserve(argStorage.size());
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        return fail("Failed to parse command line");
    }
    if (!driver.processOptions()) {
        return fail("Failed to process driver options");
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

    const grh::ir::Graph* graph = netlist.findGraph("mem_multi_clk");
    if (!graph) {
        return fail("Graph mem_multi_clk not found");
    }

    const slang::ast::InstanceSymbol* top =
        findInstanceByName(compilation->getRoot().topInstances, "mem_multi_clk");
    if (!top) {
        return fail("Top instance mem_multi_clk not found");
    }

    const auto& body = fetchBody(*top);
    const SignalMemoEntry* memEntry = findEntry(elaborator.peekMemMemo(body), "mem");
    if (!memEntry) {
        return fail("mem memo entry not found");
    }
    if (memEntry->forceRegisterArray) {
        return fail("mem should not be forced into register array");
    }
    if (!memEntry->stateOp) {
        return fail("mem memo entry missing stateOp");
    }
    if (graph->getOperation(memEntry->stateOp).kind() != grh::ir::OperationKind::kMemory) {
        return fail("mem memo entry is not a kMemory op");
    }

    const grh::ir::OperationId memOp = graph->findOperation("mem");
    if (!memOp) {
        return fail("kMemory op named mem not found");
    }
    if (graph->getOperation(memOp).kind() != grh::ir::OperationKind::kMemory) {
        return fail("mem op is not kMemory");
    }

    const int writePorts = countMemoryWritePorts(*graph, "mem");
    if (writePorts < 2) {
        return fail("Expected >=2 memory write ports, found " + std::to_string(writePorts));
    }

    std::cout << "[elaborate_mem] ok: mem kept as kMemory with "
              << writePorts << " write ports\n";
    return 0;
}
