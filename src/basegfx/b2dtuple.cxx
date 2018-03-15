/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * $RCSfile: b2dtuple.cxx,v $
 * $Revision: 1.14 $
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

#include <basegfx/tuple/b2dtuple.hxx>
#include <basegfx/numeric/ftools.hxx>
#include <rtl/instance.hxx>

namespace { struct EmptyTuple : public rtl::Static<basegfx::B2DTuple, EmptyTuple> {}; }
#include <basegfx/tuple/b2ituple.hxx>

namespace basegfx
{
    const B2DTuple& B2DTuple::getEmptyTuple()
    {
        return EmptyTuple::get();
    }

	B2DTuple::B2DTuple(const B2ITuple& rTup) 
	:	mfX( rTup.getX() ), 
		mfY( rTup.getY() ) 
	{}

	void B2DTuple::correctValues(const double fCompareValue)
	{
		if(0.0 == fCompareValue)
		{
			if(::basegfx::fTools::equalZero(mfX))
			{
				mfX = 0.0;
			}

			if(::basegfx::fTools::equalZero(mfY))
			{
				mfY = 0.0;
			}
		}
		else
		{
			if(::basegfx::fTools::equal(mfX, fCompareValue))
			{
				mfX = fCompareValue;
			}

			if(::basegfx::fTools::equal(mfY, fCompareValue))
			{
				mfY = fCompareValue;
			}
		}
	}

	B2ITuple fround(const B2DTuple& rTup)
	{
		return B2ITuple(fround(rTup.getX()), fround(rTup.getY()));
	}

} // end of namespace basegfx

// eof
