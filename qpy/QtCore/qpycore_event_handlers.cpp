// This is the implementation of the event handlers.
//
// Copyright (c) 2026 Riverbank Computing Limited <info@riverbankcomputing.com>
// 
// This file is part of PyQt6.
// 
// This file may be used under the terms of the GNU General Public License
// version 3.0 as published by the Free Software Foundation and appearing in
// the file LICENSE included in the packaging of this file.  Please review the
// following information to ensure the GNU General Public License version 3.0
// requirements will be met: http://www.gnu.org/copyleft/gpl.html.
// 
// If you do not wish to use this file under the terms of the GPL version 3.0
// then you may purchase a commercial license.  For more information contact
// info@riverbankcomputing.com.
// 
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.


#include "qpycore_event_handlers.h"

#include <QMutexLocker>

#include "sipAPIQtCore.h"


#if SIP_ABI_MAJOR_VERSION >= 14

#include <memory>

#include <QHash>
#include <QMutex>
#include <QObject>

namespace {

class QObjectMonitor : public std::enable_shared_from_this<QObjectMonitor>
{
public:
    QObjectMonitor(PyInterpreterView *view, PyObject *module_ref)
        : view(view), module_ref(module_ref)
    {
    }

    ~QObjectMonitor()
    {
        PyInterpreterView_Close(view);
    }

    void watch(QObject *object)
    {
        {
            QMutexLocker lock(&mutex);
            if (!enabled)
                return;

            auto watcher = watchers.find(object);
            if (watcher != watchers.end())
            {
                watcher.value() = true;
                return;
            }
            watchers.insert(object, true);
        }

        QObject::connect(object, &QObject::destroyed,
                [watcher = weak_from_this()](QObject *destroyed) {
                    if (auto monitor = watcher.lock())
                        monitor->destroyed(destroyed);
                });
    }

    void ignore(QObject *object)
    {
        QMutexLocker lock(&mutex);
        auto watcher = watchers.find(object);
        if (watcher != watchers.end())
            watcher.value() = false;
    }

    void disable()
    {
        QMutexLocker lock(&mutex);
        enabled = false;
        watchers.clear();
    }

    void clear()
    {
        PyObject *previous;
        {
            QMutexLocker lock(&mutex);
            enabled = false;
            watchers.clear();
            previous = module_ref;
            module_ref = nullptr;
        }
        Py_XDECREF(previous);
    }

private:
    void destroyed(QObject *object)
    {
        {
            QMutexLocker lock(&mutex);
            if (!watchers.take(object))
                return;
        }

        PyThreadStateToken *thread = PyThreadState_EnsureFromView(view);
        if (!thread)
            return;

        PyObject *saved_exception = PyErr_GetRaisedException();
        PyObject *module = nullptr;
        {
            QMutexLocker lock(&mutex);
            if (module_ref && PyWeakref_GetRef(module_ref, &module) < 0)
                PyErr_Clear();
        }
        if (module)
        {
            auto *sipMS = static_cast<sipModuleState *>(PyModule_GetState(module));
            PyObject *wrapper = sipMS ? sipGetPyObjectRef(object, sipType_QObject) : nullptr;
            if (wrapper)
            {
                auto *instance = reinterpret_cast<sipSimpleWrapper *>(wrapper);
                sipInstanceDestroyed(sipMS, &instance);
                Py_DECREF(wrapper);
            }
            if (PyErr_Occurred())
                PyErr_WriteUnraisable(module);
            Py_DECREF(module);
        }

        PyErr_SetRaisedException(saved_exception);
        PyThreadState_Release(thread);
    }

    QMutex mutex;
    QHash<QObject *, bool> watchers;
    bool enabled = true;
    PyInterpreterView *const view;
    PyObject *module_ref;
};

using MonitorOwner = std::shared_ptr<QObjectMonitor>;

static QObjectMonitor *get_monitor(sipModuleState *sipMS)
{
    auto *owner = static_cast<MonitorOwner *>(sipGetModuleUserState());
    return owner ? owner->get() : nullptr;
}

static void wrapped_instance_eh(sipModuleState *sipMS, sipTypeID, void *cpp)
{
    if (auto *monitor = get_monitor(sipMS))
        monitor->watch(static_cast<QObject *>(cpp));
}

static void collecting_wrapper_eh(sipModuleState *sipMS, sipTypeID,
        sipSimpleWrapper *self)
{
    if (auto *monitor = get_monitor(sipMS))
    {
        if (void *address = sipGetAddress(self))
            monitor->ignore(static_cast<QObject *>(address));
    }
}

static int clear_monitor(sipModuleState *, void *data)
{
    auto *owner = static_cast<MonitorOwner *>(data);
    (*owner)->clear();
    return 0;
}

static void free_monitor(sipModuleState *sipMS, void *data)
{
    clear_monitor(sipMS, data);
    delete static_cast<MonitorOwner *>(data);
}

} // namespace

