/*************************************************************************
 *
 * Caolán McNamara
 * Copyright 2010 by Red Hat, Inc. 
 *
 * openoffice.org-diafilter is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 3 or
 * later, as published by the Free Software Foundation.
 *
 * openoffice.org-diafilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 *
 * You should have received a copy of the GNU General Public License version 3
 * along with openoffice.org-diafilter.  If not, see
 * <http://www.gnu.org/copyleft/gpl.html> for a copy of the GPLv3 License.
 *
 ************************************************************************/

#include <com/sun/star/deployment/XPackageInformationProvider.hpp>
#include <com/sun/star/deployment/DeploymentException.hpp>

#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>

#include <com/sun/star/ucb/XSimpleFileAccess.hpp>

#include <com/sun/star/io/XSeekable.hpp>

#include <com/sun/star/xml/sax/XDocumentHandler.hpp>
#include <com/sun/star/xml/dom/XDocumentBuilder.hpp>
#include <comphelper/string.hxx>
#include <i18npool/paper.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>
#include <basegfx/curve/b2dcubicbezier.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>

#include <osl/file.hxx>
#include <osl/security.hxx>

#include "filters.hxx"
#include "shapefilter.hxx"
#include "gz_inputstream.hxx"

#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <math.h>
#include <stdio.h>

DIAFilter::DIAFilter( const uno::Reference< uno::XComponentContext >& rxCtx )
    : mxCtx(rxCtx), mxMSF( rxCtx->getServiceManager(), uno::UNO_QUERY_THROW )
{
}

class DiaObject;
typedef boost::shared_ptr<DiaObject> diaobject;
typedef std::pair< diaobject, PropertyMap > shape;
typedef boost::shared_ptr<ShapeImporter> shapeimporter;
typedef std::vector< shape > shapes;
typedef std::map< rtl::OUString, diaobject > objectmap;

class DiaImporter
{
private:
    uno::Reference< uno::XComponentContext > mxCtx;
    uno::Reference< lang::XMultiServiceFactory > mxMSF;

    uno::Reference < xml::sax::XDocumentHandler > mxDocHandler;
    uno::Reference < xml::dom::XElement > mxDocElem;
    rtl::OUString msInstallDir;

    float mnTop;
    float mnLeft;

    shapes maShapes;
    objectmap mapId;

    typedef std::map<rtl::OUString, shapeimporter> templates;
    templates maTemplates;

    autostyles maDashes;
    autostyles maArrows;
    TextStyleManager maTextStyles;
    GraphicStyleManager maGraphicStyles;

    boost::scoped_ptr<autostyle> page_layout_properties;
    boost::scoped_ptr<autostyle> drawing_page_properties;

public:
    DiaImporter(uno::Reference< uno::XComponentContext > xCtx,
        uno::Reference< lang::XMultiServiceFactory > xMSF,
        uno::Reference < xml::sax::XDocumentHandler > xDocHandler,
        uno::Reference < xml::dom::XElement > xDocElem,
        const rtl::OUString &rInstallDir);
    bool convert();
    void handleDiagramDataPaperAttribute(const uno::Reference<xml::dom::XElement> &rxElem, PropertyMap &rAttrs);
    void handleDiagramDataPaperComposite(const uno::Reference<xml::dom::XElement> &rxElem);
    void handleDiagramDataPaper(const uno::Reference<xml::dom::XElement> &rxElem);
    void handleDiagramDataBackGroundColor(const uno::Reference<xml::dom::XElement> &rxElem);
    void handleDiagramDataBackGround(const uno::Reference<xml::dom::XElement> &rxElem);
    void handleDiagramDataAttribute(const uno::Reference<xml::dom::XElement> &rxElem);
    void handleDiagramData(const uno::Reference<xml::dom::XElement> &rxElem);
    void addStrokeDash(PropertyMap &rStyleAttrs, sal_Int32 nLineStyle, float nDashLength);
    void addAutomaticGraphicStyle(PropertyMap &rAttrs, const PropertyMap &rStyleAttrs)
        { maGraphicStyles.addAutomaticGraphicStyle(rAttrs, rStyleAttrs); }
    void addAutomaticTextStyle(PropertyMap &rAttrs, ParaTextStyle &rStyleAttrs)
        { maTextStyles.addAutomaticTextStyle(rAttrs, rStyleAttrs); }
    PropertyMap handleStandardObject(const uno::Reference<xml::dom::XElement> &rxElem);
    void handleObject(const uno::Reference<xml::dom::XElement> &rxElem, shapes &rShapes);
    void handleGroup(const uno::Reference<xml::dom::XElement> &rxElem, shapes &rShapes);
    void handleLayer(const uno::Reference<xml::dom::XElement> &rxElem);
    bool handleDiagram(const uno::Reference<xml::dom::XElement> &rxElem);

    shapeimporter findCustomImporter(const rtl::OUString &rName);
    GraphicStyleManager& getGraphicStyleManager() { return maGraphicStyles; }
    TextStyleManager& getTextStyleManager() { return maTextStyles; }
    const GraphicStyleManager& getGraphicStyleManager() const { return maGraphicStyles; }
    const TextStyleManager& getTextStyleManager() const { return maTextStyles; }

    void recursiveScan(const rtl::OUString &rDir);
    void importShape(const rtl::OUString &rShapeFile) throw();

    //Dia positions are relative to the page margins
    //while draw's are relative to the paper
    float adjustX(float nX) const
    {
        return nX + mnLeft;
    }
    float adjustY(float nY) const
    {
        return nY + mnTop;
    }
    float unadjustX(float nX) const
    {
        return nX - mnLeft;
    }
    float unadjustY(float nY) const
    {
        return nY - mnTop;
    }

    //Seeing as so many people draw diagrams that span multiple pages
    //give up and create a single sheet big enough to contain everything
    void adjustPageSize(PropertyMap &rPageProps);

    void resizeNarrowShapes();
    void adjustConnectionPoints();
    void writeShapes();
    void writeResults();

    diaobject getobjectbyid(const rtl::OUString &rId) const;
};

DiaImporter::DiaImporter(uno::Reference< uno::XComponentContext > xCtx,
        uno::Reference< lang::XMultiServiceFactory > xMSF,
        uno::Reference < xml::sax::XDocumentHandler > xDocHandler,
        uno::Reference < xml::dom::XElement > xDocElem,
        const rtl::OUString &rInstallDir)
        : mxCtx(xCtx)
        , mxMSF(xMSF)
        , mxDocHandler(xDocHandler)
        , mxDocElem(xDocElem)
        , msInstallDir(rInstallDir)
        , mnTop(0)
        , mnLeft(0)
{
    maTextStyles.makeReferenceDevice(mxCtx);
}

void TextStyleManager::makeReferenceDevice(uno::Reference< uno::XComponentContext > xCtx)
{
    uno::Reference< frame::XComponentLoader > xComponentLoader(
        xCtx->getServiceManager()->createInstanceWithContext(USTR("com.sun.star.frame.Desktop"),
            xCtx), uno::UNO_QUERY_THROW);

    uno::Sequence < beans::PropertyValue > aArgs(1);
    aArgs[0].Name = USTR("Hidden");
    aArgs[0].Value <<= sal_True;

    uno::Reference< lang::XComponent > xComponent(xComponentLoader->loadComponentFromURL(
        rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("private:factory/sdraw")),
        rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("_blank")), 0, aArgs));

    uno::Reference< frame::XModel > xModel(xComponent, uno::UNO_QUERY_THROW);
    uno::Reference< frame::XController> xController = xModel->getCurrentController();
    uno::Reference< frame::XFrame > xFrame( xController->getFrame() );

    uno::Reference< awt::XWindow > xWindow( xFrame->getContainerWindow() );
    mxReferenceDevice = uno::Reference< awt::XDevice >(xWindow, uno::UNO_QUERY_THROW);
}

SaxAttrList *makeXAttribute(const PropertyMap &rAttrs)
{
    SaxAttrList *pSaxAttrList = new SaxAttrList(rAttrs);
    return pSaxAttrList;
}

SaxAttrList *makeXAttributeAndClear(PropertyMap &rAttrs)
{
    SaxAttrList *pSaxAttrList = makeXAttribute(rAttrs);
    rAttrs.clear();
    return pSaxAttrList;
}

//Draw isn't really accurate enough unless we bump the points and viewports up
//by at least 10
void bumpPoints(PropertyMap& rProps, sal_Int32 nMul = 10)
{
    rtl::OUString sPoints = rProps[USTR("draw:points")];
    rtl::OUString sNewPoints;

    sal_Int32 nIndex = 0;
    do
    {
        rtl::OUString x = sPoints.getToken(0, ',', nIndex);
        rtl::OUString y = sPoints.getToken(0, ' ', nIndex);
        if (sNewPoints.getLength())
            sNewPoints = sNewPoints + USTR(" ");
        sNewPoints = sNewPoints +
            rtl::OUString::number(x.toFloat()*nMul) + USTR(",") + rtl::OUString::number(y.toFloat()*nMul);
    }
    while ( nIndex >= 0 );

    rProps[USTR("draw:points")] = sNewPoints;
}

void createViewportFromRect(PropertyMap& rProps)
{
    rtl::OUString x = rtl::OUString::number(::comphelper::string::searchAndReplaceAllAsciiWithAscii(rProps[USTR("svg:x")], "cm", "").toFloat()*10);
    rtl::OUString y = rtl::OUString::number(::comphelper::string::searchAndReplaceAllAsciiWithAscii(rProps[USTR("svg:y")], "cm", "").toFloat()*10);
    rtl::OUString width = rtl::OUString::number(::comphelper::string::searchAndReplaceAllAsciiWithAscii(rProps[USTR("svg:width")], "cm", "").toFloat()*10);
    rtl::OUString height = rtl::OUString::number(::comphelper::string::searchAndReplaceAllAsciiWithAscii(rProps[USTR("svg:height")], "cm", "").toFloat()*10);

    rProps[USTR("svg:viewBox")] = x + USTR(" ") + y + USTR(" ") + width + USTR(" ") + height;

    bumpPoints(rProps);
}

namespace
{
    void reportUnknownElement(const uno::Reference<xml::dom::XElement> &rxElem)
    {
        fprintf(stderr, "Unknown tag %s\n", rtl::OUStringToOString(rxElem->getTagName(), RTL_TEXTENCODING_UTF8).getStr());
    }

    void createPoints(PropertyMap &rAttrs, const rtl::OUString &rPoints, const DiaImporter &rImporter)
    {
        sal_Int32 nPairCount = 1;
        sal_Int32 nIndex = 0;
        do
        {
            float nX = rPoints.getToken(0, ',', nIndex).toFloat();
            nX = rImporter.adjustX(nX);
            float nY = rPoints.getToken(0, ' ', nIndex).toFloat();
            nY = rImporter.adjustY(nY);
            rAttrs[USTR("svg:x")+rtl::OUString::number(nPairCount)] =
                rtl::OUString::number(nX)+USTR("cm");
            rAttrs[USTR("svg:y")+rtl::OUString::number(nPairCount)] =
                rtl::OUString::number(nY)+USTR("cm");
            ++nPairCount;
        }
        while ( nIndex >= 0 );
    }

    rtl::OUString GetArrowName(sal_Int32 nArrow)
    {
        rtl::OUString sArrow;
        switch (nArrow)
        {
            default:
            case 1:
                sArrow=USTR("Arrow_20_lines");
                break;
            case 2:
                sArrow=USTR("Hollow_20_triangle");
                break;
            case 3:
                sArrow=USTR("Filled_20_triangle");
                break;
            case 4:
                sArrow=USTR("Hollow_20_Diamond");
                break;
            case 5:
                sArrow=USTR("Filled_20_Diamond");
                break;
            case 6:
                sArrow=USTR("Half_20_Head");
                break;
            case 7:
                sArrow=USTR("Slashed_20_Cross");
                break;
            case 8:
                sArrow=USTR("Filled_20_ellipse");
                break;
            case 9:
                sArrow=USTR("Hollow_20_ellipse");
                break;
            case 10:
                sArrow=USTR("Double_20_hollow_20_triangle");
                break;
            case 11:
                sArrow=USTR("Double_20_filled_20_triangle");
                break;
            case 12:
                sArrow=USTR("Unfilled_20_triangle");
                break;
            case 13:
                sArrow=USTR("Filled_20_dot");
                break;
            case 14:
                sArrow=USTR("Dimension_20_origin");
                break;
            case 15:
                sArrow=USTR("Blanked_20_dot");
                break;
            case 16:
                sArrow=USTR("Filled_20_box");
                break;
            case 17:
                sArrow=USTR("Blanked_20_box");
                break;
            case 18:
                sArrow=USTR("Slash_20_arrow");
                break;
            case 19:
                sArrow=USTR("Integral_symbol");
                break;
            case 20:
                sArrow=USTR("Crow_foot");
                break;
            case 21:
                sArrow=USTR("Cross");
                break;
            case 22:
                sArrow=USTR("Filled_20_concave");
                break;
            case 23:
                sArrow=USTR("Blanked_20_concave");
                break;
            case 24:
                sArrow=USTR("Rounded");
                break;
            case 25:
                sArrow=USTR("Half_20_diamond");
                break;
            case 26:
                sArrow=USTR("Open_20_rounded");
                break;
            case 27:
                sArrow=USTR("Filled_20_Dot_20_and_20_Triangle");
                break;
            case 28:
                sArrow=USTR("One_20_or_20_many");
                break;
            case 29:
                sArrow=USTR("None_20_or_20_many");
                break;
            case 30:
                sArrow=USTR("One_20_or_20_none");
                break;
            case 31:
                sArrow=USTR("One_20_exactly");
                break;
            case 32:
                sArrow=USTR("Arrow_20_backslash");
                break;
            case 33:
                sArrow=USTR("Arrow_20_three_20_dots");
                break;
        }
        return sArrow;
    }

    class EqualStyle :
        public std::unary_function<const autostyle &, bool>
    {
    private:
        const PropertyMap &mrStyle;
    public:
        EqualStyle(const PropertyMap &rStyle) : mrStyle(rStyle) {};
        bool operator()(const autostyle &rStyle)
            {return mrStyle == rStyle.second;}
    };

    class EqualParaTextStyle :
        public std::unary_function<const extendedautostyle &, bool>
    {
    private:
        const ParaTextStyle &mrStyle;
    public:
        EqualParaTextStyle(const ParaTextStyle &rStyle) : mrStyle(rStyle) {};
        bool operator()(const extendedautostyle &rStyle)
        {
            return mrStyle.maTextAttrs == rStyle.second.maTextAttrs &&
                   mrStyle.maParaAttrs == rStyle.second.maParaAttrs;
        }
    };

    PropertyMap makeDash(float nLen)
    {
        PropertyMap aAttrs;
        aAttrs[USTR("draw:style")] = USTR("rect");
        aAttrs[USTR("draw:dots1")] = USTR("1");
        aAttrs[USTR("draw:dots1-length")]  = rtl::OUString::number(nLen) + USTR("cm");
        aAttrs[USTR("draw:distance")] = rtl::OUString::number(nLen) + USTR("cm");
        return aAttrs;
    }

    PropertyMap makeDashDot(float nLen)
    {
        PropertyMap aAttrs;
        aAttrs[USTR("draw:style")] = USTR("rect");
        aAttrs[USTR("draw:dots1")] = USTR("1");
        aAttrs[USTR("draw:dots1-length")] = rtl::OUString::number(nLen) + USTR("cm");
        aAttrs[USTR("draw:dots2")] = USTR("1");
        aAttrs[USTR("draw:distance")] = rtl::OUString::number(nLen*0.45) + USTR("cm");
        return aAttrs;
    }

    PropertyMap makeDashDotDot(float nLen)
    {
        PropertyMap aAttrs;
        aAttrs[USTR("draw:style")] = USTR("rect");
        aAttrs[USTR("draw:dots1")] = USTR("1");
        aAttrs[USTR("draw:dots1-length")] = rtl::OUString::number(nLen) + USTR("cm");
        aAttrs[USTR("draw:dots2")] = USTR("2");
        aAttrs[USTR("draw:distance")] =  rtl::OUString::number(nLen*0.225) + USTR("cm");
        return aAttrs;
    }

    PropertyMap makeDot(float nLen)
    {
        PropertyMap aAttrs;
        aAttrs[USTR("draw:style")] = USTR("rect");
        aAttrs[USTR("draw:dots1")] = USTR("1");
        aAttrs[USTR("draw:dots1-length")] = rtl::OUString::number(nLen/10.0) + USTR("cm");
        aAttrs[USTR("draw:distance")] = rtl::OUString::number(nLen*0.1) + USTR("cm");
        return aAttrs;
    }

