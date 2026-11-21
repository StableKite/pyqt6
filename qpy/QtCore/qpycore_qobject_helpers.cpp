// This implements the helpers for QObject.
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


#include <ctype.h>

#include <Python.h>

#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QVariant>

#include "qpycore_api.h"
#include "qpycore_chimera.h"
#include "qpycore_misc.h"
#include "qpycore_objectified_strings.h"
#include "qpycore_pyqtboundsignal.h"
#include "qpycore_pyqtproperty.h"
#include "qpycore_pyqtpyobject.h"
#include "qpycore_pyqtslot.h"
#include "qpycore_qobject_helpers.h"
#include "qpycore_types.h"

#include "sipAPIQtCore.h"


// Forward declarations.
#if SIP_ABI_MAJOR_VERSION >= 14
static int qt_metacall_worker(sipModuleState *sipMS, sipSimpleWrapper *pySelf,
        PyTypeObject *pytype, sipTypeID base, QMetaObject::Call _c, int _id,
        void **_a);
#else
static int qt_metacall_worker(sipSimpleWrapper *pySelf, PyTypeObject *pytype,
        sipTypeDef *base, QMetaObject::Call _c, int _id, void **_a);
#endif


// This is the helper for all implementations of QObject::metaObject().
#if SIP_ABI_MAJOR_VERSION >= 14
const QMetaObject *qpycore_qobject_metaobject(sipModuleState *sipMS,
        sipSimpleWrapper *pySelf, sipTypeID base)
#else
const QMetaObject *qpycore_qobject_metaobject(sipSimpleWrapper *pySelf,
        sipTypeDef *base)
#endif
{
    sipWrapperType *wt = (pySelf ? (sipWrapperType *)Py_TYPE(pySelf) : 0);

#if SIP_ABI_MAJOR_VERSION >= 14
    base = sipMakeAbsolute(base);
    sipMS = sipGetModuleState();
    return sipMS ? qpycore_get_qmetaobject(wt, base, sipMS) : nullptr;
#else
    return qpycore_get_qmetaobject(wt, base);
#endif
}


// This is the helper for all implementations of QObject::qt_metacall().
#if SIP_ABI_MAJOR_VERSION >= 14
int qpycore_qobject_qt_metacall(sipModuleState *sipMS, sipSimpleWrapper *pySelf,
        sipTypeID base, QMetaObject::Call _c, int _id, void **_a)
#else
int qpycore_qobject_qt_metacall(sipSimpleWrapper *pySelf, sipTypeDef *base,
        QMetaObject::Call _c, int _id, void **_a)
#endif
{
    // Check if the Python object has gone.
    if (!pySelf)
        return -1;

#if SIP_ABI_MAJOR_VERSION >= 14
    base = sipMakeAbsolute(base);
    sipMS = sipGetModuleState();
    return sipMS ? qt_metacall_worker(sipMS, pySelf, Py_TYPE(pySelf), base,
            _c, _id, _a) : _id;
#else
    return qt_metacall_worker(pySelf, Py_TYPE(pySelf), base, _c, _id, _a);
#endif
}


// This does the real work for all implementations of QObject::qt_metacall().
#if SIP_ABI_MAJOR_VERSION >= 14
static int qt_metacall_worker(sipModuleState *sipMS, sipSimpleWrapper *pySelf,
        PyTypeObject *pytype, sipTypeID base, QMetaObject::Call _c, int _id,
        void **_a)
#else
static int qt_metacall_worker(sipSimpleWrapper *pySelf, PyTypeObject *pytype,
        sipTypeDef *base, QMetaObject::Call _c, int _id, void **_a)
