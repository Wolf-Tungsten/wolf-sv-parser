#include "elaborate.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;

namespace {

int fail(const std::string& context, const std::string& message) {
    std::cerr << "[elaborate_hierarchy:" << context << "] " << message << '\n';
    return 1;
}

const std::filesystem::path kArtifactDir = std::filesystem::path(WOLF_SV_ELAB_ARTIFACT_DIR);

bool buildNetlist(const std::string& context, const std::filesystem::path& sourcePath,
                  grh::Netlist& netlist, ElaborateDiagnostics& diagnostics) {
    if (!std::filesystem::exists(sourcePath)) {
        std::cerr << "[elaborate_hierarchy:" << context << "] Missing testcase file: "
                  << sourcePath << '\n';
        return false;
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-hierarchy");
    argStorage.emplace_back(sourcePath.string());

    std::vector<const char*> argv;
    argv.reserve(argStorage.size());
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        std::cerr << "[elaborate_hierarchy:" << context
                  << "] Failed to parse command line arguments\n";
        return false;
    }

    if (!driver.processOptions()) {
        std::cerr << "[elaborate_hierarchy:" << context << "] Failed to process driver options\n";
        return false;
    }

    if (!driver.parseAllSources()) {
        std::cerr << "[elaborate_hierarchy:" << context << "] Source parsing failed\n";
        return false;
    }

    auto compilation = driver.createCompilation();
    if (!compilation) {
        std::cerr << "[elaborate_hierarchy:" << context << "] Failed to create compilation\n";
        return false;
    }

    driver.reportCompilation(*compilation, /* quiet */ true);
    driver.runAnalysis(*compilation);

    Elaborate elaborator(&diagnostics);
    netlist = elaborator.convert(compilation->getRoot());
    return true;
}

bool writeArtifact(const std::string& context, const grh::Netlist& netlist) {
    if (!std::filesystem::exists(kArtifactDir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(kArtifactDir, ec) && ec) {
            std::cerr << "[elaborate_hierarchy:" << context
                      << "] Failed to create artifact directory: " << ec.message() << '\n';
            return false;
        }
    }

    std::filesystem::path outputPath = kArtifactDir / (context + ".json");
    std::ofstream os(outputPath);
    if (!os.is_open()) {
        std::cerr << "[elaborate_hierarchy:" << context
                  << "] Failed to open artifact file for writing: " << outputPath << '\n';
        return false;
    }
    os << netlist.toJsonString(/* pretty */ true);
    return true;
}

int validateNested(const grh::Netlist& netlist) {
    if (netlist.topGraphs().size() != 1 || netlist.topGraphs().front() != "nested_top") {
        return fail("nested", "Unexpected top graph layout");
    }

    const grh::Graph* topGraph = netlist.findGraph("nested_top");
    const grh::Graph* midGraph = netlist.findGraph("nested_mid");
    const grh::Graph* leafGraph = netlist.findGraph("nested_leaf");

    if (!topGraph || !midGraph || !leafGraph) {
        return fail("nested", "Missing expected graphs for nested hierarchy");
    }

    bool foundMidInstance = false;
    for (const std::unique_ptr<grh::Operation>& opPtr : midGraph->operations()) {
        const grh::Operation& op = *opPtr;
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }
        foundMidInstance = true;

        const auto& attrs = op.attributes();
        auto moduleNameIt = attrs.find("moduleName");
        if (moduleNameIt == attrs.end()) {
            return fail("nested", "Instance operation missing moduleName attribute");
        }
        const std::string& moduleName = std::get<std::string>(moduleNameIt->second);
        if (moduleName != leafGraph->name()) {
            return fail("nested", "Instance moduleName mismatch: " + moduleName);
        }

        if (op.operands().size() != 1 || op.results().size() != 1) {
            return fail("nested", "Instance operand/result count mismatch");
        }

        const grh::Value* inputValue = op.operands().front();
        const grh::Value* outputValue = op.results().front();
        if (!inputValue || inputValue->symbol() != "mid_in") {
            return fail("nested", "Instance input wiring incorrect");
        }
        if (!outputValue || outputValue->symbol() != "mid_out") {
            return fail("nested", "Instance output wiring incorrect");
        }

        auto inputNamesIt = attrs.find("inputPortName");
        auto outputNamesIt = attrs.find("outputPortName");
        if (inputNamesIt == attrs.end() || outputNamesIt == attrs.end()) {
            return fail("nested", "Instance missing port name attributes");
        }

        const auto& inputNames = std::get<std::vector<std::string>>(inputNamesIt->second);
        const auto& outputNames = std::get<std::vector<std::string>>(outputNamesIt->second);
        if (inputNames.size() != 1 || inputNames.front() != "leaf_in") {
            return fail("nested", "Input port name attribute mismatch");
        }
        if (outputNames.size() != 1 || outputNames.front() != "leaf_out") {
            return fail("nested", "Output port name attribute mismatch");
        }
    }

