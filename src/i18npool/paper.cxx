/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * $RCSfile$
 * $Revision$
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

#include <sal/config.h>
#include <rtl/ustring.hxx>
#include <rtl/string.hxx>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/container/XNameAccess.hpp>

#include "i18npool/paper.hxx"

#include <utility>
#include <cstdlib>

#ifdef UNX
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>
#endif

struct PageDesc
{
    long m_nWidth;
    long m_nHeight;
    const char *m_pPSName;
    const char *m_pAltPSName;
};

#define PT2MM100( v ) \
    (long)(((v) * 35.27777778) + 0.5)

#define IN2MM100( v ) \
    ((long)(((v) * 2540) + 0.5))

#define MM2MM100( v ) \
    ((long)((v) * 100))

//PostScript Printer Description File Format Specification
//http://partners.adobe.com/public/developer/en/ps/5003.PPD_Spec_v4.3.pdf
//http://www.y-adagio.com/public/committees/docsii/doc_00-49/symp_ulaan/china_ppr.pdf (Kai)
//http://www.sls.psi.ch/controls/help/howto/Howto_Print_a_A0_Poster_at_WSLA_012_2.pdf (Dia)
static PageDesc aDinTab[] =
{
    { MM2MM100( 841 ),   MM2MM100( 1189 ),   "A0",  NULL },
    { MM2MM100( 594 ),   MM2MM100( 841 ),    "A1",  NULL },
    { MM2MM100( 420 ),   MM2MM100( 594 ),    "A2",  NULL },
    { MM2MM100( 297 ),   MM2MM100( 420 ),    "A3",  NULL },
    { MM2MM100( 210 ),   MM2MM100( 297 ),    "A4",  NULL },
    { MM2MM100( 148 ),   MM2MM100( 210 ),    "A5",  NULL },
    { MM2MM100( 250 ),   MM2MM100( 353 ),    "ISOB4",  NULL },
    { MM2MM100( 176 ),   MM2MM100( 250 ),    "ISOB5",  NULL },
    { IN2MM100( 8.5 ),   IN2MM100( 11 ),     "Letter",  "Note" },
    { IN2MM100( 8.5 ),   IN2MM100( 14 ),     "Legal",  NULL },
    { IN2MM100( 11 ),    IN2MM100( 17 ),     "Tabloid",  "11x17" },
    { 0,                 0,                  NULL, NULL }, //User
    { MM2MM100( 125 ),   MM2MM100( 176 ),    "ISOB6",  NULL },
    { MM2MM100( 229 ),   MM2MM100( 324 ),    "EnvC4",  "C4" },
    { MM2MM100( 162 ),   MM2MM100( 229 ),    "EnvC5",  "C5" },
    { MM2MM100( 114 ),   MM2MM100( 162 ),    "EnvC6",  "C6" },
    { MM2MM100( 114 ),   MM2MM100( 229 ),    "EnvC65", NULL },
    { MM2MM100( 110 ),   MM2MM100( 220 ),    "EnvDL",  "DL" },
    { MM2MM100( 180),    MM2MM100( 270 ),    NULL,  NULL }, //Dia
    { MM2MM100( 210),    MM2MM100( 280 ),    NULL,  NULL }, //Screen
    { IN2MM100( 17 ),    IN2MM100( 22 ),     "AnsiC",  "CSheet" },
    { IN2MM100( 22 ),    IN2MM100( 34 ),     "AnsiD",  "DSheet" },
    { IN2MM100( 34 ),    IN2MM100( 44 ),     "AnsiE",  "ESheet" },
    { IN2MM100( 7.25 ),  IN2MM100( 10.5 ),   "Executive",  NULL },
    //"Folio" is a different size in the PPD documentation than 8.5x11
    //This "FanFoldGermanLegal" is known in the Philippines as
    //"Legal" paper or "Long Bond Paper".  The "Legal" name causing untold
    //misery, given the differently sized US "Legal" paper
    { IN2MM100( 8.5 ),   IN2MM100( 13 ),     "FanFoldGermanLegal",  NULL },
    { IN2MM100( 3.875 ), IN2MM100( 7.5 ),    "EnvMonarch", "Monarch" },
    { IN2MM100( 3.625 ), IN2MM100( 6.5 ),    "EnvPersonal",  "Personal" },
    { IN2MM100( 3.875 ), IN2MM100( 8.875 ),  "Env9",  NULL },
    { IN2MM100( 4.125 ), IN2MM100( 9.5 ),    "Env10",  "Comm10" },
    { IN2MM100( 4.5 ),   IN2MM100( 10.375 ), "Env11",  NULL },
    { IN2MM100( 4.75 ),  IN2MM100( 11 ),     "Env12",  NULL },
    { MM2MM100( 184 ),   MM2MM100( 260 ),    NULL,  NULL }, //Kai16
    { MM2MM100( 130 ),   MM2MM100( 184 ),    NULL,  NULL }, //Kai32
    { MM2MM100( 140 ),   MM2MM100( 203 ),    NULL,  NULL }, //BigKai32
    { MM2MM100( 257 ),   MM2MM100( 364 ),    "B4",  NULL }, //JIS
    { MM2MM100( 182 ),   MM2MM100( 257 ),    "B5",  NULL }, //JIS
    { MM2MM100( 128 ),   MM2MM100( 182 ),    "B6",  NULL }, //JIS
    { IN2MM100( 17 ),    IN2MM100( 11 ),     "Ledger",  NULL },
    { IN2MM100( 5.5 ),   IN2MM100( 8.5 ),    "Statement",  NULL },
    { PT2MM100( 610 ),   PT2MM100( 780 ),    "Quarto",  NULL },
    { IN2MM100( 10 ),    IN2MM100( 14 ),     "10x14",  NULL },
    { IN2MM100( 5.5 ),   IN2MM100( 11.5 ),   "Env14",  NULL },
    { MM2MM100( 324 ),   MM2MM100( 458 ),    "EnvC3",  "C3" },
    { MM2MM100( 110 ),   MM2MM100( 230 ),    "EnvItalian",  NULL },
    { IN2MM100( 14.875 ),IN2MM100( 11 ),     "FanFoldUS",  NULL },
    { IN2MM100( 8.5 ),   IN2MM100( 13 ),     "FanFoldGerman",  NULL },
    { MM2MM100( 100 ),   MM2MM100( 148 ),    "Postcard",  NULL },
    { IN2MM100( 9 ),     IN2MM100( 11 ),     "9x11",  NULL },
    { IN2MM100( 10 ),    IN2MM100( 11 ),     "10x11",  NULL },
    { IN2MM100( 15 ),    IN2MM100( 11 ),     "15x11",  NULL },
    { MM2MM100( 220 ),   MM2MM100( 220 ),    "EnvInvite",  NULL },
    { MM2MM100( 227 ),   MM2MM100( 356 ),    "SuperA",  NULL },
    { MM2MM100( 305 ),   MM2MM100( 487 ),    "SuperB",  NULL },
    { IN2MM100( 8.5 ),   IN2MM100( 12.69 ),  "LetterPlus",  NULL },
    { IN2MM100( 8.5 ),   IN2MM100( 12.69 ),  "LetterPlus",  NULL },
    { MM2MM100( 210 ),   MM2MM100( 330 ),    "A4Plus",  NULL },
    { MM2MM100( 200 ),   MM2MM100( 148 ),    "DoublePostcard",  NULL },
    { MM2MM100( 105 ),   MM2MM100( 148 ),    "A6",  NULL },
    { IN2MM100( 12 ),    IN2MM100( 11 ),     "12x11",  NULL },
    { MM2MM100( 74 ),    MM2MM100( 105 ),    "A7",  NULL },
    { MM2MM100( 52 ),    MM2MM100( 74 ),     "A8",  NULL },
    { MM2MM100( 37 ),    MM2MM100( 52 ),     "A9",  NULL },
    { MM2MM100( 26 ),    MM2MM100( 37 ),     "A10",  NULL },
    { MM2MM100( 1000 ),  MM2MM100( 1414 ),   "ISOB0",  NULL },
    { MM2MM100( 707 ),   MM2MM100( 1000 ),   "ISOB1",  NULL },
    { MM2MM100( 500 ),   MM2MM100( 707 ),    "ISOB2",  NULL },
    { MM2MM100( 353 ),   MM2MM100( 500 ),    "ISOB3",  NULL },
    { MM2MM100( 88 ),    MM2MM100( 125 ),    "ISOB7",  NULL },
    { MM2MM100( 62 ),    MM2MM100( 88 ),     "ISOB8",  NULL },
    { MM2MM100( 44 ),    MM2MM100( 62 ),     "ISOB9",  NULL },
    { MM2MM100( 31 ),    MM2MM100( 44 ),     "ISOB10", NULL },
    { MM2MM100( 458 ),   MM2MM100( 648 ),    "EnvC2",  "C2" },
    { MM2MM100( 81 ),    MM2MM100( 114 ),    "EnvC7",  "C7" },
    { MM2MM100( 57 ),    MM2MM100( 81 ),     "EnvC8",  "C8" },
    { IN2MM100( 9 ),     IN2MM100( 12 ),     "ARCHA",  NULL },
    { IN2MM100( 12 ),    IN2MM100( 18 ),     "ARCHB",  NULL },
    { IN2MM100( 18 ),    IN2MM100( 24 ),     "ARCHC",  NULL },
    { IN2MM100( 24 ),    IN2MM100( 36 ),     "ARCHD",  NULL },
    { IN2MM100( 36 ),    IN2MM100( 48 ),     "ARCHE",  NULL }
};

