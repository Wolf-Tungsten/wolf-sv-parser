#include "native/module/methods.hpp"

#include "native/diagnostics/to_python.hpp"
#include "native/session/storage.hpp"

#include <exception>
#include <string>

namespace wolvrix::app::pybind
{

    PyObject *py_session_read_json_file(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *sessionObj = nullptr;
        const char *path = nullptr;
        const char *targetDesignKey = nullptr;
        int replace = 0;
        static const char *kwlist[] = {"session", "path", "target_design_key", "replace", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|p", const_cast<char **>(kwlist),
                                         &sessionObj, &path, &targetDesignKey, &replace))
        {
            return nullptr;
        }
        SessionHandle *session = getSessionHandle(sessionObj);
        if (!session)
        {
            return nullptr;
        }
        std::string insertError;
        if (!ensureSessionInsertable(*session, targetDesignKey, replace != 0, insertError))
        {
            PyErr_SetString(PyExc_KeyError, insertError.c_str());
            return nullptr;
        }

        std::string contents;
        std::string readError;
        if (!readFileText(path, contents, readError))
        {
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     readError, "read_json_file"),
                                    nullptr);
        }

        try
        {
            auto design = wolvrix::lib::grh::Design::fromJsonString(contents);
            if (replace)
            {
                sessionEraseKey(*session, targetDesignKey);
            }
            session->designs.insert_or_assign(std::string(targetDesignKey),
                                              DesignHandle{std::move(design), {}});
            return makeActionResult(true, {}, nullptr);
        }
        catch (const std::exception &ex)
        {
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     ex.what(), "read_json_file"),
                                    nullptr);
        }
    }

    PyObject *py_session_load_json_text(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *sessionObj = nullptr;
        const char *text = nullptr;
        const char *targetDesignKey = nullptr;
        int replace = 0;
        static const char *kwlist[] = {"session", "text", "target_design_key", "replace", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|p", const_cast<char **>(kwlist),
                                         &sessionObj, &text, &targetDesignKey, &replace))
        {
            return nullptr;
        }
        SessionHandle *session = getSessionHandle(sessionObj);
        if (!session)
        {
            return nullptr;
        }
        std::string insertError;
        if (!ensureSessionInsertable(*session, targetDesignKey, replace != 0, insertError))
        {
            PyErr_SetString(PyExc_KeyError, insertError.c_str());
            return nullptr;
        }

        try
        {
            auto design = wolvrix::lib::grh::Design::fromJsonString(text);
            if (replace)
            {
                sessionEraseKey(*session, targetDesignKey);
            }
            session->designs.insert_or_assign(std::string(targetDesignKey),
                                              DesignHandle{std::move(design), {}});
            return makeActionResult(true, {}, nullptr);
        }
        catch (const std::exception &ex)
        {
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     ex.what(), "load_json_text"),
                                    nullptr);
        }
    }

} // namespace wolvrix::app::pybind
