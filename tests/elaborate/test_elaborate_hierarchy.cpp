#include "elaborate.hpp"
#include "emit.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;
using namespace wolf_sv::emit;

namespace {

int fail(const std::string& context, const std::string& message) {
    std::cerr << "[elaborate_hierarchy:" << context << "] " << message << '\n';
    return 1;
}

const std::filesystem::path kArtifactDir = std::filesystem::path(WOLF_SV_ELAB_ARTIFACT_DIR);

std::optional<std::string> getStringAttr(const grh::Graph* graph, const grh::Operation& op,
                                         std::string_view key) {
    const grh::ir::SymbolId keyId = graph->lookupSymbol(key);
    if (!keyId.valid()) {
        return std::nullopt;
    }
    auto attr = op.attr(keyId);
    const std::string* value = attr ? std::get_if<std::string>(&*attr) : nullptr;
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

const std::vector<std::string>* getStringVecAttr(const grh::Graph* graph, const grh::Operation& op,
                                                 std::string_view key) {
    const grh::ir::SymbolId keyId = graph->lookupSymbol(key);
    if (!keyId.valid()) {
        return nullptr;
    }
    auto attr = op.attr(keyId);
    return attr ? std::get_if<std::vector<std::string>>(&*attr) : nullptr;
}

ValueId findPortValue(const grh::Graph* graph, std::string_view name, bool isInput) {
    const auto ports = isInput ? graph->inputPorts() : graph->outputPorts();
    for (const auto& port : ports) {
        if (graph->symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
}

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
    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    auto jsonOpt = emitter.emitToString(netlist, options);
    if (!jsonOpt || diagnostics.hasError()) {
        std::cerr << "[elaborate_hierarchy:" << context << "] Failed to emit JSON artifact\n";
        return false;
    }
    os << *jsonOpt;
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
    for (const auto& opId : midGraph->operations()) {
        const grh::Operation op = midGraph->getOperation(opId);
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }
        foundMidInstance = true;

        auto moduleNameOpt = getStringAttr(midGraph, op, "moduleName");
        if (!moduleNameOpt) {
            return fail("nested", "Instance operation missing moduleName attribute");
        }
        if (*moduleNameOpt != leafGraph->symbol()) {
            return fail("nested", "Instance moduleName mismatch: " + *moduleNameOpt);
        }

        if (op.operands().size() != 1 || op.results().size() != 1) {
            return fail("nested", "Instance operand/result count mismatch");
        }

        ValueId inputValue = op.operands().front();
        ValueId outputValue = op.results().front();
        if (!inputValue || midGraph->getValue(inputValue).symbolText() != "mid_in") {
            return fail("nested", "Instance input wiring incorrect");
        }
        if (!outputValue || midGraph->getValue(outputValue).symbolText() != "mid_out") {
            return fail("nested", "Instance output wiring incorrect");
        }

        const auto* inputNames = getStringVecAttr(midGraph, op, "inputPortName");
        const auto* outputNames = getStringVecAttr(midGraph, op, "outputPortName");
        if (!inputNames || !outputNames) {
            return fail("nested", "Instance missing port name attributes");
        }

        if (inputNames->size() != 1 || inputNames->front() != "leaf_in") {
            return fail("nested", "Input port name attribute mismatch");
        }
        if (outputNames->size() != 1 || outputNames->front() != "leaf_out") {
            return fail("nested", "Output port name attribute mismatch");
        }
    }

    if (!foundMidInstance) {
        return fail("nested", "Failed to locate kInstance operation in mid graph");
    }

    // Ensure the top graph references the mid graph via instance.
    bool foundTopInstance = false;
    for (const auto& opId : topGraph->operations()) {
        if (topGraph->getOperation(opId).kind() == grh::OperationKind::kInstance) {
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
    for (const auto& opId : topGraph->operations()) {
        const grh::Operation op = topGraph->getOperation(opId);
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }
        foundInstance = true;

        auto moduleNameOpt = getStringAttr(topGraph, op, "moduleName");
        if (!moduleNameOpt) {
            return fail("param", "Instance missing moduleName attribute");
        }
        if (*moduleNameOpt != leafGraph->symbol()) {
            return fail("param", "Instance moduleName mismatch: " + *moduleNameOpt);
        }

        if (op.operands().size() != 1 || op.results().size() != 1) {
            return fail("param", "Unexpected operand/result count for parameterized instance");
        }

        ValueId operand = op.operands().front();
        ValueId result = op.results().front();
        if (!operand || topGraph->getValue(operand).symbolText() != "top_in") {
            return fail("param", "Instance input wiring incorrect");
        }
        if (!result || topGraph->getValue(result).symbolText() != "top_out") {
            return fail("param", "Instance output wiring incorrect");
        }

        const auto* inputNames = getStringVecAttr(topGraph, op, "inputPortName");
        const auto* outputNames = getStringVecAttr(topGraph, op, "outputPortName");
        if (!inputNames || !outputNames) {
            return fail("param", "Missing port name attributes on parameterized instance");
        }

        if (inputNames->size() != 1 || inputNames->front() != "leaf_in") {
            return fail("param", "Input port name mismatch on parameterized instance");
        }
        if (outputNames->size() != 1 || outputNames->front() != "leaf_out") {
            return fail("param", "Output port name mismatch on parameterized instance");
        }

        break;
    }

    if (!foundInstance) {
        return fail("param", "Failed to locate parameterized kInstance operation");
    }

    // Validate the leaf graph's port widths respect parameter defaults.
    const ValueId leafInVal = findPortValue(leafGraph, "leaf_in", true);
    const ValueId leafOutVal = findPortValue(leafGraph, "leaf_out", false);
    if (!leafInVal || !leafOutVal) {
        return fail("param", "Leaf graph missing expected ports");
    }
    if (leafGraph->getValue(leafInVal).width() != 4 ||
        leafGraph->getValue(leafOutVal).width() != 4) {
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
        ValueId value = findPortValue(graph, name, isInput);
        return value && graph->getValue(value).width() == expectedWidth;
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
    for (const auto& opId : topGraph->operations()) {
        const grh::Operation op = topGraph->getOperation(opId);
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }
        foundInstance = true;

        if (op.operands().size() != 2 || op.results().size() != 2) {
            return fail("struct", "Instance port fan-in/out mismatch");
        }

        auto moduleNameOpt = getStringAttr(topGraph, op, "moduleName");
        if (!moduleNameOpt) {
            return fail("struct", "Missing moduleName attribute on struct instance");
        }
        if (*moduleNameOpt != leafGraph->symbol()) {
            return fail("struct", "moduleName on struct instance mismatches target graph");
        }

        const auto* inputNames = getStringVecAttr(topGraph, op, "inputPortName");
        const auto* outputNames = getStringVecAttr(topGraph, op, "outputPortName");
        if (!inputNames || !outputNames) {
            return fail("struct", "Missing port name attributes on struct instance");
        }
        if (inputNames->size() != 2 || outputNames->size() != 2) {
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
        ValueId inputValue = findPortValue(graph, "in", true);
        ValueId outputValue = findPortValue(graph, "out", false);
        return inputValue && outputValue && graph->getValue(inputValue).width() == expected &&
               graph->getValue(outputValue).width() == expected;
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
    for (const auto& opId : topGraph->operations()) {
        const grh::Operation op = topGraph->getOperation(opId);
        if (op.kind() != grh::OperationKind::kInstance) {
            continue;
        }

        totalInstances++;
        auto moduleNameOpt = getStringAttr(topGraph, op, "moduleName");
        if (!moduleNameOpt) {
            return fail("param_generate", "Instance missing moduleName attribute");
        }

        if (*moduleNameOpt == leaf4Graph->symbol()) {
            width4Instances++;
        }
        else if (*moduleNameOpt == leaf8Graph->symbol()) {
            width8Instances++;
        }
        else {
            return fail("param_generate",
                        "Instance references unexpected module graph: " + *moduleNameOpt);
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
