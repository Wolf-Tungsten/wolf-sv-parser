#include "convert.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[convert-write-back-slice] " << message << '\n';
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
    argStorage.emplace_back("convert-write-back-slice");
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
    if (moduleName.empty() && root.topInstances.size() == 1 && root.topInstances[0]) {
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

bool buildWriteBackPlan(const std::filesystem::path& sourcePath, std::string_view topModule,
                        wolf_sv_parser::ConvertDiagnostics& diagnostics,
                        wolf_sv_parser::ModulePlan& outPlan,
                        wolf_sv_parser::LoweringPlan& outLowering,
                        wolf_sv_parser::WriteBackPlan& outWriteBack) {
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
    wolf_sv_parser::StmtLowererPass stmtLowerer(context);
    wolf_sv_parser::WriteBackPass writeBack(context);

    outPlan = planner.plan(top->body);
    outLowering = {};
    stmtLowerer.lower(outPlan, outLowering);
    outWriteBack = writeBack.lower(outPlan, outLowering);
    return true;
}

bool hasOp(const wolf_sv_parser::LoweringPlan& lowering, grh::ir::OperationKind kind) {
    for (const auto& node : lowering.values) {
        if (node.kind == wolf_sv_parser::ExprNodeKind::Operation && node.op == kind) {
            return true;
        }
    }
    return false;
}

bool hasWarningMessage(const wolf_sv_parser::ConvertDiagnostics& diagnostics,
                       std::string_view needle) {
    for (const auto& message : diagnostics.messages()) {
        if (message.kind == wolf_sv_parser::ConvertDiagnosticKind::Warning &&
            message.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int testWriteBackSliceStatic(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    wolf_sv_parser::WriteBackPlan writeBack;
    if (!buildWriteBackPlan(sourcePath, "write_back_slice_static", diagnostics, plan, lowering,
                            writeBack)) {
        return fail("Failed to build write-back slice plan for " + sourcePath.string());
    }

    if (writeBack.entries.size() != 1) {
        return fail("Expected 1 write-back entry for static slices in " + sourcePath.string());
    }
    if (hasWarningMessage(diagnostics, "Write-back merge with slices")) {
        return fail("Unexpected slice warning in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kConcat)) {
        return fail("Missing kConcat in static slice write-back");
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kSliceDynamic)) {
        return fail("Missing kSliceDynamic in static slice write-back");
    }
    return 0;
}

int testWriteBackSliceDynamic(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    wolf_sv_parser::WriteBackPlan writeBack;
    if (!buildWriteBackPlan(sourcePath, "write_back_slice_dynamic", diagnostics, plan, lowering,
                            writeBack)) {
        return fail("Failed to build write-back dynamic slice plan for " + sourcePath.string());
    }

    if (writeBack.entries.size() != 1) {
        return fail("Expected 1 write-back entry for dynamic slices in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kShl)) {
        return fail("Missing kShl in dynamic slice write-back");
    }
    return 0;
}

int testWriteBackSliceMember(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    wolf_sv_parser::LoweringPlan lowering;
    wolf_sv_parser::WriteBackPlan writeBack;
    if (!buildWriteBackPlan(sourcePath, "write_back_slice_member", diagnostics, plan, lowering,
                            writeBack)) {
        return fail("Failed to build write-back member slice plan for " + sourcePath.string());
    }

    if (writeBack.entries.size() != 1) {
        return fail("Expected 1 write-back entry for member slices in " + sourcePath.string());
    }
    if (!hasOp(lowering, grh::ir::OperationKind::kConcat)) {
        return fail("Missing kConcat in member slice write-back");
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath = WOLF_SV_CONVERT_WRITE_BACK_SLICE_DATA_PATH;
    if (int status = testWriteBackSliceStatic(sourcePath); status != 0) {
        return status;
    }
    if (int status = testWriteBackSliceDynamic(sourcePath); status != 0) {
        return status;
    }
    return testWriteBackSliceMember(sourcePath);
}
