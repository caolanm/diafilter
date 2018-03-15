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

#ifndef GZ_INPUTSTREAM_HXX
#define GZ_INPUTSTREAM_HXX

#include <com/sun/star/io/XInputStream.hpp>
#include <cppuhelper/implbase1.hxx>

extern "C"
{
    typedef struct z_stream_s z_stream;
}

class gz_InputStream :
    public ::cppu::WeakImplHelper1< ::com::sun::star::io::XInputStream >
{
private:
	::com::sun::star::uno::Reference< ::com::sun::star::io::XInputStream > mxInputStream;
    ::com::sun::star::uno::Sequence< sal_Int8 > maBuffer;
    z_stream* mpStream;
public:
	gz_InputStream(::com::sun::star::uno::Reference< ::com::sun::star::io::XInputStream > xInputStream);
    virtual ~gz_InputStream();

    // XInputStream
    virtual sal_Int32 SAL_CALL readBytes( ::com::sun::star::uno::Sequence< sal_Int8 > & aData,
        sal_Int32 nBytesToRead )
            throw( ::com::sun::star::io::NotConnectedException,
                ::com::sun::star::io::BufferSizeExceededException,
                ::com::sun::star::io::IOException,
                ::com::sun::star::uno::RuntimeException );

    virtual sal_Int32 SAL_CALL readSomeBytes( ::com::sun::star::uno::Sequence< sal_Int8 > & aData,
        sal_Int32 nMaxBytesToRead )
            throw( ::com::sun::star::io::NotConnectedException,
                ::com::sun::star::io::BufferSizeExceededException,
                ::com::sun::star::io::IOException,
                ::com::sun::star::uno::RuntimeException );

    virtual void SAL_CALL skipBytes( sal_Int32 nBytesToSkip )
            throw( ::com::sun::star::io::NotConnectedException,
                ::com::sun::star::io::BufferSizeExceededException,
                ::com::sun::star::io::IOException,
                ::com::sun::star::uno::RuntimeException );

    virtual sal_Int32 SAL_CALL available( void )
            throw( ::com::sun::star::io::NotConnectedException,
                ::com::sun::star::io::IOException,
                ::com::sun::star::uno::RuntimeException );

    virtual void SAL_CALL closeInput( void )
            throw( ::com::sun::star::io::NotConnectedException,
                ::com::sun::star::io::IOException,
                ::com::sun::star::uno::RuntimeException );
};

#endif
/* vi:set tabstop=4 shiftwidth=4 expandtab: */
