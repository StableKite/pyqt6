// SPDX-License-Identifier: GPL-3.0-only

#ifndef QPYCORE_VECTORCALL_H
#define QPYCORE_VECTORCALL_H

#include <Python.h>
#include <memory>

// Manual parsers using PyArg_ParseTupleAndKeywords need owned containers.
template<typename Callback>
PyObject *qpycore_call_legacy_parser(Callback callback, PyObject *const *values,
        Py_ssize_t positional_count, PyObject *keyword_names)
{
    using Reference = std::unique_ptr<PyObject, decltype(&Py_DecRef)>;

    Reference args(PyTuple_New(positional_count), Py_DecRef);
    if (!args)
        return nullptr;

    for (Py_ssize_t i = 0; i < positional_count; ++i)
        if (PyTuple_SetItem(args.get(), i, Py_NewRef(values[i])) < 0)
            return nullptr;

    Reference kwargs(nullptr, Py_DecRef);
    const Py_ssize_t count = keyword_names ? PyTuple_Size(keyword_names) : 0;
    if (count < 0)
        return nullptr;

    if (count > 0)
    {
        kwargs.reset(PyDict_New());
        if (!kwargs)
            return nullptr;

        for (Py_ssize_t i = 0; i < count; ++i)
        {
            PyObject *name = PyTuple_GetItem(keyword_names, i);
            if (!name || PyDict_SetItem(kwargs.get(), name, values[positional_count + i]) < 0)
                return nullptr;
        }
    }

    return callback(args.get(), kwargs.get());
}

#endif
