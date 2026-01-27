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
    std::cerr << "[convert-rw-analyzer] " << message << '\n';
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
    argStorage.emplace_back("convert-rw-analyzer");
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

wolf_sv_parser::SignalId findSignalId(const wolf_sv_parser::ModulePlan& plan,
                                      std::string_view name) {
    const wolf_sv_parser::PlanSymbolId id = plan.symbolTable.lookup(name);
    if (!id.valid()) {
        return wolf_sv_parser::kInvalidPlanIndex;
    }
    for (wolf_sv_parser::SignalId i = 0; i < plan.signals.size(); ++i) {
        if (plan.signals[i].symbol.index == id.index) {
            return i;
        }
    }
    return wolf_sv_parser::kInvalidPlanIndex;
}

bool hasRWOp(const wolf_sv_parser::ModulePlan& plan, std::string_view name,
             wolf_sv_parser::ControlDomain domain, bool isWrite) {
    const wolf_sv_parser::SignalId id = findSignalId(plan, name);
    if (id == wolf_sv_parser::kInvalidPlanIndex) {
        return false;
    }
    for (const auto& op : plan.rwOps) {
        if (op.target == id && op.domain == domain && op.isWrite == isWrite) {
            return true;
        }
    }
    return false;
}

bool hasMemoryPort(const wolf_sv_parser::ModulePlan& plan, std::string_view name, bool isRead,
                   bool isWrite, bool isSync) {
    const wolf_sv_parser::SignalId id = findSignalId(plan, name);
    if (id == wolf_sv_parser::kInvalidPlanIndex) {
        return false;
    }
    for (const auto& port : plan.memPorts) {
        if (port.memory == id && port.isRead == isRead && port.isWrite == isWrite &&
            port.isSync == isSync) {
            return true;
        }
    }
    return false;
}

bool buildPlan(const std::filesystem::path& sourcePath, std::string_view topModule,
               wolf_sv_parser::ConvertDiagnostics& diagnostics,
               wolf_sv_parser::ModulePlan& outPlan) {
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
    outPlan = planner.plan(top->body);
    typeResolver.resolve(outPlan);
    rwAnalyzer.analyze(outPlan);
    return true;
}

int testSequentialRW(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    if (!buildPlan(sourcePath, "top_module", diagnostics, plan)) {
        return fail("Failed to build plan for " + sourcePath.string());
    }

    if (!hasRWOp(plan, "counter", wolf_sv_parser::ControlDomain::Sequential, true)) {
        return fail("Expected sequential write for counter in " + sourcePath.string());
    }
    if (!hasRWOp(plan, "counter", wolf_sv_parser::ControlDomain::Sequential, false)) {
        return fail("Expected sequential read for counter in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

int testMemoryPorts(const std::filesystem::path& sourcePath) {
    wolf_sv_parser::ConvertDiagnostics diagnostics;
    wolf_sv_parser::ModulePlan plan;
    if (!buildPlan(sourcePath, "top_module", diagnostics, plan)) {
        return fail("Failed to build plan for " + sourcePath.string());
    }

    const bool hasAsyncRead = hasMemoryPort(plan, "PHT", true, false, false);
    const bool hasSyncRead = hasMemoryPort(plan, "PHT", true, false, true);
    if (!hasAsyncRead && !hasSyncRead) {
        return fail("Expected read memory port for PHT in " + sourcePath.string());
    }
    if (!hasMemoryPort(plan, "PHT", false, true, true)) {
        return fail("Expected sync write memory port for PHT in " + sourcePath.string());
    }
    if (diagnostics.hasError()) {
        return fail("Unexpected Convert diagnostics errors in " + sourcePath.string());
    }
    return 0;
}

} // namespace

int main() {
    const std::filesystem::path dutDir =
        std::filesystem::path(WOLF_SV_CONVERT_HDLBITS_DUT_DIR);
    const std::filesystem::path seqPath = dutDir / "dut_159.v";
    const std::filesystem::path memPath = dutDir / "dut_162.v";

    if (!std::filesystem::exists(seqPath) || !std::filesystem::exists(memPath)) {
        return fail("Missing HDLBits input files in " + dutDir.string());
    }

    if (int result = testSequentialRW(seqPath); result != 0) {
        return result;
    }
    if (int result = testMemoryPorts(memPath); result != 0) {
        return result;
    }
    return 0;
}
