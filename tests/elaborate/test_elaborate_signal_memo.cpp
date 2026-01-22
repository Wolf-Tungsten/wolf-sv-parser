#include "elaborate.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

#include "slang/ast/Compilation.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"

using namespace wolf_sv_parser;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_signal_memo] " << message << '\n';
    return 1;
}

const SignalMemoEntry* findEntry(std::span<const SignalMemoEntry> entries,
                                 std::string_view name) {
    for (const SignalMemoEntry& entry : entries) {
        if (entry.symbol && entry.symbol->name == name) {
            return &entry;
        }
    }
    return nullptr;
}

const SignalMemoField* findField(const SignalMemoEntry& entry, std::string_view path) {
    for (const SignalMemoField& field : entry.fields) {
        if (field.path == path) {
            return &field;
        }
    }
    return nullptr;
}

void logMemo(std::string_view label, std::span<const SignalMemoEntry> entries) {
    std::cout << "[memo] " << label << " count=" << entries.size() << '\n';
    for (const SignalMemoEntry& entry : entries) {
        const std::string symbolName = entry.symbol ? std::string(entry.symbol->name) : "<null>";
        const std::string typeName = entry.type ? entry.type->toString() : "<null-type>";
        std::cout << "  - " << symbolName << " width=" << entry.width
                  << (entry.isSigned ? " signed" : " unsigned") << " type=" << typeName
                  << " fields=" << entry.fields.size() << '\n';
        std::size_t preview = std::min<std::size_t>(entry.fields.size(), 3);
        for (std::size_t idx = 0; idx < preview; ++idx) {
            const SignalMemoField& field = entry.fields[idx];
            std::cout << "      field=" << field.path << " [" << field.msb << ":" << field.lsb
                      << "]" << (field.isSigned ? " signed" : "") << '\n';
        }
    }
}

} // namespace