    if (!foundMidInstance) {
        return fail("nested", "Failed to locate kInstance operation in mid graph");
    }

    // Ensure the top graph references the mid graph via instance.
    bool foundTopInstance = false;
    for (const std::unique_ptr<grh::Operation>& opPtr : topGraph->operations()) {
        if (opPtr->kind() == grh::OperationKind::kInstance) {
            foundTopInstance = true;
            break;
        }
    }
    if (!foundTopInstance) {
        return fail("nested", "Top graph missing instance to mid graph");
    }

    return 0;
}

int validateParameterized(const grh::Netlist& netlist) {
    if (netlist.topGraphs().size() != 1) {
        return fail("param", "Unexpected number of top graphs");
    }

    const std::string& topName = netlist.topGraphs().front();
    if (topName.rfind("p_top", 0) != 0) {
        return fail("param", "Unexpected top graph layout");
    }

    const grh::Graph* topGraph = netlist.findGraph(topName);
    const grh::Graph* leafGraph = netlist.findGraph("p_leaf$WIDTH_4");
    if (!topGraph || !leafGraph) {
        return fail("param", "Missing expected graphs for parameterized hierarchy");
    }

    bool foundInstance = false;
    for (const std::unique_ptr<grh::Operation>& opPtr : topGraph->operations()) {
        const grh::Operation& op = *opPtr;
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }
        foundInstance = true;

        const auto& attrs = op.attributes();
        auto moduleNameIt = attrs.find("moduleName");
        if (moduleNameIt == attrs.end()) {
            return fail("param", "Instance missing moduleName attribute");
        }
        const std::string& moduleName = std::get<std::string>(moduleNameIt->second);
        if (moduleName != leafGraph->name()) {
            return fail("param", "Instance moduleName mismatch: " + moduleName);
        }

        if (op.operands().size() != 1 || op.results().size() != 1) {
            return fail("param", "Unexpected operand/result count for parameterized instance");
        }

        const grh::Value* operand = op.operands().front();
        const grh::Value* result = op.results().front();
        if (!operand || operand->symbol() != "top_in") {
            return fail("param", "Instance input wiring incorrect");
        }
        if (!result || result->symbol() != "top_out") {
            return fail("param", "Instance output wiring incorrect");
        }

        auto inputNamesIt = attrs.find("inputPortName");
        auto outputNamesIt = attrs.find("outputPortName");
        if (inputNamesIt == attrs.end() || outputNamesIt == attrs.end()) {
            return fail("param", "Missing port name attributes on parameterized instance");
        }

        const auto& inputNames = std::get<std::vector<std::string>>(inputNamesIt->second);
        const auto& outputNames = std::get<std::vector<std::string>>(outputNamesIt->second);
        if (inputNames.size() != 1 || inputNames.front() != "leaf_in") {
            return fail("param", "Input port name mismatch on parameterized instance");
        }
        if (outputNames.size() != 1 || outputNames.front() != "leaf_out") {
            return fail("param", "Output port name mismatch on parameterized instance");
        }

        break;
    }

    if (!foundInstance) {
        return fail("param", "Failed to locate parameterized kInstance operation");
    }

    // Validate the leaf graph's port widths respect parameter defaults.
    const auto leafInputIt = leafGraph->inputPorts().find("leaf_in");
    const auto leafOutputIt = leafGraph->outputPorts().find("leaf_out");
    if (leafInputIt == leafGraph->inputPorts().end() ||
        leafOutputIt == leafGraph->outputPorts().end()) {
        return fail("param", "Leaf graph missing expected ports");
    }
    if (leafInputIt->second->width() != 4 || leafOutputIt->second->width() != 4) {
        return fail("param", "Leaf port widths do not match expected parameterization");
    }

    return 0;
}

