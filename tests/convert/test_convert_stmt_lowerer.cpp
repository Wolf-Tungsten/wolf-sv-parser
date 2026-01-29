#include "convert.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[convert-stmt-lowerer] " << message << '\n';
    return 1;
}

struct CompilationBundle {
    slang::driver::Driver driver;
    std::unique_ptr<slang::ast::Compilation> compilation;
};

std::unique_ptr<CompilationBundle> compileInput(const std::filesystem::path& sourcePath,
                                                std::string_view topModule) {
    auto bundle = std::make_unique<CompilationBundle>();
    auto& driver = bundle->driver;
    driver.addStandardArgs();
    driver.languageVersion = slang::LanguageVersion::v1800_2023;
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;
    if (!topModule.empty()) {
        driver.options.topModules.emplace_back(topModule);
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("convert-stmt-lowerer");
    argStorage.emplace_back(sourcePath.string());
    std::vector<const char*> argv;
    argv.reserve(argStorage.size());
    for (const std::string& arg : argStorage) {
        argv.push_back(arg.c_str());
    }

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data())) {
        return nullptr;
    }

    if (!driver.processOptions()) {
        return nullptr;
    }

    if (!driver.parseAllSources()) {
        return nullptr;
    }

    bundle->compilation = driver.createCompilation();
    if (!bundle->compilation) {
        return nullptr;
    }
    driver.reportCompilation(*bundle->compilation, /* quiet */ true);
    driver.runAnalysis(*bundle->compilation);
    return bundle;
}

const slang::ast::InstanceSymbol* findTopInstance(slang::ast::Compilation& compilation,
                                                  const slang::ast::RootSymbol& root,
                                                  std::string_view moduleName) {
    for (const slang::ast::InstanceSymbol* instance : root.topInstances) {
        if (!instance) {
            continue;
        }
        if (instance->getDefinition().name == moduleName) {
            return instance;
        }
    }
    if (root.topInstances.size() == 1 && root.topInstances[0]) {
        return root.topInstances[0];
    }
    if (const slang::ast::Symbol* symbol = root.find(moduleName)) {
        if (const auto* definition = symbol->as_if<slang::ast::DefinitionSymbol>()) {
            return &slang::ast::InstanceSymbol::createDefault(compilation, *definition);
        }
    }
    for (const slang::ast::Symbol* symbol : compilation.getDefinitions()) {
        if (!symbol) {
            continue;
        }
        if (const auto* definition = symbol->as_if<slang::ast::DefinitionSymbol>()) {
            if (definition->name == moduleName) {
                return &slang::ast::InstanceSymbol::createDefault(compilation, *definition);
            }
        }
    }
    return nullptr;
}

bool buildLoweringPlan(const std::filesystem::path& sourcePath, std::string_view topModule,
                       wolf_sv_parser::ConvertDiagnostics& diagnostics,
                       wolf_sv_parser::ModulePlan& outPlan,
                       wolf_sv_parser::LoweringPlan& outPlanLowering) {
    auto bundle = compileInput(sourcePath, topModule);
    if (!bundle || !bundle->compilation) {
        return false;
    }
    auto& compilation = *bundle->compilation;
    const slang::ast::RootSymbol& root = compilation.getRoot();
    const slang::ast::InstanceSymbol* top = findTopInstance(compilation, root, topModule);
    if (!top) {
        return false;
    }

    wolf_sv_parser::ConvertLogger logger;
    wolf_sv_parser::PlanCache planCache;
    wolf_sv_parser::PlanTaskQueue planQueue;
    planQueue.reset();

    wolf_sv_parser::ConvertContext context{};
    context.compilation = &root.getCompilation();
    context.root = &root;
    context.diagnostics = &diagnostics;
    context.logger = &logger;
    context.planCache = &planCache;
    context.planQueue = &planQueue;

    wolf_sv_parser::ModulePlanner planner(context);
    wolf_sv_parser::TypeResolverPass typeResolver(context);
    wolf_sv_parser::RWAnalyzerPass rwAnalyzer(context);
    wolf_sv_parser::ExprLowererPass exprLowerer(context);
    wolf_sv_parser::StmtLowererPass stmtLowerer(context);

    outPlan = planner.plan(top->body);
    typeResolver.resolve(outPlan);
    rwAnalyzer.analyze(outPlan);
    outPlanLowering = exprLowerer.lower(outPlan);
    stmtLowerer.lower(outPlan, outPlanLowering);
    return true;
}

