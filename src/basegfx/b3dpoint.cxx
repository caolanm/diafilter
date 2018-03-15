/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * $RCSfile: b3dpoint.cxx,v $
 * $Revision: 1.9 $
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

#include <basegfx/point/b3dpoint.hxx>
#include <basegfx/matrix/b3dhommatrix.hxx>
#include <basegfx/numeric/ftools.hxx>

namespace basegfx
{
	B3DPoint& B3DPoint::operator*=( const ::basegfx::B3DHomMatrix& rMat )
	{
		double fTempX(
			rMat.get(0, 0) * mfX +
			rMat.get(0, 1) * mfY +
			rMat.get(0, 2) * mfZ +
			rMat.get(0, 3));
		double fTempY(
			rMat.get(1, 0) * mfX +
			rMat.get(1, 1) * mfY +
			rMat.get(1, 2) * mfZ +
			rMat.get(1, 3));
		double fTempZ(
			rMat.get(2, 0) * mfX +
			rMat.get(2, 1) * mfY +
			rMat.get(2, 2) * mfZ +
			rMat.get(2, 3));

		if(!rMat.isLastLineDefault())
		{
			const double fOne(1.0);
			const double fTempM(
				rMat.get(3, 0) * mfX + 
				rMat.get(3, 1) * mfY + 
				rMat.get(3, 2) * mfZ + 
				rMat.get(3, 3));

			if(!fTools::equalZero(fTempM) && !fTools::equal(fOne, fTempM))
			{
				fTempX /= fTempM;
				fTempY /= fTempM;
				fTempZ /= fTempM;
			}
		}

		mfX = fTempX;
		mfY = fTempY;
		mfZ = fTempZ;

		return *this;
	}

	B3DPoint operator*( const ::basegfx::B3DHomMatrix& rMat, const B3DPoint& rPoint )
	{
		B3DPoint aRes( rPoint );
		return aRes *= rMat;
	}
} // end of namespace basegfx

// eof
