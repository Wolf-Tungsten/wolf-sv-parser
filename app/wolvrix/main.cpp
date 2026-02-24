#include "diagnostics.hpp"
#include "emit.hpp"
#include "ingest.hpp"
#include "load.hpp"
#include "store.hpp"
#include "transform.hpp"
#include "transform/const_fold.hpp"
#include "transform/dead_code_elim.hpp"
#include "transform/demo_stats.hpp"
#include "transform/memory_init_check.hpp"
#include "transform/redundant_elim.hpp"
#include "transform/xmr_resolve.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/text/SourceManager.h"

#include "linenoise.h"

#include <tcl.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <sys/resource.h>

namespace wolvrix::app
{

constexpr int kHistoryMaxLen = 200;

struct Session
{
    bool hasDesign = false;
    wolvrix::lib::grh::Design design;
    std::string selectedGraph;
    std::optional<std::string> lastErrorCode;
    std::optional<std::string> lastErrorMessage;
    std::optional<std::string> lastErrorDetail;
    struct Options {
        wolvrix::lib::LogLevel logLevel = wolvrix::lib::LogLevel::Info;
        bool echoTcl = true;
        bool historyEnabled = true;
        int historyMaxLines = kHistoryMaxLen;
        std::string outputDir;
        std::vector<std::string> diagnosticQuiet;
    } options;
};

struct CommandHelp
{
    std::string_view name;
    std::string_view summary;
    std::string_view detail;
};

const CommandHelp kCommandHelp[] = {
    {
        "help",
        "Show command help",
        "help\n"
        "help <command>\n"
        "\n"
        "Show the command list or detailed help for a specific command.\n",
    },
    {
        "read_sv",
        "Parse SystemVerilog into a GRH design",
        "read_sv <file> ?<slang-opts>...?\n"
        "\n"
        "Parse a SystemVerilog design and build a GRH design in the session. This must\n"
        "be the first load command; if a design is already loaded, run close_design first.\n"
        "\n"
        "Arguments after <file> are passed directly to the slang driver. Common\n"
        "examples: --top <name>, -I/+incdir <dir>, --isystem <dir>, -D/-U <macro>,\n"
        "-G <param>=<value>, -y/-Y for library paths, -f/-F for command files.\n"
        "\n"
        "Example:\n"
        "  read_sv top.sv --top top -I include -D FOO=1\n",
    },
    {
        "read_json",
        "Load a GRH design from JSON",
        "read_json <file>\n"
        "\n"
        "Load a GRH design from JSON. If a design is already loaded, run\n"
        "close_design first.\n",
    },
    {
        "close_design",
        "Close the current GRH design",
        "close_design\n"
        "\n"
        "Release the current GRH design so another read_sv/read_json can be issued.\n",
    },
    {
        "transform",
        "Run a transform pass on the GRH design",
        "transform <passname> ?passargs...?\n"
        "transform -list\n"
        "\n"
        "Run a transform pass on the current GRH design.\n"
        "Use transform_list to see available passes.\n"
        "Add -dryrun to evaluate a pass without mutating the design.\n"
        "\n"
        "Pass notes:\n"
        "  const-fold: -max-iter <N> (default 8), -allow-x\n",
    },
    {
        "transform_list",
        "List available transform passes",
        "transform_list\n"
        "\n"
        "Return a Tcl list of available transform pass names.\n",
    },
    {
        "write_sv",
        "Emit SystemVerilog from the GRH design",
        "write_sv -o <file>\n"
        "\n"
        "Emit SystemVerilog for the current GRH design.\n",
    },
    {
        "write_json",
        "Store the GRH design as JSON",
        "write_json -o <file>\n"
        "\n"
        "Store GRH JSON for the current GRH design.\n",
    },
    {
        "grh_list_graph",
        "List graphs in the GRH design",
        "grh_list_graph\n"
        "\n"
        "Return a Tcl list of graph names in the current GRH design.\n",
    },
    {
        "grh_create_graph",
        "Create an empty GRH graph",
        "grh_create_graph <name>\n"
        "\n"
        "Create an empty graph with the given name in the current GRH design.\n",
    },
    {
        "grh_select_graph",
        "Select a GRH graph by name",
        "grh_select_graph <name>\n"
        "\n"
        "Select a graph by name (or alias) for subsequent commands.\n",
    },
    {
        "grh_delete_graph",
        "Delete a GRH graph by name",
        "grh_delete_graph <name>\n"
        "\n"
        "Delete a graph by name (or alias) from the current GRH design.\n",
    },
    {
        "grh_show_stats",
        "Show basic GRH design statistics",
        "grh_show_stats\n"
        "\n"
        "Return a Tcl dict with counts for graphs, operations, and values.\n",
    },
    {
        "show_design",
        "Show high-level design information",
        "show_design\n"
        "\n"
        "Return a Tcl dict summarizing whether a design is loaded, graph counts,\n"
        "and selected/top graphs when available.\n",
    },
    {
        "show_modules",
        "List graphs in the current design",
        "show_modules\n"
        "\n"
        "Return a Tcl list of graph names for the current design.\n",
    },
    {
        "show_stats",
        "Show design statistics",
        "show_stats\n"
        "\n"
        "Return a Tcl dict with counts for graphs, operations, and values.\n",
    },
    {
        "set_option",
        "Set a session option",
        "set_option <key> <value>\n"
        "\n"
        "Supported keys:\n"
        "  log.level           trace|debug|info|warn|error|off\n"
        "  echo_tcl            0|1\n"
        "  history.enable      0|1\n"
        "  history.max_lines   integer\n"
        "  output.dir          path\n"
        "  diagnostic.quiet    Tcl list or comma-separated patterns\n",
    },
    {
        "get_option",
        "Get a session option",
        "get_option <key>\n"
        "\n"
        "Return the current value for a supported key.\n",
    },
    {
        "last_error",
        "Show last command error",
        "last_error\n"
        "\n"
        "Return a Tcl dict with fields: code, message, detail.\n",
    },
};

constexpr std::string_view kWolvrixPrefix = "wolvrix";
constexpr std::string_view kWolvrixEchoPrompt = "wolvrix> ";
constexpr std::string_view kWolvrixContinuationPrompt = "... ";
std::optional<std::size_t> gWelcomeBoxWidth{};
constexpr std::size_t kBoxHorizPadding = 2;
constexpr std::size_t kBoxVertPadding = 1;
bool gPrintedCommandEcho = false;
bool gLogJustPrinted = false;
Session *gActiveSession = nullptr;
constexpr std::string_view kBoxTopLeft = "╔";
constexpr std::string_view kBoxTopRight = "╗";
constexpr std::string_view kBoxBottomLeft = "╚";
constexpr std::string_view kBoxBottomRight = "╝";
constexpr std::string_view kBoxHorizontal = "═";
constexpr std::string_view kBoxVertical = "║";

void appendUtf8Repeat(std::string &out, std::string_view unit, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        out.append(unit);
    }
}

#ifndef WOLVRIX_VERSION
#define WOLVRIX_VERSION "unknown"
#endif

#ifndef WOLVRIX_GIT_SHA
#define WOLVRIX_GIT_SHA "unknown"
#endif

#ifndef WOLVRIX_GIT_DATE
#define WOLVRIX_GIT_DATE "unknown"
#endif

std::string formatTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&nowTime, &localTime);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return std::string(buffer);
}

std::string_view logLevelText(wolvrix::lib::LogLevel level)
{
    switch (level)
    {
    case wolvrix::lib::LogLevel::Trace:
        return "TRACE";
    case wolvrix::lib::LogLevel::Debug:
        return "DEBUG";
    case wolvrix::lib::LogLevel::Info:
        return "INFO";
    case wolvrix::lib::LogLevel::Warn:
        return "WARN";
    case wolvrix::lib::LogLevel::Error:
        return "ERROR";
    case wolvrix::lib::LogLevel::Off:
    default:
        return "OFF";
    }
}

std::string_view logLevelKeyText(wolvrix::lib::LogLevel level)
{
    switch (level)
    {
    case wolvrix::lib::LogLevel::Trace:
        return "trace";
    case wolvrix::lib::LogLevel::Debug:
        return "debug";
    case wolvrix::lib::LogLevel::Info:
        return "info";
    case wolvrix::lib::LogLevel::Warn:
        return "warn";
    case wolvrix::lib::LogLevel::Error:
        return "error";
    case wolvrix::lib::LogLevel::Off:
    default:
        return "off";
    }
}

