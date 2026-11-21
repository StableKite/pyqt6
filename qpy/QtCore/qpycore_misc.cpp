// This contains the implementation of various odds and ends.
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


#include <Python.h>

#include <QByteArray>
#include <cstring>
#include <memory>
#include <utility>

#include "qpycore_api.h"
#include "qpycore_misc.h"

#include "sipAPIQtCore.h"
#include "qpycore_qiodevicebuffer.h"


// Convert a borrowed ASCII string object to a QByteArray.
QByteArray qpycore_convert_ASCII(PyObject *str_obj)
{
    if (!str_obj)
        return QByteArray();

    PyObject *bytes;
    if (PyUnicode_Check(str_obj))
    {
        bytes = PyUnicode_AsASCIIString(str_obj);
    }
    else if (PyBytes_Check(str_obj))
    {
        bytes = Py_NewRef(str_obj);
    }
    else
    {
        PyErr_SetString(PyExc_TypeError, "bytes or an ASCII string expected");
        return QByteArray();
    }

    if (!bytes)
        return QByteArray();

    QByteArray converted(PyBytes_AsString(bytes));
    Py_DECREF(bytes);
    return converted;
}


#if SIP_ABI_MAJOR_VERSION >= 14
// Resolve QtCore in the attached interpreter without importing it recursively.
PyObject *qpycore_qtcore_module_ref(sipModuleState *expected)
{
    PyObject *name = PyUnicode_FromString("PyQt6.QtCore");
    if (!name)
        return nullptr;

    PyObject *module = PyImport_GetModule(name);
    Py_DECREF(name);
    if (!module || (expected && PyModule_GetState(module) != expected))
    {
        Py_XDECREF(module);
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_RuntimeError, "QtCore module state is unavailable");
        return nullptr;
    }

    return module;
}


PyTypeObject *qpycore_aux_type_ref(sipModuleState *sipMS, const char *name)
{
    PyObject *module = qpycore_qtcore_module_ref(sipMS);
    if (!module)
        return nullptr;

    PyObject *type = PyObject_GetAttrString(module, name);
    Py_DECREF(module);
    if (type && !PyType_Check(type))
    {
        Py_DECREF(type);
        PyErr_Format(PyExc_TypeError, "QtCore.%s must be a type", name);
        return nullptr;
    }

    return reinterpret_cast<PyTypeObject *>(type);
}


bool qpycore_is_aux_type(PyObject *obj, const char *name)
{
    PyTypeObject *type = qpycore_aux_type_ref(nullptr, name);
    if (!type)
        return false;

    bool matches = PyObject_TypeCheck(obj, type);
    Py_DECREF(type);
    return matches;
}
#else

// Return true if the given type was wrapped for PyQt.
bool qpycore_is_pyqt_type(const sipTypeDef *td)
{
    return sipCheckPluginForType(td, "PyQt6.QtCore");
}
#endif


void qpycore_Unicode_ConcatAndDel(PyObject **string, PyObject *newpart)
{
    PyObject *old = *string;

    if (old)
    {
        if (newpart)
            *string = PyUnicode_Concat(old, newpart);
        else
            *string = 0;

        Py_DECREF(old);
    }

    Py_XDECREF(newpart);
}


namespace
{
class QtCoreThreadPoolCall
{
public:
#if SIP_ABI_MAJOR_VERSION >= 14
    QtCoreThreadPoolCall(PyObject *callable, PyInterpreterView *view)
        : callable(Py_NewRef(callable)), view(view) {}
#else
    explicit QtCoreThreadPoolCall(PyObject *callable) : callable(Py_NewRef(callable)) {}
#endif

    ~QtCoreThreadPoolCall()
    {
#if SIP_ABI_MAJOR_VERSION >= 14
        if (auto *thread = PyThreadState_EnsureFromView(view))
        {
            Py_DECREF(callable);
            PyThreadState_Release(thread);
        }
        PyInterpreterView_Close(view);
#else
        SIP_BLOCK_THREADS
        Py_DECREF(callable);
        SIP_UNBLOCK_THREADS
#endif
    }

    void run() const
    {
#if SIP_ABI_MAJOR_VERSION >= 14
        auto *thread = PyThreadState_EnsureFromView(view);
        if (!thread)
            return;

        PyObject *previous_error = PyErr_GetRaisedException();
#else
        SIP_BLOCK_THREADS
#endif
        PyObject *result = PyObject_CallNoArgs(callable);
        if (result)
            Py_DECREF(result);
        else
            pyqt6_err_print();
#if SIP_ABI_MAJOR_VERSION >= 14
        PyErr_SetRaisedException(previous_error);
        PyThreadState_Release(thread);
#else
        SIP_UNBLOCK_THREADS
#endif
    }

private:
    PyObject *const callable;
#if SIP_ABI_MAJOR_VERSION >= 14
    PyInterpreterView *const view;
#endif
};
}

std::function<void()> qpycore_thread_function(PyObject *callable)
{
#if SIP_ABI_MAJOR_VERSION >= 14
    PyInterpreterView *view = PyInterpreterView_FromCurrent();
    if (!view)
        return {};

    auto call = std::make_shared<QtCoreThreadPoolCall>(callable, view);
#else
    auto call = std::make_shared<QtCoreThreadPoolCall>(callable);
#endif
    return [call] { call->run(); };
}


qint64 qpycore_copy_io_buffer(PyObject *object, const char *data, qint64 size, bool writable)
{
    QPyIODeviceBuffer buffer(object, writable);
    if (!buffer)
        return -1;

    const qint64 length = qMin(size, static_cast<qint64>(buffer.size));
    if (length > 0)
    {
        if (writable)
            std::memcpy(buffer.data, data, static_cast<size_t>(length));
        else
            std::memcpy(const_cast<char *>(data), buffer.data, static_cast<size_t>(length));
    }
    return length;
}


#if SIP_ABI_MAJOR_VERSION >= 14
std::function<QVariant(const QUrl &)> qpycore_resource_provider(PyObject *callable)
{
    auto *view = PyInterpreterView_FromCurrent();
    if (!view)
        return {};

    auto handler = std::shared_ptr<PyObject>(Py_NewRef(callable),
            [view](PyObject *object) {
        if (auto *token = PyThreadState_EnsureFromView(view))
        {
            Py_DECREF(object);
            PyThreadState_Release(token);
        }
        PyInterpreterView_Close(view);
    });

    return [handler, view](const QUrl &url) {
        QVariant result;
        auto *token = PyThreadState_EnsureFromView(view);
        if (!token)
            return result;

        PyObject *saved_error = PyErr_GetRaisedException();
        PyObject *module = qpycore_qtcore_module_ref();
        if (module)
        {
            auto *sipMS = static_cast<sipModuleState *>(PyModule_GetState(module));
            auto *arg = sipConvertFromNewType(new QUrl(url), sipType_QUrl, nullptr);
            if (arg)
            {
                PyObject *object = PyObject_CallOneArg(handler.get(), arg);
                Py_DECREF(arg);
                if (object)
                {
                    int is_error = 0;
                    result = qpycore_PyObject_AsQVariant(object, &is_error, sipMS);
                    Py_DECREF(object);
                }
            }
            Py_DECREF(module);
        }
        if (PyErr_Occurred())
            pyqt6_err_print();
        PyErr_SetRaisedException(saved_error);
        PyThreadState_Release(token);
        return result;
    };
}
#endif