    PropertyMap makeArrow(sal_Int32 nArrow)
    {
        //In practice OOo is only allowing polygons, which is a problem
        PropertyMap aAttrs;
        rtl::OUString sPoints;
        switch (nArrow)
        {
            case 1:
            default:
                //sPoints = USTR("160.75,173.233 150.75,153.233 140.75,173.233");
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 20 30");
                aAttrs[USTR("svg:d")] = USTR("m10 0-10 30h20z");
                return aAttrs;
            case 2: //can't render hollow 
            case 3:
                sPoints = USTR("160.75,173.233 150.75,153.233 140.75,173.233");
                break;
            case 4: //can't render hollow
            case 5:
                sPoints = USTR("150.75,153.233 160.75,163.233 150.75,173.233 140.75,163.233");
                break;
            case 6:
                //can't render
                return makeArrow(1);
            case 7:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M150.75,153.233 L150.75,143.233, M140.75,143.233 L160.75,163.233 M140.75,153.233 L160.75,153.233Z");
                return makeArrow(1);
            case 8:
            case 9: //can't render hollow
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 1131 1131");
                aAttrs[USTR("svg:d")] = USTR("m462 1118-102-29-102-51-93-72-72-93-51-102-29-102-13-105 13-102 29-106 51-102 72-89 93-72 102-50 102-34 106-9 101 9 106 34 98 50 93 72 72 89 51 102 29 106 13 102-13 105-29 102-51 102-72 93-93 72-98 51-106 29-101 13z");
                return aAttrs;
            case 10: //can't render hollow
            case 11:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 1131 1918");
                aAttrs[USTR("svg:d")] = USTR("m737 1131h394l-564-1131-567 1131h398l-398 787h1131z");
                return aAttrs;
            case 12: //can't render hollow
                sPoints = USTR("160.75,173.233 150.75,153.233 140.75,173.233");
                break;
            case 13:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 100");
                aAttrs[USTR("svg:d")] = USTR("M100,0 C125,0 150,25 150,50 C150,75 125,100 100,100 C75,100 50,75 50,50 C50,25 75,0 100,0z M0,50 L200,50");
                return aAttrs;
            case 14:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 100");
                aAttrs[USTR("svg:d")] = USTR("M100,0 C125,0 150,25 150,50 C150,75 125,100 100,100 C75,100 50,75 50,50 C50,25 75,0 100,0 M0,50 L200,50");
                return aAttrs;
            case 15:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 100");
                aAttrs[USTR("svg:d")] = USTR("M100,0 C125,0 150,25 150,50 C150,75 125,100 100,100 C75,100 50,75 50,50 C50,25 75,0 100,0 M0,50 L200,50");
                return aAttrs;
            case 16:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 100");
                aAttrs[USTR("svg:d")] = USTR("M50,0 L150,0 L150,100 L50,100z M0,50 L200,50");
                return aAttrs;
            case 17:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 100");
                aAttrs[USTR("svg:d")] = USTR("M50,0 L150,0 L150,100 L50,100z M0,50 L200,50");
                return aAttrs;
            case 18:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M100,100 L100,200 M0,100 L200,100 M20,20 L180,180");
                return makeArrow(1);
            case 19:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M20,20 C20,90 180,110 180,180 M100,100 L100,200 M0,100 L200,100");
                return makeArrow(1);
            case 20:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M100,200 L200,0 M100,200 L0,0 M100,20 L100,0");
                return makeArrow(1);
            case 21:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M0,100 L200,100 M100,0 L100,200");
                return makeArrow(1);
            case 22:
            case 23: //can't render hollow
                sPoints = USTR("200.015,127.748 209.937,147.786 199.957,142.748 189.937,147.709");
                break;
            case 24:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M200,100 A 100,100 0 0 0 3.1739e-06,100 M100,200 L100,0");
                return makeArrow(1);
            case 25:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M200,100 L100,200 L0,100");
                return makeArrow(1);
            case 26:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 100");
                //aAttrs[USTR("svg:d")] = USTR("M3.1739e-06,100 A100,100 0 10 0 200,100");
                return makeArrow(1);
            case 27:
                aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 400");
                aAttrs[USTR("svg:d")] = USTR("M 200,100 C 200,155.22847 155.22847,200 100,200 44.771525,200 0,155.22847 0,100 0,44.771525 44.771525,0 100,0 155.22847,0 200,44.771525 200,100 z M 200,400 100,200 0,400 z");
                return aAttrs;
            case 28:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 400");
                //aAttrs[USTR("svg:d")] = USTR("M100,200 L200,0 M100,200 L0,0 M200,200 L 0,200 M100,200 L100,0");
                return makeArrow(1);
            case 29:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 400");
                //aAttrs[USTR("svg:d")] = USTR("M 100 250 C 150,250 200,275.832 200,300 C 200,325.832 150,350 100,350 C 50,350 0,325.832 0,300 C 0,275.832 50,250 100,250 M100,200 L200,0 M100,200 L0,0 M100,400 L10,0");
                return makeArrow(1);
            case 30:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 400");
                //aAttrs[USTR("svg:d")] = USTR("M 100 250 C 150,250 200,278.32 200,300 C 200,325.832 150,350 100,350 C 50,350 0,325.832 0,300 C 0,275.832 50,250 100,250 M0,100 L200,100 M100,400 L100,0");
                return makeArrow(1);
            case 31:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 200");
                //aAttrs[USTR("svg:d")] = USTR("M0,200 L200,200 M0,100 L200,100 M100,200 L100,0");
                return makeArrow(1);
            case 32:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 200 300");
                //aAttrs[USTR("svg:d")] = USTR("M0,300 L200,100 M100,200 L100,0");
                return makeArrow(1);
            case 33:
                //can't render
                //aAttrs[USTR("svg:viewBox")] = USTR("0 0 1 300");
                //aAttrs[USTR("svg:d")] = USTR("M0,0 L0,80 M0,134.165 L0,214.165 M0,267.499 L0,347.499");
                return makeArrow(1);
        }
        createViewportAndPolygonFromPoints(sPoints, aAttrs);

        return aAttrs;
    }

    rtl::OUString valueOfSimpleAttribute(const uno::Reference<xml::dom::XElement> &rxElem)
    {
        rtl::OUString sRet;

        uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
        const sal_Int32 nNumNodes( xChildren->getLength() );
        for( sal_Int32 i=0; i<nNumNodes; ++i )
        {
            if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
            {
                uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
                const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = xElem->getAttributes();
                uno::Reference<xml::dom::XNode> xNode;
                if (xAttributes.is())
                    xNode = xAttributes->getNamedItem(USTR("val"));
                rtl::OUString sToken;
                if (xNode.is())
                    sToken = xNode->getNodeValue();
                else
                {
                    uno::Reference<xml::dom::XNodeList> xSubChildren( xElem->getChildNodes() );
                    const sal_Int32 nNumSubNodes( xSubChildren->getLength() );
                    if( nNumSubNodes == 1 && xSubChildren->item(0)->getNodeType() == xml::dom::NodeType_TEXT_NODE )
                        sToken = xSubChildren->item(0)->getNodeValue();
                }

                if (sRet.getLength() && sToken.getLength())
                    sRet = sRet + USTR(" ");

                if (sToken.getLength())
                    sRet = sRet + sToken;
            }
        }

        return sRet;
    }

    void makeCurvedPathFromPoints(PropertyMap& rProps, bool bClose)
    {
        rtl::OUString sPoints = rProps[USTR("draw:points")];
        sal_Int32 nIndex = 0;
        rtl::OUString sStart = sPoints.getToken(0, ' ', nIndex);
        rtl::OUString sPath = USTR("M") + sStart;
        while ( nIndex >= 0 )
        {
            sPath = sPath + USTR(" ");
            sPath = sPath + USTR("C") + sPoints.getToken(0, ' ', nIndex);
            sPath = sPath + USTR(" ") + sPoints.getToken(0, ' ', nIndex);
            sPath = sPath + USTR(" ") + sPoints.getToken(0, ' ', nIndex);
        }
        if (bClose)
            sPath = sPath + USTR(" ") + sStart + USTR("Z");
        rProps[USTR("svg:d")] = sPath;
    }

    void makePathFromPoints(PropertyMap& rProps, bool bClose)
    {
        rtl::OUString sPoints = rProps[USTR("draw:points")];
        sal_Int32 nIndex = 0;
        rtl::OUString sStart = sPoints.getToken(0, ' ', nIndex);
        rtl::OUString sPath = USTR("M") + sStart;
        while ( nIndex >= 0 )
        {
            sPath = sPath + USTR(" ");
            sPath = sPath + USTR("L") + sPoints.getToken(0, ' ', nIndex);
            sPath = sPath + USTR(" ") + sPoints.getToken(0, ' ', nIndex);
            sPath = sPath + USTR(" ") + sPoints.getToken(0, ' ', nIndex);
        }
        if (bClose)
            sPath = sPath + USTR(" ") + sStart + USTR("Z");
        rProps[USTR("svg:d")] = sPath;
    }

    rtl::OUString deHashString(const rtl::OUString &rStr)
    {
        //Dia records strings e.g. A4 as #A4#
        if (rStr.getLength() > 2)
        {
            const sal_Unicode *pText = rStr.getStr();
            sal_Int32 nEnd = rStr.getLength() - 2;
            while (pText[nEnd] == '\n')
                --nEnd;
            return rStr.copy(1, nEnd);
        }
        return rtl::OUString();
    }
}

void DiaImporter::handleDiagramDataPaperAttribute(const uno::Reference<xml::dom::XElement> &rxElem, PropertyMap &rAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        rtl::OUString sVal = valueOfSimpleAttribute(rxElem);
        if (sName == USTR("name"))
        {
            rtl::OUString sPaper = deHashString(sVal);
            Paper ePaper = PaperInfo::fromPSName(rtl::OUStringToOString(sPaper, RTL_TEXTENCODING_UTF8));
            if (ePaper != PAPER_USER)
            {
                PaperInfo aPaper(ePaper);
                rAttrs[USTR("fo:page-width")] = rtl::OUString::number(aPaper.getWidth()/100.0)+USTR("mm");
                rAttrs[USTR("fo:page-height")] = rtl::OUString::number(aPaper.getHeight()/100.0)+USTR("mm");
            }
            else
                fprintf(stderr, "Unknown paper type of %s\n", rtl::OUStringToOString(sVal, RTL_TEXTENCODING_UTF8).getStr());
        }
        else if (sName == USTR("tmargin"))
        {
            rAttrs[USTR("fo:margin-top")] = sVal+USTR("cm");
            mnTop = sVal.toFloat();
        }
        else if (sName == USTR("bmargin"))
            rAttrs[USTR("fo:margin-bottom")] = sVal+USTR("cm");
        else if (sName == USTR("lmargin"))
        {
            rAttrs[USTR("fo:margin-left")] = sVal+USTR("cm");
            mnLeft = sVal.toFloat();
        }
        else if (sName == USTR("rmargin"))
            rAttrs[USTR("fo:margin-right")] = sVal+USTR("cm");
        else if (sName == USTR("is_portrait"))
        {
            rAttrs[USTR("style:print-orientation")] = 
                sVal != USTR("true") ?  USTR("landscape") : USTR("portrait");
        }
        else if (sName == USTR("scaling")) /*don't think this make sense from an OOo perspective*/
             /*IgnoreThis*/;
        else if (sName == USTR("fitto")) /*don't think this make sense from an OOo perspective*/
             /*IgnoreThis*/;
        else if (sName == USTR("fitwidth")) /*don't think this make sense from an OOo perspective*/
             /*IgnoreThis*/;
        else if (sName == USTR("fitheight")) /*don't think this make sense from an OOo perspective*/
             /*IgnoreThis*/;
        else
            fprintf(stderr, "Unknown Paper Attribute %s\n", rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
    }
}

void DiaImporter::handleDiagramDataPaperComposite(const uno::Reference<xml::dom::XElement> &rxElem)
{
    PropertyMap aAttrs;

    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("attribute"))
                handleDiagramDataPaperAttribute(xElem, aAttrs);
            else
                reportUnknownElement(xElem);
        }
    }

    //Swap dimensions for Landscape
    PropertyMap::const_iterator aI = aAttrs.find(USTR("style:print-orientation"));
    if (aI != aAttrs.end() && aI->second == USTR("landscape"))
    {
        rtl::OUString sWidth = aAttrs[USTR("fo:page-width")];
        aAttrs[USTR("fo:page-width")] = aAttrs[USTR("fo:page-height")];
        aAttrs[USTR("fo:page-height")] = sWidth;
    }

    page_layout_properties.reset(new autostyle(USTR("style:page-layout-properties"), aAttrs));
}

void DiaImporter::handleDiagramDataPaper(const uno::Reference<xml::dom::XElement> &rxElem)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = xElem->getAttributes();
            uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("type")));
            if (xElem->getTagName() == USTR("composite") && xNode.is() && xNode->getNodeValue() == USTR("paper"))
                handleDiagramDataPaperComposite(xElem);
            else
                reportUnknownElement(xElem);
        }
    }
}

void DiaImporter::handleDiagramDataBackGroundColor(const uno::Reference<xml::dom::XElement> &rxElem)
{
    rtl::OUString sColor;
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("val")));
    if(xNode.is())
    {
        PropertyMap aAttrs;

        aAttrs[USTR("draw:background-size")] = USTR("border");
        aAttrs[USTR("draw:fill")] = USTR("solid");
        aAttrs[USTR("draw:fill-color")] = xNode->getNodeValue();

        drawing_page_properties.reset(new autostyle(USTR("style:drawing-page-properties"), aAttrs ));
    }
}

void DiaImporter::handleDiagramDataBackGround(const uno::Reference<xml::dom::XElement> &rxElem)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("color"))
                handleDiagramDataBackGroundColor(xElem);
            else
                reportUnknownElement(xElem);
        }
    }
}

void DiaImporter::handleDiagramDataAttribute(const uno::Reference<xml::dom::XElement> &rxElem)
{
    rtl::OUString sName;
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
        sName = xNode->getNodeValue();

    if (sName == USTR("background"))
        handleDiagramDataBackGround(rxElem);
    else if (sName == USTR("paper"))
        handleDiagramDataPaper(rxElem);
    else if (sName == USTR("pagebreak"))    //Pagebreak color, meaningless in OOo
        /*IgnoreThis*/;
    else if (sName == USTR("grid"))         //Grid setting is per-app, not per doc in OOo
        /*IgnoreThis*/;
    else if (sName == USTR("guides"))       //Guide setting is per-app, not per doc in OOo
        /*IgnoreThis*/;
    else if (sName == USTR("color"))        //Color of the Grid
        /*IgnoreThis*/;
    else
        fprintf(stderr, "Unknown Diagram Data Attribute %s\n", rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
}

void DiaImporter::handleDiagramData(const uno::Reference<xml::dom::XElement> &rxElem)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("attribute"))
                handleDiagramDataAttribute(xElem);
            else
                reportUnknownElement(xElem);
        }
    }
}

void DiaImporter::addStrokeDash(PropertyMap &rStyleAttrs, sal_Int32 nLineStyle, float nDashLength)
{
    rStyleAttrs[USTR("draw:stroke")] = USTR("dash");

    PropertyMap aStrokeDash;
    switch (nLineStyle)
    {
        case 1:
            aStrokeDash = makeDash(nDashLength);
            break;
        case 2:
            aStrokeDash = makeDashDot(nDashLength);
            break;
        case 3:
            aStrokeDash = makeDashDotDot(nDashLength);
            break;
        case 4:
            aStrokeDash = makeDot(nDashLength);
            break;
        default:
            fprintf(stderr, "unknown dia line style %" SAL_PRIdINT32 "\n", nLineStyle);
            break;
    }

    autostyles::const_iterator aI = std::find_if(maDashes.begin(), maDashes.end(), EqualStyle(aStrokeDash));

    rtl::OUString sName;

    if (aI != maDashes.end())
        sName = aI->first;
    else
    {
        sName = USTR("DIA_20_Line_20_") + rtl::OUString::number(static_cast<sal_Int64>(maDashes.size()+1-4));
        maDashes.push_back(autostyle(sName, aStrokeDash));
    }

    rStyleAttrs[USTR("draw:stroke-dash")] = sName;
}

void GraphicStyleManager::addAutomaticGraphicStyle(PropertyMap &rAttrs, const PropertyMap &rStyleAttrs)
{
    rtl::OUString sName;

    autostyles::const_iterator aI = std::find_if(maGraphicStyles.begin(), maGraphicStyles.end(), EqualStyle(rStyleAttrs));

    if (aI != maGraphicStyles.end())
        sName = aI->first;
    else
    {
        sName = USTR("gr") + rtl::OUString::number(static_cast<sal_Int64>(maGraphicStyles.size()+1));
        maGraphicStyles.push_back(autostyle(sName, rStyleAttrs));
    }

    rAttrs[USTR("draw:style-name")] = sName;
}

void TextStyleManager::addAutomaticTextStyle(PropertyMap &rAttrs, ParaTextStyle &rStyleAttrs)
{
    rtl::OUString sName;

    fixFontSizes(rStyleAttrs.maTextAttrs);

    extendedautostyles::const_iterator aI = std::find_if(maTextStyles.begin(), maTextStyles.end(),
        EqualParaTextStyle(rStyleAttrs));

    if (aI != maTextStyles.end())
        sName = aI->first;
    else
    {
        sName = USTR("P") + rtl::OUString::number(static_cast<sal_Int64>(maTextStyles.size()+1));
        maTextStyles.push_back(extendedautostyle(sName, rStyleAttrs));
    }

    rAttrs[USTR("text:style-name")] = sName;
}

namespace
{
    class IsAutoStyleName :
        public std::unary_function<const autostyle &, bool>
    {
    private:
        const rtl::OUString &mrName;
    public:
        IsAutoStyleName(const rtl::OUString &rName) : mrName(rName) {};
        bool operator()(const autostyle &rStyle)
            {return mrName == rStyle.first;}
    };

    class IsExtendedAutoStyleName :
        public std::unary_function<const extendedautostyle &, bool>
    {
    private:
        const rtl::OUString &mrName;
    public:
        IsExtendedAutoStyleName(const rtl::OUString &rName) : mrName(rName) {};
        bool operator()(const extendedautostyle &rStyle)
            {return mrName == rStyle.first;}
    };

}

const PropertyMap *GraphicStyleManager::getStyleByName(const rtl::OUString &rName) const
{
    autostyles::const_iterator aI = std::find_if(maGraphicStyles.begin(), maGraphicStyles.end(), IsAutoStyleName(rName));
    PropertyMap *pRet = NULL;
    if (aI != maGraphicStyles.end())
        return &(aI->second);
    return pRet;
}

const PropertyMap *TextStyleManager::getStyleByName(const rtl::OUString &rName) const
{
    extendedautostyles::const_iterator aI = std::find_if(maTextStyles.begin(), maTextStyles.end(), IsExtendedAutoStyleName(rName));
    PropertyMap *pRet = NULL;
    if (aI != maTextStyles.end())
        return &(aI->second.maTextAttrs);
    return pRet;
}

void GraphicStyleManager::addTextBoxStyle()
{
    PropertyMap aStyleAttrs;

    aStyleAttrs[USTR("draw:stroke")]=USTR("none");
    aStyleAttrs[USTR("draw:fill")]=USTR("none");
    aStyleAttrs[USTR("draw:textarea-horizontal-align")] = USTR("center");
    aStyleAttrs[USTR("draw:textarea-vertical-align")] = USTR("middle");
    aStyleAttrs[USTR("draw:auto-grow-width")]=USTR("true");
    aStyleAttrs[USTR("fo:min-height")]=USTR("0.5cm");

    maGraphicStyles.push_back(autostyle(USTR("grtext"), aStyleAttrs));
}

void GraphicStyleManager::write(uno::Reference < xml::sax::XDocumentHandler > xDocHandler)
{
    autostyles::const_iterator aEnd = maGraphicStyles.end();
    for (autostyles::const_iterator aI = maGraphicStyles.begin(); aI != aEnd; ++aI)
    {
        PropertyMap aAttrs;
        aAttrs[USTR( "style:name")] = aI->first;
        aAttrs[USTR( "style:family")] = USTR("graphic");
        xDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
        xDocHandler->startElement(USTR("style:graphic-properties"), new SaxAttrList(aI->second));
        xDocHandler->endElement(USTR("style:graphic-properties"));
        xDocHandler->endElement(USTR("style:style"));
    }
}

