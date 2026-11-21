// This contains the implementation of the pyqtSignal type.
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

#include <QtGlobal>
#include <QByteArray>
#include <QMetaObject>
#include <QVarLengthArray>

#include "qpycore_chimera.h"
#include "qpycore_misc.h"
#include "qpycore_pyqtboundsignal.h"
#include "qpycore_pyqtsignal.h"


#if SIP_ABI_MAJOR_VERSION < 14
PyTypeObject *qpycore_pyqtSignal_TypeObject;
#endif


// Forward declarations.
extern "C" {
static PyObject *pyqtSignal_call(PyObject *self, PyObject *args, PyObject *kw);
static void pyqtSignal_dealloc(PyObject *self);
static PyObject *pyqtSignal_descr_get(PyObject *self, PyObject *obj,
        PyObject *type);
static int pyqtSignal_init(PyObject *self, PyObject *args, PyObject *kwd_args);
static PyObject *pyqtSignal_repr(PyObject *self);
static PyObject *pyqtSignal_mp_subscript(PyObject *self, PyObject *subscript);
static PyObject *pyqtSignal_get_doc(PyObject *self, void *);
static PyObject *pyqtSignal_get_signatures(PyObject *self, void *);
}

static int init_signal_from_types(qpycore_pyqtSignal *ps, const char *name,
        const QList<QByteArray> *parameter_names, int revision,
        PyObject *types, Chimera::ModuleState *sipMS);
static void append_overload(qpycore_pyqtSignal *ps);
static bool is_signal_name(const char *sig, const QByteArray &name);


// Doc-strings.
PyDoc_STRVAR(pyqtSignal_doc,
"pyqtSignal(*types, name: str = ..., revision: int = ..., arguments: Sequence = ...) -> PYQT_SIGNAL\n"
"\n"
"types is normally a sequence of individual types.  Each type is either a\n"
"type object or a string that is the name of a C++ type.  Alternatively\n"
"each type could itself be a sequence of types each describing a different\n"
"overloaded signal.\n"
"name is the optional C++ name of the signal.  If it is not specified then\n"
"the name of the class attribute that is bound to the signal is used.\n"
"revision is the optional revision of the signal that is exported to QML.\n"
"If it is not specified then 0 is used.\n"
"arguments is the optional sequence of the names of the signal's arguments.\n");

PyDoc_STRVAR(pyqtSignal_signatures_doc,
"The signatures of each of the overloaded signals");


// The getters/setters.
static PyGetSetDef pyqtSignal_getset[] = {
    {const_cast<char *>("__doc__"), pyqtSignal_get_doc, NULL, NULL, NULL},
    {const_cast<char *>("signatures"), pyqtSignal_get_signatures, NULL,
            const_cast<char *>(pyqtSignal_signatures_doc), NULL},
    {NULL, NULL, NULL, NULL, NULL}
};


// Define the slots.
static PyType_Slot qpycore_pyqtSignal_Slots[] = {
    {Py_tp_new,         (void *)PyType_GenericNew},
    {Py_tp_init,        (void *)pyqtSignal_init},
    {Py_tp_dealloc,     (void *)pyqtSignal_dealloc},
    {Py_tp_doc,         (void *)pyqtSignal_doc},
    {Py_tp_repr,        (void *)pyqtSignal_repr},
    {Py_tp_call,        (void *)pyqtSignal_call},
    {Py_tp_descr_get,   (void *)pyqtSignal_descr_get},
    {Py_mp_subscript,   (void *)pyqtSignal_mp_subscript},
    {Py_tp_getset,      pyqtSignal_getset},
    {0,                 0}
};


// Define the type.
static PyType_Spec qpycore_pyqtSignal_Spec = {
    "PyQt6.QtCore.pyqtSignal",
    sizeof (qpycore_pyqtSignal),
    0,
    Py_TPFLAGS_DEFAULT,
    qpycore_pyqtSignal_Slots
};


