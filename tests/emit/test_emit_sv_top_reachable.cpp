#include "emit/system_verilog.hpp"
#include "core/grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace wolvrix::lib::emit;
using namespace wolvrix::lib::grh;

namespace
{

int fail(const std::string &message)
{
    std::cerr << "[emit_sv_top_reachable] " << message << '\n';
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

std::size_t countSubstring(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos)
    {
        ++count;
        pos += needle.size();
    }
    return count;
}

void addNoPortInstance(Graph &graph, std::string_view instanceSymbol, std::string_view targetModule)
{
    const auto op = graph.createOperation(OperationKind::kInstance, graph.internSymbol(std::string(instanceSymbol)));
    graph.setAttr(op, "moduleName", std::string(targetModule));
    graph.setAttr(op, "inputPortName", std::vector<std::string>{});
    graph.setAttr(op, "outputPortName", std::vector<std::string>{});
}

Design buildDesign()
{
    Design design;
    Graph &leaf = design.createGraph("leaf");
    Graph &mid = design.createGraph("mid");
    Graph &topA = design.createGraph("top_a");
    Graph &orphan = design.createGraph("orphan");

    (void)leaf;
    (void)orphan;

    addNoPortInstance(mid, "u_leaf", "leaf");
    addNoPortInstance(topA, "u_mid", "mid");

    design.markAsTop("top_a");
    design.markAsTop("orphan");
    return design;
}

bool verifySingleModuleFile(const std::filesystem::path &path, std::string_view moduleName, std::string &error)
{
    const std::string text = readFile(path);
    if (text.empty())
    {
        error = "failed to read split module file: " + path.string();
        return false;
    }
    if (text.find("module " + std::string(moduleName)) == std::string::npos)
    {
        error = "split module file missing expected module declaration: " + path.string();
        return false;
    }
    if (countSubstring(text, "module ") != 1)
    {
        error = "split module file should contain exactly one module declaration: " + path.string();
        return false;
    }
    if (countSubstring(text, "endmodule") != 1)
    {
        error = "split module file should contain exactly one endmodule: " + path.string();
        return false;
    }
    return true;
}

} // namespace

#ifndef WOLF_SV_EMIT_ARTIFACT_DIR
#error "WOLF_SV_EMIT_ARTIFACT_DIR must be defined"
#endif

int main()
{
    const Design design = buildDesign();

    const std::filesystem::path artifactRoot = std::filesystem::path(WOLF_SV_EMIT_ARTIFACT_DIR);
    const std::filesystem::path singlePath = artifactRoot / "emit_top_reachable.sv";
    const std::filesystem::path orphanPath = artifactRoot / "emit_top_orphan_only.sv";
    const std::filesystem::path splitDir = artifactRoot / "emit_top_split_modules";

    std::error_code ec;
    std::filesystem::remove(singlePath, ec);
    ec.clear();
    std::filesystem::remove(orphanPath, ec);
    ec.clear();
    std::filesystem::remove_all(splitDir, ec);

    EmitDiagnostics diagSingle;
    EmitSystemVerilog emitterSingle(&diagSingle);
    EmitOptions singleOptions;
    singleOptions.outputDir = artifactRoot.string();
    singleOptions.outputFilename = std::string("emit_top_reachable.sv");
    singleOptions.topOverrides = {"top_a"};

    const EmitResult singleResult = emitterSingle.emit(design, singleOptions);
    if (!singleResult.success)
    {
        return fail("single-file emit with top override failed");
    }
    if (diagSingle.hasError())
    {
        return fail("single-file emit with top override reported diagnostics errors");
    }
    if (singleResult.artifacts.size() != 1)
    {
        return fail("single-file emit should report exactly one artifact");
    }

    const std::string singleOutput = readFile(singlePath);
    if (singleOutput.empty())
    {
        return fail("failed to read single-file reachable output");
    }
    if (singleOutput.find("module top_a") == std::string::npos ||
        singleOutput.find("module mid") == std::string::npos ||
        singleOutput.find("module leaf") == std::string::npos)
    {
        return fail("reachable single-file emit is missing expected modules");
    }
    if (singleOutput.find("module orphan") != std::string::npos)
    {
        return fail("reachable single-file emit should not include orphan");
    }

    EmitDiagnostics diagOrphan;
    EmitSystemVerilog emitterOrphan(&diagOrphan);
    EmitOptions orphanOptions;
    orphanOptions.outputDir = artifactRoot.string();
    orphanOptions.outputFilename = std::string("emit_top_orphan_only.sv");
    orphanOptions.topOverrides = {"orphan"};

    const EmitResult orphanResult = emitterOrphan.emit(design, orphanOptions);
    if (!orphanResult.success)
    {
        return fail("single-file orphan-only emit failed");
    }
    if (diagOrphan.hasError())
    {
        return fail("single-file orphan-only emit reported diagnostics errors");
    }

    const std::string orphanOutput = readFile(orphanPath);
    if (orphanOutput.empty())
    {
        return fail("failed to read orphan-only output");
    }
    if (orphanOutput.find("module orphan") == std::string::npos)
    {
        return fail("orphan-only emit is missing orphan module");
    }
    if (orphanOutput.find("module top_a") != std::string::npos ||
        orphanOutput.find("module mid") != std::string::npos ||
        orphanOutput.find("module leaf") != std::string::npos)
    {
        return fail("orphan-only emit should not include other modules");
    }

    EmitDiagnostics diagSplit;
    EmitSystemVerilog emitterSplit(&diagSplit);
    EmitOptions splitOptions;
    splitOptions.outputDir = splitDir.string();
    splitOptions.topOverrides = {"top_a"};
    splitOptions.splitModules = true;

    const EmitResult splitResult = emitterSplit.emit(design, splitOptions);
    if (!splitResult.success)
    {
        return fail("split-modules emit failed");
    }
    if (diagSplit.hasError())
    {
        return fail("split-modules emit reported diagnostics errors");
    }
    if (splitResult.artifacts.size() != 3)
    {
        return fail("split-modules emit should report exactly three artifacts");
    }

    std::set<std::string> artifactNames;
    for (const auto &artifact : splitResult.artifacts)
    {
        const std::filesystem::path path(artifact);
        if (!std::filesystem::exists(path))
        {
            return fail("reported split artifact does not exist");
        }
        artifactNames.insert(path.filename().string());
    }

    const std::set<std::string> expectedArtifacts = {"leaf.sv", "mid.sv", "top_a.sv"};
    if (artifactNames != expectedArtifacts)
    {
        return fail("split-modules artifact names do not match emitted module names");
    }
    if (std::filesystem::exists(splitDir / "orphan.sv"))
    {
        return fail("split-modules emit should not create an orphan module file");
    }

    std::string verifyError;
    if (!verifySingleModuleFile(splitDir / "top_a.sv", "top_a", verifyError))
    {
        return fail(verifyError);
    }
    if (!verifySingleModuleFile(splitDir / "mid.sv", "mid", verifyError))
    {
        return fail(verifyError);
    }
    if (!verifySingleModuleFile(splitDir / "leaf.sv", "leaf", verifyError))
    {
        return fail(verifyError);
    }

    return 0;
}
