#include "elaborate.hpp"
#include "emit.hpp"

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
using namespace wolf_sv::emit;

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
        return graph.findValue(it->second);
    }
    return nullptr;
}

const grh::Operation* findMemoryOp(const grh::Graph& graph, grh::OperationKind kind,
                                   std::string_view memSymbol) {
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        if (op.kind() != kind) {
            continue;
        }
        auto it = op.attributes().find("memSymbol");
        if (it == op.attributes().end()) {
            continue;
        }
        if (!std::holds_alternative<std::string>(it->second)) {
            continue;
        }
        if (std::get<std::string>(it->second) == memSymbol) {
            return &op;
        }
    }
    return nullptr;
}

std::vector<const grh::Operation*> collectMemoryOps(const grh::Graph& graph,
                                                    grh::OperationKind kind,
                                                    std::string_view memSymbol) {
    std::vector<const grh::Operation*> ops;
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        if (op.kind() != kind) {
            continue;
        }
        auto it = op.attributes().find("memSymbol");
        if (it == op.attributes().end() || !std::holds_alternative<std::string>(it->second)) {
            continue;
        }
        if (std::get<std::string>(it->second) == memSymbol) {
            ops.push_back(&op);
        }
    }
    return ops;
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

    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    auto jsonOpt = emitter.emitToString(netlist, options);
    if (!jsonOpt || diagnostics.hasError()) {
        std::cerr << "[elaborate_seq_always] Failed to emit JSON artifact\n";
        return false;
    }

    os << *jsonOpt;
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

const grh::Operation* findOpByKind(const grh::Graph& graph, grh::OperationKind kind) {
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        if (op.kind() == kind) {
            return &op;
        }
    }
    return nullptr;
}

std::vector<const grh::Operation*> collectOpsByKind(const grh::Graph& graph,
                                                    grh::OperationKind kind) {
    std::vector<const grh::Operation*> result;
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        if (op.kind() == kind) {
            result.push_back(&op);
        }
    }
    return result;
}

