#include "convert.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[convert-expr-lowerer] " << message << '\n';
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
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;
    if (!topModule.empty()) {
        driver.options.topModules.emplace_back(topModule);
    }

    std::vector<std::string> argStorage;
    argStorage.emplace_back("convert-expr-lowerer");
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
                       wolf_sv_parser::LoweringPlan& outPlan) {
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

    wolf_sv_parser::ModulePlan plan = planner.plan(top->body);
    typeResolver.resolve(plan);
    rwAnalyzer.analyze(plan);
    outPlan = exprLowerer.lower(plan);
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

int testLowerer(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::LoweringPlan plan;
    if (!buildLoweringPlan(sourcePath, "expr_lowerer_case", diagnostics, plan)) {
        return fail("Failed to build lowering plan for " + sourcePath.string());
    }

    if (plan.roots.size() != 3) {
        return fail("Expected 3 lowered roots in " + sourcePath.string());
    }
    if (!hasOp(plan, grh::ir::OperationKind::kConcat)) {
        return fail("Missing concat op in " + sourcePath.string());
    }
    if (!hasOp(plan, grh::ir::OperationKind::kMux)) {
        return fail("Missing mux op in " + sourcePath.string());
    }
    if (!hasOp(plan, grh::ir::OperationKind::kReplicate)) {
        return fail("Missing replicate op in " + sourcePath.string());
    }
    if (!hasOp(plan, grh::ir::OperationKind::kAnd)) {
        return fail("Missing and op in " + sourcePath.string());
    }
    if (!hasOp(plan, grh::ir::OperationKind::kOr)) {
        return fail("Missing or op in " + sourcePath.string());
    }
    if (!hasOp(plan, grh::ir::OperationKind::kNot)) {
        return fail("Missing not op in " + sourcePath.string());
    }

    std::size_t opCount = 0;
    for (const auto& value : plan.values) {
        if (value.kind == wolf_sv_parser::ExprNodeKind::Operation) {
            ++opCount;
        }
    }
    if (plan.tempSymbols.size() != opCount) {
        return fail("Temp symbol count does not match op count in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath =
        std::filesystem::path(WOLF_SV_CONVERT_EXPR_DATA_PATH);

    if (!std::filesystem::exists(sourcePath)) {
        return fail("Missing expr lowerer input file at " + sourcePath.string());
    }

    return testLowerer(sourcePath);
}
