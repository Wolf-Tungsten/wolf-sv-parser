#include "emit/grhsim_cpp.hpp"

#include "transform/activity_schedule.hpp"

#include "slang/numeric/SVInt.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wolvrix::lib::emit
{

    namespace
    {

        using wolvrix::lib::grh::Graph;
        using wolvrix::lib::grh::Operation;
        using wolvrix::lib::grh::OperationId;
        using wolvrix::lib::grh::OperationIdHash;
        using wolvrix::lib::grh::OperationKind;
        using wolvrix::lib::grh::Value;
        using wolvrix::lib::grh::ValueId;
        using wolvrix::lib::grh::ValueIdHash;
        using wolvrix::lib::grh::ValueType;
        using wolvrix::lib::transform::ActivityScheduleStateReadSupernodes;
        using wolvrix::lib::transform::ActivityScheduleSupernodeToOps;
        using wolvrix::lib::transform::ActivityScheduleTopoOrder;
        using wolvrix::lib::transform::ActivityScheduleValueFanout;

        template <typename T>
        std::optional<T> getAttribute(const Operation &op, std::string_view key)
        {
            auto attr = op.attr(key);
            if (!attr)
            {
                return std::nullopt;
            }
            if (const auto *ptr = std::get_if<T>(&*attr))
            {
                return *ptr;
            }
            return std::nullopt;
        }

        std::string sanitizeIdentifier(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            if (text.empty() || (!std::isalpha(static_cast<unsigned char>(text.front())) && text.front() != '_'))
            {
                out.push_back('_');
            }
            for (unsigned char ch : text)
            {
                if (std::isalnum(ch) || ch == '_')
                {
                    out.push_back(static_cast<char>(ch));
                }
                else
                {
                    out.push_back('_');
                }
            }
            return out;
        }

        std::string escapeCppString(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            for (unsigned char ch : text)
            {
                switch (ch)
                {
                case '\\':
                    out.append("\\\\");
                    break;
                case '"':
                    out.append("\\\"");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    out.push_back(static_cast<char>(ch));
                    break;
                }
            }
            return out;
        }

        std::string sanitizeCommentText(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (unsigned char ch : text)
            {
                switch (ch)
                {
                case '\n':
                case '\r':
                case '\t':
                    out.push_back(' ');
                    break;
                default:
                    out.push_back(static_cast<char>(ch));
                    break;
                }
            }
            return out;
        }

        std::string joinStrings(const std::vector<std::string> &items, std::string_view sep)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (i != 0)
                {
                    out << sep;
                }
                out << items[i];
            }
            return out.str();
        }

        bool allStringsEqual(const std::vector<std::string> &items, std::string_view value)
        {
            return std::all_of(items.begin(), items.end(), [&](const std::string &item) { return item == value; });
        }

        template <typename T>
        void sortUniqueVector(std::vector<T> &items)
        {
            std::sort(items.begin(), items.end());
            items.erase(std::unique(items.begin(), items.end()), items.end());
        }

        constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);
        constexpr std::size_t kActivationTableThreshold = 32;
        constexpr std::size_t kActiveFlagBitsPerWord = 8;

        struct ActiveMaskEntry
        {
            std::size_t wordIndex = 0;
            std::uint8_t mask = 0;
        };

        std::size_t bitWordCount(std::size_t bitCount) noexcept
        {
            return (bitCount + 63u) / 64u;
        }

        std::vector<ActiveMaskEntry> buildActiveMaskEntries(const std::vector<uint32_t> &indices)
        {
            std::map<std::size_t, std::uint8_t> masksByWord;
            for (uint32_t index : indices)
            {
                const std::size_t wordIndex = static_cast<std::size_t>(index) / kActiveFlagBitsPerWord;
                const std::uint8_t mask =
                    static_cast<std::uint8_t>(UINT8_C(1) << (static_cast<std::size_t>(index) % kActiveFlagBitsPerWord));
                masksByWord[wordIndex] = static_cast<std::uint8_t>(masksByWord[wordIndex] | mask);
            }
            std::vector<ActiveMaskEntry> entries;
            entries.reserve(masksByWord.size());
            for (const auto &[wordIndex, mask] : masksByWord)
            {
                entries.push_back(ActiveMaskEntry{.wordIndex = wordIndex, .mask = mask});
            }
            return entries;
        }

        struct ActivationEmitContext
        {
            std::size_t currentWordIndex = 0;
            std::size_t currentActiveId = kInvalidIndex;
            std::string_view localActiveExpr;
        };

        void emitActivationStatements(std::ostream &stream,
                                      std::string_view activeExpr,
                                      std::string_view activeCountExpr,
                                      const std::vector<uint32_t> &indices,
                                      std::string_view indent,
                                      const struct ActivationEmitContext *context = nullptr,
                                      const std::vector<std::size_t> *activeIdBySupernode = nullptr)
        {
            (void)activeCountExpr;
            (void)activeIdBySupernode;
            if (indices.empty())
            {
                return;
            }
            std::vector<uint32_t> localIndices;
            std::vector<uint32_t> globalIndices;
            if (context != nullptr && !context->localActiveExpr.empty())
            {
                localIndices.reserve(indices.size());
                globalIndices.reserve(indices.size());
                for (uint32_t activeId : indices)
                {
                    const std::size_t targetWordIndex =
                        static_cast<std::size_t>(activeId) / kActiveFlagBitsPerWord;
                    if (targetWordIndex == context->currentWordIndex &&
                        context->currentActiveId != kInvalidIndex &&
                        static_cast<std::size_t>(activeId) > context->currentActiveId)
                    {
                        localIndices.push_back(activeId);
                    }
                    else
                    {
                        globalIndices.push_back(activeId);
                    }
                }
            }
            else
            {
                globalIndices = indices;
            }

            if (!localIndices.empty())
            {
                std::uint8_t localMask = UINT8_C(0);
                for (uint32_t index : localIndices)
                {
                    localMask = static_cast<std::uint8_t>(
                        localMask |
                        (UINT8_C(1) << (static_cast<std::size_t>(index) % kActiveFlagBitsPerWord)));
                }
                stream << indent << "{\n";
                stream << indent << "    " << context->localActiveExpr << " |= UINT8_C("
                       << static_cast<unsigned>(localMask) << ");\n";
                stream << indent << "}\n";
            }

            const std::vector<ActiveMaskEntry> entries = buildActiveMaskEntries(globalIndices);
            if (entries.empty())
            {
                return;
            }
            if (entries.size() >= kActivationTableThreshold)
            {
                stream << indent << "{\n";
                stream << indent << "    static constexpr grhsim_active_mask_entry kActivationMasks[] = {";
                for (std::size_t i = 0; i < entries.size(); ++i)
                {
                    if ((i % 8u) == 0u)
                    {
                        stream << "\n" << indent << "        ";
                    }
                    stream << "{"
                           << entries[i].wordIndex << "u, UINT8_C(" << static_cast<unsigned>(entries[i].mask) << ")}";
                    if (i + 1u != entries.size())
                    {
                        stream << ", ";
                    }
                }
                stream << "\n" << indent << "    };\n";
                stream << indent << "    for (const auto &entry : kActivationMasks) {\n";
                stream << indent << "        " << activeExpr << "[entry.word_index] |= entry.mask;\n";
                stream << indent << "    }\n";
                stream << indent << "}\n";
                return;
            }
            for (const auto &entry : entries)
            {
                stream << indent << "{\n";
                stream << indent << "    " << activeExpr << "[" << entry.wordIndex << "u] |= UINT8_C("
                       << static_cast<unsigned>(entry.mask) << ");\n";
                stream << indent << "}\n";
            }
        }

        struct EmitModel;

        void emitChangedValuePropagation(std::ostream &stream,
                                         const EmitModel &model,
                                         ValueId resultValue,
                                         std::string_view indent,
                                         const ActivationEmitContext *context = nullptr);

        struct ScalarLogicExpr
        {
            std::string expr;
            bool alreadyBoundedToResultWidth = false;
        };

        std::string scalarMaskExpr(std::size_t width)
        {
            if (width == 0)
            {
                return "UINT64_C(0)";
            }
            if (width >= 64)
            {
                return "~UINT64_C(0)";
            }
            return "UINT64_C(" + std::to_string((UINT64_C(1) << width) - UINT64_C(1)) + ")";
        }

        std::string scalarTruncExpr(const std::string &expr, std::size_t width)
        {
            if (width == 0)
            {
                return "UINT64_C(0)";
            }
            if (width >= 64)
            {
                return "(" + expr + ")";
            }
            return "((" + expr + ") & " + scalarMaskExpr(width) + ")";
        }

        std::string scalarConcatExpr(const std::string &lhs,
                                     std::size_t lhsWidth,
                                     const std::string &rhs,
                                     std::size_t rhsWidth)
        {
            if (lhsWidth == 0)
            {
                return scalarTruncExpr(rhs, rhsWidth);
            }
            if (rhsWidth == 0)
            {
                return scalarTruncExpr(lhs, lhsWidth);
            }
            return "((" + scalarTruncExpr(lhs, lhsWidth) + " << " + std::to_string(rhsWidth) + ") | " +
                   scalarTruncExpr(rhs, rhsWidth) + ")";
        }

        std::string scalarShiftAmountExpr(const std::string &expr)
        {
            return "static_cast<std::uint64_t>(" + expr + ")";
        }

        std::string scalarShlExpr(const std::string &valueExpr,
                                  const std::string &shiftExpr,
                                  std::size_t width)
        {
            if (width == 0)
            {
                return "UINT64_C(0)";
            }
            const std::string shift = scalarShiftAmountExpr(shiftExpr);
            return "((" + shift + " >= UINT64_C(64)) ? UINT64_C(0) : " +
                   scalarTruncExpr("(" + scalarTruncExpr(valueExpr, width) + " << static_cast<unsigned>(" + shift + "))",
                                   width) +
                   ")";
        }

        std::string scalarLShrExpr(const std::string &valueExpr,
                                   const std::string &shiftExpr,
                                   std::size_t width)
        {
            if (width == 0)
            {
                return "UINT64_C(0)";
            }
            const std::string shift = scalarShiftAmountExpr(shiftExpr);
            return "((" + shift + " >= UINT64_C(64)) ? UINT64_C(0) : (" +
                   scalarTruncExpr(valueExpr, width) + " >> static_cast<unsigned>(" + shift + ")))";
        }

        std::string scalarSliceDynamicExpr(const std::string &valueExpr,
                                           std::size_t valueWidth,
                                           const std::string &startExpr,
                                           std::size_t sliceWidth)
        {
            if (sliceWidth == 0)
            {
                return "UINT64_C(0)";
            }
            return scalarTruncExpr(scalarLShrExpr(valueExpr, startExpr, valueWidth), sliceWidth);
        }

        std::string scalarSliceArrayExpr(const std::string &valueExpr,
                                         std::size_t valueWidth,
                                         const std::string &indexExpr,
                                         std::size_t sliceWidth)
        {
            if (sliceWidth == 0)
            {
                return "UINT64_C(0)";
            }
            const std::string startExpr =
                "(static_cast<std::uint64_t>(" + indexExpr + ") * UINT64_C(" + std::to_string(sliceWidth) + "))";
            return scalarSliceDynamicExpr(valueExpr, valueWidth, startExpr, sliceWidth);
        }

        std::size_t parseScheduleBatchMaxOps(const EmitOptions &options)
        {
            auto it = options.attributes.find("sched_batch_max_ops");
            if (it == options.attributes.end())
            {
                return 512;
            }
            try
            {
                return static_cast<std::size_t>(std::stoull(it->second));
            }
            catch (const std::exception &)
            {
                return 512;
            }
        }

        std::size_t parseScheduleBatchMaxEstimatedLines(const EmitOptions &options)
        {
            auto it = options.attributes.find("sched_batch_max_estimated_lines");
            if (it == options.attributes.end())
            {
                return 4096;
            }
            try
            {
                return static_cast<std::size_t>(std::stoull(it->second));
            }
            catch (const std::exception &)
            {
                return 4096;
            }
        }

        std::size_t parseEmitParallelism(const EmitOptions &options)
        {
            auto fallback = []() -> std::size_t
            {
                const unsigned value = std::thread::hardware_concurrency();
                return value == 0 ? 1u : static_cast<std::size_t>(value);
            };

            auto it = options.attributes.find("emit_parallelism");
            if (it == options.attributes.end())
            {
                return fallback();
            }
            try
            {
                const std::size_t parsed = static_cast<std::size_t>(std::stoull(it->second));
                return parsed == 0 ? fallback() : parsed;
            }
            catch (const std::exception &)
            {
                return fallback();
            }
        }

        enum class WaveformMode
        {
            kOff,
            kDeclaredSymbols,
        };

        enum class PerfMode
        {
            kOff,
            kEval,
        };

        WaveformMode parseWaveformMode(const EmitOptions &options)
        {
            auto it = options.attributes.find("waveform");
            if (it == options.attributes.end() || it->second.empty() || it->second == "off")
            {
                return WaveformMode::kOff;
            }
            if (it->second == "declared-symbols")
            {
                return WaveformMode::kDeclaredSymbols;
            }
            return WaveformMode::kOff;
        }

        PerfMode parsePerfMode(const EmitOptions &options)
        {
            auto it = options.attributes.find("perf");
            if (it == options.attributes.end() || it->second.empty() || it->second == "off")
            {
                return PerfMode::kOff;
            }
            if (it->second == "eval")
            {
                return PerfMode::kEval;
            }
            return PerfMode::kOff;
        }

        std::uint64_t effectiveMaxOutputFileBytes(const EmitOptions &options) noexcept
        {
            return options.maxOutputFileBytes == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                   : options.maxOutputFileBytes;
        }

        class LimitedOutputBuffer final : public std::streambuf
        {
        public:
            LimitedOutputBuffer(std::streambuf *dest, std::uint64_t limit) : dest_(dest), limit_(limit) {}

            [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
            [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return written_; }
            [[nodiscard]] std::uint64_t limit() const noexcept { return limit_; }

        protected:
            std::streamsize xsputn(const char *s, std::streamsize count) override
            {
                if (dest_ == nullptr || count <= 0)
                {
                    return 0;
                }
                const std::uint64_t remaining = remainingBytes();
                if (remaining == 0)
                {
                    exceeded_ = true;
                    return 0;
                }
                const std::streamsize allowed =
                    static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(count)));
                const std::streamsize written = dest_->sputn(s, allowed);
                if (written > 0)
                {
                    written_ += static_cast<std::uint64_t>(written);
                }
                if (written != count)
                {
                    exceeded_ = exceeded_ || static_cast<std::uint64_t>(count) > remaining;
                }
                return written;
            }

            int overflow(int ch) override
            {
                if (ch == traits_type::eof())
                {
                    return traits_type::not_eof(ch);
                }
                if (dest_ == nullptr)
                {
                    return traits_type::eof();
                }
                if (remainingBytes() == 0)
                {
                    exceeded_ = true;
                    return traits_type::eof();
                }
                if (traits_type::eq_int_type(dest_->sputc(traits_type::to_char_type(ch)), traits_type::eof()))
                {
                    return traits_type::eof();
                }
                ++written_;
                return ch;
            }

        private:
            [[nodiscard]] std::uint64_t remainingBytes() const noexcept
            {
                return written_ >= limit_ ? 0 : (limit_ - written_);
            }

            std::streambuf *dest_ = nullptr;
            std::uint64_t limit_ = 0;
            std::uint64_t written_ = 0;
            bool exceeded_ = false;
        };

        class LimitedOutputStream final : public std::ostream
        {
        public:
            LimitedOutputStream(const std::filesystem::path &path, std::uint64_t limit)
                : std::ostream(nullptr), file_(path, std::ios::out | std::ios::trunc), buffer_(file_.rdbuf(), limit)
            {
                rdbuf(&buffer_);
                if (!file_.is_open())
                {
                    setstate(std::ios::badbit);
                }
            }

            [[nodiscard]] bool isOpen() const noexcept { return file_.is_open(); }
            [[nodiscard]] bool exceeded() const noexcept { return buffer_.exceeded(); }
            [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return buffer_.bytesWritten(); }
            [[nodiscard]] std::uint64_t limit() const noexcept { return buffer_.limit(); }

            void close()
            {
                flush();
                file_.close();
            }

        private:
            std::ofstream file_;
            LimitedOutputBuffer buffer_;
        };

        std::optional<std::string> finalizeOutputFile(LimitedOutputStream &stream,
                                                      const std::filesystem::path &path)
        {
            stream.close();
            if (stream.exceeded())
            {
                std::ostringstream out;
                out << "emitted file exceeded byte limit (" << stream.limit() << " bytes)";
                return out.str();
            }
            if (!stream)
            {
                return "failed to write output file";
            }
            return std::nullopt;
        }

        std::optional<std::string> ensureOutputDirectory(const std::filesystem::path &path)
        {
            const std::filesystem::path parent = path.parent_path();
            if (parent.empty())
            {
                return std::nullopt;
            }
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                return "failed to create output directory: " + parent.string() + ": " + ec.message();
            }
            return std::nullopt;
        }

        bool isGeneratedArtifactName(std::string_view name,
                                     std::string_view prefix,
                                     std::string_view archiveName) noexcept
        {
            const std::string prefixText(prefix);
            return name == prefixText + ".hpp" ||
                   name == prefixText + "_runtime.hpp" ||
                   name == prefixText + "_declared_value_index.txt" ||
                   name == prefixText + "_state.cpp" ||
                   name == prefixText + "_state.o" ||
                   name == prefixText + "_eval.cpp" ||
                   name == prefixText + "_eval.o" ||
                   name == archiveName ||
                   name.rfind(prefixText + "_state_", 0) == 0 ||
                   name.rfind(prefixText + "_sched_", 0) == 0;
        }

        std::optional<std::string> removeStaleGeneratedArtifacts(const std::filesystem::path &dir,
                                                                 std::string_view prefix)
        {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec))
            {
                return std::nullopt;
            }
            if (ec)
            {
                return "failed to inspect output directory: " + dir.string() + ": " + ec.message();
            }
            if (!std::filesystem::is_directory(dir, ec))
            {
                if (ec)
                {
                    return "failed to inspect output directory: " + dir.string() + ": " + ec.message();
                }
                return "output path is not a directory: " + dir.string();
            }

            const std::string archiveName = "lib" + std::string(prefix) + ".a";
            for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec)
                {
                    return "failed to list output directory: " + dir.string() + ": " + ec.message();
                }

                std::error_code entryEc;
                if (!entry.is_regular_file(entryEc))
                {
                    if (entryEc)
                    {
                        return "failed to inspect output file: " + entry.path().string() + ": " + entryEc.message();
                    }
                    continue;
                }

                const std::string name = entry.path().filename().string();
                if (!isGeneratedArtifactName(name, prefix, archiveName))
                {
                    continue;
                }

                std::error_code removeEc;
                std::filesystem::remove(entry.path(), removeEc);
                if (removeEc)
                {
                    return "failed to remove stale generated artifact: " + entry.path().string() + ": " + removeEc.message();
                }
            }

            return std::nullopt;
        }

        bool isScalarLogicValue(const Graph &graph, ValueId value) noexcept
        {
            return graph.valueType(value) == ValueType::Logic &&
                   graph.valueWidth(value) > 0 &&
                   graph.valueWidth(value) <= 64;
        }

        std::size_t logicWordCount(int32_t width) noexcept
        {
            if (width <= 0)
            {
                return 0;
            }
            return (static_cast<std::size_t>(width) + 63u) / 64u;
        }

        bool isWideLogicWidth(int32_t width) noexcept
        {
            return width > 64;
        }

        bool isWideLogicValue(const Graph &graph, ValueId value) noexcept
        {
            return graph.valueType(value) == ValueType::Logic &&
                   isWideLogicWidth(graph.valueWidth(value));
        }

        bool isValidLogicConditionValue(const Graph &graph, ValueId value) noexcept
        {
            return graph.valueType(value) == ValueType::Logic &&
                   graph.valueWidth(value) >= 1;
        }

        std::string logicCppType(int32_t width)
        {
            if (width <= 1)
            {
                return "bool";
            }
            if (width <= 8)
            {
                return "std::uint8_t";
            }
            if (width <= 16)
            {
                return "std::uint16_t";
            }
            if (width <= 32)
            {
                return "std::uint32_t";
            }
            if (width <= 64)
            {
                return "std::uint64_t";
            }
            std::ostringstream out;
            out << "std::array<std::uint64_t, " << logicWordCount(width) << ">";
            return out.str();
        }

        std::string defaultInitExprForLogicWidth(int32_t width)
        {
            if (width <= 1)
            {
                return "false";
            }
            if (width <= 64)
            {
                return "0";
            }
            return "{}";
        }

        std::string cppTypeForValue(const Graph &graph, ValueId value)
        {
            switch (graph.valueType(value))
            {
            case ValueType::Real:
                return "double";
            case ValueType::String:
                return "std::string";
            case ValueType::Logic:
            default:
                break;
            }
            return logicCppType(graph.valueWidth(value));
        }

        std::string defaultInitExpr(const Graph &graph, ValueId value)
        {
            switch (graph.valueType(value))
            {
            case ValueType::Real:
                return "0.0";
            case ValueType::String:
                return "{}";
            case ValueType::Logic:
            default:
                break;
            }
            return defaultInitExprForLogicWidth(graph.valueWidth(value));
        }

        std::string valueDebugName(const Graph &graph, ValueId value)
        {
            const std::string_view symbol = graph.symbolText(graph.valueSymbol(value));
            if (!symbol.empty())
            {
                return sanitizeIdentifier(symbol);
            }
            return "v" + std::to_string(value.index);
        }

        std::string localValueName(const Graph &graph, ValueId value)
        {
            return "local_value_" + std::to_string(value.index) + "_" + std::to_string(value.generation) + "_" +
                   valueDebugName(graph, value);
        }

        std::string opDebugName(const Graph &graph, OperationId op)
        {
            const std::string_view symbol = graph.symbolText(graph.operationSymbol(op));
            if (!symbol.empty())
            {
                return sanitizeIdentifier(symbol);
            }
            return "op" + std::to_string(op.index);
        }

        std::optional<std::string> constantExpr(const Graph &graph, const Operation &op, ValueId resultValue)
        {
            auto literal = getAttribute<std::string>(op, "constValue");
            if (!literal)
            {
                return std::nullopt;
            }
            switch (graph.valueType(resultValue))
            {
            case ValueType::String:
            {
                if (literal->size() >= 2 && literal->front() == '"' && literal->back() == '"')
                {
                    return *literal;
                }
                return "\"" + escapeCppString(*literal) + "\"";
            }
            case ValueType::Real:
                return *literal;
            case ValueType::Logic:
            default:
                break;
            }
            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(*literal);
                parsed = parsed.resize(static_cast<slang::bitwidth_t>(graph.valueWidth(resultValue)));
                if (parsed.hasUnknown())
                {
                    parsed.flattenUnknowns();
                }
                if (!isWideLogicValue(graph, resultValue))
                {
                    const auto raw = parsed.as<uint64_t>();
                    if (!raw)
                    {
                        return std::nullopt;
                    }
                    std::ostringstream out;
                    out << "UINT64_C(" << *raw << ")";
                    return out.str();
                }

                std::ostringstream out;
                const std::size_t words = logicWordCount(graph.valueWidth(resultValue));
                out << "std::array<std::uint64_t, " << words << ">{";
                const std::uint64_t *rawWords = parsed.getRawPtr();
                for (std::size_t i = 0; i < words; ++i)
                {
                    if (i != 0)
                    {
                        out << ", ";
                    }
                    out << "UINT64_C(" << rawWords[i] << ")";
                }
                out << "}";
                return out.str();
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        std::optional<slang::SVInt> parseConstLiteral(std::string_view literal)
        {
            std::string compact;
            compact.reserve(literal.size());
            for (char ch : literal)
            {
                if (ch == '_' || std::isspace(static_cast<unsigned char>(ch)))
                {
                    continue;
                }
                compact.push_back(ch);
            }
            if (compact.empty() || compact.front() == '"' || compact.front() == '$')
            {
                return std::nullopt;
            }

            bool negative = false;
            if (compact.front() == '-' || compact.front() == '+')
            {
                negative = compact.front() == '-';
                compact.erase(compact.begin());
            }
            if (compact.empty())
            {
                return std::nullopt;
            }

            try
            {
                slang::SVInt parsed = slang::SVInt::fromString(compact);
                if (negative)
                {
                    parsed = -parsed;
                }
                return parsed;
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        std::optional<std::string> logicValueToCppExpr(const slang::SVInt &value, int32_t width)
        {
            if (width <= 0)
            {
                return std::nullopt;
            }
            slang::SVInt sized = value.resize(static_cast<slang::bitwidth_t>(width));
            if (sized.hasUnknown())
            {
                sized.flattenUnknowns();
            }
            if (!isWideLogicWidth(width))
            {
                const auto raw = sized.as<std::uint64_t>();
                if (!raw)
                {
                    return std::nullopt;
                }
                return "static_cast<" + logicCppType(width) + ">(UINT64_C(" + std::to_string(*raw) + "))";
            }

            std::ostringstream out;
            const std::size_t words = logicWordCount(width);
            out << "std::array<std::uint64_t, " << words << ">{";
            const std::uint64_t *rawWords = sized.getRawPtr();
            for (std::size_t i = 0; i < words; ++i)
            {
                if (i != 0)
                {
                    out << ", ";
                }
                out << "UINT64_C(" << rawWords[i] << ")";
            }
            out << "}";
            return out.str();
        }

        std::optional<std::string> logicLiteralToCppExpr(std::string_view literal, int32_t width)
        {
            auto parsed = parseConstLiteral(literal);
            if (!parsed)
            {
                return std::nullopt;
            }
            return logicValueToCppExpr(*parsed, width);
        }

        bool hasAnyMemoryInitAttrs(const Operation &op)
        {
            return op.attr("initKind").has_value() ||
                   op.attr("initFile").has_value() ||
                   op.attr("initValue").has_value() ||
                   op.attr("initStart").has_value() ||
                   op.attr("initLen").has_value();
        }

        bool tokenizeReadmemText(std::string_view text,
                                 std::vector<std::string> &tokens,
                                 std::string &error)
        {
            tokens.clear();
            for (std::size_t i = 0; i < text.size();)
            {
                const char ch = text[i];
                if (std::isspace(static_cast<unsigned char>(ch)))
                {
                    ++i;
                    continue;
                }
                if (ch == '/' && i + 1 < text.size())
                {
                    if (text[i + 1] == '/')
                    {
                        i += 2;
                        while (i < text.size() && text[i] != '\n')
                        {
                            ++i;
                        }
                        continue;
                    }
                    if (text[i + 1] == '*')
                    {
                        const std::size_t end = text.find("*/", i + 2);
                        if (end == std::string_view::npos)
                        {
                            error = "unterminated block comment in readmem file";
                            return false;
                        }
                        i = end + 2;
                        continue;
                    }
                }

                const std::size_t begin = i;
                while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
                {
                    if (text[i] == '/' && i + 1 < text.size() &&
                        (text[i + 1] == '/' || text[i + 1] == '*'))
                    {
                        break;
                    }
                    ++i;
                }
                if (i > begin)
                {
                    tokens.emplace_back(text.substr(begin, i - begin));
                }
            }
            return true;
        }

        std::optional<std::size_t> parseReadmemAddress(std::string_view token)
        {
            if (token.empty())
            {
                return std::nullopt;
            }
            std::size_t value = 0;
            for (unsigned char ch : token)
            {
                if (ch == '_')
                {
                    continue;
                }
                value <<= 4;
                if (ch >= '0' && ch <= '9')
                {
                    value |= static_cast<std::size_t>(ch - '0');
                }
                else if (ch >= 'a' && ch <= 'f')
                {
                    value |= static_cast<std::size_t>(ch - 'a' + 10);
                }
                else if (ch >= 'A' && ch <= 'F')
                {
                    value |= static_cast<std::size_t>(ch - 'A' + 10);
                }
                else
                {
                    return std::nullopt;
                }
            }
            return value;
        }

        struct InitExprCode
        {
            std::string expr;
            bool requiresRuntime = false;
        };

        InitExprCode randomInitExprForWidth(int32_t width);

        bool buildMemoryInitRowExprs(const Operation &memoryOp,
                                     int32_t width,
                                     int64_t rowCount,
                                     std::vector<std::optional<InitExprCode>> &rowExprs,
                                     std::string &error)
        {
            rowExprs.assign(static_cast<std::size_t>(rowCount), std::nullopt);
            if (!hasAnyMemoryInitAttrs(memoryOp))
            {
                return true;
            }

            auto kindsOpt = getAttribute<std::vector<std::string>>(memoryOp, "initKind");
            auto filesOpt = getAttribute<std::vector<std::string>>(memoryOp, "initFile");
            auto startsOpt = getAttribute<std::vector<int64_t>>(memoryOp, "initStart");
            auto lensOpt = getAttribute<std::vector<int64_t>>(memoryOp, "initLen");
            auto values = getAttribute<std::vector<std::string>>(memoryOp, "initValue").value_or(std::vector<std::string>{});

            if (!kindsOpt || !filesOpt || !startsOpt || !lensOpt)
            {
                error = "memory init attrs incomplete: " + std::string(memoryOp.symbolText());
                return false;
            }

            const auto &kinds = *kindsOpt;
            const auto &files = *filesOpt;
            const auto &starts = *startsOpt;
            const auto &lens = *lensOpt;
            if (kinds.size() != files.size() ||
                kinds.size() != starts.size() ||
                kinds.size() != lens.size())
            {
                error = "memory init attr size mismatch: " + std::string(memoryOp.symbolText());
                return false;
            }

            for (std::size_t i = 0; i < kinds.size(); ++i)
            {
                const int64_t start = starts[i];
                const int64_t len = lens[i];
                const std::size_t lower = start < 0 ? 0u : static_cast<std::size_t>(std::max<int64_t>(0, start));
                const std::size_t upper = start < 0
                                              ? static_cast<std::size_t>(rowCount)
                                              : static_cast<std::size_t>(std::min<int64_t>(rowCount,
                                                                                            len <= 0 ? rowCount : start + len));

                if (kinds[i] == "literal")
                {
                    const std::string literal = (i < values.size() && !values[i].empty()) ? values[i] : "0";
                    std::optional<InitExprCode> initExpr;
                    if (literal == "$random")
                    {
                        initExpr = randomInitExprForWidth(width);
                    }
                    else if (auto expr = logicLiteralToCppExpr(literal, width))
                    {
                        initExpr = InitExprCode{.expr = *expr, .requiresRuntime = false};
                    }
                    if (!initExpr)
                    {
                        error = "memory literal init is not statically evaluable: " + std::string(memoryOp.symbolText());
                        return false;
                    }
                    for (std::size_t row = lower; row < upper; ++row)
                    {
                        rowExprs[row] = *initExpr;
                    }
                    continue;
                }

                if (kinds[i] != "readmemh" && kinds[i] != "readmemb")
                {
                    error = "unsupported memory initKind on " + std::string(memoryOp.symbolText()) + ": " + kinds[i];
                    return false;
                }
                if (files[i].empty())
                {
                    error = "memory initFile missing on " + std::string(memoryOp.symbolText());
                    return false;
                }

                std::filesystem::path initPath(files[i]);
                if (initPath.is_relative())
                {
                    initPath = std::filesystem::current_path() / initPath;
                }
                std::ifstream stream(initPath, std::ios::in | std::ios::binary);
                if (!stream.is_open())
                {
                    error = "failed to open memory initFile: " + initPath.string();
                    return false;
                }
                const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
                std::vector<std::string> tokens;
                if (!tokenizeReadmemText(text, tokens, error))
                {
                    error = initPath.string() + ": " + error;
                    return false;
                }

                const bool isHex = kinds[i] == "readmemh";
                std::size_t row = lower;
                for (const std::string &token : tokens)
                {
                    if (!token.empty() && token.front() == '@')
                    {
                        auto addr = parseReadmemAddress(std::string_view(token).substr(1));
                        if (!addr)
                        {
                            error = "invalid readmem address token in " + initPath.string() + ": " + token;
                            return false;
                        }
                        row = *addr;
                        continue;
                    }

                    if (row >= static_cast<std::size_t>(rowCount))
                    {
                        ++row;
                        continue;
                    }
                    if (row >= lower && row < upper)
                    {
                        std::string literal = std::to_string(width) + (isHex ? "'h" : "'b") + token;
                        auto expr = logicLiteralToCppExpr(literal, width);
                        if (!expr)
                        {
                            error = "invalid readmem data token in " + initPath.string() + ": " + token;
                            return false;
                        }
                        rowExprs[row] = InitExprCode{.expr = *expr, .requiresRuntime = false};
                    }
                    ++row;
                }
            }

            return true;
        }

        struct EventOperandInfo
        {
            std::vector<ValueId> values;
            std::vector<std::string> edges;
        };

        EventOperandInfo collectEventOperands(const Operation &op)
        {
            EventOperandInfo info;
            info.edges = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{});
            if (info.edges.empty())
            {
                return info;
            }

            const auto operands = op.operands();
            std::size_t eventStart = operands.size();
            switch (op.kind())
            {
            case OperationKind::kRegisterWritePort:
                eventStart = 3;
                break;
            case OperationKind::kMemoryWritePort:
                eventStart = 4;
                break;
            case OperationKind::kSystemTask:
            case OperationKind::kDpicCall:
                eventStart = operands.size() >= info.edges.size() ? operands.size() - info.edges.size() : operands.size();
                break;
            default:
                eventStart = operands.size();
                break;
            }

            const std::size_t eventCount = std::min(info.edges.size(), operands.size() - std::min(eventStart, operands.size()));
            info.values.reserve(eventCount);
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                info.values.push_back(operands[eventStart + i]);
            }
            info.edges.resize(eventCount);
            return info;
        }

        struct ScheduleRefs
        {
            const ActivityScheduleSupernodeToOps &supernodeToOps;
            const ActivityScheduleValueFanout &valueFanout;
            const ActivityScheduleTopoOrder &topoOrder;
            const ActivityScheduleStateReadSupernodes &stateReadSupernodes;
        };

        struct ScheduleBatch
        {
            std::size_t index = 0;
            std::size_t estimatedLines = 0;
            std::size_t opCount = 0;
            std::size_t activeFlagWordIndex = 0;
            std::vector<uint32_t> supernodeIds;
        };

        enum class ValueSlotScalarKind
        {
            kBool,
            kU8,
            kU16,
            kU32,
            kU64,
            kCount,
        };

        struct StateDecl
        {
            enum class Kind
            {
                Register,
                Latch,
                Memory
            };

            Kind kind = Kind::Register;
            std::string symbol;
            std::string cppType;
            int32_t width = 0;
            bool isSigned = false;
            int64_t rowCount = 0;
            std::size_t slotIndex = kInvalidIndex;
            ValueSlotScalarKind scalarKind = ValueSlotScalarKind::kBool;
            std::size_t wordCount = 0;
            std::optional<InitExprCode> initExpr;
            std::vector<std::optional<InitExprCode>> memoryInitRowExprs;
        };

        InitExprCode randomInitExprForWidth(int32_t width)
        {
            InitExprCode init;
            init.requiresRuntime = true;
            if (isWideLogicWidth(width))
            {
                std::ostringstream out;
                out << "grhsim_random_words<" << logicWordCount(width) << ">(random_state_, " << width << ")";
                init.expr = out.str();
            }
            else
            {
                init.expr = "static_cast<" + logicCppType(width) + ">(grhsim_random_u64(random_state_, " +
                            std::to_string(width) + "))";
            }
            return init;
        }

        struct WriteDecl
        {
            enum class MemoryMaskMode
            {
                kDynamic,
                kConstZero,
                kConstAllOnes,
            };

            enum class MemoryAddrMode
            {
                kGeneric,
                kInRange,
                kPow2Wrap,
            };

            OperationId opId;
            StateDecl::Kind kind = StateDecl::Kind::Register;
            std::string symbol;
            std::size_t shadowTouchedIndex = kInvalidIndex;
            std::size_t shadowDataIndex = kInvalidIndex;
            std::size_t shadowMaskIndex = kInvalidIndex;
            std::size_t shadowAddrIndex = kInvalidIndex;
            std::size_t shadowIndex = kInvalidIndex;
            ValueSlotScalarKind shadowScalarKind = ValueSlotScalarKind::kBool;
            std::size_t shadowWordCount = 0;
            MemoryMaskMode memoryMaskMode = MemoryMaskMode::kDynamic;
            MemoryAddrMode memoryAddrMode = MemoryAddrMode::kGeneric;
            std::size_t memoryRowMask = 0;
        };

        struct StateShadowDecl
        {
            StateDecl::Kind kind = StateDecl::Kind::Register;
            std::string symbol;
            std::size_t emitIndex = kInvalidIndex;
            std::size_t touchedIndex = kInvalidIndex;
            std::size_t dataIndex = kInvalidIndex;
            ValueSlotScalarKind scalarKind = ValueSlotScalarKind::kBool;
            std::size_t wordCount = 0;
        };

        struct DpiImportDecl
        {
            std::string symbol;
            std::vector<std::string> argsDirection;
            std::vector<int64_t> argsWidth;
            std::vector<std::string> argsName;
            std::vector<bool> argsSigned;
            std::vector<std::string> argsType;
            bool hasReturn = false;
            int64_t returnWidth = 0;
            bool returnSigned = false;
            std::string returnType;
        };

        struct EventSampleDecl
        {
            std::vector<ValueId> values;
            std::vector<std::string> edges;
            std::string completedFieldName;
        };

        struct ValueScalarSlotRef
        {
            ValueSlotScalarKind kind = ValueSlotScalarKind::kBool;
            std::size_t index = 0;
        };

        struct ValueWideSlotRef
        {
            std::size_t wordCount = 0;
            std::size_t index = 0;
        };

        struct WaveformSignalDecl
        {
            enum class SourceKind
            {
                kValue,
                kState,
            };

            SourceKind sourceKind = SourceKind::kValue;
            std::string symbol;
            std::string escapedSymbol;
            ValueType type = ValueType::Logic;
            int32_t width = 0;
            bool isSigned = false;
            ValueId value{};
            std::string stateSymbol;
        };

        struct EmitModel
        {
            std::unordered_map<ValueId, std::string, ValueIdHash> inputFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> prevInputFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> valueFieldByValue;
            std::unordered_map<ValueId, std::string, ValueIdHash> localValueNameByValue;
            std::unordered_map<ValueId, ValueScalarSlotRef, ValueIdHash> valueScalarSlotByValue;
            std::unordered_map<ValueId, ValueWideSlotRef, ValueIdHash> valueWideSlotByValue;
            std::unordered_map<ValueId, std::size_t, ValueIdHash> valueRealSlotByValue;
            std::unordered_map<ValueId, std::size_t, ValueIdHash> valueStringSlotByValue;
            std::unordered_set<ValueId, ValueIdHash> materializedValues;
            std::unordered_map<ValueId, std::vector<uint32_t>, ValueIdHash> inputHeadSupernodesByValue;
            std::unordered_map<ValueId, std::vector<uint32_t>, ValueIdHash> boundaryFanoutByValue;
            std::vector<std::size_t> activeIdBySupernode;
            std::unordered_map<std::string, StateDecl> stateBySymbol;
            std::unordered_map<std::string, std::vector<uint32_t>> stateHeadSupernodesBySymbol;
            std::unordered_map<OperationId, WriteDecl, OperationIdHash> writeByOp;
            std::unordered_map<std::string, StateShadowDecl> stateShadowBySymbol;
            std::unordered_map<OperationId, EventSampleDecl, OperationIdHash> eventSamplesByOp;
            std::unordered_map<ValueId, std::string, ValueIdHash> eventEdgeFieldByValue;
            std::unordered_map<std::string, DpiImportDecl> dpiImportBySymbol;
            std::vector<WriteDecl> writes;
            std::vector<StateShadowDecl> stateShadows;
            std::vector<DpiImportDecl> dpiImports;
            std::vector<ValueId> allEventValues;
            std::vector<ValueId> inputEventValues;
            std::vector<std::string> eventFieldDecls;
            std::vector<WaveformSignalDecl> waveformSignals;
            std::vector<std::string> stateOrder;
            std::vector<std::string> valueFieldDecls;
            std::vector<std::string> stateFieldDecls;
            std::vector<std::string> shadowFieldDecls;
            std::vector<std::string> writeFieldDecls;
            std::vector<std::string> dpiDecls;
            std::vector<std::string> publicPortDecls;
            std::vector<std::string> inputFieldDecls;
            std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)> valueScalarSlotCounts{};
            std::map<std::size_t, std::size_t> valueWideSlotCountsByWords;
            std::size_t valueRealSlotCount = 0;
            std::size_t valueStringSlotCount = 0;
            std::size_t eventEdgeSlotCount = 0;
            std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)> stateLogicScalarSlotCounts{};
            std::map<std::size_t, std::size_t> stateLogicWideSlotCountsByWords;
            std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)> stateMemoryScalarSlotCounts{};
            std::map<std::size_t, std::size_t> stateMemoryWideSlotCountsByWords;
            std::size_t stateShadowTouchedCount = 0;
            std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)> stateShadowScalarSlotCounts{};
            std::map<std::size_t, std::size_t> stateShadowWideSlotCountsByWords;
            std::size_t memoryWriteTouchedCount = 0;
            std::size_t memoryWriteAddrCount = 0;
            std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)> memoryWriteDataScalarSlotCounts{};
            std::map<std::size_t, std::size_t> memoryWriteDataWideSlotCountsByWords;
            std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)> memoryWriteMaskScalarSlotCounts{};
            std::map<std::size_t, std::size_t> memoryWriteMaskWideSlotCountsByWords;
            bool needsSystemTaskRuntime = false;
            bool emitPerf = false;
            bool emitWaveform = false;
        };

        bool isStoredValue(const EmitModel &model, ValueId value) noexcept
        {
            return model.inputFieldByValue.find(value) != model.inputFieldByValue.end() ||
                   model.materializedValues.find(value) != model.materializedValues.end();
        }

        bool emitMaterializedConstantInit(std::ostream &stream,
                                          const Graph &graph,
                                          const EmitModel &model,
                                          ValueId valueId,
                                          std::string_view indent,
                                          std::string &error)
        {
            if (!model.materializedValues.contains(valueId))
            {
                return true;
            }
            const OperationId defOpId = graph.valueDef(valueId);
            if (!defOpId.valid())
            {
                return true;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kConstant)
            {
                return true;
            }

            const auto expr = constantExpr(graph, defOp, valueId);
            if (!expr)
            {
                error = "unsupported constant emit";
                return false;
            }

            const auto valueFieldIt = model.valueFieldByValue.find(valueId);
            if (valueFieldIt == model.valueFieldByValue.end())
            {
                error = "materialized constant missing storage slot";
                return false;
            }
            const std::string &lhs = valueFieldIt->second;
            switch (graph.valueType(valueId))
            {
            case ValueType::Logic:
                if (isWideLogicValue(graph, valueId))
                {
                    stream << indent << lhs << " = " << *expr << ";\n";
                }
                else
                {
                    stream << indent << lhs << " = static_cast<" << cppTypeForValue(graph, valueId)
                           << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << *expr << "), "
                           << graph.valueWidth(valueId) << "));\n";
                }
                break;
            case ValueType::Real:
            case ValueType::String:
                stream << indent << lhs << " = " << *expr << ";\n";
                break;
            }
            return true;
        }

        ValueSlotScalarKind valueScalarSlotKindForWidth(int32_t width)
        {
            if (width <= 1)
            {
                return ValueSlotScalarKind::kBool;
            }
            if (width <= 8)
            {
                return ValueSlotScalarKind::kU8;
            }
            if (width <= 16)
            {
                return ValueSlotScalarKind::kU16;
            }
            if (width <= 32)
            {
                return ValueSlotScalarKind::kU32;
            }
            return ValueSlotScalarKind::kU64;
        }

        std::string scalarLogicSlotCppType(ValueSlotScalarKind kind)
        {
            switch (kind)
            {
            case ValueSlotScalarKind::kBool:
                return "std::uint8_t";
            case ValueSlotScalarKind::kU8:
                return "std::uint8_t";
            case ValueSlotScalarKind::kU16:
                return "std::uint16_t";
            case ValueSlotScalarKind::kU32:
                return "std::uint32_t";
            case ValueSlotScalarKind::kU64:
                return "std::uint64_t";
            case ValueSlotScalarKind::kCount:
                break;
            }
            return "std::uint64_t";
        }

        std::string scalarLogicSlotFieldName(std::string_view prefix, ValueSlotScalarKind kind)
        {
            switch (kind)
            {
            case ValueSlotScalarKind::kBool:
                return std::string(prefix) + "bool_slots_";
            case ValueSlotScalarKind::kU8:
                return std::string(prefix) + "u8_slots_";
            case ValueSlotScalarKind::kU16:
                return std::string(prefix) + "u16_slots_";
            case ValueSlotScalarKind::kU32:
                return std::string(prefix) + "u32_slots_";
            case ValueSlotScalarKind::kU64:
                return std::string(prefix) + "u64_slots_";
            case ValueSlotScalarKind::kCount:
                break;
            }
            return std::string(prefix) + "unknown_slots_";
        }

        std::string valueScalarSlotFieldName(ValueSlotScalarKind kind)
        {
            return scalarLogicSlotFieldName("value_", kind);
        }

        std::string wideLogicSlotFieldName(std::string_view prefix, std::size_t wordCount)
        {
            return std::string(prefix) + "words_" + std::to_string(wordCount) + "_slots_";
        }

        std::string valueWideSlotFieldName(std::size_t wordCount)
        {
            return wideLogicSlotFieldName("value_", wordCount);
        }

        std::string logicSlotRefExpr(std::string_view scalarPrefix,
                                     std::string_view widePrefix,
                                     ValueSlotScalarKind scalarKind,
                                     std::size_t wordCount,
                                     std::size_t index,
                                     bool isWide)
        {
            if (isWide)
            {
                return wideLogicSlotFieldName(widePrefix, wordCount) + "[" + std::to_string(index) + "]";
            }
            return scalarLogicSlotFieldName(scalarPrefix, scalarKind) + "[" + std::to_string(index) + "]";
        }

        std::string stateLogicScalarSlotFieldName(ValueSlotScalarKind kind)
        {
            return scalarLogicSlotFieldName("state_logic_", kind);
        }

        std::string stateLogicWideSlotFieldName(std::size_t wordCount)
        {
            return wideLogicSlotFieldName("state_logic_", wordCount);
        }

        std::string stateMemoryScalarSlotFieldName(ValueSlotScalarKind kind)
        {
            return scalarLogicSlotFieldName("state_mem_", kind);
        }

        std::string stateMemoryWideSlotFieldName(std::size_t wordCount)
        {
            return wideLogicSlotFieldName("state_mem_", wordCount);
        }

        std::string stateRef(const StateDecl &state)
        {
            if (state.kind == StateDecl::Kind::Memory)
            {
                if (isWideLogicWidth(state.width))
                {
                    return stateMemoryWideSlotFieldName(state.wordCount) + "[" + std::to_string(state.slotIndex) + "]";
                }
                return stateMemoryScalarSlotFieldName(state.scalarKind) + "[" + std::to_string(state.slotIndex) + "]";
            }
            if (isWideLogicWidth(state.width))
            {
                return stateLogicWideSlotFieldName(state.wordCount) + "[" + std::to_string(state.slotIndex) + "]";
            }
            return stateLogicScalarSlotFieldName(state.scalarKind) + "[" + std::to_string(state.slotIndex) + "]";
        }

        void emitChangedValuePropagation(std::ostream &stream,
                                         const EmitModel &model,
                                         ValueId resultValue,
                                         std::string_view indent,
                                         const ActivationEmitContext *context)
        {
            const auto it = model.boundaryFanoutByValue.find(resultValue);
            if (it == model.boundaryFanoutByValue.end())
            {
                return;
            }
            emitActivationStatements(
                stream,
                "supernode_active_curr_",
                "active_count_",
                it->second,
                indent,
                context);
        }

        std::string renderScalarLogicExpr(const Graph &graph, ValueId resultValue, const ScalarLogicExpr &rhs)
        {
            if (rhs.alreadyBoundedToResultWidth)
            {
                return "static_cast<" + cppTypeForValue(graph, resultValue) + ">(" + rhs.expr + ")";
            }
            return "static_cast<" + cppTypeForValue(graph, resultValue) + ">(" +
                   scalarTruncExpr(rhs.expr, static_cast<std::size_t>(graph.valueWidth(resultValue))) + ")";
        }

        bool valueNeedsChangeDetect(const EmitModel &model, ValueId resultValue)
        {
            const auto it = model.boundaryFanoutByValue.find(resultValue);
            return it != model.boundaryFanoutByValue.end() && !it->second.empty();
        }

        bool isEventValue(const EmitModel &model, ValueId value)
        {
            return model.eventEdgeFieldByValue.find(value) != model.eventEdgeFieldByValue.end();
        }

        bool valueNeedsTrackedChange(const EmitModel &model, ValueId resultValue)
        {
            return valueNeedsChangeDetect(model, resultValue) || isEventValue(model, resultValue);
        }

        bool isMaterializedValue(const EmitModel &model, ValueId value);
        std::string valueRef(const EmitModel &model, ValueId value);

        void emitChangedValueEffects(std::ostream &stream,
                                     const EmitModel &model,
                                     ValueId resultValue,
                                     std::string_view oldExpr,
                                     std::string_view newExpr,
                                     std::string_view indent,
                                     const ActivationEmitContext *context = nullptr)
        {
            if (const auto eventIt = model.eventEdgeFieldByValue.find(resultValue);
                eventIt != model.eventEdgeFieldByValue.end())
            {
                stream << indent << eventIt->second << " = grhsim_classify_edge(" << oldExpr << ", " << newExpr << ");\n";
            }
            emitChangedValuePropagation(stream, model, resultValue, indent, context);
        }

        bool isMaterializedValue(const EmitModel &model, ValueId value)
        {
            return isStoredValue(model, value);
        }

        bool opNeedsWordLogicEmit(const Graph &graph, const Operation &op) noexcept;
        std::string valueRef(const EmitModel &model, ValueId value);

        std::optional<slang::SVInt> constLogicValue(const Graph &graph, ValueId value, int32_t width)
        {
            if (graph.valueType(value) != ValueType::Logic || width <= 0)
            {
                return std::nullopt;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            const Operation defOp = graph.getOperation(defOpId);
            if (defOp.kind() != OperationKind::kConstant)
            {
                return std::nullopt;
            }
            auto literal = getAttribute<std::string>(defOp, "constValue");
            if (!literal)
            {
                return std::nullopt;
            }
            auto parsed = parseConstLiteral(*literal);
            if (!parsed)
            {
                return std::nullopt;
            }
            parsed = parsed->resize(static_cast<slang::bitwidth_t>(width));
            if (parsed->hasUnknown())
            {
                return std::nullopt;
            }
            return parsed;
        }

        bool isConstLogicAllZero(const Graph &graph, ValueId value, int32_t width)
        {
            auto parsed = constLogicValue(graph, value, width);
            if (!parsed)
            {
                return false;
            }
            const std::size_t words = logicWordCount(width);
            const std::uint64_t *rawWords = parsed->getRawPtr();
            for (std::size_t i = 0; i < words; ++i)
            {
                if (rawWords[i] != 0)
                {
                    return false;
                }
            }
            return true;
        }

        bool isConstLogicAllOnes(const Graph &graph, ValueId value, int32_t width)
        {
            auto parsed = constLogicValue(graph, value, width);
            if (!parsed)
            {
                return false;
            }
            const std::size_t words = logicWordCount(width);
            const std::uint64_t *rawWords = parsed->getRawPtr();
            for (std::size_t i = 0; i < words; ++i)
            {
                const std::size_t wordWidth =
                    (i + 1u == words) ? (static_cast<std::size_t>(width) - i * 64u) : 64u;
                const std::uint64_t expected = wordWidth >= 64u ? ~UINT64_C(0) : ((UINT64_C(1) << wordWidth) - 1u);
                if (rawWords[i] != expected)
                {
                    return false;
                }
            }
            return true;
        }

        bool isPowerOfTwoU64(std::uint64_t value) noexcept
        {
            return value != 0 && (value & (value - 1u)) == 0;
        }

        std::size_t minUnsignedBitsForRange(std::uint64_t exclusiveUpperBound) noexcept
        {
            std::size_t bits = 0;
            std::uint64_t limit = 1;
            while (limit < exclusiveUpperBound)
            {
                limit <<= 1u;
                ++bits;
            }
            return bits;
        }

        bool validateRegisterWritePort(const Graph &graph,
                                       const Operation &op,
                                       const StateDecl &state,
                                       std::string &error)
        {
            const auto operands = op.operands();
            const std::string opName = std::string(op.symbolText());
            if (!op.results().empty())
            {
                error = "kRegisterWritePort must not have results: " + opName;
                return false;
            }
            if (operands.size() < 4)
            {
                error = "kRegisterWritePort missing operands: " + opName;
                return false;
            }
            if (!isValidLogicConditionValue(graph, operands[0]))
            {
                error = "kRegisterWritePort updateCond must be logic: " + opName;
                return false;
            }
            if (graph.valueType(operands[1]) != ValueType::Logic || graph.valueWidth(operands[1]) != state.width)
            {
                error = "kRegisterWritePort nextValue width/type mismatch: " + opName;
                return false;
            }
            if (graph.valueType(operands[2]) != ValueType::Logic || graph.valueWidth(operands[2]) != state.width)
            {
                error = "kRegisterWritePort mask width/type mismatch: " + opName;
                return false;
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            if (!eventEdges)
            {
                error = "kRegisterWritePort missing eventEdge: " + opName;
                return false;
            }
            const std::size_t eventCount = operands.size() - 3;
            if (eventEdges->size() != eventCount)
            {
                error = "kRegisterWritePort eventEdge size mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                const ValueId eventValue = operands[3 + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kRegisterWritePort event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }
            return true;
        }

        bool validateMemoryWritePort(const Graph &graph,
                                     const Operation &op,
                                     const StateDecl &state,
                                     std::string &error)
        {
            const auto operands = op.operands();
            const std::string opName = std::string(op.symbolText());
            if (!op.results().empty())
            {
                error = "kMemoryWritePort must not have results: " + opName;
                return false;
            }
            if (operands.size() < 5)
            {
                error = "kMemoryWritePort missing operands: " + opName;
                return false;
            }
            if (!isValidLogicConditionValue(graph, operands[0]))
            {
                error = "kMemoryWritePort updateCond must be logic: " + opName;
                return false;
            }
            if (graph.valueType(operands[1]) != ValueType::Logic || graph.valueWidth(operands[1]) <= 0)
            {
                error = "kMemoryWritePort addr must be logic: " + opName;
                return false;
            }
            if (graph.valueType(operands[2]) != ValueType::Logic || graph.valueWidth(operands[2]) != state.width)
            {
                error = "kMemoryWritePort data width/type mismatch: " + opName;
                return false;
            }
            if (graph.valueType(operands[3]) != ValueType::Logic || graph.valueWidth(operands[3]) != state.width)
            {
                error = "kMemoryWritePort mask width/type mismatch: " + opName;
                return false;
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            if (!eventEdges)
            {
                error = "kMemoryWritePort missing eventEdge: " + opName;
                return false;
            }
            const std::size_t eventCount = operands.size() - 4;
            if (eventEdges->size() != eventCount)
            {
                error = "kMemoryWritePort eventEdge size mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                const ValueId eventValue = operands[4 + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kMemoryWritePort event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }
            return true;
        }

        bool validateSystemTask(const Graph &graph,
                                const Operation &op,
                                std::string &error)
        {
            const auto operands = op.operands();
            const std::string opName = std::string(op.symbolText());
            if (!op.results().empty())
            {
                error = "kSystemTask must not have results: " + opName;
                return false;
            }
            if (operands.empty())
            {
                error = "kSystemTask missing callCond operand: " + opName;
                return false;
            }
            if (!isValidLogicConditionValue(graph, operands[0]))
            {
                error = "kSystemTask callCond must be logic: " + opName;
                return false;
            }
            auto name = getAttribute<std::string>(op, "name");
            if (!name || name->empty())
            {
                error = "kSystemTask missing name: " + opName;
                return false;
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(op, "eventEdge");
            const std::size_t eventCount = eventEdges ? eventEdges->size() : 0;
            if (operands.size() < 1 + eventCount)
            {
                error = "kSystemTask eventEdge size mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventCount; ++i)
            {
                const ValueId eventValue = operands[operands.size() - eventCount + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kSystemTask event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }
            return true;
        }

        bool systemTaskIsFinal(const Operation &op)
        {
            return getAttribute<std::string>(op, "procKind").value_or(std::string()) == "final";
        }

        bool systemTaskRunsOnlyOnInitialEval(const Operation &op)
        {
            const std::string procKind = getAttribute<std::string>(op, "procKind").value_or(std::string());
            const bool hasTiming = getAttribute<bool>(op, "hasTiming").value_or(false);
            return procKind == "initial" && !hasTiming;
        }

        bool systemTaskRunsOnceAfterTimedTrigger(const Operation &op)
        {
            const std::string procKind = getAttribute<std::string>(op, "procKind").value_or(std::string());
            const bool hasTiming = getAttribute<bool>(op, "hasTiming").value_or(false);
            return procKind == "initial" && hasTiming;
        }

        bool systemFunctionRunsOnlyOnInitialEval(const Operation &op)
        {
            const std::string procKind = getAttribute<std::string>(op, "procKind").value_or(std::string());
            const bool hasTiming = getAttribute<bool>(op, "hasTiming").value_or(false);
            return procKind == "initial" && !hasTiming;
        }

        std::string systemTaskArgExpr(const Graph &graph,
                                      const EmitModel &model,
                                      ValueId value)
        {
            auto ref = [&]() -> std::string
            {
                if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
                {
                    return it->second;
                }
                return valueRef(model, value);
            };
            const std::string signedText = graph.valueSigned(value) ? "true" : "false";
            switch (graph.valueType(value))
            {
            case ValueType::String:
                return "grhsim_make_task_arg(" + ref() + ")";
            case ValueType::Real:
                return "grhsim_make_task_arg(static_cast<double>(" + ref() + "))";
            case ValueType::Logic:
            default:
                break;
            }
            if (isWideLogicValue(graph, value))
            {
                std::ostringstream out;
                out << "grhsim_make_task_arg(" << ref() << ", "
                    << graph.valueWidth(value) << ", "
                    << signedText << ")";
                return out.str();
            }
            std::ostringstream out;
            out << "grhsim_make_task_arg(static_cast<std::uint64_t>(" << ref() << "), "
                << graph.valueWidth(value) << ", "
                << signedText << ")";
            return out.str();
        }

        std::string stableSystemTaskArgExpr(const Graph &graph,
                                            const EmitModel &model,
                                            ValueId value);

        std::string dpiLoweredTypeName(std::string_view typeName)
        {
            std::string lowered;
            lowered.reserve(typeName.size());
            for (unsigned char ch : typeName)
            {
                lowered.push_back(static_cast<char>(std::tolower(ch)));
            }
            return lowered;
        }

        bool dpiTypeIsString(std::string_view typeName)
        {
            return dpiLoweredTypeName(typeName) == "string";
        }

        bool dpiTypeIsReal(std::string_view typeName)
        {
            const std::string lowered = dpiLoweredTypeName(typeName);
            return lowered == "real" || lowered == "shortreal";
        }

        bool dpiTypeIsShortReal(std::string_view typeName)
        {
            return dpiLoweredTypeName(typeName) == "shortreal";
        }

        bool dpiTypeIsLogicLike(std::string_view typeName)
        {
            return !dpiTypeIsString(typeName) && !dpiTypeIsReal(typeName);
        }

        int64_t dpiEffectiveWidth(std::string_view typeName, int64_t width)
        {
            const std::string lowered = dpiLoweredTypeName(typeName);
            if (lowered == "byte")
            {
                return 8;
            }
            if (lowered == "shortint")
            {
                return 16;
            }
            if (lowered == "int" || lowered == "integer")
            {
                return 32;
            }
            if (lowered == "longint" || lowered == "time")
            {
                return 64;
            }
            return width > 0 ? width : 1;
        }

        std::string dpiScalarLogicCppType(int64_t width, bool isSigned)
        {
            if (width <= 1)
            {
                return "bool";
            }
            if (width <= 8)
            {
                return isSigned ? "std::int8_t" : "std::uint8_t";
            }
            if (width <= 16)
            {
                return isSigned ? "std::int16_t" : "std::uint16_t";
            }
            if (width <= 32)
            {
                return isSigned ? "std::int32_t" : "std::uint32_t";
            }
            return isSigned ? "std::int64_t" : "std::uint64_t";
        }

        std::string dpiBaseCppType(std::string_view typeName, int64_t width, bool isSigned)
        {
            const std::string lowered = dpiLoweredTypeName(typeName);
            if (dpiTypeIsShortReal(typeName))
            {
                return "float";
            }
            if (lowered == "real" || lowered == "shortreal")
            {
                return "double";
            }
            if (lowered == "string")
            {
                return "std::string";
            }
            const int64_t effectiveWidth = dpiEffectiveWidth(typeName, width);
            if (effectiveWidth <= 64)
            {
                return dpiScalarLogicCppType(effectiveWidth, isSigned);
            }
            return logicCppType(static_cast<int32_t>(effectiveWidth));
        }

        std::string dpiDeclArgCppType(std::string_view typeName,
                                      int64_t width,
                                      bool isSigned,
                                      std::string_view direction)
        {
            const std::string baseType = dpiBaseCppType(typeName, width, isSigned);
            if (direction == "output" || direction == "inout")
            {
                return baseType + " *";
            }
            if (dpiTypeIsString(typeName))
            {
                return "const char *";
            }
            if (dpiEffectiveWidth(typeName, width) > 64)
            {
                return "const " + baseType + " &";
            }
            return baseType;
        }

        int findNameIndex(const std::vector<std::string> &names, std::string_view needle)
        {
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == needle)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        ValueType dpiExpectedValueType(std::string_view typeName)
        {
            if (dpiTypeIsString(typeName))
            {
                return ValueType::String;
            }
            if (dpiTypeIsReal(typeName))
            {
                return ValueType::Real;
            }
            return ValueType::Logic;
        }

        bool validateDpiValueType(const Graph &graph,
                                  ValueId value,
                                  std::string_view typeName,
                                  int64_t width,
                                  std::string_view context,
                                  std::string &error)
        {
            const ValueType expectedType = dpiExpectedValueType(typeName);
            if (graph.valueType(value) != expectedType)
            {
                error = std::string(context) + " value type mismatch";
                return false;
            }
            if (expectedType == ValueType::Logic)
            {
                const int64_t expectedWidth = dpiEffectiveWidth(typeName, width);
                if (graph.valueWidth(value) != expectedWidth)
                {
                    error = std::string(context) + " width mismatch";
                    return false;
                }
            }
            return true;
        }

        std::string dpiValueExpr(const Graph &graph,
                                 const EmitModel &model,
                                 ValueId value,
                                 std::string_view typeName,
                                 int64_t width,
                                 bool isSigned)
        {
            const std::string ref = valueRef(model, value);
            if (dpiTypeIsString(typeName))
            {
                return ref + ".c_str()";
            }
            if (dpiTypeIsShortReal(typeName))
            {
                return "static_cast<float>(" + ref + ")";
            }
            if (dpiTypeIsReal(typeName))
            {
                return ref;
            }
            const int64_t effectiveWidth = dpiEffectiveWidth(typeName, width);
            if (effectiveWidth > 64)
            {
                return ref;
            }
            return "static_cast<" + dpiBaseCppType(typeName, width, isSigned) + ">(" + ref + ")";
        }

        bool validateDpicCall(const Graph &graph,
                              const Operation &op,
                              const EmitModel &model,
                              std::string &error)
        {
            const std::string opName = std::string(op.symbolText());
            const auto operands = op.operands();
            if (operands.empty())
            {
                error = "kDpicCall missing callCond operand: " + opName;
                return false;
            }
            if (!isValidLogicConditionValue(graph, operands[0]))
            {
                error = "kDpicCall callCond must be logic: " + opName;
                return false;
            }

            const auto targetImport = getAttribute<std::string>(op, "targetImportSymbol");
            if (!targetImport)
            {
                error = "kDpicCall missing targetImportSymbol: " + opName;
                return false;
            }
            auto importIt = model.dpiImportBySymbol.find(*targetImport);
            if (importIt == model.dpiImportBySymbol.end())
            {
                error = "kDpicCall target import not found: " + *targetImport;
                return false;
            }
            const DpiImportDecl &decl = importIt->second;

            const std::vector<std::string> inArgName =
                getAttribute<std::vector<std::string>>(op, "inArgName").value_or(std::vector<std::string>{});
            const std::vector<std::string> outArgName =
                getAttribute<std::vector<std::string>>(op, "outArgName").value_or(std::vector<std::string>{});
            const std::vector<std::string> inoutArgName =
                getAttribute<std::vector<std::string>>(op, "inoutArgName").value_or(std::vector<std::string>{});
            const std::vector<std::string> eventEdges =
                getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{});
            const bool hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);

            if (!inoutArgName.empty())
            {
                error = "kDpicCall inout args are not supported in grhsim-cpp emit: " + opName;
                return false;
            }
            for (const std::string &direction : decl.argsDirection)
            {
                if (direction == "inout")
                {
                    error = "kDpicImport with inout args is not supported in grhsim-cpp emit: " + decl.symbol;
                    return false;
                }
            }
            if (hasReturn != decl.hasReturn)
            {
                error = "kDpicCall hasReturn mismatch: " + opName;
                return false;
            }
            if (operands.size() != 1 + inArgName.size() + eventEdges.size())
            {
                error = "kDpicCall operand count mismatch: " + opName;
                return false;
            }
            const std::size_t expectedResults = (hasReturn ? 1u : 0u) + outArgName.size();
            if (op.results().size() != expectedResults)
            {
                error = "kDpicCall result count mismatch: " + opName;
                return false;
            }
            for (std::size_t i = 0; i < eventEdges.size(); ++i)
            {
                const ValueId eventValue = operands[1 + inArgName.size() + i];
                if (graph.valueType(eventValue) != ValueType::Logic || graph.valueWidth(eventValue) != 1)
                {
                    error = "kDpicCall event operand must be 1-bit logic: " + opName;
                    return false;
                }
            }

            auto validateUniqueNames = [&](const std::vector<std::string> &names, std::string_view kind) -> bool
            {
                std::unordered_set<std::string> seen;
                for (const std::string &name : names)
                {
                    if (!seen.insert(name).second)
                    {
                        error = "kDpicCall duplicate " + std::string(kind) + " name: " + opName;
                        return false;
                    }
                }
                return true;
            };
            if (!validateUniqueNames(inArgName, "input arg") || !validateUniqueNames(outArgName, "output arg"))
            {
                return false;
            }

            std::size_t importInputCount = 0;
            std::size_t importOutputCount = 0;
            for (const std::string &direction : decl.argsDirection)
            {
                if (direction == "input")
                {
                    ++importInputCount;
                }
                else if (direction == "output")
                {
                    ++importOutputCount;
                }
            }
            if (inArgName.size() != importInputCount || outArgName.size() != importOutputCount)
            {
                error = "kDpicCall arg group size mismatch: " + opName;
                return false;
            }

            std::size_t resultBase = 0;
            if (hasReturn)
            {
                if (!validateDpiValueType(graph,
                                          op.results()[0],
                                          decl.returnType,
                                          decl.returnWidth,
                                          "kDpicCall return",
                                          error))
                {
                    error += ": " + opName;
                    return false;
                }
                resultBase = 1;
            }

            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
            {
                const std::string &direction = decl.argsDirection[i];
                const std::string formalName =
                    i < decl.argsName.size() ? decl.argsName[i] : ("arg" + std::to_string(i));
                const std::string_view typeName =
                    i < decl.argsType.size() ? std::string_view(decl.argsType[i]) : std::string_view("logic");
                const int64_t width = i < decl.argsWidth.size() ? decl.argsWidth[i] : 1;
                if (direction == "input")
                {
                    const int inputIndex = findNameIndex(inArgName, formalName);
                    if (inputIndex < 0)
                    {
                        error = "kDpicCall missing matching input arg " + formalName + ": " + opName;
                        return false;
                    }
                    if (!validateDpiValueType(graph,
                                              operands[1 + static_cast<std::size_t>(inputIndex)],
                                              typeName,
                                              width,
                                              "kDpicCall input arg",
                                              error))
                    {
                        error += ": " + opName + " formal=" + formalName;
                        return false;
                    }
                }
                else if (direction == "output")
                {
                    const int outputIndex = findNameIndex(outArgName, formalName);
                    if (outputIndex < 0)
                    {
                        error = "kDpicCall missing matching output arg " + formalName + ": " + opName;
                        return false;
                    }
                    if (!validateDpiValueType(graph,
                                              op.results()[resultBase + static_cast<std::size_t>(outputIndex)],
                                              typeName,
                                              width,
                                              "kDpicCall output arg",
                                              error))
                    {
                        error += ": " + opName + " formal=" + formalName;
                        return false;
                    }
                }
                else
                {
                    error = "kDpicImport has unsupported arg direction for grhsim-cpp emit: " + decl.symbol;
                    return false;
                }
            }
            return true;
        }

        bool buildModel(const Graph &graph,
                        const ScheduleRefs &schedule,
                        const std::vector<ScheduleBatch> &scheduleBatches,
                        const std::unordered_set<ValueId, ValueIdHash> &waveformValueIds,
                        EmitModel &model,
                        std::string &error)
        {
            (void)scheduleBatches;
            auto registerInputEndpoint = [&](ValueId valueId, const std::string &fieldStem, const std::string &apiStem) {
                if (model.inputFieldByValue.find(valueId) != model.inputFieldByValue.end())
                {
                    return;
                }
                const std::string typeName = cppTypeForValue(graph, valueId);
                model.inputFieldByValue.emplace(valueId, apiStem);
                model.prevInputFieldByValue.emplace(valueId, "prev_" + fieldStem);
                model.inputFieldDecls.push_back("    " + typeName + " prev_" + fieldStem + " = " + defaultInitExpr(graph, valueId) + ";");
                model.publicPortDecls.push_back("    " + typeName + " " + apiStem + " = " + defaultInitExpr(graph, valueId) + ";");
            };

            auto registerReadableEndpoint =
                [&](ValueId valueId, const std::string &fieldStem, const std::string &apiStem, bool initializeField) {
                    const std::string typeName = cppTypeForValue(graph, valueId);
                    const std::string initExpr = initializeField ? defaultInitExpr(graph, valueId) : typeName + "{}";
                    (void)fieldStem;
                    model.publicPortDecls.push_back("    " + typeName + " " + apiStem + " = " + initExpr + ";");
                };

            for (const auto &port : graph.inputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                registerInputEndpoint(port.value, "in_" + name, name);
            }

            for (const auto &port : graph.outputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                registerReadableEndpoint(port.value, "out_" + name, name, true);
            }

            for (const auto &port : graph.inoutPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                const std::string inType = cppTypeForValue(graph, port.in);
                const std::string outType = cppTypeForValue(graph, port.out);
                const std::string oeType = cppTypeForValue(graph, port.oe);
                model.inputFieldByValue.emplace(port.in, name + ".in");
                model.prevInputFieldByValue.emplace(port.in, "prev_inout_" + name + "_in");
                model.inputFieldDecls.push_back("    " + inType + " prev_inout_" + name + "_in = " + defaultInitExpr(graph, port.in) + ";");
                model.publicPortDecls.push_back("    struct Inout_" + name + " {\n"
                                                "        " + inType + " in = " + defaultInitExpr(graph, port.in) + ";\n"
                                                "        " + outType + " out = " + defaultInitExpr(graph, port.out) + ";\n"
                                                "        " + oeType + " oe = " + defaultInitExpr(graph, port.oe) + ";\n"
                                                "    } " + name + ";");
            }

            for (OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                switch (op.kind())
                {
                case OperationKind::kRegister:
                case OperationKind::kLatch:
                case OperationKind::kMemory:
                {
                    auto width = getAttribute<int64_t>(op, "width");
                    auto isSigned = getAttribute<bool>(op, "isSigned");
                    if (!width || !isSigned)
                    {
                        error = "storage declaration missing width/isSigned: " + std::string(op.symbolText());
                        return false;
                    }
                    StateDecl state;
                    state.kind = op.kind() == OperationKind::kRegister
                                     ? StateDecl::Kind::Register
                                     : (op.kind() == OperationKind::kLatch ? StateDecl::Kind::Latch : StateDecl::Kind::Memory);
                    state.symbol = std::string(op.symbolText());
                    state.width = static_cast<int32_t>(*width);
                    state.isSigned = *isSigned;
                    if (state.kind == StateDecl::Kind::Memory)
                    {
                        auto row = getAttribute<int64_t>(op, "row");
                        if (!row || *row <= 0)
                        {
                            error = "memory declaration missing row: " + state.symbol;
                            return false;
                        }
                        state.rowCount = *row;
                        const std::string elemType = logicCppType(state.width);
                        state.cppType = "std::vector<" + elemType + ">";
                        if (isWideLogicWidth(state.width))
                        {
                            state.wordCount = logicWordCount(state.width);
                            state.slotIndex = model.stateMemoryWideSlotCountsByWords[state.wordCount]++;
                        }
                        else
                        {
                            state.scalarKind = valueScalarSlotKindForWidth(state.width);
                            state.slotIndex =
                                model.stateMemoryScalarSlotCounts[static_cast<std::size_t>(state.scalarKind)]++;
                        }
                        if (!buildMemoryInitRowExprs(op, state.width, state.rowCount, state.memoryInitRowExprs, error))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (state.width <= 0)
                        {
                            error = "storage width must be positive: " + state.symbol;
                            return false;
                        }
                        const std::string cppType = logicCppType(state.width);
                        state.cppType = cppType;
                        if (isWideLogicWidth(state.width))
                        {
                            state.wordCount = logicWordCount(state.width);
                            state.slotIndex = model.stateLogicWideSlotCountsByWords[state.wordCount]++;
                        }
                        else
                        {
                            state.scalarKind = valueScalarSlotKindForWidth(state.width);
                            state.slotIndex =
                                model.stateLogicScalarSlotCounts[static_cast<std::size_t>(state.scalarKind)]++;
                        }
                        if (auto initValue = getAttribute<std::string>(op, "initValue"))
                        {
                            if (*initValue == "$random")
                            {
                                state.initExpr = randomInitExprForWidth(state.width);
                            }
                            else if (auto expr = logicLiteralToCppExpr(*initValue, state.width))
                            {
                                state.initExpr = InitExprCode{.expr = *expr, .requiresRuntime = false};
                            }
                            else
                            {
                                error = "storage initValue is not statically evaluable: " + state.symbol;
                                return false;
                            }
                        }
                    }
                    model.stateOrder.push_back(state.symbol);
                    model.stateBySymbol.insert_or_assign(state.symbol, state);
                    break;
                }
                case OperationKind::kDpicImport:
                {
                    DpiImportDecl decl;
                    decl.symbol = std::string(op.symbolText());
                    decl.argsDirection = getAttribute<std::vector<std::string>>(op, "argsDirection").value_or(std::vector<std::string>{});
                    decl.argsWidth = getAttribute<std::vector<int64_t>>(op, "argsWidth").value_or(std::vector<int64_t>{});
                    decl.argsName = getAttribute<std::vector<std::string>>(op, "argsName").value_or(std::vector<std::string>{});
                    decl.argsSigned = getAttribute<std::vector<bool>>(op, "argsSigned").value_or(std::vector<bool>{});
                    decl.argsType = getAttribute<std::vector<std::string>>(op, "argsType").value_or(std::vector<std::string>{});
                    decl.hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);
                    decl.returnWidth = getAttribute<int64_t>(op, "returnWidth").value_or(64);
                    decl.returnSigned = getAttribute<bool>(op, "returnSigned").value_or(false);
                    decl.returnType = getAttribute<std::string>(op, "returnType").value_or(std::string("logic"));
                    decl.argsName.resize(decl.argsDirection.size());
                    decl.argsWidth.resize(decl.argsDirection.size(), 1);
                    decl.argsSigned.resize(decl.argsDirection.size(), false);
                    decl.argsType.resize(decl.argsDirection.size(), std::string("logic"));
                    for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                    {
                        if (decl.argsName[i].empty())
                        {
                            decl.argsName[i] = "arg" + std::to_string(i);
                        }
                        decl.argsWidth[i] = dpiEffectiveWidth(decl.argsType[i], decl.argsWidth[i]);
                    }
                    decl.returnWidth = dpiEffectiveWidth(decl.returnType, decl.returnWidth);
                    model.dpiImportBySymbol.insert_or_assign(decl.symbol, decl);
                    model.dpiImports.push_back(decl);
                    break;
                }
                default:
                    break;
                }
            }

            for (OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                if (op.kind() == OperationKind::kSystemTask)
                {
                    model.needsSystemTaskRuntime = true;
                    if (!validateSystemTask(graph, op, error))
                    {
                        return false;
                    }
                    continue;
                }
                if (op.kind() == OperationKind::kSystemFunction)
                {
                    const std::string name = getAttribute<std::string>(op, "name").value_or(std::string());
                    if (name == "fopen" || name == "ferror")
                    {
                        model.needsSystemTaskRuntime = true;
                    }
                    continue;
                }
                if (op.kind() == OperationKind::kDpicCall)
                {
                    if (!validateDpicCall(graph, op, model, error))
                    {
                        return false;
                    }
                    continue;
                }
                if (op.kind() == OperationKind::kRegisterWritePort ||
                    op.kind() == OperationKind::kLatchWritePort ||
                    op.kind() == OperationKind::kMemoryWritePort)
                {
                    auto registerStateShadow = [&](const std::string &stateSymbol, StateDecl::Kind kind) -> StateShadowDecl & {
                        auto existing = model.stateShadowBySymbol.find(stateSymbol);
                        if (existing != model.stateShadowBySymbol.end())
                        {
                            return existing->second;
                        }
                        const StateDecl &shadowState = model.stateBySymbol.at(stateSymbol);
                        StateShadowDecl shadow;
                        shadow.kind = kind;
                        shadow.symbol = stateSymbol;
                        shadow.emitIndex = model.stateShadows.size();
                        shadow.touchedIndex = model.stateShadowTouchedCount++;
                        if (isWideLogicWidth(shadowState.width))
                        {
                            shadow.wordCount = logicWordCount(shadowState.width);
                            shadow.dataIndex = model.stateShadowWideSlotCountsByWords[shadow.wordCount]++;
                        }
                        else
                        {
                            shadow.scalarKind = valueScalarSlotKindForWidth(shadowState.width);
                            shadow.dataIndex =
                                model.stateShadowScalarSlotCounts[static_cast<std::size_t>(shadow.scalarKind)]++;
                        }
                        model.stateShadows.push_back(shadow);
                        auto [it, inserted] = model.stateShadowBySymbol.emplace(stateSymbol, std::move(shadow));
                        (void)inserted;
                        return it->second;
                    };

                    WriteDecl write;
                    write.opId = opId;
                    write.kind = op.kind() == OperationKind::kRegisterWritePort
                                     ? StateDecl::Kind::Register
                                     : (op.kind() == OperationKind::kLatchWritePort ? StateDecl::Kind::Latch : StateDecl::Kind::Memory);
                    const char *symbolAttr = write.kind == StateDecl::Kind::Memory ? "memSymbol"
                                                                                    : (write.kind == StateDecl::Kind::Register ? "regSymbol" : "latchSymbol");
                    auto targetSymbol = getAttribute<std::string>(op, symbolAttr);
                    if (!targetSymbol)
                    {
                        error = std::string(symbolAttr) + " missing on write op: " + std::string(op.symbolText());
                        return false;
                    }
                    if (model.stateBySymbol.find(*targetSymbol) == model.stateBySymbol.end())
                    {
                        error = "write target declaration missing: " + *targetSymbol;
                        return false;
                    }
                    write.symbol = *targetSymbol;
                    const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                    if (write.kind == StateDecl::Kind::Register)
                    {
                        if (state.kind != StateDecl::Kind::Register)
                        {
                            error = "kRegisterWritePort target is not a register: " + write.symbol;
                            return false;
                        }
                        if (!validateRegisterWritePort(graph, op, state, error))
                        {
                            return false;
                        }
                    }
                    else if (write.kind == StateDecl::Kind::Memory)
                    {
                        if (state.kind != StateDecl::Kind::Memory)
                        {
                            error = "kMemoryWritePort target is not a memory: " + write.symbol;
                            return false;
                        }
                        if (!validateMemoryWritePort(graph, op, state, error))
                        {
                            return false;
                        }
                        const auto operands = op.operands();
                        const ValueId addrValue = operands[1];
                        const ValueId maskValue = operands[3];
                        if (isConstLogicAllZero(graph, maskValue, state.width))
                        {
                            write.memoryMaskMode = WriteDecl::MemoryMaskMode::kConstZero;
                        }
                        else if (isConstLogicAllOnes(graph, maskValue, state.width))
                        {
                            write.memoryMaskMode = WriteDecl::MemoryMaskMode::kConstAllOnes;
                        }
                        if (state.rowCount > 0)
                        {
                            const std::uint64_t rowCount = static_cast<std::uint64_t>(state.rowCount);
                            if (isPowerOfTwoU64(rowCount))
                            {
                                write.memoryAddrMode = WriteDecl::MemoryAddrMode::kPow2Wrap;
                                write.memoryRowMask = static_cast<std::size_t>(rowCount - 1u);
                            }
                            else
                            {
                                const int32_t addrWidth = graph.valueWidth(addrValue);
                                if (addrWidth > 0 && addrWidth < 64 &&
                                    (UINT64_C(1) << static_cast<std::size_t>(addrWidth)) <= rowCount)
                                {
                                    write.memoryAddrMode = WriteDecl::MemoryAddrMode::kInRange;
                                }
                            }
                        }
                    }
                    if (write.kind == StateDecl::Kind::Register || write.kind == StateDecl::Kind::Latch)
                    {
                        StateShadowDecl &shadow = registerStateShadow(write.symbol, write.kind);
                        write.shadowTouchedIndex = shadow.touchedIndex;
                        write.shadowDataIndex = shadow.dataIndex;
                        write.shadowScalarKind = shadow.scalarKind;
                        write.shadowWordCount = shadow.wordCount;
                        write.shadowIndex = shadow.emitIndex;
                    }
                    else
                    {
                        write.shadowTouchedIndex = model.memoryWriteTouchedCount++;
                        write.shadowAddrIndex = model.memoryWriteAddrCount++;
                        if (isWideLogicWidth(state.width))
                        {
                            write.shadowWordCount = logicWordCount(state.width);
                            write.shadowDataIndex = model.memoryWriteDataWideSlotCountsByWords[write.shadowWordCount]++;
                            if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kDynamic)
                            {
                                write.shadowMaskIndex =
                                    model.memoryWriteMaskWideSlotCountsByWords[write.shadowWordCount]++;
                            }
                        }
                        else
                        {
                            write.shadowScalarKind = valueScalarSlotKindForWidth(state.width);
                            write.shadowDataIndex =
                                model.memoryWriteDataScalarSlotCounts[static_cast<std::size_t>(write.shadowScalarKind)]++;
                            if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kDynamic)
                            {
                                write.shadowMaskIndex =
                                    model.memoryWriteMaskScalarSlotCounts[static_cast<std::size_t>(write.shadowScalarKind)]++;
                            }
                        }
                        write.shadowIndex = model.writes.size();
                    }
                    model.writeByOp.emplace(opId, write);
                    model.writes.push_back(write);
                }
            }

            auto registerEventSamples = [&](OperationId opId, const Operation &op) {
                EventOperandInfo info = collectEventOperands(op);
                if (info.values.empty())
                {
                    return;
                }
                EventSampleDecl samples;
                samples.values = std::move(info.values);
                samples.edges = std::move(info.edges);
                for (std::size_t i = 0; i < samples.values.size(); ++i)
                {
                    const ValueId value = samples.values[i];
                    if (model.eventEdgeFieldByValue.find(value) != model.eventEdgeFieldByValue.end())
                    {
                        continue;
                    }
                    const std::size_t slotIndex = model.eventEdgeSlotCount++;
                    const std::string fieldName = "event_edge_slots_[" + std::to_string(slotIndex) + "]";
                    model.eventEdgeFieldByValue.emplace(value, fieldName);
                    model.allEventValues.push_back(value);
                    if (model.inputFieldByValue.find(value) != model.inputFieldByValue.end())
                    {
                        model.inputEventValues.push_back(value);
                    }
                }
                if (op.kind() == OperationKind::kSystemTask && systemTaskRunsOnceAfterTimedTrigger(op))
                {
                    samples.completedFieldName =
                        "done_evt_" + opDebugName(graph, opId) + "_" + std::to_string(opId.index);
                    model.eventFieldDecls.push_back("    bool " + samples.completedFieldName + " = false;");
                }
                model.eventSamplesByOp.insert_or_assign(opId, std::move(samples));
            };

            for (OperationId opId : graph.operations())
            {
                const Operation op = graph.getOperation(opId);
                switch (op.kind())
                {
                case OperationKind::kRegisterWritePort:
                case OperationKind::kLatchWritePort:
                case OperationKind::kMemoryWritePort:
                case OperationKind::kSystemTask:
                case OperationKind::kDpicCall:
                    registerEventSamples(opId, op);
                    break;
                default:
                    break;
                }
            }
            model.activeIdBySupernode.assign(schedule.supernodeToOps.size(), kInvalidIndex);
            for (std::size_t topoIndex = 0; topoIndex < schedule.topoOrder.size(); ++topoIndex)
            {
                const uint32_t supernodeId = schedule.topoOrder[topoIndex];
                if (supernodeId < model.activeIdBySupernode.size())
                {
                    model.activeIdBySupernode[supernodeId] = topoIndex;
                }
            }

            for (ValueId valueId : graph.values())
            {
                if (valueId.index == 0 || valueId.index > schedule.valueFanout.size())
                {
                    continue;
                }
                const auto &succs = schedule.valueFanout[valueId.index - 1];
                if (!succs.empty())
                {
                    std::vector<uint32_t> activeIds;
                    activeIds.reserve(succs.size());
                    for (uint32_t supernodeId : succs)
                    {
                        if (supernodeId < model.activeIdBySupernode.size() &&
                            model.activeIdBySupernode[supernodeId] != kInvalidIndex)
                        {
                            activeIds.push_back(static_cast<uint32_t>(model.activeIdBySupernode[supernodeId]));
                        }
                    }
                    sortUniqueVector(activeIds);
                    if (!activeIds.empty())
                    {
                        model.boundaryFanoutByValue.emplace(valueId, std::move(activeIds));
                    }
                }
            }

            std::unordered_set<ValueId, ValueIdHash> persistentValues;
            persistentValues.reserve(graph.values().size());

            auto markPersistent = [&](ValueId valueId) {
                if (!valueId.valid() || model.inputFieldByValue.contains(valueId))
                {
                    return;
                }
                persistentValues.insert(valueId);
            };

            for (const auto &port : graph.outputPorts())
            {
                markPersistent(port.value);
            }
            for (const auto &port : graph.inoutPorts())
            {
                markPersistent(port.out);
                markPersistent(port.oe);
            }
            for (ValueId valueId : waveformValueIds)
            {
                markPersistent(valueId);
            }
            for (const auto &[valueId, _] : model.eventEdgeFieldByValue)
            {
                (void)_;
                markPersistent(valueId);
            }
            for (const auto &[valueId, _] : model.boundaryFanoutByValue)
            {
                (void)_;
                markPersistent(valueId);
            }

            for (ValueId valueId : graph.values())
            {
                if (model.inputFieldByValue.find(valueId) != model.inputFieldByValue.end())
                {
                    continue;
                }

                const OperationId defOpId = graph.valueDef(valueId);
                if (!defOpId.valid())
                {
                    markPersistent(valueId);
                }
                if (graph.valueType(valueId) != ValueType::Logic)
                {
                    markPersistent(valueId);
                }
                if (defOpId.valid())
                {
                    const OperationKind defKind = graph.getOperation(defOpId).kind();
                    if (defKind == OperationKind::kDpicCall || defKind == OperationKind::kSystemFunction)
                    {
                        markPersistent(valueId);
                    }
                }

                if (!persistentValues.contains(valueId))
                {
                    model.localValueNameByValue.emplace(valueId, localValueName(graph, valueId));
                    continue;
                }

                model.materializedValues.insert(valueId);
                switch (graph.valueType(valueId))
                {
                case ValueType::Real:
                {
                    const std::size_t slotIndex = model.valueRealSlotCount++;
                    model.valueRealSlotByValue.emplace(valueId, slotIndex);
                    model.valueFieldByValue.emplace(valueId, "value_real_slots_[" + std::to_string(slotIndex) + "]");
                    break;
                }
                case ValueType::String:
                {
                    const std::size_t slotIndex = model.valueStringSlotCount++;
                    model.valueStringSlotByValue.emplace(valueId, slotIndex);
                    model.valueFieldByValue.emplace(valueId, "value_string_slots_[" + std::to_string(slotIndex) + "]");
                    break;
                }
                case ValueType::Logic:
                default:
                {
                    if (isWideLogicValue(graph, valueId))
                    {
                        const std::size_t wordCount = logicWordCount(graph.valueWidth(valueId));
                        const std::size_t slotIndex = model.valueWideSlotCountsByWords[wordCount]++;
                        model.valueWideSlotByValue.emplace(
                            valueId, ValueWideSlotRef{.wordCount = wordCount, .index = slotIndex});
                        model.valueFieldByValue.emplace(
                            valueId, valueWideSlotFieldName(wordCount) + "[" + std::to_string(slotIndex) + "]");
                    }
                    else
                    {
                        const ValueSlotScalarKind slotKind = valueScalarSlotKindForWidth(graph.valueWidth(valueId));
                        const std::size_t slotIndex =
                            model.valueScalarSlotCounts[static_cast<std::size_t>(slotKind)]++;
                        model.valueScalarSlotByValue.emplace(
                            valueId, ValueScalarSlotRef{.kind = slotKind, .index = slotIndex});
                        model.valueFieldByValue.emplace(
                            valueId, valueScalarSlotFieldName(slotKind) + "[" + std::to_string(slotIndex) + "]");
                    }
                    break;
                }
                }
            }

            for (const auto &[stateSymbol, supernodes] : schedule.stateReadSupernodes)
            {
                auto &dst = model.stateHeadSupernodesBySymbol[stateSymbol];
                for (uint32_t supernodeId : supernodes)
                {
                    if (supernodeId < model.activeIdBySupernode.size() &&
                        model.activeIdBySupernode[supernodeId] != kInvalidIndex)
                    {
                        dst.push_back(static_cast<uint32_t>(model.activeIdBySupernode[supernodeId]));
                    }
                }
            }

            for (uint32_t supernodeId = 0; supernodeId < schedule.supernodeToOps.size(); ++supernodeId)
            {
                for (const auto opId : schedule.supernodeToOps[supernodeId])
                {
                    const Operation op = graph.getOperation(opId);
                    for (const ValueId operand : op.operands())
                    {
                        if (model.inputFieldByValue.find(operand) != model.inputFieldByValue.end())
                        {
                            if (supernodeId < model.activeIdBySupernode.size() &&
                                model.activeIdBySupernode[supernodeId] != kInvalidIndex)
                            {
                                model.inputHeadSupernodesByValue[operand].push_back(
                                    static_cast<uint32_t>(model.activeIdBySupernode[supernodeId]));
                            }
                        }
                    }
                }
            }

            for (auto &[value, supernodes] : model.inputHeadSupernodesByValue)
            {
                (void)value;
                sortUniqueVector(supernodes);
            }
            for (auto &[symbol, supernodes] : model.stateHeadSupernodesBySymbol)
            {
                (void)symbol;
                sortUniqueVector(supernodes);
            }

            for (const auto &decl : model.dpiImports)
            {
                std::vector<std::string> args;
                const std::size_t argCount = decl.argsDirection.size();
                for (std::size_t i = 0; i < argCount; ++i)
                {
                    const std::string argType =
                        dpiDeclArgCppType(i < decl.argsType.size() ? std::string_view(decl.argsType[i]) : std::string_view("logic"),
                                          i < decl.argsWidth.size() ? decl.argsWidth[i] : 64,
                                          i < decl.argsSigned.size() ? decl.argsSigned[i] : false,
                                          decl.argsDirection[i]);
                    const std::string &direction = decl.argsDirection[i];
                    (void)direction;
                    args.push_back(argType + " " + sanitizeIdentifier(i < decl.argsName.size() ? decl.argsName[i] : ("arg" + std::to_string(i))));
                }
                const std::string returnType =
                    decl.hasReturn ? dpiBaseCppType(decl.returnType, decl.returnWidth, decl.returnSigned) : "void";
                model.dpiDecls.push_back("extern \"C\" " + returnType + " " + sanitizeIdentifier(decl.symbol) + "(" +
                                         joinStrings(args, ", ") + ");");
            }
            return true;
        }

        std::string valueRef(const EmitModel &model, ValueId value)
        {
            if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
            {
                return it->second;
            }
            if (auto it = model.valueScalarSlotByValue.find(value); it != model.valueScalarSlotByValue.end())
            {
                return valueScalarSlotFieldName(it->second.kind) + "[" + std::to_string(it->second.index) + "]";
            }
            if (auto it = model.valueWideSlotByValue.find(value); it != model.valueWideSlotByValue.end())
            {
                return valueWideSlotFieldName(it->second.wordCount) + "[" + std::to_string(it->second.index) + "]";
            }
            if (auto it = model.valueRealSlotByValue.find(value); it != model.valueRealSlotByValue.end())
            {
                return "value_real_slots_[" + std::to_string(it->second) + "]";
            }
            if (auto it = model.valueStringSlotByValue.find(value); it != model.valueStringSlotByValue.end())
            {
                return "value_string_slots_[" + std::to_string(it->second) + "]";
            }
            if (auto it = model.valueFieldByValue.find(value); it != model.valueFieldByValue.end())
            {
                return it->second;
            }
            if (auto it = model.localValueNameByValue.find(value); it != model.localValueNameByValue.end())
            {
                return it->second;
            }
            return "/*missing_value_ref*/";
        }

        std::string valueDebugText(const Graph &graph, const EmitModel &model, ValueId value)
        {
            const Value info = graph.getValue(value);
            std::ostringstream out;
            out << "value ";
            if (!info.symbolText().empty())
            {
                out << sanitizeCommentText(info.symbolText());
            }
            else
            {
                out << "<unnamed>";
            }
            out << " -> " << valueRef(model, value);
            out << " [value_id=" << value.index << ":" << value.generation << "]";
            return out.str();
        }

        void emitValueAssignmentComment(std::ostream &stream,
                                        const Graph &graph,
                                        const EmitModel &model,
                                        ValueId value,
                                        std::string_view indent)
        {
            stream << indent << "// " << valueDebugText(graph, model, value) << "\n";
        }

        std::string formatSrcLocText(const std::optional<wolvrix::lib::grh::SrcLoc> &srcLoc)
        {
            if (!srcLoc)
            {
                return "<none>";
            }

            std::ostringstream out;
            if (!srcLoc->file.empty())
            {
                out << srcLoc->file;
            }
            else
            {
                out << "<unknown-file>";
            }
            if (srcLoc->line != 0)
            {
                out << ":" << srcLoc->line;
                if (srcLoc->column != 0)
                {
                    out << ":" << srcLoc->column;
                }
            }
            if (!srcLoc->origin.empty())
            {
                out << " origin=" << srcLoc->origin;
            }
            if (!srcLoc->pass.empty())
            {
                out << " pass=" << srcLoc->pass;
            }
            if (!srcLoc->note.empty())
            {
                out << " note=" << sanitizeCommentText(srcLoc->note);
            }
            return out.str();
        }

        std::string formatValueIdText(ValueId value)
        {
            std::ostringstream out;
            out << value.index << ":" << value.generation;
            return out.str();
        }

        std::string formatOperationIdText(OperationId op)
        {
            std::ostringstream out;
            out << op.index << ":" << op.generation;
            return out.str();
        }

        std::string valueSymbolOrUnnamed(const Graph &graph, ValueId value)
        {
            const std::string_view symbol = graph.symbolText(graph.valueSymbol(value));
            if (!symbol.empty())
            {
                return std::string(symbol);
            }
            return "<unnamed>";
        }

        std::string opSymbolOrUnnamed(const Graph &graph, OperationId opId)
        {
            const Operation op = graph.getOperation(opId);
            if (!op.symbolText().empty())
            {
                return std::string(op.symbolText());
            }
            return "<unnamed-op>";
        }

        bool isReadRootOpKind(OperationKind kind) noexcept
        {
            return kind == OperationKind::kRegisterReadPort ||
                   kind == OperationKind::kLatchReadPort ||
                   kind == OperationKind::kMemoryReadPort;
        }

        std::string readRootText(const Graph &graph, const Operation &op)
        {
            std::ostringstream out;
            switch (op.kind())
            {
            case OperationKind::kRegisterReadPort:
                out << "read.register";
                if (auto regSymbol = getAttribute<std::string>(op, "regSymbol"))
                {
                    out << " state=" << *regSymbol;
                }
                break;
            case OperationKind::kLatchReadPort:
                out << "read.latch";
                if (auto latchSymbol = getAttribute<std::string>(op, "latchSymbol"))
                {
                    out << " state=" << *latchSymbol;
                }
                break;
            case OperationKind::kMemoryReadPort:
                out << "read.memory";
                if (auto memSymbol = getAttribute<std::string>(op, "memSymbol"))
                {
                    out << " state=" << *memSymbol;
                }
                break;
            default:
                out << "op kind=" << toString(op.kind());
                break;
            }
            out << " op=" << opSymbolOrUnnamed(graph, op.id());
            out << " op_id=" << formatOperationIdText(op.id());
            return out.str();
        }

        std::string dependencyOperandText(const Graph &graph, const EmitModel &model, ValueId operand)
        {
            std::ostringstream out;
            out << valueSymbolOrUnnamed(graph, operand);
            out << " value_id=" << formatValueIdText(operand);
            if (model.inputFieldByValue.contains(operand))
            {
                out << " kind=input";
                return out.str();
            }

            const OperationId defOpId = graph.valueDef(operand);
            if (!defOpId.valid())
            {
                out << " kind=nodef";
                return out.str();
            }

            const Operation defOp = graph.getOperation(defOpId);
            if (isReadRootOpKind(defOp.kind()))
            {
                out << " kind=" << readRootText(graph, defOp);
                return out.str();
            }

            out << " kind=internal";
            out << " def_kind=" << toString(defOp.kind());
            out << " def_op=" << opSymbolOrUnnamed(graph, defOpId);
            out << " def_op_id=" << formatOperationIdText(defOpId);
            return out.str();
        }

        using DependencyRootCache = std::unordered_map<ValueId, std::vector<std::string>, ValueIdHash>;

        std::vector<std::string> collectCombinationalRootsForValue(const Graph &graph,
                                                                   const EmitModel &model,
                                                                   ValueId value,
                                                                   DependencyRootCache &cache,
                                                                   std::unordered_set<ValueId, ValueIdHash> &visiting)
        {
            if (const auto it = cache.find(value); it != cache.end())
            {
                return it->second;
            }

            std::vector<std::string> roots;
            if (!visiting.insert(value).second)
            {
                return roots;
            }

            if (model.inputFieldByValue.contains(value))
            {
                roots.push_back("input " + valueSymbolOrUnnamed(graph, value) + " value_id=" + formatValueIdText(value));
            }
            else
            {
                const OperationId defOpId = graph.valueDef(value);
                if (defOpId.valid())
                {
                    const Operation defOp = graph.getOperation(defOpId);
                    if (isReadRootOpKind(defOp.kind()))
                    {
                        roots.push_back(readRootText(graph, defOp));
                    }
                    else if (defOp.kind() != OperationKind::kConstant)
                    {
                        for (const ValueId operand : defOp.operands())
                        {
                            std::vector<std::string> operandRoots =
                                collectCombinationalRootsForValue(graph, model, operand, cache, visiting);
                            roots.insert(roots.end(), operandRoots.begin(), operandRoots.end());
                        }
                    }
                }
            }

            visiting.erase(value);
            sortUniqueVector(roots);
            cache.emplace(value, roots);
            return roots;
        }

        bool emitDeclaredValueIndexFile(const Graph &graph,
                                        const EmitModel &model,
                                        const ScheduleRefs &schedule,
                                        const std::vector<ScheduleBatch> &scheduleBatches,
                                        const std::vector<std::filesystem::path> &schedPaths,
                                        const std::filesystem::path &indexPath,
                                        std::string &error)
        {
            std::unordered_map<OperationId, uint32_t, OperationIdHash> supernodeByOp;
            supernodeByOp.reserve(graph.operations().size());
            for (uint32_t supernodeId = 0; supernodeId < schedule.supernodeToOps.size(); ++supernodeId)
            {
                for (OperationId opId : schedule.supernodeToOps[supernodeId])
                {
                    supernodeByOp.emplace(opId, supernodeId);
                }
            }

            std::vector<std::size_t> batchBySupernode(schedule.supernodeToOps.size(), kInvalidIndex);
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                for (uint32_t supernodeId : scheduleBatches[batchIndex].supernodeIds)
                {
                    if (supernodeId < batchBySupernode.size())
                    {
                        batchBySupernode[supernodeId] = batchIndex;
                    }
                }
            }

            if (auto dirError = ensureOutputDirectory(indexPath))
            {
                error = *dirError;
                return false;
            }

            std::ofstream stream(indexPath, std::ios::out | std::ios::trunc);
            if (!stream.is_open())
            {
                error = "failed to open output file";
                return false;
            }

            stream << "# grhsim declared value index\n";
            stream << "# graph=" << graph.symbol() << "\n";
            stream << "# schedule_supernodes=" << schedule.supernodeToOps.size() << "\n";
            stream << "# schedule_batches=" << scheduleBatches.size() << "\n";

            std::unordered_set<std::string> seenSymbols;
            seenSymbols.reserve(graph.declaredSymbols().size());
            DependencyRootCache rootCache;
            rootCache.reserve(graph.values().size());

            std::size_t emittedValueCount = 0;
            std::size_t skippedDeclaredNonValueCount = 0;
            for (const auto sym : graph.declaredSymbols())
            {
                const std::string symbol(graph.symbolText(sym));
                if (symbol.empty() || !seenSymbols.insert(symbol).second)
                {
                    continue;
                }

                const ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    ++skippedDeclaredNonValueCount;
                    continue;
                }

                const Value value = graph.getValue(valueId);
                const OperationId defOpId = graph.valueDef(valueId);
                const bool hasDefOp = defOpId.valid();

                std::string valueRefText = valueRef(model, valueId);
                uint32_t supernodeId = 0;
                bool hasSupernode = false;
                std::size_t batchIndex = kInvalidIndex;
                std::string schedFileName = "<none>";
                if (hasDefOp)
                {
                    if (const auto it = supernodeByOp.find(defOpId); it != supernodeByOp.end())
                    {
                        hasSupernode = true;
                        supernodeId = it->second;
                        if (supernodeId < batchBySupernode.size())
                        {
                            batchIndex = batchBySupernode[supernodeId];
                            if (batchIndex != kInvalidIndex && batchIndex < schedPaths.size())
                            {
                                schedFileName = schedPaths[batchIndex].filename().string();
                            }
                        }
                    }
                }

                std::vector<std::string> directOperands;
                if (hasDefOp)
                {
                    const Operation defOp = graph.getOperation(defOpId);
                    directOperands.reserve(defOp.operands().size());
                    for (ValueId operand : defOp.operands())
                    {
                        directOperands.push_back(dependencyOperandText(graph, model, operand));
                    }
                }

                std::unordered_set<ValueId, ValueIdHash> visiting;
                std::vector<std::string> combRoots =
                    collectCombinationalRootsForValue(graph, model, valueId, rootCache, visiting);

                stream << "\n";
                stream << "symbol=" << symbol << "\n";
                stream << "value_id=" << formatValueIdText(valueId) << "\n";
                stream << "value_ref=" << valueRefText << "\n";
                stream << "value_type=" << toString(value.type()) << "\n";
                stream << "value_width=" << value.width() << "\n";
                stream << "value_signed=" << (value.isSigned() ? 1 : 0) << "\n";
                stream << "value_src_loc=" << formatSrcLocText(value.srcLoc()) << "\n";
                stream << "value_is_input=" << (model.inputFieldByValue.contains(valueId) ? 1 : 0) << "\n";

                if (hasDefOp)
                {
                    const Operation defOp = graph.getOperation(defOpId);
                    stream << "def_op=" << opSymbolOrUnnamed(graph, defOpId) << "\n";
                    stream << "def_op_id=" << formatOperationIdText(defOpId) << "\n";
                    stream << "def_kind=" << toString(defOp.kind()) << "\n";
                    stream << "def_src_loc=" << formatSrcLocText(defOp.srcLoc()) << "\n";
                }
                else
                {
                    stream << "def_op=<none>\n";
                    stream << "def_op_id=<none>\n";
                    stream << "def_kind=<none>\n";
                    stream << "def_src_loc=<none>\n";
                }

                if (hasSupernode)
                {
                    stream << "supernode=" << supernodeId << "\n";
                    stream << "sched_batch=" << batchIndex << "\n";
                    stream << "sched_cpp=" << schedFileName << "\n";
                }
                else
                {
                    stream << "supernode=<none>\n";
                    stream << "sched_batch=<none>\n";
                    stream << "sched_cpp=<none>\n";
                }

                stream << "direct_operand_count=" << directOperands.size() << "\n";
                for (std::size_t i = 0; i < directOperands.size(); ++i)
                {
                    stream << "direct_operand[" << i << "]=" << directOperands[i] << "\n";
                }

                stream << "comb_root_count=" << combRoots.size() << "\n";
                for (std::size_t i = 0; i < combRoots.size(); ++i)
                {
                    stream << "comb_root[" << i << "]=" << combRoots[i] << "\n";
                }
                stream << "---\n";
                ++emittedValueCount;
            }

            stream << "\n";
            stream << "# emitted_declared_values=" << emittedValueCount << "\n";
            stream << "# skipped_declared_non_value_symbols=" << skippedDeclaredNonValueCount << "\n";

            stream.flush();
            stream.close();
            if (!stream)
            {
                error = "failed to write output file";
                return false;
            }
            return true;
        }

        bool collectDeclaredSymbolWaveformSignals(const Graph &graph,
                                                  const EmitModel &model,
                                                  std::vector<WaveformSignalDecl> &outSignals)
        {
            outSignals.clear();
            std::unordered_set<std::string> seenSymbols;
            seenSymbols.reserve(graph.declaredSymbols().size());

            for (const auto sym : graph.declaredSymbols())
            {
                const std::string symbol(graph.symbolText(sym));
                if (symbol.empty() || !seenSymbols.insert(symbol).second)
                {
                    continue;
                }

                if (const ValueId valueId = graph.findValue(sym); valueId.valid())
                {
                    const std::string ref = valueRef(model, valueId);
                    if (ref == "/*missing_value_ref*/")
                    {
                        continue;
                    }
                    WaveformSignalDecl signal;
                    signal.sourceKind = WaveformSignalDecl::SourceKind::kValue;
                    signal.symbol = symbol;
                    signal.escapedSymbol = escapeCppString(symbol);
                    signal.type = graph.valueType(valueId);
                    signal.width = graph.valueWidth(valueId);
                    signal.isSigned = graph.valueSigned(valueId);
                    signal.value = valueId;
                    outSignals.push_back(std::move(signal));
                    continue;
                }

                auto stateIt = model.stateBySymbol.find(symbol);
                if (stateIt == model.stateBySymbol.end())
                {
                    continue;
                }
                const StateDecl &state = stateIt->second;
                if (state.kind == StateDecl::Kind::Memory)
                {
                    continue;
                }
                WaveformSignalDecl signal;
                signal.sourceKind = WaveformSignalDecl::SourceKind::kState;
                signal.symbol = symbol;
                signal.escapedSymbol = escapeCppString(symbol);
                signal.type = ValueType::Logic;
                signal.width = state.width;
                signal.isSigned = state.isSigned;
                signal.stateSymbol = state.symbol;
                outSignals.push_back(std::move(signal));
            }

            return true;
        }

        std::unordered_set<ValueId, ValueIdHash> collectDeclaredSymbolWaveformValueIds(const Graph &graph)
        {
            std::unordered_set<ValueId, ValueIdHash> valueIds;
            valueIds.reserve(graph.declaredSymbols().size());
            std::unordered_set<std::string> seenSymbols;
            seenSymbols.reserve(graph.declaredSymbols().size());

            for (const auto sym : graph.declaredSymbols())
            {
                const std::string symbol(graph.symbolText(sym));
                if (symbol.empty() || !seenSymbols.insert(symbol).second)
                {
                    continue;
                }
                const ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    continue;
                }
                valueIds.insert(valueId);
            }

            return valueIds;
        }

        std::pair<std::size_t, std::size_t> waveformSignalRangeForBatch(std::size_t signalCount,
                                                                         std::size_t batchCount,
                                                                         std::size_t batchIndex) noexcept
        {
            if (signalCount == 0 || batchCount == 0 || batchIndex >= batchCount)
            {
                return {0, 0};
            }
            const std::size_t begin = (signalCount * batchIndex) / batchCount;
            const std::size_t end = (signalCount * (batchIndex + 1)) / batchCount;
            return {begin, end};
        }

        void emitWaveformSignalWrite(std::ostream &stream,
                                     const EmitModel &model,
                                     const WaveformSignalDecl &signal,
                                     std::size_t signalIndex,
                                     std::string_view indent)
        {
            std::string expr;
            if (signal.sourceKind == WaveformSignalDecl::SourceKind::kState)
            {
                expr = stateRef(model.stateBySymbol.at(signal.stateSymbol));
            }
            else
            {
                expr = valueRef(model, signal.value);
            }

            switch (signal.type)
            {
            case ValueType::Real:
                stream << indent << "waveform_writer_->emit_real(waveform_handles_[" << signalIndex << "], " << expr
                       << ");\n";
                break;
            case ValueType::String:
                stream << indent << "waveform_writer_->emit_string(waveform_handles_[" << signalIndex << "], " << expr
                       << ");\n";
                break;
            case ValueType::Logic:
            default:
                if (isWideLogicWidth(signal.width))
                {
                    stream << indent << "waveform_writer_->emit_logic_words(waveform_handles_[" << signalIndex << "], "
                           << (signal.width <= 0 ? 1 : signal.width) << "u, " << expr << ");\n";
                }
                else
                {
                    stream << indent << "waveform_writer_->emit_logic_u64(waveform_handles_[" << signalIndex << "], "
                           << (signal.width <= 0 ? 1 : signal.width) << "u, static_cast<std::uint64_t>(" << expr
                           << "));\n";
                }
                break;
            }
        }

        void emitWaveformSignalRegistrationBatch(std::ostream &stream,
                                                 std::span<const WaveformSignalDecl> signals,
                                                 std::string_view indent)
        {
            if (signals.empty())
            {
                return;
            }

            stream << indent << "enum class waveform_signal_kind : std::uint8_t {\n";
            stream << indent << "    logic,\n";
            stream << indent << "    real,\n";
            stream << indent << "    string,\n";
            stream << indent << "};\n";
            stream << indent << "struct waveform_signal_decl {\n";
            stream << indent << "    const char *name;\n";
            stream << indent << "    std::uint32_t width;\n";
            stream << indent << "    waveform_signal_kind kind;\n";
            stream << indent << "};\n";
            stream << indent << "static constexpr waveform_signal_decl kWaveformSignals[] = {";
            for (std::size_t i = 0; i < signals.size(); ++i)
            {
                const auto &signal = signals[i];
                if ((i % 4u) == 0u)
                {
                    stream << "\n" << indent << "    ";
                }
                stream << "{\"" << signal.escapedSymbol << "\", " << (signal.width <= 0 ? 1 : signal.width) << "u, ";
                switch (signal.type)
                {
                case ValueType::Real:
                    stream << "waveform_signal_kind::real}";
                    break;
                case ValueType::String:
                    stream << "waveform_signal_kind::string}";
                    break;
                case ValueType::Logic:
                default:
                    stream << "waveform_signal_kind::logic}";
                    break;
                }
                if (i + 1u != signals.size())
                {
                    stream << ", ";
                }
            }
            stream << "\n" << indent << "};\n";
            stream << indent << "for (const auto &signal : kWaveformSignals) {\n";
            stream << indent << "    switch (signal.kind) {\n";
            stream << indent << "    case waveform_signal_kind::real:\n";
            stream << indent << "        waveform_handles_.push_back(writer.register_real(signal.name));\n";
            stream << indent << "        break;\n";
            stream << indent << "    case waveform_signal_kind::string:\n";
            stream << indent << "        waveform_handles_.push_back(writer.register_string(signal.name));\n";
            stream << indent << "        break;\n";
            stream << indent << "    case waveform_signal_kind::logic:\n";
            stream << indent << "    default:\n";
            stream << indent << "        waveform_handles_.push_back(writer.register_logic(signal.name, signal.width));\n";
            stream << indent << "        break;\n";
            stream << indent << "    }\n";
            stream << indent << "}\n";
        }

        std::string stateShadowTouchedRef(const StateShadowDecl &shadow)
        {
            return "state_shadow_touched_slots_[" + std::to_string(shadow.touchedIndex) + "]";
        }

        std::string stateShadowDataRef(const StateShadowDecl &shadow, const StateDecl &state)
        {
            return logicSlotRefExpr("state_shadow_",
                                    "state_shadow_",
                                    shadow.scalarKind,
                                    shadow.wordCount,
                                    shadow.dataIndex,
                                    isWideLogicWidth(state.width));
        }

        std::string memoryWriteTouchedRef(const WriteDecl &write)
        {
            return "memory_write_touched_slots_[" + std::to_string(write.shadowTouchedIndex) + "]";
        }

        std::string memoryWriteAddrRef(const WriteDecl &write)
        {
            return "memory_write_addr_slots_[" + std::to_string(write.shadowAddrIndex) + "]";
        }

        std::string memoryWriteDataRef(const WriteDecl &write, const StateDecl &state)
        {
            return logicSlotRefExpr("memory_write_data_",
                                    "memory_write_data_",
                                    write.shadowScalarKind,
                                    write.shadowWordCount,
                                    write.shadowDataIndex,
                                    isWideLogicWidth(state.width));
        }

        std::string memoryWriteMaskRef(const WriteDecl &write, const StateDecl &state)
        {
            return logicSlotRefExpr("memory_write_mask_",
                                    "memory_write_mask_",
                                    write.shadowScalarKind,
                                    write.shadowWordCount,
                                    write.shadowMaskIndex,
                                    isWideLogicWidth(state.width));
        }

        std::optional<std::string> stableValueExpr(const Graph &graph, const EmitModel &model, ValueId value)
        {
            const OperationId defOpId = graph.valueDef(value);
            if (defOpId.valid())
            {
                const Operation defOp = graph.getOperation(defOpId);
                if (defOp.kind() == OperationKind::kConstant)
                {
                    if (auto expr = constantExpr(graph, defOp, value))
                    {
                        return expr;
                    }
                }
            }
            if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
            {
                return it->second;
            }
            if (auto it = model.valueFieldByValue.find(value); it != model.valueFieldByValue.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        std::string truthyLogicValueExpr(const Graph &graph, const EmitModel &model, ValueId value)
        {
            const std::string ref = valueRef(model, value);
            if (isWideLogicValue(graph, value))
            {
                return "grhsim_any_bits_words(" + ref + ", " + std::to_string(graph.valueWidth(value)) + ")";
            }
            return "(" + ref + ") != 0";
        }

        std::string stableTruthyLogicValueExpr(const Graph &graph, const EmitModel &model, ValueId value)
        {
            const std::string ref =
                stableValueExpr(graph, model, value).value_or(std::string("/*missing_stable_truthy_value*/"));
            if (isWideLogicValue(graph, value))
            {
                return "grhsim_any_bits_words(" + ref + ", " + std::to_string(graph.valueWidth(value)) + ")";
            }
            return "(" + ref + ") != 0";
        }

        std::string stableSystemTaskArgExpr(const Graph &graph,
                                            const EmitModel &model,
                                            ValueId value)
        {
            const std::string ref =
                stableValueExpr(graph, model, value).value_or(std::string("/*missing_stable_system_task_arg*/"));
            const std::string signedText = graph.valueSigned(value) ? "true" : "false";
            switch (graph.valueType(value))
            {
            case ValueType::String:
                return "grhsim_make_task_arg(" + ref + ")";
            case ValueType::Real:
                return "grhsim_make_task_arg(static_cast<double>(" + ref + "))";
            case ValueType::Logic:
            default:
                break;
            }
            if (isWideLogicValue(graph, value))
            {
                std::ostringstream out;
                out << "grhsim_make_task_arg(" << ref << ", "
                    << graph.valueWidth(value) << ", "
                    << signedText << ")";
                return out.str();
            }
            std::ostringstream out;
            out << "grhsim_make_task_arg(static_cast<std::uint64_t>(" << ref << "), "
                << graph.valueWidth(value) << ", "
                << signedText << ")";
            return out.str();
        }

        std::string boolLiteral(bool value)
        {
            return value ? "true" : "false";
        }

        std::string wordsArrayTypeForWidth(int32_t width)
        {
            std::ostringstream out;
            out << "std::array<std::uint64_t, " << logicWordCount(width) << ">";
            return out.str();
        }

        std::string wordsArrayLambdaExprForWidth(int32_t width, const std::string &body)
        {
            const std::string arrayType = wordsArrayTypeForWidth(width);
            return "([&]() -> " + arrayType + " { " + arrayType + " out{}; " + body + " return out; }())";
        }

        std::size_t estimateChangedValueEffectLines(const ActivityScheduleValueFanout &valueFanout,
                                                    ValueId resultValue) noexcept
        {
            if (!resultValue.valid() || resultValue.index == 0 || resultValue.index > valueFanout.size())
            {
                return 0;
            }
            return valueFanout[resultValue.index - 1].size();
        }

        std::size_t estimateOperationEmitLines(const Graph &graph,
                                               const ActivityScheduleValueFanout &valueFanout,
                                               const Operation &op)
        {
            std::size_t changedValueEffectLines = 0;
            if (!op.results().empty())
            {
                changedValueEffectLines = estimateChangedValueEffectLines(valueFanout, op.results().front());
            }
            switch (op.kind())
            {
            case OperationKind::kConstant:
            case OperationKind::kRegisterReadPort:
            case OperationKind::kLatchReadPort:
                return 4 + changedValueEffectLines;
            case OperationKind::kMemoryReadPort:
                return 6 + changedValueEffectLines;
            case OperationKind::kRegisterWritePort:
            case OperationKind::kLatchWritePort:
            case OperationKind::kMemoryWritePort:
                return 7;
            case OperationKind::kSystemTask:
            case OperationKind::kDpicCall:
                return 10;
            case OperationKind::kAssign:
            case OperationKind::kAdd:
            case OperationKind::kSub:
            case OperationKind::kMul:
            case OperationKind::kDiv:
            case OperationKind::kMod:
            case OperationKind::kAnd:
            case OperationKind::kOr:
            case OperationKind::kXor:
            case OperationKind::kXnor:
            case OperationKind::kNot:
            case OperationKind::kEq:
            case OperationKind::kNe:
            case OperationKind::kCaseEq:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardEq:
            case OperationKind::kWildcardNe:
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            case OperationKind::kLogicAnd:
            case OperationKind::kLogicOr:
            case OperationKind::kLogicNot:
            case OperationKind::kReduceAnd:
            case OperationKind::kReduceNand:
            case OperationKind::kReduceOr:
            case OperationKind::kReduceNor:
            case OperationKind::kReduceXor:
            case OperationKind::kReduceXnor:
            case OperationKind::kShl:
            case OperationKind::kLShr:
            case OperationKind::kAShr:
            case OperationKind::kMux:
            case OperationKind::kConcat:
            case OperationKind::kReplicate:
            case OperationKind::kSliceStatic:
            case OperationKind::kSliceDynamic:
            case OperationKind::kSliceArray:
            case OperationKind::kSystemFunction:
                return (opNeedsWordLogicEmit(graph, op) ? 8 : 5) + changedValueEffectLines;
            default:
                return 2;
            }
        }

        std::size_t estimateSupernodeEmitLines(const Graph &graph,
                                               const ActivityScheduleValueFanout &valueFanout,
                                               const ActivityScheduleSupernodeToOps &supernodeToOps,
                                               uint32_t supernodeId)
        {
            std::size_t total = 8;
            if (supernodeId >= supernodeToOps.size())
            {
                return total;
            }
            for (OperationId opId : supernodeToOps[supernodeId])
            {
                total += estimateOperationEmitLines(graph, valueFanout, graph.getOperation(opId));
            }
            return total;
        }

        std::vector<ScheduleBatch> buildScheduleBatches(const Graph &graph,
                                                        const ScheduleRefs &schedule,
                                                        std::size_t batchMaxOps,
                                                        std::size_t batchMaxEstimatedLines)
        {
            (void)batchMaxOps;
            (void)batchMaxEstimatedLines;
            std::vector<ScheduleBatch> batches;
            ScheduleBatch current;
            auto flushCurrent = [&]()
            {
                if (current.supernodeIds.empty())
                {
                    return;
                }
                current.index = batches.size();
                batches.push_back(std::move(current));
                current = ScheduleBatch{};
            };

            for (std::size_t topoIndex = 0; topoIndex < schedule.topoOrder.size(); ++topoIndex)
            {
                const uint32_t supernodeId = schedule.topoOrder[topoIndex];
                const std::size_t activeWordIndex =
                    topoIndex / kActiveFlagBitsPerWord;
                const std::size_t supernodeOps =
                    supernodeId < schedule.supernodeToOps.size() ? schedule.supernodeToOps[supernodeId].size() : 0;
                const std::size_t supernodeLines =
                    estimateSupernodeEmitLines(graph, schedule.valueFanout, schedule.supernodeToOps, supernodeId);
                if (!current.supernodeIds.empty() && current.activeFlagWordIndex != activeWordIndex)
                {
                    flushCurrent();
                }
                if (current.supernodeIds.empty())
                {
                    current.activeFlagWordIndex = activeWordIndex;
                }
                current.supernodeIds.push_back(supernodeId);
                current.opCount += supernodeOps;
                current.estimatedLines += supernodeLines;
            }
            flushCurrent();
            if (batches.empty())
            {
                batches.push_back(ScheduleBatch{});
            }
            return batches;
        }

        bool opNeedsWordLogicEmit(const Graph &graph, const Operation &op) noexcept
        {
            for (ValueId value : op.operands())
            {
                if (isWideLogicValue(graph, value))
                {
                    return true;
                }
            }
            for (ValueId value : op.results())
            {
                if (isWideLogicValue(graph, value))
                {
                    return true;
                }
            }
            return false;
        }

        std::string castWordsExprForValue(const Graph &graph,
                                          ValueId value,
                                          const std::string &expr,
                                          int32_t destWidth)
        {
            const int32_t srcWidth = graph.valueWidth(value);
            if (graph.valueType(value) == ValueType::Logic &&
                isWideLogicValue(graph, value) &&
                srcWidth == destWidth)
            {
                return expr;
            }

            std::ostringstream body;
            if (graph.valueType(value) == ValueType::Logic && isWideLogicValue(graph, value))
            {
                body << "const auto src = " << expr << "; ";
                body << "grhsim_cast_words(src.data(), src.size(), "
                     << srcWidth << ", "
                     << destWidth << ", "
                     << boolLiteral(graph.valueSigned(value)) << ", out.data(), out.size()); ";
            }
            else
            {
                body << "grhsim_cast_scalar_words(static_cast<std::uint64_t>(" << expr << "), "
                     << srcWidth << ", "
                     << destWidth << ", "
                     << boolLiteral(graph.valueSigned(value)) << ", out.data(), out.size()); ";
            }
            return wordsArrayLambdaExprForWidth(destWidth, body.str());
        }

        std::string wordsExprForValue(const Graph &graph,
                                      const EmitModel &model,
                                      ValueId value,
                                      int32_t destWidth)
        {
            return castWordsExprForValue(graph, value, valueRef(model, value), destWidth);
        }

        std::string sliceWordsExpr(const std::string &srcExpr,
                                   const std::string &startExpr,
                                   int32_t resultWidth)
        {
            std::ostringstream body;
            body << "const auto src = " << srcExpr << "; ";
            body << "grhsim_slice_words(src.data(), src.size(), " << startExpr << ", " << resultWidth
                 << ", out.data(), out.size()); ";
            return wordsArrayLambdaExprForWidth(resultWidth, body.str());
        }

        std::string unaryWordsBufferOpExpr(const std::string &valueExpr,
                                           int32_t resultWidth,
                                           std::string_view helperName)
        {
            std::ostringstream body;
            body << "const auto value = " << valueExpr << "; ";
            body << "out = " << helperName << "(value, " << resultWidth << "); ";
            return wordsArrayLambdaExprForWidth(resultWidth, body.str());
        }

        std::string binaryWordsBufferOpExpr(const std::string &lhsExpr,
                                            const std::string &rhsExpr,
                                            int32_t resultWidth,
                                            std::string_view helperName)
        {
            std::ostringstream body;
            body << "const auto lhs = " << lhsExpr << "; ";
            body << "const auto rhs = " << rhsExpr << "; ";
            body << "out = " << helperName << "(lhs, rhs, " << resultWidth << "); ";
            return wordsArrayLambdaExprForWidth(resultWidth, body.str());
        }

        std::string shiftWordsBufferOpExpr(const std::string &valueExpr,
                                           const std::string &shiftExpr,
                                           int32_t resultWidth,
                                           std::string_view helperName)
        {
            std::ostringstream body;
            body << "const auto value = " << valueExpr << "; ";
            body << "out = " << helperName << "(value, grhsim_index_words(" << shiftExpr << ", "
                 << resultWidth << "), " << resultWidth << "); ";
            return wordsArrayLambdaExprForWidth(resultWidth, body.str());
        }

        std::string unaryWordsBoolExpr(const std::string &valueExpr,
                                       int32_t width,
                                       std::string_view helperName)
        {
            std::ostringstream out;
            out << "([&]() -> bool { const auto value = " << valueExpr << "; return " << helperName
                << "(value.data(), value.size(), " << width << "); }())";
            return out.str();
        }

        std::string binaryWordsCompareExpr(const std::string &lhsExpr,
                                           const std::string &rhsExpr,
                                           std::string_view helperName)
        {
            std::ostringstream out;
            out << "([&]() -> int { const auto lhs = " << lhsExpr << "; const auto rhs = " << rhsExpr
                << "; return " << helperName << "(lhs.data(), lhs.size(), rhs.data(), rhs.size()); }())";
            return out.str();
        }

        std::string binaryWordsSignedCompareExpr(const std::string &lhsExpr,
                                                 const std::string &rhsExpr,
                                                 int32_t width)
        {
            std::ostringstream out;
            out << "([&]() -> int { const auto lhs = " << lhsExpr << "; const auto rhs = " << rhsExpr
                << "; return grhsim_compare_signed_words(lhs.data(), lhs.size(), rhs.data(), rhs.size(), "
                << width << "); }())";
            return out.str();
        }

        void emitLogicAssignFromWordsExpr(std::ostream &stream,
                                          const Graph &graph,
                                          const EmitModel &model,
                                          ValueId resultValue,
                                          const std::string &wordsExpr)
        {
            const std::string lhs = valueRef(model, resultValue);
            const int32_t resultWidth = graph.valueWidth(resultValue);
            const bool materialized = isMaterializedValue(model, resultValue);
            const bool needChangeDetect = valueNeedsTrackedChange(model, resultValue);
            if (!materialized)
            {
                emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                if (isWideLogicValue(graph, resultValue))
                {
                    stream << "        const auto " << lhs << " = " << wordsExpr << ";\n";
                }
                else
                {
                    stream << "        const auto " << lhs << " = [&]() -> " << cppTypeForValue(graph, resultValue)
                           << " { const auto next_words = " << wordsExpr
                           << "; return static_cast<" << cppTypeForValue(graph, resultValue)
                           << ">(grhsim_trunc_u64(next_words[0], " << resultWidth << ")); }();\n";
                }
                return;
            }
            emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
            stream << "        {\n";
            stream << "            const auto next_words = " << wordsExpr << ";\n";
            if (isWideLogicValue(graph, resultValue))
            {
                if (needChangeDetect)
                {
                    stream << "            if (grhsim_assign_words(" << lhs << ", next_words, " << resultWidth << ")) {\n";
                    emitChangedValuePropagation(stream, model, resultValue, "                ");
                    stream << "            }\n";
                }
                else
                {
                    stream << "            " << lhs << " = next_words;\n";
                }
            }
            else
            {
                stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                       << ">(grhsim_trunc_u64(next_words[0], " << resultWidth << "));\n";
                if (needChangeDetect)
                {
                    stream << "            if (" << lhs << " != next_value) {\n";
                    emitChangedValueEffects(stream, model, resultValue, lhs, "next_value", "                ");
                    stream << "                " << lhs << " = next_value;\n";
                    stream << "            }\n";
                }
                else
                {
                    stream << "            " << lhs << " = next_value;\n";
                }
            }
            stream << "        }\n";
        }

        void emitLogicAssignFromBoolExpr(std::ostream &stream,
                                         const Graph &graph,
                                         const EmitModel &model,
                                         ValueId resultValue,
                                         const std::string &boolExpr)
        {
            const std::string lhs = valueRef(model, resultValue);
            if (!isMaterializedValue(model, resultValue))
            {
                emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                stream << "        const auto " << lhs << " = static_cast<" << cppTypeForValue(graph, resultValue)
                       << ">(" << boolExpr << ");\n";
                return;
            }
            const bool needChangeDetect = valueNeedsTrackedChange(model, resultValue);
            emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
            stream << "        {\n";
            stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                   << ">(" << boolExpr << ");\n";
            if (needChangeDetect)
            {
                stream << "            if (" << lhs << " != next_value) {\n";
                emitChangedValueEffects(stream, model, resultValue, lhs, "next_value", "                ");
                stream << "                " << lhs << " = next_value;\n";
                stream << "            }\n";
            }
            else
            {
                stream << "            " << lhs << " = next_value;\n";
            }
            stream << "        }\n";
        }

        bool concatAllOperandsAreOneBit(const Graph &graph, const Operation &op)
        {
            if (op.operands().empty())
            {
                return false;
            }
            for (ValueId operand : op.operands())
            {
                if (graph.valueWidth(operand) != 1)
                {
                    return false;
                }
            }
            return true;
        }

        bool concatAllOperandsAreScalarLogic(const Graph &graph, const Operation &op)
        {
            if (op.kind() != OperationKind::kConcat || op.operands().empty())
            {
                return false;
            }
            for (ValueId operand : op.operands())
            {
                if (!isScalarLogicValue(graph, operand))
                {
                    return false;
                }
            }
            return true;
        }

        std::optional<int32_t> concatUniformOperandWidth(const Graph &graph, const Operation &op)
        {
            if (op.operands().empty())
            {
                return std::nullopt;
            }
            const int32_t width = graph.valueWidth(op.operands().front());
            for (ValueId operand : op.operands())
            {
                if (graph.valueWidth(operand) != width)
                {
                    return std::nullopt;
                }
            }
            return width;
        }

        bool preferLoopedScalarConcatEmit(const Graph &graph, const Operation &op)
        {
            return concatAllOperandsAreScalarLogic(graph, op) && op.operands().size() >= 4;
        }

        std::string scalarConcatValueArrayExpr(const std::vector<std::string> &operands)
        {
            std::vector<std::string> items;
            items.reserve(operands.size());
            for (const auto &operand : operands)
            {
                items.push_back("static_cast<std::uint64_t>(" + operand + ")");
            }
            return "std::array<std::uint64_t, " + std::to_string(operands.size()) + ">{" +
                   joinStrings(items, ", ") + "}";
        }

        std::string scalarConcatWidthArrayExpr(const Graph &graph, const Operation &op)
        {
            std::vector<std::string> items;
            items.reserve(op.operands().size());
            for (ValueId operand : op.operands())
            {
                items.push_back(std::to_string(graph.valueWidth(operand)));
            }
            return "std::array<std::size_t, " + std::to_string(op.operands().size()) + ">{" +
                   joinStrings(items, ", ") + "}";
        }

        std::string scalarBitPackExpr(const std::vector<std::string> &operands)
        {
            const std::string valuesExpr = scalarConcatValueArrayExpr(operands);
            return "([&]() -> std::uint64_t { const auto bits = " + valuesExpr +
                   "; return grhsim_pack_bits_u64(bits.data(), bits.size()); }())";
        }

        std::string scalarConcatLoopExpr(const Graph &graph,
                                         const Operation &op,
                                         const std::vector<std::string> &operands,
                                         bool wideResult)
        {
            const std::size_t operandCount = operands.size();
            const std::string valuesExpr = scalarConcatValueArrayExpr(operands);
            const auto uniformWidth = concatUniformOperandWidth(graph, op);
            const int32_t resultWidth =
                op.results().empty() ? 0 : graph.valueWidth(op.results().front());
            if (wideResult)
            {
                std::ostringstream body;
                body << "const auto values = " << valuesExpr << "; ";
                if (uniformWidth)
                {
                    body << "grhsim_concat_uniform_scalars_words(values.data(), values.size(), "
                         << *uniformWidth << ", " << resultWidth << ", out.data(), out.size()); ";
                    return wordsArrayLambdaExprForWidth(resultWidth, body.str());
                }
                body << "const auto widths = " << scalarConcatWidthArrayExpr(graph, op) << "; ";
                body << "grhsim_concat_scalars_words(values.data(), widths.data(), values.size(), "
                     << resultWidth << ", out.data(), out.size()); ";
                return wordsArrayLambdaExprForWidth(resultWidth, body.str());
            }
            if (uniformWidth)
            {
                return "grhsim_concat_uniform_scalars_u64(" + valuesExpr + ".data(), " + valuesExpr + ".size(), " +
                       std::to_string(*uniformWidth) + ", " + std::to_string(resultWidth) + ")";
            }
            const std::string widthsExpr = scalarConcatWidthArrayExpr(graph, op);
            return "grhsim_concat_scalars_u64(" + valuesExpr + ".data(), " + widthsExpr + ".data(), " +
                   valuesExpr + ".size(), " + std::to_string(resultWidth) + ")";
        }

        struct ScalarConcatPrefixCacheKey
        {
            ValueId lhs;
            int32_t lhsWidth = 0;
            int32_t rhsWidth = 0;

            friend bool operator==(const ScalarConcatPrefixCacheKey &lhsKey,
                                   const ScalarConcatPrefixCacheKey &rhsKey) noexcept
            {
                return lhsKey.lhs == rhsKey.lhs &&
                       lhsKey.lhsWidth == rhsKey.lhsWidth &&
                       lhsKey.rhsWidth == rhsKey.rhsWidth;
            }
        };

        struct ScalarConcatPrefixCacheKeyHash
        {
            std::size_t operator()(const ScalarConcatPrefixCacheKey &key) const noexcept
            {
                std::size_t seed = ValueIdHash{}(key.lhs);
                seed = seed * 1315423911u + static_cast<std::size_t>(key.lhsWidth);
                seed = seed * 1315423911u + static_cast<std::size_t>(key.rhsWidth);
                return seed;
            }
        };

        struct ScalarConcatPrefixCacheDecl
        {
            ScalarConcatPrefixCacheKey key;
            int32_t totalWidth = 0;
            std::size_t firstUseIndex = 0;
            std::size_t useCount = 0;
            std::string tempName;
        };

        bool scalarConcatPrefixCacheable(const Graph &graph, const Operation &op)
        {
            (void)graph;
            (void)op;
            // <=64-bit scalar concat is emitted as direct bitwise code now.
            return false;
        }

        std::vector<ScalarConcatPrefixCacheDecl> collectScalarConcatPrefixCaches(
            const Graph &graph,
            const std::vector<OperationId> &opIds)
        {
            std::unordered_map<ScalarConcatPrefixCacheKey, ScalarConcatPrefixCacheDecl, ScalarConcatPrefixCacheKeyHash>
                declsByKey;
            for (std::size_t opIndex = 0; opIndex < opIds.size(); ++opIndex)
            {
                const Operation op = graph.getOperation(opIds[opIndex]);
                if (!scalarConcatPrefixCacheable(graph, op))
                {
                    continue;
                }
                const auto operands = op.operands();
                const ScalarConcatPrefixCacheKey key{
                    .lhs = operands[0],
                    .lhsWidth = graph.valueWidth(operands[0]),
                    .rhsWidth = graph.valueWidth(operands[1]),
                };
                auto [it, inserted] = declsByKey.emplace(
                    key,
                    ScalarConcatPrefixCacheDecl{
                        .key = key,
                        .totalWidth = graph.valueWidth(op.results().front()),
                        .firstUseIndex = opIndex,
                        .useCount = 0,
                        .tempName = {},
                    });
                if (inserted)
                {
                    it->second.firstUseIndex = opIndex;
                }
                it->second.useCount += 1;
            }

            std::vector<ScalarConcatPrefixCacheDecl> decls;
            decls.reserve(declsByKey.size());
            for (auto &[key, decl] : declsByKey)
            {
                if (decl.useCount >= 4)
                {
                    decls.push_back(decl);
                }
            }
            std::sort(decls.begin(),
                      decls.end(),
                      [](const ScalarConcatPrefixCacheDecl &lhs, const ScalarConcatPrefixCacheDecl &rhs) {
                          if (lhs.firstUseIndex != rhs.firstUseIndex)
                          {
                              return lhs.firstUseIndex < rhs.firstUseIndex;
                          }
                          const std::size_t lhsIdHash = ValueIdHash{}(lhs.key.lhs);
                          const std::size_t rhsIdHash = ValueIdHash{}(rhs.key.lhs);
                          if (lhsIdHash != rhsIdHash)
                          {
                              return lhsIdHash < rhsIdHash;
                          }
                          if (lhs.key.lhsWidth != rhs.key.lhsWidth)
                          {
                              return lhs.key.lhsWidth < rhs.key.lhsWidth;
                          }
                          return lhs.key.rhsWidth < rhs.key.rhsWidth;
                      });
            for (std::size_t i = 0; i < decls.size(); ++i)
            {
                decls[i].tempName = "catp_" + std::to_string(i);
            }
            return decls;
        }

        ScalarLogicExpr scalarAssignmentExpr(OperationKind kind,
                                             const std::vector<std::string> &operands,
                                             const Operation &op,
                                             const Graph &graph,
                                             const std::unordered_map<ScalarConcatPrefixCacheKey,
                                                                      std::string,
                                                                      ScalarConcatPrefixCacheKeyHash> *concatPrefixTemps = nullptr);

        bool emitWordLogicOperation(std::ostream &stream,
                                    const Graph &graph,
                                    const EmitModel &model,
                                    const Operation &op,
                                    std::string &error)
        {
            if (op.results().empty())
            {
                return true;
            }
            const auto operands = op.operands();
            const ValueId resultValue = op.results().front();
            const int32_t resultWidth = graph.valueWidth(resultValue);
            const std::size_t resultWords = logicWordCount(resultWidth);

            auto unaryWords = [&](ValueId operand, int32_t width) -> std::string
            {
                return wordsExprForValue(graph, model, operand, width);
            };
            auto binaryWords = [&](ValueId lhs, ValueId rhs, int32_t width, std::string_view helper) -> std::string
            {
                return binaryWordsBufferOpExpr(wordsExprForValue(graph, model, lhs, width),
                                               wordsExprForValue(graph, model, rhs, width),
                                               width,
                                               helper);
            };
            auto compareWidth = [&]() -> int32_t
            {
                int32_t width = 1;
                for (ValueId value : operands)
                {
                    width = std::max(width, graph.valueWidth(value));
                }
                return width;
            };

            switch (op.kind())
            {
            case OperationKind::kAssign:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, unaryWords(operands[0], resultWidth));
                return true;
            case OperationKind::kAdd:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_add_words"));
                return true;
            case OperationKind::kSub:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_sub_words"));
                return true;
            case OperationKind::kMul:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_mul_words"));
                return true;
            case OperationKind::kDiv:
            {
                const bool signedMode =
                    graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth,
                                                         signedMode ? "grhsim_sdiv_words" : "grhsim_udiv_words"));
                return true;
            }
            case OperationKind::kMod:
            {
                const bool signedMode =
                    graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth,
                                                         signedMode ? "grhsim_smod_words" : "grhsim_umod_words"));
                return true;
            }
            case OperationKind::kAnd:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_and_words"));
                return true;
            case OperationKind::kOr:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_or_words"));
                return true;
            case OperationKind::kXor:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_xor_words"));
                return true;
            case OperationKind::kXnor:
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue,
                                             binaryWords(operands[0], operands[1], resultWidth, "grhsim_xnor_words"));
                return true;
            case OperationKind::kNot:
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             unaryWordsBufferOpExpr(unaryWords(operands[0], resultWidth),
                                                                    resultWidth,
                                                                    "grhsim_not_words"));
                return true;
            case OperationKind::kEq:
            case OperationKind::kCaseEq:
            case OperationKind::kWildcardEq:
            {
                const int32_t width = compareWidth();
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            binaryWordsCompareExpr(wordsExprForValue(graph, model, operands[0], width),
                                                                   wordsExprForValue(graph, model, operands[1], width),
                                                                   "grhsim_compare_unsigned_words") + " == 0");
                return true;
            }
            case OperationKind::kNe:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardNe:
            {
                const int32_t width = compareWidth();
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            binaryWordsCompareExpr(wordsExprForValue(graph, model, operands[0], width),
                                                                   wordsExprForValue(graph, model, operands[1], width),
                                                                   "grhsim_compare_unsigned_words") + " != 0");
                return true;
            }
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            {
                const int32_t width = compareWidth();
                const bool signedMode =
                    graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string cmpExpr = signedMode
                                                ? binaryWordsSignedCompareExpr(
                                                      wordsExprForValue(graph, model, operands[0], width),
                                                      wordsExprForValue(graph, model, operands[1], width),
                                                      width)
                                                : binaryWordsCompareExpr(
                                                      wordsExprForValue(graph, model, operands[0], width),
                                                      wordsExprForValue(graph, model, operands[1], width),
                                                      "grhsim_compare_unsigned_words");
                std::string predicate;
                switch (op.kind())
                {
                case OperationKind::kLt:
                    predicate = cmpExpr + " < 0";
                    break;
                case OperationKind::kLe:
                    predicate = cmpExpr + " <= 0";
                    break;
                case OperationKind::kGt:
                    predicate = cmpExpr + " > 0";
                    break;
                case OperationKind::kGe:
                    predicate = cmpExpr + " >= 0";
                    break;
                default:
                    break;
                }
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue, predicate);
                return true;
            }
            case OperationKind::kLogicAnd:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_any_bits_words") +
                                                " && " +
                                                unaryWordsBoolExpr(unaryWords(operands[1], graph.valueWidth(operands[1])),
                                                                   graph.valueWidth(operands[1]),
                                                                   "grhsim_any_bits_words"));
                return true;
            case OperationKind::kLogicOr:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_any_bits_words") +
                                                " || " +
                                                unaryWordsBoolExpr(unaryWords(operands[1], graph.valueWidth(operands[1])),
                                                                   graph.valueWidth(operands[1]),
                                                                   "grhsim_any_bits_words"));
                return true;
            case OperationKind::kLogicNot:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            "!" + unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                                     graph.valueWidth(operands[0]),
                                                                     "grhsim_any_bits_words"));
                return true;
            case OperationKind::kReduceAnd:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_reduce_and_words"));
                return true;
            case OperationKind::kReduceNand:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_reduce_nand_words"));
                return true;
            case OperationKind::kReduceOr:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_reduce_or_words"));
                return true;
            case OperationKind::kReduceNor:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_reduce_nor_words"));
                return true;
            case OperationKind::kReduceXor:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_reduce_xor_words"));
                return true;
            case OperationKind::kReduceXnor:
                emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                            unaryWordsBoolExpr(unaryWords(operands[0], graph.valueWidth(operands[0])),
                                                               graph.valueWidth(operands[0]),
                                                               "grhsim_reduce_xnor_words"));
                return true;
            case OperationKind::kShl:
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             shiftWordsBufferOpExpr(unaryWords(operands[0], resultWidth),
                                                                    valueRef(model, operands[1]),
                                                                    resultWidth,
                                                                    "grhsim_shl_words"));
                return true;
            case OperationKind::kLShr:
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             shiftWordsBufferOpExpr(unaryWords(operands[0], resultWidth),
                                                                    valueRef(model, operands[1]),
                                                                    resultWidth,
                                                                    "grhsim_lshr_words"));
                return true;
            case OperationKind::kAShr:
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             shiftWordsBufferOpExpr(unaryWords(operands[0], resultWidth),
                                                                    valueRef(model, operands[1]),
                                                                    resultWidth,
                                                                    "grhsim_ashr_words"));
                return true;
            case OperationKind::kMux:
            {
                std::ostringstream out;
                out << "((" << valueRef(model, operands[0]) << ") ? "
                    << wordsExprForValue(graph, model, operands[1], resultWidth) << " : "
                    << wordsExprForValue(graph, model, operands[2], resultWidth) << ")";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kConcat:
            {
                if (preferLoopedScalarConcatEmit(graph, op))
                {
                    std::vector<std::string> operandExprs;
                    operandExprs.reserve(operands.size());
                    for (ValueId operand : operands)
                    {
                        operandExprs.push_back(valueRef(model, operand));
                    }
                    emitLogicAssignFromWordsExpr(stream,
                                                 graph,
                                                 model,
                                                 resultValue,
                                                 scalarConcatLoopExpr(graph, op, operandExprs, true));
                    return true;
                }
                std::ostringstream out;
                out << "([&]() -> " << wordsArrayTypeForWidth(resultWidth) << " { ";
                out << wordsArrayTypeForWidth(resultWidth) << " next_words{}; ";
                out << "std::size_t concat_cursor = " << resultWidth << "; ";
                for (ValueId operand : operands)
                {
                    const int32_t operandWidth = graph.valueWidth(operand);
                    out << "concat_cursor -= " << operandWidth << "; ";
                    out << "grhsim_insert_words(next_words, concat_cursor, "
                        << wordsExprForValue(graph, model, operand, operandWidth) << ", "
                        << operandWidth << "); ";
                }
                out << "return next_words; }())";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kReplicate:
            {
                const auto rep = getAttribute<int64_t>(op, "rep").value_or(1);
                const int32_t operandWidth = graph.valueWidth(operands[0]);
                std::ostringstream out;
                out << "([&]() -> " << wordsArrayTypeForWidth(resultWidth) << " { ";
                out << wordsArrayTypeForWidth(resultWidth) << " next_words{}; ";
                out << "for (std::size_t rep_index = 0; rep_index < " << rep << "; ++rep_index) { ";
                out << "grhsim_insert_words(next_words, rep_index * " << operandWidth << ", "
                    << wordsExprForValue(graph, model, operands[0], operandWidth) << ", "
                    << operandWidth << "); ";
                out << "} grhsim_trunc_words(next_words, " << resultWidth << "); return next_words; }())";
                emitLogicAssignFromWordsExpr(stream, graph, model, resultValue, out.str());
                return true;
            }
            case OperationKind::kSliceStatic:
            {
                const auto sliceStart = getAttribute<int64_t>(op, "sliceStart").value_or(0);
                if (!isWideLogicValue(graph, resultValue) && resultWidth == 1)
                {
                    emitLogicAssignFromBoolExpr(stream, graph, model, resultValue,
                                                "grhsim_get_bit_words(" +
                                                    wordsExprForValue(graph, model, operands[0],
                                                                      graph.valueWidth(operands[0])) +
                                                    ", " + std::to_string(sliceStart) + ")");
                    return true;
                }
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             sliceWordsExpr(wordsExprForValue(graph, model, operands[0], graph.valueWidth(operands[0])),
                                                            std::to_string(sliceStart),
                                                            resultWidth));
                return true;
            }
            case OperationKind::kSliceDynamic:
            {
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             sliceWordsExpr(wordsExprForValue(graph, model, operands[0], graph.valueWidth(operands[0])),
                                                            "grhsim_index_words(" + valueRef(model, operands[1]) + ", " +
                                                                std::to_string(graph.valueWidth(operands[0])) + ")",
                                                            resultWidth));
                return true;
            }
            case OperationKind::kSliceArray:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth");
                if (!sliceWidth)
                {
                    error = "kSliceArray missing sliceWidth";
                    return false;
                }
                emitLogicAssignFromWordsExpr(stream,
                                             graph,
                                             model,
                                             resultValue,
                                             sliceWordsExpr(wordsExprForValue(graph, model, operands[0], graph.valueWidth(operands[0])),
                                                            "((" + valueRef(model, operands[1]) + ") * " + std::to_string(*sliceWidth) + ")",
                                                            resultWidth));
                return true;
            }
            default:
                error = "unsupported wide logic emit op: " + std::string(op.symbolText());
                return false;
            }
        }

        std::string eventLogicExprFromWordsExpr(const Graph &graph,
                                                ValueId resultValue,
                                                const std::string &wordsExpr)
        {
            if (isWideLogicValue(graph, resultValue))
            {
                return wordsExpr;
            }
            std::ostringstream out;
            out << "static_cast<" << cppTypeForValue(graph, resultValue) << ">("
                << scalarTruncExpr("(" + wordsExpr + ")[0]",
                                   static_cast<std::size_t>(graph.valueWidth(resultValue)))
                << ")";
            return out.str();
        }

        std::optional<std::string> eventWordLogicExprForOp(const Graph &graph,
                                                           const EmitModel &model,
                                                           const Operation &op,
                                                           ValueId resultValue,
                                                           const std::vector<std::string> &operandExprs)
        {
            const auto operands = op.operands();
            const int32_t resultWidth = graph.valueWidth(resultValue);
            const std::size_t resultWords = logicWordCount(resultWidth);
            auto operandWords = [&](std::size_t index, int32_t width) -> std::string
            {
                const ValueId value = operands[index];
                return castWordsExprForValue(graph, value, operandExprs[index], width);
            };
            auto compareWidth = [&]() -> int32_t
            {
                int32_t width = 1;
                for (ValueId value : operands)
                {
                    width = std::max(width, graph.valueWidth(value));
                }
                return width;
            };
            auto logicBoolExpr = [&](const std::string &expr) -> std::string
            {
                return "static_cast<" + cppTypeForValue(graph, resultValue) + ">(" + expr + ")";
            };

            switch (op.kind())
            {
            case OperationKind::kAssign:
                return eventLogicExprFromWordsExpr(graph, resultValue, operandWords(0, resultWidth));
            case OperationKind::kAdd:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    binaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                            operandWords(1, resultWidth),
                                            resultWidth,
                                            "grhsim_add_words"));
            case OperationKind::kSub:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    binaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                            operandWords(1, resultWidth),
                                            resultWidth,
                                            "grhsim_sub_words"));
            case OperationKind::kMul:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    "grhsim_mul_words(" + operandWords(0, resultWidth) + ", " + operandWords(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")");
            case OperationKind::kDiv:
            {
                const bool signedMode = graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string helper = signedMode ? "grhsim_sdiv_words" : "grhsim_udiv_words";
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   helper + "(" + operandWords(0, resultWidth) + ", " +
                                                       operandWords(1, resultWidth) + ", " +
                                                       std::to_string(resultWidth) + ")");
            }
            case OperationKind::kMod:
            {
                const bool signedMode = graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string helper = signedMode ? "grhsim_smod_words" : "grhsim_umod_words";
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   helper + "(" + operandWords(0, resultWidth) + ", " +
                                                       operandWords(1, resultWidth) + ", " +
                                                       std::to_string(resultWidth) + ")");
            }
            case OperationKind::kAnd:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    binaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                            operandWords(1, resultWidth),
                                            resultWidth,
                                            "grhsim_and_words"));
            case OperationKind::kOr:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    binaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                            operandWords(1, resultWidth),
                                            resultWidth,
                                            "grhsim_or_words"));
            case OperationKind::kXor:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    binaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                            operandWords(1, resultWidth),
                                            resultWidth,
                                            "grhsim_xor_words"));
            case OperationKind::kXnor:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    binaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                            operandWords(1, resultWidth),
                                            resultWidth,
                                            "grhsim_xnor_words"));
            case OperationKind::kNot:
                return eventLogicExprFromWordsExpr(
                    graph, resultValue,
                    unaryWordsBufferOpExpr(operandWords(0, resultWidth),
                                           resultWidth,
                                           "grhsim_not_words"));
            case OperationKind::kEq:
            case OperationKind::kCaseEq:
            case OperationKind::kWildcardEq:
            {
                const int32_t width = compareWidth();
                return logicBoolExpr(binaryWordsCompareExpr(operandWords(0, width),
                                                            operandWords(1, width),
                                                            "grhsim_compare_unsigned_words") + " == 0");
            }
            case OperationKind::kNe:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardNe:
            {
                const int32_t width = compareWidth();
                return logicBoolExpr(binaryWordsCompareExpr(operandWords(0, width),
                                                            operandWords(1, width),
                                                            "grhsim_compare_unsigned_words") + " != 0");
            }
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            {
                const int32_t width = compareWidth();
                const bool signedMode = graph.valueSigned(operands[0]) && graph.valueSigned(operands[1]);
                const std::string cmpExpr = signedMode
                                                ? binaryWordsSignedCompareExpr(operandWords(0, width),
                                                                               operandWords(1, width),
                                                                               width)
                                                : binaryWordsCompareExpr(operandWords(0, width),
                                                                        operandWords(1, width),
                                                                        "grhsim_compare_unsigned_words");
                if (op.kind() == OperationKind::kLt)
                {
                    return logicBoolExpr(cmpExpr + " < 0");
                }
                if (op.kind() == OperationKind::kLe)
                {
                    return logicBoolExpr(cmpExpr + " <= 0");
                }
                if (op.kind() == OperationKind::kGt)
                {
                    return logicBoolExpr(cmpExpr + " > 0");
                }
                return logicBoolExpr(cmpExpr + " >= 0");
            }
            case OperationKind::kLogicAnd:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_any_bits_words") +
                                     " && " +
                                     unaryWordsBoolExpr(operandWords(1, graph.valueWidth(operands[1])),
                                                        graph.valueWidth(operands[1]),
                                                        "grhsim_any_bits_words"));
            case OperationKind::kLogicOr:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_any_bits_words") +
                                     " || " +
                                     unaryWordsBoolExpr(operandWords(1, graph.valueWidth(operands[1])),
                                                        graph.valueWidth(operands[1]),
                                                        "grhsim_any_bits_words"));
            case OperationKind::kLogicNot:
                return logicBoolExpr("!" + unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                              graph.valueWidth(operands[0]),
                                                              "grhsim_any_bits_words"));
            case OperationKind::kReduceAnd:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_reduce_and_words"));
            case OperationKind::kReduceNand:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_reduce_nand_words"));
            case OperationKind::kReduceOr:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_reduce_or_words"));
            case OperationKind::kReduceNor:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_reduce_nor_words"));
            case OperationKind::kReduceXor:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_reduce_xor_words"));
            case OperationKind::kReduceXnor:
                return logicBoolExpr(unaryWordsBoolExpr(operandWords(0, graph.valueWidth(operands[0])),
                                                        graph.valueWidth(operands[0]),
                                                        "grhsim_reduce_xnor_words"));
            case OperationKind::kShl:
                return eventLogicExprFromWordsExpr(graph,
                                                   resultValue,
                                                   shiftWordsBufferOpExpr(operandWords(0, resultWidth),
                                                                          operandExprs[1],
                                                                          resultWidth,
                                                                          "grhsim_shl_words"));
            case OperationKind::kLShr:
                return eventLogicExprFromWordsExpr(graph,
                                                   resultValue,
                                                   shiftWordsBufferOpExpr(operandWords(0, resultWidth),
                                                                          operandExprs[1],
                                                                          resultWidth,
                                                                          "grhsim_lshr_words"));
            case OperationKind::kAShr:
                return eventLogicExprFromWordsExpr(graph,
                                                   resultValue,
                                                   shiftWordsBufferOpExpr(operandWords(0, resultWidth),
                                                                          operandExprs[1],
                                                                          resultWidth,
                                                                          "grhsim_ashr_words"));
            case OperationKind::kMux:
            {
                const ValueId condValue = operands[0];
                const std::string condExpr =
                    isWideLogicValue(graph, condValue)
                        ? ("grhsim_any_bits_words(" + operandWords(0, graph.valueWidth(condValue)) + ", " +
                           std::to_string(graph.valueWidth(condValue)) + ")")
                        : ("(" + operandExprs[0] + ") != 0");
                return eventLogicExprFromWordsExpr(graph, resultValue,
                                                   "((" + condExpr + ") ? " + operandWords(1, resultWidth) + " : " +
                                                       operandWords(2, resultWidth) + ")");
            }
            case OperationKind::kConcat:
            {
                if (preferLoopedScalarConcatEmit(graph, op))
                {
                    return eventLogicExprFromWordsExpr(graph, resultValue,
                                                       scalarConcatLoopExpr(graph, op, operandExprs, true));
                }
                if (resultWidth <= 64 && concatAllOperandsAreOneBit(graph, op))
                {
                    return logicBoolExpr(scalarBitPackExpr(operandExprs));
                }
                std::ostringstream out;
                out << "([&]() -> " << wordsArrayTypeForWidth(resultWidth) << " { ";
                out << wordsArrayTypeForWidth(resultWidth) << " next_words{}; ";
                out << "std::size_t concat_cursor = " << resultWidth << "; ";
                for (std::size_t operandIndex = 0; operandIndex < operands.size(); ++operandIndex)
                {
                    const ValueId operand = operands[operandIndex];
                    const int32_t operandWidth = graph.valueWidth(operand);
                    out << "concat_cursor -= " << operandWidth << "; ";
                    out << "grhsim_insert_words(next_words, concat_cursor, "
                        << operandWords(operandIndex, operandWidth) << ", " << operandWidth << "); ";
                }
                out << "return next_words; }())";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            case OperationKind::kReplicate:
            {
                const auto rep = getAttribute<int64_t>(op, "rep").value_or(1);
                const int32_t operandWidth = graph.valueWidth(operands[0]);
                std::ostringstream out;
                out << "([&]() -> " << wordsArrayTypeForWidth(resultWidth) << " { ";
                out << wordsArrayTypeForWidth(resultWidth) << " next_words{}; ";
                out << "for (std::size_t rep_index = 0; rep_index < " << rep << "; ++rep_index) { ";
                out << "grhsim_insert_words(next_words, rep_index * " << operandWidth << ", "
                    << operandWords(0, operandWidth) << ", " << operandWidth << "); ";
                out << "} grhsim_trunc_words(next_words, " << resultWidth << "); return next_words; }())";
                return eventLogicExprFromWordsExpr(graph, resultValue, out.str());
            }
            case OperationKind::kSliceStatic:
            {
                const auto sliceStart = getAttribute<int64_t>(op, "sliceStart").value_or(0);
                if (!isWideLogicValue(graph, resultValue) && resultWidth == 1)
                {
                    return logicBoolExpr("grhsim_get_bit_words(" +
                                         operandWords(0, graph.valueWidth(operands[0])) + ", " +
                                         std::to_string(sliceStart) + ")");
                }
                return eventLogicExprFromWordsExpr(
                    graph,
                    resultValue,
                    sliceWordsExpr(operandWords(0, graph.valueWidth(operands[0])),
                                   std::to_string(sliceStart),
                                   resultWidth));
            }
            case OperationKind::kSliceDynamic:
            {
                return eventLogicExprFromWordsExpr(
                    graph,
                    resultValue,
                    sliceWordsExpr(operandWords(0, graph.valueWidth(operands[0])),
                                   "grhsim_index_words(" + operandExprs[1] + ", " +
                                       std::to_string(graph.valueWidth(operands[0])) + ")",
                                   resultWidth));
            }
            case OperationKind::kSliceArray:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth");
                if (!sliceWidth)
                {
                    return std::nullopt;
                }
                return eventLogicExprFromWordsExpr(
                    graph,
                    resultValue,
                    sliceWordsExpr(operandWords(0, graph.valueWidth(operands[0])),
                                   "((" + operandExprs[1] + ") * " + std::to_string(*sliceWidth) + ")",
                                   resultWidth));
            }
            default:
                break;
            }
            return std::nullopt;
        }

        bool eventExprTopoCollect(const Graph &graph,
                                  ValueId value,
                                  std::unordered_set<ValueId, ValueIdHash> &visiting,
                                  std::unordered_set<ValueId, ValueIdHash> &visited,
                                  std::vector<ValueId> &ordered)
        {
            if (visited.contains(value))
            {
                return true;
            }
            if (visiting.contains(value))
            {
                return false;
            }
            visiting.insert(value);

            bool ok = true;
            const OperationId defOpId = graph.valueDef(value);
            if (defOpId.valid())
            {
                const Operation op = graph.getOperation(defOpId);
                switch (op.kind())
                {
                case OperationKind::kConstant:
                case OperationKind::kRegisterReadPort:
                case OperationKind::kLatchReadPort:
                    break;
                case OperationKind::kMemoryReadPort:
                case OperationKind::kAssign:
                case OperationKind::kAdd:
                case OperationKind::kSub:
                case OperationKind::kMul:
                case OperationKind::kDiv:
                case OperationKind::kMod:
                case OperationKind::kAnd:
                case OperationKind::kOr:
                case OperationKind::kXor:
                case OperationKind::kXnor:
                case OperationKind::kEq:
                case OperationKind::kNe:
                case OperationKind::kCaseEq:
                case OperationKind::kCaseNe:
                case OperationKind::kWildcardEq:
                case OperationKind::kWildcardNe:
                case OperationKind::kLt:
                case OperationKind::kLe:
                case OperationKind::kGt:
                case OperationKind::kGe:
                case OperationKind::kLogicAnd:
                case OperationKind::kLogicOr:
                case OperationKind::kNot:
                case OperationKind::kLogicNot:
                case OperationKind::kReduceAnd:
                case OperationKind::kReduceNand:
                case OperationKind::kReduceOr:
                case OperationKind::kReduceNor:
                case OperationKind::kReduceXor:
                case OperationKind::kReduceXnor:
                case OperationKind::kShl:
                case OperationKind::kLShr:
                case OperationKind::kAShr:
                case OperationKind::kMux:
                case OperationKind::kConcat:
                case OperationKind::kReplicate:
                case OperationKind::kSliceStatic:
                case OperationKind::kSliceDynamic:
                case OperationKind::kSliceArray:
                case OperationKind::kSystemFunction:
                    for (ValueId operand : op.operands())
                    {
                        if (!eventExprTopoCollect(graph, operand, visiting, visited, ordered))
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                    {
                        ordered.push_back(value);
                    }
                    break;
                default:
                    ok = false;
                    break;
                }
            }

            visiting.erase(value);
            if (ok)
            {
                visited.insert(value);
            }
            return ok;
        }

        std::optional<std::string> eventExprLeafRefForValue(const Graph &graph,
                                                            const EmitModel &model,
                                                            ValueId value)
        {
            if (auto it = model.inputFieldByValue.find(value); it != model.inputFieldByValue.end())
            {
                return it->second;
            }
            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return valueRef(model, value);
            }
            const Operation op = graph.getOperation(defOpId);
            switch (op.kind())
            {
            case OperationKind::kConstant:
                return constantExpr(graph, op, value);
            case OperationKind::kRegisterReadPort:
            {
                auto regSymbol = getAttribute<std::string>(op, "regSymbol");
                if (!regSymbol)
                {
                    return std::nullopt;
                }
                auto it = model.stateBySymbol.find(*regSymbol);
                if (it == model.stateBySymbol.end())
                {
                    return std::nullopt;
                }
                return stateRef(it->second);
            }
            case OperationKind::kLatchReadPort:
            {
                auto latchSymbol = getAttribute<std::string>(op, "latchSymbol");
                if (!latchSymbol)
                {
                    return std::nullopt;
                }
                auto it = model.stateBySymbol.find(*latchSymbol);
                if (it == model.stateBySymbol.end())
                {
                    return std::nullopt;
                }
                return stateRef(it->second);
            }
            default:
                break;
            }
            return std::nullopt;
        }

        std::optional<std::string> eventExprRefForValue(const Graph &graph,
                                                        const EmitModel &model,
                                                        ValueId value,
                                                        const std::unordered_map<ValueId, std::string, ValueIdHash> &materializedRefs)
        {
            if (auto it = materializedRefs.find(value); it != materializedRefs.end())
            {
                return it->second;
            }
            return eventExprLeafRefForValue(graph, model, value);
        }

        std::optional<std::string> eventExprMaterializedBodyForValue(
            const Graph &graph,
            const EmitModel &model,
            ValueId value,
            const std::unordered_map<ValueId, std::string, ValueIdHash> &materializedRefs)
        {
            if (auto leaf = eventExprLeafRefForValue(graph, model, value))
            {
                return leaf;
            }

            const OperationId defOpId = graph.valueDef(value);
            if (!defOpId.valid())
            {
                return valueRef(model, value);
            }
            const Operation op = graph.getOperation(defOpId);
            const auto operands = op.operands();
            std::vector<std::string> operandExprs;
            operandExprs.reserve(operands.size());
            for (ValueId operand : operands)
            {
                auto operandExpr = eventExprRefForValue(graph, model, operand, materializedRefs);
                if (!operandExpr)
                {
                    return std::nullopt;
                }
                operandExprs.push_back(*operandExpr);
            }

            if (op.kind() == OperationKind::kMemoryReadPort)
            {
                auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                if (!memSymbol || operandExprs.empty() || graph.valueType(value) != ValueType::Logic)
                {
                    return std::nullopt;
                }
                auto stateIt = model.stateBySymbol.find(*memSymbol);
                if (stateIt == model.stateBySymbol.end())
                {
                    return std::nullopt;
                }
                std::ostringstream out;
                out << "([&]() -> " << cppTypeForValue(graph, value) << " { ";
                out << "const std::size_t row = grhsim_index_words(" << operandExprs[0] << ", "
                    << stateIt->second.rowCount << "); ";
                out << "if (row >= " << stateIt->second.rowCount << ") return "
                    << defaultInitExprForLogicWidth(graph.valueWidth(value)) << "; ";
                out << "return " << stateRef(stateIt->second) << "[row]; }())";
                return out.str();
            }

            if (graph.valueType(value) != ValueType::Logic)
            {
                return std::nullopt;
            }
            if (opNeedsWordLogicEmit(graph, op))
            {
                return eventWordLogicExprForOp(graph, model, op, value, operandExprs);
            }
            const ScalarLogicExpr rhs = scalarAssignmentExpr(op.kind(), operandExprs, op, graph);
            if (rhs.expr.empty())
            {
                return std::nullopt;
            }
            return renderScalarLogicExpr(graph, value, rhs);
        }

        std::optional<std::string> pureExprForValue(const Graph &graph,
                                                    const EmitModel &model,
                                                    ValueId value,
                                                    std::unordered_map<ValueId, std::optional<std::string>, ValueIdHash> &cache,
                                                    std::unordered_map<ValueId, std::size_t, ValueIdHash> &costCache,
                                                    std::size_t &totalOps)
        {
            if (auto it = cache.find(value); it != cache.end())
            {
                totalOps += costCache[value];
                return it->second;
            }

            std::optional<std::string> expr;
            std::size_t cost = 0;
            if (auto inputIt = model.inputFieldByValue.find(value); inputIt != model.inputFieldByValue.end())
            {
                expr = inputIt->second;
            }
            else
            {
                const Value val = graph.getValue(value);
                const OperationId defOpId = val.definingOp();
                if (!defOpId.valid())
                {
                    expr = valueRef(model, value);
                }
                else
                {
                    const Operation op = graph.getOperation(defOpId);
                    switch (op.kind())
                    {
                    case OperationKind::kConstant:
                        expr = constantExpr(graph, op, value);
                        break;
                    case OperationKind::kRegisterReadPort:
                    {
                        auto regSymbol = getAttribute<std::string>(op, "regSymbol");
                        if (regSymbol)
                        {
                            if (auto it = model.stateBySymbol.find(*regSymbol); it != model.stateBySymbol.end())
                            {
                                expr = stateRef(it->second);
                            }
                        }
                        break;
                    }
                    case OperationKind::kLatchReadPort:
                    {
                        auto latchSymbol = getAttribute<std::string>(op, "latchSymbol");
                        if (latchSymbol)
                        {
                            if (auto it = model.stateBySymbol.find(*latchSymbol); it != model.stateBySymbol.end())
                            {
                                expr = stateRef(it->second);
                            }
                        }
                        break;
                    }
                    case OperationKind::kMemoryReadPort:
                    {
                        const auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                        const auto operands = op.operands();
                        if (!memSymbol || operands.empty() || graph.valueType(value) != ValueType::Logic)
                        {
                            break;
                        }
                        auto stateIt = model.stateBySymbol.find(*memSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            break;
                        }
                        std::size_t operandCost = 0;
                        auto addrExpr = pureExprForValue(graph, model, operands[0], cache, costCache, operandCost);
                        if (!addrExpr)
                        {
                            break;
                        }
                        cost += operandCost + 1;
                        std::ostringstream out;
                        out << "([&]() -> " << cppTypeForValue(graph, value) << " { ";
                        out << "const std::size_t row = grhsim_index_words(" << *addrExpr << ", "
                            << stateIt->second.rowCount << "); ";
                        out << "if (row >= " << stateIt->second.rowCount << ") return "
                            << defaultInitExprForLogicWidth(graph.valueWidth(value)) << "; ";
                        out << "return " << stateRef(stateIt->second) << "[row]; }())";
                        expr = out.str();
                        break;
                    }
                    case OperationKind::kAssign:
                    case OperationKind::kAdd:
                    case OperationKind::kSub:
                    case OperationKind::kMul:
                    case OperationKind::kDiv:
                    case OperationKind::kMod:
                    case OperationKind::kAnd:
                    case OperationKind::kOr:
                    case OperationKind::kXor:
                    case OperationKind::kXnor:
                    case OperationKind::kEq:
                    case OperationKind::kNe:
                    case OperationKind::kCaseEq:
                    case OperationKind::kCaseNe:
                    case OperationKind::kWildcardEq:
                    case OperationKind::kWildcardNe:
                    case OperationKind::kLt:
                    case OperationKind::kLe:
                    case OperationKind::kGt:
                    case OperationKind::kGe:
                    case OperationKind::kLogicAnd:
                    case OperationKind::kLogicOr:
                    case OperationKind::kNot:
                    case OperationKind::kLogicNot:
                    case OperationKind::kReduceAnd:
                    case OperationKind::kReduceNand:
                    case OperationKind::kReduceOr:
                    case OperationKind::kReduceNor:
                    case OperationKind::kReduceXor:
                    case OperationKind::kReduceXnor:
                    case OperationKind::kShl:
                    case OperationKind::kLShr:
                    case OperationKind::kAShr:
                    case OperationKind::kMux:
                    case OperationKind::kConcat:
                    case OperationKind::kReplicate:
                    case OperationKind::kSliceStatic:
                    case OperationKind::kSliceDynamic:
                    case OperationKind::kSliceArray:
                    case OperationKind::kSystemFunction:
                    {
                        if (graph.valueType(value) != ValueType::Logic)
                        {
                            break;
                        }
                        const auto operands = op.operands();
                        std::vector<std::string> operandExprs;
                        operandExprs.reserve(operands.size());
                        for (ValueId operand : operands)
                        {
                            std::size_t operandCost = 0;
                            auto operandExpr = pureExprForValue(graph, model, operand, cache, costCache, operandCost);
                            if (!operandExpr)
                            {
                                operandExprs.clear();
                                break;
                            }
                            cost += operandCost;
                            operandExprs.push_back(*operandExpr);
                        }
                        if (operandExprs.size() != operands.size())
                        {
                            break;
                        }
                        cost += 1;
                        if (opNeedsWordLogicEmit(graph, op))
                        {
                            expr = eventWordLogicExprForOp(graph, model, op, value, operandExprs);
                        }
                        else
                        {
                            const ScalarLogicExpr rhs = scalarAssignmentExpr(op.kind(), operandExprs, op, graph);
                            if (!rhs.expr.empty())
                            {
                                expr = renderScalarLogicExpr(graph, value, rhs);
                            }
                        }
                        if (expr && expr->empty())
                        {
                            expr.reset();
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }

            cache.emplace(value, expr);
            costCache.emplace(value, cost);
            totalOps += cost;
            return expr;
        }

        ScalarLogicExpr scalarAssignmentExpr(OperationKind kind,
                                             const std::vector<std::string> &operands,
                                             const Operation &op,
                                             const Graph &graph,
                                             const std::unordered_map<ScalarConcatPrefixCacheKey,
                                                                      std::string,
                                                                      ScalarConcatPrefixCacheKeyHash> *concatPrefixTemps)
        {
            const auto resultWidth =
                op.results().empty() ? 64 : static_cast<std::size_t>(graph.valueWidth(op.results().front()));
            auto operandExpr = [&](std::size_t index, std::size_t width) -> std::string
            {
                const ValueId value = op.operands()[index];
                const std::size_t srcWidth = static_cast<std::size_t>(graph.valueWidth(value));
                if (srcWidth == width)
                {
                    if (width == 1)
                    {
                        return operands[index];
                    }
                    return "static_cast<std::uint64_t>(" + operands[index] + ")";
                }
                return "grhsim_cast_u64(static_cast<std::uint64_t>(" + operands[index] + "), " +
                       std::to_string(srcWidth) + ", " + std::to_string(width) + ", " +
                       boolLiteral(graph.valueSigned(value)) + ")";
            };
            auto compareWidth = [&]() -> std::size_t
            {
                std::size_t width = 1;
                for (ValueId value : op.operands())
                {
                    width = std::max(width, static_cast<std::size_t>(graph.valueWidth(value)));
                }
                return width;
            };
            switch (kind)
            {
            case OperationKind::kAssign:
                return ScalarLogicExpr{operandExpr(0, resultWidth), true};
            case OperationKind::kAdd:
                return ScalarLogicExpr{"(" + operandExpr(0, resultWidth) + " + " + operandExpr(1, resultWidth) + ")", false};
            case OperationKind::kSub:
                return ScalarLogicExpr{"(" + operandExpr(0, resultWidth) + " - " + operandExpr(1, resultWidth) + ")", false};
            case OperationKind::kMul:
                return ScalarLogicExpr{"(" + operandExpr(0, resultWidth) + " * " + operandExpr(1, resultWidth) + ")", false};
            case OperationKind::kDiv:
            {
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                return ScalarLogicExpr{
                    std::string(signedMode ? "grhsim_sdiv_u64(" : "grhsim_udiv_u64(") +
                        operandExpr(0, resultWidth) + ", " + operandExpr(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")",
                    true};
            }
            case OperationKind::kMod:
            {
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                return ScalarLogicExpr{
                    std::string(signedMode ? "grhsim_smod_u64(" : "grhsim_umod_u64(") +
                        operandExpr(0, resultWidth) + ", " + operandExpr(1, resultWidth) + ", " +
                        std::to_string(resultWidth) + ")",
                    true};
            }
            case OperationKind::kAnd:
                return ScalarLogicExpr{"(" + operandExpr(0, resultWidth) + " & " + operandExpr(1, resultWidth) + ")", true};
            case OperationKind::kOr:
                return ScalarLogicExpr{"(" + operandExpr(0, resultWidth) + " | " + operandExpr(1, resultWidth) + ")", true};
            case OperationKind::kXor:
                return ScalarLogicExpr{"(" + operandExpr(0, resultWidth) + " ^ " + operandExpr(1, resultWidth) + ")", true};
            case OperationKind::kXnor:
                return ScalarLogicExpr{"(~(" + operandExpr(0, resultWidth) + " ^ " + operandExpr(1, resultWidth) + "))", false};
            case OperationKind::kNot:
                return ScalarLogicExpr{"(~(" + operandExpr(0, resultWidth) + "))", false};
            case OperationKind::kEq:
            case OperationKind::kCaseEq:
            case OperationKind::kWildcardEq:
            {
                const std::size_t width = compareWidth();
                return ScalarLogicExpr{"((" + operandExpr(0, width) + ") == (" + operandExpr(1, width) + "))", true};
            }
            case OperationKind::kNe:
            case OperationKind::kCaseNe:
            case OperationKind::kWildcardNe:
            {
                const std::size_t width = compareWidth();
                return ScalarLogicExpr{"((" + operandExpr(0, width) + ") != (" + operandExpr(1, width) + "))", true};
            }
            case OperationKind::kLt:
            case OperationKind::kLe:
            case OperationKind::kGt:
            case OperationKind::kGe:
            {
                const std::size_t width = compareWidth();
                const bool signedMode =
                    graph.valueSigned(op.operands()[0]) && graph.valueSigned(op.operands()[1]);
                const std::string cmpExpr = signedMode
                                                ? "grhsim_compare_signed_u64(" + operandExpr(0, width) + ", " +
                                                      operandExpr(1, width) + ", " + std::to_string(width) + ")"
                                                : "grhsim_compare_unsigned_u64(" + operandExpr(0, width) + ", " +
                                                      operandExpr(1, width) + ", " + std::to_string(width) + ")";
                switch (kind)
                {
                case OperationKind::kLt:
                    return ScalarLogicExpr{"(" + cmpExpr + " < 0)", true};
                case OperationKind::kLe:
                    return ScalarLogicExpr{"(" + cmpExpr + " <= 0)", true};
                case OperationKind::kGt:
                    return ScalarLogicExpr{"(" + cmpExpr + " > 0)", true};
                case OperationKind::kGe:
                    return ScalarLogicExpr{"(" + cmpExpr + " >= 0)", true};
                default:
                    break;
                }
                return ScalarLogicExpr{};
            }
            case OperationKind::kLogicAnd:
                return ScalarLogicExpr{"((" + operands[0] + ") && (" + operands[1] + "))", true};
            case OperationKind::kLogicOr:
                return ScalarLogicExpr{"((" + operands[0] + ") || (" + operands[1] + "))", true};
            case OperationKind::kLogicNot:
                return ScalarLogicExpr{"(!(" + operands[0] + "))", true};
            case OperationKind::kReduceAnd:
                return ScalarLogicExpr{"grhsim_reduce_and_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")", true};
            case OperationKind::kReduceNand:
                return ScalarLogicExpr{"grhsim_reduce_nand_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")", true};
            case OperationKind::kReduceOr:
                return ScalarLogicExpr{"grhsim_reduce_or_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")", true};
            case OperationKind::kReduceNor:
                return ScalarLogicExpr{"grhsim_reduce_nor_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")", true};
            case OperationKind::kReduceXor:
                return ScalarLogicExpr{"grhsim_reduce_xor_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")", true};
            case OperationKind::kReduceXnor:
                return ScalarLogicExpr{"grhsim_reduce_xnor_u64(" + operands[0] + ", " + std::to_string(graph.valueWidth(op.operands()[0])) + ")", true};
            case OperationKind::kShl:
                return ScalarLogicExpr{
                    scalarShlExpr(operandExpr(0, resultWidth), operands[1], static_cast<std::size_t>(resultWidth)),
                    true};
            case OperationKind::kLShr:
                return ScalarLogicExpr{
                    scalarLShrExpr(operandExpr(0, resultWidth), operands[1], static_cast<std::size_t>(resultWidth)),
                    true};
            case OperationKind::kAShr:
                return ScalarLogicExpr{"grhsim_ashr_u64(" + operandExpr(0, resultWidth) + ", " + operands[1] + ", " + std::to_string(resultWidth) + ")", true};
            case OperationKind::kMux:
                return ScalarLogicExpr{"((" + operands[0] + ") ? (" + operandExpr(1, resultWidth) + ") : (" + operandExpr(2, resultWidth) + "))", true};
            case OperationKind::kConcat:
            {
                if (preferLoopedScalarConcatEmit(graph, op))
                {
                    return ScalarLogicExpr{scalarConcatLoopExpr(graph, op, operands, false), true};
                }
                if (resultWidth <= 64 && concatAllOperandsAreOneBit(graph, op))
                {
                    return ScalarLogicExpr{scalarBitPackExpr(operands), true};
                }
                if (resultWidth <= 64)
                {
                    if (operands.empty())
                    {
                        return ScalarLogicExpr{};
                    }
                    std::string expr = scalarTruncExpr(operands[0], static_cast<std::size_t>(graph.valueWidth(op.operands()[0])));
                    std::size_t accumWidth = static_cast<std::size_t>(graph.valueWidth(op.operands()[0]));
                    for (std::size_t i = 1; i < operands.size(); ++i)
                    {
                        const std::size_t rhsWidth = static_cast<std::size_t>(graph.valueWidth(op.operands()[i]));
                        expr = scalarConcatExpr(expr, accumWidth, operands[i], rhsWidth);
                        accumWidth += rhsWidth;
                    }
                    return ScalarLogicExpr{expr, true};
                }
                if (op.operands().size() == 2)
                {
                    const int32_t lhsWidth = graph.valueWidth(op.operands()[0]);
                    const int32_t rhsWidth = graph.valueWidth(op.operands()[1]);
                    if (concatPrefixTemps != nullptr)
                    {
                        const ScalarConcatPrefixCacheKey key{
                            .lhs = op.operands()[0],
                            .lhsWidth = lhsWidth,
                            .rhsWidth = rhsWidth,
                        };
                        if (auto it = concatPrefixTemps->find(key); it != concatPrefixTemps->end())
                        {
                            return ScalarLogicExpr{
                                "grhsim_cat_rhs(" + it->second + ", " + operands[1] + ", " +
                                    std::to_string(resultWidth) + ", " + std::to_string(rhsWidth) + ")",
                                true};
                        }
                    }
                    return ScalarLogicExpr{
                        "grhsim_cat(" + operands[0] + ", " + std::to_string(lhsWidth) + ", " + operands[1] + ", " +
                            std::to_string(rhsWidth) + ")",
                        true};
                }
                if (operands.empty())
                {
                    return ScalarLogicExpr{};
                }
                std::string expr = operands[0];
                int32_t accumWidth = graph.valueWidth(op.operands()[0]);
                for (std::size_t i = 1; i < operands.size(); ++i)
                {
                    expr = "grhsim_concat_u64(" + expr + ", " + std::to_string(accumWidth) + ", " +
                           operands[i] + ", " + std::to_string(graph.valueWidth(op.operands()[i])) + ")";
                    accumWidth += graph.valueWidth(op.operands()[i]);
                }
                return ScalarLogicExpr{expr, true};
            }
            case OperationKind::kReplicate:
            {
                const auto rep = getAttribute<int64_t>(op, "rep").value_or(1);
                return ScalarLogicExpr{"grhsim_replicate_u64(" + operands[0] + ", " +
                                           std::to_string(graph.valueWidth(op.operands()[0])) + ", " + std::to_string(rep) + ")",
                                       true};
            }
            case OperationKind::kSliceStatic:
            {
                const auto sliceStart = getAttribute<int64_t>(op, "sliceStart").value_or(0);
                const auto sliceEnd = getAttribute<int64_t>(op, "sliceEnd").value_or(sliceStart);
                const int64_t width = sliceEnd - sliceStart + 1;
                if (width >= 64)
                {
                    return ScalarLogicExpr{"((" + operands[0] + ") >> " + std::to_string(sliceStart) + ")", true};
                }
                std::uint64_t mask = width <= 0 ? 0u : ((UINT64_C(1) << width) - 1u);
                return ScalarLogicExpr{"(((" + operands[0] + ") >> " + std::to_string(sliceStart) + ") & UINT64_C(" + std::to_string(mask) + "))",
                                       true};
            }
            case OperationKind::kSliceDynamic:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth").value_or(1);
                return ScalarLogicExpr{
                    scalarSliceDynamicExpr(operands[0],
                                           static_cast<std::size_t>(graph.valueWidth(op.operands()[0])),
                                           operands[1],
                                           static_cast<std::size_t>(sliceWidth)),
                    true};
            }
            case OperationKind::kSliceArray:
            {
                const auto sliceWidth = getAttribute<int64_t>(op, "sliceWidth");
                if (!sliceWidth)
                {
                    return ScalarLogicExpr{};
                }
                return ScalarLogicExpr{
                    scalarSliceArrayExpr(operands[0],
                                         static_cast<std::size_t>(graph.valueWidth(op.operands()[0])),
                                         operands[1],
                                         static_cast<std::size_t>(*sliceWidth)),
                    true};
            }
            case OperationKind::kSystemFunction:
            {
                const auto name = getAttribute<std::string>(op, "name");
                if (!name || *name != "clog2" || operands.size() != 1)
                {
                    return ScalarLogicExpr{};
                }
                return ScalarLogicExpr{"grhsim_clog2_u64(" + operands[0] + ", " +
                                           std::to_string(graph.valueWidth(op.operands()[0])) + ")",
                                       false};
            }
            default:
                break;
            }
            return ScalarLogicExpr{};
        }

        std::optional<std::string> exactEventExpr(const Graph &graph,
                                                  const EmitModel &model,
                                                  OperationId opId,
                                                  const Operation &op)
        {
            const auto sampleIt = model.eventSamplesByOp.find(opId);
            if (sampleIt == model.eventSamplesByOp.end() || sampleIt->second.values.empty())
            {
                return std::string("true");
            }

            const EventSampleDecl &samples = sampleIt->second;
            std::vector<std::string> parts;
            for (std::size_t i = 0; i < samples.values.size(); ++i)
            {
                const ValueId value = samples.values[i];
                const std::string &edgeField = model.eventEdgeFieldByValue.at(value);
                const std::string &edge = samples.edges[i];
                if (edge.empty())
                {
                    parts.push_back("(" + edgeField + " != grhsim_event_edge_kind::none)");
                }
                else if (edge == "posedge")
                {
                    parts.push_back("(" + edgeField + " == grhsim_event_edge_kind::posedge)");
                }
                else if (edge == "negedge")
                {
                    parts.push_back("(" + edgeField + " == grhsim_event_edge_kind::negedge)");
                }
                else
                {
                    parts.push_back("(" + edgeField + " != grhsim_event_edge_kind::none)");
                }
            }
            if (parts.empty())
            {
                return std::string("true");
            }
            return "(" + joinStrings(parts, " || ") + ")";
        }

        std::string memoryWriteRowExpr(const Graph &graph,
                                       const EmitModel &model,
                                       const WriteDecl &write,
                                       ValueId addrValue,
                                       const StateDecl &state);

        bool isWritePortKind(OperationKind kind) noexcept
        {
            switch (kind)
            {
            case OperationKind::kRegisterWritePort:
            case OperationKind::kLatchWritePort:
            case OperationKind::kMemoryWritePort:
                return true;
            default:
                return false;
            }
        }

        std::string scalarStateWriteHelperName(ValueSlotScalarKind kind)
        {
            switch (kind)
            {
            case ValueSlotScalarKind::kBool:
                return "apply_scalar_state_write_bool";
            case ValueSlotScalarKind::kU8:
                return "apply_scalar_state_write_u8";
            case ValueSlotScalarKind::kU16:
                return "apply_scalar_state_write_u16";
            case ValueSlotScalarKind::kU32:
                return "apply_scalar_state_write_u32";
            case ValueSlotScalarKind::kU64:
                return "apply_scalar_state_write_u64";
            case ValueSlotScalarKind::kCount:
                break;
            }
            return "apply_scalar_state_write_u64";
        }

        struct WritePortGuardKey
        {
            std::string condExpr;
            std::string eventExpr;

            bool operator==(const WritePortGuardKey &) const = default;
        };

        struct TableCompressibleScalarStateWriteDesc
        {
            ValueSlotScalarKind kind = ValueSlotScalarKind::kBool;
            std::uint32_t condIndex = 0;
            std::uint32_t touchedIndex = 0;
            std::uint32_t shadowDataIndex = 0;
            std::uint32_t stateDataIndex = 0;
            std::uint32_t nextIndex = 0;
            std::uint32_t maskIndex = 0;
            std::uint32_t shadowIndex = 0;
        };

        struct ScalarStateWriteRangeStep
        {
            std::int32_t cond = 0;
            std::int32_t touched = 0;
            std::int32_t shadowData = 0;
            std::int32_t stateData = 0;
            std::int32_t next = 0;
            std::int32_t mask = 0;
            std::int32_t shadow = 0;

            bool operator==(const ScalarStateWriteRangeStep &) const = default;
        };

        std::optional<WritePortGuardKey> writePortGuardKey(const Graph &graph,
                                                           const EmitModel &model,
                                                           OperationId opId,
                                                           const Operation &op)
        {
            const auto operands = op.operands();
            const std::string condExpr =
                operands.empty() ? "true" : truthyLogicValueExpr(graph, model, operands[0]);
            const auto eventExpr = exactEventExpr(graph, model, opId, op);
            if (!eventExpr)
            {
                return std::nullopt;
            }
            return WritePortGuardKey{
                .condExpr = condExpr,
                .eventExpr = *eventExpr,
            };
        }

        bool isCompressibleScalarStateWrite(const EmitModel &model, OperationId opId)
        {
            const auto writeIt = model.writeByOp.find(opId);
            if (writeIt == model.writeByOp.end())
            {
                return false;
            }
            const WriteDecl &write = writeIt->second;
            if (write.kind == StateDecl::Kind::Memory)
            {
                return false;
            }
            const auto stateIt = model.stateBySymbol.find(write.symbol);
            if (stateIt == model.stateBySymbol.end())
            {
                return false;
            }
            return !isWideLogicWidth(stateIt->second.width);
        }

        std::optional<TableCompressibleScalarStateWriteDesc>
        buildTableCompressibleScalarStateWriteDesc(const Graph &graph,
                                                  const EmitModel &model,
                                                  OperationId opId,
                                                  const Operation &op)
        {
            if (!isCompressibleScalarStateWrite(model, opId))
            {
                return std::nullopt;
            }
            const auto writeIt = model.writeByOp.find(opId);
            if (writeIt == model.writeByOp.end())
            {
                return std::nullopt;
            }
            const WriteDecl &write = writeIt->second;
            if (write.kind == StateDecl::Kind::Memory)
            {
                return std::nullopt;
            }
            const auto stateIt = model.stateBySymbol.find(write.symbol);
            if (stateIt == model.stateBySymbol.end())
            {
                return std::nullopt;
            }
            const StateDecl &state = stateIt->second;
            if (state.scalarKind != write.shadowScalarKind || state.slotIndex == kInvalidIndex)
            {
                return std::nullopt;
            }
            const auto operands = op.operands();
            if (operands.size() < 3 || graph.valueWidth(operands[0]) != 1)
            {
                return std::nullopt;
            }
            const auto condIt = model.valueScalarSlotByValue.find(operands[0]);
            if (condIt == model.valueScalarSlotByValue.end() ||
                condIt->second.kind != ValueSlotScalarKind::kBool)
            {
                return std::nullopt;
            }
            const auto nextIt = model.valueScalarSlotByValue.find(operands[1]);
            const auto maskIt = model.valueScalarSlotByValue.find(operands[2]);
            if (nextIt == model.valueScalarSlotByValue.end() ||
                maskIt == model.valueScalarSlotByValue.end() ||
                nextIt->second.kind != write.shadowScalarKind ||
                maskIt->second.kind != write.shadowScalarKind)
            {
                return std::nullopt;
            }
            const StateShadowDecl &shadow = model.stateShadows[write.shadowIndex];
            if (shadow.scalarKind != write.shadowScalarKind)
            {
                return std::nullopt;
            }
            return TableCompressibleScalarStateWriteDesc{
                .kind = write.shadowScalarKind,
                .condIndex = static_cast<std::uint32_t>(condIt->second.index),
                .touchedIndex = static_cast<std::uint32_t>(shadow.touchedIndex),
                .shadowDataIndex = static_cast<std::uint32_t>(shadow.dataIndex),
                .stateDataIndex = static_cast<std::uint32_t>(state.slotIndex),
                .nextIndex = static_cast<std::uint32_t>(nextIt->second.index),
                .maskIndex = static_cast<std::uint32_t>(maskIt->second.index),
                .shadowIndex = static_cast<std::uint32_t>(write.shadowIndex),
            };
        }

        ScalarStateWriteRangeStep scalarStateWriteRangeStep(const TableCompressibleScalarStateWriteDesc &lhs,
                                                            const TableCompressibleScalarStateWriteDesc &rhs)
        {
            return ScalarStateWriteRangeStep{
                .cond = static_cast<std::int32_t>(rhs.condIndex) - static_cast<std::int32_t>(lhs.condIndex),
                .touched = static_cast<std::int32_t>(rhs.touchedIndex) - static_cast<std::int32_t>(lhs.touchedIndex),
                .shadowData =
                    static_cast<std::int32_t>(rhs.shadowDataIndex) - static_cast<std::int32_t>(lhs.shadowDataIndex),
                .stateData =
                    static_cast<std::int32_t>(rhs.stateDataIndex) - static_cast<std::int32_t>(lhs.stateDataIndex),
                .next = static_cast<std::int32_t>(rhs.nextIndex) - static_cast<std::int32_t>(lhs.nextIndex),
                .mask = static_cast<std::int32_t>(rhs.maskIndex) - static_cast<std::int32_t>(lhs.maskIndex),
                .shadow = static_cast<std::int32_t>(rhs.shadowIndex) - static_cast<std::int32_t>(lhs.shadowIndex),
            };
        }

        std::string scalarStateWriteRangeDescTypeName(ValueSlotScalarKind kind)
        {
            switch (kind)
            {
            case ValueSlotScalarKind::kBool:
                return "scalar_state_write_bool_range_desc";
            case ValueSlotScalarKind::kU8:
                return "scalar_state_write_u8_range_desc";
            case ValueSlotScalarKind::kU16:
                return "scalar_state_write_u16_range_desc";
            case ValueSlotScalarKind::kU32:
                return "scalar_state_write_u32_range_desc";
            case ValueSlotScalarKind::kU64:
                return "scalar_state_write_u64_range_desc";
            }
            return "scalar_state_write_u64_range_desc";
        }

        std::string scalarStateWriteRangeHelperName(ValueSlotScalarKind kind)
        {
            switch (kind)
            {
            case ValueSlotScalarKind::kBool:
                return "apply_scalar_state_write_bool_range";
            case ValueSlotScalarKind::kU8:
                return "apply_scalar_state_write_u8_range";
            case ValueSlotScalarKind::kU16:
                return "apply_scalar_state_write_u16_range";
            case ValueSlotScalarKind::kU32:
                return "apply_scalar_state_write_u32_range";
            case ValueSlotScalarKind::kU64:
                return "apply_scalar_state_write_u64_range";
            }
            return "apply_scalar_state_write_u64_range";
        }

        bool modelUsesScalarStateWriteKind(const EmitModel &model, ValueSlotScalarKind kind)
        {
            const auto boolSlots =
                model.valueScalarSlotCounts[static_cast<std::size_t>(ValueSlotScalarKind::kBool)];
            if (boolSlots == 0)
            {
                return false;
            }
            if (kind != ValueSlotScalarKind::kBool &&
                model.valueScalarSlotCounts[static_cast<std::size_t>(kind)] == 0)
            {
                return false;
            }
            return std::any_of(model.writes.begin(), model.writes.end(), [&](const WriteDecl &write) {
                return write.kind != StateDecl::Kind::Memory && write.shadowScalarKind == kind;
            });
        }

        void emitScalarStateWriteRangeDesc(std::ostream &stream,
                                           const TableCompressibleScalarStateWriteDesc &first,
                                           const ScalarStateWriteRangeStep &step,
                                           std::size_t count,
                                           std::string_view indent,
                                           std::string_view descName)
        {
            stream << indent << "static constexpr " << scalarStateWriteRangeDescTypeName(first.kind) << " " << descName
                   << "{\n";
            stream << indent << "    " << count << "u,\n";
            stream << indent << "    " << first.condIndex << "u,\n";
            stream << indent << "    " << step.cond << ",\n";
            stream << indent << "    " << first.touchedIndex << "u,\n";
            stream << indent << "    " << step.touched << ",\n";
            stream << indent << "    " << first.shadowDataIndex << "u,\n";
            stream << indent << "    " << step.shadowData << ",\n";
            stream << indent << "    " << first.stateDataIndex << "u,\n";
            stream << indent << "    " << step.stateData << ",\n";
            stream << indent << "    " << first.nextIndex << "u,\n";
            stream << indent << "    " << step.next << ",\n";
            stream << indent << "    " << first.maskIndex << "u,\n";
            stream << indent << "    " << step.mask << ",\n";
            stream << indent << "    " << first.shadowIndex << "u,\n";
            stream << indent << "    " << step.shadow << ",\n";
            stream << indent << "};\n";
            stream << indent << scalarStateWriteRangeHelperName(first.kind) << "(" << descName << ");\n";
        }

        std::optional<std::string> emitCompressedScalarStateWriteCall(std::ostream &stream,
                                                                      const Graph &graph,
                                                                      const EmitModel &model,
                                                                      OperationId opId,
                                                                      const Operation &op,
                                                                      std::string_view indent)
        {
            const auto writeIt = model.writeByOp.find(opId);
            if (writeIt == model.writeByOp.end())
            {
                return std::string("write metadata missing: ") + std::string(op.symbolText());
            }
            const WriteDecl &write = writeIt->second;
            const auto stateIt = model.stateBySymbol.find(write.symbol);
            if (stateIt == model.stateBySymbol.end())
            {
                return std::string("write state missing: ") + write.symbol;
            }
            const StateDecl &state = stateIt->second;
            if (write.kind == StateDecl::Kind::Memory || isWideLogicWidth(state.width))
            {
                return std::string("write is not compressible scalar state write: ") + std::string(op.symbolText());
            }
            const auto operands = op.operands();
            if (operands.size() < 3)
            {
                return std::string("scalar state write missing operands: ") + std::string(op.symbolText());
            }
            const StateShadowDecl &shadow = model.stateShadows[write.shadowIndex];
            stream << indent << "// op " << op.symbolText() << "\n";
            stream << indent << scalarStateWriteHelperName(write.shadowScalarKind) << "("
                   << truthyLogicValueExpr(graph, model, operands[0]) << ", "
                   << stateShadowTouchedRef(shadow) << ", "
                   << stateShadowDataRef(shadow, state) << ", "
                   << stateRef(state) << ", "
                   << valueRef(model, operands[1]) << ", "
                   << valueRef(model, operands[2]) << ", "
                   << write.shadowIndex << "u);\n";
            return std::nullopt;
        }

        std::optional<std::string> emitWritePortBody(std::ostream &stream,
                                                     const Graph &graph,
                                                     const EmitModel &model,
                                                     OperationId opId,
                                                     const Operation &op,
                                                     std::string_view indent)
        {
            const auto writeIt = model.writeByOp.find(opId);
            if (writeIt == model.writeByOp.end())
            {
                return std::string("write metadata missing: ") + std::string(op.symbolText());
            }
            const WriteDecl &write = writeIt->second;
            const auto operands = op.operands();
            stream << indent << "// op " << op.symbolText() << "\n";
            stream << indent
                   << "// Update the next-state shadow here; commit_state_updates() applies it after all scheduled supernodes finish.\n";
            const std::string innerIndent = std::string(indent) + "    ";
            stream << indent << "{\n";
            if (write.kind == StateDecl::Kind::Memory)
            {
                const StateDecl &state = model.stateBySymbol.at(write.symbol);
                const std::string writeTouchedRef = memoryWriteTouchedRef(write);
                const std::string writeAddrRef = memoryWriteAddrRef(write);
                const std::string writeDataRef = memoryWriteDataRef(write, state);
                const std::string writeMaskRef = memoryWriteMaskRef(write, state);
                if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kConstZero)
                {
                    stream << innerIndent << "// constant zero mask: no memory update\n";
                }
                else
                {
                    stream << innerIndent << writeAddrRef << " = "
                           << memoryWriteRowExpr(graph, model, write, operands[1], state) << ";\n";
                    stream << innerIndent << writeDataRef << " = " << valueRef(model, operands[2]) << ";\n";
                    if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kDynamic)
                    {
                        stream << innerIndent << writeMaskRef << " = " << valueRef(model, operands[3]) << ";\n";
                    }
                    stream << innerIndent << writeTouchedRef << " = 1;\n";
                    stream << innerIndent
                           << "grhsim_mark_pending_write(touched_write_indices_, touched_write_flags_, touched_write_count_, "
                           << write.shadowIndex << "u);\n";
                }
            }
            else
            {
                const StateDecl &state = model.stateBySymbol.at(write.symbol);
                const std::string writeTouchedRef = stateShadowTouchedRef(model.stateShadows[write.shadowIndex]);
                const std::string writeDataRef = stateShadowDataRef(model.stateShadows[write.shadowIndex], state);
                const std::string shadowBaseExpr =
                    writeTouchedRef + " ? " + writeDataRef + " : " + stateRef(state);
                stream << innerIndent << "const auto state_shadow_base = " << shadowBaseExpr << ";\n";
                if (isWideLogicWidth(state.width))
                {
                    stream << innerIndent << writeDataRef << " = grhsim_merge_words_masked(state_shadow_base, "
                           << valueRef(model, operands[1]) << ", " << valueRef(model, operands[2]) << ", "
                           << state.width << ");\n";
                }
                else
                {
                    stream << innerIndent << writeDataRef << " = static_cast<" << state.cppType
                           << ">((state_shadow_base & ~" << valueRef(model, operands[2]) << ") | ("
                           << valueRef(model, operands[1]) << " & " << valueRef(model, operands[2])
                           << "));\n";
                }
                stream << innerIndent << writeTouchedRef << " = 1;\n";
                stream << innerIndent
                       << "grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, "
                       << write.shadowIndex << "u);\n";
            }
            stream << indent << "}\n";
            return std::nullopt;
        }

        void emitClearAllEventEdges(std::ostream &stream,
                                    const EmitModel &model,
                                    std::string_view indent)
        {
            for (ValueId value : model.allEventValues)
            {
                stream << indent << model.eventEdgeFieldByValue.at(value) << " = grhsim_event_edge_kind::none;\n";
            }
        }

        std::string memoryWriteRowExpr(const Graph &graph,
                                       const EmitModel &model,
                                       const WriteDecl &write,
                                       ValueId addrValue,
                                       const StateDecl &state)
        {
            const std::string addrExpr = valueRef(model, addrValue);
            switch (write.memoryAddrMode)
            {
            case WriteDecl::MemoryAddrMode::kPow2Wrap:
                return "grhsim_index_pow2_words(" + addrExpr + ", " + std::to_string(write.memoryRowMask) + ")";
            case WriteDecl::MemoryAddrMode::kInRange:
                return "grhsim_index_in_range_words(" + addrExpr + ")";
            case WriteDecl::MemoryAddrMode::kGeneric:
            default:
                break;
            }
            return "grhsim_index_words(" + addrExpr + ", " + std::to_string(state.rowCount) + ")";
        }

        std::optional<std::string> emitSchedBatchFile(const std::filesystem::path &schedPath,
                                                      const std::filesystem::path &headerPath,
                                                      const std::string &className,
                                                      const Graph &graph,
                                                      const EmitModel &model,
                                                      const ScheduleRefs &schedule,
                                                      const ScheduleBatch &batch,
                                                      std::size_t waveformBatchCount,
                                                      std::uint64_t maxOutputFileBytes)
        {
            if (auto error = ensureOutputDirectory(schedPath))
            {
                return error;
            }
            LimitedOutputStream stream(schedPath, maxOutputFileBytes);
            if (!stream.isOpen())
            {
                return "failed to open output file: " + schedPath.string();
            }

            auto emitError = [](std::string_view message, std::string_view detail) -> std::optional<std::string>
            {
                if (detail.empty())
                {
                    return std::string(message);
                }
                return std::string(message) + ": " + std::string(detail);
            };

            stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            stream << "#include <cstdlib>\n";
            stream << "#include <iostream>\n\n";
            for (const auto &decl : model.dpiDecls)
            {
                stream << decl << '\n';
            }
            if (!model.dpiDecls.empty())
            {
                stream << '\n';
            }
            stream << "void " << className << "::eval_batch_" << batch.index << "(std::uint8_t &activeWordFlags)\n{\n";
            stream << "    // Batch " << batch.index
                   << ": evaluate active supernodes selected from one activity-flag word.\n";
            for (uint32_t supernodeId : batch.supernodeIds)
            {
                const std::size_t activeId =
                    supernodeId < model.activeIdBySupernode.size() ? model.activeIdBySupernode[supernodeId] : kInvalidIndex;
                const ActivationEmitContext activationContext{
                    .currentWordIndex = batch.activeFlagWordIndex,
                    .currentActiveId = activeId,
                    .localActiveExpr = "activeWordFlags"};
                const std::uint8_t supernodeMask =
                    static_cast<std::uint8_t>(UINT8_C(1) << (activeId % kActiveFlagBitsPerWord));
                stream << "\n";
                stream << "    // Supernode " << supernodeId << ": run when its activity flag is set.\n";
                stream << "    if ((activeWordFlags & UINT8_C(" << static_cast<unsigned>(supernodeMask) << ")) == 0) {\n";
                stream << "        goto supernode_" << supernodeId << "_end;\n";
                stream << "    }\n";
                stream << "    activeWordFlags = static_cast<std::uint8_t>(\n";
                stream << "        activeWordFlags & static_cast<std::uint8_t>(~UINT8_C("
                       << static_cast<unsigned>(supernodeMask) << ")));\n";
                stream << "    {\n";
                const std::vector<ScalarConcatPrefixCacheDecl> concatPrefixCacheDecls =
                    collectScalarConcatPrefixCaches(graph, schedule.supernodeToOps[supernodeId]);
                std::unordered_map<ScalarConcatPrefixCacheKey, std::string, ScalarConcatPrefixCacheKeyHash>
                    concatPrefixTemps;
                for (const auto &decl : concatPrefixCacheDecls)
                {
                    concatPrefixTemps.emplace(decl.key, decl.tempName);
                }
                std::size_t opIndex = 0;
                std::size_t nextConcatPrefixDecl = 0;
                const auto &supernodeOps = schedule.supernodeToOps[supernodeId];
                while (opIndex < supernodeOps.size())
                {
                    while (nextConcatPrefixDecl < concatPrefixCacheDecls.size() &&
                           concatPrefixCacheDecls[nextConcatPrefixDecl].firstUseIndex == opIndex)
                    {
                        const auto &decl = concatPrefixCacheDecls[nextConcatPrefixDecl];
                        stream << "        const auto " << decl.tempName << " = grhsim_cat_prefix("
                               << valueRef(model, decl.key.lhs) << ", " << decl.key.lhsWidth << ", "
                               << decl.key.rhsWidth << ");\n";
                        ++nextConcatPrefixDecl;
                    }
                    const auto opId = supernodeOps[opIndex];
                    const Operation op = graph.getOperation(opId);
                    if (isWritePortKind(op.kind()))
                    {
                        const auto eventExpr = exactEventExpr(graph, model, opId, op);
                        if (!eventExpr)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        if (isCompressibleScalarStateWrite(model, opId))
                        {
                            std::vector<TableCompressibleScalarStateWriteDesc> tableRunDescs;
                            tableRunDescs.reserve(16);
                            std::vector<OperationId> tableRunOpIds;
                            tableRunOpIds.reserve(16);
                            const auto firstTableDesc =
                                buildTableCompressibleScalarStateWriteDesc(graph, model, opId, op);
                            if (firstTableDesc)
                            {
                                tableRunDescs.push_back(*firstTableDesc);
                                tableRunOpIds.push_back(opId);
                            }
                            std::size_t runEnd = opIndex + 1;
                            while (runEnd < supernodeOps.size())
                            {
                                if (nextConcatPrefixDecl < concatPrefixCacheDecls.size() &&
                                    concatPrefixCacheDecls[nextConcatPrefixDecl].firstUseIndex == runEnd)
                                {
                                    break;
                                }
                                const Operation nextOp = graph.getOperation(supernodeOps[runEnd]);
                                if (!isWritePortKind(nextOp.kind()) ||
                                    !isCompressibleScalarStateWrite(model, supernodeOps[runEnd]))
                                {
                                    break;
                                }
                                const auto nextEventExpr =
                                    exactEventExpr(graph, model, supernodeOps[runEnd], nextOp);
                                if (!nextEventExpr)
                                {
                                    return emitError("unsupported exact event expression emit",
                                                     std::string(nextOp.symbolText()));
                                }
                                if (*nextEventExpr != *eventExpr)
                                {
                                    break;
                                }
                                if (firstTableDesc)
                                {
                                    const auto nextTableDesc = buildTableCompressibleScalarStateWriteDesc(
                                        graph, model, supernodeOps[runEnd], nextOp);
                                    if (!nextTableDesc || nextTableDesc->kind != firstTableDesc->kind)
                                    {
                                        break;
                                    }
                                    tableRunDescs.push_back(*nextTableDesc);
                                    tableRunOpIds.push_back(supernodeOps[runEnd]);
                                }
                                ++runEnd;
                            }
                            if (tableRunDescs.size() >= 4)
                            {
                                if (*eventExpr != "true")
                                {
                                    stream << "        if (" << *eventExpr << ") {\n";
                                }
                                std::size_t rangeStart = 0;
                                while (rangeStart < tableRunDescs.size())
                                {
                                    std::size_t rangeEnd = rangeStart + 1;
                                    if (rangeStart + 1 < tableRunDescs.size())
                                    {
                                        const auto step = scalarStateWriteRangeStep(tableRunDescs[rangeStart],
                                                                                    tableRunDescs[rangeStart + 1]);
                                        rangeEnd = rangeStart + 2;
                                        while (rangeEnd < tableRunDescs.size() &&
                                               scalarStateWriteRangeStep(tableRunDescs[rangeEnd - 1],
                                                                         tableRunDescs[rangeEnd]) == step)
                                        {
                                            ++rangeEnd;
                                        }
                                        if (rangeEnd - rangeStart >= 4)
                                        {
                                            stream << (*eventExpr != "true" ? "            " : "        ")
                                                   << "// compressed scalar state writes: "
                                                   << (rangeEnd - rangeStart) << " ops\n";
                                            const std::string descName =
                                                "kScalarStateWriteRange_" + std::to_string(batch.index) + "_" +
                                                std::to_string(supernodeId) + "_" + std::to_string(opIndex + rangeStart);
                                            emitScalarStateWriteRangeDesc(stream,
                                                                          tableRunDescs[rangeStart],
                                                                          step,
                                                                          rangeEnd - rangeStart,
                                                                          *eventExpr != "true" ? "            "
                                                                                               : "        ",
                                                                          descName);
                                            rangeStart = rangeEnd;
                                            continue;
                                        }
                                    }
                                    const auto runOpId = tableRunOpIds[rangeStart];
                                    const Operation runOp = graph.getOperation(runOpId);
                                    if (auto error =
                                            emitCompressedScalarStateWriteCall(stream,
                                                                               graph,
                                                                               model,
                                                                               runOpId,
                                                                               runOp,
                                                                               *eventExpr != "true" ? "            "
                                                                                                    : "        "))
                                    {
                                        return emitError(*error, std::string(runOp.symbolText()));
                                    }
                                    ++rangeStart;
                                }
                                if (*eventExpr != "true")
                                {
                                    stream << "        }\n";
                                }
                                opIndex = runEnd;
                                continue;
                            }
                            if (*eventExpr != "true")
                            {
                                stream << "        if (" << *eventExpr << ") {\n";
                            }
                            for (std::size_t runIndex = opIndex; runIndex < runEnd; ++runIndex)
                            {
                                const auto runOpId = supernodeOps[runIndex];
                                const Operation runOp = graph.getOperation(runOpId);
                                if (auto error =
                                        emitCompressedScalarStateWriteCall(stream,
                                                                           graph,
                                                                           model,
                                                                           runOpId,
                                                                           runOp,
                                                                           *eventExpr != "true" ? "            "
                                                                                                : "        "))
                                {
                                    return emitError(*error, std::string(runOp.symbolText()));
                                }
                            }
                            if (*eventExpr != "true")
                            {
                                stream << "        }\n";
                            }
                            opIndex = runEnd;
                            continue;
                        }
                        const auto guardKey = writePortGuardKey(graph, model, opId, op);
                        if (!guardKey)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        std::size_t runEnd = opIndex + 1;
                        while (runEnd < supernodeOps.size())
                        {
                            if (nextConcatPrefixDecl < concatPrefixCacheDecls.size() &&
                                concatPrefixCacheDecls[nextConcatPrefixDecl].firstUseIndex == runEnd)
                            {
                                break;
                            }
                            const Operation nextOp = graph.getOperation(supernodeOps[runEnd]);
                            if (!isWritePortKind(nextOp.kind()))
                            {
                                break;
                            }
                            const auto nextGuardKey =
                                writePortGuardKey(graph, model, supernodeOps[runEnd], nextOp);
                            if (!nextGuardKey)
                            {
                                return emitError("unsupported exact event expression emit",
                                                 std::string(nextOp.symbolText()));
                            }
                            if (!(*nextGuardKey == *guardKey))
                            {
                                break;
                            }
                            ++runEnd;
                        }
                        stream << "        if ((" << guardKey->condExpr << ") && (" << guardKey->eventExpr << ")) {\n";
                        for (std::size_t runIndex = opIndex; runIndex < runEnd; ++runIndex)
                        {
                            const auto runOpId = supernodeOps[runIndex];
                            const Operation runOp = graph.getOperation(runOpId);
                            if (auto error = emitWritePortBody(stream, graph, model, runOpId, runOp, "            "))
                            {
                                return emitError(*error, std::string(runOp.symbolText()));
                            }
                        }
                        stream << "        }\n";
                        opIndex = runEnd;
                        continue;
                    }
                    const auto operands = op.operands();
                    stream << "        // op " << op.symbolText() << "\n";
                    switch (op.kind())
                    {
                    case OperationKind::kConstant:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        const ValueId resultValue = op.results().front();
                        const auto expr = constantExpr(graph, op, resultValue);
                        if (!expr)
                        {
                            return emitError("unsupported constant emit", std::string(op.symbolText()));
                        }
                        const std::string lhs = valueRef(model, resultValue);
                        const bool materialized = isMaterializedValue(model, resultValue);
                        const bool needChangeDetect = valueNeedsTrackedChange(model, resultValue);
                        if (!materialized)
                        {
                            emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                            if (graph.valueType(resultValue) == ValueType::Logic)
                            {
                                if (isWideLogicValue(graph, resultValue))
                                {
                                    stream << "        const auto " << lhs << " = " << *expr << ";\n";
                                }
                                else
                                {
                                    stream << "        const auto " << lhs << " = static_cast<" << cppTypeForValue(graph, resultValue)
                                           << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << *expr << "), "
                                           << graph.valueWidth(resultValue) << "));\n";
                                }
                            }
                            else
                            {
                                stream << "        const " << cppTypeForValue(graph, resultValue) << ' '
                                       << lhs << " = " << *expr << ";\n";
                            }
                            break;
                        }
                        (void)lhs;
                        (void)needChangeDetect;
                        emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                        stream << "        // materialized constant is initialized once in init(); no runtime update needed.\n";
                        break;
                    }
                    case OperationKind::kRegisterReadPort:
                    case OperationKind::kLatchReadPort:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        auto targetSymbol = getAttribute<std::string>(op, op.kind() == OperationKind::kRegisterReadPort ? "regSymbol" : "latchSymbol");
                        if (!targetSymbol)
                        {
                            return emitError("storage read missing symbol", std::string(op.symbolText()));
                        }
                        const auto stateIt = model.stateBySymbol.find(*targetSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            return emitError("storage read target missing", *targetSymbol);
                        }
                        const std::string stateExpr = stateRef(stateIt->second);
                        const std::string lhs = valueRef(model, op.results().front());
                        const ValueId resultValue = op.results().front();
                        const bool materialized = isMaterializedValue(model, resultValue);
                        const bool needChangeDetect = valueNeedsTrackedChange(model, resultValue);
                        if (!materialized)
                        {
                            emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                            stream << "        const auto " << lhs << " = " << stateExpr << ";\n";
                            break;
                        }
                        emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                        if (isWideLogicValue(graph, op.results().front()))
                        {
                            if (needChangeDetect)
                            {
                                stream << "        if (grhsim_assign_words(" << lhs << ", " << stateExpr << ", "
                                       << graph.valueWidth(op.results().front()) << ")) {\n";
                                emitChangedValuePropagation(stream, model, resultValue, "            ", &activationContext);
                                stream << "        }\n";
                            }
                            else
                            {
                                stream << "        " << lhs << " = " << stateExpr << ";\n";
                            }
                        }
                        else
                        {
                            if (needChangeDetect)
                            {
                                stream << "        if (" << lhs << " != " << stateExpr << ") {\n";
                                emitChangedValueEffects(stream, model, resultValue, lhs, stateExpr, "            ", &activationContext);
                                stream << "            " << lhs << " = " << stateExpr << ";\n";
                                stream << "        }\n";
                            }
                            else
                            {
                                stream << "        " << lhs << " = " << stateExpr << ";\n";
                            }
                        }
                        break;
                    }
                    case OperationKind::kMemoryReadPort:
                    {
                        if (op.results().empty() || operands.empty())
                        {
                            break;
                        }
                        auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                        if (!memSymbol)
                        {
                            return emitError("memory read missing memSymbol", std::string(op.symbolText()));
                        }
                        const auto stateIt = model.stateBySymbol.find(*memSymbol);
                        if (stateIt == model.stateBySymbol.end())
                        {
                            return emitError("memory read target missing", *memSymbol);
                        }
                        const StateDecl &state = stateIt->second;
                        const std::string stateExpr = stateRef(state);
                        const std::string lhs = valueRef(model, op.results().front());
                        const ValueId resultValue = op.results().front();
                        const bool materialized = isMaterializedValue(model, resultValue);
                        const bool needChangeDetect = valueNeedsTrackedChange(model, resultValue);
                        if (!materialized)
                        {
                            emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                            stream << "        const auto " << lhs << " = [&]() -> " << cppTypeForValue(graph, resultValue)
                                   << " {\n";
                            stream << "            const std::size_t row = grhsim_index_words(" << valueRef(model, operands[0])
                                   << ", " << state.rowCount << ");\n";
                            stream << "            if (row >= " << state.rowCount << ") {\n";
                            stream << "                return " << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())) << ";\n";
                            stream << "            }\n";
                            stream << "            return " << stateExpr << "[row];\n";
                            stream << "        }();\n";
                            break;
                        }
                        emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                        stream << "        {\n";
                        stream << "            const std::size_t row = grhsim_index_words(" << valueRef(model, operands[0])
                               << ", " << state.rowCount << ");\n";
                        if (isWideLogicValue(graph, op.results().front()))
                        {
                            stream << "            if (row >= " << state.rowCount << ") {\n";
                            if (needChangeDetect)
                            {
                                stream << "                if (grhsim_assign_words(" << lhs << ", "
                                       << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())) << ", "
                                       << graph.valueWidth(op.results().front()) << ")) {\n";
                                emitChangedValuePropagation(stream, model, resultValue, "                    ", &activationContext);
                                stream << "                }\n";
                            }
                            else
                            {
                                stream << "                " << lhs << " = "
                                       << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())) << ";\n";
                            }
                            stream << "            } else {\n";
                            stream << "                const auto next_value = " << stateExpr << "[row];\n";
                            if (needChangeDetect)
                            {
                                stream << "                if (grhsim_assign_words(" << lhs << ", next_value, "
                                       << graph.valueWidth(op.results().front()) << ")) {\n";
                                emitChangedValuePropagation(stream, model, resultValue, "                    ", &activationContext);
                                stream << "                }\n";
                            }
                            else
                            {
                                stream << "                " << lhs << " = next_value;\n";
                            }
                            stream << "            }\n";
                        }
                        else
                        {
                            stream << "            if (row >= " << state.rowCount << ") {\n";
                            if (needChangeDetect)
                            {
                                stream << "                if (" << lhs << " != " << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front()))
                                       << ") {\n";
                                emitChangedValueEffects(stream,
                                                        model,
                                                        resultValue,
                                                        lhs,
                                                        defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())),
                                                        "                    ",
                                                        &activationContext);
                                stream << "                    " << lhs << " = " << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())) << ";\n";
                                stream << "                }\n";
                            }
                            else
                            {
                                stream << "                " << lhs << " = " << defaultInitExprForLogicWidth(graph.valueWidth(op.results().front())) << ";\n";
                            }
                            stream << "            } else {\n";
                            stream << "                const auto next_value = " << stateExpr << "[row];\n";
                            if (needChangeDetect)
                            {
                                stream << "                if (" << lhs << " != next_value) {\n";
                                emitChangedValueEffects(stream, model, resultValue, lhs, "next_value", "                    ", &activationContext);
                                stream << "                    " << lhs << " = next_value;\n";
                                stream << "                }\n";
                            }
                            else
                            {
                                stream << "                " << lhs << " = next_value;\n";
                            }
                            stream << "            }\n";
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kAssign:
                    case OperationKind::kAdd:
                    case OperationKind::kSub:
                    case OperationKind::kMul:
                    case OperationKind::kDiv:
                    case OperationKind::kMod:
                    case OperationKind::kAnd:
                    case OperationKind::kOr:
                    case OperationKind::kXor:
                    case OperationKind::kXnor:
                    case OperationKind::kNot:
                    case OperationKind::kEq:
                    case OperationKind::kNe:
                    case OperationKind::kCaseEq:
                    case OperationKind::kCaseNe:
                    case OperationKind::kWildcardEq:
                    case OperationKind::kWildcardNe:
                    case OperationKind::kLt:
                    case OperationKind::kLe:
                    case OperationKind::kGt:
                    case OperationKind::kGe:
                    case OperationKind::kLogicAnd:
                    case OperationKind::kLogicOr:
                    case OperationKind::kLogicNot:
                    case OperationKind::kReduceAnd:
                    case OperationKind::kReduceNand:
                    case OperationKind::kReduceOr:
                    case OperationKind::kReduceNor:
                    case OperationKind::kReduceXor:
                    case OperationKind::kReduceXnor:
                    case OperationKind::kShl:
                    case OperationKind::kLShr:
                    case OperationKind::kAShr:
                    case OperationKind::kMux:
                    case OperationKind::kConcat:
                    case OperationKind::kReplicate:
                    case OperationKind::kSliceStatic:
                    case OperationKind::kSliceDynamic:
                    case OperationKind::kSliceArray:
                    case OperationKind::kSystemFunction:
                    {
                        if (op.results().empty())
                        {
                            break;
                        }
                        const ValueId resultValue = op.results().front();
                        if (op.kind() == OperationKind::kSystemFunction &&
                            graph.valueType(resultValue) != ValueType::Logic)
                        {
                            return emitError("unsupported kSystemFunction result type in grhsim-cpp emit",
                                             std::string(op.symbolText()));
                        }
                        if (op.kind() == OperationKind::kSystemFunction)
                        {
                            const std::string name = getAttribute<std::string>(op, "name").value_or(std::string());
                            if (name == "fopen")
                            {
                                if (isWideLogicValue(graph, resultValue))
                                {
                                    return emitError("unsupported wide kSystemFunction fopen result in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                if (operands.empty() || operands.size() > 2)
                                {
                                    return emitError("unsupported kSystemFunction fopen arity in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                const std::string lhs = valueRef(model, resultValue);
                                const std::string pathExpr =
                                    "grhsim_task_default_arg_text(" + systemTaskArgExpr(graph, model, operands[0]) + ")";
                                const std::string modeExpr =
                                    operands.size() >= 2
                                        ? ("grhsim_task_default_arg_text(" +
                                           systemTaskArgExpr(graph, model, operands[1]) + ")")
                                        : std::string("std::string(\"r\")");
                                std::string procGuard = "true";
                                if (systemFunctionRunsOnlyOnInitialEval(op))
                                {
                                    procGuard = "first_eval_";
                                }
                                emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                                stream << "        if (" << procGuard << ") {\n";
                                stream << "            const auto next_value = static_cast<"
                                       << cppTypeForValue(graph, resultValue) << ">(grhsim_trunc_u64(open_file_handle("
                                       << pathExpr << ", " << modeExpr << "), "
                                       << graph.valueWidth(resultValue) << "));\n";
                                stream << "            if (" << lhs << " != next_value) {\n";
                                emitChangedValueEffects(stream, model, resultValue, lhs, "next_value", "                ", &activationContext);
                                stream << "                " << lhs << " = next_value;\n";
                                stream << "            }\n";
                                stream << "        }\n";
                                break;
                            }
                            if (name == "ferror")
                            {
                                if (isWideLogicValue(graph, resultValue))
                                {
                                    return emitError("unsupported wide kSystemFunction ferror result in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                if (operands.size() != 1)
                                {
                                    return emitError("unsupported kSystemFunction ferror arity in grhsim-cpp emit",
                                                     std::string(op.symbolText()));
                                }
                                const std::string lhs = valueRef(model, resultValue);
                                emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                                stream << "        {\n";
                                stream << "            const auto next_value = static_cast<"
                                       << cppTypeForValue(graph, resultValue)
                                       << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(file_error_status(grhsim_task_arg_u64("
                                       << systemTaskArgExpr(graph, model, operands[0]) << "))), "
                                       << graph.valueWidth(resultValue) << "));\n";
                                stream << "            if (" << lhs << " != next_value) {\n";
                                emitChangedValueEffects(stream, model, resultValue, lhs, "next_value", "                ", &activationContext);
                                stream << "                " << lhs << " = next_value;\n";
                                stream << "            }\n";
                                stream << "        }\n";
                                break;
                            }
                        }
                        if (opNeedsWordLogicEmit(graph, op))
                        {
                            std::string emitErrorText;
                            if (!emitWordLogicOperation(stream, graph, model, op, emitErrorText))
                            {
                                return emitError(emitErrorText, std::string(op.symbolText()));
                            }
                            break;
                        }
                        std::vector<std::string> operandExprs;
                        operandExprs.reserve(operands.size());
                        for (ValueId operand : operands)
                        {
                            operandExprs.push_back(valueRef(model, operand));
                        }
                        const ScalarLogicExpr rhs =
                            scalarAssignmentExpr(op.kind(), operandExprs, op, graph, &concatPrefixTemps);
                        if (rhs.expr.empty())
                        {
                            return emitError("unsupported scalar expression emit", std::string(op.symbolText()));
                        }
                        const std::string lhs = valueRef(model, resultValue);
                        const bool materialized = isMaterializedValue(model, resultValue);
                        const bool needChangeDetect = valueNeedsTrackedChange(model, resultValue);
                        if (!materialized)
                        {
                            emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                            stream << "        const auto " << lhs << " = static_cast<" << cppTypeForValue(graph, resultValue)
                                   << ">(";
                            if (rhs.alreadyBoundedToResultWidth)
                            {
                                stream << rhs.expr;
                            }
                            else
                            {
                                stream << "grhsim_trunc_u64(" << rhs.expr << ", " << graph.valueWidth(resultValue) << ")";
                            }
                            stream << ");\n";
                            break;
                        }
                        emitValueAssignmentComment(stream, graph, model, resultValue, "        ");
                        stream << "        {\n";
                        stream << "            const auto next_value = static_cast<" << cppTypeForValue(graph, resultValue)
                               << ">(";
                        if (rhs.alreadyBoundedToResultWidth)
                        {
                            stream << rhs.expr;
                        }
                        else
                        {
                            stream << "grhsim_trunc_u64(" << rhs.expr << ", " << graph.valueWidth(resultValue) << ")";
                        }
                        stream << ");\n";
                        if (needChangeDetect)
                        {
                            stream << "            if (" << lhs << " != next_value) {\n";
                                emitChangedValueEffects(stream, model, resultValue, lhs, "next_value", "                ", &activationContext);
                                stream << "                " << lhs << " = next_value;\n";
                            stream << "            }\n";
                        }
                        else
                        {
                            stream << "            " << lhs << " = next_value;\n";
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kSystemTask:
                    {
                        if (operands.empty())
                        {
                            break;
                        }
                        const std::string condExpr = truthyLogicValueExpr(graph, model, operands[0]);
                        const auto name = getAttribute<std::string>(op, "name").value_or(std::string("display"));
                        const std::size_t eventCount = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}).size();
                        const std::size_t argEnd = operands.size() >= eventCount ? operands.size() - eventCount : operands.size();
                        if (systemTaskIsFinal(op))
                        {
                            break;
                        }
                        const auto eventExpr = exactEventExpr(graph, model, opId, op);
                        if (!eventExpr)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        stream << "        // System tasks are side effects. They execute in schedule order when condition and exact event both hit.\n";
                        std::string procGuard = "true";
                        if (systemTaskRunsOnlyOnInitialEval(op))
                        {
                            procGuard = "first_eval_";
                        }
                        const auto sampleIt = model.eventSamplesByOp.find(opId);
                        if (sampleIt != model.eventSamplesByOp.end() && !sampleIt->second.completedFieldName.empty())
                        {
                            procGuard = "(" + procGuard + ") && (!" + sampleIt->second.completedFieldName + ")";
                        }
                        stream << "        if ((" << condExpr << ") && (" << *eventExpr << ") && (" << procGuard << ")) {\n";
                        if (argEnd <= 1)
                        {
                            stream << "            execute_system_task(\"" << escapeCppString(name) << "\", {});\n";
                        }
                        else
                        {
                            stream << "            execute_system_task(\"" << escapeCppString(name) << "\", {";
                            for (std::size_t i = 1; i < argEnd; ++i)
                            {
                                if (i != 1)
                                {
                                    stream << ", ";
                                }
                                stream << systemTaskArgExpr(graph, model, operands[i]);
                            }
                            stream << "});\n";
                        }
                        if (sampleIt != model.eventSamplesByOp.end() && !sampleIt->second.completedFieldName.empty())
                        {
                            stream << "            " << sampleIt->second.completedFieldName << " = true;\n";
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kDpicCall:
                    {
                        const auto targetImport = getAttribute<std::string>(op, "targetImportSymbol");
                        const auto inArgName = getAttribute<std::vector<std::string>>(op, "inArgName").value_or(std::vector<std::string>{});
                        const auto outArgName = getAttribute<std::vector<std::string>>(op, "outArgName").value_or(std::vector<std::string>{});
                        const bool hasReturn = getAttribute<bool>(op, "hasReturn").value_or(false);
                        if (!targetImport)
                        {
                            return emitError("kDpicCall missing targetImportSymbol", std::string(op.symbolText()));
                        }
                        auto importIt = model.dpiImportBySymbol.find(*targetImport);
                        if (importIt == model.dpiImportBySymbol.end())
                        {
                            return emitError("kDpicCall target import not found", *targetImport);
                        }
                        const DpiImportDecl &decl = importIt->second;
                        const std::string condExpr =
                            operands.empty() ? "true" : truthyLogicValueExpr(graph, model, operands[0]);
                        const auto eventExpr = exactEventExpr(graph, model, opId, op);
                        if (!eventExpr)
                        {
                            return emitError("unsupported exact event expression emit", std::string(op.symbolText()));
                        }
                        stream << "        // DPIC calls may produce side effects and output values, so they stay as explicit schedule boundaries.\n";
                        stream << "        if ((" << condExpr << ") && (" << *eventExpr << ")) {\n";
                        std::vector<std::string> deferredArgs;
                        std::size_t resultBase = hasReturn ? 1u : 0u;
                        for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                        {
                            const std::string &direction = decl.argsDirection[i];
                            const std::string &formalName = decl.argsName[i];
                            const std::string_view argType =
                                i < decl.argsType.size() ? std::string_view(decl.argsType[i]) : std::string_view("logic");
                            const int64_t argWidth = i < decl.argsWidth.size() ? decl.argsWidth[i] : 1;
                            const bool argSigned = i < decl.argsSigned.size() ? decl.argsSigned[i] : false;
                            if (direction == "input")
                            {
                                const int inputIndex = findNameIndex(inArgName, formalName);
                                if (inputIndex < 0)
                                {
                                    return emitError("kDpicCall missing matching input arg", formalName);
                                }
                                deferredArgs.push_back(dpiValueExpr(graph,
                                                                    model,
                                                                    operands[1 + static_cast<std::size_t>(inputIndex)],
                                                                    argType,
                                                                    argWidth,
                                                                    argSigned));
                            }
                            else if (direction == "output")
                            {
                                const int outputIndex = findNameIndex(outArgName, formalName);
                                if (outputIndex < 0)
                                {
                                    return emitError("kDpicCall missing matching output arg", formalName);
                                }
                                const std::string tempName = "dpi_out_" + std::to_string(i);
                                stream << "            "
                                       << dpiBaseCppType(argType, argWidth, argSigned)
                                       << " " << tempName << "{};\n";
                                deferredArgs.push_back("&" + tempName);
                            }
                            else
                            {
                                return emitError("kDpicCall inout args are not supported in grhsim-cpp emit",
                                                 std::string(op.symbolText()));
                            }
                        }
                        if (hasReturn)
                        {
                            const ValueId returnValue = op.results()[0];
                            stream << "            auto dpi_ret = " << sanitizeIdentifier(decl.symbol)
                                   << "(" << joinStrings(deferredArgs, ", ") << ");\n";
                            stream << "            if (" << valueRef(model, returnValue) << " != dpi_ret) {\n";
                            emitChangedValueEffects(stream,
                                                    model,
                                                    returnValue,
                                                    valueRef(model, returnValue),
                                                    "dpi_ret",
                                                    "                ",
                                                    &activationContext);
                            stream << "                " << valueRef(model, returnValue) << " = dpi_ret;\n";
                            stream << "            }\n";
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                if (direction == "output")
                                {
                                    const int outputIndex = findNameIndex(outArgName, decl.argsName[i]);
                                    if (outputIndex < 0)
                                    {
                                        continue;
                                    }
                                    const std::string tempName = "dpi_out_" + std::to_string(i);
                                    const ValueId resultValue =
                                        op.results()[resultBase + static_cast<std::size_t>(outputIndex)];
                                    const std::string lhs =
                                        valueRef(model, resultValue);
                                    stream << "            if (" << lhs << " != " << tempName << ") {\n";
                                    emitChangedValueEffects(stream, model, resultValue, lhs, tempName, "                ", &activationContext);
                                    stream << "                " << lhs << " = " << tempName << ";\n";
                                    stream << "            }\n";
                                }
                            }
                        }
                        else
                        {
                            stream << "            " << sanitizeIdentifier(decl.symbol) << "(" << joinStrings(deferredArgs, ", ") << ");\n";
                            for (std::size_t i = 0; i < decl.argsDirection.size(); ++i)
                            {
                                const std::string &direction = decl.argsDirection[i];
                                if (direction == "output")
                                {
                                    const int outputIndex = findNameIndex(outArgName, decl.argsName[i]);
                                    if (outputIndex < 0)
                                    {
                                        continue;
                                    }
                                    const std::string tempName = "dpi_out_" + std::to_string(i);
                                    const ValueId resultValue =
                                        op.results()[resultBase + static_cast<std::size_t>(outputIndex)];
                                    const std::string lhs =
                                        valueRef(model, resultValue);
                                    stream << "            if (" << lhs << " != " << tempName << ") {\n";
                                    emitChangedValueEffects(stream, model, resultValue, lhs, tempName, "                ", &activationContext);
                                    stream << "                " << lhs << " = " << tempName << ";\n";
                                    stream << "            }\n";
                                }
                            }
                        }
                        stream << "        }\n";
                        break;
                    }
                    case OperationKind::kRegister:
                    case OperationKind::kLatch:
                    case OperationKind::kMemory:
                    case OperationKind::kDpicImport:
                        break;
                    default:
                        return emitError("unsupported op kind in grhsim-cpp emit", std::string(op.symbolText()));
                    }
                    ++opIndex;
                }
                stream << "    }\n";
                stream << "supernode_" << supernodeId << "_end:\n";
            }
            stream << "    return;\n";
            stream << "}\n";
            if (model.emitWaveform)
            {
                const auto [waveformBegin, waveformEnd] =
                    waveformSignalRangeForBatch(model.waveformSignals.size(), waveformBatchCount, batch.index);
                if (waveformBegin < waveformEnd)
                {
                    stream << "\nvoid " << className << "::register_waveform_batch_" << batch.index
                           << "(grhsim_fst_writer &writer)\n{\n";
                    emitWaveformSignalRegistrationBatch(
                        stream,
                        std::span<const WaveformSignalDecl>(
                            model.waveformSignals.data() + waveformBegin, waveformEnd - waveformBegin),
                        "    ");
                    stream << "}\n";
                    stream << "\nvoid " << className << "::dump_waveform_batch_" << batch.index << "()\n{\n";
                    for (std::size_t signalIndex = waveformBegin; signalIndex < waveformEnd; ++signalIndex)
                    {
                        emitWaveformSignalWrite(stream, model, model.waveformSignals[signalIndex], signalIndex, "    ");
                    }
                    stream << "}\n";
                }
            }
            if (auto error = finalizeOutputFile(stream, schedPath))
            {
                return *error + ": " + schedPath.string();
            }
            return std::nullopt;
        }

    } // namespace

    EmitResult EmitGrhSimCpp::emitImpl(const wolvrix::lib::grh::Design & /*design*/,
                                       std::span<const Graph *const> topGraphs,
                                       const EmitOptions &options)
    {
        EmitResult result;
        if (topGraphs.size() != 1)
        {
            reportError("emit grhsim-cpp expects exactly one top graph");
            result.success = false;
            return result;
        }
        if (!options.outputDir || options.outputDir->empty())
        {
            reportError("emit grhsim-cpp requires outputDir");
            result.success = false;
            return result;
        }
        if (options.session == nullptr)
        {
            reportError("emit grhsim-cpp requires session data");
            result.success = false;
            return result;
        }

        const auto &graph = *topGraphs.front();
        const std::string sessionPrefix = resolveSessionPathPrefix(graph, options) + ".activity_schedule.";
        const auto *supernodeToOps =
            getSessionValue<ActivityScheduleSupernodeToOps>(options, sessionPrefix + "supernode_to_ops");
        const auto *valueFanout =
            getSessionValue<ActivityScheduleValueFanout>(options, sessionPrefix + "value_fanout");
        const auto *topoOrder =
            getSessionValue<ActivityScheduleTopoOrder>(options, sessionPrefix + "topo_order");
        const auto *stateReadSupernodes =
            getSessionValue<ActivityScheduleStateReadSupernodes>(options, sessionPrefix + "state_read_supernodes");
        if (supernodeToOps == nullptr || valueFanout == nullptr || topoOrder == nullptr || stateReadSupernodes == nullptr)
        {
            reportError("missing activity-schedule session data", sessionPrefix);
            result.success = false;
            return result;
        }
        const ScheduleRefs schedule{
            .supernodeToOps = *supernodeToOps,
            .valueFanout = *valueFanout,
            .topoOrder = *topoOrder,
            .stateReadSupernodes = *stateReadSupernodes,
        };

        const std::size_t schedBatchMaxOps = parseScheduleBatchMaxOps(options);
        const std::size_t schedBatchMaxEstimatedLines = parseScheduleBatchMaxEstimatedLines(options);
        const WaveformMode waveformMode = parseWaveformMode(options);
        const PerfMode perfMode = parsePerfMode(options);
        const std::unordered_set<ValueId, ValueIdHash> waveformValueIds =
            waveformMode == WaveformMode::kDeclaredSymbols ? collectDeclaredSymbolWaveformValueIds(graph)
                                                           : std::unordered_set<ValueId, ValueIdHash>{};

#if !WOLVRIX_HAVE_LIBFST
        if (waveformMode != WaveformMode::kOff)
        {
            reportError("waveform emission requested, but wolvrix was built without libfst support");
            result.success = false;
            return result;
        }
#endif

        const std::vector<ScheduleBatch> scheduleBatches =
            buildScheduleBatches(graph, schedule, schedBatchMaxOps, schedBatchMaxEstimatedLines);

        EmitModel model;
        std::string buildError;
        if (!buildModel(graph, schedule, scheduleBatches, waveformValueIds, model, buildError))
        {
            reportError(buildError, graph.symbol());
            result.success = false;
            return result;
        }
        model.emitWaveform = waveformMode != WaveformMode::kOff;
        model.emitPerf = perfMode != PerfMode::kOff;
        if (waveformMode == WaveformMode::kDeclaredSymbols)
        {
            collectDeclaredSymbolWaveformSignals(graph, model, model.waveformSignals);
        }
        for (OperationId opId : graph.operations())
        {
            const Operation op = graph.getOperation(opId);
            if (op.kind() != OperationKind::kSystemFunction)
            {
                continue;
            }
            const std::string name = getAttribute<std::string>(op, "name").value_or(std::string());
            if (name == "fopen" && !systemFunctionRunsOnlyOnInitialEval(op))
            {
                reportWarning("$fopen may execute multiple times; use initial without timing control for one-shot open semantics",
                              std::string(op.symbolText()));
            }
        }

        const std::filesystem::path outDir = resolveOutputDir(options);
        const std::string normalizedTop = sanitizeIdentifier(graph.symbol());
        const std::string className = "GrhSIM_" + normalizedTop;
        const std::string prefix = "grhsim_" + normalizedTop;
        if (auto error = removeStaleGeneratedArtifacts(outDir, prefix))
        {
            reportError(*error, outDir.string());
            result.success = false;
            return result;
        }
        const std::filesystem::path headerPath = outDir / (prefix + ".hpp");
        const std::filesystem::path runtimePath = outDir / (prefix + "_runtime.hpp");
        const std::filesystem::path declaredValueIndexPath = outDir / (prefix + "_declared_value_index.txt");
        const std::filesystem::path statePath = outDir / (prefix + "_state.cpp");
        const std::filesystem::path evalPath = outDir / (prefix + "_eval.cpp");
        std::vector<std::filesystem::path> schedPaths;
        schedPaths.reserve(scheduleBatches.size());
        for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
        {
            schedPaths.push_back(outDir / (prefix + "_sched_" + std::to_string(batchIndex) + ".cpp"));
        }
        const std::filesystem::path makefilePath = outDir / "Makefile";
        const std::uint64_t maxOutputFileBytes = effectiveMaxOutputFileBytes(options);
        const std::filesystem::path libfstSrcDir = std::filesystem::path(WOLVRIX_SOURCE_DIR) / "external/libfst/src";

        struct InitChunkSpec
        {
            enum class Kind
            {
                kPublicInputs,
                kPublicOutputs,
                kValues,
                kConstantValues,
                kRandomSeed,
                kStateStorage,
                kStates,
                kStateShadows,
                kWrites,
                kPrevInputs,
                kEventEdges,
                kEvalState,
                kRuntimeState,
            };

            Kind kind = Kind::kEvalState;
            std::string methodName;
            std::vector<ValueId> values;
            std::vector<std::string> stateSymbols;
            std::vector<std::size_t> indices;
        };

        // Keep init shards small enough for XiangShan-scale model compilation.
        constexpr std::size_t kStateInitChunkTargetBytes = 2u * 1024u * 1024u;
        std::vector<InitChunkSpec> initChunks;
        std::size_t nextInitChunkIndex = 0;

        auto makeInitChunk = [&](InitChunkSpec::Kind kind) -> InitChunkSpec
        {
            InitChunkSpec chunk;
            chunk.kind = kind;
            chunk.methodName = "init_chunk_" + std::to_string(nextInitChunkIndex++);
            return chunk;
        };

        auto addFixedInitChunk = [&](InitChunkSpec::Kind kind)
        {
            initChunks.push_back(makeInitChunk(kind));
        };

        auto estimateValueInitBytes = [&](ValueId valueId) -> std::size_t
        {
            const auto it = model.valueFieldByValue.find(valueId);
            if (it == model.valueFieldByValue.end())
            {
                return 0;
            }
            return std::max<std::size_t>(96, it->second.size() + defaultInitExpr(graph, valueId).size() + 32);
        };

        auto estimateConstantValueInitBytes = [&](ValueId valueId) -> std::size_t
        {
            const auto it = model.valueFieldByValue.find(valueId);
            if (it == model.valueFieldByValue.end())
            {
                return 0;
            }
            std::size_t exprBytes = 64;
            const OperationId defOpId = graph.valueDef(valueId);
            if (defOpId.valid())
            {
                if (const auto expr = constantExpr(graph, graph.getOperation(defOpId), valueId))
                {
                    exprBytes = expr->size();
                }
            }
            return std::max<std::size_t>(128, it->second.size() + exprBytes + 48);
        };

        auto estimateStateInitBytes = [&](const std::string &stateSymbol) -> std::size_t
        {
            const StateDecl &state = model.stateBySymbol.at(stateSymbol);
            std::size_t bytes = 192;
            if (state.kind == StateDecl::Kind::Memory)
            {
                bytes += 96;
                for (const auto &rowExpr : state.memoryInitRowExprs)
                {
                    if (!rowExpr.has_value())
                    {
                        continue;
                    }
                    bytes += rowExpr->expr.size() + 32;
                }
            }
            else if (state.initExpr)
            {
                bytes += state.initExpr->expr.size() + 24;
            }
            return bytes;
        };

        auto estimateStateShadowInitBytes = [&](std::size_t shadowIndex) -> std::size_t
        {
            const StateShadowDecl &shadow = model.stateShadows[shadowIndex];
            const StateDecl &state = model.stateBySymbol.at(shadow.symbol);
            return std::max<std::size_t>(96, state.cppType.size() + 64);
        };

        auto estimateWriteInitBytes = [&](std::size_t writeIndex) -> std::size_t
        {
            const WriteDecl &write = model.writes[writeIndex];
            std::size_t bytes = 112;
            if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kDynamic)
            {
                bytes += 24;
            }
            return bytes;
        };

        auto estimatePrevInputBytes = [&](ValueId valueId) -> std::size_t
        {
            return std::max<std::size_t>(
                64, model.prevInputFieldByValue.at(valueId).size() + model.inputFieldByValue.at(valueId).size() + 24);
        };

        auto estimateEventEdgeBytes = [&](ValueId valueId) -> std::size_t
        {
            return std::max<std::size_t>(48, model.eventEdgeFieldByValue.at(valueId).size() + 24);
        };

        auto addValueChunks = [&](InitChunkSpec::Kind kind, const std::vector<ValueId> &items, const auto &estimateBytes)
        {
            if (kind == InitChunkSpec::Kind::kValues || kind == InitChunkSpec::Kind::kEventEdges)
            {
                initChunks.push_back(makeInitChunk(kind));
                return;
            }
            if (items.empty())
            {
                return;
            }
            InitChunkSpec chunk = makeInitChunk(kind);
            std::size_t chunkBytes = 0;
            for (ValueId item : items)
            {
                const std::size_t itemBytes = estimateBytes(item);
                if (!chunk.values.empty() && chunkBytes + itemBytes > kStateInitChunkTargetBytes)
                {
                    initChunks.push_back(std::move(chunk));
                    chunk = makeInitChunk(kind);
                    chunkBytes = 0;
                }
                chunk.values.push_back(item);
                chunkBytes += itemBytes;
            }
            initChunks.push_back(std::move(chunk));
        };

        auto addStateChunks = [&](const std::vector<std::string> &items)
        {
            if (items.empty())
            {
                return;
            }
            InitChunkSpec chunk = makeInitChunk(InitChunkSpec::Kind::kStates);
            std::size_t chunkBytes = 0;
            for (const std::string &item : items)
            {
                const std::size_t itemBytes = estimateStateInitBytes(item);
                if (!chunk.stateSymbols.empty() && chunkBytes + itemBytes > kStateInitChunkTargetBytes)
                {
                    initChunks.push_back(std::move(chunk));
                    chunk = makeInitChunk(InitChunkSpec::Kind::kStates);
                    chunkBytes = 0;
                }
                chunk.stateSymbols.push_back(item);
                chunkBytes += itemBytes;
            }
            initChunks.push_back(std::move(chunk));
        };

        auto addIndexChunks = [&](InitChunkSpec::Kind kind,
                                  const std::vector<std::size_t> &items,
                                  const auto &estimateBytes)
        {
            if (items.empty())
            {
                return;
            }
            InitChunkSpec chunk = makeInitChunk(kind);
            std::size_t chunkBytes = 0;
            for (std::size_t item : items)
            {
                const std::size_t itemBytes = estimateBytes(item);
                if (!chunk.indices.empty() && chunkBytes + itemBytes > kStateInitChunkTargetBytes)
                {
                    initChunks.push_back(std::move(chunk));
                    chunk = makeInitChunk(kind);
                    chunkBytes = 0;
                }
                chunk.indices.push_back(item);
                chunkBytes += itemBytes;
            }
            initChunks.push_back(std::move(chunk));
        };

        std::vector<std::size_t> memoryWriteIndices;
        memoryWriteIndices.reserve(model.writes.size());
        for (std::size_t writeIndex = 0; writeIndex < model.writes.size(); ++writeIndex)
        {
            if (model.writes[writeIndex].kind == StateDecl::Kind::Memory)
            {
                memoryWriteIndices.push_back(writeIndex);
            }
        }

        std::vector<ValueId> prevInputValues;
        prevInputValues.reserve(graph.inputPorts().size() + graph.inoutPorts().size());
        for (const auto &port : graph.inputPorts())
        {
            prevInputValues.push_back(port.value);
        }
        for (const auto &port : graph.inoutPorts())
        {
            prevInputValues.push_back(port.in);
        }

        std::vector<std::size_t> stateShadowIndices;
        stateShadowIndices.reserve(model.stateShadows.size());
        for (std::size_t shadowIndex = 0; shadowIndex < model.stateShadows.size(); ++shadowIndex)
        {
            stateShadowIndices.push_back(shadowIndex);
        }

        std::vector<ValueId> materializedConstantValues;
        materializedConstantValues.reserve(model.materializedValues.size());
        for (ValueId valueId : graph.values())
        {
            if (!model.materializedValues.contains(valueId))
            {
                continue;
            }
            const OperationId defOpId = graph.valueDef(valueId);
            if (!defOpId.valid() || graph.getOperation(defOpId).kind() != OperationKind::kConstant)
            {
                continue;
            }
            materializedConstantValues.push_back(valueId);
        }

        if (!graph.inputPorts().empty() || !graph.inoutPorts().empty())
        {
            addFixedInitChunk(InitChunkSpec::Kind::kPublicInputs);
        }
        if (!graph.outputPorts().empty())
        {
            addFixedInitChunk(InitChunkSpec::Kind::kPublicOutputs);
        }
        if (!model.valueFieldByValue.empty())
        {
            addValueChunks(InitChunkSpec::Kind::kValues, {}, estimateValueInitBytes);
        }
        addValueChunks(InitChunkSpec::Kind::kConstantValues,
                       materializedConstantValues,
                       estimateConstantValueInitBytes);
        addFixedInitChunk(InitChunkSpec::Kind::kRandomSeed);
        addFixedInitChunk(InitChunkSpec::Kind::kStateStorage);
        addStateChunks(model.stateOrder);
        addIndexChunks(InitChunkSpec::Kind::kStateShadows, stateShadowIndices, estimateStateShadowInitBytes);
        addIndexChunks(InitChunkSpec::Kind::kWrites, memoryWriteIndices, estimateWriteInitBytes);
        addValueChunks(InitChunkSpec::Kind::kPrevInputs, prevInputValues, estimatePrevInputBytes);
        if (!model.allEventValues.empty())
        {
            addValueChunks(InitChunkSpec::Kind::kEventEdges, {}, estimateEventEdgeBytes);
        }
        addFixedInitChunk(InitChunkSpec::Kind::kEvalState);
        if (model.needsSystemTaskRuntime)
        {
            addFixedInitChunk(InitChunkSpec::Kind::kRuntimeState);
        }

        std::vector<std::filesystem::path> stateInitPaths;
        stateInitPaths.reserve(initChunks.size());
        for (std::size_t chunkIndex = 0; chunkIndex < initChunks.size(); ++chunkIndex)
        {
            stateInitPaths.push_back(outDir / (prefix + "_state_init_" + std::to_string(chunkIndex) + ".cpp"));
        }

        struct CommitChunkSpec
        {
            std::string methodName;
            std::vector<std::size_t> indices;
            std::uint32_t maxDispatchIndex = 0;
        };

        // Commit shards tend to expand more than the byte estimator suggests, so use a tighter target.
        constexpr std::size_t kStateCommitChunkTargetBytes = 2u * 1024u * 1024u;

        auto estimateStateShadowCommitBytes = [&](std::size_t shadowIndex) -> std::size_t
        {
            const StateShadowDecl &shadow = model.stateShadows[shadowIndex];
            const StateDecl &state = model.stateBySymbol.at(shadow.symbol);
            std::size_t bytes = 224 + state.symbol.size();
            if (isWideLogicWidth(state.width))
            {
                bytes += 96;
            }
            else
            {
                bytes += state.cppType.size() + 96;
            }
            const auto headIt = model.stateHeadSupernodesBySymbol.find(shadow.symbol);
            if (headIt != model.stateHeadSupernodesBySymbol.end())
            {
                bytes += headIt->second.size() * 48;
            }
            return bytes;
        };

        auto estimateWriteCommitBytes = [&](std::size_t writeIndex) -> std::size_t
        {
            const WriteDecl &write = model.writes[writeIndex];
            const StateDecl &state = model.stateBySymbol.at(write.symbol);
            std::size_t bytes = 288 + state.symbol.size();
            if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kDynamic)
            {
                bytes += 64;
            }
            if (isWideLogicWidth(state.width))
            {
                bytes += 128;
            }
            else
            {
                bytes += state.cppType.size() + 128;
            }
            const auto headIt = model.stateHeadSupernodesBySymbol.find(write.symbol);
            if (headIt != model.stateHeadSupernodesBySymbol.end())
            {
                bytes += headIt->second.size() * 48;
            }
            return bytes;
        };

        std::vector<CommitChunkSpec> stateShadowCommitChunks;
        std::vector<CommitChunkSpec> writeCommitChunks;
        std::size_t nextStateShadowCommitChunkIndex = 0;
        std::size_t nextWriteCommitChunkIndex = 0;

        auto addCommitChunks = [&](const std::vector<std::size_t> &items,
                                   const auto &estimateBytes,
                                   const auto &dispatchIndexOf,
                                   const std::string &methodPrefix,
                                   std::vector<CommitChunkSpec> &outChunks,
                                   std::size_t &nextChunkIndex)
        {
            if (items.empty())
            {
                return;
            }
            CommitChunkSpec chunk;
            chunk.methodName = methodPrefix + std::to_string(nextChunkIndex++);
            std::size_t chunkBytes = 0;
            for (std::size_t item : items)
            {
                const std::size_t itemBytes = estimateBytes(item);
                if (!chunk.indices.empty() && chunkBytes + itemBytes > kStateCommitChunkTargetBytes)
                {
                    chunk.maxDispatchIndex = dispatchIndexOf(chunk.indices.back());
                    outChunks.push_back(std::move(chunk));
                    chunk = CommitChunkSpec{};
                    chunk.methodName = methodPrefix + std::to_string(nextChunkIndex++);
                    chunkBytes = 0;
                }
                chunk.indices.push_back(item);
                chunkBytes += itemBytes;
            }
            chunk.maxDispatchIndex = dispatchIndexOf(chunk.indices.back());
            outChunks.push_back(std::move(chunk));
        };

        addCommitChunks(stateShadowIndices,
                        estimateStateShadowCommitBytes,
                        [&](std::size_t shadowIndex) -> std::uint32_t
                        { return static_cast<std::uint32_t>(model.stateShadows[shadowIndex].emitIndex); },
                        "commit_state_shadow_chunk_",
                        stateShadowCommitChunks,
                        nextStateShadowCommitChunkIndex);
        addCommitChunks(memoryWriteIndices,
                        estimateWriteCommitBytes,
                        [&](std::size_t writeIndex) -> std::uint32_t
                        { return static_cast<std::uint32_t>(model.writes[writeIndex].shadowIndex); },
                        "commit_write_chunk_",
                        writeCommitChunks,
                        nextWriteCommitChunkIndex);

        std::vector<std::filesystem::path> stateCommitPaths;
        stateCommitPaths.reserve(stateShadowCommitChunks.size() + writeCommitChunks.size());
        for (std::size_t chunkIndex = 0; chunkIndex < stateShadowCommitChunks.size(); ++chunkIndex)
        {
            stateCommitPaths.push_back(outDir / (prefix + "_state_commit_shadow_" + std::to_string(chunkIndex) + ".cpp"));
        }
        for (std::size_t chunkIndex = 0; chunkIndex < writeCommitChunks.size(); ++chunkIndex)
        {
            stateCommitPaths.push_back(outDir / (prefix + "_state_commit_write_" + std::to_string(chunkIndex) + ".cpp"));
        }

        {
            if (auto error = ensureOutputDirectory(runtimePath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(runtimePath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "#pragma once\n\n";
            *stream << "#include <algorithm>\n";
            *stream << "#include <array>\n";
            *stream << "#include <cmath>\n";
            *stream << "#include <cstddef>\n";
            *stream << "#include <cstdlib>\n";
            *stream << "#include <cstdint>\n";
            *stream << "#include <limits>\n";
            *stream << "#include <string>\n";
            *stream << "#include <vector>\n\n";
            if (model.emitWaveform)
            {
                *stream << "#include <ctime>\n";
                *stream << "#include <string_view>\n";
                *stream << "#include \"fstapi.h\"\n\n";
                *stream << "class grhsim_fst_writer {\n";
                *stream << "public:\n";
                *stream << "    grhsim_fst_writer() = default;\n";
                *stream << "    ~grhsim_fst_writer() { close(); }\n";
                *stream << "    grhsim_fst_writer(const grhsim_fst_writer &) = delete;\n";
                *stream << "    grhsim_fst_writer &operator=(const grhsim_fst_writer &) = delete;\n";
                *stream << "    [[nodiscard]] bool open(const std::string &path, std::string_view topName)\n";
                *stream << "    {\n";
                *stream << "        close();\n";
                *stream << "        ctx_ = fstWriterCreate(path.c_str(), 1);\n";
                *stream << "        if (ctx_ == nullptr) {\n";
                *stream << "            return false;\n";
                *stream << "        }\n";
                *stream << "        fstWriterSetPackType(ctx_, FST_WR_PT_ZLIB);\n";
                *stream << "        fstWriterSetFileType(ctx_, FST_FT_VERILOG);\n";
                *stream << "        fstWriterSetTimescale(ctx_, -9);\n";
                *stream << "        fstWriterSetVersion(ctx_, \"wolvrix grhsim\");\n";
                *stream << "        std::time_t now = std::time(nullptr);\n";
                *stream << "        if (const char *date = std::ctime(&now)) {\n";
                *stream << "            fstWriterSetDate(ctx_, date);\n";
                *stream << "        }\n";
                *stream << "        const std::string scope(topName.empty() ? std::string(\"top\") : std::string(topName));\n";
                *stream << "        fstWriterSetScope(ctx_, FST_ST_VCD_MODULE, scope.c_str(), nullptr);\n";
                *stream << "        scope_open_ = true;\n";
                *stream << "        return true;\n";
                *stream << "    }\n";
                *stream << "    void close()\n";
                *stream << "    {\n";
                *stream << "        if (ctx_ == nullptr) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        if (scope_open_) {\n";
                *stream << "            fstWriterSetUpscope(ctx_);\n";
                *stream << "            scope_open_ = false;\n";
                *stream << "        }\n";
                *stream << "        fstWriterClose(ctx_);\n";
                *stream << "        ctx_ = nullptr;\n";
                *stream << "    }\n";
                *stream << "    [[nodiscard]] bool is_open() const noexcept { return ctx_ != nullptr; }\n";
                *stream << "    [[nodiscard]] fstHandle register_logic(std::string_view name, std::uint32_t width)\n";
                *stream << "    {\n";
                *stream << "        return fstWriterCreateVar(ctx_, FST_VT_SV_LOGIC, FST_VD_IMPLICIT, width == 0 ? 1u : width, std::string(name).c_str(), 0);\n";
                *stream << "    }\n";
                *stream << "    [[nodiscard]] fstHandle register_real(std::string_view name)\n";
                *stream << "    {\n";
                *stream << "        return fstWriterCreateVar(ctx_, FST_VT_VCD_REAL, FST_VD_IMPLICIT, 64u, std::string(name).c_str(), 0);\n";
                *stream << "    }\n";
                *stream << "    [[nodiscard]] fstHandle register_string(std::string_view name)\n";
                *stream << "    {\n";
                *stream << "        return fstWriterCreateVar(ctx_, FST_VT_GEN_STRING, FST_VD_IMPLICIT, 0u, std::string(name).c_str(), 0);\n";
                *stream << "    }\n";
                *stream << "    void emit_time(std::uint64_t time) { fstWriterEmitTimeChange(ctx_, time); }\n";
                *stream << "    void emit_logic_u64(fstHandle handle, std::uint32_t width, std::uint64_t value)\n";
                *stream << "    {\n";
                *stream << "        fstWriterEmitValueChange64(ctx_, handle, width == 0 ? 1u : width, value);\n";
                *stream << "    }\n";
                *stream << "    template <std::size_t N>\n";
                *stream << "    void emit_logic_words(fstHandle handle, std::uint32_t width, const std::array<std::uint64_t, N> &value)\n";
                *stream << "    {\n";
                *stream << "        fstWriterEmitValueChangeVec64(ctx_, handle, width == 0 ? 1u : width, value.data());\n";
                *stream << "    }\n";
                *stream << "    void emit_real(fstHandle handle, double value)\n";
                *stream << "    {\n";
                *stream << "        fstWriterEmitValueChange(ctx_, handle, &value);\n";
                *stream << "    }\n";
                *stream << "    void emit_string(fstHandle handle, const std::string &value)\n";
                *stream << "    {\n";
                *stream << "        fstWriterEmitVariableLengthValueChange(ctx_, handle, value.data(), static_cast<std::uint32_t>(value.size()));\n";
                *stream << "    }\n";
                *stream << "private:\n";
                *stream << "    fstWriterContext *ctx_ = nullptr;\n";
                *stream << "    bool scope_open_ = false;\n";
                *stream << "};\n\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "#include <fstream>\n";
                *stream << "#include <iomanip>\n";
                *stream << "#include <initializer_list>\n";
                *stream << "#include <iostream>\n";
                *stream << "#include <sstream>\n";
                *stream << "#include <string_view>\n";
                *stream << "#include <unordered_map>\n";
                *stream << "#include <utility>\n\n";
            }
            *stream << "inline std::uint64_t grhsim_mask(std::size_t width)\n{\n";
            *stream << "    if (width == 0) return UINT64_C(0);\n";
            *stream << "    if (width >= 64) return ~UINT64_C(0);\n";
            *stream << "    return (UINT64_C(1) << width) - UINT64_C(1);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_trunc_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return value & grhsim_mask(width);\n";
            *stream << "}\n\n";
            *stream << "inline std::int64_t grhsim_sign_extend_i64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    if (width == 0) return 0;\n";
            *stream << "    value = grhsim_trunc_u64(value, width);\n";
            *stream << "    if (width >= 64) return static_cast<std::int64_t>(value);\n";
            *stream << "    const std::uint64_t sign = UINT64_C(1) << (width - 1u);\n";
            *stream << "    if ((value & sign) == 0) return static_cast<std::int64_t>(value);\n";
            *stream << "    return static_cast<std::int64_t>(value | ~grhsim_mask(width));\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_cast_u64(std::uint64_t value,\n";
            *stream << "                                   std::size_t srcWidth,\n";
            *stream << "                                   std::size_t destWidth,\n";
            *stream << "                                   bool srcSigned)\n{\n";
            *stream << "    value = grhsim_trunc_u64(value, srcWidth);\n";
            *stream << "    if (srcSigned) {\n";
            *stream << "        value = static_cast<std::uint64_t>(grhsim_sign_extend_i64(value, srcWidth));\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(value, destWidth);\n";
            *stream << "}\n\n";
            *stream << "inline int grhsim_compare_unsigned_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    lhs = grhsim_trunc_u64(lhs, width);\n";
            *stream << "    rhs = grhsim_trunc_u64(rhs, width);\n";
            *stream << "    if (lhs < rhs) return -1;\n";
            *stream << "    if (lhs > rhs) return 1;\n";
            *stream << "    return 0;\n";
            *stream << "}\n\n";
            *stream << "inline int grhsim_compare_signed_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::int64_t lhsSigned = grhsim_sign_extend_i64(lhs, width);\n";
            *stream << "    const std::int64_t rhsSigned = grhsim_sign_extend_i64(rhs, width);\n";
            *stream << "    if (lhsSigned < rhsSigned) return -1;\n";
            *stream << "    if (lhsSigned > rhsSigned) return 1;\n";
            *stream << "    return 0;\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_concat_u64(std::uint64_t lhs, std::size_t lhsWidth, std::uint64_t rhs, std::size_t rhsWidth)\n{\n";
            *stream << "    const std::uint64_t lhsBits = grhsim_trunc_u64(lhs, lhsWidth);\n";
            *stream << "    const std::uint64_t rhsBits = grhsim_trunc_u64(rhs, rhsWidth);\n";
            *stream << "    if (rhsWidth >= 64) return rhsBits;\n";
            *stream << "    return grhsim_trunc_u64((lhsBits << rhsWidth) | rhsBits, lhsWidth + rhsWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_cat(std::uint64_t lhs,\n";
            *stream << "                               std::size_t lhsWidth,\n";
            *stream << "                               std::uint64_t rhs,\n";
            *stream << "                               std::size_t rhsWidth)\n{\n";
            *stream << "    return grhsim_concat_u64(lhs, lhsWidth, rhs, rhsWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_cat_prefix(std::uint64_t lhs,\n";
            *stream << "                                      std::size_t lhsWidth,\n";
            *stream << "                                      std::size_t rhsWidth)\n{\n";
            *stream << "    const std::uint64_t lhsBits = grhsim_trunc_u64(lhs, lhsWidth);\n";
            *stream << "    if (rhsWidth >= 64) {\n";
            *stream << "        return 0;\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(lhsBits << rhsWidth, lhsWidth + rhsWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_cat_rhs(std::uint64_t prefix,\n";
            *stream << "                                   std::uint64_t rhs,\n";
            *stream << "                                   std::size_t totalWidth,\n";
            *stream << "                                   std::size_t rhsWidth)\n{\n";
            *stream << "    return grhsim_trunc_u64(prefix | grhsim_trunc_u64(rhs, rhsWidth), totalWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_pack_bits_u64(const std::uint64_t *bits, std::size_t count)\n{\n";
            *stream << "    std::uint64_t out = 0;\n";
            *stream << "    for (std::size_t i = 0; i < count; ++i) {\n";
            *stream << "        out = (out << 1u) | (bits[i] & UINT64_C(1));\n";
            *stream << "    }\n";
            *stream << "    return out;\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_concat_scalars_u64(const std::uint64_t *values,\n";
            *stream << "                                               const std::size_t *widths,\n";
            *stream << "                                               std::size_t count,\n";
            *stream << "                                               std::size_t totalWidth)\n{\n";
            *stream << "    std::uint64_t out = 0;\n";
            *stream << "    std::size_t cursor = totalWidth;\n";
            *stream << "    for (std::size_t i = 0; i < count; ++i) {\n";
            *stream << "        const std::size_t width = widths[i];\n";
            *stream << "        if (width == 0) {\n";
            *stream << "            continue;\n";
            *stream << "        }\n";
            *stream << "        if (width > cursor) {\n";
            *stream << "            cursor = 0;\n";
            *stream << "            continue;\n";
            *stream << "        }\n";
            *stream << "        cursor -= width;\n";
            *stream << "        if (cursor >= 64u) {\n";
            *stream << "            continue;\n";
            *stream << "        }\n";
            *stream << "        const std::uint64_t bits = grhsim_trunc_u64(values[i], width);\n";
            *stream << "        out |= (cursor == 0 ? bits : (bits << cursor));\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(out, totalWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_concat_uniform_scalars_u64(const std::uint64_t *values,\n";
            *stream << "                                                       std::size_t count,\n";
            *stream << "                                                       std::size_t elemWidth,\n";
            *stream << "                                                       std::size_t totalWidth)\n{\n";
            *stream << "    std::uint64_t out = 0;\n";
            *stream << "    std::size_t cursor = totalWidth;\n";
            *stream << "    for (std::size_t i = 0; i < count; ++i) {\n";
            *stream << "        if (elemWidth == 0) {\n";
            *stream << "            continue;\n";
            *stream << "        }\n";
            *stream << "        if (elemWidth > cursor) {\n";
            *stream << "            cursor = 0;\n";
            *stream << "            continue;\n";
            *stream << "        }\n";
            *stream << "        cursor -= elemWidth;\n";
            *stream << "        if (cursor >= 64u) {\n";
            *stream << "            continue;\n";
            *stream << "        }\n";
            *stream << "        const std::uint64_t bits = grhsim_trunc_u64(values[i], elemWidth);\n";
            *stream << "        out |= (cursor == 0 ? bits : (bits << cursor));\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(out, totalWidth);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_replicate_u64(std::uint64_t value, std::size_t elemWidth, std::size_t rep)\n{\n";
            *stream << "    if (elemWidth == 0 || rep == 0) return 0;\n";
            *stream << "    const std::uint64_t elem = grhsim_trunc_u64(value, elemWidth);\n";
            *stream << "    std::uint64_t out = 0;\n";
            *stream << "    for (std::size_t i = 0; i < rep; ++i) {\n";
            *stream << "        const std::size_t shift = i * elemWidth;\n";
            *stream << "        if (shift >= 64) break;\n";
            *stream << "        out |= (elem << shift);\n";
            *stream << "    }\n";
            *stream << "    return grhsim_trunc_u64(out, elemWidth * rep);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_slice_dynamic_u64(std::uint64_t value, std::uint64_t start, std::size_t width)\n{\n";
            *stream << "    if (start >= 64) return 0;\n";
            *stream << "    return grhsim_trunc_u64(value >> start, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_clog2_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    value = grhsim_trunc_u64(value, width);\n";
            *stream << "    if (value <= 1) return 0;\n";
            *stream << "    std::uint64_t result = 0;\n";
            *stream << "    value -= 1;\n";
            *stream << "    while (value != 0) {\n";
            *stream << "        value >>= 1u;\n";
            *stream << "        ++result;\n";
            *stream << "    }\n";
            *stream << "    return result;\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_shl_u64(std::uint64_t value, std::uint64_t shift, std::size_t width)\n{\n";
            *stream << "    if (shift >= 64) return 0;\n";
            *stream << "    return grhsim_trunc_u64(value << shift, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_lshr_u64(std::uint64_t value, std::uint64_t shift, std::size_t width)\n{\n";
            *stream << "    if (shift >= 64) return 0;\n";
            *stream << "    return grhsim_trunc_u64(grhsim_trunc_u64(value, width) >> shift, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_ashr_u64(std::uint64_t value, std::uint64_t shift, std::size_t width)\n{\n";
            *stream << "    if (width == 0) return 0;\n";
            *stream << "    const std::uint64_t bounded = shift >= 64 ? 63 : shift;\n";
            *stream << "    const std::int64_t signedValue = grhsim_sign_extend_i64(value, width);\n";
            *stream << "    return grhsim_trunc_u64(static_cast<std::uint64_t>(signedValue >> bounded), width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_udiv_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::uint64_t divisor = grhsim_trunc_u64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    return grhsim_trunc_u64(grhsim_trunc_u64(lhs, width) / divisor, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_umod_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::uint64_t divisor = grhsim_trunc_u64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    return grhsim_trunc_u64(grhsim_trunc_u64(lhs, width) % divisor, width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_sdiv_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::int64_t divisor = grhsim_sign_extend_i64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    const std::int64_t dividend = grhsim_sign_extend_i64(lhs, width);\n";
            *stream << "    return grhsim_trunc_u64(static_cast<std::uint64_t>(dividend / divisor), width);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_smod_u64(std::uint64_t lhs, std::uint64_t rhs, std::size_t width)\n{\n";
            *stream << "    const std::int64_t divisor = grhsim_sign_extend_i64(rhs, width);\n";
            *stream << "    if (divisor == 0) return 0;\n";
            *stream << "    const std::int64_t dividend = grhsim_sign_extend_i64(lhs, width);\n";
            *stream << "    return grhsim_trunc_u64(static_cast<std::uint64_t>(dividend % divisor), width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_and_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    if (width == 0) return false;\n";
            *stream << "    return grhsim_trunc_u64(value, width) == grhsim_mask(width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_nand_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return !grhsim_reduce_and_u64(value, width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_or_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return grhsim_trunc_u64(value, width) != 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_nor_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return !grhsim_reduce_or_u64(value, width);\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_xor_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return (__builtin_popcountll(grhsim_trunc_u64(value, width)) & 1) != 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_reduce_xnor_u64(std::uint64_t value, std::size_t width)\n{\n";
            *stream << "    return !grhsim_reduce_xor_u64(value, width);\n";
            *stream << "}\n\n";
            *stream << "enum class grhsim_event_edge_kind : std::uint8_t {\n";
            *stream << "    none,\n";
            *stream << "    posedge,\n";
            *stream << "    negedge,\n";
            *stream << "};\n\n";
            *stream << "template <typename PrevT, typename CurrT>\n";
            *stream << "inline grhsim_event_edge_kind grhsim_classify_edge(PrevT prev, CurrT curr)\n{\n";
            *stream << "    const bool prevBool = prev != 0;\n";
            *stream << "    const bool currBool = curr != 0;\n";
            *stream << "    if (!prevBool && currBool) return grhsim_event_edge_kind::posedge;\n";
            *stream << "    if (prevBool && !currBool) return grhsim_event_edge_kind::negedge;\n";
            *stream << "    return grhsim_event_edge_kind::none;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_event_posedge(std::uint64_t curr, std::uint64_t prev)\n{\n";
            *stream << "    return curr != 0 && prev == 0;\n";
            *stream << "}\n\n";
            *stream << "inline bool grhsim_event_negedge(std::uint64_t curr, std::uint64_t prev)\n{\n";
            *stream << "    return curr == 0 && prev != 0;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_event_posedge_words(const std::array<std::uint64_t, N> &curr,\n";
            *stream << "                                     const std::array<std::uint64_t, N> &prev,\n";
            *stream << "                                     std::size_t width)\n{\n";
            *stream << "    const std::size_t live_words = (width + 63u) / 64u;\n";
            *stream << "    bool currAny = false;\n";
            *stream << "    bool prevAny = false;\n";
            *stream << "    for (std::size_t i = 0; i < live_words; ++i) {\n";
            *stream << "        const std::size_t bits = (i + 1u == live_words) ? (width - i * 64u) : 64u;\n";
            *stream << "        const std::uint64_t mask = bits < 64u ? ((UINT64_C(1) << bits) - 1u) : ~UINT64_C(0);\n";
            *stream << "        currAny = currAny || ((curr[i] & mask) != 0);\n";
            *stream << "        prevAny = prevAny || ((prev[i] & mask) != 0);\n";
            *stream << "    }\n";
            *stream << "    return currAny && !prevAny;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_event_negedge_words(const std::array<std::uint64_t, N> &curr,\n";
            *stream << "                                     const std::array<std::uint64_t, N> &prev,\n";
            *stream << "                                     std::size_t width)\n{\n";
            *stream << "    const std::size_t live_words = (width + 63u) / 64u;\n";
            *stream << "    bool currAny = false;\n";
            *stream << "    bool prevAny = false;\n";
            *stream << "    for (std::size_t i = 0; i < live_words; ++i) {\n";
            *stream << "        const std::size_t bits = (i + 1u == live_words) ? (width - i * 64u) : 64u;\n";
            *stream << "        const std::uint64_t mask = bits < 64u ? ((UINT64_C(1) << bits) - 1u) : ~UINT64_C(0);\n";
            *stream << "        currAny = currAny || ((curr[i] & mask) != 0);\n";
            *stream << "        prevAny = prevAny || ((prev[i] & mask) != 0);\n";
            *stream << "    }\n";
            *stream << "    return !currAny && prevAny;\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_splitmix64_next(std::uint64_t &state)\n{\n";
            *stream << "    std::uint64_t z = (state += UINT64_C(0x9E3779B97F4A7C15));\n";
            *stream << "    z = (z ^ (z >> 30u)) * UINT64_C(0xBF58476D1CE4E5B9);\n";
            *stream << "    z = (z ^ (z >> 27u)) * UINT64_C(0x94D049BB133111EB);\n";
            *stream << "    return z ^ (z >> 31u);\n";
            *stream << "}\n\n";
            *stream << "inline std::uint64_t grhsim_random_u64(std::uint64_t &state, std::size_t width)\n{\n";
            *stream << "    return grhsim_trunc_u64(grhsim_splitmix64_next(state), width);\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline void grhsim_trunc_words(std::array<std::uint64_t, N> &value, std::size_t width)\n{\n";
            *stream << "    const std::size_t liveWords = (width + 63u) / 64u;\n";
            *stream << "    for (std::size_t i = liveWords; i < N; ++i) {\n";
            *stream << "        value[i] = 0;\n";
            *stream << "    }\n";
            *stream << "    if constexpr (N > 0) {\n";
            *stream << "        if (liveWords != 0) {\n";
            *stream << "            const std::size_t tailWidth = width - ((liveWords - 1u) * 64u);\n";
            *stream << "            value[liveWords - 1u] = grhsim_trunc_u64(value[liveWords - 1u], tailWidth);\n";
            *stream << "        }\n";
            *stream << "    }\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_equal_words(const std::array<std::uint64_t, N> &lhs, const std::array<std::uint64_t, N> &rhs)\n{\n";
            *stream << "    return lhs == rhs;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_assign_words(std::array<std::uint64_t, N> &dst, std::array<std::uint64_t, N> src, std::size_t width)\n{\n";
            *stream << "    grhsim_trunc_words(src, width);\n";
            *stream << "    if (dst == src) {\n";
            *stream << "        return false;\n";
            *stream << "    }\n";
            *stream << "    dst = src;\n";
            *stream << "    return true;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline std::array<std::uint64_t, N> grhsim_merge_words_masked(const std::array<std::uint64_t, N> &base,\n";
            *stream << "                                                           const std::array<std::uint64_t, N> &data,\n";
            *stream << "                                                           const std::array<std::uint64_t, N> &mask,\n";
            *stream << "                                                           std::size_t width)\n{\n";
            *stream << "    std::array<std::uint64_t, N> out{};\n";
            *stream << "    for (std::size_t i = 0; i < N; ++i) {\n";
            *stream << "        out[i] = (base[i] & ~mask[i]) | (data[i] & mask[i]);\n";
            *stream << "    }\n";
            *stream << "    grhsim_trunc_words(out, width);\n";
            *stream << "    return out;\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline bool grhsim_apply_masked_words_inplace(std::array<std::uint64_t, N> &dst,\n";
            *stream << "                                             const std::array<std::uint64_t, N> &data,\n";
            *stream << "                                             const std::array<std::uint64_t, N> &mask,\n";
            *stream << "                                             std::size_t width)\n{\n";
            *stream << "    bool changed = false;\n";
            *stream << "    const std::size_t liveWords = (width + 63u) / 64u;\n";
            *stream << "    for (std::size_t i = 0; i < liveWords && i < N; ++i) {\n";
            *stream << "        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;\n";
            *stream << "        const std::uint64_t wordMask = grhsim_trunc_u64(mask[i], wordWidth);\n";
            *stream << "        const std::uint64_t next = (dst[i] & ~wordMask) | (grhsim_trunc_u64(data[i], wordWidth) & wordMask);\n";
            *stream << "        changed = changed || (dst[i] != next);\n";
            *stream << "        dst[i] = next;\n";
            *stream << "    }\n";
            *stream << "    for (std::size_t i = liveWords; i < N; ++i) {\n";
            *stream << "        changed = changed || (dst[i] != 0);\n";
            *stream << "        dst[i] = 0;\n";
            *stream << "    }\n";
            *stream << "    return changed;\n";
            *stream << "}\n\n";
            *stream << R"CPP(
inline void grhsim_trunc_words_buffer(std::uint64_t *value,
                                      std::size_t wordCount,
                                      std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    const std::size_t keptWords = liveWords < wordCount ? liveWords : wordCount;
    for (std::size_t i = keptWords; i < wordCount; ++i) {
        value[i] = 0;
    }
    if (keptWords == 0) {
        return;
    }
    const std::size_t lastBits = width & 63u;
    if (lastBits != 0) {
        value[keptWords - 1u] &= grhsim_mask(lastBits);
    }
}

inline void grhsim_clear_range_words_buffer(std::uint64_t *value,
                                            std::size_t wordCount,
                                            std::size_t start,
                                            std::size_t width)
{
    if (width == 0 || wordCount == 0) {
        return;
    }
    const std::size_t totalBits = wordCount * 64u;
    if (start >= totalBits) {
        return;
    }
    const std::size_t end = std::min(totalBits, start + width);
    if (end <= start) {
        return;
    }
    const std::size_t startWord = start / 64u;
    const std::size_t endWord = (end - 1u) / 64u;
    const std::size_t startBit = start & 63u;
    const std::size_t endBits = ((end - 1u) & 63u) + 1u;
    if (startWord == endWord) {
        const std::uint64_t lowMask = startBit == 0 ? UINT64_C(0) : grhsim_mask(startBit);
        const std::uint64_t highMask = endBits >= 64 ? UINT64_C(0) : ~grhsim_mask(endBits);
        value[startWord] &= (lowMask | highMask);
        return;
    }
    value[startWord] &= (startBit == 0 ? UINT64_C(0) : grhsim_mask(startBit));
    for (std::size_t i = startWord + 1u; i < endWord && i < wordCount; ++i) {
        value[i] = 0;
    }
    if (endWord < wordCount) {
        value[endWord] &= (endBits >= 64 ? UINT64_C(0) : ~grhsim_mask(endBits));
    }
}

inline void grhsim_insert_scalar_words_buffer(std::uint64_t *dest,
                                              std::size_t destWords,
                                              std::size_t destLsb,
                                              std::uint64_t src,
                                              std::size_t srcWidth)
{
    if (srcWidth == 0 || destWords == 0) {
        return;
    }
    grhsim_clear_range_words_buffer(dest, destWords, destLsb, srcWidth);
    const std::size_t destWord = destLsb / 64u;
    if (destWord >= destWords) {
        return;
    }
    const std::size_t bitShift = destLsb & 63u;
    const std::uint64_t bits = grhsim_trunc_u64(src, srcWidth);
    dest[destWord] |= (bitShift == 0 ? bits : (bits << bitShift));
    if (bitShift != 0 && destWord + 1u < destWords && srcWidth + bitShift > 64u) {
        dest[destWord + 1u] |= (bits >> (64u - bitShift));
    }
}

inline void grhsim_cast_scalar_words(std::uint64_t value,
                                     std::size_t srcWidth,
                                     std::size_t destWidth,
                                     bool srcSigned,
                                     std::uint64_t *out,
                                     std::size_t destWords)
{
    std::fill_n(out, destWords, UINT64_C(0));
    if (destWords > 0) {
        out[0] = grhsim_trunc_u64(value, srcWidth);
        if (srcSigned && srcWidth != 0 &&
            ((out[0] >> ((srcWidth >= 64u ? 63u : srcWidth - 1u))) & UINT64_C(1)) != 0) {
            if (srcWidth < 64u) {
                out[0] |= ~grhsim_mask(srcWidth);
            }
            for (std::size_t i = 1; i < destWords; ++i) {
                out[i] = ~UINT64_C(0);
            }
        }
    }
    grhsim_trunc_words_buffer(out, destWords, destWidth);
}

inline void grhsim_cast_words(const std::uint64_t *value,
                              std::size_t srcWords,
                              std::size_t srcWidth,
                              std::size_t destWidth,
                              bool srcSigned,
                              std::uint64_t *out,
                              std::size_t destWords)
{
    std::fill_n(out, destWords, UINT64_C(0));
    const std::size_t liveSrcWords = (srcWidth + 63u) / 64u;
    const std::size_t limit = std::min(srcWords, std::min(liveSrcWords, destWords));
    for (std::size_t i = 0; i < limit; ++i) {
        out[i] = value[i];
    }
    if (srcSigned && srcWidth != 0) {
        const std::size_t signWord = (srcWidth - 1u) / 64u;
        const std::size_t signBit = (srcWidth - 1u) & 63u;
        const bool neg = signWord < destWords && ((out[signWord] >> signBit) & UINT64_C(1)) != 0;
        if (neg) {
            if (signWord < destWords && signBit != 63u) {
                out[signWord] |= (~UINT64_C(0)) << (signBit + 1u);
            }
            for (std::size_t i = signWord + 1u; i < destWords; ++i) {
                out[i] = ~UINT64_C(0);
            }
        }
    }
    grhsim_trunc_words_buffer(out, destWords, destWidth);
}

inline void grhsim_concat_scalars_words(const std::uint64_t *values,
                                        const std::size_t *widths,
                                        std::size_t count,
                                        std::size_t totalWidth,
                                        std::uint64_t *out,
                                        std::size_t destWords)
{
    std::fill_n(out, destWords, UINT64_C(0));
    std::size_t cursor = totalWidth;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t width = widths[i];
        if (width == 0) {
            continue;
        }
        if (width > cursor) {
            cursor = 0;
            continue;
        }
        cursor -= width;
        grhsim_insert_scalar_words_buffer(out, destWords, cursor, values[i], width);
    }
    grhsim_trunc_words_buffer(out, destWords, totalWidth);
}

inline void grhsim_concat_uniform_scalars_words(const std::uint64_t *values,
                                                std::size_t count,
                                                std::size_t elemWidth,
                                                std::size_t totalWidth,
                                                std::uint64_t *out,
                                                std::size_t destWords)
{
    std::fill_n(out, destWords, UINT64_C(0));
    std::size_t cursor = totalWidth;
    for (std::size_t i = 0; i < count; ++i) {
        if (elemWidth == 0) {
            continue;
        }
        if (elemWidth > cursor) {
            cursor = 0;
            continue;
        }
        cursor -= elemWidth;
        grhsim_insert_scalar_words_buffer(out, destWords, cursor, values[i], elemWidth);
    }
    grhsim_trunc_words_buffer(out, destWords, totalWidth);
}

inline void grhsim_slice_words(const std::uint64_t *src,
                               std::size_t srcWords,
                               std::size_t start,
                               std::size_t width,
                               std::uint64_t *out,
                               std::size_t destWords)
{
    std::fill_n(out, destWords, UINT64_C(0));
    if (width == 0 || destWords == 0 || srcWords == 0) {
        return;
    }
    const std::size_t srcWord = start / 64u;
    const std::size_t bitShift = start & 63u;
    const std::size_t outWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < outWords && i < destWords; ++i) {
        const std::uint64_t low = (srcWord + i < srcWords) ? src[srcWord + i] : UINT64_C(0);
        if (bitShift == 0) {
            out[i] = low;
        }
        else {
            const std::uint64_t high = (srcWord + i + 1u < srcWords) ? src[srcWord + i + 1u] : UINT64_C(0);
            out[i] = (low >> bitShift) | (high << (64u - bitShift));
        }
    }
    grhsim_trunc_words_buffer(out, destWords, width);
}

inline bool grhsim_any_bits_words(const std::uint64_t *value,
                                  std::size_t wordCount,
                                  std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < wordCount; ++i) {
        const std::uint64_t word = (i + 1u == liveWords) ? grhsim_trunc_u64(value[i], width - i * 64u) : value[i];
        if (word != 0) {
            return true;
        }
    }
    return false;
}

inline bool grhsim_sign_bit_words(const std::uint64_t *value,
                                  std::size_t wordCount,
                                  std::size_t width)
{
    if (width == 0) {
        return false;
    }
    const std::size_t index = width - 1u;
    const std::size_t wordIndex = index / 64u;
    if (wordIndex >= wordCount) {
        return false;
    }
    return ((value[wordIndex] >> (index & 63u)) & UINT64_C(1)) != 0;
}

inline int grhsim_compare_unsigned_words(const std::uint64_t *lhs,
                                         std::size_t lhsWords,
                                         const std::uint64_t *rhs,
                                         std::size_t rhsWords)
{
    const std::size_t words = std::max(lhsWords, rhsWords);
    for (std::size_t i = words; i-- > 0;) {
        const std::uint64_t lhsWord = i < lhsWords ? lhs[i] : UINT64_C(0);
        const std::uint64_t rhsWord = i < rhsWords ? rhs[i] : UINT64_C(0);
        if (lhsWord < rhsWord) {
            return -1;
        }
        if (lhsWord > rhsWord) {
            return 1;
        }
    }
    return 0;
}

inline int grhsim_compare_signed_words(const std::uint64_t *lhs,
                                       std::size_t lhsWords,
                                       const std::uint64_t *rhs,
                                       std::size_t rhsWords,
                                       std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, lhsWords, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, rhsWords, width);
    if (lhsNeg != rhsNeg) {
        return lhsNeg ? -1 : 1;
    }
    return grhsim_compare_unsigned_words(lhs, lhsWords, rhs, rhsWords);
}

inline bool grhsim_reduce_and_words(const std::uint64_t *value,
                                    std::size_t wordCount,
                                    std::size_t width)
{
    if (width == 0) {
        return false;
    }
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < wordCount; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        if (grhsim_trunc_u64(value[i], wordWidth) != grhsim_mask(wordWidth)) {
            return false;
        }
    }
    return true;
}

inline bool grhsim_reduce_nand_words(const std::uint64_t *value,
                                     std::size_t wordCount,
                                     std::size_t width)
{
    return !grhsim_reduce_and_words(value, wordCount, width);
}

inline bool grhsim_reduce_or_words(const std::uint64_t *value,
                                   std::size_t wordCount,
                                   std::size_t width)
{
    return grhsim_any_bits_words(value, wordCount, width);
}

inline bool grhsim_reduce_nor_words(const std::uint64_t *value,
                                    std::size_t wordCount,
                                    std::size_t width)
{
    return !grhsim_reduce_or_words(value, wordCount, width);
}

inline bool grhsim_reduce_xor_words(const std::uint64_t *value,
                                    std::size_t wordCount,
                                    std::size_t width)
{
    unsigned parity = 0;
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < wordCount; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        parity ^= static_cast<unsigned>(__builtin_popcountll(grhsim_trunc_u64(value[i], wordWidth)) & 1u);
    }
    return (parity & 1u) != 0;
}

inline bool grhsim_reduce_xnor_words(const std::uint64_t *value,
                                     std::size_t wordCount,
                                     std::size_t width)
{
    return !grhsim_reduce_xor_words(value, wordCount, width);
}

inline void grhsim_not_words(const std::uint64_t *value,
                             std::size_t valueWords,
                             std::size_t width,
                             std::uint64_t *out,
                             std::size_t outWords)
{
    for (std::size_t i = 0; i < outWords; ++i) {
        out[i] = ~(i < valueWords ? value[i] : UINT64_C(0));
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_and_words(const std::uint64_t *lhs,
                             std::size_t lhsWords,
                             const std::uint64_t *rhs,
                             std::size_t rhsWords,
                             std::size_t width,
                             std::uint64_t *out,
                             std::size_t outWords)
{
    for (std::size_t i = 0; i < outWords; ++i) {
        const std::uint64_t lhsWord = i < lhsWords ? lhs[i] : UINT64_C(0);
        const std::uint64_t rhsWord = i < rhsWords ? rhs[i] : UINT64_C(0);
        out[i] = lhsWord & rhsWord;
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_or_words(const std::uint64_t *lhs,
                            std::size_t lhsWords,
                            const std::uint64_t *rhs,
                            std::size_t rhsWords,
                            std::size_t width,
                            std::uint64_t *out,
                            std::size_t outWords)
{
    for (std::size_t i = 0; i < outWords; ++i) {
        const std::uint64_t lhsWord = i < lhsWords ? lhs[i] : UINT64_C(0);
        const std::uint64_t rhsWord = i < rhsWords ? rhs[i] : UINT64_C(0);
        out[i] = lhsWord | rhsWord;
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_xor_words(const std::uint64_t *lhs,
                             std::size_t lhsWords,
                             const std::uint64_t *rhs,
                             std::size_t rhsWords,
                             std::size_t width,
                             std::uint64_t *out,
                             std::size_t outWords)
{
    for (std::size_t i = 0; i < outWords; ++i) {
        const std::uint64_t lhsWord = i < lhsWords ? lhs[i] : UINT64_C(0);
        const std::uint64_t rhsWord = i < rhsWords ? rhs[i] : UINT64_C(0);
        out[i] = lhsWord ^ rhsWord;
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_xnor_words(const std::uint64_t *lhs,
                              std::size_t lhsWords,
                              const std::uint64_t *rhs,
                              std::size_t rhsWords,
                              std::size_t width,
                              std::uint64_t *out,
                              std::size_t outWords)
{
    grhsim_xor_words(lhs, lhsWords, rhs, rhsWords, width, out, outWords);
    for (std::size_t i = 0; i < outWords; ++i) {
        out[i] = ~out[i];
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_add_words(const std::uint64_t *lhs,
                             std::size_t lhsWords,
                             const std::uint64_t *rhs,
                             std::size_t rhsWords,
                             std::size_t width,
                             std::uint64_t *out,
                             std::size_t outWords)
{
    unsigned __int128 carry = 0;
    for (std::size_t i = 0; i < outWords; ++i) {
        const unsigned __int128 lhsWord = i < lhsWords ? lhs[i] : UINT64_C(0);
        const unsigned __int128 rhsWord = i < rhsWords ? rhs[i] : UINT64_C(0);
        const unsigned __int128 sum = lhsWord + rhsWord + carry;
        out[i] = static_cast<std::uint64_t>(sum);
        carry = sum >> 64u;
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_sub_words(const std::uint64_t *lhs,
                             std::size_t lhsWords,
                             const std::uint64_t *rhs,
                             std::size_t rhsWords,
                             std::size_t width,
                             std::uint64_t *out,
                             std::size_t outWords)
{
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < outWords; ++i) {
        const std::uint64_t lhsWord = i < lhsWords ? lhs[i] : UINT64_C(0);
        const std::uint64_t rhsBase = i < rhsWords ? rhs[i] : UINT64_C(0);
        const std::uint64_t rhsWord = rhsBase + borrow;
        borrow = (rhsWord < rhsBase || lhsWord < rhsWord) ? 1 : 0;
        out[i] = lhsWord - rhsWord;
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_fill_range_words_buffer(std::uint64_t *value,
                                           std::size_t wordCount,
                                           std::size_t start,
                                           std::size_t width)
{
    if (width == 0 || wordCount == 0) {
        return;
    }
    const std::size_t totalBits = wordCount * 64u;
    if (start >= totalBits) {
        return;
    }
    const std::size_t end = std::min(totalBits, start + width);
    if (end <= start) {
        return;
    }
    const std::size_t startWord = start / 64u;
    const std::size_t endWord = (end - 1u) / 64u;
    const std::size_t startBit = start & 63u;
    const std::size_t endBits = ((end - 1u) & 63u) + 1u;
    if (startWord == endWord) {
        const std::uint64_t lowMask = startBit == 0 ? ~UINT64_C(0) : ~grhsim_mask(startBit);
        const std::uint64_t highMask = endBits >= 64 ? ~UINT64_C(0) : grhsim_mask(endBits);
        value[startWord] |= (lowMask & highMask);
        return;
    }
    value[startWord] |= (startBit == 0 ? ~UINT64_C(0) : ~grhsim_mask(startBit));
    for (std::size_t i = startWord + 1u; i < endWord && i < wordCount; ++i) {
        value[i] = ~UINT64_C(0);
    }
    if (endWord < wordCount) {
        value[endWord] |= (endBits >= 64 ? ~UINT64_C(0) : grhsim_mask(endBits));
    }
}

inline void grhsim_shl_words(const std::uint64_t *value,
                             std::size_t valueWords,
                             std::size_t amount,
                             std::size_t width,
                             std::uint64_t *out,
                             std::size_t outWords)
{
    std::fill_n(out, outWords, UINT64_C(0));
    if (amount >= width) {
        return;
    }
    const std::size_t wordShift = amount / 64u;
    const std::size_t bitShift = amount & 63u;
    for (std::size_t i = outWords; i-- > 0;) {
        if (i < wordShift) {
            continue;
        }
        const std::size_t srcIndex = i - wordShift;
        const std::uint64_t low = srcIndex < valueWords ? value[srcIndex] : UINT64_C(0);
        out[i] = (bitShift == 0 ? low : (low << bitShift));
        if (bitShift != 0 && srcIndex > 0 && srcIndex - 1u < valueWords) {
            out[i] |= (value[srcIndex - 1u] >> (64u - bitShift));
        }
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_lshr_words(const std::uint64_t *value,
                              std::size_t valueWords,
                              std::size_t amount,
                              std::size_t width,
                              std::uint64_t *out,
                              std::size_t outWords)
{
    std::fill_n(out, outWords, UINT64_C(0));
    if (amount >= width) {
        return;
    }
    const std::size_t wordShift = amount / 64u;
    const std::size_t bitShift = amount & 63u;
    for (std::size_t i = 0; i < outWords; ++i) {
        if (i + wordShift >= valueWords) {
            break;
        }
        const std::uint64_t high = value[i + wordShift];
        out[i] = (bitShift == 0 ? high : (high >> bitShift));
        if (bitShift != 0 && i + wordShift + 1u < valueWords) {
            out[i] |= (value[i + wordShift + 1u] << (64u - bitShift));
        }
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

inline void grhsim_ashr_words(const std::uint64_t *value,
                              std::size_t valueWords,
                              std::size_t amount,
                              std::size_t width,
                              std::uint64_t *out,
                              std::size_t outWords)
{
    const bool sign = grhsim_sign_bit_words(value, valueWords, width);
    if (amount >= width) {
        std::fill_n(out, outWords, UINT64_C(0));
        if (sign) {
            grhsim_fill_range_words_buffer(out, outWords, 0, width);
        }
        grhsim_trunc_words_buffer(out, outWords, width);
        return;
    }
    grhsim_lshr_words(value, valueWords, amount, width, out, outWords);
    if (sign) {
        grhsim_fill_range_words_buffer(out, outWords, width - amount, amount);
    }
    grhsim_trunc_words_buffer(out, outWords, width);
}

template <std::size_t DestN, typename T>
inline std::array<std::uint64_t, DestN> grhsim_cast_words(T value,
                                                          std::size_t srcWidth,
                                                          std::size_t destWidth,
                                                          bool srcSigned)
{
    std::array<std::uint64_t, DestN> out{};
    if constexpr (DestN > 0) {
        out[0] = grhsim_trunc_u64(static_cast<std::uint64_t>(value), srcWidth);
        if (srcSigned && srcWidth != 0 &&
            ((out[0] >> ((srcWidth >= 64u ? 63u : srcWidth - 1u))) & UINT64_C(1)) != 0) {
            if (srcWidth < 64u) {
                out[0] |= ~grhsim_mask(srcWidth);
            }
            for (std::size_t i = 1; i < DestN; ++i) {
                out[i] = ~UINT64_C(0);
            }
        }
    }
    grhsim_trunc_words(out, destWidth);
    return out;
}

template <std::size_t DestN, std::size_t SrcN>
inline std::array<std::uint64_t, DestN> grhsim_cast_words(const std::array<std::uint64_t, SrcN> &value,
                                                          std::size_t srcWidth,
                                                          std::size_t destWidth,
                                                          bool srcSigned)
{
    std::array<std::uint64_t, DestN> out{};
    const std::size_t srcWords = (srcWidth + 63u) / 64u;
    const std::size_t limit = srcWords < SrcN ? srcWords : SrcN;
    for (std::size_t i = 0; i < limit && i < DestN; ++i) {
        out[i] = value[i];
    }
    if (srcSigned && srcWidth != 0) {
        const std::size_t signWord = (srcWidth - 1u) / 64u;
        const std::size_t signBit = (srcWidth - 1u) & 63u;
        const bool neg = signWord < DestN && ((out[signWord] >> signBit) & UINT64_C(1)) != 0;
        if (neg) {
            if (signWord < DestN && signBit != 63u) {
                out[signWord] |= (~UINT64_C(0)) << (signBit + 1u);
            }
            for (std::size_t i = signWord + 1u; i < DestN; ++i) {
                out[i] = ~UINT64_C(0);
            }
        }
    }
    grhsim_trunc_words(out, destWidth);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_random_words(std::uint64_t &state, std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = grhsim_splitmix64_next(state);
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_shl_words(const std::array<std::uint64_t, N> &value,
                                                     const ShiftT &shift,
                                                     std::size_t width);

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_lshr_words(const std::array<std::uint64_t, N> &value,
                                                      const ShiftT &shift,
                                                      std::size_t width);

template <std::size_t N>
inline bool grhsim_try_u128_words(const std::array<std::uint64_t, N> &value,
                                  std::size_t width,
                                  unsigned __int128 &out);

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_from_u128_words(unsigned __int128 value, std::size_t width);

template <std::size_t N>
inline bool grhsim_any_bits_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::uint64_t word = (i + 1u == liveWords) ? grhsim_trunc_u64(value[i], width - i * 64u) : value[i];
        if (word != 0) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
inline bool grhsim_get_bit_words(const std::array<std::uint64_t, N> &value, std::size_t index)
{
    if (index / 64u >= N) {
        return false;
    }
    return (value[index / 64u] & (UINT64_C(1) << (index & 63u))) != 0;
}

template <std::size_t N>
inline void grhsim_put_bit_words(std::array<std::uint64_t, N> &value, std::size_t index, bool bit)
{
    if (index / 64u >= N) {
        return;
    }
    const std::uint64_t mask = UINT64_C(1) << (index & 63u);
    if (bit) {
        value[index / 64u] |= mask;
    }
    else {
        value[index / 64u] &= ~mask;
    }
}

template <std::size_t N>
inline void grhsim_clear_range_words(std::array<std::uint64_t, N> &value, std::size_t start, std::size_t width)
{
    if (width == 0 || N == 0) {
        return;
    }
    const std::size_t totalBits = N * 64u;
    if (start >= totalBits) {
        return;
    }
    const std::size_t end = std::min(totalBits, start + width);
    if (end <= start) {
        return;
    }
    const std::size_t startWord = start / 64u;
    const std::size_t endWord = (end - 1u) / 64u;
    const std::size_t startBit = start & 63u;
    const std::size_t endBits = ((end - 1u) & 63u) + 1u;
    if (startWord == endWord) {
        const std::uint64_t lowMask = startBit == 0 ? UINT64_C(0) : grhsim_mask(startBit);
        const std::uint64_t highMask = endBits >= 64 ? UINT64_C(0) : ~grhsim_mask(endBits);
        value[startWord] &= (lowMask | highMask);
        return;
    }
    value[startWord] &= (startBit == 0 ? UINT64_C(0) : grhsim_mask(startBit));
    for (std::size_t i = startWord + 1u; i < endWord && i < N; ++i) {
        value[i] = 0;
    }
    if (endWord < N) {
        value[endWord] &= (endBits >= 64 ? UINT64_C(0) : ~grhsim_mask(endBits));
    }
}

template <std::size_t N>
inline void grhsim_fill_range_words(std::array<std::uint64_t, N> &value, std::size_t start, std::size_t width)
{
    if (width == 0 || N == 0) {
        return;
    }
    const std::size_t totalBits = N * 64u;
    if (start >= totalBits) {
        return;
    }
    const std::size_t end = std::min(totalBits, start + width);
    if (end <= start) {
        return;
    }
    const std::size_t startWord = start / 64u;
    const std::size_t endWord = (end - 1u) / 64u;
    const std::size_t startBit = start & 63u;
    const std::size_t endBits = ((end - 1u) & 63u) + 1u;
    if (startWord == endWord) {
        const std::uint64_t lowMask = startBit == 0 ? ~UINT64_C(0) : ~grhsim_mask(startBit);
        const std::uint64_t highMask = endBits >= 64 ? ~UINT64_C(0) : grhsim_mask(endBits);
        value[startWord] |= (lowMask & highMask);
        return;
    }
    value[startWord] |= (startBit == 0 ? ~UINT64_C(0) : ~grhsim_mask(startBit));
    for (std::size_t i = startWord + 1u; i < endWord && i < N; ++i) {
        value[i] = ~UINT64_C(0);
    }
    if (endWord < N) {
        value[endWord] |= (endBits >= 64 ? ~UINT64_C(0) : grhsim_mask(endBits));
    }
}

template <std::size_t DestN, std::size_t SrcN>
inline void grhsim_insert_words(std::array<std::uint64_t, DestN> &dest,
                                std::size_t destLsb,
                                const std::array<std::uint64_t, SrcN> &src,
                                std::size_t srcWidth)
{
    if (srcWidth == 0 || DestN == 0 || SrcN == 0) {
        return;
    }
    grhsim_clear_range_words(dest, destLsb, srcWidth);
    const std::size_t srcWords = (srcWidth + 63u) / 64u;
    const std::size_t destWord = destLsb / 64u;
    const std::size_t bitShift = destLsb & 63u;
    for (std::size_t i = 0; i < srcWords && i < SrcN; ++i) {
        std::uint64_t word = src[i];
        const std::size_t wordWidth = (i + 1u == srcWords) ? (srcWidth - i * 64u) : 64u;
        word = grhsim_trunc_u64(word, wordWidth);
        if (destWord + i < DestN) {
            dest[destWord + i] |= (bitShift == 0 ? word : (word << bitShift));
        }
        if (bitShift != 0 && destWord + i + 1u < DestN) {
            dest[destWord + i + 1u] |= (word >> (64u - bitShift));
        }
    }
}

template <std::size_t DestN>
inline void grhsim_insert_scalar_words(std::array<std::uint64_t, DestN> &dest,
                                       std::size_t destLsb,
                                       std::uint64_t src,
                                       std::size_t srcWidth)
{
    if (srcWidth == 0 || DestN == 0) {
        return;
    }
    grhsim_clear_range_words(dest, destLsb, srcWidth);
    const std::size_t destWord = destLsb / 64u;
    if (destWord >= DestN) {
        return;
    }
    const std::size_t bitShift = destLsb & 63u;
    const std::uint64_t bits = grhsim_trunc_u64(src, srcWidth);
    dest[destWord] |= (bitShift == 0 ? bits : (bits << bitShift));
    if (bitShift != 0 && destWord + 1u < DestN && srcWidth + bitShift > 64u) {
        dest[destWord + 1u] |= (bits >> (64u - bitShift));
    }
}

template <std::size_t DestN, std::size_t Count>
inline std::array<std::uint64_t, DestN> grhsim_concat_scalars_words(const std::array<std::uint64_t, Count> &values,
                                                                    const std::array<std::size_t, Count> &widths,
                                                                    std::size_t totalWidth)
{
    std::array<std::uint64_t, DestN> out{};
    std::size_t cursor = totalWidth;
    for (std::size_t i = 0; i < Count; ++i) {
        const std::size_t width = widths[i];
        if (width == 0) {
            continue;
        }
        if (width > cursor) {
            cursor = 0;
            continue;
        }
        cursor -= width;
        grhsim_insert_scalar_words(out, cursor, values[i], width);
    }
    grhsim_trunc_words(out, totalWidth);
    return out;
}

template <std::size_t DestN, std::size_t Count>
inline std::array<std::uint64_t, DestN> grhsim_concat_uniform_scalars_words(const std::array<std::uint64_t, Count> &values,
                                                                            std::size_t elemWidth,
                                                                            std::size_t totalWidth)
{
    std::array<std::uint64_t, DestN> out{};
    std::size_t cursor = totalWidth;
    for (std::size_t i = 0; i < Count; ++i) {
        if (elemWidth == 0) {
            continue;
        }
        if (elemWidth > cursor) {
            cursor = 0;
            continue;
        }
        cursor -= elemWidth;
        grhsim_insert_scalar_words(out, cursor, values[i], elemWidth);
    }
    grhsim_trunc_words(out, totalWidth);
    return out;
}

template <std::size_t DestN, std::size_t SrcN>
inline std::array<std::uint64_t, DestN> grhsim_slice_words(const std::array<std::uint64_t, SrcN> &src,
                                                           std::size_t start,
                                                           std::size_t width)
{
    std::array<std::uint64_t, DestN> out{};
    if (width == 0 || DestN == 0 || SrcN == 0) {
        return out;
    }
    const std::size_t srcWord = start / 64u;
    const std::size_t bitShift = start & 63u;
    const std::size_t outWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < outWords && i < DestN; ++i) {
        const std::uint64_t low = (srcWord + i < SrcN) ? src[srcWord + i] : UINT64_C(0);
        if (bitShift == 0) {
            out[i] = low;
        }
        else {
            const std::uint64_t high = (srcWord + i + 1u < SrcN) ? src[srcWord + i + 1u] : UINT64_C(0);
            out[i] = (low >> bitShift) | (high << (64u - bitShift));
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <typename T>
inline std::size_t grhsim_index_words(T value, std::size_t cap)
{
    const std::uint64_t raw = static_cast<std::uint64_t>(value);
    if (raw >= cap) {
        return cap;
    }
    return static_cast<std::size_t>(raw);
}

template <std::size_t N>
inline std::size_t grhsim_index_words(const std::array<std::uint64_t, N> &value, std::size_t cap)
{
    for (std::size_t i = 1; i < N; ++i) {
        if (value[i] != 0) {
            return cap;
        }
    }
    if (value[0] >= cap) {
        return cap;
    }
    return static_cast<std::size_t>(value[0]);
}

template <typename T>
inline std::size_t grhsim_index_in_range_words(T value)
{
    return static_cast<std::size_t>(static_cast<std::uint64_t>(value));
}

template <std::size_t N>
inline std::size_t grhsim_index_in_range_words(const std::array<std::uint64_t, N> &value)
{
    return N == 0 ? 0 : static_cast<std::size_t>(value[0]);
}

template <typename T>
inline std::size_t grhsim_index_pow2_words(T value, std::size_t mask)
{
    return static_cast<std::size_t>(static_cast<std::uint64_t>(value)) & mask;
}

template <std::size_t N>
inline std::size_t grhsim_index_pow2_words(const std::array<std::uint64_t, N> &value, std::size_t mask)
{
    return (N == 0 ? 0 : static_cast<std::size_t>(value[0])) & mask;
}

template <std::size_t DestN, std::size_t SrcN, typename ShiftT>
inline std::array<std::uint64_t, DestN> grhsim_slice_words(const std::array<std::uint64_t, SrcN> &src,
                                                           const ShiftT &start,
                                                           std::size_t srcWidth,
                                                           std::size_t width)
{
    return grhsim_slice_words<DestN>(src, grhsim_index_words(start, srcWidth), width);
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_not_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = ~value[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_and_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lhs[i] & rhs[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_or_words(const std::array<std::uint64_t, N> &lhs,
                                                    const std::array<std::uint64_t, N> &rhs,
                                                    std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lhs[i] | rhs[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_xor_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lhs[i] ^ rhs[i];
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_xnor_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    return grhsim_not_words(grhsim_xor_words(lhs, rhs, width), width);
}

template <std::size_t N>
inline bool grhsim_sign_bit_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    if (width == 0) {
        return false;
    }
    return grhsim_get_bit_words(value, width - 1u);
}

template <std::size_t N>
inline int grhsim_compare_unsigned_words(const std::array<std::uint64_t, N> &lhs,
                                         const std::array<std::uint64_t, N> &rhs)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, N * 64u, lhs128) && grhsim_try_u128_words(rhs, N * 64u, rhs128)) {
        if (lhs128 < rhs128) {
            return -1;
        }
        if (lhs128 > rhs128) {
            return 1;
        }
        return 0;
    }
    for (std::size_t i = N; i-- > 0;) {
        if (lhs[i] < rhs[i]) {
            return -1;
        }
        if (lhs[i] > rhs[i]) {
            return 1;
        }
    }
    return 0;
}

template <std::size_t N>
inline bool grhsim_try_u64_words(const std::array<std::uint64_t, N> &value,
                                 std::size_t width,
                                 std::uint64_t &out)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    if (liveWords == 0 || N == 0) {
        out = 0;
        return true;
    }
    const std::size_t limit = liveWords < N ? liveWords : N;
    for (std::size_t i = 1; i < limit; ++i) {
        if (value[i] != 0) {
            return false;
        }
    }
    out = grhsim_trunc_u64(value[0], width >= 64 ? 64u : width);
    return true;
}

template <std::size_t N>
inline bool grhsim_try_u128_words(const std::array<std::uint64_t, N> &value,
                                  std::size_t width,
                                  unsigned __int128 &out)
{
    if (width > 128u) {
        return false;
    }
    const std::size_t liveWords = (width + 63u) / 64u;
    if (liveWords == 0 || N == 0) {
        out = 0;
        return true;
    }
    if (liveWords > 2u || liveWords > N) {
        return false;
    }
    const std::uint64_t lo = value[0];
    const std::uint64_t hi = liveWords >= 2u ? value[1] : UINT64_C(0);
    out = static_cast<unsigned __int128>(lo) | (static_cast<unsigned __int128>(hi) << 64u);
    return true;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_from_u128_words(unsigned __int128 value, std::size_t width)
{
    std::array<std::uint64_t, N> out{};
    if constexpr (N > 0) {
        out[0] = static_cast<std::uint64_t>(value);
    }
    if constexpr (N > 1) {
        out[1] = static_cast<std::uint64_t>(value >> 64u);
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline bool grhsim_try_single_bit_words(const std::array<std::uint64_t, N> &value,
                                        std::size_t width,
                                        std::size_t &bitIndex)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    bool found = false;
    bitIndex = 0;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        const std::uint64_t word = grhsim_trunc_u64(value[i], wordWidth);
        if (word == 0) {
            continue;
        }
        if ((word & (word - 1u)) != 0) {
            return false;
        }
        if (found) {
            return false;
        }
        found = true;
        bitIndex = i * 64u + static_cast<std::size_t>(__builtin_ctzll(word));
    }
    return found;
}

template <std::size_t N>
inline int grhsim_compare_signed_words(const std::array<std::uint64_t, N> &lhs,
                                       const std::array<std::uint64_t, N> &rhs,
                                       std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, width);
    if (lhsNeg != rhsNeg) {
        return lhsNeg ? -1 : 1;
    }
    return grhsim_compare_unsigned_words(lhs, rhs);
}

template <std::size_t N>
inline bool grhsim_reduce_and_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    if (width == 0) {
        return false;
    }
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        if (grhsim_trunc_u64(value[i], wordWidth) != grhsim_mask(wordWidth)) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
inline bool grhsim_reduce_nand_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return !grhsim_reduce_and_words(value, width);
}

template <std::size_t N>
inline bool grhsim_reduce_or_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return grhsim_any_bits_words(value, width);
}

template <std::size_t N>
inline bool grhsim_reduce_nor_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return !grhsim_reduce_or_words(value, width);
}

template <std::size_t N>
inline bool grhsim_reduce_xor_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    unsigned parity = 0;
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = 0; i < liveWords && i < N; ++i) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        parity ^= static_cast<unsigned>(__builtin_popcountll(grhsim_trunc_u64(value[i], wordWidth)) & 1u);
    }
    return (parity & 1u) != 0;
}

template <std::size_t N>
inline bool grhsim_reduce_xnor_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    return !grhsim_reduce_xor_words(value, width);
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_add_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        return grhsim_from_u128_words<N>(lhs128 + rhs128, width);
    }
    std::array<std::uint64_t, N> out{};
    unsigned __int128 carry = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const unsigned __int128 sum =
            static_cast<unsigned __int128>(lhs[i]) + static_cast<unsigned __int128>(rhs[i]) + carry;
        out[i] = static_cast<std::uint64_t>(sum);
        carry = sum >> 64u;
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_sub_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        return grhsim_from_u128_words<N>(lhs128 - rhs128, width);
    }
    std::array<std::uint64_t, N> out{};
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t rhsWord = rhs[i] + borrow;
        borrow = (rhsWord < rhs[i] || lhs[i] < rhsWord) ? 1 : 0;
        out[i] = lhs[i] - rhsWord;
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_mul_words(const std::array<std::uint64_t, N> &lhs,
                                                     const std::array<std::uint64_t, N> &rhs,
                                                     std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        return grhsim_from_u128_words<N>(lhs128 * rhs128, width);
    }

    auto mulByWord = [&](const std::array<std::uint64_t, N> &value,
                         std::uint64_t rhsWord) -> std::array<std::uint64_t, N>
    {
        std::array<std::uint64_t, N> out{};
        unsigned __int128 carry = 0;
        for (std::size_t i = 0; i < N; ++i) {
            const unsigned __int128 accum =
                static_cast<unsigned __int128>(value[i]) * static_cast<unsigned __int128>(rhsWord) + carry;
            out[i] = static_cast<std::uint64_t>(accum);
            carry = accum >> 64u;
        }
        grhsim_trunc_words(out, width);
        return out;
    };

    std::uint64_t rhsWord = 0;
    if (grhsim_try_u64_words(rhs, width, rhsWord)) {
        return mulByWord(lhs, rhsWord);
    }
    std::uint64_t lhsWord = 0;
    if (grhsim_try_u64_words(lhs, width, lhsWord)) {
        return mulByWord(rhs, lhsWord);
    }
    std::size_t rhsBitIndex = 0;
    if (grhsim_try_single_bit_words(rhs, width, rhsBitIndex)) {
        return grhsim_shl_words(lhs, rhsBitIndex, width);
    }
    std::size_t lhsBitIndex = 0;
    if (grhsim_try_single_bit_words(lhs, width, lhsBitIndex)) {
        return grhsim_shl_words(rhs, lhsBitIndex, width);
    }

    std::array<std::uint64_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        unsigned __int128 carry = 0;
        for (std::size_t j = 0; j + i < N; ++j) {
            const unsigned __int128 accum =
                static_cast<unsigned __int128>(out[i + j]) +
                static_cast<unsigned __int128>(lhs[i]) * static_cast<unsigned __int128>(rhs[j]) +
                carry;
            out[i + j] = static_cast<std::uint64_t>(accum);
            carry = accum >> 64u;
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_negate_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    std::array<std::uint64_t, N> out = grhsim_not_words(value, width);
    std::array<std::uint64_t, N> one{};
    if constexpr (N > 0) {
        one[0] = 1;
    }
    return grhsim_add_words(out, one, width);
}

template <std::size_t N>
inline void grhsim_shl1_words_inplace(std::array<std::uint64_t, N> &value, std::size_t width)
{
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t nextCarry = value[i] >> 63u;
        value[i] = (value[i] << 1u) | carry;
        carry = nextCarry;
    }
    grhsim_trunc_words(value, width);
}

template <std::size_t N>
inline std::size_t grhsim_highest_bit_words(const std::array<std::uint64_t, N> &value, std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    for (std::size_t i = liveWords; i-- > 0;) {
        const std::size_t wordWidth = (i + 1u == liveWords) ? (width - i * 64u) : 64u;
        const std::uint64_t word = grhsim_trunc_u64(value[i], wordWidth);
        if (word != 0) {
            return i * 64u + (63u - static_cast<std::size_t>(__builtin_clzll(word)));
        }
    }
    return 0;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_udiv_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        if (rhs128 == 0) {
            return {};
        }
        return grhsim_from_u128_words<N>(lhs128 / rhs128, width);
    }

    std::uint64_t rhsWord = 0;
    if (grhsim_try_u64_words(rhs, width, rhsWord)) {
        if (rhsWord == 0) {
            return {};
        }
        std::array<std::uint64_t, N> quotient{};
        unsigned __int128 remainder = 0;
        const std::size_t liveWords = (width + 63u) / 64u;
        for (std::size_t i = liveWords; i-- > 0;) {
            const unsigned __int128 dividend =
                (remainder << 64u) | static_cast<unsigned __int128>(lhs[i]);
            quotient[i] = static_cast<std::uint64_t>(dividend / rhsWord);
            remainder = dividend % rhsWord;
        }
        grhsim_trunc_words(quotient, width);
        return quotient;
    }
    std::size_t rhsBitIndex = 0;
    if (grhsim_try_single_bit_words(rhs, width, rhsBitIndex)) {
        return grhsim_lshr_words(lhs, rhsBitIndex, width);
    }
    if (!grhsim_any_bits_words(rhs, width)) {
        return {};
    }
    std::array<std::uint64_t, N> quotient{};
    std::array<std::uint64_t, N> remainder = lhs;
    const std::size_t rhsHighestBit = grhsim_highest_bit_words(rhs, width);
    while (grhsim_compare_unsigned_words(remainder, rhs) >= 0) {
        const std::size_t remainderHighestBit = grhsim_highest_bit_words(remainder, width);
        std::size_t shift = remainderHighestBit - rhsHighestBit;
        auto shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        if (grhsim_compare_unsigned_words(remainder, shiftedDivisor) < 0) {
            if (shift == 0) {
                break;
            }
            --shift;
            shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        }
        remainder = grhsim_sub_words(remainder, shiftedDivisor, width);
        grhsim_put_bit_words(quotient, shift, true);
    }
    grhsim_trunc_words(quotient, width);
    return quotient;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_umod_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    unsigned __int128 lhs128 = 0;
    unsigned __int128 rhs128 = 0;
    if (grhsim_try_u128_words(lhs, width, lhs128) && grhsim_try_u128_words(rhs, width, rhs128)) {
        if (rhs128 == 0) {
            return {};
        }
        return grhsim_from_u128_words<N>(lhs128 % rhs128, width);
    }

    std::uint64_t rhsWord = 0;
    if (grhsim_try_u64_words(rhs, width, rhsWord)) {
        if (rhsWord == 0) {
            return {};
        }
        unsigned __int128 remainder = 0;
        const std::size_t liveWords = (width + 63u) / 64u;
        for (std::size_t i = liveWords; i-- > 0;) {
            const unsigned __int128 dividend =
                (remainder << 64u) | static_cast<unsigned __int128>(lhs[i]);
            remainder = dividend % rhsWord;
        }
        std::array<std::uint64_t, N> out{};
        if constexpr (N > 0) {
            out[0] = static_cast<std::uint64_t>(remainder);
        }
        grhsim_trunc_words(out, width);
        return out;
    }
    std::size_t rhsBitIndex = 0;
    if (grhsim_try_single_bit_words(rhs, width, rhsBitIndex)) {
        std::array<std::uint64_t, N> out{};
        if (rhsBitIndex != 0) {
            out = grhsim_slice_words<N>(lhs, 0, rhsBitIndex);
        }
        grhsim_trunc_words(out, width);
        return out;
    }
    if (!grhsim_any_bits_words(rhs, width)) {
        return {};
    }
    std::array<std::uint64_t, N> remainder = lhs;
    const std::size_t rhsHighestBit = grhsim_highest_bit_words(rhs, width);
    while (grhsim_compare_unsigned_words(remainder, rhs) >= 0) {
        const std::size_t remainderHighestBit = grhsim_highest_bit_words(remainder, width);
        std::size_t shift = remainderHighestBit - rhsHighestBit;
        auto shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        if (grhsim_compare_unsigned_words(remainder, shiftedDivisor) < 0) {
            if (shift == 0) {
                break;
            }
            --shift;
            shiftedDivisor = grhsim_shl_words(rhs, shift, width);
        }
        remainder = grhsim_sub_words(remainder, shiftedDivisor, width);
    }
    grhsim_trunc_words(remainder, width);
    return remainder;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_sdiv_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, width);
    const auto lhsAbs = lhsNeg ? grhsim_negate_words(lhs, width) : lhs;
    const auto rhsAbs = rhsNeg ? grhsim_negate_words(rhs, width) : rhs;
    auto quotient = grhsim_udiv_words(lhsAbs, rhsAbs, width);
    if (lhsNeg != rhsNeg) {
        quotient = grhsim_negate_words(quotient, width);
    }
    grhsim_trunc_words(quotient, width);
    return quotient;
}

template <std::size_t N>
inline std::array<std::uint64_t, N> grhsim_smod_words(const std::array<std::uint64_t, N> &lhs,
                                                      const std::array<std::uint64_t, N> &rhs,
                                                      std::size_t width)
{
    const bool lhsNeg = grhsim_sign_bit_words(lhs, width);
    const bool rhsNeg = grhsim_sign_bit_words(rhs, width);
    const auto lhsAbs = lhsNeg ? grhsim_negate_words(lhs, width) : lhs;
    const auto rhsAbs = rhsNeg ? grhsim_negate_words(rhs, width) : rhs;
    auto remainder = grhsim_umod_words(lhsAbs, rhsAbs, width);
    if (lhsNeg) {
        remainder = grhsim_negate_words(remainder, width);
    }
    grhsim_trunc_words(remainder, width);
    return remainder;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_shl_words(const std::array<std::uint64_t, N> &value,
                                                     const ShiftT &shift,
                                                     std::size_t width)
{
    const std::size_t amount = grhsim_index_words(shift, width);
    if (amount >= width) {
        return {};
    }
    std::array<std::uint64_t, N> out{};
    const std::size_t wordShift = amount / 64u;
    const std::size_t bitShift = amount & 63u;
    for (std::size_t i = N; i-- > 0;) {
        if (i < wordShift) {
            continue;
        }
        const std::uint64_t low = value[i - wordShift];
        out[i] = (bitShift == 0 ? low : (low << bitShift));
        if (bitShift != 0 && i > wordShift) {
            out[i] |= (value[i - wordShift - 1u] >> (64u - bitShift));
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_lshr_words(const std::array<std::uint64_t, N> &value,
                                                      const ShiftT &shift,
                                                      std::size_t width)
{
    const std::size_t amount = grhsim_index_words(shift, width);
    if (amount >= width) {
        return {};
    }
    std::array<std::uint64_t, N> out{};
    const std::size_t wordShift = amount / 64u;
    const std::size_t bitShift = amount & 63u;
    for (std::size_t i = 0; i < N; ++i) {
        if (i + wordShift >= N) {
            break;
        }
        const std::uint64_t high = value[i + wordShift];
        out[i] = (bitShift == 0 ? high : (high >> bitShift));
        if (bitShift != 0 && i + wordShift + 1u < N) {
            out[i] |= (value[i + wordShift + 1u] << (64u - bitShift));
        }
    }
    grhsim_trunc_words(out, width);
    return out;
}

template <std::size_t N, typename ShiftT>
inline std::array<std::uint64_t, N> grhsim_ashr_words(const std::array<std::uint64_t, N> &value,
                                                      const ShiftT &shift,
                                                      std::size_t width)
{
    const std::size_t amount = grhsim_index_words(shift, width);
    const bool sign = grhsim_sign_bit_words(value, width);
    if (amount >= width) {
        std::array<std::uint64_t, N> fill{};
        if (sign) {
            grhsim_fill_range_words(fill, 0, width);
        }
        grhsim_trunc_words(fill, width);
        return fill;
    }
    std::array<std::uint64_t, N> out = grhsim_lshr_words(value, shift, width);
    if (sign) {
        grhsim_fill_range_words(out, width - amount, amount);
    }
    grhsim_trunc_words(out, width);
    return out;
}

)CPP";
            *stream << "struct grhsim_active_mask_entry\n{\n";
            *stream << "    std::uint32_t word_index;\n";
            *stream << "    std::uint8_t mask;\n";
            *stream << "};\n\n";
            *stream << "inline std::uint8_t grhsim_popcount_u8(std::uint8_t value)\n{\n";
            *stream << "    return static_cast<std::uint8_t>(__builtin_popcount(static_cast<unsigned>(value)));\n";
            *stream << "}\n\n";
            *stream << "template <typename ActiveFlags>\n";
            *stream << "inline std::size_t grhsim_count_active_supernodes(const ActiveFlags &activeFlags)\n{\n";
            *stream << "    std::size_t total = 0;\n";
            *stream << "    for (const auto word : activeFlags) {\n";
            *stream << "        total += static_cast<std::size_t>(grhsim_popcount_u8(word));\n";
            *stream << "    }\n";
            *stream << "    return total;\n";
            *stream << "}\n\n";
            *stream << "template <typename ActiveFlags>\n";
            *stream << "inline std::size_t grhsim_count_nonzero_active_words(const ActiveFlags &activeFlags)\n{\n";
            *stream << "    std::size_t total = 0;\n";
            *stream << "    for (const auto word : activeFlags) {\n";
            *stream << "        total += word == UINT8_C(0) ? 0u : 1u;\n";
            *stream << "    }\n";
            *stream << "    return total;\n";
            *stream << "}\n\n";
            *stream << "template <typename Sample>\n";
            *stream << "inline Sample grhsim_percentile_sample(std::vector<Sample> samples, std::size_t num, std::size_t den)\n{\n";
            *stream << "    if (samples.empty() || den == 0) {\n";
            *stream << "        return Sample{};\n";
            *stream << "    }\n";
            *stream << "    std::sort(samples.begin(), samples.end());\n";
            *stream << "    const std::size_t idx = ((samples.size() - 1u) * num) / den;\n";
            *stream << "    return samples[idx];\n";
            *stream << "}\n\n";
            *stream << "template <typename Sample>\n";
            *stream << "inline void grhsim_print_sample_summary(std::FILE *fp,\n";
            *stream << "                                       const char *label,\n";
            *stream << "                                       const std::vector<Sample> &samples)\n{\n";
            *stream << "    if (samples.empty()) {\n";
            *stream << "        std::fprintf(fp, \"[grhsim-activity] %s samples=0\\n\", label);\n";
            *stream << "        return;\n";
            *stream << "    }\n";
            *stream << "    Sample minValue = samples.front();\n";
            *stream << "    Sample maxValue = samples.front();\n";
            *stream << "    long double total = 0.0;\n";
            *stream << "    for (const Sample sample : samples) {\n";
            *stream << "        if (sample < minValue) {\n";
            *stream << "            minValue = sample;\n";
            *stream << "        }\n";
            *stream << "        if (sample > maxValue) {\n";
            *stream << "            maxValue = sample;\n";
            *stream << "        }\n";
            *stream << "        total += static_cast<long double>(sample);\n";
            *stream << "    }\n";
            *stream << "    std::fprintf(fp,\n";
            *stream << "                 \"[grhsim-activity] %s samples=%zu avg=%.2Lf min=%llu p50=%llu p90=%llu p99=%llu max=%llu\\n\",\n";
            *stream << "                 label,\n";
            *stream << "                 samples.size(),\n";
            *stream << "                 total / static_cast<long double>(samples.size()),\n";
            *stream << "                 static_cast<unsigned long long>(minValue),\n";
            *stream << "                 static_cast<unsigned long long>(grhsim_percentile_sample(samples, 50u, 100u)),\n";
            *stream << "                 static_cast<unsigned long long>(grhsim_percentile_sample(samples, 90u, 100u)),\n";
            *stream << "                 static_cast<unsigned long long>(grhsim_percentile_sample(samples, 99u, 100u)),\n";
            *stream << "                 static_cast<unsigned long long>(maxValue));\n";
            *stream << "}\n\n";
            *stream << "template <std::size_t N>\n";
            *stream << "inline void grhsim_mark_pending_write(std::array<std::uint32_t, N> &touchedIndices,\n";
            *stream << "                                      std::array<std::uint8_t, N> &touchedFlags,\n";
            *stream << "                                      std::size_t &touchedCount,\n";
            *stream << "                                      std::uint32_t writeIndex)\n{\n";
            *stream << "    if constexpr (N > 0) {\n";
            *stream << "        if (touchedFlags[writeIndex] == 0) {\n";
            *stream << "            touchedFlags[writeIndex] = 1;\n";
            *stream << "            touchedIndices[touchedCount++] = writeIndex;\n";
            *stream << "        }\n";
            *stream << "    } else {\n";
            *stream << "        (void)touchedIndices;\n";
            *stream << "        (void)touchedFlags;\n";
            *stream << "        (void)touchedCount;\n";
            *stream << "        (void)writeIndex;\n";
            *stream << "    }\n";
            *stream << "}\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << R"CPP(

enum class grhsim_task_arg_kind {
    Logic,
    Real,
    String,
};

struct grhsim_task_arg {
    grhsim_task_arg_kind kind = grhsim_task_arg_kind::Logic;
    std::size_t width = 0;
    bool isSigned = false;
    bool isWide = false;
    std::uint64_t scalarValue = 0;
    std::vector<std::uint64_t> words;
    double realValue = 0.0;
    std::string stringValue;
};

inline grhsim_task_arg grhsim_make_task_arg(std::uint64_t value, std::size_t width, bool isSigned)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::Logic;
    arg.width = width;
    arg.isSigned = isSigned;
    arg.isWide = false;
    arg.scalarValue = grhsim_trunc_u64(value, width == 0 ? 64u : width);
    return arg;
}

template <std::size_t N>
inline grhsim_task_arg grhsim_make_task_arg(const std::array<std::uint64_t, N> &value,
                                            std::size_t width,
                                            bool isSigned)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::Logic;
    arg.width = width;
    arg.isSigned = isSigned;
    arg.isWide = true;
    arg.words.assign(value.begin(), value.end());
    const std::size_t liveWords = (width + 63u) / 64u;
    if (arg.words.size() < liveWords) {
        arg.words.resize(liveWords, 0);
    }
    if (width != 0 && !arg.words.empty()) {
        const std::size_t tailWidth = width - ((liveWords - 1u) * 64u);
        arg.words[liveWords - 1u] = grhsim_trunc_u64(arg.words[liveWords - 1u], tailWidth);
    }
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(double value)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::Real;
    arg.realValue = value;
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(const std::string &value)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::String;
    arg.stringValue = value;
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(std::string &&value)
{
    grhsim_task_arg arg;
    arg.kind = grhsim_task_arg_kind::String;
    arg.stringValue = std::move(value);
    return arg;
}

inline grhsim_task_arg grhsim_make_task_arg(const char *value)
{
    return grhsim_make_task_arg(std::string(value == nullptr ? "" : value));
}

inline void grhsim_task_trunc_words(std::vector<std::uint64_t> &words, std::size_t width)
{
    const std::size_t liveWords = (width + 63u) / 64u;
    if (words.size() < liveWords) {
        words.resize(liveWords, 0);
    }
    if (liveWords == 0) {
        words.clear();
        return;
    }
    words.resize(liveWords);
    const std::size_t tailWidth = width - ((liveWords - 1u) * 64u);
    words[liveWords - 1u] = grhsim_trunc_u64(words[liveWords - 1u], tailWidth);
}

inline bool grhsim_task_words_is_zero(const std::vector<std::uint64_t> &words)
{
    for (std::uint64_t word : words) {
        if (word != 0) {
            return false;
        }
    }
    return true;
}

inline bool grhsim_task_sign_bit(const std::vector<std::uint64_t> &words, std::size_t width)
{
    if (width == 0 || words.empty()) {
        return false;
    }
    const std::size_t wordIndex = (width - 1u) / 64u;
    const std::size_t bitIndex = (width - 1u) & 63u;
    if (wordIndex >= words.size()) {
        return false;
    }
    return ((words[wordIndex] >> bitIndex) & UINT64_C(1)) != 0;
}

inline void grhsim_task_negate_words(std::vector<std::uint64_t> &words, std::size_t width)
{
    for (std::uint64_t &word : words) {
        word = ~word;
    }
    std::uint64_t carry = 1;
    for (std::uint64_t &word : words) {
        const std::uint64_t next = word + carry;
        carry = (next < word) ? 1 : 0;
        word = next;
        if (carry == 0) {
            break;
        }
    }
    grhsim_task_trunc_words(words, width);
}

inline std::uint32_t grhsim_task_divmod_words(std::vector<std::uint64_t> &words, std::uint32_t base)
{
    unsigned __int128 rem = 0;
    for (std::size_t i = words.size(); i-- > 0;) {
        const unsigned __int128 cur = (rem << 64u) | words[i];
        words[i] = static_cast<std::uint64_t>(cur / base);
        rem = cur % base;
    }
    return static_cast<std::uint32_t>(rem);
}

inline std::string grhsim_task_unsigned_words_to_base(std::vector<std::uint64_t> words,
                                                      std::size_t width,
                                                      std::uint32_t base,
                                                      bool uppercase)
{
    grhsim_task_trunc_words(words, width);
    if (words.empty() || grhsim_task_words_is_zero(words)) {
        return "0";
    }
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string out;
    while (!grhsim_task_words_is_zero(words)) {
        const std::uint32_t rem = grhsim_task_divmod_words(words, base);
        out.push_back(digits[rem]);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

inline std::string grhsim_task_logic_to_base(const grhsim_task_arg &arg,
                                             std::uint32_t base,
                                             bool uppercase)
{
    if (!arg.isWide) {
        if (base == 10u) {
            return std::to_string(grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width));
        }
        if (base == 16u) {
            std::ostringstream out;
            out << std::hex << (uppercase ? std::uppercase : std::nouppercase)
                << grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width);
            return out.str();
        }
        if (base == 8u) {
            std::ostringstream out;
            out << std::oct << grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width);
            return out.str();
        }
        if (base == 2u) {
            const std::size_t width = arg.width == 0 ? 1u : arg.width;
            std::string out;
            out.reserve(width);
            const std::uint64_t value = grhsim_trunc_u64(arg.scalarValue, width >= 64u ? 64u : width);
            for (std::size_t i = 0; i < width; ++i) {
                out.push_back(((value >> (width - i - 1u)) & UINT64_C(1)) != 0 ? '1' : '0');
            }
            const std::size_t pos = out.find_first_not_of('0');
            return pos == std::string::npos ? "0" : out.substr(pos);
        }
        return std::to_string(grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width));
    }
    return grhsim_task_unsigned_words_to_base(arg.words, arg.width, base, uppercase);
}

inline std::string grhsim_task_logic_to_decimal(const grhsim_task_arg &arg, bool signedMode)
{
    if (!signedMode) {
        return grhsim_task_logic_to_base(arg, 10u, false);
    }
    if (!arg.isWide) {
        return std::to_string(grhsim_sign_extend_i64(arg.scalarValue, arg.width == 0 ? 64u : arg.width));
    }
    std::vector<std::uint64_t> words = arg.words;
    grhsim_task_trunc_words(words, arg.width);
    const bool negative = grhsim_task_sign_bit(words, arg.width);
    if (negative) {
        grhsim_task_negate_words(words, arg.width);
    }
    std::string out = grhsim_task_unsigned_words_to_base(words, arg.width, 10u, false);
    if (negative && out != "0") {
        out.insert(out.begin(), '-');
    }
    return out;
}

inline std::string grhsim_task_default_arg_text(const grhsim_task_arg &arg)
{
    switch (arg.kind) {
    case grhsim_task_arg_kind::String:
        return arg.stringValue;
    case grhsim_task_arg_kind::Real: {
        std::ostringstream out;
        out << std::defaultfloat << arg.realValue;
        return out.str();
    }
    case grhsim_task_arg_kind::Logic:
    default:
        return grhsim_task_logic_to_decimal(arg, arg.isSigned);
    }
}

inline std::uint64_t grhsim_task_arg_u64(const grhsim_task_arg &arg)
{
    switch (arg.kind) {
    case grhsim_task_arg_kind::Real:
        return static_cast<std::uint64_t>(arg.realValue);
    case grhsim_task_arg_kind::String:
        return arg.stringValue.empty() ? 0 : static_cast<std::uint64_t>(static_cast<unsigned char>(arg.stringValue.front()));
    case grhsim_task_arg_kind::Logic:
    default:
        return arg.isWide ? (arg.words.empty() ? 0 : arg.words.front())
                          : grhsim_trunc_u64(arg.scalarValue, arg.width == 0 ? 64u : arg.width);
    }
}

inline std::string grhsim_task_apply_width(std::string text,
                                           int width,
                                           bool leftJustify,
                                           bool zeroPad)
{
    if (width <= 0 || static_cast<int>(text.size()) >= width) {
        return text;
    }
    const std::size_t padCount = static_cast<std::size_t>(width - static_cast<int>(text.size()));
    const char pad = zeroPad && !leftJustify ? '0' : ' ';
    if (leftJustify) {
        text.append(padCount, pad);
        return text;
    }
    if (pad == '0' && !text.empty() && text.front() == '-') {
        return std::string("-") + std::string(padCount, '0') + text.substr(1);
    }
    return std::string(padCount, pad) + text;
}

inline std::string grhsim_task_format_one(const grhsim_task_arg &arg,
                                          char spec,
                                          int width,
                                          int precision,
                                          bool leftJustify,
                                          bool zeroPad)
{
    std::string text;
    switch (spec) {
    case 'd':
    case 'i':
        if (arg.kind == grhsim_task_arg_kind::Logic) {
            text = grhsim_task_logic_to_decimal(arg, arg.isSigned);
        }
        else if (arg.kind == grhsim_task_arg_kind::Real) {
            text = std::to_string(static_cast<long long>(arg.realValue));
        }
        else {
            text = grhsim_task_default_arg_text(arg);
        }
        break;
    case 'u':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_decimal(arg, false)
                   : std::to_string(grhsim_task_arg_u64(arg));
        break;
    case 'h':
    case 'x':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 16u, false)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'H':
    case 'X':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 16u, true)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'b':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 2u, false)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'o':
        text = (arg.kind == grhsim_task_arg_kind::Logic)
                   ? grhsim_task_logic_to_base(arg, 8u, false)
                   : grhsim_task_default_arg_text(arg);
        break;
    case 'c':
        text.assign(1, static_cast<char>(grhsim_task_arg_u64(arg) & UINT64_C(0xff)));
        break;
    case 's':
        text = (arg.kind == grhsim_task_arg_kind::String) ? arg.stringValue : grhsim_task_default_arg_text(arg);
        break;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G': {
        const double value = (arg.kind == grhsim_task_arg_kind::Real)
                                 ? arg.realValue
                                 : static_cast<double>(grhsim_task_arg_u64(arg));
        std::ostringstream out;
        if (precision >= 0) {
            out << std::setprecision(precision);
        }
        switch (spec) {
        case 'e':
            out << std::scientific << std::nouppercase;
            break;
        case 'E':
            out << std::scientific << std::uppercase;
            break;
        case 'f':
            out << std::fixed << std::nouppercase;
            break;
        case 'F':
            out << std::fixed << std::uppercase;
            break;
        case 'G':
            out << std::uppercase;
            [[fallthrough]];
        case 'g':
        default:
            break;
        }
        out << value;
        text = out.str();
        break;
    }
    case 't':
        text = std::to_string(grhsim_task_arg_u64(arg));
        break;
    case 'v':
        text = grhsim_task_default_arg_text(arg);
        break;
    default:
        text = grhsim_task_default_arg_text(arg);
        break;
    }
    return grhsim_task_apply_width(std::move(text), width, leftJustify, zeroPad);
}

inline std::string grhsim_format_task_message(const std::vector<grhsim_task_arg> &items)
{
    if (items.empty()) {
        return {};
    }
    if (items.front().kind != grhsim_task_arg_kind::String) {
        std::string out;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0) {
                out.push_back(' ');
            }
            out += grhsim_task_default_arg_text(items[i]);
        }
        return out;
    }

    std::string out;
    const std::string &fmt = items.front().stringValue;
    std::size_t argIndex = 1;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        if (i + 1u >= fmt.size()) {
            out.push_back('%');
            break;
        }
        if (fmt[i + 1u] == '%') {
            out.push_back('%');
            ++i;
            continue;
        }
        ++i;
        bool leftJustify = false;
        bool zeroPad = false;
        while (i < fmt.size()) {
            if (fmt[i] == '-') {
                leftJustify = true;
                ++i;
                continue;
            }
            if (fmt[i] == '0') {
                zeroPad = true;
                ++i;
                continue;
            }
            break;
        }
        int fieldWidth = 0;
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
            fieldWidth = fieldWidth * 10 + static_cast<int>(fmt[i] - '0');
            ++i;
        }
        int precision = -1;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i;
            precision = 0;
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
                precision = precision * 10 + static_cast<int>(fmt[i] - '0');
                ++i;
            }
        }
        while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'L' || fmt[i] == 'z')) {
            ++i;
        }
        if (i >= fmt.size()) {
            break;
        }
        const char spec = fmt[i];
        if (spec == 'm') {
            out += "top";
            continue;
        }
        if (argIndex >= items.size()) {
            out.push_back('%');
            out.push_back(spec);
            continue;
        }
        out += grhsim_task_format_one(items[argIndex++],
                                      spec,
                                      fieldWidth,
                                      precision,
                                      leftJustify,
                                      zeroPad);
    }
    return out;
}

inline std::string grhsim_format_task_message(std::initializer_list<grhsim_task_arg> args)
{
    return grhsim_format_task_message(std::vector<grhsim_task_arg>(args.begin(), args.end()));
}
)CPP";
            }
            if (auto error = finalizeOutputFile(*stream, runtimePath))
            {
                reportError(*error, runtimePath.string());
                result.success = false;
                return result;
            }
        }

        {
            if (auto error = ensureOutputDirectory(headerPath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(headerPath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "#pragma once\n\n";
            *stream << "#include <array>\n";
            *stream << "#include <cstddef>\n";
            *stream << "#include <cstdint>\n";
            if (model.emitWaveform)
            *stream << "#include <algorithm>\n";
            *stream << "#include <memory>\n";
            *stream << "#include <utility>\n";
            *stream << "#include <string>\n";
            *stream << "#include <vector>\n\n";
            *stream << "#include \"" << runtimePath.filename().string() << "\"\n\n";
            *stream << "class " << className << " {\n";
            *stream << "public:\n";
            *stream << "    static constexpr std::size_t kSupernodeCount = " << schedule.supernodeToOps.size() << ";\n";
            *stream << "    static constexpr std::size_t kActiveFlagWordCount = "
                    << ((schedule.supernodeToOps.size() + kActiveFlagBitsPerWord - 1u) / kActiveFlagBitsPerWord) << ";\n";
            *stream << "    static constexpr std::size_t kBatchCount = " << scheduleBatches.size() << ";\n";
            *stream << "    static constexpr std::size_t kStateShadowCount = " << model.stateShadows.size() << ";\n";
            *stream << "    static constexpr std::size_t kWriteCount = " << model.writes.size() << ";\n";
            *stream << "\n";
            *stream << "    " << className << "();\n";
            *stream << "    ~" << className << "();\n";
            *stream << "    void init();\n";
            *stream << "    void set_random_seed(std::uint64_t seed);\n";
            *stream << "    void clear_activity_profile();\n";
            *stream << "    void dump_activity_profile() const;\n";
            *stream << "    [[nodiscard]] bool had_register_write_conflict() const;\n";
            if (model.emitWaveform)
            {
                *stream << "    void configure_waveform(bool enabled, std::string path = {});\n";
                *stream << "    void set_waveform_enabled(bool enabled);\n";
                *stream << "    [[nodiscard]] bool waveform_enabled() const;\n";
                *stream << "    void set_waveform_path(std::string path);\n";
                *stream << "    [[nodiscard]] const std::string &waveform_path() const;\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    [[nodiscard]] bool finish_requested() const;\n";
                *stream << "    [[nodiscard]] bool stop_requested() const;\n";
                *stream << "    [[nodiscard]] bool fatal_requested() const;\n";
                *stream << "    [[nodiscard]] int system_exit_code() const;\n";
                *stream << "    [[nodiscard]] const std::string &dumpfile_path() const;\n";
                *stream << "    [[nodiscard]] bool dumpvars_enabled() const;\n";
            }
            *stream << "    void eval();\n";
            for (const auto &decl : model.publicPortDecls)
            {
                *stream << decl << '\n';
            }
            *stream << "\nprivate:\n";
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                *stream << "    void eval_batch_" << batchIndex << "(std::uint8_t &activeWordFlags);\n";
            }
            if (model.emitWaveform)
            {
                for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
                {
                    const auto [waveformBegin, waveformEnd] =
                        waveformSignalRangeForBatch(model.waveformSignals.size(), scheduleBatches.size(), batchIndex);
                    if (waveformBegin < waveformEnd)
                    {
                        *stream << "    void register_waveform_batch_" << batchIndex
                                << "(grhsim_fst_writer &writer);\n";
                        *stream << "    void dump_waveform_batch_" << batchIndex << "();\n";
                    }
                }
            }
            for (const auto &chunk : initChunks)
            {
                *stream << "    void " << chunk.methodName << "();\n";
            }
            for (const auto &chunk : stateShadowCommitChunks)
            {
                *stream << "    void " << chunk.methodName << "(std::uint32_t shadowIndex, bool &activatedReaders);\n";
            }
            for (const auto &chunk : writeCommitChunks)
            {
                *stream << "    void " << chunk.methodName << "(std::uint32_t writeIndex, bool &activatedReaders);\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    struct PendingSystemTaskText {\n";
                *stream << "        bool useHandle = false;\n";
                *stream << "        std::uint64_t handle = 0;\n";
                *stream << "        bool useStderr = false;\n";
                *stream << "        bool newline = true;\n";
                *stream << "        std::string text;\n";
                *stream << "    };\n";
                *stream << "    struct FileHandleEntry {\n";
                *stream << "        std::fstream stream;\n";
                *stream << "        bool canRead = false;\n";
                *stream << "        bool canWrite = false;\n";
                *stream << "        int errorCode = 0;\n";
                *stream << "    };\n";
            }
            *stream << "    [[nodiscard]] bool commit_state_updates();\n";
            *stream << "    void refresh_outputs();\n\n";
            if (!model.stateShadows.empty())
            {
                const bool useBoolRange = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kBool);
                const bool useU8Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU8);
                const bool useU16Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU16);
                const bool useU32Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU32);
                const bool useU64Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU64);
                if (useBoolRange || useU8Range || useU16Range || useU32Range || useU64Range)
                {
                    *stream << "    struct scalar_state_write_range_desc_base {\n";
                    *stream << "        std::uint32_t count = 0;\n";
                    *stream << "        std::uint32_t condBase = 0;\n";
                    *stream << "        std::int32_t condStep = 0;\n";
                    *stream << "        std::uint32_t touchedBase = 0;\n";
                    *stream << "        std::int32_t touchedStep = 0;\n";
                    *stream << "        std::uint32_t shadowDataBase = 0;\n";
                    *stream << "        std::int32_t shadowDataStep = 0;\n";
                    *stream << "        std::uint32_t stateDataBase = 0;\n";
                    *stream << "        std::int32_t stateDataStep = 0;\n";
                    *stream << "        std::uint32_t nextBase = 0;\n";
                    *stream << "        std::int32_t nextStep = 0;\n";
                    *stream << "        std::uint32_t maskBase = 0;\n";
                    *stream << "        std::int32_t maskStep = 0;\n";
                    *stream << "        std::uint32_t shadowBase = 0;\n";
                    *stream << "        std::int32_t shadowStep = 0;\n";
                    *stream << "    };\n";
                }
                if (useBoolRange)
                {
                    *stream << "    using scalar_state_write_bool_range_desc = scalar_state_write_range_desc_base;\n";
                }
                if (useU8Range)
                {
                    *stream << "    using scalar_state_write_u8_range_desc = scalar_state_write_range_desc_base;\n";
                }
                if (useU16Range)
                {
                    *stream << "    using scalar_state_write_u16_range_desc = scalar_state_write_range_desc_base;\n";
                }
                if (useU32Range)
                {
                    *stream << "    using scalar_state_write_u32_range_desc = scalar_state_write_range_desc_base;\n";
                }
                if (useU64Range)
                {
                    *stream << "    using scalar_state_write_u64_range_desc = scalar_state_write_range_desc_base;\n";
                }
                *stream << "    void apply_scalar_state_write_bool(bool cond,\n";
                *stream << "                                      std::uint8_t &shadowTouched,\n";
                *stream << "                                      std::uint8_t &shadowData,\n";
                *stream << "                                      const std::uint8_t &stateData,\n";
                *stream << "                                      std::uint8_t nextValue,\n";
                *stream << "                                      std::uint8_t mask,\n";
                *stream << "                                      std::uint32_t shadowIndex);\n";
                if (useBoolRange)
                {
                    *stream << "    void apply_scalar_state_write_bool_range(const scalar_state_write_bool_range_desc &desc);\n";
                }
                *stream << "    void apply_scalar_state_write_u8(bool cond,\n";
                *stream << "                                    std::uint8_t &shadowTouched,\n";
                *stream << "                                    std::uint8_t &shadowData,\n";
                *stream << "                                    const std::uint8_t &stateData,\n";
                *stream << "                                    std::uint8_t nextValue,\n";
                *stream << "                                    std::uint8_t mask,\n";
                *stream << "                                    std::uint32_t shadowIndex);\n";
                if (useU8Range)
                {
                    *stream << "    void apply_scalar_state_write_u8_range(const scalar_state_write_u8_range_desc &desc);\n";
                }
                *stream << "    void apply_scalar_state_write_u16(bool cond,\n";
                *stream << "                                     std::uint8_t &shadowTouched,\n";
                *stream << "                                     std::uint16_t &shadowData,\n";
                *stream << "                                     const std::uint16_t &stateData,\n";
                *stream << "                                     std::uint16_t nextValue,\n";
                *stream << "                                     std::uint16_t mask,\n";
                *stream << "                                     std::uint32_t shadowIndex);\n";
                if (useU16Range)
                {
                    *stream << "    void apply_scalar_state_write_u16_range(const scalar_state_write_u16_range_desc &desc);\n";
                }
                *stream << "    void apply_scalar_state_write_u32(bool cond,\n";
                *stream << "                                     std::uint8_t &shadowTouched,\n";
                *stream << "                                     std::uint32_t &shadowData,\n";
                *stream << "                                     const std::uint32_t &stateData,\n";
                *stream << "                                     std::uint32_t nextValue,\n";
                *stream << "                                     std::uint32_t mask,\n";
                *stream << "                                     std::uint32_t shadowIndex);\n";
                if (useU32Range)
                {
                    *stream << "    void apply_scalar_state_write_u32_range(const scalar_state_write_u32_range_desc &desc);\n";
                }
                *stream << "    void apply_scalar_state_write_u64(bool cond,\n";
                *stream << "                                     std::uint8_t &shadowTouched,\n";
                *stream << "                                     std::uint64_t &shadowData,\n";
                *stream << "                                     const std::uint64_t &stateData,\n";
                *stream << "                                     std::uint64_t nextValue,\n";
                *stream << "                                     std::uint64_t mask,\n";
                *stream << "                                     std::uint32_t shadowIndex);\n\n";
                if (useU64Range)
                {
                    *stream << "    void apply_scalar_state_write_u64_range(const scalar_state_write_u64_range_desc &desc);\n\n";
                }
            }
            if (model.emitWaveform)
            {
                *stream << "    void ensure_waveform_open();\n";
                *stream << "    void dump_waveform(std::uint64_t time);\n\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    static bool parse_file_open_mode(std::string_view mode,\n";
                *stream << "                                     std::ios::openmode &openMode,\n";
                *stream << "                                     bool &canRead,\n";
                *stream << "                                     bool &canWrite);\n";
                *stream << "    std::uint64_t open_file_handle(const std::string &path, const std::string &mode);\n";
                *stream << "    int file_error_status(std::uint64_t handle) const;\n";
                *stream << "    void close_file_handle(std::uint64_t handle);\n";
                *stream << "    void clear_file_error(std::uint64_t handle);\n";
                *stream << "    void set_file_error(std::uint64_t handle, int errorCode);\n";
                *stream << "    void execute_system_task(std::string_view name, std::initializer_list<grhsim_task_arg> args);\n";
                *stream << "    void flush_deferred_system_task_texts();\n";
                *stream << "    void finalize();\n";
                *stream << "    [[noreturn]] void terminate_host_process(int exitCode);\n";
                *stream << "    std::ostream *resolve_output_stream(std::uint64_t handle, bool useStderr);\n";
                *stream << "    void emit_system_task_text(bool useHandle,\n";
                *stream << "                               std::uint64_t handle,\n";
                *stream << "                               bool useStderr,\n";
                *stream << "                               bool newline,\n";
                *stream << "                               std::string text,\n";
                *stream << "                               bool deferred);\n\n";
            }
            *stream << "    bool first_eval_ = true;\n";
            *stream << "    bool activity_profile_enabled_ = false;\n";
            *stream << "    std::uint64_t activity_profile_current_executed_supernodes_ = UINT64_C(0);\n";
            *stream << "    std::uint64_t activity_profile_current_executed_ops_ = UINT64_C(0);\n";
            *stream << "    std::uint64_t activity_profile_current_peak_active_supernodes_ = UINT64_C(0);\n";
            *stream << "    std::vector<std::uint64_t> activity_profile_executed_supernodes_;\n";
            *stream << "    std::vector<std::uint64_t> activity_profile_executed_ops_;\n";
            *stream << "    std::vector<std::uint64_t> activity_profile_peak_active_supernodes_;\n";
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    bool finalized_ = false;\n";
            }
            if (model.emitPerf)
            {
                *stream << "    bool trace_eval_enabled_ = false;\n";
                *stream << "    std::uint64_t trace_eval_interval_ = UINT64_C(1);\n";
            }
            if (model.emitPerf || model.emitWaveform)
            {
                *stream << "    std::uint64_t eval_invocation_count_ = UINT64_C(0);\n";
            }
            *stream << "    bool register_write_conflict_ = false;\n";
            *stream << "    std::uint64_t random_seed_ = UINT64_C(0);\n";
            *stream << "    std::uint64_t random_state_ = UINT64_C(0);\n";
            if (model.emitWaveform)
            {
                *stream << "    bool waveform_enabled_ = false;\n";
                *stream << "    std::string waveform_path_ = \"" << escapeCppString(prefix + ".fst") << "\";\n";
                *stream << "    std::unique_ptr<grhsim_fst_writer> waveform_writer_;\n";
                *stream << "    std::vector<std::uint32_t> waveform_handles_;\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    bool finish_requested_ = false;\n";
                *stream << "    bool stop_requested_ = false;\n";
                *stream << "    bool fatal_requested_ = false;\n";
                *stream << "    int system_exit_code_ = 0;\n";
                *stream << "    std::string dumpfile_path_;\n";
                *stream << "    bool dumpvars_enabled_ = false;\n";
                *stream << "    std::uint64_t next_file_handle_ = UINT64_C(3);\n";
                *stream << "    std::unordered_map<std::uint64_t, FileHandleEntry> file_handles_;\n";
                *stream << "    std::vector<PendingSystemTaskText> deferred_system_task_texts_;\n";
            }
            *stream << "    std::array<std::uint8_t, kActiveFlagWordCount> supernode_active_curr_{};\n";
            if (!model.stateShadows.empty())
            {
                *stream << "    std::array<std::uint32_t, kStateShadowCount> touched_state_shadow_indices_{};\n";
                *stream << "    std::array<std::uint8_t, kStateShadowCount> touched_state_shadow_flags_{};\n";
                *stream << "    std::size_t touched_state_shadow_count_ = 0;\n";
            }
            if (!model.writes.empty())
            {
                *stream << "    std::array<std::uint32_t, kWriteCount> touched_write_indices_{};\n";
                *stream << "    std::array<std::uint8_t, kWriteCount> touched_write_flags_{};\n";
                *stream << "    std::size_t touched_write_count_ = 0;\n";
            }
            if (model.eventEdgeSlotCount != 0)
            {
                *stream << "    std::vector<grhsim_event_edge_kind> event_edge_slots_;\n";
            }
            *stream << '\n';
            for (const auto &decl : model.inputFieldDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.inputFieldDecls.empty())
            {
                *stream << '\n';
            }
            for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
            {
                const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                if (model.valueScalarSlotCounts[kindIndex] == 0)
                {
                    continue;
                }
                *stream << "    std::vector<" << scalarLogicSlotCppType(kind) << "> "
                        << valueScalarSlotFieldName(kind) << ";\n";
            }
            for (const auto &[wordCount, slotCount] : model.valueWideSlotCountsByWords)
            {
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::array<std::uint64_t, " << wordCount << ">> "
                        << valueWideSlotFieldName(wordCount) << ";\n";
            }
            if (model.valueRealSlotCount != 0)
            {
                *stream << "    std::vector<double> value_real_slots_;\n";
            }
            if (model.valueStringSlotCount != 0)
            {
                *stream << "    std::vector<std::string> value_string_slots_;\n";
            }
            if (model.valueScalarSlotCounts != std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)>{} ||
                !model.valueWideSlotCountsByWords.empty() ||
                model.valueRealSlotCount != 0 ||
                model.valueStringSlotCount != 0)
            {
                *stream << '\n';
            }
            for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
            {
                const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                const std::size_t slotCount = model.stateLogicScalarSlotCounts[kindIndex];
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<" << scalarLogicSlotCppType(kind) << "> "
                        << stateLogicScalarSlotFieldName(kind) << ";\n";
            }
            for (const auto &[wordCount, slotCount] : model.stateLogicWideSlotCountsByWords)
            {
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::array<std::uint64_t, " << wordCount << ">> "
                        << stateLogicWideSlotFieldName(wordCount) << ";\n";
            }
            for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
            {
                const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                const std::size_t slotCount = model.stateMemoryScalarSlotCounts[kindIndex];
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::vector<" << scalarLogicSlotCppType(kind) << ">> "
                        << stateMemoryScalarSlotFieldName(kind) << ";\n";
            }
            for (const auto &[wordCount, slotCount] : model.stateMemoryWideSlotCountsByWords)
            {
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::vector<std::array<std::uint64_t, " << wordCount << ">>> "
                        << stateMemoryWideSlotFieldName(wordCount) << ";\n";
            }
            if (model.stateLogicScalarSlotCounts != std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)>{} ||
                !model.stateLogicWideSlotCountsByWords.empty() ||
                model.stateMemoryScalarSlotCounts != std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)>{} ||
                !model.stateMemoryWideSlotCountsByWords.empty())
            {
                *stream << '\n';
            }
            if (model.stateShadowTouchedCount != 0)
            {
                *stream << "    std::vector<std::uint8_t> state_shadow_touched_slots_;\n";
            }
            for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
            {
                const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                const std::size_t slotCount = model.stateShadowScalarSlotCounts[kindIndex];
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<" << scalarLogicSlotCppType(kind) << "> "
                        << scalarLogicSlotFieldName("state_shadow_", kind) << ";\n";
            }
            for (const auto &[wordCount, slotCount] : model.stateShadowWideSlotCountsByWords)
            {
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::array<std::uint64_t, " << wordCount << ">> "
                        << wideLogicSlotFieldName("state_shadow_", wordCount) << ";\n";
            }
            if (model.stateShadowTouchedCount != 0 ||
                model.stateShadowScalarSlotCounts != std::array<std::size_t, static_cast<std::size_t>(ValueSlotScalarKind::kCount)>{} ||
                !model.stateShadowWideSlotCountsByWords.empty())
            {
                *stream << '\n';
            }
            for (const auto &decl : model.eventFieldDecls)
            {
                *stream << decl << '\n';
            }
            if (!model.eventFieldDecls.empty())
            {
                *stream << '\n';
            }
            if (model.memoryWriteTouchedCount != 0)
            {
                *stream << "    std::vector<std::uint8_t> memory_write_touched_slots_;\n";
                *stream << "    std::vector<std::size_t> memory_write_addr_slots_;\n";
            }
            for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
            {
                const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                const std::size_t dataCount = model.memoryWriteDataScalarSlotCounts[kindIndex];
                if (dataCount != 0)
                {
                    *stream << "    std::vector<" << scalarLogicSlotCppType(kind) << "> "
                            << scalarLogicSlotFieldName("memory_write_data_", kind) << ";\n";
                }
                const std::size_t maskCount = model.memoryWriteMaskScalarSlotCounts[kindIndex];
                if (maskCount != 0)
                {
                    *stream << "    std::vector<" << scalarLogicSlotCppType(kind) << "> "
                            << scalarLogicSlotFieldName("memory_write_mask_", kind) << ";\n";
                }
            }
            for (const auto &[wordCount, slotCount] : model.memoryWriteDataWideSlotCountsByWords)
            {
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::array<std::uint64_t, " << wordCount << ">> "
                        << wideLogicSlotFieldName("memory_write_data_", wordCount) << ";\n";
            }
            for (const auto &[wordCount, slotCount] : model.memoryWriteMaskWideSlotCountsByWords)
            {
                if (slotCount == 0)
                {
                    continue;
                }
                *stream << "    std::vector<std::array<std::uint64_t, " << wordCount << ">> "
                        << wideLogicSlotFieldName("memory_write_mask_", wordCount) << ";\n";
            }
            *stream << "};\n";
            if (auto error = finalizeOutputFile(*stream, headerPath))
            {
                reportError(*error, headerPath.string());
                result.success = false;
                return result;
            }
        }

        auto emitCommitStateShadowBody = [&](LimitedOutputStream &stream,
                                            const StateShadowDecl &shadow,
                                            std::string_view indent)
        {
            const StateDecl &state = model.stateBySymbol.at(shadow.symbol);
            const std::string shadowDataRef = stateShadowDataRef(shadow, state);
            const std::string shadowTouchedRef = stateShadowTouchedRef(shadow);
            const std::string stateExpr = stateRef(state);
            stream << indent << "bool state_changed = false;\n";
            if (isWideLogicWidth(state.width))
            {
                stream << indent << "if (grhsim_assign_words(" << stateExpr << ", " << shadowDataRef << ", "
                       << state.width << ")) {\n";
                stream << indent << "    state_changed = true;\n";
                stream << indent << "}\n";
            }
            else
            {
                stream << indent << "if (" << stateExpr << " != " << shadowDataRef << ") {\n";
                stream << indent << "    " << stateExpr << " = " << shadowDataRef << ";\n";
                stream << indent << "    state_changed = true;\n";
                stream << indent << "}\n";
            }
            stream << indent << "if (state_changed) {\n";
            const auto headIt = model.stateHeadSupernodesBySymbol.find(shadow.symbol);
            if (headIt != model.stateHeadSupernodesBySymbol.end() && !headIt->second.empty())
            {
                stream << indent << "    activatedReaders = true;\n";
                emitActivationStatements(
                    stream,
                    "supernode_active_curr_",
                    "active_count_",
                    headIt->second,
                    std::string(indent) + "    ");
            }
            stream << indent << "}\n";
            stream << indent << shadowTouchedRef << " = 0;\n";
        };

        auto emitCommitWriteBody = [&](LimitedOutputStream &stream, const WriteDecl &write, std::string_view indent)
        {
            const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
            const std::string writeTouchedRef = memoryWriteTouchedRef(write);
            const std::string writeAddrRef = memoryWriteAddrRef(write);
            const std::string writeDataRef = memoryWriteDataRef(write, state);
            const std::string writeMaskRef = memoryWriteMaskRef(write, state);
            const std::string stateExpr = stateRef(state);
            stream << indent << "bool state_changed = false;\n";
            if (state.kind == StateDecl::Kind::Memory)
            {
                stream << indent << "const std::size_t row = " << writeAddrRef << ";\n";
                stream << indent << "if (row < " << state.rowCount << ") {\n";
                if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kConstAllOnes)
                {
                    if (isWideLogicWidth(state.width))
                    {
                        stream << indent << "    if (grhsim_assign_words(" << stateExpr << "[row], "
                               << writeDataRef << ", " << state.width << ")) {\n";
                        stream << indent << "        state_changed = true;\n";
                        stream << indent << "    }\n";
                    }
                    else
                    {
                        stream << indent << "    const auto next_value = static_cast<" << logicCppType(state.width)
                               << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << writeDataRef << "), "
                               << state.width << "));\n";
                        stream << indent << "    if (" << stateExpr << "[row] != next_value) {\n";
                        stream << indent << "        " << stateExpr << "[row] = next_value;\n";
                        stream << indent << "        state_changed = true;\n";
                        stream << indent << "    }\n";
                    }
                }
                else
                {
                    if (isWideLogicWidth(state.width))
                    {
                        stream << indent << "    if (grhsim_apply_masked_words_inplace(" << stateExpr << "[row], "
                               << writeDataRef << ", " << writeMaskRef << ", " << state.width
                               << ")) {\n";
                        stream << indent << "        state_changed = true;\n";
                        stream << indent << "    }\n";
                    }
                    else
                    {
                        stream << indent << "    const auto mask = static_cast<" << logicCppType(state.width)
                               << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << writeMaskRef << "), "
                               << state.width << "));\n";
                        stream << indent << "    const auto merged = static_cast<" << logicCppType(state.width)
                               << ">((" << stateExpr << "[row] & ~mask) | (static_cast<" << logicCppType(state.width)
                               << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << writeDataRef << "), "
                               << state.width << ")) & mask));\n";
                        stream << indent << "    if (" << stateExpr << "[row] != merged) {\n";
                        stream << indent << "        " << stateExpr << "[row] = merged;\n";
                        stream << indent << "        state_changed = true;\n";
                        stream << indent << "    }\n";
                    }
                }
                stream << indent << "}\n";
            }
            stream << indent << "if (state_changed) {\n";
            const auto headIt = model.stateHeadSupernodesBySymbol.find(write.symbol);
            if (headIt != model.stateHeadSupernodesBySymbol.end() && !headIt->second.empty())
            {
                stream << indent << "    activatedReaders = true;\n";
                emitActivationStatements(
                    stream,
                    "supernode_active_curr_",
                    "active_count_",
                    headIt->second,
                    std::string(indent) + "    ");
            }
            stream << indent << "}\n";
            stream << indent << writeTouchedRef << " = 0;\n";
        };

        {
            if (auto error = ensureOutputDirectory(statePath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(statePath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n";
            *stream << "#include <cstdio>\n\n";
            *stream << className << "::" << className << "()\n";
            *stream << "{\n";
            *stream << "    if (const char *activityProfile = std::getenv(\"GRHSIM_ACTIVITY_PROFILE\");\n";
            *stream << "        activityProfile != nullptr && activityProfile[0] != '\\0' && activityProfile[0] != '0') {\n";
            *stream << "        activity_profile_enabled_ = true;\n";
            *stream << "    }\n";
            if (model.emitPerf)
            {
                *stream << "    if (const char *traceEvalEvery = std::getenv(\"GRHSIM_TRACE_EVAL_EVERY\");\n";
                *stream << "        traceEvalEvery != nullptr && traceEvalEvery[0] != '\\0') {\n";
                *stream << "        char *end = nullptr;\n";
                *stream << "        const unsigned long long parsed = std::strtoull(traceEvalEvery, &end, 10);\n";
                *stream << "        if (end != traceEvalEvery && parsed != 0) {\n";
                *stream << "            trace_eval_enabled_ = true;\n";
                *stream << "            trace_eval_interval_ = static_cast<std::uint64_t>(parsed);\n";
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "    if (const char *traceEval = std::getenv(\"GRHSIM_TRACE_EVAL\");\n";
                *stream << "        traceEval != nullptr && traceEval[0] != '\\0' && traceEval[0] != '0') {\n";
                *stream << "        trace_eval_enabled_ = true;\n";
                *stream << "    }\n";
            }
            *stream << "}\n\n";
            *stream << className << "::~" << className << "()\n{\n";
            *stream << "    if (activity_profile_enabled_) {\n";
            *stream << "        dump_activity_profile();\n";
            *stream << "    }\n";
            if (model.emitWaveform)
            {
                *stream << "    if (waveform_writer_) {\n";
                *stream << "        waveform_writer_->close();\n";
                *stream << "        waveform_writer_.reset();\n";
                *stream << "    }\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    finalize();\n";
            }
            *stream << "}\n\n";
            *stream << "void " << className << "::init()\n{\n";
            if (model.emitWaveform)
            {
                *stream << "    if (waveform_writer_) {\n";
                *stream << "        waveform_writer_->close();\n";
                *stream << "        waveform_writer_.reset();\n";
                *stream << "    }\n";
                *stream << "    waveform_handles_.clear();\n";
            }
            for (const auto &chunk : initChunks)
            {
                *stream << "    " << chunk.methodName << "();\n";
            }
            *stream << "}\n\n";
            *stream << "void " << className << "::set_random_seed(std::uint64_t seed)\n{\n";
            *stream << "    random_seed_ = seed;\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::clear_activity_profile()\n{\n";
            *stream << "    activity_profile_current_executed_supernodes_ = UINT64_C(0);\n";
            *stream << "    activity_profile_current_executed_ops_ = UINT64_C(0);\n";
            *stream << "    activity_profile_current_peak_active_supernodes_ = UINT64_C(0);\n";
            *stream << "    activity_profile_executed_supernodes_.clear();\n";
            *stream << "    activity_profile_executed_ops_.clear();\n";
            *stream << "    activity_profile_peak_active_supernodes_.clear();\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::dump_activity_profile() const\n{\n";
            *stream << "    std::fprintf(stderr, \"[grhsim-activity] step_samples=%zu\\n\", activity_profile_executed_supernodes_.size());\n";
            *stream << "    grhsim_print_sample_summary(stderr, \"executed_supernodes_per_step\", activity_profile_executed_supernodes_);\n";
            *stream << "    grhsim_print_sample_summary(stderr, \"executed_ops_per_step\", activity_profile_executed_ops_);\n";
            *stream << "    grhsim_print_sample_summary(stderr, \"peak_active_supernodes_per_step\", activity_profile_peak_active_supernodes_);\n";
            *stream << "    std::fflush(stderr);\n";
            *stream << "}\n\n";
            *stream << "bool " << className << "::had_register_write_conflict() const\n{\n";
            *stream << "    return register_write_conflict_;\n";
            *stream << "}\n\n";
            if (!model.stateShadows.empty())
            {
                const bool useBoolRange = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kBool);
                const bool useU8Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU8);
                const bool useU16Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU16);
                const bool useU32Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU32);
                const bool useU64Range = modelUsesScalarStateWriteKind(model, ValueSlotScalarKind::kU64);
                *stream << "void " << className
                        << "::apply_scalar_state_write_bool(bool cond,\n"
                        << "                                       std::uint8_t &shadowTouched,\n"
                        << "                                       std::uint8_t &shadowData,\n"
                        << "                                       const std::uint8_t &stateData,\n"
                        << "                                       std::uint8_t nextValue,\n"
                        << "                                       std::uint8_t mask,\n"
                        << "                                       std::uint32_t shadowIndex)\n{\n";
                *stream << "    if (!cond) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if ((mask & UINT8_C(1)) == UINT8_C(0)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto state_shadow_base = shadowTouched ? shadowData : stateData;\n";
                *stream << "    const auto next_bit = static_cast<std::uint8_t>(nextValue & UINT8_C(1));\n";
                *stream << "    if (next_bit == state_shadow_base) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    shadowData = next_bit;\n";
                *stream << "    shadowTouched = 1;\n";
                *stream << "    grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "}\n\n";
                if (useBoolRange)
                {
                    *stream << "void " << className
                            << "::apply_scalar_state_write_bool_range(const scalar_state_write_bool_range_desc &desc)\n{\n";
                    *stream << "    for (std::uint32_t offset = 0; offset < desc.count; ++offset) {\n";
                    *stream << "        const auto condIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.condBase) +\n";
                    *stream << "                                                      static_cast<std::int64_t>(desc.condStep) * offset);\n";
                    *stream << "        const auto touchedIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.touchedBase) +\n";
                    *stream << "                                                         static_cast<std::int64_t>(desc.touchedStep) * offset);\n";
                    *stream << "        const auto shadowDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowDataBase) +\n";
                    *stream << "                                                            static_cast<std::int64_t>(desc.shadowDataStep) * offset);\n";
                    *stream << "        const auto stateDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.stateDataBase) +\n";
                    *stream << "                                                           static_cast<std::int64_t>(desc.stateDataStep) * offset);\n";
                    *stream << "        const auto nextIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.nextBase) +\n";
                    *stream << "                                                      static_cast<std::int64_t>(desc.nextStep) * offset);\n";
                    *stream << "        const auto maskIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.maskBase) +\n";
                    *stream << "                                                      static_cast<std::int64_t>(desc.maskStep) * offset);\n";
                    *stream << "        const auto shadowIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowBase) +\n";
                    *stream << "                                                        static_cast<std::int64_t>(desc.shadowStep) * offset);\n";
                    *stream << "        apply_scalar_state_write_bool((value_bool_slots_[condIndex]) != 0,\n";
                    *stream << "                                      state_shadow_touched_slots_[touchedIndex],\n";
                    *stream << "                                      state_shadow_bool_slots_[shadowDataIndex],\n";
                    *stream << "                                      state_logic_bool_slots_[stateDataIndex],\n";
                    *stream << "                                      value_bool_slots_[nextIndex],\n";
                    *stream << "                                      value_bool_slots_[maskIndex],\n";
                    *stream << "                                      shadowIndex);\n";
                    *stream << "    }\n";
                    *stream << "}\n\n";
                }

                *stream << "void " << className
                        << "::apply_scalar_state_write_u8(bool cond,\n"
                        << "                                     std::uint8_t &shadowTouched,\n"
                        << "                                     std::uint8_t &shadowData,\n"
                        << "                                     const std::uint8_t &stateData,\n"
                        << "                                     std::uint8_t nextValue,\n"
                        << "                                     std::uint8_t mask,\n"
                        << "                                     std::uint32_t shadowIndex)\n{\n";
                *stream << "    if (!cond) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (mask == UINT8_C(0)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto state_shadow_base = shadowTouched ? shadowData : stateData;\n";
                *stream << "    if (mask == UINT8_MAX) {\n";
                *stream << "        if (nextValue == state_shadow_base) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        shadowData = nextValue;\n";
                *stream << "        shadowTouched = 1;\n";
                *stream << "        grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if ((mask & static_cast<std::uint8_t>(mask - UINT8_C(1))) == UINT8_C(0)) {\n";
                *stream << "        const auto next_bits = static_cast<std::uint8_t>(nextValue & mask);\n";
                *stream << "        const auto base_bits = static_cast<std::uint8_t>(state_shadow_base & mask);\n";
                *stream << "        if (next_bits == base_bits) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        shadowData = static_cast<std::uint8_t>((state_shadow_base & static_cast<std::uint8_t>(~mask)) | next_bits);\n";
                *stream << "        shadowTouched = 1;\n";
                *stream << "        grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto merged = static_cast<std::uint8_t>((state_shadow_base & static_cast<std::uint8_t>(~mask)) | (nextValue & mask));\n";
                *stream << "    if (merged == state_shadow_base) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    shadowData = merged;\n";
                *stream << "    shadowTouched = 1;\n";
                *stream << "    grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "}\n\n";
                if (useU8Range)
                {
                    *stream << "void " << className
                            << "::apply_scalar_state_write_u8_range(const scalar_state_write_u8_range_desc &desc)\n{\n";
                *stream << "    for (std::uint32_t offset = 0; offset < desc.count; ++offset) {\n";
                *stream << "        const auto condIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.condBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.condStep) * offset);\n";
                *stream << "        const auto touchedIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.touchedBase) +\n";
                *stream << "                                                         static_cast<std::int64_t>(desc.touchedStep) * offset);\n";
                *stream << "        const auto shadowDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowDataBase) +\n";
                *stream << "                                                            static_cast<std::int64_t>(desc.shadowDataStep) * offset);\n";
                *stream << "        const auto stateDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.stateDataBase) +\n";
                *stream << "                                                           static_cast<std::int64_t>(desc.stateDataStep) * offset);\n";
                *stream << "        const auto nextIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.nextBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.nextStep) * offset);\n";
                *stream << "        const auto maskIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.maskBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.maskStep) * offset);\n";
                *stream << "        const auto shadowIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowBase) +\n";
                *stream << "                                                        static_cast<std::int64_t>(desc.shadowStep) * offset);\n";
                *stream << "        apply_scalar_state_write_u8((value_bool_slots_[condIndex]) != 0,\n";
                *stream << "                                    state_shadow_touched_slots_[touchedIndex],\n";
                *stream << "                                    state_shadow_u8_slots_[shadowDataIndex],\n";
                *stream << "                                    state_logic_u8_slots_[stateDataIndex],\n";
                *stream << "                                    value_u8_slots_[nextIndex],\n";
                *stream << "                                    value_u8_slots_[maskIndex],\n";
                *stream << "                                    shadowIndex);\n";
                *stream << "    }\n";
                    *stream << "}\n\n";
                }

                *stream << "void " << className
                        << "::apply_scalar_state_write_u16(bool cond,\n"
                        << "                                      std::uint8_t &shadowTouched,\n"
                        << "                                      std::uint16_t &shadowData,\n"
                        << "                                      const std::uint16_t &stateData,\n"
                        << "                                      std::uint16_t nextValue,\n"
                        << "                                      std::uint16_t mask,\n"
                        << "                                      std::uint32_t shadowIndex)\n{\n";
                *stream << "    if (!cond) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (mask == UINT16_C(0)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto state_shadow_base = shadowTouched ? shadowData : stateData;\n";
                *stream << "    if (mask == UINT16_MAX) {\n";
                *stream << "        if (nextValue == state_shadow_base) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        shadowData = nextValue;\n";
                *stream << "        shadowTouched = 1;\n";
                *stream << "        grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto merged = static_cast<std::uint16_t>((state_shadow_base & static_cast<std::uint16_t>(~mask)) | (nextValue & mask));\n";
                *stream << "    if (merged == state_shadow_base) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    shadowData = merged;\n";
                *stream << "    shadowTouched = 1;\n";
                *stream << "    grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "}\n\n";
                if (useU16Range)
                {
                    *stream << "void " << className
                            << "::apply_scalar_state_write_u16_range(const scalar_state_write_u16_range_desc &desc)\n{\n";
                *stream << "    for (std::uint32_t offset = 0; offset < desc.count; ++offset) {\n";
                *stream << "        const auto condIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.condBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.condStep) * offset);\n";
                *stream << "        const auto touchedIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.touchedBase) +\n";
                *stream << "                                                         static_cast<std::int64_t>(desc.touchedStep) * offset);\n";
                *stream << "        const auto shadowDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowDataBase) +\n";
                *stream << "                                                            static_cast<std::int64_t>(desc.shadowDataStep) * offset);\n";
                *stream << "        const auto stateDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.stateDataBase) +\n";
                *stream << "                                                           static_cast<std::int64_t>(desc.stateDataStep) * offset);\n";
                *stream << "        const auto nextIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.nextBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.nextStep) * offset);\n";
                *stream << "        const auto maskIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.maskBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.maskStep) * offset);\n";
                *stream << "        const auto shadowIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowBase) +\n";
                *stream << "                                                        static_cast<std::int64_t>(desc.shadowStep) * offset);\n";
                *stream << "        apply_scalar_state_write_u16((value_bool_slots_[condIndex]) != 0,\n";
                *stream << "                                     state_shadow_touched_slots_[touchedIndex],\n";
                *stream << "                                     state_shadow_u16_slots_[shadowDataIndex],\n";
                *stream << "                                     state_logic_u16_slots_[stateDataIndex],\n";
                *stream << "                                     value_u16_slots_[nextIndex],\n";
                *stream << "                                     value_u16_slots_[maskIndex],\n";
                *stream << "                                     shadowIndex);\n";
                *stream << "    }\n";
                    *stream << "}\n\n";
                }

                *stream << "void " << className
                        << "::apply_scalar_state_write_u32(bool cond,\n"
                        << "                                      std::uint8_t &shadowTouched,\n"
                        << "                                      std::uint32_t &shadowData,\n"
                        << "                                      const std::uint32_t &stateData,\n"
                        << "                                      std::uint32_t nextValue,\n"
                        << "                                      std::uint32_t mask,\n"
                        << "                                      std::uint32_t shadowIndex)\n{\n";
                *stream << "    if (!cond) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (mask == UINT32_C(0)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto state_shadow_base = shadowTouched ? shadowData : stateData;\n";
                *stream << "    if (mask == UINT32_MAX) {\n";
                *stream << "        if (nextValue == state_shadow_base) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        shadowData = nextValue;\n";
                *stream << "        shadowTouched = 1;\n";
                *stream << "        grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto merged = static_cast<std::uint32_t>((state_shadow_base & static_cast<std::uint32_t>(~mask)) | (nextValue & mask));\n";
                *stream << "    if (merged == state_shadow_base) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    shadowData = merged;\n";
                *stream << "    shadowTouched = 1;\n";
                *stream << "    grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "}\n\n";
                if (useU32Range)
                {
                    *stream << "void " << className
                            << "::apply_scalar_state_write_u32_range(const scalar_state_write_u32_range_desc &desc)\n{\n";
                *stream << "    for (std::uint32_t offset = 0; offset < desc.count; ++offset) {\n";
                *stream << "        const auto condIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.condBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.condStep) * offset);\n";
                *stream << "        const auto touchedIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.touchedBase) +\n";
                *stream << "                                                         static_cast<std::int64_t>(desc.touchedStep) * offset);\n";
                *stream << "        const auto shadowDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowDataBase) +\n";
                *stream << "                                                            static_cast<std::int64_t>(desc.shadowDataStep) * offset);\n";
                *stream << "        const auto stateDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.stateDataBase) +\n";
                *stream << "                                                           static_cast<std::int64_t>(desc.stateDataStep) * offset);\n";
                *stream << "        const auto nextIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.nextBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.nextStep) * offset);\n";
                *stream << "        const auto maskIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.maskBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.maskStep) * offset);\n";
                *stream << "        const auto shadowIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowBase) +\n";
                *stream << "                                                        static_cast<std::int64_t>(desc.shadowStep) * offset);\n";
                *stream << "        apply_scalar_state_write_u32((value_bool_slots_[condIndex]) != 0,\n";
                *stream << "                                     state_shadow_touched_slots_[touchedIndex],\n";
                *stream << "                                     state_shadow_u32_slots_[shadowDataIndex],\n";
                *stream << "                                     state_logic_u32_slots_[stateDataIndex],\n";
                *stream << "                                     value_u32_slots_[nextIndex],\n";
                *stream << "                                     value_u32_slots_[maskIndex],\n";
                *stream << "                                     shadowIndex);\n";
                *stream << "    }\n";
                    *stream << "}\n\n";
                }

                *stream << "void " << className
                        << "::apply_scalar_state_write_u64(bool cond,\n"
                        << "                                      std::uint8_t &shadowTouched,\n"
                        << "                                      std::uint64_t &shadowData,\n"
                        << "                                      const std::uint64_t &stateData,\n"
                        << "                                      std::uint64_t nextValue,\n"
                        << "                                      std::uint64_t mask,\n"
                        << "                                      std::uint32_t shadowIndex)\n{\n";
                *stream << "    if (!cond) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (mask == UINT64_C(0)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto state_shadow_base = shadowTouched ? shadowData : stateData;\n";
                *stream << "    if (mask == UINT64_MAX) {\n";
                *stream << "        if (nextValue == state_shadow_base) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        shadowData = nextValue;\n";
                *stream << "        shadowTouched = 1;\n";
                *stream << "        grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    const auto merged = static_cast<std::uint64_t>((state_shadow_base & static_cast<std::uint64_t>(~mask)) | (nextValue & mask));\n";
                *stream << "    if (merged == state_shadow_base) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    shadowData = merged;\n";
                *stream << "    shadowTouched = 1;\n";
                *stream << "    grhsim_mark_pending_write(touched_state_shadow_indices_, touched_state_shadow_flags_, touched_state_shadow_count_, shadowIndex);\n";
                *stream << "}\n\n";
                if (useU64Range)
                {
                    *stream << "void " << className
                            << "::apply_scalar_state_write_u64_range(const scalar_state_write_u64_range_desc &desc)\n{\n";
                *stream << "    for (std::uint32_t offset = 0; offset < desc.count; ++offset) {\n";
                *stream << "        const auto condIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.condBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.condStep) * offset);\n";
                *stream << "        const auto touchedIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.touchedBase) +\n";
                *stream << "                                                         static_cast<std::int64_t>(desc.touchedStep) * offset);\n";
                *stream << "        const auto shadowDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowDataBase) +\n";
                *stream << "                                                            static_cast<std::int64_t>(desc.shadowDataStep) * offset);\n";
                *stream << "        const auto stateDataIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.stateDataBase) +\n";
                *stream << "                                                           static_cast<std::int64_t>(desc.stateDataStep) * offset);\n";
                *stream << "        const auto nextIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.nextBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.nextStep) * offset);\n";
                *stream << "        const auto maskIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.maskBase) +\n";
                *stream << "                                                      static_cast<std::int64_t>(desc.maskStep) * offset);\n";
                *stream << "        const auto shadowIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(desc.shadowBase) +\n";
                *stream << "                                                        static_cast<std::int64_t>(desc.shadowStep) * offset);\n";
                *stream << "        apply_scalar_state_write_u64((value_bool_slots_[condIndex]) != 0,\n";
                *stream << "                                     state_shadow_touched_slots_[touchedIndex],\n";
                *stream << "                                     state_shadow_u64_slots_[shadowDataIndex],\n";
                *stream << "                                     state_logic_u64_slots_[stateDataIndex],\n";
                *stream << "                                     value_u64_slots_[nextIndex],\n";
                *stream << "                                     value_u64_slots_[maskIndex],\n";
                *stream << "                                     shadowIndex);\n";
                *stream << "    }\n";
                    *stream << "}\n\n";
                }
            }
            if (model.emitWaveform)
            {
                *stream << "void " << className << "::configure_waveform(bool enabled, std::string path)\n{\n";
                *stream << "    if (!path.empty()) {\n";
                *stream << "        set_waveform_path(std::move(path));\n";
                *stream << "    }\n";
                *stream << "    set_waveform_enabled(enabled);\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::set_waveform_enabled(bool enabled)\n{\n";
                *stream << "    waveform_enabled_ = enabled;\n";
                *stream << "    if (!waveform_enabled_ && waveform_writer_) {\n";
                *stream << "        waveform_writer_->close();\n";
                *stream << "        waveform_writer_.reset();\n";
                *stream << "        waveform_handles_.clear();\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::waveform_enabled() const\n{\n";
                *stream << "    return waveform_enabled_;\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::set_waveform_path(std::string path)\n{\n";
                *stream << "    waveform_path_ = std::move(path);\n";
                *stream << "    if (waveform_writer_) {\n";
                *stream << "        waveform_writer_->close();\n";
                *stream << "        waveform_writer_.reset();\n";
                *stream << "        waveform_handles_.clear();\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "const std::string &" << className << "::waveform_path() const\n{\n";
                *stream << "    return waveform_path_;\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::ensure_waveform_open()\n{\n";
                *stream << "    if (!waveform_enabled_ || waveform_path_.empty() || waveform_writer_) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    auto writer = std::make_unique<grhsim_fst_writer>();\n";
                *stream << "    if (!writer->open(waveform_path_, \"" << escapeCppString(graph.symbol()) << "\")) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    waveform_handles_.clear();\n";
                *stream << "    waveform_handles_.reserve(" << model.waveformSignals.size() << ");\n";
                for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
                {
                    const auto [waveformBegin, waveformEnd] =
                        waveformSignalRangeForBatch(model.waveformSignals.size(), scheduleBatches.size(), batchIndex);
                    if (waveformBegin < waveformEnd)
                    {
                        *stream << "    register_waveform_batch_" << batchIndex << "(*writer);\n";
                    }
                }
                *stream << "    waveform_writer_ = std::move(writer);\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::dump_waveform(std::uint64_t time)\n{\n";
                *stream << "    ensure_waveform_open();\n";
                *stream << "    if (!waveform_writer_) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    waveform_writer_->emit_time(time);\n";
                for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
                {
                    const auto [waveformBegin, waveformEnd] =
                        waveformSignalRangeForBatch(model.waveformSignals.size(), scheduleBatches.size(), batchIndex);
                    if (waveformBegin < waveformEnd)
                    {
                        *stream << "    dump_waveform_batch_" << batchIndex << "();\n";
                    }
                }
                *stream << "}\n\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "bool " << className << "::parse_file_open_mode(std::string_view mode,\n";
                *stream << "                                 std::ios::openmode &openMode,\n";
                *stream << "                                 bool &canRead,\n";
                *stream << "                                 bool &canWrite)\n{\n";
                *stream << "    openMode = std::ios::openmode{};\n";
                *stream << "    canRead = false;\n";
                *stream << "    canWrite = false;\n";
                *stream << "    if (mode.empty()) {\n";
                *stream << "        mode = \"r\";\n";
                *stream << "    }\n";
                *stream << "    char base = '\\0';\n";
                *stream << "    bool binary = false;\n";
                *stream << "    bool plus = false;\n";
                *stream << "    for (char ch : mode) {\n";
                *stream << "        switch (ch) {\n";
                *stream << "        case 'r':\n";
                *stream << "        case 'w':\n";
                *stream << "        case 'a':\n";
                *stream << "            if (base != '\\0') {\n";
                *stream << "                return false;\n";
                *stream << "            }\n";
                *stream << "            base = ch;\n";
                *stream << "            break;\n";
                *stream << "        case 'b':\n";
                *stream << "            binary = true;\n";
                *stream << "            break;\n";
                *stream << "        case '+':\n";
                *stream << "            plus = true;\n";
                *stream << "            break;\n";
                *stream << "        default:\n";
                *stream << "            return false;\n";
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "    switch (base) {\n";
                *stream << "    case 'r':\n";
                *stream << "        openMode = std::ios::in;\n";
                *stream << "        canRead = true;\n";
                *stream << "        break;\n";
                *stream << "    case 'w':\n";
                *stream << "        openMode = std::ios::out | std::ios::trunc;\n";
                *stream << "        canWrite = true;\n";
                *stream << "        break;\n";
                *stream << "    case 'a':\n";
                *stream << "        openMode = std::ios::out | std::ios::app;\n";
                *stream << "        canWrite = true;\n";
                *stream << "        break;\n";
                *stream << "    default:\n";
                *stream << "        return false;\n";
                *stream << "    }\n";
                *stream << "    if (plus) {\n";
                *stream << "        openMode |= std::ios::in | std::ios::out;\n";
                *stream << "        canRead = true;\n";
                *stream << "        canWrite = true;\n";
                *stream << "    }\n";
                *stream << "    if (binary) {\n";
                *stream << "        openMode |= std::ios::binary;\n";
                *stream << "    }\n";
                *stream << "    return true;\n";
                *stream << "}\n\n";
                *stream << "std::uint64_t " << className << "::open_file_handle(const std::string &path,\n";
                *stream << "                                       const std::string &mode)\n{\n";
                *stream << "    std::ios::openmode openMode{};\n";
                *stream << "    bool canRead = false;\n";
                *stream << "    bool canWrite = false;\n";
                *stream << "    if (!parse_file_open_mode(mode, openMode, canRead, canWrite)) {\n";
                *stream << "        return UINT64_C(0);\n";
                *stream << "    }\n";
                *stream << "    FileHandleEntry entry;\n";
                *stream << "    entry.canRead = canRead;\n";
                *stream << "    entry.canWrite = canWrite;\n";
                *stream << "    entry.stream.open(path, openMode);\n";
                *stream << "    if (!entry.stream.is_open()) {\n";
                *stream << "        return UINT64_C(0);\n";
                *stream << "    }\n";
                *stream << "    std::uint64_t handle = next_file_handle_ <= UINT64_C(2) ? UINT64_C(3) : next_file_handle_;\n";
                *stream << "    const std::uint64_t start = handle;\n";
                *stream << "    while (file_handles_.find(handle) != file_handles_.end()) {\n";
                *stream << "        ++handle;\n";
                *stream << "        if (handle <= UINT64_C(2)) {\n";
                *stream << "            handle = UINT64_C(3);\n";
                *stream << "        }\n";
                *stream << "        if (handle == start) {\n";
                *stream << "            return UINT64_C(0);\n";
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "    file_handles_.emplace(handle, std::move(entry));\n";
                *stream << "    next_file_handle_ = handle + UINT64_C(1);\n";
                *stream << "    if (next_file_handle_ <= UINT64_C(2)) {\n";
                *stream << "        next_file_handle_ = UINT64_C(3);\n";
                *stream << "    }\n";
                *stream << "    return handle;\n";
                *stream << "}\n\n";
                *stream << "int " << className << "::file_error_status(std::uint64_t handle) const\n{\n";
                *stream << "    if (handle <= UINT64_C(2)) {\n";
                *stream << "        return 0;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        return it->second.errorCode;\n";
                *stream << "    }\n";
                *stream << "    return 1;\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::close_file_handle(std::uint64_t handle)\n{\n";
                *stream << "    if (handle == UINT64_C(1)) {\n";
                *stream << "        std::cout.flush();\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (handle == UINT64_C(2)) {\n";
                *stream << "        std::cerr.flush();\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        if (it->second.canWrite) {\n";
                *stream << "            it->second.stream.flush();\n";
                *stream << "        }\n";
                *stream << "        it->second.stream.close();\n";
                *stream << "        file_handles_.erase(it);\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::clear_file_error(std::uint64_t handle)\n{\n";
                *stream << "    if (handle <= UINT64_C(2)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        it->second.errorCode = 0;\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::set_file_error(std::uint64_t handle, int errorCode)\n{\n";
                *stream << "    if (handle <= UINT64_C(2)) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        it->second.errorCode = errorCode;\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::finish_requested() const\n{\n";
                *stream << "    return finish_requested_;\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::stop_requested() const\n{\n";
                *stream << "    return stop_requested_;\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::fatal_requested() const\n{\n";
                *stream << "    return fatal_requested_;\n";
                *stream << "}\n\n";
                *stream << "int " << className << "::system_exit_code() const\n{\n";
                *stream << "    return system_exit_code_;\n";
                *stream << "}\n\n";
                *stream << "const std::string &" << className << "::dumpfile_path() const\n{\n";
                *stream << "    return dumpfile_path_;\n";
                *stream << "}\n\n";
                *stream << "bool " << className << "::dumpvars_enabled() const\n{\n";
                *stream << "    return dumpvars_enabled_;\n";
                *stream << "}\n\n";
                *stream << "std::ostream *" << className << "::resolve_output_stream(std::uint64_t handle,\n";
                *stream << "                                        bool useStderr)\n{\n";
                *stream << "    (void)useStderr;\n";
                *stream << "    if (handle == 1) {\n";
                *stream << "        return &std::cout;\n";
                *stream << "    }\n";
                *stream << "    if (handle == 2) {\n";
                *stream << "        return &std::cerr;\n";
                *stream << "    }\n";
                *stream << "    if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "        if (!it->second.canWrite || !it->second.stream.is_open()) {\n";
                *stream << "            it->second.errorCode = 1;\n";
                *stream << "            return nullptr;\n";
                *stream << "        }\n";
                *stream << "        it->second.errorCode = 0;\n";
                *stream << "        return &it->second.stream;\n";
                *stream << "    }\n";
                *stream << "    return nullptr;\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::emit_system_task_text(bool useHandle,\n";
                *stream << "                                std::uint64_t handle,\n";
                *stream << "                                bool useStderr,\n";
                *stream << "                                bool newline,\n";
                *stream << "                                std::string text,\n";
                *stream << "                                bool deferred)\n{\n";
                *stream << "    if (deferred) {\n";
                *stream << "        deferred_system_task_texts_.push_back(PendingSystemTaskText{\n";
                *stream << "            .useHandle = useHandle,\n";
                *stream << "            .handle = handle,\n";
                *stream << "            .useStderr = useStderr,\n";
                *stream << "            .newline = newline,\n";
                *stream << "            .text = std::move(text),\n";
                *stream << "        });\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    std::ostream *out = nullptr;\n";
                *stream << "    if (useHandle) {\n";
                *stream << "        out = resolve_output_stream(handle, useStderr);\n";
                *stream << "    }\n";
                *stream << "    else {\n";
                *stream << "        out = useStderr ? &std::cerr : &std::cout;\n";
                *stream << "    }\n";
                *stream << "    if (out == nullptr) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    (*out) << text;\n";
                *stream << "    if (newline) {\n";
                *stream << "        (*out) << '\\n';\n";
                *stream << "    }\n";
                *stream << "    if (useHandle && handle > UINT64_C(2)) {\n";
                *stream << "        clear_file_error(handle);\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::flush_deferred_system_task_texts()\n{\n";
                *stream << "    for (auto &item : deferred_system_task_texts_) {\n";
                *stream << "        emit_system_task_text(item.useHandle,\n";
                *stream << "                              item.handle,\n";
                *stream << "                              item.useStderr,\n";
                *stream << "                              item.newline,\n";
                *stream << "                              std::move(item.text),\n";
                *stream << "                              false);\n";
                *stream << "    }\n";
                *stream << "    deferred_system_task_texts_.clear();\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::execute_system_task(std::string_view name,\n";
                *stream << "                               std::initializer_list<grhsim_task_arg> args)\n{\n";
                *stream << "    const std::vector<grhsim_task_arg> items(args.begin(), args.end());\n";
                *stream << "    if (name == \"display\" || name == \"write\" || name == \"strobe\") {\n";
                *stream << "        emit_system_task_text(false,\n";
                *stream << "                              0,\n";
                *stream << "                              false,\n";
                *stream << "                              name != \"write\",\n";
                *stream << "                              grhsim_format_task_message(items),\n";
                *stream << "                              name == \"strobe\");\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fdisplay\" || name == \"fwrite\") {\n";
                *stream << "        if (items.empty()) {\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        const std::uint64_t handle = grhsim_task_arg_u64(items.front());\n";
                *stream << "        const std::vector<grhsim_task_arg> msgArgs(items.begin() + 1, items.end());\n";
                *stream << "        emit_system_task_text(true,\n";
                *stream << "                              handle,\n";
                *stream << "                              false,\n";
                *stream << "                              name == \"fdisplay\",\n";
                *stream << "                              grhsim_format_task_message(msgArgs),\n";
                *stream << "                              false);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fflush\") {\n";
                *stream << "        if (items.empty()) {\n";
                *stream << "            std::cout.flush();\n";
                *stream << "            std::cerr.flush();\n";
                *stream << "            for (auto &[handle, entry] : file_handles_) {\n";
                *stream << "                if (entry.canWrite) {\n";
                *stream << "                    entry.stream.flush();\n";
                *stream << "                    entry.errorCode = 0;\n";
                *stream << "                }\n";
                *stream << "            }\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        const std::uint64_t handle = grhsim_task_arg_u64(items.front());\n";
                *stream << "        if (handle == UINT64_C(1)) {\n";
                *stream << "            std::cout.flush();\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        if (handle == UINT64_C(2)) {\n";
                *stream << "            std::cerr.flush();\n";
                *stream << "            return;\n";
                *stream << "        }\n";
                *stream << "        if (auto it = file_handles_.find(handle); it != file_handles_.end()) {\n";
                *stream << "            if (it->second.canWrite && it->second.stream.is_open()) {\n";
                *stream << "                it->second.stream.flush();\n";
                *stream << "                it->second.errorCode = 0;\n";
                *stream << "            }\n";
                *stream << "            else {\n";
                *stream << "                it->second.errorCode = 1;\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "        else {\n";
                *stream << "            set_file_error(handle, 1);\n";
                *stream << "        }\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fclose\") {\n";
                *stream << "        if (!items.empty()) {\n";
                *stream << "            close_file_handle(grhsim_task_arg_u64(items.front()));\n";
                *stream << "        }\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"dumpfile\") {\n";
                *stream << "        if (!items.empty()) {\n";
                *stream << "            dumpfile_path_ = grhsim_task_default_arg_text(items.front());\n";
                *stream << "        }\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"dumpvars\") {\n";
                *stream << "        dumpvars_enabled_ = true;\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"info\" || name == \"warning\" || name == \"error\") {\n";
                *stream << "        const bool useStderr = (name == \"warning\" || name == \"error\");\n";
                *stream << "        const std::string prefix = \"[\" + std::string(name) + \"] \";\n";
                *stream << "        emit_system_task_text(false,\n";
                *stream << "                              0,\n";
                *stream << "                              useStderr,\n";
                *stream << "                              true,\n";
                *stream << "                              prefix + grhsim_format_task_message(items),\n";
                *stream << "                              false);\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    if (name == \"fatal\") {\n";
                *stream << "        std::size_t msgStart = 0;\n";
                *stream << "        int exitCode = 1;\n";
                *stream << "        if (!items.empty() && items.front().kind == grhsim_task_arg_kind::Logic) {\n";
                *stream << "            exitCode = static_cast<int>(grhsim_task_arg_u64(items.front()));\n";
                *stream << "            msgStart = 1;\n";
                *stream << "        }\n";
                *stream << "        const std::vector<grhsim_task_arg> msgArgs(items.begin() + msgStart, items.end());\n";
                *stream << "        fatal_requested_ = true;\n";
                *stream << "        system_exit_code_ = exitCode;\n";
                *stream << "        emit_system_task_text(false,\n";
                *stream << "                              0,\n";
                *stream << "                              true,\n";
                *stream << "                              true,\n";
                *stream << "                              std::string(\"[fatal] \") + grhsim_format_task_message(msgArgs),\n";
                *stream << "                              false);\n";
                *stream << "        terminate_host_process(exitCode);\n";
                *stream << "    }\n";
                *stream << "    if (name == \"finish\" || name == \"stop\") {\n";
                *stream << "        std::size_t msgStart = 0;\n";
                *stream << "        int exitCode = 0;\n";
                *stream << "        if (!items.empty() && items.front().kind == grhsim_task_arg_kind::Logic) {\n";
                *stream << "            exitCode = static_cast<int>(grhsim_task_arg_u64(items.front()));\n";
                *stream << "            msgStart = 1;\n";
                *stream << "        }\n";
                *stream << "        if (name == \"finish\") {\n";
                *stream << "            finish_requested_ = true;\n";
                *stream << "        }\n";
                *stream << "        else {\n";
                *stream << "            stop_requested_ = true;\n";
                *stream << "        }\n";
                *stream << "        system_exit_code_ = exitCode;\n";
                *stream << "        const std::vector<grhsim_task_arg> msgArgs(items.begin() + msgStart, items.end());\n";
                *stream << "        if (!msgArgs.empty()) {\n";
                *stream << "            emit_system_task_text(false,\n";
                *stream << "                                  0,\n";
                *stream << "                                  false,\n";
                *stream << "                                  true,\n";
                *stream << "                                  std::string(\"[\") + std::string(name) + \"] \" + grhsim_format_task_message(msgArgs),\n";
                *stream << "                                  false);\n";
                *stream << "        }\n";
                *stream << "        terminate_host_process(exitCode);\n";
                *stream << "    }\n";
                *stream << "}\n\n";
                *stream << "void " << className << "::finalize()\n{\n";
                *stream << "    if (finalized_) {\n";
                *stream << "        return;\n";
                *stream << "    }\n";
                *stream << "    finalized_ = true;\n";
                for (OperationId opId : graph.operations())
                {
                    const Operation op = graph.getOperation(opId);
                    if (op.kind() != OperationKind::kSystemTask || !systemTaskIsFinal(op))
                    {
                        continue;
                    }
                    const auto operands = op.operands();
                    const std::size_t eventCount = getAttribute<std::vector<std::string>>(op, "eventEdge").value_or(std::vector<std::string>{}).size();
                    const std::size_t argEnd = operands.size() >= eventCount ? operands.size() - eventCount : operands.size();
                    const std::string name = getAttribute<std::string>(op, "name").value_or(std::string("display"));
                    *stream << "    if (" << stableTruthyLogicValueExpr(graph, model, operands[0]) << ") {\n";
                    if (argEnd <= 1)
                    {
                        *stream << "        execute_system_task(\"" << escapeCppString(name) << "\", {});\n";
                    }
                    else
                    {
                        *stream << "        execute_system_task(\"" << escapeCppString(name) << "\", {";
                        for (std::size_t i = 1; i < argEnd; ++i)
                        {
                            if (i != 1)
                            {
                                *stream << ", ";
                            }
                            *stream << stableSystemTaskArgExpr(graph, model, operands[i]);
                        }
                        *stream << "});\n";
                    }
                    *stream << "    }\n";
                }
                *stream << "    flush_deferred_system_task_texts();\n";
                *stream << "    for (auto &[handle, entry] : file_handles_) {\n";
                *stream << "        (void)handle;\n";
                *stream << "        if (entry.canWrite) {\n";
                *stream << "            entry.stream.flush();\n";
                *stream << "        }\n";
                *stream << "        entry.stream.close();\n";
                *stream << "    }\n";
                *stream << "    file_handles_.clear();\n";
                *stream << "}\n\n";
                *stream << "[[noreturn]] void " << className << "::terminate_host_process(int exitCode)\n{\n";
                *stream << "    finalize();\n";
                *stream << "    std::cout.flush();\n";
                *stream << "    std::cerr.flush();\n";
                *stream << "    std::exit(exitCode);\n";
                *stream << "}\n\n";
            }
            auto emitCommitStateShadowBody = [&](LimitedOutputStream &stream,
                                                const StateShadowDecl &shadow,
                                                std::string_view indent)
            {
                const StateDecl &state = model.stateBySymbol.at(shadow.symbol);
                const std::string shadowDataRef = stateShadowDataRef(shadow, state);
                const std::string shadowTouchedRef = stateShadowTouchedRef(shadow);
                const std::string stateExpr = stateRef(state);
                stream << indent << "bool state_changed = false;\n";
                if (isWideLogicWidth(state.width))
                {
                    stream << indent << "if (grhsim_assign_words(" << stateExpr << ", " << shadowDataRef
                           << ", " << state.width << ")) {\n";
                    stream << indent << "    state_changed = true;\n";
                    stream << indent << "}\n";
                }
                else
                {
                    stream << indent << "if (" << stateExpr << " != " << shadowDataRef << ") {\n";
                    stream << indent << "    " << stateExpr << " = " << shadowDataRef << ";\n";
                    stream << indent << "    state_changed = true;\n";
                    stream << indent << "}\n";
                }
                stream << indent << "if (state_changed) {\n";
                const auto headIt = model.stateHeadSupernodesBySymbol.find(shadow.symbol);
                if (headIt != model.stateHeadSupernodesBySymbol.end() && !headIt->second.empty())
                {
                    emitActivationStatements(
                        stream,
                        "supernode_active_curr_",
                        "active_count_",
                        headIt->second,
                        std::string(indent) + "    ");
                }
                stream << indent << "}\n";
                stream << indent << shadowTouchedRef << " = 0;\n";
            };
            auto emitCommitWriteBody = [&](LimitedOutputStream &stream, const WriteDecl &write, std::string_view indent)
            {
                const StateDecl &state = model.stateBySymbol.find(write.symbol)->second;
                const std::string writeTouchedRef = memoryWriteTouchedRef(write);
                const std::string writeAddrRef = memoryWriteAddrRef(write);
                const std::string writeDataRef = memoryWriteDataRef(write, state);
                const std::string writeMaskRef = memoryWriteMaskRef(write, state);
                const std::string stateExpr = stateRef(state);
                stream << indent << "bool state_changed = false;\n";
                if (state.kind == StateDecl::Kind::Memory)
                {
                    stream << indent << "const std::size_t row = " << writeAddrRef << ";\n";
                    stream << indent << "if (row < " << state.rowCount << ") {\n";
                    if (write.memoryMaskMode == WriteDecl::MemoryMaskMode::kConstAllOnes)
                    {
                        if (isWideLogicWidth(state.width))
                        {
                            stream << indent << "    if (grhsim_assign_words(" << stateExpr << "[row], "
                                   << writeDataRef << ", " << state.width << ")) {\n";
                            stream << indent << "        state_changed = true;\n";
                            stream << indent << "    }\n";
                        }
                        else
                        {
                            stream << indent << "    const auto next_value = static_cast<" << logicCppType(state.width)
                                   << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << writeDataRef
                                   << "), " << state.width << "));\n";
                            stream << indent << "    if (" << stateExpr << "[row] != next_value) {\n";
                            stream << indent << "        " << stateExpr << "[row] = next_value;\n";
                            stream << indent << "        state_changed = true;\n";
                            stream << indent << "    }\n";
                        }
                    }
                    else
                    {
                        if (isWideLogicWidth(state.width))
                        {
                            stream << indent << "    if (grhsim_apply_masked_words_inplace(" << stateExpr << "[row], "
                                   << writeDataRef << ", " << writeMaskRef << ", " << state.width
                                   << ")) {\n";
                            stream << indent << "        state_changed = true;\n";
                            stream << indent << "    }\n";
                        }
                        else
                        {
                            stream << indent << "    const auto mask = static_cast<" << logicCppType(state.width)
                                   << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << writeMaskRef
                                   << "), " << state.width << "));\n";
                            stream << indent << "    const auto merged = static_cast<" << logicCppType(state.width)
                                   << ">((" << stateExpr << "[row] & ~mask) | (static_cast<"
                                   << logicCppType(state.width)
                                   << ">(grhsim_trunc_u64(static_cast<std::uint64_t>(" << writeDataRef
                                   << "), " << state.width << ")) & mask));\n";
                            stream << indent << "    if (" << stateExpr << "[row] != merged) {\n";
                            stream << indent << "        " << stateExpr << "[row] = merged;\n";
                            stream << indent << "        state_changed = true;\n";
                            stream << indent << "    }\n";
                        }
                    }
                    stream << indent << "}\n";
                }
                stream << indent << "if (state_changed) {\n";
                const auto headIt = model.stateHeadSupernodesBySymbol.find(write.symbol);
                if (headIt != model.stateHeadSupernodesBySymbol.end() && !headIt->second.empty())
                {
                    emitActivationStatements(
                        stream,
                        "supernode_active_curr_",
                        "active_count_",
                        headIt->second,
                        std::string(indent) + "    ");
                }
                stream << indent << "}\n";
                stream << indent << writeTouchedRef << " = 0;\n";
            };
            *stream << "bool " << className << "::commit_state_updates()\n{\n";
            *stream << "    // Apply touched next-state shadows after the scheduled combinational phase completes.\n";
            *stream << "    bool activatedReaders = false;\n";
            if (!model.stateShadows.empty())
            {
                *stream << "    for (std::size_t touchedIndex = 0; touchedIndex < touched_state_shadow_count_; ++touchedIndex) {\n";
                *stream << "        const std::uint32_t shadowIndex = touched_state_shadow_indices_[touchedIndex];\n";
                *stream << "        touched_state_shadow_flags_[shadowIndex] = 0;\n";
                for (std::size_t chunkIndex = 0; chunkIndex < stateShadowCommitChunks.size(); ++chunkIndex)
                {
                    const auto &chunk = stateShadowCommitChunks[chunkIndex];
                    *stream << (chunkIndex == 0 ? "        if " : "        else if ") << "(shadowIndex <= UINT32_C("
                            << chunk.maxDispatchIndex << ")) {\n";
                    *stream << "            " << chunk.methodName << "(shadowIndex, activatedReaders);\n";
                    *stream << "        }\n";
                }
                *stream << "    }\n";
                *stream << "    touched_state_shadow_count_ = 0;\n";
            }
            if (!model.writes.empty())
            {
                *stream << "    for (std::size_t touchedIndex = 0; touchedIndex < touched_write_count_; ++touchedIndex) {\n";
                *stream << "        const std::uint32_t writeIndex = touched_write_indices_[touchedIndex];\n";
                *stream << "        touched_write_flags_[writeIndex] = 0;\n";
                for (std::size_t chunkIndex = 0; chunkIndex < writeCommitChunks.size(); ++chunkIndex)
                {
                    const auto &chunk = writeCommitChunks[chunkIndex];
                    *stream << (chunkIndex == 0 ? "        if " : "        else if ") << "(writeIndex <= UINT32_C("
                            << chunk.maxDispatchIndex << ")) {\n";
                    *stream << "            " << chunk.methodName << "(writeIndex, activatedReaders);\n";
                    *stream << "        }\n";
                }
                *stream << "    }\n";
                *stream << "    touched_write_count_ = 0;\n";
            }
            *stream << "    return activatedReaders;\n";
            *stream << "}\n\n";
            *stream << "void " << className << "::refresh_outputs()\n{\n";
            *stream << "    // Publish the latest value fields to the public outputs.\n";
            for (const auto &port : graph.outputPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                *stream << "    " << name << " = " << valueRef(model, port.value) << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                const std::string name = sanitizeIdentifier(port.name);
                *stream << "    " << name << ".out = " << valueRef(model, port.out) << ";\n";
                *stream << "    " << name << ".oe = " << valueRef(model, port.oe) << ";\n";
            }
            *stream << "}\n";
            if (auto error = finalizeOutputFile(*stream, statePath))
            {
                reportError(*error, statePath.string());
                result.success = false;
                return result;
            }
        }

        for (std::size_t chunkIndex = 0; chunkIndex < initChunks.size(); ++chunkIndex)
        {
            const auto &chunk = initChunks[chunkIndex];
            const std::filesystem::path &chunkPath = stateInitPaths[chunkIndex];
            if (auto error = ensureOutputDirectory(chunkPath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(chunkPath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n";
            *stream << "#include <chrono>\n\n";

            if (chunk.kind == InitChunkSpec::Kind::kStates)
            {
                bool emittedInitNamespace = false;
                for (const std::string &stateSymbol : chunk.stateSymbols)
                {
                    const StateDecl &state = model.stateBySymbol.at(stateSymbol);
                    if (state.kind != StateDecl::Kind::Memory || state.memoryInitRowExprs.empty())
                    {
                        continue;
                    }
                    std::vector<std::size_t> initRows;
                    initRows.reserve(state.memoryInitRowExprs.size());
                    for (std::size_t row = 0; row < state.memoryInitRowExprs.size(); ++row)
                    {
                        if (state.memoryInitRowExprs[row].has_value() && !state.memoryInitRowExprs[row]->requiresRuntime)
                        {
                            initRows.push_back(row);
                        }
                    }
                    if (initRows.empty())
                    {
                        continue;
                    }
                    if (!emittedInitNamespace)
                    {
                        *stream << "namespace {\n";
                        emittedInitNamespace = true;
                    }
                    const std::string base = "k_mem_init_" + sanitizeIdentifier(stateSymbol);
                    *stream << "constexpr std::size_t " << base << "_rows[] = {";
                    for (std::size_t i = 0; i < initRows.size(); ++i)
                    {
                        if (i != 0)
                        {
                            *stream << ", ";
                        }
                        *stream << initRows[i];
                    }
                    *stream << "};\n";
                    *stream << "constexpr " << logicCppType(state.width) << " " << base << "_data[] = {\n";
                    for (std::size_t row : initRows)
                    {
                        *stream << "    " << state.memoryInitRowExprs[row]->expr << ",\n";
                    }
                    *stream << "};\n\n";
                }
                if (emittedInitNamespace)
                {
                    *stream << "} // namespace\n\n";
                }
            }

            *stream << "void " << className << "::" << chunk.methodName << "()\n{\n";
            switch (chunk.kind)
            {
            case InitChunkSpec::Kind::kPublicInputs:
                *stream << "    // Initialize public input ports and inout baselines.\n";
                for (const auto &port : graph.inputPorts())
                {
                    *stream << "    " << sanitizeIdentifier(port.name) << " = " << defaultInitExpr(graph, port.value)
                            << ";\n";
                }
                for (const auto &port : graph.inoutPorts())
                {
                    const std::string name = sanitizeIdentifier(port.name);
                    *stream << "    " << name << ".in = " << defaultInitExpr(graph, port.in) << ";\n";
                    *stream << "    " << name << ".out = " << defaultInitExpr(graph, port.out) << ";\n";
                    *stream << "    " << name << ".oe = " << defaultInitExpr(graph, port.oe) << ";\n";
                }
                break;
            case InitChunkSpec::Kind::kPublicOutputs:
                *stream << "    // Initialize public outputs.\n";
                for (const auto &port : graph.outputPorts())
                {
                    *stream << "    " << sanitizeIdentifier(port.name) << " = " << defaultInitExpr(graph, port.value)
                            << ";\n";
                }
                break;
            case InitChunkSpec::Kind::kValues:
                *stream << "    // Initialize combinational value storage.\n";
                for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
                {
                    const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                    const std::size_t slotCount = model.valueScalarSlotCounts[kindIndex];
                    if (slotCount == 0)
                    {
                        continue;
                    }
                    *stream << "    " << valueScalarSlotFieldName(kind) << ".assign(" << slotCount << ", "
                            << scalarLogicSlotCppType(kind)
                            << "{});\n";
                }
                for (const auto &[wordCount, slotCount] : model.valueWideSlotCountsByWords)
                {
                    if (slotCount == 0)
                    {
                        continue;
                    }
                    *stream << "    " << valueWideSlotFieldName(wordCount) << ".assign(" << slotCount
                            << ", std::array<std::uint64_t, " << wordCount << ">{});\n";
                }
                if (model.valueRealSlotCount != 0)
                {
                    *stream << "    value_real_slots_.assign(" << model.valueRealSlotCount << ", 0.0);\n";
                }
                if (model.valueStringSlotCount != 0)
                {
                    *stream << "    value_string_slots_.assign(" << model.valueStringSlotCount << ", std::string{});\n";
                }
                break;
            case InitChunkSpec::Kind::kConstantValues:
                if (!chunk.values.empty())
                {
                    *stream << "    // Seed materialized constants once; they do not need runtime change detection.\n";
                }
                for (ValueId valueId : chunk.values)
                {
                    std::string initError;
                    if (!emitMaterializedConstantInit(*stream, graph, model, valueId, "    ", initError))
                    {
                        reportError(initError, std::string(graph.symbolText(graph.valueSymbol(valueId))));
                        result.success = false;
                        return result;
                    }
                }
                break;
            case InitChunkSpec::Kind::kRandomSeed:
                *stream << "    random_state_ = random_seed_;\n";
                break;
            case InitChunkSpec::Kind::kStateStorage:
                *stream << "    // Allocate persistent state pools.\n";
                for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
                {
                    const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                    const std::size_t slotCount = model.stateLogicScalarSlotCounts[kindIndex];
                    if (slotCount != 0)
                    {
                        *stream << "    " << stateLogicScalarSlotFieldName(kind) << ".assign(" << slotCount << ", "
                                << scalarLogicSlotCppType(kind) << "{});\n";
                    }
                    const std::size_t memCount = model.stateMemoryScalarSlotCounts[kindIndex];
                    if (memCount != 0)
                    {
                        *stream << "    " << stateMemoryScalarSlotFieldName(kind) << ".assign(" << memCount
                                << ", std::vector<" << scalarLogicSlotCppType(kind) << ">{});\n";
                    }
                }
                for (const auto &[wordCount, slotCount] : model.stateLogicWideSlotCountsByWords)
                {
                    if (slotCount != 0)
                    {
                        *stream << "    " << stateLogicWideSlotFieldName(wordCount) << ".assign(" << slotCount
                                << ", std::array<std::uint64_t, " << wordCount << ">{});\n";
                    }
                }
                for (const auto &[wordCount, slotCount] : model.stateMemoryWideSlotCountsByWords)
                {
                    if (slotCount != 0)
                    {
                        *stream << "    " << stateMemoryWideSlotFieldName(wordCount) << ".assign(" << slotCount
                                << ", std::vector<std::array<std::uint64_t, " << wordCount << ">>{});\n";
                    }
                }
                break;
            case InitChunkSpec::Kind::kStates:
                if (!chunk.stateSymbols.empty())
                {
                    *stream << "    // Initialize persistent state objects.\n";
                }
                for (const std::string &stateSymbol : chunk.stateSymbols)
                {
                    const StateDecl &state = model.stateBySymbol.at(stateSymbol);
                    const std::string stateExpr = stateRef(state);
                    if (state.kind == StateDecl::Kind::Memory)
                    {
                        *stream << "    " << stateExpr << ".assign(" << state.rowCount << ", "
                                << logicCppType(state.width) << "{});\n";
                        std::size_t initCount = 0;
                        for (const auto &rowExpr : state.memoryInitRowExprs)
                        {
                            if (rowExpr.has_value() && !rowExpr->requiresRuntime)
                            {
                                ++initCount;
                            }
                        }
                        if (initCount != 0)
                        {
                            const std::string base = "k_mem_init_" + sanitizeIdentifier(stateSymbol);
                            *stream << "    for (std::size_t i = 0; i < " << initCount << "; ++i) {\n";
                            *stream << "        " << stateExpr << "[" << base << "_rows[i]] = " << base
                                    << "_data[i];\n";
                            *stream << "    }\n";
                        }
                        for (std::size_t row = 0; row < state.memoryInitRowExprs.size(); ++row)
                        {
                            if (!state.memoryInitRowExprs[row].has_value() || !state.memoryInitRowExprs[row]->requiresRuntime)
                            {
                                continue;
                            }
                            *stream << "    " << stateExpr << "[" << row << "] = "
                                    << state.memoryInitRowExprs[row]->expr << ";\n";
                        }
                    }
                    else if (state.initExpr)
                    {
                        *stream << "    " << stateExpr << " = " << state.initExpr->expr << ";\n";
                    }
                }
                break;
            case InitChunkSpec::Kind::kStateShadows:
                *stream << "    // Clear register/latch next-state shadows.\n";
                if (chunk.indices.empty())
                {
                    break;
                }
                *stream << "    state_shadow_touched_slots_.assign(" << model.stateShadowTouchedCount << ", 0);\n";
                for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
                {
                    const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                    const std::size_t slotCount = model.stateShadowScalarSlotCounts[kindIndex];
                    if (slotCount == 0)
                    {
                        continue;
                    }
                    *stream << "    " << scalarLogicSlotFieldName("state_shadow_", kind) << ".assign(" << slotCount
                            << ", " << scalarLogicSlotCppType(kind) << "{});\n";
                }
                for (const auto &[wordCount, slotCount] : model.stateShadowWideSlotCountsByWords)
                {
                    if (slotCount == 0)
                    {
                        continue;
                    }
                    *stream << "    " << wideLogicSlotFieldName("state_shadow_", wordCount) << ".assign(" << slotCount
                            << ", std::array<std::uint64_t, " << wordCount << ">{});\n";
                }
                break;
            case InitChunkSpec::Kind::kWrites:
                *stream << "    // Clear memory next-state shadows.\n";
                if (chunk.indices.empty())
                {
                    break;
                }
                *stream << "    memory_write_touched_slots_.assign(" << model.memoryWriteTouchedCount << ", 0);\n";
                *stream << "    memory_write_addr_slots_.assign(" << model.memoryWriteAddrCount << ", 0);\n";
                for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(ValueSlotScalarKind::kCount); ++kindIndex)
                {
                    const auto kind = static_cast<ValueSlotScalarKind>(kindIndex);
                    const std::size_t dataCount = model.memoryWriteDataScalarSlotCounts[kindIndex];
                    if (dataCount != 0)
                    {
                        *stream << "    " << scalarLogicSlotFieldName("memory_write_data_", kind) << ".assign(" << dataCount
                                << ", " << scalarLogicSlotCppType(kind) << "{});\n";
                    }
                    const std::size_t maskCount = model.memoryWriteMaskScalarSlotCounts[kindIndex];
                    if (maskCount != 0)
                    {
                        *stream << "    " << scalarLogicSlotFieldName("memory_write_mask_", kind) << ".assign(" << maskCount
                                << ", " << scalarLogicSlotCppType(kind) << "{});\n";
                    }
                }
                for (const auto &[wordCount, slotCount] : model.memoryWriteDataWideSlotCountsByWords)
                {
                    if (slotCount == 0)
                    {
                        continue;
                    }
                    *stream << "    " << wideLogicSlotFieldName("memory_write_data_", wordCount) << ".assign(" << slotCount
                            << ", std::array<std::uint64_t, " << wordCount << ">{});\n";
                }
                for (const auto &[wordCount, slotCount] : model.memoryWriteMaskWideSlotCountsByWords)
                {
                    if (slotCount == 0)
                    {
                        continue;
                    }
                    *stream << "    " << wideLogicSlotFieldName("memory_write_mask_", wordCount) << ".assign(" << slotCount
                            << ", std::array<std::uint64_t, " << wordCount << ">{});\n";
                }
                break;
            case InitChunkSpec::Kind::kPrevInputs:
                *stream << "    // Establish previous-cycle baselines used by input change and event-edge detection.\n";
                for (ValueId valueId : chunk.values)
                {
                    bool emitted = false;
                    for (const auto &port : graph.inputPorts())
                    {
                        if (port.value != valueId)
                        {
                            continue;
                        }
                        *stream << "    " << model.prevInputFieldByValue.at(valueId) << " = "
                                << sanitizeIdentifier(port.name) << ";\n";
                        emitted = true;
                        break;
                    }
                    if (emitted)
                    {
                        continue;
                    }
                    for (const auto &port : graph.inoutPorts())
                    {
                        if (port.in != valueId)
                        {
                            continue;
                        }
                        *stream << "    " << model.prevInputFieldByValue.at(valueId) << " = "
                                << sanitizeIdentifier(port.name) << ".in;\n";
                        break;
                    }
                }
                break;
            case InitChunkSpec::Kind::kEventEdges:
                *stream << "    // Clear shared event-edge slots.\n";
                if (model.eventEdgeSlotCount != 0)
                {
                    *stream << "    event_edge_slots_.assign(" << model.eventEdgeSlotCount
                            << ", grhsim_event_edge_kind::none);\n";
                }
                break;
            case InitChunkSpec::Kind::kEvalState:
                *stream << "    // Reset per-eval scheduling state.\n";
                *stream << "    supernode_active_curr_.fill(0);\n";
                if (!model.stateShadows.empty())
                {
                    *stream << "    touched_state_shadow_indices_.fill(0);\n";
                    *stream << "    touched_state_shadow_flags_.fill(0);\n";
                    *stream << "    touched_state_shadow_count_ = 0;\n";
                }
                if (!model.writes.empty())
                {
                    *stream << "    touched_write_indices_.fill(0);\n";
                    *stream << "    touched_write_flags_.fill(0);\n";
                    *stream << "    touched_write_count_ = 0;\n";
                }
                *stream << "    first_eval_ = true;\n";
                *stream << "    register_write_conflict_ = false;\n";
                break;
            case InitChunkSpec::Kind::kRuntimeState:
                *stream << "    finalized_ = false;\n";
                *stream << "    finish_requested_ = false;\n";
                *stream << "    stop_requested_ = false;\n";
                *stream << "    fatal_requested_ = false;\n";
                *stream << "    system_exit_code_ = 0;\n";
                *stream << "    dumpfile_path_.clear();\n";
                *stream << "    dumpvars_enabled_ = false;\n";
                *stream << "    next_file_handle_ = UINT64_C(3);\n";
                *stream << "    file_handles_.clear();\n";
                *stream << "    deferred_system_task_texts_.clear();\n";
                break;
            }
            *stream << "}\n";

            if (auto error = finalizeOutputFile(*stream, chunkPath))
            {
                reportError(*error, chunkPath.string());
                result.success = false;
                return result;
            }
        }

        std::size_t stateCommitPathIndex = 0;
        for (const auto &chunk : stateShadowCommitChunks)
        {
            const std::filesystem::path &chunkPath = stateCommitPaths[stateCommitPathIndex++];
            if (auto error = ensureOutputDirectory(chunkPath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(chunkPath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            *stream << "void " << className << "::" << chunk.methodName << "(std::uint32_t shadowIndex, bool &activatedReaders)\n{\n";
            *stream << "    switch (shadowIndex) {\n";
            for (std::size_t shadowIndex : chunk.indices)
            {
                const auto &shadow = model.stateShadows[shadowIndex];
                *stream << "    case " << shadow.emitIndex << "u:\n";
                *stream << "        if (" << stateShadowTouchedRef(shadow) << ") {\n";
                emitCommitStateShadowBody(*stream, shadow, "            ");
                *stream << "        }\n";
                *stream << "        break;\n";
            }
            *stream << "    default:\n";
            *stream << "        break;\n";
            *stream << "    }\n";
            *stream << "}\n";
            if (auto error = finalizeOutputFile(*stream, chunkPath))
            {
                reportError(*error, chunkPath.string());
                result.success = false;
                return result;
            }
        }

        for (const auto &chunk : writeCommitChunks)
        {
            const std::filesystem::path &chunkPath = stateCommitPaths[stateCommitPathIndex++];
            if (auto error = ensureOutputDirectory(chunkPath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(chunkPath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n\n";
            *stream << "void " << className << "::" << chunk.methodName << "(std::uint32_t writeIndex, bool &activatedReaders)\n{\n";
            *stream << "    switch (writeIndex) {\n";
            for (std::size_t writeIndex : chunk.indices)
            {
                const auto &write = model.writes[writeIndex];
                *stream << "    case " << write.shadowIndex << "u:\n";
                *stream << "        if (" << memoryWriteTouchedRef(write) << ") {\n";
                emitCommitWriteBody(*stream, write, "            ");
                *stream << "        }\n";
                *stream << "        break;\n";
            }
            *stream << "    default:\n";
            *stream << "        break;\n";
            *stream << "    }\n";
            *stream << "}\n";
            if (auto error = finalizeOutputFile(*stream, chunkPath))
            {
                reportError(*error, chunkPath.string());
                result.success = false;
                return result;
            }
        }

        {
            if (auto error = ensureOutputDirectory(evalPath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(evalPath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            const std::size_t activeFlagWordCount =
                (schedule.supernodeToOps.size() + kActiveFlagBitsPerWord - 1u) / kActiveFlagBitsPerWord;
            std::vector<std::vector<std::size_t>> batchIndicesByActiveWord(activeFlagWordCount);
            for (const auto &batch : scheduleBatches)
            {
                if (batch.activeFlagWordIndex < batchIndicesByActiveWord.size())
                {
                    batchIndicesByActiveWord[batch.activeFlagWordIndex].push_back(batch.index);
                }
            }
            std::vector<std::size_t> activeWordBatchOffsets;
            activeWordBatchOffsets.reserve(activeFlagWordCount + 1u);
            activeWordBatchOffsets.push_back(0u);
            std::vector<std::size_t> activeWordBatchIndices;
            activeWordBatchIndices.reserve(scheduleBatches.size());
            for (const auto &batchIndices : batchIndicesByActiveWord)
            {
                activeWordBatchIndices.insert(
                    activeWordBatchIndices.end(), batchIndices.begin(), batchIndices.end());
                activeWordBatchOffsets.push_back(activeWordBatchIndices.size());
            }
            *stream << "#include \"" << headerPath.filename().string() << "\"\n";
            if (model.emitPerf)
            {
                *stream << "#include <chrono>\n";
            }
            *stream << "\n";
            *stream << "void " << className << "::eval()\n{\n";
            *stream << "    using BatchEvalFn = void (" << className << "::*)(std::uint8_t &activeWordFlags);\n";
            *stream << "    static constexpr BatchEvalFn kBatchEvalFns[] = {\n";
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                *stream << "        &" << className << "::eval_batch_" << batchIndex;
                if (batchIndex + 1 != scheduleBatches.size())
                {
                    *stream << ",";
                }
                *stream << "\n";
            }
            *stream << "    };\n";
            *stream << "    static constexpr std::size_t kActiveWordBatchOffsets[] = {\n";
            for (std::size_t offsetIndex = 0; offsetIndex < activeWordBatchOffsets.size(); ++offsetIndex)
            {
                *stream << "        " << activeWordBatchOffsets[offsetIndex] << "u";
                if (offsetIndex + 1 != activeWordBatchOffsets.size())
                {
                    *stream << ",";
                }
                *stream << "\n";
            }
            *stream << "    };\n";
            *stream << "    static constexpr std::size_t kActiveWordBatchIndices[] = {\n";
            for (std::size_t entryIndex = 0; entryIndex < activeWordBatchIndices.size(); ++entryIndex)
            {
                *stream << "        " << activeWordBatchIndices[entryIndex] << "u";
                if (entryIndex + 1 != activeWordBatchIndices.size())
                {
                    *stream << ",";
                }
                *stream << "\n";
            }
            *stream << "    };\n";
            *stream << "    static constexpr std::uint16_t kSupernodeOpCounts[] = {";
            for (std::size_t topoIndex = 0; topoIndex < schedule.topoOrder.size(); ++topoIndex)
            {
                if ((topoIndex % 16u) == 0u)
                {
                    *stream << "\n        ";
                }
                const uint32_t supernodeId = schedule.topoOrder[topoIndex];
                const std::size_t opCount =
                    supernodeId < schedule.supernodeToOps.size() ? schedule.supernodeToOps[supernodeId].size() : 0;
                *stream << "UINT16_C(" << opCount << ")";
                if (topoIndex + 1u != schedule.topoOrder.size())
                {
                    *stream << ", ";
                }
            }
            if (!schedule.topoOrder.empty())
            {
                *stream << "\n";
            }
            *stream << "    };\n";
            *stream << "    // Seed this eval from first-eval full activation and changed external inputs.\n";
            *stream << "    const bool initial_eval = first_eval_;\n";
            if (model.emitPerf || model.emitWaveform)
            {
                *stream << "    const std::uint64_t eval_id = ++eval_invocation_count_;\n";
            }
            if (model.emitPerf)
            {
                *stream << "    std::uint64_t fixed_point_round_count = UINT64_C(0);\n";
            }
            *stream << "    bool pending_eval_round = initial_eval;\n";
            *stream << "    register_write_conflict_ = false;\n";
            *stream << "    if (initial_eval) {\n";
                *stream << "        supernode_active_curr_.fill(UINT8_C(0xFF));\n";
                if ((schedule.supernodeToOps.size() % kActiveFlagBitsPerWord) != 0u)
                {
                    const std::uint8_t lastMask = static_cast<std::uint8_t>(
                        (UINT8_C(1) << (schedule.supernodeToOps.size() % kActiveFlagBitsPerWord)) - UINT8_C(1));
                    *stream << "        supernode_active_curr_[kActiveFlagWordCount - 1] = UINT8_C("
                            << static_cast<unsigned>(lastMask) << ");\n";
                }
            *stream << "    }\n";
            std::map<std::vector<uint32_t>, std::vector<std::string>> inputSeedGroups;
            for (const auto &port : graph.inputPorts())
            {
                const auto it = model.inputHeadSupernodesByValue.find(port.value);
                if (it == model.inputHeadSupernodesByValue.end())
                {
                    continue;
                }
                inputSeedGroups[it->second].push_back("(" + model.inputFieldByValue.at(port.value) + " != " +
                                                      model.prevInputFieldByValue.at(port.value) + ")");
            }
            for (const auto &port : graph.inoutPorts())
            {
                const auto it = model.inputHeadSupernodesByValue.find(port.in);
                if (it == model.inputHeadSupernodesByValue.end())
                {
                    continue;
                }
                inputSeedGroups[it->second].push_back("(" + model.inputFieldByValue.at(port.in) + " != " +
                                                      model.prevInputFieldByValue.at(port.in) + ")");
            }
            for (const auto &[supernodes, conditions] : inputSeedGroups)
            {
                *stream << "    if (!initial_eval && (" << joinStrings(conditions, " || ") << ")) {\n";
                emitActivationStatements(
                    *stream,
                    "supernode_active_curr_",
                    "active_count_",
                    supernodes,
                    "        ");
                *stream << "        pending_eval_round = true;\n";
                *stream << "    }\n";
            }
            if (!model.inputEventValues.empty())
            {
                *stream << "\n";
                *stream << "    // Update shared event edges for direct input event values.\n";
                for (ValueId value : model.inputEventValues)
                {
                    *stream << "    " << model.eventEdgeFieldByValue.at(value) << " = grhsim_classify_edge("
                            << model.prevInputFieldByValue.at(value) << ", " << model.inputFieldByValue.at(value) << ");\n";
                }
            }
            *stream << '\n';
            *stream << "    if (activity_profile_enabled_) {\n";
            *stream << "        activity_profile_current_executed_supernodes_ = UINT64_C(0);\n";
            *stream << "        activity_profile_current_executed_ops_ = UINT64_C(0);\n";
            *stream << "        activity_profile_current_peak_active_supernodes_ =\n";
            *stream << "            static_cast<std::uint64_t>(grhsim_count_active_supernodes(supernode_active_curr_));\n";
            *stream << "    }\n";
            *stream << '\n';
            if (model.emitPerf)
            {
                *stream << "    const bool trace_this_eval = trace_eval_enabled_ && (trace_eval_interval_ != 0) &&\n";
                *stream << "                                  ((eval_id % trace_eval_interval_) == 0);\n";
                *stream << "    const auto trace_eval_begin_time =\n";
                *stream << "        trace_this_eval ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};\n";
                *stream << "    std::size_t total_checked_batches = 0;\n";
                *stream << "    std::size_t total_skipped_batches = 0;\n";
                *stream << "    std::size_t total_executed_batches = 0;\n";
                *stream << "    std::size_t total_executed_supernodes = 0;\n";
                *stream << "    std::size_t total_checked_flag_words = 0;\n";
                *stream << "    std::size_t total_nonzero_active_words = 0;\n";
                *stream << "    std::size_t peak_active_supernodes = trace_this_eval ? grhsim_count_active_supernodes(supernode_active_curr_) : 0;\n";
                *stream << "    std::size_t peak_active_words = trace_this_eval ? grhsim_count_nonzero_active_words(supernode_active_curr_) : 0;\n";
                *stream << "    std::size_t total_touched_state_shadows = 0;\n";
                *stream << "    std::size_t total_touched_writes = 0;\n";
                *stream << "    std::size_t total_commit_activated_rounds = 0;\n";
                *stream << "    std::uint64_t total_batch_us = UINT64_C(0);\n";
                *stream << "    std::uint64_t total_commit_us = UINT64_C(0);\n";
                *stream << "    std::uint64_t total_event_clear_us = UINT64_C(0);\n";
                *stream << "    if (trace_this_eval) {\n";
                *stream << "        std::fprintf(stderr,\n";
                *stream << "                     \"[grhsim] eval begin #%llu initial=%d seeded_active_supernodes=%zu seeded_active_words=%zu\\n\",\n";
                *stream << "                     static_cast<unsigned long long>(eval_id),\n";
                *stream << "                     initial_eval ? 1 : 0,\n";
                *stream << "                     peak_active_supernodes,\n";
                *stream << "                     peak_active_words);\n";
                *stream << "        std::fflush(stderr);\n";
                *stream << "    }\n";
                *stream << "    while (pending_eval_round) {\n";
                *stream << "        ++fixed_point_round_count;\n";
                *stream << "        pending_eval_round = false;\n";
                *stream << "        if (activity_profile_enabled_) {\n";
                *stream << "            const auto round_active_in_profile =\n";
                *stream << "                static_cast<std::uint64_t>(grhsim_count_active_supernodes(supernode_active_curr_));\n";
                *stream << "            if (activity_profile_current_peak_active_supernodes_ < round_active_in_profile) {\n";
                *stream << "                activity_profile_current_peak_active_supernodes_ = round_active_in_profile;\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "        const auto round_begin_time =\n";
                *stream << "            trace_this_eval ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};\n";
                *stream << "        const std::size_t round_active_in =\n";
                *stream << "            trace_this_eval ? grhsim_count_active_supernodes(supernode_active_curr_) : 0;\n";
                *stream << "        const std::size_t round_active_words_in =\n";
                *stream << "            trace_this_eval ? grhsim_count_nonzero_active_words(supernode_active_curr_) : 0;\n";
                *stream << "        std::size_t round_checked_batches = 0;\n";
                *stream << "        std::size_t round_skipped_batches = 0;\n";
                *stream << "        std::size_t round_executed_batches = 0;\n";
                *stream << "        std::size_t round_executed_supernodes = 0;\n";
                *stream << "        std::size_t round_checked_flag_words = 0;\n";
                *stream << "        std::size_t round_nonzero_active_words = 0;\n";
                *stream << "        std::size_t round_touched_state_shadows = 0;\n";
                *stream << "        std::size_t round_touched_writes = 0;\n";
                *stream << "        std::uint64_t round_batch_us = UINT64_C(0);\n";
                *stream << "        std::uint64_t round_commit_us = UINT64_C(0);\n";
                *stream << "        std::uint64_t round_event_clear_us = UINT64_C(0);\n";
                *stream << "        if (peak_active_supernodes < round_active_in) {\n";
                *stream << "            peak_active_supernodes = round_active_in;\n";
                *stream << "        }\n";
                *stream << "        if (peak_active_words < round_active_words_in) {\n";
                *stream << "            peak_active_words = round_active_words_in;\n";
                *stream << "        }\n";
                *stream << "        // Propagate current activity by scanning the active-flag words directly.\n";
                *stream << "        for (std::size_t activeWordIndex = 0; activeWordIndex < kActiveFlagWordCount; ++activeWordIndex) {\n";
                *stream << "            ++round_checked_batches;\n";
                *stream << "            ++round_checked_flag_words;\n";
                *stream << "            std::uint8_t activeWordFlags = supernode_active_curr_[activeWordIndex];\n";
                *stream << "            if (activeWordFlags == UINT8_C(0)) {\n";
                *stream << "                ++round_skipped_batches;\n";
                *stream << "                continue;\n";
                *stream << "            }\n";
                *stream << "            if (activity_profile_enabled_) {\n";
                *stream << "                const auto baseActiveId = activeWordIndex * kActiveFlagBitsPerWord;\n";
                *stream << "                std::uint8_t profileActiveFlags = activeWordFlags;\n";
                *stream << "                activity_profile_current_executed_supernodes_ +=\n";
                *stream << "                    static_cast<std::uint64_t>(grhsim_popcount_u8(profileActiveFlags));\n";
                *stream << "                while (profileActiveFlags != UINT8_C(0)) {\n";
                *stream << "                    const auto bitIndex =\n";
                *stream << "                        static_cast<std::size_t>(__builtin_ctz(static_cast<unsigned>(profileActiveFlags)));\n";
                *stream << "                    activity_profile_current_executed_ops_ +=\n";
                *stream << "                        static_cast<std::uint64_t>(kSupernodeOpCounts[baseActiveId + bitIndex]);\n";
                *stream << "                    profileActiveFlags = static_cast<std::uint8_t>(\n";
                *stream << "                        profileActiveFlags & static_cast<std::uint8_t>(profileActiveFlags - UINT8_C(1)));\n";
                *stream << "                }\n";
                *stream << "            }\n";
                *stream << "            supernode_active_curr_[activeWordIndex] = UINT8_C(0);\n";
                *stream << "            ++round_nonzero_active_words;\n";
                *stream << "            round_executed_supernodes +=\n";
                *stream << "                static_cast<std::size_t>(grhsim_popcount_u8(activeWordFlags));\n";
                *stream << "            for (std::size_t batchOffset = kActiveWordBatchOffsets[activeWordIndex];\n";
                *stream << "                 batchOffset < kActiveWordBatchOffsets[activeWordIndex + 1];\n";
                *stream << "                 ++batchOffset) {\n";
                *stream << "                const std::size_t batchIndex = kActiveWordBatchIndices[batchOffset];\n";
                *stream << "                ++round_executed_batches;\n";
                *stream << "                if (trace_this_eval) {\n";
                *stream << "                    const auto batch_begin_time = std::chrono::steady_clock::now();\n";
                *stream << "                    (this->*kBatchEvalFns[batchIndex])(activeWordFlags);\n";
                *stream << "                    round_batch_us += static_cast<std::uint64_t>(\n";
                *stream << "                        std::chrono::duration_cast<std::chrono::microseconds>(\n";
                *stream << "                            std::chrono::steady_clock::now() - batch_begin_time)\n";
                *stream << "                            .count());\n";
                *stream << "                } else {\n";
                *stream << "                    (this->*kBatchEvalFns[batchIndex])(activeWordFlags);\n";
                *stream << "                }\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "        // Commit deferred state updates and reactivate readers of changed state.\n";
                *stream << "        bool commit_activated_readers = false;\n";
                *stream << "        if (trace_this_eval) {\n";
                if (!model.stateShadows.empty())
                {
                    *stream << "            round_touched_state_shadows = touched_state_shadow_count_;\n";
                }
                else
                {
                    *stream << "            round_touched_state_shadows = 0;\n";
                }
                if (!model.writes.empty())
                {
                    *stream << "            round_touched_writes = touched_write_count_;\n";
                }
                else
                {
                    *stream << "            round_touched_writes = 0;\n";
                }
                *stream << "        }\n";
                *stream << "        if (trace_this_eval) {\n";
                *stream << "            const auto commit_begin_time = std::chrono::steady_clock::now();\n";
                *stream << "            commit_activated_readers = commit_state_updates();\n";
                *stream << "            round_commit_us += static_cast<std::uint64_t>(\n";
                *stream << "                std::chrono::duration_cast<std::chrono::microseconds>(\n";
                *stream << "                    std::chrono::steady_clock::now() - commit_begin_time)\n";
                *stream << "                    .count());\n";
                *stream << "        } else {\n";
                *stream << "            commit_activated_readers = commit_state_updates();\n";
                *stream << "        }\n";
                *stream << "        pending_eval_round = commit_activated_readers;\n";
                if (!model.allEventValues.empty())
                {
                    *stream << "        // Event edges are per-fixed-point-round signals, so clear them before the next round.\n";
                    *stream << "        const auto event_clear_begin_time =\n";
                    *stream << "            trace_this_eval ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};\n";
                    emitClearAllEventEdges(*stream, model, "        ");
                    *stream << "        if (trace_this_eval) {\n";
                    *stream << "            round_event_clear_us += static_cast<std::uint64_t>(\n";
                    *stream << "                std::chrono::duration_cast<std::chrono::microseconds>(\n";
                    *stream << "                    std::chrono::steady_clock::now() - event_clear_begin_time)\n";
                    *stream << "                    .count());\n";
                    *stream << "        }\n";
                }
                *stream << "        if (trace_this_eval) {\n";
                *stream << "            total_checked_batches += round_checked_batches;\n";
                *stream << "            total_skipped_batches += round_skipped_batches;\n";
                *stream << "            total_executed_batches += round_executed_batches;\n";
                *stream << "            total_executed_supernodes += round_executed_supernodes;\n";
                *stream << "            total_checked_flag_words += round_checked_flag_words;\n";
                *stream << "            total_nonzero_active_words += round_nonzero_active_words;\n";
                *stream << "            total_touched_state_shadows += round_touched_state_shadows;\n";
                *stream << "            total_touched_writes += round_touched_writes;\n";
                *stream << "            total_commit_activated_rounds += commit_activated_readers ? 1u : 0u;\n";
                *stream << "            total_batch_us += round_batch_us;\n";
                *stream << "            total_commit_us += round_commit_us;\n";
                *stream << "            total_event_clear_us += round_event_clear_us;\n";
                *stream << "            const auto round_total_us = static_cast<std::uint64_t>(\n";
                *stream << "                std::chrono::duration_cast<std::chrono::microseconds>(\n";
                *stream << "                    std::chrono::steady_clock::now() - round_begin_time)\n";
                *stream << "                    .count());\n";
                *stream << "            const std::size_t round_active_out = grhsim_count_active_supernodes(supernode_active_curr_);\n";
                *stream << "            const std::size_t round_active_words_out =\n";
                *stream << "                grhsim_count_nonzero_active_words(supernode_active_curr_);\n";
                *stream << "            if (peak_active_supernodes < round_active_out) {\n";
                *stream << "                peak_active_supernodes = round_active_out;\n";
                *stream << "            }\n";
                *stream << "            if (peak_active_words < round_active_words_out) {\n";
                *stream << "                peak_active_words = round_active_words_out;\n";
                *stream << "            }\n";
                *stream << "            std::fprintf(stderr,\n";
                *stream << "                         \"[grhsim] eval round #%llu.%llu active_in=%zu active_words_in=%zu checked_batches=%zu executed_batches=%zu executed_supernodes=%zu skipped_batches=%zu checked_flag_words=%zu nonzero_active_words=%zu touched_shadows=%zu touched_writes=%zu commit_activated=%d batch_us=%llu commit_us=%llu clear_evt_us=%llu total_us=%llu active_out=%zu active_words_out=%zu\\n\",\n";
                *stream << "                         static_cast<unsigned long long>(eval_id),\n";
                *stream << "                         static_cast<unsigned long long>(fixed_point_round_count),\n";
                *stream << "                         round_active_in,\n";
                *stream << "                         round_active_words_in,\n";
                *stream << "                         round_checked_batches,\n";
                *stream << "                         round_executed_batches,\n";
                *stream << "                         round_executed_supernodes,\n";
                *stream << "                         round_skipped_batches,\n";
                *stream << "                         round_checked_flag_words,\n";
                *stream << "                         round_nonzero_active_words,\n";
                *stream << "                         round_touched_state_shadows,\n";
                *stream << "                         round_touched_writes,\n";
                *stream << "                         commit_activated_readers ? 1 : 0,\n";
                *stream << "                         static_cast<unsigned long long>(round_batch_us),\n";
                *stream << "                         static_cast<unsigned long long>(round_commit_us),\n";
                *stream << "                         static_cast<unsigned long long>(round_event_clear_us),\n";
                *stream << "                         static_cast<unsigned long long>(round_total_us),\n";
                *stream << "                         round_active_out,\n";
                *stream << "                         round_active_words_out);\n";
                *stream << "            std::fflush(stderr);\n";
                *stream << "        }\n";
                *stream << "        if (activity_profile_enabled_) {\n";
                *stream << "            const auto round_active_out_profile =\n";
                *stream << "                static_cast<std::uint64_t>(grhsim_count_active_supernodes(supernode_active_curr_));\n";
                *stream << "            if (activity_profile_current_peak_active_supernodes_ < round_active_out_profile) {\n";
                *stream << "                activity_profile_current_peak_active_supernodes_ = round_active_out_profile;\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "    }\n";
                *stream << "    if (trace_this_eval) {\n";
                *stream << "        const auto eval_total_us = static_cast<std::uint64_t>(\n";
                *stream << "            std::chrono::duration_cast<std::chrono::microseconds>(\n";
                *stream << "                std::chrono::steady_clock::now() - trace_eval_begin_time)\n";
                *stream << "                .count());\n";
                *stream << "        std::fprintf(stderr,\n";
                *stream << "                     \"[grhsim] eval end   #%llu rounds=%llu peak_active_supernodes=%zu peak_active_words=%zu checked_batches=%zu executed_batches=%zu executed_supernodes=%zu skipped_batches=%zu checked_flag_words=%zu nonzero_active_words=%zu touched_shadows=%zu touched_writes=%zu commit_activated_rounds=%zu batch_us=%llu commit_us=%llu clear_evt_us=%llu total_us=%llu write_conflict=%d\\n\",\n";
                *stream << "                     static_cast<unsigned long long>(eval_id),\n";
                *stream << "                     static_cast<unsigned long long>(fixed_point_round_count),\n";
                *stream << "                     peak_active_supernodes,\n";
                *stream << "                     peak_active_words,\n";
                *stream << "                     total_checked_batches,\n";
                *stream << "                     total_executed_batches,\n";
                *stream << "                     total_executed_supernodes,\n";
                *stream << "                     total_skipped_batches,\n";
                *stream << "                     total_checked_flag_words,\n";
                *stream << "                     total_nonzero_active_words,\n";
                *stream << "                     total_touched_state_shadows,\n";
                *stream << "                     total_touched_writes,\n";
                *stream << "                     total_commit_activated_rounds,\n";
                *stream << "                     static_cast<unsigned long long>(total_batch_us),\n";
                *stream << "                     static_cast<unsigned long long>(total_commit_us),\n";
                *stream << "                     static_cast<unsigned long long>(total_event_clear_us),\n";
                *stream << "                     static_cast<unsigned long long>(eval_total_us),\n";
                *stream << "                     register_write_conflict_ ? 1 : 0);\n";
                *stream << "        std::fflush(stderr);\n";
                *stream << "    }\n";
            }
            else
            {
                *stream << "    while (pending_eval_round) {\n";
                *stream << "        pending_eval_round = false;\n";
                *stream << "        if (activity_profile_enabled_) {\n";
                *stream << "            const auto round_active_in_profile =\n";
                *stream << "                static_cast<std::uint64_t>(grhsim_count_active_supernodes(supernode_active_curr_));\n";
                *stream << "            if (activity_profile_current_peak_active_supernodes_ < round_active_in_profile) {\n";
                *stream << "                activity_profile_current_peak_active_supernodes_ = round_active_in_profile;\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "        // Propagate current activity by scanning the active-flag words directly.\n";
                *stream << "        for (std::size_t activeWordIndex = 0; activeWordIndex < kActiveFlagWordCount; ++activeWordIndex) {\n";
                *stream << "            std::uint8_t activeWordFlags = supernode_active_curr_[activeWordIndex];\n";
                *stream << "            if (activeWordFlags == UINT8_C(0)) {\n";
                *stream << "                continue;\n";
                *stream << "            }\n";
                *stream << "            if (activity_profile_enabled_) {\n";
                *stream << "                const auto baseActiveId = activeWordIndex * kActiveFlagBitsPerWord;\n";
                *stream << "                std::uint8_t profileActiveFlags = activeWordFlags;\n";
                *stream << "                activity_profile_current_executed_supernodes_ +=\n";
                *stream << "                    static_cast<std::uint64_t>(grhsim_popcount_u8(profileActiveFlags));\n";
                *stream << "                while (profileActiveFlags != UINT8_C(0)) {\n";
                *stream << "                    const auto bitIndex =\n";
                *stream << "                        static_cast<std::size_t>(__builtin_ctz(static_cast<unsigned>(profileActiveFlags)));\n";
                *stream << "                    activity_profile_current_executed_ops_ +=\n";
                *stream << "                        static_cast<std::uint64_t>(kSupernodeOpCounts[baseActiveId + bitIndex]);\n";
                *stream << "                    profileActiveFlags = static_cast<std::uint8_t>(\n";
                *stream << "                        profileActiveFlags & static_cast<std::uint8_t>(profileActiveFlags - UINT8_C(1)));\n";
                *stream << "                }\n";
                *stream << "            }\n";
                *stream << "            supernode_active_curr_[activeWordIndex] = UINT8_C(0);\n";
                *stream << "            for (std::size_t batchOffset = kActiveWordBatchOffsets[activeWordIndex];\n";
                *stream << "                 batchOffset < kActiveWordBatchOffsets[activeWordIndex + 1];\n";
                *stream << "                 ++batchOffset) {\n";
                *stream << "                const std::size_t batchIndex = kActiveWordBatchIndices[batchOffset];\n";
                *stream << "                (this->*kBatchEvalFns[batchIndex])(activeWordFlags);\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "        // Commit deferred state updates and reactivate readers of changed state.\n";
                *stream << "        pending_eval_round = commit_state_updates();\n";
                if (!model.allEventValues.empty())
                {
                    *stream << "        // Event edges are per-fixed-point-round signals, so clear them before the next round.\n";
                    emitClearAllEventEdges(*stream, model, "        ");
                }
                *stream << "        if (activity_profile_enabled_) {\n";
                *stream << "            const auto round_active_out_profile =\n";
                *stream << "                static_cast<std::uint64_t>(grhsim_count_active_supernodes(supernode_active_curr_));\n";
                *stream << "            if (activity_profile_current_peak_active_supernodes_ < round_active_out_profile) {\n";
                *stream << "                activity_profile_current_peak_active_supernodes_ = round_active_out_profile;\n";
                *stream << "            }\n";
                *stream << "        }\n";
                *stream << "    }\n";
            }
            if (model.needsSystemTaskRuntime)
            {
                *stream << "    flush_deferred_system_task_texts();\n";
            }
            *stream << "    if (activity_profile_enabled_) {\n";
            *stream << "        activity_profile_executed_supernodes_.push_back(activity_profile_current_executed_supernodes_);\n";
            *stream << "        activity_profile_executed_ops_.push_back(activity_profile_current_executed_ops_);\n";
            *stream << "        activity_profile_peak_active_supernodes_.push_back(activity_profile_current_peak_active_supernodes_);\n";
            *stream << "    }\n";
            *stream << "    // Refresh public outputs after the final visible state of this eval is known.\n";
            *stream << "    refresh_outputs();\n\n";
            if (model.emitWaveform)
            {
                *stream << "    dump_waveform(eval_id);\n\n";
            }
            *stream << "    // Publish current inputs as the previous-eval baseline for the next call.\n";
            for (const auto &port : graph.inputPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.value) << " = " << sanitizeIdentifier(port.name) << ";\n";
            }
            for (const auto &port : graph.inoutPorts())
            {
                *stream << "    " << model.prevInputFieldByValue.at(port.in) << " = " << sanitizeIdentifier(port.name) << ".in;\n";
            }
            *stream << "    first_eval_ = false;\n";
            *stream << "}\n";
            if (auto error = finalizeOutputFile(*stream, evalPath))
            {
                reportError(*error, evalPath.string());
                result.success = false;
                return result;
            }
        }

        const std::size_t emitParallelism =
            std::min(parseEmitParallelism(options), scheduleBatches.empty() ? std::size_t{1} : scheduleBatches.size());
        if (emitParallelism <= 1 || scheduleBatches.size() <= 1)
        {
            for (std::size_t batchIndex = 0; batchIndex < scheduleBatches.size(); ++batchIndex)
            {
                if (auto error = emitSchedBatchFile(schedPaths[batchIndex],
                                                    headerPath,
                                                    className,
                                                    graph,
                                                    model,
                                                    schedule,
                                                    scheduleBatches[batchIndex],
                                                    scheduleBatches.size(),
                                                    maxOutputFileBytes))
                {
                    reportError(*error, graph.symbol());
                    result.success = false;
                    return result;
                }
            }
        }
        else
        {
            struct PendingSchedEmit
            {
                std::size_t batchIndex = 0;
                std::future<std::optional<std::string>> future;
            };

            std::vector<PendingSchedEmit> pending;
            pending.reserve(emitParallelism);

            auto launchBatch = [&](std::size_t batchIndex)
            {
                pending.push_back(PendingSchedEmit{
                    .batchIndex = batchIndex,
                    .future = std::async(std::launch::async,
                                         [&, batchIndex]() -> std::optional<std::string>
                                         {
                                             return emitSchedBatchFile(schedPaths[batchIndex],
                                                                       headerPath,
                                                                       className,
                                                                       graph,
                                                                       model,
                                                                       schedule,
                                                                       scheduleBatches[batchIndex],
                                                                       scheduleBatches.size(),
                                                                       maxOutputFileBytes);
                                         }),
                });
            };

            std::size_t launched = 0;
            while (launched < scheduleBatches.size() || !pending.empty())
            {
                while (launched < scheduleBatches.size() && pending.size() < emitParallelism)
                {
                    launchBatch(launched++);
                }

                PendingSchedEmit task = std::move(pending.front());
                pending.erase(pending.begin());
                if (auto error = task.future.get())
                {
                    reportError(*error, graph.symbol());
                    result.success = false;
                    return result;
                }
            }
        }

        {
            if (auto error = ensureOutputDirectory(makefilePath))
            {
                reportError(*error, graph.symbol());
                result.success = false;
                return result;
            }
            auto stream = std::make_unique<LimitedOutputStream>(makefilePath, maxOutputFileBytes);
            if (!stream->isOpen())
            {
                result.success = false;
                return result;
            }
            *stream << "CXX ?= c++\n";
            if (model.emitWaveform)
            {
                *stream << "CC ?= cc\n";
            }
            *stream << "AR ?= ar\n";
            *stream << "ARFLAGS ?= rcs\n";
            *stream << "CXXFLAGS ?= -std=c++20 -O3\n";
            if (model.emitWaveform)
            {
                *stream << "CFLAGS ?= -O3 -D_GNU_SOURCE\n";
                *stream << "LIBFST_SRC_DIR := " << libfstSrcDir.string() << "\n";
            }
            *stream << "LIB := lib" << prefix << ".a\n";
            *stream << "SRCS := " << statePath.filename().string() << ' ' << evalPath.filename().string();
            for (const auto &stateInitPath : stateInitPaths)
            {
                *stream << ' ' << stateInitPath.filename().string();
            }
            for (const auto &stateCommitPath : stateCommitPaths)
            {
                *stream << ' ' << stateCommitPath.filename().string();
            }
            for (const auto &schedPath : schedPaths)
            {
                *stream << ' ' << schedPath.filename().string();
            }
            *stream << "\n";
            *stream << "OBJS := $(SRCS:.cpp=.o)\n\n";
            if (model.emitWaveform)
            {
                *stream << "FST_SRCS := $(LIBFST_SRC_DIR)/fstapi.c $(LIBFST_SRC_DIR)/fastlz.c $(LIBFST_SRC_DIR)/lz4.c\n";
                *stream << "FST_OBJS := fstapi.o fastlz.o lz4.o\n\n";
            }
            *stream << "all: $(LIB)\n\n";
            *stream << "$(LIB): $(OBJS)";
            if (model.emitWaveform)
            {
                *stream << " $(FST_OBJS)";
            }
            *stream << "\n";
            *stream << "\t$(AR) $(ARFLAGS) $@ $^\n\n";
            *stream << "%.o: %.cpp\n";
            *stream << "\t$(CXX) $(CXXFLAGS)";
            if (model.emitWaveform)
            {
                *stream << " -I$(LIBFST_SRC_DIR)";
            }
            *stream << " -I. -c $< -o $@\n\n";
            if (model.emitWaveform)
            {
                *stream << "fstapi.o: $(LIBFST_SRC_DIR)/fstapi.c\n";
                *stream << "\t$(CC) $(CFLAGS) -I$(LIBFST_SRC_DIR) -c $< -o $@\n\n";
                *stream << "fastlz.o: $(LIBFST_SRC_DIR)/fastlz.c\n";
                *stream << "\t$(CC) $(CFLAGS) -I$(LIBFST_SRC_DIR) -c $< -o $@\n\n";
                *stream << "lz4.o: $(LIBFST_SRC_DIR)/lz4.c\n";
                *stream << "\t$(CC) $(CFLAGS) -I$(LIBFST_SRC_DIR) -c $< -o $@\n\n";
            }
            *stream << "clean:\n";
            *stream << "\t$(RM) $(OBJS)";
            if (model.emitWaveform)
            {
                *stream << " $(FST_OBJS)";
            }
            *stream << " $(LIB)\n";
            if (auto error = finalizeOutputFile(*stream, makefilePath))
            {
                reportError(*error, makefilePath.string());
                result.success = false;
                return result;
            }
        }

        {
            std::string indexError;
            if (!emitDeclaredValueIndexFile(
                    graph, model, schedule, scheduleBatches, schedPaths, declaredValueIndexPath, indexError))
            {
                reportError(indexError, declaredValueIndexPath.string());
                result.success = false;
                return result;
            }
        }

        result.artifacts = {
            headerPath.string(),
            runtimePath.string(),
            declaredValueIndexPath.string(),
            statePath.string(),
            evalPath.string(),
        };
        for (const auto &stateInitPath : stateInitPaths)
        {
            result.artifacts.push_back(stateInitPath.string());
        }
        for (const auto &stateCommitPath : stateCommitPaths)
        {
            result.artifacts.push_back(stateCommitPath.string());
        }
        for (const auto &schedPath : schedPaths)
        {
            result.artifacts.push_back(schedPath.string());
        }
        result.artifacts.push_back(makefilePath.string());
        return result;
    }

} // namespace wolvrix::lib::emit
