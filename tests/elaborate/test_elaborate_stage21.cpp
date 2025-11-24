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

using namespace wolf_sv;

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

const grh::Value* findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    const auto& ports = isInput ? graph.inputPorts() : graph.outputPorts();
    auto it = ports.find(std::string(name));
    if (it != ports.end()) {
        return it->second;
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
    grh::Netlist netlist = elaborator.convert(compilation->getRoot());

    // Case 1: seq_stage21_en_reg => kRegisterEn with [clk, en, data]
    if (const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage21_en_reg")) {
        const grh::Graph* graph = netlist.findGraph("seq_stage21_en_reg");
        if (!graph) {
            return fail("Graph seq_stage21_en_reg not found");
        }
        const grh::Value* clk = findPort(*graph, "clk", /*isInput=*/true);
        const grh::Value* en  = findPort(*graph, "en",  /*isInput=*/true);
        const grh::Value* d   = findPort(*graph, "d",   /*isInput=*/true);
        if (!clk || !en || !d) {
            return fail("seq_stage21_en_reg missing ports");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage21_en_reg missing reg memo/stateOp");
        }
        if (r->stateOp->kind() != grh::OperationKind::kRegisterEn) {
            return fail("seq_stage21_en_reg expected kRegisterEn");
        }
        if (r->stateOp->operands().size() != 3) {
            return fail("seq_stage21_en_reg operand count mismatch");
        }
        if (r->stateOp->operands()[0] != clk ||
            r->stateOp->operands()[1] != en ||
            r->stateOp->operands()[2] != d) {
            return fail("seq_stage21_en_reg operand binding mismatch");
        }
        auto enLevel = r->stateOp->attributes().find("enLevel");
        if (enLevel == r->stateOp->attributes().end() ||
            !std::holds_alternative<std::string>(enLevel->second) ||
            std::get<std::string>(enLevel->second) != "high") {
            return fail("seq_stage21_en_reg enLevel missing or not high");
        }
    } else {
        return fail("Top instance seq_stage21_en_reg not found");
    }

    // Case 2: seq_stage21_rst_en_reg => kRegisterEnArst with [clk, rst, en, resetValue, data]
    if (const auto* inst = findInstanceByName(compilation->getRoot().topInstances,
                                              "seq_stage21_rst_en_reg")) {
        const grh::Graph* graph = netlist.findGraph("seq_stage21_rst_en_reg");
        if (!graph) {
            return fail("Graph seq_stage21_rst_en_reg not found");
        }
        const grh::Value* clk   = findPort(*graph, "clk",   /*isInput=*/true);
        const grh::Value* rst_n = findPort(*graph, "rst_n", /*isInput=*/true);
        const grh::Value* en    = findPort(*graph, "en",    /*isInput=*/true);
        const grh::Value* d     = findPort(*graph, "d",     /*isInput=*/true);
        const grh::Value* rv    = findPort(*graph, "rv",    /*isInput=*/true);
        if (!clk || !rst_n || !en || !d || !rv) {
            return fail("seq_stage21_rst_en_reg missing ports");
        }
        std::span<const SignalMemoEntry> memo = elaborator.peekRegMemo(fetchBody(*inst));
        const SignalMemoEntry* r = findEntry(memo, "r");
        if (!r || !r->stateOp) {
            return fail("seq_stage21_rst_en_reg missing reg memo/stateOp");
        }
        if (r->stateOp->kind() != grh::OperationKind::kRegisterEnArst) {
            return fail("seq_stage21_rst_en_reg expected kRegisterEnArst");
        }
        if (r->stateOp->operands().size() != 5) {
            return fail("seq_stage21_rst_en_reg operand count mismatch");
        }
        if (r->stateOp->operands()[0] != clk ||
            r->stateOp->operands()[1] != rst_n) {
            return fail("seq_stage21_rst_en_reg clk/rst binding mismatch");
        }
        // en may be coerced; expect direct port wiring here (1-bit).
        if (r->stateOp->operands()[2] != en) {
            return fail("seq_stage21_rst_en_reg en binding mismatch");
        }
        if (r->stateOp->operands()[3] != rv ||
            r->stateOp->operands()[4] != d) {
            return fail("seq_stage21_rst_en_reg reset/data binding mismatch");
        }
        auto rstPolarity = r->stateOp->attributes().find("rstPolarity");
        if (rstPolarity == r->stateOp->attributes().end() ||
            !std::holds_alternative<std::string>(rstPolarity->second) ||
            std::get<std::string>(rstPolarity->second) != "low") {
            return fail("seq_stage21_rst_en_reg rstPolarity missing/incorrect");
        }
        auto enLevel = r->stateOp->attributes().find("enLevel");
        if (enLevel == r->stateOp->attributes().end() ||
            !std::holds_alternative<std::string>(enLevel->second) ||
            std::get<std::string>(enLevel->second) != "high") {
            return fail("seq_stage21_rst_en_reg enLevel missing/incorrect");
        }
    } else {
        return fail("Top instance seq_stage21_rst_en_reg not found");
    }

    return 0;
}
