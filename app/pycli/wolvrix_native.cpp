#include <Python.h>

#include "emit.hpp"
#include "grh.hpp"
#include "ingest.hpp"
#include "logging.hpp"
#include "store.hpp"
#include "transform.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/driver/Driver.h"

#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

    constexpr const char *kDesignCapsuleName = "wolvrix.Design";

    void destroyDesignCapsule(PyObject *capsule)
    {
        auto *design = static_cast<wolvrix::lib::grh::Design *>(
            PyCapsule_GetPointer(capsule, kDesignCapsuleName));
        delete design;
    }

    wolvrix::lib::grh::Design *getDesign(PyObject *capsule)
    {
        return static_cast<wolvrix::lib::grh::Design *>(
            PyCapsule_GetPointer(capsule, kDesignCapsuleName));
    }

    PyObject *makeDesignCapsule(wolvrix::lib::grh::Design design)
    {
        auto *heapDesign = new wolvrix::lib::grh::Design(std::move(design));
        return PyCapsule_New(heapDesign, kDesignCapsuleName, destroyDesignCapsule);
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

    std::string formatDiagnostics(const std::vector<wolvrix::lib::diag::Diagnostic> &messages)
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
            out.append(diagnosticKindText(message.kind));
            out.append(" ");
            if (!message.passName.empty())
            {
                out.append(message.passName);
                out.append(": ");
            }
            out.append(message.message);
            if (!message.context.empty())
            {
                out.append(" (");
                out.append(message.context);
                out.append(")");
            }
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

    PyObject *py_read_sv(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *path_obj = nullptr;
        PyObject *slang_args_obj = Py_None;
        const char *log_level_text = "warn";
        static const char *kwlist[] = {"path", "slang_args", "log_level", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Os",
                                         const_cast<char **>(kwlist),
                                         &path_obj, &slang_args_obj, &log_level_text))
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
            const std::string diagText = formatDiagnostics(converter.diagnostics().messages());
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }
        if (converter.diagnostics().hasError())
        {
            const std::string diagText = formatDiagnostics(converter.diagnostics().messages());
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }

        return makeDesignCapsule(std::move(design));
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
            const std::string diagText = formatDiagnostics(diagnostics.messages());
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
            const std::string diagText = formatDiagnostics(diagnostics.messages());
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
        static const char *kwlist[] = {"design", "name", "args", "dryrun", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|Op", const_cast<char **>(kwlist),
                                         &design_obj, &pass_name, &pass_args_obj, &dryrun))
        {
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
            const std::string diagText = formatDiagnostics(diagnostics.messages());
            PyErr_SetString(PyExc_RuntimeError, diagText.c_str());
            return nullptr;
        }

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
     "read_sv(path, slang_args=None, log_level='warn') -> Design capsule"},
    {"read_json", reinterpret_cast<PyCFunction>(py_read_json), METH_VARARGS | METH_KEYWORDS,
     "read_json(path) -> Design capsule"},
    {"load_json_string", reinterpret_cast<PyCFunction>(py_load_json_string), METH_VARARGS | METH_KEYWORDS,
     "load_json_string(text) -> Design capsule"},
    {"store_json_string", reinterpret_cast<PyCFunction>(py_store_json_string), METH_VARARGS | METH_KEYWORDS,
     "store_json_string(design, mode='pretty-compact', top=None) -> str"},
    {"write_sv", reinterpret_cast<PyCFunction>(py_write_sv), METH_VARARGS | METH_KEYWORDS,
     "write_sv(design, output, top=None)"},
    {"run_pass", reinterpret_cast<PyCFunction>(py_run_pass), METH_VARARGS | METH_KEYWORDS,
     "run_pass(design, name, args=None, dryrun=False) -> bool"},
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
