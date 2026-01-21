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

using namespace wolf_sv;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_dpic] " << message << '\n';
    return 1;
}

std::optional<std::string> getStringAttr(const grh::Graph& graph, const grh::Operation& op,
                                         std::string_view key) {
    const grh::ir::SymbolId keyId = graph.lookupSymbol(key);
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

const std::vector<std::string>* getStringVecAttr(const grh::Graph& graph,
                                                 const grh::Operation& op,
                                                 std::string_view key) {
    const grh::ir::SymbolId keyId = graph.lookupSymbol(key);
    if (!keyId.valid()) {
        return nullptr;
    }
    auto attr = op.attr(keyId);
    return attr ? std::get_if<std::vector<std::string>>(&*attr) : nullptr;
}

const std::vector<int64_t>* getIntVecAttr(const grh::Graph& graph, const grh::Operation& op,
                                          std::string_view key) {
    const grh::ir::SymbolId keyId = graph.lookupSymbol(key);
    if (!keyId.valid()) {
        return nullptr;
    }
    auto attr = op.attr(keyId);
    return attr ? std::get_if<std::vector<int64_t>>(&*attr) : nullptr;
}

OperationId findOpByKind(const grh::Graph& graph, grh::OperationKind kind) {
    for (const auto& opId : graph.operations()) {
        const grh::Operation op = graph.getOperation(opId);
        if (op.kind() == kind) {
            return opId;
        }
    }
    return OperationId::invalid();
}

ValueId findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
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
    Elaborate elaborator(&diagnostics);
    grh::Netlist netlist = elaborator.convert(compilation->getRoot());
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

    grh::Graph* graph = netlist.findGraph("dpic_stage24");
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

    OperationId importOpId = findOpByKind(*graph, grh::OperationKind::kDpicImport);
    if (!importOpId) {
        return fail("kDpicImport operation missing");
    }
    const grh::Operation importOp = graph->getOperation(importOpId);

    const auto* directions = getStringVecAttr(*graph, importOp, "argsDirection");
    if (!directions) {
        return fail("kDpicImport missing argsDirection attribute");
    }
    if (*directions != std::vector<std::string>{"input", "input", "output"}) {
        return fail("kDpicImport argsDirection mismatch");
    }

    const auto* widths = getIntVecAttr(*graph, importOp, "argsWidth");
    if (!widths) {
        return fail("kDpicImport missing argsWidth attribute");
    }
    if (*widths != std::vector<int64_t>{16, 8, 16}) {
        return fail("kDpicImport argsWidth mismatch");
    }

    const auto* names = getStringVecAttr(*graph, importOp, "argsName");
    if (!names) {
        return fail("kDpicImport missing argsName attribute");
    }
    if (*names != std::vector<std::string>{"lhs_vec", "rhs_scalar", "result_vec"}) {
        return fail("kDpicImport argsName mismatch");
    }

    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(body);

    OperationId callOpId = findOpByKind(*graph, grh::OperationKind::kDpicCall);
    if (!callOpId) {
        std::cerr << "[elaborate_dpic] Existing operations:\n";
        for (const auto& opId : graph->operations()) {
            const grh::Operation op = graph->getOperation(opId);
            std::cerr << "  - " << std::string(grh::toString(op.kind())) << '\n';
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
    const grh::Operation callOp = graph->getOperation(callOpId);
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

    const auto* inNames = getStringVecAttr(*graph, callOp, "inArgName");
    if (!inNames) {
        return fail("kDpicCall missing inArgName attribute");
    }
    if (*inNames != std::vector<std::string>{"lhs_vec", "rhs_scalar"}) {
        return fail("kDpicCall inArgName mismatch");
    }
    const auto* outNames = getStringVecAttr(*graph, callOp, "outArgName");
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
    const grh::Operation sumOp = graph->getOperation(sumEntry->stateOp);
    if (sumOp.operands().size() < 2) {
        return fail("sum state op missing data operand");
    }
    ValueId callResult = callOp.results().front();
    ValueId dataOperand = sumOp.operands().back();
    bool connectsToCall = dataOperand == callResult;
    if (!connectsToCall && dataOperand) {
        OperationId dataOpId = graph->getValue(dataOperand).definingOp();
        if (dataOpId && graph->getOperation(dataOpId).kind() == grh::OperationKind::kMux) {
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

    return 0;
}
