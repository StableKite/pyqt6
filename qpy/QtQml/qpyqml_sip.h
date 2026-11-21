#ifndef QPYQML_SIP_H
#define QPYQML_SIP_H

#include "sipAPIQtQml.h"

#if SIP_ABI_MAJOR_VERSION >= 14
extern sipModuleState *qpyqml_module_state;

inline bool qpyqml_is_subtype(PyTypeObject *type, sipTypeID target)
{
    auto *base = sipABI_QtQml->api_get_py_type_ref(qpyqml_module_state, target);
    bool matches = base && PyType_IsSubtype(type, base);
    Py_XDECREF(base);
    return matches;
}

inline bool qpyqml_is_instance(PyObject *obj, sipTypeID target)
{
    return qpyqml_is_subtype(Py_TYPE(obj), target);
}

#define sipMS qpyqml_module_state
#else
inline bool qpyqml_is_subtype(PyTypeObject *type, const sipTypeDef *target)
{
    return PyType_IsSubtype(type, sipTypeAsPyTypeObject(target));
}
inline bool qpyqml_is_instance(PyObject *obj, const sipTypeDef *target)
{
    return PyObject_TypeCheck(obj, sipTypeAsPyTypeObject(target));
}
#endif

#endif