void TextStyleManager::write(uno::Reference < xml::sax::XDocumentHandler > xDocHandler)
{
    extendedautostyles::const_iterator aEnd = maTextStyles.end();
    for (extendedautostyles::const_iterator aI = maTextStyles.begin(); aI != aEnd; ++aI)
    {
        PropertyMap aAttrs;
        aAttrs[USTR( "style:name")] = aI->first;
        aAttrs[USTR( "style:family")] = USTR("paragraph");
        xDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
        xDocHandler->startElement(USTR("style:text-properties"), new SaxAttrList(aI->second.maTextAttrs));
        xDocHandler->endElement(USTR("style:text-properties"));
        xDocHandler->startElement(USTR("style:paragraph-properties"), new SaxAttrList(aI->second.maParaAttrs));
        xDocHandler->endElement(USTR("style:paragraph-properties"));
        xDocHandler->endElement(USTR("style:style"));
    }
}

awt::FontDescriptor TextStyleManager::getFontDescriptor(const PropertyMap &rStyleAttrs) const
{
    awt::FontDescriptor aFD;
    PropertyMap::const_iterator aI;
    aI = rStyleAttrs.find(USTR("fo:font-family"));
    if (aI != rStyleAttrs.end())
        aFD.Name = aI->second;
    aI = rStyleAttrs.find(USTR("fo:font-size"));
    if (aI != rStyleAttrs.end())
    {
        rtl::OUString ptsize = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "pt", "");
        aFD.Height = ptsize.toFloat();
    }
    aI = rStyleAttrs.find(USTR("fo:font-style"));
    if (aI != rStyleAttrs.end() && aI->second == USTR("italic"))
        aFD.Slant = awt::FontSlant_ITALIC;
    aI = rStyleAttrs.find(USTR("fo:font-weight"));
    if (aI != rStyleAttrs.end() && aI->second == USTR("bold"))
        aFD.Weight = 700;
    return aFD;
}

uno::Reference< awt::XFont > TextStyleManager::getMatchingFont(const PropertyMap &rStyleAttrs) const
{
    awt::FontDescriptor aFD = getFontDescriptor(rStyleAttrs);
    return mxReferenceDevice->getFont(aFD);
}

double TextStyleManager::getStringWidth(const rtl::OUString &rStyleName, const rtl::OUString &rString) const
{
    sal_Int32 nWidth = 0;

    if (rStyleName.getLength())
    {
        if (const PropertyMap *pStyle = getStyleByName(rStyleName))
        {
            uno::Reference< awt::XFont > xFont = getMatchingFont(*pStyle);
            nWidth = xFont->getStringWidth(rString);
        }
    }

    return nWidth / 72.0 * 2.54;
}

void TextStyleManager::fixFontSizes(PropertyMap &rStyleAttrs)
{
    /*
     * Dia purports to measure the font in points, but it measures the font
     * ascent+descent(+leading?)
     *
     * Do the best we can here and fix it up.
     */

    awt::FontDescriptor aFD = getFontDescriptor(rStyleAttrs);

    uno::Reference< awt::XFont > xFont(mxReferenceDevice->getFont(aFD));
    awt::SimpleFontMetric aMetric = xFont->getFontMetric();

    float nTotal = aMetric.Ascent + aMetric.Descent + aMetric.Leading;
    float fAdjust = aFD.Height/nTotal;

    rStyleAttrs[USTR("fo:font-size")] = rtl::OUString::number(aFD.Height * fAdjust) + USTR("pt");
}

diaobject DiaImporter::getobjectbyid(const rtl::OUString &rId) const
{
    diaobject ret;
    objectmap::const_iterator aI = mapId.find(rId);
    if (aI != mapId.end())
        ret = aI->second;
    return ret;
}

class DiaObject
{
private:
    void handleObjectTextAttribute(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter, ParaTextStyle &rStyleProps);
    void handleObjectTextComposite(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    void handleObjectText(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
protected:
    std::vector< ConnectionPoint > maConnectionPoints;
    PropertyMap maTextProps;
    rtl::OUString msString;
    int mnTextAlign;
    bool mbShowBorder;
    bool mbShowBackground;
    bool mbAutoWidth;
    bool mbFlipVert;
    bool mbFlipHori;
    sal_Int32 mnLineStyle;
    float mnDashLength;
    float mnX, mnY, mnWidth, mnHeight;
    float mnObjPosX, mnObjPosY;
    float mnPadding;
    float mnTextX, mnTextY;

    PropertyMap handleStandardObject(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual void setdefaultpadding(const uno::Reference<xml::dom::XElement> &rxElem);
    void writeText(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler) const;
    virtual void writeConnectionPoints(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler) const;
    void handleObjectConnections(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs);
    void handleObjectConnection(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs);
public:
    DiaObject()
        : mnTextAlign(0), mbShowBorder(true), mbShowBackground(true), mbAutoWidth(false)
        , mbFlipVert(false), mbFlipHori(false), mnLineStyle(0), mnDashLength(1.0)
        , mnX(0.0), mnY(0.0), mnWidth(0.0), mnHeight(0.0), mnObjPosX(0.0), mnObjPosY(0.0)
        , mnPadding(0.0), mnTextX(0.0), mnTextY(0.0) {}
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual rtl::OUString outputtype() const = 0;
    virtual void handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs, PropertyMap &rStyleAttrs);
    virtual void write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const;
    virtual void resizeIfNarrow(PropertyMap &rProps, const DiaImporter &rImporter);
    basegfx::B2DRectangle getBoundingBox() const;
    virtual int getConnectionDirection(sal_Int32 nConnection) const;
    virtual void snapConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint, const DiaImporter &rImporter) const;
    virtual void adjustConnectionPoints(PropertyMap &rProps, const DiaImporter &rImporter) {}
    virtual ~DiaObject() {}
};

basegfx::B2DRectangle DiaObject::getBoundingBox() const
{
    return basegfx::B2DRectangle(mnX, mnY, mnX+mnWidth, mnY+mnHeight);
}

PropertyMap DiaObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    return handleStandardObject(rxElem, rImporter);
}

void DiaObject::writeText(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler) const
{
    ::writeText(rDocHandler, maTextProps, msString);
}

void DiaObject::writeConnectionPoints(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler) const
{
    if (maConnectionPoints.size())
    {
        /*
         * For connection points the svg:x  and svg:y attributes are relative
         * values, not absolute coordinates. Values range from -5cm (the left side
         * or bottom) to 5cm (the right or top). Which frankly is rather bizarre.
         */
        sal_Int32 id=4;
        PropertyMap aProps;
        std::vector< ConnectionPoint >::const_iterator aEnd = maConnectionPoints.end();
        for (std::vector< ConnectionPoint >::const_iterator aI = maConnectionPoints.begin(); aI != aEnd; ++aI)
        {
            aProps[USTR("svg:x")] = rtl::OUString::number(aI->mx) + USTR("cm");
            aProps[USTR("svg:y")] = rtl::OUString::number(aI->my) + USTR("cm");
            aProps[USTR("draw:id")] = rtl::OUString::number(id++);

#ifdef DEBUG
            PropertyMap::const_iterator aEnd = aProps.end();
            for (PropertyMap::const_iterator aI = aProps.begin(); aI != aEnd; ++aI)
            {
                fprintf(stderr, "writing connection prop %s %s\n",
                    rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
                    rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
                );
            }
#endif

            rxDocHandler->startElement(USTR("draw:glue-point"), makeXAttributeAndClear(aProps));
            rxDocHandler->endElement(USTR("draw:glue-point"));
        }
    }
}

void DiaObject::write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &) const
{
#ifdef DEBUG
    PropertyMap::const_iterator aEnd = rProps.end();
    for (PropertyMap::const_iterator aI = rProps.begin(); aI != aEnd; ++aI)
    {
        fprintf(stderr, "writing prop %s %s\n",
            rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
            rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
        );
    }
#endif

    rDocHandler->startElement(outputtype(), new SaxAttrList(rProps));

    writeConnectionPoints(rDocHandler);

    if (msString.getLength())
        writeText(rDocHandler);

    rDocHandler->endElement(outputtype());
}

void DiaObject::resizeIfNarrow(PropertyMap &rProps, const DiaImporter &rImporter)
{
    PropertyMap::const_iterator aI;

    float fStrokeWidth = 0.1;
    float fWidth = 0;

    aI = rProps.find(USTR("svg:width"));
    if (aI != rProps.end())
        fWidth = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat();

    fWidth = mnWidth;

    rtl::OUString sDrawStyleName;
    aI = rProps.find(USTR("draw:style-name"));
    if (aI != rProps.end())
        sDrawStyleName = aI->second;

    if (sDrawStyleName.getLength())
    {
        const GraphicStyleManager &rGraphicStyleManager = rImporter.getGraphicStyleManager();
        if (const PropertyMap *pStyle = rGraphicStyleManager.getStyleByName(sDrawStyleName))
        {
            aI = pStyle->find(USTR("svg:stroke-width"));
            if (aI != pStyle->end())
                fStrokeWidth = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat();
        }
    }

    rtl::OUString sTextStyleName;
    aI = maTextProps.find(USTR("text:style-name"));
    if (aI != maTextProps.end())
        sTextStyleName = aI->second;
    if (sTextStyleName.getLength())
    {
        const TextStyleManager &rTextStyleManager = rImporter.getTextStyleManager();
        float fTextWidth = 0.0;

        sal_Int32 nIndex=0;
        do
        {
            rtl::OUString sSpan = msString.getToken(0, '\n', nIndex);
            float fSpanWidth = rTextStyleManager.getStringWidth(sTextStyleName, sSpan);
            if (fSpanWidth > fTextWidth)
             fTextWidth = fSpanWidth;
        }
        while ( nIndex >= 0 );

        float fCalcWidth = mnPadding * 2 + fStrokeWidth * 2 + fTextWidth;
        if (fCalcWidth > fWidth)
        {
            float fDiff = (fCalcWidth - fWidth) / 2;
            rProps[USTR("svg:width")] = rtl::OUString::number(fCalcWidth)+USTR("cm");
            mnWidth = fCalcWidth;
            mnX-=fDiff;
            rProps[USTR("svg:x")] = rtl::OUString::number(mnX)+USTR("cm");
        }
    }
}

void DiaObject::handleObjectConnection(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("handle")));
    sal_Int32 nHandle = xNode.is() ? xNode->getNodeValue().toInt32() : -1;

    if (nHandle < 0)
        fprintf(stderr, "unknown handle %" SAL_PRIdINT32 "\n", nHandle);

    const sal_Int32 nNumNodes( xAttributes->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        uno::Reference<xml::dom::XNode> xNode(xAttributes->item(i));
        rtl::OUString sName = xNode->getNodeName();
        rtl::OUString sValue = xNode->getNodeValue();
        if (sName.equalsAsciiL(RTL_CONSTASCII_STRINGPARAM("to")))
        {
            switch (nHandle)
            {
                case 0:
                    rAttrs[USTR("draw:start-shape")]=sValue;
                    break;
                case 1:
                default: //e.g. bezier curves
                    rAttrs[USTR("draw:end-shape")]=sValue;
                    break;
            }
        }
        else if (sName.equalsAsciiL(RTL_CONSTASCII_STRINGPARAM("connection")))
        {
            switch (nHandle)
            {
                case 0:
                    rAttrs[USTR("draw:start-glue-point")]=rtl::OUString::number(sValue.toInt32()+4);
                    break;
                case 1:
                    rAttrs[USTR("draw:end-glue-point")]=rtl::OUString::number(sValue.toInt32()+4);
                    break;
            }
        }
        else if (sName.equalsAsciiL(RTL_CONSTASCII_STRINGPARAM("handle")))
            /*skip*/;
        else
            fprintf(stderr, "unknown attribute %s\n", rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
    }
}


void DiaObject::handleObjectConnections(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("connection"))
                handleObjectConnection(xElem, rImporter, rAttrs);
            else
                reportUnknownElement(xElem);
        }
    }
}

void DiaObject::setdefaultpadding(const uno::Reference<xml::dom::XElement> &rxElem)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("version")));
    if(xNode.is())
    {
        switch (xNode->getNodeValue().toInt32())
        {
            case 0:
                mnPadding = 0.353553;
                break;
            case 1:
            default:
                mnPadding = 0.10;
                break;
        }
    }
}

PropertyMap DiaObject::handleStandardObject(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter)
{
    PropertyMap aAttrs, aStyleAttrs;

    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("id")));

    if(xNode.is())
        aAttrs[USTR("draw:id")] = xNode->getNodeValue();
    else
        fprintf(stderr, "missing id!\n");

    setdefaultpadding(rxElem);

    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("attribute"))
                handleObjectAttribute(xElem, rImporter, aAttrs, aStyleAttrs);
            else if (xElem->getTagName() == USTR("connections"))
                handleObjectConnections(xElem, rImporter, aAttrs);
            else
                reportUnknownElement(xElem);
        }
    }

    //Translate to 0, do flip, Translate back
    if (mbFlipVert || mbFlipHori)
    {
        sal_Int32 nFlipHori = mbFlipHori ? -1 : 1;
        sal_Int32 nFlipVert = mbFlipVert ? -1 : 1;
        float nXTrans1 = mbFlipHori ? -mnX : 0;
        float nXTrans2 = mbFlipHori ? mnX+mnWidth : 0;
        float nYTrans1 = mbFlipVert ? -mnY : 0;
        float nYTrans2 = mbFlipVert ? mnY+mnHeight : 0;
        aAttrs[USTR("draw:transform")] =
            USTR("translate (") +
            rtl::OUString::number(nXTrans1) + USTR("cm") +
            USTR(" ") +
            rtl::OUString::number(nYTrans1) + USTR("cm") +
            USTR(") ")
            +
            USTR("scale (") +
            rtl::OUString::number(nFlipHori) +
            USTR(" ") +
            rtl::OUString::number(nFlipVert) +
            USTR(") ")
            +
            USTR("translate (") +
            rtl::OUString::number(nXTrans2) + USTR("cm") +
            USTR(" ") +
            rtl::OUString::number(nYTrans2) + USTR("cm") +
            USTR(") ");
    }

    aStyleAttrs[USTR("draw:textarea-vertical-align")] = USTR("middle");
    if (mnTextAlign == 0)
        aStyleAttrs[USTR("draw:textarea-horizontal-align")] = USTR("left");
    else if (mnTextAlign == 2)
        aStyleAttrs[USTR("draw:textarea-horizontal-align")] = USTR("right");
    aStyleAttrs[USTR("fo:padding-top")] = rtl::OUString::number(mnPadding) + USTR("cm");
    aStyleAttrs[USTR("fo:padding-bottom")] = rtl::OUString::number(mnPadding) + USTR("cm");
    aStyleAttrs[USTR("fo:padding-left")] = rtl::OUString::number(mnPadding) + USTR("cm");
    aStyleAttrs[USTR("fo:padding-right")] = rtl::OUString::number(mnPadding) + USTR("cm");

    if (mbAutoWidth)
        aStyleAttrs[USTR("draw:auto-grow-width")] = USTR("true");

    if (!mbShowBackground)
        aStyleAttrs[USTR("draw:fill")] = USTR("none");
    else
        aStyleAttrs[USTR("draw:fill")] = USTR("solid");

    if (!mbShowBorder)
        aStyleAttrs[USTR("draw:stroke")] = USTR("none");
    else if (mbShowBorder && mnLineStyle != 0)
        rImporter.addStrokeDash(aStyleAttrs, mnLineStyle, mnDashLength);
    else
        aStyleAttrs[USTR("draw:stroke")] = USTR("solid");

    rImporter.addAutomaticGraphicStyle(aAttrs, aStyleAttrs);

    return aAttrs;
}

