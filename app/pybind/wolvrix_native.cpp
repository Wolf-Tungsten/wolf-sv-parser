#include <Python.h>

#include "emit.hpp"
#include "grh.hpp"
#include "ingest.hpp"
#include "logging.hpp"
#include "store.hpp"
#include "transform.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/text/SourceManager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <unistd.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

    constexpr const char *kDesignCapsuleName = "wolvrix.Design";

    struct DesignHandle
    {
        wolvrix::lib::grh::Design design;
        std::shared_ptr<slang::ast::Compilation> compilation;
    };

    void destroyDesignCapsule(PyObject *capsule)
    {
        auto *handle = static_cast<DesignHandle *>(
            PyCapsule_GetPointer(capsule, kDesignCapsuleName));
        delete handle;
    }

    DesignHandle *getDesignHandle(PyObject *capsule)
    {
        return static_cast<DesignHandle *>(
            PyCapsule_GetPointer(capsule, kDesignCapsuleName));
    }

    wolvrix::lib::grh::Design *getDesign(PyObject *capsule)
    {
        auto *handle = getDesignHandle(capsule);
        if (!handle)
        {
            return nullptr;
        }
        return &handle->design;
    }

    const slang::SourceManager *getDesignSourceManager(PyObject *capsule)
    {
        auto *handle = getDesignHandle(capsule);
        if (!handle || !handle->compilation)
        {
            return nullptr;
        }
        return handle->compilation->getSourceManager();
    }

    PyObject *makeDesignCapsule(wolvrix::lib::grh::Design design,
                                std::shared_ptr<slang::ast::Compilation> compilation = {})
    {
        auto *handle = new DesignHandle{std::move(design), std::move(compilation)};
        return PyCapsule_New(handle, kDesignCapsuleName, destroyDesignCapsule);
    }

    bool readFileText(const std::string &path, std::string &out, std::string &error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "open failed: " + path;
            return false;
        }
        out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (out.empty())
        {
            error = "empty file: " + path;
            return false;
        }
        return true;
    }

    bool parseStringList(PyObject *obj, std::vector<std::string> &out, std::string &error)
    {
        if (obj == Py_None)
        {
            return true;
        }
        PyObject *seq = PySequence_Fast(obj, "expected a list of strings");
        if (!seq)
        {
            error = "expected a list of strings";
            return false;
        }
        const Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
        PyObject **items = PySequence_Fast_ITEMS(seq);
        out.reserve(static_cast<std::size_t>(count));
        for (Py_ssize_t i = 0; i < count; ++i)
        {
            PyObject *item = items[i];
            if (!PyUnicode_Check(item))
            {
                Py_DECREF(seq);
                error = "expected string item in list";
                return false;
            }
            const char *text = PyUnicode_AsUTF8(item);
            if (!text)
            {
                Py_DECREF(seq);
                error = "invalid string item in list";
                return false;
            }
            out.emplace_back(text);
        }
        Py_DECREF(seq);
        return true;
    }

    struct PassSpec
    {
        std::string name;
        std::vector<std::string> args;
    };

    bool parsePassPipeline(PyObject *obj, std::vector<PassSpec> &out, std::string &error)
    {
        if (obj == Py_None)
        {
            error = "pipeline must be a non-empty list";
            return false;
        }

        PyObject *seq = PySequence_Fast(
            obj,
            "pipeline must be a non-empty list of pass specs");
        if (!seq)
        {
            error = "pipeline must be a non-empty list of pass specs";
            return false;
        }

        const Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
        if (count <= 0)
        {
            Py_DECREF(seq);
            error = "pipeline must be a non-empty list";
            return false;
        }

        PyObject **items = PySequence_Fast_ITEMS(seq);
        out.reserve(static_cast<std::size_t>(count));
        for (Py_ssize_t i = 0; i < count; ++i)
        {
            PyObject *item = items[i];
            PassSpec spec;

            if (PyUnicode_Check(item))
            {
                const char *nameText = PyUnicode_AsUTF8(item);
                if (!nameText)
                {
                    Py_DECREF(seq);
                    error = "pipeline contains invalid pass name";
                    return false;
                }
                spec.name = nameText;
            }
            else
            {
                PyObject *pair = PySequence_Fast(
                    item,
                    "each pipeline item must be pass name string or [name, args]");
                if (!pair)
                {
                    Py_DECREF(seq);
                    error = "each pipeline item must be pass name string or [name, args]";
                    return false;
                }
                const Py_ssize_t pairCount = PySequence_Fast_GET_SIZE(pair);
                if (pairCount != 2)
                {
                    Py_DECREF(pair);
                    Py_DECREF(seq);
                    error = "pipeline item sequence must have exactly 2 elements: [name, args]";
                    return false;
                }
                PyObject **pairItems = PySequence_Fast_ITEMS(pair);
                PyObject *nameObj = pairItems[0];
                PyObject *argsObj = pairItems[1];
                if (!PyUnicode_Check(nameObj))
                {
                    Py_DECREF(pair);
                    Py_DECREF(seq);
                    error = "pipeline pass name must be string";
                    return false;
                }
                const char *nameText = PyUnicode_AsUTF8(nameObj);
                if (!nameText)
                {
                    Py_DECREF(pair);
                    Py_DECREF(seq);
                    error = "pipeline contains invalid pass name";
                    return false;
                }
                spec.name = nameText;

                std::string parseErr;
                if (!parseStringList(argsObj, spec.args, parseErr))
                {
                    Py_DECREF(pair);
                    Py_DECREF(seq);
                    error = "invalid args for pass '" + spec.name + "': " + parseErr;
                    return false;
                }
                Py_DECREF(pair);
            }

            if (spec.name.empty())
            {
                Py_DECREF(seq);
                error = "pipeline pass name must not be empty";
                return false;
            }

            out.push_back(std::move(spec));
        }

        Py_DECREF(seq);
        return true;
    }

    wolvrix::lib::LogLevel parseLogLevel(std::string_view text, bool &ok)
    {
        ok = true;
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
        if (lowered == "off")
        {
            return wolvrix::lib::LogLevel::Off;
        }
        ok = false;
        return wolvrix::lib::LogLevel::Warn;
    }

    const char *diagnosticKindText(wolvrix::lib::diag::DiagnosticKind kind)
    {
        switch (kind)
        {
        case wolvrix::lib::diag::DiagnosticKind::Todo:
            return "todo";
        case wolvrix::lib::diag::DiagnosticKind::Warning:
            return "warning";
        case wolvrix::lib::diag::DiagnosticKind::Info:
            return "info";
        case wolvrix::lib::diag::DiagnosticKind::Debug:
            return "debug";
        case wolvrix::lib::diag::DiagnosticKind::Error:
        default:
            return "error";
        }
    }

    std::string formatDiagnostic(const wolvrix::lib::diag::Diagnostic &message,
                                 const slang::SourceManager *sourceManager,
                                 bool useColor);

    std::string formatDiagnostics(const std::vector<wolvrix::lib::diag::Diagnostic> &messages,
                                  const slang::SourceManager *sourceManager)
    {
        if (messages.empty())
        {
            return "unknown error";
        }
        std::string out;
        for (const auto &message : messages)
        {
            if (!out.empty())
            {
                out.append("\n");
            }
            out.append(formatDiagnostic(message, sourceManager, /* useColor */ false));
        }
        return out;
    }

    wolvrix::lib::store::JsonPrintMode parseJsonMode(std::string_view text, bool &ok)
    {
        ok = true;
        std::string lowered(text);
        for (char &ch : lowered)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (lowered == "compact")
        {
            return wolvrix::lib::store::JsonPrintMode::Compact;
        }
        if (lowered == "pretty")
        {
            return wolvrix::lib::store::JsonPrintMode::Pretty;
        }
        if (lowered == "pretty-compact" || lowered == "pretty_compact")
        {
            return wolvrix::lib::store::JsonPrintMode::PrettyCompact;
        }
        ok = false;
        return wolvrix::lib::store::JsonPrintMode::PrettyCompact;
    }

    bool parseDiagnosticsLevel(std::string_view text, wolvrix::lib::LogLevel &level)
    {
        if (text == "none")
        {
            level = wolvrix::lib::LogLevel::Off;
            return true;
        }
        bool ok = false;
        level = parseLogLevel(text, ok);
        return ok;
    }

    int logLevelRank(wolvrix::lib::LogLevel level)
    {
        return static_cast<int>(level);
    }

    wolvrix::lib::LogLevel diagnosticToLogLevel(wolvrix::lib::diag::DiagnosticKind kind)
    {
        switch (kind)
        {
        case wolvrix::lib::diag::DiagnosticKind::Debug:
            return wolvrix::lib::LogLevel::Debug;
        case wolvrix::lib::diag::DiagnosticKind::Info:
            return wolvrix::lib::LogLevel::Info;
        case wolvrix::lib::diag::DiagnosticKind::Warning:
            return wolvrix::lib::LogLevel::Warn;
        case wolvrix::lib::diag::DiagnosticKind::Todo:
        case wolvrix::lib::diag::DiagnosticKind::Error:
        default:
            return wolvrix::lib::LogLevel::Error;
        }
    }

    struct DiagnosticLocationInfo
    {
        slang::SourceLocation location;
        std::string filename;
        std::size_t line = 0;
        std::size_t column = 0;
    };

    bool getDiagnosticLocationInfo(const slang::SourceManager *sourceManager,
                                   slang::SourceLocation location,
                                   DiagnosticLocationInfo &info)
    {
        if (!sourceManager || !location.valid())
        {
            return false;
        }
        auto resolved = sourceManager->getFullyOriginalLoc(location);
        if (!resolved.valid())
        {
            resolved = location;
        }
        if (!sourceManager->isFileLoc(resolved))
        {
            auto expanded = sourceManager->getFullyExpandedLoc(location);
            if (expanded.valid() && sourceManager->isFileLoc(expanded))
            {
                resolved = expanded;
            }
            else if (!sourceManager->isFileLoc(resolved))
            {
                return false;
            }
        }

        std::string fileName;
        const auto nameView = sourceManager->getFileName(resolved);
        if (!nameView.empty())
        {
            fileName.assign(nameView);
        }
        if (fileName.empty())
        {
            const auto &fullPath = sourceManager->getFullPath(resolved.buffer());
            if (!fullPath.empty())
            {
                fileName = fullPath.string();
            }
        }
        if (fileName.empty())
        {
            const auto rawName = sourceManager->getRawFileName(resolved.buffer());
            if (!rawName.empty())
            {
                fileName.assign(rawName);
            }
        }
        if (fileName.empty())
        {
            return false;
        }
        info.location = resolved;
        info.filename = std::move(fileName);
        info.line = sourceManager->getLineNumber(resolved);
        info.column = sourceManager->getColumnNumber(resolved);
        return true;
    }

    std::string formatSourceLineSnippet(const slang::SourceManager *sourceManager,
                                        const DiagnosticLocationInfo &info)
    {
        if (!sourceManager || !info.location.valid())
        {
            return {};
        }
        const auto bufferId = info.location.buffer();
        const auto sourceText = sourceManager->getSourceText(bufferId);
        if (sourceText.empty())
        {
            return {};
        }
        const std::size_t offset = info.location.offset();
        if (offset >= sourceText.size())
        {
            return {};
        }

        std::size_t lineStart = sourceText.rfind('\n', offset);
        if (lineStart == std::string_view::npos)
        {
            lineStart = 0;
        }
        else
        {
            ++lineStart;
        }
        std::size_t lineEnd = sourceText.find('\n', offset);
        if (lineEnd == std::string_view::npos)
        {
            lineEnd = sourceText.size();
        }
        if (lineEnd <= lineStart)
        {
            return {};
        }

        std::string line(sourceText.substr(lineStart, lineEnd - lineStart));
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        for (char &ch : line)
        {
            if (ch == '\t')
            {
                ch = ' ';
            }
        }

        std::size_t caretPos = info.column > 0 ? info.column - 1 : 0;
        if (caretPos > line.size())
        {
            caretPos = line.size();
        }

        const std::size_t maxLen = 200;
        std::size_t prefixLen = 0;
        std::size_t sliceStart = 0;
        std::size_t sliceEnd = line.size();
        if (line.size() > maxLen)
        {
            const std::size_t context = 80;
            if (caretPos > context)
            {
                sliceStart = caretPos - context;
            }
            sliceEnd = std::min(sliceStart + maxLen, line.size());
            if (sliceStart > 0)
            {
                prefixLen = 3;
            }
            if (sliceEnd < line.size())
            {
                line = line.substr(sliceStart, sliceEnd - sliceStart);
                line.append("...");
            }
            else
            {
                line = line.substr(sliceStart);
            }
            if (sliceStart > 0)
            {
                line.insert(0, "...");
            }
            caretPos = prefixLen + (caretPos - sliceStart);
        }

        std::string caretLine;
        caretLine.assign(caretPos, ' ');
        caretLine.push_back('^');

        std::string snippet;
        snippet.append("\n  | ");
        snippet.append(line);
        snippet.append("\n  | ");
        snippet.append(caretLine);
        return snippet;
    }

    bool shouldUseColor()
    {
        const char *noColor = std::getenv("NO_COLOR");
        if (noColor && *noColor != '\0')
        {
            return false;
        }
        const char *env = std::getenv("WOLVRIX_COLOR");
        if (env && *env != '\0')
        {
            std::string value(env);
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (value == "1" || value == "true" || value == "yes" || value == "on" ||
                value == "always")
            {
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off" ||
                value == "never")
            {
                return false;
            }
        }
        return isatty(fileno(stderr)) == 1;
    }

    const char *diagnosticColorCode(wolvrix::lib::diag::DiagnosticKind kind, bool useColor)
    {
        if (!useColor)
        {
            return "";
        }
        switch (kind)
        {
        case wolvrix::lib::diag::DiagnosticKind::Error:
            return "\033[1;31m";
        case wolvrix::lib::diag::DiagnosticKind::Warning:
            return "\033[1;33m";
        case wolvrix::lib::diag::DiagnosticKind::Info:
            return "\033[1;34m";
        case wolvrix::lib::diag::DiagnosticKind::Debug:
            return "\033[2m";
        case wolvrix::lib::diag::DiagnosticKind::Todo:
            return "\033[1;35m";
        default:
            return "";
        }
    }

    const char *diagnosticColorReset(bool useColor)
    {
        return useColor ? "\033[0m" : "";
    }

    std::string formatDiagnostic(const wolvrix::lib::diag::Diagnostic &message,
                                 const slang::SourceManager *sourceManager,
                                 bool useColor)
    {
        std::string out;
        out.append(diagnosticColorCode(message.kind, useColor));
        out.append(diagnosticKindText(message.kind));
        out.append(diagnosticColorReset(useColor));
        DiagnosticLocationInfo info;
        const bool hasLocation =
            message.location && message.location->valid() &&
            getDiagnosticLocationInfo(sourceManager, *message.location, info);
        if (hasLocation)
        {
            out.append(" ");
            out.append(info.filename);
            out.append(":");
            out.append(std::to_string(info.line));
            out.append(":");
            out.append(std::to_string(info.column));
        }
        if (!message.passName.empty())
        {
            out.append(" [");
            out.append(message.passName);
            out.append("]");
        }
        out.append(" ");
        out.append(message.message);
        if (!message.context.empty())
        {
            out.append(" (");
            out.append(message.context);
            out.append(")");
        }
        if (!message.originSymbol.empty())
        {
            out.append(" <");
            out.append(message.originSymbol);
            out.append(">");
        }
        if (hasLocation)
        {
            out.append(formatSourceLineSnippet(sourceManager, info));
        }
        return out;
    }

    void emitDiagnostics(const std::vector<wolvrix::lib::diag::Diagnostic> &messages,
                         wolvrix::lib::LogLevel threshold,
                         const slang::SourceManager *sourceManager)
    {
        if (threshold == wolvrix::lib::LogLevel::Off)
        {
            return;
        }
        if (messages.empty())
        {
            return;
        }
        std::string out;
        const bool useColor = shouldUseColor();
        for (const auto &message : messages)
        {
            const auto level = diagnosticToLogLevel(message.kind);
            if (logLevelRank(level) < logLevelRank(threshold))
            {
                continue;
            }
            if (!out.empty())
            {
                out.append("\n");
            }
            out.append(formatDiagnostic(message, sourceManager, useColor));
        }
        if (!out.empty())
        {
            std::cerr << out << '\n';
        }
    }

    PyObject *py_read_sv(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *path_obj = nullptr;
        PyObject *slang_args_obj = Py_None;
        const char *log_level_text = "warn";
        const char *diag_text = "warn";
        static const char *kwlist[] = {"path", "slang_args", "log_level", "diagnostics", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Oss",
                                         const_cast<char **>(kwlist),
                                         &path_obj, &slang_args_obj, &log_level_text, &diag_text))
        {
            return nullptr;
        }

        const char *path = nullptr;
        if (path_obj == Py_None)
        {
            path = nullptr;
        }
        else if (PyUnicode_Check(path_obj))
        {
            path = PyUnicode_AsUTF8(path_obj);
            if (!path)
            {
                return nullptr;
            }
        }
        else
        {
            PyErr_SetString(PyExc_ValueError, "path must be a string or None");
            return nullptr;
        }

        std::vector<std::string> slang_args;
        std::string error;
        if (!parseStringList(slang_args_obj, slang_args, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        std::vector<std::string> argv_storage;
        argv_storage.reserve(2 + slang_args.size());
        argv_storage.emplace_back("read_sv");
        if (path && path[0] != '\0')
        {
            argv_storage.emplace_back(path);
        }
        else if (path && path[0] == '\0')
        {
            PyErr_SetString(PyExc_ValueError, "path must be a non-empty string or None");
            return nullptr;
        }
        for (const auto &arg : slang_args)
        {
            argv_storage.push_back(arg);
        }
        std::vector<const char *> argv;
        argv.reserve(argv_storage.size());
        for (const auto &arg : argv_storage)
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
            (void)driver.reportDiagnostics(/* quiet */ false);
        };

        if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()))
        {
            reportSlangDiagnostics();
            PyErr_SetString(PyExc_RuntimeError, "failed to parse slang options");
            return nullptr;
        }
        if (!driver.processOptions())
        {
            reportSlangDiagnostics();
            PyErr_SetString(PyExc_RuntimeError, "failed to apply slang options");
            return nullptr;
        }
        if (!driver.parseAllSources())
        {
            reportSlangDiagnostics();
            PyErr_SetString(PyExc_RuntimeError, "failed to parse sources");
            return nullptr;
        }

        auto compilation = std::shared_ptr<slang::ast::Compilation>(driver.createCompilation());
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
            driver.reportCompilation(*compilation, /* quiet */ false);
            reportSlangDiagnostics();
        }
        if (hasSlangErrors)
        {
            PyErr_SetString(PyExc_RuntimeError, "slang reported errors; see diagnostics");
            return nullptr;
        }

        bool ok = false;
        const wolvrix::lib::LogLevel log_level = parseLogLevel(log_level_text, ok);
        if (!ok)
        {
            PyErr_SetString(PyExc_ValueError, "unknown log_level");
            return nullptr;
        }
        wolvrix::lib::LogLevel diag_level = wolvrix::lib::LogLevel::Warn;
        if (!parseDiagnosticsLevel(diag_text, diag_level))
        {
            PyErr_SetString(PyExc_ValueError, "unknown diagnostics level");
            return nullptr;
        }

        wolvrix::lib::ingest::ConvertOptions convertOptions;
        convertOptions.abortOnError = true;
        convertOptions.enableLogging = log_level != wolvrix::lib::LogLevel::Off;
        convertOptions.logLevel = log_level;

        wolvrix::lib::ingest::ConvertDriver converter(convertOptions);
        if (convertOptions.enableLogging)
        {
            converter.logger().setSink([](const wolvrix::lib::LogEvent &event) {
                std::string message;
                if (!event.tag.empty())
                {
                    message.append(event.tag);
                    message.append(": ");
                }
                message.append(event.message);
                std::cerr << message << '\n';
            });
        }

        wolvrix::lib::grh::Design design;
        try
        {
            design = converter.convert(compilation->getRoot());
        }
        catch (const wolvrix::lib::ingest::ConvertAbort &)
        {
            converter.diagnostics().flushThreadLocal();
            emitDiagnostics(converter.diagnostics().messages(), diag_level, compilation->getSourceManager());
            const std::string diagText = formatDiagnostics(converter.diagnostics().messages(),
                                                           compilation->getSourceManager());
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }
        if (converter.diagnostics().hasError())
        {
            emitDiagnostics(converter.diagnostics().messages(), diag_level, compilation->getSourceManager());
            const std::string diagText = formatDiagnostics(converter.diagnostics().messages(),
                                                           compilation->getSourceManager());
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }
        emitDiagnostics(converter.diagnostics().messages(), diag_level, compilation->getSourceManager());

        return makeDesignCapsule(std::move(design), std::move(compilation));
    }

    PyObject *py_read_json(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        const char *path = nullptr;
        static const char *kwlist[] = {"path", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", const_cast<char **>(kwlist), &path))
        {
            return nullptr;
        }
        std::string contents;
        std::string error;
        if (!readFileText(path, contents, error))
        {
            PyErr_SetString(PyExc_RuntimeError, error.c_str());
            return nullptr;
        }
        try
        {
            auto design = wolvrix::lib::grh::Design::fromJsonString(contents);
            return makeDesignCapsule(std::move(design));
        }
        catch (const std::exception &ex)
        {
            PyErr_SetString(PyExc_RuntimeError, ex.what());
            return nullptr;
        }
    }

    PyObject *py_load_json_string(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        const char *text = nullptr;
        static const char *kwlist[] = {"text", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", const_cast<char **>(kwlist), &text))
        {
            return nullptr;
        }
        try
        {
            auto design = wolvrix::lib::grh::Design::fromJsonString(text);
            return makeDesignCapsule(std::move(design));
        }
        catch (const std::exception &ex)
        {
            PyErr_SetString(PyExc_RuntimeError, ex.what());
            return nullptr;
        }
    }

    PyObject *py_store_json_string(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *design_obj = nullptr;
        const char *mode_text = "pretty-compact";
        PyObject *top_list_obj = Py_None;
        static const char *kwlist[] = {"design", "mode", "top", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|sO", const_cast<char **>(kwlist),
                                         &design_obj, &mode_text, &top_list_obj))
        {
            return nullptr;
        }

        auto *design = getDesign(design_obj);
        if (!design)
        {
            return nullptr;
        }

        bool ok = false;
        const auto mode = parseJsonMode(mode_text, ok);
        if (!ok)
        {
            PyErr_SetString(PyExc_ValueError, "unknown json mode");
            return nullptr;
        }

        std::vector<std::string> top_names;
        std::string error;
        if (!parseStringList(top_list_obj, top_names, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        wolvrix::lib::store::StoreDiagnostics diagnostics;
        wolvrix::lib::store::StoreJson store(&diagnostics);
        wolvrix::lib::store::StoreOptions options;
        options.jsonMode = mode;
        options.topOverrides = std::move(top_names);

        auto text = store.storeToString(*design, options);
        if (!text || diagnostics.hasError())
        {
            const std::string diagText =
                formatDiagnostics(diagnostics.messages(), getDesignSourceManager(design_obj));
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }

        return PyUnicode_FromStringAndSize(text->data(), static_cast<Py_ssize_t>(text->size()));
    }

    PyObject *py_write_sv(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *design_obj = nullptr;
        const char *output = nullptr;
        PyObject *top_list_obj = Py_None;
        static const char *kwlist[] = {"design", "output", "top", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|O", const_cast<char **>(kwlist),
                                         &design_obj, &output, &top_list_obj))
        {
            return nullptr;
        }
        auto *design = getDesign(design_obj);
        if (!design)
        {
            return nullptr;
        }

        std::vector<std::string> top_names;
        std::string error;
        if (!parseStringList(top_list_obj, top_names, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        wolvrix::lib::emit::EmitDiagnostics diagnostics;
        wolvrix::lib::emit::EmitSystemVerilog emitter(&diagnostics);
        wolvrix::lib::emit::EmitOptions options;
        std::filesystem::path out_path(output);
        options.outputFilename = out_path.filename().string();
        if (!out_path.parent_path().empty())
        {
            options.outputDir = out_path.parent_path().string();
        }
        options.topOverrides = std::move(top_names);

        const auto result = emitter.emit(*design, options);
        if (diagnostics.hasError() || !result.success)
        {
            const std::string diagText =
                formatDiagnostics(diagnostics.messages(), getDesignSourceManager(design_obj));
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }

        Py_RETURN_NONE;
    }

    PyObject *py_run_pass(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *design_obj = nullptr;
        const char *pass_name = nullptr;
        PyObject *pass_args_obj = Py_None;
        int dryrun = 0;
        const char *diag_text = "warn";
        static const char *kwlist[] = {"design", "name", "args", "dryrun", "diagnostics", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|Ops", const_cast<char **>(kwlist),
                                         &design_obj, &pass_name, &pass_args_obj, &dryrun, &diag_text))
        {
            return nullptr;
        }
        wolvrix::lib::LogLevel diag_level = wolvrix::lib::LogLevel::Warn;
        if (!parseDiagnosticsLevel(diag_text, diag_level))
        {
            PyErr_SetString(PyExc_ValueError, "unknown diagnostics level");
            return nullptr;
        }
        if (!pass_name || pass_name[0] == '\0')
        {
            PyErr_SetString(PyExc_ValueError, "pass name must be non-empty");
            return nullptr;
        }
        auto *design = getDesign(design_obj);
        if (!design)
        {
            return nullptr;
        }

        std::vector<std::string> pass_args_storage;
        std::string error;
        if (!parseStringList(pass_args_obj, pass_args_storage, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }
        std::vector<std::string_view> pass_args;
        pass_args.reserve(pass_args_storage.size());
        for (const auto &arg : pass_args_storage)
        {
            pass_args.emplace_back(arg);
        }

        auto pass = wolvrix::lib::transform::makePass(pass_name, pass_args, error);
        if (!pass)
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        wolvrix::lib::transform::PassDiagnostics diagnostics;
        wolvrix::lib::transform::PassManager manager;
        manager.addPass(std::move(pass));

        wolvrix::lib::transform::PassManagerResult result;
        if (dryrun)
        {
            wolvrix::lib::grh::Design temp = design->clone();
            result = manager.run(temp, diagnostics);
        }
        else
        {
            result = manager.run(*design, diagnostics);
        }

        if (diagnostics.hasError() || !result.success)
        {
            const auto *sourceManager = getDesignSourceManager(design_obj);
            emitDiagnostics(diagnostics.messages(), diag_level, sourceManager);
            const std::string diagText = formatDiagnostics(diagnostics.messages(), sourceManager);
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }
        emitDiagnostics(diagnostics.messages(), diag_level, getDesignSourceManager(design_obj));

        return PyBool_FromLong(result.changed ? 1 : 0);
    }

    PyObject *py_run_pipeline(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *design_obj = nullptr;
        PyObject *pipeline_obj = Py_None;
        int dryrun = 0;
        const char *diag_text = "warn";
        static const char *kwlist[] = {"design", "pipeline", "dryrun", "diagnostics", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|ps", const_cast<char **>(kwlist),
                                         &design_obj, &pipeline_obj, &dryrun, &diag_text))
        {
            return nullptr;
        }

        wolvrix::lib::LogLevel diag_level = wolvrix::lib::LogLevel::Warn;
        if (!parseDiagnosticsLevel(diag_text, diag_level))
        {
            PyErr_SetString(PyExc_ValueError, "unknown diagnostics level");
            return nullptr;
        }

        auto *design = getDesign(design_obj);
        if (!design)
        {
            return nullptr;
        }

        std::vector<PassSpec> pipeline;
        std::string parseError;
        if (!parsePassPipeline(pipeline_obj, pipeline, parseError))
        {
            PyErr_SetString(PyExc_ValueError, parseError.c_str());
            return nullptr;
        }

        wolvrix::lib::transform::PassDiagnostics diagnostics;
        wolvrix::lib::transform::PassManager manager;

        for (const auto &spec : pipeline)
        {
            std::vector<std::string_view> passArgs;
            passArgs.reserve(spec.args.size());
            for (const auto &arg : spec.args)
            {
                passArgs.emplace_back(arg);
            }

            std::string makeErr;
            auto pass = wolvrix::lib::transform::makePass(spec.name, passArgs, makeErr);
            if (!pass)
            {
                const std::string err = "invalid pass '" + spec.name + "': " + makeErr;
                PyErr_SetString(PyExc_ValueError, err.c_str());
                return nullptr;
            }
            manager.addPass(std::move(pass));
        }

        wolvrix::lib::transform::PassManagerResult result;
        if (dryrun)
        {
            wolvrix::lib::grh::Design temp = design->clone();
            result = manager.run(temp, diagnostics);
        }
        else
        {
            result = manager.run(*design, diagnostics);
        }

        if (diagnostics.hasError() || !result.success)
        {
            const auto *sourceManager = getDesignSourceManager(design_obj);
            emitDiagnostics(diagnostics.messages(), diag_level, sourceManager);
            const std::string diagText = formatDiagnostics(diagnostics.messages(), sourceManager);
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }
        emitDiagnostics(diagnostics.messages(), diag_level, getDesignSourceManager(design_obj));

        return PyBool_FromLong(result.changed ? 1 : 0);
    }

    PyObject *py_list_passes(PyObject * /*self*/, PyObject * /*args*/)
    {
        const auto passes = wolvrix::lib::transform::availableTransformPasses();
        PyObject *list = PyList_New(static_cast<Py_ssize_t>(passes.size()));
        if (!list)
        {
            return nullptr;
        }
        for (std::size_t i = 0; i < passes.size(); ++i)
        {
            PyObject *item = PyUnicode_FromString(passes[i].c_str());
            if (!item)
            {
                Py_DECREF(list);
                return nullptr;
            }
            PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), item);
        }
        return list;
    }

} // namespace

