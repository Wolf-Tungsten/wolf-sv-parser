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

#include "elaborate.hpp"
#include "emit.hpp"
#include "transform.hpp"
#include "pass/demo_stats.hpp"
#include "pass/grh_verify.hpp"

using namespace slang::driver;

int main(int argc, char **argv)
{
    Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::optional<bool> dumpAst;
    driver.cmdLine.add("--dump-ast", dumpAst, "Dump a summary of the elaborated AST");
    std::optional<bool> dumpJson;
    driver.cmdLine.add("--emit-json", dumpJson, "Emit GRH JSON after elaboration");
    std::optional<bool> dumpSv;
    driver.cmdLine.add("--emit-sv", dumpSv, "Emit SystemVerilog after elaboration");
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

    auto applyCommonEmitOptions = [&](wolf_sv::emit::EmitOptions &emitOptions)
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

    wolf_sv::ElaborateDiagnostics elaborateDiagnostics;
    wolf_sv::Elaborate elaborator(&elaborateDiagnostics);
    auto netlist = elaborator.convert(root);

    bool hasElaborateError = false;
    if (!elaborateDiagnostics.empty())
    {
        const auto *sourceManager = compilation->getSourceManager();
        for (const auto &message : elaborateDiagnostics.messages())
        {
            const char *tag = "NYI";
            switch (message.kind)
            {
            case wolf_sv::ElaborateDiagnosticKind::Todo:
                tag = "TODO";
                break;
            case wolf_sv::ElaborateDiagnosticKind::Warning:
                tag = "WARN";
                break;
            case wolf_sv::ElaborateDiagnosticKind::NotYetImplemented:
            default:
                tag = "NYI";
                hasElaborateError = true;
                break;
            }
            std::cerr << "[elaborate] [" << tag << "] ";

            bool printedLocation = false;
            if (sourceManager && message.location && message.location->valid())
            {
                const auto loc = sourceManager->getFullyOriginalLoc(*message.location);
                if (loc.valid() && sourceManager->isFileLoc(loc))
                {
                    auto fileName = sourceManager->getFileName(loc);
                    auto line = sourceManager->getLineNumber(loc);
                    std::cerr << fileName << ":" << line << " ";
                    printedLocation = true;
                }
            }
            if (!printedLocation && !message.originSymbol.empty())
            {
                std::cerr << message.originSymbol << " ";
            }
            std::cerr << "- " << message.message << '\n';
        }
    }

    // Terminate if there are NYI (Not Yet Implemented) errors
    if (hasElaborateError)
    {
        std::cerr << "Build failed: elaboration encountered Not Yet Implemented features\n";
        return 2;
    }

    // Transform stage: built-in passes can be registered here; no CLI-configured pipeline for now.
    wolf_sv::transform::PassDiagnostics transformDiagnostics;
    wolf_sv::transform::PassManager passManager;
    passManager.addPass(std::make_unique<wolf_sv::transform::GRHVerifyPass>());
    passManager.addPass(std::make_unique<wolf_sv::transform::StatsPass>());
    wolf_sv::transform::PassManagerResult passManagerResult = passManager.run(netlist, transformDiagnostics);

    if (!transformDiagnostics.empty())
    {
        for (const auto &message : transformDiagnostics.messages())
        {
            const char *tag = "info";
            switch (message.kind)
            {
            case wolf_sv::transform::PassDiagnosticKind::Error:
                tag = "error";
                break;
            case wolf_sv::transform::PassDiagnosticKind::Warning:
                tag = "warn";
                break;
            case wolf_sv::transform::PassDiagnosticKind::Debug:
                tag = "debug";
                break;
            case wolf_sv::transform::PassDiagnosticKind::Info:
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
        wolf_sv::emit::EmitDiagnostics emitDiagnostics;
        wolf_sv::emit::EmitJSON emitter(&emitDiagnostics);

        wolf_sv::emit::EmitOptions emitOptions;
        emitOptions.jsonMode = wolf_sv::emit::JsonPrintMode::PrettyCompact;
        applyCommonEmitOptions(emitOptions);
        if (jsonOutputName)
        {
            emitOptions.outputFilename = *jsonOutputName;
        }

        wolf_sv::emit::EmitResult emitResult = emitter.emit(netlist, emitOptions);
        if (!emitDiagnostics.empty())
        {
            for (const auto &message : emitDiagnostics.messages())
            {
                const char *tag = message.kind == wolf_sv::emit::EmitDiagnosticKind::Error ? "error" : "warn";
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
        wolf_sv::emit::EmitDiagnostics emitDiagnostics;
        wolf_sv::emit::EmitSystemVerilog emitter(&emitDiagnostics);
        wolf_sv::emit::EmitOptions emitOptions;
        applyCommonEmitOptions(emitOptions);
        if (svOutputName)
        {
            emitOptions.outputFilename = *svOutputName;
        }

        wolf_sv::emit::EmitResult emitResult = emitter.emit(netlist, emitOptions);
        if (!emitDiagnostics.empty())
        {
            for (const auto &message : emitDiagnostics.messages())
            {
                const char *tag = message.kind == wolf_sv::emit::EmitDiagnosticKind::Error ? "error" : "warn";
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
