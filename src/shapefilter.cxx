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

#include <com/sun/star/xml/sax/XDocumentHandler.hpp>
#include <com/sun/star/xml/dom/XDocumentBuilder.hpp>
#include <com/sun/star/io/XSeekable.hpp>
#include <comphelper/string.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>

#include "filters.hxx"
#include "shapefilter.hxx"

#include <vector>
#include <algorithm>
#include <functional>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include <math.h>
#include <stdio.h>

#define DEFAULTSIZE 2.0

class ShapeObject
{
protected:
    PropertyMap maAttrs;
    basegfx::B2DPolyPolygon &mrScene;
    rtl::OUString msStroke;
    rtl::OUString msFill;
    float mnStrokeScale;
    virtual bool importAttribute(const uno::Reference<xml::dom::XNode> &rxNode);
    virtual void setPosAndSize(PropertyMap &rAttrs, float x, float y, float hscale, float vscale) const;
    virtual rtl::OUString getTagName() const = 0;
    virtual basegfx::B2DRange getB2DRange() const = 0;
    virtual void addToScene() const = 0;
public:
    ShapeObject(basegfx::B2DPolyPolygon &rScene) : mrScene(rScene), msFill(USTR("none")), mnStrokeScale(1.0) {}
    void import(const uno::Reference<xml::dom::XNamedNodeMap> xAttributes);
    void generateStyle(GraphicStyleManager &rStyleManager, const PropertyMap &rParentProps, PropertyMap &rShapeOverrides, bool bShowBackground) const;
    void write(uno::Reference < xml::sax::XDocumentHandler > &rxDocHandler, const PropertyMap &rParentProps, const PropertyMap &rShapeOverride, float x, float y, float hscale, float vscale) const;
    virtual ~ShapeObject() {}
};

bool ShapeObject::importAttribute(const uno::Reference<xml::dom::XNode> &rxNode)
{
    rtl::OUString sAttribute = rxNode->getNodeName();
    if (sAttribute == USTR("points"))
        maAttrs[USTR("draw:points")] = rxNode->getNodeValue().trim();
    else if (sAttribute == USTR("d"))
        maAttrs[USTR("svg:d")] = rxNode->getNodeValue();
    else if (sAttribute == USTR("stroke-dasharray"))
        /*Ignore, gnome#625381*/;
    else if (sAttribute == USTR("style"))
    {
        rtl::OUString sStyle = rxNode->getNodeValue();
        sal_Int32 nIndex = 0;
        do
        {
            rtl::OUString sAttrib = sStyle.getToken(0, ';', nIndex);
            sal_Int32 nSubIndex = 0;
            rtl::OUString sName = sAttrib.getToken(0, ':', nSubIndex).trim();
            rtl::OUString sValue = sAttrib.getToken(0, ':', nSubIndex).trim();
            //gnome#625380  mobile_phone has bad svg of fill: #bfbfbf stroke-width...
            nSubIndex = 0;
            sValue = sValue.getToken(0, ' ', nSubIndex).trim();
            if (sName == USTR("stroke"))
                msStroke=sValue;
            else if (sName == USTR("fill"))
                msFill=sValue;
            else if (sName == USTR("stroke-width"))
                mnStrokeScale = sValue.toFloat();
            else if (sName == USTR("fill-rule") && sValue == USTR("evenodd"))
                /*ToDo*/;
            else if (sName == USTR("stroke-miterlimit"))
                /*ToDo*/;
            else if (sName == USTR("stroke-linecap"))
                /*ToDo*/;
            else if (sName == USTR("stroke-linejoin"))
                /*ToDo*/;
            else if (sName == USTR("stroke-width"))
                /*ToDo*/;
            else if (sName == USTR("fill-opacity"))
                /*ToDo*/;
            else if (sName == USTR("stroke-pattern"))
                /*ToDo*/;
            else if (sName == USTR("stroke-width"))
                /*ToDo*/;
            else if (sName == USTR("stroke-dasharray"))
                /*ToDo*/;
            else if (sName == USTR("stroke-dashlength"))
                /*ToDo*/;
            else if (sName == USTR("stroke-width 0.01"))
                /*Ignore, gnome#625377*/;
            else if (sName.getLength())
            {
                fprintf(stderr, "unknown attribute pair is %s %s\n", 
                    rtl::OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr(),
                    rtl::OUStringToOString(sValue, RTL_TEXTENCODING_UTF8).getStr());
            }
        }
        while ( nIndex >= 0 );
    }
    else
        return false;
    return true;
}

void ShapeObject::generateStyle(GraphicStyleManager &rStyleManager, const PropertyMap &rParentProps, PropertyMap &rShapeOverrides, bool bShowBackground) const
{
#if 0
    fprintf(stderr, "ShapeObject::generateStyle, width of %f\n", mnStrokeScale);
    {
        PropertyMap::const_iterator aEnd = rParentProps.end();
        for (PropertyMap::const_iterator aI = rParentProps.begin(); aI != aEnd; ++aI)
        {
            fprintf(stderr, "orig style %s %s\n",
                rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
                rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
            );
        }
    }

#endif

    PropertyMap aStyleAttrs(rParentProps);
    //Show no background at all
    if (!bShowBackground)
        aStyleAttrs[USTR("draw:fill")] = USTR("none");
    else if (msFill.getLength() && msFill.compareToAscii("background") != 0 && msFill.compareToAscii("bg") != 0 && msFill.compareToAscii("default") != 0)
    {
        //if the shape's background isn't a placeholder "background" or "bg",
        //then force in the exact colours needed
        if (msFill.compareToAscii("none") == 0)
            aStyleAttrs[USTR("draw:fill")] = msFill;
        else if ((msFill.compareToAscii("foreground") == 0) || (msFill.compareToAscii("fg") == 0))
            aStyleAttrs[USTR("draw:fill-color")] = aStyleAttrs[USTR("svg:stroke-color")];
        else
            aStyleAttrs[USTR("draw:fill-color")] = msFill;
    }
    if (msStroke.getLength() && msStroke.compareToAscii("foreground") != 0 && msStroke.compareToAscii("fg") != 0 && msStroke.compareToAscii("default"))
    {
        if (msStroke.compareToAscii("none") == 0)
            aStyleAttrs[USTR("draw:stroke")] = msStroke;
        else if ((msStroke.compareToAscii("background") == 0) || (msStroke.compareToAscii("bg") == 0))
            aStyleAttrs[USTR("svg:stroke-color")] = aStyleAttrs[USTR("draw:fill-color")];
        else
            aStyleAttrs[USTR("svg:stroke-color")] = msStroke;
    }
    if (mnStrokeScale != 1.0)
    {
        PropertyMap::const_iterator aI = rParentProps.find(USTR("svg:stroke-width"));
        float width = aI != rParentProps.end() ? ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat() : 0.10;
        aStyleAttrs[USTR("svg:stroke-width")] = rtl::OUString::number(width*mnStrokeScale) + USTR("cm");
    }

#if 0
    {
        PropertyMap::const_iterator aEnd = aStyleAttrs.end();
        for (PropertyMap::const_iterator aI = aStyleAttrs.begin(); aI != aEnd; ++aI)
        {
            fprintf(stderr, "new style %s %s\n",
                rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
                rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
            );
        }
    }
#endif
    rStyleManager.addAutomaticGraphicStyle(rShapeOverrides, aStyleAttrs);
}

