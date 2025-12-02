#include "emit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
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
                                           { appendQuotedString(out, user.operation); });
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
        };

        struct PortDecl
        {
            PortDir dir = PortDir::Input;
            int64_t width = 1;
            bool isSigned = false;
            bool isReg = false;
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
            std::string out(type);
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

        std::ostringstream moduleBuffer;
        bool firstModule = true;
        for (const grh::Graph *graph : graphsSortedByName(netlist))
        {
            if (!firstModule)
            {
                moduleBuffer << '\n';
            }
            firstModule = false;

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
                portDecls[name] = PortDecl{PortDir::Input, value->width(), value->isSigned(), false};
            }
            for (const auto &[name, valueSymbol] : graph->outputPorts())
            {
                const grh::Value *value = graph->findValue(valueSymbol);
                if (!value)
                {
                    continue;
                }
                portDecls[name] = PortDecl{PortDir::Output, value->width(), value->isSigned(), false};
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
            std::vector<std::string> memoryDecls;
            std::vector<std::string> instanceDecls;
            std::vector<std::string> dpiImportDecls;
            std::vector<std::string> assignStmts;
            std::vector<std::string> latchBlocks;
            std::vector<std::pair<SeqKey, std::vector<std::string>>> seqBlocks;

            auto ensureRegDecl = [&](const std::string &name, int64_t width, bool isSigned)
            {
                if (declaredNames.find(name) != declaredNames.end())
                {
                    return;
                }
                regDecls.emplace(name, NetDecl{width, isSigned});
                declaredNames.insert(name);
            };

            auto ensureWireDecl = [&](const grh::Value &value)
            {
                const std::string &name = value.symbol();
                if (declaredNames.find(name) != declaredNames.end())
                {
                    return;
                }
                wireDecls.emplace(name, NetDecl{value.width(), value.isSigned()});
                declaredNames.insert(name);
            };

            auto addAssign = [&](std::string stmt)
            {
                assignStmts.push_back(std::move(stmt));
            };

            auto addSequentialStmt = [&](const SeqKey &key, std::string stmt)
            {
                seqBlocks.emplace_back(key, std::vector<std::string>{std::move(stmt)});
            };

            auto addLatchBlock = [&](std::string stmt)
            {
                latchBlocks.push_back(std::move(stmt));
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
                    addAssign("assign " + results[0]->symbol() + " = " + *constValue + ";");
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
                    addAssign("assign " + results[0]->symbol() + " = " + operands[0]->symbol() + " " + tok + " " + operands[1]->symbol() + ";");
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
                    addAssign("assign " + results[0]->symbol() + " = " + tok + operands[0]->symbol() + ";");
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
                    addAssign("assign " + results[0]->symbol() + " = " + operands[0]->symbol() + " ? " + operands[1]->symbol() + " : " + operands[2]->symbol() + ";");
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
                    addAssign("assign " + results[0]->symbol() + " = " + operands[0]->symbol() + ";");
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
                    addAssign(expr.str());
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
                    addAssign(expr.str());
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
                    addAssign(expr.str());
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
                    addAssign(expr.str());
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
                    addAssign(expr.str());
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
                    ensureRegDecl(regName, d->width(), d->isSigned());
                    const bool directDrive = q->symbol() == regName;
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(*q);
                    }
                    else
                    {
                        ensureWireDecl(*q);
                        addAssign("assign " + q->symbol() + " = " + regName + ";");
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
                    addLatchBlock(block.str());
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
                    ensureRegDecl(regName, d->width(), d->isSigned());
                    if (directDrive)
                    {
                        markPortAsRegIfNeeded(*q);
                    }
                    else
                    {
                        ensureWireDecl(*q);
                        addAssign("assign " + q->symbol() + " = " + regName + ";");
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
                        addSequentialStmt(key, stmt.str());
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
                    memoryDecls.push_back(decl.str());
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
                    addAssign("assign " + results[0]->symbol() + " = " + *memSymbol + "[" + operands[0]->symbol() + "];");
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
                    ensureRegDecl(op.symbol(), memWidth, memSigned);

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
                    addSequentialStmt(key, stmt.str());

                    const grh::Value *data = results[0];
                    if (data->symbol() != op.symbol())
                    {
                        ensureWireDecl(*data);
                        addAssign("assign " + data->symbol() + " = " + op.symbol() + ";");
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
                    addSequentialStmt(key, stmt.str());
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
                    addSequentialStmt(key, stmt.str());
                    break;
                }
                case grh::OperationKind::kInstance:
                case grh::OperationKind::kBlackbox:
                {
                    auto moduleName = getAttribute<std::string>(op, "moduleName");
                    auto inputNames = getAttribute<std::vector<std::string>>(op, "inputPortName");
                    auto outputNames = getAttribute<std::vector<std::string>>(op, "outputPortName");
                    auto instanceName = getAttribute<std::string>(op, "instanceName").value_or(op.symbol());
                    if (!moduleName || !inputNames || !outputNames)
                    {
                        reportError("Instance missing module or port names", op.symbol());
                        break;
                    }

                    std::ostringstream decl;
                    if (op.kind() == grh::OperationKind::kBlackbox)
                    {
                        auto paramNames = getAttribute<std::vector<std::string>>(op, "parameterNames");
                        auto paramValues = getAttribute<std::vector<std::string>>(op, "parameterValues");
                        if (paramNames && paramValues && !paramNames->empty() && paramNames->size() == paramValues->size())
                        {
                            decl << *moduleName << " #(" << '\n';
                            for (std::size_t i = 0; i < paramNames->size(); ++i)
                            {
                                decl << "    ." << (*paramNames)[i] << "(" << (*paramValues)[i] << ")";
                                decl << (i + 1 == paramNames->size() ? "\n" : ",\n");
                            }
                            decl << ") ";
                        }
                        else
                        {
                            decl << *moduleName << " ";
                        }
                    }
                    else
                    {
                    decl << *moduleName << " ";
                    }
                    decl << instanceName << " (\n";
                    for (std::size_t i = 0; i < inputNames->size(); ++i)
                    {
                        if (i < operands.size())
                        {
                            decl << "    ." << (*inputNames)[i] << "(" << operands[i]->symbol() << "),\n";
                        }
                    }
                    for (std::size_t i = 0; i < outputNames->size(); ++i)
                    {
                        if (i < results.size())
                        {
                            decl << "    ." << (*outputNames)[i] << "(" << results[i]->symbol() << ")";
                            decl << (i + 1 == outputNames->size() ? "\n" : ",\n");
                        }
                    }
                    decl << "  );";
                    instanceDecls.push_back(decl.str());

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
                    addSequentialStmt(key, stmt.str());
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
                    addSequentialStmt(key, stmt.str());
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
                    dpiImportDecls.push_back(decl.str());
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
                        ensureRegDecl(tempName, res->width(), res->isSigned());
                        addAssign("assign " + res->symbol() + " = " + tempName + ";");
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
                    addSequentialStmt(key, stmt.str());
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
            moduleBuffer << "module " << graph->symbol() << " (\n";
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
                    if (!first)
                    {
                        moduleBuffer << ",\n";
                    }
                    first = false;
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
                for (const auto &decl : memoryDecls)
                {
                    moduleBuffer << "  " << decl << '\n';
                }
            }

            if (!instanceDecls.empty())
            {
                moduleBuffer << '\n';
                for (const auto &inst : instanceDecls)
                {
                    moduleBuffer << "  " << inst << '\n';
                }
            }

            if (!dpiImportDecls.empty())
            {
                moduleBuffer << '\n';
                for (const auto &decl : dpiImportDecls)
                {
                    moduleBuffer << "  " << decl << '\n';
                }
            }

            if (!assignStmts.empty())
            {
                moduleBuffer << '\n';
                for (const auto &stmt : assignStmts)
                {
                    moduleBuffer << "  " << stmt << '\n';
                }
            }

            if (!latchBlocks.empty())
            {
                moduleBuffer << '\n';
                for (const auto &block : latchBlocks)
                {
                    moduleBuffer << block << '\n';
                }
            }

            if (!seqBlocks.empty())
            {
                moduleBuffer << '\n';
                for (const auto &[key, stmts] : seqBlocks)
                {
                    const std::string sens = sensitivityList(key);
                    if (sens.empty())
                    {
                        reportError("Sequential block missing sensitivity list", graph->symbol());
                        continue;
                    }
                    moduleBuffer << "  always " << sens << " begin\n";
                    for (const auto &stmt : stmts)
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

} // namespace wolf_sv::emit