// The __doc__ getter.
static PyObject *pyqtSignal_get_doc(PyObject *self, void *)
{
    qpycore_pyqtSignal *ps = (qpycore_pyqtSignal *)self;

    // Make sure we have the default signal.
    ps = ps->default_signal;

    QByteArray doc;

    // Get any docstrings from any non-signal overloads.
#if SIP_ABI_MAJOR_VERSION < 14
    if (ps->non_signals && ps->non_signals->ml_doc)
    {
        doc.append('\n');
        doc.append(ps->non_signals->ml_doc);
    }

#endif

    // Get any docstrings from the signals.
    do
    {
        const char *docstring = ps->docstring;

        if (docstring)
        {
            if (*docstring == '\1')
                ++docstring;

            doc.append('\n');
            doc.append(docstring);
            doc.append(" [signal]");
        }

        ps = ps->next;
    }
    while (ps);

    if (doc.isEmpty())
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    return PyUnicode_FromString(doc.constData() + 1);
}


// The 'signatures' getter.
static PyObject *pyqtSignal_get_signatures(PyObject *self, void *)
{
    qpycore_pyqtSignal *head = ((qpycore_pyqtSignal *)self)->default_signal;
    qpycore_pyqtSignal *ps;
    PyObject *sigs;
    Py_ssize_t sig_nr;

    // Count the number of overloads.
    for (sig_nr = 0, ps = head; ps; ps = ps->next, ++sig_nr)
        ;

    sigs = PyTuple_New(sig_nr);
    if (!sigs)
        return 0;

    for (sig_nr = 0, ps = head; ps; ps = ps->next, ++sig_nr)
    {
        PyObject *sig = PyUnicode_FromString(
                ps->parsed_signature->signature.constData() + 1);

        if (!sig || PyTuple_SetItem(sigs, sig_nr, sig))
        {
            Py_DECREF(sigs);
            return 0;
        }
    }

    return sigs;
}


// The type repr slot.
static PyObject *pyqtSignal_repr(PyObject *self)
{
    qpycore_pyqtSignal *ps = (qpycore_pyqtSignal *)self;

    return PyUnicode_FromFormat("<unbound PYQT_SIGNAL %s>",
            ps->parsed_signature->signature.constData() + 1);
}


// The type call slot.
static PyObject *pyqtSignal_call(PyObject *self, PyObject *args, PyObject *kw)
{
    return qpycore_call_signal_overload((qpycore_pyqtSignal *)self, 0, args,
            kw);
}


// The type dealloc slot.
static void pyqtSignal_dealloc(PyObject *self)
{
    qpycore_pyqtSignal *ps = (qpycore_pyqtSignal *)self;

    delete ps->parsed_signature;

    if (ps->parameter_names)
        delete ps->parameter_names;

    // If we are the default then we own the overloads references.
    if (ps == ps->default_signal)
    {
        qpycore_pyqtSignal *next = ps->next;

        while (next)
        {
            ps = next;
            next = ps->next;

            Py_DECREF((PyObject *)ps);
        }
    }

#if SIP_ABI_MAJOR_VERSION >= 14
    auto *type = Py_TYPE(self);
    Py_XDECREF(((qpycore_pyqtSignal *)self)->native_module_ref);
    PyObject_Del(self);
    Py_DECREF(type);
#else
    PyObject_Del(self);
#endif
}


// The type descriptor get slot.
static PyObject *pyqtSignal_descr_get(PyObject *self, PyObject *obj,
        PyObject *)
{
    qpycore_pyqtSignal *ps = (qpycore_pyqtSignal *)self;

    // Return the unbound signal if there is nothing to bind it to.
    if (obj == NULL || obj == Py_None)
    {
        Py_INCREF(self);
        return self;
    }

#if SIP_ABI_MAJOR_VERSION >= 14
    auto *sipMS = sipGetModuleStateByType(Py_TYPE(self));
    if (!sipMS)
        return nullptr;
#endif

    // Get the QObject.
    int is_err = 0;
    void *qobject = sipForceConvertToType(obj, sipType_QObject, 0,
            SIP_NO_CONVERTORS, 0, &is_err);

    if (is_err)
        return 0;

    // Return the bound signal.
    return qpycore_pyqtBoundSignal_New(ps, obj,
            reinterpret_cast<QObject *>(qobject));
}


