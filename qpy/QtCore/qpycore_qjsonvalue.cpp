// This is the support for QJsonValue.
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

#include "qpycore_api.h"

#include "qpycore_api.h"

#include "sipAPIQtCore.h"


static bool is_json_value_type(PyObject *py,
        [[maybe_unused]] QPyCoreModuleState *sipMS)
{
#if SIP_ABI_MAJOR_VERSION >= 14
    PyTypeObject *type = sipGetPyTypeRef(sipType_QJsonValue_Type);
    if (!type)
        return false;

    bool matches = PyObject_TypeCheck(py, type);
    Py_DECREF(type);
    return matches;
#else
    return PyObject_TypeCheck(py, sipTypeAsPyTypeObject(sipType_QJsonValue_Type));
#endif
}


// See if a Python object can be converted to a QJsonValue.
int qpycore_canConvertTo_QJsonValue(PyObject *py,
        [[maybe_unused]] QPyCoreModuleState *sipMS)
{
    // Note that the API doesn't provide a test for constrained named enums.
    if (is_json_value_type(py, sipMS))
        return 1;
    if (PyErr_Occurred())
        return 0;

    if (PyBool_Check(py))
        return 1;

    if (PyLong_Check(py))
        return 1;

    if (PyFloat_Check(py))
        return 1;

    if (sipCanConvertToType(py, sipType_QString, 0))
        return 1;

    // We must check for QJsonObject before QJsonArray.
    if (sipCanConvertToType(py, sipType_QJsonObject, 0))
        return 1;

    // We must check for QJsonValue before QJsonArray.
    if (sipCanConvertToType(py, sipType_QJsonValue, SIP_NO_CONVERTORS))
        return 1;

    return sipCanConvertToType(py, sipType_QJsonArray, 0);
}


// Convert a Python object to a QJsonValue.
int qpycore_convertTo_QJsonValue(PyObject *py, PyObject *transferObj,
        QJsonValue **cpp, int *isErr, [[maybe_unused]] QPyCoreModuleState *sipMS)
{
    if (py == Py_None)
    {
        *cpp = new QJsonValue();

        return sipGetState(transferObj);
    }

    if (is_json_value_type(py, sipMS))
    {
        int v = sipConvertToEnum(py, sipType_QJsonValue_Type);

        if (PyErr_Occurred())
        {
            *isErr = 1;
            return 0;
        }

        *cpp = new QJsonValue(static_cast<QJsonValue::Type>(v));

        return sipGetState(transferObj);
    }

    if (PyErr_Occurred())
    {
        *isErr = 1;
        return 0;
    }

    if (PyBool_Check(py))
    {
        *cpp = new QJsonValue(py == Py_True);

        return sipGetState(transferObj);
    }

    if (PyLong_Check(py))
    {
        long long value = PyLong_AsLongLong(py);
        if (PyErr_Occurred())
        {
            *isErr = 1;
            return 0;
        }

        *cpp = new QJsonValue(static_cast<qint64>(value));

        return sipGetState(transferObj);
    }

    if (PyFloat_Check(py))
    {
        *cpp = new QJsonValue((double)PyFloat_AsDouble(py));

        return sipGetState(transferObj);
    }

    if (sipCanConvertToType(py, sipType_QString, 0))
    {
        int state;
        QString *q = reinterpret_cast<QString *>(sipConvertToType(py,
                sipType_QString, 0, 0, &state, isErr));

        if (*isErr)
            return 0;

        *cpp = new QJsonValue(*q);

        sipReleaseType(q, sipType_QString, state);

        return sipGetState(transferObj);
    }

    // We must check for QJsonObject before QJsonArray.
    if (sipCanConvertToType(py, sipType_QJsonObject, 0))
    {
        int state;
        QJsonObject *q = reinterpret_cast<QJsonObject *>(sipConvertToType(py,
                sipType_QJsonObject, 0, 0, &state, isErr));

        if (*isErr)
            return 0;

        *cpp = new QJsonValue(*q);

        sipReleaseType(q, sipType_QJsonObject, state);

        return sipGetState(transferObj);
    }

    // We must check for QJsonValue before QJsonArray.
    if (sipCanConvertToType(py, sipType_QJsonValue, SIP_NO_CONVERTORS))
    {
        *cpp = reinterpret_cast<QJsonValue *>(sipConvertToType(py,
                sipType_QJsonValue, transferObj, SIP_NO_CONVERTORS, 0, isErr));

        return 0;
    }

    // Now it is safe to convert to QJsonArray.
    {
        int state;
        QJsonArray *q = reinterpret_cast<QJsonArray *>(sipConvertToType(py,
                sipType_QJsonArray, 0, 0, &state, isErr));

        if (*isErr)
            return 0;

        *cpp = new QJsonValue(*q);

        sipReleaseType(q, sipType_QJsonArray, state);

        return sipGetState(transferObj);
    }
}