bool expectStringAttr(const grh::Operation& op, std::string_view key, std::string_view expect) {
    auto it = op.attributes().find(std::string(key));
    if (it == op.attributes().end()) {
        return false;
    }
    if (!std::holds_alternative<std::string>(it->second)) {
        return false;
    }
    return std::get<std::string>(it->second) == expect;
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
        !expectRegisterKind(*regAsync, grh::OperationKind::kRegisterArst, 4)) {
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
    const grh::Operation* dataOp = syncData ? syncData->definingOp() : nullptr;
    if (dataOp && dataOp->kind() == grh::OperationKind::kMux) {
        // 图中仍保留 mux 表达 reset：走原有校验路径
        if (dataOp->operands().size() != 3) {
            return fail("reg_sync_rst mux operand count mismatch");
        }
        if (!mentionsPort(dataOp->operands().front(), rstSyncPort)) {
            return fail("reg_sync_rst mux condition does not reference rst_sync");
        }
        if (!isZeroConstant(dataOp->operands()[1])) {
            return fail("reg_sync_rst reset value is not zero");
        }
        const grh::Operation* syncConcat =
            dataOp->operands()[2] ? dataOp->operands()[2]->definingOp() : nullptr;
        if (!syncConcat || syncConcat->kind() != grh::OperationKind::kConcat) {
            return fail("reg_sync_rst data path is not driven by concat");
        }
        if (syncConcat->operands().size() != 2 ||
            syncConcat->operands()[0] != hiPort || syncConcat->operands()[1] != loPort) {
            return fail("reg_sync_rst concat operands do not match hi/lo data");
        }
    } else {
        // 允许阶段21抽取 reset 后，data 直接为 concat(hi, lo)
        const grh::Operation* syncConcat = dataOp;
        if (!syncConcat || syncConcat->kind() != grh::OperationKind::kConcat) {
            return fail("reg_sync_rst data is not driven by kMux");
        }
        if (syncConcat->operands().size() != 2 ||
            syncConcat->operands()[0] != hiPort || syncConcat->operands()[1] != loPort) {
            return fail("reg_sync_rst concat operands do not match hi/lo data");
        }
    }

    auto checkResetOperands = [&](const SignalMemoEntry& entry, const grh::Value* expectedSignal,
                                  std::string_view expectLevel) -> bool {
        const auto& attrs = entry.stateOp->attributes();
        auto it = attrs.find("rstPolarity");
        if (it == attrs.end() || !std::holds_alternative<std::string>(it->second) ||
            std::get<std::string>(it->second) != expectLevel) {
            fail("rstPolarity attribute mismatch");
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

    if (!checkResetOperands(*regSync, rstSyncPort, "low")) {
        return 1;
    }
    if (!checkResetOperands(*regAsync, rstPort, "low")) {
        return 1;
    }

    const auto* inst18 = findInstanceByName(compilation->getRoot().topInstances, "seq_stage18");
    if (!inst18) {
        return fail("Top instance seq_stage18 not found");
    }
    const grh::Graph* graph18 = netlist.findGraph("seq_stage18");
    if (!graph18) {
        return fail("GRH graph seq_stage18 not found");
    }

    const grh::Value* clk18 = findPort(*graph18, "clk", /*isInput=*/true);
    const grh::Value* wrAddr = findPort(*graph18, "wr_addr", /*isInput=*/true);
    const grh::Value* rdAddr = findPort(*graph18, "rd_addr", /*isInput=*/true);
    const grh::Value* maskAddr = findPort(*graph18, "mask_addr", /*isInput=*/true);
    const grh::Value* bitIndex = findPort(*graph18, "bit_index", /*isInput=*/true);
    const grh::Value* bitValue = findPort(*graph18, "bit_value", /*isInput=*/true);
    const grh::Value* rdDataOut = findPort(*graph18, "rd_data", /*isInput=*/false);
    if (!clk18 || !wrAddr || !rdAddr || !maskAddr || !bitIndex || !bitValue || !rdDataOut) {
        return fail("seq_stage18 ports are missing");
    }

    std::span<const SignalMemoEntry> regMemo18 = elaborator.peekRegMemo(fetchBody(*inst18));
    const SignalMemoEntry* memEntry = findEntry(regMemo18, "mem");
    const SignalMemoEntry* rdRegEntry = findEntry(regMemo18, "rd_reg");
    if (!memEntry || !rdRegEntry) {
        return fail("seq_stage18 memo entries missing mem or rd_reg");
    }
    if (!memEntry->stateOp || memEntry->stateOp->kind() != grh::OperationKind::kMemory) {
        return fail("seq_stage18 mem entry lacks kMemory state op");
    }

    const std::string memSymbol = memEntry->stateOp->symbol();

    const grh::Operation* syncRead =
        findMemoryOp(*graph18, grh::OperationKind::kMemorySyncReadPort, memSymbol);
    if (!syncRead) {
        return fail("kMemorySyncReadPort not found for seq_stage18");
    }
    if (syncRead->operands().size() != 3 || syncRead->operands()[0] != clk18 ||
        syncRead->operands()[1] != rdAddr) {
        return fail("Memory sync read operands are incorrect");
    }
    if (!syncRead->operands()[2] || !syncRead->operands()[2]->definingOp() ||
        syncRead->operands()[2]->definingOp()->kind() != grh::OperationKind::kConstant) {
        return fail("Memory sync read enable is not tied to constant one");
    }
    if (syncRead->results().size() != 1 || syncRead->results().front()->width() != 8) {
        return fail("Memory sync read result width mismatch");
    }
    if (rdRegEntry->stateOp->operands().empty() ||
        rdRegEntry->stateOp->operands().back() != syncRead->results().front()) {
        return fail("rd_reg data input is not driven by the sync read port");
    }
    const grh::Operation* writePort =
        findMemoryOp(*graph18, grh::OperationKind::kMemoryWritePort, memSymbol);
    if (!writePort) {
        return fail("kMemoryWritePort not found for seq_stage18");
    }
    if (writePort->operands().size() != 4 || writePort->operands()[0] != clk18 ||
        writePort->operands()[1] != wrAddr) {
        return fail("Memory write port operands mismatched");
    }
    if (!writePort->operands()[2] || !writePort->operands()[2]->definingOp() ||
        writePort->operands()[2]->definingOp()->kind() != grh::OperationKind::kConstant) {
        return fail("Memory write port enable should be constant one");
    }
    if (writePort->operands()[3]->width() != 8) {
        return fail("Memory write port data width mismatch");
    }

    const grh::Operation* maskPort =
        findMemoryOp(*graph18, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
    if (!maskPort) {
        return fail("kMemoryMaskWritePort not found for seq_stage18");
    }
    if (maskPort->operands().size() != 5 || maskPort->operands()[0] != clk18 ||
        maskPort->operands()[1] != maskAddr) {
        return fail("Memory mask write operands mismatched");
    }
    if (!maskPort->operands()[2] || !maskPort->operands()[2]->definingOp() ||
        maskPort->operands()[2]->definingOp()->kind() != grh::OperationKind::kConstant) {
        return fail("Memory mask write enable should be constant one");
    }
    if (maskPort->operands()[3]->width() != 8 || maskPort->operands()[4]->width() != 8) {
        return fail("Memory mask write data/mask widths mismatch");
    }
    const grh::Operation* dataShift =
        maskPort->operands()[3] ? maskPort->operands()[3]->definingOp() : nullptr;
    const grh::Operation* maskShift =
        maskPort->operands()[4] ? maskPort->operands()[4]->definingOp() : nullptr;
    if (!dataShift || dataShift->kind() != grh::OperationKind::kShl ||
        dataShift->operands().size() != 2 || dataShift->operands()[1] != bitIndex) {
        return fail("Memory mask write data path is not shifted by bit_index");
    }
    const grh::Operation* dataConcat =
        dataShift->operands()[0] ? dataShift->operands()[0]->definingOp() : nullptr;
    if (!dataConcat || dataConcat->kind() != grh::OperationKind::kConcat ||
        dataConcat->operands().size() != 2 || dataConcat->operands()[1] != bitValue) {
        return fail("Memory mask write data concat does not source bit_value");
    }
    if (!maskShift || maskShift->kind() != grh::OperationKind::kShl ||
        maskShift->operands().size() != 2 || maskShift->operands()[1] != bitIndex) {
        return fail("Memory mask write mask is not shifted by bit_index");
    }
    const grh::Operation* maskConst =
        maskShift->operands()[0] ? maskShift->operands()[0]->definingOp() : nullptr;
    if (!maskConst || maskConst->kind() != grh::OperationKind::kConstant) {
        return fail("Memory mask base should be a constant one-hot literal");
    }

    bool unexpectedDiag = false;
    for (const ElaborateDiagnostic& msg : diagnostics.messages()) {
        if (msg.message.find("Module body elaboration pending") != std::string::npos) {
            continue;
        }
        if (msg.message.find("display-like task") != std::string::npos) {
            continue;
        }
        std::cerr << "[diag] " << msg.message << '\n';
        unexpectedDiag = true;
    }
    if (unexpectedDiag) {
        return fail("Sequential finalize should not emit diagnostics for supported cases");
    }

    // -----------------------
    // Stage19: if/case tests
    // -----------------------

    // Helper to find graph by name.
    auto fetchGraph = [&](std::string_view name) -> const grh::Graph* {
        const slang::ast::InstanceSymbol* ins = findInstanceByName(compilation->getRoot().topInstances, name);
        if (!ins) {
            fail(std::string("Top instance not found: ") + std::string(name));
            return nullptr;
        }
        const grh::Graph* g = netlist.findGraph(std::string(name));
        if (!g) {
            fail(std::string("GRH graph not found: ") + std::string(name));
            return nullptr;
        }
        return g;
    };

    // 19.1 if (en) r <= d;
    if (const grh::Graph* g19_1 = fetchGraph("seq_stage19_if_en_reg")) {
        const grh::Value* clk = findPort(*g19_1, "clk", /*isInput=*/true);
        const grh::Value* en  = findPort(*g19_1, "en", /*isInput=*/true);
        const grh::Value* d   = findPort(*g19_1, "d", /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage19_if_en_reg missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_if_en_reg");
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage19_if_en_reg missing stateOp");
        }
        if (r->stateOp->kind() == grh::OperationKind::kRegister) {
            if (r->stateOp->operands().size() != 2 || r->stateOp->operands().front() != clk) {
                return fail("seq_stage19_if_en_reg clock binding error");
            }
            const grh::Value* data = r->stateOp->operands().back();
            const grh::Operation* mux = data ? data->definingOp() : nullptr;
            if (!mux || mux->kind() != grh::OperationKind::kMux || mux->operands().size() != 3) {
                return fail("seq_stage19_if_en_reg data is not a kMux");
            }
            if (!mentionsPort(mux->operands().front(), en)) {
                return fail("seq_stage19_if_en_reg mux condition does not reference en");
            }
            // True branch should be driven by d, false branch by Q (hold).
            if (mux->operands()[1] != d) {
                return fail("seq_stage19_if_en_reg mux true branch is not d");
            }
            if (mux->operands()[2] != r->value) {
                return fail("seq_stage19_if_en_reg mux false branch is not hold(Q)");
            }
        } else if (r->stateOp->kind() == grh::OperationKind::kRegisterEn) {
            // Stage21+：允许被特化为带使能原语
            if (r->stateOp->operands().size() != 3 || r->stateOp->operands().front() != clk) {
                return fail("seq_stage19_if_en_reg kRegisterEn operand mismatch");
            }
            // 使能操作数应当能“提及”en 端口（可能存在归一化/取反等）
            if (!mentionsPort(r->stateOp->operands()[1], en)) {
                return fail("seq_stage19_if_en_reg kRegisterEn enable does not mention en");
            }
            if (r->stateOp->operands()[2] != d) {
                return fail("seq_stage19_if_en_reg kRegisterEn data is not d");
            }
        } else {
            return fail("seq_stage19_if_en_reg unexpected register kind");
        }
    }

    // 19.2 if-en gated memory read/write/mask
    if (const grh::Graph* g19_2 = fetchGraph("seq_stage19_if_en_mem")) {
        const grh::Value* clk      = findPort(*g19_2, "clk", /*isInput=*/true);
        const grh::Value* en_wr    = findPort(*g19_2, "en_wr", /*isInput=*/true);
        const grh::Value* en_bit   = findPort(*g19_2, "en_bit", /*isInput=*/true);
        const grh::Value* en_rd    = findPort(*g19_2, "en_rd", /*isInput=*/true);
        const grh::Value* wr_addr  = findPort(*g19_2, "wr_addr", /*isInput=*/true);
        const grh::Value* rd_addr  = findPort(*g19_2, "rd_addr", /*isInput=*/true);
        const grh::Value* mask_addr= findPort(*g19_2, "mask_addr", /*isInput=*/true);
        const grh::Value* bit_index= findPort(*g19_2, "bit_index", /*isInput=*/true);
        if (!clk || !en_wr || !en_bit || !en_rd || !wr_addr || !rd_addr || !mask_addr || !bit_index) {
            return fail("seq_stage19_if_en_mem missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_if_en_mem");
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        const SignalMemoEntry* rd_reg = findEntry(memo, "rd_reg");
        if (!mem || !rd_reg || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage19_if_en_mem mem/rd_reg not found or mem not kMemory");
        }
        const std::string memSymbol = mem->stateOp->symbol();
        const grh::Operation* wr =
            findMemoryOp(*g19_2, grh::OperationKind::kMemoryWritePort, memSymbol);
        const grh::Operation* mwr =
            findMemoryOp(*g19_2, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
        const grh::Operation* rd =
            findMemoryOp(*g19_2, grh::OperationKind::kMemorySyncReadPort, memSymbol);
        if (!wr || !mwr || !rd) {
            return fail("seq_stage19_if_en_mem expected memory ports not found");
        }
        if (wr->operands().size() != 4 || wr->operands()[0] != clk || wr->operands()[1] != wr_addr) {
            return fail("seq_stage19_if_en_mem write port operand mismatch");
        }
        if (!mentionsPort(wr->operands()[2], en_wr)) {
            return fail("seq_stage19_if_en_mem write enable does not mention en_wr");
        }
        if (mwr->operands().size() != 5 || mwr->operands()[0] != clk || mwr->operands()[1] != mask_addr) {
            return fail("seq_stage19_if_en_mem mask write operand mismatch");
        }
        if (!mentionsPort(mwr->operands()[2], en_bit)) {
            return fail("seq_stage19_if_en_mem mask write enable does not mention en_bit");
        }
        if (rd->operands().size() != 3 || rd->operands()[0] != clk || rd->operands()[1] != rd_addr) {
            return fail("seq_stage19_if_en_mem sync read operand mismatch");
        }
        if (!mentionsPort(rd->operands()[2], en_rd)) {
            return fail("seq_stage19_if_en_mem sync read enable does not mention en_rd");
        }
        if (rd_reg->stateOp->operands().empty()) {
            return fail("seq_stage19_if_en_mem rd_reg missing data operand");
        }
        const grh::Value* rdData = rd_reg->stateOp->operands().back();
        if (rdData != rd->results().front()) {
            // Allow gated data path: mux(en_rd, rd_result, Q)
            const grh::Operation* m = rdData->definingOp();
            if (!m || m->kind() != grh::OperationKind::kMux || m->operands().size() != 3) {
                return fail("seq_stage19_if_en_mem rd_reg not driven by sync read or mux");
            }
            if (!mentionsPort(m->operands().front(), en_rd)) {
                return fail("seq_stage19_if_en_mem mux condition does not reference en_rd");
            }
            if (m->operands()[1] != rd->results().front() || m->operands()[2] != rd_reg->value) {
                return fail("seq_stage19_if_en_mem mux branches are not (rd_result, hold(Q))");
            }
        }
    }

    // 19.3 case(sel) branches -> write/mask enables
    if (const grh::Graph* g19_3 = fetchGraph("seq_stage19_case_mem")) {
        const grh::Value* clk = findPort(*g19_3, "clk", /*isInput=*/true);
        const grh::Value* sel = findPort(*g19_3, "sel", /*isInput=*/true);
        const grh::Value* addr = findPort(*g19_3, "addr", /*isInput=*/true);
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_case_mem");
        if (!clk || !sel || !addr || !inst) {
            return fail("seq_stage19_case_mem ports missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage19_case_mem mem not found");
        }
        const std::string memSymbol = mem->stateOp->symbol();
        const grh::Operation* wr =
            findMemoryOp(*g19_3, grh::OperationKind::kMemoryWritePort, memSymbol);
        const grh::Operation* mwr =
            findMemoryOp(*g19_3, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
        if (!wr || !mwr) {
            return fail("seq_stage19_case_mem expected write/mask ports missing");
        }
        // write enable should reference sel and equal sel==0
        const grh::Value* wrEn = wr->operands().size() > 2 ? wr->operands()[2] : nullptr;
        const grh::Operation* wrEnOp = wrEn ? wrEn->definingOp() : nullptr;
        if (!wrEn || !wrEnOp || wrEnOp->kind() != grh::OperationKind::kEq) {
            return fail("seq_stage19_case_mem write enable is not eq(sel, const)");
        }
        if (!mentionsPort(wrEnOp->operands().front(), sel) &&
            !mentionsPort(wrEnOp->operands().back(), sel)) {
            return fail("seq_stage19_case_mem write enable does not reference sel");
        }
        // mask write enable should reference sel and equal sel==1
        const grh::Value* mwrEn = mwr->operands().size() > 2 ? mwr->operands()[2] : nullptr;
        const grh::Operation* mwrEnOp = mwrEn ? mwrEn->definingOp() : nullptr;
        if (!mwrEn || !mwrEnOp || mwrEnOp->kind() != grh::OperationKind::kEq) {
            return fail("seq_stage19_case_mem mask write enable is not eq(sel, const)");
        }
        if (!mentionsPort(mwrEnOp->operands().front(), sel) &&
            !mentionsPort(mwrEnOp->operands().back(), sel)) {
            return fail("seq_stage19_case_mem mask write enable does not reference sel");
        }
    }

    // 19.4 casez wildcard: two writes, each enable references sel with wildcard logic
    if (const grh::Graph* g19_4 = fetchGraph("seq_stage19_casez_mem")) {
        const grh::Value* clk = findPort(*g19_4, "clk", /*isInput=*/true);
        const grh::Value* sel = findPort(*g19_4, "sel", /*isInput=*/true);
        const grh::Value* addr = findPort(*g19_4, "addr", /*isInput=*/true);
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_casez_mem");
        if (!clk || !sel || !addr || !inst) {
            return fail("seq_stage19_casez_mem ports missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage19_casez_mem mem not found");
        }
        const std::string memSymbol = mem->stateOp->symbol();
        // Collect all write ports for this mem
        std::vector<const grh::Operation*> writes;
        for (const auto& opSymbol : g19_4->operationOrder()) {
            const grh::Operation& op = g19_4->getOperation(opSymbol);
            if (op.kind() == grh::OperationKind::kMemoryWritePort) {
                auto it = op.attributes().find("memSymbol");
                if (it != op.attributes().end() && std::holds_alternative<std::string>(it->second) &&
                    std::get<std::string>(it->second) == memSymbol) {
                    writes.push_back(&op);
                }
            }
        }
        if (writes.size() != 2) {
            return fail("seq_stage19_casez_mem expects two write ports");
        }
        for (const grh::Operation* wr : writes) {
            if (wr->operands().size() < 3) {
                return fail("seq_stage19_casez_mem write port missing enable");
            }
            const grh::Value* en = wr->operands()[2];
            if (!mentionsPort(en, sel)) {
                return fail("seq_stage19_casez_mem write enable does not reference sel");
            }
        }
    }

    // 19.5 rst + en register
    if (const grh::Graph* g19_5 = fetchGraph("seq_stage19_rst_en_reg")) {
        const grh::Value* clk = findPort(*g19_5, "clk", /*isInput=*/true);
        const grh::Value* rst = findPort(*g19_5, "rst", /*isInput=*/true);
        const grh::Value* en  = findPort(*g19_5, "en",  /*isInput=*/true);
        const grh::Value* d   = findPort(*g19_5, "d",   /*isInput=*/true);
        if (!clk || !rst || !en || !d) {
            return fail("seq_stage19_rst_en_reg missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_rst_en_reg");
        if (!inst) {
            return fail("seq_stage19_rst_en_reg instance missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage19_rst_en_reg missing stateOp");
        }
        if (r->stateOp->kind() == grh::OperationKind::kRegisterRst) {
            auto it = r->stateOp->attributes().find("rstPolarity");
            if (it == r->stateOp->attributes().end() || !std::holds_alternative<std::string>(it->second) ||
                std::get<std::string>(it->second) != "high") {
                return fail("seq_stage19_rst_en_reg rstPolarity attribute unexpected");
            }
            if (r->stateOp->operands().size() < 4 || r->stateOp->operands()[0] != clk ||
                r->stateOp->operands()[1] != rst) {
                return fail("seq_stage19_rst_en_reg clk/rst operands not bound");
            }
            if (!isZeroConstant(r->stateOp->operands()[2])) {
                return fail("seq_stage19_rst_en_reg reset value is not zero constant");
            }
            // Data path should reference en (gated assignment)
            const grh::Value* data = r->stateOp->operands().back();
            if (!mentionsPort(data, en)) {
                return fail("seq_stage19_rst_en_reg data path does not reference en");
            }
        } else if (r->stateOp->kind() == grh::OperationKind::kRegisterEnRst) {
            // Stage21+：特化为带使能 + 同步复位原语
            auto it = r->stateOp->attributes().find("rstPolarity");
            if (it == r->stateOp->attributes().end() || !std::holds_alternative<std::string>(it->second) ||
                std::get<std::string>(it->second) != "high") {
                return fail("seq_stage19_rst_en_reg (EnRst) rstPolarity attribute unexpected");
            }
            auto enAttr = r->stateOp->attributes().find("enLevel");
            if (enAttr == r->stateOp->attributes().end() ||
                !std::holds_alternative<std::string>(enAttr->second) ||
                std::get<std::string>(enAttr->second) != "high") {
                return fail("seq_stage19_rst_en_reg (EnRst) enLevel attribute unexpected");
            }
            if (r->stateOp->operands().size() != 5 ||
                r->stateOp->operands()[0] != clk ||
                r->stateOp->operands()[1] != rst) {
                return fail("seq_stage19_rst_en_reg (EnRst) clk/rst operands mismatch");
            }
            if (!mentionsPort(r->stateOp->operands()[2], en)) {
                return fail("seq_stage19_rst_en_reg (EnRst) enable does not mention en");
            }
            // resetValue == zero; data mentions d
            if (!isZeroConstant(r->stateOp->operands()[3])) {
                return fail("seq_stage19_rst_en_reg (EnRst) reset value is not zero");
            }
            if (!mentionsPort(r->stateOp->operands()[4], d)) {
                return fail("seq_stage19_rst_en_reg (EnRst) data does not mention d");
            }
        } else {
            return fail("seq_stage19_rst_en_reg unexpected register kind");
        }
    }

    // -----------------------
    // Stage20: loop tests
    // -----------------------

    // 20.1 for + continue: last-write-wins -> r <= d2
    if (const grh::Graph* g20_1 = fetchGraph("seq_stage20_for_last_write")) {
        const grh::Value* clk = findPort(*g20_1, "clk", /*isInput=*/true);
        const grh::Value* d0  = findPort(*g20_1, "d0",  /*isInput=*/true);
        const grh::Value* d2  = findPort(*g20_1, "d2",  /*isInput=*/true);
        if (!clk || !d0 || !d2) {
            return fail("seq_stage20_for_last_write missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage20_for_last_write");
        if (!inst) {
            return fail("seq_stage20_for_last_write instance missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp || r->stateOp->kind() != grh::OperationKind::kRegister) {
            return fail("seq_stage20_for_last_write r is not kRegister");
        }
        if (r->stateOp->operands().size() < 2 || r->stateOp->operands().front() != clk) {
            return fail("seq_stage20_for_last_write clock binding error");
        }
        const grh::Value* data = r->stateOp->operands().back();
        // 最终数据应直接等于 d2（最后一次写）
        if (data != d2) {
            return fail("seq_stage20_for_last_write last-write is not d2");
        }
        // 确保没有意外依赖 d0
        std::function<bool(const grh::Value*, const grh::Value*)> mentionsPort =
            [&](const grh::Value* node, const grh::Value* port) -> bool {
            if (!node) return false;
            if (node == port) return true;
            if (const grh::Operation* op = node->definingOp()) {
                for (const grh::Value* operand : op->operands()) {
                    if (mentionsPort(operand, port)) return true;
                }
            }
            return false;
        };
        if (mentionsPort(data, d0)) {
            return fail("seq_stage20_for_last_write data should not depend on d0");
        }
    }

    // -----------------------
    // Stage22: display/write/strobe lowering
    // -----------------------

    // 22.1 basic display emits kDisplay with clk/en/var
    if (const grh::Graph* g22_1 = fetchGraph("seq_stage22_display_basic")) {
        const grh::Value* clk = findPort(*g22_1, "clk", /*isInput=*/true);
        const grh::Value* d = findPort(*g22_1, "d", /*isInput=*/true);
        const grh::Value* q = findPort(*g22_1, "q", /*isInput=*/false);
        if (!clk || !d || !q) {
            return fail("seq_stage22_display_basic missing ports");
        }
        const auto* inst =
            findInstanceByName(compilation->getRoot().topInstances, "seq_stage22_display_basic");
        if (!inst) {
            return fail("seq_stage22_display_basic instance missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* rEntry = findEntry(memo, "r");
        if (!rEntry || !rEntry->value) {
            return fail("seq_stage22_display_basic missing reg memo for r");
        }
        const grh::Operation* display = findOpByKind(*g22_1, grh::OperationKind::kDisplay);
        if (!display) {
            return fail("seq_stage22_display_basic missing kDisplay");
        }
        if (display->operands().size() != 3) {
            return fail("seq_stage22_display_basic display operand count mismatch");
        }
        if (display->operands()[0] != clk) {
            return fail("seq_stage22_display_basic clk operand mismatch");
        }
        // enable should be constant 1; data should reference register value
        if (display->operands()[2] != rEntry->value) {
            return fail("seq_stage22_display_basic value operand mismatch");
        }
        if (!expectStringAttr(*display, "formatString", "r=%0d")) {
            return fail("seq_stage22_display_basic formatString attribute mismatch");
        }
        if (!expectStringAttr(*display, "displayKind", "display")) {
            return fail("seq_stage22_display_basic displayKind attribute missing");
        }
    }

    // 22.2 guarded write: enable operand should reference guard (en)
    if (const grh::Graph* g22_2 = fetchGraph("seq_stage22_guarded_write")) {
        const grh::Value* clk = findPort(*g22_2, "clk", /*isInput=*/true);
        const grh::Value* en = findPort(*g22_2, "en", /*isInput=*/true);
        const grh::Value* d = findPort(*g22_2, "d", /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage22_guarded_write missing ports");
        }
        const grh::Operation* display = findOpByKind(*g22_2, grh::OperationKind::kDisplay);
        if (!display) {
            return fail("seq_stage22_guarded_write missing kDisplay");
        }
        if (display->operands().size() != 4) {
            return fail("seq_stage22_guarded_write operand count mismatch");
        }
        if (display->operands()[0] != clk || display->operands()[1] != en) {
            return fail("seq_stage22_guarded_write clk/enable operands mismatch");
        }
        if (display->operands()[2] != en || display->operands()[3] != d) {
            return fail("seq_stage22_guarded_write value operands mismatch");
        }
        if (!expectStringAttr(*display, "displayKind", "write")) {
            return fail("seq_stage22_guarded_write displayKind unexpected");
        }
    }

    // 22.3 strobe variant: ensure kind recorded
    if (const grh::Graph* g22_3 = fetchGraph("seq_stage22_strobe")) {
        const grh::Value* clk = findPort(*g22_3, "clk", /*isInput=*/true);
        const grh::Value* d = findPort(*g22_3, "d", /*isInput=*/true);
        if (!clk || !d) {
            return fail("seq_stage22_strobe missing ports");
        }
        const grh::Operation* display = findOpByKind(*g22_3, grh::OperationKind::kDisplay);
        if (!display) {
            return fail("seq_stage22_strobe missing kDisplay");
        }
        if (display->operands().size() != 3 ||
            display->operands()[0] != clk || display->operands()[2] != d) {
            return fail("seq_stage22_strobe operands mismatch");
        }
        if (!expectStringAttr(*display, "displayKind", "strobe")) {
            return fail("seq_stage22_strobe displayKind unexpected");
        }
    }

    // 22.x (diag filter already handled above)

    // -----------------------
    // Stage23: assert lowering
    // -----------------------

    // Helper: fetch assert ops and basic checks.
    auto expectAssert = [&](const grh::Graph& graph, const grh::Value* clk,
                            std::optional<std::string_view> message = std::nullopt) -> bool {
        std::vector<const grh::Operation*> asserts =
            collectOpsByKind(graph, grh::OperationKind::kAssert);
        if (asserts.empty()) {
            return fail("Expected at least one kAssert");
        }
        for (const grh::Operation* op : asserts) {
            if (op->operands().size() != 2) {
                return fail("kAssert operand count mismatch");
            }
            if (op->operands()[0] != clk) {
                return fail("kAssert clock operand mismatch");
            }
            if (message) {
                auto it = op->attributes().find("message");
                if (it == op->attributes().end() || !std::holds_alternative<std::string>(it->second) ||
                    std::get<std::string>(it->second) != *message) {
                    return fail("kAssert message attribute mismatch");
                }
            }
        }
        return true;
    };

    // 23.1 basic assert
    if (const grh::Graph* g23_1 = fetchGraph("seq_stage23_assert_basic")) {
        const grh::Value* clk = findPort(*g23_1, "clk", /*isInput=*/true);
        if (!clk) {
            return fail("seq_stage23_assert_basic missing clk");
        }
        if (!expectAssert(*g23_1, clk)) {
            return 1;
        }
    }

    // 23.2 guarded assert with message
    if (const grh::Graph* g23_2 = fetchGraph("seq_stage23_assert_guard")) {
        const grh::Value* clk = findPort(*g23_2, "clk", /*isInput=*/true);
        const grh::Value* en = findPort(*g23_2, "en", /*isInput=*/true);
        const grh::Value* d = findPort(*g23_2, "d", /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage23_assert_guard missing ports");
        }
        std::vector<const grh::Operation*> asserts =
            collectOpsByKind(*g23_2, grh::OperationKind::kAssert);
        if (asserts.size() != 1) {
            return fail("seq_stage23_assert_guard expected one kAssert");
        }
        const grh::Operation* op = asserts.front();
        if (op->operands().size() != 2 || op->operands()[0] != clk) {
            return fail("seq_stage23_assert_guard operand mismatch");
        }
        // guard -> cond encoded as (!en) || cond; ensure guard is present via operand users.
        std::function<bool(const grh::Value*, const grh::Value*)> mentionsPort =
            [&](const grh::Value* node, const grh::Value* port) -> bool {
            if (!node) return false;
            if (node == port) return true;
            if (const grh::Operation* dop = node->definingOp()) {
                for (const grh::Value* operand : dop->operands()) {
                    if (mentionsPort(operand, port)) return true;
                }
            }
            return false;
        };
        if (!mentionsPort(op->operands()[1], en) || !mentionsPort(op->operands()[1], d)) {
            return fail("seq_stage23_assert_guard condition missing guard/data references");
        }
        if (!expectStringAttr(*op, "message", "bad d")) {
            return fail("seq_stage23_assert_guard message missing");
        }
    }

    // 23.3 comb assert warning only
    if (const grh::Graph* g23_3 = fetchGraph("comb_stage23_assert_warning")) {
        std::vector<const grh::Operation*> asserts =
            collectOpsByKind(*g23_3, grh::OperationKind::kAssert);
        if (!asserts.empty()) {
            return fail("comb_stage23_assert_warning should not emit kAssert");
        }
    }
    // 20.2 foreach + static break: lower 4 bits from d, upper 4 bits hold from Q
    if (const grh::Graph* g20_2 = fetchGraph("seq_stage20_foreach_partial")) {
        const grh::Value* clk = findPort(*g20_2, "clk", /*isInput=*/true);
        const grh::Value* d   = findPort(*g20_2, "d",   /*isInput=*/true);
        if (!clk || !d) {
            return fail("seq_stage20_foreach_partial missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage20_foreach_partial");
        if (!inst) {
            return fail("seq_stage20_foreach_partial instance missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp || r->stateOp->kind() != grh::OperationKind::kRegister) {
            return fail("seq_stage20_foreach_partial r is not kRegister");
        }
        if (r->stateOp->operands().size() < 2 || r->stateOp->operands().front() != clk) {
            return fail("seq_stage20_foreach_partial clock binding error");
        }
        const grh::Value* data = r->stateOp->operands().back();
        const grh::Operation* concat = data ? data->definingOp() : nullptr;
        if (!concat || concat->kind() != grh::OperationKind::kConcat || concat->operands().size() != 2) {
            return fail("seq_stage20_foreach_partial data is not kConcat");
        }
        // hi operand should be hold slice of Q[7:4]
        const grh::Value* hi = concat->operands()[0];
        const grh::Operation* hiSlice = hi ? hi->definingOp() : nullptr;
        if (!hiSlice || hiSlice->kind() != grh::OperationKind::kSliceStatic ||
            hiSlice->operands().size() != 1 || hiSlice->operands().front() != r->value) {
            return fail("seq_stage20_foreach_partial high hold slice incorrect");
        }
        if (!expectAttrs(*hiSlice, "sliceStart", 4) || !expectAttrs(*hiSlice, "sliceEnd", 7)) {
            return fail("seq_stage20_foreach_partial high slice attributes incorrect");
        }
        // lo operand should mention d (source of bits [3:0])
        const grh::Value* lo = concat->operands()[1];
        std::function<bool(const grh::Value*, const grh::Value*)> mentionsPort2 =
            [&](const grh::Value* node, const grh::Value* port) -> bool {
            if (!node) return false;
            if (node == port) return true;
            if (const grh::Operation* op = node->definingOp()) {
                for (const grh::Value* operand : op->operands()) {
                    if (mentionsPort2(operand, port)) return true;
                }
            }
            return false;
        };
        if (!mentionsPort2(lo, d)) {
            return fail("seq_stage20_foreach_partial low concat input does not reference d");
        }
    }

    // 20.3 for with memory writes: expect two kMemoryWritePort and one kMemorySyncReadPort
    if (const grh::Graph* g20_3 = fetchGraph("seq_stage20_for_memory")) {
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage20_for_memory");
        if (!inst) {
            return fail("seq_stage20_for_memory instance not found");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage20_for_memory mem not found or mem not kMemory");
        }
        const std::string memSymbol = mem->stateOp->symbol();
        int wrCount = 0;
        int rdCount = 0;
        for (const auto& opSymbol : g20_3->operationOrder()) {
            const grh::Operation& op = g20_3->getOperation(opSymbol);
            if (op.kind() == grh::OperationKind::kMemoryWritePort) {
                auto it = op.attributes().find("memSymbol");
                if (it != op.attributes().end() && std::holds_alternative<std::string>(it->second) &&
                    std::get<std::string>(it->second) == memSymbol) {
                    wrCount++;
                }
            }
            else if (op.kind() == grh::OperationKind::kMemorySyncReadPort) {
                auto it = op.attributes().find("memSymbol");
                if (it != op.attributes().end() && std::holds_alternative<std::string>(it->second) &&
                    std::get<std::string>(it->second) == memSymbol) {
                    rdCount++;
                }
            }
        }
        if (wrCount != 2 || rdCount != 1) {
            return fail("seq_stage20_for_memory expected 2 write ports and 1 sync read port");
        }
    }

    // -----------------------
    // Stage27: memory addr/clkPolarity normalization
    // -----------------------
    if (const grh::Graph* g27 = fetchGraph("seq_stage27_mem_addr")) {
        const grh::Value* clk = findPort(*g27, "clk", /*isInput=*/true);
        if (!clk) {
            return fail("seq_stage27_mem_addr missing clk port");
        }
        const auto* inst =
            findInstanceByName(compilation->getRoot().topInstances, "seq_stage27_mem_addr");
        if (!inst) {
            return fail("seq_stage27_mem_addr instance not found");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage27_mem_addr mem not found or not kMemory");
        }
        const std::string memSymbol = mem->stateOp->symbol();
        const grh::Operation* wr =
            findMemoryOp(*g27, grh::OperationKind::kMemoryWritePort, memSymbol);
        const grh::Operation* mwr =
            findMemoryOp(*g27, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
        const grh::Operation* rd =
            findMemoryOp(*g27, grh::OperationKind::kMemorySyncReadPort, memSymbol);
        if (!wr || !mwr || !rd) {
            return fail("seq_stage27_mem_addr expected write/mask/read ports");
        }
        auto expectAddrShape = [&](const grh::Operation& op, std::string_view label) -> bool {
            if (op.operands().size() < 2) {
                std::string msg(label);
                msg.append(" has insufficient operands");
                return fail(msg);
            }
            const grh::Value* addr = op.operands()[1];
            if (!addr || addr->width() != 7 || addr->isSigned()) {
                std::string msg(label);
                msg.append(" addr width/sign mismatch");
                return fail(msg);
            }
            return true;
        };
        if (!expectStringAttr(*wr, "clkPolarity", "posedge") ||
            !expectStringAttr(*mwr, "clkPolarity", "posedge") ||
            !expectStringAttr(*rd, "clkPolarity", "posedge")) {
            return fail("seq_stage27_mem_addr clkPolarity missing on memory ports");
        }
        if (!expectAddrShape(*wr, "write port") || !expectAddrShape(*mwr, "mask write port") ||
            !expectAddrShape(*rd, "sync read port")) {
            return 1;
        }
    }

    // -----------------------
    // Stage29: memory ports with reset
    // -----------------------
    if (const grh::Graph* g29_arst = fetchGraph("seq_stage29_arst_mem")) {
        const grh::Value* clk = findPort(*g29_arst, "clk", /*isInput=*/true);
        const grh::Value* rst_n = findPort(*g29_arst, "rst_n", /*isInput=*/true);
        if (!clk || !rst_n) {
            return fail("seq_stage29_arst_mem missing clk/rst_n ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage29_arst_mem");
        if (!inst) {
            return fail("seq_stage29_arst_mem instance missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage29_arst_mem mem not found or not kMemory");
        }
        const std::string memSymbol = mem->stateOp->symbol();

        auto writes = collectMemoryOps(*g29_arst, grh::OperationKind::kMemoryWritePortArst, memSymbol);
        auto masks = collectMemoryOps(*g29_arst, grh::OperationKind::kMemoryMaskWritePortArst, memSymbol);
        auto reads = collectMemoryOps(*g29_arst, grh::OperationKind::kMemorySyncReadPortArst, memSymbol);
        if (writes.size() != 2 || masks.size() != 2 || reads.size() != 1) {
            return fail("seq_stage29_arst_mem expected 2 write, 2 mask write, 1 read port");
        }
        for (const grh::Operation* op : writes) {
            if (op->operands().size() < 5 || op->operands()[0] != clk || op->operands()[1] != rst_n) {
                return fail("seq_stage29_arst_mem write port operands mismatch");
            }
            if (!expectStringAttr(*op, "rstPolarity", "low") ||
                !expectStringAttr(*op, "enLevel", "high") ||
                !expectStringAttr(*op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_arst_mem write port attributes mismatch");
            }
        }
        for (const grh::Operation* op : masks) {
            if (op->operands().size() < 6 || op->operands()[0] != clk || op->operands()[1] != rst_n) {
                return fail("seq_stage29_arst_mem mask port operands mismatch");
            }
            if (!expectStringAttr(*op, "rstPolarity", "low") ||
                !expectStringAttr(*op, "enLevel", "high") ||
                !expectStringAttr(*op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_arst_mem mask port attributes mismatch");
            }
        }
        const grh::Operation* rd = reads.front();
        if (rd->operands().size() < 4 || rd->operands()[0] != clk || rd->operands()[1] != rst_n) {
            return fail("seq_stage29_arst_mem read port operands mismatch");
        }
        if (!expectStringAttr(*rd, "rstPolarity", "low") ||
            !expectStringAttr(*rd, "enLevel", "high") ||
            !expectStringAttr(*rd, "clkPolarity", "posedge")) {
            return fail("seq_stage29_arst_mem read port attributes mismatch");
        }
    }

    if (const grh::Graph* g29_rst = fetchGraph("seq_stage29_rst_mem")) {
        const grh::Value* clk = findPort(*g29_rst, "clk", /*isInput=*/true);
        const grh::Value* rst = findPort(*g29_rst, "rst", /*isInput=*/true);
        if (!clk || !rst) {
            return fail("seq_stage29_rst_mem missing clk/rst ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage29_rst_mem");
        if (!inst) {
            return fail("seq_stage29_rst_mem instance missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp || mem->stateOp->kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage29_rst_mem mem not found or not kMemory");
        }
        const std::string memSymbol = mem->stateOp->symbol();

        auto writes = collectMemoryOps(*g29_rst, grh::OperationKind::kMemoryWritePortRst, memSymbol);
        auto masks = collectMemoryOps(*g29_rst, grh::OperationKind::kMemoryMaskWritePortRst, memSymbol);
        auto reads = collectMemoryOps(*g29_rst, grh::OperationKind::kMemorySyncReadPortRst, memSymbol);
        if (writes.size() != 2 || masks.size() != 2 || reads.size() != 1) {
            return fail("seq_stage29_rst_mem expected 2 write, 2 mask write, 1 read port");
        }
        for (const grh::Operation* op : writes) {
            if (op->operands().size() < 5 || op->operands()[0] != clk || op->operands()[1] != rst) {
                return fail("seq_stage29_rst_mem write port operands mismatch");
            }
            if (!expectStringAttr(*op, "rstPolarity", "high") ||
                !expectStringAttr(*op, "enLevel", "high") ||
                !expectStringAttr(*op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_rst_mem write port attributes mismatch");
            }
        }
        for (const grh::Operation* op : masks) {
            if (op->operands().size() < 6 || op->operands()[0] != clk || op->operands()[1] != rst) {
                return fail("seq_stage29_rst_mem mask port operands mismatch");
            }
            if (!expectStringAttr(*op, "rstPolarity", "high") ||
                !expectStringAttr(*op, "enLevel", "high") ||
                !expectStringAttr(*op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_rst_mem mask port attributes mismatch");
            }
        }
        const grh::Operation* rd = reads.front();
        if (rd->operands().size() < 4 || rd->operands()[0] != clk || rd->operands()[1] != rst) {
            return fail("seq_stage29_rst_mem read port operands mismatch");
        }
        if (!expectStringAttr(*rd, "rstPolarity", "high") ||
            !expectStringAttr(*rd, "enLevel", "high") ||
            !expectStringAttr(*rd, "clkPolarity", "posedge")) {
                return fail("seq_stage29_rst_mem read port attributes mismatch");
        }
    }

    return 0;
}