// The type init slot.
static int pyqtSignal_init(PyObject *self, PyObject *args, PyObject *kwd_args)
{
    qpycore_pyqtSignal *ps = (qpycore_pyqtSignal *)self;

#if SIP_ABI_MAJOR_VERSION >= 14
    auto *sipMS = sipGetModuleStateByType(Py_TYPE(self));
    if (!sipMS)
        return -1;
#else
    Chimera::ModuleState *sipMS = nullptr;
#endif

    // Get the keyword arguments.
    PyObject *name_obj = 0;
    const char *name = 0;
    int revision = 0;
    QList<QByteArray> *parameter_names = 0;

    if (kwd_args)
    {
        Py_ssize_t pos = 0;
        PyObject *key, *value;

        while (PyDict_Next(kwd_args, &pos, &key, &value))
        {
            if (PyUnicode_CompareWithASCIIString(key, "name") == 0)
            {
                if (PyUnicode_Check(value))
                    name_obj = PyUnicode_AsASCIIString(value);
                else if (PyBytes_Check(value))
                    name_obj = Py_NewRef(value);

                name = name_obj ? PyBytes_AsString(name_obj) : nullptr;

                if (!name)
                {
                    PyErr_Format(PyExc_TypeError,
                            "signal 'name' must be a str, not %s",
                            sipPyTypeName(Py_TYPE(value)));

                    return -1;
                }
            }
            else if (PyUnicode_CompareWithASCIIString(key, "revision") == 0)
            {
                revision = sipLong_AsInt(value);

                if (PyErr_Occurred())
                {
                    if (PyErr_ExceptionMatches(PyExc_TypeError))
                        PyErr_Format(PyExc_TypeError,
                                "signal 'revision' must be an int, not %s",
                                sipPyTypeName(Py_TYPE(value)));

                    Py_XDECREF(name_obj);
                    return -1;
                }
            }
            else if (PyUnicode_CompareWithASCIIString(key, "arguments") == 0)
            {
                bool ok = true;

                if (PySequence_Check(value))
                {
                    Py_ssize_t len = PySequence_Size(value);

                    parameter_names = new QList<QByteArray>;

                    for (Py_ssize_t i = 0; i < len; ++i)
                    {
                        PyObject *py_attr = PySequence_GetItem(value, i);

                        if (!py_attr)
                        {
                            ok = false;
                            break;
                        }

                        QByteArray attr = qpycore_convert_ASCII(py_attr);
                        Py_DECREF(py_attr);

                        if (attr.isNull())
                        {
                            ok = false;
                            break;
                        }

                        parameter_names->append(attr);
                    }
                }
                else
                {
                    ok = false;
                }

                if (!ok)
                {
                    PyErr_Format(PyExc_TypeError,
                            "signal 'attribute_names' must be a sequence of str, not %s",
                            sipPyTypeName(Py_TYPE(value)));

                    if (parameter_names)
                        delete parameter_names;

                    Py_XDECREF(name_obj);
                    return -1;
                }
            }
            else
            {
                PyErr_Format(PyExc_TypeError,
                        "pyqtSignal() got an unexpected keyword argument '%U'",
                        key);

                Py_XDECREF(name_obj);
                return -1;
            }
        }
    }

    // If there is at least one argument and it is a sequence then assume all
    // arguments are sequences.  Unfortunately a string is also a sequence so
    // check for tuples and lists explicitly.
    if (PyTuple_Size(args) > 0 && (PyTuple_Check(PyTuple_GetItem(args, 0)) || PyList_Check(PyTuple_GetItem(args, 0))))
    {
        for (Py_ssize_t i = 0; i < PyTuple_Size(args); ++i)
        {
            PyObject *types = PySequence_Tuple(PyTuple_GetItem(args, i));

            if (!types)
            {
                PyErr_SetString(PyExc_TypeError,
                        "pyqtSignal() argument expected to be sequence of types");

                if (name)
                {
                    Py_DECREF(name_obj);
                }

                return -1;
            }

            int rc;

            if (i == 0)
            {
                // The first is the default.
                rc = init_signal_from_types(ps, name, parameter_names,
                        revision, types, sipMS);
            }
            else
            {
                qpycore_pyqtSignal *overload = (qpycore_pyqtSignal *)PyType_GenericNew(Py_TYPE(self), 0, 0);

                if (!overload)
                {
                    rc = -1;
                }
                else if ((rc = init_signal_from_types(overload, name, 0, revision, types, sipMS)) < 0)
                {
                    Py_DECREF((PyObject *)overload);
                }
                else
                {
                    overload->default_signal = ps;
                    append_overload(overload);
                }
            }

            Py_DECREF(types);

            if (rc < 0)
            {
                if (name)
                {
                    Py_DECREF(name_obj);
                }

                return -1;
            }
        }
    }
    else if (init_signal_from_types(ps, name, parameter_names, revision, args, sipMS) < 0)
    {
        if (name)
        {
            Py_DECREF(name_obj);
        }

        return -1;
    }

    if (name)
    {
        Py_DECREF(name_obj);
    }

    return 0;
}