std::optional<wolvrix::lib::LogLevel> parseLogLevel(std::string_view text)
{
    std::string lowered(text);
    for (char &ch : lowered)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lowered == "trace")
    {
        return wolvrix::lib::LogLevel::Trace;
    }
    if (lowered == "debug")
    {
        return wolvrix::lib::LogLevel::Debug;
    }
    if (lowered == "info")
    {
        return wolvrix::lib::LogLevel::Info;
    }
    if (lowered == "warn" || lowered == "warning")
    {
        return wolvrix::lib::LogLevel::Warn;
    }
    if (lowered == "error")
    {
        return wolvrix::lib::LogLevel::Error;
    }
    if (lowered == "off" || lowered == "none")
    {
        return wolvrix::lib::LogLevel::Off;
    }
    return std::nullopt;
}

std::string indentMessage(std::string_view message)
{
    std::string output;
    output.reserve(message.size() + 2);
    output.append("  ");
    for (char ch : message)
    {
        output.push_back(ch);
        if (ch == '\n')
        {
            output.append("  ");
        }
    }
    return output;
}

bool shouldLog(wolvrix::lib::LogLevel level)
{
    if (!gActiveSession)
    {
        return true;
    }
    const auto minLevel = gActiveSession->options.logLevel;
    if (minLevel == wolvrix::lib::LogLevel::Off)
    {
        return false;
    }
    return static_cast<int>(level) >= static_cast<int>(minLevel);
}

void logLine(std::string_view entity, wolvrix::lib::LogLevel level, std::string_view message)
{
    if (!shouldLog(level))
    {
        return;
    }
    std::cerr << '\n';
    std::cerr << entity << " " << formatTimestamp() << " " << logLevelText(level) << ":\n";
    std::cerr << indentMessage(message) << '\n';
    gLogJustPrinted = true;
}

wolvrix::lib::LogLevel diagnosticKindToLogLevel(wolvrix::lib::diag::DiagnosticKind kind)
{
    switch (kind)
    {
    case wolvrix::lib::diag::DiagnosticKind::Error:
        return wolvrix::lib::LogLevel::Error;
    case wolvrix::lib::diag::DiagnosticKind::Warning:
        return wolvrix::lib::LogLevel::Warn;
    case wolvrix::lib::diag::DiagnosticKind::Debug:
        return wolvrix::lib::LogLevel::Debug;
    case wolvrix::lib::diag::DiagnosticKind::Info:
    case wolvrix::lib::diag::DiagnosticKind::Todo:
    default:
        return wolvrix::lib::LogLevel::Info;
    }
}

int logLevelRank(wolvrix::lib::LogLevel level)
{
    switch (level)
    {
    case wolvrix::lib::LogLevel::Trace:
        return 0;
    case wolvrix::lib::LogLevel::Debug:
        return 1;
    case wolvrix::lib::LogLevel::Info:
        return 2;
    case wolvrix::lib::LogLevel::Warn:
        return 3;
    case wolvrix::lib::LogLevel::Error:
        return 4;
    case wolvrix::lib::LogLevel::Off:
    default:
        return 5;
    }
}

std::string formatDiagnosticMessage(const wolvrix::lib::diag::Diagnostic &message,
                                    const slang::SourceManager *sourceManager)
{
    std::string locationText;
    std::string statementSnippet;
    if (sourceManager && message.location && message.location->valid())
    {
        const auto loc = sourceManager->getFullyOriginalLoc(*message.location);
        if (loc.valid() && sourceManager->isFileLoc(loc))
        {
            const auto fileName = sourceManager->getFileName(loc);
            locationText.append(fileName.data(), fileName.size());
            locationText.append(":");
            locationText.append(std::to_string(sourceManager->getLineNumber(loc)));
            locationText.append(":");
            locationText.append(std::to_string(sourceManager->getColumnNumber(loc)));

            const std::string_view sourceText = sourceManager->getSourceText(loc.buffer());
            if (!sourceText.empty())
            {
                auto extractLine = [](std::string_view text, size_t offset) -> std::string_view {
                    if (offset > text.size())
                    {
                        return {};
                    }
                    size_t lineStart = text.rfind('\n', offset);
                    if (lineStart == std::string_view::npos)
                    {
                        lineStart = 0;
                    }
                    else
                    {
                        lineStart += 1;
                    }
                    size_t lineEnd = text.find('\n', offset);
                    if (lineEnd == std::string_view::npos)
                    {
                        lineEnd = text.size();
                    }
                    return text.substr(lineStart, lineEnd - lineStart);
                };
                auto trimLine = [](std::string_view line) -> std::string {
                    size_t start = 0;
                    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
                    {
                        start++;
                    }
                    size_t end = line.size();
                    while (end > start && std::isspace(static_cast<unsigned char>(line[end - 1])))
                    {
                        end--;
                    }
                    return std::string(line.substr(start, end - start));
                };
                auto shortenLine = [](const std::string &line, size_t maxLen) -> std::string {
                    if (line.size() <= maxLen)
                    {
                        return line;
                    }
                    std::string clipped = line.substr(0, maxLen);
                    clipped.append("...");
                    return clipped;
                };

                const std::string_view lineText = extractLine(sourceText, loc.offset());
                if (!lineText.empty())
                {
                    statementSnippet = shortenLine(trimLine(lineText), 200);
                }
            }
        }
    }

    std::string text;
    if (!locationText.empty())
    {
        text.append(locationText);
        text.append(" ");
    }
    if (!message.passName.empty())
    {
        text.append(message.passName);
        text.append(": ");
    }
    text.append(message.message);
    if (!message.context.empty())
    {
        text.append(" (");
        text.append(message.context);
        text.append(")");
    }
    if (!message.originSymbol.empty())
    {
        text.append(" origin=");
        text.append(message.originSymbol);
    }
    if (!statementSnippet.empty())
    {
        text.append("\nstatement: ");
        text.append(statementSnippet);
    }
    return text;
}

bool shouldSuppressDiagnostic(const wolvrix::lib::diag::Diagnostic &message)
{
    if (message.kind != wolvrix::lib::diag::DiagnosticKind::Warning)
    {
        return false;
    }
    const std::string_view suppressList[] = {
        "Ignoring timing control (event)",
    };
    for (std::string_view pattern : suppressList)
    {
        if (message.message.find(pattern) != std::string::npos)
        {
            return true;
        }
    }
    if (gActiveSession)
    {
        for (const auto &pattern : gActiveSession->options.diagnosticQuiet)
        {
            if (!pattern.empty() && message.message.find(pattern) != std::string::npos)
            {
                return true;
            }
        }
    }
    return false;
}

