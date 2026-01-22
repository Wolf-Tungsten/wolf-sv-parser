#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace grh::emit;
namespace grh_ir = grh::ir;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[emit_sv_ir] " << message << '\n';
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
    const auto symClk = symbols.intern("clk");
    const auto symDout = symbols.intern("dout");
    const auto symAdd0 = symbols.intern("add0");
    const auto symDoutReg = symbols.intern("dout_reg");

    grh_ir::GraphBuilder builder(symbols);
    const auto vA = builder.addValue(symA, 8, false);
    const auto vB = builder.addValue(symB, 8, false);
    const auto vSum = builder.addValue(symSum, 8, false);
    const auto vClk = builder.addValue(symClk, 1, false);
    const auto vDout = builder.addValue(symDout, 8, false);

    builder.bindInputPort(symA, vA);
    builder.bindInputPort(symB, vB);
    builder.bindInputPort(symClk, vClk);
    builder.bindOutputPort(symSum, vSum);
    builder.bindOutputPort(symDout, vDout);

    const auto opAdd = builder.addOp(grh::ir::OperationKind::kAdd, symAdd0);
    builder.addOperand(opAdd, vA);
    builder.addOperand(opAdd, vB);
    builder.addResult(opAdd, vSum);

    const auto opReg = builder.addOp(grh::ir::OperationKind::kRegister, symDoutReg);
    builder.addOperand(opReg, vClk);
    builder.addOperand(opReg, vSum);
    builder.addResult(opReg, vDout);
    builder.setAttr(opReg, "clkPolarity", std::string("posedge"));

    const grh_ir::GraphView view = builder.freeze();

    EmitDiagnostics diagnostics;
    EmitSystemVerilog emitter(&diagnostics);
    EmitOptions options;
    options.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);
    options.outputFilename = std::string("emit_sv_ir.sv");

    EmitResult result = emitter.emitGraphView(view, symbols, "emit_ir_top", options);
    if (!result.success || diagnostics.hasError())
    {
        return fail("emitGraphView reported failure");
    }
    if (result.artifacts.empty())
    {
        return fail("emitGraphView produced no artifact");
    }

    const std::filesystem::path svPath = result.artifacts.front();
    const std::string sv = readFile(svPath);
    if (sv.find("module emit_ir_top") == std::string::npos)
    {
        return fail("Missing module declaration");
    }
    if (sv.find("input wire [7:0] a") == std::string::npos ||
        sv.find("input wire [7:0] b") == std::string::npos ||
        sv.find("input wire clk") == std::string::npos)
    {
        return fail("Missing input port declarations");
    }
    if (sv.find("output reg [7:0] dout") == std::string::npos)
    {
        return fail("Output reg port not emitted");
    }
    if (sv.find("assign sum = a + b;") == std::string::npos)
    {
        return fail("Missing combinational assign");
    }
    if (sv.find("always @(posedge clk)") == std::string::npos ||
        sv.find("dout <= sum;") == std::string::npos)
    {
        return fail("Missing sequential logic");
    }

    return 0;
}
