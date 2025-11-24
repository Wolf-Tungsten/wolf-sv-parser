#include "elaborate.hpp"
#include "emit.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
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
    std::cerr << "[elaborate_assign] " << message << '\n';
    return 1;
}

const SignalMemoEntry* findEntry(std::span<const SignalMemoEntry> memo,
                                 std::string_view name) {
    for (const SignalMemoEntry& entry : memo) {
        if (entry.symbol && entry.symbol->name == name) {
            return &entry;
        }
    }
    return nullptr;
}

const grh::Value* findPort(const grh::Graph& graph, std::string_view name, bool isInput) {
    if (isInput) {
        auto it = graph.inputPorts().find(std::string(name));
        if (it != graph.inputPorts().end()) {
            return graph.findValue(it->second);
        }
    }
    else {
        auto it = graph.outputPorts().find(std::string(name));
        if (it != graph.outputPorts().end()) {
            return graph.findValue(it->second);
        }
    }
    return nullptr;
}

bool writeArtifact(const grh::Netlist& netlist) {
    const std::filesystem::path artifactPath(WOLF_SV_ELAB_ASSIGN_ARTIFACT_PATH);
    if (artifactPath.empty()) {
        return true;
    }

    if (const std::filesystem::path dir = artifactPath.parent_path(); !dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(dir) &&
            !std::filesystem::create_directories(dir, ec) && ec) {
            std::cerr << "[elaborate_assign] Failed to create artifact dir: "
                      << ec.message() << '\n';
            return false;
        }
    }

    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    auto jsonOpt = emitter.emitToString(netlist, options);
    if (!jsonOpt || diagnostics.hasError()) {
        std::cerr << "[elaborate_assign] Failed to emit JSON artifact\n";
        return false;
    }

    std::ofstream os(artifactPath, std::ios::trunc);
    if (!os.is_open()) {
        std::cerr << "[elaborate_assign] Failed to open artifact file: "
                  << artifactPath.string() << '\n';
        return false;
    }
    os << *jsonOpt;
    return true;
}

} // namespace

#ifndef WOLF_SV_ELAB_ASSIGN_DATA_PATH
#error "WOLF_SV_ELAB_ASSIGN_DATA_PATH must be defined"
#endif
#ifndef WOLF_SV_ELAB_ASSIGN_ARTIFACT_PATH
#error "WOLF_SV_ELAB_ASSIGN_ARTIFACT_PATH must be defined"
#endif

