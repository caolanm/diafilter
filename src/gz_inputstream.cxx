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

#include <gz_inputstream.hxx>

#include <stdio.h>
#include <string.h>
#include <zlib.h>

#define ASCII_FLAG   0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC     0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define RESERVED     0xE0 /* bits 5..7: reserved */

#define Z_BUFSIZE    16384

using namespace com::sun::star;

gz_InputStream::gz_InputStream(uno::Reference<io::XInputStream> xInputStream)
	: mxInputStream(xInputStream)
{
    if (!mxInputStream.is())
        throw io::NotConnectedException();

    uno::Sequence< sal_Int8 > aData(10);
    if (10 != mxInputStream->readBytes(aData, 10))
        throw io::NotConnectedException();

    if (aData[0] != 0x1F || aData[1] != static_cast<sal_Int8>(0x8B))
        throw io::NotConnectedException();

    int method = aData[2];
    int flags = aData[3];

    if (method != Z_DEFLATED || (flags & RESERVED) != 0)
        throw io::NotConnectedException();

    if ((flags & EXTRA_FIELD) != 0)
    {
        if (2 != mxInputStream->readBytes(aData, 2))
            throw io::NotConnectedException();
        unsigned int len = static_cast<unsigned int>(aData[0]);
        len += static_cast<unsigned int>(aData[1])<<8;
        mxInputStream->skipBytes(len);
    }
    if ((flags & ORIG_NAME) != 0)
        while (mxInputStream->readBytes(aData, 1) == 1 && aData[0] != 0) {};
    if ((flags & COMMENT) != 0)
        while (mxInputStream->readBytes(aData, 1) == 1 && aData[0] != 0) {};
    if ((flags & HEAD_CRC) != 0)
        mxInputStream->skipBytes(2);

    mpStream = new z_stream;
    memset(mpStream, 0, sizeof(z_stream));
    if (Z_OK != inflateInit2(mpStream, -MAX_WBITS))
    {
        delete mpStream;
        throw io::NotConnectedException();
    }
}

gz_InputStream::~gz_InputStream()
{
    closeInput();
}

void SAL_CALL gz_InputStream::closeInput()
    throw( io::NotConnectedException, io::IOException, uno::RuntimeException )
{
    inflateEnd(mpStream);
    delete mpStream;
    mpStream = NULL;
}

void SAL_CALL gz_InputStream::skipBytes( sal_Int32 nBytesToSkip )
    throw( io::NotConnectedException, io::BufferSizeExceededException,
      io::IOException, uno::RuntimeException )
{
	uno::Sequence< sal_Int8 > aData(nBytesToSkip);
    readBytes(aData, nBytesToSkip);
}

sal_Int32 SAL_CALL gz_InputStream::readBytes( uno::Sequence< sal_Int8 >& aData, sal_Int32 nBytesToRead )
    throw( io::NotConnectedException, io::BufferSizeExceededException,
      io::IOException, uno::RuntimeException )
{
    try
    {
        aData.realloc( nBytesToRead );
    }
    catch ( const uno::Exception &e )
    {
        throw io::BufferSizeExceededException();
    }

    if (!nBytesToRead)
        return 0;

    mpStream->avail_out = nBytesToRead;
    mpStream->next_out = reinterpret_cast<unsigned char*>(aData.getArray());

    while (mpStream->avail_out)
    {
        if (mpStream->avail_in == 0)
        {
            mpStream->avail_in = mxInputStream->readSomeBytes(maBuffer, Z_BUFSIZE);
            mpStream->next_in = reinterpret_cast<unsigned char*>(maBuffer.getArray());
        }
        if (mpStream->avail_in == 0)
            break;
        if (Z_OK != inflate(mpStream, Z_NO_FLUSH))
            break;
    }
    return nBytesToRead-mpStream->avail_out;
}

sal_Int32 SAL_CALL gz_InputStream::available()
    throw( io::NotConnectedException, io::IOException, uno::RuntimeException )
{
    return 0;
}

sal_Int32 SAL_CALL gz_InputStream::readSomeBytes(
	uno::Sequence< sal_Int8 >& aData, sal_Int32 nMaxBytesToRead )
    throw( io::NotConnectedException, io::BufferSizeExceededException,
      io::IOException, uno::RuntimeException )
{
    return readBytes(aData, nMaxBytesToRead);
}

/* vi:set tabstop=4 shiftwidth=4 expandtab: */