// The mapping subscript slot.
static PyObject *pyqtSignal_mp_subscript(PyObject *self, PyObject *subscript)
{
    qpycore_pyqtSignal *ps = (qpycore_pyqtSignal *)self;

    PyObject *sig = (PyObject *)qpycore_find_signal(ps, subscript,
            "an unbound signal type argument");

    Py_XINCREF(sig);

    return sig;
}


// Initialise the type and return true if there was no error.
#if SIP_ABI_MAJOR_VERSION >= 14
bool qpycore_pyqtSignal_init_type(PyObject *module)
{
    PyObject *type = PyType_FromModuleAndSpec(module, &qpycore_pyqtSignal_Spec, nullptr);
    if (!type)
        return false;

    int status = PyModule_AddObjectRef(module, "pyqtSignal", type);
    Py_DECREF(type);
    return status == 0;
}
#else
bool qpycore_pyqtSignal_init_type()
{
    qpycore_pyqtSignal_TypeObject = (PyTypeObject *)PyType_FromSpec(
            &qpycore_pyqtSignal_Spec);

    return qpycore_pyqtSignal_TypeObject;
}
#endif


// Create a new signal instance.
qpycore_pyqtSignal *qpycore_pyqtSignal_New(const char *signature, bool *fatal,
        Chimera::ModuleState *sipMS)
{
    // Assume any error is fatal.
    if (fatal)
        *fatal = true;

    // See if we have two variants of the signature, ie. one with namespaces
    // stripped that we can use with connect() and another that we can parse.
    const char *full_sig = strchr(signature, '|');

    Chimera::Signature *parsed_signature = Chimera::parse(
            (full_sig ? full_sig + 1 : signature), "a signal argument", sipMS);

    // At first glance the parse should never fail because the signature
    // originates from the .sip file.  However it might if it includes a type
    // that has been forward declared, but not yet defined.  The only example
    // in PyQt is the declaration of QWidget by QSignalMapper.  Therefore we
    // assume the error isn't fatal.
    if (!parsed_signature)
    {
        if (fatal)
            *fatal = false;

        return 0;
    }

    if (full_sig)
        // Use the variant with namespaces stripped.
        parsed_signature->signature = QByteArray(signature,
                full_sig - signature);

    parsed_signature->signature.prepend('2');

    // Create and initialise the signal.
#if SIP_ABI_MAJOR_VERSION >= 14
    PyTypeObject *signal_type = qpycore_aux_type_ref(sipMS, "pyqtSignal");
    auto *ps = signal_type ? reinterpret_cast<qpycore_pyqtSignal *>(
            PyType_GenericNew(signal_type, nullptr, nullptr)) : nullptr;
    Py_XDECREF(signal_type);
#else
    auto *ps = reinterpret_cast<qpycore_pyqtSignal *>(
            PyType_GenericNew(qpycore_pyqtSignal_TypeObject, nullptr, nullptr));
#endif

    if (!ps)
    {
        delete parsed_signature;
        return 0;
    }

    ps->default_signal = ps;
    ps->next = 0;
    ps->docstring = 0;
    ps->parameter_names = 0;
    ps->revision = 0;
    ps->parsed_signature = parsed_signature;
    ps->emitter = 0;
    ps->non_signals = 0;

    return ps;
}


