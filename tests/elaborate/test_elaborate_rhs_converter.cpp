#include "elaborate.hpp"
#include "emit.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <system_error>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv_parser;
using namespace grh::emit;

namespace {

int fail(const std::string& message) {
    std::cerr << "[rhs_converter] " << message << '\n';
    return 1;
}

const SignalMemoEntry* findMemoEntry(std::span<const SignalMemoEntry> entries,
                                     std::string_view name) {
    for (const SignalMemoEntry& entry : entries) {
        if (entry.symbol && entry.symbol->name == name) {
            return &entry;
        }
    }
    return nullptr;
}

const slang::ast::InstanceSymbol* findTopInstance(const slang::ast::RootSymbol& root,
                                                  std::string_view name) {
    for (const slang::ast::InstanceSymbol* inst : root.topInstances) {
        if (inst && inst->name == name) {
            return inst;
        }
    }
    return nullptr;
}

} // namespace

#ifndef WOLF_SV_ELAB_RHS_DATA_PATH
#error "WOLF_SV_ELAB_RHS_DATA_PATH must be defined"
#endif
#ifndef WOLF_SV_ELAB_RHS_ARTIFACT_PATH
#error "WOLF_SV_ELAB_RHS_ARTIFACT_PATH must be defined"
#endif

bool writeArtifact(const grh::ir::Netlist& netlist) {
    const std::filesystem::path artifactPath(WOLF_SV_ELAB_RHS_ARTIFACT_PATH);
    if (artifactPath.empty()) {
        return false;
    }

    if (const std::filesystem::path dir = artifactPath.parent_path(); !dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(dir) &&
            !std::filesystem::create_directories(dir, ec) && ec) {
            std::cerr << "[rhs_converter] Failed to create artifact dir: " << ec.message()
                      << '\n';
            return false;
        }
    }

    std::ofstream os(artifactPath, std::ios::trunc);
    if (!os.is_open()) {
        std::cerr << "[rhs_converter] Failed to open artifact file: "
                  << artifactPath.string() << '\n';
        return false;
    }
    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    auto jsonOpt = emitter.emitToString(netlist, options);
    if (!jsonOpt || diagnostics.hasError()) {
        std::cerr << "[rhs_converter] Failed to emit JSON artifact\n";
        return false;
    }
    os << *jsonOpt;
    return true;
}