bool hasOp(const wolf_sv_parser::LoweringPlan& plan, grh::ir::OperationKind op) {
    for (const auto& value : plan.values) {
        if (value.kind == wolf_sv_parser::ExprNodeKind::Operation && value.op == op) {
            return true;
        }
    }
    return false;
}

constexpr std::size_t kLargeLoopCount = 5000;

int testLowerer(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_case", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 4) {
        return fail("Expected 4 write intents in " + sourcePath.string());
    }

    std::unordered_map<std::string, int> targetCounts;
    std::size_t unguarded = 0;
    bool hasLogicNot = false;
    bool hasLogicAnd = false;
    for (const auto& write : lowering.writes) {
        if (write.target.valid()) {
            targetCounts[std::string(plan.symbolTable.text(write.target))]++;
        }
        if (write.guard == wolf_sv_parser::kInvalidPlanIndex) {
            ++unguarded;
            continue;
        }
        if (write.guard >= lowering.values.size()) {
            return fail("Write guard index out of range in " + sourcePath.string());
        }
        const auto& guardNode = lowering.values[write.guard];
        if (guardNode.kind == wolf_sv_parser::ExprNodeKind::Operation) {
            if (guardNode.op == grh::ir::OperationKind::kLogicNot) {
                hasLogicNot = true;
            }
            if (guardNode.op == grh::ir::OperationKind::kLogicAnd) {
                hasLogicAnd = true;
            }
        }
    }

    if (targetCounts["y"] != 2 || targetCounts["z"] != 1 || targetCounts["w"] != 1) {
        return fail("Unexpected write targets in " + sourcePath.string());
    }
    if (unguarded != 1) {
        return fail("Expected 1 unguarded write in " + sourcePath.string());
    }
    if (!hasLogicNot) {
        return fail("Missing logic-not guard in " + sourcePath.string());
    }
    if (!hasLogicAnd) {
        return fail("Missing logic-and guard in " + sourcePath.string());
    }

    std::size_t opCount = 0;
    for (const auto& value : lowering.values) {
        if (value.kind == wolf_sv_parser::ExprNodeKind::Operation) {
            ++opCount;
        }
    }
    if (lowering.tempSymbols.size() != opCount) {
        return fail("Temp symbol count does not match op count in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testIfChain(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_if_chain", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }
    if (plan.moduleSymbol.valid() &&
        plan.symbolTable.text(plan.moduleSymbol) != std::string_view("stmt_lowerer_if_chain")) {
        return fail("Unexpected module symbol " +
                    std::string(plan.symbolTable.text(plan.moduleSymbol)) + " in " +
                    sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents, got " +
                    std::to_string(lowering.writes.size()) + " (roots=" +
                    std::to_string(lowering.roots.size()) + ") in " +
                    sourcePath.string());
    }

    std::size_t yWrites = 0;
    std::size_t zWrites = 0;
    std::size_t guarded = 0;
    for (const auto& write : lowering.writes) {
        if (write.target.valid()) {
            const std::string_view name = plan.symbolTable.text(write.target);
            if (name == std::string_view("y")) {
                ++yWrites;
            } else if (name == std::string_view("z")) {
                ++zWrites;
            }
        }
        if (write.guard != wolf_sv_parser::kInvalidPlanIndex &&
            write.guard < lowering.values.size()) {
            ++guarded;
        }
    }

    if (yWrites != 1 || zWrites != 1) {
        return fail("Unexpected write targets in " + sourcePath.string());
    }
    if (guarded == 0) {
        return fail("Expected guarded writes in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testCase(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_case_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kCaseEq)) {
        std::size_t opCount = 0;
        for (const auto& value : lowering.values) {
            if (value.kind == wolf_sv_parser::ExprNodeKind::Operation) {
                ++opCount;
            }
        }
        return fail("Missing case-eq op (ops=" + std::to_string(opCount) + ") in " +
                    sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kLogicOr)) {
        return fail("Missing logic-or op in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kLogicNot)) {
        return fail("Missing logic-not op in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testCaseZ(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_casez_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kEq)) {
        return fail("Missing eq op in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kAnd)) {
        return fail("Missing mask-and op in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testCaseX(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_casex_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kEq)) {
        return fail("Missing eq op in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kAnd)) {
        return fail("Missing mask-and op in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testCaseZ2State(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_casez_2state_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kEq)) {
        return fail("Missing eq op in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kAnd)) {
        return fail("Missing mask-and op in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testCaseX2State(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_casex_2state_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kEq)) {
        return fail("Missing eq op in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kAnd)) {
        return fail("Missing mask-and op in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testCaseInside(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_case_inside_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 5) {
        return fail("Expected 5 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kWildcardEq)) {
        return fail("Missing wildcard-eq op in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kGe) ||
        !hasOp(lowering, grh::ir::OperationKind::kLe)) {
        return fail("Missing range compare ops in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kAdd) ||
        !hasOp(lowering, grh::ir::OperationKind::kSub) ||
        !hasOp(lowering, grh::ir::OperationKind::kMul) ||
        !hasOp(lowering, grh::ir::OperationKind::kDiv)) {
        return fail("Missing tolerance math ops in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLhsSelect(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_lhs_select", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 5) {
        return fail("Expected 5 write intents in " + sourcePath.string());
    }

    std::size_t bitSelects = 0;
    std::size_t rangeSelects = 0;
    std::size_t rangeSimple = 0;
    std::size_t rangeUp = 0;
    std::size_t rangeDown = 0;
    for (const auto& write : lowering.writes) {
        if (!write.target.valid() ||
            plan.symbolTable.text(write.target) != std::string_view("y")) {
            return fail("Unexpected write target in " + sourcePath.string());
        }
        if (write.slices.size() != 1) {
            return fail("Expected one slice per write in " + sourcePath.string());
        }
        const auto& slice = write.slices.front();
        if (slice.kind == wolf_sv_parser::WriteSliceKind::BitSelect) {
            if (slice.index == wolf_sv_parser::kInvalidPlanIndex) {
                return fail("Missing bit-select index in " + sourcePath.string());
            }
            ++bitSelects;
        } else if (slice.kind == wolf_sv_parser::WriteSliceKind::RangeSelect) {
            if (slice.left == wolf_sv_parser::kInvalidPlanIndex ||
                slice.right == wolf_sv_parser::kInvalidPlanIndex) {
                return fail("Missing range-select bounds in " + sourcePath.string());
            }
            ++rangeSelects;
            if (slice.rangeKind == wolf_sv_parser::WriteRangeKind::Simple) {
                ++rangeSimple;
            } else if (slice.rangeKind == wolf_sv_parser::WriteRangeKind::IndexedUp) {
                ++rangeUp;
            } else if (slice.rangeKind == wolf_sv_parser::WriteRangeKind::IndexedDown) {
                ++rangeDown;
            }
        } else {
            return fail("Unexpected slice kind in " + sourcePath.string());
        }
    }

    if (bitSelects != 2 || rangeSelects != 3) {
        return fail("Unexpected slice counts in " + sourcePath.string());
    }
    if (rangeSimple != 1 || rangeUp != 1 || rangeDown != 1) {
        return fail("Unexpected range selection kinds in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLhsConcat(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_lhs_concat", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }

    std::unordered_map<std::string, int> targetCounts;
    for (const auto& write : lowering.writes) {
        if (!write.target.valid()) {
            return fail("Missing write target in " + sourcePath.string());
        }
        targetCounts[std::string(plan.symbolTable.text(write.target))]++;
        if (!write.slices.empty()) {
            return fail("Unexpected slices for concat LHS in " + sourcePath.string());
        }
        if (write.value == wolf_sv_parser::kInvalidPlanIndex ||
            write.value >= lowering.values.size()) {
            return fail("Invalid write value in " + sourcePath.string());
        }
        const auto& node = lowering.values[write.value];
        if (node.kind != wolf_sv_parser::ExprNodeKind::Operation ||
            node.op != grh::ir::OperationKind::kSliceDynamic) {
            return fail("Expected RHS slice op for concat LHS in " + sourcePath.string());
        }
    }

    if (targetCounts["y"] != 1 || targetCounts["z"] != 1) {
        return fail("Unexpected concat LHS targets in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLhsStream(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_lhs_stream", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }

    std::unordered_map<std::string, int> targetCounts;
    for (const auto& write : lowering.writes) {
        if (!write.target.valid()) {
            return fail("Missing write target in " + sourcePath.string());
        }
        targetCounts[std::string(plan.symbolTable.text(write.target))]++;
        if (!write.slices.empty()) {
            return fail("Unexpected slices for streaming LHS in " + sourcePath.string());
        }
        if (write.value == wolf_sv_parser::kInvalidPlanIndex ||
            write.value >= lowering.values.size()) {
            return fail("Invalid write value in " + sourcePath.string());
        }
        const auto& node = lowering.values[write.value];
        if (node.kind != wolf_sv_parser::ExprNodeKind::Operation ||
            node.op != grh::ir::OperationKind::kSliceDynamic) {
            return fail("Expected RHS slice op for streaming LHS in " + sourcePath.string());
        }
    }

    if (targetCounts["y"] != 1 || targetCounts["z"] != 1) {
        return fail("Unexpected streaming LHS targets in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLhsMemberSelect(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_lhs_member", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }

    std::unordered_map<std::string, int> memberCounts;
    for (const auto& write : lowering.writes) {
        if (!write.target.valid() ||
            plan.symbolTable.text(write.target) != std::string_view("y")) {
            return fail("Unexpected write target in " + sourcePath.string());
        }
        if (write.slices.size() != 1) {
            return fail("Expected one member slice per write in " + sourcePath.string());
        }
        const auto& slice = write.slices.front();
        if (slice.kind != wolf_sv_parser::WriteSliceKind::MemberSelect ||
            !slice.member.valid()) {
            return fail("Missing member select slice in " + sourcePath.string());
        }
        memberCounts[std::string(plan.symbolTable.text(slice.member))]++;
    }

    if (memberCounts["hi"] != 1 || memberCounts["lo"] != 1) {
        return fail("Unexpected member select targets in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testRepeatLoop(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_repeat_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForLoop(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForeachLoop(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_foreach_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLargeRepeatLoop(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_repeat_large_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != kLargeLoopCount) {
        return fail("Expected " + std::to_string(kLargeLoopCount) +
                    " write intents in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLargeForLoop(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_large_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != kLargeLoopCount) {
        return fail("Expected " + std::to_string(kLargeLoopCount) +
                    " write intents in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testLargeForeachLoop(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_foreach_large_stmt", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != kLargeLoopCount) {
        return fail("Expected " + std::to_string(kLargeLoopCount) +
                    " write intents in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForLoopBreak(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_break", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }
    for (const auto& write : lowering.writes) {
        if (!write.target.valid() ||
            plan.symbolTable.text(write.target) != std::string_view("y")) {
            return fail("Unexpected write target in " + sourcePath.string());
        }
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForLoopContinue(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_continue", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    for (const auto& write : lowering.writes) {
        if (!write.target.valid() ||
            plan.symbolTable.text(write.target) != std::string_view("y")) {
            return fail("Unexpected write target in " + sourcePath.string());
        }
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForeachLoopBreak(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_foreach_break", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 2) {
        return fail("Expected 2 write intents in " + sourcePath.string());
    }
    for (const auto& write : lowering.writes) {
        if (!write.target.valid() ||
            plan.symbolTable.text(write.target) != std::string_view("y")) {
            return fail("Unexpected write target in " + sourcePath.string());
        }
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForeachLoopContinue(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_foreach_continue", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    for (const auto& write : lowering.writes) {
        if (!write.target.valid() ||
            plan.symbolTable.text(write.target) != std::string_view("y")) {
            return fail("Unexpected write target in " + sourcePath.string());
        }
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForBreakDynamic(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_break_dynamic", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    std::size_t guarded = 0;
    for (const auto& write : lowering.writes) {
        if (write.guard != wolf_sv_parser::kInvalidPlanIndex &&
            write.guard < lowering.values.size()) {
            ++guarded;
        }
    }
    if (guarded != lowering.writes.size()) {
        return fail("Expected all writes guarded in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kLogicNot) ||
        !hasOp(lowering, grh::ir::OperationKind::kLogicAnd)) {
        return fail("Missing guard ops for dynamic break in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForContinueDynamic(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_continue_dynamic", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    std::size_t guarded = 0;
    for (const auto& write : lowering.writes) {
        if (write.guard != wolf_sv_parser::kInvalidPlanIndex &&
            write.guard < lowering.values.size()) {
            ++guarded;
        }
    }
    if (guarded != lowering.writes.size()) {
        return fail("Expected all writes guarded in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kLogicNot) ||
        !hasOp(lowering, grh::ir::OperationKind::kLogicAnd)) {
        return fail("Missing guard ops for dynamic continue in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testForBreakCaseDynamic(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    if (!buildLoweringPlan(sourcePath, "stmt_lowerer_for_break_case_dynamic", diagnostics, plan, lowering)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (lowering.writes.size() != 3) {
        return fail("Expected 3 write intents in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kLogicNot) ||
        !hasOp(lowering, grh::ir::OperationKind::kLogicAnd)) {
        return fail("Missing guard ops for case break in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath =
        std::filesystem::path(WOLF_SV_CONVERT_STMT_DATA_PATH);

    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing stmt lowerer input file at " + sourcePath.string());
    }

    if (int status = testLowerer(sourcePath); status != 0) {
        return status;
    }
    if (int status = testIfChain(sourcePath); status != 0) {
        return status;
    }
    if (int status = testCase(sourcePath); status != 0) {
        return status;
    }
    if (int status = testCaseZ(sourcePath); status != 0) {
        return status;
    }
    if (int status = testCaseX(sourcePath); status != 0) {
        return status;
    }
    if (int status = testCaseZ2State(sourcePath); status != 0) {
        return status;
    }
    if (int status = testCaseX2State(sourcePath); status != 0) {
        return status;
    }
    if (int status = testCaseInside(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLhsSelect(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLhsConcat(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLhsStream(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLhsMemberSelect(sourcePath); status != 0) {
        return status;
    }
    if (int status = testRepeatLoop(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForLoop(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForeachLoop(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLargeRepeatLoop(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLargeForLoop(sourcePath); status != 0) {
        return status;
    }
    if (int status = testLargeForeachLoop(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForLoopBreak(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForLoopContinue(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForeachLoopBreak(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForeachLoopContinue(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForBreakDynamic(sourcePath); status != 0) {
        return status;
    }
    if (int status = testForContinueDynamic(sourcePath); status != 0) {
        return status;
    }
    return testForBreakCaseDynamic(sourcePath);
}