class ShapePolygon : public ShapeObject
{
private:
    bool mbClosed;
    basegfx::B2DPolygon maPoly;   
    void createViewportAndPolygonFromPoints(const rtl::OUString &rPoints);
public:
    ShapePolygon(basegfx::B2DPolyPolygon &rScene, bool bClosed=true) : ShapeObject(rScene), mbClosed(bClosed) {}
    virtual bool importAttribute(const uno::Reference<xml::dom::XNode> &rxNode);
    rtl::OUString getTagName() const { return USTR("draw:path"); }
    virtual void addToScene() const;
    virtual basegfx::B2DRange getB2DRange() const { return maPoly.getB2DRange(); }
};

void ShapePolygon::addToScene() const
{
    mrScene.append(maPoly);
}

namespace
{
    //*10 to make it accurate enough for OOo, and
    //protect against 0 width/height, which it 
    //doesn't like either
    float safeViewPortDimension(float nDim)
    {
        nDim*=10;
        return nDim < 1.0 ? 1.0 : nDim;
    }

    //protect against 0 width/height, which OOo
    //doesn't like
    float safeDimension(float nDim)
    {
        return nDim == 0.0 ? 0.001 : nDim;
    }
}

void createViewportFromPoints(const rtl::OUString &rPoints, PropertyMap &rAttrs,
    float fAdjustX, float fAdjustY)
{
    basegfx::B2DPolygon aPoly;

    bool bSuccess = basegfx::tools::importFromSvgPoints( aPoly, rPoints );
    if (!bSuccess)
    {
        fprintf(stderr, "Import from %s failed\n",
            rtl::OUStringToOString(rPoints, RTL_TEXTENCODING_UTF8).getStr());
    }

    basegfx::B2DRange aRange = aPoly.getB2DRange();

    float x = aRange.getMinX();
    float y = aRange.getMinY();
    float width = aRange.getWidth();
    float height = aRange.getHeight();

    rAttrs[USTR("svg:x")] = rtl::OUString::number(x+fAdjustX) + USTR("cm");
    rAttrs[USTR("svg:y")] = rtl::OUString::number(y+fAdjustY) + USTR("cm");
    rAttrs[USTR("svg:width")] = rtl::OUString::number(safeDimension(width)) + USTR("cm");
    rAttrs[USTR("svg:height")] = rtl::OUString::number(safeDimension(height)) + USTR("cm");

    rAttrs[USTR("svg:viewBox")] =
        rtl::OUString::number(x) + USTR(" ") +
        rtl::OUString::number(y) + USTR(" ") +
        rtl::OUString::number(safeViewPortDimension(width)) + USTR(" ") +
        rtl::OUString::number(safeViewPortDimension(height));
}

void createViewportAndPolygonFromPoints(const rtl::OUString &rPoints, PropertyMap &rAttrs, basegfx::B2DPolygon &rPoly, bool bClose)
{
    bool bSuccess = basegfx::tools::importFromSvgPoints( rPoly, rPoints );
    rPoly.setClosed(bClose);
    if (!bSuccess)
    {
        fprintf(stderr, "Import from %s failed\n",
            rtl::OUStringToOString(rPoints, RTL_TEXTENCODING_UTF8).getStr());
    }

    basegfx::B2DRange aRange = rPoly.getB2DRange();
    basegfx::B2DPolyPolygon aPolyPoly(rPoly);

    basegfx::B2DHomMatrix aMatrix;
    aMatrix.translate( -aRange.getMinX(), -aRange.getMinY() );
    aMatrix.scale( 10, 10 );
    aPolyPoly.transform( aMatrix );

    rAttrs[USTR("svg:viewBox")] = USTR("0 0 ") +
        rtl::OUString::number(safeViewPortDimension(aRange.getWidth())) + USTR(" ") +
        rtl::OUString::number(safeViewPortDimension(aRange.getHeight()));
    rtl::OUString sNewString = basegfx::tools::exportToSvgD( aPolyPoly );
    rAttrs[USTR("svg:d")] = sNewString;
}

void createViewportAndPolygonFromPoints(const rtl::OUString &rPoints, PropertyMap &rAttrs, bool bClose)
{
    basegfx::B2DPolygon aScratch;
    createViewportAndPolygonFromPoints(rPoints, rAttrs, aScratch, bClose);
}

void ShapePolygon::createViewportAndPolygonFromPoints(const rtl::OUString &rPoints)
{
    ::createViewportAndPolygonFromPoints(rPoints, maAttrs, maPoly, mbClosed);
}

bool ShapePolygon::importAttribute(const uno::Reference<xml::dom::XNode> &rxNode)
{
    rtl::OUString sAttribute = rxNode->getNodeName();
    if (sAttribute == USTR("points"))
    {
        createViewportAndPolygonFromPoints(rxNode->getNodeValue().trim());
        return true;
    }
    return ShapeObject::importAttribute(rxNode);
}

