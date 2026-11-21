// This contains the support for Python objects and Qt's metatype system.
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
#include <QMutex>
#include <utility>

#include "qpycore_api.h"
#include "qpycore_pyqtpyobject.h"

#include "sipAPIQtCore.h"


#if SIP_ABI_MAJOR_VERSION >= 14
namespace
{
using Interpreter = std::shared_ptr<PyInterpreterView>;

// Qt streams can deserialize a QVariant while its calling Python thread is
// detached. Retain the QtCore interpreter's view for those native calls.
QMutex pickle_mutex;
Interpreter pickle_interpreter;
PyObject *pickle_protocol = nullptr;

Interpreter current_pickle_interpreter()
{
    QMutexLocker lock(&pickle_mutex);
    return pickle_interpreter;
}
}

class PyQt_PyObject::Payload
{
public:
    Payload(PyObject *object, Interpreter owner)
        : object(Py_NewRef(object)), owner(std::move(owner)) {}

    ~Payload()
    {
        if (auto *thread = PyThreadState_EnsureFromView(owner.get()))
        {
            Py_DECREF(object);
            PyThreadState_Release(thread);
        }
    }

    PyObject *const object;
    const Interpreter owner;
};


int qpycore_init_pyobject_bridge()
{
    PyInterpreterView *view = PyInterpreterView_FromCurrent();
    if (!view)
        return -1;

    QMutexLocker lock(&pickle_mutex);
    pickle_interpreter.reset(view, PyInterpreterView_Close);
    return 0;
}


void qpycore_clear_pyobject_bridge()
{
    Interpreter previous;
    PyObject *protocol;
    {
        QMutexLocker lock(&pickle_mutex);
        protocol = std::exchange(pickle_protocol, nullptr);
        previous.swap(pickle_interpreter);
    }
    Py_XDECREF(protocol);
}


void qpycore_set_pickle_protocol(PyObject *value)
{
    PyObject *previous;
    {
        QMutexLocker lock(&pickle_mutex);
        previous = std::exchange(pickle_protocol, Py_NewRef(value));
    }
    Py_XDECREF(previous);
}


PyObject *qpycore_get_pickle_protocol()
{
    QMutexLocker lock(&pickle_mutex);
    return Py_NewRef(pickle_protocol ? pickle_protocol : Py_None);
}


PyQt_PyObject::PyQt_PyObject(PyObject *py)
{
    if (py)
    {
        if (auto owner = current_pickle_interpreter())
            payload = std::make_shared<Payload>(py, std::move(owner));
    }
}


PyQt_PyObject::PyQt_PyObject() = default;
PyQt_PyObject::PyQt_PyObject(const PyQt_PyObject &) = default;
PyQt_PyObject::~PyQt_PyObject() = default;
PyQt_PyObject &PyQt_PyObject::operator=(const PyQt_PyObject &) = default;


PyObject *PyQt_PyObject::borrowedPyObject() const
{
    return payload ? payload->object : nullptr;
}


PyQt_PyObject::operator QString() const
{
    auto retained = payload;
    if (!retained)
        return QString();

    auto *thread = PyThreadState_EnsureFromView(retained->owner.get());
    if (!thread)
        return QString();

    PyObject *previous_error = PyErr_GetRaisedException();
    PyObject *text = PyObject_Str(retained->object);
    QString result = text ? qpycore_PyObject_AsQString(text)
            : QString("PyQt_PyObject string conversion failed");
    Py_XDECREF(text);
    PyErr_SetRaisedException(previous_error);
    PyThreadState_Release(thread);
    return result;
}


bool operator==(const PyQt_PyObject &self, const PyQt_PyObject &other)
{
    return self.borrowedPyObject() == other.borrowedPyObject();
}


QDataStream &operator<<(QDataStream &out, const PyQt_PyObject &obj)
{
    QByteArray bytes;
    auto retained = obj.payload;
    if (retained)
    {
        if (auto *thread = PyThreadState_EnsureFromView(retained->owner.get()))
        {
            PyObject *previous_error = PyErr_GetRaisedException();
            PyObject *pickle = PyImport_ImportModule("pickle");
            if (pickle)
            {
                PyObject *protocol = qpycore_get_pickle_protocol();
                PyObject *serialized = PyObject_CallMethod(pickle, "dumps",
                        "OO", retained->object, protocol);
                if (serialized && PyBytes_Check(serialized))
                    bytes = QByteArray(PyBytes_AsString(serialized), PyBytes_Size(serialized));

                Py_XDECREF(serialized);
                Py_DECREF(protocol);
                Py_DECREF(pickle);
            }
            if (PyErr_Occurred())
                pyqt6_err_print();
            PyErr_SetRaisedException(previous_error);
            PyThreadState_Release(thread);
        }
    }

    out.writeBytes(bytes.constData(), bytes.size());
    return out;
}