std::string formatDuration(std::chrono::milliseconds duration)
{
    if (duration.count() < 1000)
    {
        return std::to_string(duration.count()) + "ms";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << (static_cast<double>(duration.count()) / 1000.0) << "s";
    return oss.str();
}

std::string formatMemoryKb(std::optional<long> kb)
{
    if (!kb || *kb <= 0)
    {
        return "unknown";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(*kb) / 1024.0) << "MB";
    return oss.str();
}

std::string formatBytes(std::uintmax_t bytes)
{
    struct Unit
    {
        const char *label;
        double scale;
    };
    const Unit units[] = {
        {"B", 1.0},
        {"KB", 1024.0},
        {"MB", 1024.0 * 1024.0},
        {"GB", 1024.0 * 1024.0 * 1024.0},
    };

    const Unit *unit = &units[0];
    for (const auto &entry : units)
    {
        if (bytes < static_cast<std::uintmax_t>(entry.scale * 1024.0))
        {
            unit = &entry;
            break;
        }
        unit = &entry;
    }

    double value = static_cast<double>(bytes) / unit->scale;
    std::ostringstream oss;
    if (value < 10.0)
    {
        oss << std::fixed << std::setprecision(2);
    }
    else if (value < 100.0)
    {
        oss << std::fixed << std::setprecision(1);
    }
    else
    {
        oss << std::fixed << std::setprecision(0);
    }
    oss << value << " " << unit->label;
    return oss.str();
}

std::size_t utf8DisplayWidth(std::string_view text)
{
    std::size_t width = 0;
    for (unsigned char ch : text)
    {
        if ((ch & 0xC0) != 0x80)
        {
            ++width;
        }
    }
    return width;
}

std::size_t calcContentWidth(const std::vector<std::string> &lines)
{
    std::size_t maxLen = 0;
    for (const auto &line : lines)
    {
        maxLen = std::max(maxLen, utf8DisplayWidth(line));
    }
    return maxLen;
}

void printBoxed(const std::vector<std::string> &lines, std::optional<std::size_t> widthOverride = {})
{
    const std::size_t contentWidth = calcContentWidth(lines);
    const std::size_t desiredWidth = contentWidth + kBoxHorizPadding * 2 + 2;
    const std::size_t width = widthOverride ? std::max(*widthOverride, desiredWidth) : desiredWidth;
    const std::size_t innerWidth = width - 2;
    const std::size_t boxContentWidth =
        innerWidth > kBoxHorizPadding * 2 ? innerWidth - kBoxHorizPadding * 2 : 0;

    std::string border;
    border.reserve(width * kBoxHorizontal.size());
    border.append(kBoxTopLeft);
    appendUtf8Repeat(border, kBoxHorizontal, width - 2);
    border.append(kBoxTopRight);
    std::cerr << border << '\n';

    for (std::size_t i = 0; i < kBoxVertPadding; ++i)
    {
        std::string row;
        row.reserve(width * kBoxHorizontal.size());
        row.append(kBoxVertical);
        row.append(innerWidth, ' ');
        row.append(kBoxVertical);
        std::cerr << row << '\n';
    }

    for (const auto &line : lines)
    {
        const std::size_t lineWidth = utf8DisplayWidth(line);
        std::size_t padLeft = 0;
        std::size_t padRight = 0;
        if (boxContentWidth > lineWidth)
        {
            padLeft = (boxContentWidth - lineWidth) / 2;
            padRight = boxContentWidth - lineWidth - padLeft;
        }
        std::string row;
        row.reserve(width * kBoxHorizontal.size());
        row.append(kBoxVertical);
        row.append(kBoxHorizPadding, ' ');
        row.append(padLeft, ' ');
        row.append(line);
        row.append(padRight, ' ');
        if (kBoxHorizPadding > 0)
        {
            row.append(kBoxHorizPadding, ' ');
        }
        row.append(kBoxVertical);
        std::cerr << row << '\n';
    }

    for (std::size_t i = 0; i < kBoxVertPadding; ++i)
    {
        std::string row;
        row.reserve(width * kBoxHorizontal.size());
        row.append(kBoxVertical);
        row.append(innerWidth, ' ');
        row.append(kBoxVertical);
        std::cerr << row << '\n';
    }

    std::string bottom;
    bottom.reserve(width * kBoxHorizontal.size());
    bottom.append(kBoxBottomLeft);
    appendUtf8Repeat(bottom, kBoxHorizontal, width - 2);
    bottom.append(kBoxBottomRight);
    std::cerr << bottom << '\n';
    gLogJustPrinted = true;
}

void printWelcome()
{
    std::vector<std::string> lines;
    const std::vector<std::string> asciiLines = {
        "██╗    ██╗ ██████╗ ██╗    ██╗   ██╗██████╗ ██╗██╗  ██╗",
        "██║    ██║██╔═══██╗██║    ██║   ██║██╔══██╗██║╚██╗██╔╝",
        "██║ █╗ ██║██║   ██║██║    ██║   ██║██████╔╝██║ ╚███╔╝",
        "██║███╗██║██║   ██║██║    ╚██╗ ██╔╝██╔══██╗██║ ██╔██╗",
        "╚███╔███╔╝╚██████╔╝███████╗╚████╔╝ ██║  ██║██║██╔╝ ██╗",
        " ╚══╝╚══╝  ╚═════╝ ╚══════╝ ╚═══╝  ╚═╝  ╚═╝╚═╝╚═╝  ╚═╝",
    };
    std::string versionLine = std::string("version: ") + WOLVRIX_VERSION;
    std::string commitLine = std::string("commit: ") + WOLVRIX_GIT_SHA +
                             " (" + WOLVRIX_GIT_DATE + ")";
    std::size_t asciiWidth = 0;
    for (const auto &line : asciiLines)
    {
        asciiWidth = std::max(asciiWidth, utf8DisplayWidth(line));
    }

    lines.insert(lines.end(), asciiLines.begin(), asciiLines.end());
    lines.emplace_back("");
    lines.emplace_back(versionLine);
    lines.emplace_back(commitLine);
    gWelcomeBoxWidth = asciiWidth + kBoxHorizPadding * 2 + 2;
    printBoxed(lines, gWelcomeBoxWidth);
}

void printGoodbye(std::chrono::milliseconds elapsed, std::optional<long> maxRssKb)
{
    std::vector<std::string> lines;
    lines.emplace_back("Wolvrix Goodbye!");
    lines.emplace_back(std::string("elapsed: ") + formatDuration(elapsed) +
                       "  max rss: " + formatMemoryKb(maxRssKb));
    std::cerr << '\n';
    printBoxed(lines, gWelcomeBoxWidth);
}

void printDiagnostics(std::string_view prefix,
                      const std::vector<wolvrix::lib::diag::Diagnostic> &messages,
                      const slang::SourceManager *sourceManager = nullptr)
{
    std::size_t visibleCount = 0;
    for (const auto &message : messages)
    {
        if (!shouldSuppressDiagnostic(message))
        {
            ++visibleCount;
        }
    }
    if (visibleCount == 0)
    {
        return;
    }
    std::string entity(prefix);

    wolvrix::lib::LogLevel mergedLevel = wolvrix::lib::LogLevel::Info;
    for (const auto &message : messages)
    {
        if (shouldSuppressDiagnostic(message))
        {
            continue;
        }
        const auto level = diagnosticKindToLogLevel(message.kind);
        if (logLevelRank(level) > logLevelRank(mergedLevel))
        {
            mergedLevel = level;
        }
    }

    std::string text;
    for (const auto &message : messages)
    {
        if (shouldSuppressDiagnostic(message))
        {
            continue;
        }
        if (!text.empty())
        {
            text.append("\n");
        }
        text.append(logLevelText(diagnosticKindToLogLevel(message.kind)));
        text.append(" ");
        text.append(formatDiagnosticMessage(message, sourceManager));
    }
    logLine(entity, mergedLevel, text);
}

void printDiagnosticsExpanded(std::string_view entity,
                              const std::vector<wolvrix::lib::diag::Diagnostic> &messages,
                              const slang::SourceManager *sourceManager)
{
    std::vector<const wolvrix::lib::diag::Diagnostic *> ordered;
    ordered.reserve(messages.size());
    for (const auto &message : messages)
    {
        if (shouldSuppressDiagnostic(message))
        {
            continue;
        }
        ordered.push_back(&message);
    }
    if (ordered.empty())
    {
        return;
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const wolvrix::lib::diag::Diagnostic *lhs,
                        const wolvrix::lib::diag::Diagnostic *rhs) {
                         const auto lhsLevel = diagnosticKindToLogLevel(lhs->kind);
                         const auto rhsLevel = diagnosticKindToLogLevel(rhs->kind);
                         return logLevelRank(lhsLevel) < logLevelRank(rhsLevel);
                     });

    for (const auto *message : ordered)
    {
        logLine(entity,
                diagnosticKindToLogLevel(message->kind),
                formatDiagnosticMessage(*message, sourceManager));
    }
}

