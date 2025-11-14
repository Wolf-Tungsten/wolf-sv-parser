#include "elaborate.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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
    std::cerr << "[elaborate_seq_always] " << message << '\n';
    return 1;
}

const SignalMemoEntry* findEntry(std::span<const SignalMemoEntry> memo, std::string_view name) {
    for (const SignalMemoEntry& entry : memo) {
        if (entry.symbol && entry.symbol->name == name) {
            return &entry;
        }
    }
    return nullptr;
}

const grh::Value* findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    auto it = ports.find(std::string(name));
    if (it != ports.end()) {
        return it->second;
    }
    return nullptr;
}

bool writeArtifact(const grh::Netlist& netlist) {
    const std::filesystem::path artifactPath(WOLF_SV_ELAB_SEQ_ALWAYS_ARTIFACT_PATH);
    if (artifactPath.empty()) {
        return true;
    }

    if (const std::filesystem::path dir = artifactPath.parent_path(); !dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(dir) &&
            !std::filesystem::create_directories(dir, ec) && ec) {
            std::cerr << "[elaborate_seq_always] Failed to create artifact dir: "
                      << ec.message() << '\n';
            return false;
        }
    }

    std::ofstream os(artifactPath, std::ios::trunc);
    if (!os.is_open()) {
        std::cerr << "[elaborate_seq_always] Failed to open artifact file: "
                  << artifactPath.string() << '\n';
        return false;
    }

    os << netlist.toJsonString(true);
    return true;
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

bool expectAttrs(const grh::Operation& op, std::string_view key, int64_t value) {
    auto it = op.attributes().find(std::string(key));
    if (it == op.attributes().end()) {
        return false;
    }
    if (!std::holds_alternative<int64_t>(it->second)) {
        return false;
    }
    return std::get<int64_t>(it->second) == value;
}

} // namespace

#ifndef WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH
#error "WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH must be defined"
#endif
#ifndef WOLF_SV_ELAB_SEQ_ALWAYS_ARTIFACT_PATH
#error "WOLF_SV_ELAB_SEQ_ALWAYS_ARTIFACT_PATH must be defined"
#endif

