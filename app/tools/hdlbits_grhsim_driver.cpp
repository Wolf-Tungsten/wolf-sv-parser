#include "core/diagnostics.hpp"
#include "core/emit.hpp"
#include "core/grh.hpp"
#include "core/ingest.hpp"
#include "core/store.hpp"
#include "core/transform.hpp"
#include "emit/grhsim_cpp.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

    using wolvrix::lib::diag::Diagnostic;
    using wolvrix::lib::diag::DiagnosticKind;
    using wolvrix::lib::emit::EmitDiagnostics;
    using wolvrix::lib::emit::EmitGrhSimCpp;
    using wolvrix::lib::emit::EmitOptions;
    using wolvrix::lib::emit::EmitResult;
    using wolvrix::lib::grh::Design;
    using wolvrix::lib::ingest::ConvertAbort;
    using wolvrix::lib::ingest::ConvertDiagnostics;
    using wolvrix::lib::ingest::ConvertDriver;
    using wolvrix::lib::ingest::ConvertOptions;
    using wolvrix::lib::LogEvent;
    using wolvrix::lib::LogLevel;
    using wolvrix::lib::store::StoreDiagnostics;
    using wolvrix::lib::store::StoreJson;
    using wolvrix::lib::store::StoreOptions;
    using wolvrix::lib::transform::makePass;
    using wolvrix::lib::transform::PassDiagnostics;
    using wolvrix::lib::transform::PassManager;
    using wolvrix::lib::transform::PassManagerResult;
    using wolvrix::lib::transform::SessionStore;

    const char *kindName(DiagnosticKind kind)
    {
        switch (kind)
        {
        case DiagnosticKind::Error:
            return "error";
        case DiagnosticKind::Warning:
            return "warning";
        case DiagnosticKind::Info:
            return "info";
        case DiagnosticKind::Debug:
            return "debug";
        }
        return "unknown";
    }

    template <typename DiagnosticsT>
    void printDiagnostics(const DiagnosticsT &diags, std::string_view tag)
    {
        for (const Diagnostic &diag : diags.messages())
        {
            std::cerr << "[" << tag << "] " << kindName(diag.kind) << ": ";
            if (!diag.passName.empty())
            {
                std::cerr << diag.passName << ": ";
            }
            if (!diag.context.empty())
            {
                std::cerr << diag.context << ": ";
            }
            std::cerr << diag.message << '\n';
        }
    }

    bool addPass(PassManager &manager, std::string_view name, std::vector<std::string> args)
    {
        std::vector<std::string_view> argViews;
        argViews.reserve(args.size());
        for (const std::string &arg : args)
        {
            argViews.push_back(arg);
        }

        std::string error;
        auto pass = makePass(name, std::span<const std::string_view>(argViews), error);
        if (!pass)
        {
            std::cerr << "[hdlbits-grhsim-driver] failed to create pass " << name << ": " << error << '\n';
            return false;
        }
        manager.addPass(std::move(pass));
        return true;
    }

    bool runPipeline(Design &design, std::string_view topGraphName, SessionStore &session)
    {
        PassManager manager;
        manager.options().session = &session;
        manager.options().logLevel = LogLevel::Info;
        manager.options().logSink = [](LogLevel, std::string_view tag, std::string_view message) {
            if (tag.empty())
            {
                std::cerr << "[hdlbits-grhsim-driver] " << message << '\n';
            }
            else
            {
                std::cerr << "[hdlbits-grhsim-driver] " << tag << ": " << message << '\n';
            }
        };

        if (!addPass(manager, "xmr-resolve", {}) ||
            !addPass(manager, "multidriven-guard", {}) ||
            !addPass(manager, "latch-transparent-read", {}) ||
            !addPass(manager, "hier-flatten", {"-sym-protect", "hierarchy"}) ||
            !addPass(manager, "comb-loop-elim", {}) ||
            !addPass(manager, "simplify", {"-semantics", "2state"}) ||
            !addPass(manager, "memory-init-check", {}) ||
            !addPass(manager, "stats", {}) ||
            !addPass(manager,
                     "activity-schedule",
                     {"-path", std::string(topGraphName), "-enable-replication", "true"}))
        {
            return false;
        }

        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);
        printDiagnostics(diags, "hdlbits-grhsim-driver");
        return result.success && !diags.hasError();
    }

    std::shared_ptr<slang::ast::Compilation> compileSv(const std::filesystem::path &inputPath,
                                                       std::string_view topName)
    {
        std::vector<std::string> argvStorage{
            "hdlbits-grhsim-driver",
            inputPath.string(),
            "--top",
            std::string(topName),
        };
        std::vector<const char *> argv;
        argv.reserve(argvStorage.size());
        for (const std::string &arg : argvStorage)
        {
            argv.push_back(arg.c_str());
        }

        slang::driver::Driver driver;
        driver.addStandardArgs();
        driver.options.singleUnit = true;
        driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

        auto reportSlangDiagnostics = [&]() {
            if (driver.diagEngine.getNumErrors() == 0 && driver.diagEngine.getNumWarnings() == 0)
            {
                return;
            }
            (void)driver.reportDiagnostics(true);
        };

        if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()) ||
            !driver.processOptions() ||
            !driver.parseAllSources())
        {
            reportSlangDiagnostics();
            return nullptr;
        }

        auto compilation = std::shared_ptr<slang::ast::Compilation>(driver.createCompilation());
        driver.runAnalysis(*compilation);

        const auto &allDiagnostics = compilation->getAllDiagnostics();
        bool hasErrors = false;
        bool hasIssues = false;
        for (const auto &diag : allDiagnostics)
        {
            const auto severity = slang::getDefaultSeverity(diag.code);
            if (severity >= slang::DiagnosticSeverity::Warning)
            {
                hasIssues = true;
            }
            if (severity >= slang::DiagnosticSeverity::Error || diag.isError())
            {
                hasErrors = true;
            }
        }
        if (hasIssues)
        {
            driver.reportCompilation(*compilation, true);
            reportSlangDiagnostics();
        }
        if (hasErrors)
        {
            return nullptr;
        }
        return compilation;
    }

} // namespace

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::cerr << "usage: hdlbits-grhsim-driver <input.sv> <top> <out-dir> <json-out>\n";
        return 1;
    }

    const std::filesystem::path inputPath = argv[1];
    const std::string topName = argv[2];
    const std::filesystem::path outDir = argv[3];
    const std::filesystem::path jsonPath = argv[4];

    auto compilation = compileSv(inputPath, topName);
    if (!compilation)
    {
        std::cerr << "[hdlbits-grhsim-driver] failed to parse input SV\n";
        return 1;
    }

    ConvertOptions convertOptions;
    convertOptions.abortOnError = true;
    convertOptions.enableLogging = true;
    convertOptions.logLevel = LogLevel::Info;

    ConvertDriver converter(convertOptions);
    converter.logger().setSink([](const LogEvent &event) {
        std::string message;
        if (!event.tag.empty())
        {
            message.append(event.tag);
            message.append(": ");
        }
        message.append(event.message);
        std::cerr << "[hdlbits-grhsim-driver] " << message << '\n';
    });

    Design design;
    try
    {
        design = converter.convert(compilation->getRoot());
    }
    catch (const ConvertAbort &)
    {
        converter.diagnostics().flushThreadLocal();
        printDiagnostics(converter.diagnostics(), "hdlbits-grhsim-driver");
        return 1;
    }
    converter.diagnostics().flushThreadLocal();
    printDiagnostics(converter.diagnostics(), "hdlbits-grhsim-driver");
    if (converter.diagnostics().hasError())
    {
        return 1;
    }

    if (design.topGraphs().size() != 1)
    {
        std::cerr << "[hdlbits-grhsim-driver] expected exactly one top graph, got "
                  << design.topGraphs().size() << '\n';
        return 1;
    }
    std::string topGraphName = design.topGraphs().front();
    if (topGraphName != topName)
    {
        design.cloneGraph(topGraphName, topName);
        design.markAsTop(topName);
        design.unmarkAsTop(topGraphName);
        design.deleteGraph(topGraphName);
        topGraphName = topName;
    }

    SessionStore session;
    if (!runPipeline(design, topGraphName, session))
    {
        return 1;
    }

    std::filesystem::create_directories(outDir);
    std::filesystem::create_directories(jsonPath.parent_path());

    StoreDiagnostics storeDiags;
    StoreJson store(&storeDiags);
    StoreOptions storeOptions;
    storeOptions.outputDir = jsonPath.parent_path().string();
    storeOptions.outputFilename = jsonPath.filename().string();
    storeOptions.topOverrides = {topGraphName};
    std::cerr << "[hdlbits-grhsim-driver] dbg: before store\n";
    const auto storeResult = store.store(design, storeOptions);
    std::cerr << "[hdlbits-grhsim-driver] dbg: after store\n";
    printDiagnostics(storeDiags, "hdlbits-grhsim-driver");
    if (!storeResult.success || storeDiags.hasError())
    {
        return 1;
    }

    EmitDiagnostics emitDiags;
    EmitGrhSimCpp emitter(&emitDiags);
    EmitOptions emitOptions;
    emitOptions.outputDir = outDir.string();
    emitOptions.topOverrides = {topGraphName};
    emitOptions.session = &session;
    emitOptions.sessionPathPrefix = topGraphName;
    std::cerr << "[hdlbits-grhsim-driver] dbg: before emit\n";
    const EmitResult emitResult = emitter.emit(design, emitOptions);
    std::cerr << "[hdlbits-grhsim-driver] dbg: after emit\n";
    printDiagnostics(emitDiags, "hdlbits-grhsim-driver");
    if (!emitResult.success || emitDiags.hasError())
    {
        return 1;
    }
    std::cerr << "[hdlbits-grhsim-driver] dbg: success\n";

    return 0;
}
