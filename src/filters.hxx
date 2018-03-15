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

#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/document/XFilter.hpp>
#include <com/sun/star/document/XImporter.hpp>
#include <com/sun/star/document/XExtendedFilterDetection.hpp>
#include <com/sun/star/awt/XDevice.hpp>
#include <com/sun/star/xml/sax/XDocumentHandler.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <cppuhelper/implbase4.hxx>
#include <boost/unordered_map.hpp>
#include "saxattrlist.hxx"

using namespace ::com::sun::star;

class DIAFilter : public cppu::WeakImplHelper4<
    lang::XServiceInfo,
    document::XFilter,
    document::XImporter,
    document::XExtendedFilterDetection >
{
public:
    explicit DIAFilter( const uno::Reference< uno::XComponentContext >& rxCtx );

    static rtl::OUString getImplementationName_static();
    static uno::Sequence<rtl::OUString> getSupportedServiceNames_static();
    static uno::Reference<XInterface> get(uno::Reference<uno::XComponentContext> const & context);

protected:
    // ::com::sun::star::lang::XServiceInfo:
    virtual rtl::OUString SAL_CALL getImplementationName();
    virtual sal_Bool SAL_CALL supportsService(const rtl::OUString & serviceName);
    virtual uno::Sequence<rtl::OUString> SAL_CALL getSupportedServiceNames();

    // XFilter
    virtual sal_Bool SAL_CALL filter( const uno::Sequence< beans::PropertyValue >& rDescriptor );
    virtual void SAL_CALL cancel();

    // XImporter
    virtual void SAL_CALL setTargetDocument( const uno::Reference< lang::XComponent >& xDoc );

    // XExtendedFilterDetection
    virtual rtl::OUString SAL_CALL detect( uno::Sequence< beans::PropertyValue >& io_rDescriptor );
private:
    rtl::OUString msInstallDir;
    uno::Reference< uno::XComponentContext > mxCtx;
    uno::Reference< lang::XMultiServiceFactory > mxMSF;
    uno::Reference< lang::XComponent > mxDstDoc;

    rtl::OUString getInstallPath();
};

typedef boost::unordered_map< rtl::OUString, rtl::OUString, rtl::OUStringHash > PropertyMap;
typedef std::pair< rtl::OUString, PropertyMap > autostyle;
typedef std::vector< autostyle > autostyles;

struct ParaTextStyle
{
    PropertyMap maTextAttrs;
    PropertyMap maParaAttrs;
};

typedef std::pair< rtl::OUString, ParaTextStyle  > extendedautostyle;
typedef std::vector< extendedautostyle > extendedautostyles;

class GraphicStyleManager
{
private:
    autostyles maGraphicStyles;
    void addTextBoxStyle();
public:
    GraphicStyleManager()
    {
        //Ensure a suitable style for textboxes
        addTextBoxStyle();
    }
    void addAutomaticGraphicStyle(PropertyMap &rAttrs, const PropertyMap &rStyleAttrs);
    void write(uno::Reference < xml::sax::XDocumentHandler > xDocHandler);
    const PropertyMap *getStyleByName(const rtl::OUString &rName) const;
};

class TextStyleManager
{
private:
    extendedautostyles maTextStyles;
    uno::Reference< awt::XDevice > mxReferenceDevice;
    void fixFontSizes(PropertyMap &rStyleAttrs);
public:
    void makeReferenceDevice(uno::Reference< uno::XComponentContext > xCtx);
    void addAutomaticTextStyle(PropertyMap &rAttrs, ParaTextStyle &rStyleAttrs);
    void write(uno::Reference < xml::sax::XDocumentHandler > xDocHandler);
    const PropertyMap *getStyleByName(const rtl::OUString &rName) const;
    double getStringWidth(const rtl::OUString &rStyleName, const rtl::OUString &rString) const;
    awt::FontDescriptor getFontDescriptor(const PropertyMap &rStyleAttrs) const;
    uno::Reference< awt::XFont > getMatchingFont(const PropertyMap &rStyleAttrs) const;
};

class ShapeTemplate;

class DIAShapeFilter : public cppu::WeakImplHelper4<
    lang::XServiceInfo,
    document::XFilter,
    document::XImporter,
    document::XExtendedFilterDetection >
{
public:
    explicit DIAShapeFilter( const uno::Reference< uno::XComponentContext >& rxCtx );

    static rtl::OUString getImplementationName_static();
    static uno::Sequence<rtl::OUString> getSupportedServiceNames_static();
    static uno::Reference<XInterface> get(uno::Reference<uno::XComponentContext> const & context);
protected:
    // ::com::sun::star::lang::XServiceInfo:
    virtual rtl::OUString SAL_CALL getImplementationName();
    virtual sal_Bool SAL_CALL supportsService(const rtl::OUString & serviceName);
    virtual uno::Sequence<rtl::OUString> SAL_CALL getSupportedServiceNames();

    // XFilter
    virtual sal_Bool SAL_CALL filter( const uno::Sequence< beans::PropertyValue >& rDescriptor );
    virtual void SAL_CALL cancel();

    // XImporter
    virtual void SAL_CALL setTargetDocument( const uno::Reference< lang::XComponent >& xDoc );

    // XExtendedFilterDetection
    virtual rtl::OUString SAL_CALL detect( uno::Sequence< beans::PropertyValue >& io_rDescriptor );
private:
    uno::Reference< lang::XMultiServiceFactory > mxMSF;
    uno::Reference< lang::XComponent > mxDstDoc;
    GraphicStyleManager maGraphicStyles;
    float mfAspectRatio;

    bool convert(const ShapeTemplate &rTemplate, uno::Reference < xml::sax::XDocumentHandler > xDocHandler);
};

#define USTR(x) rtl::OUString( RTL_CONSTASCII_USTRINGPARAM( x ) )
#define OASIS_STR "urn:oasis:names:tc:opendocument:xmlns:"

using pdfi::SaxAttrList;
SaxAttrList *makeXAttribute(const PropertyMap &rAttrs);
SaxAttrList *makeXAttributeAndClear(PropertyMap &rProps);

void createViewportAndPolygonFromPoints(const rtl::OUString &rPoints, PropertyMap &rAttrs, bool bClose=false);
void createViewportAndPathFromPath(const rtl::OUString &rPath, PropertyMap &rAttrs);
void createViewportFromPoints(const rtl::OUString &rPath, PropertyMap &rAttrs, float fAdjustX, float fAdjustY);
void writeText(uno::Reference<xml::sax::XDocumentHandler> &rDocHandler,
    const PropertyMap &rTextProps, const rtl::OUString &rString);

/* vi:set tabstop=4 shiftwidth=4 expandtab: */