void ShapeObject::setPosAndSize(PropertyMap &rAttrs, float x, float y, float hscale, float vscale) const
{
    basegfx::B2DRange aRange = getB2DRange();
    basegfx::B2DRange aSceneRange = mrScene.getB2DRange();

    float relx = aRange.getMinX() - aSceneRange.getMinX();
    float rely = aRange.getMinY() - aSceneRange.getMinY();

    rAttrs[USTR("svg:x")] = rtl::OUString::number(x+relx*hscale) + USTR("cm");
    rAttrs[USTR("svg:y")] = rtl::OUString::number(y+rely*vscale) + USTR("cm");
    rAttrs[USTR("svg:width")] = rtl::OUString::number(safeDimension(aRange.getWidth()*hscale)) + USTR("cm");
    rAttrs[USTR("svg:height")] = rtl::OUString::number(safeDimension(aRange.getHeight()*vscale)) + USTR("cm");
}

void ShapeObject::write(uno::Reference < xml::sax::XDocumentHandler > &rxDocHandler, const PropertyMap &rParentProps, const PropertyMap &rShapeOverrides, float x, float y, float hscale, float vscale) const
{
    PropertyMap aProps;

    //Make a copy of the outside properties as defaults
    {
        PropertyMap::const_iterator aEnd = rParentProps.end();
        for (PropertyMap::const_iterator aI = rParentProps.begin(); aI != aEnd; ++aI)
            aProps[aI->first] = aI->second;
    }
    //Overwrite with our properties
    {
        PropertyMap::const_iterator aEnd = maAttrs.end();
        for (PropertyMap::const_iterator aI = maAttrs.begin(); aI != aEnd; ++aI)
            aProps[aI->first] = aI->second;
    }
    //Overwrite with custom backgrounds/foregrounds
    {
        PropertyMap::const_iterator aEnd = rShapeOverrides.end();
        for (PropertyMap::const_iterator aI = rShapeOverrides.begin(); aI != aEnd; ++aI)
            aProps[aI->first] = aI->second;
    }
    //Set size and position
    setPosAndSize(aProps, x, y, hscale, vscale);

#ifdef DEBUG
    PropertyMap::iterator aEnd = aProps.end();
    for (PropertyMap::iterator aI = aProps.begin(); aI != aEnd; ++aI)
    {
        fprintf(stderr, "prop, value is %s %s\n",
            rtl::OUStringToOString(aI->first, RTL_TEXTENCODING_UTF8).getStr(),
            rtl::OUStringToOString(aI->second, RTL_TEXTENCODING_UTF8).getStr()
        );
    }
#endif

    rxDocHandler->startElement(getTagName(), makeXAttribute(aProps));
    rxDocHandler->endElement(getTagName());
}

class ShapePath : public ShapeObject
{
private:
    basegfx::B2DPolyPolygon maPolyPoly;
    void createViewportAndPathFromPath(const rtl::OUString &rPath);
public:
    ShapePath(basegfx::B2DPolyPolygon &rScene) : ShapeObject(rScene) {}
    virtual bool importAttribute(const uno::Reference<xml::dom::XNode> &rxNode);
    rtl::OUString getTagName() const { return USTR("draw:path"); }
    virtual void addToScene() const;
    virtual basegfx::B2DRange getB2DRange() const { return maPolyPoly.getB2DRange(); }
};

bool ShapePath::importAttribute(const uno::Reference<xml::dom::XNode> &rxNode)
{
    rtl::OUString sAttribute = rxNode->getNodeName();
    if (sAttribute == USTR("d"))
    {
        createViewportAndPathFromPath(rxNode->getNodeValue().trim());
        return true;
    }
    return ShapeObject::importAttribute(rxNode);
}

void ShapePath::addToScene() const
{
    mrScene.append(maPolyPoly);
}

void createViewportAndPathFromPath(const rtl::OUString &rPath, PropertyMap &rAttrs, basegfx::B2DPolyPolygon &rPolyPoly)
{
    bool bSuccess = basegfx::tools::importFromSvgD( rPolyPoly, rPath );
    if (!bSuccess)
    {
        fprintf(stderr, "Import from %s failed\n",
            rtl::OUStringToOString(rPath, RTL_TEXTENCODING_UTF8).getStr());
    }

    basegfx::B2DPolyPolygon aPolyPoly(rPolyPoly);

    basegfx::B2DRange aRange = aPolyPoly.getB2DRange();

    basegfx::B2DHomMatrix aMatrix;
    aMatrix.translate( -aRange.getMinX(), -aRange.getMinY() );
    aMatrix.scale( 10, 10 );
    aPolyPoly.transform( aMatrix );

    rAttrs[USTR("svg:viewBox")] = USTR("0 0 ") +
        rtl::OUString::number(safeViewPortDimension(aRange.getWidth())) + USTR(" ") +
        rtl::OUString::number(safeViewPortDimension(aRange.getHeight()));
    rtl::OUString sNewString = basegfx::tools::exportToSvgD( aPolyPoly );
    rAttrs[USTR("svg:d")] = sNewString;
}

void createViewportAndPathFromPath(const rtl::OUString &rPath, PropertyMap &rAttrs)
{
    basegfx::B2DPolyPolygon aScratch;
    createViewportAndPathFromPath(rPath, rAttrs, aScratch);
}

void ShapePath::createViewportAndPathFromPath(const rtl::OUString &rPath)
{
    ::createViewportAndPathFromPath(rPath, maAttrs, maPolyPoly);
}

class ShapeEllipse : public ShapeObject
{
private:
    float cx, cy, rx, ry;
public:
    ShapeEllipse(basegfx::B2DPolyPolygon &rScene) : ShapeObject(rScene), cx(1), cy(1), rx(1), ry(1) {}
    virtual bool importAttribute(const uno::Reference<xml::dom::XNode> &rxNode);
    rtl::OUString getTagName() const { return USTR("draw:ellipse"); }
    virtual void addToScene() const;
    virtual basegfx::B2DRange getB2DRange() const { return basegfx::B2DRange(cx-rx, cy-ry, cx-rx+rx*2, cy-ry+ry*2); }
};

