#include "emit.hpp"
#include "elaborate.hpp"
#include "grh.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

using namespace wolf_sv;
using namespace wolf_sv::emit;
using namespace wolf_sv::grh;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_sv] " << message << '\n';
        return 1;
    }

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    std::optional<Netlist> elaborateFromFile(const std::filesystem::path &sourcePath)
    {
        slang::driver::Driver driver;
        driver.addStandardArgs();
        driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

        std::vector<std::string> argStorage;
        argStorage.emplace_back("emit-sv");
        argStorage.emplace_back(sourcePath.string());
        std::vector<const char *> argv;
        for (const std::string &arg : argStorage)
        {
            argv.push_back(arg.c_str());
        }

        if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()))
        {
            return std::nullopt;
        }
        if (!driver.processOptions())
        {
            return std::nullopt;
        }
        if (!driver.parseAllSources())
        {
            return std::nullopt;
        }

        auto compilation = driver.createCompilation();
        if (!compilation)
        {
            return std::nullopt;
        }
        driver.reportCompilation(*compilation, /* quiet */ true);
        driver.runAnalysis(*compilation);

        ElaborateDiagnostics diagnostics;
        Elaborate elaborator(&diagnostics);
        Netlist netlist = elaborator.convert(compilation->getRoot());
        if (!diagnostics.messages().empty())
        {
            return std::nullopt;
        }
        return netlist;
    }

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

#ifndef WOLF_SV_EMIT_SV_INPUT_PATH
#error "WOLF_SV_EMIT_SV_INPUT_PATH must be defined"
#endif

int main()
{
    const std::filesystem::path inputPath = std::filesystem::path(WOLF_SV_EMIT_SV_INPUT_PATH);
    if (!std::filesystem::exists(inputPath))
    {
        return fail("Missing input sv: " + inputPath.string());
    }

    auto netlistOpt = elaborateFromFile(inputPath);
    if (!netlistOpt)
    {
        return fail("Elaboration failed");
    }
    Netlist &netlist = *netlistOpt;
    if (netlist.topGraphs().empty())
    {
        return fail("No top graphs after elaboration");
    }

    EmitDiagnostics diagnostics;
    EmitSystemVerilog emitter(&diagnostics);
    EmitOptions options;
    options.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);

    EmitResult result = emitter.emit(netlist, options);
    if (!result.success || diagnostics.hasError())
    {
        return fail("Emission failed");
    }
    if (result.artifacts.empty())
    {
        return fail("No artifact reported");
    }

    const std::filesystem::path svPath = result.artifacts.front();
    const std::string sv = readFile(svPath);
    if (sv.find("module emit_sv_child") == std::string::npos || sv.find("module emit_sv_top") == std::string::npos)
    {
        return fail("Missing emitted modules");
    }
    if (sv.find("output reg [7:0] dout") == std::string::npos)
    {
        return fail("Output register not declared as reg");
    }
    if (sv.find("output reg [7:0] async_dout") == std::string::npos ||
        sv.find("always @(posedge clk or negedge rst)") == std::string::npos)
    {
        return fail("Async reset register not emitted correctly");
    }
    if (sv.find("emit_sv_child u_child (\n    .cin(en),\n    .cout(cout)\n  );") == std::string::npos)
    {
        return fail("Instance indentation incorrect");
    }
    const std::size_t firstAlways = sv.find("always @");
    const std::size_t secondAlways = sv.find("always @", firstAlways == std::string::npos ? 0 : firstAlways + 1);
    if (firstAlways == std::string::npos || secondAlways == std::string::npos)
    {
        return fail("Expected multiple sequential always blocks");
    }
    if (sv.find("\n    if (rst") == std::string::npos)
    {
        return fail("Sequential block indentation missing");
    }

    // Attempt lint with verilator if available.
    const std::string lintCmd = "verilator --lint-only " + svPath.string() + " > /dev/null 2>&1";
    const int lintStatus = std::system(lintCmd.c_str());
    const int cmdNotFound = 127 << 8;
    if (lintStatus != 0 && lintStatus != cmdNotFound)
    {
        return fail("verilator lint failed");
    }

    return 0;
}
