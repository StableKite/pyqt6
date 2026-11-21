// This is the support for QRangeModel.
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

#include <qglobal.h>

#if QT_VERSION >= 0x060a00

#include "qpycore_qrangemodel.h"

#include "qpycore_api.h"
#include "qpycore_misc.h"

#include "sipAPIQtCore.h"


// Construct a range and take a strong reference to the Python object.
QPyAbstractRange::QPyAbstractRange(PyObject *data, bool editable) :
        m_data(data), m_editable(editable)
{
#if SIP_ABI_MAJOR_VERSION >= 14
    Py_INCREF(m_data);
    PyObject *module = qpycore_qtcore_module_ref();
    if (module)
    {
        m_state = static_cast<sipModuleState *>(PyModule_GetState(module));
        Py_DECREF(module);
        m_view = PyInterpreterView_FromCurrent();
    }
#else
    SIP_BLOCK_THREADS
    Py_INCREF(m_data);
    SIP_UNBLOCK_THREADS
#endif
}


// Destroy the range.
QPyAbstractRange::~QPyAbstractRange()
{
#if SIP_ABI_MAJOR_VERSION >= 14
    if (m_view)
    {
        if (auto *thread = PyThreadState_EnsureFromView(m_view))
        {
            Py_DECREF(m_data);
            PyThreadState_Release(thread);
        }
        PyInterpreterView_Close(m_view);
    }
#else
    SIP_BLOCK_THREADS
    Py_DECREF(m_data);
    SIP_UNBLOCK_THREADS
#endif
}


// Raise a Python exception regarding the structure of the data.
void QPyAbstractRange::raiseBadStructure()
{
    PyErr_SetString(PyExc_ValueError,
            "the range data does not have the correct structure");
}


// Populate a 1D range from a Python list.
bool QPyAbstractRange::populate1DFromList(QVariantList &range, PyObject *data)
{
    if (!PyList_Check(data))
    {
        raiseBadStructure();
        return false;
    }

    Py_ssize_t size = PyList_Size(data);
    if (size < 0)
        return false;

    for (Py_ssize_t i = 0; i < size; ++i)
    {
#if SIP_ABI_MAJOR_VERSION >= 14
        PyObject *val = PyList_GetItemRef(data, i);
#else
        PyObject *val = Py_XNewRef(PyList_GetItem(data, i));
#endif
        if (!val)
            return false;

        int is_err = 0;

        QVariant qv = qpycore_PyObject_AsQVariant(val, &is_err, moduleState());
        Py_DECREF(val);

        if (is_err)
            return false;

        range.append(qv);
    }

    return true;
}


// Populate a 1D range from a Python sequence.
bool QPyAbstractRange::populate1DFromSequence(QVariantList &range,
        PyObject *data)
{
    if (!PySequence_Check(data))
    {
        raiseBadStructure();
        return false;
    }

    Py_ssize_t size = PySequence_Size(data);
    if (size < 0)
        return false;

    for (Py_ssize_t i = 0; i < size; ++i)
    {
        PyObject *val = PySequence_GetItem(data, i);
        if (!val)
            return false;

        int is_err = 0;

        QVariant qv = qpycore_PyObject_AsQVariant(val, &is_err, moduleState());
        Py_DECREF(val);

        if (is_err)
            return false;

        range.append(qv);
    }

    return true;
}


// Construct a sequence range.
QPySequenceRange::QPySequenceRange(PyObject *data, bool editable) :
        QPyAbstractRange(data, editable)
{
}


// Create an appropriately configured QRangeModel for a sequence range.  This
// must be called with the GIL.
sipQRangeModel *QPySequenceRange::create(QObject *parent)
{
    sipQRangeModel *model = nullptr;

    if (editable())
    {
        if (populate1DFromList(m_seq_range, borrowedData()))
        {
            model = new sipQRangeModel(std::ref(m_seq_range), parent);
            QObject::connect(model, &QRangeModel::dataChanged, this,
                    &QPySequenceRange::handleDataChanged);
        }
    }
    else
    {
        if (populate1DFromSequence(m_seq_range, borrowedData()))
            model = new sipQRangeModel(std::cref(m_seq_range), parent);
    }

    if (model)
        setParent(model);

    return model;
};


