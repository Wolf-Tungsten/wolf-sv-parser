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
        PassVerbosity verbosity = PassVerbosity::Error;
    };

    class RecordingPass : public Pass
    {
    public:
        RecordingPass(std::string id, PassRecord &record, std::vector<std::string> &order)
            : Pass(std::move(id), "recording"), record_(record), order_(order)
        {
        }

        PassResult run() override
        {
            record_.ran = true;
            record_.verbosity = verbosity();
            order_.push_back(id());
            if (emitDiagError)
            {
                diags().error(id(), "diagnostic failure");
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

    class ScratchpadCheckEmpty : public Pass
    {
    public:
        ScratchpadCheckEmpty(std::string id, bool &reuseFlag)
            : Pass(std::move(id), "scratchpad-check"), reuseFlag_(reuseFlag)
        {
        }

        PassResult run() override
        {
            if (hasScratchpad("count"))
            {
                reuseFlag_ = true;
                diags().error(id(), "scratchpad was not cleared between runs");
                return PassResult{false, true, {}};
            }
            return {};
        }

    private:
        bool &reuseFlag_;
    };

    class ScratchpadWriter : public Pass
    {
    public:
        ScratchpadWriter(std::string id, int value)
            : Pass(std::move(id), "scratchpad-writer"), value_(value)
        {
        }

        PassResult run() override
        {
            setScratchpad("count", value_);
            return {};
        }

    private:
        int value_;
    };

    class ScratchpadReader : public Pass
    {
    public:
        ScratchpadReader(std::string id, int expected)
            : Pass(std::move(id), "scratchpad-reader"), expected_(expected)
        {
        }

        PassResult run() override
        {
            const int *value = getScratchpad<int>("count");
            if (value == nullptr || *value != expected_)
            {
                diags().error(id(), "scratchpad value missing or mismatched");
                return PassResult{false, true, {}};
            }
            return {};
        }

    private:
        int expected_;
    };

    class VerbosityEmitter : public Pass
    {
    public:
        VerbosityEmitter() : Pass("verbosity-emitter", "verbosity-emitter") {}

        PassResult run() override
        {
            debug("debug message");
            info("info message");
            warning("warn message");
            return {};
        }
    };

} // namespace

int main()
{
    grh::Netlist netlist;
    netlist.createGraph("top");

    // Case 1: pipeline order and aggregated changed flag
    {
        PassManager manager;
        manager.options().verbosity = PassVerbosity::Debug;
        PassDiagnostics diags;
        std::vector<std::string> order;

        PassRecord firstRecord;
        PassRecord secondRecord;

        auto firstPass = std::make_unique<RecordingPass>("first", firstRecord, order);
        firstPass->changedOnRun = true;
        manager.addPass(std::move(firstPass));

        auto secondPass = std::make_unique<RecordingPass>("second", secondRecord, order);
        manager.addPass(std::move(secondPass));

        PassManagerResult result = manager.run(netlist, diags);
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
        if (firstRecord.verbosity != PassVerbosity::Debug || secondRecord.verbosity != PassVerbosity::Debug)
        {
            return fail("Expected verbosity level to propagate through context");
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
        PassManagerResult result = manager.run(netlist, diags);
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
        PassManagerResult result = manager.run(netlist, diags);
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

    // Case 4: scratchpad allows cross-pass data and resets per run
    {
        PassManager manager;
        bool scratchpadReused = false;

        manager.addPass(std::make_unique<ScratchpadCheckEmpty>("check", scratchpadReused));
        manager.addPass(std::make_unique<ScratchpadWriter>("write", 7));
        manager.addPass(std::make_unique<ScratchpadReader>("read", 7));

        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlist, diags);
        if (!result.success || diags.hasError())
        {
            return fail("Expected scratchpad pipeline to succeed on first run");
        }
        diags.clear();
        result = manager.run(netlist, diags);
        if (!result.success || diags.hasError())
        {
            return fail("Expected scratchpad pipeline to succeed on second run");
        }
        if (scratchpadReused)
        {
            return fail("Scratchpad should be cleared between PassManager runs");
        }
    }

    // Case 5: verbosity filters diagnostics below threshold
    {
        PassManager manager;
        manager.options().verbosity = PassVerbosity::Warning;

        PassDiagnostics diags;
        manager.addPass(std::make_unique<VerbosityEmitter>());
        PassManagerResult result = manager.run(netlist, diags);
        if (!result.success)
        {
            return fail("Verbosity filtering should not fail the pipeline without errors");
        }
        std::size_t debugCount = 0;
        std::size_t infoCount = 0;
        std::size_t warnCount = 0;
        for (const auto &msg : diags.messages())
        {
            if (msg.kind == PassDiagnosticKind::Debug)
            {
                ++debugCount;
            }
            else if (msg.kind == PassDiagnosticKind::Info)
            {
                ++infoCount;
            }
            else if (msg.kind == PassDiagnosticKind::Warning)
            {
                ++warnCount;
            }
        }
        if (warnCount != 1)
        {
            return fail("Warning diagnostics should survive filtering");
        }
        if (infoCount != 0 || debugCount != 0)
        {
            return fail("Diagnostics below verbosity threshold should be filtered out");
        }
    }

    // Case 6: built-in stats pass reports counts
    {
        grh::Netlist netlistStats;
        grh::Graph &graph = netlistStats.createGraph("g");
        graph.createValue(graph.internSymbol("v0"), 1, false);
        graph.createValue(graph.internSymbol("v1"), 1, false);
        graph.createOperation(grh::OperationKind::kAssign, graph.internSymbol("op0"));

        PassManager manager;
        manager.addPass(std::make_unique<StatsPass>());

        PassDiagnostics diags;
        PassManagerResult result = manager.run(netlistStats, diags);
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
        if (message.passName != "stats" || message.kind != PassDiagnosticKind::Warning)
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