std::string_view trimWhitespace(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
    {
        ++start;
    }
    if (start == text.size())
    {
        return {};
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return text.substr(start, end - start);
}

void echoTclCommand(std::string_view command)
{
    if (!gPrintedCommandEcho || gLogJustPrinted)
    {
        std::cerr << '\n';
        gPrintedCommandEcho = true;
    }
    gLogJustPrinted = false;
    std::string output;
    output.reserve(command.size() + 8);
    output.append(kWolvrixEchoPrompt);
    for (char ch : command)
    {
        if (ch == '\n')
        {
            output.push_back('\n');
            output.append("...      ");
        }
        else
        {
            output.push_back(ch);
        }
    }
    std::cerr << output << '\n';
}

bool shouldEchoTclCommand(const Session &session, std::string_view command)
{
    if (!session.options.echoTcl)
    {
        return false;
    }
    if (command.empty())
    {
        return false;
    }
    if (command.front() == '#')
    {
        return false;
    }
    return true;
}

std::string buildPrompt(const Session &session)
{
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    std::string cwdText = ec ? "?" : cwd.string();
    const char *state = session.hasDesign ? "loaded" : "empty";

    std::string prompt;
    prompt.reserve(cwdText.size() + 24);
    prompt.append(kWolvrixPrefix);
    prompt.append("[");
    prompt.append(state);
    prompt.append("] ");
    prompt.append(cwdText);
    prompt.append("> ");
    return prompt;
}

const CommandHelp *findCommandHelp(std::string_view name)
{
    for (const auto &entry : kCommandHelp)
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

void ensureTclLibraryPath()
{
#ifdef WOLVRIX_TCL_LIBRARY_DIR
    if (std::getenv("TCL_LIBRARY") == nullptr)
    {
        setenv("TCL_LIBRARY", WOLVRIX_TCL_LIBRARY_DIR, 0);
    }
#endif
}

std::string normalizePassName(std::string_view name)
{
    std::string normalized(name);
    for (char &ch : normalized)
    {
        if (ch == '_')
        {
            ch = '-';
        }
    }
    return normalized;
}

void setLastError(Session &session, std::string_view code,
                  std::string_view message,
                  std::string_view detail = {})
{
    session.lastErrorCode = std::string(code);
    session.lastErrorMessage = std::string(message);
    if (!detail.empty())
    {
        session.lastErrorDetail = std::string(detail);
    }
    else
    {
        session.lastErrorDetail.reset();
    }
}

int setError(Session &session, Tcl_Interp *interp,
             std::string_view code,
             std::string_view message,
             std::string_view detail = {})
{
    setLastError(session, code, message, detail);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(message.data(), static_cast<int>(message.size())));
    return TCL_ERROR;
}

int setErrorFromInterp(Session &session, Tcl_Interp *interp, std::string_view code)
{
    const char *message = Tcl_GetStringResult(interp);
    setLastError(session, code, message ? message : "");
    return TCL_ERROR;
}

Tcl_Obj *buildLastErrorDict(const Session &session)
{
    Tcl_Obj *dict = Tcl_NewDictObj();
    const char *code = session.lastErrorCode ? session.lastErrorCode->c_str() : "";
    const char *message = session.lastErrorMessage ? session.lastErrorMessage->c_str() : "";
    const char *detail = session.lastErrorDetail ? session.lastErrorDetail->c_str() : "";
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("code", -1), Tcl_NewStringObj(code, -1));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("message", -1), Tcl_NewStringObj(message, -1));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("detail", -1), Tcl_NewStringObj(detail, -1));
    return dict;
}

std::optional<bool> parseBool(std::string_view text)
{
    std::string lowered(text);
    for (char &ch : lowered)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes")
    {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no")
    {
        return false;
    }
    return std::nullopt;
}

std::string trimString(std::string_view text)
{
    std::string_view trimmed = trimWhitespace(text);
    return std::string(trimmed);
}

std::vector<std::string> parseQuietList(Tcl_Interp *interp, Tcl_Obj *obj)
{
    std::vector<std::string> patterns;
    Tcl_Size listCount = 0;
    Tcl_Obj **listObjv = nullptr;
    if (Tcl_ListObjGetElements(interp, obj, &listCount, &listObjv) != TCL_OK)
    {
        return patterns;
    }
    for (Tcl_Size i = 0; i < listCount; ++i)
    {
        std::string item = Tcl_GetString(listObjv[i]);
        if (!item.empty())
        {
            patterns.push_back(std::move(item));
        }
    }
    if (patterns.size() == 1 && patterns[0].find(',') != std::string::npos)
    {
        std::vector<std::string> split;
        std::string current;
        for (char ch : patterns[0])
        {
            if (ch == ',')
            {
                std::string trimmed = trimString(current);
                if (!trimmed.empty())
                {
                    split.push_back(trimmed);
                }
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }
        std::string trimmed = trimString(current);
        if (!trimmed.empty())
        {
            split.push_back(trimmed);
        }
        patterns = std::move(split);
    }
    return patterns;
}

bool ensureDesign(Session &session, Tcl_Interp *interp)
{
    if (!session.hasDesign)
    {
        setError(session, interp, "NO_DESIGN", "no design loaded; run read_sv or read_json first");
        return false;
    }
    return true;
}

void appendTclList(Tcl_Obj *list, const std::vector<std::string> &items)
{
    for (const auto &item : items)
    {
        Tcl_ListObjAppendElement(nullptr, list,
                                 Tcl_NewStringObj(item.data(), static_cast<int>(item.size())));
    }
}

std::vector<std::string> availableTransformPasses()
{
    return {
        "xmr-resolve",
        "const-fold",
        "redundant-elim",
        "memory-init-check",
        "dead-code-elim",
        "stats",
    };
}

Tcl_Obj *buildTransformListObj()
{
    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    appendTclList(list, availableTransformPasses());
    return list;
}

Tcl_Obj *buildGraphListObj(const Session &session)
{
    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    std::vector<std::string> names;
    names.reserve(session.design.graphOrder().size());
    for (const auto &name : session.design.graphOrder())
    {
        names.push_back(name);
    }
    appendTclList(list, names);
    return list;
}

Tcl_Obj *buildStatsDict(const Session &session)
{
    std::size_t graphCount = session.design.graphs().size();
    std::size_t opCount = 0;
    std::size_t valueCount = 0;
    for (const auto &entry : session.design.graphs())
    {
        const auto &graph = *entry.second;
        opCount += graph.operations().size();
        valueCount += graph.values().size();
    }

    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("graphs", -1), Tcl_NewWideIntObj(graphCount));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("operations", -1), Tcl_NewWideIntObj(opCount));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("values", -1), Tcl_NewWideIntObj(valueCount));
    return dict;
}

std::optional<std::string> readFile(std::string_view path, std::string &error)
{
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input)
    {
        error = "open failed: " + std::string(path);
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contents.empty())
    {
        error = "empty file: " + std::string(path);
        return std::nullopt;
    }
    return contents;
}

std::optional<std::filesystem::path> parseOutputPath(Session &session,
                                                     int objc,
                                                     Tcl_Obj *const objv[],
                                                     Tcl_Interp *interp)
{
    std::optional<std::filesystem::path> output;
    for (int i = 1; i < objc; ++i)
    {
        const char *arg = Tcl_GetString(objv[i]);
        if (std::string_view(arg) == "-o")
        {
            if (i + 1 >= objc)
            {
                setError(session, interp, "ARG_ERROR", "-o expects a path");
                return std::nullopt;
            }
            output = std::filesystem::path(Tcl_GetString(objv[i + 1]));
            ++i;
        }
        else
        {
            setError(session, interp, "ARG_ERROR", "unknown option for write_* (expected -o)");
            return std::nullopt;
        }
    }
    if (!output)
    {
        setError(session, interp, "ARG_ERROR", "-o <file> is required");
        return std::nullopt;
    }
    if (output->parent_path().empty() && !session.options.outputDir.empty())
    {
        *output = std::filesystem::path(session.options.outputDir) / output->filename();
    }
    return output;
}

