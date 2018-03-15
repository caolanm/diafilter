/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * $RCSfile: b3dtuple.cxx,v $
 * $Revision: 1.10 $
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

#include <basegfx/tuple/b3dtuple.hxx>
#include <rtl/instance.hxx>

namespace { struct EmptyTuple : public rtl::Static<basegfx::B3DTuple, EmptyTuple> {}; }
#include <basegfx/tuple/b3ituple.hxx>

namespace basegfx
{
    const B3DTuple& B3DTuple::getEmptyTuple()
    {
        return EmptyTuple::get();
    }

	B3DTuple::B3DTuple(const B3ITuple& rTup) 
	:	mfX( rTup.getX() ), 
		mfY( rTup.getY() ),
		mfZ( rTup.getZ() ) 
	{}

	B3ITuple fround(const B3DTuple& rTup)
	{
		return B3ITuple(fround(rTup.getX()), fround(rTup.getY()), fround(rTup.getZ()));
	}
} // end of namespace basegfx

// eof
