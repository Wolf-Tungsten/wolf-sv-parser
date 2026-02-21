#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "slang/ast/ASTSerializer.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"
#include "slang/text/Json.h"

#include "convert.hpp"
#include "emit.hpp"
#include "transform.hpp"
#include "pass/demo_stats.hpp"
#include "pass/const_fold.hpp"
#include "pass/memory_init_check.hpp"
#include "pass/redundant_elim.hpp"
#include "pass/dead_code_elim.hpp"
#include "pass/xmr_resolve.hpp"

using namespace slang::driver;

namespace {
class Watchdog {
public:
    explicit Watchdog(std::chrono::seconds timeout)
        : timeout_(timeout),
          thread_([this]() { run(); })
    {
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    ~Watchdog()
    {
        cancel();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    void cancel()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
        }
        cv_.notify_one();
    }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout_, [this]() { return cancelled_; }))
        {
            return;
        }
        std::cerr << "[timeout] Exceeded " << timeout_.count() << " seconds; terminating\n";
        std::cerr.flush();
        std::exit(124);
    }

    std::chrono::seconds timeout_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool cancelled_ = false;
    std::thread thread_;
};

using TimingClock = std::chrono::steady_clock;

std::string formatDuration(TimingClock::duration duration)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms > 0)
    {
        return std::to_string(ms) + "ms";
    }
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    if (us > 0)
    {
        return std::to_string(us) + "us";
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    return std::to_string(ns) + "ns";
}

const char* logLevelText(wolf_sv_parser::LogLevel level)
{
    switch (level)
    {
    case wolf_sv_parser::LogLevel::Trace:
        return "trace";
    case wolf_sv_parser::LogLevel::Debug:
        return "debug";
    case wolf_sv_parser::LogLevel::Info:
        return "info";
    case wolf_sv_parser::LogLevel::Warn:
        return "warn";
    case wolf_sv_parser::LogLevel::Error:
        return "error";
    case wolf_sv_parser::LogLevel::Off:
    default:
        return "off";
    }
}

