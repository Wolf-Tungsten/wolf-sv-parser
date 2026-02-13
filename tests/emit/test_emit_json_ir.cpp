#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace grh::emit;
using namespace grh::ir;
namespace grh_ir = grh::ir;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_json_ir] " << message << '\n';
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

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

int main()
{
    grh_ir::Netlist netlist;
    grh_ir::Graph &graph = netlist.createGraph("demo_ir");

    const auto symA = graph.internSymbol("a");
    const auto symB = graph.internSymbol("b");
    const auto symSum = graph.internSymbol("sum");
    const auto symOut = graph.internSymbol("out");
    const auto symAdd = graph.internSymbol("add0");
    const auto symAssign = graph.internSymbol("assign0");

    const auto vA = graph.createValue(symA, 8, false);
    const auto vB = graph.createValue(symB, 8, false);
    const auto vSum = graph.createValue(symSum, 8, false);
    const auto vOut = graph.createValue(symOut, 8, false);

    graph.bindInputPort(symA, vA);
    graph.bindInputPort(symB, vB);
    graph.bindOutputPort(symOut, vOut);

    const auto opAdd = graph.createOperation(grh::ir::OperationKind::kAdd, symAdd);
    graph.addOperand(opAdd, vA);
    graph.addOperand(opAdd, vB);
    graph.addResult(opAdd, vSum);
    graph.setAttr(opAdd, "weights", AttributeValue(std::vector<int64_t>{1, 2}));

    const auto opAssign = graph.createOperation(grh::ir::OperationKind::kAssign, symAssign);
    graph.addOperand(opAssign, vSum);
    graph.addResult(opAssign, vOut);

    netlist.markAsTop(graph.symbol());

    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    options.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);
    options.outputFilename = std::string("emit_json_ir.json");

    EmitResult result = emitter.emit(netlist, options);
    if (!result.success || diagnostics.hasError())
    {
        return fail("emit failed");
    }
    if (result.artifacts.empty())
    {
        return fail("emit produced no artifact");
    }

    const std::filesystem::path jsonPath = result.artifacts.front();
    const std::string jsonText = readFile(jsonPath);
    if (jsonText.find("\"vals\"") == std::string::npos || jsonText.find("\"ops\"") == std::string::npos)
    {
        return fail("Missing vals/ops in JSON output");
    }
    if (jsonText.find("\"tops\"") == std::string::npos || jsonText.find("\"demo_ir\"") == std::string::npos)
    {
        return fail("Missing tops or graph symbol in JSON output");
    }
    if (jsonText.find("\"attrs\"") == std::string::npos || jsonText.find("\"int[]\"") == std::string::npos)
    {
        return fail("Missing attribute payload in JSON output");
    }

    grh::ir::Netlist parsed = grh::ir::Netlist::fromJsonString(jsonText);
    if (!parsed.findGraph("demo_ir"))
    {
        return fail("Parsed netlist missing demo_ir graph");
    }

    return 0;
}
