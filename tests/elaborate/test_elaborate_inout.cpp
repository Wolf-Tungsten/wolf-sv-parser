#include "elaborate.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv_parser;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_inout] " << message << '\n';
    return 1;
}

ValueId findPort(const grh::ir::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
}

const grh::ir::InoutPort* findInoutPort(const grh::ir::Graph& graph,
                                        std::string_view name) {
    for (const auto& port : graph.inoutPorts()) {
        if (graph.symbolText(port.name) == name) {
            return &port;
        }
    }
    return nullptr;
}

} // namespace

#ifndef WOLF_SV_ELAB_INOUT_DATA_PATH
#error "WOLF_SV_ELAB_INOUT_DATA_PATH must be defined"
#endif

int main() {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    const std::filesystem::path sourcePath(WOLF_SV_ELAB_INOUT_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing inout testcase file: " + sourcePath.string());
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-inout");
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
    ElaborateOptions elaborateOptions;
    elaborateOptions.abortOnError = false;
    Elaborate elaborator(&diagnostics, elaborateOptions);
    grh::ir::Netlist netlist = elaborator.convert(compilation->getRoot());
    if (!diagnostics.empty()) {
        return fail("Unexpected diagnostics while elaborating inout_case");
    }

    const slang::ast::InstanceSymbol* topInstance = nullptr;
    for (const slang::ast::InstanceSymbol* inst : compilation->getRoot().topInstances) {
        if (inst && inst->name == "inout_case") {
            topInstance = inst;
            break;
        }
    }
    if (!topInstance) {
        return fail("Top instance inout_case not found");
    }

    grh::ir::Graph* graph = netlist.findGraph("inout_case");
    if (!graph) {
        return fail("GRH graph inout_case not found");
    }

    if (!findPort(*graph, "en", /*isInput=*/true) ||
        !findPort(*graph, "data", /*isInput=*/true)) {
        return fail("Input ports en/data missing");
    }
    if (!findPort(*graph, "io_in", /*isInput=*/false)) {
        return fail("Output port io_in missing");
    }
    if (findPort(*graph, "io", /*isInput=*/true) ||
        findPort(*graph, "io", /*isInput=*/false)) {
        return fail("Inout port io should not appear in input/output lists");
    }

    const grh::ir::InoutPort* ioPort = findInoutPort(*graph, "io");
    if (!ioPort) {
        return fail("Inout port io missing");
    }

    const grh::ir::Value inValue = graph->getValue(ioPort->in);
    const grh::ir::Value outValue = graph->getValue(ioPort->out);
    const grh::ir::Value oeValue = graph->getValue(ioPort->oe);

    if (inValue.symbolText() != "io__in" ||
        outValue.symbolText() != "io__out" ||
        oeValue.symbolText() != "io__oe") {
        return fail("Inout value symbols do not follow __in/__out/__oe suffixes");
    }
    if (inValue.isInput() || inValue.isOutput() ||
        outValue.isInput() || outValue.isOutput() ||
        oeValue.isInput() || oeValue.isOutput()) {
        return fail("Inout port values must not be marked as input/output");
    }
    if (!inValue.isInout() || !outValue.isInout() || !oeValue.isInout()) {
        return fail("Inout port values must be marked as inout");
    }
    if (inValue.width() != 4 || outValue.width() != 4 || oeValue.width() != 4) {
        return fail("Inout port value widths do not match port width");
    }

    return 0;
}
