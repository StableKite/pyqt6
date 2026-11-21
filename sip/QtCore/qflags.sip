// This is the SIP interface definition for the QFlags based mapped types.
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


template<ENUM>
%MappedType QFlags<ENUM> /PyQtFlags=1, TypeHint="ENUM"/
{
%TypeHeaderCode
#include <qglobal.h>
%End

%ConvertToTypeCode
#if SIP_ABI_MAJOR_VERSION >= 14
ENUM enm{};
const bool is_enm = sipConvertToEnum(sipPy, &enm, sipType_ENUM) >= 0;

if (sipIsErr == nullptr)
{
    if (!is_enm)
        PyErr_Clear();

    return is_enm;
}

if (!is_enm)
{
    *sipIsErr = 1;
    return 0;
}

*sipCppPtr = new QFlags<ENUM>(enm);
#else
bool is_enm = true;
unsigned enm = (unsigned)sipConvertToEnum(sipPy, sipType_ENUM);

if (PyErr_Occurred())
{
    PyErr_Clear();
    is_enm = false;
}

if (sipIsErr == NULL)
    return is_enm;

*sipCppPtr = new QFlags<ENUM>(static_cast<ENUM>(enm));
#endif

return sipGetState(sipTransferObj);
%End

%ConvertFromTypeCode
#if SIP_ABI_MAJOR_VERSION >= 14
ENUM enm = static_cast<ENUM>(sipCpp->operator::QFlags<ENUM>::Int());
return sipConvertFromEnum(&enm, sipType_ENUM);
#else
return sipConvertFromEnum(sipCpp->operator::QFlags<ENUM>::Int(), sipType_ENUM);
#endif
%End
};
