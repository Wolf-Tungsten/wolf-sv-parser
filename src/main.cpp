#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

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
#include "pass/const_inline.hpp"
#include "pass/dead_code_elim.hpp"

using namespace slang::driver;

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
    std::optional<bool> convertLog;
    driver.cmdLine.add("--convert-log", convertLog, "Enable Convert debug logging");
    std::optional<std::string> convertLogLevel;
    driver.cmdLine.add("--convert-log-level", convertLogLevel,
                       "Convert log level: trace|debug|info|warn|error|off", "<level>");
    std::optional<std::string> convertLogTag;
    driver.cmdLine.add("--convert-log-tag", convertLogTag,
                       "Limit Convert logging to a tag (comma separated)", "<tag>");
    std::optional<std::string> emitOutputDir;
    driver.cmdLine.add("--emit-out-dir,--emit-out", emitOutputDir, "Directory to write emitted GRH/SV files", "<path>");
    std::optional<std::string> outputPathArg;
    driver.cmdLine.add("-o", outputPathArg, "Output file path for emitted artifacts", "<path>", slang::CommandLineFlags::FilePath);

    if (!driver.parseCommandLine(argc, argv)) {
        return 1;
    }

    if (!driver.processOptions()) {
        return 2;
    }

    if (!driver.parseAllSources()) {
        return 3;
    }

    auto compilation = driver.createCompilation();
    driver.reportCompilation(*compilation, /* quiet */ false);

    driver.runAnalysis(*compilation);

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
                                 auto kindToTag,
                                 auto isErrorKind) -> bool {
        bool hasError = false;
        for (const auto &message : messages)
        {
            const char *tag = kindToTag(message.kind);
            if (isErrorKind(message.kind))
            {
                hasError = true;
            }
            std::cerr << "[" << prefix << "] [" << tag << "] ";

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
    if (convertLog == true || (convertLogLevel && !convertLogLevel->empty()))
    {
        convertOptions.enableLogging = true;
        if (convertLogLevel && !convertLogLevel->empty())
        {
            std::string levelText = *convertLogLevel;
            for (char &c : levelText)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (levelText == "trace")
            {
                convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Trace;
            }
            else if (levelText == "debug")
            {
                convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Debug;
            }
            else if (levelText == "info")
            {
                convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Info;
            }
            else if (levelText == "warn")
            {
                convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Warn;
            }
            else if (levelText == "error")
            {
                convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Error;
            }
            else if (levelText == "off")
            {
                convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Off;
            }
            else
            {
                std::cerr << "[convert] Unknown log level: " << levelText << '\n';
                return 1;
            }
        }
        else
        {
            convertOptions.logLevel = wolf_sv_parser::ConvertLogLevel::Debug;
        }
    }

    wolf_sv_parser::ConvertDriver converter(convertOptions);
    converter.logger().setSink([](const wolf_sv_parser::ConvertLogEvent& event) {
        const char *levelText = "debug";
        switch (event.level)
        {
        case wolf_sv_parser::ConvertLogLevel::Trace:
            levelText = "trace";
            break;
        case wolf_sv_parser::ConvertLogLevel::Debug:
            levelText = "debug";
            break;
        case wolf_sv_parser::ConvertLogLevel::Info:
            levelText = "info";
            break;
        case wolf_sv_parser::ConvertLogLevel::Warn:
            levelText = "warn";
            break;
        case wolf_sv_parser::ConvertLogLevel::Error:
            levelText = "error";
            break;
        case wolf_sv_parser::ConvertLogLevel::Off:
        default:
            levelText = "off";
            break;
        }
        std::cerr << "[convert] [" << levelText << "]";
        if (!event.tag.empty())
        {
            std::cerr << " [" << event.tag << "]";
        }
        std::cerr << " " << event.message << '\n';
    });
    if (convertLogTag && !convertLogTag->empty())
    {
        std::string tags = *convertLogTag;
        std::size_t start = 0;
        while (start < tags.size())
        {
            std::size_t comma = tags.find(',', start);
            if (comma == std::string::npos)
            {
                comma = tags.size();
            }
            std::size_t end = comma;
            while (start < end && std::isspace(static_cast<unsigned char>(tags[start])))
            {
                start++;
            }
            while (end > start && std::isspace(static_cast<unsigned char>(tags[end - 1])))
            {
                end--;
            }
            if (end > start)
            {
                converter.logger().allowTag(std::string_view(tags).substr(start, end - start));
            }
            start = comma + 1;
        }
    }

    try {
        netlist = converter.convert(root);
    } catch (const wolf_sv_parser::ConvertAbort&) {
        // Diagnostics already recorded; stop conversion immediately.
    }

    auto kindToTag = [](wolf_sv_parser::ConvertDiagnosticKind kind) -> const char * {
        switch (kind)
        {
        case wolf_sv_parser::ConvertDiagnosticKind::Todo:
            return "TODO";
        case wolf_sv_parser::ConvertDiagnosticKind::Warning:
            return "WARN";
        case wolf_sv_parser::ConvertDiagnosticKind::Error:
        default:
            return "ERROR";
        }
    };
    auto isErrorKind = [](wolf_sv_parser::ConvertDiagnosticKind kind) {
        return kind == wolf_sv_parser::ConvertDiagnosticKind::Error;
    };
    if (!converter.diagnostics().empty())
    {
        hasFrontendError = reportDiagnostics("convert", converter.diagnostics().messages(),
                                             kindToTag, isErrorKind);
    }
    if (converter.diagnostics().hasError())
    {
        hasFrontendError = true;
    }

    if (hasFrontendError)
    {
        std::cerr << "Build failed: convert encountered errors\n";
        return 2;
    }

    if (netlist.graphs().empty())
    {
        std::cerr << "[convert] Netlist is empty; skipping transform and emit\n";
        return driver.reportDiagnostics(/* quiet */ false) ? 0 : 4;
    }

    // Transform stage: built-in passes can be registered here; no CLI-configured pipeline for now.
    wolf_sv_parser::transform::PassDiagnostics transformDiagnostics;
    wolf_sv_parser::transform::PassManager passManager;
    passManager.addPass(std::make_unique<wolf_sv_parser::transform::ConstantFoldPass>());
    passManager.addPass(std::make_unique<wolf_sv_parser::transform::ConstInlinePass>());
    passManager.addPass(std::make_unique<wolf_sv_parser::transform::DeadCodeElimPass>());
    passManager.addPass(std::make_unique<wolf_sv_parser::transform::StatsPass>());
    wolf_sv_parser::transform::PassManagerResult passManagerResult = passManager.run(netlist, transformDiagnostics);

    if (!transformDiagnostics.empty())
    {
        for (const auto &message : transformDiagnostics.messages())
        {
            const char *tag = "info";
            switch (message.kind)
            {
            case wolf_sv_parser::transform::PassDiagnosticKind::Error:
                tag = "error";
                break;
            case wolf_sv_parser::transform::PassDiagnosticKind::Warning:
                tag = "warn";
                break;
            case wolf_sv_parser::transform::PassDiagnosticKind::Debug:
                tag = "debug";
                break;
            case wolf_sv_parser::transform::PassDiagnosticKind::Info:
            default:
                tag = "info";
                break;
            }
            std::cerr << "[transform] [" << message.passName << "] [" << tag << "] " << message.message;
            if (!message.context.empty())
            {
                std::cerr << " (" << message.context << ")";
            }
            std::cerr << '\n';
        }
    }

    if (!passManagerResult.success || transformDiagnostics.hasError())
    {
        return 5;
    }

    bool emitOk = true;
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
                const char *tag = message.kind == grh::emit::EmitDiagnosticKind::Error ? "error" : "warn";
                std::cerr << "[emit-json] [" << tag << "] " << message.message;
                if (!message.context.empty())
                {
                    std::cerr << " (" << message.context << ")";
                }
                std::cerr << '\n';
            }
        }

        emitOk = emitResult.success && !emitDiagnostics.hasError();
        if (emitResult.success && !emitResult.artifacts.empty())
        {
            std::cout << "[emit-json] Wrote GRH JSON to " << emitResult.artifacts.front() << '\n';
        }
        else if (!emitResult.success)
        {
            std::cerr << "[emit-json] Failed to emit GRH JSON\n";
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
                const char *tag = message.kind == grh::emit::EmitDiagnosticKind::Error ? "error" : "warn";
                std::cerr << "[emit-sv] [" << tag << "] " << message.message;
                if (!message.context.empty())
                {
                    std::cerr << " (" << message.context << ")";
                }
                std::cerr << '\n';
            }
        }
        emitOk = emitOk && emitResult.success && !emitDiagnostics.hasError();
        if (emitResult.success && !emitResult.artifacts.empty())
        {
            std::cout << "[emit-sv] Wrote SystemVerilog to " << emitResult.artifacts.front() << '\n';
        }
        else if (!emitResult.success)
        {
            std::cerr << "[emit-sv] Failed to emit SystemVerilog\n";
        }
    }

    bool ok = driver.reportDiagnostics(/* quiet */ false);
    ok = ok && emitOk;
    return ok ? 0 : 4;
}