static const size_t nTabSize = sizeof(aDinTab) / sizeof(aDinTab[0]);

#define MAXSLOPPY 11

bool PaperInfo::doSloppyFit()
{
    if (m_eType != PAPER_USER)
        return true;

    for ( size_t i = 0; i < nTabSize; ++i )
    {
        if (i == PAPER_USER) continue;

        long lDiffW = labs(aDinTab[i].m_nWidth - m_nPaperWidth);
        long lDiffH = labs(aDinTab[i].m_nHeight - m_nPaperHeight);

        if ( lDiffW < MAXSLOPPY && lDiffH < MAXSLOPPY )
        {
            m_nPaperWidth = aDinTab[i].m_nWidth;
            m_nPaperHeight = aDinTab[i].m_nHeight;
            m_eType = (Paper)i;
            return true;
        }
    }

    return false;
}

bool PaperInfo::sloppyEqual(const PaperInfo &rOther) const
{
    return 
    (
      (labs(m_nPaperWidth - rOther.m_nPaperWidth) < MAXSLOPPY) &&
      (labs(m_nPaperHeight - rOther.m_nPaperHeight) < MAXSLOPPY)
    );
}

long PaperInfo::sloppyFitPageDimension(long nDimension)
{
    for ( size_t i = 0; i < nTabSize; ++i )
    {
        if (i == PAPER_USER) continue;
        long lDiff;

        lDiff = labs(aDinTab[i].m_nWidth - nDimension);
        if ( lDiff < MAXSLOPPY )
            return aDinTab[i].m_nWidth;

        lDiff = labs(aDinTab[i].m_nHeight - nDimension);
        if ( lDiff < MAXSLOPPY )
            return aDinTab[i].m_nHeight;
    }
    return nDimension;
}