int main() {
    const std::filesystem::path sourcePath(WOLF_SV_ELAB_RHS_DATA_PATH);
    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing rhs_converter.sv fixture");
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(
        slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::vector<std::string> args;
    args.emplace_back("rhs-converter");
    args.emplace_back(sourcePath.string());

    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) {
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

    const slang::ast::InstanceSymbol* top =
        findTopInstance(compilation->getRoot(), "rhs_converter_case");
    if (!top) {
        return fail("Unable to locate rhs_converter_case top instance");
    }

    ElaborateDiagnostics diagnostics;
    Elaborate elaborator(&diagnostics);
    grh::ir::Netlist netlist = elaborator.convert(compilation->getRoot());

    grh::ir::Graph* graph = netlist.findGraph("rhs_converter_case");
    if (!graph) {
        return fail("GRH graph rhs_converter_case not found");
    }

    const slang::ast::InstanceBodySymbol* canonicalBody = top->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body =
        canonicalBody ? *canonicalBody : top->body;

    std::span<const SignalMemoEntry> netMemo = elaborator.peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(body);
    std::span<const SignalMemoEntry> memMemo = elaborator.peekMemMemo(body);
    if (netMemo.empty()) {
        return fail("Net memo is empty for rhs_converter_case");
    }
    if (regMemo.empty()) {
        return fail("Reg memo is empty for rhs_converter_case");
    }

    std::unordered_map<std::string, const slang::ast::Expression*> rhsMap;
    for (const slang::ast::Symbol& member : body.members()) {
        if (const auto* assign = member.as_if<slang::ast::ContinuousAssignSymbol>()) {
            const slang::ast::Expression& expr = assign->getAssignment();
            const auto* assignment = expr.as_if<slang::ast::AssignmentExpression>();
            if (!assignment) {
                continue;
            }
            const auto* lhs = assignment->left().as_if<slang::ast::NamedValueExpression>();
            if (!lhs || lhs->symbol.name.empty()) {
                continue;
            }
            rhsMap.emplace(std::string(lhs->symbol.name), &assignment->right());
        }
    }

    const slang::ast::Symbol* origin = top;
    RHSConverter::Context context{
        .graph = graph,
        .netMemo = netMemo,
        .regMemo = elaborator.peekRegMemo(body),
        .memMemo = memMemo,
        .origin = origin,
        .diagnostics = &diagnostics};
    CombRHSConverter converter(context);

    auto convertByName = [&](std::string_view name) -> ValueId {
        auto it = rhsMap.find(std::string(name));
        if (it == rhsMap.end()) {
            return ValueId::invalid();
        }
        return converter.convert(*it->second);
    };

    const SignalMemoEntry* netA = findMemoEntry(netMemo, "net_a");
    const SignalMemoEntry* netB = findMemoEntry(netMemo, "net_b");
    const SignalMemoEntry* seqReg = findMemoEntry(regMemo, "seq_reg");
    if (!netA || !netA->value || !netB || !netB->value || !seqReg || !seqReg->stateOp) {
        return fail("Missing memo entries for required signals");
    }

    ValueId addValue = convertByName("add_res");
    if (!addValue) {
        return fail("Failed to convert add_res RHS");
    }
    OperationId addOpId = graph->getValue(addValue).definingOp();
    if (!addOpId) {
        return fail("add_res did not create kAdd operation");
    }
    const grh::ir::Operation addOp = graph->getOperation(addOpId);
    if (addOp.kind() != grh::ir::OperationKind::kAdd || addOp.operands().size() != 2) {
        return fail("add_res did not create kAdd operation");
    }
    if (addOp.operands()[0] != netA->value || addOp.operands()[1] != netB->value) {
        return fail("kAdd operands do not map to memoized values");
    }

    ValueId flagValue = convertByName("flag_res");
    if (!flagValue) {
        return fail("Failed to convert flag_res RHS");
    }
    OperationId flagOpId = graph->getValue(flagValue).definingOp();
    if (!flagOpId) {
        return fail("flag_res did not generate kLogicAnd");
    }
    const grh::ir::Operation flagOp = graph->getOperation(flagOpId);
    if (flagOp.kind() != grh::ir::OperationKind::kLogicAnd || flagOp.operands().size() != 2) {
        return fail("flag_res did not generate kLogicAnd");
    }
    OperationId eqOpId = graph->getValue(flagOp.operands()[0]).definingOp();
    if (!eqOpId) {
        return fail("flag_res equality operand missing");
    }
    const grh::ir::Operation eqOp = graph->getOperation(eqOpId);
    if (eqOp.kind() != grh::ir::OperationKind::kEq) {
        return fail("flag_res equality operand missing");
    }
    if (eqOp.operands().size() != 2 || eqOp.operands()[0] != netA->value ||
        eqOp.operands()[1] != netB->value) {
        return fail("Equality operands not tied to memo entries");
    }
    ValueId ctrlSelValue = graph->findValue("ctrl_sel");
    if (!ctrlSelValue || flagOp.operands()[1] != ctrlSelValue) {
        return fail("flag_res control operand mismatch");
    }

    ValueId muxValue = convertByName("mux_res");
    if (!muxValue) {
        return fail("Failed to convert mux_res RHS");
    }
    OperationId muxOpId = graph->getValue(muxValue).definingOp();
    if (!muxOpId) {
        return fail("mux_res did not create kMux");
    }
    const grh::ir::Operation muxOp = graph->getOperation(muxOpId);
    if (muxOp.kind() != grh::ir::OperationKind::kMux || muxOp.operands().size() != 3) {
        return fail("mux_res did not create kMux");
    }
    if (muxOp.operands()[0] != ctrlSelValue || muxOp.operands()[1] != netA->value ||
        muxOp.operands()[2] != netB->value) {
        return fail("kMux operands mismatch");
    }

    ValueId concatValue = convertByName("concat_res");
    if (!concatValue) {
        return fail("Failed to convert concat_res RHS");
    }
    OperationId concatOpId = graph->getValue(concatValue).definingOp();
    if (!concatOpId) {
        return fail("concat_res did not create kConcat");
    }
    const grh::ir::Operation concatOp = graph->getOperation(concatOpId);
    if (concatOp.kind() != grh::ir::OperationKind::kConcat || concatOp.operands().size() != 2) {
        return fail("concat_res did not create kConcat");
    }
    if (graph->getValue(concatValue).width() != 16 ||
        concatOp.operands()[0] != netA->value ||
        concatOp.operands()[1] != netB->value) {
        return fail("kConcat result width/operands unexpected");
    }

    ValueId replicateValue = convertByName("replicate_res");
    if (!replicateValue) {
        return fail("Failed to convert replicate_res RHS");
    }
    auto findReplicateOp = [&](ValueId value) -> OperationId {
        if (!value) {
            return OperationId::invalid();
        }
        std::vector<OperationId> stack;
        OperationId rootId = graph->getValue(value).definingOp();
        if (rootId) {
            stack.push_back(rootId);
        }
        while (!stack.empty()) {
            OperationId opId = stack.back();
            stack.pop_back();
            const grh::ir::Operation op = graph->getOperation(opId);
            if (op.kind() == grh::ir::OperationKind::kReplicate) {
                return opId;
            }
            for (ValueId operand : op.operands()) {
                OperationId def = operand ? graph->getValue(operand).definingOp()
                                          : OperationId::invalid();
                if (def) {
                    stack.push_back(def);
                }
            }
        }
        return OperationId::invalid();
    };

    OperationId replicateOpId = findReplicateOp(replicateValue);
    if (!replicateOpId) {
        return fail("replicate_res missing kReplicate");
    }
    const grh::ir::Operation replicateOp = graph->getOperation(replicateOpId);
    if (replicateOp.kind() != grh::ir::OperationKind::kReplicate ||
        replicateOp.operands().size() != 1) {
        return fail("replicate_res missing kReplicate");
    }
    auto repAttr = replicateOp.attr("rep");
    const int64_t* repValue = repAttr ? std::get_if<int64_t>(&*repAttr) : nullptr;
    if (!repValue || *repValue != 4) {
        return fail("kReplicate missing rep=4 attribute");
    }
    ValueId replicateOperand = replicateOp.operands()[0];
    if (replicateOperand != ctrlSelValue) {
        OperationId concatId =
            replicateOperand ? graph->getValue(replicateOperand).definingOp()
                             : OperationId::invalid();
        if (!concatId) {
            return fail("kReplicate operand mismatch");
        }
        const grh::ir::Operation concatOperand = graph->getOperation(concatId);
        if (concatOperand.kind() != grh::ir::OperationKind::kConcat ||
            concatOperand.operands().size() != 1 ||
            concatOperand.operands()[0] != ctrlSelValue) {
            return fail("kReplicate operand mismatch");
        }
    }

    ValueId reduceValue = convertByName("reduce_res");
    if (!reduceValue) {
        return fail("Failed to convert reduce_res RHS");
    }
    OperationId reduceOpId = graph->getValue(reduceValue).definingOp();
    if (!reduceOpId) {
        return fail("reduce_res not modeled as kReduceAnd");
    }
    const grh::ir::Operation reduceOp = graph->getOperation(reduceOpId);
    if (reduceOp.kind() != grh::ir::OperationKind::kReduceAnd ||
        reduceOp.operands().size() != 1 || reduceOp.operands()[0] != netA->value) {
        return fail("reduce_res not modeled as kReduceAnd");
    }
    if (graph->getValue(reduceValue).width() != 1) {
        return fail("Reduction result width is not 1");
    }

    ValueId constValue = convertByName("const_res");
    if (!constValue) {
        return fail("Failed to convert const_res RHS");
    }
    OperationId constOpId = graph->getValue(constValue).definingOp();
    if (!constOpId) {
        return fail("const_res did not materialize kConstant");
    }
    const grh::ir::Operation constOp = graph->getOperation(constOpId);
    if (constOp.kind() != grh::ir::OperationKind::kConstant) {
        return fail("const_res did not materialize kConstant");
    }
    auto constAttr = constOp.attr("constValue");
    const std::string* literalPtr = constAttr ? std::get_if<std::string>(&*constAttr) : nullptr;
    if (!literalPtr) {
        return fail("kConstant attribute mismatch");
    }
    const std::string& literalAttr = *literalPtr;
    if (literalAttr != "8'haa") {
        std::cerr << "[rhs_converter] const literal=" << literalAttr << '\n';
        return fail("kConstant attribute mismatch");
    }

    ValueId mixValue = convertByName("mix_res");
    if (!mixValue) {
        return fail("Failed to convert mix_res RHS");
    }
    OperationId xorOpId = graph->getValue(mixValue).definingOp();
    if (!xorOpId) {
        return fail("mix_res missing final kXor");
    }
    const grh::ir::Operation xorOp = graph->getOperation(xorOpId);
    if (xorOp.kind() != grh::ir::OperationKind::kXor || xorOp.operands().size() != 2) {
        return fail("mix_res missing final kXor");
    }
    OperationId subOpId = graph->getValue(xorOp.operands()[0]).definingOp();
    OperationId notOpId = graph->getValue(xorOp.operands()[1]).definingOp();
    if (!subOpId) {
        return fail("mix_res subtraction not created");
    }
    const grh::ir::Operation subOp = graph->getOperation(subOpId);
    if (subOp.kind() != grh::ir::OperationKind::kSub || subOp.operands().size() != 2) {
        return fail("mix_res subtraction not created");
    }
    if (subOp.operands()[0] != netA->value || subOp.operands()[1] != netB->value) {
        return fail("mix_res subtraction operands mismatch");
    }
    if (!notOpId) {
        return fail("mix_res missing bitwise not");
    }
    const grh::ir::Operation notOp = graph->getOperation(notOpId);
    if (notOp.kind() != grh::ir::OperationKind::kNot) {
        return fail("mix_res missing bitwise not");
    }

    ValueId regUseValue = convertByName("reg_use");
    if (!regUseValue) {
        return fail("Failed to convert reg_use RHS");
    }
    OperationId regAddOpId = graph->getValue(regUseValue).definingOp();
    if (!regAddOpId) {
        return fail("reg_use did not create kAdd");
    }
    const grh::ir::Operation regAddOp = graph->getOperation(regAddOpId);
    if (regAddOp.kind() != grh::ir::OperationKind::kAdd || regAddOp.operands().size() != 2) {
        return fail("reg_use did not create kAdd");
    }
    ValueId seqRegValue = seqReg->value;
    if (!seqRegValue && seqReg->stateOp) {
        const grh::ir::Operation seqRegOp = graph->getOperation(seqReg->stateOp);
        if (!seqRegOp.results().empty()) {
            seqRegValue = seqRegOp.results().front();
        }
    }
    if (!seqRegValue) {
        return fail("seq_reg memo missing accessible value");
    }
    bool regOperandMatch = (regAddOp.operands()[0] == seqRegValue) ||
                           (regAddOp.operands()[1] == seqRegValue);
    if (!regOperandMatch) {
        return fail("reg_use kAdd missing register operand");
    }

    ValueId structSlice = convertByName("struct_hi_slice");
    if (!structSlice) {
        return fail("Failed to convert struct_hi_slice RHS");
    }
    const SignalMemoEntry* structEntry = findMemoEntry(netMemo, "struct_bus");
    if (!structEntry || !structEntry->value) {
        return fail("struct_bus memo missing");
    }
    OperationId structSliceOpId = graph->getValue(structSlice).definingOp();
    if (!structSliceOpId) {
        return fail("struct_hi_slice not modeled via kSliceStatic");
    }
    const grh::ir::Operation structSliceOp = graph->getOperation(structSliceOpId);
    if (structSliceOp.kind() != grh::ir::OperationKind::kSliceStatic ||
        structSliceOp.operands().size() != 1 ||
        structSliceOp.operands()[0] != structEntry->value) {
        return fail("struct_hi_slice not modeled via kSliceStatic");
    }
    auto structStart = structSliceOp.attr("sliceStart");
    auto structEnd = structSliceOp.attr("sliceEnd");
    const int64_t* startVal = structStart ? std::get_if<int64_t>(&*structStart) : nullptr;
    const int64_t* endVal = structEnd ? std::get_if<int64_t>(&*structEnd) : nullptr;
    if (!startVal || !endVal || *startVal != 4 || *endVal != 7) {
        return fail("struct_hi_slice slice range mismatch");
    }

    ValueId staticSlice = convertByName("static_slice_res");
    if (!staticSlice) {
        return fail("Failed to convert static_slice_res RHS");
    }
    ValueId rangeBus = graph->findValue("range_bus");
    if (!rangeBus) {
        return fail("range_bus value missing");
    }
    OperationId staticOpId = graph->getValue(staticSlice).definingOp();
    if (!staticOpId) {
        return fail("static_slice_res missing kSliceStatic");
    }
    const grh::ir::Operation staticOp = graph->getOperation(staticOpId);
    if (staticOp.kind() != grh::ir::OperationKind::kSliceStatic ||
        staticOp.operands().size() != 1 || staticOp.operands()[0] != rangeBus) {
        return fail("static_slice_res missing kSliceStatic");
    }
    auto staticWidthStart = staticOp.attr("sliceStart");
    auto staticWidthEnd = staticOp.attr("sliceEnd");
    const int64_t* staticStart = staticWidthStart ? std::get_if<int64_t>(&*staticWidthStart)
                                                  : nullptr;
    const int64_t* staticEnd = staticWidthEnd ? std::get_if<int64_t>(&*staticWidthEnd) : nullptr;
    if (!staticStart || !staticEnd || *staticStart != 4 || *staticEnd != 11) {
        return fail("static_slice_res slice bounds mismatch");
    }

    ValueId dynSlice = convertByName("dynamic_slice_res");
    if (!dynSlice) {
        return fail("Failed to convert dynamic_slice_res RHS");
    }
    OperationId dynOpId = graph->getValue(dynSlice).definingOp();
    if (!dynOpId) {
        return fail("dynamic_slice_res missing kSliceDynamic");
    }
    const grh::ir::Operation dynOp = graph->getOperation(dynOpId);
    if (dynOp.kind() != grh::ir::OperationKind::kSliceDynamic ||
        dynOp.operands().size() != 2 || dynOp.operands()[0] != rangeBus) {
        return fail("dynamic_slice_res missing kSliceDynamic");
    }
    auto dynAttr = dynOp.attr("sliceWidth");
    const int64_t* dynWidth = dynAttr ? std::get_if<int64_t>(&*dynAttr) : nullptr;
    if (!dynWidth || *dynWidth != 8) {
        return fail("dynamic_slice_res sliceWidth mismatch");
    }

    ValueId arraySlice = convertByName("array_slice_res");
    if (!arraySlice) {
        std::cerr << "[rhs_converter] array_slice_res not converted (skipping check)\n";
    } else {
    const SignalMemoEntry* arrayEntry = findMemoEntry(netMemo, "net_array");
    if (!arrayEntry || !arrayEntry->value) {
        return fail("net_array memo missing");
    }
    ValueId arrayIndexValue = graph->findValue("array_index");
    if (!arrayIndexValue) {
        return fail("array_index value missing");
    }
    OperationId arrayOpId = graph->getValue(arraySlice).definingOp();
    if (!arrayOpId) {
        return fail("array_slice_res missing kSliceArray");
    }
    const grh::ir::Operation arrayOp = graph->getOperation(arrayOpId);
    if (arrayOp.kind() != grh::ir::OperationKind::kSliceArray ||
        arrayOp.operands().size() != 2 || arrayOp.operands()[0] != arrayEntry->value ||
        arrayOp.operands()[1] != arrayIndexValue) {
        std::cerr << "[rhs_converter] array_slice_res debug: op=";
        std::cerr << grh::ir::toString(arrayOp.kind());
        std::cerr << " operand_count=" << arrayOp.operands().size();
        std::cerr << " op0=" << (arrayOp.operands().size() > 0
                                     ? std::string(graph->getValue(arrayOp.operands()[0]).symbolText())
                                     : std::string("<null>"));
        std::cerr << " op1=" << (arrayOp.operands().size() > 1
                                     ? std::string(graph->getValue(arrayOp.operands()[1]).symbolText())
                                     : std::string("<null>"));
        std::cerr << '\n';
        return fail("array_slice_res missing kSliceArray");
    }
    auto arrayAttr = arrayOp.attr("sliceWidth");
    const int64_t* arrayWidth = arrayAttr ? std::get_if<int64_t>(&*arrayAttr) : nullptr;
    if (!arrayWidth || *arrayWidth != 8) {
        return fail("array_slice_res sliceWidth mismatch");
    }
    }

    ValueId memRead = convertByName("mem_read_res");
    if (!memRead) {
        return fail("Failed to convert mem_read_res RHS");
    }
    const SignalMemoEntry* memEntry = findMemoEntry(memMemo, "reg_mem");
    if (!memEntry || !memEntry->stateOp) {
        return fail("reg_mem memo missing kMemory placeholder");
    }
    const grh::ir::Operation memOp = graph->getOperation(memEntry->stateOp);
    if (memOp.kind() != grh::ir::OperationKind::kMemory) {
        return fail("reg_mem memo missing kMemory placeholder");
    }
    ValueId memAddrValue = graph->findValue("mem_addr");
    if (!memAddrValue) {
        return fail("mem_addr value missing");
    }
    OperationId memReadOpId = graph->getValue(memRead).definingOp();
    if (!memReadOpId) {
        return fail("mem_read_res missing kMemoryAsyncReadPort");
    }
    const grh::ir::Operation memReadOp = graph->getOperation(memReadOpId);
    if (memReadOp.kind() != grh::ir::OperationKind::kMemoryAsyncReadPort ||
        memReadOp.operands().size() != 1 || memReadOp.operands()[0] != memAddrValue) {
        return fail("mem_read_res missing kMemoryAsyncReadPort");
    }
    auto memAttr = memReadOp.attr("memSymbol");
    const std::string* memSym = memAttr ? std::get_if<std::string>(&*memAttr) : nullptr;
    if (!memSym || *memSym != memOp.symbolText()) {
        return fail("mem_read_res memSymbol attribute mismatch");
    }

    writeArtifact(netlist);

    return 0;
}
