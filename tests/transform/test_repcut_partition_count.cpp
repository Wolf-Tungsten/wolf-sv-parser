#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/repcut.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[repcut-partition-count-tests] " << message << '\n';
        return 1;
    }

    void buildSmallRepcutGraph(wolvrix::lib::grh::Design &design, const std::string &graphName)
    {
        wolvrix::lib::grh::Graph &graph = design.createGraph(graphName);
        design.markAsTop(graphName);

        const auto a = graph.createValue(graph.internSymbol("a"), 8, false);
        const auto b = graph.createValue(graph.internSymbol("b"), 8, false);
        const auto en = graph.createValue(graph.internSymbol("en"), 1, false);
        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindInputPort("en", en);

        const auto sum = graph.createValue(graph.internSymbol("sum"), 8, false);
        const auto sumOp =
            graph.createOperation(wolvrix::lib::grh::OperationKind::kAdd, graph.internSymbol("sum_add"));
        graph.addOperand(sumOp, a);
        graph.addOperand(sumOp, b);
        graph.addResult(sumOp, sum);

        const auto latDecl =
            graph.createOperation(wolvrix::lib::grh::OperationKind::kLatch, graph.internSymbol("lat0"));
        graph.setAttr(latDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(latDecl, "isSigned", false);

        const auto latWr =
            graph.createOperation(wolvrix::lib::grh::OperationKind::kLatchWritePort, graph.internSymbol("lat0_wr"));
        graph.addOperand(latWr, en);
        graph.addOperand(latWr, sum);
        graph.setAttr(latWr, "latchSymbol", std::string("lat0"));
    }

    bool containsText(const PassDiagnostics &diags, const std::string &text)
    {
        for (const auto &diag : diags.messages())
        {
            if (diag.message.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

} // namespace

int main()
{
    const std::vector<std::size_t> partitionCounts = {64, 512, 4096};
    for (const std::size_t partitionCount : partitionCounts)
    {
        wolvrix::lib::grh::Design design;
        const std::string graphName = "top_" + std::to_string(partitionCount);
        buildSmallRepcutGraph(design, graphName);

        const std::filesystem::path outDir =
            std::filesystem::path(WOLF_SV_TEST_ARTIFACT_DIR) / ("repcut_part_count_" + std::to_string(partitionCount));
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        if (ec)
        {
            return fail("failed to create output directory: " + outDir.string());
        }

        PassManager manager;
        manager.options().verbosity = PassVerbosity::Info;
        RepcutOptions options;
        options.path = graphName;
        options.partitionCount = partitionCount;
        options.imbalanceFactor = 1.0;
        options.workDir = outDir.string();
        options.partitioner = "unsupported-backend-for-validation";
        manager.addPass(std::make_unique<RepcutPass>(options));

        PassDiagnostics diags;
        const PassManagerResult result = manager.run(design, diags);

        if (result.success || !diags.hasError())
        {
            return fail("repcut should fail with unsupported backend for partition_count=" +
                        std::to_string(partitionCount));
        }
        if (!containsText(diags, "unsupported partitioner"))
        {
            return fail("missing unsupported partitioner diagnostic for partition_count=" +
                        std::to_string(partitionCount));
        }
        if (containsText(diags, "partition_count must be <="))
        {
            return fail("partition_count validation rejected supported upper-bound value " +
                        std::to_string(partitionCount));
        }
    }

    return 0;
}
