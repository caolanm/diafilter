/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * $RCSfile: b2dpoint.cxx,v $
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

#include <basegfx/point/b2dpoint.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/numeric/ftools.hxx>

//////////////////////////////////////////////////////////////////////////////

namespace basegfx
{
	B2DPoint& B2DPoint::operator=( const ::basegfx::B2DTuple& rPoint ) 
	{ 
		mfX = rPoint.getX();
		mfY = rPoint.getY(); 
		return *this; 
	}

	B2DPoint& B2DPoint::operator*=( const ::basegfx::B2DHomMatrix& rMat )
	{
		double fTempX(
			rMat.get(0, 0) * mfX + 
			rMat.get(0, 1) * mfY + 
			rMat.get(0, 2));
		double fTempY(
			rMat.get(1, 0) * mfX + 
			rMat.get(1, 1) * mfY + 
			rMat.get(1, 2));

		if(!rMat.isLastLineDefault())
		{
			const double fOne(1.0);
			const double fTempM(
				rMat.get(2, 0) * mfX + 
				rMat.get(2, 1) * mfY + 
				rMat.get(2, 2));

			if(!fTools::equalZero(fTempM) && !fTools::equal(fOne, fTempM))
			{
				fTempX /= fTempM;
				fTempY /= fTempM;
			}
		}

		mfX = fTempX;
		mfY = fTempY;

		return *this;
	}

	B2DPoint operator*( const ::basegfx::B2DHomMatrix& rMat, const B2DPoint& rPoint )
	{
		B2DPoint aRes( rPoint );
		return aRes *= rMat;
	}
} // end of namespace basegfx

//////////////////////////////////////////////////////////////////////////////
// eof
