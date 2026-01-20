#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace wolf_sv;
using namespace wolf_sv::emit;
using namespace wolf_sv::grh;
namespace grh_ir = wolf_sv::grh::ir;

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
    grh_ir::GraphSymbolTable symbols;
    const auto symA = symbols.intern("a");
    const auto symB = symbols.intern("b");
    const auto symSum = symbols.intern("sum");
    const auto symOut = symbols.intern("out");
    const auto symAdd = symbols.intern("add0");
    const auto symAssign = symbols.intern("assign0");
    const auto symWeights = symbols.intern("weights");

    grh_ir::GraphBuilder builder(symbols);
    const auto vA = builder.addValue(symA, 8, false);
    const auto vB = builder.addValue(symB, 8, false);
    const auto vSum = builder.addValue(symSum, 8, false);
    const auto vOut = builder.addValue(symOut, 8, false);

    builder.bindInputPort(symA, vA);
    builder.bindInputPort(symB, vB);
    builder.bindOutputPort(symOut, vOut);

    const auto opAdd = builder.addOp(grh::OperationKind::kAdd, symAdd);
    builder.addOperand(opAdd, vA);
    builder.addOperand(opAdd, vB);
    builder.addResult(opAdd, vSum);
    builder.setAttr(opAdd, symWeights, AttributeValue(std::vector<int64_t>{1, 2}));

    const auto opAssign = builder.addOp(grh::OperationKind::kAssign, symAssign);
    builder.addOperand(opAssign, vSum);
    builder.addResult(opAssign, vOut);

    const grh_ir::GraphView view = builder.freeze();

    EmitDiagnostics diagnostics;
    EmitJSON emitter(&diagnostics);
    EmitOptions options;
    options.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);
    options.outputFilename = std::string("emit_json_ir.json");

    EmitResult result = emitter.emitGraphView(view, symbols, "demo_ir", options);
    if (!result.success || diagnostics.hasError())
    {
        return fail("emitGraphView failed");
    }
    if (result.artifacts.empty())
    {
        return fail("emitGraphView produced no artifact");
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

    grh::Netlist parsed = grh::Netlist::fromJsonString(jsonText);
    if (!parsed.findGraph("demo_ir"))
    {
        return fail("Parsed netlist missing demo_ir graph");
    }

    return 0;
}