void DiaObject::handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs,
    PropertyMap &rStyleAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("obj_pos"))
        {
            rtl::OUString sObjPos = valueOfSimpleAttribute(rxElem);
            float nX1=0, nY1=0;
            sal_Int32 c = sObjPos.indexOf(',');
            if (c != -1)
            {
                nX1 = sObjPos.copy(0, c).toFloat();
                nY1 = sObjPos.copy(c+1).toFloat();
            }
            mnObjPosX = rImporter.adjustX(nX1);
            mnObjPosY = rImporter.adjustY(nY1);
        }
        else if (sName == USTR("obj_bb"))
        {
            rtl::OUString sVal = valueOfSimpleAttribute(rxElem);
            sal_Int32 c = sVal.indexOf(';');
            if (c != -1)
            {
                rtl::OUString sTopLeft = sVal.copy(0, c);
                rtl::OUString sBottomRight = sVal.copy(c+1);
        
                float nX1=0, nY1=0, nX2, nY2;

                c = sTopLeft.indexOf(',');
                if (c != -1)
                {
                    nX1 = sTopLeft.copy(0, c).toFloat();
                    nY1 = sTopLeft.copy(c+1).toFloat();
                }

                mnX = rImporter.adjustX(nX1);
                mnY = rImporter.adjustY(nY1);
                rAttrs[USTR("svg:x")] = rtl::OUString::number(mnX)+USTR("cm");
                rAttrs[USTR("svg:y")] = rtl::OUString::number(mnY)+USTR("cm");

                c = sBottomRight.indexOf(',');
                if (c != -1)
                {
                    nX2 = sBottomRight.copy(0, c).toFloat();
                    nY2 = sBottomRight.copy(c+1).toFloat();
                    mnWidth = nX2-nX1;
                    mnHeight = nY2-nY1;
                    rAttrs[USTR("svg:width")] = rtl::OUString::number(mnWidth)+USTR("cm");
                    rAttrs[USTR("svg:height")] = rtl::OUString::number(mnHeight)+USTR("cm");
                }
            }
        }
        else if (sName == USTR("elem_corner"))
        {
            rtl::OUString sTopLeft = valueOfSimpleAttribute(rxElem);
            float nX1=0, nY1=0;
            sal_Int32 c = sTopLeft.indexOf(',');
            if (c != -1)
            {
                nX1 = sTopLeft.copy(0, c).toFloat();
                nY1 = sTopLeft.copy(c+1).toFloat();
            }
            mnX = rImporter.adjustX(nX1);
            mnY = rImporter.adjustY(nY1);
            rAttrs[USTR("svg:x")] = rtl::OUString::number(mnX)+USTR("cm");
            rAttrs[USTR("svg:y")] = rtl::OUString::number(mnY)+USTR("cm");
        }
        else if (sName == USTR("elem_width"))
        {
            mnWidth = valueOfSimpleAttribute(rxElem).toFloat();
            rAttrs[USTR("svg:width")] = rtl::OUString::number(mnWidth)+USTR("cm");
        }
        else if (sName == USTR("elem_height"))
        {
            mnHeight = valueOfSimpleAttribute(rxElem).toFloat();
            rAttrs[USTR("svg:height")] = rtl::OUString::number(mnHeight)+USTR("cm");
        }
        else if (sName == USTR("border_width"))
            rStyleAttrs[USTR("svg:stroke-width")] = valueOfSimpleAttribute(rxElem)+USTR("cm");
        else if (sName == USTR("line_width"))
            rStyleAttrs[USTR("svg:stroke-width")] = valueOfSimpleAttribute(rxElem)+USTR("cm");
        else if (sName == USTR("border_color"))
            rStyleAttrs[USTR("svg:stroke-color")] = valueOfSimpleAttribute(rxElem);
        else if (sName == USTR("line_color") || sName == USTR("line_colour"))
            rStyleAttrs[USTR("svg:stroke-color")] = valueOfSimpleAttribute(rxElem);
        else if (sName == USTR("inner_color"))
            rStyleAttrs[USTR("draw:fill-color")] = valueOfSimpleAttribute(rxElem);
        else if (sName == USTR("fill_color") || sName == USTR("fill_colour"))
            rStyleAttrs[USTR("draw:fill-color")] = valueOfSimpleAttribute(rxElem);
        else if (sName == USTR("show_background"))
            mbShowBackground = valueOfSimpleAttribute(rxElem) == USTR("true");
        else if (sName == USTR("line_style"))
            mnLineStyle = valueOfSimpleAttribute(rxElem).toInt32();
        else if (sName == USTR("dashlength"))
            mnDashLength = valueOfSimpleAttribute(rxElem).toFloat();
        else if (sName == USTR("corner_radius"))
            rAttrs[USTR("draw:corner-radius")] = valueOfSimpleAttribute(rxElem)+USTR("cm");
        else if (sName == USTR("aspect"))
            /*IgnoreThis*/;
        else if (sName == USTR("poly_points"))
            rAttrs[USTR("draw:points")] = valueOfSimpleAttribute(rxElem).trim();
        else if (sName == USTR("orth_points"))
            rAttrs[USTR("draw:points")] = valueOfSimpleAttribute(rxElem).trim();
        else if (sName == USTR("bez_points"))
            rAttrs[USTR("draw:points")] = valueOfSimpleAttribute(rxElem).trim();
        else if (sName == USTR("orth_orient"))
            /*IgnoreThis*/;
        else if (sName == USTR("keep_aspect"))
            /*IgnoreThis*/;
        else if (sName == USTR("conn_endpoints"))
            createPoints(rAttrs, valueOfSimpleAttribute(rxElem), rImporter);
        else if (sName == USTR("draw_border"))
            mbShowBorder = valueOfSimpleAttribute(rxElem) == USTR("true");
        else if (sName == USTR("subscale"))
            /*IgnoreThis*/;
        else if (sName == USTR("flip_horizontal"))
            mbFlipHori = valueOfSimpleAttribute(rxElem) == USTR("true");
        else if (sName == USTR("flip_vertical"))
            mbFlipVert = valueOfSimpleAttribute(rxElem) == USTR("true");
        else if (sName == USTR("text"))
            handleObjectText(rxElem, rImporter);
        else if (sName == USTR("padding"))
            mnPadding = valueOfSimpleAttribute(rxElem).toFloat();
        else if (sName == USTR("start_arrow"))
        {
            sal_Int32 nArrow = valueOfSimpleAttribute(rxElem).toInt32();
            if (nArrow)
                rStyleAttrs[USTR("draw:marker-start")] = GetArrowName(nArrow);
        }
        else if (sName == USTR("start_arrow_width"))
            rStyleAttrs[USTR("draw:marker-start-width")] = valueOfSimpleAttribute(rxElem)+USTR("cm");
        else if (sName == USTR("end_arrow"))
        {
            sal_Int32 nArrow = valueOfSimpleAttribute(rxElem).toInt32();
            if (nArrow)
                rStyleAttrs[USTR("draw:marker-end")] = GetArrowName(nArrow);
        }
        else if (sName == USTR("end_arrow_width"))
            rStyleAttrs[USTR("draw:marker-end-width")] = valueOfSimpleAttribute(rxElem)+USTR("cm");
        else if (sName == USTR("valign")) /*don't think this really matters if we take the obj_pos*/
             /*IgnoreThis*/;
        else if (sName == USTR("meta"))
             /*IgnoreThis*/;
        else if (sName == USTR("numcp"))
             /*ToDo*/;
        else if (sName == USTR("start_arrow_length"))
             /*ToDo*/;
        else if (sName == USTR("end_arrow_length"))
             /*ToDo*/;
        else if (sName == USTR("corner_types"))
             /*ToDo*/;
        else
            fprintf(stderr, "Unknown Object Attribute %s\n", rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
    }
}

void handleObjectTextFont(const uno::Reference<xml::dom::XElement> rxElem, PropertyMap &rAttrs)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = xElem->getAttributes();
            if (xElem->getTagName() == USTR("font"))
            {
                sal_Int32 nAttribs = xAttributes->getLength();
                for (sal_Int32 j=0; j < nAttribs; ++j)
                {
                    uno::Reference<xml::dom::XNode> xNode(xAttributes->item(j));
                    rtl::OUString sName = xNode->getNodeName();
                    if (sName == USTR("family"))
                        rAttrs[USTR("fo:font-family")] = xNode->getNodeValue();
                    else if (sName == USTR("name"))
                        /*IgnoreMe*/;
                    else if (sName == USTR("style"))
                    {
                        rtl::OUString sStyle = xNode->getNodeValue();
                        if (sStyle == USTR("0"))
                            rAttrs[USTR("fo:font-style")] = USTR("normal");
                        else if (sStyle == USTR("8"))
                            rAttrs[USTR("fo:font-style")] = USTR("italic");
                        else if (sStyle == USTR("80"))
                            rAttrs[USTR("fo:font-weight")] = USTR("bold");
                        else if (sStyle == USTR("88"))
                        {
                            rAttrs[USTR("fo:font-style")] = USTR("italic");
                            rAttrs[USTR("fo:font-weight")] = USTR("bold");
                        }
                        else
                            fprintf(stderr, "unknown text style %s\n", rtl::OUStringToOString(sStyle, RTL_TEXTENCODING_UTF8).getStr());
                    }
                    else
                        fprintf(stderr, "unknown attribute %s\n", rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
                }
            }
            else
                reportUnknownElement(xElem);
        }
    }
}

void DiaObject::handleObjectTextAttribute(const uno::Reference<xml::dom::XElement> &rElem, DiaImporter &rImporter, ParaTextStyle &rStyleProps)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("string"))
            msString = deHashString(valueOfSimpleAttribute(rElem));
        else if (sName == USTR("color"))
            rStyleProps.maTextAttrs[USTR("fo:color")] = valueOfSimpleAttribute(rElem);
        else if (sName == USTR("font"))
            handleObjectTextFont(rElem, rStyleProps.maTextAttrs);
        else if (sName == USTR("height"))
        {
            float nHeight = valueOfSimpleAttribute(rElem).toFloat();
            rStyleProps.maTextAttrs[USTR("fo:font-size")] = rtl::OUString::number(nHeight * 72 / 2.54) + USTR("pt");
        }
        else if (sName == USTR("pos"))
        {
            rtl::OUString sTopLeft = valueOfSimpleAttribute(rElem);
            sal_Int32 c = sTopLeft.indexOf(',');
            if (c != -1)
            {
                mnTextX = sTopLeft.copy(0, c).toFloat();
                mnTextY = sTopLeft.copy(c+1).toFloat();
            }
            mnTextX = rImporter.adjustX(mnTextX);
            mnTextY = rImporter.adjustY(mnTextY);
        }
        else if (sName == USTR("alignment"))
        {
            switch (valueOfSimpleAttribute(rElem).toInt32())
            {
                default:
                    mnTextAlign = 0;
                    break;
                case 1:
                    rStyleProps.maParaAttrs[USTR("fo:text-align")] = USTR("center");
                    mnTextAlign = 1;
                    break;
                case 2:
                    rStyleProps.maParaAttrs[USTR("fo:text-align")] = USTR("end");
                    mnTextAlign = 2;
                    break;
            }
        }
        else
        {
            fprintf(stderr, "Unknown Text Attribute %s\n", rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
        }
    }
}

void DiaObject::handleObjectTextComposite(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    ParaTextStyle aStyleProps;

    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("attribute"))
                handleObjectTextAttribute(xElem, rImporter, aStyleProps);
            else
                reportUnknownElement(xElem);
        }
    }

    rImporter.addAutomaticTextStyle(maTextProps, aStyleProps);
}

void DiaObject::handleObjectText(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = xElem->getAttributes();
            uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("type")));
            if (xElem->getTagName() == USTR("composite") && xNode.is() && xNode->getNodeValue() == USTR("text"))
                handleObjectTextComposite(xElem, rImporter);
            else
                reportUnknownElement(xElem);
        }
    }
}

int DiaObject::getConnectionDirection(sal_Int32 nConnection) const
{
    nConnection-=4;
    if (static_cast<size_t>(nConnection) >= maConnectionPoints.size())
    {
        fprintf(stderr, "connection point %" SAL_PRIdINT32 " unknown\n", nConnection);
        return DIR_ALL;
    }
   
    return maConnectionPoints[nConnection].mdir;
}

void DiaObject::snapConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint, const DiaImporter &rImporter) const

{
    nConnection-=4;
    if (static_cast<size_t>(nConnection) < maConnectionPoints.size())
    {
        float nCenterX = mnX + mnWidth/2;
        float nCenterY = mnY + mnHeight/2;
        float nX = nCenterX + (maConnectionPoints[nConnection].mx * mnWidth / 10);
        float nY = nCenterY + (maConnectionPoints[nConnection].my * mnHeight / 10);

        rPoint.setX(rImporter.unadjustX(nX));
        rPoint.setY(rImporter.unadjustY(nY));
    }
}

class StandardBoxObject : public DiaObject
{
public:
    StandardBoxObject();
    virtual rtl::OUString outputtype() const { return USTR("draw:rect"); }
};

StandardBoxObject::StandardBoxObject()
{
    maConnectionPoints.push_back(ConnectionPoint(-5, -5, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(0, -5, DIR_NORTH));
    maConnectionPoints.push_back(ConnectionPoint(5, -5, DIR_NORTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(-5, 0, DIR_WEST));
    maConnectionPoints.push_back(ConnectionPoint(5, 0, DIR_EAST));
    maConnectionPoints.push_back(ConnectionPoint(-5, 5, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(0, 5, DIR_SOUTH));
    maConnectionPoints.push_back(ConnectionPoint(5, 5, DIR_SOUTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(0, 0, DIR_ALL));
}

class StandardEllipseObject : public DiaObject
{
public:
    StandardEllipseObject();
    virtual rtl::OUString outputtype() const { return USTR("draw:ellipse"); }
};

StandardEllipseObject::StandardEllipseObject()
{
    float stepx = 10 * M_SQRT1_2 / 2.0;
    float stepy = 10 * M_SQRT1_2 / 2.0;

    maConnectionPoints.push_back(ConnectionPoint(-stepx, -stepy, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(0, -5, DIR_NORTH));
    maConnectionPoints.push_back(ConnectionPoint(stepx, -stepy, DIR_NORTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(-5, 0, DIR_WEST));
    maConnectionPoints.push_back(ConnectionPoint(5, 0, DIR_EAST));
    maConnectionPoints.push_back(ConnectionPoint(-stepx, stepy, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(0, 5, DIR_SOUTH));
    maConnectionPoints.push_back(ConnectionPoint(stepx, stepy, DIR_SOUTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(0, 0, DIR_ALL));
}

class StandardPolygonObject : public DiaObject
{
public:
    virtual rtl::OUString outputtype() const { return USTR("draw:polygon"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
};

class StandardLineObject : public DiaObject
{
protected:
    virtual void writeConnectionPoints(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler) const;
public:
    virtual rtl::OUString outputtype() const { return USTR("draw:connector"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
};

PropertyMap StandardLineObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);
    aProps[USTR("draw:type")] = USTR("line");
    return aProps;
}

void StandardLineObject::writeConnectionPoints(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler) const
{
    if (maConnectionPoints.size() > 1)
        fprintf(stderr, "OOo format doesn't currently allow extra connection points on a connector\n");
}

class StandardArcObject : public DiaObject
{
public:
    virtual rtl::OUString outputtype() const { return USTR("draw:circle"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual void handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs, PropertyMap &rStyleAttrs);
};

class ZigZagLineObject : public DiaObject
{
    bool mbAutoRoute;
    void confirmZigZag(PropertyMap &rProps, const DiaImporter &rImporter) const;
    void rejectZigZag(PropertyMap &rProps, const DiaImporter &rImporter) const;
public:
    ZigZagLineObject() : mbAutoRoute(false) {}
    virtual rtl::OUString outputtype() const { return USTR("draw:connector"); }
    virtual void write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const;
    virtual void handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs, PropertyMap &rStyleAttrs);
    virtual void adjustConnectionPoints(PropertyMap &rProps, const DiaImporter &rImporter);
};

class StandardPolyLineObject : public DiaObject
{
public:
    virtual rtl::OUString outputtype() const { return USTR("draw:polyline"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
};

class StandardBezierLineObject : public DiaObject
{
public:
    virtual rtl::OUString outputtype() const { return USTR("draw:path"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
};

class StandardBeziergonObject : public DiaObject
{
public:
    virtual rtl::OUString outputtype() const { return USTR("draw:path"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
};

class StandardImageObject : public DiaObject
{
private:
    PropertyMap maImageProps;
public:
    StandardImageObject();
    virtual rtl::OUString outputtype() const { return USTR("draw:frame"); }
    virtual void write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const;
    virtual void handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs, PropertyMap &rStyleAttrs);
};

class StandardTextObject : public DiaObject
{
public:
    StandardTextObject();
    virtual rtl::OUString outputtype() const { return USTR("draw:frame"); }
    virtual void write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const;
    virtual void setdefaultpadding(const uno::Reference<xml::dom::XElement> &) {}
    virtual void resizeIfNarrow(PropertyMap &, const DiaImporter &) {}
};

StandardTextObject::StandardTextObject()
{
    mbShowBorder = false;
    mbShowBackground = false;
    mbAutoWidth = true;
}

class CustomObject : public DiaObject
{
private:
    ShapeTemplate maTemplate;
public:
    CustomObject(shapeimporter aImporter) : maTemplate(aImporter) { /*fprintf(stderr, "custom object created\n");*/ }
    virtual rtl::OUString outputtype() const { return USTR("draw:g"); }
    virtual void write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const;
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual int getConnectionDirection(sal_Int32 nConnection) const;
    virtual void snapConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint, const DiaImporter &rImporter) const;
};

PropertyMap CustomObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = handleStandardObject(rxElem, rImporter);
    GraphicStyleManager &rStyleManager = rImporter.getGraphicStyleManager();
    if (const PropertyMap *pStyle = rStyleManager.getStyleByName(aProps[USTR("draw:style-name")]))
        maTemplate.generateStyles(rStyleManager, *pStyle, mbShowBackground);
    return aProps;
}

void CustomObject::write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter & rImporter) const
{
#ifdef DEBUG
    PropertyMap::const_iterator aEnd = rProps.end();
    for (PropertyMap::const_iterator aI = rProps.begin(); aI != aEnd; ++aI)
    {
        fprintf(stderr, "customobject prop %s value %s\n",
            rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
            rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
        );
    }
#endif

//    rDocHandler->startElement(outputtype(), new SaxAttrList(PropertyMap()));
    maTemplate.convertShapes(rDocHandler, rProps, maTextProps, msString);
//    rDocHandler->endElement(outputtype());
}

int CustomObject::getConnectionDirection(sal_Int32 nConnection) const
{
    nConnection-=4;
    return maTemplate.getConnectionDirection(nConnection);
}

void CustomObject::snapConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint, const DiaImporter &rImporter) const

{
    nConnection-=4;
    basegfx::B2DPoint aConnectionPoint;
    if (maTemplate.getConnectionPoint(nConnection, aConnectionPoint))
    {
        float nCenterX = mnX + mnWidth/2;
        float nCenterY = mnY + mnHeight/2;
        float nX = nCenterX + (aConnectionPoint.getX() * mnWidth / 10);
        float nY = nCenterY + (aConnectionPoint.getY() * mnHeight / 10);

        rPoint.setX(rImporter.unadjustX(nX));
        rPoint.setY(rImporter.unadjustY(nY));
    }
}

StandardImageObject::StandardImageObject()
{
    maImageProps[USTR("xlink:type")] = USTR("simple");
    maImageProps[USTR("xlink:show")] = USTR("embed");
    maImageProps[USTR("xlink:actuate")] = USTR("onLoad");
    mbShowBorder = false;
    mbShowBackground = false;
}

void StandardImageObject::handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs,
    PropertyMap &rStyleAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("file"))
        {
            rtl::OUString sHomeURL, sFileURL, sSystemPath;
            osl::Security aSecurity;
            aSecurity.getHomeDir(sHomeURL);
            sSystemPath = deHashString(valueOfSimpleAttribute(rxElem));
            osl::File::getAbsoluteFileURL(sHomeURL, sSystemPath, sFileURL);
            maImageProps[USTR("xlink:href")] = sFileURL;
        }
        else
            DiaObject::handleObjectAttribute(rxElem, rImporter, rAttrs, rStyleAttrs);
    }
}

void StandardImageObject::write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &) const
{
    rDocHandler->startElement(outputtype(), new SaxAttrList(rProps));
    rDocHandler->startElement(USTR("draw:image"), new SaxAttrList(maImageProps));
    rDocHandler->endElement(USTR("draw:image"));
    rDocHandler->endElement(outputtype());
}

void StandardTextObject::write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const
{
    PropertyMap aProps(rProps);

    rtl::OUString sTextStyleName;
    PropertyMap::const_iterator aI = maTextProps.find(USTR("text:style-name"));
    if (aI != maTextProps.end())
        sTextStyleName = aI->second;
    if (sTextStyleName.getLength())
    {
        const TextStyleManager &rTextStyleManager = rImporter.getTextStyleManager();

        if (const PropertyMap *pStyle = rTextStyleManager.getStyleByName(sTextStyleName))
        {
            uno::Reference< awt::XFont > xFont = rTextStyleManager.getMatchingFont(*pStyle);
            awt::SimpleFontMetric aMetric = xFont->getFontMetric();

            float fHeight = (aMetric.Ascent + aMetric.Leading + aMetric.Descent) / 72.0 * 2.54;
            int nCount=1;
            sal_Int32 nIndex=0;
            do
            {
                msString.getToken(0, '\n', nIndex);
                ++nCount;
            }
            while ( nIndex >= 0 );
            fHeight *= (nCount-1);
            aProps[USTR("svg:height")] = rtl::OUString::number(fHeight + 0.2) + USTR("cm");

            float fDiff = (aMetric.Ascent + aMetric.Leading) / 72.0 * 2.54;
            aProps[USTR("svg:y")] = rtl::OUString::number(mnObjPosY - fDiff) + USTR("cm");
        }
    }

#ifdef DEBUG
    PropertyMap::const_iterator aEnd = aProps.end();
    for (PropertyMap::const_iterator aI = aProps.begin(); aI != aEnd; ++aI)
    {
        fprintf(stderr, "textobject writing prop %s %s\n",
            rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
            rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
        );
    }
#endif

    rDocHandler->startElement(outputtype(), new SaxAttrList(aProps));
    rDocHandler->startElement(USTR("draw:text-box"), new SaxAttrList(PropertyMap()));

    writeText(rDocHandler);

    rDocHandler->endElement(USTR("draw:text-box"));
    rDocHandler->endElement(outputtype());
}

PropertyMap StandardBezierLineObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);
    createViewportFromRect(aProps);
    makeCurvedPathFromPoints(aProps, false);
    return aProps;
}

PropertyMap StandardBeziergonObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);
    createViewportFromRect(aProps);
    makeCurvedPathFromPoints(aProps, true);

    basegfx::B2DPolyPolygon aPolyPoly;
    bool bSuccess = basegfx::tools::importFromSvgD( aPolyPoly, aProps[USTR("svg:d")] );
    if (!bSuccess)
    {
        fprintf(stderr, "Failed to import a polypolygon from %s\n",
            rtl::OUStringToOString(aProps[USTR("draw:d")], RTL_TEXTENCODING_UTF8).getStr());
    }

    basegfx::B2DRange aRange = aPolyPoly.getB2DRange();
    basegfx::B2DHomMatrix aMatrix;
    aMatrix.translate( -aRange.getMinX(), -aRange.getMinY() );
    aMatrix.scale( 10/aRange.getWidth(), 10/aRange.getHeight() ); 
    aMatrix.translate( -5, -5 );
    aPolyPoly.transform( aMatrix );

    sal_uInt32 nCount = aPolyPoly.count();
    for (sal_uInt32 nI = 0; nI < nCount; ++nI)
    {
        basegfx::B2DPolygon aPoly = aPolyPoly.getB2DPolygon(nI);
        sal_uInt32 nInnerCount = aPoly.count();
        for (sal_uInt32 nJ = 0; nJ < nInnerCount; ++nJ)
        {
            if (!aPoly.isBezierSegment(nJ))
            {
                fprintf(stderr, "unexpected non bezier segment\n");
                continue;
            }
            basegfx::B2DCubicBezier aTarget;
            aPoly.getBezierSegment(nJ, aTarget);
            basegfx::B2DPoint aPt;
            aPt = aTarget.getStartPoint();
            //fprintf(stderr, "aStart %f %f\n", aPt.getX(), aPt.getY());
            maConnectionPoints.push_back(ConnectionPoint(aPt.getX(), aPt.getY(), DIR_ALL));
            aPt = aTarget.interpolatePoint(0.5);
            //fprintf(stderr, "aMiddle %f %f\n", aPt.getX(), aPt.getY());
            maConnectionPoints.push_back(ConnectionPoint(aPt.getX(), aPt.getY(), DIR_ALL));
        }
    }
    aRange = aPolyPoly.getB2DRange();
    maConnectionPoints.push_back(ConnectionPoint(aRange.getCenterX(), aRange.getCenterY(), DIR_ALL));

    return aProps;
}

PropertyMap StandardPolyLineObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);
    createViewportFromPoints(aProps[USTR("draw:points")], aProps,
        rImporter.adjustX(0), rImporter.adjustY(0));
    bumpPoints(aProps);
    return aProps;
}

void ZigZagLineObject::handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs,
    PropertyMap &rStyleAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("autorouting"))
            mbAutoRoute = valueOfSimpleAttribute(rxElem) == USTR("true");
        else
            DiaObject::handleObjectAttribute(rxElem, rImporter, rAttrs, rStyleAttrs);
    }
}

