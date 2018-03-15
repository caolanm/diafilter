/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * $RCSfile: string.cxx,v $
 * $Revision: 1.7 $
 *
 * This file is part of OpenOffice.org.
 *
 * OpenOffice.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3
 * only, as published by the Free Software Foundation.
 *
 * OpenOffice.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 3 for more details
 * (a copy is included in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 3 along with OpenOffice.org.  If not, see
 * <http://www.openoffice.org/license.html>
 * for a copy of the LGPLv3 License.
 *
 ************************************************************************/

#include "sal/config.h"

#include <cstddef>
#include <string.h>
#include <vector>
#include <algorithm>

#include <rtl/ustring.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/types.h>

#include <comphelper/string.hxx>

namespace comphelper { namespace string {

::rtl::OUString searchAndReplaceAllAsciiWithAscii(
    const ::rtl::OUString& _source, const sal_Char* _from, const sal_Char* _to,
    const sal_Int32 _beginAt )
{
    sal_Int32 fromLength = strlen( _from );
    sal_Int32 n = _source.indexOfAsciiL( _from, fromLength, _beginAt );
    if ( n == -1 )
        return _source;

    ::rtl::OUString dest( _source );
    ::rtl::OUString to( ::rtl::OUString::createFromAscii( _to ) );
    do
    {
        dest = dest.replaceAt( n, fromLength, to );
        n = dest.indexOfAsciiL( _from, fromLength, n + to.getLength() );
    }
    while ( n != -1 );

    return dest;
}

} }
