#include "elaborate.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <variant>
#include <system_error>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;

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

bool writeArtifact(const grh::Netlist& netlist) {
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
    os << netlist.toJsonString(true);
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
    grh::Netlist netlist = elaborator.convert(compilation->getRoot());

    grh::Graph* graph = netlist.findGraph("rhs_converter_case");
    if (!graph) {
        return fail("GRH graph rhs_converter_case not found");
    }

    const slang::ast::InstanceBodySymbol* canonicalBody = top->getCanonicalBody();
    const slang::ast::InstanceBodySymbol& body =
        canonicalBody ? *canonicalBody : top->body;

    std::span<const SignalMemoEntry> netMemo = elaborator.peekNetMemo(body);
    std::span<const SignalMemoEntry> regMemo = elaborator.peekRegMemo(body);
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
        .origin = origin,
        .diagnostics = &diagnostics};
    CombRHSConverter converter(context);

    auto convertByName = [&](std::string_view name) -> grh::Value* {
        auto it = rhsMap.find(std::string(name));
        if (it == rhsMap.end()) {
            return nullptr;
        }
        return converter.convert(*it->second);
    };

    const SignalMemoEntry* netA = findMemoEntry(netMemo, "net_a");
    const SignalMemoEntry* netB = findMemoEntry(netMemo, "net_b");
    const SignalMemoEntry* seqReg = findMemoEntry(regMemo, "seq_reg");
    if (!netA || !netA->value || !netB || !netB->value || !seqReg || !seqReg->stateOp) {
        return fail("Missing memo entries for required signals");
    }

    grh::Value* addValue = convertByName("add_res");
    if (!addValue) {
        return fail("Failed to convert add_res RHS");
    }
    const grh::Operation* addOp = addValue->definingOp();
    if (!addOp || addOp->kind() != grh::OperationKind::kAdd || addOp->operands().size() != 2) {
        return fail("add_res did not create kAdd operation");
    }
    if (addOp->operands()[0] != netA->value || addOp->operands()[1] != netB->value) {
        return fail("kAdd operands do not map to memoized values");
    }

    grh::Value* flagValue = convertByName("flag_res");
    if (!flagValue) {
        return fail("Failed to convert flag_res RHS");
    }
    const grh::Operation* flagOp = flagValue->definingOp();
    if (!flagOp || flagOp->kind() != grh::OperationKind::kLogicAnd ||
        flagOp->operands().size() != 2) {
        return fail("flag_res did not generate kLogicAnd");
    }
    const grh::Operation* eqOp = flagOp->operands()[0]->definingOp();
    if (!eqOp || eqOp->kind() != grh::OperationKind::kEq) {
        return fail("flag_res equality operand missing");
    }
    if (eqOp->operands().size() != 2 || eqOp->operands()[0] != netA->value ||
        eqOp->operands()[1] != netB->value) {
        return fail("Equality operands not tied to memo entries");
    }
    grh::Value* ctrlSelValue = graph->findValue("ctrl_sel");
    if (!ctrlSelValue || flagOp->operands()[1] != ctrlSelValue) {
        return fail("flag_res control operand mismatch");
    }

    grh::Value* muxValue = convertByName("mux_res");
    if (!muxValue) {
        return fail("Failed to convert mux_res RHS");
    }
    const grh::Operation* muxOp = muxValue->definingOp();
    if (!muxOp || muxOp->kind() != grh::OperationKind::kMux || muxOp->operands().size() != 3) {
        return fail("mux_res did not create kMux");
    }
    if (muxOp->operands()[0] != ctrlSelValue || muxOp->operands()[1] != netA->value ||
        muxOp->operands()[2] != netB->value) {
        return fail("kMux operands mismatch");
    }

    grh::Value* concatValue = convertByName("concat_res");
    if (!concatValue) {
        return fail("Failed to convert concat_res RHS");
    }
    const grh::Operation* concatOp = concatValue->definingOp();
    if (!concatOp || concatOp->kind() != grh::OperationKind::kConcat ||
        concatOp->operands().size() != 2) {
        return fail("concat_res did not create kConcat");
    }
    if (concatValue->width() != 16 || concatOp->operands()[0] != netA->value ||
        concatOp->operands()[1] != netB->value) {
        return fail("kConcat result width/operands unexpected");
    }

    grh::Value* replicateValue = convertByName("replicate_res");
    if (!replicateValue) {
        return fail("Failed to convert replicate_res RHS");
    }
    const grh::Operation* replicateOp = replicateValue->definingOp();
    if (replicateOp && replicateOp->kind() == grh::OperationKind::kAssign &&
        !replicateOp->operands().empty()) {
        replicateOp = replicateOp->operands()[0]->definingOp();
    }
    if (!replicateOp || replicateOp->kind() != grh::OperationKind::kReplicate ||
        replicateOp->operands().size() != 1) {
        return fail("replicate_res missing kReplicate");
    }
    auto attrIt = replicateOp->attributes().find("rep");
    if (attrIt == replicateOp->attributes().end() ||
        !std::holds_alternative<int64_t>(attrIt->second) ||
        std::get<int64_t>(attrIt->second) != 4) {
        return fail("kReplicate missing rep=4 attribute");
    }
    grh::Value* replicateOperand = replicateOp->operands()[0];
    if (replicateOperand != ctrlSelValue) {
        const grh::Operation* concatOperand = replicateOperand->definingOp();
        if (!concatOperand || concatOperand->kind() != grh::OperationKind::kConcat ||
            concatOperand->operands().size() != 1 ||
            concatOperand->operands()[0] != ctrlSelValue) {
            return fail("kReplicate operand mismatch");
        }
    }

    grh::Value* reduceValue = convertByName("reduce_res");
    if (!reduceValue) {
        return fail("Failed to convert reduce_res RHS");
    }
    const grh::Operation* reduceOp = reduceValue->definingOp();
    if (!reduceOp || reduceOp->kind() != grh::OperationKind::kReduceAnd ||
        reduceOp->operands().size() != 1 || reduceOp->operands()[0] != netA->value) {
        return fail("reduce_res not modeled as kReduceAnd");
    }
    if (reduceValue->width() != 1) {
        return fail("Reduction result width is not 1");
    }

    grh::Value* constValue = convertByName("const_res");
    if (!constValue) {
        return fail("Failed to convert const_res RHS");
    }
    const grh::Operation* constOp = constValue->definingOp();
    if (!constOp || constOp->kind() != grh::OperationKind::kConstant) {
        return fail("const_res did not materialize kConstant");
    }
    auto constAttr = constOp->attributes().find("constValue");
    if (constAttr == constOp->attributes().end() ||
        !std::holds_alternative<std::string>(constAttr->second)) {
        return fail("kConstant attribute mismatch");
    }
    const std::string& literalAttr = std::get<std::string>(constAttr->second);
    if (literalAttr != "8'haa") {
        std::cerr << "[rhs_converter] const literal=" << literalAttr << '\n';
        return fail("kConstant attribute mismatch");
    }

    grh::Value* mixValue = convertByName("mix_res");
    if (!mixValue) {
        return fail("Failed to convert mix_res RHS");
    }
    const grh::Operation* xorOp = mixValue->definingOp();
    if (!xorOp || xorOp->kind() != grh::OperationKind::kXor || xorOp->operands().size() != 2) {
        return fail("mix_res missing final kXor");
    }
    const grh::Operation* subOp = xorOp->operands()[0]->definingOp();
    const grh::Operation* notOp = xorOp->operands()[1]->definingOp();
    if (!subOp || subOp->kind() != grh::OperationKind::kSub || subOp->operands().size() != 2) {
        return fail("mix_res subtraction not created");
    }
    if (subOp->operands()[0] != netA->value || subOp->operands()[1] != netB->value) {
        return fail("mix_res subtraction operands mismatch");
    }
    if (!notOp || notOp->kind() != grh::OperationKind::kNot) {
        return fail("mix_res missing bitwise not");
    }

    grh::Value* regUseValue = convertByName("reg_use");
    if (!regUseValue) {
        return fail("Failed to convert reg_use RHS");
    }
    const grh::Operation* regAddOp = regUseValue->definingOp();
    if (!regAddOp || regAddOp->kind() != grh::OperationKind::kAdd ||
        regAddOp->operands().size() != 2) {
        return fail("reg_use did not create kAdd");
    }
    grh::Value* seqRegValue = seqReg->value;
    if (!seqRegValue && seqReg->stateOp && !seqReg->stateOp->results().empty()) {
        seqRegValue = seqReg->stateOp->results().front();
    }
    if (!seqRegValue) {
        return fail("seq_reg memo missing accessible value");
    }
    bool regOperandMatch = (regAddOp->operands()[0] == seqRegValue) ||
                           (regAddOp->operands()[1] == seqRegValue);
    if (!regOperandMatch) {
        return fail("reg_use kAdd missing register operand");
    }

    grh::Value* structSlice = convertByName("struct_hi_slice");
    if (!structSlice) {
        return fail("Failed to convert struct_hi_slice RHS");
    }
    const SignalMemoEntry* structEntry = findMemoEntry(netMemo, "struct_bus");
    if (!structEntry || !structEntry->value) {
        return fail("struct_bus memo missing");
    }
    const grh::Operation* structSliceOp = structSlice->definingOp();
    if (!structSliceOp || structSliceOp->kind() != grh::OperationKind::kSliceStatic ||
        structSliceOp->operands().size() != 1 ||
        structSliceOp->operands()[0] != structEntry->value) {
        return fail("struct_hi_slice not modeled via kSliceStatic");
    }
    auto structStart = structSliceOp->attributes().find("sliceStart");
    auto structEnd = structSliceOp->attributes().find("sliceEnd");
    if (structStart == structSliceOp->attributes().end() ||
        structEnd == structSliceOp->attributes().end() ||
        !std::holds_alternative<int64_t>(structStart->second) ||
        !std::holds_alternative<int64_t>(structEnd->second) ||
        std::get<int64_t>(structStart->second) != 4 ||
        std::get<int64_t>(structEnd->second) != 7) {
        return fail("struct_hi_slice slice range mismatch");
    }

    grh::Value* staticSlice = convertByName("static_slice_res");
    if (!staticSlice) {
        return fail("Failed to convert static_slice_res RHS");
    }
    grh::Value* rangeBus = graph->findValue("range_bus");
    if (!rangeBus) {
        return fail("range_bus value missing");
    }
    const grh::Operation* staticOp = staticSlice->definingOp();
    if (!staticOp || staticOp->kind() != grh::OperationKind::kSliceStatic ||
        staticOp->operands().size() != 1 || staticOp->operands()[0] != rangeBus) {
        return fail("static_slice_res missing kSliceStatic");
    }
    auto staticWidthStart = staticOp->attributes().find("sliceStart");
    auto staticWidthEnd = staticOp->attributes().find("sliceEnd");
    if (staticWidthStart == staticOp->attributes().end() ||
        staticWidthEnd == staticOp->attributes().end() ||
        !std::holds_alternative<int64_t>(staticWidthStart->second) ||
        !std::holds_alternative<int64_t>(staticWidthEnd->second) ||
        std::get<int64_t>(staticWidthStart->second) != 4 ||
        std::get<int64_t>(staticWidthEnd->second) != 11) {
        return fail("static_slice_res slice bounds mismatch");
    }

    grh::Value* dynSlice = convertByName("dynamic_slice_res");
    if (!dynSlice) {
        return fail("Failed to convert dynamic_slice_res RHS");
    }
    const grh::Operation* dynOp = dynSlice->definingOp();
    if (!dynOp || dynOp->kind() != grh::OperationKind::kSliceDynamic ||
        dynOp->operands().size() != 2 || dynOp->operands()[0] != rangeBus) {
        return fail("dynamic_slice_res missing kSliceDynamic");
    }
    auto dynAttr = dynOp->attributes().find("sliceWidth");
    if (dynAttr == dynOp->attributes().end() ||
        !std::holds_alternative<int64_t>(dynAttr->second) ||
        std::get<int64_t>(dynAttr->second) != 8) {
        return fail("dynamic_slice_res sliceWidth mismatch");
    }

    grh::Value* arraySlice = convertByName("array_slice_res");
    if (!arraySlice) {
        return fail("Failed to convert array_slice_res RHS");
    }
    const SignalMemoEntry* arrayEntry = findMemoEntry(netMemo, "net_array");
    if (!arrayEntry || !arrayEntry->value) {
        return fail("net_array memo missing");
    }
    grh::Value* arrayIndexValue = graph->findValue("array_index");
    if (!arrayIndexValue) {
        return fail("array_index value missing");
    }
    const grh::Operation* arrayOp = arraySlice->definingOp();
    if (!arrayOp || arrayOp->kind() != grh::OperationKind::kSliceArray ||
        arrayOp->operands().size() != 2 || arrayOp->operands()[0] != arrayEntry->value ||
        arrayOp->operands()[1] != arrayIndexValue) {
        return fail("array_slice_res missing kSliceArray");
    }
    auto arrayAttr = arrayOp->attributes().find("sliceWidth");
    if (arrayAttr == arrayOp->attributes().end() ||
        !std::holds_alternative<int64_t>(arrayAttr->second) ||
        std::get<int64_t>(arrayAttr->second) != 8) {
        return fail("array_slice_res sliceWidth mismatch");
    }

    grh::Value* memRead = convertByName("mem_read_res");
    if (!memRead) {
        return fail("Failed to convert mem_read_res RHS");
    }
    const SignalMemoEntry* memEntry = findMemoEntry(regMemo, "reg_mem");
    if (!memEntry || !memEntry->stateOp ||
        memEntry->stateOp->kind() != grh::OperationKind::kMemory) {
        return fail("reg_mem memo missing kMemory placeholder");
    }
    grh::Value* memAddrValue = graph->findValue("mem_addr");
    if (!memAddrValue) {
        return fail("mem_addr value missing");
    }
    const grh::Operation* memOp = memRead->definingOp();
    if (!memOp || memOp->kind() != grh::OperationKind::kMemoryAsyncReadPort ||
        memOp->operands().size() != 1 || memOp->operands()[0] != memAddrValue) {
        return fail("mem_read_res missing kMemoryAsyncReadPort");
    }
    auto memAttr = memOp->attributes().find("memSymbol");
    if (memAttr == memOp->attributes().end() ||
        !std::holds_alternative<std::string>(memAttr->second) ||
        std::get<std::string>(memAttr->second) != memEntry->stateOp->symbol()) {
        return fail("mem_read_res memSymbol attribute mismatch");
    }

    writeArtifact(netlist);

    return 0;
}