QDataStream &operator>>(QDataStream &in, PyQt_PyObject &obj)
{
    char *data = nullptr;
    qint64 size = 0;
    in.readBytes(data, size);
    std::unique_ptr<char[]> bytes(data);
    if (!size)
        return in;

    auto owner = current_pickle_interpreter();
    if (!owner)
        return in;

    if (auto *thread = PyThreadState_EnsureFromView(owner.get()))
    {
        PyObject *previous_error = PyErr_GetRaisedException();
        PyObject *pickle = PyImport_ImportModule("pickle");
        PyObject *serialized = pickle ? PyBytes_FromStringAndSize(bytes.get(), size)
                : nullptr;
        PyObject *value = serialized ? PyObject_CallMethod(pickle, "loads", "O", serialized)
                : nullptr;
        if (value)
            obj.payload = std::make_shared<PyQt_PyObject::Payload>(value, owner);

        Py_XDECREF(value);
        Py_XDECREF(serialized);
        Py_XDECREF(pickle);
        PyErr_SetRaisedException(previous_error);
        PyThreadState_Release(thread);
    }
    return in;
}

#else

// The current pickle protocol.
PyObject *qpycore_pickle_protocol;


// Wrap a Python object.
PyQt_PyObject::PyQt_PyObject(PyObject *py)
{
    pyobject = py;

    SIP_BLOCK_THREADS
    Py_XINCREF(pyobject);
    SIP_UNBLOCK_THREADS
}


// Create a new wrapper, with no Python object.  This is called by Qt's
// metatype system.
PyQt_PyObject::PyQt_PyObject()
{
    pyobject = 0;
}


// Create a copy of an existing wrapper.  This is called by Qt's metatype
// system.
PyQt_PyObject::PyQt_PyObject(const PyQt_PyObject &other)
{
    pyobject = other.pyobject;

    SIP_BLOCK_THREADS
    Py_XINCREF(pyobject);
    SIP_UNBLOCK_THREADS
}


// Destroy a wrapper.
PyQt_PyObject::~PyQt_PyObject()
{
    // Qt can still be tidying up after Python has gone so make sure that it
    // hasn't.
    if (Py_IsInitialized())
    {
        SIP_BLOCK_THREADS
        Py_XDECREF(pyobject);
        SIP_UNBLOCK_THREADS
    }

    pyobject = 0;
}


// Assignment operator.
PyQt_PyObject &PyQt_PyObject::operator=(const PyQt_PyObject &other)
{
    pyobject = other.pyobject;

    SIP_BLOCK_THREADS
    Py_XINCREF(pyobject);
    SIP_UNBLOCK_THREADS

    return *this;
}


// QString operator cast.
PyQt_PyObject::operator QString() const
{
    QString qs;

    SIP_BLOCK_THREADS

    PyObject *py_s = PyObject_Str(pyobject);

    if (py_s)
    {
        qs = qpycore_PyObject_AsQString(py_s);
        Py_DECREF(py_s);
    }
    else
    {
        qs = QString("PyQt_PyObject string conversion failed");
    }

    SIP_UNBLOCK_THREADS

    return qs;
}


// Comparison operator.  It must be global to be available to QMetaType.
bool operator==(const PyQt_PyObject &self, const PyQt_PyObject &other)
{
    // Note that we implement 'self is other' rather than 'self == other'.
    return self.pyobject == other.pyobject;
}


// Serialise operator.
QDataStream &operator<<(QDataStream &out, const PyQt_PyObject &obj)
{
    PyObject *ser_obj = 0;
    const char *ser = 0;
    uint len = 0;

    if (obj.pyobject)
    {
        static PyObject *dumps = 0;

        SIP_BLOCK_THREADS

        if (!dumps)
        {
            PyObject *pickle = PyImport_ImportModule("pickle");

            if (pickle)
            {
                dumps = PyObject_GetAttrString(pickle, "dumps");
                Py_DECREF(pickle);
            }
        }

        if (dumps)
        {
            if (!qpycore_pickle_protocol)
            {
                Py_INCREF(Py_None);
                qpycore_pickle_protocol = Py_None;
            }

            ser_obj = PyObject_CallFunctionObjArgs(dumps, obj.pyobject,
                    qpycore_pickle_protocol, 0);

            if (ser_obj)
            {
                if (PyBytes_Check(ser_obj))
                {
                    ser = PyBytes_AsString(ser_obj);
                    len = PyBytes_Size(ser_obj);
                }
                else
                {
                    Py_DECREF(ser_obj);
                    ser_obj = 0;
                }
            }
            else
            {
                pyqt6_err_print();
            }
        }

        SIP_UNBLOCK_THREADS
    }

    out.writeBytes(ser, len);

    if (ser_obj)
    {
        SIP_BLOCK_THREADS
        Py_DECREF(ser_obj);
        SIP_UNBLOCK_THREADS
    }

    return out;
}


// De-serialise operator.
QDataStream &operator>>(QDataStream &in, PyQt_PyObject &obj)
{
    char *ser;
    uint len;

    in.readBytes(ser, len);

    if (len)
    {
        static PyObject *loads = 0;

        SIP_BLOCK_THREADS

        if (!loads)
        {
            PyObject *pickle = PyImport_ImportModule("pickle");

            if (pickle)
            {
                loads = PyObject_GetAttrString(pickle, "loads");
                Py_DECREF(pickle);
            }
        }

        if (loads)
        {
            PyObject *ser_obj = PyBytes_FromStringAndSize(ser, len);

            if (ser_obj)
            {
                obj.pyobject = PyObject_CallFunctionObjArgs(loads, ser_obj, 0);
                Py_DECREF(ser_obj);
            }
        }

        SIP_UNBLOCK_THREADS
    }

    delete[] ser;

    return in;
}


PyObject *PyQt_PyObject::borrowedPyObject() const
{
    return pyobject;
}

#endif
