#include "emit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <functional>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <unordered_map>
#include <tuple>
#include <string_view>

#include "slang/numeric/SVInt.h"
#include "slang/text/Json.h"

namespace grh::emit
{

    namespace
    {
        template <class... Ts>
        struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <class... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        constexpr int kIndentSize = 2;

        void appendDouble(std::string &out, double value)
        {
            std::ostringstream oss;
            oss << value;
            out.append(oss.str());
        }

        void appendQuotedString(std::string &out, std::string_view text)
        {
            out.push_back('"');
            for (char c : text)
            {
                switch (c)
                {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\b':
                    out.append("\\b");
                    break;
                case '\f':
                    out.append("\\f");
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
                    if (static_cast<unsigned char>(c) <= 0x1f)
                    {
                        char buf[7];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out.append(buf);
                    }
                    else
                    {
                        out.push_back(c);
                    }
                    break;
                }
            }
            out.push_back('"');
        }

        std::string escapeSvString(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
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
                case '\b':
                    out.append("\\b");
                    break;
                case '\f':
                    out.append("\\f");
                    break;
                case '\v':
                    out.append("\\v");
                    break;
                default:
                    if (ch < 0x20 || ch == 0x7f)
                    {
                        char buf[5];
                        std::snprintf(buf, sizeof(buf), "\\x%02x", ch);
                        out.append(buf);
                    }
                    else
                    {
                        out.push_back(static_cast<char>(ch));
                    }
                    break;
                }
            }
            return out;
        }

        std::optional<std::string> sizedLiteralIfUnsized(std::string_view literal, int64_t width)
        {
            if (width <= 0)
            {
                return std::nullopt;
            }
            std::string compact;
            compact.reserve(literal.size());
            for (char ch : literal)
            {
                if (std::isspace(static_cast<unsigned char>(ch)) || ch == '_')
                {
                    continue;
                }
                compact.push_back(ch);
            }
            if (compact.empty())
            {
                return std::nullopt;
            }
            std::size_t start = 0;
            bool negative = false;
            if (compact[0] == '-' || compact[0] == '+')
            {
                negative = compact[0] == '-';
                start = 1;
            }
            auto allDigits = [&](std::size_t from, std::size_t to) -> bool
            {
                if (from >= to)
                {
                    return false;
                }
                for (std::size_t i = from; i < to; ++i)
                {
                    if (!std::isdigit(static_cast<unsigned char>(compact[i])))
                    {
                        return false;
                    }
                }
                return true;
            };
            const std::size_t quotePos = compact.find('\'', start);
            if (quotePos != std::string::npos)
            {
                if (quotePos > start && allDigits(start, quotePos))
                {
                    return std::nullopt;
                }
                if (quotePos + 1 >= compact.size())
                {
                    return std::nullopt;
                }
                const char baseChar = compact[quotePos + 1];
                char base = 'b';
                std::string digits;
                if (baseChar == 'b' || baseChar == 'B' ||
                    baseChar == 'd' || baseChar == 'D' ||
                    baseChar == 'h' || baseChar == 'H' ||
                    baseChar == 'o' || baseChar == 'O')
                {
                    base = static_cast<char>(std::tolower(static_cast<unsigned char>(baseChar)));
                    digits = compact.substr(quotePos + 2);
                    if (digits.empty())
                    {
                        digits = "0";
                    }
                }
                else
                {
                    digits = compact.substr(quotePos + 1);
                }
                std::string sized = std::to_string(width);
                sized.push_back('\'');
                sized.push_back(base);
                sized.append(digits);
                if (negative)
                {
                    sized.insert(sized.begin(), '-');
                }
                return sized;
            }
            if (!allDigits(start, compact.size()))
            {
                return std::nullopt;
            }
            std::string sized = std::to_string(width);
            sized.append("'d");
            sized.append(compact.substr(start));
            if (negative)
            {
                sized.insert(sized.begin(), '-');
            }
            return sized;
        }

        std::optional<std::string> signBitLiteralForConst(std::string_view literal,
                                                          int64_t width,
                                                          bool isSigned)
        {
            if (width <= 0)
            {
                return std::nullopt;
            }
            std::string compact;
            compact.reserve(literal.size());
            for (char ch : literal)
            {
                if (std::isspace(static_cast<unsigned char>(ch)) || ch == '_')
                {
                    continue;
                }
                compact.push_back(ch);
            }
            if (compact.empty())
            {
                return std::nullopt;
            }
            if (compact.size() >= 2 && compact.front() == '(' && compact.back() == ')')
            {
                bool nested = false;
                for (std::size_t i = 1; i + 1 < compact.size(); ++i)
                {
                    if (compact[i] == '(' || compact[i] == ')')
                    {
                        nested = true;
                        break;
                    }
                }
                if (!nested)
                {
                    compact = compact.substr(1, compact.size() - 2);
                }
            }
            if (compact.empty())
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
                parsed.setSigned(isSigned);
                parsed = parsed.resize(static_cast<slang::bitwidth_t>(width));
                const slang::logic_t bit = parsed[static_cast<int32_t>(width - 1)];
                char bitChar = bit.toChar();
                bitChar = static_cast<char>(std::tolower(static_cast<unsigned char>(bitChar)));
                std::string out = "1'b";
                out.push_back(bitChar);
                return out;
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        void appendNewlineAndIndent(std::string &out, int indent)
        {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(indent * kIndentSize), ' ');
        }

        std::optional<std::vector<uint8_t>> parseConstMaskBits(std::string_view literal,
                                                               int64_t targetWidth)
        {
            if (targetWidth <= 0)
            {
                return std::nullopt;
            }

            std::string text;
            text.reserve(literal.size());
            for (char ch : literal)
            {
                if (ch == '_' || std::isspace(static_cast<unsigned char>(ch)))
                {
                    continue;
                }
                text.push_back(ch);
            }
            if (text.empty() || text.front() == '-')
            {
                return std::nullopt;
            }

            bool isSigned = false;
            int64_t literalWidth = -1;
            char base = 'd';
            std::string digits;

            const std::size_t quotePos = text.find('\'');
            if (quotePos != std::string::npos)
            {
                const std::string widthText = text.substr(0, quotePos);
                if (!widthText.empty())
                {
                    for (char ch : widthText)
                    {
                        if (!std::isdigit(static_cast<unsigned char>(ch)))
                        {
                            return std::nullopt;
                        }
                    }
                    try
                    {
                        literalWidth = std::stoll(widthText);
                    }
                    catch (const std::exception &)
                    {
                        return std::nullopt;
                    }
                    if (literalWidth <= 0)
                    {
                        return std::nullopt;
                    }
                }
                std::size_t basePos = quotePos + 1;
                if (basePos >= text.size())
                {
                    return std::nullopt;
                }
                if (text[basePos] == 's' || text[basePos] == 'S')
                {
                    isSigned = true;
                    basePos++;
                }
                if (basePos >= text.size())
                {
                    return std::nullopt;
                }
                bool unbasedLiteral = false;
                const char unbased = text[basePos];
                if ((unbased == '0' || unbased == '1') && basePos + 1 == text.size())
                {
                    base = 'b';
                    literalWidth = targetWidth;
                    digits.assign(1, unbased);
                    unbasedLiteral = true;
                }
                if (!unbasedLiteral)
                {
                    base = static_cast<char>(std::tolower(static_cast<unsigned char>(text[basePos])));
                    basePos++;
                    digits = text.substr(basePos);
                }
            }
            else
            {
                digits = text;
            }

            if (digits.empty())
            {
                return std::nullopt;
            }

            std::vector<uint8_t> bits;
            switch (base)
            {
            case 'b':
            {
                bits.reserve(digits.size());
                for (auto it = digits.rbegin(); it != digits.rend(); ++it)
                {
                    const char ch = *it;
                    if (ch == '0' || ch == '1')
                    {
                        bits.push_back(static_cast<uint8_t>(ch - '0'));
                    }
                    else
                    {
                        return std::nullopt;
                    }
                }
                break;
            }
            case 'h':
            {
                bits.reserve(digits.size() * 4);
                for (auto it = digits.rbegin(); it != digits.rend(); ++it)
                {
                    const char ch = *it;
                    int value = 0;
                    if (ch >= '0' && ch <= '9')
                    {
                        value = ch - '0';
                    }
                    else if (ch >= 'a' && ch <= 'f')
                    {
                        value = 10 + (ch - 'a');
                    }
                    else if (ch >= 'A' && ch <= 'F')
                    {
                        value = 10 + (ch - 'A');
                    }
                    else
                    {
                        return std::nullopt;
                    }
                    for (int bit = 0; bit < 4; ++bit)
                    {
                        bits.push_back(static_cast<uint8_t>((value >> bit) & 1));
                    }
                }
                break;
            }
            case 'o':
            {
                bits.reserve(digits.size() * 3);
                for (auto it = digits.rbegin(); it != digits.rend(); ++it)
                {
                    const char ch = *it;
                    if (ch < '0' || ch > '7')
                    {
                        return std::nullopt;
                    }
                    const int value = ch - '0';
                    for (int bit = 0; bit < 3; ++bit)
                    {
                        bits.push_back(static_cast<uint8_t>((value >> bit) & 1));
                    }
                }
                break;
            }
            case 'd':
            {
                for (char ch : digits)
                {
                    if (!std::isdigit(static_cast<unsigned char>(ch)))
                    {
                        return std::nullopt;
                    }
                }
                std::string dec = digits;
                if (dec == "0")
                {
                    bits.push_back(0);
                    break;
                }
                while (!(dec.size() == 1 && dec[0] == '0'))
                {
                    int carry = 0;
                    std::string next;
                    next.reserve(dec.size());
                    for (char ch : dec)
                    {
                        const int digit = ch - '0';
                        const int value = carry * 10 + digit;
                        const int q = value / 2;
                        carry = value % 2;
                        if (!next.empty() || q != 0)
                        {
                            next.push_back(static_cast<char>('0' + q));
                        }
                    }
                    bits.push_back(static_cast<uint8_t>(carry));
                    if (next.empty())
                    {
                        next = "0";
                    }
                    dec = std::move(next);
                }
                break;
            }
            default:
                return std::nullopt;
            }

            if (bits.empty())
            {
                bits.push_back(0);
            }

            auto resizeBits = [&](int64_t width, bool signedExtend)
            {
                if (width <= 0)
                {
                    return;
                }
                uint8_t fill = 0;
                if (signedExtend && !bits.empty())
                {
                    fill = bits.back();
                }
                if (static_cast<int64_t>(bits.size()) > width)
                {
                    bits.resize(static_cast<std::size_t>(width));
                }
                else if (static_cast<int64_t>(bits.size()) < width)
                {
                    bits.resize(static_cast<std::size_t>(width), fill);
                }
            };

            if (literalWidth > 0)
            {
                resizeBits(literalWidth, isSigned);
            }
            resizeBits(targetWidth, isSigned);

            return bits;
        }

        std::vector<const grh::ir::Graph *> graphsSortedByName(const grh::ir::Netlist &netlist)
        {
            std::vector<const grh::ir::Graph *> graphs;
            graphs.reserve(netlist.graphs().size());
            for (const auto &symbol : netlist.graphOrder())
            {
                if (auto it = netlist.graphs().find(symbol); it != netlist.graphs().end())
                {
                    graphs.push_back(it->second.get());
                }
            }
            return graphs;
        }

        std::string serializeWithJsonWriter(const grh::ir::Netlist &netlist,
                                            std::span<const grh::ir::Graph *const> topGraphs,
                                            bool pretty)
        {
            slang::JsonWriter writer;
            writer.setPrettyPrint(pretty);
            writer.startObject();

            writer.writeProperty("graphs");
            writer.startArray();
            for (const grh::ir::Graph *graph : graphsSortedByName(netlist))
            {
                graph->writeJson(writer);
            }
            writer.endArray();

            writer.writeProperty("tops");
            writer.startArray();
            for (const grh::ir::Graph *graph : topGraphs)
            {
                writer.writeValue(graph->symbol());
            }
            writer.endArray();

            writer.endObject();
            return std::string(writer.view());
        }

        const char *colonToken(JsonPrintMode mode)
        {
            return mode == JsonPrintMode::Compact ? ":" : ": ";
        }

        const char *commaToken(JsonPrintMode mode)
        {
            return mode == JsonPrintMode::Compact ? "," : ", ";
        }

        template <typename Writer>
        void writeInlineObject(std::string &out, JsonPrintMode mode, Writer &&writer)
        {
            out.push_back('{');
            bool first = true;
            const char *comma = commaToken(mode);
            writer([&](std::string_view key, auto &&valueWriter)
                   {
                       if (!first)
                       {
                           out.append(comma);
                       }
                       appendQuotedString(out, key);
                       out.append(colonToken(mode));
                       valueWriter();
                       first = false;
                   });
            out.push_back('}');
        }

        void writeDebugInline(std::string &out, JsonPrintMode mode,
                              const std::optional<grh::ir::SrcLoc> &debugInfo)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("file", [&]
                                       { appendQuotedString(out, debugInfo->file); });
                                  prop("line", [&]
                                       { out.append(std::to_string(debugInfo->line)); });
                                  prop("col", [&]
                                       { out.append(std::to_string(debugInfo->column)); });
                                  prop("endLine", [&]
                                       { out.append(std::to_string(debugInfo->endLine)); });
                                  prop("endCol", [&]
                                       { out.append(std::to_string(debugInfo->endColumn)); });
                              });
        }

        std::string formatSrcAttribute(const std::optional<grh::ir::SrcLoc> &srcLoc)
        {
            if (!srcLoc || srcLoc->file.empty() || srcLoc->line == 0)
            {
                return {};
            }

            const uint32_t startLine = srcLoc->line;
            const uint32_t startCol = srcLoc->column;
            const uint32_t endLine = srcLoc->endLine ? srcLoc->endLine : startLine;
            const uint32_t endCol = srcLoc->endColumn ? srcLoc->endColumn : startCol;

            std::string file = srcLoc->file;
            for (char &ch : file)
            {
                if (ch == '\n' || ch == '\r')
                {
                    ch = ' ';
                }
            }
            std::string sanitized;
            sanitized.reserve(file.size() + 8);
            for (std::size_t i = 0; i < file.size(); ++i)
            {
                if (i + 1 < file.size() && file[i] == '*' && file[i + 1] == '/')
                {
                    sanitized.append("* /");
                    ++i;
                    continue;
                }
                if (i + 1 < file.size() && file[i] == '/' && file[i + 1] == '*')
                {
                    sanitized.append("/ *");
                    ++i;
                    continue;
                }
                sanitized.push_back(file[i]);
            }

            std::ostringstream oss;
            oss << "/* src: " << sanitized << ":" << startLine;
            if (startCol != 0)
            {
                oss << "." << startCol;
            }
            if (endLine != 0 || endCol != 0)
            {
                oss << "-" << endLine;
                if (endCol != 0)
                {
                    oss << "." << endCol;
                }
            }
            oss << " */";
            return oss.str();
        }

        std::string opSymbolOrFallback(const grh::ir::Operation &op)
        {
            std::string sym(op.symbolText());
            if (!sym.empty())
            {
                return sym;
            }
            if (op.id().valid())
            {
                return "op_" + std::to_string(op.id().index);
            }
            return {};
        }

        void writeUsersInline(std::string &out, const grh::ir::Graph &graph, std::span<const grh::ir::ValueUser> users, JsonPrintMode mode)
        {
            const char *comma = commaToken(mode);
            out.push_back('[');
            bool first = true;
            for (const auto &user : users)
            {
                if (!first)
                {
                    out.append(comma);
                }
                writeInlineObject(out, mode, [&](auto &&prop)
                                  {
                                      prop("op", [&]
                                           {
                                               if (user.operation.valid())
                                               {
                                                   appendQuotedString(out, opSymbolOrFallback(graph.getOperation(user.operation)));
                                               }
                                               else
                                               {
                                                   appendQuotedString(out, "");
                                               }
                                           });
                                      prop("idx", [&]
                                           { out.append(std::to_string(static_cast<int64_t>(user.operandIndex))); });
                                  });
                first = false;
            }
            out.push_back(']');
        }

        void writeAttrsInline(std::string &out, std::span<const grh::ir::AttrKV> attrs, JsonPrintMode mode)
        {
            out.push_back('{');
            bool firstAttr = true;
            const char *comma = commaToken(mode);
            for (const auto &attr : attrs)
            {
                if (!firstAttr)
                {
                    out.append(comma);
                }
                appendQuotedString(out, attr.key);
                out.append(colonToken(mode));
                writeInlineObject(out, mode, [&](auto &&prop)
                                  {
                                      std::visit(
                                          Overloaded{
                                              [&](bool v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "bool"); });
                                                  prop("v", [&]
                                                       { out.append(v ? "true" : "false"); });
                                              },
                                              [&](int64_t v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "int"); });
                                                  prop("v", [&]
                                                       { out.append(std::to_string(v)); });
                                              },
                                              [&](double v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "double"); });
                                                  prop("v", [&]
                                                       { appendDouble(out, v); });
                                              },
                                              [&](const std::string &v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "string"); });
                                                  prop("v", [&]
                                                       { appendQuotedString(out, v); });
                                              },
                                              [&](const std::vector<bool> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "bool[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (bool entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               out.append(entry ? "true" : "false");
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              },
                                              [&](const std::vector<int64_t> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "int[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (int64_t entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               out.append(std::to_string(entry));
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              },
                                              [&](const std::vector<double> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "double[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                          for (double entry : arr)
                                                          {
                                                              if (!first)
                                                              {
                                                                  out.append(comma);
                                                              }
                                                              appendDouble(out, entry);
                                                              first = false;
                                                          }
                                                          out.push_back(']');
                                                       });
                                              },
                                              [&](const std::vector<std::string> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "string[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (const auto &entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               appendQuotedString(out, entry);
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              }},
                                          attr.value);
                                  });
                firstAttr = false;
            }
            out.push_back('}');
        }

        void writeValueInline(std::string &out, const grh::ir::Graph &graph, const grh::ir::Value &value, JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("sym", [&]
                                       { appendQuotedString(out, value.symbolText()); });
                                  prop("w", [&]
                                       { out.append(std::to_string(value.width())); });
                                  prop("sgn", [&]
                                       { out.append(value.isSigned() ? "true" : "false"); });
                                  prop("type", [&]
                                       { appendQuotedString(out, std::string(grh::ir::toString(value.type()))); });
                                  prop("in", [&]
                                       { out.append(value.isInput() ? "true" : "false"); });
                                  prop("out", [&]
                                       { out.append(value.isOutput() ? "true" : "false"); });
                                  prop("inout", [&]
                                       { out.append(value.isInout() ? "true" : "false"); });
                                  if (value.definingOp())
                                  {
                                      prop("def", [&]
                                           { appendQuotedString(out, opSymbolOrFallback(graph.getOperation(value.definingOp()))); });
                                  }
                                  prop("users", [&]
                                       { writeUsersInline(out, graph, value.users(), mode); });
                                  if (value.srcLoc())
                                  {
                                      prop("loc", [&]
                                           { writeDebugInline(out, mode, value.srcLoc()); });
                                  }
                              });
        }

        void writePortInline(std::string &out, std::string_view name, std::string_view valueSymbol, JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("name", [&]
                                       { appendQuotedString(out, name); });
                                  prop("val", [&]
                                       { appendQuotedString(out, valueSymbol); });
                              });
        }

        void writeOperationInline(std::string &out, const grh::ir::Graph &graph, const grh::ir::Operation &op, JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("sym", [&]
                                       { appendQuotedString(out, opSymbolOrFallback(op)); });
                                  prop("kind", [&]
                                       { appendQuotedString(out, grh::ir::toString(op.kind())); });
                                  prop("in", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const auto operandId : op.operands())
                                           {
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, graph.getValue(operandId).symbolText());
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  prop("out", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const auto resultId : op.results())
                                           {
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, graph.getValue(resultId).symbolText());
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  if (!op.attrs().empty())
                                  {
                                      prop("attrs", [&]
                                           { writeAttrsInline(out, op.attrs(), mode); });
                                  }
                                  if (op.srcLoc())
                                  {
                                      prop("loc", [&]
                                           { writeDebugInline(out, mode, op.srcLoc()); });
                                  }
                              });
        }

        template <typename Range, typename Fn>
        void writeInlineArray(std::string &out, JsonPrintMode mode, int elementIndent, const Range &range, Fn &&writeElement)
        {
            (void)mode;
            out.push_back('[');
            bool first = true;
            for (const auto &entry : range)
            {
                if (!first)
                {
                    out.push_back(',');
                }
                appendNewlineAndIndent(out, elementIndent);
                writeElement(entry);
                first = false;
            }
            if (!range.empty())
            {
                appendNewlineAndIndent(out, elementIndent - 1);
            }
            out.push_back(']');
        }

        void writePortsPrettyCompact(std::string &out,
                                     const grh::ir::Graph &graph,
                                     std::span<const grh::ir::Port> ports,
                                     JsonPrintMode mode,
                                     int indent)
        {
            (void)mode;
            out.push_back('[');
            bool first = true;
            for (const auto &port : ports)
            {
                if (!first)
                {
                    out.push_back(',');
                }
                appendNewlineAndIndent(out, indent);
                writePortInline(out,
                                graph.symbolText(port.name),
                                graph.getValue(port.value).symbolText(),
                                mode);
                first = false;
            }
            if (!ports.empty())
            {
                appendNewlineAndIndent(out, indent - 1);
            }
            out.push_back(']');
        }

        void writeInoutPortsPrettyCompact(std::string &out,
                                          const grh::ir::Graph &graph,
                                          std::span<const grh::ir::InoutPort> ports,
                                          JsonPrintMode mode,
                                          int indent)
        {
            (void)mode;
            out.push_back('[');
            bool first = true;
            for (const auto &port : ports)
            {
                if (!first)
                {
                    out.push_back(',');
                }
                appendNewlineAndIndent(out, indent);
                writeInlineObject(out, mode, [&](auto &&prop)
                                  {
                                      prop("name", [&]
                                           { appendQuotedString(out, graph.symbolText(port.name)); });
                                      prop("in", [&]
                                           { appendQuotedString(out, graph.getValue(port.in).symbolText()); });
                                      prop("out", [&]
                                           { appendQuotedString(out, graph.getValue(port.out).symbolText()); });
                                      prop("oe", [&]
                                           { appendQuotedString(out, graph.getValue(port.oe).symbolText()); });
                                  });
                first = false;
            }
            if (!ports.empty())
            {
                appendNewlineAndIndent(out, indent - 1);
            }
            out.push_back(']');
        }

        void writeGraphPrettyCompact(std::string &out, const grh::ir::Graph &graph, int baseIndent)
        {
            out.push_back('{');
            int indent = baseIndent + 1;

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "symbol");
            out.append(": ");
            appendQuotedString(out, graph.symbol());
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "vals");
            out.append(": ");
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, graph.values(),
                             [&](const grh::ir::ValueId valueId)
                             { writeValueInline(out, graph, graph.getValue(valueId), JsonPrintMode::PrettyCompact); });
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "ports");
            out.append(": ");
            out.push_back('{');
            int portsIndent = indent + 1;

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "in");
            out.append(": ");
            writePortsPrettyCompact(out, graph, graph.inputPorts(), JsonPrintMode::PrettyCompact, portsIndent + 1);
            out.push_back(',');

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "out");
            out.append(": ");
            writePortsPrettyCompact(out, graph, graph.outputPorts(), JsonPrintMode::PrettyCompact, portsIndent + 1);
            out.push_back(',');

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "inout");
            out.append(": ");
            writeInoutPortsPrettyCompact(out, graph, graph.inoutPorts(), JsonPrintMode::PrettyCompact, portsIndent + 1);

            appendNewlineAndIndent(out, indent);
            out.push_back('}');
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "ops");
            out.append(": ");
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, graph.operations(),
                             [&](const grh::ir::OperationId opId)
                             { writeOperationInline(out, graph, graph.getOperation(opId), JsonPrintMode::PrettyCompact); });

            appendNewlineAndIndent(out, baseIndent);
            out.push_back('}');
        }

        const std::string &lookupName(const std::vector<std::string> &names, uint32_t index)
        {
            static const std::string kEmpty;
            if (index == 0 || index > names.size())
            {
                return kEmpty;
            }
            return names[index - 1];
        }

        void writeUsersInlineIr(std::string &out,
                                std::span<const grh::ir::ValueUser> users,
                                const std::vector<std::string> &opNames,
                                JsonPrintMode mode)
        {
            const char *comma = commaToken(mode);
            out.push_back('[');
            bool first = true;
            for (const auto &user : users)
            {
                if (!first)
                {
                    out.append(comma);
                }
                writeInlineObject(out, mode, [&](auto &&prop)
                                  {
                                      const std::string &opName = lookupName(opNames, user.operation.index);
                                      prop("op", [&]
                                           { appendQuotedString(out, opName); });
                                      prop("idx", [&]
                                           { out.append(std::to_string(static_cast<int64_t>(user.operandIndex))); });
                                  });
                first = false;
            }
            out.push_back(']');
        }

        void writeAttrsInlineIr(std::string &out,
                                std::span<const grh::ir::AttrKV> attrs,
                                JsonPrintMode mode)
        {
            out.push_back('{');
            bool firstAttr = true;
            const char *comma = commaToken(mode);
            for (const auto &attr : attrs)
            {
                if (!firstAttr)
                {
                    out.append(comma);
                }
                appendQuotedString(out, attr.key);
                out.append(colonToken(mode));
                writeInlineObject(out, mode, [&](auto &&prop)
                                  {
                                      std::visit(
                                          Overloaded{
                                              [&](bool v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "bool"); });
                                                  prop("v", [&]
                                                       { out.append(v ? "true" : "false"); });
                                              },
                                              [&](int64_t v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "int"); });
                                                  prop("v", [&]
                                                       { out.append(std::to_string(v)); });
                                              },
                                              [&](double v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "double"); });
                                                  prop("v", [&]
                                                       { appendDouble(out, v); });
                                              },
                                              [&](const std::string &v)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "string"); });
                                                  prop("v", [&]
                                                       { appendQuotedString(out, v); });
                                              },
                                              [&](const std::vector<bool> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "bool[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (bool entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               out.append(entry ? "true" : "false");
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              },
                                              [&](const std::vector<int64_t> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "int[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (int64_t entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               out.append(std::to_string(entry));
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              },
                                              [&](const std::vector<double> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "double[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (double entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               appendDouble(out, entry);
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              },
                                              [&](const std::vector<std::string> &arr)
                                              {
                                                  prop("t", [&]
                                                       { appendQuotedString(out, "string[]"); });
                                                  prop("vs", [&]
                                                       {
                                                           out.push_back('[');
                                                           bool first = true;
                                                           for (const auto &entry : arr)
                                                           {
                                                               if (!first)
                                                               {
                                                                   out.append(comma);
                                                               }
                                                               appendQuotedString(out, entry);
                                                               first = false;
                                                           }
                                                           out.push_back(']');
                                                       });
                                              }},
                                          attr.value);
                                  });
                firstAttr = false;
            }
            out.push_back('}');
        }

        std::string serializePrettyCompact(const grh::ir::Netlist &netlist, std::span<const grh::ir::Graph *const> topGraphs)
        {
            std::string out;
            int indent = 0;

            out.push_back('{');
            indent = 1;
            appendNewlineAndIndent(out, indent);

            appendQuotedString(out, "graphs");
            out.append(": [");

            const auto graphs = graphsSortedByName(netlist);
            if (!graphs.empty())
            {
                bool first = true;
                for (const grh::ir::Graph *graph : graphs)
                {
                    if (!first)
                    {
                        out.push_back(',');
                    }
                    appendNewlineAndIndent(out, indent + 1);
                    writeGraphPrettyCompact(out, *graph, indent + 1);
                    first = false;
                }
                appendNewlineAndIndent(out, indent);
            }
            out.push_back(']');
            out.push_back(',');
            appendNewlineAndIndent(out, indent);

            appendQuotedString(out, "tops");
            out.append(": [");
            if (!topGraphs.empty())
            {
                bool firstTop = true;
                for (const grh::ir::Graph *graph : topGraphs)
                {
                    if (!firstTop)
                    {
                        out.push_back(',');
                    }
                    appendNewlineAndIndent(out, indent + 1);
                    appendQuotedString(out, graph->symbol());
                    firstTop = false;
                }
                appendNewlineAndIndent(out, indent);
            }
            out.push_back(']');

            appendNewlineAndIndent(out, indent - 1);
            out.push_back('}');
            return out;
        }

        std::string serializeNetlistJson(const grh::ir::Netlist &netlist,
                                         std::span<const grh::ir::Graph *const> topGraphs,
                                         JsonPrintMode mode)
        {
            switch (mode)
            {
            case JsonPrintMode::Compact:
                return serializeWithJsonWriter(netlist, topGraphs, /* pretty */ false);
            case JsonPrintMode::Pretty:
                return serializeWithJsonWriter(netlist, topGraphs, /* pretty */ true);
            case JsonPrintMode::PrettyCompact:
            default:
                return serializePrettyCompact(netlist, topGraphs);
            }
        }
    } // namespace

    Emit::Emit(EmitDiagnostics *diagnostics) : diagnostics_(diagnostics) {}

    void EmitDiagnostics::error(std::string message, std::string context)
    {
        messages_.push_back(EmitDiagnostic{EmitDiagnosticKind::Error, std::move(message), std::move(context)});
    }

    void EmitDiagnostics::warning(std::string message, std::string context)
    {
        messages_.push_back(EmitDiagnostic{EmitDiagnosticKind::Warning, std::move(message), std::move(context)});
    }

    bool EmitDiagnostics::hasError() const noexcept
    {
        for (const auto &message : messages_)
        {
            if (message.kind == EmitDiagnosticKind::Error)
            {
                return true;
            }
        }
        return false;
    }

    void Emit::reportError(std::string message, std::string context) const
    {
        if (diagnostics_ != nullptr)
        {
            diagnostics_->error(std::move(message), std::move(context));
        }
    }

    void Emit::reportWarning(std::string message, std::string context) const
    {
        if (diagnostics_ != nullptr)
        {
            diagnostics_->warning(std::move(message), std::move(context));
        }
    }

    bool Emit::validateTopGraphs(const std::vector<const grh::ir::Graph *> &topGraphs) const
    {
        if (topGraphs.empty())
        {
            reportError("No top graphs available for emission");
            return false;
        }
        return true;
    }

    std::vector<const grh::ir::Graph *> Emit::resolveTopGraphs(const grh::ir::Netlist &netlist,
                                                           const EmitOptions &options) const
    {
        std::vector<const grh::ir::Graph *> result;
        std::unordered_set<std::string> seen;

        auto tryAdd = [&](std::string_view name)
        {
            if (seen.find(std::string(name)) != seen.end())
            {
                return;
            }

            const grh::ir::Graph *graph = netlist.findGraph(name);
            if (graph == nullptr)
            {
                reportError("Top graph not found", std::string(name));
                return;
            }

            seen.insert(std::string(graph->symbol()));
            result.push_back(graph);
        };

        if (!options.topOverrides.empty())
        {
            for (const auto &name : options.topOverrides)
            {
                tryAdd(name);
            }
        }
        else
        {
            for (const auto &name : netlist.topGraphs())
            {
                tryAdd(name);
            }
        }

        return result;
    }

    std::filesystem::path Emit::resolveOutputDir(const EmitOptions &options) const
    {
        if (options.outputDir && !options.outputDir->empty())
        {
            return std::filesystem::path(*options.outputDir);
        }
        return std::filesystem::current_path();
    }

    bool Emit::ensureParentDirectory(const std::filesystem::path &path) const
    {
        const std::filesystem::path parent = path.parent_path();
        if (parent.empty())
        {
            return true;
        }

        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            reportError("Failed to create output directory: " + ec.message(), parent.string());
            return false;
        }
        return true;
    }

    std::unique_ptr<std::ofstream> Emit::openOutputFile(const std::filesystem::path &path) const
    {
        if (!ensureParentDirectory(path))
        {
            return nullptr;
        }

        auto stream = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
        if (!stream->is_open())
        {
            reportError("Failed to open output file for writing", path.string());
            return nullptr;
        }
        return stream;
    }

    EmitResult Emit::emit(const grh::ir::Netlist &netlist, const EmitOptions &options)
    {
        EmitResult result;

        std::vector<const grh::ir::Graph *> topGraphs = resolveTopGraphs(netlist, options);
        if (!validateTopGraphs(topGraphs))
        {
            result.success = false;
            return result;
        }

        result = emitImpl(netlist, topGraphs, options);
        if (diagnostics_ && diagnostics_->hasError())
        {
            result.success = false;
        }
        return result;
    }

    std::optional<std::string> EmitJSON::emitToString(const grh::ir::Netlist &netlist, const EmitOptions &options)
    {
        std::vector<const grh::ir::Graph *> topGraphs = resolveTopGraphs(netlist, options);
        if (!validateTopGraphs(topGraphs))
        {
            return std::nullopt;
        }

        try
        {
            return serializeNetlistJson(netlist, topGraphs, options.jsonMode);
        }
        catch (const std::exception &ex)
        {
            reportError("Failed to serialize netlist to JSON: " + std::string(ex.what()));
            return std::nullopt;
        }
    }

    EmitResult EmitJSON::emitImpl(const grh::ir::Netlist &netlist,
                                  std::span<const grh::ir::Graph *const> topGraphs,
                                  const EmitOptions &options)
    {
        EmitResult result;

        std::string jsonText;
        try
        {
            jsonText = serializeNetlistJson(netlist, topGraphs, options.jsonMode);
        }
        catch (const std::exception &ex)
        {
            reportError("Failed to serialize netlist to JSON: " + std::string(ex.what()));
            result.success = false;
            return result;
        }

        const std::string filename = options.outputFilename.value_or(std::string("grh.json"));
        const std::filesystem::path outputPath = resolveOutputDir(options) / filename;
        auto stream = openOutputFile(outputPath);
        if (!stream)
        {
            result.success = false;
            return result;
        }

        *stream << jsonText;
        result.artifacts.push_back(outputPath.string());
        return result;
    }

    namespace
    {
        constexpr int kIndentSizeSv = 2;

        enum class PortDir
        {
            Input,
            Output,
            Inout
        };

        struct NetDecl
        {
            int64_t width = 1;
            bool isSigned = false;
            std::optional<grh::ir::SrcLoc> debug;
        };

        struct VarDecl
        {
            grh::ir::ValueType type = grh::ir::ValueType::Logic;
            std::optional<grh::ir::SrcLoc> debug;
            std::optional<std::string> initValue;  // For compile-time constants (e.g., strings)
        };

        struct PortDecl
        {
            PortDir dir = PortDir::Input;
            int64_t width = 1;
            bool isSigned = false;
            bool isReg = false;
            grh::ir::ValueType valueType = grh::ir::ValueType::Logic;
            std::optional<grh::ir::SrcLoc> debug;
        };

        struct SeqEvent
        {
            std::string edge;
            grh::ir::ValueId signal = grh::ir::ValueId::invalid();
        };

        struct SeqKey
        {
            std::vector<SeqEvent> events;
        };

        struct SeqBlock
        {
            SeqKey key{};
            std::vector<std::string> stmts;
            grh::ir::OperationId op = grh::ir::OperationId::invalid();
        };

        struct SimpleBlock
        {
            std::string header;
            std::vector<std::string> stmts;
            grh::ir::OperationId op = grh::ir::OperationId::invalid();
        };

        void appendIndented(std::ostringstream &out, int indent, const std::string &text)
        {
            out << std::string(static_cast<std::size_t>(indent * kIndentSizeSv), ' ') << text << '\n';
        }

        std::string widthRange(int64_t width)
        {
            if (width <= 1)
            {
                return {};
            }
            return "[" + std::to_string(width - 1) + ":0]";
        }

        std::string signedPrefix(bool isSigned)
        {
            return isSigned ? "signed " : "";
        }

        bool isSimpleIdentifier(std::string_view expr)
        {
            if (expr.empty())
            {
                return false;
            }
            const auto isIdentStart = [](char ch) -> bool
            {
                return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
            };
            const auto isIdentChar = [](char ch) -> bool
            {
                return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
            };
            bool first = true;
            for (char ch : expr)
            {
                if (first)
                {
                    if (!isIdentStart(ch))
                    {
                        return false;
                    }
                    first = false;
                    continue;
                }
                if (!isIdentChar(ch))
                {
                    return false;
                }
            }
            return true;
        }

        std::string parenIfNeeded(std::string_view expr)
        {
            if (expr.empty() || isSimpleIdentifier(expr))
            {
                return std::string(expr);
            }
            if (expr.front() == '(' && expr.back() == ')')
            {
                return std::string(expr);
            }
            std::string out;
            out.reserve(expr.size() + 2);
            out.push_back('(');
            out.append(expr);
            out.push_back(')');
            return out;
        }

        struct ExprTools
        {
            std::function<grh::ir::ValueType(grh::ir::ValueId)> valueType;
            std::function<int64_t(grh::ir::ValueId)> valueWidth;
            std::function<bool(grh::ir::ValueId)> valueSigned;
            std::function<std::optional<std::string>(grh::ir::ValueId)> constLiteralRawFor;
            std::function<std::string(grh::ir::ValueId)> valueExpr;
            bool parenReduce = false;
            bool allowConstSignBit = true;
            bool allowInlineLiteral = true;

            std::string logicalOperand(grh::ir::ValueId valueId) const
            {
                if (valueType(valueId) != grh::ir::ValueType::Logic)
                {
                    return valueExpr(valueId);
                }
                const int64_t width = valueWidth(valueId);
                const std::string name = valueExpr(valueId);
                if (width == 1)
                {
                    return name;
                }
                if (parenReduce)
                {
                    return "(|" + parenIfNeeded(name) + ")";
                }
                return "(|" + name + ")";
            }

            std::string sizedOperandExpr(grh::ir::ValueId valueId) const
            {
                if (valueType(valueId) != grh::ir::ValueType::Logic)
                {
                    return valueExpr(valueId);
                }
                const std::string expr = valueExpr(valueId);
                if (!expr.empty() && isSimpleIdentifier(expr))
                {
                    return expr;
                }
                if (allowInlineLiteral)
                {
                    if (auto literal = constLiteralRawFor(valueId))
                    {
                        if (auto sized = sizedLiteralIfUnsized(*literal, valueWidth(valueId)))
                        {
                            return *sized;
                        }
                    }
                }
                return expr;
            }

            std::string extendOperand(grh::ir::ValueId valueId, int64_t targetWidth) const
            {
                if (valueType(valueId) != grh::ir::ValueType::Logic)
                {
                    return valueExpr(valueId);
                }
                const int64_t width = valueWidth(valueId);
                const std::string name = sizedOperandExpr(valueId);
                if (targetWidth <= 0 || width <= 0 || width == targetWidth)
                {
                    return name;
                }
                const int64_t diff = targetWidth - width;
                if (diff == 0)
                {
                    return name;
                }
                if (diff < 0)
                {
                    // Truncation needed: generate explicit slice to avoid WIDTHTRUNC warnings
                    if (targetWidth == 1)
                    {
                        return name + "[0]";
                    }
                    return name + "[" + std::to_string(targetWidth - 1) + ":0]";
                }
                if (valueSigned(valueId))
                {
                    if (allowConstSignBit)
                    {
                        if (auto literal = constLiteralRawFor(valueId))
                        {
                            if (auto signBit = signBitLiteralForConst(*literal, width, valueSigned(valueId)))
                            {
                                return "{{" + std::to_string(diff) + "{" + *signBit + "}}," + name + "}";
                            }
                        }
                    }
                    const std::string indexed = parenIfNeeded(name);
                    const std::string signBit =
                        width == 1 ? indexed
                                   : indexed + "[" + std::to_string(width - 1) + "]";
                    return "{{" + std::to_string(diff) + "{" + signBit + "}}," + name + "}";
                }
                return "{{" + std::to_string(diff) + "{1'b0}}," + name + "}";
            }

            std::string extendShiftOperand(grh::ir::ValueId valueId,
                                           int64_t targetWidth,
                                           bool signExtend) const
            {
                if (valueType(valueId) != grh::ir::ValueType::Logic)
                {
                    return valueExpr(valueId);
                }
                const int64_t width = valueWidth(valueId);
                const std::string name = sizedOperandExpr(valueId);
                if (targetWidth <= 0 || width <= 0 || width == targetWidth)
                {
                    return name;
                }
                const int64_t diff = targetWidth - width;
                if (diff <= 0)
                {
                    return name;
                }
                if (signExtend)
                {
                    if (allowConstSignBit)
                    {
                        if (auto literal = constLiteralRawFor(valueId))
                        {
                            if (auto signBit = signBitLiteralForConst(*literal, width, valueSigned(valueId)))
                            {
                                return "{{" + std::to_string(diff) + "{" + *signBit + "}}," + name + "}";
                            }
                        }
                    }
                    const std::string indexed = parenIfNeeded(name);
                    const std::string signBit =
                        width == 1 ? indexed
                                   : indexed + "[" + std::to_string(width - 1) + "]";
                    return "{{" + std::to_string(diff) + "{" + signBit + "}}," + name + "}";
                }
                return "{{" + std::to_string(diff) + "{1'b0}}," + name + "}";
            }

            std::string concatOperandExpr(grh::ir::ValueId valueId) const
            {
                if (valueType(valueId) != grh::ir::ValueType::Logic)
                {
                    return valueExpr(valueId);
                }
                const std::string expr = valueExpr(valueId);
                if (!expr.empty() && isSimpleIdentifier(expr))
                {
                    return expr;
                }
                if (allowInlineLiteral)
                {
                    if (auto literal = constLiteralRawFor(valueId))
                    {
                        if (auto sized = sizedLiteralIfUnsized(*literal, valueWidth(valueId)))
                        {
                            return *sized;
                        }
                    }
                }
                return expr;
            }

            std::string clampIndexExpr(grh::ir::ValueId indexId, int64_t operandWidth) const
            {
                std::string indexExpr = parenIfNeeded(valueExpr(indexId));
                if (operandWidth <= 1)
                {
                    return indexExpr;
                }
                const int64_t indexWidth = valueWidth(indexId);
                if (indexWidth <= 0)
                {
                    return indexExpr;
                }
                uint64_t temp = static_cast<uint64_t>(operandWidth - 1);
                int64_t clampWidth = 0;
                while (temp > 0)
                {
                    ++clampWidth;
                    temp >>= 1;
                }
                if (clampWidth <= 0 || indexWidth <= clampWidth)
                {
                    return indexExpr;
                }
                std::ostringstream expr;
                expr << clampWidth << "'(" << indexExpr << ")";
                return expr.str();
            }
        };

        template <typename T>
        std::optional<T> getAttribute(const grh::ir::Graph &graph, const grh::ir::Operation &op, std::string_view key)
        {
            (void)graph;
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

        std::string binOpToken(grh::ir::OperationKind kind)
        {
            switch (kind)
            {
            case grh::ir::OperationKind::kAdd:
                return "+";
            case grh::ir::OperationKind::kSub:
                return "-";
            case grh::ir::OperationKind::kMul:
                return "*";
            case grh::ir::OperationKind::kDiv:
                return "/";
            case grh::ir::OperationKind::kMod:
                return "%";
            case grh::ir::OperationKind::kEq:
                return "==";
            case grh::ir::OperationKind::kNe:
                return "!=";
            case grh::ir::OperationKind::kCaseEq:
                return "===";
            case grh::ir::OperationKind::kCaseNe:
                return "!==";
            case grh::ir::OperationKind::kWildcardEq:
                return "==?";
            case grh::ir::OperationKind::kWildcardNe:
                return "!=?";
            case grh::ir::OperationKind::kLt:
                return "<";
            case grh::ir::OperationKind::kLe:
                return "<=";
            case grh::ir::OperationKind::kGt:
                return ">";
            case grh::ir::OperationKind::kGe:
                return ">=";
            case grh::ir::OperationKind::kAnd:
                return "&";
            case grh::ir::OperationKind::kOr:
                return "|";
            case grh::ir::OperationKind::kXor:
                return "^";
            case grh::ir::OperationKind::kXnor:
                return "~^";
            case grh::ir::OperationKind::kLogicAnd:
                return "&&";
            case grh::ir::OperationKind::kLogicOr:
                return "||";
            case grh::ir::OperationKind::kShl:
                return "<<";
            case grh::ir::OperationKind::kLShr:
                return ">>";
            case grh::ir::OperationKind::kAShr:
                return ">>>";
            default:
                return {};
            }
        }

        std::string unaryOpToken(grh::ir::OperationKind kind)
        {
            switch (kind)
            {
            case grh::ir::OperationKind::kNot:
                return "~";
            case grh::ir::OperationKind::kLogicNot:
                return "!";
            case grh::ir::OperationKind::kReduceAnd:
                return "&";
            case grh::ir::OperationKind::kReduceOr:
                return "|";
            case grh::ir::OperationKind::kReduceXor:
                return "^";
            case grh::ir::OperationKind::kReduceNor:
                return "~|";
            case grh::ir::OperationKind::kReduceNand:
                return "~&";
            case grh::ir::OperationKind::kReduceXnor:
                return "~^";
            default:
                return {};
            }
        }

        std::string formatNetDecl(std::string_view type, const std::string &name, const NetDecl &decl)
        {
            std::string out;
            out.append(type);
            out.push_back(' ');
            out.append(signedPrefix(decl.isSigned));
            const std::string range = widthRange(decl.width);
            if (!range.empty())
            {
                out.append(range);
                out.push_back(' ');
            }
            out.append(name);
            out.push_back(';');
            return out;
        }

        std::string formatVarDecl(grh::ir::ValueType type, const std::string &name,
                                   const std::optional<std::string> &initValue = std::nullopt)
        {
            std::string out;
            switch (type)
            {
            case grh::ir::ValueType::Real:
                out.append("real ");
                break;
            case grh::ir::ValueType::String:
                out.append("string ");
                break;
            default:
                out.append("logic ");
                break;
            }
            out.append(name);
            if (initValue.has_value())
            {
                out.append(" = ");
                out.append(*initValue);
            }
            out.push_back(';');
            return out;
        }

        int findNameIndex(const std::vector<std::string> &names, const std::string &target)
        {
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == target)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        std::string sensitivityList(const grh::ir::Graph &graph, const SeqKey &key)
        {
            if (key.events.empty())
            {
                return {};
            }
            std::string out = "@(";
            for (std::size_t i = 0; i < key.events.size(); ++i)
            {
                if (i != 0)
                {
                    out.append(" or ");
                }
                const auto &event = key.events[i];
                out.append(event.edge);
                out.push_back(' ');
                out.append(graph.getValue(event.signal).symbolText());
            }
            out.push_back(')');
            return out;
        }
    } // namespace

    EmitResult EmitSystemVerilog::emitImpl(const grh::ir::Netlist &netlist,
                                           std::span<const grh::ir::Graph *const> topGraphs,
                                           const EmitOptions &options)
    {
        EmitResult result;
        (void)topGraphs;

        // Index DPI imports across the netlist for later resolution.
        struct DpiImportRef
        {
            const grh::ir::Graph *graph = nullptr;
            grh::ir::OperationId op = grh::ir::OperationId::invalid();
        };
        std::unordered_map<std::string, DpiImportRef> dpicImports;
        for (const auto &graphSymbol : netlist.graphOrder())
        {
            auto graphIt = netlist.graphs().find(graphSymbol);
            if (graphIt == netlist.graphs().end() || !graphIt->second)
            {
                continue;
            }
            const grh::ir::Graph &graph = *graphIt->second;
            for (const auto opId : graph.operations())
            {
                const grh::ir::Operation op = graph.getOperation(opId);
                if (op.kind() == grh::ir::OperationKind::kDpicImport)
                {
                    dpicImports.emplace(std::string(op.symbolText()), DpiImportRef{&graph, opId});
                }
            }
        }

        std::unordered_map<std::string, std::string> emittedModuleNames;
        std::unordered_set<std::string> usedModuleNames;
        for (const auto &graphSymbol : netlist.graphOrder())
        {
            const grh::ir::Graph *graph = netlist.findGraph(graphSymbol);
            if (!graph)
            {
                continue;
            }
            std::string emittedName = graphSymbol;
            auto aliases = netlist.aliasesForGraph(graphSymbol);
            for (const auto &alias : aliases)
            {
                if (usedModuleNames.find(alias) == usedModuleNames.end())
                {
                    emittedName = alias;
                    break;
                }
            }
            usedModuleNames.insert(emittedName);
            emittedModuleNames.emplace(graphSymbol, std::move(emittedName));
        }

        const std::string filename = options.outputFilename.value_or(std::string("grh.sv"));
        const std::filesystem::path outputPath = resolveOutputDir(options) / filename;
        auto stream = openOutputFile(outputPath);
        if (!stream)
        {
            result.success = false;
            return result;
        }
        std::ostream &out = *stream;
        bool firstModule = true;
        for (const grh::ir::Graph *graph : graphsSortedByName(netlist))
        {
            if (!firstModule)
            {
                out << '\n';
            }
            firstModule = false;

            const auto moduleNameIt = emittedModuleNames.find(graph->symbol());
            const std::string &moduleName = moduleNameIt != emittedModuleNames.end() ? moduleNameIt->second : graph->symbol();

            uint32_t maxValueIndex = 0;
            for (const auto valueId : graph->values())
            {
                maxValueIndex = std::max(maxValueIndex, valueId.index);
            }
            uint32_t maxOpIndex = 0;
            for (const auto opId : graph->operations())
            {
                maxOpIndex = std::max(maxOpIndex, opId.index);
            }
            std::unordered_set<std::string> usedValueNames;
            usedValueNames.reserve(static_cast<std::size_t>(maxValueIndex) + 8);
            for (const auto valueId : graph->values())
            {
                if (!valueId.valid() || valueId.graph != graph->id())
                {
                    continue;
                }
                const grh::ir::Value value = graph->getValue(valueId);
                const std::string name = std::string(value.symbolText());
                if (!name.empty())
                {
                    usedValueNames.insert(name);
                }
            }
            for (const auto &port : graph->inputPorts())
            {
                if (port.name.valid())
                {
                    usedValueNames.insert(std::string(graph->symbolText(port.name)));
                }
            }
            for (const auto &port : graph->outputPorts())
            {
                if (port.name.valid())
                {
                    usedValueNames.insert(std::string(graph->symbolText(port.name)));
                }
            }
            for (const auto &port : graph->inoutPorts())
            {
                if (port.name.valid())
                {
                    usedValueNames.insert(std::string(graph->symbolText(port.name)));
                }
            }
            auto makeUniqueValueName = [&](uint32_t idx) -> std::string
            {
                std::string base = "__wolf_v" + std::to_string(idx);
                std::string name = base;
                int suffix = 0;
                while (usedValueNames.find(name) != usedValueNames.end())
                {
                    ++suffix;
                    name = base + "_" + std::to_string(suffix);
                }
                usedValueNames.insert(name);
                return name;
            };
            std::vector<std::string> valueNameCache(maxValueIndex + 1);
            std::vector<std::string> opNameCache(maxOpIndex + 1);
            const std::string emptyName;
            auto valueName = [&](grh::ir::ValueId valueId) -> const std::string &
            {
                if (!valueId.valid() || valueId.graph != graph->id())
                {
                    return emptyName;
                }
                const uint32_t idx = valueId.index;
                if (idx == 0 || idx > maxValueIndex)
                {
                    return emptyName;
                }
                std::string &slot = valueNameCache[idx];
                if (slot.empty())
                {
                    grh::ir::Value value = graph->getValue(valueId);
                    slot = std::string(value.symbolText());
                    if (slot.empty())
                    {
                        slot = makeUniqueValueName(idx);
                    }
                }
                return slot;
            };
            auto opName = [&](grh::ir::OperationId opId) -> const std::string &
            {
                if (!opId.valid() || opId.graph != graph->id())
                {
                    return emptyName;
                }
                const uint32_t idx = opId.index;
                if (idx == 0 || idx > maxOpIndex)
                {
                    return emptyName;
                }
                std::string &slot = opNameCache[idx];
                if (slot.empty())
                {
                    grh::ir::Operation op = graph->getOperation(opId);
                    slot = std::string(op.symbolText());
                }
                return slot;
            };
            auto isPortValue = [&](grh::ir::ValueId valueId) -> bool
            {
                const grh::ir::Value value = graph->getValue(valueId);
                return value.isInput() || value.isOutput() || value.isInout();
            };
            auto valueType = [&](grh::ir::ValueId valueId) -> grh::ir::ValueType
            {
                return graph->getValue(valueId).type();
            };
            auto valueWidth = [&](grh::ir::ValueId valueId) -> int64_t
            {
                return graph->getValue(valueId).width();
            };
            auto valueSigned = [&](grh::ir::ValueId valueId) -> bool
            {
                return graph->getValue(valueId).isSigned();
            };
            auto formatConstLiteral = [&](grh::ir::ValueId valueId,
                                          std::string_view literal) -> std::string
            {
                if (valueType(valueId) == grh::ir::ValueType::String)
                {
                    if (literal.size() >= 2 && literal.front() == '"' && literal.back() == '"')
                    {
                        return std::string(literal);
                    }
                    return "\"" + escapeSvString(literal) + "\"";
                }
                return std::string(literal);
            };
            auto constLiteralRawFor = [&](grh::ir::ValueId valueId) -> std::optional<std::string>
            {
                if (!valueId.valid())
                {
                    return std::nullopt;
                }
                const grh::ir::Value value = graph->getValue(valueId);
                const grh::ir::OperationId defOpId = value.definingOp();
                if (!defOpId.valid())
                {
                    return std::nullopt;
                }
                const grh::ir::Operation defOp = graph->getOperation(defOpId);
                if (defOp.kind() != grh::ir::OperationKind::kConstant)
                {
                    return std::nullopt;
                }
                return getAttribute<std::string>(*graph, defOp, "constValue");
            };
            auto constLiteralFor = [&](grh::ir::ValueId valueId) -> std::optional<std::string>
            {
                auto raw = constLiteralRawFor(valueId);
                if (!raw)
                {
                    return std::nullopt;
                }
                return formatConstLiteral(valueId, *raw);
            };
            std::function<std::string(grh::ir::ValueId)> valueExpr =
                [&](grh::ir::ValueId valueId) -> std::string
            {
                if (!valueId.valid())
                {
                    return {};
                }
                return valueName(valueId);
            };
            ExprTools baseExpr;
            baseExpr.valueType = valueType;
            baseExpr.valueWidth = valueWidth;
            baseExpr.valueSigned = valueSigned;
            baseExpr.constLiteralRawFor = constLiteralRawFor;
            baseExpr.valueExpr = valueExpr;
            baseExpr.parenReduce = false;
            baseExpr.allowConstSignBit = true;
            baseExpr.allowInlineLiteral = false;

            // -------------------------
            // Ports
            // -------------------------
            std::map<std::string, PortDecl, std::less<>> portDecls;
            for (const auto &port : graph->inputPorts())
            {
                if (!port.name.valid())
                {
                    continue;
                }
                const std::string name = std::string(graph->symbolText(port.name));
                grh::ir::Value value = graph->getValue(port.value);
                portDecls[name] = PortDecl{PortDir::Input, value.width(), value.isSigned(), false,
                                           value.type(), value.srcLoc()};
            }
            for (const auto &port : graph->outputPorts())
            {
                if (!port.name.valid())
                {
                    continue;
                }
                const std::string name = std::string(graph->symbolText(port.name));
                grh::ir::Value value = graph->getValue(port.value);
                portDecls[name] = PortDecl{PortDir::Output, value.width(), value.isSigned(), false,
                                           value.type(), value.srcLoc()};
            }
            for (const auto &port : graph->inoutPorts())
            {
                if (!port.name.valid())
                {
                    continue;
                }
                const std::string name = std::string(graph->symbolText(port.name));
                grh::ir::Value value = graph->getValue(port.out);
                portDecls[name] = PortDecl{PortDir::Inout, value.width(), value.isSigned(), false,
                                           value.type(), value.srcLoc()};
            }

            // Book-keeping for declarations.
            std::unordered_set<std::string> declaredNames;
            for (const auto &[name, _] : portDecls)
            {
                declaredNames.insert(name);
            }

            // Storage for various sections.
            std::map<std::string, VarDecl, std::less<>> varDecls;
            std::map<std::string, NetDecl, std::less<>> regDecls;
            std::map<std::string, NetDecl, std::less<>> wireDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> memoryDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> instanceDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> dpiImportDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> assignStmts;
            std::vector<std::pair<std::string, grh::ir::OperationId>> portBindingStmts;
            std::vector<std::pair<std::string, grh::ir::OperationId>> latchBlocks;
            std::vector<SimpleBlock> simpleBlocks;
            std::vector<SeqBlock> seqBlocks;
            std::unordered_set<std::string> instanceNamesUsed;
            std::unordered_map<grh::ir::ValueId, std::string, grh::ir::ValueIdHash> dpiTempNames;

            for (const auto opId : graph->operations())
            {
                const grh::ir::Operation op = graph->getOperation(opId);
                if (op.kind() != grh::ir::OperationKind::kDpicCall)
                {
                    continue;
                }
                for (const auto res : op.results())
                {
                    if (!res.valid())
                    {
                        continue;
                    }
                    const std::string name = valueName(res);
                    if (!name.empty())
                    {
                        dpiTempNames.emplace(res, name + "_intm");
                    }
                }
            }

            auto ensureVarDecl = [&](const std::string &name,
                                     grh::ir::ValueType type,
                                     const std::optional<grh::ir::SrcLoc> &debug = std::nullopt,
                                     const std::optional<std::string> &initValue = std::nullopt)
            {
                if (declaredNames.find(name) != declaredNames.end())
                {
                    auto it = varDecls.find(name);
                    if (it != varDecls.end())
                    {
                        if (debug && !it->second.debug)
                        {
                            it->second.debug = *debug;
                        }
                        if (initValue && !it->second.initValue)
                        {
                            it->second.initValue = *initValue;
                        }
                    }
                    return;
                }
                varDecls.emplace(name, VarDecl{type, debug, initValue});
                declaredNames.insert(name);
            };

            auto ensureRegDecl = [&](const std::string &name, int64_t width, bool isSigned,
                                     grh::ir::ValueType valueType,
                                     const std::optional<grh::ir::SrcLoc> &debug = std::nullopt)
            {
                if (valueType != grh::ir::ValueType::Logic)
                {
                    ensureVarDecl(name, valueType, debug);
                    return;
                }
                if (declaredNames.find(name) != declaredNames.end())
                {
                    auto it = regDecls.find(name);
                    if (it != regDecls.end() && debug && !it->second.debug)
                    {
                        it->second.debug = *debug;
                    }
                    return;
                }
                regDecls.emplace(name, NetDecl{width, isSigned, debug});
                declaredNames.insert(name);
            };

            auto ensureWireDecl = [&](grh::ir::ValueId valueId)
            {
                grh::ir::Value value = graph->getValue(valueId);
                const std::string name = std::string(value.symbolText());
                if (value.type() != grh::ir::ValueType::Logic)
                {
                    ensureVarDecl(name, value.type(), value.srcLoc());
                    return;
                }
                if (declaredNames.find(name) != declaredNames.end())
                {
                    auto it = wireDecls.find(name);
                    if (it != wireDecls.end() && value.srcLoc() && !it->second.debug)
                    {
                        it->second.debug = *value.srcLoc();
                    }
                    return;
                }
                wireDecls.emplace(name, NetDecl{value.width(), value.isSigned(), value.srcLoc()});
                declaredNames.insert(name);
            };

            auto addAssign = [&](std::string stmt, grh::ir::OperationId sourceOp)
            {
                assignStmts.emplace_back(std::move(stmt), sourceOp);
            };
            auto addSimpleStmt = [&](std::string header, std::string stmt,
                                     grh::ir::OperationId sourceOp)
            {
                for (auto &block : simpleBlocks)
                {
                    if (block.header == header)
                    {
                        block.stmts.push_back(std::move(stmt));
                        return;
                    }
                }
                simpleBlocks.push_back(
                    SimpleBlock{std::move(header), std::vector<std::string>{std::move(stmt)}, sourceOp});
            };
            auto addCombAssign = [&](const std::string &lhs, const std::string &rhs,
                                     grh::ir::OperationId sourceOp)
            {
                std::ostringstream stmt;
                appendIndented(stmt, 2, lhs + " = " + rhs + ";");
                addSimpleStmt("always_comb", stmt.str(), sourceOp);
            };
            auto addValueAssign = [&](grh::ir::ValueId result,
                                      const std::string &rhs,
                                      grh::ir::OperationId sourceOp)
            {
                if (valueType(result) != grh::ir::ValueType::Logic)
                {
                    // For String constants, use declaration-time initialization
                    // instead of always_comb to ensure value is available in initial blocks
                    if (valueType(result) == grh::ir::ValueType::String)
                    {
                        ensureVarDecl(valueName(result), valueType(result), 
                                      graph->getValue(result).srcLoc(), rhs);
                    }
                    else
                    {
                        addCombAssign(valueName(result), rhs, sourceOp);
                    }
                }
                else
                {
                    addAssign("assign " + valueName(result) + " = " + rhs + ";", sourceOp);
                }
            };

            auto sameSeqKey = [](const SeqKey &lhs, const SeqKey &rhs) -> bool
            {
                if (lhs.events.size() != rhs.events.size())
                {
                    return false;
                }
                for (std::size_t i = 0; i < lhs.events.size(); ++i)
                {
                    if (lhs.events[i].edge != rhs.events[i].edge ||
                        lhs.events[i].signal != rhs.events[i].signal)
                    {
                        return false;
                    }
                }
                return true;
            };
            auto addSequentialStmt = [&](const SeqKey &key, std::string stmt,
                                         grh::ir::OperationId sourceOp)
            {
                for (auto &block : seqBlocks)
                {
                    if (sameSeqKey(block.key, key))
                    {
                        block.stmts.push_back(std::move(stmt));
                        return;
                    }
                }
                seqBlocks.push_back(SeqBlock{key, std::vector<std::string>{std::move(stmt)}, sourceOp});
            };

            auto addLatchBlock = [&](std::string stmt, grh::ir::OperationId sourceOp)
            {
                latchBlocks.emplace_back(std::move(stmt), sourceOp);
            };
            const uint32_t dpiMaxValueIndex = maxValueIndex;
            std::vector<int8_t> dpiDependsDense(dpiMaxValueIndex + 1, -1);
            std::vector<uint8_t> dpiDependsVisiting(dpiMaxValueIndex + 1, 0);
            std::vector<uint8_t> dpiTempDense(dpiMaxValueIndex + 1, 0);
            for (const auto &entry : dpiTempNames)
            {
                const uint32_t idx = entry.first.index;
                if (idx <= dpiMaxValueIndex)
                {
                    dpiTempDense[idx] = 1;
                }
            }
            auto computeDpiDepends = [&](auto&& self, grh::ir::ValueId valueId) -> bool
            {
                if (!valueId.valid())
                {
                    return false;
                }
                if (valueId.graph != graph->id())
                {
                    return false;
                }
                const uint32_t idx = valueId.index;
                if (idx == 0 || idx > dpiMaxValueIndex)
                {
                    return false;
                }
                if (dpiTempDense[idx])
                {
                    return true;
                }
                const int8_t cached = dpiDependsDense[idx];
                if (cached >= 0)
                {
                    return cached != 0;
                }
                if (dpiDependsVisiting[idx])
                {
                    return false;
                }
                dpiDependsVisiting[idx] = 1;
                const grh::ir::Value value = graph->getValue(valueId);
                const grh::ir::OperationId defOpId = value.definingOp();
                if (!defOpId.valid())
                {
                    dpiDependsVisiting[idx] = 0;
                    dpiDependsDense[idx] = 0;
                    return false;
                }
                const grh::ir::Operation defOp = graph->getOperation(defOpId);
                const auto &ops = defOp.operands();
                auto dependsForOperands = [&]() -> bool {
                    for (const auto operand : ops)
                    {
                        if (self(self, operand))
                        {
                            return true;
                        }
                    }
                    return false;
                };
                bool depends = false;
                switch (defOp.kind())
                {
                case grh::ir::OperationKind::kConstant:
                case grh::ir::OperationKind::kEq:
                case grh::ir::OperationKind::kNe:
                case grh::ir::OperationKind::kCaseEq:
                case grh::ir::OperationKind::kCaseNe:
                case grh::ir::OperationKind::kWildcardEq:
                case grh::ir::OperationKind::kWildcardNe:
                case grh::ir::OperationKind::kLt:
                case grh::ir::OperationKind::kLe:
                case grh::ir::OperationKind::kGt:
                case grh::ir::OperationKind::kGe:
                case grh::ir::OperationKind::kAnd:
                case grh::ir::OperationKind::kOr:
                case grh::ir::OperationKind::kXor:
                case grh::ir::OperationKind::kXnor:
                case grh::ir::OperationKind::kShl:
                case grh::ir::OperationKind::kLShr:
                case grh::ir::OperationKind::kAShr:
                case grh::ir::OperationKind::kAdd:
                case grh::ir::OperationKind::kSub:
                case grh::ir::OperationKind::kMul:
                case grh::ir::OperationKind::kDiv:
                case grh::ir::OperationKind::kMod:
                case grh::ir::OperationKind::kLogicAnd:
                case grh::ir::OperationKind::kLogicOr:
                case grh::ir::OperationKind::kNot:
                case grh::ir::OperationKind::kReduceAnd:
                case grh::ir::OperationKind::kReduceOr:
                case grh::ir::OperationKind::kReduceXor:
                case grh::ir::OperationKind::kReduceNor:
                case grh::ir::OperationKind::kReduceNand:
                case grh::ir::OperationKind::kReduceXnor:
                case grh::ir::OperationKind::kLogicNot:
                case grh::ir::OperationKind::kMux:
                case grh::ir::OperationKind::kAssign:
                case grh::ir::OperationKind::kConcat:
                case grh::ir::OperationKind::kReplicate:
                case grh::ir::OperationKind::kSliceStatic:
                case grh::ir::OperationKind::kSliceDynamic:
                case grh::ir::OperationKind::kSliceArray:
                    depends = dependsForOperands();
                    break;
                default:
                    depends = false;
                    break;
                }
                dpiDependsVisiting[idx] = 0;
                dpiDependsDense[idx] = depends ? 1 : 0;
                return depends;
            };
            for (const auto valueId : graph->values())
            {
                computeDpiDepends(computeDpiDepends, valueId);
            }
            auto valueDependsOnDpi = [&](grh::ir::ValueId valueId) -> bool
            {
                if (!valueId.valid())
                {
                    return false;
                }
                if (valueId.graph != graph->id())
                {
                    return false;
                }
                const uint32_t idx = valueId.index;
                if (idx == 0 || idx > dpiMaxValueIndex)
                {
                    return false;
                }
                return dpiDependsDense[idx] > 0;
            };
            std::unordered_set<grh::ir::ValueId, grh::ir::ValueIdHash> dpiInlineResolving;
            std::unordered_map<grh::ir::ValueId, std::string, grh::ir::ValueIdHash> dpiInlineCache;
            std::function<std::string(grh::ir::ValueId)> dpiInlineExpr;
            ExprTools inlineExpr = baseExpr;
            inlineExpr.parenReduce = true;
            inlineExpr.allowConstSignBit = false;
            inlineExpr.allowInlineLiteral = true;
            inlineExpr.valueExpr = [&](grh::ir::ValueId valueId) -> std::string
            {
                if (valueDependsOnDpi(valueId))
                {
                    return dpiInlineExpr(valueId);
                }
                return valueExpr(valueId);
            };
            dpiInlineExpr = [&](grh::ir::ValueId valueId) -> std::string
            {
                if (!valueId.valid())
                {
                    return {};
                }
                if (auto cached = dpiInlineCache.find(valueId); cached != dpiInlineCache.end())
                {
                    return cached->second;
                }
                if (auto it = dpiTempNames.find(valueId); it != dpiTempNames.end())
                {
                    return it->second;
                }
                if (!valueDependsOnDpi(valueId))
                {
                    return valueExpr(valueId);
                }
                if (!dpiInlineResolving.insert(valueId).second)
                {
                    return valueExpr(valueId);
                }
                const grh::ir::Value value = graph->getValue(valueId);
                const grh::ir::OperationId defOpId = value.definingOp();
                if (!defOpId.valid())
                {
                    dpiInlineResolving.erase(valueId);
                    return valueExpr(valueId);
                }
                const grh::ir::Operation defOp = graph->getOperation(defOpId);
                const auto &ops = defOp.operands();
                std::string expr = valueExpr(valueId);
                switch (defOp.kind())
                {
                case grh::ir::OperationKind::kConstant:
                    if (auto literal = constLiteralFor(valueId))
                    {
                        expr = *literal;
                    }
                    break;
                case grh::ir::OperationKind::kEq:
                case grh::ir::OperationKind::kNe:
                case grh::ir::OperationKind::kCaseEq:
                case grh::ir::OperationKind::kCaseNe:
                case grh::ir::OperationKind::kWildcardEq:
                case grh::ir::OperationKind::kWildcardNe:
                case grh::ir::OperationKind::kLt:
                case grh::ir::OperationKind::kLe:
                case grh::ir::OperationKind::kGt:
                case grh::ir::OperationKind::kGe:
                {
                    if (ops.size() >= 2)
                    {
                        int64_t resultWidth =
                            std::max(graph->getValue(ops[0]).width(),
                                     graph->getValue(ops[1]).width());
                        const std::string tok = binOpToken(defOp.kind());
                        const std::string lhs = resultWidth > 0
                                                    ? inlineExpr.extendOperand(ops[0], resultWidth)
                                                    : inlineExpr.valueExpr(ops[0]);
                        const std::string rhs = resultWidth > 0
                                                    ? inlineExpr.extendOperand(ops[1], resultWidth)
                                                    : inlineExpr.valueExpr(ops[1]);
                        const bool signedCompare =
                            (defOp.kind() == grh::ir::OperationKind::kLt ||
                             defOp.kind() == grh::ir::OperationKind::kLe ||
                             defOp.kind() == grh::ir::OperationKind::kGt ||
                             defOp.kind() == grh::ir::OperationKind::kGe) &&
                            graph->getValue(ops[0]).isSigned() &&
                            graph->getValue(ops[1]).isSigned();
                        const std::string lhsExpr = signedCompare ? "$signed(" + lhs + ")" : lhs;
                        const std::string rhsExpr = signedCompare ? "$signed(" + rhs + ")" : rhs;
                        expr = lhsExpr + " " + tok + " " + rhsExpr;
                    }
                    break;
                }
                case grh::ir::OperationKind::kAnd:
                case grh::ir::OperationKind::kOr:
                case grh::ir::OperationKind::kXor:
                case grh::ir::OperationKind::kXnor:
                    if (ops.size() >= 2)
                    {
                        const std::string tok = binOpToken(defOp.kind());
                        expr = inlineExpr.valueExpr(ops[0]) + " " + tok + " " + inlineExpr.valueExpr(ops[1]);
                    }
                    break;
                case grh::ir::OperationKind::kShl:
                case grh::ir::OperationKind::kLShr:
                case grh::ir::OperationKind::kAShr:
                    if (ops.size() >= 2)
                    {
                        const int64_t resultWidth = graph->getValue(valueId).width();
                        const bool signExtend =
                            defOp.kind() == grh::ir::OperationKind::kAShr &&
                            graph->getValue(ops[0]).isSigned();
                        const std::string tok = binOpToken(defOp.kind());
                        const std::string lhs = resultWidth > 0
                                                    ? inlineExpr.extendShiftOperand(ops[0], resultWidth, signExtend)
                                                    : inlineExpr.valueExpr(ops[0]);
                        expr = lhs + " " + tok + " " + inlineExpr.valueExpr(ops[1]);
                    }
                    break;
                case grh::ir::OperationKind::kAdd:
                case grh::ir::OperationKind::kSub:
                case grh::ir::OperationKind::kMul:
                case grh::ir::OperationKind::kDiv:
                case grh::ir::OperationKind::kMod:
                    if (ops.size() >= 2)
                    {
                        int64_t resultWidth = graph->getValue(valueId).width();
                        if (resultWidth <= 0)
                        {
                            resultWidth = std::max(graph->getValue(ops[0]).width(),
                                                   graph->getValue(ops[1]).width());
                        }
                        const std::string tok = binOpToken(defOp.kind());
                        expr = inlineExpr.extendOperand(ops[0], resultWidth) + " " + tok + " " +
                               inlineExpr.extendOperand(ops[1], resultWidth);
                    }
                    break;
                case grh::ir::OperationKind::kLogicAnd:
                case grh::ir::OperationKind::kLogicOr:
                    if (ops.size() >= 2)
                    {
                        const std::string tok = binOpToken(defOp.kind());
                        expr = inlineExpr.logicalOperand(ops[0]) + " " + tok + " " +
                               inlineExpr.logicalOperand(ops[1]);
                    }
                    break;
                case grh::ir::OperationKind::kNot:
                case grh::ir::OperationKind::kReduceAnd:
                case grh::ir::OperationKind::kReduceOr:
                case grh::ir::OperationKind::kReduceXor:
                case grh::ir::OperationKind::kReduceNor:
                case grh::ir::OperationKind::kReduceNand:
                case grh::ir::OperationKind::kReduceXnor:
                    if (!ops.empty())
                    {
                        const std::string tok = unaryOpToken(defOp.kind());
                        expr = tok + inlineExpr.valueExpr(ops[0]);
                    }
                    break;
                case grh::ir::OperationKind::kLogicNot:
                    if (!ops.empty())
                    {
                        expr = "!" + inlineExpr.logicalOperand(ops[0]);
                    }
                    break;
                case grh::ir::OperationKind::kMux:
                    if (ops.size() >= 3)
                    {
                        const int64_t resultWidth = graph->getValue(valueId).width();
                        const std::string lhs = inlineExpr.extendOperand(ops[1], resultWidth);
                        const std::string rhs = inlineExpr.extendOperand(ops[2], resultWidth);
                        expr = inlineExpr.valueExpr(ops[0]) + " ? " + lhs + " : " + rhs;
                    }
                    break;
                case grh::ir::OperationKind::kAssign:
                    if (!ops.empty())
                    {
                        int64_t resultWidth = graph->getValue(valueId).width();
                        expr = inlineExpr.extendOperand(ops[0], resultWidth);
                    }
                    break;
                case grh::ir::OperationKind::kConcat:
                    if (!ops.empty())
                    {
                        if (ops.size() == 1)
                        {
                            expr = inlineExpr.valueExpr(ops[0]);
                        }
                        else if (ops.size() <= 4)
                        {
                            // Short concat: single line
                            std::ostringstream exprStream;
                            exprStream << "{";
                            for (std::size_t i = 0; i < ops.size(); ++i)
                            {
                                if (i != 0)
                                {
                                    exprStream << ", ";
                                }
                                exprStream << inlineExpr.concatOperandExpr(ops[i]);
                            }
                            exprStream << "}";
                            expr = exprStream.str();
                        }
                        else
                        {
                            // Long concat: multi-line format
                            std::ostringstream exprStream;
                            exprStream << "{\n";
                            for (std::size_t i = 0; i < ops.size(); ++i)
                            {
                                exprStream << "    " << inlineExpr.concatOperandExpr(ops[i]);
                                if (i + 1 < ops.size())
                                {
                                    exprStream << ",";
                                }
                                exprStream << "\n";
                            }
                            exprStream << "  }";
                            expr = exprStream.str();
                        }
                    }
                    break;
                case grh::ir::OperationKind::kReplicate:
                {
                    if (!ops.empty())
                    {
                        auto rep = getAttribute<int64_t>(*graph, defOp, "rep");
                        if (rep)
                        {
                            std::ostringstream exprStream;
                            exprStream << "{" << *rep << "{" << inlineExpr.concatOperandExpr(ops[0]) << "}}";
                            expr = exprStream.str();
                        }
                    }
                    break;
                }
                case grh::ir::OperationKind::kSliceStatic:
                {
                    if (!ops.empty())
                    {
                        auto sliceStart = getAttribute<int64_t>(*graph, defOp, "sliceStart");
                        auto sliceEnd = getAttribute<int64_t>(*graph, defOp, "sliceEnd");
                        if (sliceStart && sliceEnd)
                        {
                            const int64_t operandWidth = graph->getValue(ops[0]).width();
                            std::ostringstream exprStream;
                            if (operandWidth == 1)
                            {
                                exprStream << inlineExpr.valueExpr(ops[0]);
                            }
                            else
                            {
                                exprStream << parenIfNeeded(inlineExpr.valueExpr(ops[0])) << "[";
                                if (*sliceStart == *sliceEnd)
                                {
                                    exprStream << *sliceStart;
                                }
                                else
                                {
                                    exprStream << *sliceEnd << ":" << *sliceStart;
                                }
                                exprStream << "]";
                            }
                            expr = exprStream.str();
                        }
                    }
                    break;
                }
                case grh::ir::OperationKind::kSliceDynamic:
                {
                    if (ops.size() >= 2)
                    {
                        auto width = getAttribute<int64_t>(*graph, defOp, "sliceWidth");
                        if (width)
                        {
                            const int64_t operandWidth = graph->getValue(ops[0]).width();
                            std::ostringstream exprStream;
                            if (operandWidth == 1)
                            {
                                exprStream << inlineExpr.valueExpr(ops[0]);
                            }
                            else if (*width == 1)
                            {
                                exprStream << parenIfNeeded(inlineExpr.valueExpr(ops[0])) << "[" <<
                                    parenIfNeeded(inlineExpr.valueExpr(ops[1])) << "]";
                            }
                            else
                            {
                                exprStream << parenIfNeeded(inlineExpr.valueExpr(ops[0])) << "[" <<
                                    parenIfNeeded(inlineExpr.valueExpr(ops[1])) << " +: " << *width << "]";
                            }
                            expr = exprStream.str();
                        }
                    }
                    break;
                }
                case grh::ir::OperationKind::kSliceArray:
                {
                    if (ops.size() >= 2)
                    {
                        auto width = getAttribute<int64_t>(*graph, defOp, "sliceWidth");
                        if (width)
                        {
                            const int64_t operandWidth = graph->getValue(ops[0]).width();
                            std::ostringstream exprStream;
                            if (*width == 1 && operandWidth == 1)
                            {
                                exprStream << inlineExpr.valueExpr(ops[0]);
                            }
                            else
                            {
                                exprStream << parenIfNeeded(inlineExpr.valueExpr(ops[0])) << "[";
                                if (*width == 1)
                                {
                                    exprStream << parenIfNeeded(inlineExpr.valueExpr(ops[1]));
                                }
                                else
                                {
                                    exprStream << parenIfNeeded(inlineExpr.valueExpr(ops[1])) << " * " << *width << " +: " << *width;
                                }
                                exprStream << "]";
                            }
                            expr = exprStream.str();
                        }
                    }
                    break;
                }
                default:
                    break;
                }
                dpiInlineResolving.erase(valueId);
                dpiInlineCache.emplace(valueId, expr);
                return expr;
            };
            auto valueExprSeq = [&](grh::ir::ValueId valueId) -> std::string
            {
                if (valueDependsOnDpi(valueId))
                {
                    return dpiInlineExpr(valueId);
                }
                return valueExpr(valueId);
            };
            auto isConstOne = [&](grh::ir::ValueId valueId) -> bool
            {
                if (!valueId.valid())
                {
                    return false;
                }
                const grh::ir::Value value = graph->getValue(valueId);
                const grh::ir::OperationId defOpId = value.definingOp();
                if (!defOpId.valid())
                {
                    return false;
                }
                const grh::ir::Operation defOp = graph->getOperation(defOpId);
                if (defOp.kind() != grh::ir::OperationKind::kConstant)
                {
                    return false;
                }
                auto constValue = getAttribute<std::string>(*graph, defOp, "constValue");
                if (!constValue)
                {
                    return false;
                }
                std::string text = *constValue;
                text.erase(std::remove_if(text.begin(), text.end(),
                                          [](unsigned char ch)
                                          { return std::isspace(ch) || ch == '_'; }),
                           text.end());
                if (text == "1")
                {
                    return true;
                }
                if (text.size() >= 3 && text.back() == '1')
                {
                    const std::size_t quote = text.find('\'');
                    if (quote != std::string::npos && quote + 1 < text.size())
                    {
                        return true;
                    }
                }
                return false;
            };
            auto isConstZero = [&](grh::ir::ValueId valueId) -> bool
            {
                if (!valueId.valid())
                {
                    return false;
                }
                const grh::ir::Value value = graph->getValue(valueId);
                const grh::ir::OperationId defOpId = value.definingOp();
                if (!defOpId.valid())
                {
                    return false;
                }
                const grh::ir::Operation defOp = graph->getOperation(defOpId);
                if (defOp.kind() != grh::ir::OperationKind::kConstant)
                {
                    return false;
                }
                auto constValue = getAttribute<std::string>(*graph, defOp, "constValue");
                if (!constValue)
                {
                    return false;
                }
                std::string text = *constValue;
                text.erase(std::remove_if(text.begin(), text.end(),
                                          [](unsigned char ch)
                                          { return std::isspace(ch) || ch == '_'; }),
                           text.end());
                if (text == "0")
                {
                    return true;
                }
                if (text.size() >= 3 && text.back() == '0')
                {
                    const std::size_t quote = text.find('\'');
                    if (quote != std::string::npos && quote + 1 < text.size())
                    {
                        return true;
                    }
                }
                return false;
            };
            auto isLogicNotOf = [&](grh::ir::ValueId maybeNot,
                                    grh::ir::ValueId operand) -> bool
            {
                if (!maybeNot.valid() || !operand.valid())
                {
                    return false;
                }
                const grh::ir::OperationId defOpId =
                    graph->getValue(maybeNot).definingOp();
                if (!defOpId.valid())
                {
                    return false;
                }
                const grh::ir::Operation defOp = graph->getOperation(defOpId);
                if (defOp.kind() != grh::ir::OperationKind::kLogicNot &&
                    defOp.kind() != grh::ir::OperationKind::kNot)
                {
                    return false;
                }
                const auto &ops = defOp.operands();
                return !ops.empty() && ops.front() == operand;
            };
            auto isAlwaysTrueByTruthTable = [&](grh::ir::ValueId valueId) -> bool
            {
                if (!valueId.valid())
                {
                    return false;
                }
                std::vector<grh::ir::ValueId> leaves;
                std::unordered_map<grh::ir::ValueId, std::size_t, grh::ir::ValueIdHash> leafIndex;

                std::function<void(grh::ir::ValueId)> collectLeaves =
                    [&](grh::ir::ValueId id) {
                        if (!id.valid())
                        {
                            return;
                        }
                        if (isConstOne(id) || isConstZero(id))
                        {
                            return;
                        }
                        const grh::ir::OperationId defId =
                            graph->getValue(id).definingOp();
                        if (!defId.valid())
                        {
                            if (leafIndex.find(id) == leafIndex.end())
                            {
                                leafIndex[id] = leaves.size();
                                leaves.push_back(id);
                            }
                            return;
                        }
                        const grh::ir::Operation op = graph->getOperation(defId);
                        if (op.kind() == grh::ir::OperationKind::kLogicAnd ||
                            op.kind() == grh::ir::OperationKind::kLogicOr ||
                            op.kind() == grh::ir::OperationKind::kLogicNot ||
                            op.kind() == grh::ir::OperationKind::kNot)
                        {
                            for (const auto operand : op.operands())
                            {
                                collectLeaves(operand);
                            }
                            return;
                        }
                        if (leafIndex.find(id) == leafIndex.end())
                        {
                            leafIndex[id] = leaves.size();
                            leaves.push_back(id);
                        }
                    };

                collectLeaves(valueId);
                if (leaves.size() > 8)
                {
                    return false;
                }

                auto evalExpr = [&](grh::ir::ValueId id,
                                    const std::vector<bool>& assignment,
                                    auto&& self) -> bool {
                    if (!id.valid())
                    {
                        return false;
                    }
                    if (isConstOne(id))
                    {
                        return true;
                    }
                    if (isConstZero(id))
                    {
                        return false;
                    }
                    const grh::ir::OperationId defId =
                        graph->getValue(id).definingOp();
                    if (!defId.valid())
                    {
                        auto it = leafIndex.find(id);
                        return it != leafIndex.end() && assignment[it->second];
                    }
                    const grh::ir::Operation op = graph->getOperation(defId);
                    if (op.kind() == grh::ir::OperationKind::kLogicAnd)
                    {
                        for (const auto operand : op.operands())
                        {
                            if (!self(operand, assignment, self))
                            {
                                return false;
                            }
                        }
                        return true;
                    }
                    if (op.kind() == grh::ir::OperationKind::kLogicOr)
                    {
                        for (const auto operand : op.operands())
                        {
                            if (self(operand, assignment, self))
                            {
                                return true;
                            }
                        }
                        return false;
                    }
                    if (op.kind() == grh::ir::OperationKind::kLogicNot ||
                        op.kind() == grh::ir::OperationKind::kNot)
                    {
                        const auto &ops = op.operands();
                        if (ops.empty())
                        {
                            return false;
                        }
                        return !self(ops.front(), assignment, self);
                    }
                    auto it = leafIndex.find(id);
                    return it != leafIndex.end() && assignment[it->second];
                };

                const std::size_t total =
                    leaves.empty() ? 1u : (static_cast<std::size_t>(1) << leaves.size());
                std::vector<bool> assignment(leaves.size(), false);
                for (std::size_t mask = 0; mask < total; ++mask)
                {
                    for (std::size_t i = 0; i < leaves.size(); ++i)
                    {
                        assignment[i] = ((mask >> i) & 1U) != 0U;
                    }
                    if (!evalExpr(valueId, assignment, evalExpr))
                    {
                        return false;
                    }
                }
                return true;
            };
            auto isAlwaysTrue = [&](grh::ir::ValueId valueId) -> bool
            {
                if (isConstOne(valueId))
                {
                    return true;
                }
                if (!valueId.valid())
                {
                    return false;
                }
                const grh::ir::OperationId defOpId =
                    graph->getValue(valueId).definingOp();
                if (defOpId.valid())
                {
                    const grh::ir::Operation defOp = graph->getOperation(defOpId);
                    if (defOp.kind() == grh::ir::OperationKind::kLogicOr)
                    {
                        const auto &ops = defOp.operands();
                        for (std::size_t i = 0; i < ops.size(); ++i)
                        {
                            for (std::size_t j = i + 1; j < ops.size(); ++j)
                            {
                                if (isLogicNotOf(ops[i], ops[j]) ||
                                    isLogicNotOf(ops[j], ops[i]))
                                {
                                    return true;
                                }
                            }
                        }
                    }
                }
                return isAlwaysTrueByTruthTable(valueId);
            };

            auto markPortAsRegIfNeeded = [&](grh::ir::ValueId valueId)
            {
                if (graph->getValue(valueId).type() != grh::ir::ValueType::Logic)
                {
                    return;
                }
                const std::string name = valueName(valueId);
                auto itPort = portDecls.find(name);
                if (itPort != portDecls.end() && itPort->second.dir == PortDir::Output)
                {
                    itPort->second.isReg = true;
                }
            };

            auto resolveMemorySymbol = [&](const grh::ir::Operation &userOp) -> std::optional<std::string>
            {
                auto attr = getAttribute<std::string>(*graph, userOp, "memSymbol");
                if (attr)
                {
                    return attr;
                }

                std::optional<std::string> candidate;
                for (const auto maybeId : graph->operations())
                {
                    const grh::ir::Operation maybeOp = graph->getOperation(maybeId);
                    if (maybeOp.kind() == grh::ir::OperationKind::kMemory)
                    {
                        if (candidate)
                        {
                            return std::nullopt;
                        }
                        candidate = std::string(maybeOp.symbolText());
                    }
                }
                return candidate;
            };

            // -------------------------
            // Port bindings (handle ports mapped to internal nets with different names)
            // -------------------------
            auto bindInputPort = [&](const std::string &portName, grh::ir::ValueId valueId)
            {
                const std::string valueSymbol = valueName(valueId);
                if (portName == valueSymbol)
                {
                    return;
                }
                ensureWireDecl(valueId);
                portBindingStmts.emplace_back(
                    "assign " + valueSymbol + " = " + portName + ";", grh::ir::OperationId::invalid());
            };

            auto bindOutputPort = [&](const std::string &portName, grh::ir::ValueId valueId)
            {
                const std::string valueSymbol = valueName(valueId);
                if (portName == valueSymbol)
                {
                    return;
                }
                portBindingStmts.emplace_back(
                    "assign " + portName + " = " + valueSymbol + ";", grh::ir::OperationId::invalid());
            };

            for (const auto &port : graph->inputPorts())
            {
                if (!port.name.valid())
                {
                    continue;
                }
                bindInputPort(std::string(graph->symbolText(port.name)), port.value);
            }
            for (const auto &port : graph->outputPorts())
            {
                if (!port.name.valid())
                {
                    continue;
                }
                bindOutputPort(std::string(graph->symbolText(port.name)), port.value);
            }
            auto emitInoutAssign = [&](const std::string &dest,
                                       grh::ir::ValueId oeValue,
                                       grh::ir::ValueId outValue,
                                       int64_t width,
                                       grh::ir::OperationId sourceOp) {
                const std::string oeExpr = valueExpr(oeValue);
                const std::string outExpr = valueExpr(outValue);
                const int64_t oeWidth = graph->getValue(oeValue).width();
                auto oeConstBits = [&]() -> std::optional<std::vector<uint8_t>>
                {
                    if (auto literal = constLiteralFor(oeValue))
                    {
                        std::string literalText = *literal;
                        if (auto sized = sizedLiteralIfUnsized(literalText, oeWidth))
                        {
                            literalText = *sized;
                        }
                        return parseConstMaskBits(literalText, oeWidth);
                    }
                    return std::nullopt;
                }();
                auto bitExpr = [&](grh::ir::ValueId valueId, int64_t bit, int64_t valueWidth) -> std::string {
                    if (auto literal = constLiteralFor(valueId))
                    {
                        std::string literalText = *literal;
                        if (auto sized = sizedLiteralIfUnsized(literalText, valueWidth))
                        {
                            literalText = *sized;
                        }
                        return "((" + literalText + " >> " + std::to_string(bit) + ") & 1'b1)";
                    }
                    return parenIfNeeded(valueExpr(valueId)) + "[" + std::to_string(bit) + "]";
                };
                if (oeConstBits && !oeConstBits->empty())
                {
                    const auto &bits = *oeConstBits;
                    bool allZero = true;
                    bool allOne = true;
                    for (uint8_t bit : bits)
                    {
                        allZero = allZero && (bit == 0);
                        allOne = allOne && (bit == 1);
                    }
                    if (allZero)
                    {
                        portBindingStmts.emplace_back(
                            "assign " + dest + " = {" + std::to_string(width) + "{1'bz}};",
                            sourceOp);
                        return;
                    }
                    if (allOne)
                    {
                        portBindingStmts.emplace_back(
                            "assign " + dest + " = " + outExpr + ";",
                            sourceOp);
                        return;
                    }
                }
                if (width <= 1 || oeWidth == 1)
                {
                    portBindingStmts.emplace_back(
                        "assign " + dest + " = " + oeExpr + " ? " + outExpr + " : {" +
                            std::to_string(width) + "{1'bz}};",
                        sourceOp);
                    return;
                }
                if (oeWidth == width)
                {
                    for (int64_t bit = 0; bit < width; ++bit)
                    {
                        const std::string oeBit =
                            (oeConstBits && bit < static_cast<int64_t>(oeConstBits->size()))
                                ? ((*oeConstBits)[bit] ? "1'b1" : "1'b0")
                                : bitExpr(oeValue, bit, oeWidth);
                        const std::string outBit = bitExpr(outValue, bit, width);
                        if (oeBit == "1'b0")
                        {
                            portBindingStmts.emplace_back(
                                "assign " + dest + "[" + std::to_string(bit) + "] = 1'bz;",
                                sourceOp);
                        }
                        else if (oeBit == "1'b1")
                        {
                            portBindingStmts.emplace_back(
                                "assign " + dest + "[" + std::to_string(bit) + "] = " + outBit + ";",
                                sourceOp);
                        }
                        else
                        {
                            portBindingStmts.emplace_back(
                                "assign " + dest + "[" + std::to_string(bit) + "] = " + oeBit +
                                    " ? " + outBit + " : 1'bz;",
                                sourceOp);
                        }
                    }
                    return;
                }
                portBindingStmts.emplace_back(
                    "assign " + dest + " = (" + parenIfNeeded(oeExpr) + " != {" + std::to_string(oeWidth) +
                        "{1'b0}}) ? " + valueExpr(outValue) + " : {" + std::to_string(width) + "{1'bz}};",
                    sourceOp);
            };
            for (const auto &port : graph->inoutPorts())
            {
                if (!port.name.valid())
                {
                    continue;
                }
                const std::string portName = std::string(graph->symbolText(port.name));
                const std::string inName = valueName(port.in);
                const int64_t width = graph->getValue(port.out).width();
                ensureWireDecl(port.in);
                ensureWireDecl(port.out);
                ensureWireDecl(port.oe);
                portBindingStmts.emplace_back(
                    "assign " + inName + " = " + portName + ";", grh::ir::OperationId::invalid());
                emitInoutAssign(portName, port.oe, port.out, width, grh::ir::OperationId::invalid());
            }

            // -------------------------
            // Operation traversal
            // -------------------------
            for (const auto opId : graph->operations())
            {
                const grh::ir::Operation op = graph->getOperation(opId);
                const auto &operands = op.operands();
                const auto &results = op.results();
                const std::string opContext = std::string(op.symbolText());
                auto buildEventKey = [&](const grh::ir::Operation &eventOp,
                                         std::size_t eventStart) -> std::optional<SeqKey>
                {
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, eventOp, "eventEdge");
                    if (!eventEdges)
                    {
                        reportError(std::string(grh::ir::toString(eventOp.kind())) + " missing eventEdge", opContext);
                        return std::nullopt;
                    }
                    const std::size_t eventCount = operands.size() > eventStart ? operands.size() - eventStart : 0;
                    if (eventCount == 0 || eventEdges->size() != eventCount)
                    {
                        reportError(std::string(grh::ir::toString(eventOp.kind())) + " eventEdge size mismatch",
                                    opContext);
                        return std::nullopt;
                    }
                    SeqKey key;
                    key.events.reserve(eventCount);
                    for (std::size_t i = 0; i < eventCount; ++i)
                    {
                        const grh::ir::ValueId signal = operands[eventStart + i];
                        if (!signal.valid())
                        {
                            reportError(std::string(grh::ir::toString(eventOp.kind())) + " missing event operand",
                                        opContext);
                            return std::nullopt;
                        }
                        key.events.push_back(SeqEvent{(*eventEdges)[i], signal});
                    }
                    return key;
                };
                auto buildSingleEventKey = [&](grh::ir::ValueId signal,
                                               const std::optional<std::string> &edgeAttr,
                                               std::string_view opName) -> std::optional<SeqKey>
                {
                    if (!signal.valid() || !edgeAttr || edgeAttr->empty())
                    {
                        reportError(std::string(opName) + " missing clk/clkPolarity", opContext);
                        return std::nullopt;
                    }
                    SeqKey key;
                    key.events.push_back(SeqEvent{*edgeAttr, signal});
                    return key;
                };

                switch (op.kind())
                {
                case grh::ir::OperationKind::kConstant:
                {
                    if (results.empty())
                    {
                        reportError("kConstant missing result", opContext);
                        break;
                    }
                    auto constValue = getAttribute<std::string>(*graph, op, "constValue");
                    if (!constValue)
                    {
                        reportError("kConstant missing constValue attribute", opContext);
                        break;
                    }
                    addValueAssign(results[0], formatConstLiteral(results[0], *constValue), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kSystemFunction:
                {
                    if (results.empty())
                    {
                        reportError("kSystemFunction missing result", opContext);
                        break;
                    }
                    auto name = getAttribute<std::string>(*graph, op, "name");
                    if (!name || name->empty())
                    {
                        reportError("kSystemFunction missing name", opContext);
                        break;
                    }
                    std::ostringstream expr;
                    expr << "$" << *name;
                    if (!operands.empty())
                    {
                        if (operands.size() <= 4)
                        {
                            // Short arg list: single line
                            expr << "(";
                            for (std::size_t i = 0; i < operands.size(); ++i)
                            {
                                if (i != 0)
                                {
                                    expr << ", ";
                                }
                                expr << valueExpr(operands[i]);
                            }
                            expr << ")";
                        }
                        else
                        {
                            // Long arg list: multi-line format
                            expr << "(\n";
                            for (std::size_t i = 0; i < operands.size(); ++i)
                            {
                                expr << "    " << valueExpr(operands[i]);
                                if (i + 1 < operands.size())
                                {
                                    expr << ",";
                                }
                                expr << "\n";
                            }
                            expr << "  )";
                        }
                    }
                    addValueAssign(results[0], expr.str(), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kEq:
                case grh::ir::OperationKind::kNe:
                case grh::ir::OperationKind::kCaseEq:
                case grh::ir::OperationKind::kCaseNe:
                case grh::ir::OperationKind::kWildcardEq:
                case grh::ir::OperationKind::kWildcardNe:
                case grh::ir::OperationKind::kLt:
                case grh::ir::OperationKind::kLe:
                case grh::ir::OperationKind::kGt:
                case grh::ir::OperationKind::kGe:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", opContext);
                        break;
                    }
                    int64_t resultWidth =
                        std::max(graph->getValue(operands[0]).width(),
                                 graph->getValue(operands[1]).width());
                    const std::string tok = binOpToken(op.kind());
                    const std::string lhs = resultWidth > 0
                                                ? baseExpr.extendOperand(operands[0], resultWidth)
                                                : valueExpr(operands[0]);
                    const std::string rhs = resultWidth > 0
                                                ? baseExpr.extendOperand(operands[1], resultWidth)
                                                : valueExpr(operands[1]);
                    const bool signedCompare =
                        (op.kind() == grh::ir::OperationKind::kLt ||
                         op.kind() == grh::ir::OperationKind::kLe ||
                         op.kind() == grh::ir::OperationKind::kGt ||
                         op.kind() == grh::ir::OperationKind::kGe) &&
                        graph->getValue(operands[0]).isSigned() &&
                        graph->getValue(operands[1]).isSigned();
                    const std::string lhsExpr = signedCompare ? "$signed(" + lhs + ")" : lhs;
                    const std::string rhsExpr = signedCompare ? "$signed(" + rhs + ")" : rhs;
                    addValueAssign(results[0], lhsExpr + " " + tok + " " + rhsExpr, opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kAnd:
                case grh::ir::OperationKind::kOr:
                case grh::ir::OperationKind::kXor:
                case grh::ir::OperationKind::kXnor:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", opContext);
                        break;
                    }
                    const std::string tok = binOpToken(op.kind());
                    addValueAssign(results[0],
                                   valueExpr(operands[0]) + " " + tok + " " + valueExpr(operands[1]),
                                   opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kShl:
                case grh::ir::OperationKind::kLShr:
                case grh::ir::OperationKind::kAShr:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", opContext);
                        break;
                    }
                    const int64_t resultWidth = graph->getValue(results[0]).width();
                    const bool signExtend =
                        op.kind() == grh::ir::OperationKind::kAShr &&
                        graph->getValue(operands[0]).isSigned();
                    const std::string tok = binOpToken(op.kind());
                    const std::string lhs = resultWidth > 0
                                                ? baseExpr.extendShiftOperand(operands[0], resultWidth, signExtend)
                                                : valueExpr(operands[0]);
                    addValueAssign(results[0],
                                   lhs + " " + tok + " " + valueExpr(operands[1]),
                                   opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kAdd:
                case grh::ir::OperationKind::kSub:
                case grh::ir::OperationKind::kMul:
                case grh::ir::OperationKind::kDiv:
                case grh::ir::OperationKind::kMod:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", opContext);
                        break;
                    }
                    int64_t resultWidth = graph->getValue(results[0]).width();
                    if (resultWidth <= 0)
                    {
                        resultWidth = std::max(graph->getValue(operands[0]).width(),
                                               graph->getValue(operands[1]).width());
                    }
                    const std::string tok = binOpToken(op.kind());
                    addValueAssign(results[0],
                                   baseExpr.extendOperand(operands[0], resultWidth) + " " + tok + " " +
                                       baseExpr.extendOperand(operands[1], resultWidth),
                                   opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kLogicAnd:
                case grh::ir::OperationKind::kLogicOr:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", opContext);
                        break;
                    }
                    const std::string tok = binOpToken(op.kind());
                    addValueAssign(results[0],
                                   baseExpr.logicalOperand(operands[0]) + " " + tok + " " +
                                       baseExpr.logicalOperand(operands[1]),
                                   opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kNot:
                case grh::ir::OperationKind::kReduceAnd:
                case grh::ir::OperationKind::kReduceOr:
                case grh::ir::OperationKind::kReduceXor:
                case grh::ir::OperationKind::kReduceNor:
                case grh::ir::OperationKind::kReduceNand:
                case grh::ir::OperationKind::kReduceXnor:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("Unary operation missing operands or results", opContext);
                        break;
                    }
                    const std::string tok = unaryOpToken(op.kind());
                    addValueAssign(results[0], tok + valueExpr(operands[0]), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kLogicNot:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("Unary operation missing operands or results", opContext);
                        break;
                    }
                    addValueAssign(results[0], "!" + baseExpr.logicalOperand(operands[0]), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kMux:
                {
                    if (operands.size() < 3 || results.empty())
                    {
                        reportError("kMux missing operands or results", opContext);
                        break;
                    }
                    int64_t resultWidth = graph->getValue(results[0]).width();
                    const std::string lhs = baseExpr.extendOperand(operands[1], resultWidth);
                    const std::string rhs = baseExpr.extendOperand(operands[2], resultWidth);
                    addValueAssign(results[0],
                                   valueExpr(operands[0]) + " ? " + lhs + " : " + rhs,
                                   opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kAssign:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kAssign missing operands or results", opContext);
                        break;
                    }
                    int64_t resultWidth = graph->getValue(results[0]).width();
                    const std::string rhs = baseExpr.extendOperand(operands[0], resultWidth);
                    addValueAssign(results[0], rhs, opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kConcat:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kConcat missing operands or results", opContext);
                        break;
                    }
                    if (operands.size() == 1)
                    {
                        addValueAssign(results[0], valueExpr(operands[0]), opId);
                        ensureWireDecl(results[0]);
                        break;
                    }
                    std::ostringstream expr;
                    if (operands.size() <= 4)
                    {
                        // Short concat: single line
                        expr << "{";
                        for (std::size_t i = 0; i < operands.size(); ++i)
                        {
                            if (i != 0)
                            {
                                expr << ", ";
                            }
                            expr << baseExpr.concatOperandExpr(operands[i]);
                        }
                        expr << "}";
                    }
                    else
                    {
                        // Long concat: multi-line format
                        expr << "{\n";
                        for (std::size_t i = 0; i < operands.size(); ++i)
                        {
                            expr << "    " << baseExpr.concatOperandExpr(operands[i]);
                            if (i + 1 < operands.size())
                            {
                                expr << ",";
                            }
                            expr << "\n";
                        }
                        expr << "  }";
                    }
                    addValueAssign(results[0], expr.str(), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kReplicate:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kReplicate missing operands or results", opContext);
                        break;
                    }
                    auto rep = getAttribute<int64_t>(*graph, op, "rep");
                    if (!rep)
                    {
                        reportError("kReplicate missing rep attribute", opContext);
                        break;
                    }
                    std::ostringstream expr;
                    expr << "{" << *rep << "{" << baseExpr.concatOperandExpr(operands[0]) << "}}";
                    addValueAssign(results[0], expr.str(), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kSliceStatic:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kSliceStatic missing operands or results", opContext);
                        break;
                    }
                    auto sliceStart = getAttribute<int64_t>(*graph, op, "sliceStart");
                    auto sliceEnd = getAttribute<int64_t>(*graph, op, "sliceEnd");
                    if (!sliceStart || !sliceEnd)
                    {
                        reportError("kSliceStatic missing sliceStart or sliceEnd", opContext);
                        break;
                    }
                    const int64_t operandWidth = graph->getValue(operands[0]).width();
                    std::ostringstream expr;
                    if (operandWidth == 1)
                    {
                        if (*sliceStart != 0 || *sliceEnd != 0)
                        {
                            reportError("kSliceStatic index out of range for scalar", opContext);
                        }
                        expr << valueExpr(operands[0]);
                    }
                    else
                    {
                        expr << parenIfNeeded(valueExpr(operands[0])) << "[";
                        if (*sliceStart == *sliceEnd)
                        {
                            expr << *sliceStart;
                        }
                        else
                        {
                            expr << *sliceEnd << ":" << *sliceStart;
                        }
                        expr << "]";
                    }
                    addValueAssign(results[0], expr.str(), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kSliceDynamic:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("kSliceDynamic missing operands or results", opContext);
                        break;
                    }
                    auto width = getAttribute<int64_t>(*graph, op, "sliceWidth");
                    if (!width)
                    {
                        reportError("kSliceDynamic missing sliceWidth", opContext);
                        break;
                    }
                    const int64_t operandWidth = graph->getValue(operands[0]).width();
                    std::ostringstream expr;
                    if (operandWidth == 1)
                    {
                        if (*width != 1)
                        {
                            reportError("kSliceDynamic width exceeds scalar", opContext);
                        }
                        expr << valueExpr(operands[0]);
                    }
                    else if (*width == 1)
                    {
                        expr << parenIfNeeded(valueExpr(operands[0])) << "["
                             << baseExpr.clampIndexExpr(operands[1], operandWidth) << "]";
                    }
                    else
                    {
                        expr << parenIfNeeded(valueExpr(operands[0])) << "["
                             << baseExpr.clampIndexExpr(operands[1], operandWidth) << " +: " << *width << "]";
                    }
                    addValueAssign(results[0], expr.str(), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kSliceArray:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("kSliceArray missing operands or results", opContext);
                        break;
                    }
                    auto width = getAttribute<int64_t>(*graph, op, "sliceWidth");
                    if (!width)
                    {
                        reportError("kSliceArray missing sliceWidth", opContext);
                        break;
                    }
                    const int64_t operandWidth = graph->getValue(operands[0]).width();
                    std::ostringstream expr;
                    if (*width == 1 && operandWidth == 1)
                    {
                        expr << valueExpr(operands[0]);
                    }
                    else
                    {
                        expr << parenIfNeeded(valueExpr(operands[0])) << "[";
                        if (*width == 1)
                        {
                            expr << parenIfNeeded(valueExpr(operands[1]));
                        }
                        else
                        {
                            expr << parenIfNeeded(valueExpr(operands[1])) << " * " << *width << " +: " << *width;
                        }
                        expr << "]";
                    }
                    addValueAssign(results[0], expr.str(), opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kLatch:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("kLatch missing operands or results", opContext);
                        break;
                    }
                    const grh::ir::ValueId updateCond = operands[0];
                    const grh::ir::ValueId nextValue = operands[1];
                    if (!updateCond.valid() || !nextValue.valid())
                    {
                        reportError("kLatch missing updateCond/nextValue operands", opContext);
                        break;
                    }
                    if (graph->getValue(updateCond).width() != 1)
                    {
                        reportError("kLatch updateCond must be 1 bit", opContext);
                        break;
                    }

                    const grh::ir::ValueId q = results[0];
                    const std::string &valueSym = valueName(q);
                    const std::string &regName = !valueSym.empty() ? valueSym : opContext;
                    const grh::ir::Value dValue = graph->getValue(nextValue);
                    if (graph->getValue(q).width() != dValue.width())
                    {
                        reportError("kLatch data/output width mismatch", opContext);
                        break;
                    }
                    ensureRegDecl(regName, dValue.width(), dValue.isSigned(), dValue.type(),
                                  op.srcLoc());
                    const bool directDrive = valueName(q) == regName;
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(q);
                    }
                    else
                    {
                        ensureWireDecl(q);
                        addValueAssign(q, regName, opId);
                    }

                    std::ostringstream block;
                    if (isAlwaysTrue(updateCond))
                    {
                        appendIndented(block, 1, "always @* begin");
                        appendIndented(block, 2, regName + " = " + valueExprSeq(nextValue) + ";");
                        appendIndented(block, 1, "end");
                    }
                    else
                    {
                        std::ostringstream stmt;
                        appendIndented(stmt, 2, "if (" + valueExprSeq(updateCond) + ") begin");
                        appendIndented(stmt, 3, regName + " = " + valueExprSeq(nextValue) + ";");
                        appendIndented(stmt, 2, "end");
                        block << "  always_latch begin\n";
                        block << stmt.str();
                        block << "  end";
                    }
                    addLatchBlock(block.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kRegister:
                {
                    if (operands.size() < 3 || results.empty())
                    {
                        reportError("kRegister missing operands or results", opContext);
                        break;
                    }

                    const grh::ir::ValueId updateCond = operands[0];
                    const grh::ir::ValueId nextValue = operands[1];
                    if (!updateCond.valid() || !nextValue.valid())
                    {
                        reportError("kRegister missing updateCond/nextValue operands", opContext);
                        break;
                    }
                    if (graph->getValue(updateCond).width() != 1)
                    {
                        reportError("kRegister updateCond must be 1 bit", opContext);
                        break;
                    }

                    auto seqKey = buildEventKey(op, 2);
                    if (!seqKey)
                    {
                        break;
                    }

                    const grh::ir::ValueId q = results[0];
                    const std::string &valueSym = valueName(q);
                    const std::string &regName = !valueSym.empty() ? valueSym : opContext;
                    const grh::ir::Value dValue = graph->getValue(nextValue);
                    if (graph->getValue(q).width() != dValue.width())
                    {
                        reportError("kRegister data/output width mismatch", opContext);
                        break;
                    }

                    ensureRegDecl(regName, dValue.width(), dValue.isSigned(), dValue.type(),
                                  op.srcLoc());
                    const bool directDrive = valueName(q) == regName;
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(q);
                    }
                    else
                    {
                        ensureWireDecl(q);
                        addValueAssign(q, regName, opId);
                    }

                    // Handle register initialization
                    auto initKinds = getAttribute<std::vector<std::string>>(*graph, op, "initKind");
                    auto initValues = getAttribute<std::vector<std::string>>(*graph, op, "initValue");
                    if (initKinds && initValues && !initKinds->empty())
                    {
                        for (std::size_t i = 0; i < initKinds->size(); ++i)
                        {
                            const std::string& kind = (*initKinds)[i];
                            const std::string& value = (*initValues)[i];
                            std::string stmt;
                            if (kind == "literal")
                            {
                                stmt = regName + " = " + value + ";";
                            }
                            else if (kind == "random")
                            {
                                stmt = regName + " = " + value + ";";
                            }
                            if (!stmt.empty())
                            {
                                std::ostringstream oss;
                                appendIndented(oss, 2, stmt);
                                addSimpleStmt("initial", oss.str(), opId);
                            }
                        }
                    }

                    std::ostringstream stmt;
                    if (isConstOne(updateCond))
                    {
                        appendIndented(stmt, 2, regName + " <= " + valueExprSeq(nextValue) + ";");
                    }
                    else
                    {
                        appendIndented(stmt, 2, "if (" + valueExprSeq(updateCond) + ") begin");
                        appendIndented(stmt, 3, regName + " <= " + valueExprSeq(nextValue) + ";");
                        appendIndented(stmt, 2, "end");
                    }
                    addSequentialStmt(*seqKey, stmt.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kMemory:
                {
                    auto widthAttr = getAttribute<int64_t>(*graph, op, "width");
                    auto rowAttr = getAttribute<int64_t>(*graph, op, "row");
                    auto isSignedAttr = getAttribute<bool>(*graph, op, "isSigned");
                    if (!widthAttr || !rowAttr || !isSignedAttr)
                    {
                        reportError("kMemory missing width/row/isSigned", opContext);
                        break;
                    }
                    std::ostringstream decl;
                    decl << "reg " << (*isSignedAttr ? "signed " : "") << "[" << (*widthAttr - 1) << ":0] " << opContext << " [0:" << (*rowAttr - 1) << "];";
                    memoryDecls.emplace_back(decl.str(), opId);
                    declaredNames.insert(opContext);

                    auto initKinds = getAttribute<std::vector<std::string>>(*graph, op, "initKind");
                    auto initFiles = getAttribute<std::vector<std::string>>(*graph, op, "initFile");
                    auto initValues = getAttribute<std::vector<std::string>>(*graph, op, "initValue");
                    if (initKinds || initFiles)
                    {
                        if (!initKinds || !initFiles || initKinds->size() != initFiles->size())
                        {
                            reportError("kMemory initKind/initFile size mismatch", opContext);
                            break;
                        }
                        const std::size_t count = initKinds->size();
                        auto hasStart = getAttribute<std::vector<bool>>(*graph, op, "initHasStart");
                        auto hasFinish = getAttribute<std::vector<bool>>(*graph, op, "initHasFinish");
                        auto starts = getAttribute<std::vector<int64_t>>(*graph, op, "initStart");
                        auto finishes = getAttribute<std::vector<int64_t>>(*graph, op, "initFinish");
                        auto addresses = getAttribute<std::vector<int64_t>>(*graph, op, "initAddress");
                        if (hasStart && hasStart->size() != count)
                        {
                            reportError("kMemory initHasStart size mismatch", opContext);
                            break;
                        }
                        if (hasFinish && hasFinish->size() != count)
                        {
                            reportError("kMemory initHasFinish size mismatch", opContext);
                            break;
                        }
                        if (starts && starts->size() != count)
                        {
                            reportError("kMemory initStart size mismatch", opContext);
                            break;
                        }
                        if (finishes && finishes->size() != count)
                        {
                            reportError("kMemory initFinish size mismatch", opContext);
                            break;
                        }
                        std::vector<bool> localHasStart = hasStart.value_or(std::vector<bool>(count, false));
                        std::vector<bool> localHasFinish = hasFinish.value_or(std::vector<bool>(count, false));
                        std::vector<int64_t> localStarts = starts.value_or(std::vector<int64_t>(count, 0));
                        std::vector<int64_t> localFinishes = finishes.value_or(std::vector<int64_t>(count, 0));
                        std::vector<int64_t> localAddresses = addresses.value_or(std::vector<int64_t>(count, -1));
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            const std::string& kind = (*initKinds)[i];
                            std::string stmt;
                            
                            if (kind == "readmemh" || kind == "readmemb")
                            {
                                // readmemh/readmemb: $readmemh("file", memory, start, finish);
                                stmt = "$" + kind + "(\"" + escapeSvString((*initFiles)[i]) + "\", " + opContext;
                                if (localHasStart[i])
                                {
                                    stmt.append(", ");
                                    stmt.append(std::to_string(localStarts[i]));
                                    if (localHasFinish[i])
                                    {
                                        stmt.append(", ");
                                        stmt.append(std::to_string(localFinishes[i]));
                                    }
                                }
                                stmt.append(");");
                            }
                            else if (kind == "literal" || kind == "random")
                            {
                                // Direct assignment: memory[addr] = value;
                                std::string value = (initValues && i < initValues->size()) ? (*initValues)[i] : "0";
                                if (localAddresses[i] >= 0)
                                {
                                    // Specific address
                                    stmt = opContext + "[" + std::to_string(localAddresses[i]) + "] = " + value + ";";
                                }
                                else
                                {
                                    // Unknown address (e.g., loop variable) - need to init all elements
                                    // Generate a for loop to initialize all entries
                                    stmt = "for (int __i = 0; __i < " + std::to_string(*rowAttr) + "; __i = __i + 1) " + opContext + "[__i] = " + value + ";";
                                }
                            }
                            else
                            {
                                // Unknown kind, skip
                                continue;
                            }
                            
                            std::ostringstream oss;
                            appendIndented(oss, 2, stmt);
                            addSimpleStmt("initial", oss.str(), opId);
                        }
                    }
                    break;
                }
                case grh::ir::OperationKind::kMemoryReadPort:
                {
                    if (operands.size() < 1 || results.empty())
                    {
                        reportError("kMemoryReadPort missing operands or results", opContext);
                        break;
                    }
                    auto memSymbolAttr = resolveMemorySymbol(op);
                    if (!memSymbolAttr)
                    {
                        reportError("kMemoryReadPort missing memSymbol", opContext);
                        break;
                    }
                    addAssign("assign " + valueName(results[0]) + " = " + *memSymbolAttr + "[" +
                                  valueExpr(operands[0]) + "];",
                              opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kMemoryWritePort:
                {
                    if (operands.size() < 5)
                    {
                        reportError("kMemoryWritePort missing operands", opContext);
                        break;
                    }
                    const grh::ir::ValueId updateCond = operands[0];
                    const grh::ir::ValueId addr = operands[1];
                    const grh::ir::ValueId data = operands[2];
                    const grh::ir::ValueId mask = operands[3];
                    if (!updateCond.valid() || !addr.valid() || !data.valid() || !mask.valid())
                    {
                        reportError("kMemoryWritePort missing operands", opContext);
                        break;
                    }
                    if (graph->getValue(updateCond).width() != 1)
                    {
                        reportError("kMemoryWritePort updateCond must be 1 bit", opContext);
                        break;
                    }
                    auto memSymbolAttr = resolveMemorySymbol(op);
                    if (!memSymbolAttr)
                    {
                        reportError("kMemoryWritePort missing memSymbol", opContext);
                        break;
                    }
                    auto seqKey = buildEventKey(op, 4);
                    if (!seqKey)
                    {
                        break;
                    }

                    const std::string &memSymbol = *memSymbolAttr;
                    const grh::ir::OperationId memOpId = graph->findOperation(memSymbol);
                    int64_t memWidth = 1;
                    if (memOpId.valid())
                    {
                        const grh::ir::Operation memOp = graph->getOperation(memOpId);
                        memWidth = getAttribute<int64_t>(*graph, memOp, "width").value_or(1);
                    }

                    const bool guardUpdate = !isConstOne(updateCond);
                    const int baseIndent = guardUpdate ? 3 : 2;
                    std::optional<std::vector<uint8_t>> maskBits;
                    if (auto maskLiteral = constLiteralFor(mask))
                    {
                        maskBits = parseConstMaskBits(*maskLiteral, memWidth);
                    }
                    auto maskAll = [&](uint8_t value) -> bool
                    {
                        if (!maskBits)
                        {
                            return false;
                        }
                        for (uint8_t bit : *maskBits)
                        {
                            if (bit != value)
                            {
                                return false;
                            }
                        }
                        return true;
                    };
                    const int64_t dataWidth = graph->getValue(data).width();
                    const bool maskAllOnes = maskBits ? maskAll(1) : false;
                    if (dataWidth > 0 && memWidth > 0 && dataWidth != memWidth && !maskAllOnes)
                    {
                        reportWarning("kMemoryWritePort data width does not match memory width", opContext);
                    }
                    std::ostringstream stmt;
                    if (guardUpdate)
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(updateCond) + ") begin");
                    }
                    if (maskBits)
                    {
                        const bool allZero = maskAll(0);
                        const bool allOnes = maskAll(1);
                        if (allZero)
                        {
                            break;
                        }
                        if (allOnes || memWidth <= 1)
                        {
                            appendIndented(stmt, baseIndent, memSymbol + "[" + valueExpr(addr) + "] <= " + valueExpr(data) + ";");
                        }
                        else
                        {
                            for (int64_t bit = 0; bit < memWidth; ++bit)
                            {
                                if ((*maskBits)[static_cast<std::size_t>(bit)] != 0)
                                {
                                    appendIndented(stmt, baseIndent,
                                                   memSymbol + "[" + valueExpr(addr) + "][" + std::to_string(bit) + "] <= " +
                                                       valueExpr(data) + "[" + std::to_string(bit) + "];");
                                }
                            }
                        }
                        if (guardUpdate)
                        {
                            appendIndented(stmt, 2, "end");
                        }
                        addSequentialStmt(*seqKey, stmt.str(), opId);
                        break;
                    }
                    if (memWidth <= 1)
                    {
                        appendIndented(stmt, baseIndent, "if (" + valueExpr(mask) + ") begin");
                        appendIndented(stmt, baseIndent + 1, memSymbol + "[" + valueExpr(addr) + "] <= " + valueExpr(data) + ";");
                        appendIndented(stmt, baseIndent, "end");
                        if (guardUpdate)
                        {
                            appendIndented(stmt, 2, "end");
                        }
                        addSequentialStmt(*seqKey, stmt.str(), opId);
                        break;
                    }
                    appendIndented(stmt, baseIndent, "if (" + valueExpr(mask) + " == {" + std::to_string(memWidth) + "{1'b1}}) begin");
                    appendIndented(stmt, baseIndent + 1, memSymbol + "[" + valueExpr(addr) + "] <= " + valueExpr(data) + ";");
                    appendIndented(stmt, baseIndent, "end else begin");
                    appendIndented(stmt, baseIndent + 1, "integer i;");
                    appendIndented(stmt, baseIndent + 1, "for (i = 0; i < " + std::to_string(memWidth) + "; i = i + 1) begin");
                    appendIndented(stmt, baseIndent + 2, "if (" + valueExpr(mask) + "[i]) begin");
                    appendIndented(stmt, baseIndent + 3, memSymbol + "[" + valueExpr(addr) + "][i] <= " + valueExpr(data) + "[i];");
                    appendIndented(stmt, baseIndent + 2, "end");
                    appendIndented(stmt, baseIndent + 1, "end");
                    appendIndented(stmt, baseIndent, "end");
                    if (guardUpdate)
                    {
                        appendIndented(stmt, 2, "end");
                    }
                    addSequentialStmt(*seqKey, stmt.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kInstance:
                case grh::ir::OperationKind::kBlackbox:
                {
                    auto moduleName = getAttribute<std::string>(*graph, op, "moduleName");
                    auto inputNames = getAttribute<std::vector<std::string>>(*graph, op, "inputPortName");
                    auto outputNames = getAttribute<std::vector<std::string>>(*graph, op, "outputPortName");
                    auto inoutNames = getAttribute<std::vector<std::string>>(*graph, op, "inoutPortName");
                    auto instanceNameBase = getAttribute<std::string>(*graph, op, "instanceName").value_or(opContext);
                    std::string instanceName = instanceNameBase;
                    int instSuffix = 1;
                    while (!instanceNamesUsed.insert(instanceName).second)
                    {
                        instanceName = instanceNameBase + "_" + std::to_string(instSuffix++);
                    }
                    if (!moduleName || !inputNames || !outputNames)
                    {
                        reportError("Instance missing module or port names", opContext);
                        break;
                    }
                    const std::size_t inoutCount = inoutNames ? inoutNames->size() : 0;
                    if (operands.size() < inputNames->size() + inoutCount * 2 ||
                        results.size() < outputNames->size() + inoutCount)
                    {
                        reportError("Instance port counts do not match operands/results", opContext);
                        break;
                    }
                    const auto moduleNameIt = emittedModuleNames.find(*moduleName);
                    const std::string &targetModuleName =
                        moduleNameIt != emittedModuleNames.end() ? moduleNameIt->second : *moduleName;

                    std::ostringstream decl;
                    if (op.kind() == grh::ir::OperationKind::kBlackbox)
                    {
                        auto paramNames = getAttribute<std::vector<std::string>>(*graph, op, "parameterNames");
                        auto paramValues = getAttribute<std::vector<std::string>>(*graph, op, "parameterValues");
                        if (paramNames && paramValues && !paramNames->empty() && paramNames->size() == paramValues->size())
                        {
                            decl << targetModuleName << " #(" << '\n';
                            for (std::size_t i = 0; i < paramNames->size(); ++i)
                            {
                                decl << "    ." << (*paramNames)[i] << "(" << (*paramValues)[i] << ")";
                                decl << (i + 1 == paramNames->size() ? "\n" : ",\n");
                            }
                            decl << ") ";
                        }
                        else
                        {
                            decl << targetModuleName << " ";
                        }
                    }
                    else
                    {
                        decl << targetModuleName << " ";
                    }
                    std::vector<std::pair<std::string, std::string>> connections;
                    connections.reserve(inputNames->size() + outputNames->size() + inoutCount);
                    for (std::size_t i = 0; i < inputNames->size(); ++i)
                    {
                        if (i < operands.size())
                        {
                            connections.emplace_back((*inputNames)[i], valueExpr(operands[i]));
                        }
                    }
                    for (std::size_t i = 0; i < outputNames->size(); ++i)
                    {
                        if (i < results.size())
                        {
                            connections.emplace_back((*outputNames)[i], valueName(results[i]));
                        }
                    }
                    if (inoutNames)
                    {
                        auto ensureNamedWireDecl = [&](const std::string &base, int64_t width,
                                                       bool isSigned) -> std::string
                        {
                            const std::string root = base.empty() ? std::string("_inout") : base;
                            std::string candidate = root;
                            std::size_t suffix = 0;
                            while (declaredNames.find(candidate) != declaredNames.end())
                            {
                                candidate = root;
                                candidate.push_back('_');
                                candidate.append(std::to_string(++suffix));
                            }
                            wireDecls.emplace(candidate, NetDecl{width, isSigned, op.srcLoc()});
                            declaredNames.insert(candidate);
                            return candidate;
                        };
                        bool inoutOk = true;
                        for (std::size_t i = 0; i < inoutNames->size(); ++i)
                        {
                            const std::size_t outIndex = inputNames->size() + i;
                            const std::size_t oeIndex = inputNames->size() + inoutNames->size() + i;
                            const std::size_t inIndex = outputNames->size() + i;
                            if (outIndex >= operands.size() || oeIndex >= operands.size() ||
                                inIndex >= results.size())
                            {
                                reportError("Instance inout index out of range", opContext);
                                inoutOk = false;
                                break;
                            }
                            const std::string wireBase =
                                instanceName.empty()
                                    ? (*inoutNames)[i] + "_inout"
                                    : instanceName + "_" + (*inoutNames)[i] + "_inout";
                            const int64_t width = graph->getValue(operands[outIndex]).width();
                            const bool isSigned = graph->getValue(operands[outIndex]).isSigned();
                            const std::string wireName = ensureNamedWireDecl(wireBase, width, isSigned);
                            connections.emplace_back((*inoutNames)[i], wireName);
                            ensureWireDecl(operands[outIndex]);
                            ensureWireDecl(operands[oeIndex]);
                            ensureWireDecl(results[inIndex]);
                            emitInoutAssign(wireName, operands[oeIndex], operands[outIndex], width, opId);
                            portBindingStmts.emplace_back(
                                "assign " + valueName(results[inIndex]) + " = " + wireName + ";",
                                opId);
                        }
                        if (!inoutOk)
                        {
                            break;
                        }
                    }

                    decl << instanceName << " (\n";
                    for (std::size_t i = 0; i < connections.size(); ++i)
                    {
                        const auto &conn = connections[i];
                        decl << "    ." << conn.first << "(" << conn.second << ")";
                        decl << (i + 1 == connections.size() ? "\n" : ",\n");
                    }
                    decl << "  );";
                    instanceDecls.emplace_back(decl.str(), opId);

                    for (const auto res : results)
                    {
                        ensureWireDecl(res);
                    }
                    break;
                }
                case grh::ir::OperationKind::kSystemTask:
                {
                    if (operands.empty())
                    {
                        reportError("kSystemTask missing operands", opContext);
                        break;
                    }
                    auto taskName = getAttribute<std::string>(*graph, op, "name");
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                    auto procKind = getAttribute<std::string>(*graph, op, "procKind");
                    if (!taskName || taskName->empty())
                    {
                        reportError("kSystemTask missing name", opContext);
                        break;
                    }
                    const std::size_t eventCount = eventEdges ? eventEdges->size() : 0;
                    if (operands.size() < 1 + eventCount)
                    {
                        reportError("kSystemTask operand count does not match eventEdge", opContext);
                        break;
                    }
                    const std::size_t argCount = operands.size() - 1 - eventCount;
                    const std::size_t eventStart = 1 + argCount;
                    std::optional<SeqKey> seqKey;
                    if (eventCount > 0)
                    {
                        seqKey = buildEventKey(op, eventStart);
                        if (!seqKey)
                        {
                            break;
                        }
                    }

                    // Helper to inline string literals for system tasks
                    auto getSystemTaskArgExpr = [&](grh::ir::ValueId argId) -> std::string
                    {
                        // For string constants used in system tasks, inline the literal
                        // to avoid initialization order issues with initial blocks
                        if (valueType(argId) == grh::ir::ValueType::String)
                        {
                            if (auto literal = constLiteralFor(argId))
                            {
                                return *literal;
                            }
                        }
                        return valueExpr(argId);
                    };

                    const bool guardTask = !isConstOne(operands[0]);
                    const int baseIndent = guardTask ? 3 : 2;
                    std::ostringstream stmt;
                    if (guardTask)
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                    }
                    stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$" << *taskName;
                    const bool anyArgs = argCount > 0;
                    if (anyArgs)
                    {
                        bool firstArg = true;
                        auto appendArg = [&](const std::string &text)
                        {
                            if (!firstArg)
                            {
                                stmt << ", ";
                            }
                            stmt << text;
                            firstArg = false;
                        };
                        stmt << "(";
                        for (std::size_t i = 0; i < argCount; ++i)
                        {
                            appendArg(getSystemTaskArgExpr(operands[1 + i]));
                        }
                        stmt << ")";
                    }
                    stmt << ";\n";
                    if (guardTask)
                    {
                        appendIndented(stmt, 2, "end");
                    }

                    if (seqKey)
                    {
                        addSequentialStmt(*seqKey, stmt.str(), opId);
                    }
                    else
                    {
                        auto headerForKind = [&](const std::optional<std::string> &kind) -> std::string
                        {
                            if (!kind)
                            {
                                return "always @*";
                            }
                            if (*kind == "initial")
                            {
                                return "initial";
                            }
                            if (*kind == "final")
                            {
                                return "final";
                            }
                            if (*kind == "always_comb")
                            {
                                return "always_comb";
                            }
                            if (*kind == "always_latch")
                            {
                                return "always_latch";
                            }
                            return "always @*";
                        };
                        addSimpleStmt(headerForKind(procKind), stmt.str(), opId);
                    }
                    break;
                }
                case grh::ir::OperationKind::kDpicImport:
                {
                    auto argsDir = getAttribute<std::vector<std::string>>(*graph, op, "argsDirection");
                    auto argsWidth = getAttribute<std::vector<int64_t>>(*graph, op, "argsWidth");
                    auto argsName = getAttribute<std::vector<std::string>>(*graph, op, "argsName");
                    auto argsSigned = getAttribute<std::vector<bool>>(*graph, op, "argsSigned");
                    auto argsType = getAttribute<std::vector<std::string>>(*graph, op, "argsType");
                    auto hasReturn = getAttribute<bool>(*graph, op, "hasReturn").value_or(false);
                    auto returnWidth = getAttribute<int64_t>(*graph, op, "returnWidth").value_or(0);
                    auto returnSigned = getAttribute<bool>(*graph, op, "returnSigned").value_or(false);
                    auto returnType = getAttribute<std::string>(*graph, op, "returnType");
                    if (!argsDir || !argsWidth || !argsName ||
                        argsDir->size() != argsWidth->size() || argsDir->size() != argsName->size())
                    {
                        reportError("kDpicImport missing or inconsistent arg metadata", opContext);
                        break;
                    }
                    if (argsSigned && argsSigned->size() != argsDir->size())
                    {
                        reportError("kDpicImport argsSigned size mismatch", opContext);
                        break;
                    }
                    if (argsType && argsType->size() != argsDir->size())
                    {
                        reportError("kDpicImport argsType size mismatch", opContext);
                        break;
                    }
                    std::ostringstream decl;
                    const auto isPackedVector = [](const std::string& typeName) {
                        return typeName == "logic" || typeName == "bit";
                    };
                    decl << "import \"DPI-C\" function ";
                    if (hasReturn)
                    {
                        std::string typeName = "logic";
                        if (returnType && !returnType->empty())
                        {
                            typeName = *returnType;
                        }
                        if (isPackedVector(typeName))
                        {
                            const int64_t width = returnWidth > 0 ? returnWidth : 1;
                            decl << typeName << " ";
                            if (returnSigned)
                            {
                                decl << "signed ";
                            }
                            if (width > 1)
                            {
                                decl << "[" << (width - 1) << ":0] ";
                            }
                        }
                        else
                        {
                            decl << typeName << " ";
                        }
                    }
                    else
                    {
                        decl << "void ";
                    }
                    decl << opContext << " (\n";
                    for (std::size_t i = 0; i < argsDir->size(); ++i)
                    {
                        std::string typeName = "logic";
                        if (argsType && i < argsType->size())
                        {
                            typeName = (*argsType)[i];
                        }
                        decl << "  " << (*argsDir)[i] << " ";
                        if (isPackedVector(typeName))
                        {
                            decl << typeName << " ";
                            if (argsSigned && (*argsSigned)[i])
                            {
                                decl << "signed ";
                            }
                            if ((*argsWidth)[i] > 1)
                            {
                                decl << "[" << ((*argsWidth)[i] - 1) << ":0] ";
                            }
                        }
                        else
                        {
                            decl << typeName << " ";
                        }
                        decl << (*argsName)[i];
                        decl << (i + 1 == argsDir->size() ? "\n" : ",\n");
                    }
                    decl << ");";
                    dpiImportDecls.emplace_back(decl.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kDpicCall:
                {
                    if (operands.empty())
                    {
                        reportError("kDpicCall missing operands", opContext);
                        break;
                    }
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                    auto targetImport = getAttribute<std::string>(*graph, op, "targetImportSymbol");
                    auto inArgName = getAttribute<std::vector<std::string>>(*graph, op, "inArgName");
                    auto outArgName = getAttribute<std::vector<std::string>>(*graph, op, "outArgName");
                    auto inoutArgName = getAttribute<std::vector<std::string>>(*graph, op, "inoutArgName");
                    auto hasReturn = getAttribute<bool>(*graph, op, "hasReturn").value_or(false);
                    if (!eventEdges || !targetImport || !inArgName || !outArgName)
                    {
                        reportError("kDpicCall missing metadata", opContext);
                        break;
                    }
                    if (eventEdges->empty())
                    {
                        reportError("kDpicCall missing eventEdge entries", opContext);
                        break;
                    }
                    if (operands.size() < 1 + eventEdges->size())
                    {
                        reportError("kDpicCall operand count does not match eventEdge", opContext);
                        break;
                    }
                    const std::size_t eventStart = operands.size() - eventEdges->size();
                    if (eventStart < 1)
                    {
                        reportError("kDpicCall missing updateCond operand", opContext);
                        break;
                    }
                    const std::size_t argCount = eventStart - 1;
                    const std::size_t inoutCount = inoutArgName ? inoutArgName->size() : 0;
                    if (argCount != inArgName->size() + inoutCount)
                    {
                        reportError("kDpicCall operand count does not match args", opContext);
                        break;
                    }
                    const std::size_t outputOffset = hasReturn ? 1 : 0;
                    if (results.size() < outputOffset + outArgName->size() + inoutCount)
                    {
                        reportError("kDpicCall result count does not match args", opContext);
                        break;
                    }
                    auto seqKey = buildEventKey(op, eventStart);
                    if (!seqKey)
                    {
                        break;
                    }
                    auto itImport = dpicImports.find(*targetImport);
                    if (itImport == dpicImports.end() || itImport->second.graph == nullptr)
                    {
                        reportError("kDpicCall cannot resolve import symbol", *targetImport);
                        break;
                    }
                    const DpiImportRef &importRef = itImport->second;
                    const grh::ir::Operation importOp = importRef.graph->getOperation(importRef.op);
                    auto importArgs = getAttribute<std::vector<std::string>>(*importRef.graph, importOp, "argsName");
                    auto importDirs = getAttribute<std::vector<std::string>>(*importRef.graph, importOp, "argsDirection");
                    if (!importArgs || !importDirs || importArgs->size() != importDirs->size())
                    {
                        reportError("kDpicCall found malformed import signature", *targetImport);
                        break;
                    }

                    // Declare intermediate regs for outputs and connect them back.
                    for (const auto res : results)
                    {
                        const grh::ir::Value resValue = graph->getValue(res);
                        const std::string tempName = std::string(resValue.symbolText()) + "_intm";
                        ensureRegDecl(tempName, resValue.width(), resValue.isSigned(),
                                      resValue.type(), op.srcLoc());
                        addValueAssign(res, tempName, opId);
                    }

                    bool callOk = true;
                    const bool guardCall = !isConstOne(operands[0]);
                    const int baseIndent = guardCall ? 3 : 2;
                    std::ostringstream stmt;
                    if (guardCall)
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                    }
                    if (inoutArgName && !inoutArgName->empty())
                    {
                        for (std::size_t i = 0; i < inoutArgName->size(); ++i)
                        {
                            const std::size_t operandIndex = 1 + inArgName->size() + i;
                            const std::size_t resultIndex = outputOffset + outArgName->size() + i;
                            if (operandIndex >= operands.size() || resultIndex >= results.size())
                            {
                                reportError("kDpicCall inout arg index out of range", opContext);
                                callOk = false;
                                break;
                            }
                            const std::string tempName = valueName(results[resultIndex]) + "_intm";
                            appendIndented(stmt, baseIndent, tempName + " = " + valueExpr(operands[operandIndex]) + ";");
                        }
                        if (!callOk)
                        {
                            break;
                        }
                    }
                    stmt << std::string(kIndentSizeSv * baseIndent, ' ');
                    if (hasReturn && !results.empty())
                    {
                        stmt << valueName(results[0]) << "_intm = ";
                    }
                    stmt << *targetImport << "(";
                    bool firstArg = true;
                    for (std::size_t i = 0; i < importArgs->size(); ++i)
                    {
                        if (!firstArg)
                        {
                            stmt << ", ";
                        }
                        firstArg = false;
                        const std::string &formal = (*importArgs)[i];
                        const std::string &dir = (*importDirs)[i];
                        if (dir == "input")
                        {
                            int idx = findNameIndex(*inArgName, formal);
                            if (idx < 0 || static_cast<std::size_t>(idx + 1) >= operands.size())
                            {
                                reportError("kDpicCall missing matching input arg " + formal, opContext);
                                callOk = false;
                                break;
                            }
                            stmt << valueExpr(operands[static_cast<std::size_t>(idx + 1)]);
                        }
                        else if (dir == "inout")
                        {
                            if (!inoutArgName)
                            {
                                reportError("kDpicCall missing inoutArgName for " + formal, opContext);
                                callOk = false;
                                break;
                            }
                            int idx = findNameIndex(*inoutArgName, formal);
                            const std::size_t resultIndex =
                                outputOffset + outArgName->size() + static_cast<std::size_t>(idx);
                            if (idx < 0 || resultIndex >= results.size())
                            {
                                reportError("kDpicCall missing matching inout arg " + formal, opContext);
                                callOk = false;
                                break;
                            }
                            stmt << valueName(results[resultIndex]) << "_intm";
                        }
                        else
                        {
                            int idx = findNameIndex(*outArgName, formal);
                            const std::size_t resultIndex =
                                outputOffset + static_cast<std::size_t>(idx);
                            if (idx < 0 || resultIndex >= results.size())
                            {
                                reportError("kDpicCall missing matching output arg " + formal, opContext);
                                callOk = false;
                                break;
                            }
                            stmt << valueName(results[resultIndex]) << "_intm";
                        }
                    }
                    if (!callOk)
                    {
                        break;
                    }
                    stmt << ");\n";
                    if (guardCall)
                    {
                        appendIndented(stmt, 2, "end");
                    }
                    addSequentialStmt(*seqKey, stmt.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kXMRRead:
                case grh::ir::OperationKind::kXMRWrite:
                    reportError("Unresolved XMR operation in emit", opContext);
                    break;
                default:
                    reportWarning("Unsupported operation for SystemVerilog emission", opContext);
                    break;
                }
            }

            // Declare remaining wires for non-port values not defined above.
            for (const auto valueId : graph->values())
            {
                const grh::ir::Value val = graph->getValue(valueId);
                if (val.isInput() || val.isOutput() || val.isInout())
                {
                    continue;
                }
                ensureWireDecl(valueId);
            }

            std::vector<std::string> traceAliasDecls;
            if (options.traceUnderscoreValues)
            {
                std::unordered_set<std::string> aliasNames = declaredNames;
                auto makeUniqueAlias = [&](const std::string &base) -> std::string
                {
                    std::string name = base;
                    int suffix = 0;
                    while (aliasNames.find(name) != aliasNames.end())
                    {
                        ++suffix;
                        name = base + "_" + std::to_string(suffix);
                    }
                    aliasNames.insert(name);
                    return name;
                };

                for (const auto valueId : graph->values())
                {
                    if (!valueId.valid() || valueId.graph != graph->id())
                    {
                        continue;
                    }
                    const grh::ir::Value val = graph->getValue(valueId);
                    if (val.isInput() || val.isOutput() || val.isInout())
                    {
                        continue;
                    }
                    if (val.type() != grh::ir::ValueType::Logic || val.width() <= 0)
                    {
                        continue;
                    }
                    const std::string &orig = valueName(valueId);
                    if (orig.empty() || orig.front() != '_')
                    {
                        continue;
                    }
                    std::string alias = makeUniqueAlias("wd_" + orig);
                    std::string decl = "(* keep *) wire ";
                    decl.append(signedPrefix(val.isSigned()));
                    const std::string range = widthRange(val.width());
                    if (!range.empty())
                    {
                        decl.append(range);
                        decl.push_back(' ');
                    }
                    decl.append(alias);
                    decl.append(" = ");
                    decl.append(orig);
                    decl.append(";");
                    traceAliasDecls.emplace_back(std::move(decl));
                }
            }

            // -------------------------
            // Module emission
            // -------------------------
            out << "module " << moduleName << " (\n";
            {
                bool first = true;
                auto emitPortLine = [&](const std::string &name)
                {
                    auto it = portDecls.find(name);
                    if (it == portDecls.end())
                    {
                        return;
                    }
                    const PortDecl &decl = it->second;
                    const std::string attr = formatSrcAttribute(decl.debug);
                    if (!first)
                    {
                        out << ",\n";
                    }
                    first = false;
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    if (decl.valueType != grh::ir::ValueType::Logic)
                    {
                        out << "  ";
                        if (decl.dir == PortDir::Input)
                        {
                            out << "input ";
                        }
                        else if (decl.dir == PortDir::Output)
                        {
                            out << "output ";
                        }
                        else
                        {
                            out << "inout ";
                        }
                        out << (decl.valueType == grh::ir::ValueType::Real ? "real " : "string ");
                        out << name;
                        return;
                    }
                    if (decl.dir == PortDir::Input)
                    {
                        out << "  input wire ";
                    }
                    else if (decl.dir == PortDir::Output)
                    {
                        out << "  " << (decl.isReg ? "output reg " : "output wire ");
                    }
                    else
                    {
                        out << "  inout wire ";
                    }
                    out << signedPrefix(decl.isSigned);
                    const std::string range = widthRange(decl.width);
                    if (!range.empty())
                    {
                        out << range << " ";
                    }
                    out << name;
                };

                for (const auto &port : graph->inputPorts())
                {
                    if (!port.name.valid())
                    {
                        continue;
                    }
                    emitPortLine(std::string(graph->symbolText(port.name)));
                }
                for (const auto &port : graph->outputPorts())
                {
                    if (!port.name.valid())
                    {
                        continue;
                    }
                    emitPortLine(std::string(graph->symbolText(port.name)));
                }
                for (const auto &port : graph->inoutPorts())
                {
                    if (!port.name.valid())
                    {
                        continue;
                    }
                    emitPortLine(std::string(graph->symbolText(port.name)));
                }
                out << "\n);\n";
            }

            if (!wireDecls.empty())
            {
                out << '\n';
                for (const auto &[name, decl] : wireDecls)
                {
                    out << "  " << formatNetDecl("wire", name, decl) << '\n';
                }
            }

            if (!regDecls.empty())
            {
                out << '\n';
                for (const auto &[name, decl] : regDecls)
                {
                    out << "  " << formatNetDecl("reg", name, decl) << '\n';
                }
            }

            if (!varDecls.empty())
            {
                out << '\n';
                for (const auto &[name, decl] : varDecls)
                {
                    out << "  " << formatVarDecl(decl.type, name, decl.initValue) << '\n';
                }
            }

            if (!traceAliasDecls.empty())
            {
                out << '\n';
                out << "  // Trace aliases for underscore-prefixed internals\n";
                for (const auto &decl : traceAliasDecls)
                {
                    out << "  " << decl << '\n';
                }
            }

            if (!memoryDecls.empty())
            {
                out << '\n';
                auto opSrcLoc = [&](grh::ir::OperationId opId) -> std::optional<grh::ir::SrcLoc>
                {
                    if (!opId.valid())
                    {
                        return std::nullopt;
                    }
                    return graph->getOperation(opId).srcLoc();
                };
                for (const auto &[decl, opPtr] : memoryDecls)
                {
                    const std::string attr =
                        formatSrcAttribute(opSrcLoc(opPtr));
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    out << "  " << decl << '\n';
                }
            }

            if (!instanceDecls.empty())
            {
                out << '\n';
                for (const auto &[inst, opPtr] : instanceDecls)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    out << "  " << inst << '\n';
                }
            }

            if (!dpiImportDecls.empty())
            {
                out << '\n';
                for (const auto &[decl, opPtr] : dpiImportDecls)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    out << "  " << decl << '\n';
                }
            }

            if (!portBindingStmts.empty())
            {
                out << '\n';
                for (const auto &[stmt, opPtr] : portBindingStmts)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    out << "  " << stmt << '\n';
                }
            }

            if (!assignStmts.empty())
            {
                out << '\n';
                for (const auto &[stmt, opPtr] : assignStmts)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    out << "  " << stmt << '\n';
                }
            }

            if (!latchBlocks.empty())
            {
                out << '\n';
                for (const auto &[block, opPtr] : latchBlocks)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << '\n';
                    }
                    out << block << '\n';
                }
            }

            if (!simpleBlocks.empty())
            {
                out << '\n';
                for (const auto &block : simpleBlocks)
                {
                    const std::string attr =
                        formatSrcAttribute(block.op.valid() ? graph->getOperation(block.op).srcLoc()
                                                            : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << '\n';
                    }
                    out << "  " << block.header << " begin\n";
                    for (const auto &stmt : block.stmts)
                    {
                        out << stmt;
                        if (!stmt.empty() && stmt.back() != '\n')
                        {
                            out << '\n';
                        }
                    }
                    out << "  end\n";
                }
            }

            if (!seqBlocks.empty())
            {
                out << '\n';
                for (const auto &seq : seqBlocks)
                {
                    const std::string sens = sensitivityList(*graph, seq.key);
                    if (sens.empty())
                    {
                        reportError("Sequential block missing sensitivity list", graph->symbol());
                        continue;
                    }
                    const std::string attr =
                        formatSrcAttribute(seq.op.valid() ? graph->getOperation(seq.op).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        out << "  " << attr << "\n";
                    }
                    out << "  always " << sens << " begin\n";
                    for (const auto &stmt : seq.stmts)
                    {
                        out << stmt;
                        if (!stmt.empty() && stmt.back() != '\n')
                        {
                            out << '\n';
                        }
                    }
                    out << "  end\n";
                }
            }

            out << "endmodule\n";
        }
        result.artifacts.push_back(outputPath.string());
        return result;
    }

    
} // namespace grh::emit