void ShapeEllipse::addToScene() const
{
    mrScene.append(basegfx::tools::createPolygonFromEllipse( basegfx::B2DPoint(cx, cy), rx, ry));
}

bool ShapeEllipse::importAttribute(const uno::Reference<xml::dom::XNode> &rxNode)
{
    rtl::OUString sAttribute = rxNode->getNodeName();
    if (sAttribute == USTR("cx"))
        cx = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("cy"))
        cy = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("rx"))
        rx = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("ry"))
        ry = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("r"))
        rx = ry = rxNode->getNodeValue().toFloat();
    else
        return ShapeObject::importAttribute(rxNode);
    return true;
}

class ShapeRect : public ShapeObject
{
private:
    float x, y, width, height;
public:
    ShapeRect(basegfx::B2DPolyPolygon &rScene) : ShapeObject(rScene), x(0), y(0), width(0), height(0) {}
    virtual bool importAttribute(const uno::Reference<xml::dom::XNode> &rxNode);
    rtl::OUString getTagName() const { return USTR("draw:rect"); }
    virtual void addToScene() const;
    virtual basegfx::B2DRange getB2DRange() const { return basegfx::B2DRange(x,y,x+width,y+height); }
};

void ShapeRect::addToScene() const
{
    mrScene.append(basegfx::tools::createPolygonFromRect(basegfx::B2DRectangle(x, y, x+width, y+height)));
}

bool ShapeRect::importAttribute(const uno::Reference<xml::dom::XNode> &rxNode)
{
    rtl::OUString sAttribute = rxNode->getNodeName();
    if (sAttribute == USTR("x"))
        x = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("y"))
        y = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("width"))
        width = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("height"))
        height = rxNode->getNodeValue().toFloat();
    else
        return ShapeObject::importAttribute(rxNode);
    return true;
}

class ShapeLine : public ShapeObject
{
private:
    float x1, x2, y1, y2;
protected:
    virtual void setPosAndSize(PropertyMap &rAttrs, float x, float y, float hscale, float vscale) const;
public:
    ShapeLine(basegfx::B2DPolyPolygon &rScene) : ShapeObject(rScene), x1(0), x2(0), y1(0), y2(0) {}
    virtual bool importAttribute(const uno::Reference<xml::dom::XNode> &rxNode);
    rtl::OUString getTagName() const { return USTR("draw:line"); }
    virtual void addToScene() const;
    virtual basegfx::B2DRange getB2DRange() const { return basegfx::B2DRange(x1,y1,x2,y2); }
};

void ShapeLine::addToScene() const
{
    mrScene.append(basegfx::tools::createPolygonFromRect(basegfx::B2DRectangle(x1,y1,x2,y2)));
}

bool ShapeLine::importAttribute(const uno::Reference<xml::dom::XNode> &rxNode)
{
    rtl::OUString sAttribute = rxNode->getNodeName();
    if (sAttribute == USTR("x1"))
        x1 = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("y1"))
        y1 = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("x2"))
        x2 = rxNode->getNodeValue().toFloat();
    else if (sAttribute == USTR("y2"))
        y2 = rxNode->getNodeValue().toFloat();
    else
        return ShapeObject::importAttribute(rxNode);
    return true;
}

void ShapeLine::setPosAndSize(PropertyMap &rAttrs, float x, float y, float hscale, float vscale) const
{
    basegfx::B2DRange aSceneRange = mrScene.getB2DRange();

    float relx;
    float rely;
    relx = x1 - aSceneRange.getMinX();
    rely = y1 - aSceneRange.getMinY();
    rAttrs[USTR("svg:x1")] = rtl::OUString::number(x+relx*hscale) + USTR("cm");
    rAttrs[USTR("svg:y1")] = rtl::OUString::number(y+rely*vscale) + USTR("cm");
    relx = x2 - aSceneRange.getMinX();
    rely = y2 - aSceneRange.getMinY();
    rAttrs[USTR("svg:x2")] = rtl::OUString::number(x+relx*hscale) + USTR("cm");
    rAttrs[USTR("svg:y2")] = rtl::OUString::number(y+rely*vscale) + USTR("cm");
}

void ShapeObject::import(const uno::Reference<xml::dom::XNamedNodeMap> xAttributes)
{
    sal_Int32 nAttribs = xAttributes->getLength();
    for (sal_Int32 j=0; j < nAttribs; ++j)
    {
        uno::Reference<xml::dom::XNode> xNode(xAttributes->item(j));
        rtl::OUString sAttribute = xNode->getNodeName();
        if (!importAttribute(xNode))
        {
            fprintf(stderr, "unknown attribute \"%s\" of value \"%s\"\n",
                rtl::OUStringToOString(sAttribute, RTL_TEXTENCODING_UTF8).getStr(),
                rtl::OUStringToOString(xNode->getNodeValue(), RTL_TEXTENCODING_UTF8).getStr());
        }
    }
    addToScene();
}

