#include "grh.hpp"
#include "transform.hpp"
#include "transform/repcut.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[repcut-tests] " << message << '\n';
        return 1;
    }

    wolvrix::lib::grh::ValueId makeValue(wolvrix::lib::grh::Graph &graph,
                                         const std::string &name,
                                         int32_t width = 8,
                                         bool isSigned = false)
    {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
    }

    wolvrix::lib::grh::OperationId makeBinaryOp(wolvrix::lib::grh::Graph &graph,
                                                 wolvrix::lib::grh::OperationKind kind,
                                                 const std::string &name,
                                                 wolvrix::lib::grh::ValueId lhs,
                                                 wolvrix::lib::grh::ValueId rhs,
                                                 wolvrix::lib::grh::ValueId out)
    {
        const auto op = graph.createOperation(kind, graph.internSymbol(name));
        graph.addOperand(op, lhs);
        graph.addOperand(op, rhs);
        graph.addResult(op, out);
        return op;
    }

    const std::string *findRepcutStatsDiagnostic(const PassDiagnostics &diags)
    {
        for (const auto &diag : diags.messages())
        {
            if (diag.passName == "repcut" &&
                diag.kind == PassDiagnosticKind::Info &&
                diag.message.find("\"pass\":\"repcut\"") != std::string::npos)
            {
                return &diag.message;
            }
        }
        return nullptr;
    }

    std::optional<std::string> getAttrString(const wolvrix::lib::grh::Operation &op,
                                             std::string_view key)
    {
        auto attr = op.attr(key);
        if (!attr)
        {
            return std::nullopt;
        }
        if (const auto *value = std::get_if<std::string>(&*attr))
        {
            return *value;
        }
        return std::nullopt;
    }

    wolvrix::lib::grh::OperationId findInstanceByName(const wolvrix::lib::grh::Graph &graph,
                                                      std::string_view instanceName)
    {
        for (const auto opId : graph.operations())
        {
            if (!opId.valid())
            {
                continue;
            }
            const auto op = graph.getOperation(opId);
            if (op.kind() != wolvrix::lib::grh::OperationKind::kInstance)
            {
                continue;
            }
            const auto name = getAttrString(op, "instanceName");
            if (name && *name == instanceName)
            {
                return opId;
            }
        }
        return wolvrix::lib::grh::OperationId::invalid();
    }

    bool hasGraphWithPrefix(wolvrix::lib::grh::Design &design, std::string_view prefix)
    {
        if (design.findGraph(std::string(prefix)) != nullptr)
        {
            return true;
        }
        for (int suffix = 1; suffix <= 8; ++suffix)
        {
            if (design.findGraph(std::string(prefix) + "_" + std::to_string(suffix)) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

} // namespace

int main()
{
#if !WOLVRIX_HAVE_MT_KAHYPAR
    std::cout << "[repcut-tests] mt-kahypar backend unavailable, skip test.\n";
    return 0;
#else
    wolvrix::lib::grh::Design design;
    wolvrix::lib::grh::Graph &graph = design.createGraph("top");

    const auto inA = makeValue(graph, "a");
    const auto inB = makeValue(graph, "b");
    const auto en = makeValue(graph, "en", 1, false);
    graph.bindInputPort("a", inA);
    graph.bindInputPort("b", inB);
    graph.bindInputPort("en", en);

    const auto shared = makeValue(graph, "shared");
    makeBinaryOp(graph, wolvrix::lib::grh::OperationKind::kAdd, "shared_add", inA, inB, shared);

    constexpr int kSinkCount = 6;
    for (int i = 0; i < kSinkCount; ++i)
    {
        const std::string suffix = std::to_string(i);
        const auto datav = makeValue(graph, "wr_data_" + suffix);
        const auto kind = (i % 2 == 0) ? wolvrix::lib::grh::OperationKind::kAdd
                                       : wolvrix::lib::grh::OperationKind::kSub;
        const std::string dataOpName = (i % 2 == 0 ? "sink_add_" : "sink_sub_") + suffix;
        makeBinaryOp(graph, kind, dataOpName, shared, (i % 2 == 0) ? inA : inB, datav);

        const std::string latchName = "lat_" + suffix;
        const auto latchDecl = graph.createOperation(wolvrix::lib::grh::OperationKind::kLatch,
                                                     graph.internSymbol(latchName));
        graph.setAttr(latchDecl, "width", static_cast<int64_t>(8));
        graph.setAttr(latchDecl, "isSigned", false);

        const auto writeOp = graph.createOperation(wolvrix::lib::grh::OperationKind::kLatchWritePort,
                                                   graph.internSymbol(latchName + "_wr"));
        graph.addOperand(writeOp, en);
        graph.addOperand(writeOp, datav);
        graph.setAttr(writeOp, "latchSymbol", latchName);
    }
    design.markAsTop("top");

    const std::filesystem::path outDir = std::filesystem::path(WOLF_SV_TEST_ARTIFACT_DIR) / "repcut_test";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec)
    {
        return fail("failed to create output directory: " + outDir.string());
    }

    PassManager manager;
    manager.options().verbosity = PassVerbosity::Info;
    RepcutOptions options;
    options.path = "top";
    options.partitionCount = 2;
    options.imbalanceFactor = 1.0;
    options.workDir = outDir.string();
    options.partitioner = "mt-kahypar";
    options.mtKaHyParPreset = "quality";
    options.mtKaHyParThreads = 0;
    options.keepIntermediateFiles = true;
    manager.addPass(std::make_unique<RepcutPass>(options));

    PassDiagnostics diags;
    PassManagerResult result{};
    try
    {
        result = manager.run(design, diags);
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("exception during repcut run: ") + ex.what());
    }

    std::vector<std::filesystem::path> nativePartFiles;
    {
        std::error_code ec2;
        for (const auto &entry : std::filesystem::directory_iterator(outDir, ec2))
        {
            if (ec2 || !entry.is_regular_file())
            {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.rfind("top_repcut_k2.hgr.part", 0) == 0)
            {
                nativePartFiles.push_back(entry.path());
            }
        }
    }
    if (nativePartFiles.empty())
    {
        return fail("expected native mt-kahypar partition output file");
    }

    if (!result.success || diags.hasError())
    {
        bool hasExpectedDiagnostic = false;
        for (const auto &diag : diags.messages())
        {
            if (diag.message.find("repcut phase-e: detected forbidden cross-partition values") != std::string::npos ||
                diag.message.find("repcut phase-e: detected memory-read cross-partition usage") != std::string::npos)
            {
                hasExpectedDiagnostic = true;
                break;
            }
        }
        if (!hasExpectedDiagnostic)
        {
            return fail("repcut failed without explicit phase-e cross-partition diagnostic");
        }
        return 0;
    }

    if (!result.changed)
    {
        return fail("expected repcut pass to report graph changes");
    }

    wolvrix::lib::grh::Graph *newTop = design.findGraph("top");
    if (!newTop)
    {
        return fail("top graph missing after repcut");
    }

    std::size_t instanceCount = 0;
    for (const auto opId : newTop->operations())
    {
        if (newTop->getOperation(opId).kind() == wolvrix::lib::grh::OperationKind::kInstance)
        {
            ++instanceCount;
        }
    }
    if (instanceCount < 1)
    {
        return fail("expected at least one partition instance in reconstructed top");
    }

    if (!hasGraphWithPrefix(design, "top_repcut_part0"))
    {
        return fail("expected partition graph with top_repcut_part0 prefix");
    }

    // Regression: read-only register (initValue-only, no write port) must still
    // get a stable partition owner so reconstructed inputs can be fully mapped.
    wolvrix::lib::grh::Design readOnlyDesign;
    wolvrix::lib::grh::Graph &readOnlyGraph = readOnlyDesign.createGraph("top_read_only_reg");
    readOnlyDesign.markAsTop("top_read_only_reg");

    const auto roIn = makeValue(readOnlyGraph, "in_data", 32, false);
    const auto roEn = makeValue(readOnlyGraph, "en", 1, false);
    readOnlyGraph.bindInputPort("in_data", roIn);
    readOnlyGraph.bindInputPort("en", roEn);

    const auto roRegDecl =
        readOnlyGraph.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                      readOnlyGraph.internSymbol("random_bits"));
    readOnlyGraph.setAttr(roRegDecl, "width", static_cast<int64_t>(32));
    readOnlyGraph.setAttr(roRegDecl, "isSigned", false);
    readOnlyGraph.setAttr(roRegDecl, "initValue", std::string("$random"));

    const auto roRead =
        readOnlyGraph.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                      readOnlyGraph.internSymbol("random_bits_rd"));
    const auto roReadValue = makeValue(readOnlyGraph, "random_bits_rd_v", 32, false);
    readOnlyGraph.addResult(roRead, roReadValue);
    readOnlyGraph.setAttr(roRead, "regSymbol", std::string("random_bits"));

    const auto roData = makeValue(readOnlyGraph, "lat_data", 32, false);
    makeBinaryOp(readOnlyGraph, wolvrix::lib::grh::OperationKind::kAdd,
                 "lat_data_add", roReadValue, roIn, roData);

    const auto roLatchDecl =
        readOnlyGraph.createOperation(wolvrix::lib::grh::OperationKind::kLatch,
                                      readOnlyGraph.internSymbol("ro_lat"));
    readOnlyGraph.setAttr(roLatchDecl, "width", static_cast<int64_t>(32));
    readOnlyGraph.setAttr(roLatchDecl, "isSigned", false);

    const auto roLatchWrite =
        readOnlyGraph.createOperation(wolvrix::lib::grh::OperationKind::kLatchWritePort,
                                      readOnlyGraph.internSymbol("ro_lat_wr"));
    readOnlyGraph.addOperand(roLatchWrite, roEn);
    readOnlyGraph.addOperand(roLatchWrite, roData);
    readOnlyGraph.setAttr(roLatchWrite, "latchSymbol", std::string("ro_lat"));

    const std::filesystem::path roOutDir =
        std::filesystem::path(WOLF_SV_TEST_ARTIFACT_DIR) / "repcut_test_read_only_reg";
    std::filesystem::create_directories(roOutDir, ec);
    if (ec)
    {
        return fail("failed to create output directory: " + roOutDir.string());
    }

    PassManager roManager;
    roManager.options().verbosity = PassVerbosity::Info;
    RepcutOptions roOptions;
    roOptions.path = "top_read_only_reg";
    roOptions.partitionCount = 2;
    roOptions.imbalanceFactor = 1.0;
    roOptions.workDir = roOutDir.string();
    roOptions.partitioner = "mt-kahypar";
    roOptions.mtKaHyParPreset = "quality";
    roOptions.mtKaHyParThreads = 0;
    roOptions.keepIntermediateFiles = true;
    roManager.addPass(std::make_unique<RepcutPass>(roOptions));

    PassDiagnostics roDiags;
    PassManagerResult roResult{};
    try
    {
        roResult = roManager.run(readOnlyDesign, roDiags);
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("read-only register repcut exception: ") + ex.what());
    }

    if (!roResult.success || roDiags.hasError())
    {
        return fail("read-only register repcut failed unexpectedly");
    }
    if (!roResult.changed)
    {
        return fail("read-only register repcut expected graph changes");
    }
    if (readOnlyDesign.findGraph("top_read_only_reg") == nullptr)
    {
        return fail("read-only register top graph missing after repcut");
    }

    const std::string *roStats = findRepcutStatsDiagnostic(roDiags);
    if (roStats == nullptr)
    {
        return fail("read-only register repcut missing stats artifact");
    }
    if (roStats->find("\"weight_partition_stats\":{") == std::string::npos)
    {
        return fail("read-only register repcut missing weight_partition_stats");
    }
    if (roStats->find("\"original_total_weight\":") == std::string::npos)
    {
        return fail("read-only register repcut missing original_total_weight");
    }
    if (roStats->find("\"max_partition_weight\":") == std::string::npos)
    {
        return fail("read-only register repcut missing max_partition_weight");
    }
    if (roStats->find("\"avg_partition_weight\":") == std::string::npos)
    {
        return fail("read-only register repcut missing avg_partition_weight");
    }
    if (roStats->find("\"original_over_max_weight_ratio\":") == std::string::npos)
    {
        return fail("read-only register repcut missing original_over_max_weight_ratio");
    }
    if (roStats->find("\"original_over_avg_weight_ratio\":") == std::string::npos)
    {
        return fail("read-only register repcut missing original_over_avg_weight_ratio");
    }
    if (roStats->find("\"max_partition_weight_fraction_of_original\":") == std::string::npos)
    {
        return fail("read-only register repcut missing max_partition_weight_fraction_of_original");
    }
    if (roStats->find("\"avg_partition_weight_fraction_of_original\":") == std::string::npos)
    {
        return fail("read-only register repcut missing avg_partition_weight_fraction_of_original");
    }
    if (roStats->find("\"weight\":") == std::string::npos)
    {
        return fail("read-only register repcut missing per-partition weight entries");
    }

    // Path-mode regression: resolving "top.u_child" should partition the child module graph
    // rather than the top wrapper graph.
    wolvrix::lib::grh::Design pathDesign;
    wolvrix::lib::grh::Graph &pathChild = pathDesign.createGraph("child");
    wolvrix::lib::grh::Graph &pathTop = pathDesign.createGraph("top");
    pathDesign.markAsTop("top");

    const auto childIn = makeValue(pathChild, "in_data", 32, false);
    const auto childEn = makeValue(pathChild, "en", 1, false);
    pathChild.bindInputPort("in_data", childIn);
    pathChild.bindInputPort("en", childEn);

    const auto childRegDecl =
        pathChild.createOperation(wolvrix::lib::grh::OperationKind::kRegister,
                                  pathChild.internSymbol("random_bits"));
    pathChild.setAttr(childRegDecl, "width", static_cast<int64_t>(32));
    pathChild.setAttr(childRegDecl, "isSigned", false);
    pathChild.setAttr(childRegDecl, "initValue", std::string("$random"));

    const auto childRead =
        pathChild.createOperation(wolvrix::lib::grh::OperationKind::kRegisterReadPort,
                                  pathChild.internSymbol("random_bits_rd"));
    const auto childReadValue = makeValue(pathChild, "random_bits_rd_v", 32, false);
    pathChild.addResult(childRead, childReadValue);
    pathChild.setAttr(childRead, "regSymbol", std::string("random_bits"));

    const auto childData = makeValue(pathChild, "lat_data", 32, false);
    makeBinaryOp(pathChild, wolvrix::lib::grh::OperationKind::kAdd,
                 "lat_data_add", childReadValue, childIn, childData);

    const auto childLatchDecl =
        pathChild.createOperation(wolvrix::lib::grh::OperationKind::kLatch,
                                  pathChild.internSymbol("ro_lat"));
    pathChild.setAttr(childLatchDecl, "width", static_cast<int64_t>(32));
    pathChild.setAttr(childLatchDecl, "isSigned", false);

    const auto childLatchWrite =
        pathChild.createOperation(wolvrix::lib::grh::OperationKind::kLatchWritePort,
                                  pathChild.internSymbol("ro_lat_wr"));
    pathChild.addOperand(childLatchWrite, childEn);
    pathChild.addOperand(childLatchWrite, childData);
    pathChild.setAttr(childLatchWrite, "latchSymbol", std::string("ro_lat"));

    const auto topIn = makeValue(pathTop, "in_data", 32, false);
    const auto topEn = makeValue(pathTop, "en", 1, false);
    pathTop.bindInputPort("in_data", topIn);
    pathTop.bindInputPort("en", topEn);

    const auto childInst =
        pathTop.createOperation(wolvrix::lib::grh::OperationKind::kInstance, pathTop.makeInternalOpSym());
    pathTop.addOperand(childInst, topIn);
    pathTop.addOperand(childInst, topEn);
    pathTop.setAttr(childInst, "moduleName", std::string("child"));
    pathTop.setAttr(childInst, "instanceName", std::string("u_child"));
    pathTop.setAttr(childInst, "inputPortName", std::vector<std::string>{"in_data", "en"});
    pathTop.setAttr(childInst, "outputPortName", std::vector<std::string>{});

    const std::filesystem::path pathOutDir =
        std::filesystem::path(WOLF_SV_TEST_ARTIFACT_DIR) / "repcut_test_path_mode";
    std::filesystem::create_directories(pathOutDir, ec);
    if (ec)
    {
        return fail("failed to create output directory: " + pathOutDir.string());
    }

    PassManager pathManager;
    pathManager.options().verbosity = PassVerbosity::Info;
    RepcutOptions pathOptions;
    pathOptions.path = "top.u_child";
    pathOptions.partitionCount = 2;
    pathOptions.imbalanceFactor = 1.0;
    pathOptions.workDir = pathOutDir.string();
    pathOptions.partitioner = "mt-kahypar";
    pathOptions.mtKaHyParPreset = "quality";
    pathOptions.mtKaHyParThreads = 0;
    pathOptions.keepIntermediateFiles = true;
    pathManager.addPass(std::make_unique<RepcutPass>(pathOptions));

    PassDiagnostics pathDiags;
    PassManagerResult pathResult{};
    try
    {
        pathResult = pathManager.run(pathDesign, pathDiags);
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("path-mode repcut exception: ") + ex.what());
    }

    if (!pathResult.success || pathDiags.hasError())
    {
        return fail("path-mode repcut failed unexpectedly");
    }
    if (!pathResult.changed)
    {
        return fail("path-mode repcut expected graph changes");
    }
    if (pathDesign.findGraph("top") == nullptr)
    {
        return fail("path-mode top graph missing after repcut");
    }
    wolvrix::lib::grh::Graph *rebuiltChild = pathDesign.findGraph("child");
    if (rebuiltChild == nullptr)
    {
        return fail("path-mode child graph missing after repcut");
    }
    if (!findInstanceByName(pathTop, "u_child").valid())
    {
        return fail("path-mode top instance should remain after child repcut");
    }
    std::size_t childInstanceCount = 0;
    for (const auto opId : rebuiltChild->operations())
    {
        if (rebuiltChild->getOperation(opId).kind() == wolvrix::lib::grh::OperationKind::kInstance)
        {
            ++childInstanceCount;
        }
    }
    if (childInstanceCount < 1)
    {
        return fail("path-mode rebuilt child should contain partition instances");
    }
    if (!hasGraphWithPrefix(pathDesign, "child_repcut_part0"))
    {
        return fail("expected child_repcut_part0 graph after path-mode repcut");
    }

    return 0;
#endif
}
