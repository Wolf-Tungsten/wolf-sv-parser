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

struct Session
{
    bool hasNetlist = false;
    wolvrix::lib::grh::Netlist netlist;
};

constexpr int kHistoryMaxLen = 200;

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
        "Parse SystemVerilog into a GRH netlist",
        "read_sv <file> ?<slang-opts>...?\n"
        "\n"
        "Parse a SystemVerilog design and build a GRH netlist in the session. This must\n"
        "be the first load command; if a netlist is already loaded, run close_design first.\n"
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
        "Load a GRH netlist from JSON",
        "read_json <file>\n"
        "\n"
        "Load a GRH netlist from JSON. If a netlist is already loaded, run\n"
        "close_design first.\n",
    },
    {
        "close_design",
        "Close the current GRH netlist",
        "close_design\n"
        "\n"
        "Release the current GRH netlist so another read_sv/read_json can be issued.\n",
    },
    {
        "transform",
        "Run a transform pass on the GRH netlist",
        "transform <passname> ?passargs...?\n"
        "\n"
        "Run a transform pass on the current GRH netlist.\n"
        "Use transform_list to see available passes.\n"
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
        "Emit SystemVerilog from the GRH netlist",
        "write_sv -o <file>\n"
        "\n"
        "Emit SystemVerilog for the current GRH netlist.\n",
    },
    {
        "write_json",
        "Store the GRH netlist as JSON",
        "write_json -o <file>\n"
        "\n"
        "Store GRH JSON for the current GRH netlist.\n",
    },
    {
        "grh_list_graph",
        "List graphs in the GRH netlist",
        "grh_list_graph\n"
        "\n"
        "Return a Tcl list of graph names in the current GRH netlist.\n",
    },
    {
        "grh_create_graph",
        "Create an empty GRH graph",
        "grh_create_graph <name>\n"
        "\n"
        "Create an empty graph with the given name in the current GRH netlist.\n",
    },
    {
        "grh_show_stats",
        "Show basic GRH netlist statistics",
        "grh_show_stats\n"
        "\n"
        "Return a Tcl dict with counts for graphs, operations, and values.\n",
    },
};

constexpr std::string_view kWolvrixPrefix = "wolvrix";
constexpr std::string_view kWolvrixPrompt = "wolvrix> ";
constexpr std::string_view kWolvrixContinuationPrompt = "... ";
std::optional<std::size_t> gWelcomeBoxWidth{};
constexpr std::size_t kBoxHorizPadding = 2;
constexpr std::size_t kBoxVertPadding = 1;
bool gPrintedCommandEcho = false;
bool gLogJustPrinted = false;
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