//The actual shape is described using a subset of the SVG specification.  The
//line, polyline, polygon, rect, circle, ellipse, path and g elements are
//supported
void ShapeImporter::importShapeSVG(const uno::Reference < xml::dom::XNode > &rxNode, const uno::Reference<xml::dom::XNamedNodeMap> &rxParentAttributes)
{
    uno::Reference<xml::dom::XNodeList> xChildren( rxNode->getChildNodes() );
    const sal_Int32 nNumNodes( xChildren->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        if( xChildren->item(i)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
        {
            uno::Reference<xml::dom::XElement> xElem(xChildren->item(i), uno::UNO_QUERY_THROW);
            rtl::OUString sType = xElem->getTagName();

            boost::shared_ptr<ShapeObject> pShapeObject;

            if (sType == USTR("polygon"))
                pShapeObject.reset(new ShapePolygon(maScene));
            else if (sType == USTR("polyline"))
                pShapeObject.reset(new ShapePolygon(maScene, false));
            else if (sType == USTR("path"))
                pShapeObject.reset(new ShapePath(maScene));
            else if (sType == USTR("ellipse") || sType == USTR("circle"))
                pShapeObject.reset(new ShapeEllipse(maScene));
            else if (sType == USTR("rect"))
                pShapeObject.reset(new ShapeRect(maScene));
            else if (sType == USTR("line"))
                pShapeObject.reset(new ShapeLine(maScene));
            else if (sType == USTR("g"))
                importShapeSVG(xChildren->item(i), xElem->getAttributes());
            else
                fprintf(stderr, "unknown nodepath %s\n", rtl::OUStringToOString(sType, RTL_TEXTENCODING_UTF8).getStr());

            if (pShapeObject)
            {
                if (rxParentAttributes.is())
                    pShapeObject->import(rxParentAttributes);
                pShapeObject->import(xElem->getAttributes());
                maShapes.push_back(pShapeObject);
            }
        }
    }
}

void ShapeImporter::writeConnectionPoints(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler) const
{
    if (maConnectionPoints.size())
    {
        /*
         * For connection points the svg:x  and svg:y attributes are relative
         * values, not absolute coordinates. Values range from -5cm (the left side
         * or bottom) to 5cm (the right or top). Which frankly is rather bizarre.
         */
        basegfx::B2DRange aSceneRange = maScene.getB2DRange();
        float chscale = 10/aSceneRange.getWidth();
        float cvscale = 10/aSceneRange.getHeight();
        sal_Int32 id=4;
        PropertyMap aProps;
        std::vector< ConnectionPoint >::const_iterator aEnd = maConnectionPoints.end();
        for (std::vector< ConnectionPoint >::const_iterator aI = maConnectionPoints.begin(); aI != aEnd; ++aI)
        {
            float cx = aI->mx, cy = aI->my;
            float relx = cx - aSceneRange.getMinX();
            float rely = cy - aSceneRange.getMinY();
            cx = -5+relx*chscale;
            cy = -5+rely*cvscale;
            aProps[USTR("svg:x")] = rtl::OUString::number(cx) + USTR("cm");
            aProps[USTR("svg:y")] = rtl::OUString::number(cy) + USTR("cm");
            aProps[USTR("draw:id")] = rtl::OUString::number(id++);

            rxDocHandler->startElement(USTR("draw:glue-point"), makeXAttributeAndClear(aProps));
            rxDocHandler->endElement(USTR("draw:glue-point"));
        }
    }
}

void writeText(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler, 
    const PropertyMap &rTextProps, const rtl::OUString &rString)
{
    rDocHandler->startElement(USTR("text:p"), new SaxAttrList(rTextProps));
    sal_Int32 nIndex=0;
    do
    {
        rDocHandler->startElement(USTR("text:span"), uno::Reference<xml::sax::XAttributeList>());
        rtl::OUString sSpan = rString.getToken(0, '\n', nIndex);
        rDocHandler->characters(sSpan);
        rDocHandler->endElement(USTR("text:span"));
        if (nIndex >= 0)
        {
            rDocHandler->startElement(USTR("text:span"), uno::Reference<xml::sax::XAttributeList>());
            rDocHandler->startElement(USTR("text:line-break"), uno::Reference<xml::sax::XAttributeList>());
            rDocHandler->endElement(USTR("text:line-break"));
            rDocHandler->endElement(USTR("text:span"));
        }
    }
    while ( nIndex >= 0 );
    rDocHandler->endElement(USTR("text:p"));
}

void ShapeImporter::writeTextBox(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler, float x, float y, float hscale, float vscale, const PropertyMap &rTextProps, const rtl::OUString &rString) const
{
    if (maTextBox.isEmpty())
        return;

    basegfx::B2DRange aSceneRange = maScene.getB2DRange();

    float relx = maTextBox.getMinX() - aSceneRange.getMinX();
    float rely = maTextBox.getMinY() - aSceneRange.getMinY();

    PropertyMap aTextAttrs;
    aTextAttrs[USTR("draw:style-name")] = USTR("grtext");
    aTextAttrs[USTR("svg:x")] = rtl::OUString::number(x+relx*hscale) + USTR("cm");
    aTextAttrs[USTR("svg:y")] = rtl::OUString::number(y+rely*vscale) + USTR("cm");
    aTextAttrs[USTR("svg:width")] = rtl::OUString::number(safeDimension(maTextBox.getWidth()*hscale)) + USTR("cm");
    aTextAttrs[USTR("svg:height")] = rtl::OUString::number(safeDimension(maTextBox.getHeight()*vscale)) + USTR("cm");
    rxDocHandler->startElement(USTR("draw:frame"), new SaxAttrList(aTextAttrs));
    rxDocHandler->startElement(USTR("draw:text-box"), new SaxAttrList(PropertyMap()));
    writeText(rxDocHandler, rTextProps, rString);
    rxDocHandler->endElement(USTR("draw:text-box"));
    rxDocHandler->endElement(USTR("draw:frame"));
}

void ShapeImporter::importConnectionPoints(const uno::Reference < xml::dom::XElement > &rxDocElem)
{
    uno::Reference<xml::dom::XNodeList> xNameNodes(rxDocElem->getElementsByTagName( USTR("connections") ));
    const sal_Int32 nNumNodes( xNameNodes->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        uno::Reference<xml::dom::XNodeList> xSubChildren( xNameNodes->item(i)->getChildNodes() );
        const sal_Int32 nNumSubNodes( xSubChildren->getLength() );
        for( sal_Int32 j=0; j<nNumSubNodes; ++j )
        {
            if( xSubChildren->item(j)->getNodeType() == xml::dom::NodeType_ELEMENT_NODE )
            {
                uno::Reference<xml::dom::XElement> xElem(xSubChildren->item(j), uno::UNO_QUERY_THROW);
                rtl::OUString sType = xElem->getTagName();
                if (!sType.equalsAscii("point"))
                    continue;
                const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = xElem->getAttributes();
                if (!xAttributes.is())
                    continue;
                uno::Reference<xml::dom::XNode> xNode;
                float x, y;
                xNode = xAttributes->getNamedItem(USTR("x"));
                if (!xNode.is())
                    continue;
                x = xNode->getNodeValue().toFloat();
                xNode = xAttributes->getNamedItem(USTR("y"));
                if (!xNode.is())
                    continue;
                y = xNode->getNodeValue().toFloat();
                maConnectionPoints.push_back(ConnectionPoint(x, y, DIR_ALL));
            }
        }
    }
}

void ShapeImporter::importTextBox(const uno::Reference < xml::dom::XElement > &rxDocElem)
{
    uno::Reference<xml::dom::XNodeList> xNameNodes(rxDocElem->getElementsByTagName( USTR("textbox") ));
    const sal_Int32 nNumNodes( xNameNodes->getLength() );
    for( sal_Int32 i=0; i<nNumNodes; ++i )
    {
        uno::Reference<xml::dom::XElement> xElem(xNameNodes->item(i), uno::UNO_QUERY_THROW);
        const uno::Reference<xml::dom::XNamedNodeMap> xAttributes = xElem->getAttributes();
        if (!xAttributes.is())
            continue;
        uno::Reference<xml::dom::XNode> xNode;
        float x1, y1, x2, y2;
        xNode = xAttributes->getNamedItem(USTR("x1"));
        if (!xNode.is())
            continue;
        x1 = xNode->getNodeValue().toFloat();
        xNode = xAttributes->getNamedItem(USTR("y1"));
        if (!xNode.is())
            continue;
        y1 = xNode->getNodeValue().toFloat();
        xNode = xAttributes->getNamedItem(USTR("x2"));
        if (!xNode.is())
            continue;
        x2 = xNode->getNodeValue().toFloat();
        xNode = xAttributes->getNamedItem(USTR("y2"));
        if (!xNode.is())
            continue;
        y2 = xNode->getNodeValue().toFloat();

        maTextBox = basegfx::B2DRectangle(x1, y1, x2, y2);
        maScene.append(basegfx::tools::createPolygonFromRect(maTextBox));
    }
}

bool ShapeImporter::import(uno::Reference < xml::dom::XElement > xDocElem)
{
    if (xDocElem->getTagName() != USTR("shape"))
        return false;

    //Get name of shape
    {
        uno::Reference<xml::dom::XNodeList> xNameNodes(xDocElem->getElementsByTagName( USTR("name") ));
        const sal_Int32 nNumNodes( xNameNodes->getLength() );
        for( sal_Int32 i=0; i<nNumNodes; ++i )
        {
            uno::Reference<xml::dom::XNodeList> xSubChildren( xNameNodes->item(i)->getChildNodes() );
            const sal_Int32 nNumSubNodes( xSubChildren->getLength() );
            if( nNumSubNodes == 1 && xSubChildren->item(0)->getNodeType() == xml::dom::NodeType_TEXT_NODE )
                msTitle = xSubChildren->item(0)->getNodeValue();
#if 0
            fprintf(stderr, "Title is %s\n", rtl::OUStringToOString(msTitle, RTL_TEXTENCODING_UTF8).getStr());
#endif
        }
    }

    //Get connection points
    importConnectionPoints(xDocElem);

    //Get Text Box
    importTextBox(xDocElem);

    {
        uno::Reference<xml::dom::XNodeList> xSVGNodes(xDocElem->getElementsByTagName( USTR("svg") ));
        const sal_Int32 nNumNodes( xSVGNodes->getLength() );
        for( sal_Int32 i=0; i<nNumNodes; ++i )
            importShapeSVG(xSVGNodes->item(i), uno::Reference<xml::dom::XNamedNodeMap>());
    }

    setConnectionDirections();

    return true;
}

void ShapeImporter::setConnectionDirections()
{
    basegfx::B2DRange aSceneRange = maScene.getB2DRange();
    float left = aSceneRange.getMinX();
    float right = aSceneRange.getMaxX();
    float top = aSceneRange.getMinY();
    float bottom = aSceneRange.getMaxY();

    std::vector< ConnectionPoint >::iterator aEnd = maConnectionPoints.end();
    for (std::vector< ConnectionPoint >::iterator aI = maConnectionPoints.begin(); aI != aEnd; ++aI)
    {
        aI->mdir = 0;
        if(aI->mx == left)
           aI->mdir |= DIR_WEST;
        if(aI->mx == right)
            aI->mdir |= DIR_EAST;
        if(aI->my == top)
            aI->mdir |= DIR_NORTH;
        if(aI->my == bottom)
            aI->mdir |= DIR_SOUTH;
#if 0
        //Enable when #i114567# is fixed
        if (!aI->mdir)
            aI->mdir = DIR_ALL;
#endif
    }
}

int ShapeImporter::getConnectionDirection(sal_Int32 nConnection) const
{
    if (static_cast<size_t>(nConnection) >= maConnectionPoints.size())
    {
        fprintf(stderr, ".shape connection point %" SAL_PRIdINT32 " unknown\n", nConnection);
        return DIR_ALL;
    }

    return maConnectionPoints[nConnection].mdir;
}

bool ShapeImporter::getConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint) const
{
    if (static_cast<size_t>(nConnection) >= maConnectionPoints.size())
    {
        fprintf(stderr, ".shape connection point %" SAL_PRIdINT32 " unknown\n", nConnection);
        return false;
    }

    basegfx::B2DRange aSceneRange = maScene.getB2DRange();
    float chscale = 10/aSceneRange.getWidth();
    float cvscale = 10/aSceneRange.getHeight();
    float cx = maConnectionPoints[nConnection].mx, cy = maConnectionPoints[nConnection].my;
    float relx = cx - aSceneRange.getMinX();
    float rely = cy - aSceneRange.getMinY();
    cx = -5+relx*chscale;
    cy = -5+rely*cvscale;

    rPoint = basegfx::B2DPoint(cx, cy);
    return true;
}