int cmdReadSv(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (session->hasDesign)
    {
        return setError(*session, interp, "DESIGN_EXISTS",
                        "design already loaded; run close_design first");
    }
    if (objc < 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<file> ?<slang-opts>...? ");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(objc));
    args.emplace_back("read_sv");
    for (int i = 1; i < objc; ++i)
    {
        args.emplace_back(Tcl_GetString(objv[i]));
    }
    std::vector<const char *> argv;
    argv.reserve(args.size());
    for (const auto &arg : args)
    {
        argv.push_back(arg.c_str());
    }

    slang::driver::Driver driver;
    driver.addStandardArgs();
    driver.options.singleUnit = true;
    driver.options.compilationFlags.at(slang::ast::CompilationFlags::AllowTopLevelIfacePorts) = true;

    auto reportSlangDiagnostics = [&]() {
        if (driver.diagEngine.getNumErrors() == 0 && driver.diagEngine.getNumWarnings() == 0)
        {
            return;
        }
        logLine("read_sv", wolvrix::lib::LogLevel::Info,
                "slang reportDiagnostics output follows");
        (void)driver.reportDiagnostics(/* quiet */ false);
    };

    if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()))
    {
        reportSlangDiagnostics();
        return setError(*session, interp, "SLANG_ARG_ERROR", "failed to parse slang options");
    }
    if (!driver.processOptions())
    {
        reportSlangDiagnostics();
        return setError(*session, interp, "SLANG_ARG_ERROR", "failed to apply slang options");
    }
    if (!driver.parseAllSources())
    {
        reportSlangDiagnostics();
        return setError(*session, interp, "SLANG_PARSE_ERROR", "failed to parse sources");
    }

    auto compilation = driver.createCompilation();
    driver.runAnalysis(*compilation);
    const auto &allDiagnostics = compilation->getAllDiagnostics();
    bool hasSlangIssues = false;
    bool hasSlangErrors = false;
    for (const auto &diag : allDiagnostics)
    {
        const auto severity = slang::getDefaultSeverity(diag.code);
        if (severity >= slang::DiagnosticSeverity::Warning)
        {
            hasSlangIssues = true;
        }
        if (severity >= slang::DiagnosticSeverity::Error || diag.isError())
        {
            hasSlangErrors = true;
        }
    }
    if (hasSlangIssues)
    {
        logLine("read_sv", wolvrix::lib::LogLevel::Info,
                "slang reportCompilation output follows");
        driver.reportCompilation(*compilation, /* quiet */ false);
        reportSlangDiagnostics();
    }
    if (hasSlangErrors)
    {
        return setError(*session, interp, "SLANG_ERROR", "slang reported errors; see diagnostics");
    }

    const auto &root = compilation->getRoot();
    std::vector<std::string> topNames;
    topNames.reserve(root.topInstances.size());
    for (const auto *instance : root.topInstances)
    {
        if (!instance)
        {
            continue;
        }
        std::string_view defName = instance->getDefinition().name;
        if (!defName.empty())
        {
            topNames.emplace_back(defName);
        }
        else if (!instance->name.empty())
        {
            topNames.emplace_back(instance->name);
        }
    }

    wolvrix::lib::ingest::ConvertOptions convertOptions;
    convertOptions.abortOnError = true;
    convertOptions.enableLogging = session->options.logLevel != wolvrix::lib::LogLevel::Off;
    convertOptions.logLevel = session->options.logLevel;
    wolvrix::lib::ingest::ConvertDriver converter(convertOptions);
    converter.logger().setSink([&](const wolvrix::lib::LogEvent &event) {
        std::string message;
        if (!event.tag.empty())
        {
            message.append(event.tag);
            message.append(": ");
        }
        message.append(event.message);
        logLine("read_sv", event.level, message);
    });

    const auto convertStart = std::chrono::steady_clock::now();
    try
    {
        session->design = converter.convert(root);
    }
    catch (const wolvrix::lib::ingest::ConvertAbort &)
    {
        converter.diagnostics().flushThreadLocal();
        printDiagnosticsExpanded("read_sv",
                                 converter.diagnostics().messages(),
                                 compilation->getSourceManager());
        return setError(*session, interp, "CONVERT_ABORT", "convert aborted; see diagnostics");
    }
    const auto convertEnd = std::chrono::steady_clock::now();
    const auto convertMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(convertEnd - convertStart).count();
    if (converter.diagnostics().hasError())
    {
        printDiagnosticsExpanded("read_sv",
                                 converter.diagnostics().messages(),
                                 compilation->getSourceManager());
        return setError(*session, interp, "CONVERT_ERROR", "convert failed; see diagnostics");
    }
    printDiagnosticsExpanded("read_sv",
                             converter.diagnostics().messages(),
                             compilation->getSourceManager());

    std::string doneMessage = "done tops=";
    if (topNames.empty())
    {
        doneMessage.append("<none>");
    }
    else
    {
        for (std::size_t i = 0; i < topNames.size(); ++i)
        {
            if (i != 0)
            {
                doneMessage.append(",");
            }
            doneMessage.append(topNames[i]);
        }
    }
    doneMessage.append(" ");
    doneMessage.append(std::to_string(convertMs));
    doneMessage.append("ms");
    logLine("read_sv", wolvrix::lib::LogLevel::Info, doneMessage);

    session->hasDesign = true;
    if (!session->design.topGraphs().empty())
    {
        session->selectedGraph = session->design.topGraphs().front();
    }
    else if (!session->design.graphOrder().empty())
    {
        session->selectedGraph = session->design.graphOrder().front();
    }
    else
    {
        session->selectedGraph.clear();
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdHelp(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc == 1)
    {
        std::string output;
        output.append("Commands:\n");
        for (const auto &entry : kCommandHelp)
        {
            output.append("  ");
            output.append(entry.name);
            output.append(" - ");
            output.append(entry.summary);
            output.append("\n");
        }
        output.append("\nUse 'help <command>' for details.\n");
        Tcl_SetObjResult(interp, Tcl_NewStringObj(output.c_str(), -1));
        return TCL_OK;
    }
    if (objc == 2)
    {
        std::string_view name = Tcl_GetString(objv[1]);
        const CommandHelp *entry = findCommandHelp(name);
        if (!entry)
        {
            return setError(*session, interp, "ARG_ERROR", "unknown command: " + std::string(name));
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj(entry->detail.data(),
                                                  static_cast<int>(entry->detail.size())));
        return TCL_OK;
    }
    Tcl_WrongNumArgs(interp, 1, objv, "?command?");
    return setErrorFromInterp(*session, interp, "ARG_ERROR");
}

