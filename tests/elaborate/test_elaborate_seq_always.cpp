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

ValueId findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    const auto ports = isInput ? graph.inputPorts() : graph.outputPorts();
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
}

OperationId findMemoryOp(const grh::Graph& graph, grh::OperationKind kind,
                         std::string_view memSymbol) {
    const grh::ir::SymbolId memKey = graph.lookupSymbol("memSymbol");
    for (const auto& opId : graph.operations()) {
        const grh::Operation op = graph.getOperation(opId);
        if (op.kind() != kind) {
            continue;
        }
        if (!memKey.valid()) {
            continue;
        }
        auto attr = op.attr(memKey);
        const std::string* symbol = attr ? std::get_if<std::string>(&*attr) : nullptr;
        if (symbol && *symbol == memSymbol) {
            return opId;
        }
    }
    return OperationId::invalid();
}

std::vector<OperationId> collectMemoryOps(const grh::Graph& graph,
                                          grh::OperationKind kind,
                                          std::string_view memSymbol) {
    std::vector<OperationId> ops;
    const grh::ir::SymbolId memKey = graph.lookupSymbol("memSymbol");
    for (const auto& opId : graph.operations()) {
        const grh::Operation op = graph.getOperation(opId);
        if (op.kind() != kind) {
            continue;
        }
        if (!memKey.valid()) {
            continue;
        }
        auto attr = op.attr(memKey);
        const std::string* symbol = attr ? std::get_if<std::string>(&*attr) : nullptr;
        if (symbol && *symbol == memSymbol) {
            ops.push_back(opId);
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

bool expectAttrs(const grh::Graph& graph, const grh::Operation& op, std::string_view key,
                 int64_t value) {
    const grh::ir::SymbolId keyId = graph.lookupSymbol(key);
    if (!keyId.valid()) {
        return false;
    }
    auto attr = op.attr(keyId);
    if (!attr) {
        return false;
    }
    const int64_t* attrValue = std::get_if<int64_t>(&*attr);
    return attrValue && *attrValue == value;
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

std::vector<OperationId> collectOpsByKind(const grh::Graph& graph, grh::OperationKind kind) {
    std::vector<OperationId> result;
    for (const auto& opId : graph.operations()) {
        const grh::Operation op = graph.getOperation(opId);
        if (op.kind() == kind) {
            result.push_back(opId);
        }
    }
    return result;
}

bool expectStringAttr(const grh::Graph& graph, const grh::Operation& op, std::string_view key,
                      std::string_view expect) {
    const grh::ir::SymbolId keyId = graph.lookupSymbol(key);
    if (!keyId.valid()) {
        return false;
    }
    auto attr = op.attr(keyId);
    if (!attr) {
        return false;
    }
    const std::string* value = std::get_if<std::string>(&*attr);
    return value && *value == expect;
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

    ValueId clkPort = findPort(*graph, "clk", /*isInput=*/true);
    ValueId rstPort = findPort(*graph, "rst_n", /*isInput=*/true);
    ValueId rstSyncPort = findPort(*graph, "rst_sync", /*isInput=*/true);
    ValueId loPort = findPort(*graph, "lo_data", /*isInput=*/true);
    ValueId hiPort = findPort(*graph, "hi_data", /*isInput=*/true);
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
        if (!entry.stateOp) {
            fail("Register state operation missing or has wrong kind");
            return false;
        }
        const grh::Operation op = graph->getOperation(entry.stateOp);
        if (op.kind() != kind) {
            fail("Register state operation missing or has wrong kind");
            return false;
        }
        if (op.results().size() != 1 || op.results().front() != entry.value) {
            fail("Register state op result does not match memo value");
            return false;
        }
        if (op.operands().size() != operandCount) {
            fail("Register operand count mismatch");
            return false;
        }
        if (op.operands().empty() || op.operands().front() != clkPort) {
            fail("Register clock operand is not bound to clk port");
            return false;
        }
        ValueId dataOperand = op.operands().back();
        if (!dataOperand || graph->getValue(dataOperand).width() != entry.width) {
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

    auto verifyConcat = [&](const SignalMemoEntry& entry, ValueId expectedHi,
                            ValueId expectedLo) -> bool {
        const grh::Operation op = graph->getOperation(entry.stateOp);
        ValueId dataValue = op.operands().back();
        OperationId concatId = dataValue ? graph->getValue(dataValue).definingOp()
                                         : OperationId::invalid();
        if (!concatId) {
            return fail("Expected register data to be driven by kConcat");
        }
        const grh::Operation concatOp = graph->getOperation(concatId);
        if (concatOp.kind() != grh::OperationKind::kConcat) {
            return fail("Expected register data to be driven by kConcat");
        }
        if (concatOp.operands().size() != 2) {
            return fail("Concat operand count mismatch");
        }
        if (concatOp.operands()[0] != expectedHi || concatOp.operands()[1] != expectedLo) {
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
    const grh::Operation partialOp = graph->getOperation(regPartial->stateOp);
    ValueId partialData = partialOp.operands().back();
    OperationId partialConcatId = partialData ? graph->getValue(partialData).definingOp()
                                              : OperationId::invalid();
    if (!partialConcatId) {
        return fail("reg_partial data is not driven by kConcat");
    }
    const grh::Operation partialConcat = graph->getOperation(partialConcatId);
    if (partialConcat.kind() != grh::OperationKind::kConcat) {
        return fail("reg_partial data is not driven by kConcat");
    }
    if (partialConcat.operands().size() != 2) {
        return fail("reg_partial concat operand count mismatch");
    }

    ValueId holdValue = partialConcat.operands()[0];
    ValueId rhsValue = partialConcat.operands()[1];
    if (rhsValue != loPort) {
        return fail("reg_partial low bits are not sourced from lo_data");
    }

    OperationId holdSliceId = holdValue ? graph->getValue(holdValue).definingOp()
                                        : OperationId::invalid();
    if (!holdSliceId) {
        return fail("reg_partial high bits are not provided by a kSliceStatic over Q");
    }
    const grh::Operation holdSlice = graph->getOperation(holdSliceId);
    if (holdSlice.kind() != grh::OperationKind::kSliceStatic) {
        return fail("reg_partial high bits are not provided by a kSliceStatic over Q");
    }
    if (holdSlice.operands().size() != 1 ||
        holdSlice.operands().front() != regPartial->value) {
        return fail("reg_partial slice does not target the register's Q output");
    }
    if (!expectAttrs(*graph, holdSlice, "sliceStart", 4) ||
        !expectAttrs(*graph, holdSlice, "sliceEnd", 7)) {
        return fail("reg_partial slice attributes are incorrect");
    }

    std::function<bool(const grh::Graph&, ValueId, ValueId)> mentionsPort =
        [&](const grh::Graph& g, ValueId node, ValueId port) -> bool {
        if (!node) {
            return false;
        }
        if (node == port) {
            return true;
        }
        OperationId opId = g.getValue(node).definingOp();
        if (!opId) {
            return false;
        }
        const grh::Operation op = g.getOperation(opId);
        for (ValueId operand : op.operands()) {
            if (mentionsPort(g, operand, port)) {
                return true;
            }
        }
        return false;
    };

    auto isZeroConstant = [&](const grh::Graph& g, ValueId value) -> bool {
        if (!value) {
            return false;
        }
        OperationId opId = g.getValue(value).definingOp();
        if (!opId) {
            return false;
        }
        const grh::Operation op = g.getOperation(opId);
        if (op.kind() != grh::OperationKind::kConstant) {
            return false;
        }
        const grh::ir::SymbolId constValueKey = g.lookupSymbol("constValue");
        if (!constValueKey.valid()) {
            return false;
        }
        auto attr = op.attr(constValueKey);
        const std::string* literal = attr ? std::get_if<std::string>(&*attr) : nullptr;
        if (!literal) {
            return false;
        }
        return literal->find("'h0") != std::string::npos ||
               literal->find("'d0") != std::string::npos ||
               literal->find("'b0") != std::string::npos;
    };

    const grh::Operation regSyncOp = graph->getOperation(regSync->stateOp);
    ValueId syncData = regSyncOp.operands().back();
    OperationId dataOpId = syncData ? graph->getValue(syncData).definingOp()
                                    : OperationId::invalid();
    if (!dataOpId) {
        return fail("reg_sync_rst data is missing defining operation");
    }
    const grh::Operation dataOp = graph->getOperation(dataOpId);
    if (dataOp.kind() == grh::OperationKind::kMux) {
        // 图中仍保留 mux 表达 reset：走原有校验路径
        if (dataOp.operands().size() != 3) {
            return fail("reg_sync_rst mux operand count mismatch");
        }
        if (!mentionsPort(*graph, dataOp.operands().front(), rstSyncPort)) {
            return fail("reg_sync_rst mux condition does not reference rst_sync");
        }
        if (!isZeroConstant(*graph, dataOp.operands()[1])) {
            return fail("reg_sync_rst reset value is not zero");
        }
        OperationId syncConcatId =
            dataOp.operands()[2] ? graph->getValue(dataOp.operands()[2]).definingOp()
                                 : OperationId::invalid();
        if (!syncConcatId) {
            return fail("reg_sync_rst data path is not driven by concat");
        }
        const grh::Operation syncConcat = graph->getOperation(syncConcatId);
        if (syncConcat.kind() != grh::OperationKind::kConcat) {
            return fail("reg_sync_rst data path is not driven by concat");
        }
        if (syncConcat.operands().size() != 2 ||
            syncConcat.operands()[0] != hiPort || syncConcat.operands()[1] != loPort) {
            return fail("reg_sync_rst concat operands do not match hi/lo data");
        }
    } else {
        // 允许阶段21抽取 reset 后，data 直接为 concat(hi, lo)
        const grh::Operation syncConcat = dataOp;
        if (syncConcat.kind() != grh::OperationKind::kConcat) {
            return fail("reg_sync_rst data is not driven by kMux");
        }
        if (syncConcat.operands().size() != 2 ||
            syncConcat.operands()[0] != hiPort || syncConcat.operands()[1] != loPort) {
            return fail("reg_sync_rst concat operands do not match hi/lo data");
        }
    }

    auto checkResetOperands = [&](const SignalMemoEntry& entry, ValueId expectedSignal,
                                  std::string_view expectLevel) -> bool {
        if (!entry.stateOp) {
            fail("Reset state operation missing");
            return false;
        }
        const grh::Operation op = graph->getOperation(entry.stateOp);
        if (!expectStringAttr(*graph, op, "rstPolarity", expectLevel)) {
            fail("rstPolarity attribute mismatch");
            return false;
        }
        if (op.operands().size() < 3) {
            fail("Reset operands missing");
            return false;
        }
        if (op.operands()[1] != expectedSignal) {
            fail("Reset operand does not reference expected signal");
            return false;
        }
        if (!isZeroConstant(*graph, op.operands()[2])) {
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

    ValueId clk18 = findPort(*graph18, "clk", /*isInput=*/true);
    ValueId wrAddr = findPort(*graph18, "wr_addr", /*isInput=*/true);
    ValueId rdAddr = findPort(*graph18, "rd_addr", /*isInput=*/true);
    ValueId maskAddr = findPort(*graph18, "mask_addr", /*isInput=*/true);
    ValueId bitIndex = findPort(*graph18, "bit_index", /*isInput=*/true);
    ValueId bitValue = findPort(*graph18, "bit_value", /*isInput=*/true);
    ValueId rdDataOut = findPort(*graph18, "rd_data", /*isInput=*/false);
    if (!clk18 || !wrAddr || !rdAddr || !maskAddr || !bitIndex || !bitValue || !rdDataOut) {
        return fail("seq_stage18 ports are missing");
    }

    std::span<const SignalMemoEntry> regMemo18 = elaborator.peekRegMemo(fetchBody(*inst18));
    const SignalMemoEntry* memEntry = findEntry(regMemo18, "mem");
    const SignalMemoEntry* rdRegEntry = findEntry(regMemo18, "rd_reg");
    if (!memEntry || !rdRegEntry) {
        return fail("seq_stage18 memo entries missing mem or rd_reg");
    }
    if (!memEntry->stateOp) {
        return fail("seq_stage18 mem entry lacks kMemory state op");
    }
    const grh::Operation memOp18 = graph18->getOperation(memEntry->stateOp);
    if (memOp18.kind() != grh::OperationKind::kMemory) {
        return fail("seq_stage18 mem entry lacks kMemory state op");
    }

    const std::string memSymbol(memOp18.symbolText());

    OperationId syncReadId =
        findMemoryOp(*graph18, grh::OperationKind::kMemorySyncReadPort, memSymbol);
    if (!syncReadId) {
        return fail("kMemorySyncReadPort not found for seq_stage18");
    }
    const grh::Operation syncRead = graph18->getOperation(syncReadId);
    if (syncRead.operands().size() != 3 || syncRead.operands()[0] != clk18 ||
        syncRead.operands()[1] != rdAddr) {
        return fail("Memory sync read operands are incorrect");
    }
    ValueId syncReadEn = syncRead.operands()[2];
    OperationId syncReadEnOpId =
        syncReadEn ? graph18->getValue(syncReadEn).definingOp() : OperationId::invalid();
    if (!syncReadEnOpId) {
        return fail("Memory sync read enable is not tied to constant one");
    }
    const grh::Operation syncReadEnOp = graph18->getOperation(syncReadEnOpId);
    if (syncReadEnOp.kind() != grh::OperationKind::kConstant) {
        return fail("Memory sync read enable is not tied to constant one");
    }
    if (syncRead.results().size() != 1 ||
        graph18->getValue(syncRead.results().front()).width() != 8) {
        return fail("Memory sync read result width mismatch");
    }
    const grh::Operation rdRegOp = graph18->getOperation(rdRegEntry->stateOp);
    if (rdRegOp.operands().empty() ||
        rdRegOp.operands().back() != syncRead.results().front()) {
        return fail("rd_reg data input is not driven by the sync read port");
    }
    OperationId writePortId =
        findMemoryOp(*graph18, grh::OperationKind::kMemoryWritePort, memSymbol);
    if (!writePortId) {
        return fail("kMemoryWritePort not found for seq_stage18");
    }
    const grh::Operation writePort = graph18->getOperation(writePortId);
    if (writePort.operands().size() != 4 || writePort.operands()[0] != clk18 ||
        writePort.operands()[1] != wrAddr) {
        return fail("Memory write port operands mismatched");
    }
    ValueId writeEn = writePort.operands()[2];
    OperationId writeEnOpId =
        writeEn ? graph18->getValue(writeEn).definingOp() : OperationId::invalid();
    if (!writeEnOpId) {
        return fail("Memory write port enable should be constant one");
    }
    const grh::Operation writeEnOp = graph18->getOperation(writeEnOpId);
    if (writeEnOp.kind() != grh::OperationKind::kConstant) {
        return fail("Memory write port enable should be constant one");
    }
    if (graph18->getValue(writePort.operands()[3]).width() != 8) {
        return fail("Memory write port data width mismatch");
    }

    OperationId maskPortId =
        findMemoryOp(*graph18, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
    if (!maskPortId) {
        return fail("kMemoryMaskWritePort not found for seq_stage18");
    }
    const grh::Operation maskPort = graph18->getOperation(maskPortId);
    if (maskPort.operands().size() != 5 || maskPort.operands()[0] != clk18 ||
        maskPort.operands()[1] != maskAddr) {
        return fail("Memory mask write operands mismatched");
    }
    ValueId maskEn = maskPort.operands()[2];
    OperationId maskEnOpId =
        maskEn ? graph18->getValue(maskEn).definingOp() : OperationId::invalid();
    if (!maskEnOpId) {
        return fail("Memory mask write enable should be constant one");
    }
    const grh::Operation maskEnOp = graph18->getOperation(maskEnOpId);
    if (maskEnOp.kind() != grh::OperationKind::kConstant) {
        return fail("Memory mask write enable should be constant one");
    }
    if (graph18->getValue(maskPort.operands()[3]).width() != 8 ||
        graph18->getValue(maskPort.operands()[4]).width() != 8) {
        return fail("Memory mask write data/mask widths mismatch");
    }
    ValueId dataShiftValue = maskPort.operands()[3];
    ValueId maskShiftValue = maskPort.operands()[4];
    OperationId dataShiftId =
        dataShiftValue ? graph18->getValue(dataShiftValue).definingOp() : OperationId::invalid();
    OperationId maskShiftId =
        maskShiftValue ? graph18->getValue(maskShiftValue).definingOp() : OperationId::invalid();
    if (!dataShiftId) {
        return fail("Memory mask write data path is not shifted by bit_index");
    }
    const grh::Operation dataShift = graph18->getOperation(dataShiftId);
    if (dataShift.kind() != grh::OperationKind::kShl ||
        dataShift.operands().size() != 2 || dataShift.operands()[1] != bitIndex) {
        return fail("Memory mask write data path is not shifted by bit_index");
    }
    OperationId dataConcatId = dataShift.operands()[0]
                                   ? graph18->getValue(dataShift.operands()[0]).definingOp()
                                   : OperationId::invalid();
    if (!dataConcatId) {
        return fail("Memory mask write data concat does not source bit_value");
    }
    const grh::Operation dataConcat = graph18->getOperation(dataConcatId);
    if (dataConcat.kind() != grh::OperationKind::kConcat ||
        dataConcat.operands().size() != 2 || dataConcat.operands()[1] != bitValue) {
        return fail("Memory mask write data concat does not source bit_value");
    }
    if (!maskShiftId) {
        return fail("Memory mask write mask is not shifted by bit_index");
    }
    const grh::Operation maskShift = graph18->getOperation(maskShiftId);
    if (maskShift.kind() != grh::OperationKind::kShl ||
        maskShift.operands().size() != 2 || maskShift.operands()[1] != bitIndex) {
        return fail("Memory mask write mask is not shifted by bit_index");
    }
    OperationId maskConstId = maskShift.operands()[0]
                                  ? graph18->getValue(maskShift.operands()[0]).definingOp()
                                  : OperationId::invalid();
    if (!maskConstId) {
        return fail("Memory mask base should be a constant one-hot literal");
    }
    const grh::Operation maskConst = graph18->getOperation(maskConstId);
    if (maskConst.kind() != grh::OperationKind::kConstant) {
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
        ValueId clk = findPort(*g19_1, "clk", /*isInput=*/true);
        ValueId en  = findPort(*g19_1, "en", /*isInput=*/true);
        ValueId d   = findPort(*g19_1, "d", /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage19_if_en_reg missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_if_en_reg");
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage19_if_en_reg missing stateOp");
        }
        const grh::Operation stateOp = g19_1->getOperation(r->stateOp);
        if (stateOp.kind() == grh::OperationKind::kRegister) {
            if (stateOp.operands().size() != 2 || stateOp.operands().front() != clk) {
                return fail("seq_stage19_if_en_reg clock binding error");
            }
            ValueId data = stateOp.operands().back();
            OperationId muxId = data ? g19_1->getValue(data).definingOp()
                                     : OperationId::invalid();
            if (!muxId) {
                return fail("seq_stage19_if_en_reg data is not a kMux");
            }
            const grh::Operation mux = g19_1->getOperation(muxId);
            if (mux.kind() != grh::OperationKind::kMux || mux.operands().size() != 3) {
                return fail("seq_stage19_if_en_reg data is not a kMux");
            }
            if (!mentionsPort(*g19_1, mux.operands().front(), en)) {
                return fail("seq_stage19_if_en_reg mux condition does not reference en");
            }
            // True branch should be driven by d, false branch by Q (hold).
            if (mux.operands()[1] != d) {
                return fail("seq_stage19_if_en_reg mux true branch is not d");
            }
            if (mux.operands()[2] != r->value) {
                return fail("seq_stage19_if_en_reg mux false branch is not hold(Q)");
            }
        } else if (stateOp.kind() == grh::OperationKind::kRegisterEn) {
            // Stage21+：允许被特化为带使能原语
            if (stateOp.operands().size() != 3 || stateOp.operands().front() != clk) {
                return fail("seq_stage19_if_en_reg kRegisterEn operand mismatch");
            }
            // 使能操作数应当能“提及”en 端口（可能存在归一化/取反等）
            if (!mentionsPort(*g19_1, stateOp.operands()[1], en)) {
                return fail("seq_stage19_if_en_reg kRegisterEn enable does not mention en");
            }
            if (stateOp.operands()[2] != d) {
                return fail("seq_stage19_if_en_reg kRegisterEn data is not d");
            }
        } else {
            return fail("seq_stage19_if_en_reg unexpected register kind");
        }
    }

    // 19.2 if-en gated memory read/write/mask
    if (const grh::Graph* g19_2 = fetchGraph("seq_stage19_if_en_mem")) {
        ValueId clk      = findPort(*g19_2, "clk", /*isInput=*/true);
        ValueId en_wr    = findPort(*g19_2, "en_wr", /*isInput=*/true);
        ValueId en_bit   = findPort(*g19_2, "en_bit", /*isInput=*/true);
        ValueId en_rd    = findPort(*g19_2, "en_rd", /*isInput=*/true);
        ValueId wr_addr  = findPort(*g19_2, "wr_addr", /*isInput=*/true);
        ValueId rd_addr  = findPort(*g19_2, "rd_addr", /*isInput=*/true);
        ValueId mask_addr= findPort(*g19_2, "mask_addr", /*isInput=*/true);
        ValueId bit_index= findPort(*g19_2, "bit_index", /*isInput=*/true);
        if (!clk || !en_wr || !en_bit || !en_rd || !wr_addr || !rd_addr || !mask_addr || !bit_index) {
            return fail("seq_stage19_if_en_mem missing ports");
        }
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_if_en_mem");
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        const SignalMemoEntry* rd_reg = findEntry(memo, "rd_reg");
        if (!mem || !rd_reg || !mem->stateOp) {
            return fail("seq_stage19_if_en_mem mem/rd_reg not found or mem not kMemory");
        }
        const grh::Operation memOp = g19_2->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage19_if_en_mem mem/rd_reg not found or mem not kMemory");
        }
        const std::string memSymbol(memOp.symbolText());
        OperationId wrId =
            findMemoryOp(*g19_2, grh::OperationKind::kMemoryWritePort, memSymbol);
        OperationId mwrId =
            findMemoryOp(*g19_2, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
        OperationId rdId =
            findMemoryOp(*g19_2, grh::OperationKind::kMemorySyncReadPort, memSymbol);
        if (!wrId || !mwrId || !rdId) {
            return fail("seq_stage19_if_en_mem expected memory ports not found");
        }
        const grh::Operation wr = g19_2->getOperation(wrId);
        const grh::Operation mwr = g19_2->getOperation(mwrId);
        const grh::Operation rd = g19_2->getOperation(rdId);
        if (wr.operands().size() != 4 || wr.operands()[0] != clk || wr.operands()[1] != wr_addr) {
            return fail("seq_stage19_if_en_mem write port operand mismatch");
        }
        if (!mentionsPort(*g19_2, wr.operands()[2], en_wr)) {
            return fail("seq_stage19_if_en_mem write enable does not mention en_wr");
        }
        if (mwr.operands().size() != 5 || mwr.operands()[0] != clk ||
            mwr.operands()[1] != mask_addr) {
            return fail("seq_stage19_if_en_mem mask write operand mismatch");
        }
        if (!mentionsPort(*g19_2, mwr.operands()[2], en_bit)) {
            return fail("seq_stage19_if_en_mem mask write enable does not mention en_bit");
        }
        if (rd.operands().size() != 3 || rd.operands()[0] != clk ||
            rd.operands()[1] != rd_addr) {
            return fail("seq_stage19_if_en_mem sync read operand mismatch");
        }
        if (!mentionsPort(*g19_2, rd.operands()[2], en_rd)) {
            return fail("seq_stage19_if_en_mem sync read enable does not mention en_rd");
        }
        const grh::Operation rdRegOp = g19_2->getOperation(rd_reg->stateOp);
        if (rdRegOp.operands().empty()) {
            return fail("seq_stage19_if_en_mem rd_reg missing data operand");
        }
        ValueId rdData = rdRegOp.operands().back();
        if (rdData != rd.results().front()) {
            // Allow gated data path: mux(en_rd, rd_result, Q)
            OperationId mId = rdData ? g19_2->getValue(rdData).definingOp()
                                     : OperationId::invalid();
            if (!mId) {
                return fail("seq_stage19_if_en_mem rd_reg not driven by sync read or mux");
            }
            const grh::Operation m = g19_2->getOperation(mId);
            if (m.kind() != grh::OperationKind::kMux || m.operands().size() != 3) {
                return fail("seq_stage19_if_en_mem rd_reg not driven by sync read or mux");
            }
            if (!mentionsPort(*g19_2, m.operands().front(), en_rd)) {
                return fail("seq_stage19_if_en_mem mux condition does not reference en_rd");
            }
            if (m.operands()[1] != rd.results().front() || m.operands()[2] != rd_reg->value) {
                return fail("seq_stage19_if_en_mem mux branches are not (rd_result, hold(Q))");
            }
        }
    }

    // 19.3 case(sel) branches -> write/mask enables
    if (const grh::Graph* g19_3 = fetchGraph("seq_stage19_case_mem")) {
        ValueId clk = findPort(*g19_3, "clk", /*isInput=*/true);
        ValueId sel = findPort(*g19_3, "sel", /*isInput=*/true);
        ValueId addr = findPort(*g19_3, "addr", /*isInput=*/true);
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_case_mem");
        if (!clk || !sel || !addr || !inst) {
            return fail("seq_stage19_case_mem ports missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp) {
            return fail("seq_stage19_case_mem mem not found");
        }
        const grh::Operation memOp = g19_3->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage19_case_mem mem not found");
        }
        const std::string memSymbol(memOp.symbolText());
        OperationId wrId =
            findMemoryOp(*g19_3, grh::OperationKind::kMemoryWritePort, memSymbol);
        OperationId mwrId =
            findMemoryOp(*g19_3, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
        if (!wrId || !mwrId) {
            return fail("seq_stage19_case_mem expected write/mask ports missing");
        }
        const grh::Operation wr = g19_3->getOperation(wrId);
        const grh::Operation mwr = g19_3->getOperation(mwrId);
        // write enable should reference sel and equal sel==0
        ValueId wrEn = wr.operands().size() > 2 ? wr.operands()[2] : ValueId::invalid();
        OperationId wrEnOpId = wrEn ? g19_3->getValue(wrEn).definingOp()
                                    : OperationId::invalid();
        if (!wrEn || !wrEnOpId) {
            return fail("seq_stage19_case_mem write enable is not eq(sel, const)");
        }
        const grh::Operation wrEnOp = g19_3->getOperation(wrEnOpId);
        if (wrEnOp.kind() != grh::OperationKind::kEq) {
            return fail("seq_stage19_case_mem write enable is not eq(sel, const)");
        }
        if (!mentionsPort(*g19_3, wrEnOp.operands().front(), sel) &&
            !mentionsPort(*g19_3, wrEnOp.operands().back(), sel)) {
            return fail("seq_stage19_case_mem write enable does not reference sel");
        }
        // mask write enable should reference sel and equal sel==1
        ValueId mwrEn = mwr.operands().size() > 2 ? mwr.operands()[2] : ValueId::invalid();
        OperationId mwrEnOpId = mwrEn ? g19_3->getValue(mwrEn).definingOp()
                                      : OperationId::invalid();
        if (!mwrEn || !mwrEnOpId) {
            return fail("seq_stage19_case_mem mask write enable is not eq(sel, const)");
        }
        const grh::Operation mwrEnOp = g19_3->getOperation(mwrEnOpId);
        if (mwrEnOp.kind() != grh::OperationKind::kEq) {
            return fail("seq_stage19_case_mem mask write enable is not eq(sel, const)");
        }
        if (!mentionsPort(*g19_3, mwrEnOp.operands().front(), sel) &&
            !mentionsPort(*g19_3, mwrEnOp.operands().back(), sel)) {
            return fail("seq_stage19_case_mem mask write enable does not reference sel");
        }
    }

    // 19.4 casez wildcard: two writes, each enable references sel with wildcard logic
    if (const grh::Graph* g19_4 = fetchGraph("seq_stage19_casez_mem")) {
        ValueId clk = findPort(*g19_4, "clk", /*isInput=*/true);
        ValueId sel = findPort(*g19_4, "sel", /*isInput=*/true);
        ValueId addr = findPort(*g19_4, "addr", /*isInput=*/true);
        const auto* inst = findInstanceByName(compilation->getRoot().topInstances, "seq_stage19_casez_mem");
        if (!clk || !sel || !addr || !inst) {
            return fail("seq_stage19_casez_mem ports missing");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* mem = findEntry(memo, "mem");
        if (!mem || !mem->stateOp) {
            return fail("seq_stage19_casez_mem mem not found");
        }
        const grh::Operation memOp = g19_4->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage19_casez_mem mem not found");
        }
        const std::string memSymbol(memOp.symbolText());
        // Collect all write ports for this mem
        std::vector<OperationId> writes;
        const grh::ir::SymbolId memKey = g19_4->lookupSymbol("memSymbol");
        for (const auto& opId : g19_4->operations()) {
            const grh::Operation op = g19_4->getOperation(opId);
            if (op.kind() != grh::OperationKind::kMemoryWritePort) {
                continue;
            }
            auto attr = memKey.valid() ? op.attr(memKey) : std::nullopt;
            const std::string* symbol = attr ? std::get_if<std::string>(&*attr) : nullptr;
            if (symbol && *symbol == memSymbol) {
                writes.push_back(opId);
            }
        }
        if (writes.size() != 2) {
            return fail("seq_stage19_casez_mem expects two write ports");
        }
        for (OperationId wrId : writes) {
            const grh::Operation wr = g19_4->getOperation(wrId);
            if (wr.operands().size() < 3) {
                return fail("seq_stage19_casez_mem write port missing enable");
            }
            ValueId en = wr.operands()[2];
            if (!mentionsPort(*g19_4, en, sel)) {
                return fail("seq_stage19_casez_mem write enable does not reference sel");
            }
        }
    }

    // 19.5 rst + en register
    if (const grh::Graph* g19_5 = fetchGraph("seq_stage19_rst_en_reg")) {
        ValueId clk = findPort(*g19_5, "clk", /*isInput=*/true);
        ValueId rst = findPort(*g19_5, "rst", /*isInput=*/true);
        ValueId en  = findPort(*g19_5, "en",  /*isInput=*/true);
        ValueId d   = findPort(*g19_5, "d",   /*isInput=*/true);
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
        const grh::Operation stateOp = g19_5->getOperation(r->stateOp);
        if (stateOp.kind() == grh::OperationKind::kRegisterRst) {
            if (!expectStringAttr(*g19_5, stateOp, "rstPolarity", "high")) {
                return fail("seq_stage19_rst_en_reg rstPolarity attribute unexpected");
            }
            if (stateOp.operands().size() < 4 || stateOp.operands()[0] != clk ||
                stateOp.operands()[1] != rst) {
                return fail("seq_stage19_rst_en_reg clk/rst operands not bound");
            }
            if (!isZeroConstant(*g19_5, stateOp.operands()[2])) {
                return fail("seq_stage19_rst_en_reg reset value is not zero constant");
            }
            // Data path should reference en (gated assignment)
            ValueId data = stateOp.operands().back();
            if (!mentionsPort(*g19_5, data, en)) {
                return fail("seq_stage19_rst_en_reg data path does not reference en");
            }
        } else if (stateOp.kind() == grh::OperationKind::kRegisterEnRst) {
            // Stage21+：特化为带使能 + 同步复位原语
            if (!expectStringAttr(*g19_5, stateOp, "rstPolarity", "high")) {
                return fail("seq_stage19_rst_en_reg (EnRst) rstPolarity attribute unexpected");
            }
            if (!expectStringAttr(*g19_5, stateOp, "enLevel", "high")) {
                return fail("seq_stage19_rst_en_reg (EnRst) enLevel attribute unexpected");
            }
            if (stateOp.operands().size() != 5 ||
                stateOp.operands()[0] != clk ||
                stateOp.operands()[1] != rst) {
                return fail("seq_stage19_rst_en_reg (EnRst) clk/rst operands mismatch");
            }
            if (!mentionsPort(*g19_5, stateOp.operands()[2], en)) {
                return fail("seq_stage19_rst_en_reg (EnRst) enable does not mention en");
            }
            // resetValue == zero; data mentions d
            if (!isZeroConstant(*g19_5, stateOp.operands()[3])) {
                return fail("seq_stage19_rst_en_reg (EnRst) reset value is not zero");
            }
            if (!mentionsPort(*g19_5, stateOp.operands()[4], d)) {
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
        ValueId clk = findPort(*g20_1, "clk", /*isInput=*/true);
        ValueId d0  = findPort(*g20_1, "d0",  /*isInput=*/true);
        ValueId d2  = findPort(*g20_1, "d2",  /*isInput=*/true);
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
        if (!r || !r->stateOp) {
            return fail("seq_stage20_for_last_write r is not kRegister");
        }
        const grh::Operation rOp = g20_1->getOperation(r->stateOp);
        if (rOp.kind() != grh::OperationKind::kRegister) {
            return fail("seq_stage20_for_last_write r is not kRegister");
        }
        if (rOp.operands().size() < 2 || rOp.operands().front() != clk) {
            return fail("seq_stage20_for_last_write clock binding error");
        }
        ValueId data = rOp.operands().back();
        // 最终数据应直接等于 d2（最后一次写）
        if (data != d2) {
            return fail("seq_stage20_for_last_write last-write is not d2");
        }
        // 确保没有意外依赖 d0
        if (mentionsPort(*g20_1, data, d0)) {
            return fail("seq_stage20_for_last_write data should not depend on d0");
        }
    }

    // -----------------------
    // Stage22: display/write/strobe lowering
    // -----------------------

    // 22.1 basic display emits kDisplay with clk/en/var
    if (const grh::Graph* g22_1 = fetchGraph("seq_stage22_display_basic")) {
        ValueId clk = findPort(*g22_1, "clk", /*isInput=*/true);
        ValueId d = findPort(*g22_1, "d", /*isInput=*/true);
        ValueId q = findPort(*g22_1, "q", /*isInput=*/false);
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
        OperationId displayId = findOpByKind(*g22_1, grh::OperationKind::kDisplay);
        if (!displayId) {
            return fail("seq_stage22_display_basic missing kDisplay");
        }
        const grh::Operation display = g22_1->getOperation(displayId);
        if (display.operands().size() != 3) {
            return fail("seq_stage22_display_basic display operand count mismatch");
        }
        if (display.operands()[0] != clk) {
            return fail("seq_stage22_display_basic clk operand mismatch");
        }
        // enable should be constant 1; data should reference register value
        if (display.operands()[2] != rEntry->value) {
            return fail("seq_stage22_display_basic value operand mismatch");
        }
        if (!expectStringAttr(*g22_1, display, "formatString", "r=%0d")) {
            return fail("seq_stage22_display_basic formatString attribute mismatch");
        }
        if (!expectStringAttr(*g22_1, display, "displayKind", "display")) {
            return fail("seq_stage22_display_basic displayKind attribute missing");
        }
    }

    // 22.2 guarded write: enable operand should reference guard (en)
    if (const grh::Graph* g22_2 = fetchGraph("seq_stage22_guarded_write")) {
        ValueId clk = findPort(*g22_2, "clk", /*isInput=*/true);
        ValueId en = findPort(*g22_2, "en", /*isInput=*/true);
        ValueId d = findPort(*g22_2, "d", /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage22_guarded_write missing ports");
        }
        OperationId displayId = findOpByKind(*g22_2, grh::OperationKind::kDisplay);
        if (!displayId) {
            return fail("seq_stage22_guarded_write missing kDisplay");
        }
        const grh::Operation display = g22_2->getOperation(displayId);
        if (display.operands().size() != 4) {
            return fail("seq_stage22_guarded_write operand count mismatch");
        }
        if (display.operands()[0] != clk || display.operands()[1] != en) {
            return fail("seq_stage22_guarded_write clk/enable operands mismatch");
        }
        if (display.operands()[2] != en || display.operands()[3] != d) {
            return fail("seq_stage22_guarded_write value operands mismatch");
        }
        if (!expectStringAttr(*g22_2, display, "displayKind", "write")) {
            return fail("seq_stage22_guarded_write displayKind unexpected");
        }
    }

    // 22.3 strobe variant: ensure kind recorded
    if (const grh::Graph* g22_3 = fetchGraph("seq_stage22_strobe")) {
        ValueId clk = findPort(*g22_3, "clk", /*isInput=*/true);
        ValueId d = findPort(*g22_3, "d", /*isInput=*/true);
        if (!clk || !d) {
            return fail("seq_stage22_strobe missing ports");
        }
        OperationId displayId = findOpByKind(*g22_3, grh::OperationKind::kDisplay);
        if (!displayId) {
            return fail("seq_stage22_strobe missing kDisplay");
        }
        const grh::Operation display = g22_3->getOperation(displayId);
        if (display.operands().size() != 3 ||
            display.operands()[0] != clk || display.operands()[2] != d) {
            return fail("seq_stage22_strobe operands mismatch");
        }
        if (!expectStringAttr(*g22_3, display, "displayKind", "strobe")) {
            return fail("seq_stage22_strobe displayKind unexpected");
        }
    }

    // 22.x (diag filter already handled above)

    // -----------------------
    // Stage23: assert lowering
    // -----------------------

    // Helper: fetch assert ops and basic checks.
    auto expectAssert = [&](const grh::Graph& graph, ValueId clk,
                            std::optional<std::string_view> message = std::nullopt) -> bool {
        std::vector<OperationId> asserts =
            collectOpsByKind(graph, grh::OperationKind::kAssert);
        if (asserts.empty()) {
            return fail("Expected at least one kAssert");
        }
        for (OperationId opId : asserts) {
            const grh::Operation op = graph.getOperation(opId);
            if (op.operands().size() != 2) {
                return fail("kAssert operand count mismatch");
            }
            if (op.operands()[0] != clk) {
                return fail("kAssert clock operand mismatch");
            }
            if (message) {
                if (!expectStringAttr(graph, op, "message", *message)) {
                    return fail("kAssert message attribute mismatch");
                }
            }
        }
        return true;
    };

    // 23.1 basic assert
    if (const grh::Graph* g23_1 = fetchGraph("seq_stage23_assert_basic")) {
        ValueId clk = findPort(*g23_1, "clk", /*isInput=*/true);
        if (!clk) {
            return fail("seq_stage23_assert_basic missing clk");
        }
        if (!expectAssert(*g23_1, clk)) {
            return 1;
        }
    }

    // 23.2 guarded assert with message
    if (const grh::Graph* g23_2 = fetchGraph("seq_stage23_assert_guard")) {
        ValueId clk = findPort(*g23_2, "clk", /*isInput=*/true);
        ValueId en = findPort(*g23_2, "en", /*isInput=*/true);
        ValueId d = findPort(*g23_2, "d", /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage23_assert_guard missing ports");
        }
        std::vector<OperationId> asserts =
            collectOpsByKind(*g23_2, grh::OperationKind::kAssert);
        if (asserts.size() != 1) {
            return fail("seq_stage23_assert_guard expected one kAssert");
        }
        const grh::Operation op = g23_2->getOperation(asserts.front());
        if (op.operands().size() != 2 || op.operands()[0] != clk) {
            return fail("seq_stage23_assert_guard operand mismatch");
        }
        // guard -> cond encoded as (!en) || cond; ensure guard is present via operand users.
        if (!mentionsPort(*g23_2, op.operands()[1], en) ||
            !mentionsPort(*g23_2, op.operands()[1], d)) {
            return fail("seq_stage23_assert_guard condition missing guard/data references");
        }
        if (!expectStringAttr(*g23_2, op, "message", "bad d")) {
            return fail("seq_stage23_assert_guard message missing");
        }
    }

    // 23.3 comb assert warning only
    if (const grh::Graph* g23_3 = fetchGraph("comb_stage23_assert_warning")) {
        std::vector<OperationId> asserts =
            collectOpsByKind(*g23_3, grh::OperationKind::kAssert);
        if (!asserts.empty()) {
            return fail("comb_stage23_assert_warning should not emit kAssert");
        }
    }
    // 20.2 foreach + static break: lower 4 bits from d, upper 4 bits hold from Q
    if (const grh::Graph* g20_2 = fetchGraph("seq_stage20_foreach_partial")) {
        ValueId clk = findPort(*g20_2, "clk", /*isInput=*/true);
        ValueId d   = findPort(*g20_2, "d",   /*isInput=*/true);
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
        if (!r || !r->stateOp) {
            return fail("seq_stage20_foreach_partial r is not kRegister");
        }
        const grh::Operation rOp = g20_2->getOperation(r->stateOp);
        if (rOp.kind() != grh::OperationKind::kRegister) {
            return fail("seq_stage20_foreach_partial r is not kRegister");
        }
        if (rOp.operands().size() < 2 || rOp.operands().front() != clk) {
            return fail("seq_stage20_foreach_partial clock binding error");
        }
        ValueId data = rOp.operands().back();
        OperationId concatId = data ? g20_2->getValue(data).definingOp()
                                    : OperationId::invalid();
        if (!concatId) {
            return fail("seq_stage20_foreach_partial data is not kConcat");
        }
        const grh::Operation concat = g20_2->getOperation(concatId);
        if (concat.kind() != grh::OperationKind::kConcat || concat.operands().size() != 2) {
            return fail("seq_stage20_foreach_partial data is not kConcat");
        }
        // hi operand should be hold slice of Q[7:4]
        ValueId hi = concat.operands()[0];
        OperationId hiSliceId = hi ? g20_2->getValue(hi).definingOp()
                                   : OperationId::invalid();
        if (!hiSliceId) {
            return fail("seq_stage20_foreach_partial high hold slice incorrect");
        }
        const grh::Operation hiSlice = g20_2->getOperation(hiSliceId);
        if (hiSlice.kind() != grh::OperationKind::kSliceStatic ||
            hiSlice.operands().size() != 1 || hiSlice.operands().front() != r->value) {
            return fail("seq_stage20_foreach_partial high hold slice incorrect");
        }
        if (!expectAttrs(*g20_2, hiSlice, "sliceStart", 4) ||
            !expectAttrs(*g20_2, hiSlice, "sliceEnd", 7)) {
            return fail("seq_stage20_foreach_partial high slice attributes incorrect");
        }
        // lo operand should mention d (source of bits [3:0])
        ValueId lo = concat.operands()[1];
        if (!mentionsPort(*g20_2, lo, d)) {
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
        if (!mem || !mem->stateOp) {
            return fail("seq_stage20_for_memory mem not found or mem not kMemory");
        }
        const grh::Operation memOp = g20_3->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage20_for_memory mem not found or mem not kMemory");
        }
        const std::string memSymbol(memOp.symbolText());
        int wrCount = 0;
        int rdCount = 0;
        const grh::ir::SymbolId memKey = g20_3->lookupSymbol("memSymbol");
        for (const auto& opId : g20_3->operations()) {
            const grh::Operation op = g20_3->getOperation(opId);
            if (op.kind() != grh::OperationKind::kMemoryWritePort &&
                op.kind() != grh::OperationKind::kMemorySyncReadPort) {
                continue;
            }
            auto attr = memKey.valid() ? op.attr(memKey) : std::nullopt;
            const std::string* symbol = attr ? std::get_if<std::string>(&*attr) : nullptr;
            if (!symbol || *symbol != memSymbol) {
                continue;
            }
            if (op.kind() == grh::OperationKind::kMemoryWritePort) {
                wrCount++;
            } else {
                rdCount++;
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
        ValueId clk = findPort(*g27, "clk", /*isInput=*/true);
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
        if (!mem || !mem->stateOp) {
            return fail("seq_stage27_mem_addr mem not found or not kMemory");
        }
        const grh::Operation memOp = g27->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage27_mem_addr mem not found or not kMemory");
        }
        const std::string memSymbol(memOp.symbolText());
        OperationId wrId =
            findMemoryOp(*g27, grh::OperationKind::kMemoryWritePort, memSymbol);
        OperationId mwrId =
            findMemoryOp(*g27, grh::OperationKind::kMemoryMaskWritePort, memSymbol);
        OperationId rdId =
            findMemoryOp(*g27, grh::OperationKind::kMemorySyncReadPort, memSymbol);
        if (!wrId || !mwrId || !rdId) {
            return fail("seq_stage27_mem_addr expected write/mask/read ports");
        }
        const grh::Operation wr = g27->getOperation(wrId);
        const grh::Operation mwr = g27->getOperation(mwrId);
        const grh::Operation rd = g27->getOperation(rdId);
        auto expectAddrShape = [&](const grh::Operation& op, std::string_view label) -> bool {
            if (op.operands().size() < 2) {
                std::string msg(label);
                msg.append(" has insufficient operands");
                return fail(msg);
            }
            ValueId addr = op.operands()[1];
            if (!addr || g27->getValue(addr).width() != 7 || g27->getValue(addr).isSigned()) {
                std::string msg(label);
                msg.append(" addr width/sign mismatch");
                return fail(msg);
            }
            return true;
        };
        if (!expectStringAttr(*g27, wr, "clkPolarity", "posedge") ||
            !expectStringAttr(*g27, mwr, "clkPolarity", "posedge") ||
            !expectStringAttr(*g27, rd, "clkPolarity", "posedge")) {
            return fail("seq_stage27_mem_addr clkPolarity missing on memory ports");
        }
        if (!expectAddrShape(wr, "write port") || !expectAddrShape(mwr, "mask write port") ||
            !expectAddrShape(rd, "sync read port")) {
            return 1;
        }
    }

    // -----------------------
    // Stage29: memory ports with reset
    // -----------------------
    if (const grh::Graph* g29_arst = fetchGraph("seq_stage29_arst_mem")) {
        ValueId clk = findPort(*g29_arst, "clk", /*isInput=*/true);
        ValueId rst_n = findPort(*g29_arst, "rst_n", /*isInput=*/true);
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
        if (!mem || !mem->stateOp) {
            return fail("seq_stage29_arst_mem mem not found or not kMemory");
        }
        const grh::Operation memOp = g29_arst->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage29_arst_mem mem not found or not kMemory");
        }
        const std::string memSymbol(memOp.symbolText());

        auto writes = collectMemoryOps(*g29_arst, grh::OperationKind::kMemoryWritePortArst, memSymbol);
        auto masks = collectMemoryOps(*g29_arst, grh::OperationKind::kMemoryMaskWritePortArst, memSymbol);
        auto reads = collectMemoryOps(*g29_arst, grh::OperationKind::kMemorySyncReadPortArst, memSymbol);
        if (writes.size() != 2 || masks.size() != 2 || reads.size() != 1) {
            return fail("seq_stage29_arst_mem expected 2 write, 2 mask write, 1 read port");
        }
        for (OperationId opId : writes) {
            const grh::Operation op = g29_arst->getOperation(opId);
            if (op.operands().size() < 5 || op.operands()[0] != clk ||
                op.operands()[1] != rst_n) {
                return fail("seq_stage29_arst_mem write port operands mismatch");
            }
            if (!expectStringAttr(*g29_arst, op, "rstPolarity", "low") ||
                !expectStringAttr(*g29_arst, op, "enLevel", "high") ||
                !expectStringAttr(*g29_arst, op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_arst_mem write port attributes mismatch");
            }
        }
        for (OperationId opId : masks) {
            const grh::Operation op = g29_arst->getOperation(opId);
            if (op.operands().size() < 6 || op.operands()[0] != clk ||
                op.operands()[1] != rst_n) {
                return fail("seq_stage29_arst_mem mask port operands mismatch");
            }
            if (!expectStringAttr(*g29_arst, op, "rstPolarity", "low") ||
                !expectStringAttr(*g29_arst, op, "enLevel", "high") ||
                !expectStringAttr(*g29_arst, op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_arst_mem mask port attributes mismatch");
            }
        }
        const grh::Operation rd = g29_arst->getOperation(reads.front());
        if (rd.operands().size() < 4 || rd.operands()[0] != clk ||
            rd.operands()[1] != rst_n) {
            return fail("seq_stage29_arst_mem read port operands mismatch");
        }
        if (!expectStringAttr(*g29_arst, rd, "rstPolarity", "low") ||
            !expectStringAttr(*g29_arst, rd, "enLevel", "high") ||
            !expectStringAttr(*g29_arst, rd, "clkPolarity", "posedge")) {
            return fail("seq_stage29_arst_mem read port attributes mismatch");
        }
    }

    if (const grh::Graph* g29_rst = fetchGraph("seq_stage29_rst_mem")) {
        ValueId clk = findPort(*g29_rst, "clk", /*isInput=*/true);
        ValueId rst = findPort(*g29_rst, "rst", /*isInput=*/true);
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
        if (!mem || !mem->stateOp) {
            return fail("seq_stage29_rst_mem mem not found or not kMemory");
        }
        const grh::Operation memOp = g29_rst->getOperation(mem->stateOp);
        if (memOp.kind() != grh::OperationKind::kMemory) {
            return fail("seq_stage29_rst_mem mem not found or not kMemory");
        }
        const std::string memSymbol(memOp.symbolText());

        auto writes = collectMemoryOps(*g29_rst, grh::OperationKind::kMemoryWritePortRst, memSymbol);
        auto masks = collectMemoryOps(*g29_rst, grh::OperationKind::kMemoryMaskWritePortRst, memSymbol);
        auto reads = collectMemoryOps(*g29_rst, grh::OperationKind::kMemorySyncReadPortRst, memSymbol);
        if (writes.size() != 2 || masks.size() != 2 || reads.size() != 1) {
            return fail("seq_stage29_rst_mem expected 2 write, 2 mask write, 1 read port");
        }
        for (OperationId opId : writes) {
            const grh::Operation op = g29_rst->getOperation(opId);
            if (op.operands().size() < 5 || op.operands()[0] != clk ||
                op.operands()[1] != rst) {
                return fail("seq_stage29_rst_mem write port operands mismatch");
            }
            if (!expectStringAttr(*g29_rst, op, "rstPolarity", "high") ||
                !expectStringAttr(*g29_rst, op, "enLevel", "high") ||
                !expectStringAttr(*g29_rst, op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_rst_mem write port attributes mismatch");
            }
        }
        for (OperationId opId : masks) {
            const grh::Operation op = g29_rst->getOperation(opId);
            if (op.operands().size() < 6 || op.operands()[0] != clk ||
                op.operands()[1] != rst) {
                return fail("seq_stage29_rst_mem mask port operands mismatch");
            }
            if (!expectStringAttr(*g29_rst, op, "rstPolarity", "high") ||
                !expectStringAttr(*g29_rst, op, "enLevel", "high") ||
                !expectStringAttr(*g29_rst, op, "clkPolarity", "posedge")) {
                return fail("seq_stage29_rst_mem mask port attributes mismatch");
            }
        }
        const grh::Operation rd = g29_rst->getOperation(reads.front());
        if (rd.operands().size() < 4 || rd.operands()[0] != clk ||
            rd.operands()[1] != rst) {
            return fail("seq_stage29_rst_mem read port operands mismatch");
        }
        if (!expectStringAttr(*g29_rst, rd, "rstPolarity", "high") ||
            !expectStringAttr(*g29_rst, rd, "enLevel", "high") ||
            !expectStringAttr(*g29_rst, rd, "clkPolarity", "posedge")) {
                return fail("seq_stage29_rst_mem read port attributes mismatch");
        }
    }

    return 0;
}
