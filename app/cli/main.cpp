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

#include "ingest.hpp"
#include "emit.hpp"
#include "store.hpp"
#include "transform.hpp"
#include "transform/demo_stats.hpp"
#include "transform/const_fold.hpp"
#include "transform/memory_init_check.hpp"
#include "transform/redundant_elim.hpp"
#include "transform/dead_code_elim.hpp"
#include "transform/xmr_resolve.hpp"

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

const char* logLevelText(wolvrix::lib::LogLevel level)
{
    switch (level)
    {
    case wolvrix::lib::LogLevel::Trace:
        return "trace";
    case wolvrix::lib::LogLevel::Debug:
        return "debug";
    case wolvrix::lib::LogLevel::Info:
        return "info";
    case wolvrix::lib::LogLevel::Warn:
        return "warn";
    case wolvrix::lib::LogLevel::Error:
        return "error";
    case wolvrix::lib::LogLevel::Off:
    default:
        return "off";
    }
}

std::optional<wolvrix::lib::LogLevel> parseLogLevel(std::string_view text)
{
    std::string lowered(text);
    for (char &c : lowered)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lowered == "trace")
    {
        return wolvrix::lib::LogLevel::Trace;
    }
    if (lowered == "debug")
    {
        return wolvrix::lib::LogLevel::Debug;
    }
    if (lowered == "info")
    {
        return wolvrix::lib::LogLevel::Info;
    }
    if (lowered == "warn" || lowered == "warning")
    {
        return wolvrix::lib::LogLevel::Warn;
    }
    if (lowered == "error")
    {
        return wolvrix::lib::LogLevel::Error;
    }
    if (lowered == "off" || lowered == "none")
    {
        return wolvrix::lib::LogLevel::Off;
    }
    return std::nullopt;
}

} // namespace