int main() {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    const std::filesystem::path sourcePath(WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing seq always testcase file: " + sourcePath.string());
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-seq-always");
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

    if (!writeArtifact(netlist)) {
        return fail("Failed to write seq always artifact");
    }

    const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage17");
    if (!inst) {
        return fail("Top instance seq_stage17 not found");
    }

    const grh::Graph* graph = netlist.findGraph("seq_stage17");
    if (!graph) {
        return fail("GRH graph seq_stage17 not found");
    }

    const grh::Value* clkPort = findPort(*graph, "clk", /*isInput=*/true);
    const grh::Value* rstPort = findPort(*graph, "rst_n", /*isInput=*/true);
    const grh::Value* rstSyncPort = findPort(*graph, "rst_sync", /*isInput=*/true);
    const grh::Value* loPort = findPort(*graph, "lo_data", /*isInput=*/true);
    const grh::Value* hiPort = findPort(*graph, "hi_data", /*isInput=*/true);
    if (!clkPort || !rstPort || !rstSyncPort || !loPort || !hiPort) {
        return fail("Missing clk/rst_n/rst_sync/lo_data/hi_data input ports");
    }

    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(fetchBody(*inst));
    if (regMemo.empty()) {
        return fail("Register memo is empty");
    }

    const SignalMemoEntry* regFull = findEntry(regMemo, "reg_full");
    const SignalMemoEntry* regPartial = findEntry(regMemo, "reg_partial");
    const SignalMemoEntry* regMulti = findEntry(regMemo, "reg_multi");
    const SignalMemoEntry* regAsync = findEntry(regMemo, "reg_async_rst");
    const SignalMemoEntry* regSync = findEntry(regMemo, "reg_sync_rst");
    if (!regFull || !regPartial || !regMulti || !regAsync || !regSync) {
        return fail("Missing register memo entries");
    }

    auto expectRegisterKind = [&](const SignalMemoEntry& entry, grh::OperationKind kind,
                                  std::size_t operandCount) -> bool {
        if (!entry.stateOp || entry.stateOp->kind() != kind) {
            fail("Register state operation missing or has wrong kind");
            return false;
        }
        if (entry.stateOp->results().size() != 1 || entry.stateOp->results().front() != entry.value) {
            fail("Register state op result does not match memo value");
            return false;
        }
        if (entry.stateOp->operands().size() != operandCount) {
            fail("Register operand count mismatch");
            return false;
        }
        if (entry.stateOp->operands().empty() || entry.stateOp->operands().front() != clkPort) {
            fail("Register clock operand is not bound to clk port");
            return false;
        }
        const grh::Value* dataOperand = entry.stateOp->operands().back();
        if (!dataOperand || dataOperand->width() != entry.width) {
            fail("Register data operand width mismatch");
            return false;
        }
        return true;
    };

    if (!expectRegisterKind(*regFull, grh::OperationKind::kRegister, 2) ||
        !expectRegisterKind(*regPartial, grh::OperationKind::kRegister, 2) ||
        !expectRegisterKind(*regMulti, grh::OperationKind::kRegister, 2) ||
        !expectRegisterKind(*regSync, grh::OperationKind::kRegisterRst, 4) ||
        !expectRegisterKind(*regAsync, grh::OperationKind::kRegisterARst, 4)) {
        return 1;
    }

    auto verifyConcat = [&](const SignalMemoEntry& entry, const grh::Value* expectedHi,
                            const grh::Value* expectedLo) -> bool {
        const grh::Value* dataValue = entry.stateOp->operands().back();
        const grh::Operation* concatOp = dataValue->definingOp();
        if (!concatOp || concatOp->kind() != grh::OperationKind::kConcat) {
            return fail("Expected register data to be driven by kConcat");
        }
        if (concatOp->operands().size() != 2) {
            return fail("Concat operand count mismatch");
        }
        if (concatOp->operands()[0] != expectedHi || concatOp->operands()[1] != expectedLo) {
            return fail("Concat operands do not match expected inputs");
        }
        return true;
    };

    if (!verifyConcat(*regFull, hiPort, loPort)) {
        return 1;
    }

    if (!verifyConcat(*regMulti, hiPort, loPort)) {
        return 1;
    }

    // reg_partial should keep upper bits from previous Q via kSlice and append new low nibble.
    const grh::Value* partialData = regPartial->stateOp->operands().back();
    const grh::Operation* partialConcat = partialData->definingOp();
    if (!partialConcat || partialConcat->kind() != grh::OperationKind::kConcat) {
        return fail("reg_partial data is not driven by kConcat");
    }
    if (partialConcat->operands().size() != 2) {
        return fail("reg_partial concat operand count mismatch");
    }

    const grh::Value* holdValue = partialConcat->operands()[0];
    const grh::Value* rhsValue = partialConcat->operands()[1];
    if (rhsValue != loPort) {
        return fail("reg_partial low bits are not sourced from lo_data");
    }

    const grh::Operation* holdSlice = holdValue ? holdValue->definingOp() : nullptr;
    if (!holdSlice || holdSlice->kind() != grh::OperationKind::kSliceStatic) {
        return fail("reg_partial high bits are not provided by a kSliceStatic over Q");
    }
    if (holdSlice->operands().size() != 1 || holdSlice->operands().front() != regPartial->value) {
        return fail("reg_partial slice does not target the register's Q output");
    }
    if (!expectAttrs(*holdSlice, "sliceStart", 4) || !expectAttrs(*holdSlice, "sliceEnd", 7)) {
        return fail("reg_partial slice attributes are incorrect");
    }

    std::function<bool(const grh::Value*, const grh::Value*)> mentionsPort =
        [&](const grh::Value* node, const grh::Value* port) -> bool {
        if (!node) {
            return false;
        }
        if (node == port) {
            return true;
        }
        if (const grh::Operation* op = node->definingOp()) {
            for (const grh::Value* operand : op->operands()) {
                if (mentionsPort(operand, port)) {
                    return true;
                }
            }
        }
        return false;
    };

    auto isZeroConstant = [&](const grh::Value* value) -> bool {
        if (!value) {
            return false;
        }
        const grh::Operation* op = value->definingOp();
        if (!op || op->kind() != grh::OperationKind::kConstant) {
            return false;
        }
        auto it = op->attributes().find("constValue");
        if (it == op->attributes().end() || !std::holds_alternative<std::string>(it->second)) {
            return false;
        }
        const std::string& literal = std::get<std::string>(it->second);
        return literal.find("'h0") != std::string::npos || literal.find("'d0") != std::string::npos ||
               literal.find("'b0") != std::string::npos;
    };

    const grh::Value* syncData = regSync->stateOp->operands().back();
    const grh::Operation* syncMux = syncData ? syncData->definingOp() : nullptr;
    if (!syncMux || syncMux->kind() != grh::OperationKind::kMux) {
        return fail("reg_sync_rst data is not driven by kMux");
    }
    if (syncMux->operands().size() != 3) {
        return fail("reg_sync_rst mux operand count mismatch");
    }
    if (!mentionsPort(syncMux->operands().front(), rstSyncPort)) {
        return fail("reg_sync_rst mux condition does not reference rst_sync");
    }
    if (!isZeroConstant(syncMux->operands()[1])) {
        return fail("reg_sync_rst reset value is not zero");
    }
    const grh::Operation* syncConcat =
        syncMux->operands()[2] ? syncMux->operands()[2]->definingOp() : nullptr;
    if (!syncConcat || syncConcat->kind() != grh::OperationKind::kConcat) {
        return fail("reg_sync_rst data path is not driven by concat");
    }
    if (syncConcat->operands().size() != 2 ||
        syncConcat->operands()[0] != hiPort || syncConcat->operands()[1] != loPort) {
        return fail("reg_sync_rst concat operands do not match hi/lo data");
    }

    auto checkResetOperands = [&](const SignalMemoEntry& entry, const grh::Value* expectedSignal,
                                  std::string_view expectLevel) -> bool {
        const auto& attrs = entry.stateOp->attributes();
        auto it = attrs.find("rstLevel");
        if (it == attrs.end() || !std::holds_alternative<std::string>(it->second) ||
            std::get<std::string>(it->second) != expectLevel) {
            fail("rstLevel attribute mismatch");
            return false;
        }
        if (entry.stateOp->operands().size() < 3) {
            fail("Reset operands missing");
            return false;
        }
        if (entry.stateOp->operands()[1] != expectedSignal) {
            fail("Reset operand does not reference expected signal");
            return false;
        }
        if (!isZeroConstant(entry.stateOp->operands()[2])) {
            fail("Reset value is not zero constant");
            return false;
        }
        return true;
    };

    if (!checkResetOperands(*regSync, rstSyncPort, "1'b0")) {
        return 1;
    }
    if (!checkResetOperands(*regAsync, rstPort, "1'b0")) {
        return 1;
    }

    bool unexpectedDiag = false;
    for (const ElaborateDiagnostic& msg : diagnostics.messages()) {
        if (msg.message.find("Module body elaboration pending") != std::string::npos) {
            continue;
        }
        std::cerr << "[diag] " << msg.message << '\n';
        unexpectedDiag = true;
    }
    if (unexpectedDiag) {
        return fail("Sequential finalize should not emit diagnostics for supported cases");
    }

    return 0;
}
