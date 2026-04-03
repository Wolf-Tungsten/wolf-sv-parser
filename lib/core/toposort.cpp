#include "core/toposort.hpp"

#include <stdexcept>

namespace wolvrix::lib::toposort
{

    [[noreturn]] void throwDuplicateNodeError()
    {
        throw std::runtime_error("toposort addNode failed: duplicate node");
    }

    [[noreturn]] void throwCycleError()
    {
        throw std::runtime_error("toposort failed: graph contains cycle");
    }

    [[noreturn]] void throwNodeLimitError()
    {
        throw std::runtime_error("toposort failed: node count exceeds NodeId limit");
    }

    [[noreturn]] void throwIndegreeOverflowError()
    {
        throw std::runtime_error("toposort failed: indegree exceeds uint32_t limit");
    }

} // namespace wolvrix::lib::toposort