namespace wolvrix::app::cli
{

int run(int argc, char **argv)
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
    wolvrix::lib::LogLevel globalLogLevel = wolvrix::lib::LogLevel::Info;
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
            static_cast<int>(globalLogLevel) > static_cast<int>(wolvrix::lib::LogLevel::Debug))
        {
            globalLogLevel = wolvrix::lib::LogLevel::Debug;
        }
    }
    auto shouldLog = [&](wolvrix::lib::LogLevel level) -> bool {
        if (globalLogLevel == wolvrix::lib::LogLevel::Off)
        {
            return false;
        }
        return static_cast<int>(level) >= static_cast<int>(globalLogLevel);
    };
    auto logLine = [&](wolvrix::lib::LogLevel level, std::string_view prefix,
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
    logLine(wolvrix::lib::LogLevel::Info, "slang", {}, slangBegin);
    if (!driver.parseAllSources()) {
        std::string slangEnd = "end (parse failed, errors=";
        slangEnd.append(std::to_string(driver.diagEngine.getNumErrors()));
        slangEnd.append(", warnings=");
        slangEnd.append(std::to_string(driver.diagEngine.getNumWarnings()));
        slangEnd.append(")");
        logLine(wolvrix::lib::LogLevel::Info, "slang", {}, slangEnd);
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
    logLine(wolvrix::lib::LogLevel::Info, "slang", {}, slangEnd);
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

    auto applyCommonEmitOptions = [&](wolvrix::lib::emit::EmitOptions &emitOptions)
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
    auto applyCommonStoreOptions = [&](wolvrix::lib::store::StoreOptions &storeOptions)
    {
        if (outputDirOverride)
        {
            storeOptions.outputDir = *outputDirOverride;
        }
        else if (emitOutputDir && !emitOutputDir->empty())
        {
            storeOptions.outputDir = *emitOutputDir;
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

    wolvrix::lib::grh::Netlist netlist;
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
    wolvrix::lib::ingest::ConvertOptions convertOptions;
    convertOptions.abortOnError = true;
    convertOptions.enableLogging = globalLogLevel != wolvrix::lib::LogLevel::Off;
    convertOptions.logLevel = globalLogLevel;
    convertOptions.enableTiming = timingEnabled;
    if (convertThreads)
    {
        if (*convertThreads <= 0)
        {
            logLine(wolvrix::lib::LogLevel::Error, "convert", {},
                    "--convert-threads must be a positive number");
            return 1;
        }
        convertOptions.threadCount = static_cast<uint32_t>(*convertThreads);
    }
    if (singleThread == true)
    {
        convertOptions.singleThread = true;
    }

    wolvrix::lib::ingest::ConvertDriver converter(convertOptions);
    converter.logger().setSink([&](const wolvrix::lib::LogEvent& event) {
        logLine(event.level, "convert", event.tag, event.message);
    });
    const auto convertStart = TimingClock::now();
    bool convertAborted = false;
    try {
        netlist = converter.convert(root);
    } catch (const wolvrix::lib::ingest::ConvertAbort&) {
        // Diagnostics already recorded; stop conversion immediately.
        convertAborted = true;
    }
    const auto convertEnd = TimingClock::now();
    const std::string convertLabel =
        convertAborted ? std::string("convert-total (aborted)") : std::string("convert-total");
    logTimingStage("convert", convertLabel, convertStart, convertEnd);

    auto kindToLevel = [](wolvrix::lib::ingest::ConvertDiagnosticKind kind) {
        switch (kind)
        {
        case wolvrix::lib::ingest::ConvertDiagnosticKind::Todo:
            return wolvrix::lib::LogLevel::Error;
        case wolvrix::lib::ingest::ConvertDiagnosticKind::Warning:
            return wolvrix::lib::LogLevel::Warn;
        case wolvrix::lib::ingest::ConvertDiagnosticKind::Info:
            return wolvrix::lib::LogLevel::Info;
        case wolvrix::lib::ingest::ConvertDiagnosticKind::Debug:
            return wolvrix::lib::LogLevel::Debug;
        case wolvrix::lib::ingest::ConvertDiagnosticKind::Error:
        default:
            return wolvrix::lib::LogLevel::Error;
        }
    };
    auto isErrorKind = [](wolvrix::lib::ingest::ConvertDiagnosticKind kind) {
        return kind == wolvrix::lib::ingest::ConvertDiagnosticKind::Error ||
               kind == wolvrix::lib::ingest::ConvertDiagnosticKind::Todo;
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
        logLine(wolvrix::lib::LogLevel::Error, "convert", {},
                "Build failed: convert encountered errors");
        return 2;
    }

    if (netlist.graphs().empty())
    {
        logLine(wolvrix::lib::LogLevel::Warn, "convert", {},
                "Netlist is empty; skipping transform and emit");
        return driver.reportDiagnostics(/* quiet */ false) ? 0 : 4;
    }

    bool transformOk = true;
    const auto transformStart = TimingClock::now();
    if (skipTransform == true)
    {
        logLine(wolvrix::lib::LogLevel::Info, "transform", {}, "skipped");
        const auto transformEnd = TimingClock::now();
        logTimingStage("transform", "transform", transformStart, transformEnd);
    }
    else
    {
        // Transform stage: built-in passes can be registered here; no CLI-configured pipeline for now.
        wolvrix::lib::transform::PassDiagnostics transformDiagnostics;
        wolvrix::lib::transform::PassManager passManager;
        auto transformKindToLevel = [](wolvrix::lib::transform::PassDiagnosticKind kind) {
            switch (kind)
            {
            case wolvrix::lib::transform::PassDiagnosticKind::Error:
            case wolvrix::lib::transform::PassDiagnosticKind::Todo:
                return wolvrix::lib::LogLevel::Error;
            case wolvrix::lib::transform::PassDiagnosticKind::Warning:
                return wolvrix::lib::LogLevel::Warn;
            case wolvrix::lib::transform::PassDiagnosticKind::Info:
                return wolvrix::lib::LogLevel::Info;
            case wolvrix::lib::transform::PassDiagnosticKind::Debug:
            default:
                return wolvrix::lib::LogLevel::Debug;
            }
        };
        auto toTransformVerbosity = [](wolvrix::lib::LogLevel level) {
            switch (level)
            {
            case wolvrix::lib::LogLevel::Trace:
            case wolvrix::lib::LogLevel::Debug:
                return wolvrix::lib::transform::PassVerbosity::Debug;
            case wolvrix::lib::LogLevel::Info:
                return wolvrix::lib::transform::PassVerbosity::Info;
            case wolvrix::lib::LogLevel::Warn:
                return wolvrix::lib::transform::PassVerbosity::Warning;
            case wolvrix::lib::LogLevel::Error:
            case wolvrix::lib::LogLevel::Off:
            default:
                return wolvrix::lib::transform::PassVerbosity::Error;
            }
        };
        passManager.options().verbosity = toTransformVerbosity(globalLogLevel);
        passManager.options().emitTiming = timingEnabled;
        passManager.options().logLevel = globalLogLevel;
        passManager.options().logSink =
            [&](wolvrix::lib::LogLevel level,
                std::string_view tag, std::string_view message) {
                logLine(level, "transform", tag, message);
            };
        if (dropDeclaredSymbols)
        {
            passManager.options().keepDeclaredSymbols = false;
        }
        passManager.addPass(std::make_unique<wolvrix::lib::transform::XmrResolvePass>());
        passManager.addPass(std::make_unique<wolvrix::lib::transform::ConstantFoldPass>());
        passManager.addPass(std::make_unique<wolvrix::lib::transform::RedundantElimPass>());
        passManager.addPass(std::make_unique<wolvrix::lib::transform::MemoryInitCheckPass>());
        passManager.addPass(std::make_unique<wolvrix::lib::transform::DeadCodeElimPass>());
        passManager.addPass(std::make_unique<wolvrix::lib::transform::StatsPass>());
        wolvrix::lib::transform::PassManagerResult passManagerResult =
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
        wolvrix::lib::store::StoreDiagnostics storeDiagnostics;
        wolvrix::lib::store::StoreJson emitter(&storeDiagnostics);

        wolvrix::lib::store::StoreOptions storeOptions;
        storeOptions.jsonMode = wolvrix::lib::store::JsonPrintMode::PrettyCompact;
        applyCommonStoreOptions(storeOptions);
        if (jsonOutputName)
        {
            storeOptions.outputFilename = *jsonOutputName;
        }

        wolvrix::lib::store::StoreResult storeResult = emitter.store(netlist, storeOptions);
        if (!storeDiagnostics.empty())
        {
            for (const auto &message : storeDiagnostics.messages())
            {
                const auto level =
                    (message.kind == wolvrix::lib::store::StoreDiagnosticKind::Error ||
                     message.kind == wolvrix::lib::store::StoreDiagnosticKind::Todo)
                        ? wolvrix::lib::LogLevel::Error
                        : (message.kind == wolvrix::lib::store::StoreDiagnosticKind::Warning
                               ? wolvrix::lib::LogLevel::Warn
                               : (message.kind == wolvrix::lib::store::StoreDiagnosticKind::Info
                                      ? wolvrix::lib::LogLevel::Info
                                      : wolvrix::lib::LogLevel::Debug));
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

        emitOk = storeResult.success && !storeDiagnostics.hasError();
        if (storeResult.success && !storeResult.artifacts.empty())
        {
            logLine(wolvrix::lib::LogLevel::Info, "emit-json", {},
                    std::string("Wrote GRH JSON to ") + storeResult.artifacts.front());
        }
        else if (!storeResult.success)
        {
            logLine(wolvrix::lib::LogLevel::Error, "emit-json", {},
                    "Failed to emit GRH JSON");
        }
    }

    if (dumpSv == true)
    {
        wolvrix::lib::emit::EmitDiagnostics emitDiagnostics;
        wolvrix::lib::emit::EmitSystemVerilog emitter(&emitDiagnostics);
        wolvrix::lib::emit::EmitOptions emitOptions;
        applyCommonEmitOptions(emitOptions);
        if (svOutputName)
        {
            emitOptions.outputFilename = *svOutputName;
        }

        wolvrix::lib::emit::EmitResult emitResult = emitter.emit(netlist, emitOptions);
        if (!emitDiagnostics.empty())
        {
            for (const auto &message : emitDiagnostics.messages())
            {
                const auto level =
                    (message.kind == wolvrix::lib::emit::EmitDiagnosticKind::Error ||
                     message.kind == wolvrix::lib::emit::EmitDiagnosticKind::Todo)
                        ? wolvrix::lib::LogLevel::Error
                        : (message.kind == wolvrix::lib::emit::EmitDiagnosticKind::Warning
                               ? wolvrix::lib::LogLevel::Warn
                               : (message.kind == wolvrix::lib::emit::EmitDiagnosticKind::Info
                                      ? wolvrix::lib::LogLevel::Info
                                      : wolvrix::lib::LogLevel::Debug));
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
            logLine(wolvrix::lib::LogLevel::Info, "emit-sv", {},
                    std::string("Wrote SystemVerilog to ") + emitResult.artifacts.front());
        }
        else if (!emitResult.success)
        {
            logLine(wolvrix::lib::LogLevel::Error, "emit-sv", {},
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
        logLine(wolvrix::lib::LogLevel::Info, "wolf", {}, "Completed successfully");
    }
    return ok ? 0 : 4;
}

} // namespace wolvrix::app::cli

int main(int argc, char **argv)
{
    return wolvrix::app::cli::run(argc, argv);
}
