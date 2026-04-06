#ifndef WOLVRIX_APP_PYBIND_NATIVE_SESSION_ADAPTER_EXPORT_HPP
#define WOLVRIX_APP_PYBIND_NATIVE_SESSION_ADAPTER_EXPORT_HPP

#include <Python.h>

#include "native/session/state.hpp"

#include <string>
#include <string_view>

namespace wolvrix::app::pybind
{

    using SessionNativeExportFn =
        PyObject *(*)(const wolvrix::lib::transform::SessionSlot &slot,
                      std::string_view key,
                      std::string_view view);

    void registerSessionNativeExporter(std::string kind, SessionNativeExportFn exporter);
    PyObject *exportSessionNativeValue(const SessionHandle &session,
                                       std::string_view key,
                                       std::string_view view);

} // namespace wolvrix::app::pybind

#endif // WOLVRIX_APP_PYBIND_NATIVE_SESSION_ADAPTER_EXPORT_HPP
