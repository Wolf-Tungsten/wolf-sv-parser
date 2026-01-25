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

using namespace wolf_sv_parser;
using namespace grh::emit;

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

ValueId findPort(const grh::ir::Graph& graph, std::string_view name, bool isInput) {
    const auto ports = isInput ? graph.inputPorts() : graph.outputPorts();
    for (const auto& port : ports) {
        if (graph.symbolText(port.name) == name) {
            return port.value;
        }
    }
    return ValueId::invalid();
}

bool writeArtifact(const grh::ir::Netlist& netlist) {
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
    ElaborateOptions elaborateOptions;
    elaborateOptions.abortOnError = false;
    Elaborate elaborator(&diagnostics, elaborateOptions);
    grh::ir::Netlist netlist = elaborator.convert(compilation->getRoot());

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

    grh::ir::Graph* graph = netlist.findGraph("assign_stage11_case");
    if (!graph) {
        return fail("GRH graph assign_stage11_case not found");
    }
    auto opForValue = [&](ValueId value) -> OperationId {
        return value ? graph->getValue(value).definingOp() : OperationId::invalid();
    };

    const slang::ast::InstanceBodySymbol* canonicalBody = topInstance->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body = canonicalBody ? *canonicalBody : topInstance->body;
    std::span<const SignalMemoEntry> netMemo = elaborator.peekNetMemo(body);
    if (netMemo.empty()) {
        return fail("Net memo is empty for assign_stage11_case");
    }

    ValueId portInA = findPort(*graph, "in_a", /* isInput */ true);
    ValueId portInB = findPort(*graph, "in_b", /* isInput */ true);
    if (!portInA || !portInB) {
        return fail("Input ports in_a/in_b not registered in graph");
    }

    // scalar_net should directly connect to in_a via kAssign.
    const SignalMemoEntry* scalarNet = findEntry(netMemo, "scalar_net");
    if (!scalarNet || !scalarNet->value) {
        return fail("scalar_net memo entry missing value");
    }
    OperationId scalarAssignId = opForValue(scalarNet->value);
    if (!scalarAssignId || graph->getOperation(scalarAssignId).kind() != grh::ir::OperationKind::kAssign) {
        return fail("scalar_net is not driven by kAssign");
    }
    const grh::ir::Operation scalarAssign = graph->getOperation(scalarAssignId);
    if (scalarAssign.operands().size() != 1 || scalarAssign.operands().front() != portInA) {
        return fail("scalar_net assign operand does not reference in_a");
    }

    // struct_net should aggregate three slices (hi, lo[3:2], lo[1:0]).
    const SignalMemoEntry* structNet = findEntry(netMemo, "struct_net");
    if (!structNet || !structNet->value) {
        return fail("struct_net memo entry missing value");
    }
    OperationId structAssignId = opForValue(structNet->value);
    if (!structAssignId || graph->getOperation(structAssignId).kind() != grh::ir::OperationKind::kAssign) {
        return fail("struct_net is not driven by kAssign");
    }
    const grh::ir::Operation structAssign = graph->getOperation(structAssignId);
    if (structAssign.operands().empty()) {
        return fail("struct_net assign has no operand");
    }
    ValueId structComposite = structAssign.operands().front();
    OperationId structConcatId = opForValue(structComposite);
    if (!structConcatId || graph->getOperation(structConcatId).kind() != grh::ir::OperationKind::kConcat) {
        return fail("struct_net assign is expected to use kConcat");
    }
    const grh::ir::Operation structConcat = graph->getOperation(structConcatId);
    if (structConcat.operands().size() != 3) {
        return fail("struct_net concat should have three operands");
    }
    OperationId hiSliceId = opForValue(structConcat.operands()[0]);
    if (hiSliceId) {
        const grh::ir::Operation hiSlice = graph->getOperation(hiSliceId);
        if (hiSlice.kind() != grh::ir::OperationKind::kSliceStatic ||
            hiSlice.operands().empty() || hiSlice.operands().front() != portInA) {
            return fail("struct_net hi slice does not originate from in_a");
        }
    }
    bool loSlicesFromB = true;
    for (std::size_t idx = 1; idx < structConcat.operands().size(); ++idx) {
        OperationId sliceOpId = opForValue(structConcat.operands()[idx]);
        if (!sliceOpId) {
            loSlicesFromB = false;
            break;
        }
        const grh::ir::Operation sliceOp = graph->getOperation(sliceOpId);
        if (sliceOp.kind() != grh::ir::OperationKind::kSliceStatic ||
            sliceOp.operands().empty() || sliceOp.operands().front() != portInB) {
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
    OperationId arrayAssignId = opForValue(arrayNet->value);
    if (!arrayAssignId || graph->getOperation(arrayAssignId).kind() != grh::ir::OperationKind::kAssign) {
        return fail("array_net is not driven by kAssign");
    }
    const grh::ir::Operation arrayAssign = graph->getOperation(arrayAssignId);
    ValueId arrayComposite = arrayAssign.operands().front();
    OperationId arrayConcatId = opForValue(arrayComposite);
    if (!arrayConcatId || graph->getOperation(arrayConcatId).kind() != grh::ir::OperationKind::kConcat) {
        return fail("array_net assign should use kConcat");
    }
    bool hasZeroFill = false;
    bool hasSliceFromA = false;
    bool hasSliceFromB = false;
    const grh::ir::Operation arrayConcat = graph->getOperation(arrayConcatId);
    for (ValueId operand : arrayConcat.operands()) {
        if (!operand) {
            continue;
        }
        OperationId opId = opForValue(operand);
        if (opId) {
            const grh::ir::Operation op = graph->getOperation(opId);
            if (op.kind() == grh::ir::OperationKind::kConstant) {
                hasZeroFill = true;
            }
            else if (op.kind() == grh::ir::OperationKind::kSliceStatic && !op.operands().empty()) {
                if (op.operands().front() == portInA) {
                    hasSliceFromA = true;
                }
                if (op.operands().front() == portInB) {
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
    OperationId partialAssignId = opForValue(partialNet->value);
    if (!partialAssignId || graph->getOperation(partialAssignId).kind() != grh::ir::OperationKind::kAssign) {
        return fail("partial_net is not driven by kAssign");
    }
    const grh::ir::Operation partialAssign = graph->getOperation(partialAssignId);
    ValueId partialComposite = partialAssign.operands().front();
    OperationId partialConcatId = opForValue(partialComposite);
    if (!partialConcatId || graph->getOperation(partialConcatId).kind() != grh::ir::OperationKind::kConcat) {
        return fail("partial_net assign should produce kConcat");
    }
    bool sawUnitZero = false;
    const grh::ir::Operation partialConcat = graph->getOperation(partialConcatId);
    for (ValueId operand : partialConcat.operands()) {
        OperationId operandOpId = opForValue(operand);
        if (operandOpId &&
            graph->getOperation(operandOpId).kind() == grh::ir::OperationKind::kConstant &&
            graph->getValue(operand).width() == 1) {
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
    OperationId concatBAssignId = opForValue(concatB->value);
    if (!concatBAssignId || graph->getOperation(concatBAssignId).kind() != grh::ir::OperationKind::kAssign) {
        return fail("concat_b is not driven by kAssign");
    }
    const grh::ir::Operation concatBAssign = graph->getOperation(concatBAssignId);
    ValueId concatBComposite = concatBAssign.operands().front();
    OperationId concatBConcatId = opForValue(concatBComposite);
    if (!concatBConcatId || graph->getOperation(concatBConcatId).kind() != grh::ir::OperationKind::kConcat) {
        return fail("concat_b assign should use kConcat");
    }
    const grh::ir::Operation concatBConcat = graph->getOperation(concatBConcatId);
    if (concatBConcat.operands().size() != 2) {
        return fail("concat_b concat should have two operands (zero-fill + slice)");
    }
    OperationId concatBZeroOpId = opForValue(concatBConcat.operands()[0]);
    if (!concatBZeroOpId ||
        graph->getOperation(concatBZeroOpId).kind() != grh::ir::OperationKind::kConstant ||
        graph->getValue(concatBConcat.operands()[0]).width() != 2) {
        return fail("concat_b zero-fill operand has unexpected shape");
    }
    ValueId concatBSliceValue = concatBConcat.operands()[1];
    if (!concatBSliceValue || graph->getValue(concatBSliceValue).width() != 2) {
        return fail("concat_b slice operand has unexpected width");
    }
    OperationId concatBSliceOpId = opForValue(concatBSliceValue);
    if (!concatBSliceOpId ||
        graph->getOperation(concatBSliceOpId).kind() != grh::ir::OperationKind::kSliceStatic) {
        return fail("concat_b slice operand is expected to be created via kSliceStatic");
    }

    if (!writeArtifact(netlist)) {
        return fail("Failed to write assign artifact JSON");
    }
    return 0;
}
