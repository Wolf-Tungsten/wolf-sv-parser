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

const grh::Operation* findOpByKind(const grh::Graph& graph, grh::OperationKind kind) {
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        if (op.kind() == kind) {
            return &op;
        }
    }
    return nullptr;
}

const grh::Value* findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    auto it = ports.find(std::string(name));
    if (it != ports.end()) {
        return graph.findValue(it->second);
    }
    return nullptr;
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

    const grh::Operation* importOp = findOpByKind(*graph, grh::OperationKind::kDpicImport);
    if (!importOp) {
        return fail("kDpicImport operation missing");
    }

    auto dirIt = importOp->attributes().find("argsDirection");
    if (dirIt == importOp->attributes().end()) {
        return fail("kDpicImport missing argsDirection attribute");
    }
    if (std::get<std::vector<std::string>>(dirIt->second) !=
        std::vector<std::string>{"input", "input", "output"}) {
        return fail("kDpicImport argsDirection mismatch");
    }

    auto widthIt = importOp->attributes().find("argsWidth");
    if (widthIt == importOp->attributes().end()) {
        return fail("kDpicImport missing argsWidth attribute");
    }
    if (std::get<std::vector<int64_t>>(widthIt->second) !=
        std::vector<int64_t>{16, 8, 16}) {
        return fail("kDpicImport argsWidth mismatch");
    }

    auto nameIt = importOp->attributes().find("argsName");
    if (nameIt == importOp->attributes().end()) {
        return fail("kDpicImport missing argsName attribute");
    }
    if (std::get<std::vector<std::string>>(nameIt->second) !=
        std::vector<std::string>{"lhs_vec", "rhs_scalar", "result_vec"}) {
        return fail("kDpicImport argsName mismatch");
    }

    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(body);

    const grh::Operation* callOp = findOpByKind(*graph, grh::OperationKind::kDpicCall);
    if (!callOp) {
        std::cerr << "[elaborate_dpic] Existing operations:\n";
        for (const auto& opSymbol : graph->operationOrder()) {
            const grh::Operation& op = graph->getOperation(opSymbol);
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
    if (callOp->operands().size() != 4) {
        return fail("kDpicCall operand count mismatch");
    }
    if (callOp->results().size() != 1) {
        return fail("kDpicCall result count mismatch");
    }

    const grh::Value* clkPort = findPort(*graph, "clk", /*isInput=*/true);
    const grh::Value* enPort = findPort(*graph, "en", /*isInput=*/true);
    const grh::Value* lhsPort = findPort(*graph, "lhs_vec", /*isInput=*/true);
    const grh::Value* rhsPort = findPort(*graph, "rhs_scalar", /*isInput=*/true);
    if (!clkPort || !enPort || !lhsPort || !rhsPort) {
        return fail("Module ports missing");
    }
    if (callOp->operands()[0] != clkPort || callOp->operands()[1] != enPort ||
        callOp->operands()[2] != lhsPort || callOp->operands()[3] != rhsPort) {
        return fail("kDpicCall operand wiring mismatch");
    }

    auto inNameIt = callOp->attributes().find("inArgName");
    if (inNameIt == callOp->attributes().end()) {
        return fail("kDpicCall missing inArgName attribute");
    }
    if (std::get<std::vector<std::string>>(inNameIt->second) !=
        std::vector<std::string>{"lhs_vec", "rhs_scalar"}) {
        return fail("kDpicCall inArgName mismatch");
    }
    auto outNameIt = callOp->attributes().find("outArgName");
    if (outNameIt == callOp->attributes().end()) {
        return fail("kDpicCall missing outArgName attribute");
    }
    if (std::get<std::vector<std::string>>(outNameIt->second) !=
        std::vector<std::string>{"result_vec"}) {
        return fail("kDpicCall outArgName mismatch");
    }

    auto targetIt = callOp->attributes().find("targetImportSymbol");
    if (targetIt == callOp->attributes().end()) {
        return fail("kDpicCall missing targetImportSymbol attribute");
    }
    if (!std::holds_alternative<std::string>(targetIt->second) ||
        std::get<std::string>(targetIt->second) != importOp->symbol()) {
        return fail("kDpicCall targetImportSymbol does not reference kDpicImport");
    }

    const SignalMemoEntry* sumEntry = findEntry(regMemo, "sum_storage");
    if (!sumEntry || !sumEntry->stateOp) {
        return fail("sum_storage memo/state op missing");
    }
    if (sumEntry->stateOp->operands().size() < 2) {
        return fail("sum state op missing data operand");
    }
    const grh::Value* callResult = callOp->results().front();
    const grh::Value* dataOperand = sumEntry->stateOp->operands().back();
    bool connectsToCall = dataOperand == callResult;
    if (!connectsToCall && dataOperand) {
        if (const grh::Operation* dataOp = dataOperand->definingOp();
            dataOp && dataOp->kind() == grh::OperationKind::kMux) {
            for (const grh::Value* operand : dataOp->operands()) {
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
