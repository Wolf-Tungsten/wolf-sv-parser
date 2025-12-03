#include "grh.hpp"
#include "transform.hpp"
#include "pass/demo_stats.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace wolf_sv;
using namespace wolf_sv::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[transform-tests] " << message << '\n';
        return 1;
    }

    struct PassRecord
    {
        bool ran = false;
        bool verbose = false;
        bool flagArg = false;
        int64_t countArg = 0;
        std::string nameArg;
        int configureCount = 0;
    };

    class RecordingPass : public Pass
    {
    public:
        RecordingPass(std::string id, PassRecord &record, std::vector<std::string> &order)
            : Pass(std::move(id), "recording"), record_(record), order_(order)
        {
        }

        void configure(const PassConfig &config) override
        {
            ++record_.configureCount;
            record_.flagArg = config.getBool("flag");
            record_.countArg = config.getInt("count", record_.countArg);
            record_.nameArg = config.getString("name", record_.nameArg);
        }

        PassResult run(PassContext &context) override
        {
            record_.ran = true;
            record_.verbose = context.verbose;
            order_.push_back(id());
            if (emitDiagError)
            {
                context.diags.error(id(), "diagnostic failure");
            }
            return PassResult{changedOnRun, failOnRun, {}};
        }

        bool changedOnRun = false;
        bool failOnRun = false;
        bool emitDiagError = false;

    private:
        PassRecord &record_;
        std::vector<std::string> &order_;
    };

} // namespace

int main()
{
    grh::Netlist netlist;
    netlist.createGraph("top");

    // Case 1: pipeline order and config helpers with explicit pass instances
    {
        PassManager manager;
        manager.options().verbose = true;
        PassDiagnostics diags;
        std::vector<std::string> order;

        PassRecord firstRecord;
        PassRecord secondRecord;

        PassConfig firstConfig;
        firstConfig.args.emplace("flag", "true");
        firstConfig.args.emplace("name", "alpha");

        PassConfig secondConfig;
        secondConfig.args.emplace("count", "7");
        secondConfig.args.emplace("name", "beta");

        auto firstPass = std::make_unique<RecordingPass>("first", firstRecord, order);
        firstPass->changedOnRun = true;
        manager.addPass(std::move(firstPass), std::move(firstConfig));

        auto secondPass = std::make_unique<RecordingPass>("second", secondRecord, order);
        manager.addPass(std::move(secondPass), std::move(secondConfig));

        TransformResult result = manager.run(netlist, diags);
        if (!result.success)
        {
            return fail("Expected transform pipeline to succeed");
        }
        if (!result.changed)
        {
            return fail("Expected pipeline to report aggregated changes");
        }
        if (order != std::vector<std::string>{"first", "second"})
        {
            return fail("Unexpected pass execution order");
        }
        if (!firstRecord.ran || !secondRecord.ran)
        {
            return fail("Expected both passes to run");
        }
        if (!firstRecord.verbose || !secondRecord.verbose)
        {
            return fail("Expected verbose flag to propagate through context");
        }
        if (!firstRecord.flagArg || firstRecord.nameArg != "alpha")
        {
            return fail("PassConfig helpers did not parse first pass arguments");
        }
        if (secondRecord.countArg != 7 || secondRecord.nameArg != "beta")
        {
            return fail("PassConfig helpers did not parse second pass arguments");
        }
        if (firstRecord.configureCount == 0 || secondRecord.configureCount == 0)
        {
            return fail("configure should be invoked for each pass");
        }
        if (!diags.empty())
        {
            return fail("Did not expect diagnostics for successful pipeline");
        }
    }

    // Case 2: failure short-circuits subsequent passes when stopOnError is true
    {
        PassManager manager;
        std::vector<std::string> order;
        PassRecord failingRecord;
        PassRecord tailRecord;

        auto failing = std::make_unique<RecordingPass>("fail", failingRecord, order);
        failing->failOnRun = true;
        manager.addPass(std::move(failing));

        auto tail = std::make_unique<RecordingPass>("tail", tailRecord, order);
        manager.addPass(std::move(tail));

        PassDiagnostics diags;
        TransformResult result = manager.run(netlist, diags);
        if (result.success)
        {
            return fail("Expected transform pipeline to fail when a pass reports failure");
        }
        if (order != std::vector<std::string>{"fail"})
        {
            return fail("stopOnError should prevent downstream passes after failure");
        }
        if (tailRecord.ran)
        {
            return fail("Trailing pass should not have executed after failure");
        }
    }

    // Case 3: diagnostics errors respect stopOnError option
    {
        PassManager manager;
        manager.options().stopOnError = false;
        std::vector<std::string> order;
        PassRecord diagRecord;
        PassRecord tailRecord;

        auto diagPass = std::make_unique<RecordingPass>("diag", diagRecord, order);
        diagPass->emitDiagError = true;
        manager.addPass(std::move(diagPass));

        auto tail = std::make_unique<RecordingPass>("tail", tailRecord, order);
        tail->changedOnRun = true;
        manager.addPass(std::move(tail));

        PassDiagnostics diags;
        TransformResult result = manager.run(netlist, diags);
        if (order != std::vector<std::string>{"diag", "tail"})
        {
            return fail("stopOnError disabled should allow pipeline to continue after diagnostics error");
        }
        if (!diags.hasError())
        {
            return fail("Diagnostics should record errors emitted by passes");
        }
        if (result.success)
        {
            return fail("Pipeline should report failure when diagnostics contain errors");
        }
        if (!result.changed)
        {
            return fail("Changes should still be aggregated even when diagnostics contain errors");
        }
    }

    // Case 4: built-in stats pass reports counts
    {
        grh::Netlist netlistStats;
        grh::Graph &graph = netlistStats.createGraph("g");
        graph.createValue("v0", 1, false);
        graph.createValue("v1", 1, false);
        graph.createOperation(grh::OperationKind::kAssign, "op0");

        PassManager manager;
        manager.addPass(std::make_unique<StatsPass>());

        PassDiagnostics diags;
        TransformResult result = manager.run(netlistStats, diags);
        if (!result.success)
        {
            return fail("Expected stats pass to succeed");
        }
        if (diags.hasError())
        {
            return fail("Stats pass should not record errors");
        }
        if (diags.messages().empty())
        {
            return fail("Stats pass should emit a diagnostic with counts");
        }
        const auto &message = diags.messages().front();
        if (message.passId != "stats" || message.kind != PassDiagnosticKind::Warning)
        {
            return fail("Stats pass should emit a warning diagnostic");
        }
        if (message.message.find("graphs=1") == std::string::npos ||
            message.message.find("operations=1") == std::string::npos ||
            message.message.find("values=2") == std::string::npos)
        {
            return fail("Stats pass diagnostic did not contain expected counts");
        }
    }

    return 0;
}