int cmdReadJson(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (session->hasDesign)
    {
        return setError(*session, interp, "DESIGN_EXISTS",
                        "design already loaded; run close_design first");
    }
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<file>");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    std::string error;
    const char *path = Tcl_GetString(objv[1]);
    auto jsonText = readFile(path, error);
    if (!jsonText)
    {
        return setError(*session, interp, "IO_ERROR", error);
    }

    try
    {
        session->design = wolvrix::lib::grh::Design::fromJsonString(*jsonText);
    }
    catch (const std::exception &ex)
    {
        return setError(*session, interp, "JSON_ERROR", std::string("load json failed: ") + ex.what());
    }

    session->hasDesign = true;
    if (!session->design.topGraphs().empty())
    {
        session->selectedGraph = session->design.topGraphs().front();
    }
    else if (!session->design.graphOrder().empty())
    {
        session->selectedGraph = session->design.graphOrder().front();
    }
    else
    {
        session->selectedGraph.clear();
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdCloseDesign(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    session->design = wolvrix::lib::grh::Design{};
    session->hasDesign = false;
    session->selectedGraph.clear();
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdTransformList(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    Tcl_SetObjResult(interp, buildTransformListObj());
    return TCL_OK;
}

std::unique_ptr<wolvrix::lib::transform::Pass> makePass(std::string_view name,
                                                         std::vector<std::string_view> args,
                                                         std::string &error)
{
    const std::string normalized = normalizePassName(name);
    if (normalized == "xmr-resolve")
    {
        if (!args.empty())
        {
            error = "xmr-resolve does not accept arguments";
            return nullptr;
        }
        return std::make_unique<wolvrix::lib::transform::XmrResolvePass>();
    }
    if (normalized == "const-fold")
    {
        wolvrix::lib::transform::ConstantFoldOptions options;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string_view arg = args[i];
            if (arg == "-max-iter")
            {
                if (i + 1 >= args.size())
                {
                    error = "-max-iter expects a value";
                    return nullptr;
                }
                try
                {
                    options.maxIterations = std::stoi(std::string(args[++i]));
                }
                catch (const std::exception &)
                {
                    error = "invalid -max-iter value";
                    return nullptr;
                }
            }
            else if (arg == "-allow-x")
            {
                options.allowXPropagation = true;
            }
            else
            {
                error = "unknown const-fold option";
                return nullptr;
            }
        }
        return std::make_unique<wolvrix::lib::transform::ConstantFoldPass>(options);
    }
    if (normalized == "redundant-elim")
    {
        if (!args.empty())
        {
            error = "redundant-elim does not accept arguments";
            return nullptr;
        }
        return std::make_unique<wolvrix::lib::transform::RedundantElimPass>();
    }
    if (normalized == "memory-init-check")
    {
        if (!args.empty())
        {
            error = "memory-init-check does not accept arguments";
            return nullptr;
        }
        return std::make_unique<wolvrix::lib::transform::MemoryInitCheckPass>();
    }
    if (normalized == "dead-code-elim")
    {
        if (!args.empty())
        {
            error = "dead-code-elim does not accept arguments";
            return nullptr;
        }
        return std::make_unique<wolvrix::lib::transform::DeadCodeElimPass>();
    }
    if (normalized == "stats")
    {
        if (!args.empty())
        {
            error = "stats does not accept arguments";
            return nullptr;
        }
        return std::make_unique<wolvrix::lib::transform::StatsPass>();
    }

    error = "unknown pass: " + normalized;
    return nullptr;
}

int cmdTransform(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc < 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<passname> ?passargs...? ");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    if (objc == 2 && std::string_view(Tcl_GetString(objv[1])) == "-list")
    {
        Tcl_SetObjResult(interp, buildTransformListObj());
        return TCL_OK;
    }
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }

    bool dryRun = false;
    std::vector<std::string_view> args;
    for (int i = 2; i < objc; ++i)
    {
        std::string_view arg = Tcl_GetString(objv[i]);
        if (arg == "-list")
        {
            return setError(*session, interp, "ARG_ERROR", "-list must be used alone");
        }
        if (arg == "-dryrun")
        {
            dryRun = true;
            continue;
        }
        args.push_back(arg);
    }

    std::string error;
    auto pass = makePass(Tcl_GetString(objv[1]), args, error);
    if (!pass)
    {
        return setError(*session, interp, "ARG_ERROR", error);
    }

    const char *passName = Tcl_GetString(objv[1]);
    const std::string entity = std::string("transform ") + passName;
    wolvrix::lib::transform::PassDiagnostics diagnostics;
    wolvrix::lib::transform::PassManager manager;
    manager.options().logLevel = session->options.logLevel;
    manager.options().logSink = nullptr;
    manager.addPass(std::move(pass));
    const auto transformStart = std::chrono::steady_clock::now();
    wolvrix::lib::transform::PassManagerResult result;
    if (dryRun)
    {
        wolvrix::lib::grh::Design temp = session->design.clone();
        result = manager.run(temp, diagnostics);
    }
    else
    {
        result = manager.run(session->design, diagnostics);
    }
    const auto transformEnd = std::chrono::steady_clock::now();
    const auto transformMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(transformEnd - transformStart).count();
    if (diagnostics.hasError() || !result.success)
    {
        printDiagnostics(entity, diagnostics.messages());
        return setError(*session, interp, "TRANSFORM_ERROR", "transform failed; see diagnostics");
    }
    printDiagnostics(entity, diagnostics.messages());
    std::string doneMessage = "done ";
    doneMessage.append(std::to_string(transformMs));
    doneMessage.append("ms ");
    doneMessage.append(result.changed ? "changed" : "ok");
    if (dryRun)
    {
        doneMessage.append(" (dryrun)");
    }
    logLine(entity, wolvrix::lib::LogLevel::Info, doneMessage);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(result.changed ? "changed" : "ok", -1));
    return TCL_OK;
}

int cmdWriteJson(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }

    auto outputPath = parseOutputPath(*session, objc, objv, interp);
    if (!outputPath)
    {
        return TCL_ERROR;
    }

    const auto storeStart = std::chrono::steady_clock::now();
    wolvrix::lib::store::StoreDiagnostics diagnostics;
    wolvrix::lib::store::StoreJson store(&diagnostics);
    wolvrix::lib::store::StoreOptions options;
    options.jsonMode = wolvrix::lib::store::JsonPrintMode::PrettyCompact;
    options.outputFilename = outputPath->filename().string();
    if (!outputPath->parent_path().empty())
    {
        options.outputDir = outputPath->parent_path().string();
    }

    wolvrix::lib::store::StoreResult result = store.store(session->design, options);
    const auto storeEnd = std::chrono::steady_clock::now();
    const auto storeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(storeEnd - storeStart).count();
    if (diagnostics.hasError() || !result.success)
    {
        return setError(*session, interp, "WRITE_ERROR", "write_json failed; see diagnostics");
    }

    std::string outputFile =
        result.artifacts.empty() ? outputPath->string() : result.artifacts.front();
    std::error_code ec;
    std::uintmax_t fileSize = std::filesystem::file_size(outputFile, ec);
    std::string sizeText = ec ? "unknown" : formatBytes(fileSize);
    std::string message = "done ";
    message.append(std::to_string(storeMs));
    message.append("ms\n");
    message.append("path: ");
    message.append(outputFile);
    message.append("\n");
    message.append("size: ");
    message.append(sizeText);
    logLine("write_json", wolvrix::lib::LogLevel::Info, message);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(outputFile.c_str(), -1));
    return TCL_OK;
}

int cmdWriteSv(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }

    auto outputPath = parseOutputPath(*session, objc, objv, interp);
    if (!outputPath)
    {
        return TCL_ERROR;
    }

    const auto emitStart = std::chrono::steady_clock::now();
    wolvrix::lib::emit::EmitDiagnostics diagnostics;
    wolvrix::lib::emit::EmitSystemVerilog emitter(&diagnostics);
    wolvrix::lib::emit::EmitOptions options;
    options.outputFilename = outputPath->filename().string();
    if (!outputPath->parent_path().empty())
    {
        options.outputDir = outputPath->parent_path().string();
    }

    wolvrix::lib::emit::EmitResult result = emitter.emit(session->design, options);
    const auto emitEnd = std::chrono::steady_clock::now();
    const auto emitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(emitEnd - emitStart).count();
    if (diagnostics.hasError() || !result.success)
    {
        return setError(*session, interp, "WRITE_ERROR", "write_sv failed; see diagnostics");
    }

    std::string outputFile =
        result.artifacts.empty() ? outputPath->string() : result.artifacts.front();
    std::error_code ec;
    std::uintmax_t fileSize = std::filesystem::file_size(outputFile, ec);
    std::string sizeText = ec ? "unknown" : formatBytes(fileSize);
    std::string message = "done ";
    message.append(std::to_string(emitMs));
    message.append("ms\n");
    message.append("path: ");
    message.append(outputFile);
    message.append("\n");
    message.append("size: ");
    message.append(sizeText);
    logLine("write_sv", wolvrix::lib::LogLevel::Info, message);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(outputFile.c_str(), -1));
    return TCL_OK;
}

int cmdGrhListGraph(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    Tcl_SetObjResult(interp, buildGraphListObj(*session));
    return TCL_OK;
}

int cmdGrhCreateGraph(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<name>");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    const char *name = Tcl_GetString(objv[1]);
    try
    {
        session->design.createGraph(name);
    }
    catch (const std::exception &ex)
    {
        return setError(*session, interp, "GRH_ERROR", ex.what());
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdGrhSelectGraph(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<name>");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    const char *name = Tcl_GetString(objv[1]);
    auto *graph = session->design.findGraph(name);
    if (!graph)
    {
        return setError(*session, interp, "NOT_FOUND", "graph not found: " + std::string(name));
    }
    session->selectedGraph = graph->symbol();
    Tcl_SetObjResult(interp, Tcl_NewStringObj(session->selectedGraph.c_str(), -1));
    return TCL_OK;
}

int cmdGrhDeleteGraph(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<name>");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    const char *name = Tcl_GetString(objv[1]);
    auto *graph = session->design.findGraph(name);
    if (!graph)
    {
        return setError(*session, interp, "NOT_FOUND", "graph not found: " + std::string(name));
    }
    const std::string symbol = graph->symbol();
    if (!session->design.deleteGraph(symbol))
    {
        return setError(*session, interp, "GRH_ERROR", "failed to delete graph: " + symbol);
    }
    if (session->selectedGraph == symbol)
    {
        if (!session->design.graphOrder().empty())
        {
            session->selectedGraph = session->design.graphOrder().front();
        }
        else
        {
            session->selectedGraph.clear();
        }
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdGrhShowStats(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    Tcl_SetObjResult(interp, buildStatsDict(*session));
    return TCL_OK;
}

int cmdLastError(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    Tcl_SetObjResult(interp, buildLastErrorDict(*session));
    return TCL_OK;
}

int cmdShowDesign(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("loaded", -1),
                   Tcl_NewBooleanObj(session->hasDesign ? 1 : 0));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("graphs", -1),
                   Tcl_NewWideIntObj(session->hasDesign ? session->design.graphs().size() : 0));
    Tcl_Obj *tops = Tcl_NewListObj(0, nullptr);
    if (session->hasDesign)
    {
        for (const auto &name : session->design.topGraphs())
        {
            Tcl_ListObjAppendElement(nullptr, tops,
                                     Tcl_NewStringObj(name.c_str(), static_cast<int>(name.size())));
        }
    }
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("top_graphs", -1), tops);
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("selected_graph", -1),
                   Tcl_NewStringObj(session->selectedGraph.c_str(), -1));
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
}

