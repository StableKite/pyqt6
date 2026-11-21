// This contains the implementation of pyqtEnum.
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

#include <QHash>
#include <QMutex>

#include "qpycore_chimera.h"
#include "qpycore_enums_flags.h"
#include "qpycore_enums_flags_metatype.h"
#include "qpycore_misc.h"
#include "qpycore_objectified_strings.h"

#include "sipAPIQtCore.h"


// Forward declarations.
extern "C" {static PyObject *decorator(PyObject *, PyObject *enum_cls);}


// Forward declarations.
static bool add_key_value(EnumFlag &enum_flag, bool &unsigned_enum,
        PyObject *key, PyObject *value);
static bool parse_members(PyObject *members, EnumFlag &enum_flag,
        bool &unsigned_enum);


// The enums keyed by the enum type object.
static QHash<PyObject *, EnumFlag> enums_hash;
static QMutex enums_mutex;


// Implement the pyqtEnum enum decorator.
PyObject *qpycore_pyqtEnum(PyObject *enum_cls)
{
    if (enum_cls)
        return decorator(0, enum_cls);

    // Create the decorator function itself.
    static PyMethodDef deco_method = {
        "_deco", decorator, METH_O, 0
    };

    return PyCFunction_New(&deco_method, 0);
}


// Return the EnumFlag for an enum class.  The name will be null if there was
// none.
EnumFlag qpycore_pop_enum_flag(PyObject *enum_cls)
{
    EnumFlag enum_flag;
    {
        QMutexLocker locker(&enums_mutex);
        enum_flag = enums_hash.take(enum_cls);
    }

    if (!enum_flag.name.isNull())
        Py_DECREF(enum_cls);

    return enum_flag;
}


// This is the decorator function that saves the parsed enum for later
// retrieval.
static PyObject *decorator(PyObject *, PyObject *enum_cls)
{
    // Check the type.
    EnumFlag enum_flag;

#if SIP_ABI_MAJOR_VERSION >= 14
    PyObject *module = qpycore_qtcore_module_ref();
    if (!module)
        return nullptr;

    auto *sipMS = static_cast<sipModuleState *>(PyModule_GetState(module));
#endif
    enum_flag.isFlag = sipIsEnumFlag(enum_cls);
#if SIP_ABI_MAJOR_VERSION >= 14
    Py_DECREF(module);
#endif

    // Get the name.
    PyObject *name = PyObject_GetAttr(enum_cls, qpycore_dunder_name);
    if (!name)
        return 0;

    enum_flag.name = qpycore_convert_ASCII(name);
    Py_DECREF(name);

    if (enum_flag.name.isNull())
        return 0;

    // Get the members.
    PyObject *members = PyObject_GetAttrString(enum_cls, "__members__");
    if (!members)
        return 0;

    bool ok, unsigned_enum = true;

    ok = parse_members(members, enum_flag, unsigned_enum);

    Py_DECREF(members);

    if (!ok)
        return 0;

    // Get the pseudo fully qualified C++ name.
    PyObject *fq_py_name = PyObject_GetAttrString(enum_cls, "__qualname__");
    if (!fq_py_name)
        return 0;

    QByteArray fq_cpp_name = qpycore_convert_ASCII(fq_py_name);
    Py_DECREF(fq_py_name);

    fq_cpp_name.replace(QByteArray("."), QByteArray("::"));

    // Register the enum with our type system.
    Chimera::registerPyEnum(enum_cls, fq_cpp_name);

    // Register the enum with the Qt type system.
    enum_flag.metaType = qpycore_register_enum_metatype(fq_cpp_name,
            unsigned_enum);

    // Save the parsed enum for when building the QMetaObject.
    {
        QMutexLocker locker(&enums_mutex);
        if (!enums_hash.contains(enum_cls))
            Py_INCREF(enum_cls);
        enums_hash.insert(enum_cls, enum_flag);
    }

    // Return the enum class.
    Py_INCREF(enum_cls);
    return enum_cls;
}


// Parse a __members__ mapping.
static bool parse_members(PyObject *members, EnumFlag &enum_flag,
        bool &unsigned_enum)
{
    PyObject *items;
    Py_ssize_t nr_items;

    // Get the contents of __members__.
    items = PyMapping_Items(members);

    if (!items)
        goto return_error;

    nr_items = PySequence_Length(items);
    if (nr_items < 0)
        goto release_items;

    for (Py_ssize_t i = 0; i < nr_items; ++i)
    {
        PyObject *item, *key, *member, *value;

        item = PySequence_GetItem(items, i);
        if (!item)
            goto release_items;

        // The item should be a 2-element sequence of the key name and an
        // object containing the value.
        key = PySequence_GetItem(item, 0);
        member = PySequence_GetItem(item, 1);

        Py_DECREF(item);

        if (!key || !member)
        {
            Py_XDECREF(key);
            Py_XDECREF(member);

            goto release_items;
        }

        // Get the value.
        value = PyObject_GetAttrString(member, "value");

        Py_DECREF(member);

        if (!value)
        {
            Py_DECREF(key);

            goto release_items;
        }

        bool ok = add_key_value(enum_flag, unsigned_enum, key, value);

        Py_DECREF(key);
        Py_DECREF(value);

        if (!ok)
            goto release_items;
    }

    Py_DECREF(items);

    return true;

release_items:
    Py_DECREF(items);

return_error:
    return false;
}


// Add a key/value to an enum/flag.
static bool add_key_value(EnumFlag &enum_flag, bool &unsigned_enum,
        PyObject *key, PyObject *value)
{
    PyErr_Clear();

    int i_value = sipLong_AsInt(value);

    if (i_value < 0)
        unsigned_enum = false;

    if (PyErr_Occurred())
        return false;

    QByteArray key_ba = qpycore_convert_ASCII(key);

    if (key_ba.isNull())
        return false;

    enum_flag.keys.append(QPair<QByteArray, int>(key_ba, i_value));

    return true;
}