static void point_rotate_ccw(basegfx::B2DPoint &p)
{
    double tmp = p.getX();
    p.setX(p.getY());
    p.setY(-tmp);
}

static void point_rotate_cw(basegfx::B2DPoint &p)
{
    double tmp = p.getX();
    p.setX(-p.getY());
    p.setY(tmp);
}

static void point_rotate_180(basegfx::B2DPoint &p)
{
    p.setX(-p.getX());
    p.setY(-p.getY());
}

static int autolayout_normalize_points(int startdir, int enddir,
    const basegfx::B2DPoint &start, const basegfx::B2DPoint &end, basegfx::B2DPoint &newend)
{
    newend.setX(end.getX()-start.getX());
    newend.setY(end.getY()-start.getY());

    if (startdir == DIR_NORTH)
    {
        return enddir;
    }
    else if (startdir == DIR_EAST)
    {
        point_rotate_ccw(newend);
        if (enddir == DIR_NORTH)
            return DIR_WEST;
        return enddir/2;
    }
    else if (startdir == DIR_WEST)
    {
        point_rotate_cw(newend);
        if (enddir == DIR_WEST)
            return DIR_NORTH;
        return enddir*2;
    }
    else
    { 
        /* startdir == DIR_SOUTH */
        point_rotate_180(newend);
        if (enddir < DIR_SOUTH)
            return enddir*4;
        else
            return enddir/4;
    }
    /* Insert handling of other stuff here */
    return enddir;
}

static double distance_point_point_manhattan(const basegfx::B2DPoint &p1, const basegfx::B2DPoint &p2)
{
    double dx = p1.getX() - p2.getX();
    double dy = p1.getY() - p2.getY();
    return fabs(dx) + fabs(dy);
}

#define MIN_DIST 0.0

#define MAX_SMALL_BADNESS 10.0

static double length_badness(double len)
{
    if (len < MIN_DIST)
        return 2*MAX_SMALL_BADNESS/(1.0+len/MIN_DIST) - MAX_SMALL_BADNESS;
    else
        return len-MIN_DIST;
}

#define EXTRA_SEGMENT_BADNESS 10.0

static double calculate_badness(const std::vector<basegfx::B2DPoint> &ps)
{
    double badness = (ps.size()-1)*EXTRA_SEGMENT_BADNESS;
    for (size_t i = 0; i < ps.size()-1; ++i)
    {
      double len = distance_point_point_manhattan(ps[i], ps[i+1]);
      badness += length_badness(len);
    }
    return badness;
}


static double autoroute_layout_parallel(const basegfx::B2DPoint &to, std::vector<basegfx::B2DPoint> &ps)
{
    if (fabs(to.getX()) > MIN_DIST)
    {
        double top = std::min(-MIN_DIST, to.getY()-MIN_DIST);
        ps.resize(4);
        /* points[0] is 0,0 */
        ps[1].setY(top);
        ps[2].setX(to.getX());
        ps[2].setY(top);
        ps[3] = to;
    }
    else if (to.getY() > 0)
    {
        /* Close together, end below */
        double top = -MIN_DIST;
        double off = to.getX()+MIN_DIST*(to.getX()>0?1.0:-1.0);
        double bottom = to.getY()-MIN_DIST;
        ps.resize(6);
        /* points[0] is 0,0 */
        ps[1].setY(top);
        ps[2].setX(off);
        ps[2].setY(top);
        ps[3].setX(off);
        ps[3].setY(bottom);
        ps[4].setX(to.getX());
        ps[4].setY(bottom);
        ps[5] = to;
    }
    else
    {
        double top = to.getY()-MIN_DIST;
        double off = MIN_DIST*(to.getX()>0?-1.0:1.0);
        double bottom = -MIN_DIST;
        ps.resize(6);
        /* points[0] is 0,0 */
        ps[1].setY(bottom);
        ps[2].setX(off);
        ps[2].setY(bottom);
        ps[3].setX(off);
        ps[3].setY(top);
        ps[4].setX(to.getX());
        ps[4].setY(top);
        ps[5] = to;
    }
    return calculate_badness(ps);
}

static double autoroute_layout_opposite(const basegfx::B2DPoint &to, std::vector<basegfx::B2DPoint> &ps)
{
    if (to.getY() < -MIN_DIST)
    {
        ps.resize(4);
        if (fabs(to.getX()) < 0.00000001)
        {
            ps[2] = ps[3] = to;
            return length_badness(fabs(to.getY()))+2*EXTRA_SEGMENT_BADNESS;
        }
        else
        {
            double mid = to.getY()/2;
            /* points[0] is 0,0 */
            ps[1].setY(mid);
            ps[2].setX(to.getX());
            ps[2].setY(mid);
            ps[3] = to;
            return 2*length_badness(fabs(mid))+2*EXTRA_SEGMENT_BADNESS;
        }
    }
    else if (fabs(to.getX()) > 2*MIN_DIST)
    {
        double mid = to.getX()/2;
        ps.resize(6);
        /* points[0] is 0,0 */
        ps[1].setY(-MIN_DIST);
        ps[2].setX(mid);
        ps[2].setY(-MIN_DIST);
        ps[3].setX(mid);
        ps[3].setY(to.getY()+MIN_DIST);
        ps[4].setX(to.getX());
        ps[4].setY(to.getY()+MIN_DIST);
        ps[5] = to;
    }
    else
    {
        double off = MIN_DIST*(to.getX()>0?-1.0:1.0);
        ps.resize(6);
        ps[1].setY(-MIN_DIST);
        ps[2].setX(off);
        ps[2].setY(-MIN_DIST);
        ps[3].setX(off);
        ps[3].setY(to.getY()+MIN_DIST);
        ps[4].setX(to.getX());
        ps[4].setY(to.getY()+MIN_DIST);
        ps[5] = to;
    }
    return calculate_badness(ps);
}

static double autoroute_layout_orthogonal(const basegfx::B2DPoint &to, 
    int enddir, std::vector<basegfx::B2DPoint> &ps)
{
    /* This one doesn't consider enddir yet, not more complex layouts. */
    double dirmult = (enddir==DIR_WEST?1.0:-1.0);
    if (to.getY() < -MIN_DIST)
    {
        if (dirmult*to.getX() > MIN_DIST)
        {
            ps.resize(3);
            /* points[0] is 0,0 */
            ps[1].setY(to.getY());
            ps[2] = to;
        }
        else
        {
            double off;
            if (dirmult*to.getX() > 0)
                off = -dirmult*MIN_DIST;
            else
                off = -dirmult*(MIN_DIST+fabs(to.getX()));
            ps.resize(5);
            ps[1].setY(-MIN_DIST);
            ps[2].setX(off);
            ps[2].setY(-MIN_DIST);
            ps[3].setX(off);
            ps[3].setY(to.getY());
            ps[4] = to;
        }
    }
    else
    {
        if (dirmult*to.getX() > 2*MIN_DIST)
        {
            double mid = to.getX()/2;
            ps.resize(5);
            ps[1].setY(-MIN_DIST);
            ps[2].setX(mid);
            ps[2].setY(-MIN_DIST);
            ps[3].setX(mid);
            ps[3].setY(to.getY());
            ps[4] = to;
        }
        else
        {
            double off;
            if (dirmult*to.getX() > 0)
                off = -dirmult*MIN_DIST;
            else
                off = -dirmult*(MIN_DIST+fabs(to.getX()));
            ps.resize(5);
            ps[1].setY(-MIN_DIST);
            ps[2].setX(off);
            ps[2].setY(-MIN_DIST);
            ps[3].setX(off);
            ps[3].setY(to.getY());
            ps[4] = to;
        }
    }
    return calculate_badness(ps);
}

void point_add(basegfx::B2DPoint &p1, const basegfx::B2DPoint &p2)
{
    p1.setX(p1.getX() + p2.getX());
    p1.setY(p1.getY() + p2.getY());
}

static std::vector<basegfx::B2DPoint> autolayout_unnormalize_points(int startdir,
    const basegfx::B2DPoint &start, const std::vector<basegfx::B2DPoint> &points)
{
    std::vector<basegfx::B2DPoint> newpoints(points.size());
    if (startdir == DIR_NORTH)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            newpoints[i] = points[i];
            point_add(newpoints[i], start);
        }
    }
    else if (startdir == DIR_WEST)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            newpoints[i] = points[i];
            point_rotate_ccw(newpoints[i]);
            point_add(newpoints[i], start);
        }
    }
    else if (startdir == DIR_SOUTH)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            newpoints[i] = points[i];
            point_rotate_180(newpoints[i]);
            point_add(newpoints[i], start);
        }
    }
    else if (startdir == DIR_EAST)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            newpoints[i] = points[i];
            point_rotate_cw(newpoints[i]);
            point_add(newpoints[i], start);
        }
    }
    return newpoints;
}

#define MAX_BADNESS 10000.0

bool what_would_dia_do(const basegfx::B2DPoint &frompos, int fromdir,
                       const basegfx::B2DPoint &topos, int todir,
                       std::vector<basegfx::B2DPoint> &best_layout)
{
    double min_badness = MAX_BADNESS;
    int startdir, enddir;

    for (startdir = DIR_NORTH; startdir <= DIR_WEST; startdir *= 2)
    {
        for (enddir = DIR_NORTH; enddir <= DIR_WEST; enddir *= 2)
        {
            if ((fromdir & startdir) && (todir & enddir))
            {
	            double this_badness;
                std::vector<basegfx::B2DPoint> this_layout;
                int normal_enddir;
                basegfx::B2DPoint startpoint, endpoint;
                basegfx::B2DPoint otherpoint;

                startpoint = frompos;
                endpoint = topos;

                normal_enddir = autolayout_normalize_points(startdir, enddir,
                    startpoint, endpoint, otherpoint);

	            if (normal_enddir == DIR_NORTH )
                {
	                this_badness = autoroute_layout_parallel(otherpoint, this_layout);
	            }
                else if (normal_enddir == DIR_SOUTH)
                {
	                this_badness = autoroute_layout_opposite(otherpoint, this_layout);
	            }
                else
                {
	                this_badness = autoroute_layout_orthogonal(otherpoint,
						normal_enddir, this_layout);
                }

                if (!this_layout.empty())
                {
	                if (this_badness-min_badness < -0.00001)
                    {
	                    min_badness = this_badness;
	                    best_layout = autolayout_unnormalize_points(startdir, startpoint,
							this_layout);
	                }
                    else
                    {
	                    this_layout.clear();
	                }
	            }
            }
        }    
    }

    if (min_badness < MAX_BADNESS)
    {
#ifdef DEBUG
        fprintf(stderr, "no points is %ld\n", best_layout.size());
        for (size_t i = 0; i < best_layout.size(); ++i)
        {
            fprintf(stderr, "%f %f\n", best_layout[i].getX(), best_layout[i].getY());
        }
#endif
        return true;
    }
    return false;
}

bool what_would_ooo_do(const basegfx::B2DPoint &frompos, int fromdir,
                       const basegfx::B2DPoint &topos, int todir,
                       std::vector<basegfx::B2DPoint> &best_layout)
{
    //to-do, use OOo algorithms here
    return what_would_dia_do(frompos, fromdir, topos, todir, best_layout);
}

#define BUMPFACTOR 1000

void ZigZagLineObject::confirmZigZag(PropertyMap &rProps, const DiaImporter &rImporter) const
{
    rtl::OUString sPoints = rProps[USTR("draw:points")];

    sal_Int32 nIndex = 0;
    float nStartX = sPoints.getToken(0, ',', nIndex).toFloat();
    nStartX = rImporter.adjustX(nStartX);
    float nStartY = sPoints.getToken(0, ' ', nIndex).toFloat();
    nStartY = rImporter.adjustY(nStartY);

    rtl::OUString sNewPoints =
        rtl::OUString::number(nStartX) + USTR(",") +
        rtl::OUString::number(nStartY);

    //get endpoints, and fix up positions
    float nEndX, nEndY;
    do
    {
        nEndX = sPoints.getToken(0, ',', nIndex).toFloat();
        nEndX = rImporter.adjustX(nEndX);

        nEndY = sPoints.getToken(0, ' ', nIndex).toFloat();
        nEndY = rImporter.adjustY(nEndY);

        sNewPoints = sNewPoints + USTR(" ");
        sNewPoints = sNewPoints +
            rtl::OUString::number(nEndX) + USTR(",") + rtl::OUString::number(nEndY);
    }
    while ( nIndex >= 0 );
    rProps[USTR("draw:points")] = sNewPoints;

    rProps[USTR("svg:x1")] = rtl::OUString::number(nStartX)+USTR("cm");
    rProps[USTR("svg:y1")] = rtl::OUString::number(nStartY)+USTR("cm");

    rProps[USTR("svg:x2")] = rtl::OUString::number(nEndX)+USTR("cm");
    rProps[USTR("svg:y2")] = rtl::OUString::number(nEndY)+USTR("cm");

    bumpPoints(rProps, BUMPFACTOR);
    makePathFromPoints(rProps, false);
}

void ZigZagLineObject::rejectZigZag(PropertyMap &rProps, const DiaImporter &rImporter) const
{
    createViewportFromPoints(rProps[USTR("draw:points")], rProps,
        rImporter.adjustX(0), rImporter.adjustY(0));
    bumpPoints(rProps);
}

