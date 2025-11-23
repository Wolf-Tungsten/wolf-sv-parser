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

using namespace slang::driver;

namespace
{

    void dumpScope(const slang::ast::Scope &scope, int indent = 0)
    {
        const std::string indentStr(static_cast<size_t>(indent) * 2, ' ');
        for (const slang::ast::Symbol &symbol : scope.members())
        {
            std::cout << indentStr << "- [" << slang::ast::toString(symbol.kind) << "] ";
            if (symbol.name.empty())
            {
                std::cout << "<anonymous>";
            }
            else
            {
                std::cout << symbol.name;
            }
            std::cout << '\n';

            if (const auto *instance = symbol.as_if<slang::ast::InstanceSymbol>())
            {
                dumpScope(instance->body, indent + 1);
            }
            else if (symbol.isScope())
            {
                dumpScope(symbol.as<slang::ast::Scope>(), indent + 1);
            }
        }
    }

} // namespace

int main(int argc, char **argv)
{
    Driver driver;
    driver.addStandardArgs();
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    std::optional<bool> dumpAst;
    driver.cmdLine.add("--dump-ast", dumpAst, "Dump a summary of the elaborated AST");
    std::optional<bool> dumpGrh;
    driver.cmdLine.add("--dump-grh", dumpGrh, "Dump GRH JSON after elaboration");
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
        const bool wantsJson = dumpGrh == true;

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

    if (!elaborateDiagnostics.empty())
    {
        const auto *sourceManager = compilation->getSourceManager();
        for (const auto &message : elaborateDiagnostics.messages())
        {
            const char *tag = message.kind == wolf_sv::ElaborateDiagnosticKind::Todo ? "TODO" : "NYI";
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

    bool emitOk = true;
    if (dumpGrh == true)
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
            std::cout << "=== GRH JSON ===\n";
            std::ifstream jsonFile(emitResult.artifacts.front());
            if (!jsonFile.is_open())
            {
                std::cerr << "[emit-json] Failed to read emitted artifact: " << emitResult.artifacts.front() << '\n';
                emitOk = false;
            }
            else
            {
                std::cout << jsonFile.rdbuf() << '\n';
            }
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
