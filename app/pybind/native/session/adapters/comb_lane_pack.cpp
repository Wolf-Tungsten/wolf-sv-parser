#include "native/session/adapters/export.hpp"

#include "core/transform.hpp"
#include "transform/comb_lane_pack.hpp"

#include <string>

namespace wolvrix::app::pybind
{

    namespace
    {

        PyObject *exportCombLanePackReports(const wolvrix::lib::transform::SessionSlot &slot,
                                            std::string_view key,
                                            std::string_view view)
        {
            if (view != "python")
            {
                PyErr_Format(PyExc_ValueError,
                             "unsupported comb-lane-pack export view: %s",
                             std::string(view).c_str());
                return nullptr;
            }

            const auto *typed =
                dynamic_cast<const wolvrix::lib::transform::SessionSlotValue<
                    std::vector<wolvrix::lib::transform::CombLanePackReport>> *>(&slot);
            if (!typed)
            {
                PyErr_Format(PyExc_TypeError,
                             "comb-lane-pack session value has unexpected native type: %s",
                             std::string(key).c_str());
                return nullptr;
            }

            PyObject *out = PyList_New(static_cast<Py_ssize_t>(typed->value.size()));
            if (!out)
            {
                return nullptr;
            }

            for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(typed->value.size()); ++i)
            {
                const auto &report = typed->value[static_cast<std::size_t>(i)];
                PyObject *item = PyDict_New();
                if (!item)
                {
                    Py_DECREF(out);
                    return nullptr;
                }

                auto setString = [&](const char *name, const std::string &value) -> bool {
                    PyObject *pyValue = PyUnicode_FromStringAndSize(value.data(),
                                                                    static_cast<Py_ssize_t>(value.size()));
                    if (!pyValue)
                    {
                        return false;
                    }
                    const int status = PyDict_SetItemString(item, name, pyValue);
                    Py_DECREF(pyValue);
                    return status == 0;
                };
                auto setSize = [&](const char *name, std::size_t value) -> bool {
                    PyObject *pyValue = PyLong_FromUnsignedLongLong(
                        static_cast<unsigned long long>(value));
                    if (!pyValue)
                    {
                        return false;
                    }
                    const int status = PyDict_SetItemString(item, name, pyValue);
                    Py_DECREF(pyValue);
                    return status == 0;
                };
                auto setList = [&](const char *name, const std::vector<std::string> &values) -> bool {
                    PyObject *list = PyList_New(static_cast<Py_ssize_t>(values.size()));
                    if (!list)
                    {
                        return false;
                    }
                    for (Py_ssize_t idx = 0; idx < static_cast<Py_ssize_t>(values.size()); ++idx)
                    {
                        const auto &text = values[static_cast<std::size_t>(idx)];
                        PyObject *pyItem = PyUnicode_FromStringAndSize(text.data(),
                                                                       static_cast<Py_ssize_t>(text.size()));
                        if (!pyItem)
                        {
                            Py_DECREF(list);
                            return false;
                        }
                        PyList_SET_ITEM(list, idx, pyItem);
                    }
                    const int status = PyDict_SetItemString(item, name, list);
                    Py_DECREF(list);
                    return status == 0;
                };

                if (!setString("graph_name", report.graphName) ||
                    !setString("root_source", report.rootSource) ||
                    !setList("root_symbols", report.rootSymbols) ||
                    !setString("anchor_kind", report.anchorKind) ||
                    !setList("anchor_symbols", report.anchorSymbols) ||
                    !setList("storage_target_symbols", report.storageTargetSymbols) ||
                    !setSize("group_size", report.groupSize) ||
                    !setSize("lane_width", report.laneWidth) ||
                    !setSize("packed_width", report.packedWidth) ||
                    !setSize("tree_nodes", report.treeNodes) ||
                    !setString("root_kind", report.rootKind) ||
                    !setString("signature", report.signature) ||
                    !setSize("packed_root_value_id", report.packedRootValueId) ||
                    !setString("description", report.description))
                {
                    Py_DECREF(item);
                    Py_DECREF(out);
                    return nullptr;
                }

                PyList_SET_ITEM(out, i, item);
            }

            return out;
        }

        const bool kCombLanePackExporterRegistered = []() {
            registerSessionNativeExporter("comb-lane-pack.reports", exportCombLanePackReports);
            return true;
        }();

    } // namespace

} // namespace wolvrix::app::pybind
