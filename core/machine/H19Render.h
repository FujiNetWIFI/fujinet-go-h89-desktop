/*
 * H19Render -- the H19's picture: 80 columns of 8-pixel characters by 25
 * rows of 10 scanlines (640x250, MAME's visible area for the TLB), drawn
 * from the 444-29 character generator ROM and line-doubled into a 640x500
 * XRGB8888 frame.
 *
 * The ROM holds 128 characters of 16 bytes; the first ten bytes are the
 * character's scanlines, bit 7 the leftmost pixel. Bit 7 of a screen cell
 * reverses it, as the H19's video logic does; the cursor reverses its cell
 * (block) or its last scanline (underline).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_H19RENDER_H
#define H89_H19RENDER_H

#include <cstddef>
#include <cstdint>

class H19Terminal;

class H19Render
{
  public:
    static const int width_c       = 640;
    static const int height_c      = 500;   // 250 scanlines, doubled
    static const int scanlines_c   = 250;
    static const int glyphRows_c   = 10;
    static const size_t fontSize_c = 2048;

    enum Phosphor
    {
        White = 0,   // P4, the H19's own
        Green,       // P1
        Amber,       // P3
        PhosphorCount
    };

    H19Render();

    /// 444-29 (2048 bytes); false (and the font left as it was) otherwise.
    bool setFont(const uint8_t* rom, size_t size);
    bool hasFont() const
    {
        return haveFont_m;
    }

    void setPhosphor(int phosphor);
    /// Dim the second line of each doubled pair, like a real raster.
    void setScanlines(bool on)
    {
        scanlines_m = on;
    }

    /// The scanline (0-9) bitmap of character `ch` (0-127).
    uint8_t glyphRow(unsigned ch, unsigned row) const
    {
        return font_m[(ch & 0x7f) * 16 + (row % glyphRows_c)];
    }

    /// Draw the terminal's screen into out (width_c * height_c pixels).
    /// cursorOn is the blink phase.
    void render(const H19Terminal& term, bool cursorOn, uint32_t* out) const;

    static uint32_t phosphorColour(int phosphor);

  private:
    uint8_t  font_m[fontSize_c];
    bool     haveFont_m;
    bool     scanlines_m;
    uint32_t fg_m, bg_m, fgDim_m, bgDim_m;
};

#endif /* H89_H19RENDER_H */
