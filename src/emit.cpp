#include "emit.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

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
            for (const auto &graphPtr : netlist.graphs())
            {
                graphs.push_back(graphPtr.get());
            }
            std::sort(graphs.begin(), graphs.end(), [](const grh::Graph *lhs, const grh::Graph *rhs)
                      { return lhs->name() < rhs->name(); });
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
                writer.writeValue(graph->name());
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
                                           { appendQuotedString(out, user.operation ? user.operation->symbol() : ""); });
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
                                                  prop("k", [&]
                                                       { appendQuotedString(out, "bool"); });
                                                  prop("v", [&]
                                                       { out.append(v ? "true" : "false"); });
                                              },
                                              [&](int64_t v)
                                              {
                                                  prop("k", [&]
                                                       { appendQuotedString(out, "int"); });
                                                  prop("v", [&]
                                                       { out.append(std::to_string(v)); });
                                              },
                                              [&](double v)
                                              {
                                                  prop("k", [&]
                                                       { appendQuotedString(out, "double"); });
                                                  prop("v", [&]
                                                       { appendDouble(out, v); });
                                              },
                                              [&](const std::string &v)
                                              {
                                                  prop("k", [&]
                                                       { appendQuotedString(out, "string"); });
                                                  prop("v", [&]
                                                       { appendQuotedString(out, v); });
                                              },
                                              [&](const std::vector<bool> &arr)
                                              {
                                                  prop("k", [&]
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
                                                  prop("k", [&]
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
                                                  prop("k", [&]
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
                                                  prop("k", [&]
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
                              });
        }

        void writePortInline(std::string &out, std::string_view name, const grh::Value &value, JsonPrintMode mode)
        {
            writeInlineObject(out, mode, [&](auto &&prop)
                              {
                                  prop("name", [&]
                                       { appendQuotedString(out, name); });
                                  prop("val", [&]
                                       { appendQuotedString(out, value.symbol()); });
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
                                           for (const grh::Value *operand : op.operands())
                                           {
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, operand->symbol());
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  prop("out", [&]
                                       {
                                           out.push_back('[');
                                           bool first = true;
                                           const char *comma = commaToken(mode);
                                           for (const grh::Value *result : op.results())
                                           {
                                               if (!first)
                                               {
                                                   out.append(comma);
                                               }
                                               appendQuotedString(out, result->symbol());
                                               first = false;
                                           }
                                           out.push_back(']');
                                       });
                                  if (!op.attributes().empty())
                                  {
                                      prop("attrs", [&]
                                           { writeAttrsInline(out, op.attributes(), mode); });
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
                                     const std::map<std::string, grh::Value *> &ports,
                                     JsonPrintMode mode,
                                     int indent)
        {
            (void)mode;
            out.push_back('[');
            bool first = true;
            for (const auto &[name, value] : ports)
            {
                if (!first)
                {
                    out.push_back(',');
                }
                appendNewlineAndIndent(out, indent);
                writePortInline(out, name, *value, mode);
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
            appendQuotedString(out, "name");
            out.append(": ");
            appendQuotedString(out, graph.name());
            out.push_back(',');

            appendNewlineAndIndent(out, indent);
            appendQuotedString(out, "vals");
            out.append(": ");
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, graph.values(),
                             [&](const std::unique_ptr<grh::Value> &value)
                             { writeValueInline(out, *value, JsonPrintMode::PrettyCompact); });
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
            writeInlineArray(out, JsonPrintMode::PrettyCompact, indent + 1, graph.operations(),
                             [&](const std::unique_ptr<grh::Operation> &op)
                             { writeOperationInline(out, *op, JsonPrintMode::PrettyCompact); });

            appendNewlineAndIndent(out, baseIndent);
            out.push_back('}');
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
                    appendQuotedString(out, graph->name());
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

            seen.insert(std::string(graph->name()));
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

        const std::filesystem::path outputPath = resolveOutputDir(options) / "grh.json";
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

} // namespace wolf_sv::emit