// Handle the dataChanged() signal for a sequence range.
void QPySequenceRange::handleDataChanged(const QModelIndex &tl,
        const QModelIndex &br)
{
    Q_UNUSED(br);

    bool do_emit = false;

#if SIP_ABI_MAJOR_VERSION >= 14
    auto *view = interpreterView();
    auto *thread = view ? PyThreadState_EnsureFromView(view) : nullptr;
    if (!thread)
        return;

    PyObject *previous_error = PyErr_GetRaisedException();
    PyObject *module = qpycore_qtcore_module_ref(moduleState());
    PyObject *py_data = module ? qpycore_PyObject_FromQVariant(tl.data(),
            moduleState()) : nullptr;
#else
    SIP_BLOCK_THREADS

    PyObject *py_data = qpycore_PyObject_FromQVariant(tl.data());
#endif
    if (py_data)
        do_emit = (PyList_SetItem(borrowedData(), tl.row(), py_data) == 0);

#if SIP_ABI_MAJOR_VERSION >= 14
    Py_XDECREF(module);
    PyErr_SetRaisedException(previous_error);
    PyThreadState_Release(thread);
#else
    SIP_UNBLOCK_THREADS
#endif

    // Emit the signal after detaching from the interpreter.
    if (do_emit)
        emit dataChanged(tl.row());
}


// Construct a table range.
QPyTableRange::QPyTableRange(PyObject *data, bool editable) :
        QPyAbstractRange(data, editable)
{
}


// Create an appropriately configured QRangeModel for a table range.  This must
// be called with the GIL.
sipQRangeModel *QPyTableRange::create(QObject *parent)
{
    if (!populate(m_table_range, borrowedData()))
        return nullptr;

    sipQRangeModel *model;

    if (editable())
    {
        model = new sipQRangeModel(std::ref(m_table_range), parent);
        QObject::connect(model, &QRangeModel::dataChanged, this,
                    &QPyTableRange::handleDataChanged);
    }
    else
    {
        model = new sipQRangeModel(std::cref(m_table_range), parent);
    }

    setParent(model);

    return model;
};


// Handle the dataChanged() signal for a table range.
void QPyTableRange::handleDataChanged(const QModelIndex &tl,
        const QModelIndex &br)
{
    Q_UNUSED(br);

    bool do_emit = false;

#if SIP_ABI_MAJOR_VERSION >= 14
    auto *view = interpreterView();
    auto *thread = view ? PyThreadState_EnsureFromView(view) : nullptr;
    if (!thread)
        return;

    PyObject *previous_error = PyErr_GetRaisedException();
    PyObject *module = qpycore_qtcore_module_ref(moduleState());
    PyObject *py_data = module ? qpycore_PyObject_FromQVariant(tl.data(),
            moduleState()) : nullptr;
#else
    SIP_BLOCK_THREADS

    PyObject *py_data = qpycore_PyObject_FromQVariant(tl.data());
#endif
    if (py_data)
    {
        PyObject *row_data = PySequence_GetItem(borrowedData(), tl.row());

        if (row_data != NULL)
        {
            do_emit = (PyList_SetItem(row_data, tl.column(), py_data) == 0);
            Py_DECREF(row_data);
        }
        else
        {
            Py_DECREF(py_data);
        }
    }

#if SIP_ABI_MAJOR_VERSION >= 14
    Py_XDECREF(module);
    PyErr_SetRaisedException(previous_error);
    PyThreadState_Release(thread);
#else
    SIP_UNBLOCK_THREADS
#endif

    // Emit the signal after detaching from the interpreter.
    if (do_emit)
        emit dataChanged(tl.row(), tl.column());
}


// Populate a table range.
bool QPyTableRange::populate(QList<QVariantList> &range, PyObject *data)
{
    bool ok = true;

    if (PySequence_Check(data))
    {
        Py_ssize_t size = PySequence_Size(data);
        if (size >= 0)
        {
            Py_ssize_t nr_cols = -1;

            for (Py_ssize_t i = 0; i < size; ++i)
            {
                PyObject *val = PySequence_GetItem(data, i);
                if (!val)
                {
                    ok = false;
                    break;
                }

                QVariantList qvl;

                if (editable())
                    ok = populate1DFromList(qvl, val);
                else
                    ok = populate1DFromSequence(qvl, val);

                Py_DECREF(val);

                if (!ok)
                    break;

                if (nr_cols < 0)
                {
                    nr_cols = qvl.size();
                }
                else if (nr_cols != qvl.size())
                {
                    PyErr_SetString(PyExc_ValueError,
                            "the range data does not have the same number of columns in each row");
                    ok = false;
                    break;
                }

                range.append(qvl);
            }
        }
        else
        {
            ok = false;
        }
    }
    else
    {
        raiseBadStructure();
        ok = false;
    }

    return ok;
}

#endif
