/*
 * H19Render -- see H19Render.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "H19Render.h"

#include "H19Terminal.h"

#include <cstring>

namespace
{

const uint32_t phosphors[H19Render::PhosphorCount] = {
    0xdde6ee,   // P4 white, faintly blue
    0x3cff7a,   // P1 green
    0xffb000,   // P3 amber
};
const uint32_t screenBlack = 0x050605;

uint32_t
scale(uint32_t rgb, unsigned num, unsigned den)
{
    uint32_t r = ((rgb >> 16) & 0xff) * num / den;
    uint32_t g = ((rgb >> 8) & 0xff) * num / den;
    uint32_t b = (rgb & 0xff) * num / den;
    return (r << 16) | (g << 8) | b;
}

}

H19Render::H19Render(): haveFont_m(false), scanlines_m(false)
{
    memset(font_m, 0, sizeof font_m);
    setPhosphor(White);
}

bool
H19Render::setFont(const uint8_t* rom, size_t size)
{
    if (!rom || size != fontSize_c)
    {
        return false;
    }
    memcpy(font_m, rom, fontSize_c);
    haveFont_m = true;
    return true;
}

uint32_t
H19Render::phosphorColour(int phosphor)
{
    return phosphors[(phosphor >= 0 && phosphor < PhosphorCount) ? phosphor : 0];
}

void
H19Render::setPhosphor(int phosphor)
{
    fg_m    = phosphorColour(phosphor);
    bg_m    = screenBlack;
    fgDim_m = scale(fg_m, 35, 100);
    bgDim_m = scale(bg_m, 35, 100);
}

void
H19Render::render(const H19Terminal& term, bool cursorOn, uint32_t* out) const
{
    const unsigned curCol   = term.cursorCol();
    const unsigned curRow   = term.cursorRow();
    const bool     showCur  = cursorOn && term.cursorVisible();
    const bool     block    = term.cursorBlock();
    const uint32_t lineFg[2] = {fg_m, scanlines_m ? fgDim_m : fg_m};
    const uint32_t lineBg[2] = {bg_m, scanlines_m ? bgDim_m : bg_m};

    for (unsigned row = 0; row < H19Terminal::rows_c; ++row)
    {
        for (unsigned ra = 0; ra < (unsigned) glyphRows_c; ++ra)
        {
            unsigned  y   = row * glyphRows_c + ra;
            uint32_t* p0  = out + (size_t) (2 * y) * width_c;
            uint32_t* p1  = p0 + width_c;

            for (unsigned col = 0; col < H19Terminal::cols_c; ++col)
            {
                uint8_t cell = term.cell(col, row);
                uint8_t bits = font_m[(cell & 0x7f) * 16 + ra];

                if (cell & 0x80)
                {
                    bits ^= 0xff;
                }
                if (showCur && row == curRow && col == curCol &&
                    (block || ra == (unsigned) glyphRows_c - 1))
                {
                    bits ^= 0xff;
                }
                for (int b = 7; b >= 0; --b)
                {
                    bool on = (bits >> b) & 1;
                    *p0++ = on ? lineFg[0] : lineBg[0];
                    *p1++ = on ? lineFg[1] : lineBg[1];
                }
            }
        }
    }
}