void ZigZagLineObject::adjustConnectionPoints(PropertyMap &rProps, const DiaImporter &rImporter)
{
    rtl::OUString sOutputType = outputtype();

    rtl::OUString sStartShape, sStartPoint, sEndShape, sEndPoint;
    PropertyMap::const_iterator aI;
    aI = rProps.find(USTR("draw:start-shape"));
    if (aI != rProps.end())
    {
        sStartShape = aI->second;
    }
    aI = rProps.find(USTR("draw:start-glue-point"));
    if (aI != rProps.end())
    {
        sStartPoint = aI->second;
    }
    aI = rProps.find(USTR("draw:end-shape"));
    if (aI != rProps.end())
    {
        sEndShape = aI->second;
    }
    aI = rProps.find(USTR("draw:end-glue-point"));
    if (aI != rProps.end())
    {
        sEndPoint = aI->second;
    }

    diaobject aStartShape, aEndShape;

    if (sStartShape.getLength())
    {
        if (!sStartPoint.getLength())
            fprintf(stderr, "start shape, but no start point!\n");
        else
            aStartShape = rImporter.getobjectbyid(sStartShape);
    }

    if (sEndShape.getLength())
    {
        if (!sEndPoint.getLength())
            fprintf(stderr, "end shape, but no end point!\n");
        else
            aEndShape = rImporter.getobjectbyid(sEndShape);
    }

    rtl::OUString sPoints = rProps[USTR("draw:points")];
    std::vector<basegfx::B2DPoint> dia_layout;

    sal_Int32 nIndex = 0;
    float xend, xstart = sPoints.getToken(0, ',', nIndex).toFloat();
    float yend, ystart = sPoints.getToken(0, ' ', nIndex).toFloat();
    dia_layout.push_back(basegfx::B2DPoint(xstart, ystart));
    size_t nPairs=1;
    do
    {
        xend = sPoints.getToken(0, ',', nIndex).toFloat();
        yend = sPoints.getToken(0, ' ', nIndex).toFloat();
        dia_layout.push_back(basegfx::B2DPoint(xend, yend));
        ++nPairs;
    }
    while ( nIndex >= 0 );

    if (aStartShape.get())
    {
        basegfx::B2DPoint aOrig = dia_layout.front();
        aStartShape->snapConnectionPoint(sStartPoint.toInt32(), dia_layout.front(), rImporter);
        std::vector<basegfx::B2DPoint>::iterator aEnd = dia_layout.end();
        for (std::vector<basegfx::B2DPoint>::iterator aI = dia_layout.begin(); aI != aEnd; ++aI)
        {
            if (aI->getX() == aOrig.getX())
                aI->setX(dia_layout.front().getX());
            if (aI->getY() == aOrig.getY())
                aI->setY(dia_layout.front().getY());
        }
    }
    if (aEndShape.get())
    {
        basegfx::B2DPoint aOrig = dia_layout.front();
        aEndShape->snapConnectionPoint(sEndPoint.toInt32(), dia_layout.back(), rImporter);
        std::vector<basegfx::B2DPoint>::iterator aEnd = dia_layout.end();
        for (std::vector<basegfx::B2DPoint>::iterator aI = dia_layout.begin(); aI != aEnd; ++aI)
        {
            if (aI->getX() == aOrig.getX())
                aI->setX(dia_layout.front().getX());
            if (aI->getY() == aOrig.getY())
                aI->setY(dia_layout.front().getY());
        }
    }

    rtl::OUString sNewPoints;

    std::vector<basegfx::B2DPoint>::const_iterator aEnd = dia_layout.end();
    for (std::vector<basegfx::B2DPoint>::const_iterator aI = dia_layout.begin(); aI != aEnd; ++aI)
    {
        if (sNewPoints.getLength())
            sNewPoints = sNewPoints + USTR(" ");
        sNewPoints = sNewPoints +
            rtl::OUString::number(aI->getX()) + USTR(",") + rtl::OUString::number(aI->getY());
    }

    rProps[USTR("draw:points")] = sNewPoints;
}

void ZigZagLineObject::write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const
{
    rtl::OUString sOutputType = outputtype();

    PropertyMap aProps = rProps;

    rtl::OUString sStartShape, sStartPoint, sEndShape, sEndPoint;
    PropertyMap::const_iterator aI;
    aI = aProps.find(USTR("draw:start-shape"));
    if (aI != aProps.end())
    {
        sStartShape = aI->second;
    }
    aI = aProps.find(USTR("draw:start-glue-point"));
    if (aI != aProps.end())
    {
        sStartPoint = aI->second;
    }
    aI = aProps.find(USTR("draw:end-shape"));
    if (aI != aProps.end())
    {
        sEndShape = aI->second;
    }
    aI = aProps.find(USTR("draw:end-glue-point"));
    if (aI != aProps.end())
    {
        sEndPoint = aI->second;
    }

    diaobject aStartShape, aEndShape;
    int startdirection = DIR_ALL, enddirection = DIR_ALL;

    if (sStartShape.getLength())
    {
        if (!sStartPoint.getLength())
            fprintf(stderr, "start shape, but no start point!\n");
        else
            aStartShape = rImporter.getobjectbyid(sStartShape);
    }

    if (aStartShape.get())
        startdirection = aStartShape->getConnectionDirection(sStartPoint.toInt32());

    if (sEndShape.getLength())
    {
        if (!sEndPoint.getLength())
            fprintf(stderr, "end shape, but no end point!\n");
        else
            aEndShape = rImporter.getobjectbyid(sEndShape);
    }

    if (aEndShape.get())
        enddirection = aEndShape->getConnectionDirection(sEndPoint.toInt32());

    rtl::OUString sPoints = aProps[USTR("draw:points")];
    std::vector<basegfx::B2DPoint> dia_layout;

    sal_Int32 nIndex = 0;
    float xend, xstart = sPoints.getToken(0, ',', nIndex).toFloat();
    float yend, ystart = sPoints.getToken(0, ' ', nIndex).toFloat();
    dia_layout.push_back(basegfx::B2DPoint(xstart, ystart));
    size_t nPairs=1;
    do
    {
        xend = sPoints.getToken(0, ',', nIndex).toFloat();
        yend = sPoints.getToken(0, ' ', nIndex).toFloat();
        dia_layout.push_back(basegfx::B2DPoint(xend, yend));
        ++nPairs;
    }
    while ( nIndex >= 0 );

    std::vector<basegfx::B2DPoint> best_layout;
    bool ok = what_would_dia_do(dia_layout.front(), startdirection,
                      dia_layout.back(), enddirection, best_layout);

    if (nPairs != 4)
        ok = false;

#if DEBUG_CONNECTOR_RECALCULATE
    if (0)
#else
    if (ok)
#endif
    {
        if (best_layout.size() != nPairs)
        {
#if DEBUG
            fprintf(stderr, "to-do, more work (a) here\n");
#endif
            ok = false;
        }
        if (ok)
        {
            if ((best_layout[0] != dia_layout.front()) || (best_layout[3] != dia_layout.back()))
            {
#if DEBUG
                fprintf(stderr, "impossible!\n");
#endif
                ok = false;
            }
        }
        if (ok)
        {
            if (best_layout[1].getX() != best_layout[2].getX() &&
                best_layout[1].getY() != best_layout[2].getY())
            {
#if DEBUG
                fprintf(stderr, "not good!\n");
#endif
                ok = false;
            }
        }
        if (ok)
        {
            float fSkew=0.0;

            if (best_layout[1].getX() != best_layout[2].getX())
                fSkew = dia_layout[2].getY() - best_layout[2].getY();
            else if (best_layout[1].getY() != best_layout[2].getY())
                fSkew = dia_layout[2].getX() - best_layout[2].getX();

            aProps[USTR("draw:line-skew")] = rtl::OUString::number(fSkew) + USTR("cm");
            confirmZigZag(aProps, rImporter);
        }
    }
    if (!ok)
    {
        //Fallback to using a PolyLine if we can't get a correct Connector
        if (nPairs > 4)
            fprintf(stderr, "INFO: ZigZagLine has more segments than OOo currently supports, replacing with PolyLine\n");
        else
            fprintf(stderr, "INFO: Forced to use a PolyLine instead of a Connector\n");
        rejectZigZag(aProps, rImporter);
        sOutputType = USTR("draw:polyline");
    }

#if DEBUG
            PropertyMap::const_iterator aEnd = aProps.end();
            for (PropertyMap::const_iterator aI = aProps.begin(); aI != aEnd; ++aI)
            {
                fprintf(stderr, "writing connection prop %s %s\n",
                    rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
                    rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
                );
            }
#endif

    
    rDocHandler->startElement(sOutputType, new SaxAttrList(aProps));
    writeConnectionPoints(rDocHandler);
    if (msString.getLength())
        writeText(rDocHandler);
    rDocHandler->endElement(outputtype());
}

PropertyMap StandardArcObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);

    rtl::OUString sPoints = aProps[USTR("dia:endpoints")];
    sal_Int32 nIndex = 0;
    float x1 = sPoints.getToken(0, ',', nIndex).toFloat();
    float y1 = sPoints.getToken(0, ' ', nIndex).toFloat();
    float x2 = sPoints.getToken(0, ',', nIndex).toFloat();
    float y2 = sPoints.getToken(0, ' ', nIndex).toFloat();

    float curve_distance = aProps[USTR("dia:curve_distance")].toFloat();

    float lensq = (x2-x1)*(x2-x1) + (y2-y1)*(y2-y1);
    float radius = lensq/(8*curve_distance) + curve_distance/2.0;

    float alpha;

    if (lensq == 0.0)
        alpha = 1.0;
    else
        alpha = (radius - curve_distance) / sqrt(lensq);

    float xc = (x1 + x2) / 2.0 + (y2 - y1)*alpha;
    float yc = (y1 + y2) / 2.0 + (x1 - x2)*alpha;

    float angle1 = -atan2(y1-yc, x1-xc)*180.0/M_PI;
    if (angle1<0)
        angle1+=360.0;
    float angle2 = -atan2(y2-yc, x2-xc)*180.0/M_PI;
    if (angle2<0)
        angle2+=360.0;

    if (radius<0.0)
    {
        std::swap(angle1, angle2);
        radius = -radius;
    }

    aProps[USTR("draw:kind")] = USTR("arc");
    aProps[USTR("draw:start-angle")] = rtl::OUString::number(angle1);
    aProps[USTR("draw:end-angle")] = rtl::OUString::number(angle2);
    mnWidth = mnHeight = radius*2;
    mnX = rImporter.adjustX(xc-radius);
    mnY = rImporter.adjustY(yc-radius);
    aProps[USTR("svg:width")] = aProps[USTR("svg:height")] = rtl::OUString::number(mnWidth) + USTR("cm");
    aProps[USTR("svg:x")] = rtl::OUString::number(mnX) + USTR("cm");
    aProps[USTR("svg:y")] = rtl::OUString::number(mnY) + USTR("cm");

    return aProps;
}

void StandardArcObject::handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs,
    PropertyMap &rStyleAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("conn_endpoints"))
            rAttrs[USTR("dia:endpoints")] = valueOfSimpleAttribute(rxElem);
        else if (sName == USTR("curve_distance"))
            rAttrs[USTR("dia:curve_distance")] = valueOfSimpleAttribute(rxElem);
        else
            DiaObject::handleObjectAttribute(rxElem, rImporter, rAttrs, rStyleAttrs);
    }
}

PropertyMap StandardPolygonObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);
    createViewportFromRect(aProps);

    basegfx::B2DPolygon aPoly;
    bool bSuccess = basegfx::tools::importFromSvgPoints(aPoly, aProps[USTR("draw:points")]);
    if (!bSuccess)
    {
        fprintf(stderr, "Failed to import a polygon from %s\n",
            rtl::OUStringToOString(aProps[USTR("draw:points")], RTL_TEXTENCODING_UTF8).getStr());
    }
    aPoly.setClosed(true);

    basegfx::B2DRange aRange = aPoly.getB2DRange();
    basegfx::B2DHomMatrix aMatrix;
    aMatrix.translate( -aRange.getMinX(), -aRange.getMinY() );
    aMatrix.scale( 10/aRange.getWidth(), 10/aRange.getHeight() ); 
    aMatrix.translate( -5, -5 );
    aPoly.transform( aMatrix );

    bool first = true;
    basegfx::B2DPoint aFirst;
    basegfx::B2DPoint aPrev;
    sal_uInt32 nCount = aPoly.count();
    for (sal_uInt32 nI = 0; nI < nCount; ++nI)
    {
        basegfx::B2DPoint aPt = aPoly.getB2DPoint(nI);
        if (first)
        {
            aFirst = aPt;
            first = false;
        }
        else
        {
            maConnectionPoints.push_back(ConnectionPoint(
                (aPt.getX()+aPrev.getX())/2, (aPt.getY()+aPrev.getY())/2, DIR_ALL));
        }
        maConnectionPoints.push_back(ConnectionPoint(aPt.getX(), aPt.getY(), DIR_ALL));
        aPrev = aPt;
    }

    maConnectionPoints.push_back(ConnectionPoint(
        (aPrev.getX()+aFirst.getX())/2, (aPrev.getY()+aFirst.getY())/2, DIR_ALL));

    return aProps;
}

shapeimporter DiaImporter::findCustomImporter(const rtl::OUString &rName)
{
    if (maTemplates.empty())
        recursiveScan(msInstallDir + USTR("shapes"));
    return maTemplates[rName];
}

void DiaImporter::recursiveScan(const rtl::OUString &rDir)
{
    osl::Directory aShapeDir(rDir);
    if (aShapeDir.open() != osl::FileBase::E_None)
        return;
    osl::DirectoryItem aItem;
    while (aShapeDir.getNextItem(aItem) == osl::FileBase::E_None)
    {
        osl::FileStatus aStatus(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileURL);
        if (!aItem.getFileStatus(aStatus) == osl::FileBase::E_None)
            continue;
        if (aStatus.getFileType() == osl::FileStatus::Directory)
            recursiveScan(aStatus.getFileURL());
        else
            importShape(aStatus.getFileURL());
    }
}

void DiaImporter::importShape(const rtl::OUString &rShapeFile) throw()
{
    try
    {

        uno::Reference< ucb::XSimpleFileAccess > xSimpleFileAccess(
            mxCtx->getServiceManager()->createInstanceWithContext(USTR("com.sun.star.ucb.SimpleFileAccess"),
                mxCtx), uno::UNO_QUERY_THROW);
        uno::Reference< io::XInputStream > xInputStream(xSimpleFileAccess->openFileRead(rShapeFile));
        uno::Reference<xml::dom::XDocumentBuilder> xDomBuilder(
            mxMSF->createInstance(USTR("com.sun.star.xml.dom.DocumentBuilder") ), uno::UNO_QUERY_THROW);
        uno::Reference<xml::dom::XDocument> xDom(xDomBuilder->parse(xInputStream), uno::UNO_QUERY_THROW);
        uno::Reference<xml::dom::XElement> xDocElem(xDom->getDocumentElement(), uno::UNO_QUERY_THROW);
    
        shapeimporter pImporter(new ShapeImporter());
        if (pImporter->import(xDocElem))
        {
            //fprintf(stderr, "title s %s\n", rtl::OUStringToOString(pImporter->getTitle(), RTL_TEXTENCODING_UTF8).getStr());
            maTemplates[pImporter->getTitle()] = pImporter;
        }
    }
    catch ( ... )
    {
        fprintf(stderr, "Could not parse %s\n", rtl::OUStringToOString(rShapeFile, RTL_TEXTENCODING_UTF8).getStr());
    }
}

class FlowchartBoxObject : public DiaObject
{
public:
    FlowchartBoxObject();
    virtual rtl::OUString outputtype() const { return USTR("draw:rect"); }
};

FlowchartBoxObject::FlowchartBoxObject()
{
    maConnectionPoints.push_back(ConnectionPoint(-5, -5, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-2.5, -5, DIR_NORTH));
    maConnectionPoints.push_back(ConnectionPoint(0, -5, DIR_NORTH));
    maConnectionPoints.push_back(ConnectionPoint(2.5, -5, DIR_NORTH));
    maConnectionPoints.push_back(ConnectionPoint(5, -5, DIR_NORTHEAST));

    maConnectionPoints.push_back(ConnectionPoint(-5, -2.5, DIR_WEST));
    maConnectionPoints.push_back(ConnectionPoint(5, -2.5, DIR_EAST));

    maConnectionPoints.push_back(ConnectionPoint(-5, 0, DIR_WEST));
    maConnectionPoints.push_back(ConnectionPoint(5, 0, DIR_EAST));

    maConnectionPoints.push_back(ConnectionPoint(-5, 2.5, DIR_WEST));
    maConnectionPoints.push_back(ConnectionPoint(5, 2.5, DIR_EAST));

    maConnectionPoints.push_back(ConnectionPoint(-5, 5, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-2.5, 5, DIR_SOUTH));
    maConnectionPoints.push_back(ConnectionPoint(0, 5, DIR_SOUTH));
    maConnectionPoints.push_back(ConnectionPoint(2.5, 5, DIR_SOUTH));
    maConnectionPoints.push_back(ConnectionPoint(5, 5, DIR_SOUTHEAST));

    maConnectionPoints.push_back(ConnectionPoint(0, 0, DIR_ALL));
}


class FlowchartParallelogramObject : public DiaObject
{
    float mnShearAngle;
public:
    FlowchartParallelogramObject() : mnShearAngle(45) {}
    virtual rtl::OUString outputtype() const { return USTR("draw:polygon"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual void handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter,
        PropertyMap &rAttrs,
        PropertyMap &rStyleAttrs);
};

void FlowchartParallelogramObject::handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs,
    PropertyMap &rStyleAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("shear_angle"))
            mnShearAngle = valueOfSimpleAttribute(rxElem).toFloat();
        else
            DiaObject::handleObjectAttribute(rxElem, rImporter, rAttrs, rStyleAttrs);
    }
}

namespace
{
    rtl::OUString makePointsString(const basegfx::B2DPolygon &rPoly)
    {
        rtl::OUString sPoints;
        sal_uInt32 nEnd = rPoly.count();
        for (sal_uInt32 i = 0; i < nEnd; ++i)
        {
            if (sPoints.getLength())
                sPoints = sPoints + USTR(" ");
            basegfx::B2DPoint aPoint = rPoly.getB2DPoint(i);
            sPoints = sPoints + rtl::OUString::number(aPoint.getX())+
                USTR(",") + rtl::OUString::number(aPoint.getY());
        }
        return sPoints;
    }
}

