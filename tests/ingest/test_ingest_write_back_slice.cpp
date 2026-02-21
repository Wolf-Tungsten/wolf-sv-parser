#include "ingest.hpp"

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
    std::cerr << "[ingest-write-back-slice] " << message << '\n';
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
    argStorage.emplace_back("ingest-write-back-slice");
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
                        wolvrix::lib::ingest::ConvertDiagnostics& diagnostics,
                        wolvrix::lib::ingest::ModulePlan& outPlan,
                        wolvrix::lib::ingest::LoweringPlan& outLowering,
                        wolvrix::lib::ingest::WriteBackPlan& outWriteBack) {
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

    wolvrix::lib::Logger logger;
    wolvrix::lib::ingest::PlanCache planCache;
    wolvrix::lib::ingest::PlanTaskQueue planQueue;
    planQueue.reset();

    wolvrix::lib::ingest::ConvertContext context{};
    context.compilation = &root.getCompilation();
    context.root = &root;
    context.diagnostics = &diagnostics;
    context.logger = &logger;
    context.planCache = &planCache;
    context.planQueue = &planQueue;

    wolvrix::lib::ingest::ModulePlanner planner(context);
    wolvrix::lib::ingest::StmtLowererPass stmtLowerer(context);
    wolvrix::lib::ingest::WriteBackPass writeBack(context);

    outPlan = planner.plan(top->body);
    outLowering = {};
    stmtLowerer.lower(outPlan, outLowering);
    outWriteBack = writeBack.lower(outPlan, outLowering);
    return true;
}

bool hasOp(const wolvrix::lib::ingest::LoweringPlan& lowering, wolvrix::lib::grh::OperationKind kind) {
    for (const auto& node : lowering.values) {
        if (node.kind == wolvrix::lib::ingest::ExprNodeKind::Operation && node.op == kind) {
            return true;
        }
    }
    return false;
}

bool hasWarningMessage(const wolvrix::lib::ingest::ConvertDiagnostics& diagnostics,
                       std::string_view needle) {
    for (const auto& message : diagnostics.messages()) {
        if (message.kind == wolvrix::lib::ingest::ConvertDiagnosticKind::Warning &&
            message.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int testWriteBackSliceStatic(const std::filesystem::path& sourcePath) {
    wolvrix::lib::ingest::ConvertDiagnostics diagnostics;
    wolvrix::lib::ingest::ModulePlan plan;
    wolvrix::lib::ingest::LoweringPlan lowering;
    wolvrix::lib::ingest::WriteBackPlan writeBack;
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
    if (!hasOp(lowering, wolvrix::lib::grh::OperationKind::kConcat)) {
        return fail("Missing kConcat in static slice write-back");
    }
    if (!hasOp(lowering, wolvrix::lib::grh::OperationKind::kSliceDynamic)) {
        return fail("Missing kSliceDynamic in static slice write-back");
    }
    return 0;
}

int testWriteBackSliceDynamic(const std::filesystem::path& sourcePath) {
    wolvrix::lib::ingest::ConvertDiagnostics diagnostics;
    wolvrix::lib::ingest::ModulePlan plan;
    wolvrix::lib::ingest::LoweringPlan lowering;
    wolvrix::lib::ingest::WriteBackPlan writeBack;
    if (!buildWriteBackPlan(sourcePath, "write_back_slice_dynamic", diagnostics, plan, lowering,
                            writeBack)) {
        return fail("Failed to build write-back dynamic slice plan for " + sourcePath.string());
    }

    if (writeBack.entries.size() != 1) {
        return fail("Expected 1 write-back entry for dynamic slices in " + sourcePath.string());
    }
    if (!hasOp(lowering, wolvrix::lib::grh::OperationKind::kShl)) {
        return fail("Missing kShl in dynamic slice write-back");
    }
    return 0;
}

int testWriteBackSliceMember(const std::filesystem::path& sourcePath) {
    wolvrix::lib::ingest::ConvertDiagnostics diagnostics;
    wolvrix::lib::ingest::ModulePlan plan;
    wolvrix::lib::ingest::LoweringPlan lowering;
    wolvrix::lib::ingest::WriteBackPlan writeBack;
    if (!buildWriteBackPlan(sourcePath, "write_back_slice_member", diagnostics, plan, lowering,
                            writeBack)) {
        return fail("Failed to build write-back member slice plan for " + sourcePath.string());
    }

    if (writeBack.entries.size() != 1) {
        return fail("Expected 1 write-back entry for member slices in " + sourcePath.string());
    }
    if (!hasOp(lowering, wolvrix::lib::grh::OperationKind::kConcat)) {
        return fail("Missing kConcat in member slice write-back");
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

} // namespace

int main() {
    const std::filesystem::path sourcePath = WOLF_SV_INGEST_WRITE_BACK_SLICE_DATA_PATH;
    if (int status = testWriteBackSliceStatic(sourcePath); status != 0) {
        return status;
    }
    if (int status = testWriteBackSliceDynamic(sourcePath); status != 0) {
        return status;
    }
    return testWriteBackSliceMember(sourcePath);
}
