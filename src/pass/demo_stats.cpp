#include "pass/demo_stats.hpp"

#include "grh.hpp"

#include <sstream>
#include <string>

namespace wolf_sv_parser::transform
{

    StatsPass::StatsPass() : Pass("stats", "operation-value-stats", "Count graphs, operations, and values for diagnostics") {}

    PassResult StatsPass::run()
    {
        std::size_t graphCount = 0;
        std::size_t opCount = 0;
        std::size_t valueCount = 0;

        for (const auto &entry : netlist().graphs())
        {
            ++graphCount;
            const auto &graph = entry.second;
            opCount += graph->operations().size();
            valueCount += graph->values().size();
        }

        std::ostringstream oss;
        oss << "graphs=" << graphCount << ", operations=" << opCount << ", values=" << valueCount;

        warning(oss.str());

        PassResult result;
        result.changed = false;
        result.failed = false;
        result.artifacts.push_back(oss.str());
        return result;
    }

} // namespace wolf_sv_parser::transform
