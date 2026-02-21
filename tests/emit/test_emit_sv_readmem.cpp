#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace wolvrix::lib::emit;
using namespace wolvrix::lib::grh;

namespace
{

int fail(const std::string &message)
{
    std::cerr << "[emit_sv_readmem] " << message << '\n';
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

Netlist buildNetlist()
{
    Netlist netlist;
    Graph &graph = netlist.createGraph("mem_init");
    netlist.markAsTop(graph.symbol());

    OperationId memOp = graph.createOperation(OperationKind::kMemory, graph.internSymbol("mem"));
    graph.setAttr(memOp, "width", static_cast<int64_t>(8));
    graph.setAttr(memOp, "row", static_cast<int64_t>(16));
    graph.setAttr(memOp, "isSigned", false);
    graph.setAttr(memOp, "initKind", std::vector<std::string>{"readmemh", "readmemb"});
    graph.setAttr(memOp, "initFile", std::vector<std::string>{"mem_init.hex", "mem_init.bin"});
    graph.setAttr(memOp, "initHasStart", std::vector<bool>{false, true});
    graph.setAttr(memOp, "initHasFinish", std::vector<bool>{false, true});
    graph.setAttr(memOp, "initStart", std::vector<int64_t>{0, 2});
    graph.setAttr(memOp, "initFinish", std::vector<int64_t>{0, 7});

    return netlist;
}

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

int main()
{
    Netlist netlist = buildNetlist();

    EmitDiagnostics diag;
    EmitSystemVerilog emitter(&diag);

    EmitOptions options;
    options.outputDir = std::string(WOLF_SV_EMIT_ARTIFACT_DIR);
    options.outputFilename = std::string("emit_readmem.sv");

    EmitResult result = emitter.emit(netlist, options);
    if (!result.success)
    {
        return fail("EmitSystemVerilog failed");
    }
    if (diag.hasError())
    {
        return fail("EmitSystemVerilog reported diagnostics errors");
    }
    if (result.artifacts.empty())
    {
        return fail("EmitSystemVerilog did not report artifacts");
    }

    const std::filesystem::path outputPath = result.artifacts.front();
    const std::string output = readFile(outputPath);
    if (output.empty())
    {
        return fail("Failed to read emitted SystemVerilog file");
    }

    if (output.find("$readmemh(\"mem_init.hex\", mem") == std::string::npos)
    {
        return fail("Missing $readmemh init in emitted SystemVerilog");
    }
    if (output.find("$readmemb(\"mem_init.bin\", mem, 2, 7") == std::string::npos)
    {
        return fail("Missing $readmemb init in emitted SystemVerilog");
    }

    return 0;
}
