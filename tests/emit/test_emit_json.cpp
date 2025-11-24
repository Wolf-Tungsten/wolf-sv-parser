#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace wolf_sv;
using namespace wolf_sv::emit;
using namespace wolf_sv::grh;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_json] " << message << '\n';
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

    Netlist buildDemoNetlist()
    {
        Netlist netlist;
        Graph &graph = netlist.createGraph("demo");

        Value &in = graph.createValue("in", 8, false);
        graph.bindInputPort("in", in);

        Value &out = graph.createValue("out", 8, false);
        graph.bindOutputPort("out", out);

        Operation &add = graph.createOperation(OperationKind::kAdd, "add0");
        add.addOperand(in);
        Value &sum = graph.createValue("sum", 8, false);
        add.addResult(sum);
        add.setAttribute("weights", AttributeValue(std::vector<int64_t>{1, 2}));

        Operation &assign = graph.createOperation(OperationKind::kAssign, "assign0");
        assign.addOperand(sum);
        assign.addResult(out);

        netlist.markAsTop(graph.symbol());
        return netlist;
    }

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

int main()
{
    // Case 1: missing top graphs should fail gracefully.
    EmitDiagnostics diagNoTop;
    EmitJSON emitterNoTop(&diagNoTop);
    Netlist emptyNetlist;
    EmitResult resultNoTop = emitterNoTop.emit(emptyNetlist);
    if (resultNoTop.success)
    {
        return fail("EmitJSON should fail when no tops are present");
    }
    if (!diagNoTop.hasError())
    {
        return fail("Expected diagnostics to capture missing tops for EmitJSON");
    }

    Netlist netlist = buildDemoNetlist();

    // Case 2: prettyCompact JSON emission with compact keys.
    EmitDiagnostics diagPrettyCompact;
    EmitJSON emitterPrettyCompact(&diagPrettyCompact);
    EmitOptions prettyCompactOptions;
    prettyCompactOptions.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);

    EmitResult prettyCompactResult = emitterPrettyCompact.emit(netlist, prettyCompactOptions);
    if (!prettyCompactResult.success)
    {
        return fail("EmitJSON prettyCompact path failed");
    }
    if (diagPrettyCompact.hasError())
    {
        return fail("Unexpected diagnostics errors for prettyCompact emit");
    }
    if (prettyCompactResult.artifacts.empty())
    {
        return fail("PrettyCompact emit did not report an artifact");
    }

    const std::filesystem::path prettyCompactPath = prettyCompactResult.artifacts.front();
    const std::string prettyCompactJson = readFile(prettyCompactPath);
    if (prettyCompactJson.find("\"vals\"") == std::string::npos || prettyCompactJson.find("\"ops\"") == std::string::npos)
    {
        return fail("Compressed keys vals/ops not found in prettyCompact JSON");
    }
    if (prettyCompactJson.find("\"tops\"") == std::string::npos)
    {
        return fail("Top graph list is missing in prettyCompact JSON");
    }
    if (prettyCompactJson.find("\"attrs\"") == std::string::npos || prettyCompactJson.find("\"int[]\"") == std::string::npos)
    {
        return fail("Attribute payload missing expected compact layout");
    }
    const std::size_t valsInlinePos = prettyCompactJson.find("{\"sym\": \"in\"");
    if (valsInlinePos == std::string::npos)
    {
        return fail("Value entry not rendered inline in prettyCompact mode");
    }
    const std::size_t newlineAfterVal = prettyCompactJson.find('\n', valsInlinePos);
    const std::size_t braceAfterVal = prettyCompactJson.find('}', valsInlinePos);
    if (newlineAfterVal != std::string::npos && newlineAfterVal < braceAfterVal)
    {
        return fail("Value entry spans multiple lines in prettyCompact mode");
    }

    Netlist parsed = Netlist::fromJsonString(prettyCompactJson);
    if (!parsed.findGraph("demo"))
    {
        return fail("Round-trip parsed netlist missing demo graph");
    }

    // Case 3: compact mode should differ from prettyCompact output and avoid newlines.
    EmitDiagnostics diagCompact;
    EmitJSON emitterCompact(&diagCompact);
    EmitOptions compactOptions = prettyCompactOptions;
    compactOptions.jsonMode = JsonPrintMode::Compact;

    EmitResult compactResult = emitterCompact.emit(netlist, compactOptions);
    if (!compactResult.success || diagCompact.hasError())
    {
        return fail("Compact emit failed");
    }

    const std::string compactJson = readFile(compactResult.artifacts.front());
    if (compactJson == prettyCompactJson)
    {
        return fail("Compact emit should produce different layout from prettyCompact emit");
    }
    if (compactJson.find('\n') != std::string::npos)
    {
        return fail("Compact JSON should not contain newlines");
    }

    return 0;
}