float ShapeImporter::getAspectRatio() const
{
    basegfx::B2DRange aSceneRange = getScene().getB2DRange();
    return aSceneRange.getWidth() / aSceneRange.getHeight();
}

ShapeTemplate::ShapeTemplate(shapeimporter aImporter)
    : maImporter(aImporter)
{
}

void ShapeTemplate::convertShapes(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler,
    const PropertyMap &rParentProps, const PropertyMap &rTextProps, const rtl::OUString &rString) const
{
#if 0
    fprintf(stderr, "scene has %d shapes\n", maScene.count());
#endif

    PropertyMap::const_iterator aI;
    aI = rParentProps.find(USTR("svg:x"));
    float x = aI != rParentProps.end() ? ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat() : 0;
    aI = rParentProps.find(USTR("svg:y"));
    float y = aI != rParentProps.end() ? ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat() : 0;
    aI = rParentProps.find(USTR("svg:width"));
    float width = aI != rParentProps.end() ? ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat() : DEFAULTSIZE;
    aI = rParentProps.find(USTR("svg:height"));
    float height = aI != rParentProps.end() ? ::comphelper::string::searchAndReplaceAllAsciiWithAscii(aI->second, "cm", "").toFloat() : DEFAULTSIZE;

    PropertyMap aProps;
    aI = rParentProps.find(USTR("draw:id"));
    if (aI != rParentProps.end())
        aProps[USTR("draw:id")] = aI->second;

    rxDocHandler->startElement(USTR("draw:g"), makeXAttribute(aProps));

    maImporter->writeConnectionPoints(rxDocHandler);

    basegfx::B2DRange aSceneRange = maImporter->getScene().getB2DRange();
    float hscale = width/aSceneRange.getWidth();
    float vscale = height/aSceneRange.getHeight();

    const shapevec &rShapes = maImporter->getShapes();
    shapevec::const_iterator aEnd = rShapes.end();
    std::vector< PropertyMap >::const_iterator shapeoverride = maShapeOverrideProps.begin();
    for (shapevec::const_iterator aI = rShapes.begin(); aI != aEnd; ++aI, ++shapeoverride)
    {
        (*aI)->write(rxDocHandler, rParentProps, *shapeoverride, x, y, hscale, vscale);
    }

    maImporter->writeTextBox(rxDocHandler, x, y, hscale, vscale, rTextProps, rString);

    rxDocHandler->endElement(USTR("draw:g"));
}