void logLine(std::string_view entity, wolvrix::lib::LogLevel level, std::string_view message)
{
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
    return message.message.find("Ignoring timing control (event)") != std::string::npos;
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
    output.append(kWolvrixPrompt);
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

bool shouldEchoTclCommand(std::string_view command)
{
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

int setError(Tcl_Interp *interp, std::string_view message)
{
    Tcl_SetObjResult(interp, Tcl_NewStringObj(message.data(), static_cast<int>(message.size())));
    return TCL_ERROR;
}

bool ensureNetlist(Session &session, Tcl_Interp *interp)
{
    if (!session.hasNetlist)
    {
        setError(interp, "no netlist loaded; run read_sv or read_json first");
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

std::optional<std::filesystem::path> parseOutputPath(int objc, Tcl_Obj *const objv[], Tcl_Interp *interp)
{
    std::optional<std::filesystem::path> output;
    for (int i = 1; i < objc; ++i)
    {
        const char *arg = Tcl_GetString(objv[i]);
        if (std::string_view(arg) == "-o")
        {
            if (i + 1 >= objc)
            {
                setError(interp, "-o expects a path");
                return std::nullopt;
            }
            output = std::filesystem::path(Tcl_GetString(objv[i + 1]));
            ++i;
        }
        else
        {
            setError(interp, "unknown option for write_* (expected -o)");
            return std::nullopt;
        }
    }
    if (!output)
    {
        setError(interp, "-o <file> is required");
        return std::nullopt;
    }
    return output;
}

int cmdReadSv(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (session->hasNetlist)
    {
        return setError(interp, "netlist already loaded; run close_design first");
    }
    if (objc < 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<file> ?<slang-opts>...? ");
        return TCL_ERROR;
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
        return setError(interp, "failed to parse slang options");
    }
    if (!driver.processOptions())
    {
        reportSlangDiagnostics();
        return setError(interp, "failed to apply slang options");
    }
    if (!driver.parseAllSources())
    {
        reportSlangDiagnostics();
        return setError(interp, "failed to parse sources");
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
        return setError(interp, "slang reported errors; see diagnostics");
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
    convertOptions.enableLogging = true;
    convertOptions.logLevel = wolvrix::lib::LogLevel::Info;
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
        session->netlist = converter.convert(root);
    }
    catch (const wolvrix::lib::ingest::ConvertAbort &)
    {
        converter.diagnostics().flushThreadLocal();
        printDiagnosticsExpanded("read_sv",
                                 converter.diagnostics().messages(),
                                 compilation->getSourceManager());
        return setError(interp, "convert aborted; see diagnostics");
    }
    const auto convertEnd = std::chrono::steady_clock::now();
    const auto convertMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(convertEnd - convertStart).count();
    if (converter.diagnostics().hasError())
    {
        printDiagnosticsExpanded("read_sv",
                                 converter.diagnostics().messages(),
                                 compilation->getSourceManager());
        return setError(interp, "convert failed; see diagnostics");
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

    session->hasNetlist = true;
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdHelp(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    (void)clientData;
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
            return setError(interp, "unknown command: " + std::string(name));
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj(entry->detail.data(),
                                                  static_cast<int>(entry->detail.size())));
        return TCL_OK;
    }
    Tcl_WrongNumArgs(interp, 1, objv, "?command?");
    return TCL_ERROR;
}

int cmdReadJson(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (session->hasNetlist)
    {
        return setError(interp, "netlist already loaded; run close_design first");
    }
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<file>");
        return TCL_ERROR;
    }

    std::string error;
    const char *path = Tcl_GetString(objv[1]);
    auto jsonText = readFile(path, error);
    if (!jsonText)
    {
        return setError(interp, error);
    }

    try
    {
        session->netlist = wolvrix::lib::grh::Netlist::fromJsonString(*jsonText);
    }
    catch (const std::exception &ex)
    {
        return setError(interp, std::string("load json failed: ") + ex.what());
    }

    session->hasNetlist = true;
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdCloseDesign(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    session->netlist = wolvrix::lib::grh::Netlist{};
    session->hasNetlist = false;
    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdTransformList(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    (void)clientData;
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    const std::vector<std::string> passes{
        "xmr-resolve",
        "const-fold",
        "redundant-elim",
        "memory-init-check",
        "dead-code-elim",
        "stats",
    };
    appendTclList(list, passes);
    Tcl_SetObjResult(interp, list);
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
    if (!ensureNetlist(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc < 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<passname> ?passargs...? ");
        return TCL_ERROR;
    }

    std::vector<std::string_view> args;
    for (int i = 2; i < objc; ++i)
    {
        args.push_back(Tcl_GetString(objv[i]));
    }

    std::string error;
    auto pass = makePass(Tcl_GetString(objv[1]), args, error);
    if (!pass)
    {
        return setError(interp, error);
    }

    const char *passName = Tcl_GetString(objv[1]);
    const std::string entity = std::string("transform ") + passName;
    wolvrix::lib::transform::PassDiagnostics diagnostics;
    wolvrix::lib::transform::PassManager manager;
    manager.options().logLevel = wolvrix::lib::LogLevel::Info;
    manager.options().logSink = nullptr;
    manager.addPass(std::move(pass));
    const auto transformStart = std::chrono::steady_clock::now();
    wolvrix::lib::transform::PassManagerResult result = manager.run(session->netlist, diagnostics);
    const auto transformEnd = std::chrono::steady_clock::now();
    const auto transformMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(transformEnd - transformStart).count();
    if (diagnostics.hasError() || !result.success)
    {
        printDiagnostics(entity, diagnostics.messages());
        return setError(interp, "transform failed; see diagnostics");
    }
    printDiagnostics(entity, diagnostics.messages());
    std::string doneMessage = "done ";
    doneMessage.append(std::to_string(transformMs));
    doneMessage.append("ms ");
    doneMessage.append(result.changed ? "changed" : "ok");
    logLine(entity, wolvrix::lib::LogLevel::Info, doneMessage);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(result.changed ? "changed" : "ok", -1));
    return TCL_OK;
}

int cmdWriteJson(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureNetlist(*session, interp))
    {
        return TCL_ERROR;
    }

    auto outputPath = parseOutputPath(objc, objv, interp);
    if (!outputPath)
    {
        return TCL_ERROR;
    }

    wolvrix::lib::store::StoreDiagnostics diagnostics;
    wolvrix::lib::store::StoreJson store(&diagnostics);
    wolvrix::lib::store::StoreOptions options;
    options.jsonMode = wolvrix::lib::store::JsonPrintMode::PrettyCompact;
    options.outputFilename = outputPath->filename().string();
    if (!outputPath->parent_path().empty())
    {
        options.outputDir = outputPath->parent_path().string();
    }

    wolvrix::lib::store::StoreResult result = store.store(session->netlist, options);
    if (diagnostics.hasError() || !result.success)
    {
        return setError(interp, "write_json failed; see diagnostics");
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(result.artifacts.empty()
                                                  ? outputPath->string().c_str()
                                                  : result.artifacts.front().c_str(),
                                              -1));
    return TCL_OK;
}

int cmdWriteSv(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureNetlist(*session, interp))
    {
        return TCL_ERROR;
    }

    auto outputPath = parseOutputPath(objc, objv, interp);
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

    wolvrix::lib::emit::EmitResult result = emitter.emit(session->netlist, options);
    const auto emitEnd = std::chrono::steady_clock::now();
    const auto emitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(emitEnd - emitStart).count();
    if (diagnostics.hasError() || !result.success)
    {
        return setError(interp, "write_sv failed; see diagnostics");
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
    if (!ensureNetlist(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }

    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    std::vector<std::string> names;
    names.reserve(session->netlist.graphOrder().size());
    for (const auto &name : session->netlist.graphOrder())
    {
        names.push_back(name);
    }
    appendTclList(list, names);
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

int cmdGrhCreateGraph(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureNetlist(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 2)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "<name>");
        return TCL_ERROR;
    }

    const char *name = Tcl_GetString(objv[1]);
    try
    {
        session->netlist.createGraph(name);
    }
    catch (const std::exception &ex)
    {
        return setError(interp, ex.what());
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj("ok", -1));
    return TCL_OK;
}

int cmdGrhShowStats(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    auto *session = static_cast<Session *>(clientData);
    if (!ensureNetlist(*session, interp))
    {
        return TCL_ERROR;
    }
    if (objc != 1)
    {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }

    std::size_t graphCount = session->netlist.graphs().size();
    std::size_t opCount = 0;
    std::size_t valueCount = 0;
    for (const auto &entry : session->netlist.graphs())
    {
        const auto &graph = *entry.second;
        opCount += graph.operations().size();
        valueCount += graph.values().size();
    }

    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("graphs", -1), Tcl_NewWideIntObj(graphCount));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("operations", -1), Tcl_NewWideIntObj(opCount));
    Tcl_DictObjPut(nullptr, dict, Tcl_NewStringObj("values", -1), Tcl_NewWideIntObj(valueCount));
    Tcl_SetObjResult(interp, dict);
    return TCL_OK;
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
    Tcl_CreateObjCommand(interp, "grh_show_stats", cmdGrhShowStats, &session, nullptr);
}

int runRepl(Tcl_Interp *interp)
{
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(kHistoryMaxLen);

    std::string buffer;
    for (;;)
    {
        const char *prompt = buffer.empty() ? kWolvrixPrompt.data() : kWolvrixContinuationPrompt.data();
        char *line = linenoise(prompt);
        if (!line)
        {
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

        if (!buffer.empty())
        {
            linenoiseHistoryAdd(buffer.c_str());
        }

        int code = Tcl_Eval(interp, buffer.c_str());
        if (code != TCL_OK)
        {
            std::cerr << Tcl_GetStringResult(interp) << '\n';
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
    return 0;
}

int runScript(Tcl_Interp *interp, const char *path)
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
        if (!trimmed.empty() && shouldEchoTclCommand(trimmed))
        {
            echoTclCommand(trimmed);
        }
        int code = Tcl_Eval(interp, buffer.c_str());
        if (code != TCL_OK)
        {
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
        if (!trimmed.empty() && shouldEchoTclCommand(trimmed))
        {
            echoTclCommand(trimmed);
        }
        int code = Tcl_Eval(interp, buffer.c_str());
        if (code != TCL_OK)
        {
            std::cerr << Tcl_GetStringResult(interp) << '\n';
            return 1;
        }
    }
    return 0;
}

int runCommand(Tcl_Interp *interp, const char *command)
{
    int code = Tcl_Eval(interp, command);
    if (code != TCL_OK)
    {
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
    registerCommands(interp, session);

    const auto sessionStart = std::chrono::steady_clock::now();
    printWelcome();

    int result = 0;
    if (argc >= 3 && std::string_view(argv[1]) == "-f")
    {
        result = runScript(interp, argv[2]);
    }
    else if (argc >= 3 && std::string_view(argv[1]) == "-c")
    {
        result = runCommand(interp, argv[2]);
    }
    else
    {
        result = runRepl(interp);
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
    return result;
}

} // namespace wolvrix::app

int main(int argc, char **argv)
{
    return wolvrix::app::entry(argc, argv);
}
