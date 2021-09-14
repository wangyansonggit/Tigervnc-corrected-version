/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2014 Pierre Ossman for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#include <rfb_win32/DIBSectionBuffer.h>
#include <rfb_win32/DeviceContext.h>
#include <rfb_win32/BitmapInfo.h>
#include <rfb/LogWriter.h>

using namespace rfb;
using namespace win32;

static LogWriter vlog("DIBSectionBuffer");


DIBSectionBuffer::DIBSectionBuffer(HWND window_)
  : bitmap(0), window(window_), device(0) {
  memset(&format, 0, sizeof(format));
}

DIBSectionBuffer::DIBSectionBuffer(HDC device_)
  : bitmap(0), window(0), device(device_) {
  memset(&format, 0, sizeof(format));
}

DIBSectionBuffer::~DIBSectionBuffer() {
  if (bitmap)
    DeleteObject(bitmap);
}


void DIBSectionBuffer::setPF(const PixelFormat& pf) {
  if (memcmp(&getPF(), &pf, sizeof(pf)) == 0) {
    vlog.debug("pixel format unchanged by setPF()");
    return;
  }
  if (!pf.trueColour)
    throw rfb::Exception("palette format not supported");
  format = pf;
  recreateBuffer();
}

void DIBSectionBuffer::setSize(int w, int h) {
  if (width_ == w && height_ == h) {
    vlog.debug("size unchanged by setSize()");
    return;
  }
  width_ = w;
  height_ = h;
  recreateBuffer();
}


inline void initMaxAndShift(DWORD mask, int* max, int* shift) {
  for ((*shift) = 0; (mask & 1) == 0; (*shift)++) mask >>= 1;
  (*max) = (rdr::U16)mask;
}

void DIBSectionBuffer::recreateBuffer() {
  HBITMAP new_bitmap = 0;
  rdr::U8* new_data = 0;

  if (width_ && height_ && (format.depth != 0)) {
    BitmapInfo bi;
    memset(&bi, 0, sizeof(bi));
    UINT iUsage = DIB_RGB_COLORS;
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biBitCount = format.bpp;
    bi.bmiHeader.biSizeImage = (format.bpp / 8) * width_ * height_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biWidth = width_;
    bi.bmiHeader.biHeight = -height_;
    bi.bmiHeader.biCompression = (format.bpp > 8) ? BI_BITFIELDS : BI_RGB;
    bi.mask.red = format.pixelFromRGB((rdr::U16)~0, 0, 0);
    bi.mask.green = format.pixelFromRGB(0, (rdr::U16)~0, 0);
    bi.mask.blue = format.pixelFromRGB(0, 0, (rdr::U16)~0);

    // Create a DIBSection to draw into
    if (device)
      new_bitmap = ::CreateDIBSection(device, (BITMAPINFO*)&bi.bmiHeader, iUsage,
                                      (void**)&new_data, NULL, 0);
    else
      new_bitmap = ::CreateDIBSection(WindowDC(window), (BITMAPINFO*)&bi.bmiHeader, iUsage,
                                      (void**)&new_data, NULL, 0);

    if (!new_bitmap) {
      int err = GetLastError();
      throw rdr::SystemException("unable to create DIB section", err);
    }

    vlog.debug("recreateBuffer()");
  } else {
    vlog.debug("one of area or format not set");
  }

  if (new_bitmap && bitmap) {
    vlog.debug("preserving bitmap contents");

    // Copy the contents across
    if (device) {
      BitmapDC src_dev(device, bitmap);
      BitmapDC dest_dev(device, new_bitmap);
      BitBlt(dest_dev, 0, 0, width_, height_, src_dev, 0, 0, SRCCOPY);
    } else {
      WindowDC wndDC(window);
      BitmapDC src_dev(wndDC, bitmap);
      BitmapDC dest_dev(wndDC, new_bitmap);
      BitBlt(dest_dev, 0, 0, width_, height_, src_dev, 0, 0, SRCCOPY);
    }
  }
  
  if (bitmap) {
    // Delete the old bitmap
    DeleteObject(bitmap);
    bitmap = 0;
    data = 0;
  }

  if (new_bitmap) {
    int bpp, depth;
    int redMax, greenMax, blueMax;
    int redShift, greenShift, blueShift;

    // Set up the new bitmap
    bitmap = new_bitmap;
    data = new_data;

    // Determine the *actual* DIBSection format
    DIBSECTION ds;
    if (!GetObject(bitmap, sizeof(ds), &ds))
      throw rdr::SystemException("GetObject", GetLastError());

    // Correct the "stride" of the DIB
    // *** This code DWORD aligns each row - is that right???
    stride = width_;
    int bytesPerRow = stride * format.bpp/8;
    if (bytesPerRow % 4) {
      bytesPerRow += 4 - (bytesPerRow % 4);
      stride = (bytesPerRow * 8) / format.bpp;
      vlog.info("adjusting DIB stride: %d to %d", width_, stride);
    }

    // Calculate the PixelFormat for the DIB
    bpp = depth = ds.dsBm.bmBitsPixel;

    // Get the truecolour format used by the DIBSection
    initMaxAndShift(ds.dsBitfields[0], &redMax, &redShift);
    initMaxAndShift(ds.dsBitfields[1], &greenMax, &greenShift);
    initMaxAndShift(ds.dsBitfields[2], &blueMax, &blueShift);

    // Calculate the effective depth
    depth = 0;
    Pixel bits = ds.dsBitfields[0] | ds.dsBitfields[1] | ds.dsBitfields[2];
    while (bits) {
      depth++;
      bits = bits >> 1;
    }
    if (depth > bpp)
      throw Exception("Bad DIBSection format (depth exceeds bpp)");

    format = PixelFormat(bpp, depth, false, true,
                         redMax, greenMax, blueMax,
                         redShift, greenShift, blueShift);
  }
}
