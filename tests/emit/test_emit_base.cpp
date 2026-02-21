#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace wolvrix::lib::emit;
using namespace wolvrix::lib::grh;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_base] " << message << '\n';
        return 1;
    }

    class StubEmit : public Emit
    {
    public:
        using Emit::Emit;

        std::size_t callCount = 0;
        std::size_t lastTopCount = 0;
        std::filesystem::path lastOutputPath;

    private:
        EmitResult emitImpl(const Netlist &, std::span<const Graph *const> topGraphs, const EmitOptions &options) override
        {
            ++callCount;
            lastTopCount = topGraphs.size();

            EmitResult result;
            if (!options.outputDir)
            {
                result.success = false;
                return result;
            }

            lastOutputPath = std::filesystem::path(*options.outputDir) / "emit_stub.txt";
            auto stream = openOutputFile(lastOutputPath);
            if (!stream)
            {
                result.success = false;
                return result;
            }

            *stream << "emit_stub";
            result.artifacts.push_back(lastOutputPath.string());
            return result;
        }
    };

} // namespace

int main()
{
#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

    // Case 1: no tops available
    EmitDiagnostics diagNoTop;
    StubEmit emitterNoTop(&diagNoTop);
    Netlist emptyNetlist;
    EmitResult noTopResult = emitterNoTop.emit(emptyNetlist);
    if (noTopResult.success)
    {
        return fail("Expected emit to fail when no top graphs are present");
    }
    if (!diagNoTop.hasError())
    {
        return fail("Expected diagnostics to record an error for missing tops");
    }
    if (emitterNoTop.callCount != 0)
    {
        return fail("emitImpl should not be invoked when tops are missing");
    }

    // Case 2: override points to missing top
    EmitDiagnostics diagMissingOverride;
    StubEmit emitterMissingOverride(&diagMissingOverride);
    Netlist netlistWithTop;
    netlistWithTop.createGraph("demo");
    EmitOptions missingOverrideOptions;
    missingOverrideOptions.topOverrides.push_back("absent_top");
    EmitResult missingOverrideResult = emitterMissingOverride.emit(netlistWithTop, missingOverrideOptions);
    if (missingOverrideResult.success)
    {
        return fail("Expected emit to fail when override top cannot be resolved");
    }
    if (!diagMissingOverride.hasError())
    {
        return fail("Expected diagnostics to capture missing override error");
    }
    if (emitterMissingOverride.callCount != 0)
    {
        return fail("emitImpl should not be called when override tops are unresolved");
    }

    // Case 3: successful path with output
    EmitDiagnostics diagOk;
    StubEmit emitterOk(&diagOk);
    Netlist okNetlist;
    Graph &topGraph = okNetlist.createGraph("top");
    okNetlist.markAsTop(topGraph.symbol());

    EmitOptions okOptions;
    okOptions.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);

    EmitResult okResult = emitterOk.emit(okNetlist, okOptions);
    if (!okResult.success)
    {
        return fail("Expected emit to succeed for valid top and output dir");
    }
    if (diagOk.hasError())
    {
        return fail("Unexpected diagnostics errors for successful emit path");
    }
    if (emitterOk.callCount != 1)
    {
        return fail("emitImpl should be invoked once on successful emit");
    }
    if (emitterOk.lastTopCount != 1)
    {
        return fail("emitImpl should see exactly one top graph");
    }
    if (okResult.artifacts.empty())
    {
        return fail("EmitResult should record produced artifacts");
    }

    const std::filesystem::path artifactPath = emitterOk.lastOutputPath;
    if (!std::filesystem::exists(artifactPath))
    {
        return fail("Expected output artifact file to be created");
    }

    std::ifstream artifactFile(artifactPath);
    std::string content;
    artifactFile >> content;
    if (content != "emit_stub")
    {
        return fail("Unexpected artifact content from emitImpl");
    }

    return 0;
}
