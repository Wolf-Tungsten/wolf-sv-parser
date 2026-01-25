#include "elaborate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
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
    std::cerr << "[elaborate_dpic] " << message << '\n';
    return 1;
}

std::optional<std::string> getStringAttr(const grh::ir::Graph& graph, const grh::ir::Operation& op,
                                         std::string_view key) {
    (void)graph;
    auto attr = op.attr(key);
    const std::string* value = attr ? std::get_if<std::string>(&*attr) : nullptr;
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

std::optional<std::vector<std::string>> getStringVecAttr(const grh::ir::Graph& graph,
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

std::optional<std::vector<int64_t>> getIntVecAttr(const grh::ir::Graph& graph,
                                                  const grh::ir::Operation& op,
                                                  std::string_view key) {
    (void)graph;
    auto attr = op.attr(key);
    const auto* value = attr ? std::get_if<std::vector<int64_t>>(&*attr) : nullptr;
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

OperationId findOpByKind(const grh::ir::Graph& graph, grh::ir::OperationKind kind) {
    for (const auto& opId : graph.operations()) {
        const grh::ir::Operation op = graph.getOperation(opId);
        if (op.kind() == kind) {
            return opId;
        }
    }
    return OperationId::invalid();
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

ValueId findValueByName(const grh::ir::Graph& graph, std::string_view name) {
    return graph.findValue(name);
}

const SignalMemoEntry* findEntry(std::span<const SignalMemoEntry> memo, std::string_view name) {
    for (const SignalMemoEntry& entry : memo) {
        if (entry.symbol && entry.symbol->name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

#ifndef WOLF_SV_ELAB_DPIC_DATA_PATH
#error "WOLF_SV_ELAB_DPIC_DATA_PATH must be defined"
#endif

int main() {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    const std::filesystem::path sourcePath(WOLF_SV_ELAB_DPIC_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing dpic testcase file: " + sourcePath.string());
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-dpic");
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
        bool unexpected = false;
        for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
            const bool ignorable =
                diag.message.find("Module body elaboration pending") != std::string::npos ||
                diag.message.find("Assign LHS is not a memoized signal") != std::string::npos ||
                diag.message.find("Unsupported RHS named value") != std::string::npos ||
                diag.message.find("Unable to derive flatten path") != std::string::npos;
            if (ignorable)
                continue;
            unexpected = true;
            std::cerr << "[elaborate_dpic] diag: " << diag.message;
            if (!diag.originSymbol.empty()) {
                std::cerr << " (" << diag.originSymbol << ")";
            }
            std::cerr << '\n';
        }
        if (unexpected) {
            return fail("Unexpected diagnostics while elaborating dpic_stage24");
        }
    }

    const slang::ast::InstanceSymbol* topInstance = nullptr;
    for (const slang::ast::InstanceSymbol* inst : compilation->getRoot().topInstances) {
        if (inst && inst->name == "dpic_stage24") {
            topInstance = inst;
            break;
        }
    }
    if (!topInstance) {
        return fail("Top instance dpic_stage24 not found");
    }

    grh::ir::Graph* graph = netlist.findGraph("dpic_stage24");
    if (!graph) {
        return fail("GRH graph dpic_stage24 not found");
    }

    const slang::ast::InstanceBodySymbol* canonicalBody = topInstance->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body =
        canonicalBody ? *canonicalBody : topInstance->body;

    std::span<const DpiImportEntry> dpiImports = elaborator.peekDpiImports(body);
    if (dpiImports.size() != 1) {
        return fail("Expected exactly one DPI import entry");
    }

    OperationId importOpId = findOpByKind(*graph, grh::ir::OperationKind::kDpicImport);
    if (!importOpId) {
        return fail("kDpicImport operation missing");
    }
    const grh::ir::Operation importOp = graph->getOperation(importOpId);

    auto directions = getStringVecAttr(*graph, importOp, "argsDirection");
    if (!directions) {
        return fail("kDpicImport missing argsDirection attribute");
    }
    if (*directions != std::vector<std::string>{"input", "input", "output"}) {
        return fail("kDpicImport argsDirection mismatch");
    }

    auto widths = getIntVecAttr(*graph, importOp, "argsWidth");
    if (!widths) {
        return fail("kDpicImport missing argsWidth attribute");
    }
    if (*widths != std::vector<int64_t>{16, 8, 16}) {
        return fail("kDpicImport argsWidth mismatch");
    }

    auto names = getStringVecAttr(*graph, importOp, "argsName");
    if (!names) {
        return fail("kDpicImport missing argsName attribute");
    }
    if (*names != std::vector<std::string>{"lhs_vec", "rhs_scalar", "result_vec"}) {
        return fail("kDpicImport argsName mismatch");
    }

    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(body);

    OperationId callOpId = findOpByKind(*graph, grh::ir::OperationKind::kDpicCall);
    if (!callOpId) {
        std::cerr << "[elaborate_dpic] Existing operations:\n";
        for (const auto& opId : graph->operations()) {
            const grh::ir::Operation op = graph->getOperation(opId);
            std::cerr << "  - " << std::string(grh::ir::toString(op.kind())) << '\n';
        }
        std::cerr << "[elaborate_dpic] Reg memo entries: " << regMemo.size() << '\n';
        if (regMemo.empty()) {
            std::cerr << "[elaborate_dpic] Body members:\n";
            for (const slang::ast::Symbol& member : body.members()) {
                std::cerr << "  - kind=" << static_cast<int>(member.kind);
                if (!member.name.empty()) {
                    std::cerr << " name=" << member.name;
                }
                std::cerr << '\n';
            }
        }
        return fail("kDpicCall operation missing");
    }
    const grh::ir::Operation callOp = graph->getOperation(callOpId);
    if (callOp.operands().size() != 4) {
        return fail("kDpicCall operand count mismatch");
    }
    if (callOp.results().size() != 1) {
        return fail("kDpicCall result count mismatch");
    }

    ValueId clkPort = findPort(*graph, "clk", /*isInput=*/true);
    ValueId enPort = findPort(*graph, "en", /*isInput=*/true);
    ValueId lhsPort = findPort(*graph, "lhs_vec", /*isInput=*/true);
    ValueId rhsPort = findPort(*graph, "rhs_scalar", /*isInput=*/true);
    if (!clkPort || !enPort || !lhsPort || !rhsPort) {
        return fail("Module ports missing");
    }
    if (callOp.operands()[0] != clkPort || callOp.operands()[1] != enPort ||
        callOp.operands()[2] != lhsPort || callOp.operands()[3] != rhsPort) {
        return fail("kDpicCall operand wiring mismatch");
    }

    auto inNames = getStringVecAttr(*graph, callOp, "inArgName");
    if (!inNames) {
        return fail("kDpicCall missing inArgName attribute");
    }
    if (*inNames != std::vector<std::string>{"lhs_vec", "rhs_scalar"}) {
        return fail("kDpicCall inArgName mismatch");
    }
    auto outNames = getStringVecAttr(*graph, callOp, "outArgName");
    if (!outNames) {
        return fail("kDpicCall missing outArgName attribute");
    }
    if (*outNames != std::vector<std::string>{"result_vec"}) {
        return fail("kDpicCall outArgName mismatch");
    }

    auto targetSymbolOpt = getStringAttr(*graph, callOp, "targetImportSymbol");
    if (!targetSymbolOpt) {
        return fail("kDpicCall missing targetImportSymbol attribute");
    }
    if (*targetSymbolOpt != std::string(importOp.symbolText())) {
        return fail("kDpicCall targetImportSymbol does not reference kDpicImport");
    }

    const SignalMemoEntry* sumEntry = findEntry(regMemo, "sum_storage");
    if (!sumEntry || !sumEntry->stateOp) {
        return fail("sum_storage memo/state op missing");
    }
    const grh::ir::Operation sumOp = graph->getOperation(sumEntry->stateOp);
    if (sumOp.operands().size() < 2) {
        return fail("sum state op missing data operand");
    }
    ValueId callResult = callOp.results().front();
    ValueId dataOperand = sumOp.operands().back();
    bool connectsToCall = dataOperand == callResult;
    if (!connectsToCall && dataOperand) {
        OperationId dataOpId = graph->getValue(dataOperand).definingOp();
        if (dataOpId && graph->getOperation(dataOpId).kind() == grh::ir::OperationKind::kMux) {
            for (ValueId operand : graph->getOperation(dataOpId).operands()) {
                if (operand == callResult) {
                    connectsToCall = true;
                    break;
                }
            }
        }
    }
    if (!connectsToCall) {
        return fail("sum data operand is not driven by kDpicCall result");
    }

    const slang::ast::InstanceSymbol* inoutInstance = nullptr;
    for (const slang::ast::InstanceSymbol* inst : compilation->getRoot().topInstances) {
        if (inst && inst->name == "dpic_inout_case") {
            inoutInstance = inst;
            break;
        }
    }
    if (!inoutInstance) {
        return fail("Top instance dpic_inout_case not found");
    }

    grh::ir::Graph* inoutGraph = netlist.findGraph("dpic_inout_case");
    if (!inoutGraph) {
        return fail("GRH graph dpic_inout_case not found");
    }

    const slang::ast::InstanceBodySymbol* inoutCanonical = inoutInstance->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& inoutBody =
        inoutCanonical ? *inoutCanonical : inoutInstance->body;

    std::span<const DpiImportEntry> inoutImports = elaborator.peekDpiImports(inoutBody);
    if (inoutImports.size() != 1) {
        return fail("Expected exactly one DPI import entry in dpic_inout_case");
    }

    OperationId inoutImportOpId = findOpByKind(*inoutGraph, grh::ir::OperationKind::kDpicImport);
    if (!inoutImportOpId) {
        return fail("dpic_inout_case kDpicImport operation missing");
    }
    const grh::ir::Operation inoutImportOp = inoutGraph->getOperation(inoutImportOpId);

    auto inoutDirections = getStringVecAttr(*inoutGraph, inoutImportOp, "argsDirection");
    if (!inoutDirections || *inoutDirections != std::vector<std::string>{"input", "inout"}) {
        return fail("dpic_inout_case argsDirection mismatch");
    }
    auto inoutWidths = getIntVecAttr(*inoutGraph, inoutImportOp, "argsWidth");
    if (!inoutWidths || *inoutWidths != std::vector<int64_t>{8, 8}) {
        return fail("dpic_inout_case argsWidth mismatch");
    }
    auto inoutNames = getStringVecAttr(*inoutGraph, inoutImportOp, "argsName");
    if (!inoutNames || *inoutNames != std::vector<std::string>{"seed", "accum"}) {
        return fail("dpic_inout_case argsName mismatch");
    }

    OperationId inoutCallOpId = findOpByKind(*inoutGraph, grh::ir::OperationKind::kDpicCall);
    if (!inoutCallOpId) {
        return fail("dpic_inout_case kDpicCall operation missing");
    }
    const grh::ir::Operation inoutCallOp = inoutGraph->getOperation(inoutCallOpId);
    if (inoutCallOp.operands().size() != 4) {
        return fail("dpic_inout_case kDpicCall operand count mismatch");
    }
    if (inoutCallOp.results().size() != 1) {
        return fail("dpic_inout_case kDpicCall result count mismatch");
    }

    ValueId inoutClk = findPort(*inoutGraph, "clk", /*isInput=*/true);
    ValueId inoutEn = findPort(*inoutGraph, "en", /*isInput=*/true);
    ValueId inoutSeed = findPort(*inoutGraph, "seed", /*isInput=*/true);
    ValueId accumValue = findValueByName(*inoutGraph, "accum");
    if (!inoutClk || !inoutEn || !inoutSeed || !accumValue) {
        return fail("dpic_inout_case module ports missing");
    }
    if (inoutCallOp.operands()[0] != inoutClk ||
        inoutCallOp.operands()[1] != inoutEn ||
        inoutCallOp.operands()[2] != inoutSeed ||
        inoutCallOp.operands()[3] != accumValue) {
        return fail("dpic_inout_case kDpicCall operand wiring mismatch");
    }

    auto inoutInNames = getStringVecAttr(*inoutGraph, inoutCallOp, "inArgName");
    if (!inoutInNames || *inoutInNames != std::vector<std::string>{"seed"}) {
        return fail("dpic_inout_case inArgName mismatch");
    }
    auto inoutOutNames = getStringVecAttr(*inoutGraph, inoutCallOp, "outArgName");
    if (!inoutOutNames || !inoutOutNames->empty()) {
        return fail("dpic_inout_case outArgName mismatch");
    }
    auto inoutInoutNames = getStringVecAttr(*inoutGraph, inoutCallOp, "inoutArgName");
    if (!inoutInoutNames || *inoutInoutNames != std::vector<std::string>{"accum"}) {
        return fail("dpic_inout_case inoutArgName mismatch");
    }

    std::span<const SignalMemoEntry> inoutRegMemo = elaborator.peekRegMemo(inoutBody);
    const SignalMemoEntry* accumEntry = findEntry(inoutRegMemo, "accum");
    if (!accumEntry || !accumEntry->stateOp) {
        return fail("dpic_inout_case accum memo/state op missing");
    }
    const grh::ir::Operation accumOp = inoutGraph->getOperation(accumEntry->stateOp);
    if (accumOp.operands().size() < 2) {
        return fail("dpic_inout_case accum state op missing data operand");
    }
    ValueId inoutCallResult = inoutCallOp.results().front();
    ValueId accumData = accumOp.operands().back();
    bool accumConnects = accumData == inoutCallResult;
    if (!accumConnects && accumData) {
        OperationId dataOpId = inoutGraph->getValue(accumData).definingOp();
        if (dataOpId && inoutGraph->getOperation(dataOpId).kind() == grh::ir::OperationKind::kMux) {
            for (ValueId operand : inoutGraph->getOperation(dataOpId).operands()) {
                if (operand == inoutCallResult) {
                    accumConnects = true;
                    break;
                }
            }
        }
    }
    if (!accumConnects) {
        return fail("dpic_inout_case accum data operand is not driven by kDpicCall result");
    }

    return 0;
}
