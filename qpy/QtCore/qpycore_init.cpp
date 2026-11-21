// This is the initialisation support code for the QtCore module.
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
#include "qpycore_public_api.h"
#include "qpycore_qobject_helpers.h"

#include "sipAPIQtCore.h"

#include <QCoreApplication>


#if SIP_ABI_MAJOR_VERSION >= 14
void qpycore_clear_qt_message_handler(sipModuleState *sipMS);
#endif


// Set if any QCoreApplication (or sub-class) instance was created from Python.
bool qpycore_created_qapp;


// This is called to clean up on exit.  It is done in case the QCoreApplication
// dealloc code hasn't been called.
static PyObject *cleanup_on_exit(PyObject *module, PyObject *)
{
#if SIP_ABI_MAJOR_VERSION >= 14
    auto *sipMS = static_cast<sipModuleState *>(PyModule_GetState(module));
    if (!sipMS)
        return nullptr;
    qpycore_clear_qt_message_handler(sipMS);
    qpycore_clear_pyobject_bridge();
    pyqt6_cleanup_qobjects(sipMS);
#else
    (void)module;
    pyqt6_cleanup_qobjects();
#endif

    // Never destroy a QCoreApplication if we didn't create it (eg. if we are
    // embedded in a C++ application).
    if (qpycore_created_qapp)
    {
        QCoreApplication *app = QCoreApplication::instance();

        if (app)
        {
            Py_BEGIN_ALLOW_THREADS
            delete app;
            Py_END_ALLOW_THREADS
        }
    }

    Py_INCREF(Py_None);
    return Py_None;
}


// Perform any required initialisation.
#if SIP_ABI_MAJOR_VERSION >= 14
void qpycore_init(PyObject *module, sipModuleState *sipMS)
#else
void qpycore_init()
#endif
{
    // We haven't created a QCoreApplication instance.
    qpycore_created_qapp = false;
#if SIP_ABI_MAJOR_VERSION >= 14
    if (qpycore_init_pyobject_bridge() < 0)
        return;
#endif

    // Export the private helpers, ie. those that should not be used by
    // external handwritten code.
#if SIP_ABI_MAJOR_VERSION >= 14
    using Symbol = sipSymbolSpec;
#else
    struct Symbol {
        const char *name;
        void *symbol;
    };
#endif
    static const Symbol exports[] = {
        {"qtcore_qt_metaobject", reinterpret_cast<void *>(qpycore_qobject_metaobject)},
        {"qtcore_qt_metacall", reinterpret_cast<void *>(qpycore_qobject_qt_metacall)},
        {"qtcore_qt_metacast", reinterpret_cast<void *>(qpycore_qobject_qt_metacast)},
        {"qtcore_qobject_sender", reinterpret_cast<void *>(qpycore_qobject_sender)},
        {"pyqt6_cleanup_qobjects", reinterpret_cast<void *>(pyqt6_cleanup_qobjects)},
        {"pyqt6_err_print", reinterpret_cast<void *>(pyqt6_err_print)},
        {"pyqt6_from_argv_list", reinterpret_cast<void *>(pyqt6_from_argv_list)},
        {"pyqt6_from_qvariant_by_type", reinterpret_cast<void *>(pyqt6_from_qvariant_by_type)},
        {"pyqt6_get_connection_parts", reinterpret_cast<void *>(pyqt6_get_connection_parts)},
        {"pyqt6_get_pyqtsignal_parts", reinterpret_cast<void *>(pyqt6_get_pyqtsignal_parts)},
        {"pyqt6_get_pyqtslot_parts", reinterpret_cast<void *>(pyqt6_get_pyqtslot_parts)},
        {"pyqt6_get_qmetaobject", reinterpret_cast<void *>(pyqt6_get_qmetaobject)},
        {"pyqt6_get_signal_signature", reinterpret_cast<void *>(pyqt6_get_signal_signature)},
        {"pyqt6_register_from_qvariant_convertor", reinterpret_cast<void *>(pyqt6_register_from_qvariant_convertor)},
        {"pyqt6_register_to_qvariant_convertor", reinterpret_cast<void *>(pyqt6_register_to_qvariant_convertor)},
        {"pyqt6_register_to_qvariant_data_convertor", reinterpret_cast<void *>(pyqt6_register_to_qvariant_data_convertor)},
        {"pyqt6_update_argv_list", reinterpret_cast<void *>(pyqt6_update_argv_list)},
        {nullptr, nullptr}
    };
#if SIP_ABI_MAJOR_VERSION >= 14
    sipExportSymbols(exports);
#else
    for (const Symbol *symbol = exports; symbol->name; ++symbol)
        sipExportSymbol(symbol->name, symbol->symbol);
#endif

    // Register the cleanup function.
    static PyMethodDef cleanup_md = {
        "_qtcore_cleanup", cleanup_on_exit, METH_NOARGS, SIP_NULLPTR
    };

#if SIP_ABI_MAJOR_VERSION >= 14
    PyObject *callback = PyCFunction_New(&cleanup_md, module);
    if (!callback)
        return;

    PyObject *atexit = PyImport_ImportModule("atexit");
    if (atexit)
    {
        PyObject *registered = PyObject_CallMethod(atexit, "register", "O", callback);
        Py_XDECREF(registered);
        Py_DECREF(atexit);
    }
    Py_DECREF(callback);
#else
    sipRegisterExitNotifier(&cleanup_md);
#endif
}