PaperInfo::PaperInfo(Paper eType) : m_eType(eType)
{
    m_nPaperWidth = aDinTab[m_eType].m_nWidth;
    m_nPaperHeight = aDinTab[m_eType].m_nHeight;
}

PaperInfo::PaperInfo(long nPaperWidth, long nPaperHeight)
    : m_eType(PAPER_USER),
      m_nPaperWidth(nPaperWidth),
      m_nPaperHeight(nPaperHeight)
{
    for ( size_t i = 0; i < nTabSize; ++i )
    {
        if ( 
             (nPaperWidth == aDinTab[i].m_nWidth) &&
             (nPaperHeight == aDinTab[i].m_nHeight)
           )
        {
            m_eType = static_cast<Paper>(i);
            break;
        }
    }
}

rtl::OString PaperInfo::toPSName(Paper ePaper)
{
    return static_cast<size_t>(ePaper) < nTabSize ?
        rtl::OString(aDinTab[ePaper].m_pPSName) : rtl::OString();
}

Paper PaperInfo::fromPSName(const rtl::OString &rName)
{
    if (!rName.getLength())
        return PAPER_USER;

    for ( size_t i = 0; i < nTabSize; ++i )
    {
        if (aDinTab[i].m_pPSName && 
          !rtl_str_compareIgnoreAsciiCase(aDinTab[i].m_pPSName, rName.getStr()))
        {
            return static_cast<Paper>(i);
        }
        else if (aDinTab[i].m_pAltPSName &&
          !rtl_str_compareIgnoreAsciiCase(aDinTab[i].m_pAltPSName, rName.getStr()))
        {
            return static_cast<Paper>(i);
        }
    }

    return PAPER_USER;
}

/* vi:set tabstop=4 shiftwidth=4 expandtab: */
