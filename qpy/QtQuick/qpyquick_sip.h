#ifndef QPYQUICK_SIP_H
#define QPYQUICK_SIP_H

#include "sipAPIQtQuick.h"

#if SIP_ABI_MAJOR_VERSION >= 14
extern sipModuleState *qpyquick_module_state;

inline bool qpyquick_is_subtype(PyTypeObject *type, sipTypeID target)
{
    auto *base = sipABI_QtQuick->api_get_py_type_ref(qpyquick_module_state, target);
    bool matches = base && PyType_IsSubtype(type, base);
    Py_XDECREF(base);
    return matches;
}

#define sipMS qpyquick_module_state
#else
inline bool qpyquick_is_subtype(PyTypeObject *type, const sipTypeDef *target)
{
    return PyType_IsSubtype(type, sipTypeAsPyTypeObject(target));
}
#endif

#endif
