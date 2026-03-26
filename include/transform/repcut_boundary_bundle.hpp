#ifndef WOLVRIX_TRANSFORM_REPCUT_BOUNDARY_BUNDLE_HPP
#define WOLVRIX_TRANSFORM_REPCUT_BOUNDARY_BUNDLE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace wolvrix::lib::transform::detail
{

    struct RepcutBoundaryValueDesc
    {
        uint64_t valueKey = 0;
        uint32_t ownerPart = 0;
        int64_t width = 0;
        std::vector<uint32_t> dstParts;
    };

    struct RepcutBoundaryBundle
    {
        uint32_t ownerPart = 0;
        std::vector<uint32_t> dstParts;
        std::string dstSignature;
        std::vector<uint64_t> members;
        int64_t totalWidth = 0;
    };

    struct RepcutBoundaryBundleMember
    {
        std::size_t groupIndex = 0;
        int64_t sliceStart = 0;
        int64_t sliceEnd = 0;
    };

    struct RepcutBoundaryBundleResult
    {
        std::vector<RepcutBoundaryBundle> groups;
        std::unordered_map<uint64_t, RepcutBoundaryBundleMember> members;
    };

    RepcutBoundaryBundleResult buildRepcutBoundaryBundles(std::span<const RepcutBoundaryValueDesc> values);

} // namespace wolvrix::lib::transform::detail

#endif // WOLVRIX_TRANSFORM_REPCUT_BOUNDARY_BUNDLE_HPP
