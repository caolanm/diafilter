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
#include <boost/shared_ptr.hpp>
#include <basegfx/polygon/b2dpolypolygon.hxx>

#define DIR_NORTH 1
#define DIR_EAST  2
#define DIR_SOUTH 4
#define DIR_WEST  8

#define DIR_NORTHEAST (DIR_NORTH|DIR_EAST)
#define DIR_SOUTHEAST (DIR_SOUTH|DIR_EAST)
#define DIR_NORTHWEST (DIR_NORTH|DIR_WEST)
#define DIR_SOUTHWEST (DIR_SOUTH|DIR_WEST)
#define DIR_ALL       (DIR_NORTH|DIR_SOUTH|DIR_EAST|DIR_WEST)

struct ConnectionPoint
{
    float mx;
    float my;
    int mdir;
    ConnectionPoint(float x, float y, int dir) : mx(x), my(y), mdir(dir) {}
};

class ShapeObject;

typedef std::vector< boost::shared_ptr<ShapeObject> > shapevec;

class ShapeImporter
{
private:
    rtl::OUString msTitle;
    basegfx::B2DPolyPolygon maScene;
    basegfx::B2DRectangle maTextBox;
    shapevec maShapes;
    std::vector< ConnectionPoint > maConnectionPoints;
    void importShapeSVG(const uno::Reference < xml::dom::XNode > &rxNode, const uno::Reference<xml::dom::XNamedNodeMap> &rxParentAttributes);
    void importConnectionPoints(const uno::Reference < xml::dom::XElement > &rxDocElem);
    void importTextBox(const uno::Reference < xml::dom::XElement > &rxDocElem);
    void setConnectionDirections();
public:
    float getAspectRatio() const;
    bool import(uno::Reference < xml::dom::XElement > xDocElem);
    void writeConnectionPoints(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler) const;
    void writeTextBox(uno::Reference < xml::sax::XDocumentHandler >& rxDocHandler, float x, float y, float hscale, float vscale, const PropertyMap &rTextProps, const rtl::OUString &rString) const;
    const rtl::OUString & getTitle() const { return msTitle; }
    const basegfx::B2DPolyPolygon & getScene() const { return maScene; }
    const shapevec & getShapes() const { return maShapes; }
    int getConnectionDirection(sal_Int32 nConnection) const;
    bool getConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint) const;
};

typedef boost::shared_ptr<ShapeImporter> shapeimporter;

class ShapeTemplate
{
private:
    shapeimporter maImporter;
    std::vector< PropertyMap > maShapeOverrideProps;
public:
    void generateStyles(GraphicStyleManager &rStyleManager, const PropertyMap &rParentProps,
        bool bShowBackground);
    void convertShapes(uno::Reference < xml::sax::XDocumentHandler > &rxDocHandler, 
        const PropertyMap &rParentProps, const PropertyMap &rTextProps, const rtl::OUString &rString) const;
    const rtl::OUString & getTitle() const { return maImporter->getTitle(); }
    int getConnectionDirection(sal_Int32 nConnection) const { return maImporter->getConnectionDirection(nConnection); }
    bool getConnectionPoint(sal_Int32 nConnection, basegfx::B2DPoint &rPoint) const {return maImporter->getConnectionPoint(nConnection, rPoint); }
    ShapeTemplate(shapeimporter aImporter);
};

/* vi:set tabstop=4 shiftwidth=4 expandtab: */
