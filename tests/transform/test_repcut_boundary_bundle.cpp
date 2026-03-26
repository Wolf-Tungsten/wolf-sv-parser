#include "transform/repcut_boundary_bundle.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[repcut-boundary-bundle-tests] " << message << '\n';
        return 1;
    }

} // namespace

int main()
{
    using wolvrix::lib::transform::detail::RepcutBoundaryBundleResult;
    using wolvrix::lib::transform::detail::RepcutBoundaryValueDesc;
    using wolvrix::lib::transform::detail::buildRepcutBoundaryBundles;

    const std::vector<RepcutBoundaryValueDesc> values = {
        {.valueKey = 1, .ownerPart = 1, .width = 8, .dstParts = {2}},
        {.valueKey = 2, .ownerPart = 1, .width = 4, .dstParts = {2}},
        {.valueKey = 3, .ownerPart = 1, .width = 1, .dstParts = {2, 3}},
        {.valueKey = 4, .ownerPart = 1, .width = 2, .dstParts = {3, 2, 3}},
        {.valueKey = 5, .ownerPart = 1, .width = 7, .dstParts = {3}},
        {.valueKey = 6, .ownerPart = 2, .width = 4, .dstParts = {3}},
        {.valueKey = 7, .ownerPart = 2, .width = 5, .dstParts = {3}},
    };

    const RepcutBoundaryBundleResult result = buildRepcutBoundaryBundles(values);
    if (result.groups.size() != 3)
    {
        return fail("expected 3 grouped bundles");
    }

    if (result.groups[0].ownerPart != 1 || result.groups[0].dstSignature != "p2" ||
        result.groups[0].members != std::vector<uint64_t>({1, 2}) || result.groups[0].totalWidth != 12)
    {
        return fail("unexpected bundle layout for owner=1,dst=p2");
    }
    if (result.groups[1].ownerPart != 1 || result.groups[1].dstSignature != "p2_p3" ||
        result.groups[1].members != std::vector<uint64_t>({3, 4}) || result.groups[1].totalWidth != 3)
    {
        return fail("unexpected bundle layout for owner=1,dst=p2_p3");
    }
    if (result.groups[2].ownerPart != 2 || result.groups[2].dstSignature != "p3" ||
        result.groups[2].members != std::vector<uint64_t>({6, 7}) || result.groups[2].totalWidth != 9)
    {
        return fail("unexpected bundle layout for owner=2,dst=p3");
    }

    const auto member1 = result.members.find(1);
    const auto member2 = result.members.find(2);
    const auto member3 = result.members.find(3);
    const auto member4 = result.members.find(4);
    const auto member6 = result.members.find(6);
    const auto member7 = result.members.find(7);
    if (member1 == result.members.end() || member2 == result.members.end() ||
        member3 == result.members.end() || member4 == result.members.end() ||
        member6 == result.members.end() || member7 == result.members.end())
    {
        return fail("expected grouped members to have slice layouts");
    }
    if (member1->second.groupIndex != 0 || member1->second.sliceStart != 4 || member1->second.sliceEnd != 11)
    {
        return fail("unexpected slice layout for value 1");
    }
    if (member2->second.groupIndex != 0 || member2->second.sliceStart != 0 || member2->second.sliceEnd != 3)
    {
        return fail("unexpected slice layout for value 2");
    }
    if (member3->second.groupIndex != 1 || member3->second.sliceStart != 2 || member3->second.sliceEnd != 2)
    {
        return fail("unexpected slice layout for value 3");
    }
    if (member4->second.groupIndex != 1 || member4->second.sliceStart != 0 || member4->second.sliceEnd != 1)
    {
        return fail("unexpected slice layout for value 4");
    }
    if (member6->second.groupIndex != 2 || member6->second.sliceStart != 5 || member6->second.sliceEnd != 8)
    {
        return fail("unexpected slice layout for value 6");
    }
    if (member7->second.groupIndex != 2 || member7->second.sliceStart != 0 || member7->second.sliceEnd != 4)
    {
        return fail("unexpected slice layout for value 7");
    }
    if (result.members.find(5) != result.members.end())
    {
        return fail("single connectivity value should not be bundled");
    }

    return 0;
}
