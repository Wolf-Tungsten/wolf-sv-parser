#include "elaborate.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;

namespace {

int fail(const std::string& context, const std::string& message) {
    std::cerr << "[elaborate_blackbox:" << context << "] " << message << '\n';
    return 1;
}

std::optional<std::string> getStringAttr(const grh::ir::Graph* graph, const grh::ir::Operation& op,
                                         std::string_view key) {
    (void)graph;
    auto attr = op.attr(key);
    const std::string* value = attr ? std::get_if<std::string>(&*attr) : nullptr;
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

std::optional<std::vector<std::string>> getStringVecAttr(const grh::ir::Graph* graph,
                                                         const grh::ir::Operation& op,
                                                         std::string_view key) {
    (void)graph;
    auto attr = op.attr(key);
    const auto* value = attr ? std::get_if<std::vector<std::string>>(&*attr) : nullptr;
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

bool buildNetlist(const std::filesystem::path& sourcePath, grh::ir::Netlist& netlist,
                  ElaborateDiagnostics& diagnostics) {
    if (!std::filesystem::exists(sourcePath)) {
        std::cerr << "[elaborate_blackbox] Missing testcase file: " << sourcePath << '\n';
        return false;
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-blackbox");
    argStorage.emplace_back(sourcePath.string());

    std::vector<const char*> argv;
    argv.reserve(argStorage.size());
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        std::cerr << "[elaborate_blackbox] Failed to parse command line arguments\n";
        return false;
    }

    if (!driver.processOptions()) {
        std::cerr << "[elaborate_blackbox] Failed to process driver options\n";
        return false;
    }

    if (!driver.parseAllSources()) {
        std::cerr << "[elaborate_blackbox] Source parsing failed\n";
        return false;
    }

    auto compilation = driver.createCompilation();
    if (!compilation) {
        std::cerr << "[elaborate_blackbox] Failed to create compilation\n";
        return false;
    }

    driver.reportCompilation(*compilation, /* quiet */ true);
    driver.runAnalysis(*compilation);

    Elaborate elaborator(&diagnostics);
    netlist = elaborator.convert(compilation->getRoot());
    return true;
}

int validateBlackbox(const grh::ir::Netlist& netlist) {
    if (netlist.topGraphs().empty()) {
        return fail("netlist", "No top graphs produced");
    }

    const std::string& topName = netlist.topGraphs().front();
    if (topName.rfind("blackbox_top", 0) != 0) {
        return fail("netlist", "Unexpected top graph name: " + topName);
    }

    const grh::ir::Graph* topGraph = netlist.findGraph(topName);
    if (!topGraph) {
        return fail("netlist", "Top graph lookup failed");
    }

    // All parameterized blackbox leaf graphs should be empty (ports only, no placeholder).
    int leafGraphCount = 0;
    for (const auto& [symbol, graphPtr] : netlist.graphs()) {
        if (!graphPtr) {
            continue;
        }
        if (symbol.rfind("blackbox_leaf", 0) == 0) {
            ++leafGraphCount;
            if (!graphPtr->operations().empty()) {
                return fail("netlist", "blackbox_leaf graph unexpectedly contains operations");
            }
        }
    }
    if (leafGraphCount != 2) {
        return fail("netlist", "Expected two parameterized blackbox_leaf graphs");
    }

    std::vector<OperationId> blackboxes;
    for (const auto& opId : topGraph->operations()) {
        const grh::ir::Operation op = topGraph->getOperation(opId);
        if (op.kind() != grh::ir::OperationKind::kBlackbox) {
            continue;
        }
        if (!getStringAttr(topGraph, op, "moduleName")) {
            continue; // Skip placeholder TODO nodes.
        }
        blackboxes.push_back(opId);
    }

    if (blackboxes.size() != 2) {
        return fail("ops", "Expected two kBlackbox operations in top graph");
    }

    auto checkPortNames = [&](const grh::ir::Operation& op) -> bool {
        auto inputNames = getStringVecAttr(topGraph, op, "inputPortName");
        auto outputNames = getStringVecAttr(topGraph, op, "outputPortName");
        return inputNames &&
               outputNames &&
               *inputNames == std::vector<std::string>{"clk", "in0", "in1"} &&
               *outputNames == std::vector<std::string>{"out0"};
    };

    auto checkParams = [&](const grh::ir::Operation& op, const std::string& expectedDepth) -> bool {
        auto paramNames = getStringVecAttr(topGraph, op, "parameterNames");
        auto paramValues = getStringVecAttr(topGraph, op, "parameterValues");
        if (!paramNames || !paramValues) {
            return false;
        }
        if (paramNames->size() != 2 || paramValues->size() != 2) {
            return false;
        }
        if ((*paramNames)[0] != "WIDTH" || (*paramNames)[1] != "DEPTH") {
            return false;
        }
        if ((*paramValues)[0] != "6") {
            return false;
        }
        return (*paramValues)[1] == expectedDepth;
    };

    bool foundDirect = false;
    bool foundGen = false;

    for (OperationId opId : blackboxes) {
        const grh::ir::Operation op = topGraph->getOperation(opId);
        auto moduleNameOpt = getStringAttr(topGraph, op, "moduleName");
        auto instanceNameOpt = getStringAttr(topGraph, op, "instanceName");
        if (!moduleNameOpt || !instanceNameOpt) {
            return fail("ops", "Blackbox operation missing moduleName or instanceName");
        }

        if (*moduleNameOpt != "blackbox_leaf") {
            return fail("ops", "Unexpected moduleName on blackbox op");
        }

        if (!checkPortNames(op)) {
            return fail("ops", "Port names on blackbox op do not match interface");
        }

        if (op.operands().size() != 3 || op.results().size() != 1) {
            return fail("ops", "Unexpected operand/result count on blackbox op");
        }

        const std::string& instanceName = *instanceNameOpt;
        if (checkParams(op, "8")) {
            foundDirect = true;
            ValueId result = op.results().front();
            if (!result || topGraph->getValue(result).symbolText() != "y_direct") {
                return fail("ops", "Direct blackbox output is not wired to y_direct");
            }
        }
        else if (checkParams(op, "4")) {
            foundGen = true;
            ValueId result = op.results().front();
            if (!result || topGraph->getValue(result).symbolText() != "y_gen") {
                return fail("ops", "Generated blackbox output is not wired to y_gen");
            }
        }
        else {
            return fail("ops", "Unexpected parameter values on blackbox op " + instanceName);
        }
    }

    if (!foundDirect || !foundGen) {
        return fail("ops", "Missing expected blackbox instances (direct/gen)");
    }

    return 0;
}

} // namespace

int main() {
    const std::filesystem::path dataPath = std::filesystem::path(WOLF_SV_ELAB_BLACKBOX_DATA_PATH);

    ElaborateDiagnostics diagnostics;
    grh::ir::Netlist netlist;
    if (!buildNetlist(dataPath, netlist, diagnostics)) {
        return 1;
    }

    return validateBlackbox(netlist);
}
