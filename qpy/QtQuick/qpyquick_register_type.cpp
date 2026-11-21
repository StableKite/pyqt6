// This is the implementation of the qml_register_type() helper.
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


#include "qpyquick_register_type.h"
#include "qpyquickframebufferobject.h"
#include "qpyquickitem.h"
#include "qpyquickpainteditem.h"
#include "qpyquickview.h"
#include "qpyquickwindow.h"

#include "qpyquick_sip.h"


sipErrorState qpyquick_register_type(PyTypeObject *py_type,
        const QMetaObject *mo, const QByteArray &ptr_name,
        const QByteArray &list_name, QQmlPrivate::RegisterType **rtp)
{
    // Make sure the types are tested in the right order (ie. more specific
    // types first).

    if (qpyquick_is_subtype(py_type, sipType_QQuickFramebufferObject))
        return ((*rtp = QPyQuickFramebufferObject::addType(py_type, mo, ptr_name, list_name)) ? sipErrorNone : sipErrorFail);

    if (qpyquick_is_subtype(py_type, sipType_QQuickPaintedItem))
        return ((*rtp = QPyQuickPaintedItem::addType(py_type, mo, ptr_name, list_name)) ? sipErrorNone : sipErrorFail);

    if (qpyquick_is_subtype(py_type, sipType_QQuickItem))
        return ((*rtp = QPyQuickItem::addType(py_type, mo, ptr_name, list_name)) ? sipErrorNone : sipErrorFail);

    if (qpyquick_is_subtype(py_type, sipType_QQuickView))
        return ((*rtp = QPyQuickView::addType(py_type, mo, ptr_name, list_name)) ? sipErrorNone : sipErrorFail);

    if (qpyquick_is_subtype(py_type, sipType_QQuickWindow))
        return ((*rtp = QPyQuickWindow::addType(py_type, mo, ptr_name, list_name)) ? sipErrorNone : sipErrorFail);

    // We don't recognise the type.
    return sipErrorContinue;
}