// Find an overload that matches a subscript.
qpycore_pyqtSignal *qpycore_find_signal(qpycore_pyqtSignal *ps,
        PyObject *subscript, const char *context)
{
    // Make sure the subscript is a tuple.
    PyObject *args;

    if (PyTuple_Check(subscript))
    {
        args = subscript;
    }
    else
    {
        args = PyTuple_New(1);

        if (!args)
            return 0;

        PyTuple_SetItem(args, 0, subscript);
    }

    Py_INCREF(subscript);

    // Parse the subscript as a tuple of types.
#if SIP_ABI_MAJOR_VERSION >= 14
    auto *sipMS = sipGetModuleStateByType(Py_TYPE(ps));
    if (!sipMS)
    {
        Py_DECREF(args);
        return nullptr;
    }
#else
    Chimera::ModuleState *sipMS = nullptr;
#endif
    Chimera::Signature *ss_signature = Chimera::parse(args, 0, context, sipMS);

    Py_DECREF(args);

    if (!ss_signature)
        return 0;

    // Search for an overload with this signature.
    qpycore_pyqtSignal *overload = ps->default_signal;

    do
    {
        if (overload->parsed_signature->arguments() == ss_signature->signature)
            break;

        overload = overload->next;
    }
    while (overload);

    delete ss_signature;

    if (!overload)
        PyErr_SetString(PyExc_KeyError,
                "there is no matching overloaded signal");

    return overload;
}


// Initialise a signal when given a tuple of types.
static int init_signal_from_types(qpycore_pyqtSignal *ps, const char *name,
        const QList<QByteArray> *parameter_names, int revision,
        PyObject *types, Chimera::ModuleState *sipMS)
{
    Chimera::Signature *parsed_signature = Chimera::parse(types, name,
            "a pyqtSignal() type argument", sipMS);

    if (!parsed_signature)
        return -1;

    if (name)
        parsed_signature->signature.prepend('2');

    ps->default_signal = ps;
    ps->next = 0;
    ps->docstring = 0;
    ps->parameter_names = parameter_names;
    ps->revision = revision;
    ps->parsed_signature = parsed_signature;
    ps->emitter = 0;
    ps->non_signals = 0;

    return 0;
}


// Append an overload to the default signal's list.
static void append_overload(qpycore_pyqtSignal *ps)
{
    // Append to the list of overloads.
    qpycore_pyqtSignal **tailp = &ps->default_signal->next;

    while (*tailp)
        tailp = &(*tailp)->next;

    *tailp = ps;
}


// Give a signal a name if it hasn't already got one.
void qpycore_set_signal_name(qpycore_pyqtSignal *ps, const char *type_name,
        const char *name)
{
    ps = ps->default_signal;

    // If the signature already has a name then they all have and there is
    // nothing more to do.
    if (!ps->parsed_signature->signature.startsWith('('))
        return;

    do
    {
        QByteArray &sig = ps->parsed_signature->signature;

        sig.prepend(name);
        sig.prepend('2');

        QByteArray &py_sig = ps->parsed_signature->py_signature;

        py_sig.prepend(name);
        py_sig.prepend('.');
        py_sig.prepend(type_name);

        ps = ps->next;
    }
    while (ps);
}


// Install native Qt signals on the wrapped type before it is published.
static int add_native_signals(const pyqt6ClassPluginDef *plugin, PyObject *dict,
        Chimera::ModuleState *sipMS
#if SIP_ABI_MAJOR_VERSION >= 14
        , PyObject *module_ref
#endif
        )
{
    if (!plugin || !plugin->qt_signals)
        return 0;

    const pyqt6QtSignal *sigs = plugin->qt_signals;
    QByteArray default_name;
    qpycore_pyqtSignal *default_signal = nullptr;

    for (; sigs->signature; ++sigs)
    {
        if (default_signal && !is_signal_name(sigs->signature, default_name))
        {
            int status = PyDict_SetItemString(dict, default_name.constData(),
                    reinterpret_cast<PyObject *>(default_signal));
            Py_DECREF(reinterpret_cast<PyObject *>(default_signal));
            default_signal = nullptr;
            if (status < 0)
                return -1;
        }

        bool fatal;
        qpycore_pyqtSignal *sig = qpycore_pyqtSignal_New(sigs->signature,
                &fatal, sipMS);
        if (!sig)
        {
            if (fatal)
            {
                Py_XDECREF(reinterpret_cast<PyObject *>(default_signal));
                return -1;
            }
            PyErr_Clear();
            continue;
        }

        sig->docstring = sigs->docstring;
        sig->emitter = sigs->emitter;
#if SIP_ABI_MAJOR_VERSION >= 14
        sig->native_module_ref = Py_NewRef(module_ref);
#endif

        if (default_signal)
        {
            sig->default_signal = default_signal;
            append_overload(sig);
        }
        else
        {
            sig->non_signals = sigs->non_signals;
            default_signal = sig;

            default_name = sigs->signature;
            default_name.truncate(default_name.indexOf('('));
        }
    }

    if (!default_signal)
        return 0;

    int status = PyDict_SetItemString(dict, default_name.constData(),
            reinterpret_cast<PyObject *>(default_signal));
    Py_DECREF(reinterpret_cast<PyObject *>(default_signal));
    return status;
}


