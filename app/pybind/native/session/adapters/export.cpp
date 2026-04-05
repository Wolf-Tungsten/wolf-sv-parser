#include "native/session/adapters/export.hpp"

#include <unordered_map>

namespace wolvrix::app::pybind
{

    namespace
    {

        using SessionNativeExportRegistry = std::unordered_map<std::string, SessionNativeExportFn>;

        SessionNativeExportRegistry &nativeExportRegistry()
        {
            static SessionNativeExportRegistry registry;
            return registry;
        }

    } // namespace

    void registerSessionNativeExporter(std::string kind, SessionNativeExportFn exporter)
    {
        nativeExportRegistry().insert_or_assign(std::move(kind), exporter);
    }

    PyObject *exportSessionNativeValue(const SessionHandle &session,
                                       std::string_view key,
                                       std::string_view view)
    {
        const auto it = session.nativeValues.find(std::string(key));
        if (it == session.nativeValues.end() || !it->second)
        {
            PyErr_Format(PyExc_KeyError, "session key is not a native value: %s",
                         std::string(key).c_str());
            return nullptr;
        }

        const auto kind = std::string(it->second->kind());
        const auto exporterIt = nativeExportRegistry().find(kind);
        if (exporterIt == nativeExportRegistry().end())
        {
            PyErr_Format(PyExc_TypeError,
                         "session key does not have a Python adapter exporter: %s (kind=%s)",
                         std::string(key).c_str(),
                         kind.c_str());
            return nullptr;
        }

        return exporterIt->second(*it->second, key, view);
    }

} // namespace wolvrix::app::pybind