int cmdShowModules(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    Tcl_SetObjResult(interp, buildGraphListObj(*session));
    return TCL_OK;
}

int cmdShowStats(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureDesign(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }
    Tcl_SetObjResult(interp, buildStatsDict(*session));
    return TCL_OK;
}

int cmdSetOption(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 3)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<key> <value>");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    std::string key = Tcl_GetString(objv[1]);
    std::string value = Tcl_GetString(objv[2]);

    if (key == "log.level")
    {
        auto level = parseLogLevel(value);
        if (!level.has_value())
        {
            return setError(*session, interp, "ARG_ERROR", "invalid log.level value");
        }
        session->options.logLevel = *level;
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }
    if (key == "echo_tcl")
    {
        auto parsed = parseBool(value);
        if (!parsed.has_value())
        {
            return setError(*session, interp, "ARG_ERROR", "echo_tcl expects 0 or 1");
        }
        session->options.echoTcl = *parsed;
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }
    if (key == "history.enable")
    {
        auto parsed = parseBool(value);
        if (!parsed.has_value())
        {
            return setError(*session, interp, "ARG_ERROR", "history.enable expects 0 or 1");
        }
        session->options.historyEnabled = *parsed;
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }
    if (key == "history.max_lines")
    {
        try
        {
            int maxLines = std::stoi(value);
            if (maxLines <= 0)
            {
                return setError(*session, interp, "ARG_ERROR", "history.max_lines must be > 0");
            }
            session->options.historyMaxLines = maxLines;
            linenoiseHistorySetMaxLen(maxLines);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
            return TCL_OK;
        }
        catch (const std::exception &)
        {
            return setError(*session, interp, "ARG_ERROR", "history.max_lines expects integer");
        }
    }
    if (key == "output.dir")
    {
        session->options.outputDir = value;
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }
    if (key == "diagnostic.quiet")
    {
        session->options.diagnosticQuiet = parseQuietList(interp, objv[2]);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
        return TCL_OK;
    }

    return setError(*session, interp, "ARG_ERROR", "unknown option key");
}

int cmdGetOption(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<key>");
        return setErrorFromInterp(*session, interp, "ARG_ERROR");
    }

    std::string key = Tcl_GetString(objv[1]);
    if (key == "log.level")
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(logLevelKeyText(session->options.logLevel).data(), -1));
        return TCL_OK;
    }
    if (key == "echo_tcl")
    {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(session->options.echoTcl ? 1 : 0));
        return TCL_OK;
    }
    if (key == "history.enable")
    {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(session->options.historyEnabled ? 1 : 0));
        return TCL_OK;
    }
    if (key == "history.max_lines")
    {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(session->options.historyMaxLines));
        return TCL_OK;
    }
    if (key == "output.dir")
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(session->options.outputDir.c_str(), -1));
        return TCL_OK;
    }
    if (key == "diagnostic.quiet")
    {
        Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
        for (const auto &pattern : session->options.diagnosticQuiet)
        {
            Tcl_ListObjAppendElement(nullptr, list, Tcl_NewStringObj(pattern.c_str(), -1));
        }
        Tcl_SetObjResult(interp, list);
        return TCL_OK;
    }

    return setError(*session, interp, "ARG_ERROR", "unknown option key");
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::vector<std::string_view> splitTokens(std::string_view text)
{
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        {
            ++i;
        }
        if (i >= text.size())
        {
            break;
        }
        std::size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        {
            ++i;
        }
        tokens.push_back(text.substr(start, i - start));
    }
    return tokens;
}

void addCommandCompletions(std::string_view prefix, linenoiseCompletions *lc)
{
    for (const auto &entry : kCommandHelp)
    {
        if (startsWith(entry.name, prefix))
        {
            linenoiseAddCompletion(lc, std::string(entry.name).c_str());
        }
    }
    if (startsWith("exit", prefix))
    {
        linenoiseAddCompletion(lc, "exit");
    }
    if (startsWith("quit", prefix))
    {
        linenoiseAddCompletion(lc, "quit");
    }
}

void addOptionCompletions(std::string_view command, std::string_view prefix, linenoiseCompletions *lc)
{
    const std::string_view writeOptions[] = {"-o"};
    const std::string_view transformOptions[] = {"-list", "-dryrun"};
    const std::string_view readSvOptions[] = {
        "--std",
        "--top",
        "-I",
        "+incdir",
        "--isystem",
        "-D",
        "-U",
        "-G",
        "-y",
        "-Y",
        "-f",
        "-F",
        "--single-unit",
        "--timescale",
    };
    const std::string_view *options = nullptr;
    std::size_t optionCount = 0;

    if (command == "write_sv" || command == "write_json")
    {
        options = writeOptions;
        optionCount = sizeof(writeOptions) / sizeof(writeOptions[0]);
    }
    else if (command == "transform")
    {
        options = transformOptions;
        optionCount = sizeof(transformOptions) / sizeof(transformOptions[0]);
    }
    else if (command == "read_sv")
    {
        options = readSvOptions;
        optionCount = sizeof(readSvOptions) / sizeof(readSvOptions[0]);
    }

    if (!options)
    {
        return;
    }
    for (std::size_t i = 0; i < optionCount; ++i)
    {
        if (startsWith(options[i], prefix))
        {
            linenoiseAddCompletion(lc, std::string(options[i]).c_str());
        }
    }
}

void addPathCompletions(std::string_view prefix, linenoiseCompletions *lc)
{
    std::string prefixStr(prefix);
    std::filesystem::path dirPath = ".";
    std::string baseName = prefixStr;
    if (!prefixStr.empty() && prefixStr.back() == '/')
    {
        dirPath = prefixStr;
        baseName.clear();
    }
    else
    {
        std::filesystem::path input(prefixStr);
        if (input.has_parent_path())
        {
            dirPath = input.parent_path();
            baseName = input.filename().string();
        }
    }

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec))
    {
        if (ec)
        {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (!startsWith(name, baseName))
        {
            continue;
        }
        std::string completion;
        if (dirPath == ".")
        {
            completion = name;
        }
        else
        {
            completion = (dirPath / name).string();
        }
        if (entry.is_directory())
        {
            completion.push_back('/');
        }
        linenoiseAddCompletion(lc, completion.c_str());
    }
}

void replCompletionCallback(const char *buf, linenoiseCompletions *lc)
{
    std::string_view buffer(buf ? buf : "");
    const bool endsWithSpace =
        !buffer.empty() && std::isspace(static_cast<unsigned char>(buffer.back()));
    std::vector<std::string_view> tokens = splitTokens(buffer);
    std::string_view currentToken;
    if (endsWithSpace)
    {
        currentToken = std::string_view{};
    }
    else if (!tokens.empty())
    {
        currentToken = tokens.back();
    }
    else
    {
        currentToken = buffer;
    }

    if (tokens.empty() && !endsWithSpace)
    {
        addCommandCompletions(currentToken, lc);
        return;
    }
    if (tokens.empty() && endsWithSpace)
    {
        addCommandCompletions({}, lc);
        return;
    }

    const std::string_view command = tokens.front();
    if (tokens.size() == 1 && !endsWithSpace)
    {
        addCommandCompletions(currentToken, lc);
        return;
    }

    const std::string_view lastToken = tokens.back();
    const bool previousWasOutput =
        tokens.size() >= 2 && tokens[tokens.size() - 2] == "-o";
    if (lastToken == "-o" || previousWasOutput)
    {
        addPathCompletions(currentToken, lc);
        return;
    }
    if (currentToken.empty() || currentToken.front() == '-')
    {
        addOptionCompletions(command, currentToken, lc);
        return;
    }
    addPathCompletions(currentToken, lc);
}

