#include "emit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <unordered_map>
#include <tuple>

#include "slang/text/Json.h"

namespace wolf_sv::emit
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

        void appendNewlineAndIndent(std::string &out, int indent)
        {
            out.push_back('\n');
            out.append(static_cast<std::size_t>(indent * kIndentSize), ' ');
        }

        std::vector<const grh::Graph *> graphsSortedByName(const grh::Netlist &netlist)
        {
            std::vector<const grh::Graph *> graphs;
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

        std::string serializeWithJsonWriter(const grh::Netlist &netlist,
                                            std::span<const grh::Graph *const> topGraphs,
                                            bool pretty)
        {
            slang::JsonWriter writer;
            writer.setPrettyPrint(pretty);
            writer.startObject();

            writer.writeProperty("graphs");
            writer.startArray();
            for (const grh::Graph *graph : graphsSortedByName(netlist))
            {
                graph->writeJson(writer);
            }
            writer.endArray();

            writer.writeProperty("tops");
            writer.startArray();
            for (const grh::Graph *graph : topGraphs)
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
                              const std::optional<grh::SrcLoc> &debugInfo)
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

        std::string formatSrcAttribute(const std::optional<grh::SrcLoc> &srcLoc)
        {
            if (!srcLoc || srcLoc->file.empty() || srcLoc->line == 0)
            {
                return {};
            }

            const uint32_t startLine = srcLoc->line;
            const uint32_t startCol = srcLoc->column;
            const uint32_t endLine = srcLoc->endLine ? srcLoc->endLine : startLine;
            const uint32_t endCol = srcLoc->endColumn ? srcLoc->endColumn : startCol;

            std::ostringstream oss;
            oss << "(* src = \"" << srcLoc->file << ":" << startLine;
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
            oss << "\" *)";
            return oss.str();
        }

        void writeUsersInline(std::string &out, std::span<const grh::ValueUser> users, JsonPrintMode mode)
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
                                           { appendQuotedString(out, user.operationSymbol); });
                                      prop("idx", [&]
                                           { out.append(std::to_string(static_cast<int64_t>(user.operandIndex))); });
                                  });
                first = false;
            }
            out.push_back(']');
        }

        void writeAttrsInline(std::string &out, const std::map<std::string, grh::AttributeValue> &attrs, JsonPrintMode mode)
        {
            out.push_back('{');
            bool firstAttr = true;
            const char *comma = commaToken(mode);
            for (const auto &[key, value] : attrs)
            {
                if (!firstAttr)
                {
                    out.append(comma);
                }
                appendQuotedString(out, key);
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
                                          value);
                                  });
                firstAttr = false;
            }
            out.push_back('}');
        }

        void writeValueInline(std::string &out, const grh::Value &value, JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("sym", [&]
                                       { appendQuotedString(out, value.symbol()); });
                                  prop("w", [&]
                                       { out.append(std::to_string(value.width())); });
                                  prop("sgn", [&]
                                       { out.append(value.isSigned() ? "true" : "false"); });
                                  prop("in", [&]
                                       { out.append(value.isInput() ? "true" : "false"); });
                                  prop("out", [&]
                                       { out.append(value.isOutput() ? "true" : "false"); });
                                  if (value.definingOp())
                                  {
                                      prop("def", [&]
                                           { appendQuotedString(out, value.definingOp()->symbol()); });
                                  }
                                  prop("users", [&]
                                       { writeUsersInline(out, value.users(), mode); });
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

        void writeOperationInline(std::string &out, const grh::Operation &op, JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("sym", [&]
                                       { appendQuotedString(out, op.symbol()); });
                                  prop("kind", [&]
                                       { appendQuotedString(out, grh::toString(op.kind())); });
                                  prop("in", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const grh::Value *operandPtr : op.operands())
                                           {
                                               const grh::Value &operand = *operandPtr;
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, operand.symbol());
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  prop("out", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const grh::Value *resultPtr : op.results())
                                           {
                                               const grh::Value &result = *resultPtr;
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, result.symbol());
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  if (!op.attributes().empty())
                                  {
                                      prop("attrs", [&]
                                           { writeAttrsInline(out, op.attributes(), mode); });
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
                                     const std::map<std::string, grh::ValueId> &ports,
                                     JsonPrintMode mode,
                                     int indent)
        {
            (void)mode;
            out.push_back('[');
            bool first = true;
            for (const auto &[name, valueSymbol] : ports)
            {
                if (!first)
                {
                    out.push_back(',');
                }
                appendNewlineAndIndent(out, indent);
                writePortInline(out, name, valueSymbol, mode);
                first = false;
            }
            if (!ports.empty())
            {
                appendNewlineAndIndent(out, indent - 1);
            }
            out.push_back(']');
        }

        void writeGraphPrettyCompact(std::string &out, const grh::Graph &graph, int baseIndent)
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
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, graph.valueOrder(),
                             [&](const grh::ValueId &valueSymbol)
                             { writeValueInline(out, graph.getValue(valueSymbol), JsonPrintMode::PrettyCompact); });
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "ports");
            out.append(": ");
            out.push_back('{');
            int portsIndent = indent + 1;

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "in");
            out.append(": ");
            writePortsPrettyCompact(out, graph.inputPorts(), JsonPrintMode::PrettyCompact, portsIndent + 1);
            out.push_back(',');

            appendNewlineAndIndent(out, portsIndent);
            appendQuotedString(out, "out");
            out.append(": ");
            writePortsPrettyCompact(out, graph.outputPorts(), JsonPrintMode::PrettyCompact, portsIndent + 1);

            appendNewlineAndIndent(out, indent);
            out.push_back('}');
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "ops");
            out.append(": ");
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, graph.operationOrder(),
                             [&](const grh::OperationId &opSymbol)
                             { writeOperationInline(out, graph.getOperation(opSymbol), JsonPrintMode::PrettyCompact); });

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
                                const grh::ir::GraphSymbolTable &symbols,
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
                appendQuotedString(out, symbols.text(attr.key));
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
                                       { appendQuotedString(out, grh::toString(view.opKind(opId))); });
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
                                           { writeAttrsInlineIr(out, attrs, symbols, mode); });
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

        void writeAttributeValue(slang::JsonWriter &writer, const grh::AttributeValue &value)
        {
            if (!grh::attributeValueIsJsonSerializable(value))
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

        void writeSrcLocJson(slang::JsonWriter &writer, const std::optional<grh::SrcLoc> &srcLoc)
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

            writer.endObject(); // ports

            writer.writeProperty("ops");
            writer.startArray();
            for (const auto opId : view.operations())
            {
                writer.startObject();
                writer.writeProperty("sym");
                writer.writeValue(lookupName(opNames, opId.index));
                writer.writeProperty("kind");
                writer.writeValue(grh::toString(view.opKind(opId)));

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
                        writer.writeProperty(symbols.text(attr.key));
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

        std::string serializePrettyCompact(const grh::Netlist &netlist, std::span<const grh::Graph *const> topGraphs)
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
                for (const grh::Graph *graph : graphs)
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
                for (const grh::Graph *graph : topGraphs)
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

        std::string serializeNetlistJson(const grh::Netlist &netlist,
                                         std::span<const grh::Graph *const> topGraphs,
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

    bool Emit::validateTopGraphs(const std::vector<const grh::Graph *> &topGraphs) const
    {
        if (topGraphs.empty())
        {
            reportError("No top graphs available for emission");
            return false;
        }
        return true;
    }

    std::vector<const grh::Graph *> Emit::resolveTopGraphs(const grh::Netlist &netlist,
                                                           const EmitOptions &options) const
    {
        std::vector<const grh::Graph *> result;
        std::unordered_set<std::string> seen;

        auto tryAdd = [&](std::string_view name)
        {
            if (seen.find(std::string(name)) != seen.end())
            {
                return;
            }

            const grh::Graph *graph = netlist.findGraph(name);
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

    EmitResult Emit::emit(const grh::Netlist &netlist, const EmitOptions &options)
    {
        EmitResult result;

        std::vector<const grh::Graph *> topGraphs = resolveTopGraphs(netlist, options);
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

    std::optional<std::string> EmitJSON::emitToString(const grh::Netlist &netlist, const EmitOptions &options)
    {
        std::vector<const grh::Graph *> topGraphs = resolveTopGraphs(netlist, options);
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

    EmitResult EmitJSON::emitImpl(const grh::Netlist &netlist,
                                  std::span<const grh::Graph *const> topGraphs,
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
            Output
        };

        struct NetDecl
        {
            int64_t width = 1;
            bool isSigned = false;
            std::optional<grh::SrcLoc> debug;
        };

        struct PortDecl
        {
            PortDir dir = PortDir::Input;
            int64_t width = 1;
            bool isSigned = false;
            bool isReg = false;
            std::optional<grh::SrcLoc> debug;
        };

        struct SeqKey
        {
            const grh::Value *clk = nullptr;
            std::string clkEdge;
            const grh::Value *asyncRst = nullptr;
            std::string asyncEdge;
            const grh::Value *syncRst = nullptr;
            std::string syncPolarity;
        };

        struct SeqKeyLess
        {
            bool operator()(const SeqKey &lhs, const SeqKey &rhs) const
            {
                auto lhsTuple = std::make_tuple(lhs.clk, lhs.clkEdge, lhs.asyncRst, lhs.asyncEdge, lhs.syncRst, lhs.syncPolarity);
                auto rhsTuple = std::make_tuple(rhs.clk, rhs.clkEdge, rhs.asyncRst, rhs.asyncEdge, rhs.syncRst, rhs.syncPolarity);
                return lhsTuple < rhsTuple;
            }
        };

        struct SeqBlock
        {
            SeqKey key{};
            std::vector<std::string> stmts;
            const grh::Operation *op = nullptr;
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
        std::optional<T> getAttribute(const grh::Operation &op, std::string_view key)
        {
            auto it = op.attributes().find(std::string(key));
            if (it == op.attributes().end())
            {
                return std::nullopt;
            }
            if (const auto *ptr = std::get_if<T>(&it->second))
            {
                return *ptr;
            }
            return std::nullopt;
        }

        template <typename T>
        std::optional<T> getAttribute(const grh::ir::GraphView &view,
                                      const grh::ir::GraphSymbolTable &symbols,
                                      grh::ir::OperationId op,
                                      std::string_view key)
        {
            const grh::ir::SymbolId keyId = symbols.lookup(key);
            if (!keyId.valid())
            {
                return std::nullopt;
            }
            auto attr = view.opAttr(op, keyId);
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

        std::string binOpToken(grh::OperationKind kind)
        {
            switch (kind)
            {
            case grh::OperationKind::kAdd:
                return "+";
            case grh::OperationKind::kSub:
                return "-";
            case grh::OperationKind::kMul:
                return "*";
            case grh::OperationKind::kDiv:
                return "/";
            case grh::OperationKind::kMod:
                return "%";
            case grh::OperationKind::kEq:
                return "==";
            case grh::OperationKind::kNe:
                return "!=";
            case grh::OperationKind::kLt:
                return "<";
            case grh::OperationKind::kLe:
                return "<=";
            case grh::OperationKind::kGt:
                return ">";
            case grh::OperationKind::kGe:
                return ">=";
            case grh::OperationKind::kAnd:
                return "&";
            case grh::OperationKind::kOr:
                return "|";
            case grh::OperationKind::kXor:
                return "^";
            case grh::OperationKind::kXnor:
                return "~^";
            case grh::OperationKind::kLogicAnd:
                return "&&";
            case grh::OperationKind::kLogicOr:
                return "||";
            case grh::OperationKind::kShl:
                return "<<";
            case grh::OperationKind::kLShr:
                return ">>";
            case grh::OperationKind::kAShr:
                return ">>>";
            default:
                return {};
            }
        }

        std::string unaryOpToken(grh::OperationKind kind)
        {
            switch (kind)
            {
            case grh::OperationKind::kNot:
                return "~";
            case grh::OperationKind::kLogicNot:
                return "!";
            case grh::OperationKind::kReduceAnd:
                return "&";
            case grh::OperationKind::kReduceOr:
                return "|";
            case grh::OperationKind::kReduceXor:
                return "^";
            case grh::OperationKind::kReduceNor:
                return "~|";
            case grh::OperationKind::kReduceNand:
                return "~&";
            case grh::OperationKind::kReduceXnor:
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

        std::string sensitivityList(const SeqKey &key)
        {
            if (key.clk == nullptr || key.clkEdge.empty())
            {
                return {};
            }
            std::string out = "@(" + key.clkEdge + " " + key.clk->symbol();
            if (key.asyncRst && !key.asyncEdge.empty())
            {
                out.append(" or ");
                out.append(key.asyncEdge);
                out.push_back(' ');
                out.append(key.asyncRst->symbol());
            }
            out.push_back(')');
            return out;
        }
    } // namespace

    EmitResult EmitSystemVerilog::emitImpl(const grh::Netlist &netlist,
                                           std::span<const grh::Graph *const> topGraphs,
                                           const EmitOptions &options)
    {
        EmitResult result;
        (void)topGraphs;

        // Index DPI imports across the netlist for later resolution.
        std::unordered_map<std::string, const grh::Operation *> dpicImports;
        for (const auto &graphSymbol : netlist.graphOrder())
        {
            auto graphIt = netlist.graphs().find(graphSymbol);
            if (graphIt == netlist.graphs().end() || !graphIt->second)
            {
                continue;
            }
            const grh::Graph &graph = *graphIt->second;
            for (const auto &opSymbol : graph.operationOrder())
            {
                const grh::Operation &op = graph.getOperation(opSymbol);
                if (op.kind() == grh::OperationKind::kDpicImport)
                {
                    dpicImports.emplace(op.symbol(), &op);
                }
            }
        }

        std::unordered_map<std::string, std::string> emittedModuleNames;
        std::unordered_set<std::string> usedModuleNames;
        for (const auto &graphSymbol : netlist.graphOrder())
        {
            const grh::Graph *graph = netlist.findGraph(graphSymbol);
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
        for (const grh::Graph *graph : graphsSortedByName(netlist))
        {
            if (!firstModule)
            {
                moduleBuffer << '\n';
            }
            firstModule = false;

            const auto moduleNameIt = emittedModuleNames.find(graph->symbol());
            const std::string &moduleName = moduleNameIt != emittedModuleNames.end() ? moduleNameIt->second : graph->symbol();

            // -------------------------
            // Ports
            // -------------------------
            std::map<std::string, PortDecl, std::less<>> portDecls;
            for (const auto &[name, valueSymbol] : graph->inputPorts())
            {
                const grh::Value *value = graph->findValue(valueSymbol);
                if (!value)
                {
                    continue;
                }
                portDecls[name] = PortDecl{PortDir::Input, value->width(), value->isSigned(), false,
                                           value->srcLoc()};
            }
            for (const auto &[name, valueSymbol] : graph->outputPorts())
            {
                const grh::Value *value = graph->findValue(valueSymbol);
                if (!value)
                {
                    continue;
                }
                portDecls[name] = PortDecl{PortDir::Output, value->width(), value->isSigned(), false,
                                           value->srcLoc()};
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
            std::vector<std::pair<std::string, const grh::Operation *>> memoryDecls;
            std::vector<std::pair<std::string, const grh::Operation *>> instanceDecls;
            std::vector<std::pair<std::string, const grh::Operation *>> dpiImportDecls;
            std::vector<std::pair<std::string, const grh::Operation *>> assignStmts;
            std::vector<std::pair<std::string, const grh::Operation *>> portBindingStmts;
            std::vector<std::pair<std::string, const grh::Operation *>> latchBlocks;
            std::vector<SeqBlock> seqBlocks;
            std::unordered_set<std::string> instanceNamesUsed;

            auto ensureRegDecl = [&](const std::string &name, int64_t width, bool isSigned,
                                     const std::optional<grh::SrcLoc> &debug = std::nullopt)
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

            auto ensureWireDecl = [&](const grh::Value &value)
            {
                const std::string &name = value.symbol();
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

            auto addAssign = [&](std::string stmt, const grh::Operation *sourceOp)
            {
                assignStmts.emplace_back(std::move(stmt), sourceOp);
            };

            auto addSequentialStmt = [&](const SeqKey &key, std::string stmt,
                                         const grh::Operation *sourceOp)
            {
                seqBlocks.push_back(SeqBlock{key, std::vector<std::string>{std::move(stmt)}, sourceOp});
            };

            auto addLatchBlock = [&](std::string stmt, const grh::Operation *sourceOp)
            {
                latchBlocks.emplace_back(std::move(stmt), sourceOp);
            };

            auto markPortAsRegIfNeeded = [&](const grh::Value &value)
            {
                auto itPort = portDecls.find(value.symbol());
                if (itPort != portDecls.end() && itPort->second.dir == PortDir::Output)
                {
                    itPort->second.isReg = true;
                }
            };

            auto resolveMemorySymbol = [&](const grh::Operation &userOp) -> std::optional<std::string>
            {
                auto attr = getAttribute<std::string>(userOp, "memSymbol");
                if (attr)
                {
                    return attr;
                }

                std::optional<std::string> candidate;
                for (const auto &maybeSym : graph->operationOrder())
                {
                    const auto &maybeOp = graph->getOperation(maybeSym);
                    if (maybeOp.kind() == grh::OperationKind::kMemory)
                    {
                        if (candidate)
                        {
                            return std::nullopt;
                        }
                        candidate = maybeOp.symbol();
                    }
                }
                return candidate;
            };

            // -------------------------
            // Port bindings (handle ports mapped to internal nets with different names)
            // -------------------------
            auto bindInputPort = [&](const std::string &portName, const std::string &valueSymbol)
            {
                if (portName == valueSymbol)
                {
                    return;
                }
                if (const grh::Value *val = graph->findValue(valueSymbol))
                {
                    ensureWireDecl(*val);
                }
                portBindingStmts.emplace_back(
                    "assign " + valueSymbol + " = " + portName + ";", nullptr);
            };

            auto bindOutputPort = [&](const std::string &portName, const std::string &valueSymbol)
            {
                if (portName == valueSymbol)
                {
                    return;
                }
                portBindingStmts.emplace_back(
                    "assign " + portName + " = " + valueSymbol + ";", nullptr);
            };

            for (const auto &[name, valueSymbol] : graph->inputPorts())
            {
                bindInputPort(name, valueSymbol);
            }
            for (const auto &[name, valueSymbol] : graph->outputPorts())
            {
                bindOutputPort(name, valueSymbol);
            }

            // -------------------------
            // Operation traversal
            // -------------------------
            for (const auto &opSymbol : graph->operationOrder())
            {
                const grh::Operation &op = graph->getOperation(opSymbol);
                const auto &operands = op.operands();
                const auto &results = op.results();
                auto normalizeLower = [](const std::optional<std::string> &attr) -> std::optional<std::string>
                {
                    if (!attr)
                    {
                        return std::nullopt;
                    }
                    std::string v = *attr;
                    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                                   { return static_cast<char>(std::tolower(c)); });
                    return v;
                };
                auto parsePolarityBool = [&](const std::optional<std::string> &attr,
                                             std::string_view name) -> std::optional<bool>
                {
                    auto norm = normalizeLower(attr);
                    if (!norm)
                    {
                        reportError(std::string(name) + " missing", op.symbol());
                        return std::nullopt;
                    }
                    if (*norm == "high" || *norm == "1'b1")
                    {
                        return true;
                    }
                    if (*norm == "low" || *norm == "1'b0")
                    {
                        return false;
                    }
                    reportError("Unknown " + std::string(name) + " value: " + *attr, op.symbol());
                    return std::nullopt;
                };
                auto formatEnableExpr = [&](const grh::Value *enVal, std::string_view enLevel) -> std::optional<std::string>
                {
                    if (!enVal)
                    {
                        return std::nullopt;
                    }
                    if (enLevel == "high")
                    {
                        return enVal->symbol();
                    }
                    if (enLevel == "low")
                    {
                        return "!(" + enVal->symbol() + ")";
                    }
                    reportError("Unknown enLevel: " + std::string(enLevel), op.symbol());
                    return std::nullopt;
                };

                switch (op.kind())
                {
                case grh::OperationKind::kConstant:
                {
                    if (results.empty())
                    {
                        reportError("kConstant missing result", op.symbol());
                        break;
                    }
                    auto constValue = getAttribute<std::string>(op, "constValue");
                    if (!constValue)
                    {
                        reportError("kConstant missing constValue attribute", op.symbol());
                        break;
                    }
                    addAssign("assign " + results[0]->symbol() + " = " + *constValue + ";", &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kAdd:
                case grh::OperationKind::kSub:
                case grh::OperationKind::kMul:
                case grh::OperationKind::kDiv:
                case grh::OperationKind::kMod:
                case grh::OperationKind::kEq:
                case grh::OperationKind::kNe:
                case grh::OperationKind::kLt:
                case grh::OperationKind::kLe:
                case grh::OperationKind::kGt:
                case grh::OperationKind::kGe:
                case grh::OperationKind::kAnd:
                case grh::OperationKind::kOr:
                case grh::OperationKind::kXor:
                case grh::OperationKind::kXnor:
                case grh::OperationKind::kLogicAnd:
                case grh::OperationKind::kLogicOr:
                case grh::OperationKind::kShl:
                case grh::OperationKind::kLShr:
                case grh::OperationKind::kAShr:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Binary operation missing operands or results", op.symbol());
                        break;
                    }
                    const std::string tok = binOpToken(op.kind());
                    addAssign("assign " + results[0]->symbol() + " = " + operands[0]->symbol() + " " + tok + " " + operands[1]->symbol() + ";", &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kNot:
                case grh::OperationKind::kLogicNot:
                case grh::OperationKind::kReduceAnd:
                case grh::OperationKind::kReduceOr:
                case grh::OperationKind::kReduceXor:
                case grh::OperationKind::kReduceNor:
                case grh::OperationKind::kReduceNand:
                case grh::OperationKind::kReduceXnor:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("Unary operation missing operands or results", op.symbol());
                        break;
                    }
                    const std::string tok = unaryOpToken(op.kind());
                    addAssign("assign " + results[0]->symbol() + " = " + tok + operands[0]->symbol() + ";", &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kMux:
                {
                    if (operands.size() < 3 || results.empty())
                    {
                        reportError("kMux missing operands or results", op.symbol());
                        break;
                    }
                    addAssign("assign " + results[0]->symbol() + " = " + operands[0]->symbol() + " ? " + operands[1]->symbol() + " : " + operands[2]->symbol() + ";", &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kAssign:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kAssign missing operands or results", op.symbol());
                        break;
                    }
                    addAssign("assign " + results[0]->symbol() + " = " + operands[0]->symbol() + ";", &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kConcat:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("kConcat missing operands or results", op.symbol());
                        break;
                    }
                    std::ostringstream expr;
                    expr << "assign " << results[0]->symbol() << " = {";
                    for (std::size_t i = 0; i < operands.size(); ++i)
                    {
                        if (i != 0)
                        {
                            expr << ", ";
                        }
                        expr << operands[i]->symbol();
                    }
                    expr << "};";
                    addAssign(expr.str(), &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kReplicate:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kReplicate missing operands or results", op.symbol());
                        break;
                    }
                    auto rep = getAttribute<int64_t>(op, "rep");
                    if (!rep)
                    {
                        reportError("kReplicate missing rep attribute", op.symbol());
                        break;
                    }
                    std::ostringstream expr;
                    expr << "assign " << results[0]->symbol() << " = {" << *rep << "{" << operands[0]->symbol() << "}};";
                    addAssign(expr.str(), &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kSliceStatic:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("kSliceStatic missing operands or results", op.symbol());
                        break;
                    }
                    auto sliceStart = getAttribute<int64_t>(op, "sliceStart");
                    auto sliceEnd = getAttribute<int64_t>(op, "sliceEnd");
                    if (!sliceStart || !sliceEnd)
                    {
                        reportError("kSliceStatic missing sliceStart or sliceEnd", op.symbol());
                        break;
                    }
                    std::ostringstream expr;
                    expr << "assign " << results[0]->symbol() << " = " << operands[0]->symbol() << "[";
                    if (*sliceStart == *sliceEnd)
                    {
                        expr << *sliceStart;
                    }
                    else
                    {
                        expr << *sliceEnd << ":" << *sliceStart;
                    }
                    expr << "];";
                    addAssign(expr.str(), &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kSliceDynamic:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("kSliceDynamic missing operands or results", op.symbol());
                        break;
                    }
                    auto width = getAttribute<int64_t>(op, "sliceWidth");
                    if (!width)
                    {
                        reportError("kSliceDynamic missing sliceWidth", op.symbol());
                        break;
                    }
                    std::ostringstream expr;
                    expr << "assign " << results[0]->symbol() << " = " << operands[0]->symbol() << "[" << operands[1]->symbol() << " +: " << *width << "];";
                    addAssign(expr.str(), &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kSliceArray:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("kSliceArray missing operands or results", op.symbol());
                        break;
                    }
                    auto width = getAttribute<int64_t>(op, "sliceWidth");
                    if (!width)
                    {
                        reportError("kSliceArray missing sliceWidth", op.symbol());
                        break;
                    }
                    std::ostringstream expr;
                    expr << "assign " << results[0]->symbol() << " = " << operands[0]->symbol() << "[";
                    if (*width == 1)
                    {
                        expr << operands[1]->symbol();
                    }
                    else
                    {
                        expr << operands[1]->symbol() << " * " << *width << " +: " << *width;
                    }
                    expr << "];";
                    addAssign(expr.str(), &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kLatch:
                case grh::OperationKind::kLatchArst:
                {
                    if (operands.size() < 2 || results.empty())
                    {
                        reportError("Latch operation missing operands or results", op.symbol());
                        break;
                    }
                    const grh::Value *en = operands[0];
                    const grh::Value *rst = nullptr;
                    const grh::Value *resetVal = nullptr;
                    const grh::Value *d = nullptr;

                    if (op.kind() == grh::OperationKind::kLatch)
                    {
                        if (operands.size() >= 2)
                        {
                            d = operands[1];
                        }
                    }
                    else
                    {
                        if (operands.size() >= 4)
                        {
                            rst = operands[1];
                            resetVal = operands[2];
                            d = operands[3];
                        }
                    }

                    if (!en || !d)
                    {
                        reportError("Latch operation missing enable or data operand", op.symbol());
                        break;
                    }
                    if (en->width() != 1)
                    {
                        reportError("Latch enable must be 1 bit", op.symbol());
                        break;
                    }
                    if (op.kind() == grh::OperationKind::kLatchArst)
                    {
                        if (!rst || !resetVal)
                        {
                            reportError("kLatchArst missing reset operands", op.symbol());
                            break;
                        }
                        if (rst->width() != 1)
                        {
                            reportError("Latch reset must be 1 bit", op.symbol());
                            break;
                        }
                    }

                    auto enLevelAttr = getAttribute<std::string>(op, "enLevel");
                    const std::string enLevel =
                        normalizeLower(enLevelAttr).value_or(std::string("high"));
                    auto enExpr = formatEnableExpr(en, enLevel);
                    if (!enExpr)
                    {
                        break;
                    }

                    std::optional<bool> rstActiveHigh;
                    if (op.kind() == grh::OperationKind::kLatchArst)
                    {
                        auto rstPolarityAttr = getAttribute<std::string>(op, "rstPolarity");
                        rstActiveHigh = parsePolarityBool(rstPolarityAttr, "Latch rstPolarity");
                        if (!rstActiveHigh)
                        {
                            break;
                        }
                    }

                    const std::string &regName = op.symbol();
                    const grh::Value *q = results[0];
                    if (q->width() != d->width())
                    {
                        reportError("Latch data/output width mismatch", op.symbol());
                        break;
                    }
                    if (op.kind() == grh::OperationKind::kLatchArst && resetVal &&
                        resetVal->width() != d->width())
                    {
                        reportError("Latch resetValue width mismatch", op.symbol());
                        break;
                    }
                    ensureRegDecl(regName, d->width(), d->isSigned(), op.srcLoc());
                    const bool directDrive = q->symbol() == regName;
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(*q);
                    }
                    else
                    {
                        ensureWireDecl(*q);
                        addAssign("assign " + q->symbol() + " = " + regName + ";", &op);
                    }

                    std::ostringstream stmt;
                    const int baseIndent = 2;
                    if (op.kind() == grh::OperationKind::kLatchArst)
                    {
                        if (!rst || !resetVal || !rstActiveHigh)
                        {
                            reportError("Latch with reset missing operands or polarity", op.symbol());
                            break;
                        }
                        const std::string rstCond =
                            *rstActiveHigh ? rst->symbol() : "!" + rst->symbol();
                        appendIndented(stmt, baseIndent, "if (" + rstCond + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " = " + resetVal->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end else if (" + *enExpr + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " = " + d->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end");
                    }
                    else
                    {
                        appendIndented(stmt, baseIndent, "if (" + *enExpr + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " = " + d->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end");
                    }

                    std::ostringstream block;
                    block << "  always_latch begin\n";
                    block << stmt.str();
                    block << "  end";
                    addLatchBlock(block.str(), &op);
                    break;
                }
                case grh::OperationKind::kRegister:
                case grh::OperationKind::kRegisterEn:
                case grh::OperationKind::kRegisterRst:
                case grh::OperationKind::kRegisterEnRst:
                case grh::OperationKind::kRegisterArst:
                case grh::OperationKind::kRegisterEnArst:
                {
                    if (operands.empty() || results.empty())
                    {
                        reportError("Register operation missing operands or results", op.symbol());
                        break;
                    }
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    if (!clkPolarity)
                    {
                        reportError("Register operation missing clkPolarity", op.symbol());
                        break;
                    }

                    const std::string &regName = op.symbol();
                    const grh::Value *q = results[0];
                    const grh::Value *clk = operands[0];
                    const grh::Value *rst = nullptr;
                    const grh::Value *en = nullptr;
                    const grh::Value *resetVal = nullptr;
                    const grh::Value *d = nullptr;

                    if (op.kind() == grh::OperationKind::kRegister)
                    {
                        if (operands.size() >= 2)
                        {
                            d = operands[1];
                        }
                    }
                    else if (op.kind() == grh::OperationKind::kRegisterEn)
                    {
                        if (operands.size() >= 3)
                        {
                            en = operands[1];
                            d = operands[2];
                        }
                    }
                    else if (op.kind() == grh::OperationKind::kRegisterRst || op.kind() == grh::OperationKind::kRegisterArst)
                    {
                        if (operands.size() >= 4)
                        {
                            rst = operands[1];
                            resetVal = operands[2];
                            d = operands[3];
                        }
                    }
                    else if (op.kind() == grh::OperationKind::kRegisterEnRst || op.kind() == grh::OperationKind::kRegisterEnArst)
                    {
                        if (operands.size() >= 5)
                        {
                            rst = operands[1];
                            en = operands[2];
                            resetVal = operands[3];
                            d = operands[4];
                        }
                    }

                    if (!d)
                    {
                        reportError("Register operation missing data operand", op.symbol());
                        break;
                    }

                    auto rstPolarityAttr = getAttribute<std::string>(op, "rstPolarity");
                    auto enLevelAttr = getAttribute<std::string>(op, "enLevel");
                    const std::string enLevel =
                        normalizeLower(enLevelAttr).value_or(std::string("high"));
                    const auto enExpr = formatEnableExpr(en, enLevel);
                    std::optional<bool> rstActiveHigh;
                    std::string asyncEdge;
                    if (op.kind() == grh::OperationKind::kRegisterArst ||
                        op.kind() == grh::OperationKind::kRegisterEnArst) {
                        rstActiveHigh = parsePolarityBool(rstPolarityAttr, "Register rstPolarity");
                        if (!rstActiveHigh) {
                            break;
                        }
                        asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                    } else if (rst) {
                        rstActiveHigh = parsePolarityBool(rstPolarityAttr, "Register rstPolarity");
                        if (!rstActiveHigh) {
                            break;
                        }
                    }

                    const bool directDrive = q->symbol() == regName;
                    ensureRegDecl(regName, d->width(), d->isSigned(), op.srcLoc());
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(*q);
                    }
                    else
                    {
                        ensureWireDecl(*q);
                        addAssign("assign " + q->symbol() + " = " + regName + ";", &op);
                    }

                    SeqKey key;
                    key.clk = clk;
                    key.clkEdge = *clkPolarity;
                    if (!asyncEdge.empty())
                    {
                        key.asyncRst = rst;
                        key.asyncEdge = asyncEdge;
                    }
                    else if (rst && rstActiveHigh)
                    {
                        key.syncRst = rst;
                        key.syncPolarity = *rstActiveHigh ? "high" : "low";
                    }

                    std::ostringstream stmt;
                    const int baseIndent = 2;
                    bool emitted = true;
                    switch (op.kind())
                    {
                    case grh::OperationKind::kRegister:
                        appendIndented(stmt, baseIndent, regName + " <= " + d->symbol() + ";");
                        break;
                    case grh::OperationKind::kRegisterEn:
                        if (!en)
                        {
                            reportError("kRegisterEn missing enable operand", op.symbol());
                            emitted = false;
                            break;
                        }
                        if (!enExpr)
                        {
                            emitted = false;
                            break;
                        }
                        appendIndented(stmt, baseIndent, "if (" + *enExpr + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " <= " + d->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end");
                        break;
                    case grh::OperationKind::kRegisterRst:
                    case grh::OperationKind::kRegisterArst: {
                        if (!rst || !rstActiveHigh || !resetVal)
                        {
                            reportError("Register with reset missing operand or rstPolarity", op.symbol());
                            emitted = false;
                            break;
                        }
                        const std::string rstCond =
                            *rstActiveHigh ? rst->symbol() : "!" + rst->symbol();
                        appendIndented(stmt, baseIndent, "if (" + rstCond + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " <= " + resetVal->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end else begin");
                        appendIndented(stmt, baseIndent + 1, regName + " <= " + d->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end");
                        break;
                    }
                    case grh::OperationKind::kRegisterEnRst:
                    case grh::OperationKind::kRegisterEnArst: {
                        if (!rst || !rstActiveHigh || !resetVal || !en)
                        {
                            reportError("kRegisterEnRst missing operands", op.symbol());
                            emitted = false;
                            break;
                        }
                        if (!enExpr)
                        {
                            emitted = false;
                            break;
                        }
                        const std::string rstCond =
                            *rstActiveHigh ? rst->symbol() : "!" + rst->symbol();
                        appendIndented(stmt, baseIndent, "if (" + rstCond + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " <= " + resetVal->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end else if (" + *enExpr + ") begin");
                        appendIndented(stmt, baseIndent + 1, regName + " <= " + d->symbol() + ";");
                        appendIndented(stmt, baseIndent, "end");
                        break;
                    }
                    default:
                        emitted = false;
                        break;
                    }

                    if (emitted)
                    {
                        addSequentialStmt(key, stmt.str(), &op);
                    }
                    break;
                }
                case grh::OperationKind::kMemory:
                {
                    auto widthAttr = getAttribute<int64_t>(op, "width");
                    auto rowAttr = getAttribute<int64_t>(op, "row");
                    auto isSignedAttr = getAttribute<bool>(op, "isSigned");
                    if (!widthAttr || !rowAttr || !isSignedAttr)
                    {
                        reportError("kMemory missing width/row/isSigned", op.symbol());
                        break;
                    }
                    std::ostringstream decl;
                    decl << "reg " << (*isSignedAttr ? "signed " : "") << "[" << (*widthAttr - 1) << ":0] " << op.symbol() << " [0:" << (*rowAttr - 1) << "];";
                    memoryDecls.emplace_back(decl.str(), &op);
                    declaredNames.insert(op.symbol());
                    break;
                }
                case grh::OperationKind::kMemoryAsyncReadPort:
                {
                    if (operands.size() < 1 || results.empty())
                    {
                        reportError("kMemoryAsyncReadPort missing operands or results", op.symbol());
                        break;
                    }
                    auto memSymbol = getAttribute<std::string>(op, "memSymbol");
                    if (!memSymbol)
                    {
                        reportError("kMemoryAsyncReadPort missing memSymbol", op.symbol());
                        break;
                    }
                    addAssign("assign " + results[0]->symbol() + " = " + *memSymbol + "[" + operands[0]->symbol() + "];", &op);
                    ensureWireDecl(*results[0]);
                    break;
                }
                case grh::OperationKind::kMemorySyncReadPort:
                case grh::OperationKind::kMemorySyncReadPortRst:
                case grh::OperationKind::kMemorySyncReadPortArst:
                {
                    const bool hasReset =
                        op.kind() == grh::OperationKind::kMemorySyncReadPortRst ||
                        op.kind() == grh::OperationKind::kMemorySyncReadPortArst;
                    const bool asyncReset = op.kind() == grh::OperationKind::kMemorySyncReadPortArst;
                    const std::size_t expectedOperands = hasReset ? 4 : 3;
                    if (operands.size() < expectedOperands || results.empty())
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing operands or results", op.symbol());
                        break;
                    }
                    auto memSymbolAttr = resolveMemorySymbol(op);
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    if (!clkPolarity)
                    {
                        reportWarning(std::string(grh::toString(op.kind())) + " missing clkPolarity, defaulting to posedge", op.symbol());
                        clkPolarity = std::string("posedge");
                    }
                    auto rstPolarityAttr = hasReset ? getAttribute<std::string>(op, "rstPolarity")
                                                    : std::optional<std::string>{};
                    auto enLevelAttr = getAttribute<std::string>(op, "enLevel");
                    const std::string enLevel =
                        normalizeLower(enLevelAttr).value_or(std::string("high"));
                    const grh::Value *clk = operands[0];
                    const grh::Value *rst = hasReset ? operands[1] : nullptr;
                    const grh::Value *addr = operands[hasReset ? 2 : 1];
                    const grh::Value *en = operands[hasReset ? 3 : 2];
                    if (!clk || !addr || !en || (hasReset && !rst))
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing operands or results", op.symbol());
                        break;
                    }
                    auto enExpr = formatEnableExpr(en, enLevel);
                    if (!enExpr)
                    {
                        break;
                    }
                    std::optional<bool> rstActiveHigh;
                    if (hasReset)
                    {
                        rstActiveHigh = parsePolarityBool(rstPolarityAttr, "memory rstPolarity");
                        if (!rstActiveHigh)
                        {
                            break;
                        }
                    }
                    if (!memSymbolAttr)
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing memSymbol or clkPolarity", op.symbol());
                        break;
                    }
                    const std::string &memSymbol = *memSymbolAttr;

                    const grh::Operation *memOp = graph->findOperation(memSymbol);
                    int64_t memWidth = 1;
                    bool memSigned = false;
                    if (memOp)
                    {
                        memWidth = getAttribute<int64_t>(*memOp, "width").value_or(1);
                        memSigned = getAttribute<bool>(*memOp, "isSigned").value_or(false);
                    }
                    ensureRegDecl(op.symbol(), memWidth, memSigned, op.srcLoc());

                    SeqKey key{clk, *clkPolarity, nullptr, {}, nullptr, {}};
                    if (asyncReset && rst && rstActiveHigh)
                    {
                        key.asyncRst = rst;
                        key.asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                    }
                    else if (hasReset && rst && rstActiveHigh)
                    {
                        key.syncRst = rst;
                        key.syncPolarity = *rstActiveHigh ? "high" : "low";
                    }

                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (" + *enExpr + ") begin");
                    appendIndented(stmt, 3, op.symbol() + " <= " + memSymbol + "[" + addr->symbol() + "];");
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(key, stmt.str(), &op);

                    const grh::Value *data = results[0];
                    if (data->symbol() != op.symbol())
                    {
                        ensureWireDecl(*data);
                        addAssign("assign " + data->symbol() + " = " + op.symbol() + ";", &op);
                    }
                    else
                    {
                        markPortAsRegIfNeeded(*data);
                    }
                    break;
                }
                case grh::OperationKind::kMemoryWritePort:
                case grh::OperationKind::kMemoryWritePortRst:
                case grh::OperationKind::kMemoryWritePortArst:
                {
                    const bool hasReset =
                        op.kind() == grh::OperationKind::kMemoryWritePortRst ||
                        op.kind() == grh::OperationKind::kMemoryWritePortArst;
                    const bool asyncReset = op.kind() == grh::OperationKind::kMemoryWritePortArst;
                    const std::size_t expectedOperands = hasReset ? 5 : 4;
                    if (operands.size() < expectedOperands)
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing operands", op.symbol());
                        break;
                    }
                    auto memSymbolAttr = resolveMemorySymbol(op);
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    if (!clkPolarity)
                    {
                        reportWarning(std::string(grh::toString(op.kind())) + " missing clkPolarity, defaulting to posedge", op.symbol());
                        clkPolarity = std::string("posedge");
                    }
                    auto rstPolarityAttr = hasReset ? getAttribute<std::string>(op, "rstPolarity")
                                                    : std::optional<std::string>{};
                    auto enLevelAttr = getAttribute<std::string>(op, "enLevel");
                    const std::string enLevel =
                        normalizeLower(enLevelAttr).value_or(std::string("high"));
                    const grh::Value *clk = operands[0];
                    const grh::Value *rst = hasReset ? operands[1] : nullptr;
                    const grh::Value *addr = operands[hasReset ? 2 : 1];
                    const grh::Value *en = operands[hasReset ? 3 : 2];
                    const grh::Value *data = operands[hasReset ? 4 : 3];
                    if (!clk || !addr || !en || !data || (hasReset && !rst))
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing operands", op.symbol());
                        break;
                    }
                    auto enExpr = formatEnableExpr(en, enLevel);
                    if (!enExpr)
                    {
                        break;
                    }
                    std::optional<bool> rstActiveHigh;
                    if (hasReset)
                    {
                        rstActiveHigh = parsePolarityBool(rstPolarityAttr, "memory rstPolarity");
                        if (!rstActiveHigh)
                        {
                            break;
                        }
                    }
                    if (!memSymbolAttr)
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing memSymbol or clkPolarity", op.symbol());
                        break;
                    }
                    const std::string &memSymbol = *memSymbolAttr;

                    SeqKey key{clk, *clkPolarity, nullptr, {}, nullptr, {}};
                    if (asyncReset && rst && rstActiveHigh)
                    {
                        key.asyncRst = rst;
                        key.asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                    }
                    else if (hasReset && rst && rstActiveHigh)
                    {
                        key.syncRst = rst;
                        key.syncPolarity = *rstActiveHigh ? "high" : "low";
                    }

                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (" + *enExpr + ") begin");
                    appendIndented(stmt, 3, memSymbol + "[" + addr->symbol() + "] <= " + data->symbol() + ";");
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(key, stmt.str(), &op);
                    break;
                }
                case grh::OperationKind::kMemoryMaskWritePort:
                case grh::OperationKind::kMemoryMaskWritePortRst:
                case grh::OperationKind::kMemoryMaskWritePortArst:
                {
                    const bool hasReset =
                        op.kind() == grh::OperationKind::kMemoryMaskWritePortRst ||
                        op.kind() == grh::OperationKind::kMemoryMaskWritePortArst;
                    const bool asyncReset = op.kind() == grh::OperationKind::kMemoryMaskWritePortArst;
                    const std::size_t expectedOperands = hasReset ? 6 : 5;
                    if (operands.size() < expectedOperands)
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing operands", op.symbol());
                        break;
                    }
                    auto memSymbolAttr = resolveMemorySymbol(op);
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    if (!clkPolarity)
                    {
                        reportWarning(std::string(grh::toString(op.kind())) + " missing clkPolarity, defaulting to posedge", op.symbol());
                        clkPolarity = std::string("posedge");
                    }
                    auto rstPolarityAttr = hasReset ? getAttribute<std::string>(op, "rstPolarity")
                                                    : std::optional<std::string>{};
                    auto enLevelAttr = getAttribute<std::string>(op, "enLevel");
                    const std::string enLevel =
                        normalizeLower(enLevelAttr).value_or(std::string("high"));
                    const grh::Value *clk = operands[0];
                    const grh::Value *rst = hasReset ? operands[1] : nullptr;
                    const grh::Value *addr = operands[hasReset ? 2 : 1];
                    const grh::Value *en = operands[hasReset ? 3 : 2];
                    const grh::Value *data = operands[hasReset ? 4 : 3];
                    const grh::Value *mask = operands[hasReset ? 5 : 4];
                    if (!clk || !addr || !en || !data || !mask || (hasReset && !rst))
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing operands", op.symbol());
                        break;
                    }
                    auto enExpr = formatEnableExpr(en, enLevel);
                    if (!enExpr)
                    {
                        break;
                    }
                    std::optional<bool> rstActiveHigh;
                    if (hasReset)
                    {
                        rstActiveHigh = parsePolarityBool(rstPolarityAttr, "memory rstPolarity");
                        if (!rstActiveHigh)
                        {
                            break;
                        }
                    }
                    if (!memSymbolAttr)
                    {
                        reportError(std::string(grh::toString(op.kind())) + " missing memSymbol or clkPolarity", op.symbol());
                        break;
                    }
                    const std::string &memSymbol = *memSymbolAttr;
                    const grh::Operation *memOp = graph->findOperation(memSymbol);
                    int64_t memWidth = memOp ? getAttribute<int64_t>(*memOp, "width").value_or(1) : 1;

                    SeqKey key{clk, *clkPolarity, nullptr, {}, nullptr, {}};
                    if (asyncReset && rst && rstActiveHigh)
                    {
                        key.asyncRst = rst;
                        key.asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                    }
                    else if (hasReset && rst && rstActiveHigh)
                    {
                        key.syncRst = rst;
                        key.syncPolarity = *rstActiveHigh ? "high" : "low";
                    }

                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (" + *enExpr + ") begin");
                    appendIndented(stmt, 3, "if (" + mask->symbol() + " == {" + std::to_string(memWidth) + "{1'b1}}) begin");
                    appendIndented(stmt, 4, memSymbol + "[" + addr->symbol() + "] <= " + data->symbol() + ";");
                    appendIndented(stmt, 3, "end else begin");
                    appendIndented(stmt, 4, "integer i;");
                    appendIndented(stmt, 4, "for (i = 0; i < " + std::to_string(memWidth) + "; i = i + 1) begin");
                    appendIndented(stmt, 5, "if (" + mask->symbol() + "[i]) begin");
                    appendIndented(stmt, 6, memSymbol + "[" + addr->symbol() + "][i] <= " + data->symbol() + "[i];");
                    appendIndented(stmt, 5, "end");
                    appendIndented(stmt, 4, "end");
                    appendIndented(stmt, 3, "end");
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(key, stmt.str(), &op);
                    break;
                }
                case grh::OperationKind::kInstance:
                case grh::OperationKind::kBlackbox:
                {
                    auto moduleName = getAttribute<std::string>(op, "moduleName");
                    auto inputNames = getAttribute<std::vector<std::string>>(op, "inputPortName");
                    auto outputNames = getAttribute<std::vector<std::string>>(op, "outputPortName");
                    auto instanceNameBase = getAttribute<std::string>(op, "instanceName").value_or(op.symbol());
                    std::string instanceName = instanceNameBase;
                    int instSuffix = 1;
                    while (!instanceNamesUsed.insert(instanceName).second)
                    {
                        instanceName = instanceNameBase + "_" + std::to_string(instSuffix++);
                    }
                    if (!moduleName || !inputNames || !outputNames)
                    {
                        reportError("Instance missing module or port names", op.symbol());
                        break;
                    }
                    const auto moduleNameIt = emittedModuleNames.find(*moduleName);
                    const std::string &targetModuleName =
                        moduleNameIt != emittedModuleNames.end() ? moduleNameIt->second : *moduleName;

                    std::ostringstream decl;
                    if (op.kind() == grh::OperationKind::kBlackbox)
                    {
                        auto paramNames = getAttribute<std::vector<std::string>>(op, "parameterNames");
                        auto paramValues = getAttribute<std::vector<std::string>>(op, "parameterValues");
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
                    connections.reserve(inputNames->size() + outputNames->size());
                    for (std::size_t i = 0; i < inputNames->size(); ++i)
                    {
                        if (i < operands.size())
                        {
                            connections.emplace_back((*inputNames)[i], operands[i]->symbol());
                        }
                    }
                    for (std::size_t i = 0; i < outputNames->size(); ++i)
                    {
                        if (i < results.size())
                        {
                            connections.emplace_back((*outputNames)[i], results[i]->symbol());
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
                    instanceDecls.emplace_back(decl.str(), &op);

                    for (const grh::Value *res : results)
                    {
                        ensureWireDecl(*res);
                    }
                    break;
                }
                case grh::OperationKind::kDisplay:
                {
                    if (operands.size() < 2)
                    {
                        reportError("kDisplay missing operands", op.symbol());
                        break;
                    }
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    auto format = getAttribute<std::string>(op, "formatString");
                    if (!clkPolarity || !format)
                    {
                        reportError("kDisplay missing clkPolarity or formatString", op.symbol());
                        break;
                    }
                    SeqKey key{operands[0], *clkPolarity, nullptr, {}, nullptr, {}};
                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (" + operands[1]->symbol() + ") begin");
                    stmt << std::string(kIndentSizeSv * 3, ' ') << "$display(\"" << *format << "\"";
                    for (std::size_t i = 2; i < operands.size(); ++i)
                    {
                        stmt << ", " << operands[i]->symbol();
                    }
                    stmt << ");\n";
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(key, stmt.str(), &op);
                    break;
                }
                case grh::OperationKind::kAssert:
                {
                    if (operands.size() < 2)
                    {
                        reportError("kAssert missing operands", op.symbol());
                        break;
                    }
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    if (!clkPolarity)
                    {
                        reportError("kAssert missing clkPolarity", op.symbol());
                        break;
                    }
                    auto message = getAttribute<std::string>(op, "message").value_or(std::string("Assertion failed"));
                    SeqKey key{operands[0], *clkPolarity, nullptr, {}, nullptr, {}};
                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (!" + operands[1]->symbol() + ") begin");
                    appendIndented(stmt, 3, "$fatal(\"" + message + " at time %0t\", $time);");
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(key, stmt.str(), &op);
                    break;
                }
                case grh::OperationKind::kDpicImport:
                {
                    auto argsDir = getAttribute<std::vector<std::string>>(op, "argsDirection");
                    auto argsWidth = getAttribute<std::vector<int64_t>>(op, "argsWidth");
                    auto argsName = getAttribute<std::vector<std::string>>(op, "argsName");
                    if (!argsDir || !argsWidth || !argsName || argsDir->size() != argsWidth->size() || argsDir->size() != argsName->size())
                    {
                        reportError("kDpicImport missing or inconsistent arg metadata", op.symbol());
                        break;
                    }
                    std::ostringstream decl;
                    decl << "import \"DPI-C\" function void " << op.symbol() << " (\n";
                    for (std::size_t i = 0; i < argsDir->size(); ++i)
                    {
                        decl << "  " << (*argsDir)[i] << " logic ";
                        if ((*argsWidth)[i] > 1)
                        {
                            decl << "[" << ((*argsWidth)[i] - 1) << ":0] ";
                        }
                        decl << (*argsName)[i];
                        decl << (i + 1 == argsDir->size() ? "\n" : ",\n");
                    }
                    decl << ");";
                    dpiImportDecls.emplace_back(decl.str(), &op);
                    break;
                }
                case grh::OperationKind::kDpicCall:
                {
                    if (operands.size() < 2)
                    {
                        reportError("kDpicCall missing clk/enable operands", op.symbol());
                        break;
                    }
                    auto clkPolarity = getAttribute<std::string>(op, "clkPolarity");
                    auto targetImport = getAttribute<std::string>(op, "targetImportSymbol");
                    auto inArgName = getAttribute<std::vector<std::string>>(op, "inArgName");
                    auto outArgName = getAttribute<std::vector<std::string>>(op, "outArgName");
                    if (!clkPolarity || !targetImport || !inArgName || !outArgName)
                    {
                        reportError("kDpicCall missing metadata", op.symbol());
                        break;
                    }
                    auto itImport = dpicImports.find(*targetImport);
                    if (itImport == dpicImports.end() || !itImport->second)
                    {
                        reportError("kDpicCall cannot resolve import symbol", *targetImport);
                        break;
                    }
                    auto importArgs = getAttribute<std::vector<std::string>>(*itImport->second, "argsName");
                    auto importDirs = getAttribute<std::vector<std::string>>(*itImport->second, "argsDirection");
                    if (!importArgs || !importDirs || importArgs->size() != importDirs->size())
                    {
                        reportError("kDpicCall found malformed import signature", *targetImport);
                        break;
                    }

                    // Declare intermediate regs for outputs and connect them back.
                    for (const grh::Value *res : results)
                    {
                        if (!res)
                        {
                            continue;
                        }
                        const std::string tempName = res->symbol() + "_intm";
                        ensureRegDecl(tempName, res->width(), res->isSigned(), op.srcLoc());
                        addAssign("assign " + res->symbol() + " = " + tempName + ";", &op);
                    }

                    SeqKey key{operands[0], *clkPolarity, nullptr, {}, nullptr, {}};
                    std::ostringstream stmt;
                    appendIndented(stmt, 2, "if (" + operands[1]->symbol() + ") begin");
                    stmt << std::string(kIndentSizeSv * 3, ' ') << *targetImport << "(";
                    bool firstArg = true;
                    bool callOk = true;
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
                            if (idx < 0 || static_cast<std::size_t>(idx + 2) >= operands.size())
                            {
                                reportError("kDpicCall missing matching input arg " + formal, op.symbol());
                                callOk = false;
                                break;
                            }
                            stmt << operands[static_cast<std::size_t>(idx + 2)]->symbol();
                        }
                        else
                        {
                            int idx = findNameIndex(*outArgName, formal);
                            if (idx < 0 || static_cast<std::size_t>(idx) >= results.size())
                            {
                                reportError("kDpicCall missing matching output arg " + formal, op.symbol());
                                callOk = false;
                                break;
                            }
                            stmt << results[static_cast<std::size_t>(idx)]->symbol() << "_intm";
                        }
                    }
                    if (!callOk)
                    {
                        break;
                    }
                    stmt << ");\n";
                    appendIndented(stmt, 2, "end");
                    addSequentialStmt(key, stmt.str(), &op);
                    break;
                }
                default:
                    reportWarning("Unsupported operation for SystemVerilog emission", op.symbol());
                    break;
                }
            }

            // Declare remaining wires for non-port values not defined above.
            for (const auto &valueSymbol : graph->valueOrder())
            {
                const grh::Value &val = graph->getValue(valueSymbol);
                if (val.isInput() || val.isOutput())
                {
                    continue;
                }
                ensureWireDecl(val);
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
                    moduleBuffer << "  " << (decl.dir == PortDir::Input ? "input wire " : (decl.isReg ? "output reg " : "output wire "));
                    moduleBuffer << signedPrefix(decl.isSigned);
                    const std::string range = widthRange(decl.width);
                    if (!range.empty())
                    {
                        moduleBuffer << range << " ";
                    }
                    moduleBuffer << name;
                };

                for (const auto &[name, _] : graph->inputPorts())
                {
                    emitPortLine(name);
                }
                for (const auto &[name, _] : graph->outputPorts())
                {
                    emitPortLine(name);
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
                for (const auto &[decl, opPtr] : memoryDecls)
                {
                    const std::string attr =
                        formatSrcAttribute(opPtr ? opPtr->srcLoc() : std::optional<grh::SrcLoc>{});
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
                        formatSrcAttribute(opPtr ? opPtr->srcLoc() : std::optional<grh::SrcLoc>{});
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
                        formatSrcAttribute(opPtr ? opPtr->srcLoc() : std::optional<grh::SrcLoc>{});
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
                        formatSrcAttribute(opPtr ? opPtr->srcLoc() : std::optional<grh::SrcLoc>{});
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
                        formatSrcAttribute(opPtr ? opPtr->srcLoc() : std::optional<grh::SrcLoc>{});
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
                        formatSrcAttribute(opPtr ? opPtr->srcLoc() : std::optional<grh::SrcLoc>{});
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
                    const std::string sens = sensitivityList(seq.key);
                    if (sens.empty())
                    {
                        reportError("Sequential block missing sensitivity list", graph->symbol());
                        continue;
                    }
                    const std::string attr =
                        formatSrcAttribute(seq.op ? seq.op->srcLoc() : std::optional<grh::SrcLoc>{});
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
        auto opSrcLoc = [&](grh::ir::OperationId opId) -> std::optional<grh::SrcLoc>
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
            if (view.opKind(opId) == grh::OperationKind::kDpicImport)
            {
                const std::string &name = opName(opId);
                if (!name.empty())
                {
                    dpicImports.emplace(name, opId);
                }
            }
        }

        struct IrSeqKey
        {
            grh::ir::ValueId clk = grh::ir::ValueId::invalid();
            std::string clkEdge;
            grh::ir::ValueId asyncRst = grh::ir::ValueId::invalid();
            std::string asyncEdge;
            grh::ir::ValueId syncRst = grh::ir::ValueId::invalid();
            std::string syncPolarity;
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
        inputPorts.reserve(view.inputPorts().size());
        outputPorts.reserve(view.outputPorts().size());

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
                                 const std::optional<grh::SrcLoc> &debug = std::nullopt)
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

        auto addSequentialStmt = [&](const IrSeqKey &key, std::string stmt,
                                     grh::ir::OperationId sourceOp)
        {
            seqBlocks.push_back(IrSeqBlock{key, std::vector<std::string>{std::move(stmt)}, sourceOp});
        };

        auto addLatchBlock = [&](std::string stmt, grh::ir::OperationId sourceOp)
        {
            latchBlocks.emplace_back(std::move(stmt), sourceOp);
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
            auto attr = getAttribute<std::string>(view, symbols, userOp, "memSymbol");
            if (attr)
            {
                return attr;
            }
            std::optional<std::string> candidate;
            for (const auto opId : view.operations())
            {
                if (view.opKind(opId) == grh::OperationKind::kMemory)
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

        auto normalizeLower = [](const std::optional<std::string> &attr) -> std::optional<std::string>
        {
            if (!attr)
            {
                return std::nullopt;
            }
            std::string v = *attr;
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return v;
        };
        auto parsePolarityBool = [&](const std::optional<std::string> &attr,
                                     std::string_view name,
                                     const std::string &context) -> std::optional<bool>
        {
            auto norm = normalizeLower(attr);
            if (!norm)
            {
                reportError(std::string(name) + " missing", context);
                return std::nullopt;
            }
            if (*norm == "high" || *norm == "1'b1")
            {
                return true;
            }
            if (*norm == "low" || *norm == "1'b0")
            {
                return false;
            }
            reportError("Unknown " + std::string(name) + " value: " + *attr, context);
            return std::nullopt;
        };
        auto formatEnableExpr = [&](grh::ir::ValueId enVal,
                                    std::string_view enLevel,
                                    const std::string &context) -> std::optional<std::string>
        {
            const std::string &enName = valueName(enVal);
            if (enName.empty())
            {
                reportError("Enable value missing symbol", context);
                return std::nullopt;
            }
            if (enLevel == "high")
            {
                return enName;
            }
            if (enLevel == "low")
            {
                return "!(" + enName + ")";
            }
            reportError("Unknown enLevel: " + std::string(enLevel), context);
            return std::nullopt;
        };

        // -------------------------
        // Operation traversal
        // -------------------------
        for (const auto opId : view.operations())
        {
            const auto kind = view.opKind(opId);
            const auto operands = view.opOperands(opId);
            const auto results = view.opResults(opId);
            const std::string context = opContext(opId);

            switch (kind)
            {
            case grh::OperationKind::kConstant:
            {
                if (results.empty())
                {
                    reportError("kConstant missing result", context);
                    break;
                }
                auto constValue = getAttribute<std::string>(view, symbols, opId, "constValue");
                if (!constValue)
                {
                    reportError("kConstant missing constValue attribute", context);
                    break;
                }
                addAssign("assign " + valueName(results[0]) + " = " + *constValue + ";", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kAdd:
            case grh::OperationKind::kSub:
            case grh::OperationKind::kMul:
            case grh::OperationKind::kDiv:
            case grh::OperationKind::kMod:
            case grh::OperationKind::kEq:
            case grh::OperationKind::kNe:
            case grh::OperationKind::kLt:
            case grh::OperationKind::kLe:
            case grh::OperationKind::kGt:
            case grh::OperationKind::kGe:
            case grh::OperationKind::kAnd:
            case grh::OperationKind::kOr:
            case grh::OperationKind::kXor:
            case grh::OperationKind::kXnor:
            case grh::OperationKind::kLogicAnd:
            case grh::OperationKind::kLogicOr:
            case grh::OperationKind::kShl:
            case grh::OperationKind::kLShr:
            case grh::OperationKind::kAShr:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("Binary operation missing operands or results", context);
                    break;
                }
                const std::string tok = binOpToken(kind);
                addAssign("assign " + valueName(results[0]) + " = " + valueName(operands[0]) +
                              " " + tok + " " + valueName(operands[1]) + ";",
                          opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kNot:
            case grh::OperationKind::kLogicNot:
            case grh::OperationKind::kReduceAnd:
            case grh::OperationKind::kReduceOr:
            case grh::OperationKind::kReduceXor:
            case grh::OperationKind::kReduceNor:
            case grh::OperationKind::kReduceNand:
            case grh::OperationKind::kReduceXnor:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("Unary operation missing operands or results", context);
                    break;
                }
                const std::string tok = unaryOpToken(kind);
                addAssign("assign " + valueName(results[0]) + " = " + tok + valueName(operands[0]) + ";", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kMux:
            {
                if (operands.size() < 3 || results.empty())
                {
                    reportError("kMux missing operands or results", context);
                    break;
                }
                addAssign("assign " + valueName(results[0]) + " = " + valueName(operands[0]) +
                              " ? " + valueName(operands[1]) + " : " + valueName(operands[2]) + ";",
                          opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kAssign:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kAssign missing operands or results", context);
                    break;
                }
                addAssign("assign " + valueName(results[0]) + " = " + valueName(operands[0]) + ";", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kConcat:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("kConcat missing operands or results", context);
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
                    expr << valueName(operands[i]);
                }
                expr << "};";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kReplicate:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kReplicate missing operands or results", context);
                    break;
                }
                auto rep = getAttribute<int64_t>(view, symbols, opId, "rep");
                if (!rep)
                {
                    reportError("kReplicate missing rep attribute", context);
                    break;
                }
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = {" << *rep << "{" << valueName(operands[0]) << "}};";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kSliceStatic:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("kSliceStatic missing operands or results", context);
                    break;
                }
                auto sliceStart = getAttribute<int64_t>(view, symbols, opId, "sliceStart");
                auto sliceEnd = getAttribute<int64_t>(view, symbols, opId, "sliceEnd");
                if (!sliceStart || !sliceEnd)
                {
                    reportError("kSliceStatic missing sliceStart or sliceEnd", context);
                    break;
                }
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = " << valueName(operands[0]) << "[";
                if (*sliceStart == *sliceEnd)
                {
                    expr << *sliceStart;
                }
                else
                {
                    expr << *sliceEnd << ":" << *sliceStart;
                }
                expr << "];";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kSliceDynamic:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("kSliceDynamic missing operands or results", context);
                    break;
                }
                auto width = getAttribute<int64_t>(view, symbols, opId, "sliceWidth");
                if (!width)
                {
                    reportError("kSliceDynamic missing sliceWidth", context);
                    break;
                }
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = " << valueName(operands[0]) << "["
                     << valueName(operands[1]) << " +: " << *width << "];";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kSliceArray:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("kSliceArray missing operands or results", context);
                    break;
                }
                auto width = getAttribute<int64_t>(view, symbols, opId, "sliceWidth");
                if (!width)
                {
                    reportError("kSliceArray missing sliceWidth", context);
                    break;
                }
                std::ostringstream expr;
                expr << "assign " << valueName(results[0]) << " = " << valueName(operands[0]) << "[";
                if (*width == 1)
                {
                    expr << valueName(operands[1]);
                }
                else
                {
                    expr << valueName(operands[1]) << " * " << *width << " +: " << *width;
                }
                expr << "];";
                addAssign(expr.str(), opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kLatch:
            case grh::OperationKind::kLatchArst:
            {
                if (operands.size() < 2 || results.empty())
                {
                    reportError("Latch operation missing operands or results", context);
                    break;
                }
                const grh::ir::ValueId en = operands[0];
                grh::ir::ValueId rst = grh::ir::ValueId::invalid();
                grh::ir::ValueId resetVal = grh::ir::ValueId::invalid();
                grh::ir::ValueId d = grh::ir::ValueId::invalid();

                if (kind == grh::OperationKind::kLatch)
                {
                    if (operands.size() >= 2)
                    {
                        d = operands[1];
                    }
                }
                else
                {
                    if (operands.size() >= 4)
                    {
                        rst = operands[1];
                        resetVal = operands[2];
                        d = operands[3];
                    }
                }

                if (!en.valid() || !d.valid())
                {
                    reportError("Latch operation missing enable or data operand", context);
                    break;
                }
                if (view.valueWidth(en) != 1)
                {
                    reportError("Latch enable must be 1 bit", context);
                    break;
                }
                if (kind == grh::OperationKind::kLatchArst)
                {
                    if (!rst.valid() || !resetVal.valid())
                    {
                        reportError("kLatchArst missing reset operands", context);
                        break;
                    }
                    if (view.valueWidth(rst) != 1)
                    {
                        reportError("Latch reset must be 1 bit", context);
                        break;
                    }
                }

                auto enLevelAttr = getAttribute<std::string>(view, symbols, opId, "enLevel");
                const std::string enLevel = normalizeLower(enLevelAttr).value_or(std::string("high"));
                auto enExpr = formatEnableExpr(en, enLevel, context);
                if (!enExpr)
                {
                    break;
                }

                std::optional<bool> rstActiveHigh;
                if (kind == grh::OperationKind::kLatchArst)
                {
                    auto rstPolarityAttr = getAttribute<std::string>(view, symbols, opId, "rstPolarity");
                    rstActiveHigh = parsePolarityBool(rstPolarityAttr, "Latch rstPolarity", context);
                    if (!rstActiveHigh)
                    {
                        break;
                    }
                }

                const std::string &regName = opName(opId);
                if (regName.empty())
                {
                    reportError("Latch operation missing symbol", context);
                    break;
                }
                const grh::ir::ValueId q = results[0];
                if (view.valueWidth(q) != view.valueWidth(d))
                {
                    reportError("Latch data/output width mismatch", context);
                    break;
                }
                if (kind == grh::OperationKind::kLatchArst && resetVal.valid() &&
                    view.valueWidth(resetVal) != view.valueWidth(d))
                {
                    reportError("Latch resetValue width mismatch", context);
                    break;
                }
                ensureRegDecl(regName, view.valueWidth(d), view.valueSigned(d), view.opSrcLoc(opId));
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
                const int baseIndent = 2;
                if (kind == grh::OperationKind::kLatchArst)
                {
                    if (!rst.valid() || !resetVal.valid() || !rstActiveHigh)
                    {
                        reportError("Latch with reset missing operands or polarity", context);
                        break;
                    }
                    const std::string rstCond =
                        *rstActiveHigh ? valueName(rst) : "!" + valueName(rst);
                    appendIndented(stmt, baseIndent, "if (" + rstCond + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " = " + valueName(resetVal) + ";");
                    appendIndented(stmt, baseIndent, "end else if (" + *enExpr + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " = " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                }
                else
                {
                    appendIndented(stmt, baseIndent, "if (" + *enExpr + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " = " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                }

                std::ostringstream block;
                block << "  always_latch begin\n";
                block << stmt.str();
                block << "  end";
                addLatchBlock(block.str(), opId);
                break;
            }
            case grh::OperationKind::kRegister:
            case grh::OperationKind::kRegisterEn:
            case grh::OperationKind::kRegisterRst:
            case grh::OperationKind::kRegisterEnRst:
            case grh::OperationKind::kRegisterArst:
            case grh::OperationKind::kRegisterEnArst:
            {
                if (operands.empty() || results.empty())
                {
                    reportError("Register operation missing operands or results", context);
                    break;
                }
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                if (!clkPolarity)
                {
                    reportError("Register operation missing clkPolarity", context);
                    break;
                }

                const std::string &regName = opName(opId);
                if (regName.empty())
                {
                    reportError("Register operation missing symbol", context);
                    break;
                }
                const grh::ir::ValueId q = results[0];
                const grh::ir::ValueId clk = operands[0];
                grh::ir::ValueId rst = grh::ir::ValueId::invalid();
                grh::ir::ValueId en = grh::ir::ValueId::invalid();
                grh::ir::ValueId resetVal = grh::ir::ValueId::invalid();
                grh::ir::ValueId d = grh::ir::ValueId::invalid();

                if (kind == grh::OperationKind::kRegister)
                {
                    if (operands.size() >= 2)
                    {
                        d = operands[1];
                    }
                }
                else if (kind == grh::OperationKind::kRegisterEn)
                {
                    if (operands.size() >= 3)
                    {
                        en = operands[1];
                        d = operands[2];
                    }
                }
                else if (kind == grh::OperationKind::kRegisterRst || kind == grh::OperationKind::kRegisterArst)
                {
                    if (operands.size() >= 4)
                    {
                        rst = operands[1];
                        resetVal = operands[2];
                        d = operands[3];
                    }
                }
                else if (kind == grh::OperationKind::kRegisterEnRst || kind == grh::OperationKind::kRegisterEnArst)
                {
                    if (operands.size() >= 5)
                    {
                        rst = operands[1];
                        en = operands[2];
                        resetVal = operands[3];
                        d = operands[4];
                    }
                }

                if (!d.valid())
                {
                    reportError("Register operation missing data operand", context);
                    break;
                }

                auto rstPolarityAttr = getAttribute<std::string>(view, symbols, opId, "rstPolarity");
                auto enLevelAttr = getAttribute<std::string>(view, symbols, opId, "enLevel");
                const std::string enLevel = normalizeLower(enLevelAttr).value_or(std::string("high"));
                const auto enExpr = en.valid() ? formatEnableExpr(en, enLevel, context) : std::optional<std::string>{};
                std::optional<bool> rstActiveHigh;
                std::string asyncEdge;
                if (kind == grh::OperationKind::kRegisterArst ||
                    kind == grh::OperationKind::kRegisterEnArst)
                {
                    rstActiveHigh = parsePolarityBool(rstPolarityAttr, "Register rstPolarity", context);
                    if (!rstActiveHigh)
                    {
                        break;
                    }
                    asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                }
                else if (kind == grh::OperationKind::kRegisterRst ||
                         kind == grh::OperationKind::kRegisterEnRst)
                {
                    rstActiveHigh = parsePolarityBool(rstPolarityAttr, "Register rstPolarity", context);
                    if (!rstActiveHigh)
                    {
                        break;
                    }
                }

                if (view.valueWidth(q) != view.valueWidth(d))
                {
                    reportError("Register data/output width mismatch", context);
                    break;
                }
                if (resetVal.valid() && view.valueWidth(resetVal) != view.valueWidth(d))
                {
                    reportError("Register resetValue width mismatch", context);
                    break;
                }

                ensureRegDecl(regName, view.valueWidth(d), view.valueSigned(d), view.opSrcLoc(opId));
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

                IrSeqKey key{clk, *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                if (kind == grh::OperationKind::kRegisterArst ||
                    kind == grh::OperationKind::kRegisterEnArst)
                {
                    if (rst.valid() && rstActiveHigh)
                    {
                        key.asyncRst = rst;
                        key.asyncEdge = asyncEdge;
                    }
                }
                else if (kind == grh::OperationKind::kRegisterRst ||
                         kind == grh::OperationKind::kRegisterEnRst)
                {
                    if (rst.valid() && rstActiveHigh)
                    {
                        key.syncRst = rst;
                        key.syncPolarity = *rstActiveHigh ? "high" : "low";
                    }
                }

                std::ostringstream stmt;
                const int baseIndent = 2;
                bool emitted = true;
                switch (kind)
                {
                case grh::OperationKind::kRegister:
                    appendIndented(stmt, baseIndent, regName + " <= " + valueName(d) + ";");
                    break;
                case grh::OperationKind::kRegisterEn:
                    if (!enExpr)
                    {
                        emitted = false;
                        break;
                    }
                    appendIndented(stmt, baseIndent, "if (" + *enExpr + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                    break;
                case grh::OperationKind::kRegisterRst:
                    if (!rst.valid() || !resetVal.valid() || !rstActiveHigh)
                    {
                        reportError("Register reset missing operands or polarity", context);
                        emitted = false;
                        break;
                    }
                    if (*rstActiveHigh)
                    {
                        appendIndented(stmt, baseIndent, "if (" + valueName(rst) + ") begin");
                    }
                    else
                    {
                        appendIndented(stmt, baseIndent, "if (!" + valueName(rst) + ") begin");
                    }
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(resetVal) + ";");
                    appendIndented(stmt, baseIndent, "end else begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                    break;
                case grh::OperationKind::kRegisterEnRst:
                    if (!rst.valid() || !resetVal.valid() || !rstActiveHigh)
                    {
                        reportError("Register reset missing operands or polarity", context);
                        emitted = false;
                        break;
                    }
                    if (!enExpr)
                    {
                        emitted = false;
                        break;
                    }
                    if (*rstActiveHigh)
                    {
                        appendIndented(stmt, baseIndent, "if (" + valueName(rst) + ") begin");
                    }
                    else
                    {
                        appendIndented(stmt, baseIndent, "if (!" + valueName(rst) + ") begin");
                    }
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(resetVal) + ";");
                    appendIndented(stmt, baseIndent, "end else if (" + *enExpr + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                    break;
                case grh::OperationKind::kRegisterArst:
                    if (!rst.valid() || !resetVal.valid() || !rstActiveHigh)
                    {
                        reportError("Register reset missing operands or polarity", context);
                        emitted = false;
                        break;
                    }
                    appendIndented(stmt, baseIndent, "if (" + (*rstActiveHigh ? valueName(rst) : "!" + valueName(rst)) + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(resetVal) + ";");
                    appendIndented(stmt, baseIndent, "end else begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                    break;
                case grh::OperationKind::kRegisterEnArst:
                    if (!rst.valid() || !resetVal.valid() || !rstActiveHigh)
                    {
                        reportError("Register reset missing operands or polarity", context);
                        emitted = false;
                        break;
                    }
                    if (!enExpr)
                    {
                        emitted = false;
                        break;
                    }
                    appendIndented(stmt, baseIndent, "if (" + (*rstActiveHigh ? valueName(rst) : "!" + valueName(rst)) + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(resetVal) + ";");
                    appendIndented(stmt, baseIndent, "end else if (" + *enExpr + ") begin");
                    appendIndented(stmt, baseIndent + 1, regName + " <= " + valueName(d) + ";");
                    appendIndented(stmt, baseIndent, "end");
                    break;
                default:
                    emitted = false;
                    break;
                }

                if (emitted)
                {
                    addSequentialStmt(key, stmt.str(), opId);
                }
                break;
            }
            case grh::OperationKind::kMemory:
            {
                auto widthAttr = getAttribute<int64_t>(view, symbols, opId, "width");
                auto rowAttr = getAttribute<int64_t>(view, symbols, opId, "row");
                auto isSignedAttr = getAttribute<bool>(view, symbols, opId, "isSigned");
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
            case grh::OperationKind::kMemoryAsyncReadPort:
            {
                if (operands.size() < 1 || results.empty())
                {
                    reportError("kMemoryAsyncReadPort missing operands or results", context);
                    break;
                }
                auto memSymbol = getAttribute<std::string>(view, symbols, opId, "memSymbol");
                if (!memSymbol)
                {
                    reportError("kMemoryAsyncReadPort missing memSymbol", context);
                    break;
                }
                addAssign("assign " + valueName(results[0]) + " = " + *memSymbol + "[" + valueName(operands[0]) + "];", opId);
                ensureWireDecl(results[0]);
                break;
            }
            case grh::OperationKind::kMemorySyncReadPort:
            case grh::OperationKind::kMemorySyncReadPortRst:
            case grh::OperationKind::kMemorySyncReadPortArst:
            {
                const bool hasReset = kind == grh::OperationKind::kMemorySyncReadPortRst ||
                                      kind == grh::OperationKind::kMemorySyncReadPortArst;
                const bool asyncReset = kind == grh::OperationKind::kMemorySyncReadPortArst;
                const std::size_t expectedOperands = hasReset ? 4 : 3;
                if (operands.size() < expectedOperands || results.empty())
                {
                    reportError(std::string(grh::toString(kind)) + " missing operands or results", context);
                    break;
                }
                auto memSymbolAttr = resolveMemorySymbol(opId);
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                if (!clkPolarity)
                {
                    reportWarning(std::string(grh::toString(kind)) + " missing clkPolarity, defaulting to posedge", context);
                    clkPolarity = std::string("posedge");
                }
                auto rstPolarityAttr = hasReset ? getAttribute<std::string>(view, symbols, opId, "rstPolarity")
                                                : std::optional<std::string>{};
                auto enLevelAttr = getAttribute<std::string>(view, symbols, opId, "enLevel");
                const std::string enLevel = normalizeLower(enLevelAttr).value_or(std::string("high"));
                const grh::ir::ValueId clk = operands[0];
                const grh::ir::ValueId rst = hasReset ? operands[1] : grh::ir::ValueId::invalid();
                const grh::ir::ValueId addr = operands[hasReset ? 2 : 1];
                const grh::ir::ValueId en = operands[hasReset ? 3 : 2];
                if (!clk.valid() || !addr.valid() || !en.valid() || (hasReset && !rst.valid()))
                {
                    reportError(std::string(grh::toString(kind)) + " missing operands or results", context);
                    break;
                }
                auto enExpr = formatEnableExpr(en, enLevel, context);
                if (!enExpr)
                {
                    break;
                }
                std::optional<bool> rstActiveHigh;
                if (hasReset)
                {
                    rstActiveHigh = parsePolarityBool(rstPolarityAttr, "memory rstPolarity", context);
                    if (!rstActiveHigh)
                    {
                        break;
                    }
                }
                if (!memSymbolAttr)
                {
                    reportError(std::string(grh::toString(kind)) + " missing memSymbol or clkPolarity", context);
                    break;
                }
                const std::string &memSymbol = *memSymbolAttr;

                int64_t memWidth = 1;
                bool memSigned = false;
                if (auto it = opByName.find(memSymbol); it != opByName.end())
                {
                    memWidth = getAttribute<int64_t>(view, symbols, it->second, "width").value_or(1);
                    memSigned = getAttribute<bool>(view, symbols, it->second, "isSigned").value_or(false);
                }
                ensureRegDecl(opName(opId), memWidth, memSigned, view.opSrcLoc(opId));

                IrSeqKey key{clk, *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                if (asyncReset && rst.valid() && rstActiveHigh)
                {
                    key.asyncRst = rst;
                    key.asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                }
                else if (hasReset && rst.valid() && rstActiveHigh)
                {
                    key.syncRst = rst;
                    key.syncPolarity = *rstActiveHigh ? "high" : "low";
                }

                std::ostringstream stmt;
                appendIndented(stmt, 2, "if (" + *enExpr + ") begin");
                appendIndented(stmt, 3, opName(opId) + " <= " + memSymbol + "[" + valueName(addr) + "];");
                appendIndented(stmt, 2, "end");
                addSequentialStmt(key, stmt.str(), opId);

                const grh::ir::ValueId data = results[0];
                if (valueName(data) != opName(opId))
                {
                    ensureWireDecl(data);
                    addAssign("assign " + valueName(data) + " = " + opName(opId) + ";", opId);
                }
                else
                {
                    markPortAsRegIfNeeded(data);
                }
                break;
            }
            case grh::OperationKind::kMemoryWritePort:
            case grh::OperationKind::kMemoryWritePortRst:
            case grh::OperationKind::kMemoryWritePortArst:
            {
                const bool hasReset = kind == grh::OperationKind::kMemoryWritePortRst ||
                                      kind == grh::OperationKind::kMemoryWritePortArst;
                const bool asyncReset = kind == grh::OperationKind::kMemoryWritePortArst;
                const std::size_t expectedOperands = hasReset ? 5 : 4;
                if (operands.size() < expectedOperands)
                {
                    reportError(std::string(grh::toString(kind)) + " missing operands", context);
                    break;
                }
                auto memSymbolAttr = resolveMemorySymbol(opId);
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                if (!clkPolarity)
                {
                    reportWarning(std::string(grh::toString(kind)) + " missing clkPolarity, defaulting to posedge", context);
                    clkPolarity = std::string("posedge");
                }
                auto rstPolarityAttr = hasReset ? getAttribute<std::string>(view, symbols, opId, "rstPolarity")
                                                : std::optional<std::string>{};
                auto enLevelAttr = getAttribute<std::string>(view, symbols, opId, "enLevel");
                const std::string enLevel = normalizeLower(enLevelAttr).value_or(std::string("high"));
                const grh::ir::ValueId clk = operands[0];
                const grh::ir::ValueId rst = hasReset ? operands[1] : grh::ir::ValueId::invalid();
                const grh::ir::ValueId addr = operands[hasReset ? 2 : 1];
                const grh::ir::ValueId en = operands[hasReset ? 3 : 2];
                const grh::ir::ValueId data = operands[hasReset ? 4 : 3];
                if (!clk.valid() || !addr.valid() || !en.valid() || !data.valid() || (hasReset && !rst.valid()))
                {
                    reportError(std::string(grh::toString(kind)) + " missing operands", context);
                    break;
                }
                auto enExpr = formatEnableExpr(en, enLevel, context);
                if (!enExpr)
                {
                    break;
                }
                std::optional<bool> rstActiveHigh;
                if (hasReset)
                {
                    rstActiveHigh = parsePolarityBool(rstPolarityAttr, "memory rstPolarity", context);
                    if (!rstActiveHigh)
                    {
                        break;
                    }
                }
                if (!memSymbolAttr)
                {
                    reportError(std::string(grh::toString(kind)) + " missing memSymbol or clkPolarity", context);
                    break;
                }
                const std::string &memSymbol = *memSymbolAttr;

                IrSeqKey key{clk, *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                if (asyncReset && rst.valid() && rstActiveHigh)
                {
                    key.asyncRst = rst;
                    key.asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                }
                else if (hasReset && rst.valid() && rstActiveHigh)
                {
                    key.syncRst = rst;
                    key.syncPolarity = *rstActiveHigh ? "high" : "low";
                }

                std::ostringstream stmt;
                appendIndented(stmt, 2, "if (" + *enExpr + ") begin");
                appendIndented(stmt, 3, memSymbol + "[" + valueName(addr) + "] <= " + valueName(data) + ";");
                appendIndented(stmt, 2, "end");
                addSequentialStmt(key, stmt.str(), opId);
                break;
            }
            case grh::OperationKind::kMemoryMaskWritePort:
            case grh::OperationKind::kMemoryMaskWritePortRst:
            case grh::OperationKind::kMemoryMaskWritePortArst:
            {
                const bool hasReset = kind == grh::OperationKind::kMemoryMaskWritePortRst ||
                                      kind == grh::OperationKind::kMemoryMaskWritePortArst;
                const bool asyncReset = kind == grh::OperationKind::kMemoryMaskWritePortArst;
                const std::size_t expectedOperands = hasReset ? 6 : 5;
                if (operands.size() < expectedOperands)
                {
                    reportError(std::string(grh::toString(kind)) + " missing operands", context);
                    break;
                }
                auto memSymbolAttr = resolveMemorySymbol(opId);
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                if (!clkPolarity)
                {
                    reportWarning(std::string(grh::toString(kind)) + " missing clkPolarity, defaulting to posedge", context);
                    clkPolarity = std::string("posedge");
                }
                auto rstPolarityAttr = hasReset ? getAttribute<std::string>(view, symbols, opId, "rstPolarity")
                                                : std::optional<std::string>{};
                auto enLevelAttr = getAttribute<std::string>(view, symbols, opId, "enLevel");
                const std::string enLevel = normalizeLower(enLevelAttr).value_or(std::string("high"));
                const grh::ir::ValueId clk = operands[0];
                const grh::ir::ValueId rst = hasReset ? operands[1] : grh::ir::ValueId::invalid();
                const grh::ir::ValueId addr = operands[hasReset ? 2 : 1];
                const grh::ir::ValueId en = operands[hasReset ? 3 : 2];
                const grh::ir::ValueId data = operands[hasReset ? 4 : 3];
                const grh::ir::ValueId mask = operands[hasReset ? 5 : 4];
                if (!clk.valid() || !addr.valid() || !en.valid() || !data.valid() || !mask.valid() ||
                    (hasReset && !rst.valid()))
                {
                    reportError(std::string(grh::toString(kind)) + " missing operands", context);
                    break;
                }
                auto enExpr = formatEnableExpr(en, enLevel, context);
                if (!enExpr)
                {
                    break;
                }
                std::optional<bool> rstActiveHigh;
                if (hasReset)
                {
                    rstActiveHigh = parsePolarityBool(rstPolarityAttr, "memory rstPolarity", context);
                    if (!rstActiveHigh)
                    {
                        break;
                    }
                }
                if (!memSymbolAttr)
                {
                    reportError(std::string(grh::toString(kind)) + " missing memSymbol or clkPolarity", context);
                    break;
                }
                const std::string &memSymbol = *memSymbolAttr;
                int64_t memWidth = 1;
                if (auto it = opByName.find(memSymbol); it != opByName.end())
                {
                    memWidth = getAttribute<int64_t>(view, symbols, it->second, "width").value_or(1);
                }

                IrSeqKey key{clk, *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                if (asyncReset && rst.valid() && rstActiveHigh)
                {
                    key.asyncRst = rst;
                    key.asyncEdge = *rstActiveHigh ? "posedge" : "negedge";
                }
                else if (hasReset && rst.valid() && rstActiveHigh)
                {
                    key.syncRst = rst;
                    key.syncPolarity = *rstActiveHigh ? "high" : "low";
                }

                std::ostringstream stmt;
                appendIndented(stmt, 2, "if (" + *enExpr + ") begin");
                appendIndented(stmt, 3, "if (" + valueName(mask) + " == {" + std::to_string(memWidth) + "{1'b1}}) begin");
                appendIndented(stmt, 4, memSymbol + "[" + valueName(addr) + "] <= " + valueName(data) + ";");
                appendIndented(stmt, 3, "end else begin");
                appendIndented(stmt, 4, "integer i;");
                appendIndented(stmt, 4, "for (i = 0; i < " + std::to_string(memWidth) + "; i = i + 1) begin");
                appendIndented(stmt, 5, "if (" + valueName(mask) + "[i]) begin");
                appendIndented(stmt, 6, memSymbol + "[" + valueName(addr) + "][i] <= " + valueName(data) + "[i];");
                appendIndented(stmt, 5, "end");
                appendIndented(stmt, 4, "end");
                appendIndented(stmt, 3, "end");
                appendIndented(stmt, 2, "end");
                addSequentialStmt(key, stmt.str(), opId);
                break;
            }
            case grh::OperationKind::kInstance:
            case grh::OperationKind::kBlackbox:
            {
                auto moduleNameAttr = getAttribute<std::string>(view, symbols, opId, "moduleName");
                auto inputNames = getAttribute<std::vector<std::string>>(view, symbols, opId, "inputPortName");
                auto outputNames = getAttribute<std::vector<std::string>>(view, symbols, opId, "outputPortName");
                auto instanceNameBase = getAttribute<std::string>(view, symbols, opId, "instanceName")
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
                const std::string &targetModuleName = *moduleNameAttr;

                std::ostringstream decl;
                if (kind == grh::OperationKind::kBlackbox)
                {
                    auto paramNames = getAttribute<std::vector<std::string>>(view, symbols, opId, "parameterNames");
                    auto paramValues = getAttribute<std::vector<std::string>>(view, symbols, opId, "parameterValues");
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
                connections.reserve(inputNames->size() + outputNames->size());
                for (std::size_t i = 0; i < inputNames->size(); ++i)
                {
                    if (i < operands.size())
                    {
                        connections.emplace_back((*inputNames)[i], valueName(operands[i]));
                    }
                }
                for (std::size_t i = 0; i < outputNames->size(); ++i)
                {
                    if (i < results.size())
                    {
                        connections.emplace_back((*outputNames)[i], valueName(results[i]));
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
            case grh::OperationKind::kDisplay:
            {
                if (operands.size() < 2)
                {
                    reportError("kDisplay missing operands", context);
                    break;
                }
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                auto format = getAttribute<std::string>(view, symbols, opId, "format");
                if (!clkPolarity || !format)
                {
                    reportError("kDisplay missing clkPolarity or format", context);
                    break;
                }
                IrSeqKey key{operands[0], *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                std::ostringstream stmt;
                appendIndented(stmt, 2, "if (" + valueName(operands[1]) + ") begin");
                stmt << std::string(kIndentSizeSv * 3, ' ') << "$display(\"" << *format << "\"";
                for (std::size_t i = 2; i < operands.size(); ++i)
                {
                    stmt << ", " << valueName(operands[i]);
                }
                stmt << ");\n";
                appendIndented(stmt, 2, "end");
                addSequentialStmt(key, stmt.str(), opId);
                break;
            }
            case grh::OperationKind::kAssert:
            {
                if (operands.size() < 2)
                {
                    reportError("kAssert missing operands", context);
                    break;
                }
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                if (!clkPolarity)
                {
                    reportError("kAssert missing clkPolarity", context);
                    break;
                }
                auto message = getAttribute<std::string>(view, symbols, opId, "message")
                                   .value_or(std::string("Assertion failed"));
                IrSeqKey key{operands[0], *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                std::ostringstream stmt;
                appendIndented(stmt, 2, "if (!" + valueName(operands[1]) + ") begin");
                appendIndented(stmt, 3, "$fatal(\"" + message + " at time %0t\", $time);");
                appendIndented(stmt, 2, "end");
                addSequentialStmt(key, stmt.str(), opId);
                break;
            }
            case grh::OperationKind::kDpicImport:
            {
                auto argsDir = getAttribute<std::vector<std::string>>(view, symbols, opId, "argsDirection");
                auto argsWidth = getAttribute<std::vector<int64_t>>(view, symbols, opId, "argsWidth");
                auto argsName = getAttribute<std::vector<std::string>>(view, symbols, opId, "argsName");
                if (!argsDir || !argsWidth || !argsName ||
                    argsDir->size() != argsWidth->size() || argsDir->size() != argsName->size())
                {
                    reportError("kDpicImport missing or inconsistent arg metadata", context);
                    break;
                }
                const std::string &funcName = opName(opId);
                if (funcName.empty())
                {
                    reportError("kDpicImport missing symbol", context);
                    break;
                }
                std::ostringstream decl;
                decl << "import \"DPI-C\" function void " << funcName << " (\n";
                for (std::size_t i = 0; i < argsDir->size(); ++i)
                {
                    decl << "  " << (*argsDir)[i] << " logic ";
                    if ((*argsWidth)[i] > 1)
                    {
                        decl << "[" << ((*argsWidth)[i] - 1) << ":0] ";
                    }
                    decl << (*argsName)[i];
                    decl << (i + 1 == argsDir->size() ? "\n" : ",\n");
                }
                decl << ");";
                dpiImportDecls.emplace_back(decl.str(), opId);
                break;
            }
            case grh::OperationKind::kDpicCall:
            {
                if (operands.size() < 2)
                {
                    reportError("kDpicCall missing clk/enable operands", context);
                    break;
                }
                auto clkPolarity = getAttribute<std::string>(view, symbols, opId, "clkPolarity");
                auto targetImport = getAttribute<std::string>(view, symbols, opId, "targetImportSymbol");
                auto inArgName = getAttribute<std::vector<std::string>>(view, symbols, opId, "inArgName");
                auto outArgName = getAttribute<std::vector<std::string>>(view, symbols, opId, "outArgName");
                if (!clkPolarity || !targetImport || !inArgName || !outArgName)
                {
                    reportError("kDpicCall missing metadata", context);
                    break;
                }
                auto itImport = dpicImports.find(*targetImport);
                if (itImport == dpicImports.end())
                {
                    reportError("kDpicCall cannot resolve import symbol", *targetImport);
                    break;
                }
                auto importArgs = getAttribute<std::vector<std::string>>(view, symbols, itImport->second, "argsName");
                auto importDirs = getAttribute<std::vector<std::string>>(view, symbols, itImport->second, "argsDirection");
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

                IrSeqKey key{operands[0], *clkPolarity, grh::ir::ValueId::invalid(), {}, grh::ir::ValueId::invalid(), {}};
                std::ostringstream stmt;
                appendIndented(stmt, 2, "if (" + valueName(operands[1]) + ") begin");
                stmt << std::string(kIndentSizeSv * 3, ' ') << *targetImport << "(";
                bool firstArg = true;
                bool callOk = true;
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
                        if (idx < 0 || static_cast<std::size_t>(idx + 2) >= operands.size())
                        {
                            reportError("kDpicCall missing matching input arg " + formal, context);
                            callOk = false;
                            break;
                        }
                        stmt << valueName(operands[static_cast<std::size_t>(idx + 2)]);
                    }
                    else
                    {
                        int idx = findNameIndex(*outArgName, formal);
                        if (idx < 0 || static_cast<std::size_t>(idx) >= results.size())
                        {
                            reportError("kDpicCall missing matching output arg " + formal, context);
                            callOk = false;
                            break;
                        }
                        stmt << valueName(results[static_cast<std::size_t>(idx)]) << "_intm";
                    }
                }
                if (!callOk)
                {
                    break;
                }
                stmt << ");\n";
                appendIndented(stmt, 2, "end");
                addSequentialStmt(key, stmt.str(), opId);
                break;
            }
            default:
                reportWarning("Unsupported operation for SystemVerilog emission", context);
                break;
            }
        }

        // Declare remaining wires for non-port values not defined above.
        for (const auto valueId : view.values())
        {
            if (view.valueIsInput(valueId) || view.valueIsOutput(valueId))
            {
                continue;
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
                moduleBuffer << "  " << (decl.dir == PortDir::Input ? "input wire " : (decl.isReg ? "output reg " : "output wire "));
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
            if (!key.clk.valid() || key.clkEdge.empty())
            {
                return {};
            }
            std::string out = "@(" + key.clkEdge + " " + valueName(key.clk);
            if (key.asyncRst.valid() && !key.asyncEdge.empty())
            {
                out.append(" or ");
                out.append(key.asyncEdge);
                out.push_back(' ');
                out.append(valueName(key.asyncRst));
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

} // namespace wolf_sv::emit
