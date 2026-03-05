#include "transform/repcut_partition_set.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[repcut-partition-set-tests] " << message << '\n';
        return 1;
    }

    std::vector<uint32_t> collectParts(const wolvrix::lib::transform::detail::PartitionSet &set)
    {
        std::vector<uint32_t> out;
        set.forEach([&](uint32_t partId) { out.push_back(partId); });
        return out;
    }

} // namespace

int main()
{
    using wolvrix::lib::transform::detail::PartitionSet;

    {
        PartitionSet set;
        if (!set.empty())
        {
            return fail("new PartitionSet should be empty");
        }
        if (set.first().has_value())
        {
            return fail("empty PartitionSet should not have first owner");
        }
        set.add(7);
        set.add(3);
        set.add(7);

        if (set.empty())
        {
            return fail("PartitionSet with members should not be empty");
        }
        if (!set.contains(3) || !set.contains(7) || set.contains(1))
        {
            return fail("contains() mismatch in inline mode");
        }
        const std::optional<uint32_t> first = set.first();
        if (!first || *first != 3)
        {
            return fail("first() should return smallest partition id in inline mode");
        }
        if (collectParts(set) != std::vector<uint32_t>({3, 7}))
        {
            return fail("forEach() order mismatch in inline mode");
        }
    }

    {
        PartitionSet set;
        set.add(19);
        set.add(1);
        set.add(1024);
        set.add(64);
        set.add(63);
        if (collectParts(set) != std::vector<uint32_t>({1, 19, 63, 64, 1024}))
        {
            return fail("forEach() order mismatch in sparse mode");
        }
        const std::optional<uint32_t> first = set.first();
        if (!first || *first != 1)
        {
            return fail("first() should return smallest partition id in sparse mode");
        }
    }

    {
        PartitionSet set;
        for (uint32_t partId = 80; partId > 0; --partId)
        {
            set.add(partId - 1);
        }
        if (!set.contains(0) || !set.contains(79) || set.contains(80))
        {
            return fail("contains() mismatch in dense mode");
        }
        const std::vector<uint32_t> parts = collectParts(set);
        if (parts.size() != 80)
        {
            return fail("expected 80 unique partitions after dense promotion");
        }
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i] != static_cast<uint32_t>(i))
            {
                return fail("dense forEach() should stay sorted and deduplicated");
            }
        }
    }

    {
        PartitionSet lhs;
        PartitionSet rhs;
        for (uint32_t partId = 0; partId < 64; ++partId)
        {
            lhs.add(partId);
        }
        for (uint32_t partId = 32; partId < 96; ++partId)
        {
            rhs.add(partId);
        }
        lhs.merge(rhs);
        const std::vector<uint32_t> parts = collectParts(lhs);
        if (parts.size() != 96)
        {
            return fail("merge() should keep union cardinality");
        }
        const std::optional<uint32_t> first = lhs.first();
        if (!first || *first != 0 || !lhs.contains(95))
        {
            return fail("merge() union content mismatch");
        }
    }

    return 0;
}