std::optional<std::filesystem::path> historyFilePath()
{
    const char *home = std::getenv("HOME");
    if (!home || !*home)
    {
        return std::nullopt;
    }
    return std::filesystem::path(home) / ".wolvrix" / "history";
}

void recordEvalError(Session &session, Tcl_Interp *interp)
{
    const char *message = Tcl_GetStringResult(interp);
    if (!session.lastErrorMessage || *session.lastErrorMessage != (message ? message : ""))
    {
        setLastError(session, "TCL_ERROR", message ? message : "");
    }
}

void registerCommands(Tcl_Interp *interp, Session &session)
{
    Tcl_CreateObjCommand(interp, "help", cmdHelp, &session, nullptr);
    Tcl_CreateObjCommand(interp, "read_sv", cmdReadSv, &session, nullptr);
    Tcl_CreateObjCommand(interp, "read_json", cmdReadJson, &session, nullptr);
    Tcl_CreateObjCommand(interp, "close_design", cmdCloseDesign, &session, nullptr);
    Tcl_CreateObjCommand(interp, "transform", cmdTransform, &session, nullptr);
    Tcl_CreateObjCommand(interp, "transform_list", cmdTransformList, &session, nullptr);
    Tcl_CreateObjCommand(interp, "write_sv", cmdWriteSv, &session, nullptr);
    Tcl_CreateObjCommand(interp, "write_json", cmdWriteJson, &session, nullptr);
    Tcl_CreateObjCommand(interp, "grh_list_graph", cmdGrhListGraph, &session, nullptr);
    Tcl_CreateObjCommand(interp, "grh_create_graph", cmdGrhCreateGraph, &session, nullptr);
    Tcl_CreateObjCommand(interp, "grh_select_graph", cmdGrhSelectGraph, &session, nullptr);
    Tcl_CreateObjCommand(interp, "grh_delete_graph", cmdGrhDeleteGraph, &session, nullptr);
    Tcl_CreateObjCommand(interp, "grh_show_stats", cmdGrhShowStats, &session, nullptr);
    Tcl_CreateObjCommand(interp, "show_design", cmdShowDesign, &session, nullptr);
    Tcl_CreateObjCommand(interp, "show_modules", cmdShowModules, &session, nullptr);
    Tcl_CreateObjCommand(interp, "show_stats", cmdShowStats, &session, nullptr);
    Tcl_CreateObjCommand(interp, "set_option", cmdSetOption, &session, nullptr);
    Tcl_CreateObjCommand(interp, "get_option", cmdGetOption, &session, nullptr);
    Tcl_CreateObjCommand(interp, "last_error", cmdLastError, &session, nullptr);
}

int runRepl(Tcl_Interp *interp, Session &session)
{
    linenoiseSetCompletionCallback(replCompletionCallback);
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(session.options.historyMaxLines);
    std::optional<std::filesystem::path> historyPath = historyFilePath();
    if (historyPath && session.options.historyEnabled)
    {
        std::error_code ec;
        std::filesystem::create_directories(historyPath->parent_path(), ec);
        (void)linenoiseHistoryLoad(historyPath->string().c_str());
    }

    std::string buffer;
    for (;;)
    {
        std::string prompt =
            buffer.empty() ? buildPrompt(session) : std::string(kWolvrixContinuationPrompt);
        char *line = linenoise(prompt.c_str());
        if (!line)
        {
            int keyType = linenoiseKeyType();
            if (keyType == 1)
            {
                buffer.clear();
                std::cerr << '\n';
                continue;
            }
            break;
        }
        if (!buffer.empty())
        {
            buffer.push_back('\n');
        }
        buffer.append(line);
        std::free(line);

        if (!Tcl_CommandComplete(buffer.c_str()))
        {
            continue;
        }

        if (!buffer.empty() && session.options.historyEnabled)
        {
            linenoiseHistoryAdd(buffer.c_str());
        }

        int code = Tcl_Eval(interp, buffer.c_str());
        if (code != TCL_OK)
        {
            recordEvalError(session, interp);
            const char *message = Tcl_GetStringResult(interp);
            std::cerr << (message ? message : "") << '\n';
        }
        else
        {
            const char *result = Tcl_GetStringResult(interp);
            if (result && *result)
            {
                std::cout << result << '\n';
            }
        }
        buffer.clear();
    }
    if (historyPath && session.options.historyEnabled)
    {
        (void)linenoiseHistorySave(historyPath->string().c_str());
    }
    linenoiseHistoryFree();
    return 0;
}

int runScript(Tcl_Interp *interp, Session &session, const char *path)
{
    std::error_code ec;
    const std::filesystem::path scriptPath = std::filesystem::absolute(path, ec);
    const std::filesystem::path scriptDir = scriptPath.has_parent_path()
                                                ? scriptPath.parent_path()
                                                : std::filesystem::path{};
    if (!scriptDir.empty())
    {
        Tcl_SetVar(interp, "WOLVRIX_SCRIPT_DIR", scriptDir.string().c_str(), TCL_GLOBAL_ONLY);
        Tcl_SetVar(interp, "WOLVRIX_SCRIPT_PATH", scriptPath.string().c_str(), TCL_GLOBAL_ONLY);
    }
    std::ifstream input(path);
    if (!input)
    {
        std::cerr << "failed to open script: " << path << '\n';
        return 1;
    }
    std::string buffer;
    std::string line;
    while (std::getline(input, line))
    {
        buffer.append(line);
        buffer.push_back('\n');
        if (!Tcl_CommandComplete(buffer.c_str()))
        {
            continue;
        }
        std::string_view trimmed = trimWhitespace(buffer);
        if (!trimmed.empty() && shouldEchoTclCommand(session, trimmed))
        {
            echoTclCommand(trimmed);
        }
        int code = Tcl_Eval(interp, buffer.c_str());
        if (code != TCL_OK)
        {
            recordEvalError(session, interp);
            std::cerr << Tcl_GetStringResult(interp) << '\n';
            return 1;
        }
        buffer.clear();
    }
    if (!buffer.empty())
    {
        if (!Tcl_CommandComplete(buffer.c_str()))
        {
            std::cerr << "script ended with an incomplete command\n";
            return 1;
        }
        std::string_view trimmed = trimWhitespace(buffer);
        if (!trimmed.empty() && shouldEchoTclCommand(session, trimmed))
        {
            echoTclCommand(trimmed);
        }
        int code = Tcl_Eval(interp, buffer.c_str());
        if (code != TCL_OK)
        {
            recordEvalError(session, interp);
            std::cerr << Tcl_GetStringResult(interp) << '\n';
            return 1;
        }
    }
    return 0;
}

int runCommand(Tcl_Interp *interp, Session &session, const char *command)
{
    int code = Tcl_Eval(interp, command);
    if (code != TCL_OK)
    {
        recordEvalError(session, interp);
        std::cerr << Tcl_GetStringResult(interp) << '\n';
        return 1;
    }
    return 0;
}

int entry(int argc, char **argv)
{
    ensureTclLibraryPath();
    Tcl_FindExecutable(argv[0]);
    Tcl_Interp *interp = Tcl_CreateInterp();
    if (Tcl_Init(interp) != TCL_OK)
    {
        std::cerr << "tcl init failed: " << Tcl_GetStringResult(interp) << '\n';
        Tcl_DeleteInterp(interp);
        return 1;
    }

    Session session;
    gActiveSession = &session;
    registerCommands(interp, session);

    const auto sessionStart = std::chrono::steady_clock::now();
    printWelcome();

    int result = 0;
    if (argc >= 3 && std::string_view(argv[1]) == "-f")
    {
        result = runScript(interp, session, argv[2]);
    }
    else if (argc >= 3 && std::string_view(argv[1]) == "-c")
    {
        result = runCommand(interp, session, argv[2]);
    }
    else
    {
        result = runRepl(interp, session);
    }

    const auto sessionEnd = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(sessionEnd - sessionStart);
    struct rusage usage {};
    std::optional<long> maxRssKb;
    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
        maxRssKb = usage.ru_maxrss;
    }
    printGoodbye(elapsed, maxRssKb);

    Tcl_DeleteInterp(interp);
    Tcl_Finalize();
    gActiveSession = nullptr;
    return result;
}

} // namespace wolvrix::app

int main(int argc, char **argv)
{
    return wolvrix::app::entry(argc, argv);
}
