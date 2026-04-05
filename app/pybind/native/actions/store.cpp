#include "native/module/methods.hpp"

#include "core/store.hpp"
#include "native/diagnostics/to_python.hpp"
#include "native/session/storage.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace wolvrix::app::pybind
{

    PyObject *py_session_store_json(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *sessionObj = nullptr;
        const char *designKey = nullptr;
        const char *output = nullptr;
        const char *modeText = "pretty-compact";
        PyObject *topListObj = Py_None;
        static const char *kwlist[] = {"session", "design", "output", "mode", "top", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|sO", const_cast<char **>(kwlist),
                                         &sessionObj, &designKey, &output, &modeText, &topListObj))
        {
            return nullptr;
        }
        SessionHandle *session = getSessionHandle(sessionObj);
        if (!session)
        {
            return nullptr;
        }
        auto *design = sessionDesign(*session, designKey);
        if (!design)
        {
            PyErr_Format(PyExc_KeyError, "design key not found: %s", designKey);
            return nullptr;
        }

        bool ok = false;
        const auto mode = parseJsonMode(modeText, ok);
        if (!ok)
        {
            PyErr_SetString(PyExc_ValueError, "unknown json mode");
            return nullptr;
        }

        std::vector<std::string> topNames;
        std::string error;
        if (!parseStringList(topListObj, topNames, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        const std::filesystem::path outPath(output);
        const auto filename = outPath.filename().string();
        if (filename.empty())
        {
            PyErr_SetString(PyExc_ValueError, "store_json(...) expects an output file path");
            return nullptr;
        }

        wolvrix::lib::store::StoreDiagnostics diagnostics;
        wolvrix::lib::store::StoreJson store(&diagnostics);
        wolvrix::lib::store::StoreOptions options;
        options.jsonMode = mode;
        options.topOverrides = std::move(topNames);
        options.outputFilename = filename;
        if (!outPath.parent_path().empty())
        {
            options.outputDir = outPath.parent_path().string();
        }
        const auto result = store.store(*design, options);
        return makeActionResult(result.success && !diagnostics.hasError(),
                                diagnostics.messages(),
                                sessionDesignSourceManager(*session, designKey));
    }

} // namespace wolvrix::app::pybind