int validateStructArray(const grh::Netlist& netlist) {
    if (netlist.topGraphs().size() != 1 || netlist.topGraphs().front() != "struct_top") {
        return fail("struct", "Unexpected top graph layout");
    }

    const grh::Graph* topGraph = netlist.findGraph("struct_top");
    const grh::Graph* leafGraph = netlist.findGraph("struct_leaf");
    if (!topGraph || !leafGraph) {
        return fail("struct", "Missing graphs for struct/array hierarchy");
    }

    auto checkPort = [&](const grh::Graph* graph, std::string_view name, int64_t expectedWidth,
                         bool isInput) -> bool {
        const auto& ports = isInput ? graph->inputPorts() : graph->outputPorts();
        auto it = ports.find(std::string(name));
        if (it == ports.end()) {
            return false;
        }
        const grh::Value* value = it->second;
        return value && value->width() == expectedWidth;
    };

    if (!checkPort(topGraph, "top_struct_in", 6, true) ||
        !checkPort(topGraph, "top_struct_out", 6, false)) {
        return fail("struct", "Packed struct port widths incorrect");
    }
    if (!checkPort(topGraph, "top_arr_in", 8, true) ||
        !checkPort(topGraph, "top_arr_out", 8, false)) {
        return fail("struct", "Packed array port widths incorrect");
    }

    bool foundInstance = false;
    for (const std::unique_ptr<grh::Operation>& opPtr : topGraph->operations()) {
        const grh::Operation& op = *opPtr;
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }
        foundInstance = true;

        if (op.operands().size() != 2 || op.results().size() != 2) {
            return fail("struct", "Instance port fan-in/out mismatch");
        }

        auto moduleNameIt = op.attributes().find("moduleName");
        if (moduleNameIt == op.attributes().end()) {
            return fail("struct", "Missing moduleName attribute on struct instance");
        }
        if (std::get<std::string>(moduleNameIt->second) != leafGraph->name()) {
            return fail("struct", "moduleName on struct instance mismatches target graph");
        }

        auto inputNamesIt = op.attributes().find("inputPortName");
        auto outputNamesIt = op.attributes().find("outputPortName");
        if (inputNamesIt == op.attributes().end() || outputNamesIt == op.attributes().end()) {
            return fail("struct", "Missing port name attributes on struct instance");
        }
        const auto inputNames = std::get<std::vector<std::string>>(inputNamesIt->second);
        const auto outputNames = std::get<std::vector<std::string>>(outputNamesIt->second);
        if (inputNames.size() != 2 || outputNames.size() != 2) {
            return fail("struct", "Port name attribute counts mismatch");
        }
    }

    if (!foundInstance) {
        return fail("struct", "Top graph missing struct instance");
    }

    if (!checkPort(leafGraph, "s_in", 6, true) || !checkPort(leafGraph, "s_out", 6, false) ||
        !checkPort(leafGraph, "arr_in", 8, true) || !checkPort(leafGraph, "arr_out", 8, false)) {
        return fail("struct", "Leaf port widths incorrect");
    }

    return 0;
}

