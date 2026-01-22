#include "elaborate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "slang/driver/Driver.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

using namespace wolf_sv_parser;

namespace {

int fail(const std::string& message) {
    std::cerr << "[elaborate_stage21] " << message << '\n';
    return 1;
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

ValueId findPortValue(const grh::ir::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
}

std::optional<std::string> getStringAttr(const grh::ir::Operation& op, std::string_view key) {
    if (auto attr = op.attr(key)) {
        if (const auto* text = std::get_if<std::string>(&*attr)) {
            return *text;
        }
    }
    return std::nullopt;
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

#ifndef WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH
#error "WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH must be defined"
#endif

int main() {
    // Use the same data path as seq_always tests.
    const std::filesystem::path sourcePath(WOLF_SV_ELAB_SEQ_ALWAYS_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing seq always testcase file: " + sourcePath.string());
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-stage21");
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
    grh::ir::Netlist netlist = elaborator.convert(compilation->getRoot());

    // Case 1: seq_stage21_en_reg => kRegisterEn with [clk, en, data]
    if (const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage21_en_reg")) {
        const grh::ir::Graph* graph = netlist.findGraph("seq_stage21_en_reg");
        if (!graph) {
            return fail("Graph seq_stage21_en_reg not found");
        }
        ValueId clk = findPortValue(*graph, "clk", /*isInput=*/true);
        ValueId en  = findPortValue(*graph, "en",  /*isInput=*/true);
        ValueId d   = findPortValue(*graph, "d",   /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage21_en_reg missing ports");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage21_en_reg missing reg memo/stateOp");
        }
        const grh::ir::Operation op = graph->getOperation(r->stateOp);
        if (op.kind() != grh::ir::OperationKind::kRegisterEn) {
            return fail("seq_stage21_en_reg expected kRegisterEn got " +
                        std::string(grh::ir::toString(op.kind())));
        }
        if (op.operands().size() != 3) {
            return fail("seq_stage21_en_reg operand count mismatch");
        }
        if (op.operands()[0] != clk ||
            op.operands()[1] != en ||
            op.operands()[2] != d) {
            return fail("seq_stage21_en_reg operand binding mismatch");
        }
        auto enLevel = getStringAttr(op, "enLevel");
        if (!enLevel || *enLevel != "high") {
            return fail("seq_stage21_en_reg enLevel missing or not high");
        }
    } else {
        return fail("Top instance seq_stage21_en_reg not found");
    }

    // Case 2: seq_stage21_rst_en_reg => kRegisterEnArst with [clk, rst, en, resetValue, data]
    if (const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage21_rst_en_reg")) {
        const grh::ir::Graph* graph = netlist.findGraph("seq_stage21_rst_en_reg");
        if (!graph) {
            return fail("Graph seq_stage21_rst_en_reg not found");
        }
        ValueId clk   = findPortValue(*graph, "clk",   /*isInput=*/true);
        ValueId rst_n = findPortValue(*graph, "rst_n", /*isInput=*/true);
        ValueId en    = findPortValue(*graph, "en",    /*isInput=*/true);
        ValueId d     = findPortValue(*graph, "d",     /*isInput=*/true);
        ValueId rv    = findPortValue(*graph, "rv",    /*isInput=*/true);
        if (!clk || !rst_n || !en || !d || !rv) {
            return fail("seq_stage21_rst_en_reg missing ports");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage21_rst_en_reg missing reg memo/stateOp");
        }
        const grh::ir::Operation op = graph->getOperation(r->stateOp);
        if (op.kind() != grh::ir::OperationKind::kRegisterEnArst) {
            return fail("seq_stage21_rst_en_reg expected kRegisterEnArst got " +
                        std::string(grh::ir::toString(op.kind())));
        }
        if (op.operands().size() != 5) {
            return fail("seq_stage21_rst_en_reg operand count mismatch");
        }
        if (op.operands()[0] != clk ||
            op.operands()[1] != rst_n) {
            return fail("seq_stage21_rst_en_reg clk/rst binding mismatch");
        }
        // en may be coerced; expect direct port wiring here (1-bit).
        if (op.operands()[2] != en) {
            return fail("seq_stage21_rst_en_reg en binding mismatch");
        }
        if (op.operands()[3] != rv ||
            op.operands()[4] != d) {
            return fail("seq_stage21_rst_en_reg reset/data binding mismatch");
        }
        auto rstPolarity = getStringAttr(op, "rstPolarity");
        if (!rstPolarity || *rstPolarity != "low") {
            return fail("seq_stage21_rst_en_reg rstPolarity missing/incorrect");
        }
        auto enLevel = getStringAttr(op, "enLevel");
        if (!enLevel || *enLevel != "high") {
            return fail("seq_stage21_rst_en_reg enLevel missing/incorrect");
        }
    } else {
        return fail("Top instance seq_stage21_rst_en_reg not found");
    }

    return 0;
}
