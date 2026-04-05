#include "native/module/methods.hpp"

#include "core/transform.hpp"
#include "native/diagnostics/to_python.hpp"
#include "native/session/storage.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace wolvrix::app::pybind
{

    PyObject *py_session_run_pass(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *sessionObj = nullptr;
        const char *passName = nullptr;
        const char *designKey = nullptr;
        PyObject *passArgsObj = Py_None;
        int dryrun = 0;
        const char *logLevelText = "warn";
        static const char *kwlist[] = {"session", "name", "design", "args", "dryrun", "log_level", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|Ops", const_cast<char **>(kwlist),
                                         &sessionObj, &passName, &designKey, &passArgsObj, &dryrun, &logLevelText))
        {
            return nullptr;
        }
        SessionHandle *session = getSessionHandle(sessionObj);
        if (!session)
        {
            return nullptr;
        }
        if (!passName || passName[0] == '\0')
        {
            PyErr_SetString(PyExc_ValueError, "pass name must be non-empty");
            return nullptr;
        }
        auto *design = sessionDesign(*session, designKey);
        if (!design)
        {
            PyErr_Format(PyExc_KeyError, "design key not found: %s", designKey);
            return nullptr;
        }

        bool ok = false;
        const wolvrix::lib::LogLevel logLevel = parseLogLevel(logLevelText, ok);
        if (!ok)
        {
            PyErr_SetString(PyExc_ValueError, "unknown log_level");
            return nullptr;
        }

        std::vector<std::string> passArgsStorage;
        std::string error;
        if (!parseStringList(passArgsObj, passArgsStorage, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }
        std::vector<std::string_view> passArgs;
        passArgs.reserve(passArgsStorage.size());
        for (const auto &arg : passArgsStorage)
        {
            passArgs.emplace_back(arg);
        }

        auto pass = wolvrix::lib::transform::makePass(passName, passArgs, error);
        if (!pass)
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        wolvrix::lib::transform::PassDiagnostics diagnostics;
        wolvrix::lib::transform::PassManager manager;
        auto &options = manager.options();
        options.logLevel = logLevel;
        options.scratchpad = dryrun ? nullptr : &session->nativeValues;
        if (logLevel != wolvrix::lib::LogLevel::Off)
        {
            options.logSink = [](wolvrix::lib::LogLevel level,
                                 std::string_view tag,
                                 std::string_view message) {
                (void)level;
                std::string out;
                if (!tag.empty())
                {
                    out.append(tag);
                    out.append(": ");
                }
                out.append(message);
                std::cerr << out << '\n';
            };
        }
        manager.addPass(std::move(pass));

        wolvrix::lib::transform::PassManagerResult result;
        if (dryrun)
        {
            wolvrix::lib::grh::Design tempDesign = design->clone();
            auto tempScratchpad = cloneScratchpadStore(session->nativeValues);
            options.scratchpad = &tempScratchpad;
            result = manager.run(tempDesign, diagnostics);
        }
        else
        {
            result = manager.run(*design, diagnostics);
        }

        return makePassActionResult(result.success,
                                    result.changed,
                                    diagnostics.messages(),
                                    sessionDesignSourceManager(*session, designKey));
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

} // namespace wolvrix::app::pybind