int validateParamGenerate(const grh::Netlist& netlist) {
    if (netlist.topGraphs().size() != 1 || netlist.topGraphs().front() != "pg_top") {
        return fail("param_generate", "Unexpected top graph layout");
    }

    if (netlist.graphs().size() != 3) {
        return fail("param_generate", "Expected exactly three graphs (top + two leaf specializations)");
    }

    const grh::Graph* topGraph = netlist.findGraph("pg_top");
    const grh::Graph* leaf4Graph = netlist.findGraph("pg_leaf$WIDTH_4");
    const grh::Graph* leaf8Graph = netlist.findGraph("pg_leaf$WIDTH_8");
    if (!topGraph || !leaf4Graph || !leaf8Graph) {
        return fail("param_generate", "Missing expected graphs for parameterized generate test");
    }

    auto checkPortWidth = [](const grh::Graph* graph, int64_t expected) {
        auto inputIt = graph->inputPorts().find("in");
        auto outputIt = graph->outputPorts().find("out");
        if (inputIt == graph->inputPorts().end() || outputIt == graph->outputPorts().end()) {
            return false;
        }
        const grh::Value* inputValue = inputIt->second;
        const grh::Value* outputValue = outputIt->second;
        return inputValue && outputValue && inputValue->width() == expected &&
               outputValue->width() == expected;
    };

    if (!checkPortWidth(leaf4Graph, 4)) {
        return fail("param_generate", "pg_leaf ports do not match WIDTH=4 expectation");
    }
    if (!checkPortWidth(leaf8Graph, 8)) {
        return fail("param_generate", "pg_leaf_1 ports do not match WIDTH=8 expectation");
    }

    std::size_t totalInstances = 0;
    std::size_t width4Instances = 0;
    std::size_t width8Instances = 0;
    for (const std::unique_ptr<grh::Operation>& opPtr : topGraph->operations()) {
        const grh::Operation& op = *opPtr;
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }

        totalInstances++;
        auto moduleNameIt = op.attributes().find("moduleName");
        if (moduleNameIt == op.attributes().end()) {
            return fail("param_generate", "Instance missing moduleName attribute");
        }

        const std::string& moduleName = std::get<std::string>(moduleNameIt->second);
        if (moduleName == leaf4Graph->name()) {
            width4Instances++;
        }
        else if (moduleName == leaf8Graph->name()) {
            width8Instances++;
        }
        else {
            return fail("param_generate", "Instance references unexpected module graph: " + moduleName);
        }
    }

    if (totalInstances != 6) {
        return fail("param_generate", "Unexpected number of instances in top graph");
    }
    if (width4Instances != 5 || width8Instances != 1) {
        return fail("param_generate", "Instance distribution across parameterizations is incorrect");
    }

    return 0;
}

} // namespace

int main() {
    const std::filesystem::path dataDir = std::filesystem::path(WOLF_SV_ELAB_DATA_DIR);

    {
        grh::Netlist netlist;
        ElaborateDiagnostics diagnostics;
        if (!buildNetlist("nested", dataDir / "hierarchy_nested.sv", netlist, diagnostics)) {
            return 1;
        }
        if (!writeArtifact("nested", netlist)) {
            return 1;
        }
        if (int result = validateNested(netlist); result != 0) {
            return result;
        }
    }

    {
        grh::Netlist netlist;
        ElaborateDiagnostics diagnostics;
        if (!buildNetlist("param", dataDir / "param_instance.sv", netlist, diagnostics)) {
            return 1;
        }
        if (!writeArtifact("param", netlist)) {
            return 1;
        }
        if (int result = validateParameterized(netlist); result != 0) {
            return result;
        }
    }

    {
        grh::Netlist netlist;
        ElaborateDiagnostics diagnostics;
        if (!buildNetlist("struct", dataDir / "struct_array.sv", netlist, diagnostics)) {
            return 1;
        }
        if (!writeArtifact("struct", netlist)) {
            return 1;
        }
        if (int result = validateStructArray(netlist); result != 0) {
            return result;
        }
    }

    {
        grh::Netlist netlist;
        ElaborateDiagnostics diagnostics;
        if (!buildNetlist("param_generate", dataDir / "param_generate.sv", netlist, diagnostics)) {
            return 1;
        }
        if (!writeArtifact("param_generate", netlist)) {
            return 1;
        }
        if (int result = validateParamGenerate(netlist); result != 0) {
            return result;
        }
    }

    return 0;
}