void ShapeTemplate::generateStyles(GraphicStyleManager &rStyleManager,
    const PropertyMap &rParentProps, bool bShowBackground)
{
    const shapevec &rShapes = maImporter->getShapes();
    shapevec::const_iterator aEnd = rShapes.end();
    maShapeOverrideProps.clear();
    PropertyMap aShapeOverrides;
    PropertyMap aParentProps(rParentProps);
    for (shapevec::const_iterator aI = rShapes.begin(); aI != aEnd; ++aI)
    {
        (*aI)->generateStyle(rStyleManager, aParentProps, aShapeOverrides, bShowBackground);
        maShapeOverrideProps.push_back(aShapeOverrides);
        aShapeOverrides.clear();
    }
}

bool DIAShapeFilter::convert(const ShapeTemplate &rTemplate, uno::Reference < xml::sax::XDocumentHandler > xDocHandler)
{
    xDocHandler->startDocument();

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
    aAttrs[USTR("xmlns:meta")] = USTR("urn:oasis:names:tc:opendocument:xmlns:meta:1.0");
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

    xDocHandler->startElement(USTR("office:document"), makeXAttributeAndClear(aAttrs));

//    xDocHandler->startElement(USTR("office:meta"), uno::Reference<xml::sax::XAttributeList>());
    xDocHandler->startElement(USTR("office:meta"), new SaxAttrList(PropertyMap()));
//    xDocHandler->startElement(USTR("dc:title"), uno::Reference<xml::sax::XAttributeList>());
    xDocHandler->startElement(USTR("dc:title"), new SaxAttrList(PropertyMap()));
    xDocHandler->characters(rTemplate.getTitle());
    xDocHandler->endElement(USTR("dc:title"));
    xDocHandler->endElement(USTR("office:meta"));

    xDocHandler->startElement(USTR("office:styles"), uno::Reference<xml::sax::XAttributeList>());
    aAttrs[USTR("style:name")] = USTR("standard");
    aAttrs[USTR("style:family")] = USTR("graphic");
    xDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
    aAttrs[USTR("svg:stroke-width")] = USTR("0.10cm");
    aAttrs[USTR("draw:fill-color")] = USTR("#ffffff");
    xDocHandler->startElement(USTR("style:graphic-properties"), makeXAttributeAndClear(aAttrs));
    xDocHandler->endElement(USTR("style:graphic-properties"));
    xDocHandler->endElement(USTR("style:style"));
    xDocHandler->endElement(USTR("office:styles"));

    xDocHandler->startElement(USTR("office:automatic-styles"), uno::Reference<xml::sax::XAttributeList>());

    aAttrs[USTR("style:name")] = USTR("pagelayout1");
    xDocHandler->startElement(USTR("style:page-layout"), makeXAttributeAndClear(aAttrs));
    aAttrs[USTR("fo:margin-top")] = USTR("0mm");
    aAttrs[USTR("fo:margin-bottom")] = USTR("0mm");
    aAttrs[USTR("fo:margin-left")] = USTR("0mm");
    aAttrs[USTR("fo:margin-right")] = USTR("0mm");
    aAttrs[USTR("fo:page-width")] = USTR("210mm");
    aAttrs[USTR("fo:page-height")] = USTR("297mm");
    xDocHandler->startElement(USTR("style:page-layout-properties"), makeXAttributeAndClear(aAttrs));
    xDocHandler->endElement(USTR("style:page-layout-properties"));
    xDocHandler->endElement(USTR("style:page-layout"));

    aAttrs[USTR( "style:name")] = USTR("pagestyle1");
    aAttrs[USTR( "style:family")] = USTR("drawing-page");
    xDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
//    xDocHandler->startElement(USTR("style:drawing-page-properties"), uno::Reference<xml::sax::XAttributeList>());
    xDocHandler->startElement(USTR("style:drawing-page-properties"), new SaxAttrList(PropertyMap()));
    xDocHandler->endElement(USTR("style:drawing-page-properties"));
    xDocHandler->endElement(USTR("style:style"));

    aAttrs[USTR("style:name")] = USTR("grtext");
    aAttrs[USTR("style:family")] = USTR("graphic");
    xDocHandler->startElement(USTR("style:style"), makeXAttributeAndClear(aAttrs));
    aAttrs[USTR("draw:stroke")] = USTR("none");
    aAttrs[USTR("draw:fill")] = USTR("none");
    aAttrs[USTR("draw:textarea-horizontal-align")] = USTR("center");
    aAttrs[USTR("draw:textarea-vertical-align")] = USTR("middle");
    aAttrs[USTR("draw:auto-grow-width")] = USTR("true");
    xDocHandler->startElement(USTR("style:graphic-properties"), makeXAttributeAndClear(aAttrs));
    xDocHandler->endElement(USTR("style:graphic-properties"));
    xDocHandler->endElement(USTR("style:style"));


    maGraphicStyles.write(xDocHandler);

    xDocHandler->endElement( USTR("office:automatic-styles") );

    xDocHandler->startElement( USTR("office:master-styles"), uno::Reference<xml::sax::XAttributeList>());
    aAttrs[USTR( "style:name")] = USTR("Default");
    aAttrs[USTR( "style:page-layout-name")] = USTR("pagelayout1");
    aAttrs[USTR( "draw:style-name")] = USTR("pagestyle1");
    xDocHandler->startElement(USTR("style:master-page"), makeXAttributeAndClear(aAttrs));
    xDocHandler->endElement(USTR("style:master-page"));
    xDocHandler->endElement(USTR("office:master-styles"));

    xDocHandler->startElement(USTR("office:body"), uno::Reference<xml::sax::XAttributeList>());
    xDocHandler->startElement(USTR("office:drawing"), uno::Reference<xml::sax::XAttributeList>());

    aAttrs[USTR("draw:master-page-name")] = USTR("Default");
    aAttrs[USTR("style:page-layout-name")] = USTR("pagelayout1");
    aAttrs[USTR("draw:style-name")] = USTR("pagestyle1");
    xDocHandler->startElement(USTR("draw:page"), makeXAttributeAndClear(aAttrs));

    PropertyMap aDefaultAttrs;
    aDefaultAttrs[USTR("svg:x")] = USTR("0cm");
    aDefaultAttrs[USTR("svg:y")] = USTR("0cm");
    aDefaultAttrs[USTR("svg:width")] = rtl::OUString::number(DEFAULTSIZE * mfAspectRatio) + USTR("cm");
    aDefaultAttrs[USTR("svg:height")] = rtl::OUString::number(DEFAULTSIZE) + USTR("cm");

    rTemplate.convertShapes(xDocHandler, aDefaultAttrs, PropertyMap(), rtl::OUString());

    xDocHandler->endElement(USTR("draw:page"));
    xDocHandler->endElement(USTR("office:drawing"));
    xDocHandler->endElement(USTR("office:body"));

    xDocHandler->endElement(USTR("office:document"));
    xDocHandler->endDocument();  
    return true;
}