#if SIP_ABI_MAJOR_VERSION >= 14
int qpycore_finalize_signal_type(sipModuleState *sipMS, sipTypeID,
        PyTypeObject *py_type)
{
    sipTypeID type = sipTypeFromPyTypeObject(py_type);
    auto *plugin = static_cast<const pyqt6ClassPluginDef *>(sipTypePluginData(type));
    if (!plugin || !plugin->qt_signals)
        return 0;

    PyObject *defining_module = PyType_GetModule(py_type);
    if (!defining_module)
        return -1;

    PyObject *module_ref = PyWeakref_NewRef(defining_module, nullptr);
    if (!module_ref)
        return -1;

    PyObject *dict = sipPyTypeDictRef(py_type);
    int status = dict ? add_native_signals(plugin, dict, sipMS, module_ref) : -1;
    if (status == 0)
        PyType_Modified(py_type);

    Py_XDECREF(dict);
    Py_DECREF(module_ref);
    return status;
}


PyObject *qpycore_signal_native_module(qpycore_pyqtSignal *signal)
{
    PyObject *module = nullptr;
    if (signal->native_module_ref &&
            PyWeakref_GetRef(signal->native_module_ref, &module) < 0)
        return nullptr;

    if (!module)
        PyErr_SetString(PyExc_RuntimeError, "the Qt signal's module is unavailable");
    return module;
}
#else
int qpycore_get_lazy_attr(const sipTypeDef *td, PyObject *dict)
{
    auto *plugin = static_cast<const pyqt6ClassPluginDef *>(sipTypePluginData(td));
    return add_native_signals(plugin, dict, nullptr);
}
#endif


// Return true if a signal signatures has a particular name.
static bool is_signal_name(const char *sig, const QByteArray &name)
{
    return (qstrncmp(sig, name.constData(), name.size()) == 0 && sig[name.size()] == '(');
}


// Call a signal's overloaded method (if there is one).
PyObject *qpycore_call_signal_overload(qpycore_pyqtSignal *ps, PyObject *bound,
        PyObject *args, PyObject *kw)
{
    if (!ps->non_signals)
    {
        PyErr_SetString(PyExc_TypeError, "native Qt signal is not callable");
        return 0;
    }

#if SIP_ABI_MAJOR_VERSION >= 14
    PyObject *module = qpycore_signal_native_module(ps);
    if (!module)
        return nullptr;

    PyObject *keywords = kw ? PyDict_Copy(kw) : nullptr;
    PyObject *keys = keywords ? PyDict_Keys(keywords) : nullptr;
    PyObject *names = keys ? PyList_AsTuple(keys) : nullptr;
    Py_XDECREF(keys);
    if (kw && !names)
    {
        Py_XDECREF(keywords);
        Py_DECREF(module);
        return nullptr;
    }

    Py_ssize_t positional = PyTuple_Size(args);
    Py_ssize_t named = names ? PyTuple_Size(names) : 0;
    QVarLengthArray<PyObject *, 16> argv(positional + named);
    for (Py_ssize_t i = 0; i < positional; ++i)
        argv[i] = PyTuple_GetItem(args, i);

    for (Py_ssize_t i = 0; i < named; ++i)
        argv[positional + i] = PyDict_GetItem(keywords, PyTuple_GetItem(names, i));

    auto *sipMS = static_cast<sipModuleState *>(PyModule_GetState(module));
    PyObject *parse_state = nullptr;
    PyObject *result = ps->non_signals(sipMS, &parse_state, bound, argv.data(),
            positional, names);
    Py_XDECREF(parse_state);
    Py_XDECREF(names);
    Py_XDECREF(keywords);
    Py_DECREF(module);

    if (!result && !PyErr_Occurred())
        PyErr_SetString(PyExc_TypeError, "arguments do not match the native method overload");
    return result;
#else
    PyObject *func = PyCFunction_New(ps->non_signals, bound);

    if (!func)
        return 0;

    PyObject *result = PyObject_Call(func, args, kw);

    Py_DECREF(func);

    return result;
#endif
}