int main() {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    const std::filesystem::path sourcePath(WOLF_SV_ELAB_ASSIGN_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing assign testcase file: " + sourcePath.string());
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("elaborate-assign");
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

    const slang::ast::InstanceSymbol* topInstance = nullptr;
    for (const slang::ast::InstanceSymbol* inst : compilation->getRoot().topInstances) {
        if (inst && inst->name == "assign_stage11_case") {
            topInstance = inst;
            break;
        }
    }
    if (!topInstance) {
        return fail("assign_stage11_case top instance not found");
    }

    grh::Graph* graph = netlist.findGraph("assign_stage11_case");
    if (!graph) {
        return fail("GRH graph assign_stage11_case not found");
    }

    const slang::ast::InstanceBodySymbol* canonicalBody = topInstance->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body = canonicalBody ? *canonicalBody : topInstance->body;
    std::span<const SignalMemoEntry> netMemo = elaborator.peekNetMemo(body);
    if (netMemo.empty()) {
        return fail("Net memo is empty for assign_stage11_case");
    }

    const grh::Value* portInA = findPort(*graph, "in_a", /* isInput */ true);
    const grh::Value* portInB = findPort(*graph, "in_b", /* isInput */ true);
    if (!portInA || !portInB) {
        return fail("Input ports in_a/in_b not registered in graph");
    }

    // scalar_net should directly connect to in_a via kAssign.
    const SignalMemoEntry* scalarNet = findEntry(netMemo, "scalar_net");
    if (!scalarNet || !scalarNet->value) {
        return fail("scalar_net memo entry missing value");
    }
    const grh::Operation* scalarAssign = scalarNet->value ? scalarNet->value->definingOp() : nullptr;
    if (!scalarAssign || scalarAssign->kind() != grh::OperationKind::kAssign) {
        return fail("scalar_net is not driven by kAssign");
    }
    if (scalarAssign->operands().size() != 1 || scalarAssign->operands().front() != portInA) {
        return fail("scalar_net assign operand does not reference in_a");
    }

    // struct_net should aggregate three slices (hi, lo[3:2], lo[1:0]).
    const SignalMemoEntry* structNet = findEntry(netMemo, "struct_net");
    if (!structNet || !structNet->value) {
        return fail("struct_net memo entry missing value");
    }
    const grh::Operation* structAssign = structNet->value ? structNet->value->definingOp() : nullptr;
    if (!structAssign || structAssign->kind() != grh::OperationKind::kAssign) {
        return fail("struct_net is not driven by kAssign");
    }
    if (structAssign->operands().empty()) {
        return fail("struct_net assign has no operand");
    }
    const grh::Value* structComposite = structAssign->operands().front();
    const grh::Operation* structConcat = structComposite ? structComposite->definingOp() : nullptr;
    if (!structConcat || structConcat->kind() != grh::OperationKind::kConcat) {
        return fail("struct_net assign is expected to use kConcat");
    }
    if (structConcat->operands().size() != 3) {
        return fail("struct_net concat should have three operands");
    }
    if (const grh::Operation* hiSlice = structConcat->operands()[0]->definingOp()) {
        if (hiSlice->kind() != grh::OperationKind::kSliceStatic ||
            hiSlice->operands().empty() || hiSlice->operands().front() != portInA) {
            return fail("struct_net hi slice does not originate from in_a");
        }
    }
    bool loSlicesFromB = true;
    for (std::size_t idx = 1; idx < structConcat->operands().size(); ++idx) {
        const grh::Operation* sliceOp = structConcat->operands()[idx]->definingOp();
        if (!sliceOp || sliceOp->kind() != grh::OperationKind::kSliceStatic ||
            sliceOp->operands().empty() || sliceOp->operands().front() != portInB) {
            loSlicesFromB = false;
            break;
        }
    }
    if (!loSlicesFromB) {
        return fail("struct_net lo slices are not sourced from in_b");
    }

    // array_net should include zero-fill as well as slices from both inputs.
    const SignalMemoEntry* arrayNet = findEntry(netMemo, "array_net");
    if (!arrayNet || !arrayNet->value) {
        return fail("array_net memo entry missing value");
    }
    const grh::Operation* arrayAssign = arrayNet->value ? arrayNet->value->definingOp() : nullptr;
    if (!arrayAssign || arrayAssign->kind() != grh::OperationKind::kAssign) {
        return fail("array_net is not driven by kAssign");
    }
    const grh::Value* arrayComposite = arrayAssign->operands().front();
    const grh::Operation* arrayConcat = arrayComposite ? arrayComposite->definingOp() : nullptr;
    if (!arrayConcat || arrayConcat->kind() != grh::OperationKind::kConcat) {
        return fail("array_net assign should use kConcat");
    }
    bool hasZeroFill = false;
    bool hasSliceFromA = false;
    bool hasSliceFromB = false;
    for (grh::Value* operand : arrayConcat->operands()) {
        if (!operand) {
            continue;
        }
        if (const grh::Operation* op = operand->definingOp()) {
            if (op->kind() == grh::OperationKind::kConstant) {
                hasZeroFill = true;
            }
            else if (op->kind() == grh::OperationKind::kSliceStatic && !op->operands().empty()) {
                if (op->operands().front() == portInA) {
                    hasSliceFromA = true;
                }
                if (op->operands().front() == portInB) {
                    hasSliceFromB = true;
                }
            }
        }
    }
    if (!hasZeroFill || !hasSliceFromA || !hasSliceFromB) {
        return fail("array_net concat missing zero-fill or expected slices");
    }

    // partial_net should zero-fill the LSB.
    const SignalMemoEntry* partialNet = findEntry(netMemo, "partial_net");
    if (!partialNet || !partialNet->value) {
        return fail("partial_net memo entry missing value");
    }
    const grh::Operation* partialAssign = partialNet->value->definingOp();
    if (!partialAssign || partialAssign->kind() != grh::OperationKind::kAssign) {
        return fail("partial_net is not driven by kAssign");
    }
    const grh::Value* partialComposite = partialAssign->operands().front();
    const grh::Operation* partialConcat = partialComposite->definingOp();
    if (!partialConcat || partialConcat->kind() != grh::OperationKind::kConcat) {
        return fail("partial_net assign should produce kConcat");
    }
    bool sawUnitZero = false;
    for (grh::Value* operand : partialConcat->operands()) {
        if (operand && operand->definingOp() &&
            operand->definingOp()->kind() == grh::OperationKind::kConstant &&
            operand->width() == 1) {
            sawUnitZero = true;
            break;
        }
    }
    if (!sawUnitZero) {
        return fail("partial_net concat missing 1-bit zero-fill");
    }

    // concat_b should be partially assigned with zero-fill for high bits.
    const SignalMemoEntry* concatB = findEntry(netMemo, "concat_b");
    if (!concatB || !concatB->value) {
        return fail("concat_b memo entry missing value");
    }
    const grh::Operation* concatBAssign = concatB->value->definingOp();
    if (!concatBAssign || concatBAssign->kind() != grh::OperationKind::kAssign) {
        return fail("concat_b is not driven by kAssign");
    }
    const grh::Operation* concatBConcat = concatBAssign->operands().front()->definingOp();
    if (!concatBConcat || concatBConcat->kind() != grh::OperationKind::kConcat) {
        return fail("concat_b assign should use kConcat");
    }
    if (concatBConcat->operands().size() != 2) {
        return fail("concat_b concat should have two operands (zero-fill + slice)");
    }
    if (!concatBConcat->operands()[0]->definingOp() ||
        concatBConcat->operands()[0]->definingOp()->kind() != grh::OperationKind::kConstant ||
        concatBConcat->operands()[0]->width() != 2) {
        return fail("concat_b zero-fill operand has unexpected shape");
    }
    const grh::Value* concatBSliceValue = concatBConcat->operands()[1];
    if (!concatBSliceValue || concatBSliceValue->width() != 2) {
        return fail("concat_b slice operand has unexpected width");
    }
    if (!concatBSliceValue->definingOp() ||
        concatBSliceValue->definingOp()->kind() != grh::OperationKind::kSliceStatic) {
        return fail("concat_b slice operand is expected to be created via kSliceStatic");
    }

    if (!writeArtifact(netlist)) {
        return fail("Failed to write assign artifact JSON");
    }
    return 0;
}
