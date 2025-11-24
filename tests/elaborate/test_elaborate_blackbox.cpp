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

bool buildNetlist(const std::filesystem::path& sourcePath, grh::Netlist& netlist,
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

int validateBlackbox(const grh::Netlist& netlist) {
    if (netlist.topGraphs().empty()) {
        return fail("netlist", "No top graphs produced");
    }

    const std::string& topName = netlist.topGraphs().front();
    if (topName.rfind("blackbox_top", 0) != 0) {
        return fail("netlist", "Unexpected top graph name: " + topName);
    }

    const grh::Graph* topGraph = netlist.findGraph(topName);
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
            if (!graphPtr->operationOrder().empty()) {
                return fail("netlist", "blackbox_leaf graph unexpectedly contains operations");
            }
        }
    }
    if (leafGraphCount != 2) {
        return fail("netlist", "Expected two parameterized blackbox_leaf graphs");
    }

    std::vector<const grh::Operation*> blackboxes;
    for (const auto& opSymbol : topGraph->operationOrder()) {
        const grh::Operation& op = topGraph->getOperation(opSymbol);
        if (op.kind() != grh::OperationKind::kBlackbox) {
            continue;
        }
        if (op.attributes().count("moduleName") == 0) {
            continue; // Skip placeholder TODO nodes.
        }
        blackboxes.push_back(&op);
    }

    if (blackboxes.size() != 2) {
        return fail("ops", "Expected two kBlackbox operations in top graph");
    }

    auto checkPortNames = [](const grh::Operation& op) -> bool {
        const auto& attrs = op.attributes();
        auto inputNamesIt = attrs.find("inputPortName");
        auto outputNamesIt = attrs.find("outputPortName");
        if (inputNamesIt == attrs.end() || outputNamesIt == attrs.end()) {
            return false;
        }
        const auto& inputNames = std::get<std::vector<std::string>>(inputNamesIt->second);
        const auto& outputNames = std::get<std::vector<std::string>>(outputNamesIt->second);
        return inputNames == std::vector<std::string>{"clk", "in0", "in1"} &&
               outputNames == std::vector<std::string>{"out0"};
    };

    auto checkParams = [](const grh::Operation& op, const std::string& expectedDepth) -> bool {
        const auto& attrs = op.attributes();
        auto paramNamesIt = attrs.find("parameterNames");
        auto paramValuesIt = attrs.find("parameterValues");
        if (paramNamesIt == attrs.end() || paramValuesIt == attrs.end()) {
            return false;
        }
        const auto& paramNames = std::get<std::vector<std::string>>(paramNamesIt->second);
        const auto& paramValues = std::get<std::vector<std::string>>(paramValuesIt->second);
        if (paramNames.size() != 2 || paramValues.size() != 2) {
            return false;
        }
        if (paramNames[0] != "WIDTH" || paramNames[1] != "DEPTH") {
            return false;
        }
        if (paramValues[0] != "6") {
            return false;
        }
        return paramValues[1] == expectedDepth;
    };

    bool foundDirect = false;
    bool foundGen = false;

    for (const grh::Operation* op : blackboxes) {
        if (!op) {
            continue;
        }
        const auto& attrs = op->attributes();
        auto moduleNameIt = attrs.find("moduleName");
        auto instanceNameIt = attrs.find("instanceName");
        if (moduleNameIt == attrs.end() || instanceNameIt == attrs.end()) {
            return fail("ops", "Blackbox operation missing moduleName or instanceName");
        }

        if (std::get<std::string>(moduleNameIt->second) != "blackbox_leaf") {
            return fail("ops", "Unexpected moduleName on blackbox op");
        }

        if (!checkPortNames(*op)) {
            return fail("ops", "Port names on blackbox op do not match interface");
        }

        if (op->operands().size() != 3 || op->results().size() != 1) {
            return fail("ops", "Unexpected operand/result count on blackbox op");
        }

        const std::string& instanceName = std::get<std::string>(instanceNameIt->second);
        if (checkParams(*op, "8")) {
            foundDirect = true;
            if (op->results().front()->symbol() != "y_direct") {
                return fail("ops", "Direct blackbox output is not wired to y_direct");
            }
        }
        else if (checkParams(*op, "4")) {
            foundGen = true;
            if (op->results().front()->symbol() != "y_gen") {
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
    grh::Netlist netlist;
    if (!buildNetlist(dataPath, netlist, diagnostics)) {
        return 1;
    }

    return validateBlackbox(netlist);
}