int main() {
    const std::filesystem::path sourcePath =
        std::filesystem::path(WOLF_SV_ELAB_SIGNAL_MEMO_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing testcase file: " + sourcePath.string());
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-signal-memo");
    argStorage.emplace_back(sourcePath.string());

    std::vector<const char*> argv;
    argv.reserve(argStorage.size());
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        return fail("Failed to parse command line");
    }
    if (!driver.processOptions()) {
        return fail("Failed to process driver options");
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
    grh::ir::Netlist netlist = elaborator.convert(compilation->getRoot());

    const grh::ir::Graph* graph = netlist.findGraph("memo_child");
    if (!graph) {
        return fail("Graph memo_child not found");
    }

    const slang::ast::InstanceSymbol* memoTop = nullptr;
    for (const slang::ast::InstanceSymbol* top : compilation->getRoot().topInstances) {
        if (top && top->name == "memo_top") {
            memoTop = top;
            break;
        }
    }
    if (!memoTop) {
        return fail("Unable to locate memo_top instance");
    }

    const slang::ast::InstanceSymbol* childInstance = nullptr;
    for (const slang::ast::Symbol& member : memoTop->body.members()) {
        if (const auto* instance = member.as_if<slang::ast::InstanceSymbol>()) {
            if (instance->name == "u_child") {
                childInstance = instance;
                break;
            }
        }
    }
    if (!childInstance) {
        return fail("Child instance u_child not found");
    }

    const slang::ast::InstanceBodySymbol* childBody = childInstance->getCanonicalBody();
    if (!childBody) {
        childBody = &childInstance->body;
    }

    std::span<const SignalMemoEntry> netMemo = elaborator.peekNetMemo(*childBody);
    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(*childBody);
    std::span<const SignalMemoEntry> memMemo = elaborator.peekMemMemo(*childBody);

    logMemo("net", netMemo);
    logMemo("reg", regMemo);

    if (!findEntry(netMemo, "w_assign")) {
        return fail("net memo missing w_assign");
    }
    const SignalMemoEntry* combBus = findEntry(netMemo, "comb_bus");
    if (!combBus) {
        return fail("net memo missing comb_bus");
    }
    if (combBus->width != 8 || !combBus->isSigned) {
        return fail("comb_bus memo entry has unexpected width/sign");
    }
    if (!findEntry(netMemo, "star_assign")) {
        return fail("net memo missing star_assign");
    }

    const SignalMemoEntry* structNet = findEntry(netMemo, "net_struct_bus");
    if (!structNet) {
        return fail("net memo missing net_struct_bus");
    }
    if (structNet->width != 6 || structNet->fields.size() != 6) {
        return fail("net_struct_bus expected 6-bit flattened fields");
    }
    if (!findField(*structNet, "net_struct_bus.parts_hi[3]") ||
        !findField(*structNet, "net_struct_bus.parts_lo[0]")) {
        return fail("net_struct_bus fields missing expected slices");
    }

    const SignalMemoEntry* unpackedNet = findEntry(netMemo, "net_unpacked_bus");
    if (!unpackedNet) {
        return fail("net memo missing net_unpacked_bus");
    }
    if (unpackedNet->width != 6 || unpackedNet->fields.size() != 6) {
        return fail("net_unpacked_bus expected 6 flattened bits");
    }
    if (!findField(*unpackedNet, "net_unpacked_bus[1][0]")) {
        return fail("net_unpacked_bus missing [1][0] slice");
    }

    if (!findEntry(regMemo, "seq_logic")) {
        return fail("reg memo missing seq_logic");
    }
    if (!findEntry(regMemo, "reg_ff")) {
        return fail("reg memo missing reg_ff");
    }
    if (!findEntry(regMemo, "latch_target")) {
        return fail("reg memo missing latch_target");
    }
    if (findEntry(netMemo, "conflict_signal") || findEntry(regMemo, "conflict_signal")) {
        return fail("conflict_signal should have been excluded due to conflicting drivers");
    }

    const SignalMemoEntry* structReg = findEntry(regMemo, "reg_struct_bus");
    if (!structReg) {
        return fail("reg memo missing reg_struct_bus");
    }
    if (structReg->width != 6 || structReg->fields.size() != 6) {
        return fail("reg_struct_bus expected 6-bit flattened fields");
    }
    if (!findField(*structReg, "reg_struct_bus.parts_hi[2]") ||
        !findField(*structReg, "reg_struct_bus.parts_lo[1]")) {
        return fail("reg_struct_bus fields missing expected slices");
    }

    const SignalMemoEntry* packedReg = findEntry(regMemo, "reg_packed_matrix");
    if (!packedReg) {
        return fail("reg memo missing reg_packed_matrix");
    }
    if (packedReg->width != 8) {
        return fail("reg_packed_matrix width mismatch");
    }
    if (!findField(*packedReg, "reg_packed_matrix[0][0]")) {
        return fail("reg_packed_matrix missing packed field path");
    }

    auto expectNetValue = [&](std::string_view name) -> bool {
        const SignalMemoEntry* entry = findEntry(netMemo, name);
        if (!entry) {
            return fail(std::string("net memo missing entry for ") + std::string(name));
        }
        if (!entry->value) {
            return fail(std::string("net memo entry ") + std::string(name) + " is missing GRH value");
        }
        std::cout << "[memo] net " << name << " entryWidth=" << entry->width
                  << " valueWidth=" << graph->getValue(entry->value).width() << '\n';
        if (graph->getValue(entry->value).width() != entry->width) {
            return fail(std::string("value width mismatch for net ") + std::string(name));
        }
        std::cout << "        value symbol=" << graph->getValue(entry->value).symbolText() << '\n';
        return true;
    };
    if (!expectNetValue("w_assign")) {
        return 1;
    }
    if (!expectNetValue("comb_bus")) {
        return 1;
    }

    auto expectRegister = [&](std::string_view name, std::string_view clkPolarity) -> bool {
        const SignalMemoEntry* entry = findEntry(regMemo, name);
        if (!entry) {
            return fail(std::string("reg memo missing entry for ") + std::string(name));
        }
        if (!entry->stateOp) {
            return fail(std::string("reg memo entry ") + std::string(name) +
                        " is missing state operation");
        }
        if (graph->getOperation(entry->stateOp).kind() != grh::ir::OperationKind::kRegister) {
            return fail(std::string("reg memo entry ") + std::string(name) +
                        " is not bound to kRegister");
        }
        if (!entry->value) {
            return fail(std::string("reg memo entry ") + std::string(name) +
                        " is missing GRH value");
        }
        const grh::ir::Operation op = graph->getOperation(entry->stateOp);
        if (op.results().empty() || op.results().front() != entry->value) {
            return fail(std::string("register operation result mismatch for ") +
                        std::string(name));
        }
        auto clkAttr = op.attr("clkPolarity");
        if (!clkAttr) {
            return fail(std::string("register operation missing clkPolarity attribute for ") +
                        std::string(name));
        }
        const std::string* attrValue = std::get_if<std::string>(&*clkAttr);
        if (!attrValue) {
            return fail(std::string("register clkPolarity attribute type mismatch for ") +
                        std::string(name));
        }
        if (*attrValue != clkPolarity) {
            return fail(std::string("register clkPolarity mismatch for ") + std::string(name));
        }
        std::cout << "[memo] register " << name << " clk=" << *attrValue
                  << " op=" << graph->getOperation(entry->stateOp).symbolText() << '\n';
        std::cout << "        value=" << graph->getValue(entry->value).symbolText() << '\n';
        return true;
    };

    if (!expectRegister("seq_logic", "posedge") || !expectRegister("reg_ff", "posedge") ||
        !expectRegister("reg_struct_bus", "posedge") ||
        !expectRegister("reg_packed_matrix", "posedge")) {
        return 1;
    }
    if (!expectRegister("latch_target", "negedge")) {
        return 1;
    }

    const SignalMemoEntry* memoryEntry = findEntry(memMemo, "reg_unpacked_bus");
    if (!memoryEntry) {
        return fail("mem memo missing reg_unpacked_bus");
    }
    if (memoryEntry->value) {
        return fail("reg_unpacked_bus should not materialize a flat value");
    }
    if (!memoryEntry->stateOp ||
        graph->getOperation(memoryEntry->stateOp).kind() != grh::ir::OperationKind::kMemory) {
        return fail("reg_unpacked_bus expected kMemory placeholder");
    }
    const grh::ir::Operation memOp = graph->getOperation(memoryEntry->stateOp);
    auto widthAttr = memOp.attr("width");
    auto rowAttr = memOp.attr("row");
    auto signedAttr = memOp.attr("isSigned");
    if (!widthAttr || !rowAttr || !signedAttr) {
        return fail("reg_unpacked_bus memory attributes incomplete");
    }
    const int64_t* widthVal = std::get_if<int64_t>(&*widthAttr);
    const int64_t* rowVal = std::get_if<int64_t>(&*rowAttr);
    const bool* signedVal = std::get_if<bool>(&*signedAttr);
    if (!widthVal || !rowVal || !signedVal) {
        return fail("reg_unpacked_bus memory attributes have unexpected types");
    }
    if (*widthVal != 3 || *rowVal != 2 || *signedVal) {
        return fail("reg_unpacked_bus memory attributes mismatch");
    }
    std::cout << "[memo] memory reg_unpacked_bus width=" << *widthVal
              << " rows=" << *rowVal << '\n';

    std::cout << "[memo] diagnostics count=" << diagnostics.messages().size() << '\n';
    for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
        const char* kindStr = "NYI";
        switch (diag.kind) {
        case ElaborateDiagnosticKind::Todo:
            kindStr = "TODO";
            break;
        case ElaborateDiagnosticKind::Warning:
            kindStr = "WARN";
            break;
        case ElaborateDiagnosticKind::NotYetImplemented:
        default:
            kindStr = "NYI";
            break;
        }
        std::cout << "  - kind=" << kindStr << " origin=" << diag.originSymbol
                  << " message=" << diag.message << '\n';
    }

    bool foundConflictDiag = false;
    for (const ElaborateDiagnostic& diag : diagnostics.messages()) {
        if (diag.message.find("conflicting net/reg") != std::string::npos ||
            diag.originSymbol.find("conflict_signal") != std::string::npos) {
            foundConflictDiag = true;
            break;
        }
    }
    if (!foundConflictDiag) {
        return fail("Expected conflicting driver diagnostic for conflict_signal");
    }

    return 0;
}
