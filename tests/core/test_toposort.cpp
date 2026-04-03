#include "core/toposort.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

using namespace wolvrix::lib::toposort;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[toposort_tests] " << message << '\n';
        return 1;
    }

    template <typename Func>
    bool expectThrows(Func &&func)
    {
        try
        {
            func();
        }
        catch (const std::exception &)
        {
            return true;
        }
        return false;
    }

    bool sameNodes(std::vector<std::string> actual, std::vector<std::string> expected)
    {
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        return actual == expected;
    }

} // namespace

int main()
{
    try
    {
        {
            TopoDag<std::string> dag;
            dag.addNode("a");
            dag.addNode("a");
            dag.addEdge("a", "b");
            dag.addEdge("a", "c");

            if (dag.nodeCount() != 3)
            {
                return fail("Expected duplicate nodes to be ignored");
            }

            const auto layers = dag.toposort();
            if (layers.size() != 2)
            {
                return fail("Expected two layers for a simple fanout DAG");
            }
            if (layers[0] != std::vector<std::string>{"a"})
            {
                return fail("Expected source node to be in the first layer");
            }
            if (!sameNodes(layers[1], {"b", "c"}))
            {
                return fail("Expected both sinks to appear in the second layer");
            }
        }

        {
            TopoDag<std::string> dag({DuplicateNodePolicy::kError, true});
            dag.addNode("root");
            if (!expectThrows([&]
                              { dag.addNode("root"); }))
            {
                return fail("Expected duplicate addNode to throw under kError");
            }

            dag.addEdge("root", "left");
            dag.addEdge("root", "right");
            const auto layers = dag.toposort();
            if (layers.size() != 2)
            {
                return fail("Expected addEdge to reuse existing nodes without duplicate-node failure");
            }
        }

        {
            TopoDag<int> dag;
            dag.addEdge(1, 2);
            dag.addEdge(1, 2);
            dag.addEdge(1, 2);

            if (dag.edgeCount() != 1)
            {
                return fail("Expected duplicate edges to be deduplicated");
            }

            const auto layers = dag.toposort();
            if (layers.size() != 2 || layers[0] != std::vector<int>{1} || layers[1] != std::vector<int>{2})
            {
                return fail("Unexpected layered order after duplicate-edge deduplication");
            }
        }

        {
            TopoDag<int> dag;
            dag.addEdge(1, 2);
            dag.addEdge(2, 3);
            dag.addEdge(3, 1);

            if (!expectThrows([&]
                              { (void)dag.toposort(); }))
            {
                return fail("Expected cycle detection to throw");
            }
        }

        {
            TopoDagBuilder<std::string> builder;
            builder.reserveLocalBuilders(2);
            auto left = builder.createLocalBuilder();
            auto right = builder.createLocalBuilder();

            left.addNode("isolated");
            left.addEdge("a", "c");
            right.addEdge("b", "c");
            right.addEdge("b", "c");

            auto dag = builder.finalize();
            if (dag.nodeCount() != 4)
            {
                return fail("Expected builder finalize to merge local builders into one graph");
            }
            if (dag.edgeCount() != 2)
            {
                return fail("Expected builder finalize to preserve global duplicate-edge deduplication");
            }

            const auto layers = dag.toposort();
            if (layers.size() != 2)
            {
                return fail("Expected builder graph to produce two layers");
            }
            if (!sameNodes(layers[0], {"a", "b", "isolated"}))
            {
                return fail("Expected all indegree-zero nodes in the first layer");
            }
            if (layers[1] != std::vector<std::string>{"c"})
            {
                return fail("Expected merged sink node in the second layer");
            }
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Unexpected exception: ") + ex.what());
    }

    return 0;
}
