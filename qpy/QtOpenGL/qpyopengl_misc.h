// This defines the interfaces to various odd and ends.
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


#ifndef _QPYOPENGL_MISC_H
#define _QPYOPENGL_MISC_H


#include <Python.h>
#include <memory>

#include "sipAPIQtOpenGL.h"


using QPyOpenGLObjectRef = std::unique_ptr<PyObject, decltype(&Py_DecRef)>;

inline PyObject *qpyopengl_user_object_ref(PyObject *wrapper)
{
    auto *sw = reinterpret_cast<sipSimpleWrapper *>(wrapper);
#if SIP_ABI_MAJOR_VERSION >= 14
    return sipGetUserObjectRef(sw);
#else
    return Py_XNewRef(sipGetUserObject(sw));
#endif
}

inline QPyOpenGLObjectRef qpyopengl_shader_arrays(PyObject *shader)
{
    QPyOpenGLObjectRef dict(qpyopengl_user_object_ref(shader), Py_DecRef);
    if (!dict)
    {
        dict.reset(PyDict_New());
        if (dict)
        {
#if SIP_ABI_MAJOR_VERSION >= 14
            sipSetUserObject(reinterpret_cast<sipSimpleWrapper *>(shader), dict.get());
#else
            sipSetUserObject(reinterpret_cast<sipSimpleWrapper *>(shader), Py_NewRef(dict.get()));
#endif
        }
    }
    return dict;
}

inline PyObject *qpyopengl_sequence(PyObject *values, const char *message)
{
#if SIP_ABI_MAJOR_VERSION >= 14
    PyObject *tuple = PySequence_Tuple(values);
    if (!tuple && PyErr_ExceptionMatches(PyExc_TypeError))
        PyErr_SetString(PyExc_TypeError, message);
    return tuple;
#else
    return PySequence_Fast(values, message);
#endif
}


// Replacements for the corresponding Python macros that use the limited API.
#define Sequence_Fast_Size(o) \
    (PyList_Check(o) ? PyList_Size(o) : PyTuple_Size(o))
#define Sequence_Fast_GetItem(o, i)\
    (PyList_Check(o) ? PyList_GetItem(o, i) : PyTuple_GetItem(o, i))


#endif
