// SPDX-License-Identifier: GPL-3.0-only

#ifndef QPYCORE_QIODEVICEBUFFER_H
#define QPYCORE_QIODEVICEBUFFER_H

#include <QtCore/qglobal.h>

class QPyIODeviceBuffer
{
public:
    explicit QPyIODeviceBuffer(PyObject *object, bool writable = false)
    {
#if defined(Py_LIMITED_API) && Py_LIMITED_API < 0x030b0000
        acquired = sipGetBufferInfo(object, &view) > 0;

        if (acquired && writable && view.bi_readonly)
        {
            sipReleaseBufferInfo(&view);
            acquired = false;
            PyErr_SetString(PyExc_BufferError, "a writable buffer is required");
        }

        if (acquired)
        {
            data = view.bi_buf;
            size = view.bi_len;
        }
#else
        acquired = PyObject_GetBuffer(object, &view,
                writable ? PyBUF_WRITABLE : PyBUF_SIMPLE) == 0;

        if (acquired)
        {
            data = view.buf;
            size = view.len;
        }
#endif
    }

    ~QPyIODeviceBuffer()
    {
        if (acquired)
        {
#if defined(Py_LIMITED_API) && Py_LIMITED_API < 0x030b0000
            sipReleaseBufferInfo(&view);
#else
            PyBuffer_Release(&view);
#endif
        }
    }

    explicit operator bool() const { return acquired; }

    void *data = nullptr;
    Py_ssize_t size = 0;

private:
    Q_DISABLE_COPY(QPyIODeviceBuffer)
    bool acquired;
#if defined(Py_LIMITED_API) && Py_LIMITED_API < 0x030b0000
    sipBufferInfoDef view;
#else
    Py_buffer view;
#endif
};

#endif