DIAShapeFilter::DIAShapeFilter( const uno::Reference< uno::XComponentContext >& rxCtx )
    : mxMSF( rxCtx->getServiceManager(), uno::UNO_QUERY_THROW ), mfAspectRatio(1.0)
{
}

sal_Bool SAL_CALL DIAShapeFilter::filter( const uno::Sequence< beans::PropertyValue >& rDescriptor )
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

    uno::Reference<xml::dom::XDocument> xDom( xDomBuilder->parse(xInputStream), uno::UNO_QUERY_THROW );

    uno::Reference<xml::dom::XElement> xDocElem( xDom->getDocumentElement(), uno::UNO_QUERY_THROW );

    shapeimporter aImporter(new ShapeImporter);
    if (aImporter->import(xDocElem))
    {
        mfAspectRatio = aImporter->getAspectRatio();
        ShapeTemplate aTemplate(aImporter);
        PropertyMap aDefaultStyle;
        aDefaultStyle[USTR("svg:stroke-width")] = USTR("0.10cm");
        aDefaultStyle[USTR("draw:fill-color")] = USTR("#ffffff");
        aTemplate.generateStyles(maGraphicStyles, aDefaultStyle, true);
        return convert(aTemplate, xDocHandler);
    }
    return false;
}

void SAL_CALL DIAShapeFilter::setTargetDocument( const uno::Reference< lang::XComponent >& xDoc )
    throw (lang::IllegalArgumentException, uno::RuntimeException)
{
    mxDstDoc = xDoc;
}

rtl::OUString SAL_CALL DIAShapeFilter::detect( uno::Sequence< beans::PropertyValue >& io_rDescriptor )
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

        rtl::OUString sRet;

        uno::Sequence<sal_Int8> aData(64);
        sal_Int32 nLen = xInputStream->readBytes(aData, 64);
        rtl::OString aTmp(reinterpret_cast<const sal_Char*>(aData.getArray()), nLen);
        if (aTmp.indexOf(rtl::OString(RTL_CONSTASCII_STRINGPARAM("<shape "))) != -1)
            sRet = rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("shape_DIA"));

        if (xSeekable.is())
            xSeekable->seek(nPos);

        return sRet;
    }
    catch (io::IOException const&)
    {
        return rtl::OUString();
    }
}

rtl::OUString SAL_CALL DIAShapeFilter::getImplementationName()
    throw (uno::RuntimeException)
{
    return getImplementationName_static();
}

sal_Bool SAL_CALL DIAShapeFilter::supportsService(const rtl::OUString &serviceName)
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

uno::Sequence<rtl::OUString> SAL_CALL DIAShapeFilter::getSupportedServiceNames()
    throw (uno::RuntimeException)
{
    return getSupportedServiceNames_static();
}

rtl::OUString DIAShapeFilter::getImplementationName_static() 
{
    return rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("mcnamara.caolan.comp.Draw.DIAShapeFilter"));
}

uno::Sequence<rtl::OUString> DIAShapeFilter::getSupportedServiceNames_static()
{
    uno::Sequence<rtl::OUString> snames(2);
    snames.getArray()[0] = rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("com.sun.star.document.ExtendedTypeDetection"));
    snames.getArray()[1] = rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("com.sun.star.document.ImportFilter"));
    return snames;
}

uno::Reference<uno::XInterface> DIAShapeFilter::get(uno::Reference<uno::XComponentContext> const & context)
{
    return static_cast< ::cppu::OWeakObject * >(new DIAShapeFilter(context));
}

/* vi:set tabstop=4 shiftwidth=4 expandtab: */