#endif
{
    // Only Python subtypes contribute extra meta-methods.
#if SIP_ABI_MAJOR_VERSION >= 14
    if (!pytype || !sipIsUserType(reinterpret_cast<sipWrapperType *>(pytype)))
#else
    if (pytype == sipTypeAsPyTypeObject(base))
#endif
        return _id;

    PyTypeObject *tp_base = reinterpret_cast<PyTypeObject *>(
            PyType_GetSlot(pytype, Py_tp_base));

#if SIP_ABI_MAJOR_VERSION >= 14
    _id = qt_metacall_worker(sipMS, pySelf, tp_base, base, _c, _id, _a);
#else
    _id = qt_metacall_worker(pySelf, tp_base, base, _c, _id, _a);
#endif

    if (_id < 0)
        return _id;

#if SIP_ABI_MAJOR_VERSION >= 14
    const qpycore_metaobject *qo = qpycore_get_dynamic_metaobject(
            reinterpret_cast<sipWrapperType *>(pytype), sipMS);
#else
    const qpycore_metaobject *qo = qpycore_get_dynamic_metaobject(
            (sipWrapperType *)pytype);
#endif
    if (!qo)
        return _id;

    bool ok = true;

    if (_c == QMetaObject::InvokeMetaMethod)
    {
        if (_id < qo->nr_signals + qo->pslots.count())
        {
            if (_id < qo->nr_signals)
            {
#if SIP_ABI_MAJOR_VERSION >= 14
                auto *qthis = static_cast<QObject *>(sipGetCppPtr(sipMS,
                        reinterpret_cast<PyObject *>(pySelf), sipType_QObject));
#else
                QObject *qthis = reinterpret_cast<QObject *>(sipGetCppPtr(pySelf, sipType_QObject));
#endif
                if (!qthis)
                    return -1;

                Py_BEGIN_ALLOW_THREADS
                QMetaObject::activate(qthis, qo->mo, _id, _a);
                Py_END_ALLOW_THREADS
            }
            else
            {
                PyQtSlot *slot = qo->pslots.at(_id - qo->nr_signals);

                ok = slot->invoke(_a, (PyObject *)pySelf, _a[0]);
            }
        }

        _id -= qo->nr_signals + qo->pslots.count();
    }
    else if (_c == QMetaObject::ReadProperty)
    {
        if (_id < qo->pprops.count())
        {
            qpycore_pyqtProperty *prop = qo->pprops.at(_id);

            if (prop->pyqtprop_get)
            {
                PyObject *py = PyObject_CallFunction(prop->pyqtprop_get,
                        const_cast<char *>("O"), pySelf);

                if (py)
                {
                    ok = prop->pyqtprop_parsed_type->fromPyObject(py, _a[0]);
                    Py_DECREF(py);
                }
                else
                {
                    ok = false;
                }
            }
        }

        _id -= qo->pprops.count();
    }
    else if (_c == QMetaObject::WriteProperty)
    {
        if (_id < qo->pprops.count())
        {
            qpycore_pyqtProperty *prop = qo->pprops.at(_id);

            if (prop->pyqtprop_set)
            {
                PyObject *py = prop->pyqtprop_parsed_type->toPyObject(_a[0]);

                if (py)
                {
                    PyObject *res = PyObject_CallFunction(prop->pyqtprop_set,
                            const_cast<char *>("OO"), pySelf, py);

                    if (res)
                        Py_DECREF(res);
                    else
                        ok = false;

                    Py_DECREF(py);
                }
                else
                {
                    ok = false;
                }
            }
        }

        _id -= qo->pprops.count();
    }
    else if (_c == QMetaObject::ResetProperty)
    {
        if (_id < qo->pprops.count())
        {
            qpycore_pyqtProperty *prop = qo->pprops.at(_id);

            if (prop->pyqtprop_reset)
            {
                PyObject *py = PyObject_CallFunction(prop->pyqtprop_reset,
                        const_cast<char *>("O"), pySelf);

                if (py)
                    Py_DECREF(py);
                else
                    ok = false;
            }
        }

        _id -= qo->pprops.count();
    }

    // Handle any Python errors.
    if (!ok)
    {
        pyqt6_err_print();
        return -1;
    }

    return _id;
}