std::optional<wolf_sv_parser::LogLevel> parseLogLevel(std::string_view text)
{
    std::string lowered(text);
    for (char &c : lowered)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lowered == "trace")
    {
        return wolf_sv_parser::LogLevel::Trace;
    }
    if (lowered == "debug")
    {
        return wolf_sv_parser::LogLevel::Debug;
    }
    if (lowered == "info")
    {
        return wolf_sv_parser::LogLevel::Info;
    }
    if (lowered == "warn" || lowered == "warning")
    {
        return wolf_sv_parser::LogLevel::Warn;
    }
    if (lowered == "error")
    {
        return wolf_sv_parser::LogLevel::Error;
    }
    if (lowered == "off" || lowered == "none")
    {
        return wolf_sv_parser::LogLevel::Off;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char **argv)
{
    Driver driver;
    driver.addStandardArgs();
    driver.options.singleUnit = true;
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::optional<bool> dumpAst;
    driver.cmdLine.add("--dump-ast", dumpAst, "Dump a summary of the parsed AST");
    std::optional<bool> dumpJson;
    driver.cmdLine.add("--emit-json", dumpJson, "Emit GRH JSON after convert");
    std::optional<bool> dumpSv;
    driver.cmdLine.add("--emit-sv", dumpSv, "Emit SystemVerilog after convert");
    std::optional<bool> emitTraceUnderscore;
    driver.cmdLine.add("--emit-trace-underscore", emitTraceUnderscore,
                       "Emit wd_* aliases for underscore-prefixed internal values to improve tracing");
    std::optional<bool> skipTransform;
    driver.cmdLine.add("--skip-transform", skipTransform,
                       "Skip transform passes and emit raw Convert netlist");
    std::optional<bool> dropDeclaredSymbols;
    driver.cmdLine.add("--transform-drop-declared", dropDeclaredSymbols,
                       "Allow transform to drop user-declared symbols (default keeps them)");
    std::optional<std::string> logLevel;
    driver.cmdLine.add("--log", logLevel,
                       "Log level: none|error|warn|info|debug|trace", "<level>");
    std::optional<bool> profileTimer;
    driver.cmdLine.add("--profile-timer", profileTimer,
                       "Emit detailed timing logs for convert/transform/emit passes");
    std::optional<int64_t> convertThreads;
    driver.cmdLine.add("--convert-threads", convertThreads,
                       "Number of Convert worker threads (default 32)", "<count>");
    std::optional<bool> singleThread;
    driver.cmdLine.add("--single-thread", singleThread,
                       "Force single-threaded Convert execution");
    std::optional<std::string> emitOutputDir;
    driver.cmdLine.add("--emit-out-dir,--emit-out", emitOutputDir, "Directory to write emitted GRH/SV files", "<path>");
    std::optional<std::string> outputPathArg;
    driver.cmdLine.add("-o", outputPathArg, "Output file path for emitted artifacts", "<path>", slang::CommandLineFlags::FilePath);
    std::optional<int64_t> timeoutSeconds;
    driver.cmdLine.add("--timeout", timeoutSeconds, "Terminate if runtime exceeds timeout seconds", "<sec>");

    if (!driver.parseCommandLine(argc, argv)) {
        return 1;
    }

    if (timeoutSeconds && *timeoutSeconds <= 0) {
        std::cerr << "[timeout] Value must be a positive number of seconds\n";
        return 1;
    }
    std::optional<Watchdog> watchdog;
    if (timeoutSeconds) {
        watchdog.emplace(std::chrono::seconds(*timeoutSeconds));
    }

    if (!driver.processOptions()) {
        return 2;
    }

    bool timingEnabled = profileTimer == true;
    wolf_sv_parser::LogLevel globalLogLevel = wolf_sv_parser::LogLevel::Info;
    const bool logLevelExplicit = logLevel && !logLevel->empty();
    if (logLevelExplicit)
    {
        const auto parsed = parseLogLevel(*logLevel);
        if (!parsed.has_value())
        {
            std::cerr << "[log] Unknown log level: " << *logLevel << '\n';
            return 1;
        }
        globalLogLevel = *parsed;
    }
    if (timingEnabled)
    {
        if (!logLevelExplicit &&
            static_cast<int>(globalLogLevel) > static_cast<int>(wolf_sv_parser::LogLevel::Debug))
        {
            globalLogLevel = wolf_sv_parser::LogLevel::Debug;
        }
    }
    auto shouldLog = [&](wolf_sv_parser::LogLevel level) -> bool {
        if (globalLogLevel == wolf_sv_parser::LogLevel::Off)
        {
            return false;
        }
        return static_cast<int>(level) >= static_cast<int>(globalLogLevel);
    };
    auto logLine = [&](wolf_sv_parser::LogLevel level, std::string_view prefix,
                       std::string_view tag, std::string_view message) {
        if (!shouldLog(level))
        {
            return;
        }
        std::cerr << "[" << prefix << "] [" << logLevelText(level) << "]";
        if (!tag.empty())
        {
            std::cerr << " [" << tag << "]";
        }
        std::cerr << " " << message << '\n';
    };
    const auto pipelineStart = TimingClock::now();
    auto logTimingStage = [&](std::string_view prefix, std::string_view label,
                              TimingClock::time_point stageStart,
                              TimingClock::time_point stageEnd) {
        std::string message;
        message.reserve(label.size() + 48);
        message.append(label);
        message.append(" took ");
        message.append(formatDuration(stageEnd - stageStart));
        message.append(" (total ");
        message.append(formatDuration(stageEnd - pipelineStart));
        message.append(")");
        std::cerr << "[" << prefix << "] [timing] " << message << '\n';
    };

    const auto slangStart = TimingClock::now();
    std::string slangBegin = "begin sources=";
    slangBegin.append(std::to_string(driver.sourceLoader.getFilePaths().size()));
    slangBegin.append(", defines=");
    slangBegin.append(std::to_string(driver.options.defines.size()));
    slangBegin.append(", undefs=");
    slangBegin.append(std::to_string(driver.options.undefines.size()));
    slangBegin.append(", tops=");
    slangBegin.append(std::to_string(driver.options.topModules.size()));
    slangBegin.append(", singleUnit=");
    slangBegin.append(driver.options.singleUnit.value_or(false) ? "1" : "0");
    slangBegin.append(", lint=");
    slangBegin.append(driver.options.lintMode() ? "1" : "0");
    slangBegin.append(", std=");
    if (driver.options.languageVersion && !driver.options.languageVersion->empty())
    {
        slangBegin.append(*driver.options.languageVersion);
    }
    else
    {
        slangBegin.append("default");
    }
    logLine(wolf_sv_parser::LogLevel::Info, "slang", {}, slangBegin);
    if (!driver.parseAllSources()) {
        std::string slangEnd = "end (parse failed, errors=";
        slangEnd.append(std::to_string(driver.diagEngine.getNumErrors()));
        slangEnd.append(", warnings=");
        slangEnd.append(std::to_string(driver.diagEngine.getNumWarnings()));
        slangEnd.append(")");
        logLine(wolf_sv_parser::LogLevel::Info, "slang", {}, slangEnd);
        const auto slangEndTime = TimingClock::now();
        logTimingStage("slang", "slang", slangStart, slangEndTime);
        return 3;
    }

    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, /* quiet */ false);

    driver.runAnalysis(*compilation);
    std::string slangEnd = "end (errors=";
    slangEnd.append(std::to_string(driver.diagEngine.getNumErrors()));
    slangEnd.append(", warnings=");
    slangEnd.append(std::to_string(driver.diagEngine.getNumWarnings()));
    slangEnd.append(")");
    logLine(wolf_sv_parser::LogLevel::Info, "slang", {}, slangEnd);
    bool diagOk = true;
    if (driver.diagEngine.getNumErrors() > 0)
    {
        diagOk = driver.reportDiagnostics(/* quiet */ false);
        (void)diagOk;
        const auto slangEndTime = TimingClock::now();
        logTimingStage("slang", "slang", slangStart, slangEndTime);
        return 4;
    }
    diagOk = driver.reportDiagnostics(/* quiet */ false);
    const auto slangEndTime = TimingClock::now();
    logTimingStage("slang", "slang", slangStart, slangEndTime);

    auto &root = compilation->getRoot();

    std::optional<std::string> outputDirOverride;
    std::optional<std::string> jsonOutputName;
    std::optional<std::string> svOutputName;

    auto applyOutputPath = [&]()
    {
        if (!outputPathArg || outputPathArg->empty())
        {
            return;
        }
        const std::filesystem::path path(*outputPathArg);
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            outputDirOverride = parent.string();
        }

        const std::string filename = path.filename().string();
        if (filename.empty() || filename == "." || filename == "..")
        {
            return;
        }

        std::string ext = path.extension().string();
        for (char &c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        const bool wantsSv = dumpSv == true;
        const bool wantsJson = dumpJson == true;

        if (ext == ".sv" || ext == ".v")
        {
            svOutputName = filename;
        }
        else if (ext == ".json")
        {
            jsonOutputName = filename;
        }
        else if (wantsSv && !wantsJson)
        {
            svOutputName = filename;
        }
        else if (wantsJson && !wantsSv)
        {
            jsonOutputName = filename;
        }
        else if (wantsSv)
        {
            svOutputName = filename;
        }
    };
    applyOutputPath();

    auto applyCommonEmitOptions = [&](grh::emit::EmitOptions &emitOptions)
    {
        if (outputDirOverride)
        {
            emitOptions.outputDir = *outputDirOverride;
        }
        else if (emitOutputDir && !emitOutputDir->empty())
        {
            emitOptions.outputDir = *emitOutputDir;
        }
        if (emitTraceUnderscore == true)
        {
            emitOptions.traceUnderscoreValues = true;
        }
    };

    if (dumpAst == true) {
        std::cout << "=== AST JSON ===\n";

        slang::JsonWriter writer;
        writer.setPrettyPrint(true);

        slang::ast::ASTSerializer serializer(*compilation, writer);
        serializer.serialize(root);
        writer.writeNewLine();

        std::cout << writer.view();
    }

    grh::ir::Netlist netlist;
    const auto *sourceManager = compilation->getSourceManager();
    auto extractLine = [](std::string_view text, size_t offset) -> std::string_view {
        if (offset > text.size()) {
            return {};
        }
        size_t lineStart = text.rfind('\n', offset);
        if (lineStart == std::string_view::npos) {
            lineStart = 0;
        } else {
            lineStart += 1;
        }
        size_t lineEnd = text.find('\n', offset);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }
        return text.substr(lineStart, lineEnd - lineStart);
    };
    auto trimLine = [](std::string_view line) -> std::string {
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
            start++;
        }
        size_t end = line.size();
        while (end > start &&
               std::isspace(static_cast<unsigned char>(line[end - 1]))) {
            end--;
        }
        return std::string(line.substr(start, end - start));
    };
    auto shortenLine = [](const std::string& line, size_t maxLen) -> std::string {
        if (line.size() <= maxLen) {
            return line;
        }
        std::string clipped = line.substr(0, maxLen);
        clipped.append("...");
        return clipped;
    };
    auto reportDiagnostics = [&](std::string_view prefix,
                                 const auto& messages,
                                 auto kindToLevel,
                                 auto isErrorKind) -> bool {
        bool hasError = false;
        for (const auto &message : messages)
        {
            const auto level = kindToLevel(message.kind);
            if (isErrorKind(message.kind))
            {
                hasError = true;
            }
            if (!shouldLog(level))
            {
                continue;
            }
            std::cerr << "[" << prefix << "] [" << logLevelText(level) << "] ";

            bool printedLocation = false;
            std::string statementSnippet;
            if (sourceManager && message.location && message.location->valid())
            {
                const auto loc = sourceManager->getFullyOriginalLoc(*message.location);
                if (loc.valid() && sourceManager->isFileLoc(loc))
                {
                    auto fileName = sourceManager->getFileName(loc);
                    auto line = sourceManager->getLineNumber(loc);
                    auto column = sourceManager->getColumnNumber(loc);
                    std::cerr << fileName << ":" << line << ":" << column << " ";
                    printedLocation = true;

                    const std::string_view text = sourceManager->getSourceText(loc.buffer());
                    if (!text.empty())
                    {
                        const std::string_view lineText = extractLine(text, loc.offset());
                        if (!lineText.empty())
                        {
                            statementSnippet = shortenLine(trimLine(lineText), 200);
                        }
                    }
                }
            }
            if (!printedLocation && !message.originSymbol.empty())
            {
                std::cerr << message.originSymbol << " ";
            }
            std::cerr << "- " << message.message << '\n';
            if (!statementSnippet.empty())
            {
                std::cerr << "  statement: " << statementSnippet << '\n';
            }
        }
        return hasError;
    };

    bool hasFrontendError = false;
    wolf_sv_parser::ConvertOptions convertOptions;
    convertOptions.abortOnError = true;
    convertOptions.enableLogging = globalLogLevel != wolf_sv_parser::LogLevel::Off;
    convertOptions.logLevel = globalLogLevel;
    convertOptions.enableTiming = timingEnabled;
    if (convertThreads)
    {
        if (*convertThreads <= 0)
        {
            logLine(wolf_sv_parser::LogLevel::Error, "convert", {},
                    "--convert-threads must be a positive number");
            return 1;
        }
        convertOptions.threadCount = static_cast<uint32_t>(*convertThreads);
    }
    if (singleThread == true)
    {
        convertOptions.singleThread = true;
    }

    wolf_sv_parser::ConvertDriver converter(convertOptions);
    converter.logger().setSink([&](const wolf_sv_parser::LogEvent& event) {
        logLine(event.level, "convert", event.tag, event.message);
    });
    const auto convertStart = TimingClock::now();
    bool convertAborted = false;
    try {
        netlist = converter.convert(root);
    } catch (const wolf_sv_parser::ConvertAbort&) {
        // Diagnostics already recorded; stop conversion immediately.
        convertAborted = true;
    }
    const auto convertEnd = TimingClock::now();
    const std::string convertLabel =
        convertAborted ? std::string("convert-total (aborted)") : std::string("convert-total");
    logTimingStage("convert", convertLabel, convertStart, convertEnd);

    auto kindToLevel = [](wolf_sv_parser::ConvertDiagnosticKind kind) {
        switch (kind)
        {
        case wolf_sv_parser::ConvertDiagnosticKind::Todo:
            return wolf_sv_parser::LogLevel::Error;
        case wolf_sv_parser::ConvertDiagnosticKind::Warning:
            return wolf_sv_parser::LogLevel::Warn;
        case wolf_sv_parser::ConvertDiagnosticKind::Error:
        default:
            return wolf_sv_parser::LogLevel::Error;
        }
    };
    auto isErrorKind = [](wolf_sv_parser::ConvertDiagnosticKind kind) {
        return kind == wolf_sv_parser::ConvertDiagnosticKind::Error ||
               kind == wolf_sv_parser::ConvertDiagnosticKind::Todo;
    };
    if (!converter.diagnostics().empty())
    {
        hasFrontendError = reportDiagnostics("convert", converter.diagnostics().messages(),
                                             kindToLevel, isErrorKind);
    }
    if (converter.diagnostics().hasError())
    {
        hasFrontendError = true;
    }

    if (hasFrontendError)
    {
        logLine(wolf_sv_parser::LogLevel::Error, "convert", {},
                "Build failed: convert encountered errors");
        return 2;
    }

    if (netlist.graphs().empty())
    {
        logLine(wolf_sv_parser::LogLevel::Warn, "convert", {},
                "Netlist is empty; skipping transform and emit");
        return driver.reportDiagnostics(/* quiet */ false) ? 0 : 4;
    }

    bool transformOk = true;
    const auto transformStart = TimingClock::now();
    if (skipTransform == true)
    {
        logLine(wolf_sv_parser::LogLevel::Info, "transform", {}, "skipped");
        const auto transformEnd = TimingClock::now();
        logTimingStage("transform", "transform", transformStart, transformEnd);
    }
    else
    {
        // Transform stage: built-in passes can be registered here; no CLI-configured pipeline for now.
        wolf_sv_parser::transform::PassDiagnostics transformDiagnostics;
        wolf_sv_parser::transform::PassManager passManager;
        auto transformKindToLevel = [](wolf_sv_parser::transform::PassDiagnosticKind kind) {
            switch (kind)
            {
            case wolf_sv_parser::transform::PassDiagnosticKind::Error:
                return wolf_sv_parser::LogLevel::Error;
            case wolf_sv_parser::transform::PassDiagnosticKind::Warning:
                return wolf_sv_parser::LogLevel::Warn;
            case wolf_sv_parser::transform::PassDiagnosticKind::Info:
                return wolf_sv_parser::LogLevel::Info;
            case wolf_sv_parser::transform::PassDiagnosticKind::Debug:
            default:
                return wolf_sv_parser::LogLevel::Debug;
            }
        };
        auto toTransformVerbosity = [](wolf_sv_parser::LogLevel level) {
            switch (level)
            {
            case wolf_sv_parser::LogLevel::Trace:
            case wolf_sv_parser::LogLevel::Debug:
                return wolf_sv_parser::transform::PassVerbosity::Debug;
            case wolf_sv_parser::LogLevel::Info:
                return wolf_sv_parser::transform::PassVerbosity::Info;
            case wolf_sv_parser::LogLevel::Warn:
                return wolf_sv_parser::transform::PassVerbosity::Warning;
            case wolf_sv_parser::LogLevel::Error:
            case wolf_sv_parser::LogLevel::Off:
            default:
                return wolf_sv_parser::transform::PassVerbosity::Error;
            }
        };
        passManager.options().verbosity = toTransformVerbosity(globalLogLevel);
        passManager.options().emitTiming = timingEnabled;
        passManager.options().logLevel = globalLogLevel;
        passManager.options().logSink =
            [&](wolf_sv_parser::LogLevel level,
                std::string_view tag, std::string_view message) {
                logLine(level, "transform", tag, message);
            };
        if (dropDeclaredSymbols)
        {
            passManager.options().keepDeclaredSymbols = false;
        }
        passManager.addPass(std::make_unique<wolf_sv_parser::transform::XmrResolvePass>());
        passManager.addPass(std::make_unique<wolf_sv_parser::transform::ConstantFoldPass>());
        passManager.addPass(std::make_unique<wolf_sv_parser::transform::RedundantElimPass>());
        passManager.addPass(std::make_unique<wolf_sv_parser::transform::MemoryInitCheckPass>());
        passManager.addPass(std::make_unique<wolf_sv_parser::transform::DeadCodeElimPass>());
        passManager.addPass(std::make_unique<wolf_sv_parser::transform::StatsPass>());
        wolf_sv_parser::transform::PassManagerResult passManagerResult =
            passManager.run(netlist, transformDiagnostics);
        const auto transformEnd = TimingClock::now();
        logTimingStage("transform", "transform", transformStart, transformEnd);

        if (!transformDiagnostics.empty())
        {
            for (const auto &message : transformDiagnostics.messages())
            {
                const auto level = transformKindToLevel(message.kind);
                if (!shouldLog(level))
                {
                    continue;
                }
                std::string text = message.message;
                if (!message.context.empty())
                {
                    text.append(" (");
                    text.append(message.context);
                    text.append(")");
                }
                logLine(level, "transform", message.passName, text);
            }
        }

        if (!passManagerResult.success || transformDiagnostics.hasError())
        {
            transformOk = false;
        }
    }

    if (!transformOk)
    {
        return 5;
    }

    bool emitOk = true;
    const bool wantsEmit = dumpJson == true || dumpSv == true;
    std::optional<TimingClock::time_point> emitStart;
    if (wantsEmit)
    {
        emitStart = TimingClock::now();
    }
    if (dumpJson == true)
    {
        grh::emit::EmitDiagnostics emitDiagnostics;
        grh::emit::EmitJSON emitter(&emitDiagnostics);

        grh::emit::EmitOptions emitOptions;
        emitOptions.jsonMode = grh::emit::JsonPrintMode::PrettyCompact;
        applyCommonEmitOptions(emitOptions);
        if (jsonOutputName)
        {
            emitOptions.outputFilename = *jsonOutputName;
        }

        grh::emit::EmitResult emitResult = emitter.emit(netlist, emitOptions);
        if (!emitDiagnostics.empty())
        {
            for (const auto &message : emitDiagnostics.messages())
            {
                const auto level = message.kind == grh::emit::EmitDiagnosticKind::Error
                    ? wolf_sv_parser::LogLevel::Error
                    : wolf_sv_parser::LogLevel::Warn;
                if (!shouldLog(level))
                {
                    continue;
                }
                std::string text = message.message;
                if (!message.context.empty())
                {
                    text.append(" (");
                    text.append(message.context);
                    text.append(")");
                }
                logLine(level, "emit-json", {}, text);
            }
        }

        emitOk = emitResult.success && !emitDiagnostics.hasError();
        if (emitResult.success && !emitResult.artifacts.empty())
        {
            logLine(wolf_sv_parser::LogLevel::Info, "emit-json", {},
                    std::string("Wrote GRH JSON to ") + emitResult.artifacts.front());
        }
        else if (!emitResult.success)
        {
            logLine(wolf_sv_parser::LogLevel::Error, "emit-json", {},
                    "Failed to emit GRH JSON");
        }
    }

    if (dumpSv == true)
    {
        grh::emit::EmitDiagnostics emitDiagnostics;
        grh::emit::EmitSystemVerilog emitter(&emitDiagnostics);
        grh::emit::EmitOptions emitOptions;
        applyCommonEmitOptions(emitOptions);
        if (svOutputName)
        {
            emitOptions.outputFilename = *svOutputName;
        }

        grh::emit::EmitResult emitResult = emitter.emit(netlist, emitOptions);
        if (!emitDiagnostics.empty())
        {
            for (const auto &message : emitDiagnostics.messages())
            {
                const auto level = message.kind == grh::emit::EmitDiagnosticKind::Error
                    ? wolf_sv_parser::LogLevel::Error
                    : wolf_sv_parser::LogLevel::Warn;
                if (!shouldLog(level))
                {
                    continue;
                }
                std::string text = message.message;
                if (!message.context.empty())
                {
                    text.append(" (");
                    text.append(message.context);
                    text.append(")");
                }
                logLine(level, "emit-sv", {}, text);
            }
        }
        emitOk = emitOk && emitResult.success && !emitDiagnostics.hasError();
        if (emitResult.success && !emitResult.artifacts.empty())
        {
            logLine(wolf_sv_parser::LogLevel::Info, "emit-sv", {},
                    std::string("Wrote SystemVerilog to ") + emitResult.artifacts.front());
        }
        else if (!emitResult.success)
        {
            logLine(wolf_sv_parser::LogLevel::Error, "emit-sv", {},
                    "Failed to emit SystemVerilog");
        }
    }

    if (emitStart)
    {
        const auto emitEnd = TimingClock::now();
        logTimingStage("emit", "emit", *emitStart, emitEnd);
    }

    bool ok = diagOk;
    ok = ok && emitOk;
    if (ok)
    {
        logLine(wolf_sv_parser::LogLevel::Info, "wolf", {}, "Completed successfully");
    }
    return ok ? 0 : 4;
}
