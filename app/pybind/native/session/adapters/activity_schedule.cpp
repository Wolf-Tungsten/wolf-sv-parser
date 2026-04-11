#include "native/session/adapters/export.hpp"

#include "core/transform.hpp"
#include "transform/activity_schedule.hpp"

namespace wolvrix::app::pybind
{

    namespace
    {

        PyObject *exportActivityScheduleSupernodeToOps(const wolvrix::lib::transform::SessionSlot &slot,
                                                       std::string_view key,
                                                       std::string_view view)
        {
            if (view != "python")
            {
                PyErr_Format(PyExc_ValueError,
                             "unsupported activity-schedule export view: %s",
                             std::string(view).c_str());
                return nullptr;
            }

            const auto *typed =
                dynamic_cast<const wolvrix::lib::transform::SessionSlotValue<
                    wolvrix::lib::transform::ActivityScheduleSupernodeToOps> *>(&slot);
            if (!typed)
            {
                PyErr_Format(PyExc_TypeError,
                             "activity-schedule session value has unexpected native type: %s",
                             std::string(key).c_str());
                return nullptr;
            }

            const auto &value = typed->value;
            PyObject *outer = PyList_New(static_cast<Py_ssize_t>(value.size()));
            if (!outer)
            {
                return nullptr;
            }

            for (Py_ssize_t superIdx = 0; superIdx < static_cast<Py_ssize_t>(value.size()); ++superIdx)
            {
                const auto &ops = value[static_cast<std::size_t>(superIdx)];
                PyObject *inner = PyList_New(static_cast<Py_ssize_t>(ops.size()));
                if (!inner)
                {
                    Py_DECREF(outer);
                    return nullptr;
                }
                for (Py_ssize_t opIdx = 0; opIdx < static_cast<Py_ssize_t>(ops.size()); ++opIdx)
                {
                    PyObject *item = PyLong_FromUnsignedLong(
                        static_cast<unsigned long>(ops[static_cast<std::size_t>(opIdx)].index));
                    if (!item)
                    {
                        Py_DECREF(inner);
                        Py_DECREF(outer);
                        return nullptr;
                    }
                    PyList_SET_ITEM(inner, opIdx, item);
                }
                PyList_SET_ITEM(outer, superIdx, inner);
            }

            return outer;
        }

        const bool kActivityScheduleExporterRegistered = []() {
            registerSessionNativeExporter("activity-schedule.supernode-to-ops",
                                          exportActivityScheduleSupernodeToOps);
            return true;
        }();

    } // namespace

} // namespace wolvrix::app::pybind
