#ifndef WOLVRIX_TRANSFORM_REPCUT_PARTITION_SET_HPP
#define WOLVRIX_TRANSFORM_REPCUT_PARTITION_SET_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wolvrix::lib::transform::detail
{

    class PartitionSet
    {
    public:
        void add(uint32_t partId)
        {
            switch (mode_)
            {
            case Mode::kInline:
                addInline(partId);
                return;
            case Mode::kSparse:
                addSparse(partId);
                return;
            case Mode::kDense:
                addDense(partId);
                return;
            }
        }

        void merge(const PartitionSet &other)
        {
            if (other.empty())
            {
                return;
            }
            if (mode_ == Mode::kDense && other.mode_ == Mode::kDense)
            {
                if (denseWords_.size() < other.denseWords_.size())
                {
                    denseWords_.resize(other.denseWords_.size(), 0);
                }
                for (std::size_t i = 0; i < other.denseWords_.size(); ++i)
                {
                    denseWords_[i] |= other.denseWords_[i];
                }
                return;
            }
            other.forEach([&](uint32_t partId) { add(partId); });
        }

        bool contains(uint32_t partId) const
        {
            switch (mode_)
            {
            case Mode::kInline:
            {
                const auto begin = inlineParts_.begin();
                const auto end = begin + inlineSize_;
                const auto it = std::lower_bound(begin, end, partId);
                return it != end && *it == partId;
            }
            case Mode::kSparse:
                return std::binary_search(sparseParts_.begin(), sparseParts_.end(), partId);
            case Mode::kDense:
            {
                const std::size_t wordIdx = static_cast<std::size_t>(partId >> 6u);
                if (wordIdx >= denseWords_.size())
                {
                    return false;
                }
                const uint64_t bit = uint64_t{1} << (partId & 63u);
                return (denseWords_[wordIdx] & bit) != 0;
            }
            }
            return false;
        }

        bool empty() const noexcept
        {
            switch (mode_)
            {
            case Mode::kInline:
                return inlineSize_ == 0;
            case Mode::kSparse:
                return sparseParts_.empty();
            case Mode::kDense:
            {
                for (const uint64_t word : denseWords_)
                {
                    if (word != 0)
                    {
                        return false;
                    }
                }
                return true;
            }
            }
            return true;
        }

        std::optional<uint32_t> first() const
        {
            switch (mode_)
            {
            case Mode::kInline:
                if (inlineSize_ > 0)
                {
                    return inlineParts_[0];
                }
                return std::nullopt;
            case Mode::kSparse:
                if (!sparseParts_.empty())
                {
                    return sparseParts_.front();
                }
                return std::nullopt;
            case Mode::kDense:
                for (std::size_t i = 0; i < denseWords_.size(); ++i)
                {
                    const uint64_t word = denseWords_[i];
                    if (word == 0)
                    {
                        continue;
                    }
                    const uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    return static_cast<uint32_t>(i * 64 + bit);
                }
                return std::nullopt;
            }
            return std::nullopt;
        }

        template <typename Fn>
        void forEach(Fn &&fn) const
        {
            switch (mode_)
            {
            case Mode::kInline:
                for (std::size_t i = 0; i < inlineSize_; ++i)
                {
                    fn(inlineParts_[i]);
                }
                return;
            case Mode::kSparse:
                for (const uint32_t partId : sparseParts_)
                {
                    fn(partId);
                }
                return;
            case Mode::kDense:
                for (std::size_t wordIdx = 0; wordIdx < denseWords_.size(); ++wordIdx)
                {
                    uint64_t word = denseWords_[wordIdx];
                    while (word != 0)
                    {
                        const uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                        fn(static_cast<uint32_t>(wordIdx * 64 + bit));
                        word &= (word - 1);
                    }
                }
                return;
            }
        }

    private:
        static constexpr std::size_t kInlineCapacity = 4;
        static constexpr std::size_t kDenseThreshold = 32;

        enum class Mode : uint8_t
        {
            kInline,
            kSparse,
            kDense,
        };

        void addInline(uint32_t partId)
        {
            const auto begin = inlineParts_.begin();
            const auto end = begin + inlineSize_;
            const auto it = std::lower_bound(begin, end, partId);
            if (it != end && *it == partId)
            {
                return;
            }
            const std::size_t insertAt = static_cast<std::size_t>(it - begin);
            if (inlineSize_ < kInlineCapacity)
            {
                for (std::size_t idx = inlineSize_; idx > insertAt; --idx)
                {
                    inlineParts_[idx] = inlineParts_[idx - 1];
                }
                inlineParts_[insertAt] = partId;
                ++inlineSize_;
                return;
            }

            sparseParts_.assign(begin, end);
            inlineSize_ = 0;
            mode_ = Mode::kSparse;
            addSparse(partId);
        }

        void addSparse(uint32_t partId)
        {
            auto it = std::lower_bound(sparseParts_.begin(), sparseParts_.end(), partId);
            if (it != sparseParts_.end() && *it == partId)
            {
                return;
            }
            sparseParts_.insert(it, partId);
            if (sparseParts_.size() >= kDenseThreshold)
            {
                promoteSparseToDense();
            }
        }

        void addDense(uint32_t partId)
        {
            const std::size_t wordIdx = static_cast<std::size_t>(partId >> 6u);
            if (wordIdx >= denseWords_.size())
            {
                denseWords_.resize(wordIdx + 1, 0);
            }
            denseWords_[wordIdx] |= (uint64_t{1} << (partId & 63u));
        }

        void promoteSparseToDense()
        {
            uint32_t maxPartId = 0;
            if (!sparseParts_.empty())
            {
                maxPartId = sparseParts_.back();
            }
            denseWords_.assign(static_cast<std::size_t>(maxPartId >> 6u) + 1, 0);
            for (const uint32_t partId : sparseParts_)
            {
                const std::size_t wordIdx = static_cast<std::size_t>(partId >> 6u);
                denseWords_[wordIdx] |= (uint64_t{1} << (partId & 63u));
            }
            sparseParts_.clear();
            mode_ = Mode::kDense;
        }

        Mode mode_ = Mode::kInline;
        std::array<uint32_t, kInlineCapacity> inlineParts_{};
        std::size_t inlineSize_ = 0;
        std::vector<uint32_t> sparseParts_;
        std::vector<uint64_t> denseWords_;
    };

} // namespace wolvrix::lib::transform::detail

#endif // WOLVRIX_TRANSFORM_REPCUT_PARTITION_SET_HPP
