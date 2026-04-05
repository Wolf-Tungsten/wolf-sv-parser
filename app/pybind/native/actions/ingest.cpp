#include "native/module/methods.hpp"

#include "core/ingest.hpp"
#include "native/diagnostics/to_python.hpp"
#include "native/session/storage.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace wolvrix::app::pybind
{

    PyObject *py_session_read_sv(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *sessionObj = nullptr;
        PyObject *pathObj = nullptr;
        const char *targetDesignKey = nullptr;
        PyObject *slangArgsObj = Py_None;
        int replace = 0;
        const char *logLevelText = "info";
        static const char *kwlist[] = {
            "session", "path", "target_design_key", "slang_args", "replace", "log_level", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOs|Ops", const_cast<char **>(kwlist),
                                         &sessionObj, &pathObj, &targetDesignKey,
                                         &slangArgsObj, &replace, &logLevelText))
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

        const char *path = nullptr;
        if (pathObj == Py_None)
        {
            path = nullptr;
        }
        else if (PyUnicode_Check(pathObj))
        {
            path = PyUnicode_AsUTF8(pathObj);
            if (!path)
            {
                return nullptr;
            }
            if (path[0] == '\0')
            {
                PyErr_SetString(PyExc_ValueError, "path must be a non-empty string or None");
                return nullptr;
            }
        }
        else
        {
            PyErr_SetString(PyExc_ValueError, "path must be a string or None");
            return nullptr;
        }

        std::vector<std::string> slangArgs;
        std::string error;
        if (!parseStringList(slangArgsObj, slangArgs, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }

        bool ok = false;
        const wolvrix::lib::LogLevel logLevel = parseLogLevel(logLevelText, ok);
        if (!ok)
        {
            PyErr_SetString(PyExc_ValueError, "unknown log_level");
            return nullptr;
        }

        std::vector<std::string> argvStorage;
        argvStorage.reserve(2 + slangArgs.size());
        argvStorage.emplace_back("read_sv");
        if (path)
        {
            argvStorage.emplace_back(path);
        }
        for (const auto &arg : slangArgs)
        {
            argvStorage.push_back(arg);
        }
        std::vector<const char *> argv;
        argv.reserve(argvStorage.size());
        for (const auto &arg : argvStorage)
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
            (void)driver.reportDiagnostics(/* quiet */ true);
        };

        if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()))
        {
            reportSlangDiagnostics();
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     "failed to parse slang options", "read_sv"),
                                    nullptr);
        }
        if (!driver.processOptions())
        {
            reportSlangDiagnostics();
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     "failed to apply slang options", "read_sv"),
                                    nullptr);
        }
        if (!driver.parseAllSources())
        {
            reportSlangDiagnostics();
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     "failed to parse sources", "read_sv"),
                                    nullptr);
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
            driver.reportCompilation(*compilation, /* quiet */ true);
            reportSlangDiagnostics();
        }
        if (hasSlangErrors)
        {
            return makeActionResult(false,
                                    singleDiagnostic(wolvrix::lib::diag::DiagnosticKind::Error,
                                                     "slang reported errors; see stderr diagnostics",
                                                     "read_sv"),
                                    compilation->getSourceManager());
        }

        wolvrix::lib::ingest::ConvertOptions convertOptions;
        convertOptions.abortOnError = true;
        convertOptions.enableLogging = logLevel != wolvrix::lib::LogLevel::Off;
        convertOptions.logLevel = logLevel;

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
            return makeActionResult(false,
                                    converter.diagnostics().messages(),
                                    compilation->getSourceManager());
        }

        converter.diagnostics().flushThreadLocal();
        const bool success = !converter.diagnostics().hasError();
        if (success)
        {
            if (replace)
            {
                sessionEraseKey(*session, targetDesignKey);
            }
            session->designs.insert_or_assign(
                std::string(targetDesignKey),
                DesignHandle{std::move(design), std::move(compilation)});
        }
        return makeActionResult(success,
                                converter.diagnostics().messages(),
                                success ? sessionDesignSourceManager(*session, targetDesignKey)
                                        : compilation->getSourceManager());
    }

    PyObject *py_session_clone_design(PyObject * /*self*/, PyObject *args, PyObject *kwargs)
    {
        PyObject *sessionObj = nullptr;
        const char *src = nullptr;
        const char *dst = nullptr;
        int replace = 0;
        static const char *kwlist[] = {"session", "src", "dst", "replace", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|p", const_cast<char **>(kwlist),
                                         &sessionObj, &src, &dst, &replace))
        {
            return nullptr;
        }
        SessionHandle *session = getSessionHandle(sessionObj);
        if (!session)
        {
            return nullptr;
        }
        auto srcIt = session->designs.find(src);
        if (srcIt == session->designs.end())
        {
            PyErr_Format(PyExc_KeyError, "design key not found: %s", src);
            return nullptr;
        }
        std::string insertError;
        if (!ensureSessionInsertable(*session, dst, replace != 0, insertError))
        {
            PyErr_SetString(PyExc_KeyError, insertError.c_str());
            return nullptr;
        }
        if (replace)
        {
            sessionEraseKey(*session, dst);
        }
        session->designs.insert_or_assign(std::string(dst),
                                          DesignHandle{srcIt->second.design.clone(),
                                                       srcIt->second.compilation});
        return makeActionResult(true, {}, sessionDesignSourceManager(*session, dst));
    }

} // namespace wolvrix::app::pybind