static PyMethodDef WolvrixMethods[] = {
    {"read_sv", reinterpret_cast<PyCFunction>(py_read_sv), METH_VARARGS | METH_KEYWORDS,
     "read_sv(path, slang_args=None, log_level='warn', diagnostics='warn') -> Design capsule"},
    {"read_json", reinterpret_cast<PyCFunction>(py_read_json), METH_VARARGS | METH_KEYWORDS,
     "read_json(path) -> Design capsule"},
    {"load_json_string", reinterpret_cast<PyCFunction>(py_load_json_string), METH_VARARGS | METH_KEYWORDS,
     "load_json_string(text) -> Design capsule"},
    {"store_json_string", reinterpret_cast<PyCFunction>(py_store_json_string), METH_VARARGS | METH_KEYWORDS,
     "store_json_string(design, mode='pretty-compact', top=None) -> str"},
    {"write_sv", reinterpret_cast<PyCFunction>(py_write_sv), METH_VARARGS | METH_KEYWORDS,
     "write_sv(design, output, top=None)"},
    {"run_pass", reinterpret_cast<PyCFunction>(py_run_pass), METH_VARARGS | METH_KEYWORDS,
     "run_pass(design, name, args=None, dryrun=False, diagnostics='warn') -> bool"},
    {"run_pipeline", reinterpret_cast<PyCFunction>(py_run_pipeline), METH_VARARGS | METH_KEYWORDS,
     "run_pipeline(design, pipeline, dryrun=False, diagnostics='warn') -> bool"},
    {"list_passes", reinterpret_cast<PyCFunction>(py_list_passes), METH_NOARGS,
     "list_passes() -> list[str]"},
    {nullptr, nullptr, 0, nullptr},
};

static PyModuleDef WolvrixModule = {
    PyModuleDef_HEAD_INIT,
    "_wolvrix",
    "Wolvrix native bindings",
    -1,
    WolvrixMethods,
};

PyMODINIT_FUNC PyInit__wolvrix(void)
{
    return PyModule_Create(&WolvrixModule);
}