int qpycore_register_event_handlers(sipModuleState *sipMS, PyObject *module)
{
    static const sipEventHandlerSpec handlers[] = {
        {sipEventWrappedInstance, sipType_QObject, reinterpret_cast<void *>(wrapped_instance_eh)},
        {sipEventCollectingWrapper, sipType_QObject, reinterpret_cast<void *>(collecting_wrapper_eh)},
        {sipEventNone, 0, nullptr}
    };

    PyObject *module_ref = PyWeakref_NewRef(module, nullptr);
    if (!module_ref)
        return -1;

    PyInterpreterView *view = PyInterpreterView_FromCurrent();
    if (!view)
    {
        Py_DECREF(module_ref);
        return -1;
    }

    auto monitor = std::make_shared<QObjectMonitor>(view, module_ref);
    if (sipRegisterEventHandlers(handlers) < 0)
    {
        monitor->clear();
        return -1;
    }

    sipSetModuleUserState(new MonitorOwner(std::move(monitor)), clear_monitor,
            free_monitor, nullptr);
    return 0;
}

void qpycore_disable_monitoring(sipModuleState *sipMS)
{
    if (auto *monitor = get_monitor(sipMS))
        monitor->disable();
}

#else

// The singleton that monitors the destruction of QObject instances created by
// C++.
PyQtMonitor *PyQtMonitor::instance = SIP_NULLPTR;

// Set if monitoring is enabled.
std::atomic_bool PyQtMonitor::enabled{true};


// Forward declarations.
static void wrapped_instance_eh(void *cpp);
static void collecting_wrapper_eh(sipSimpleWrapper *self);


// Register the event handlers.
void qpycore_register_event_handlers()
{
    // Create the monitor.
    PyQtMonitor::instance = new PyQtMonitor;

    // Register the handlers.
    sipRegisterEventHandler(sipEventWrappedInstance, sipType_QObject,
            (void *)wrapped_instance_eh);
    sipRegisterEventHandler(sipEventCollectingWrapper, sipType_QObject,
            (void *)collecting_wrapper_eh);
}


// Invoked when a QObject that is created by C++ is wrapped.
static void wrapped_instance_eh(void *cpp)
{
    if (PyQtMonitor::instance && PyQtMonitor::enabled)
        PyQtMonitor::instance->monitor(reinterpret_cast<QObject *>(cpp));
}


// Invoked when the Python wrapper of a QObject is garbage collected.
static void collecting_wrapper_eh(sipSimpleWrapper *self)
{
    if (PyQtMonitor::instance && PyQtMonitor::enabled)
        PyQtMonitor::instance->ignore(self);
}


// Monitor a C++ created QObject instance.
void PyQtMonitor::monitor(QObject *cppInst)
{
    // Connect the monitor.
    {
        QMutexLocker lock(&monitored_mutex);
        monitored.insert(cppInst);
    }

    // Note that the C++ instance may be in the process of being destroyed.
    // This will happen if it is the argument to the destroyed() signal.
    // Python will have forgotten about the object (even if it was created by
    // Python) so by the time it gets wrapped again to be passed to the slot
    // it will appear to have been created by C++ and will therefore be subject
    // to monitoring.  Note that subsequently calling disconnect() would cause
    // a crash - this is why we keep a separate record of C++ instances
    // currently being monitored and never explicitly disconnect the monitor.
    Py_BEGIN_ALLOW_THREADS
    connect(cppInst, SIGNAL(destroyed(QObject *)),
            SLOT(on_destroyed(QObject *)), Qt::UniqueConnection);
    Py_END_ALLOW_THREADS
}


// Stop monitoring a C++ created QObject instance.
void PyQtMonitor::ignore(sipSimpleWrapper *pyObj)
{
    void *addr = sipGetAddress(pyObj);

    if (addr)
    {
        // Note the reason given above we do not disconnect the monitor.
        QMutexLocker lock(&monitored_mutex);
        monitored.remove(reinterpret_cast<QObject *>(addr));
    }
}


// Invoked when a monitored C++ created QObject instance is destroyed.
void PyQtMonitor::on_destroyed(QObject *cppInst)
{
    {
        QMutexLocker lock(&monitored_mutex);

        // See if we are currently monitoring this instance.
        if (!monitored.remove(cppInst))
            return;
    }

    if (sipGetInterpreter())
    {
        SIP_BLOCK_THREADS

        PyObject *pyObj = sipGetPyObject(cppInst, sipType_QObject);

        if (pyObj)
            sipInstanceDestroyed((sipSimpleWrapper *)pyObj);

        SIP_UNBLOCK_THREADS
    }
}

#endif