// This is the helper for all implementations of QObject::qt_metacast().
#if SIP_ABI_MAJOR_VERSION >= 14
bool qpycore_qobject_qt_metacast(sipModuleState *sipMS,
        sipSimpleWrapper *pySelf, sipTypeID base, const char *_clname,
        void **sipCpp)
{
    *sipCpp = nullptr;
    if (!_clname || !pySelf)
        return false;

    base = sipMakeAbsolute(base);
    sipMS = sipGetModuleState();
    if (!sipMS)
        return false;

    PyTypeObject *base_type = sipGetPyTypeRef(base);
    if (!base_type)
        return false;

    PyTypeObject *type = Py_TYPE(pySelf);
    bool matches = false;
    if (type != base_type)
    {
        PyObject *mro = PyObject_GetAttrString(reinterpret_cast<PyObject *>(type),
                "__mro__");
        if (mro)
        {
            for (Py_ssize_t i = 0; i < PyTuple_Size(mro); ++i)
            {
                auto *entry = reinterpret_cast<PyTypeObject *>(PyTuple_GetItem(mro, i));
                sipTypeID candidate = sipTypeFromPyTypeObject(entry);
                if (!candidate)
                    continue;

                auto *info = static_cast<const pyqt6ClassPluginDef *>(
                        sipTypePluginData(candidate));
                if (qstrcmp(sipPyTypeName(entry), _clname) == 0 ||
                        (info && info->qt_interface && qstrcmp(info->qt_interface, _clname) == 0))
                {
                    *sipCpp = sipGetCppPtr(sipMS, reinterpret_cast<PyObject *>(pySelf),
                            candidate);
                    matches = *sipCpp != nullptr;
                    break;
                }
            }
            Py_DECREF(mro);
        }
    }
    Py_DECREF(base_type);
    return matches;
}
#else
bool qpycore_qobject_qt_metacast(sipSimpleWrapper *pySelf,
        const sipTypeDef *base, const char *_clname, void **sipCpp)
{
    *sipCpp = 0;

    if (!_clname)
        return false;

    // Check if the Python object has gone.
    if (!pySelf)
        return false;

    bool is_py_class = false;

    SIP_BLOCK_THREADS

    PyTypeObject *base_pytype = sipTypeAsPyTypeObject(base);

    // We only need to look at the MRO if the object is a wrapped type (rather
    // than a user sub-class).
    if (base_pytype != Py_TYPE(pySelf))
    {
        PyObject *mro = PyObject_GetAttr((PyObject *)Py_TYPE(pySelf),
                qpycore_dunder_mro);
        Q_ASSERT(mro);

        for (Py_ssize_t i = 0; i < PyTuple_Size(mro); ++i)
        {
            PyTypeObject *pytype = (PyTypeObject *)PyTuple_GetItem(mro, i);

            const sipTypeDef *td = sipTypeFromPyTypeObject(pytype);

            if (!td || !qpycore_is_pyqt_type(td))
                continue;

            if (qstrcmp(sipPyTypeName(pytype), _clname) == 0)
            {
                // The generated type definitions represent the C++ (rather
                // than Python) hierachy.  If the C++ hierachy doesn't match
                // then the super-type must be provided by a mixin.  Note that
                // we check the type in both "directions" as we may be either
                // "side" of the base type in the MRO.
                if (PyType_IsSubtype(pytype, base_pytype) || PyType_IsSubtype(base_pytype, pytype))
                    *sipCpp = sipGetAddress(pySelf);
                else
                    *sipCpp = sipGetMixinAddress(pySelf, td);

                is_py_class = true;
                break;
            }

            const pyqt6ClassPluginDef *plugin = reinterpret_cast<const pyqt6ClassPluginDef *>(sipTypePluginData(td));

            if (plugin && plugin->qt_interface && qstrcmp(plugin->qt_interface, _clname) == 0)
            {
                *sipCpp = sipGetMixinAddress(pySelf, td);
                is_py_class = true;
                break;
            }
        }

        Py_DECREF(mro);
    }

    SIP_UNBLOCK_THREADS

    return is_py_class;
}
#endif


// This is a helper for QObject.staticMetaObject %GetCode.
PyObject *qpycore_qobject_staticmetaobject(PyObject *type_obj)
{
#if SIP_ABI_MAJOR_VERSION >= 14
    auto *sipMS = sipGetModuleStateByType(reinterpret_cast<PyTypeObject *>(type_obj));
    if (!sipMS)
        return nullptr;
    const QMetaObject *mo = qpycore_get_qmetaobject(
            reinterpret_cast<sipWrapperType *>(type_obj), 0, sipMS);
#else
    const QMetaObject *mo = qpycore_get_qmetaobject((sipWrapperType *)type_obj);
#endif

    if (!mo)
    {
        // We assume that this is a side effect of a wrapped class not being
        // fully ready until sip's meta-class's __init__() has run (rather than
        // after its __new__() method as might be expected).
        PyErr_SetString(PyExc_AttributeError,
                "staticMetaObject isn't available until the meta-class's __init__ returns");

        return 0;
    }

    return sipConvertFromType(const_cast<QMetaObject *>(mo),
            sipType_QMetaObject, 0);
}
