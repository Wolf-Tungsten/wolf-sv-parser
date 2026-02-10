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

        void writeValueInlineIr(std::string &out,
                                const grh::ir::GraphView &view,
                                grh::ir::ValueId valueId,
                                const std::vector<std::string> &valueNames,
                                const std::vector<std::string> &opNames,
                                JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("sym", [&]
                                       { appendQuotedString(out, lookupName(valueNames, valueId.index)); });
                                  prop("w", [&]
                                       { out.append(std::to_string(view.valueWidth(valueId))); });
                                  prop("sgn", [&]
                                       { out.append(view.valueSigned(valueId) ? "true" : "false"); });
                                  prop("in", [&]
                                       { out.append(view.valueIsInput(valueId) ? "true" : "false"); });
                                  prop("out", [&]
                                       { out.append(view.valueIsOutput(valueId) ? "true" : "false"); });
                                  prop("inout", [&]
                                       { out.append(view.valueIsInout(valueId) ? "true" : "false"); });
                                  if (const auto def = view.valueDef(valueId); def.valid())
                                  {
                                      prop("def", [&]
                                           { appendQuotedString(out, lookupName(opNames, def.index)); });
                                  }
                                  prop("users", [&]
                                       { writeUsersInlineIr(out, view.valueUsers(valueId), opNames, mode); });
                                  if (auto loc = view.valueSrcLoc(valueId))
                                  {
                                      prop("loc", [&]
                                           { writeDebugInline(out, mode, loc); });
                                  }
                              });
        }

        void writeOperationInlineIr(std::string &out,
                                    const grh::ir::GraphView &view,
                                    grh::ir::OperationId opId,
                                    const std::vector<std::string> &valueNames,
                                    const std::vector<std::string> &opNames,
                                    const grh::ir::GraphSymbolTable &symbols,
                                    JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("sym", [&]
                                       { appendQuotedString(out, lookupName(opNames, opId.index)); });
                                  prop("kind", [&]
                                       { appendQuotedString(out, grh::ir::toString(view.opKind(opId))); });
                                  prop("in", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const auto operand : view.opOperands(opId))
                                           {
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, lookupName(valueNames, operand.index));
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  prop("out", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const auto result : view.opResults(opId))
                                           {
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, lookupName(valueNames, result.index));
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  const auto attrs = view.opAttrs(opId);
                                  if (!attrs.empty())
                                  {
                                      prop("attrs", [&]
                                           { writeAttrsInlineIr(out, attrs, mode); });
                                  }
                                  if (auto loc = view.opSrcLoc(opId))
                                  {
                                      prop("loc", [&]
                                           { writeDebugInline(out, mode, loc); });
                                  }
                              });
        }

        void writePortsPrettyCompactIr(std::string &out,
                                       std::span<const grh::ir::Port> ports,
                                       const std::vector<std::string> &valueNames,
                                       const grh::ir::GraphSymbolTable &symbols,
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
                writePortInline(out, symbols.text(port.name), lookupName(valueNames, port.value.index), mode);
                first = false;
            }
            if (!ports.empty())
            {
                appendNewlineAndIndent(out, indent - 1);
            }
            out.push_back(']');
        }

        void writeInoutPortsPrettyCompactIr(std::string &out,
                                            std::span<const grh::ir::InoutPort> ports,
                                            const std::vector<std::string> &valueNames,
                                            const grh::ir::GraphSymbolTable &symbols,
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
                                           { appendQuotedString(out, symbols.text(port.name)); });
                                      prop("in", [&]
                                           { appendQuotedString(out, lookupName(valueNames, port.in.index)); });
                                      prop("out", [&]
                                           { appendQuotedString(out, lookupName(valueNames, port.out.index)); });
                                      prop("oe", [&]
                                           { appendQuotedString(out, lookupName(valueNames, port.oe.index)); });
                                  });
                first = false;
            }
            if (!ports.empty())
            {
                appendNewlineAndIndent(out, indent - 1);
            }
            out.push_back(']');
        }

        void writeGraphPrettyCompactIr(std::string &out,
                                       const grh::ir::GraphView &view,
                                       const grh::ir::GraphSymbolTable &symbols,
                                       std::string_view graphSymbol,
                                       const std::vector<std::string> &valueNames,
                                       const std::vector<std::string> &opNames,
                                       int baseIndent)
        {
            out.push_back('{');
            int indent = baseIndent + 1;

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "symbol");
            out.append(": ");
            appendQuotedString(out, graphSymbol);
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "vals");
            out.append(": ");
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, view.values(),
                             [&](const grh::ir::ValueId valueId)
                             { writeValueInlineIr(out, view, valueId, valueNames, opNames, JsonPrintMode::PrettyCompact); });
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "ports");
            out.append(": ");
            out.push_back('{');
            int portsIndent = indent + 1;

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "in");
            out.append(": ");
            writePortsPrettyCompactIr(out, view.inputPorts(), valueNames, symbols, JsonPrintMode::PrettyCompact, portsIndent + 1);
            out.push_back(',');

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "out");
            out.append(": ");
            writePortsPrettyCompactIr(out, view.outputPorts(), valueNames, symbols, JsonPrintMode::PrettyCompact, portsIndent + 1);
            out.push_back(',');

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "inout");
            out.append(": ");
            writeInoutPortsPrettyCompactIr(out, view.inoutPorts(), valueNames, symbols, JsonPrintMode::PrettyCompact, portsIndent + 1);

            appendNewlineAndIndent(out, indent);
            out.push_back('}');
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "ops");
            out.append(": ");
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, view.operations(),
                             [&](const grh::ir::OperationId opId)
                             { writeOperationInlineIr(out, view, opId, valueNames, opNames, symbols, JsonPrintMode::PrettyCompact); });

            appendNewlineAndIndent(out, baseIndent);
            out.push_back('}');
        }

        void writeAttributeValue(slang::JsonWriter &writer, const grh::ir::AttributeValue &value)
        {
            if (!grh::ir::attributeValueIsJsonSerializable(value))
            {
                throw std::runtime_error("Attribute value is not JSON serializable");
            }
            std::visit(
                Overloaded{
                    [&](bool v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("bool"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](int64_t v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("int"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](double v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("double"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](const std::string &v)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("string"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](const std::vector<bool> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("bool[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (bool entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<int64_t> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("int[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (int64_t entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<double> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("double[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (double entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<std::string> &arr)
                    {
                        writer.writeProperty("t");
                        writer.writeValue(std::string_view("string[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (const auto &entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    }},
                value);
        }

        void writeSrcLocJson(slang::JsonWriter &writer, const std::optional<grh::ir::SrcLoc> &srcLoc)
        {
            if (!srcLoc || srcLoc->file.empty())
            {
                return;
            }
            writer.writeProperty("loc");
            writer.startObject();
            writer.writeProperty("file");
            writer.writeValue(srcLoc->file);
            writer.writeProperty("line");
            writer.writeValue(static_cast<int64_t>(srcLoc->line));
            writer.writeProperty("col");
            writer.writeValue(static_cast<int64_t>(srcLoc->column));
            writer.writeProperty("endLine");
            writer.writeValue(static_cast<int64_t>(srcLoc->endLine));
            writer.writeProperty("endCol");
            writer.writeValue(static_cast<int64_t>(srcLoc->endColumn));
            writer.endObject();
        }

        void writeGraphViewJson(slang::JsonWriter &writer,
                                const grh::ir::GraphView &view,
                                const grh::ir::GraphSymbolTable &symbols,
                                std::string_view graphSymbol,
                                const std::vector<std::string> &valueNames,
                                const std::vector<std::string> &opNames)
        {
            writer.startObject();
            writer.writeProperty("symbol");
            writer.writeValue(graphSymbol);

            writer.writeProperty("vals");
            writer.startArray();
            for (const auto valueId : view.values())
            {
                writer.startObject();
                writer.writeProperty("sym");
                writer.writeValue(lookupName(valueNames, valueId.index));
                writer.writeProperty("w");
                writer.writeValue(static_cast<int64_t>(view.valueWidth(valueId)));
                writer.writeProperty("sgn");
                writer.writeValue(view.valueSigned(valueId));
                writer.writeProperty("in");
                writer.writeValue(view.valueIsInput(valueId));
                writer.writeProperty("out");
                writer.writeValue(view.valueIsOutput(valueId));
                writer.writeProperty("inout");
                writer.writeValue(view.valueIsInout(valueId));
                if (const auto def = view.valueDef(valueId); def.valid())
                {
                    writer.writeProperty("def");
                    writer.writeValue(lookupName(opNames, def.index));
                }

                writer.writeProperty("users");
                writer.startArray();
                for (const auto &user : view.valueUsers(valueId))
                {
                    writer.startObject();
                    writer.writeProperty("op");
                    writer.writeValue(lookupName(opNames, user.operation.index));
                    writer.writeProperty("idx");
                    writer.writeValue(static_cast<int64_t>(user.operandIndex));
                    writer.endObject();
                }
                writer.endArray();
                writeSrcLocJson(writer, view.valueSrcLoc(valueId));
                writer.endObject();
            }
            writer.endArray();

            writer.writeProperty("ports");
            writer.startObject();

            writer.writeProperty("in");
            writer.startArray();
            for (const auto &port : view.inputPorts())
            {
                writer.startObject();
                writer.writeProperty("name");
                writer.writeValue(symbols.text(port.name));
                writer.writeProperty("val");
                writer.writeValue(lookupName(valueNames, port.value.index));
                writer.endObject();
            }
            writer.endArray();

            writer.writeProperty("out");
            writer.startArray();
            for (const auto &port : view.outputPorts())
            {
                writer.startObject();
                writer.writeProperty("name");
                writer.writeValue(symbols.text(port.name));
                writer.writeProperty("val");
                writer.writeValue(lookupName(valueNames, port.value.index));
                writer.endObject();
            }
            writer.endArray();

            writer.writeProperty("inout");
            writer.startArray();
            for (const auto &port : view.inoutPorts())
            {
                writer.startObject();
                writer.writeProperty("name");
                writer.writeValue(symbols.text(port.name));
                writer.writeProperty("in");
                writer.writeValue(lookupName(valueNames, port.in.index));
                writer.writeProperty("out");
                writer.writeValue(lookupName(valueNames, port.out.index));
                writer.writeProperty("oe");
                writer.writeValue(lookupName(valueNames, port.oe.index));
                writer.endObject();
            }
            writer.endArray();

            writer.endObject(); // ports

            writer.writeProperty("ops");
            writer.startArray();
            for (const auto opId : view.operations())
            {
                writer.startObject();
                writer.writeProperty("sym");
                writer.writeValue(lookupName(opNames, opId.index));
                writer.writeProperty("kind");
                writer.writeValue(grh::ir::toString(view.opKind(opId)));

                writer.writeProperty("in");
                writer.startArray();
                for (const auto operand : view.opOperands(opId))
                {
                    writer.writeValue(lookupName(valueNames, operand.index));
                }
                writer.endArray();

                writer.writeProperty("out");
                writer.startArray();
                for (const auto result : view.opResults(opId))
                {
                    writer.writeValue(lookupName(valueNames, result.index));
                }
                writer.endArray();

                const auto attrs = view.opAttrs(opId);
                if (!attrs.empty())
                {
                    writer.writeProperty("attrs");
                    writer.startObject();
                    for (const auto &attr : attrs)
                    {
                        writer.writeProperty(attr.key);
                        writer.startObject();
                        writeAttributeValue(writer, attr.value);
                        writer.endObject();
                    }
                    writer.endObject();
                }

                writeSrcLocJson(writer, view.opSrcLoc(opId));
                writer.endObject();
            }
            writer.endArray();

            writer.endObject();
        }

        std::string serializeGraphViewWithJsonWriter(const grh::ir::GraphView &view,
                                                     const grh::ir::GraphSymbolTable &symbols,
                                                     std::string_view graphSymbol,
                                                     const std::vector<std::string> &valueNames,
                                                     const std::vector<std::string> &opNames,
                                                     bool pretty)
        {
            slang::JsonWriter writer;
            writer.setPrettyPrint(pretty);
            writer.startObject();

            writer.writeProperty("graphs");
            writer.startArray();
            writeGraphViewJson(writer, view, symbols, graphSymbol, valueNames, opNames);
            writer.endArray();

            writer.writeProperty("tops");
            writer.startArray();
            writer.writeValue(graphSymbol);
            writer.endArray();

            writer.endObject();
            return std::string(writer.view());
        }

        std::string serializeGraphViewPrettyCompact(const grh::ir::GraphView &view,
                                                    const grh::ir::GraphSymbolTable &symbols,
                                                    std::string_view graphSymbol,
                                                    const std::vector<std::string> &valueNames,
                                                    const std::vector<std::string> &opNames)
        {
            std::string out;
            int indent = 0;

            out.push_back('{');
            indent = 1;
            appendNewlineAndIndent(out, indent);

            appendQuotedString(out, "graphs");
            out.append(": [");
            appendNewlineAndIndent(out, indent + 1);
            writeGraphPrettyCompactIr(out, view, symbols, graphSymbol, valueNames, opNames, indent + 1);
            appendNewlineAndIndent(out, indent);
            out.push_back(']');
            out.push_back(',');
            appendNewlineAndIndent(out, indent);

            appendQuotedString(out, "tops");
            out.append(": [");
            appendNewlineAndIndent(out, indent + 1);
            appendQuotedString(out, graphSymbol);
            appendNewlineAndIndent(out, indent);
            out.push_back(']');

            appendNewlineAndIndent(out, indent - 1);
            out.push_back('}');
            return out;
        }

        std::string serializeGraphViewJson(const grh::ir::GraphView &view,
                                           const grh::ir::GraphSymbolTable &symbols,
                                           std::string_view graphSymbol,
                                           const std::vector<std::string> &valueNames,
                                           const std::vector<std::string> &opNames,
                                           JsonPrintMode mode)
        {
            switch (mode)
            {
            case JsonPrintMode::Compact:
                return serializeGraphViewWithJsonWriter(view, symbols, graphSymbol, valueNames, opNames, /* pretty */ false);
            case JsonPrintMode::Pretty:
                return serializeGraphViewWithJsonWriter(view, symbols, graphSymbol, valueNames, opNames, /* pretty */ true);
            case JsonPrintMode::PrettyCompact:
            default:
                return serializeGraphViewPrettyCompact(view, symbols, graphSymbol, valueNames, opNames);
            }
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

    EmitResult EmitJSON::emitGraphView(const grh::ir::GraphView &view,
                                       const grh::ir::GraphSymbolTable &symbols,
                                       std::string_view graphSymbol,
                                       const EmitOptions &options)
    {
        EmitResult result;
        if (graphSymbol.empty())
        {
            reportError("GraphView JSON emit requires graph symbol");
            result.success = false;
            return result;
        }

        const std::size_t valueCount = view.values().size();
        const std::size_t opCount = view.operations().size();
        std::vector<std::string> valueNames(valueCount);
        std::vector<std::string> opNames(opCount);

        for (const auto valueId : view.values())
        {
            if (valueId.index == 0 || valueId.index > valueCount)
            {
                reportError("ValueId out of range during JSON emit");
                continue;
            }
            const std::size_t idx = valueId.index - 1;
            const grh::ir::SymbolId sym = view.valueSymbol(valueId);
            if (!sym.valid())
            {
                reportError("Value missing symbol", "value_" + std::to_string(valueId.index));
                valueNames[idx] = "value_" + std::to_string(valueId.index);
                continue;
            }
            valueNames[idx] = std::string(symbols.text(sym));
        }

        for (const auto opId : view.operations())
        {
            if (opId.index == 0 || opId.index > opCount)
            {
                reportError("OperationId out of range during JSON emit");
                continue;
            }
            const std::size_t idx = opId.index - 1;
            const grh::ir::SymbolId sym = view.opSymbol(opId);
            if (!sym.valid())
            {
                reportError("Operation missing symbol", "op_" + std::to_string(opId.index));
                opNames[idx] = "op_" + std::to_string(opId.index);
                continue;
            }
            opNames[idx] = std::string(symbols.text(sym));
        }

        std::string jsonText;
        try
        {
            jsonText = serializeGraphViewJson(view, symbols, graphSymbol, valueNames, opNames, options.jsonMode);
        }
        catch (const std::exception &ex)
        {
            reportError("Failed to serialize GraphView to JSON: " + std::string(ex.what()));
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
        if (diagnostics() && diagnostics()->hasError())
        {
            result.success = false;
        }
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

        struct PortDecl
        {
            PortDir dir = PortDir::Input;
            int64_t width = 1;
            bool isSigned = false;
            bool isReg = false;
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

        template <typename T>
        std::optional<T> getAttribute(const grh::ir::GraphView &view,
                                      grh::ir::OperationId op,
                                      std::string_view key)
        {
            auto attr = view.opAttr(op, key);
            if (!attr)
            {
                return std::nullopt;
            }
            const auto &value = *attr;
            if (const auto *ptr = std::get_if<T>(&value))
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

        std::ostringstream moduleBuffer;
        bool firstModule = true;
        for (const grh::ir::Graph *graph : graphsSortedByName(netlist))
        {
            if (!firstModule)
            {
                moduleBuffer << '\n';
            }
            firstModule = false;

            const auto moduleNameIt = emittedModuleNames.find(graph->symbol());
            const std::string &moduleName = moduleNameIt != emittedModuleNames.end() ? moduleNameIt->second : graph->symbol();

            auto valueName = [&](grh::ir::ValueId valueId) -> std::string
            {
                grh::ir::Value value = graph->getValue(valueId);
                return std::string(value.symbolText());
            };
            auto opName = [&](grh::ir::OperationId opId) -> std::string
            {
                grh::ir::Operation op = graph->getOperation(opId);
                return std::string(op.symbolText());
            };
            auto isPortValue = [&](grh::ir::ValueId valueId) -> bool
            {
                const grh::ir::Value value = graph->getValue(valueId);
                return value.isInput() || value.isOutput() || value.isInout();
            };
            auto constLiteralFor = [&](grh::ir::ValueId valueId) -> std::optional<std::string>
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
            auto assignSourceFor = [&](grh::ir::ValueId valueId) -> std::optional<grh::ir::ValueId>
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
                if (defOp.kind() != grh::ir::OperationKind::kAssign)
                {
                    return std::nullopt;
                }
                const auto &ops = defOp.operands();
                if (ops.empty())
                {
                    return std::nullopt;
                }
                return ops.front();
            };
            std::unordered_set<grh::ir::ValueId, grh::ir::ValueIdHash> materializeValues;
            materializeValues.reserve(graph->operations().size());
            for (const auto opId : graph->operations())
            {
                const grh::ir::Operation op = graph->getOperation(opId);
                switch (op.kind())
                {
                case grh::ir::OperationKind::kSliceStatic:
                case grh::ir::OperationKind::kSliceDynamic:
                case grh::ir::OperationKind::kSliceArray:
                    if (!op.operands().empty())
                    {
                        materializeValues.insert(op.operands().front());
                    }
                    break;
                case grh::ir::OperationKind::kMemoryWritePort:
                    if (op.operands().size() > 3)
                    {
                        materializeValues.insert(op.operands()[2]);
                        materializeValues.insert(op.operands()[3]);
                    }
                    break;
                default:
                    break;
                }
                auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                if (eventEdges && !eventEdges->empty())
                {
                    const auto &ops = op.operands();
                    if (ops.size() >= eventEdges->size())
                    {
                        const std::size_t start = ops.size() - eventEdges->size();
                        for (std::size_t i = start; i < ops.size(); ++i)
                        {
                            if (ops[i].valid())
                            {
                                materializeValues.insert(ops[i]);
                            }
                        }
                    }
                }
            }
            std::unordered_set<grh::ir::ValueId, grh::ir::ValueIdHash> resolvingExpr;
            std::function<std::string(grh::ir::ValueId)> valueExpr =
                [&](grh::ir::ValueId valueId) -> std::string
            {
                if (!valueId.valid())
                {
                    return {};
                }
                if (materializeValues.find(valueId) != materializeValues.end())
                {
                    return valueName(valueId);
                }
                if (!resolvingExpr.insert(valueId).second)
                {
                    return valueName(valueId);
                }
                if (!isPortValue(valueId))
                {
                    if (auto literal = constLiteralFor(valueId))
                    {
                        resolvingExpr.erase(valueId);
                        return *literal;
                    }
                    if (auto source = assignSourceFor(valueId))
                    {
                        std::string resolved = valueExpr(*source);
                        resolvingExpr.erase(valueId);
                        return resolved;
                    }
                }
                resolvingExpr.erase(valueId);
                return valueName(valueId);
            };
            auto concatOperandExpr = [&](grh::ir::ValueId valueId) -> std::string
            {
                if (auto literal = constLiteralFor(valueId))
                {
                    if (auto sized = sizedLiteralIfUnsized(*literal, graph->getValue(valueId).width()))
                    {
                        return *sized;
                    }
                }
                return valueExpr(valueId);
            };
            auto isSimpleIdentifier = [](std::string_view expr) -> bool
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
            };
            auto parenIfNeeded = [&](const std::string &expr) -> std::string
            {
                if (expr.empty() || isSimpleIdentifier(expr))
                {
                    return expr;
                }
                if (expr.front() == '(' && expr.back() == ')')
                {
                    return expr;
                }
                return "(" + expr + ")";
            };

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
                                           value.srcLoc()};
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
                                           value.srcLoc()};
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
                                           value.srcLoc()};
            }

            // Book-keeping for declarations.
            std::unordered_set<std::string> declaredNames;
            for (const auto &[name, _] : portDecls)
            {
                declaredNames.insert(name);
            }

            // Storage for various sections.
            std::map<std::string, NetDecl, std::less<>> regDecls;
            std::map<std::string, NetDecl, std::less<>> wireDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> memoryDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> instanceDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> dpiImportDecls;
            std::vector<std::pair<std::string, grh::ir::OperationId>> assignStmts;
            std::vector<std::pair<std::string, grh::ir::OperationId>> portBindingStmts;
            std::vector<std::pair<std::string, grh::ir::OperationId>> latchBlocks;
            std::vector<SeqBlock> seqBlocks;
            std::unordered_set<std::string> instanceNamesUsed;

            auto ensureRegDecl = [&](const std::string &name, int64_t width, bool isSigned,
                                     const std::optional<grh::ir::SrcLoc> &debug = std::nullopt)
            {
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

            auto logicalOperand = [&](grh::ir::ValueId valueId) -> std::string
            {
                const int64_t width = graph->getValue(valueId).width();
                const std::string name = valueExpr(valueId);
                if (width == 1)
                {
                    return name;
                }
                return "(|" + name + ")";
            };
            auto extendOperand = [&](grh::ir::ValueId valueId, int64_t targetWidth) -> std::string
            {
                const grh::ir::Value value = graph->getValue(valueId);
                const int64_t width = value.width();
                const std::string name = valueExpr(valueId);
                if (targetWidth <= 0 || width <= 0 || width == targetWidth)
                {
                    return name;
                }
                const int64_t diff = targetWidth - width;
                if (diff <= 0)
                {
                    return name;
                }
                if (value.isSigned())
                {
                    return "{{" + std::to_string(diff) + "{" + name + "[" + std::to_string(width - 1) + "]}}," + name + "}";
                }
                return "{{" + std::to_string(diff) + "{1'b0}}," + name + "}";
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
                    if (!isPortValue(results[0]) &&
                        materializeValues.find(results[0]) == materializeValues.end())
                    {
                        break;
                    }
                    auto constValue = getAttribute<std::string>(*graph, op, "constValue");
                    if (!constValue)
                    {
                        reportError("kConstant missing constValue attribute", opContext);
                        break;
                    }
                    addAssign("assign " + valueName(results[0]) + " = " + *constValue + ";", opId);
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
                                                ? extendOperand(operands[0], resultWidth)
                                                : valueExpr(operands[0]);
                    const std::string rhs = resultWidth > 0
                                                ? extendOperand(operands[1], resultWidth)
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
                    addAssign("assign " + valueName(results[0]) + " = " + lhsExpr + " " + tok + " " + rhsExpr + ";", opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                case grh::ir::OperationKind::kAnd:
                case grh::ir::OperationKind::kOr:
                case grh::ir::OperationKind::kXor:
                case grh::ir::OperationKind::kXnor:
                case grh::ir::OperationKind::kShl:
                case grh::ir::OperationKind::kLShr:
                case grh::ir::OperationKind::kAShr:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", opContext);
                        break;
                    }
                    const std::string tok = binOpToken(op.kind());
                    addAssign("assign " + valueName(results[0]) + " = " + valueExpr(operands[0]) + " " + tok + " " +
                                  valueExpr(operands[1]) + ";",
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
                    addAssign("assign " + valueName(results[0]) + " = " +
                                  extendOperand(operands[0], resultWidth) + " " + tok + " " +
                                  extendOperand(operands[1], resultWidth) + ";",
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
                    addAssign("assign " + valueName(results[0]) + " = " + logicalOperand(operands[0]) + " " + tok +
                                  " " + logicalOperand(operands[1]) + ";",
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
                    addAssign("assign " + valueName(results[0]) + " = " + tok + valueExpr(operands[0]) + ";", opId);
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
                    addAssign("assign " + valueName(results[0]) + " = !" + logicalOperand(operands[0]) + ";", opId);
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
                    const std::string lhs = extendOperand(operands[1], resultWidth);
                    const std::string rhs = extendOperand(operands[2], resultWidth);
                    addAssign("assign " + valueName(results[0]) + " = " + valueExpr(operands[0]) + " ? " +
                                  lhs + " : " + rhs + ";",
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
                    if (!isPortValue(results[0]) &&
                        materializeValues.find(results[0]) == materializeValues.end())
                    {
                        break;
                    }
                    int64_t resultWidth = graph->getValue(results[0]).width();
                    const std::string rhs = extendOperand(operands[0], resultWidth);
                    addAssign("assign " + valueName(results[0]) + " = " + rhs + ";", opId);
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
                        addAssign("assign " + valueName(results[0]) + " = " + valueExpr(operands[0]) + ";", opId);
                        ensureWireDecl(results[0]);
                        break;
                    }
                    std::ostringstream expr;
                    expr << "assign " << valueName(results[0]) << " = {";
                    for (std::size_t i = 0; i < operands.size(); ++i)
                    {
                        if (i != 0)
                        {
                            expr << ", ";
                        }
                        expr << concatOperandExpr(operands[i]);
                    }
                    expr << "};";
                    addAssign(expr.str(), opId);
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
                    expr << "assign " << valueName(results[0]) << " = {" << *rep << "{" << concatOperandExpr(operands[0]) << "}};";
                    addAssign(expr.str(), opId);
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
                    expr << "assign " << valueName(results[0]) << " = ";
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
                    expr << ";";
                    addAssign(expr.str(), opId);
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
                    expr << "assign " << valueName(results[0]) << " = ";
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
                        expr << parenIfNeeded(valueExpr(operands[0])) << "[" << parenIfNeeded(valueExpr(operands[1])) << "]";
                    }
                    else
                    {
                        expr << parenIfNeeded(valueExpr(operands[0])) << "[" << parenIfNeeded(valueExpr(operands[1])) << " +: " << *width << "]";
                    }
                    expr << ";";
                    addAssign(expr.str(), opId);
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
                    expr << "assign " << valueName(results[0]) << " = ";
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
                    expr << ";";
                    addAssign(expr.str(), opId);
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
                    ensureRegDecl(regName, dValue.width(), dValue.isSigned(), op.srcLoc());
                    const bool directDrive = valueName(q) == regName;
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(q);
                    }
                    else
                    {
                        ensureWireDecl(q);
                        addAssign("assign " + valueName(q) + " = " + regName + ";", opId);
                    }

                    std::ostringstream block;
                    if (isAlwaysTrue(updateCond))
                    {
                        appendIndented(block, 1, "always @* begin");
                        appendIndented(block, 2, regName + " = " + valueExpr(nextValue) + ";");
                        appendIndented(block, 1, "end");
                    }
                    else
                    {
                        std::ostringstream stmt;
                        appendIndented(stmt, 2, "if (" + valueExpr(updateCond) + ") begin");
                        appendIndented(stmt, 3, regName + " = " + valueExpr(nextValue) + ";");
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

                    ensureRegDecl(regName, dValue.width(), dValue.isSigned(), op.srcLoc());
                    const bool directDrive = valueName(q) == regName;
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(q);
                    }
                    else
                    {
                        ensureWireDecl(q);
                        addAssign("assign " + valueName(q) + " = " + regName + ";", opId);
                    }

                    std::ostringstream stmt;
                    if (isConstOne(updateCond))
                    {
                        appendIndented(stmt, 2, regName + " <= " + valueExpr(nextValue) + ";");
                    }
                    else
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(updateCond) + ") begin");
                        appendIndented(stmt, 3, regName + " <= " + valueExpr(nextValue) + ";");
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
                case grh::ir::OperationKind::kDisplay:
                {
                    if (operands.size() < 1)
                    {
                        reportError("kDisplay missing operands", opContext);
                        break;
                    }
                    auto format = getAttribute<std::string>(*graph, op, "formatString");
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                    auto displayKind = getAttribute<std::string>(*graph, op, "displayKind");
                    const bool hasExitCode = getAttribute<bool>(*graph, op, "hasExitCode").value_or(false);
                    if (!format || !eventEdges)
                    {
                        reportError("kDisplay missing eventEdge or formatString", opContext);
                        break;
                    }
                    if (eventEdges->empty())
                    {
                        reportError("kDisplay missing eventEdge entries", opContext);
                        break;
                    }
                    const std::size_t baseIndex = 1 + (hasExitCode ? 1 : 0);
                    if (operands.size() < baseIndex + eventEdges->size())
                    {
                        reportError("kDisplay operand count does not match eventEdge", opContext);
                        break;
                    }
                    const std::size_t argCount = operands.size() - baseIndex - eventEdges->size();
                    const std::size_t eventStart = baseIndex + argCount;
                    auto seqKey = buildEventKey(op, eventStart);
                    if (!seqKey)
                    {
                        break;
                    }
                    const bool guardDisplay = !isConstOne(operands[0]);
                    const int baseIndent = guardDisplay ? 3 : 2;
                    std::string taskName = displayKind.value_or(std::string("display"));
                    if (taskName.empty())
                    {
                        taskName = "display";
                    }
                    std::ostringstream stmt;
                    if (guardDisplay)
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                    }
                    stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$" << taskName << "(";
                    if (taskName == "fatal" && hasExitCode)
                    {
                        stmt << valueExpr(operands[1]) << ", ";
                    }
                    stmt << "\"" << *format << "\"";
                    for (std::size_t i = 0; i < argCount; ++i)
                    {
                        stmt << ", " << valueExpr(operands[baseIndex + i]);
                    }
                    stmt << ");\n";
                    if (guardDisplay)
                    {
                        appendIndented(stmt, 2, "end");
                    }
                    addSequentialStmt(*seqKey, stmt.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kFinish:
                {
                    if (operands.size() < 1)
                    {
                        reportError("kFinish missing operands", opContext);
                        break;
                    }
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                    const bool hasExitCode = getAttribute<bool>(*graph, op, "hasExitCode").value_or(false);
                    if (!eventEdges)
                    {
                        reportError("kFinish missing eventEdge", opContext);
                        break;
                    }
                    if (eventEdges->empty())
                    {
                        reportError("kFinish missing eventEdge entries", opContext);
                        break;
                    }
                    const std::size_t baseIndex = 1 + (hasExitCode ? 1 : 0);
                    if (operands.size() < baseIndex + eventEdges->size())
                    {
                        reportError("kFinish operand count does not match eventEdge", opContext);
                        break;
                    }
                    const std::size_t eventStart = baseIndex;
                    auto seqKey = buildEventKey(op, eventStart);
                    if (!seqKey)
                    {
                        break;
                    }
                    const bool guardFinish = !isConstOne(operands[0]);
                    const int baseIndent = guardFinish ? 3 : 2;
                    std::ostringstream stmt;
                    if (guardFinish)
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                    }
                    stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$finish";
                    if (hasExitCode)
                    {
                        stmt << "(" << valueExpr(operands[1]) << ")";
                    }
                    stmt << ";\n";
                    if (guardFinish)
                    {
                        appendIndented(stmt, 2, "end");
                    }
                    addSequentialStmt(*seqKey, stmt.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kFwrite:
                {
                    if (operands.size() < 2)
                    {
                        reportError("kFwrite missing operands", opContext);
                        break;
                    }
                    auto format = getAttribute<std::string>(*graph, op, "formatString");
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                    if (!format || !eventEdges)
                    {
                        reportError("kFwrite missing eventEdge or formatString", opContext);
                        break;
                    }
                    if (eventEdges->empty())
                    {
                        reportError("kFwrite missing eventEdge entries", opContext);
                        break;
                    }
                    if (operands.size() < 2 + eventEdges->size())
                    {
                        reportError("kFwrite operand count does not match eventEdge", opContext);
                        break;
                    }
                    const std::size_t argCount = operands.size() - 2 - eventEdges->size();
                    const std::size_t eventStart = 2 + argCount;
                    auto seqKey = buildEventKey(op, eventStart);
                    if (!seqKey)
                    {
                        break;
                    }
                    const bool guardDisplay = !isConstOne(operands[0]);
                    const int baseIndent = guardDisplay ? 3 : 2;
                    std::ostringstream stmt;
                    if (guardDisplay)
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                    }
                    stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$fwrite(" << valueExpr(operands[1]) << ", \"" << *format << "\"";
                    for (std::size_t i = 0; i < argCount; ++i)
                    {
                        stmt << ", " << valueExpr(operands[2 + i]);
                    }
                    stmt << ");\n";
                    if (guardDisplay)
                    {
                        appendIndented(stmt, 2, "end");
                    }
                    addSequentialStmt(*seqKey, stmt.str(), opId);
                    break;
                }
                case grh::ir::OperationKind::kAssert:
                {
                    if (operands.size() < 2)
                    {
                        reportError("kAssert missing operands", opContext);
                        break;
                    }
                    auto eventEdges = getAttribute<std::vector<std::string>>(*graph, op, "eventEdge");
                    if (!eventEdges)
                    {
                        reportError("kAssert missing eventEdge", opContext);
                        break;
                    }
                    if (eventEdges->empty())
                    {
                        reportError("kAssert missing eventEdge entries", opContext);
                        break;
                    }
                    auto message = getAttribute<std::string>(*graph, op, "message").value_or(std::string("Assertion failed"));
                    auto seqKey = buildEventKey(op, 2);
                    if (!seqKey)
                    {
                        break;
                    }
                    std::ostringstream stmt;
                    if (isConstOne(operands[0]))
                    {
                        appendIndented(stmt, 2, "if (!" + valueExpr(operands[1]) + ") begin");
                    }
                    else
                    {
                        appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + " && !" + valueExpr(operands[1]) + ") begin");
                    }
                    appendIndented(stmt, 3, "$fatal(\"" + message + " at time %0t\", $time);");
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(*seqKey, stmt.str(), opId);
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
                    decl << "import \"DPI-C\" function ";
                    if (hasReturn)
                    {
                        const bool useLogic =
                            !(returnType && !returnType->empty() && *returnType != "logic");
                        if (useLogic)
                        {
                            const int64_t width = returnWidth > 0 ? returnWidth : 1;
                            decl << "logic ";
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
                            decl << *returnType << " ";
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
                        if (typeName == "logic")
                        {
                            decl << "logic ";
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
                        ensureRegDecl(tempName, resValue.width(), resValue.isSigned(), op.srcLoc());
                        addAssign("assign " + std::string(resValue.symbolText()) + " = " + tempName + ";", opId);
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
                if (!isPortValue(valueId))
                {
                    const grh::ir::OperationId defOpId = val.definingOp();
                    if (defOpId.valid())
                    {
                        const grh::ir::Operation defOp = graph->getOperation(defOpId);
                        if (defOp.kind() == grh::ir::OperationKind::kConstant ||
                            defOp.kind() == grh::ir::OperationKind::kAssign)
                        {
                            if (materializeValues.find(valueId) == materializeValues.end())
                            {
                                continue;
                            }
                        }
                    }
                }
                ensureWireDecl(valueId);
            }

            // -------------------------
            // Module emission
            // -------------------------
            moduleBuffer << "module " << moduleName << " (\n";
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
                        moduleBuffer << ",\n";
                    }
                    first = false;
                    if (!attr.empty())
                    {
                        moduleBuffer << "  " << attr << "\n";
                    }
                    if (decl.dir == PortDir::Input)
                    {
                        moduleBuffer << "  input wire ";
                    }
                    else if (decl.dir == PortDir::Output)
                    {
                        moduleBuffer << "  " << (decl.isReg ? "output reg " : "output wire ");
                    }
                    else
                    {
                        moduleBuffer << "  inout wire ";
                    }
                    moduleBuffer << signedPrefix(decl.isSigned);
                    const std::string range = widthRange(decl.width);
                    if (!range.empty())
                    {
                        moduleBuffer << range << " ";
                    }
                    moduleBuffer << name;
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
                moduleBuffer << "\n);\n";
            }

            if (!wireDecls.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[name, decl] : wireDecls)
                {
                    moduleBuffer << "  " << formatNetDecl("wire", name, decl) << '\n';
                }
            }

            if (!regDecls.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[name, decl] : regDecls)
                {
                    moduleBuffer << "  " << formatNetDecl("reg", name, decl) << '\n';
                }
            }

            if (!memoryDecls.empty())
            {
                moduleBuffer << '\n';
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
                        moduleBuffer << "  " << attr << "\n";
                    }
                    moduleBuffer << "  " << decl << '\n';
                }
            }

            if (!instanceDecls.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[inst, opPtr] : instanceDecls)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        moduleBuffer << "  " << attr << "\n";
                    }
                    moduleBuffer << "  " << inst << '\n';
                }
            }

            if (!dpiImportDecls.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[decl, opPtr] : dpiImportDecls)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        moduleBuffer << "  " << attr << "\n";
                    }
                    moduleBuffer << "  " << decl << '\n';
                }
            }

            if (!portBindingStmts.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[stmt, opPtr] : portBindingStmts)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        moduleBuffer << "  " << attr << "\n";
                    }
                    moduleBuffer << "  " << stmt << '\n';
                }
            }

            if (!assignStmts.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[stmt, opPtr] : assignStmts)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        moduleBuffer << "  " << attr << "\n";
                    }
                    moduleBuffer << "  " << stmt << '\n';
                }
            }

            if (!latchBlocks.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[block, opPtr] : latchBlocks)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr.valid() ? graph->getOperation(opPtr).srcLoc() : std::optional<grh::ir::SrcLoc>{});
                    if (!attr.empty())
                    {
                        moduleBuffer << "  " << attr << '\n';
                    }
                    moduleBuffer << block << '\n';
                }
            }

            if (!seqBlocks.empty())
            {
                moduleBuffer << '\n';
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
                        moduleBuffer << "  " << attr << "\n";
                    }
                    moduleBuffer << "  always " << sens << " begin\n";
                    for (const auto &stmt : seq.stmts)
                    {
                        moduleBuffer << stmt;
                        if (!stmt.empty() && stmt.back() != '\n')
                        {
                            moduleBuffer << '\n';
                        }
                    }
                    moduleBuffer << "  end\n";
                }
            }

            moduleBuffer << "endmodule\n";
        }

        const std::string filename = options.outputFilename.value_or(std::string("grh.sv"));
        const std::filesystem::path outputPath = resolveOutputDir(options) / filename;
        auto stream = openOutputFile(outputPath);
        if (!stream)
        {
            result.success = false;
            return result;
        }
        *stream << moduleBuffer.str();
        result.artifacts.push_back(outputPath.string());
        return result;
    }

    EmitResult EmitSystemVerilog::emitGraphView(const grh::ir::GraphView &view,
                                                const grh::ir::GraphSymbolTable &symbols,
                                                std::string_view moduleName,
                                                const EmitOptions &options)
    {
        EmitResult result;
        if (moduleName.empty())
        {
            reportError("SystemVerilog emit requires a module name");
            result.success = false;
            return result;
        }

        const std::size_t valueCount = view.values().size();
        const std::size_t opCount = view.operations().size();
        std::vector<std::string> valueNames(valueCount);
        std::vector<std::string> opNames(opCount);

        for (const auto valueId : view.values())
        {
            if (valueId.index == 0 || valueId.index > valueCount)
            {
                reportError("ValueId out of range during emit");
                continue;
            }
            const std::size_t idx = valueId.index - 1;
            const std::string fallback = "value_" + std::to_string(valueId.index);
            const grh::ir::SymbolId sym = view.valueSymbol(valueId);
            if (!sym.valid())
            {
                reportError("Value missing symbol", fallback);
                valueNames[idx] = fallback;
                continue;
            }
            valueNames[idx] = std::string(symbols.text(sym));
        }

        for (const auto opId : view.operations())
        {
            if (opId.index == 0 || opId.index > opCount)
            {
                reportError("OperationId out of range during emit");
                continue;
            }
            const std::size_t idx = opId.index - 1;
            const grh::ir::SymbolId sym = view.opSymbol(opId);
            if (!sym.valid())
            {
                continue;
            }
            opNames[idx] = std::string(symbols.text(sym));
        }

        auto valueName = [&](grh::ir::ValueId valueId) -> const std::string &
        {
            static const std::string kEmpty;
            if (valueId.index == 0 || valueId.index > valueNames.size())
            {
                return kEmpty;
            }
            return valueNames[valueId.index - 1];
        };
        auto opName = [&](grh::ir::OperationId opId) -> const std::string &
        {
            static const std::string kEmpty;
            if (opId.index == 0 || opId.index > opNames.size())
            {
                return kEmpty;
            }
            return opNames[opId.index - 1];
        };
        auto isPortValue = [&](grh::ir::ValueId valueId) -> bool
        {
            return view.valueIsInput(valueId) || view.valueIsOutput(valueId) || view.valueIsInout(valueId);
        };
        auto constLiteralFor = [&](grh::ir::ValueId valueId) -> std::optional<std::string>
        {
            if (!valueId.valid())
            {
                return std::nullopt;
            }
            const grh::ir::OperationId defOpId = view.valueDef(valueId);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            if (view.opKind(defOpId) != grh::ir::OperationKind::kConstant)
            {
                return std::nullopt;
            }
            return getAttribute<std::string>(view, defOpId, "constValue");
        };
        auto assignSourceFor = [&](grh::ir::ValueId valueId) -> std::optional<grh::ir::ValueId>
        {
            if (!valueId.valid())
            {
                return std::nullopt;
            }
            const grh::ir::OperationId defOpId = view.valueDef(valueId);
            if (!defOpId.valid())
            {
                return std::nullopt;
            }
            if (view.opKind(defOpId) != grh::ir::OperationKind::kAssign)
            {
                return std::nullopt;
            }
            auto operands = view.opOperands(defOpId);
            if (operands.empty())
            {
                return std::nullopt;
            }
            return operands.front();
        };
        std::unordered_set<grh::ir::ValueId, grh::ir::ValueIdHash> materializeValues;
        materializeValues.reserve(view.operations().size());
        for (const auto opId : view.operations())
        {
            const auto kind = view.opKind(opId);
            if (kind == grh::ir::OperationKind::kSliceStatic ||
                kind == grh::ir::OperationKind::kSliceDynamic ||
                kind == grh::ir::OperationKind::kSliceArray)
            {
                const auto operands = view.opOperands(opId);
                if (!operands.empty())
                {
                    materializeValues.insert(operands.front());
                }
            }
            else if (kind == grh::ir::OperationKind::kMemoryWritePort)
            {
                const auto operands = view.opOperands(opId);
                if (operands.size() > 3)
                {
                    materializeValues.insert(operands[2]);
                    materializeValues.insert(operands[3]);
                }
            }
            auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
            if (eventEdges && !eventEdges->empty())
            {
                const auto operands = view.opOperands(opId);
                if (operands.size() >= eventEdges->size())
                {
                    const std::size_t start = operands.size() - eventEdges->size();
                    for (std::size_t i = start; i < operands.size(); ++i)
                    {
                        if (operands[i].valid())
                        {
                            materializeValues.insert(operands[i]);
                        }
                    }
                }
            }
        }
        std::unordered_set<grh::ir::ValueId, grh::ir::ValueIdHash> resolvingExpr;
        std::function<std::string(grh::ir::ValueId)> valueExpr =
            [&](grh::ir::ValueId valueId) -> std::string
        {
            if (!valueId.valid())
            {
                return {};
            }
            if (materializeValues.find(valueId) != materializeValues.end())
            {
                return valueName(valueId);
            }
            if (!resolvingExpr.insert(valueId).second)
            {
                return valueName(valueId);
            }
            if (!isPortValue(valueId))
            {
                if (auto literal = constLiteralFor(valueId))
                {
                    resolvingExpr.erase(valueId);
                    return *literal;
                }
                if (auto source = assignSourceFor(valueId))
                {
                    std::string resolved = valueExpr(*source);
                    resolvingExpr.erase(valueId);
                    return resolved;
                }
            }
            resolvingExpr.erase(valueId);
            return valueName(valueId);
        };
        auto concatOperandExpr = [&](grh::ir::ValueId valueId) -> std::string
        {
            if (auto literal = constLiteralFor(valueId))
            {
                if (auto sized = sizedLiteralIfUnsized(*literal, view.valueWidth(valueId)))
                {
                    return *sized;
                }
            }
            return valueExpr(valueId);
        };
        auto isSimpleIdentifier = [](std::string_view expr) -> bool
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
        };
        auto parenIfNeeded = [&](const std::string &expr) -> std::string
        {
            if (expr.empty() || isSimpleIdentifier(expr))
            {
                return expr;
            }
            if (expr.front() == '(' && expr.back() == ')')
            {
                return expr;
            }
            return "(" + expr + ")";
        };
        auto opContext = [&](grh::ir::OperationId opId) -> std::string
        {
            if (!opId.valid())
            {
                return {};
            }
            const std::string &name = opName(opId);
            if (!name.empty())
            {
                return name;
            }
            return "op_" + std::to_string(opId.index);
        };
        auto opSrcLoc = [&](grh::ir::OperationId opId) -> std::optional<grh::ir::SrcLoc>
        {
            if (!opId.valid())
            {
                return std::nullopt;
            }
            return view.opSrcLoc(opId);
        };

        std::unordered_map<std::string, grh::ir::OperationId> opByName;
        opByName.reserve(opCount);
        for (const auto opId : view.operations())
        {
            const std::string &name = opName(opId);
            if (!name.empty())
            {
                opByName.emplace(name, opId);
            }
        }

        std::unordered_map<std::string, grh::ir::OperationId> dpicImports;
        for (const auto opId : view.operations())
        {
            if (view.opKind(opId) == grh::ir::OperationKind::kDpicImport)
            {
                const std::string &name = opName(opId);
                if (!name.empty())
                {
                    dpicImports.emplace(name, opId);
                }
            }
        }

        struct IrSeqEvent
        {
            std::string edge;
            grh::ir::ValueId signal = grh::ir::ValueId::invalid();
        };

        struct IrSeqKey
        {
            std::vector<IrSeqEvent> events;
        };

        struct IrSeqBlock
        {
            IrSeqKey key{};
            std::vector<std::string> stmts;
            grh::ir::OperationId op = grh::ir::OperationId::invalid();
        };

        std::ostringstream moduleBuffer;

        // -------------------------
        // Ports
        // -------------------------
        std::vector<std::pair<std::string, grh::ir::ValueId>> inputPorts;
        std::vector<std::pair<std::string, grh::ir::ValueId>> outputPorts;
        struct InoutPortBinding {
            std::string name;
            grh::ir::ValueId in;
            grh::ir::ValueId out;
            grh::ir::ValueId oe;
            int64_t width = 1;
        };
        std::vector<InoutPortBinding> inoutPorts;
        inputPorts.reserve(view.inputPorts().size());
        outputPorts.reserve(view.outputPorts().size());
        inoutPorts.reserve(view.inoutPorts().size());

        std::map<std::string, PortDecl, std::less<>> portDecls;
        for (const auto &port : view.inputPorts())
        {
            if (!port.name.valid())
            {
                reportError("Input port missing symbol");
                continue;
            }
            const std::string portName = std::string(symbols.text(port.name));
            const std::string &valName = valueName(port.value);
            if (portName.empty() || valName.empty())
            {
                reportError("Input port missing symbol");
                continue;
            }
            inputPorts.emplace_back(portName, port.value);
            portDecls[portName] = PortDecl{PortDir::Input, view.valueWidth(port.value),
                                           view.valueSigned(port.value), false,
                                           view.valueSrcLoc(port.value)};
        }
        for (const auto &port : view.outputPorts())
        {
            if (!port.name.valid())
            {
                reportError("Output port missing symbol");
                continue;
            }
            const std::string portName = std::string(symbols.text(port.name));
            const std::string &valName = valueName(port.value);
            if (portName.empty() || valName.empty())
            {
                reportError("Output port missing symbol");
                continue;
            }
            outputPorts.emplace_back(portName, port.value);
            portDecls[portName] = PortDecl{PortDir::Output, view.valueWidth(port.value),
                                           view.valueSigned(port.value), false,
                                           view.valueSrcLoc(port.value)};
        }
        for (const auto &port : view.inoutPorts())
        {
            if (!port.name.valid())
            {
                reportError("Inout port missing symbol");
                continue;
            }
            const std::string portName = std::string(symbols.text(port.name));
            const std::string &inName = valueName(port.in);
            const std::string &outName = valueName(port.out);
            const std::string &oeName = valueName(port.oe);
            if (portName.empty() || inName.empty() || outName.empty() || oeName.empty())
            {
                reportError("Inout port missing symbol");
                continue;
            }
            inoutPorts.push_back(InoutPortBinding{portName, port.in, port.out, port.oe,
                                                 view.valueWidth(port.out)});
            portDecls[portName] = PortDecl{PortDir::Inout, view.valueWidth(port.out),
                                           view.valueSigned(port.out), false,
                                           view.valueSrcLoc(port.out)};
        }

        // Book-keeping for declarations.
        std::unordered_set<std::string> declaredNames;
        for (const auto &[name, _] : portDecls)
        {
            declaredNames.insert(name);
        }

        // Storage for various sections.
        std::map<std::string, NetDecl, std::less<>> regDecls;
        std::map<std::string, NetDecl, std::less<>> wireDecls;
        std::vector<std::pair<std::string, grh::ir::OperationId>> memoryDecls;
        std::vector<std::pair<std::string, grh::ir::OperationId>> instanceDecls;
        std::vector<std::pair<std::string, grh::ir::OperationId>> dpiImportDecls;
        std::vector<std::pair<std::string, grh::ir::OperationId>> assignStmts;
        std::vector<std::pair<std::string, grh::ir::OperationId>> portBindingStmts;
        std::vector<std::pair<std::string, grh::ir::OperationId>> latchBlocks;
        std::vector<IrSeqBlock> seqBlocks;
        std::unordered_set<std::string> instanceNamesUsed;

        auto ensureRegDecl = [&](const std::string &name, int64_t width, bool isSigned,
                                 const std::optional<grh::ir::SrcLoc> &debug = std::nullopt)
        {
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

        auto ensureWireDecl = [&](grh::ir::ValueId value)
        {
            const std::string &name = valueName(value);
            if (name.empty())
            {
                reportError("Value missing name during wire emission");
                return;
            }
            if (declaredNames.find(name) != declaredNames.end())
            {
                auto it = wireDecls.find(name);
                if (it != wireDecls.end())
                {
                    auto debug = view.valueSrcLoc(value);
                    if (debug && !it->second.debug)
                    {
                        it->second.debug = *debug;
                    }
                }
                return;
            }
            wireDecls.emplace(name, NetDecl{view.valueWidth(value), view.valueSigned(value),
                                            view.valueSrcLoc(value)});
            declaredNames.insert(name);
        };

        auto addAssign = [&](std::string stmt, grh::ir::OperationId sourceOp)
        {
            assignStmts.emplace_back(std::move(stmt), sourceOp);
        };

        auto sameSeqKey = [](const IrSeqKey &lhs, const IrSeqKey &rhs) -> bool
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
        auto addSequentialStmt = [&](const IrSeqKey &key, std::string stmt,
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
            seqBlocks.push_back(IrSeqBlock{key, std::vector<std::string>{std::move(stmt)}, sourceOp});
        };

        auto addLatchBlock = [&](std::string stmt, grh::ir::OperationId sourceOp)
        {
            latchBlocks.emplace_back(std::move(stmt), sourceOp);
        };

        auto logicalOperand = [&](grh::ir::ValueId valueId) -> std::string
        {
            const int64_t width = view.valueWidth(valueId);
            const std::string name = valueExpr(valueId);
            if (width == 1)
            {
                return name;
            }
            return "(|" + name + ")";
        };
        auto extendOperand = [&](grh::ir::ValueId valueId, int64_t targetWidth) -> std::string
        {
            const int64_t width = view.valueWidth(valueId);
            const std::string name = valueExpr(valueId);
            if (targetWidth <= 0 || width <= 0 || width == targetWidth)
            {
                return name;
            }
            const int64_t diff = targetWidth - width;
            if (diff <= 0)
            {
                return name;
            }
            if (view.valueSigned(valueId))
            {
                return "{{" + std::to_string(diff) + "{" + name + "[" + std::to_string(width - 1) + "]}}," + name + "}";
            }
            return "{{" + std::to_string(diff) + "{1'b0}}," + name + "}";
        };
        auto isConstOne = [&](grh::ir::ValueId valueId) -> bool
        {
            if (!valueId.valid())
            {
                return false;
            }
            const grh::ir::OperationId defOpId = view.valueDef(valueId);
            if (!defOpId.valid())
            {
                return false;
            }
            if (view.opKind(defOpId) != grh::ir::OperationKind::kConstant)
            {
                return false;
            }
            auto constValue = getAttribute<std::string>(view, defOpId, "constValue");
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
            const grh::ir::OperationId defOpId = view.valueDef(valueId);
            if (!defOpId.valid())
            {
                return false;
            }
            if (view.opKind(defOpId) != grh::ir::OperationKind::kConstant)
            {
                return false;
            }
            auto constValue = getAttribute<std::string>(view, defOpId, "constValue");
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
            const grh::ir::OperationId defOpId = view.valueDef(maybeNot);
            if (!defOpId.valid())
            {
                return false;
            }
            const grh::ir::OperationKind kind = view.opKind(defOpId);
            if (kind != grh::ir::OperationKind::kLogicNot &&
                kind != grh::ir::OperationKind::kNot)
            {
                return false;
            }
            const auto ops = view.opOperands(defOpId);
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
                    const grh::ir::OperationId defId = view.valueDef(id);
                    if (!defId.valid())
                    {
                        if (leafIndex.find(id) == leafIndex.end())
                        {
                            leafIndex[id] = leaves.size();
                            leaves.push_back(id);
                        }
                        return;
                    }
                    const grh::ir::OperationKind kind = view.opKind(defId);
                    if (kind == grh::ir::OperationKind::kLogicAnd ||
                        kind == grh::ir::OperationKind::kLogicOr ||
                        kind == grh::ir::OperationKind::kLogicNot ||
                        kind == grh::ir::OperationKind::kNot)
                    {
                        const auto ops = view.opOperands(defId);
                        for (const auto operand : ops)
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
                const grh::ir::OperationId defId = view.valueDef(id);
                if (!defId.valid())
                {
                    auto it = leafIndex.find(id);
                    return it != leafIndex.end() && assignment[it->second];
                }
                const grh::ir::OperationKind kind = view.opKind(defId);
                if (kind == grh::ir::OperationKind::kLogicAnd)
                {
                    const auto ops = view.opOperands(defId);
                    for (const auto operand : ops)
                    {
                        if (!self(operand, assignment, self))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                if (kind == grh::ir::OperationKind::kLogicOr)
                {
                    const auto ops = view.opOperands(defId);
                    for (const auto operand : ops)
                    {
                        if (self(operand, assignment, self))
                        {
                            return true;
                        }
                    }
                    return false;
                }
                if (kind == grh::ir::OperationKind::kLogicNot ||
                    kind == grh::ir::OperationKind::kNot)
                {
                    const auto ops = view.opOperands(defId);
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
            const grh::ir::OperationId defOpId = view.valueDef(valueId);
            if (defOpId.valid() &&
                view.opKind(defOpId) == grh::ir::OperationKind::kLogicOr)
            {
                const auto ops = view.opOperands(defOpId);
                for (std::size_t i = 0; i < ops.size(); ++i)
                {
                    for (std::size_t j = i + 1; j < ops.size(); ++j)
                    {
                        const grh::ir::ValueId a = ops[i];
                        const grh::ir::ValueId b = ops[j];
                        if (isLogicNotOf(a, b) || isLogicNotOf(b, a))
                        {
                            return true;
                        }
                    }
                }
            }
            return isAlwaysTrueByTruthTable(valueId);
        };

        auto markPortAsRegIfNeeded = [&](grh::ir::ValueId value)
        {
            const std::string &name = valueName(value);
            auto itPort = portDecls.find(name);
            if (itPort != portDecls.end() && itPort->second.dir == PortDir::Output)
            {
                itPort->second.isReg = true;
            }
        };

        auto resolveMemorySymbol = [&](grh::ir::OperationId userOp) -> std::optional<std::string>
        {
            auto attr = getAttribute<std::string>(view, userOp, "memSymbol");
            if (attr)
            {
                return attr;
            }
            std::optional<std::string> candidate;
            for (const auto opId : view.operations())
            {
                if (view.opKind(opId) == grh::ir::OperationKind::kMemory)
                {
                    const std::string &name = opName(opId);
                    if (name.empty())
                    {
                        continue;
                    }
                    if (candidate)
                    {
                        return std::nullopt;
                    }
                    candidate = name;
                }
            }
            return candidate;
        };

        // -------------------------
        // Port bindings (handle ports mapped to internal nets with different names)
        // -------------------------
        auto bindInputPort = [&](const std::string &portName, grh::ir::ValueId valueId)
        {
            const std::string &valName = valueName(valueId);
            if (portName == valName)
            {
                return;
            }
            ensureWireDecl(valueId);
            portBindingStmts.emplace_back(
                "assign " + valName + " = " + portName + ";", grh::ir::OperationId::invalid());
        };

        auto bindOutputPort = [&](const std::string &portName, grh::ir::ValueId valueId)
        {
            const std::string &valName = valueName(valueId);
            if (portName == valName)
            {
                return;
            }
            portBindingStmts.emplace_back(
                "assign " + portName + " = " + valName + ";", grh::ir::OperationId::invalid());
        };

        for (const auto &[portName, valueId] : inputPorts)
        {
            bindInputPort(portName, valueId);
        }
        for (const auto &[portName, valueId] : outputPorts)
        {
            bindOutputPort(portName, valueId);
        }
        auto emitInoutAssign = [&](const std::string &dest,
                                   grh::ir::ValueId oeValue,
                                   grh::ir::ValueId outValue,
                                   int64_t width,
                                   grh::ir::OperationId sourceOp) {
            const std::string oeExpr = valueExpr(oeValue);
            const std::string outExpr = valueExpr(outValue);
            const int64_t oeWidth = view.valueWidth(oeValue);
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
        for (const auto &port : inoutPorts)
        {
            const std::string &inName = valueName(port.in);
            const std::string &outName = valueName(port.out);
            const std::string &oeName = valueName(port.oe);
            ensureWireDecl(port.in);
            ensureWireDecl(port.out);
            ensureWireDecl(port.oe);
            portBindingStmts.emplace_back(
                "assign " + inName + " = " + port.name + ";",
                grh::ir::OperationId::invalid());
            emitInoutAssign(port.name, port.oe, port.out, port.width, grh::ir::OperationId::invalid());
        }

        // Event lists are modeled directly per operation in this emission path.

        // -------------------------
        // Operation traversal
        // -------------------------
        for (const auto opId : view.operations())
        {
            const auto kind = view.opKind(opId);
            const auto operands = view.opOperands(opId);
            const auto results = view.opResults(opId);
            const std::string context = opContext(opId);
            auto buildEventKey = [&](std::size_t eventStart) -> std::optional<IrSeqKey>
            {
                auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
                if (!eventEdges)
                {
                    reportError(std::string(grh::ir::toString(kind)) + " missing eventEdge", context);
                    return std::nullopt;
                }
                const std::size_t eventCount = operands.size() > eventStart ? operands.size() - eventStart : 0;
                if (eventCount == 0 || eventEdges->size() != eventCount)
                {
                    reportError(std::string(grh::ir::toString(kind)) + " eventEdge size mismatch", context);
                    return std::nullopt;
                }
                IrSeqKey key;
                key.events.reserve(eventCount);
                for (std::size_t i = 0; i < eventCount; ++i)
                {
                    const grh::ir::ValueId signal = operands[eventStart + i];
                    if (!signal.valid())
                    {
                        reportError(std::string(grh::ir::toString(kind)) + " missing event operand", context);
                        return std::nullopt;
                    }
                    key.events.push_back(IrSeqEvent{(*eventEdges)[i], signal});
                }
                return key;
            };
            auto buildSingleEventKey = [&](grh::ir::ValueId signal,
                                           const std::optional<std::string> &edgeAttr,
                                           std::string_view opName) -> std::optional<IrSeqKey>
            {
                if (!signal.valid() || !edgeAttr || edgeAttr->empty())
                {
                    reportError(std::string(opName) + " missing clk/clkPolarity", context);
                    return std::nullopt;
                }
                IrSeqKey key;
                key.events.push_back(IrSeqEvent{*edgeAttr, signal});
                return key;
            };

            switch (kind)
            {
            case grh::ir::OperationKind::kConstant:
            {
                if (results.empty())
                {
                    reportError("kConstant missing result", context);
                    break;
                }
                if (!isPortValue(results[0]) &&
                    materializeValues.find(results[0]) == materializeValues.end())
                {
                    break;
                }
                auto constValue = getAttribute<std::string>(view, opId, "constValue");
                if (!constValue)
                {
                    reportError("kConstant missing constValue attribute", context);
                    break;
                }
                addAssign("assign " + valueName(results[0]) + " = " + *constValue + ";", opId);
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
                    reportError("Binary operation missing operands or results", context);
                    break;
                }
                int64_t resultWidth =
                    std::max(view.valueWidth(operands[0]),
                             view.valueWidth(operands[1]));
                const std::string tok = binOpToken(kind);
                const std::string lhs = resultWidth > 0
                                            ? extendOperand(operands[0], resultWidth)
                                            : valueExpr(operands[0]);
                const std::string rhs = resultWidth > 0
                                            ? extendOperand(operands[1], resultWidth)
                                            : valueExpr(operands[1]);
                const bool signedCompare =
                    (kind == grh::ir::OperationKind::kLt ||
                     kind == grh::ir::OperationKind::kLe ||
                     kind == grh::ir::OperationKind::kGt ||
                     kind == grh::ir::OperationKind::kGe) &&
                    view.valueSigned(operands[0]) &&
                    view.valueSigned(operands[1]);
                const std::string lhsExpr = signedCompare ? "$signed(" + lhs + ")" : lhs;
                const std::string rhsExpr = signedCompare ? "$signed(" + rhs + ")" : rhs;
                addAssign("assign " + valueName(results[0]) + " = " + lhsExpr + " " + tok + " " + rhsExpr + ";",
                          opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kAnd:
            case grh::ir::OperationKind::kOr:
            case grh::ir::OperationKind::kXor:
            case grh::ir::OperationKind::kXnor:
            case grh::ir::OperationKind::kShl:
            case grh::ir::OperationKind::kLShr:
            case grh::ir::OperationKind::kAShr:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("Binary operation missing operands or results", context);
                    break;
                }
                const std::string tok = binOpToken(kind);
                addAssign("assign " + valueName(results[0]) + " = " + valueExpr(operands[0]) +
                              " " + tok + " " + valueExpr(operands[1]) + ";",
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
                    reportError("Binary operation missing operands or results", context);
                    break;
                }
                int64_t resultWidth = view.valueWidth(results[0]);
                if (resultWidth <= 0)
                {
                    resultWidth = std::max(view.valueWidth(operands[0]),
                                           view.valueWidth(operands[1]));
                }
                const std::string tok = binOpToken(kind);
                addAssign("assign " + valueName(results[0]) + " = " +
                              extendOperand(operands[0], resultWidth) + " " + tok + " " +
                              extendOperand(operands[1], resultWidth) + ";",
                          opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kLogicAnd:
            case grh::ir::OperationKind::kLogicOr:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("Binary operation missing operands or results", context);
                    break;
                }
                const std::string tok = binOpToken(kind);
                addAssign("assign " + valueName(results[0]) + " = " + logicalOperand(operands[0]) +
                              " " + tok + " " + logicalOperand(operands[1]) + ";",
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
                    reportError("Unary operation missing operands or results", context);
                    break;
                }
                const std::string tok = unaryOpToken(kind);
                addAssign("assign " + valueName(results[0]) + " = " + tok + valueExpr(operands[0]) + ";", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kLogicNot:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("Unary operation missing operands or results", context);
                    break;
                }
                addAssign("assign " + valueName(results[0]) + " = !" + logicalOperand(operands[0]) + ";", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kMux:
            {
                if (operands.size() < 3 || results.empty())
                {
                    reportError("kMux missing operands or results", context);
                    break;
                }
                int64_t resultWidth = view.valueWidth(results[0]);
                const std::string lhs = extendOperand(operands[1], resultWidth);
                const std::string rhs = extendOperand(operands[2], resultWidth);
                addAssign("assign " + valueName(results[0]) + " = " + valueExpr(operands[0]) +
                              " ? " + lhs + " : " + rhs + ";",
                          opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kAssign:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kAssign missing operands or results", context);
                    break;
                }
                if (!isPortValue(results[0]) &&
                    materializeValues.find(results[0]) == materializeValues.end())
                {
                    break;
                }
                int64_t resultWidth = view.valueWidth(results[0]);
                const std::string rhs = extendOperand(operands[0], resultWidth);
                addAssign("assign " + valueName(results[0]) + " = " + rhs + ";", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kConcat:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kConcat missing operands or results", context);
                    break;
                }
                if (operands.size() == 1)
                {
                    addAssign("assign " + valueName(results[0]) + " = " + valueExpr(operands[0]) + ";", opId);
                    ensureWireDecl(results[0]);
                    break;
                }
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = {";
                for (std::size_t i = 0; i < operands.size(); ++i)
                {
                    if (i != 0)
                    {
                        expr << ", ";
                    }
                    expr << concatOperandExpr(operands[i]);
                }
                expr << "};";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kReplicate:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kReplicate missing operands or results", context);
                    break;
                }
                auto rep = getAttribute<int64_t>(view, opId, "rep");
                if (!rep)
                {
                    reportError("kReplicate missing rep attribute", context);
                    break;
                }
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = {" << *rep << "{" << concatOperandExpr(operands[0]) << "}};";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kSliceStatic:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kSliceStatic missing operands or results", context);
                    break;
                }
                auto sliceStart = getAttribute<int64_t>(view, opId, "sliceStart");
                auto sliceEnd = getAttribute<int64_t>(view, opId, "sliceEnd");
                if (!sliceStart || !sliceEnd)
                {
                    reportError("kSliceStatic missing sliceStart or sliceEnd", context);
                    break;
                }
                const int64_t operandWidth = view.valueWidth(operands[0]);
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = ";
                if (operandWidth == 1)
                {
                    if (*sliceStart != 0 || *sliceEnd != 0)
                    {
                        reportError("kSliceStatic index out of range for scalar", context);
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
                expr << ";";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kSliceDynamic:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("kSliceDynamic missing operands or results", context);
                    break;
                }
                auto width = getAttribute<int64_t>(view, opId, "sliceWidth");
                if (!width)
                {
                    reportError("kSliceDynamic missing sliceWidth", context);
                    break;
                }
                const int64_t operandWidth = view.valueWidth(operands[0]);
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = ";
                if (operandWidth == 1)
                {
                    if (*width != 1)
                    {
                        reportError("kSliceDynamic width exceeds scalar", context);
                    }
                    expr << valueExpr(operands[0]);
                }
                else if (*width == 1)
                {
                    expr << parenIfNeeded(valueExpr(operands[0])) << "[" << parenIfNeeded(valueExpr(operands[1])) << "]";
                }
                else
                {
                    expr << parenIfNeeded(valueExpr(operands[0])) << "[" << parenIfNeeded(valueExpr(operands[1])) << " +: " << *width << "]";
                }
                expr << ";";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kSliceArray:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("kSliceArray missing operands or results", context);
                    break;
                }
                auto width = getAttribute<int64_t>(view, opId, "sliceWidth");
                if (!width)
                {
                    reportError("kSliceArray missing sliceWidth", context);
                    break;
                }
                const int64_t operandWidth = view.valueWidth(operands[0]);
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = ";
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
                expr << ";";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::ir::OperationKind::kLatch:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("kLatch missing operands or results", context);
                    break;
                }
                const grh::ir::ValueId updateCond = operands[0];
                const grh::ir::ValueId nextValue = operands[1];
                if (!updateCond.valid() || !nextValue.valid())
                {
                    reportError("kLatch missing updateCond/nextValue operands", context);
                    break;
                }
                if (view.valueWidth(updateCond) != 1)
                {
                    reportError("kLatch updateCond must be 1 bit", context);
                    break;
                }

                const grh::ir::ValueId q = results[0];
                const std::string &valueSym = valueName(q);
                const std::string &regName = !valueSym.empty() ? valueSym : opName(opId);
                if (regName.empty())
                {
                    reportError("kLatch missing symbol", context);
                    break;
                }
                if (view.valueWidth(q) != view.valueWidth(nextValue))
                {
                    reportError("kLatch data/output width mismatch", context);
                    break;
                }
                ensureRegDecl(regName, view.valueWidth(nextValue), view.valueSigned(nextValue), view.opSrcLoc(opId));
                const bool directDrive = valueName(q) == regName;
                if (directDrive)
                {
                    markPortAsRegIfNeeded(q);
                }
                else
                {
                    ensureWireDecl(q);
                    addAssign("assign " + valueName(q) + " = " + regName + ";", opId);
                }

                std::ostringstream block;
                if (isAlwaysTrue(updateCond))
                {
                    appendIndented(block, 1, "always @* begin");
                    appendIndented(block, 2, regName + " = " + valueExpr(nextValue) + ";");
                    appendIndented(block, 1, "end");
                }
                else
                {
                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (" + valueExpr(updateCond) + ") begin");
                    appendIndented(stmt, 3, regName + " = " + valueExpr(nextValue) + ";");
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
                    reportError("kRegister missing operands or results", context);
                    break;
                }
                const grh::ir::ValueId updateCond = operands[0];
                const grh::ir::ValueId nextValue = operands[1];
                if (!updateCond.valid() || !nextValue.valid())
                {
                    reportError("kRegister missing updateCond/nextValue operands", context);
                    break;
                }
                if (view.valueWidth(updateCond) != 1)
                {
                    reportError("kRegister updateCond must be 1 bit", context);
                    break;
                }

                auto seqKey = buildEventKey(2);
                if (!seqKey)
                {
                    break;
                }

                const grh::ir::ValueId q = results[0];
                const std::string &valueSym = valueName(q);
                const std::string &regName = !valueSym.empty() ? valueSym : opName(opId);
                if (regName.empty())
                {
                    reportError("kRegister missing symbol", context);
                    break;
                }
                if (view.valueWidth(q) != view.valueWidth(nextValue))
                {
                    reportError("kRegister data/output width mismatch", context);
                    break;
                }

                ensureRegDecl(regName, view.valueWidth(nextValue), view.valueSigned(nextValue), view.opSrcLoc(opId));
                const bool directDrive = valueName(q) == regName;
                if (directDrive)
                {
                    markPortAsRegIfNeeded(q);
                }
                else
                {
                    ensureWireDecl(q);
                    addAssign("assign " + valueName(q) + " = " + regName + ";", opId);
                }

                std::ostringstream stmt;
                if (isConstOne(updateCond))
                {
                    appendIndented(stmt, 2, regName + " <= " + valueExpr(nextValue) + ";");
                }
                else
                {
                    appendIndented(stmt, 2, "if (" + valueExpr(updateCond) + ") begin");
                    appendIndented(stmt, 3, regName + " <= " + valueExpr(nextValue) + ";");
                    appendIndented(stmt, 2, "end");
                }
                addSequentialStmt(*seqKey, stmt.str(), opId);
                break;
            }
            case grh::ir::OperationKind::kMemory:
            {
                auto widthAttr = getAttribute<int64_t>(view, opId, "width");
                auto rowAttr = getAttribute<int64_t>(view, opId, "row");
                auto isSignedAttr = getAttribute<bool>(view, opId, "isSigned");
                if (!widthAttr || !rowAttr || !isSignedAttr)
                {
                    reportError("kMemory missing width/row/isSigned", context);
                    break;
                }
                const std::string &memName = opName(opId);
                if (memName.empty())
                {
                    reportError("kMemory missing symbol", context);
                    break;
                }
                std::ostringstream decl;
                decl << "reg " << (*isSignedAttr ? "signed " : "") << "[" << (*widthAttr - 1)
                     << ":0] " << memName << " [0:" << (*rowAttr - 1) << "];";
                memoryDecls.emplace_back(decl.str(), opId);
                declaredNames.insert(memName);
                break;
            }
            case grh::ir::OperationKind::kMemoryReadPort:
            {
                if (operands.size() < 1 || results.empty())
                {
                    reportError("kMemoryReadPort missing operands or results", context);
                    break;
                }
                auto memSymbolAttr = resolveMemorySymbol(opId);
                if (!memSymbolAttr)
                {
                    reportError("kMemoryReadPort missing memSymbol", context);
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
                    reportError("kMemoryWritePort missing operands", context);
                    break;
                }
                const grh::ir::ValueId updateCond = operands[0];
                const grh::ir::ValueId addr = operands[1];
                const grh::ir::ValueId data = operands[2];
                const grh::ir::ValueId mask = operands[3];
                if (!updateCond.valid() || !addr.valid() || !data.valid() || !mask.valid())
                {
                    reportError("kMemoryWritePort missing operands", context);
                    break;
                }
                if (view.valueWidth(updateCond) != 1)
                {
                    reportError("kMemoryWritePort updateCond must be 1 bit", context);
                    break;
                }
                auto memSymbolAttr = resolveMemorySymbol(opId);
                if (!memSymbolAttr)
                {
                    reportError("kMemoryWritePort missing memSymbol", context);
                    break;
                }
                auto seqKey = buildEventKey(4);
                if (!seqKey)
                {
                    break;
                }

                const std::string &memSymbol = *memSymbolAttr;
                int64_t memWidth = 1;
                if (auto it = opByName.find(memSymbol); it != opByName.end())
                {
                    memWidth = getAttribute<int64_t>(view, it->second, "width").value_or(1);
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
                auto moduleNameAttr = getAttribute<std::string>(view, opId, "moduleName");
                auto inputNames = getAttribute<std::vector<std::string>>(view, opId, "inputPortName");
                auto outputNames = getAttribute<std::vector<std::string>>(view, opId, "outputPortName");
                auto inoutNames = getAttribute<std::vector<std::string>>(view, opId, "inoutPortName");
                auto instanceNameBase = getAttribute<std::string>(view, opId, "instanceName")
                                            .value_or(opName(opId));
                std::string instanceName = instanceNameBase;
                int instSuffix = 1;
                while (!instanceNamesUsed.insert(instanceName).second)
                {
                    instanceName = instanceNameBase + "_" + std::to_string(instSuffix++);
                }
                if (!moduleNameAttr || !inputNames || !outputNames)
                {
                    reportError("Instance missing module or port names", context);
                    break;
                }
                const std::size_t inoutCount = inoutNames ? inoutNames->size() : 0;
                if (operands.size() < inputNames->size() + inoutCount * 2 ||
                    results.size() < outputNames->size() + inoutCount)
                {
                    reportError("Instance port counts do not match operands/results", context);
                    break;
                }
                const std::string &targetModuleName = *moduleNameAttr;

                std::ostringstream decl;
                if (kind == grh::ir::OperationKind::kBlackbox)
                {
                    auto paramNames = getAttribute<std::vector<std::string>>(view, opId, "parameterNames");
                    auto paramValues = getAttribute<std::vector<std::string>>(view, opId, "parameterValues");
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
                        wireDecls.emplace(candidate, NetDecl{width, isSigned, view.opSrcLoc(opId)});
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
                            reportError("Instance inout index out of range", context);
                            inoutOk = false;
                            break;
                        }
                        const std::string wireBase =
                            instanceName.empty()
                                ? (*inoutNames)[i] + "_inout"
                                : instanceName + "_" + (*inoutNames)[i] + "_inout";
                        const int64_t width = view.valueWidth(operands[outIndex]);
                        const bool isSigned = view.valueSigned(operands[outIndex]);
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
                    decl << "    ." << connections[i].first << "(" << connections[i].second << ")";
                    decl << (i + 1 == connections.size() ? "\n" : ",\n");
                }
                decl << "  );";
                instanceDecls.emplace_back(decl.str(), opId);
                break;
            }
            case grh::ir::OperationKind::kDisplay:
            {
                if (operands.size() < 1)
                {
                    reportError("kDisplay missing operands", context);
                    break;
                }
                auto format = getAttribute<std::string>(view, opId, "formatString");
                if (!format)
                {
                    format = getAttribute<std::string>(view, opId, "format");
                }
                auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
                auto displayKind = getAttribute<std::string>(view, opId, "displayKind");
                const bool hasExitCode = getAttribute<bool>(view, opId, "hasExitCode").value_or(false);
                if (!format || !eventEdges)
                {
                    reportError("kDisplay missing eventEdge or format", context);
                    break;
                }
                if (eventEdges->empty())
                {
                    reportError("kDisplay missing eventEdge entries", context);
                    break;
                }
                const std::size_t baseIndex = 1 + (hasExitCode ? 1 : 0);
                if (operands.size() < baseIndex + eventEdges->size())
                {
                    reportError("kDisplay operand count does not match eventEdge", context);
                    break;
                }
                const std::size_t argCount = operands.size() - baseIndex - eventEdges->size();
                const std::size_t eventStart = baseIndex + argCount;
                auto seqKey = buildEventKey(eventStart);
                if (!seqKey)
                {
                    break;
                }
                const bool guardDisplay = !isConstOne(operands[0]);
                const int baseIndent = guardDisplay ? 3 : 2;
                std::string taskName = displayKind.value_or(std::string("display"));
                if (taskName.empty())
                {
                    taskName = "display";
                }
                std::ostringstream stmt;
                if (guardDisplay)
                {
                    appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                }
                stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$" << taskName << "(";
                if (taskName == "fatal" && hasExitCode)
                {
                    stmt << valueExpr(operands[1]) << ", ";
                }
                stmt << "\"" << *format << "\"";
                for (std::size_t i = 0; i < argCount; ++i)
                {
                    stmt << ", " << valueExpr(operands[baseIndex + i]);
                }
                stmt << ");\n";
                if (guardDisplay)
                {
                    appendIndented(stmt, 2, "end");
                }
                addSequentialStmt(*seqKey, stmt.str(), opId);
                break;
            }
            case grh::ir::OperationKind::kFinish:
            {
                if (operands.size() < 1)
                {
                    reportError("kFinish missing operands", context);
                    break;
                }
                auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
                const bool hasExitCode = getAttribute<bool>(view, opId, "hasExitCode").value_or(false);
                if (!eventEdges)
                {
                    reportError("kFinish missing eventEdge", context);
                    break;
                }
                if (eventEdges->empty())
                {
                    reportError("kFinish missing eventEdge entries", context);
                    break;
                }
                const std::size_t baseIndex = 1 + (hasExitCode ? 1 : 0);
                if (operands.size() < baseIndex + eventEdges->size())
                {
                    reportError("kFinish operand count does not match eventEdge", context);
                    break;
                }
                const std::size_t eventStart = baseIndex;
                auto seqKey = buildEventKey(eventStart);
                if (!seqKey)
                {
                    break;
                }
                const bool guardFinish = !isConstOne(operands[0]);
                const int baseIndent = guardFinish ? 3 : 2;
                std::ostringstream stmt;
                if (guardFinish)
                {
                    appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                }
                stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$finish";
                if (hasExitCode)
                {
                    stmt << "(" << valueExpr(operands[1]) << ")";
                }
                stmt << ";\n";
                if (guardFinish)
                {
                    appendIndented(stmt, 2, "end");
                }
                addSequentialStmt(*seqKey, stmt.str(), opId);
                break;
            }
            case grh::ir::OperationKind::kFwrite:
            {
                if (operands.size() < 2)
                {
                    reportError("kFwrite missing operands", context);
                    break;
                }
                auto format = getAttribute<std::string>(view, opId, "formatString");
                if (!format)
                {
                    format = getAttribute<std::string>(view, opId, "format");
                }
                auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
                if (!format || !eventEdges)
                {
                    reportError("kFwrite missing eventEdge or format", context);
                    break;
                }
                if (eventEdges->empty())
                {
                    reportError("kFwrite missing eventEdge entries", context);
                    break;
                }
                if (operands.size() < 2 + eventEdges->size())
                {
                    reportError("kFwrite operand count does not match eventEdge", context);
                    break;
                }
                const std::size_t argCount = operands.size() - 2 - eventEdges->size();
                const std::size_t eventStart = 2 + argCount;
                auto seqKey = buildEventKey(eventStart);
                if (!seqKey)
                {
                    break;
                }
                const bool guardDisplay = !isConstOne(operands[0]);
                const int baseIndent = guardDisplay ? 3 : 2;
                std::ostringstream stmt;
                if (guardDisplay)
                {
                    appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + ") begin");
                }
                stmt << std::string(kIndentSizeSv * baseIndent, ' ') << "$fwrite(" << valueExpr(operands[1]) << ", \"" << *format << "\"";
                for (std::size_t i = 0; i < argCount; ++i)
                {
                    stmt << ", " << valueExpr(operands[2 + i]);
                }
                stmt << ");\n";
                if (guardDisplay)
                {
                    appendIndented(stmt, 2, "end");
                }
                addSequentialStmt(*seqKey, stmt.str(), opId);
                break;
            }
            case grh::ir::OperationKind::kAssert:
            {
                if (operands.size() < 2)
                {
                    reportError("kAssert missing operands", context);
                    break;
                }
                auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
                if (!eventEdges)
                {
                    reportError("kAssert missing eventEdge", context);
                    break;
                }
                if (eventEdges->empty())
                {
                    reportError("kAssert missing eventEdge entries", context);
                    break;
                }
                auto message = getAttribute<std::string>(view, opId, "message")
                                   .value_or(std::string("Assertion failed"));
                auto seqKey = buildEventKey(2);
                if (!seqKey)
                {
                    break;
                }
                std::ostringstream stmt;
                if (isConstOne(operands[0]))
                {
                    appendIndented(stmt, 2, "if (!" + valueExpr(operands[1]) + ") begin");
                }
                else
                {
                    appendIndented(stmt, 2, "if (" + valueExpr(operands[0]) + " && !" + valueExpr(operands[1]) + ") begin");
                }
                appendIndented(stmt, 3, "$fatal(\"" + message + " at time %0t\", $time);");
                appendIndented(stmt, 2, "end");
                addSequentialStmt(*seqKey, stmt.str(), opId);
                break;
            }
            case grh::ir::OperationKind::kDpicImport:
            {
                auto argsDir = getAttribute<std::vector<std::string>>(view, opId, "argsDirection");
                auto argsWidth = getAttribute<std::vector<int64_t>>(view, opId, "argsWidth");
                auto argsName = getAttribute<std::vector<std::string>>(view, opId, "argsName");
                auto argsSigned = getAttribute<std::vector<bool>>(view, opId, "argsSigned");
                auto argsType = getAttribute<std::vector<std::string>>(view, opId, "argsType");
                auto hasReturn = getAttribute<bool>(view, opId, "hasReturn").value_or(false);
                auto returnWidth = getAttribute<int64_t>(view, opId, "returnWidth").value_or(0);
                auto returnSigned = getAttribute<bool>(view, opId, "returnSigned").value_or(false);
                auto returnType = getAttribute<std::string>(view, opId, "returnType");
                if (!argsDir || !argsWidth || !argsName ||
                    argsDir->size() != argsWidth->size() || argsDir->size() != argsName->size())
                {
                    reportError("kDpicImport missing or inconsistent arg metadata", context);
                    break;
                }
                if (argsSigned && argsSigned->size() != argsDir->size())
                {
                    reportError("kDpicImport argsSigned size mismatch", context);
                    break;
                }
                if (argsType && argsType->size() != argsDir->size())
                {
                    reportError("kDpicImport argsType size mismatch", context);
                    break;
                }
                const std::string &funcName = opName(opId);
                if (funcName.empty())
                {
                    reportError("kDpicImport missing symbol", context);
                    break;
                }
                std::ostringstream decl;
                decl << "import \"DPI-C\" function ";
                if (hasReturn)
                {
                    const bool useLogic =
                        !(returnType && !returnType->empty() && *returnType != "logic");
                    if (useLogic)
                    {
                        const int64_t width = returnWidth > 0 ? returnWidth : 1;
                        decl << "logic ";
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
                        decl << *returnType << " ";
                    }
                }
                else
                {
                    decl << "void ";
                }
                decl << funcName << " (\n";
                for (std::size_t i = 0; i < argsDir->size(); ++i)
                {
                    std::string typeName = "logic";
                    if (argsType && i < argsType->size())
                    {
                        typeName = (*argsType)[i];
                    }
                    decl << "  " << (*argsDir)[i] << " ";
                    if (typeName == "logic")
                    {
                        decl << "logic ";
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
                    reportError("kDpicCall missing operands", context);
                    break;
                }
                auto eventEdges = getAttribute<std::vector<std::string>>(view, opId, "eventEdge");
                auto targetImport = getAttribute<std::string>(view, opId, "targetImportSymbol");
                auto inArgName = getAttribute<std::vector<std::string>>(view, opId, "inArgName");
                auto outArgName = getAttribute<std::vector<std::string>>(view, opId, "outArgName");
                auto inoutArgName = getAttribute<std::vector<std::string>>(view, opId, "inoutArgName");
                auto hasReturn = getAttribute<bool>(view, opId, "hasReturn").value_or(false);
                if (!eventEdges || !targetImport || !inArgName || !outArgName)
                {
                    reportError("kDpicCall missing metadata", context);
                    break;
                }
                if (eventEdges->empty())
                {
                    reportError("kDpicCall missing eventEdge entries", context);
                    break;
                }
                if (operands.size() < 1 + eventEdges->size())
                {
                    reportError("kDpicCall operand count does not match eventEdge", context);
                    break;
                }
                const std::size_t eventStart = operands.size() - eventEdges->size();
                if (eventStart < 1)
                {
                    reportError("kDpicCall missing updateCond operand", context);
                    break;
                }
                const std::size_t argCount = eventStart - 1;
                const std::size_t inoutCount = inoutArgName ? inoutArgName->size() : 0;
                if (argCount != inArgName->size() + inoutCount)
                {
                    reportError("kDpicCall operand count does not match args", context);
                    break;
                }
                const std::size_t outputOffset = hasReturn ? 1 : 0;
                if (results.size() < outputOffset + outArgName->size() + inoutCount)
                {
                    reportError("kDpicCall result count does not match args", context);
                    break;
                }
                auto seqKey = buildEventKey(eventStart);
                if (!seqKey)
                {
                    break;
                }
                auto itImport = dpicImports.find(*targetImport);
                if (itImport == dpicImports.end())
                {
                    reportError("kDpicCall cannot resolve import symbol", *targetImport);
                    break;
                }
                auto importArgs = getAttribute<std::vector<std::string>>(view, itImport->second, "argsName");
                auto importDirs = getAttribute<std::vector<std::string>>(view, itImport->second, "argsDirection");
                if (!importArgs || !importDirs || importArgs->size() != importDirs->size())
                {
                    reportError("kDpicCall found malformed import signature", *targetImport);
                    break;
                }

                // Declare intermediate regs for outputs and connect them back.
                for (const grh::ir::ValueId res : results)
                {
                    const std::string &resName = valueName(res);
                    if (resName.empty())
                    {
                        continue;
                    }
                    const std::string tempName = resName + "_intm";
                    ensureRegDecl(tempName, view.valueWidth(res), view.valueSigned(res), view.opSrcLoc(opId));
                    addAssign("assign " + resName + " = " + tempName + ";", opId);
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
                            reportError("kDpicCall inout arg index out of range", context);
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
                            reportError("kDpicCall missing matching input arg " + formal, context);
                            callOk = false;
                            break;
                        }
                        stmt << valueExpr(operands[static_cast<std::size_t>(idx + 1)]);
                    }
                    else if (dir == "inout")
                    {
                        if (!inoutArgName)
                        {
                            reportError("kDpicCall missing inoutArgName for " + formal, context);
                            callOk = false;
                            break;
                        }
                        int idx = findNameIndex(*inoutArgName, formal);
                        const std::size_t resultIndex =
                            outputOffset + outArgName->size() + static_cast<std::size_t>(idx);
                        if (idx < 0 || resultIndex >= results.size())
                        {
                            reportError("kDpicCall missing matching inout arg " + formal, context);
                            callOk = false;
                            break;
                        }
                        stmt << valueName(results[resultIndex]) << "_intm";
                    }
                    else
                    {
                        int idx = findNameIndex(*outArgName, formal);
                        const std::size_t resultIndex = outputOffset + static_cast<std::size_t>(idx);
                        if (idx < 0 || resultIndex >= results.size())
                        {
                            reportError("kDpicCall missing matching output arg " + formal, context);
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
                reportError("Unresolved XMR operation in emit", context);
                break;
            default:
                reportWarning("Unsupported operation for SystemVerilog emission", context);
                break;
            }
        }

        // Declare remaining wires for non-port values not defined above.
        for (const auto valueId : view.values())
        {
            if (view.valueIsInput(valueId) || view.valueIsOutput(valueId) || view.valueIsInout(valueId))
            {
                continue;
            }
            if (!isPortValue(valueId))
            {
                const grh::ir::OperationId defOpId = view.valueDef(valueId);
                if (defOpId.valid())
                {
                    const auto defKind = view.opKind(defOpId);
                    if (defKind == grh::ir::OperationKind::kConstant ||
                        defKind == grh::ir::OperationKind::kAssign)
                    {
                        if (materializeValues.find(valueId) == materializeValues.end())
                        {
                            continue;
                        }
                    }
                }
            }
            ensureWireDecl(valueId);
        }

        // -------------------------
        // Module emission
        // -------------------------
        moduleBuffer << "module " << moduleName << " (\n";
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
                    moduleBuffer << ",\n";
                }
                first = false;
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                if (decl.dir == PortDir::Input)
                {
                    moduleBuffer << "  input wire ";
                }
                else if (decl.dir == PortDir::Output)
                {
                    moduleBuffer << "  " << (decl.isReg ? "output reg " : "output wire ");
                }
                else
                {
                    moduleBuffer << "  inout wire ";
                }
                moduleBuffer << signedPrefix(decl.isSigned);
                const std::string range = widthRange(decl.width);
                if (!range.empty())
                {
                    moduleBuffer << range << " ";
                }
                moduleBuffer << name;
            };

            for (const auto &port : inputPorts)
            {
                emitPortLine(port.first);
            }
            for (const auto &port : outputPorts)
            {
                emitPortLine(port.first);
            }
            for (const auto &port : inoutPorts)
            {
                emitPortLine(port.name);
            }
            moduleBuffer << "\n);\n";
        }

        if (!wireDecls.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[name, decl] : wireDecls)
            {
                moduleBuffer << "  " << formatNetDecl("wire", name, decl) << '\n';
            }
        }

        if (!regDecls.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[name, decl] : regDecls)
            {
                moduleBuffer << "  " << formatNetDecl("reg", name, decl) << '\n';
            }
        }

        if (!memoryDecls.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[decl, opId] : memoryDecls)
            {
                const std::string attr = formatSrcAttribute(opSrcLoc(opId));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                moduleBuffer << "  " << decl << '\n';
            }
        }

        if (!instanceDecls.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[inst, opId] : instanceDecls)
            {
                const std::string attr = formatSrcAttribute(opSrcLoc(opId));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                moduleBuffer << "  " << inst << '\n';
            }
        }

        if (!dpiImportDecls.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[decl, opId] : dpiImportDecls)
            {
                const std::string attr = formatSrcAttribute(opSrcLoc(opId));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                moduleBuffer << "  " << decl << '\n';
            }
        }

        if (!portBindingStmts.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[stmt, opId] : portBindingStmts)
            {
                const std::string attr = formatSrcAttribute(opSrcLoc(opId));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                moduleBuffer << "  " << stmt << '\n';
            }
        }

        if (!assignStmts.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[stmt, opId] : assignStmts)
            {
                const std::string attr = formatSrcAttribute(opSrcLoc(opId));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                moduleBuffer << "  " << stmt << '\n';
            }
        }

        if (!latchBlocks.empty())
        {
            moduleBuffer << '\n';
            for (const auto &[block, opId] : latchBlocks)
            {
                const std::string attr = formatSrcAttribute(opSrcLoc(opId));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << '\n';
                }
                moduleBuffer << block << '\n';
            }
        }

        auto sensitivityList = [&](const IrSeqKey &key) -> std::string
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
                out.append(valueName(event.signal));
            }
            out.push_back(')');
            return out;
        };

        if (!seqBlocks.empty())
        {
            moduleBuffer << '\n';
            for (const auto &seq : seqBlocks)
            {
                const std::string sens = sensitivityList(seq.key);
                if (sens.empty())
                {
                    reportError("Sequential block missing sensitivity list", std::string(moduleName));
                    continue;
                }
                const std::string attr = formatSrcAttribute(opSrcLoc(seq.op));
                if (!attr.empty())
                {
                    moduleBuffer << "  " << attr << "\n";
                }
                moduleBuffer << "  always " << sens << " begin\n";
                for (const auto &stmt : seq.stmts)
                {
                    moduleBuffer << stmt;
                    if (!stmt.empty() && stmt.back() != '\n')
                    {
                        moduleBuffer << '\n';
                    }
                }
                moduleBuffer << "  end\n";
            }
        }

        moduleBuffer << "endmodule\n";

        const std::string filename = options.outputFilename.value_or(std::string("grh.sv"));
        const std::filesystem::path outputPath = resolveOutputDir(options) / filename;
        auto stream = openOutputFile(outputPath);
        if (!stream)
        {
            result.success = false;
            return result;
        }
        *stream << moduleBuffer.str();
        result.artifacts.push_back(outputPath.string());
        if (diagnostics() && diagnostics()->hasError())
        {
            result.success = false;
        }
        return result;
    }

} // namespace grh::emit