PropertyMap FlowchartParallelogramObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);

    basegfx::B2DPolygon aPoly = basegfx::tools::createPolygonFromRect(basegfx::B2DRectangle(mnX, mnY, mnX+mnWidth, mnY+mnHeight));
    basegfx::B2DRange aOldSize = aPoly.getB2DRange();

    basegfx::B2DHomMatrix aMatrix;
    aMatrix.shearX(-tan(M_PI/2.0 - M_PI/180.0 * mnShearAngle));
    aPoly.transform(aMatrix);

    basegfx::B2DRange aNewSize = aPoly.getB2DRange();
    aMatrix = basegfx::B2DHomMatrix();
    aMatrix.scale(aOldSize.getWidth()/aNewSize.getWidth(), 1);
    aPoly.transform(aMatrix);

    aProps[USTR("draw:points")] = makePointsString(aPoly);
    createViewportFromRect(aProps);
    return aProps;
}

class FlowchartDiamondObject : public DiaObject
{
public:
    FlowchartDiamondObject();
    virtual rtl::OUString outputtype() const { return USTR("draw:polygon"); }
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual void resizeIfNarrow(PropertyMap &rProps, const DiaImporter &rImporter);
};

FlowchartDiamondObject::FlowchartDiamondObject()
{
    maConnectionPoints.push_back(ConnectionPoint(0, -5, DIR_NORTH));

    maConnectionPoints.push_back(ConnectionPoint(1.25, -3.75, DIR_NORTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(2.5, -2.5, DIR_NORTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(3.75, -1.25, DIR_NORTHEAST));

    maConnectionPoints.push_back(ConnectionPoint(5, 0, DIR_EAST));

    maConnectionPoints.push_back(ConnectionPoint(3.25, 1.25, DIR_SOUTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(2.5, 2.5, DIR_SOUTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(1.25, 3.75, DIR_SOUTHEAST));

    maConnectionPoints.push_back(ConnectionPoint(0, 5, DIR_SOUTH));

    maConnectionPoints.push_back(ConnectionPoint(-1.25, 3.75, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-2.5, 2.5, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-3.75, 1.25, DIR_SOUTHWEST));

    maConnectionPoints.push_back(ConnectionPoint(-5, 0, DIR_WEST));

    maConnectionPoints.push_back(ConnectionPoint(-3.75, -1.25, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-2.5, -2.5, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-1.25, -3.75, DIR_NORTHWEST));

    maConnectionPoints.push_back(ConnectionPoint(0, 0, DIR_ALL));
}

PropertyMap FlowchartDiamondObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);

    aProps[USTR("draw:points")] =
        rtl::OUString::number(mnX+mnWidth/2) + USTR(",") + rtl::OUString::number(mnY) +
        USTR(" ") +
        rtl::OUString::number(mnX+mnWidth) + USTR(",") + rtl::OUString::number(mnY+mnHeight/2) +
        USTR(" ") +
        rtl::OUString::number(mnX+mnWidth/2) + USTR(",") + rtl::OUString::number(mnY+mnHeight) +
        USTR(" ") +
        rtl::OUString::number(mnX) + USTR(",") + rtl::OUString::number(mnY+mnHeight/2);

    createViewportFromRect(aProps);
    return aProps;
}

void FlowchartDiamondObject::resizeIfNarrow(PropertyMap &rProps, const DiaImporter &rImporter)
{
    PropertyMap::const_iterator aI;

    float fStrokeWidth = 0.1;
    float fWidth = 0, fHeight = 0;

    aI = rProps.find(USTR("svg:width"));
    if (aI != rProps.end())
        fWidth = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat();
    aI = rProps.find(USTR("svg:height"));
    if (aI != rProps.end())
        fHeight = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat();

    rtl::OUString sDrawStyleName;
    aI = rProps.find(USTR("draw:style-name"));
    if (aI != rProps.end())
        sDrawStyleName = aI->second;

    if (sDrawStyleName.getLength())
    {
        const GraphicStyleManager &rGraphicStyleManager = rImporter.getGraphicStyleManager();
        if (const PropertyMap *pStyle = rGraphicStyleManager.getStyleByName(sDrawStyleName))
        {
            aI = pStyle->find(USTR("svg:stroke-width"));
            if (aI != pStyle->end())
                fStrokeWidth = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat();
        }
    }

    rtl::OUString sTextStyleName;
    aI = maTextProps.find(USTR("text:style-name"));
    if (aI != maTextProps.end())
        sTextStyleName = aI->second;
    if (sTextStyleName.getLength())
    {
        const TextStyleManager &rTextStyleManager = rImporter.getTextStyleManager();
        double fTextWidth = 0.0;

        float fCalcHeight = 0.0;
        if (const PropertyMap *pStyle = rTextStyleManager.getStyleByName(sTextStyleName))
        {
            uno::Reference< awt::XFont > xFont = rTextStyleManager.getMatchingFont(*pStyle);
            awt::SimpleFontMetric aMetric = xFont->getFontMetric();
            fCalcHeight = (aMetric.Ascent + aMetric.Leading + aMetric.Descent) / 72.0 * 2.54;
        }

        int nCount=1;
        sal_Int32 nIndex=0;
        do
        {
            rtl::OUString sSpan = msString.getToken(0, '\n', nIndex);
            double fSpanWidth = rTextStyleManager.getStringWidth(sTextStyleName, sSpan);
            if (fSpanWidth > fTextWidth)
                fTextWidth = fSpanWidth;
            ++nCount;
        }
        while ( nIndex >= 0 );
        fCalcHeight *= (nCount-1);
        fCalcHeight += mnPadding * 2 + fStrokeWidth * 2;

        double fCalcWidth = mnPadding * 2 + fStrokeWidth * 2 + fTextWidth;

        double fNewWidth = fWidth, fNewHeight = fHeight;
        //fairly arbitrary dia settings
        if (fCalcHeight > (fWidth - fCalcWidth) * fHeight / fWidth)
        {
            /* increase size of the diamond while keeping its aspect ratio */
            float grad = fWidth/fHeight;
            if (grad < 1.0/4) grad = 1.0/4;
            if (grad > 4)     grad = 4;
            fNewWidth = fCalcWidth + fCalcHeight * grad;
            fNewHeight = fCalcHeight + fCalcWidth  / grad;
        }

        if (fNewWidth > fWidth)
        {
            mnWidth=fNewWidth;
            rProps[USTR("svg:width")] = rtl::OUString::number(fNewWidth)+USTR("cm");
            double fDiff = (fNewWidth - fWidth) / 2;
            mnX-=fDiff;
            rProps[USTR("svg:x")] = rtl::OUString::number(mnX)+USTR("cm");
        }

        if (fNewHeight > fHeight)
        {
            mnHeight=fNewHeight;
            rProps[USTR("svg:height")] = rtl::OUString::number(fNewHeight)+USTR("cm");
            double fDiff = (fNewHeight - fHeight) / 2;
            mnY-=fDiff;
            rProps[USTR("svg:y")] = rtl::OUString::number(mnY)+USTR("cm");
        }

        rProps[USTR("draw:points")] =
            rtl::OUString::number(mnX+mnWidth/2) + USTR(",") + rtl::OUString::number(mnY) +
            USTR(" ") +
            rtl::OUString::number(mnX+mnWidth) + USTR(",") + rtl::OUString::number(mnY+mnHeight/2) +
            USTR(" ") +
            rtl::OUString::number(mnX+mnWidth/2) + USTR(",") + rtl::OUString::number(mnY+mnHeight) +
            USTR(" ") +
            rtl::OUString::number(mnX) + USTR(",") + rtl::OUString::number(mnY+mnHeight/2);

        createViewportFromRect(rProps);
    }
}

class KaosGoalObject : public DiaObject
{
private:
    sal_Int32 mnType;
public:
    KaosGoalObject();
    virtual rtl::OUString outputtype() const;
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual void handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
        DiaImporter &rImporter, PropertyMap &rAttrs, PropertyMap &rStyleAttrs);
};

rtl::OUString KaosGoalObject::outputtype() const
{
    rtl::OUString sRet = USTR("draw:polygon");
    switch (mnType)
    {
        case 0:
        case 3:
            sRet = USTR("draw:path");
            break;
        default:
            break;
    }
    return sRet;
}

void KaosGoalObject::handleObjectAttribute(const uno::Reference<xml::dom::XElement> &rxElem,
    DiaImporter &rImporter,
    PropertyMap &rAttrs,
    PropertyMap &rStyleAttrs)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    uno::Reference<xml::dom::XNode> xNode(xAttributes->getNamedItem(USTR("name")));
    if(xNode.is())
    {
        rtl::OUString sName(xNode->getNodeValue());
        if (sName == USTR("type"))
        {
            mnType = valueOfSimpleAttribute(rxElem).toInt32();
            if (mnType == 2 || mnType == 3)
                rStyleAttrs[USTR("svg:stroke-width")] = USTR("0.18cm");
            else
                rStyleAttrs[USTR("svg:stroke-width")] = USTR("0.09cm");
        }
        else
            DiaObject::handleObjectAttribute(rxElem, rImporter, rAttrs, rStyleAttrs);
    }
}

KaosGoalObject::KaosGoalObject() : mnType(0)
{
    maConnectionPoints.push_back(ConnectionPoint(0, -5, DIR_NORTH));

    maConnectionPoints.push_back(ConnectionPoint(1.25, -3.75, DIR_NORTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(2.5, -2.5, DIR_NORTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(3.75, -1.25, DIR_NORTHEAST));

    maConnectionPoints.push_back(ConnectionPoint(5, 0, DIR_EAST));

    maConnectionPoints.push_back(ConnectionPoint(3.25, 1.25, DIR_SOUTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(2.5, 2.5, DIR_SOUTHEAST));
    maConnectionPoints.push_back(ConnectionPoint(1.25, 3.75, DIR_SOUTHEAST));

    maConnectionPoints.push_back(ConnectionPoint(0, 5, DIR_SOUTH));

    maConnectionPoints.push_back(ConnectionPoint(-1.25, 3.75, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-2.5, 2.5, DIR_SOUTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-3.75, 1.25, DIR_SOUTHWEST));

    maConnectionPoints.push_back(ConnectionPoint(-5, 0, DIR_WEST));

    maConnectionPoints.push_back(ConnectionPoint(-3.75, -1.25, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-2.5, -2.5, DIR_NORTHWEST));
    maConnectionPoints.push_back(ConnectionPoint(-1.25, -3.75, DIR_NORTHWEST));

    maConnectionPoints.push_back(ConnectionPoint(0, 0, DIR_ALL));
}

PropertyMap KaosGoalObject::import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter)
{
    PropertyMap aProps = DiaObject::import(rxElem, rImporter);

    switch (mnType)
    {
        case 0:
            {
            rtl::OUString sPath = USTR("M 514.625 73 C 514.625,18.6 527.875,32.2 527.875,86.6 C 527.875,37.3 541.125,16.9 541.125,66.2 C 541.125,16.9 561,37.3 554.375,86.6 C 563.208,86.6 563.208,141 554.375,141 C 561,185.2 537.812,185.862 538.475,141.662 C 538.475,185.862 525.225,186.525 525.225,142.325 C 525.225,191.625 513.3,187.65 513.3,138.35 C 505.019,138.35 506.344,73 514.625,73Z");
            createViewportAndPathFromPath(sPath ,aProps);
            }
            break;
        case 3:
            {
            rtl::OUString sPath = USTR("m59.9 0h908.1l-59.9 680.1h-908.1zm50.0-530.1 200.0-150.0z");
            createViewportAndPathFromPath(sPath ,aProps);
            }
            break;
        default:
            {
            basegfx::B2DPolygon aPoly = basegfx::tools::createPolygonFromRect(basegfx::B2DRectangle(mnX, mnY, mnX+mnWidth, mnY+mnHeight));
            basegfx::B2DRange aOldSize = aPoly.getB2DRange();

            basegfx::B2DHomMatrix aMatrix;
            int nShearAngle = mnType == 4 ? -85 : 85;
            aMatrix.shearX(-tan(M_PI/2.0 - M_PI/180.0 * nShearAngle));
            aPoly.transform(aMatrix);

            basegfx::B2DRange aNewSize = aPoly.getB2DRange();
            aMatrix = basegfx::B2DHomMatrix();
            aMatrix.scale(aOldSize.getWidth()/aNewSize.getWidth(), 1);
            aPoly.transform(aMatrix);

            aProps[USTR("draw:points")] = makePointsString(aPoly);
            createViewportFromRect(aProps);
            }
            break;
    }

    return aProps;
}


void DiaImporter::handleObject(const uno::Reference<xml::dom::XElement> &rxElem, shapes &rShapes)
{
    const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = rxElem->getAttributes();
    if (!xAttributes.is())
    {
        fprintf(stderr, "object without attributes!\n");
        return;
    }

    uno::Reference<xml::dom::XNode> xNode = xAttributes->getNamedItem(USTR("type"));
    if (!xNode.is())
    {
        fprintf(stderr, "object without type node!\n");
        return;
    }

    rtl::OUString sType = xNode->getNodeValue();
    if (!sType.getLength())
    {
        fprintf(stderr, "object without type!\n");
        return;
    }

    diaobject diaobj;
    if (sType == USTR("Standard - Box"))
        diaobj.reset(new StandardBoxObject());
    else if (sType == USTR("Standard - Ellipse"))
        diaobj.reset(new StandardEllipseObject());
    else if (sType == USTR("Standard - Polygon"))
        diaobj.reset(new StandardPolygonObject());
    else if (sType == USTR("Standard - Line"))
        diaobj.reset(new StandardLineObject());
    else if (sType == USTR("Standard - Arc"))
        diaobj.reset(new StandardArcObject());
    else if (sType == USTR("Standard - ZigZagLine"))
        diaobj.reset(new ZigZagLineObject());
    else if (sType == USTR("Standard - PolyLine"))
        diaobj.reset(new StandardPolyLineObject());
    else if (sType == USTR("Standard - BezierLine"))
        diaobj.reset(new StandardBezierLineObject());
    else if (sType == USTR("Standard - Beziergon"))
        diaobj.reset(new StandardBeziergonObject());
    else if (sType == USTR("Standard - Image"))
        diaobj.reset(new StandardImageObject());
    else if (sType == USTR("Standard - Text"))
        diaobj.reset(new StandardTextObject());
    else if (sType == USTR("Flowchart - Box"))
        diaobj.reset(new FlowchartBoxObject());
    else if (sType == USTR("Flowchart - Parallelogram"))
        diaobj.reset(new FlowchartParallelogramObject());
    else if (sType == USTR("Flowchart - Diamond"))
        diaobj.reset(new FlowchartDiamondObject());
    else if (sType == USTR("Flowchart - Ellipse"))
        diaobj.reset(new StandardEllipseObject());
    else if (sType == USTR("KAOS - goal"))
        diaobj.reset(new KaosGoalObject());
    else
    {
        shapeimporter aTemplate = findCustomImporter(sType);
        if (aTemplate.get())
            diaobj.reset(new CustomObject(aTemplate));
        else
        {
            fprintf(stderr, "warning: unknown dia shape \"%s\", substituting with a box\n",
                rtl::OUStringToOString(sType, RTL_TEXTENCODING_UTF8).getStr());
            diaobj.reset(new StandardBoxObject());
        }
    }

    PropertyMap aProps = diaobj->import(rxElem, *this);
    rShapes.push_back(shape(diaobj, aProps));
    mapId[aProps[USTR("draw:id")]] = diaobj;
}

//DIA will resize shapes that are too narrow to contain their text,
//so we have to do it too
void DiaImporter::resizeNarrowShapes()
{
    shapes::iterator aEnd = maShapes.end();
    for (shapes::iterator aI = maShapes.begin(); aI != aEnd; ++aI)
        aI->first->resizeIfNarrow(aI->second, *this);
}

//If we resized shapes, or if the dia file itself is "messy"
//then we need to adjust the connection points on connectors
void DiaImporter::adjustConnectionPoints()
{
    shapes::iterator aEnd = maShapes.end();
    for (shapes::iterator aI = maShapes.begin(); aI != aEnd; ++aI)
        aI->first->adjustConnectionPoints(aI->second, *this);
}

void DiaImporter::writeShapes()
{
    shapes::const_iterator aEnd = maShapes.end();
    for (shapes::const_iterator aI = maShapes.begin(); aI != aEnd; ++aI)
        aI->first->write(mxDocHandler, aI->second, *this);
}

//Somewhat against my better judgement, but lets expand the page in units of
//the "real" page size in order to fit everything in
void DiaImporter::adjustPageSize(PropertyMap &rPageProps)
{
    float fPageWidth = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(rPageProps[USTR("fo:page-width")], "mm", "").toFloat();
    float fPageHeight = ::comphelper::string::searchAndReplaceAllAsciiWithAscii(rPageProps[USTR("fo:page-height")], "mm", "").toFloat();

    basegfx::B2DPolyPolygon aScene;

    shapes::const_iterator aEnd = maShapes.end();
    for (shapes::const_iterator aI = maShapes.begin(); aI != aEnd; ++aI)
        aScene.append(basegfx::tools::createPolygonFromRect(aI->first->getBoundingBox()));

    basegfx::B2DRange aSceneRange = aScene.getB2DRange();

    double fMaxY = aSceneRange.getMaxY()*10;
    if (fPageHeight < fMaxY)
    {
        float fMul = ceilf(fMaxY / fPageHeight);
        rPageProps[USTR("fo:page-height")] = rtl::OUString::number(fPageHeight*fMul)+USTR("mm");
    }

    double fMaxX = aSceneRange.getMaxX()*10;
    if (fPageWidth < fMaxX)
    {
        float fMul = ceilf(fMaxX / fPageWidth);
        rPageProps[USTR("fo:page-width")] = rtl::OUString::number(fPageWidth*fMul)+USTR("mm");
    }
}

void DiaImporter::handleLayer(const uno::Reference<xml::dom::XElement> &rxElem)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("object"))
                handleObject(xElem, maShapes);
            else if (xElem->getTagName() == USTR("group"))
                handleGroup(xElem, maShapes);
            else
                reportUnknownElement(xElem);
        }
    }
}

class GroupObject : public DiaObject
{
private:
    shapes maShapes;
public:
    GroupObject() { /*fprintf(stderr, "custom object created\n");*/ }
    virtual rtl::OUString outputtype() const { return USTR("draw:g"); }
    virtual void write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const;
    virtual PropertyMap import(const uno::Reference<xml::dom::XElement> &rxElem, DiaImporter &rImporter);
    virtual void resizeIfNarrow(PropertyMap &rProps, const DiaImporter &rImporter);
    virtual void adjustConnectionPoints(PropertyMap &rProps, const DiaImporter &rImporter);
    shapes &getShapes() { return maShapes; }
};

void GroupObject::resizeIfNarrow(PropertyMap &rProps, const DiaImporter &rImporter)
{
    shapes::iterator aShapeEnd = maShapes.end();
    for (shapes::iterator aI = maShapes.begin(); aI != aShapeEnd; ++aI)
        aI->first->resizeIfNarrow(aI->second, rImporter);
}

void GroupObject::adjustConnectionPoints(PropertyMap &rProps, const DiaImporter &rImporter)
{
    shapes::iterator aShapeEnd = maShapes.end();
    for (shapes::iterator aI = maShapes.begin(); aI != aShapeEnd; ++aI)
        aI->first->adjustConnectionPoints(aI->second, rImporter);
}

void GroupObject::write(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, const PropertyMap &rProps, const DiaImporter &rImporter) const
{
#ifdef DEBUG
    PropertyMap::const_iterator aEnd = rProps.end();

    for (PropertyMap::const_iterator aI = rProps.begin(); aI != aEnd; ++aI)
    {
        fprintf(stderr, "groupobject prop %s value %s\n",
            rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
            rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
        );
    }
#endif

    rDocHandler->startElement(outputtype(), new SaxAttrList(PropertyMap()));

    shapes::const_iterator aShapeEnd = maShapes.end();
    for (shapes::const_iterator aI = maShapes.begin(); aI != aShapeEnd; ++aI)
        aI->first->write(rDocHandler, aI->second, rImporter);

    rDocHandler->endElement(outputtype());
}

PropertyMap GroupObject::import(const uno::Reference<xml::dom::XElement> &, DiaImporter &)
{
    return PropertyMap();
}

void DiaImporter::handleGroup(const uno::Reference<xml::dom::XElement> &rxElem, shapes &rShapes)
{
    diaobject diaobj;
    GroupObject *groupobj = new GroupObject();
    diaobj.reset(groupobj);

    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("object"))
                handleObject(xElem, groupobj->getShapes());
            else if (xElem->getTagName() == USTR("group"))
                handleGroup(xElem, groupobj->getShapes());
            else
                reportUnknownElement(xElem);
        }
    }

    PropertyMap aProps = diaobj->import(rxElem, *this);
    rShapes.push_back(shape(diaobj, aProps));
    mapId[aProps[USTR("draw:id")]] = diaobj;
}

void DiaImporter::writeResults()
{
    mxDocHandler->startDocument();

    PropertyMap aAttrs;

    aAttrs[USTR("xmlns:office")] = USTR(OASIS_STR "office:1.0");
    aAttrs[USTR("xmlns:style")] = USTR(OASIS_STR "style:1.0");
    aAttrs[USTR("xmlns:text")] = USTR(OASIS_STR "text:1.0");
    aAttrs[USTR("xmlns:svg")] = USTR(OASIS_STR "svg-compatible:1.0");
    aAttrs[USTR("xmlns:table")] = USTR(OASIS_STR "table:1.0");
    aAttrs[USTR("xmlns:draw")] = USTR(OASIS_STR "drawing:1.0");
    aAttrs[USTR("xmlns:fo")] = USTR(OASIS_STR "xsl-fo-compatible:1.0");
    aAttrs[USTR("xmlns:xlink")] = USTR("http://www.w3.org/1999/xlink");
    aAttrs[USTR("xmlns:dc")] = USTR("http://purl.org/dc/elements/1.1/");
    aAttrs[USTR("xmlns:number")] = USTR(OASIS_STR "datastyle:1.0");
    aAttrs[USTR("xmlns:presentation")] = USTR(OASIS_STR "presentation:1.0");
    aAttrs[USTR("xmlns:math")] = USTR("http://www.w3.org/1998/Math/MathML");
    aAttrs[USTR("xmlns:form")] = USTR(OASIS_STR "form:1.0");
    aAttrs[USTR("xmlns:script")] = USTR(OASIS_STR "script:1.0");
    aAttrs[USTR("xmlns:dom")] = USTR("http://www.w3.org/2001/xml-events");
    aAttrs[USTR("xmlns:xforms")] = USTR("http://www.w3.org/2002/xforms");
    aAttrs[USTR("xmlns:xsd")] = USTR("http://www.w3.org/2001/XMLSchema");
    aAttrs[USTR("xmlns:xsi")] = USTR("http://www.w3.org/2001/XMLSchema-instance");
    aAttrs[USTR("office:version")] = USTR("1.0");
    aAttrs[USTR("office:mimetype")] = USTR("application/vnd.oasis.opendocument.graphics");

    mxDocHandler->startElement(USTR("office:document"), makeXAttributeAndClear(aAttrs));
    mxDocHandler->startElement(USTR("office:styles"), uno::Reference<xml::sax::XAttributeList>());

    {
        autostyles::const_iterator aEnd = maArrows.end();
        for (autostyles::const_iterator aI = maArrows.begin(); aI != aEnd; ++aI)
        {
            aAttrs = aI->second;
            aAttrs[USTR("draw:name")] = aI->first;
            aAttrs[USTR("draw:display-name")] = 
                ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->first, "_20_", " ");
            mxDocHandler->startElement(USTR("draw:marker"), makeXAttributeAndClear(aAttrs));
            mxDocHandler->endElement(USTR("draw:marker"));
        }
    }

    {
        autostyles::const_iterator aEnd = maDashes.end();
        for (autostyles::const_iterator aI = maDashes.begin(); aI != aEnd; ++aI)
        {
            aAttrs = aI->second;
            aAttrs[USTR("draw:name")] = aI->first;
            aAttrs[USTR("draw:display-name")] = 
                ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->first, "_20_", " ");
            mxDocHandler->startElement(USTR("draw:stroke-dash"), makeXAttributeAndClear(aAttrs));
            mxDocHandler->endElement(USTR("draw:stroke-dash"));
        }
    }

    aAttrs[USTR("style:name")] = USTR("standard");
    aAttrs[USTR("style:family")] = USTR("graphic");
    mxDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
    aAttrs[USTR("svg:stroke-width")] = USTR("0.10cm");
    aAttrs[USTR("draw:fill-color")] = USTR("#ffffff");
    aAttrs[USTR("draw:start-line-spacing-horizontal")] = USTR("0cm");
    aAttrs[USTR("draw:start-line-spacing-vertical")] = USTR("0cm");
    aAttrs[USTR("draw:end-line-spacing-horizontal")] = USTR("0cm");
    aAttrs[USTR("draw:end-line-spacing-vertical")] = USTR("0cm");
    mxDocHandler->startElement(USTR("style:graphic-properties"), makeXAttributeAndClear(aAttrs));
    mxDocHandler->endElement(USTR("style:graphic-properties"));
    aAttrs[USTR("fo:language")] = USTR("zxx");
    aAttrs[USTR("fo:country")] = USTR("none");
    mxDocHandler->startElement(USTR("style:text-properties"), makeXAttributeAndClear(aAttrs));
    mxDocHandler->endElement(USTR("style:text-properties"));
    mxDocHandler->endElement(USTR("style:style"));
    mxDocHandler->endElement(USTR("office:styles"));

    mxDocHandler->startElement(USTR("office:automatic-styles"), uno::Reference<xml::sax::XAttributeList>());
    if (page_layout_properties.get())
    {
        aAttrs[USTR("style:name")] = USTR("pagelayout1");
        mxDocHandler->startElement(USTR("style:page-layout"), makeXAttributeAndClear(aAttrs));
        adjustPageSize(page_layout_properties->second);
        mxDocHandler->startElement(USTR("style:page-layout-properties"), new SaxAttrList(page_layout_properties->second));
        mxDocHandler->endElement(USTR("style:page-layout-properties"));
        mxDocHandler->endElement(USTR("style:page-layout"));
    }
    if (drawing_page_properties.get())
    {
        aAttrs[USTR( "style:name")] = USTR("pagestyle1");
        aAttrs[USTR( "style:family")] = USTR("drawing-page");
        mxDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
        mxDocHandler->startElement(USTR("style:drawing-page-properties"), new SaxAttrList(drawing_page_properties->second));
        mxDocHandler->endElement(USTR("style:drawing-page-properties"));
        mxDocHandler->endElement(USTR("style:style"));
    }

    maGraphicStyles.write(mxDocHandler);

    maTextStyles.write(mxDocHandler);

    mxDocHandler->endElement( USTR("office:automatic-styles") );

    mxDocHandler->startElement( USTR("office:master-styles"), uno::Reference<xml::sax::XAttributeList>());
    aAttrs[USTR( "style:name")] = USTR("Default");
    if (page_layout_properties.get())
        aAttrs[USTR( "style:page-layout-name")] = USTR("pagelayout1");
    if (drawing_page_properties.get())
        aAttrs[USTR( "draw:style-name")] = USTR("pagestyle1");
    mxDocHandler->startElement(USTR("style:master-page"), makeXAttributeAndClear(aAttrs));
    mxDocHandler->endElement(USTR("style:master-page"));
    mxDocHandler->endElement(USTR("office:master-styles"));

    mxDocHandler->startElement(USTR("office:body"), uno::Reference<xml::sax::XAttributeList>());
    mxDocHandler->startElement(USTR("office:drawing"), uno::Reference<xml::sax::XAttributeList>());

    aAttrs[USTR("draw:master-page-name")] = USTR("Default");
    if (drawing_page_properties.get())
        aAttrs[USTR("draw:style-name")] = USTR("pagestyle1");
    mxDocHandler->startElement(USTR("draw:page"), makeXAttributeAndClear(aAttrs));

    resizeNarrowShapes();

    adjustConnectionPoints();

    writeShapes();

    mxDocHandler->endElement(USTR("draw:page"));
    mxDocHandler->endElement(USTR("office:drawing"));
    mxDocHandler->endElement(USTR("office:body"));

    mxDocHandler->endElement(USTR("office:document"));
    mxDocHandler->endDocument();  
}

bool DiaImporter::handleDiagram(const uno::Reference<xml::dom::XElement> &rxElem)
{
    //Get Page Layout and Drawing Page styles
    {
        uno::Reference<xml::dom::XNodeList> xDiagramDataNodes(rxElem->getElementsByTagName( USTR("diagramdata") ));
        const sal_Int32 nNumNodes( xDiagramDataNodes->getLength() );
        for( sal_Int32 i=0; i<nNumNodes; ++i )
        {
            uno::Reference<xml::dom::XElement> xElem(xDiagramDataNodes->item(i), uno::UNO_QUERY_THROW);
            handleDiagramData(xElem);
        }
    }

    maDashes.push_back(autostyle(USTR("DIA_20_Dashed"), makeDash(1)));
    maDashes.push_back(autostyle(USTR("DIA_20_Dash_20_Dot"), makeDashDot(1)));
    maDashes.push_back(autostyle(USTR("DIA_20_Dash_20_Dot_20_Dot"), makeDashDotDot(1)));
    maDashes.push_back(autostyle(USTR("DIA_20_Dotted"), makeDot(1)));

    for (int i = 2; i < 34; ++i)
        maArrows.push_back(autostyle(GetArrowName(i), makeArrow(i)));

    //Collect shapes and their required Auto-Styles
    {
        uno::Reference<xml::dom::XNodeList> xDiagramDataNodes(rxElem->getElementsByTagName( USTR("layer") ));
        const sal_Int32 nNumNodes( xDiagramDataNodes->getLength() );
        for( sal_Int32 i=0; i<nNumNodes; ++i )
        {
            uno::Reference<xml::dom::XElement> xElem(xDiagramDataNodes->item(i), uno::UNO_QUERY_THROW);
            handleLayer(xElem);
        }
    }

    writeResults();

    //Just check if there's anything we don't know about
    uno::Reference<xml::dom::XNodeList> xChildren( rxElem->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            if (xElem->getTagName() == USTR("diagramdata")) //Handled explicitly first above
                /*IgnoreThis*/;
            else if (xElem->getTagName() == USTR("layer")) //Handled explicitly first above
                /*IgnoreThis*/;
            else
                reportUnknownElement(xElem);
        }
    }

    return true;
}

bool DiaImporter::convert()
{
    bool bOk = false;
    if (mxDocElem->getTagName() == USTR("diagram"))
        bOk = handleDiagram(mxDocElem);
    else
        reportUnknownElement(mxDocElem);
    return bOk;
}

::rtl::OUString DIAFilter::getInstallPath()
{
    if (msInstallDir.getLength() == 0)
    {
        // Determine the base path of the shapes
        uno::Reference<deployment::XPackageInformationProvider> xInformationProvider (
            mxCtx->getValueByName(
                USTR("/singletons/com.sun.star.deployment.PackageInformationProvider")),
            uno::UNO_QUERY);
        if (xInformationProvider.is())
        {
            try
            {
                msInstallDir = xInformationProvider->getPackageLocation(USTR("mcnamara.caolan.diafilter"))
                    + USTR("/");
            }
            catch(deployment::DeploymentException&)
            {
            }
        }
    }

    return msInstallDir;
}


sal_Bool SAL_CALL DIAFilter::filter( const uno::Sequence< beans::PropertyValue >& rDescriptor )
    throw (uno::RuntimeException)
{
    if (!mxDstDoc.is())
        return sal_False;

    uno::Reference< io::XInputStream > xInputStream;
    const sal_Int32 nLength = rDescriptor.getLength();
    const beans::PropertyValue* pAttribs = rDescriptor.getConstArray();
    for ( sal_Int32 i=0 ; i<nLength; ++i, ++pAttribs )
    {   
        if( pAttribs->Name.equalsAscii( "InputStream" ) )
            pAttribs->Value >>= xInputStream;
    }   
    if (!xInputStream.is())
        return sal_False;

    uno::Reference < xml::sax::XDocumentHandler > xDocHandler(
        mxMSF->createInstance( USTR("com.sun.star.comp.Draw.XMLOasisImporter") ), uno::UNO_QUERY_THROW );

    uno::Reference < XImporter > xImporter(xDocHandler, uno::UNO_QUERY_THROW);
    xImporter->setTargetDocument(mxDstDoc);

    uno::Reference<xml::dom::XDocumentBuilder> xDomBuilder(
        mxMSF->createInstance( USTR("com.sun.star.xml.dom.DocumentBuilder") ), uno::UNO_QUERY_THROW );

    sal_Int64 nPos = 0;
    uno::Reference< io::XSeekable > xSeekable( xInputStream, uno::UNO_QUERY );
    if (xSeekable.is())
       nPos = xSeekable->getPosition();
    try
    {
        uno::Reference< io::XInputStream > xTmpInputStream(new gz_InputStream(xInputStream));
        xInputStream = xTmpInputStream;
    }
    catch(...)
    {
        if (xSeekable.is())
            xSeekable->seek(nPos);
    }

    uno::Reference<xml::dom::XDocument> xDom( xDomBuilder->parse(xInputStream), uno::UNO_QUERY_THROW );

    uno::Reference<xml::dom::XElement> xDocElem( xDom->getDocumentElement(), uno::UNO_QUERY_THROW );

    DiaImporter aImporter(mxCtx, mxMSF, xDocHandler, xDocElem, getInstallPath());
    return aImporter.convert();
}

void SAL_CALL DIAFilter::setTargetDocument( const uno::Reference< lang::XComponent >& xDoc )
    throw (lang::IllegalArgumentException, uno::RuntimeException)
{
    mxDstDoc = xDoc;
}

rtl::OUString SAL_CALL DIAFilter::detect( uno::Sequence< beans::PropertyValue >& io_rDescriptor )
    throw (uno::RuntimeException)
{
    com::sun::star::uno::Reference< com::sun::star::io::XInputStream > xInputStream;

    const beans::PropertyValue *pValue = io_rDescriptor.getConstArray();
    sal_Int32 nLength = io_rDescriptor.getLength();
    for (sal_Int32 i = 0; i < nLength; i++)
    {
        if (pValue[i].Name.equalsAsciiL(RTL_CONSTASCII_STRINGPARAM("InputStream")))
            pValue[i].Value >>= xInputStream;
    }

    if (!xInputStream.is())
        return rtl::OUString();

    sal_Int64 nPos = 0;
    uno::Reference< io::XSeekable > xSeekable( xInputStream, uno::UNO_QUERY );

    try
    {
        if (xSeekable.is())
           nPos = xSeekable->getPosition();

        try
        {
            uno::Reference< io::XInputStream > xTmpInputStream(new gz_InputStream(xInputStream));
            xInputStream = xTmpInputStream;
        }
        catch(...)
        {
            if (xSeekable.is())
                xSeekable->seek(nPos);
        }

        rtl::OUString sRet;

        uno::Sequence<sal_Int8> aData(64);
        sal_Int32 nLen = xInputStream->readBytes(aData, 64);
        rtl::OString aTmp(reinterpret_cast<const sal_Char*>(aData.getArray()), nLen);
        if (aTmp.indexOf(rtl::OString(RTL_CONSTASCII_STRINGPARAM("<dia:diagram "))) != -1)
            sRet = rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("dia_DIA"));

        if (xSeekable.is())
            xSeekable->seek(nPos);

        return sRet;
    }
    catch (io::IOException const&)
    {
        return rtl::OUString();
    }
}

rtl::OUString SAL_CALL DIAFilter::getImplementationName()
    throw (uno::RuntimeException)
{
    return getImplementationName_static();
}

sal_Bool SAL_CALL DIAFilter::supportsService(const rtl::OUString &serviceName)
    throw (uno::RuntimeException) 
{
    uno::Sequence<rtl::OUString> serviceNames = getSupportedServiceNames();
    for (sal_Int32 i = 0; i < serviceNames.getLength(); ++i)
    {
        if (serviceNames[i] == serviceName)
            return sal_True;
    }
    return sal_False;
}

uno::Sequence<rtl::OUString> SAL_CALL DIAFilter::getSupportedServiceNames()
    throw (uno::RuntimeException)
{
    return getSupportedServiceNames_static();
}

rtl::OUString DIAFilter::getImplementationName_static() 
{
    return rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("mcnamara.caolan.comp.Draw.DIAFilter"));
}

uno::Sequence<rtl::OUString> DIAFilter::getSupportedServiceNames_static()
{
    uno::Sequence<rtl::OUString> snames(2);
    snames.getArray()[0] = rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("com.sun.star.document.ExtendedTypeDetection"));
    snames.getArray()[1] = rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("com.sun.star.document.ImportFilter"));
    return snames;
}

uno::Reference<uno::XInterface> DIAFilter::get(uno::Reference<uno::XComponentContext> const & context)
{
    return static_cast< ::cppu::OWeakObject * >(new DIAFilter(context));
}

/* vi:set tabstop=4 shiftwidth=4 expandtab: */
