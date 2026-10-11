/*
 * h19_escape -- the H19 terminal (core/machine/H19Terminal), on its own with
 * a real 8250 (VirtualH89's INS8250) on the other end of its line.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "H19Terminal.h"
#include "INS8250.h"
#include "MachineContext.h"
#include "h89keys.h"
#include "test_util.h"

#include <string>

namespace
{

struct Rig
{
    MachineContext        ctx;
    MachineContext::Scope scope{&ctx};
    INS8250               uart{nullptr, 0xe8};
    H19Terminal           term{0x8c, 0x00};

    Rig()
    {
        uart.attachDevice(&term);
        term.attachUart(&uart);
        // 9600 baud, 8N1, as MTR-90 programs it
        uart.out(0xeb, 0x80);
        uart.out(0xe8, 12);
        uart.out(0xe9, 0);
        uart.out(0xeb, 0x03);
    }

    void send(const char* s)
    {
        while (*s)
        {
            term.receiveData((BYTE) *s++);
        }
    }

    /// What the terminal transmits for whatever was posted.
    std::string drain()
    {
        std::string out;
        for (int i = 0; i < 4000; ++i)
        {
            term.notification(256);
            ctx.clock().addTicks(256);
            if (uart.in(0xed) & 0x01)
            {
                out += (char) uart.in(0xe8);
            }
        }
        return out;
    }

    std::string row(unsigned r)
    {
        std::string s;
        for (unsigned c = 0; c < H19Terminal::cols_c; ++c)
        {
            s += (char) (term.cell(c, r) & 0x7f);
        }
        while (!s.empty() && s.back() == ' ')
        {
            s.pop_back();
        }
        return s;
    }
};

}

int
main()
{
    {
        Rig r;
        r.send("HELLO\r\nWORLD");
        CHECK(r.row(0) == "HELLO");
        CHECK(r.row(1) == "WORLD");
        CHECK_EQ_INT(r.term.cursorRow(), 1);
        CHECK_EQ_INT(r.term.cursorCol(), 5);

        // direct cursor addressing: ESC Y row+32 col+32
        r.send("\033Y%(X");
        CHECK_EQ_INT(r.term.cursorRow(), 5);
        CHECK_EQ_INT(r.term.cursorCol(), 9);
        CHECK(r.row(5) == "        X");

        // reverse video sets bit 7
        r.send("\033Y  \033pR\033q");
        CHECK_EQ_INT(r.term.cell(0, 0), 0x80 | 'R');

        // graphics mode maps ^ _ ` a..~ onto the ROM's 0x7f, 0x1f, 0x00..
        r.send("\033Y  \033F`a^\033G");
        CHECK_EQ_INT(r.term.cell(0, 0), 0x00);
        CHECK_EQ_INT(r.term.cell(1, 0), 0x01);
        CHECK_EQ_INT(r.term.cell(2, 0), 0x7f);

        // erase to end of line, insert and delete line and character
        r.send("\033Y\" ABCDEF\033Y\"#\033K");
        CHECK(r.row(2) == "ABC");
        r.send("\033Y\"!\033N");
        CHECK(r.row(2) == "AC");
        r.send("\033Y\"!\033@Z\033O");
        CHECK(r.row(2) == "AZC");
        r.send("\033Y!!\033L");
        CHECK(r.row(1).empty());
        CHECK(r.row(2) == "WORLD");
        r.send("\033M");
        CHECK(r.row(1) == "WORLD");

        // clear screen
        r.send("\033E");
        CHECK(r.row(1).empty());
        CHECK_EQ_INT(r.term.cursorRow(), 0);
    }

    {
        // scrolling, and line 25 staying put
        Rig r;
        r.send("\033x1\033Y8 STATUS\033Y  ");
        CHECK(r.term.line25());
        for (int i = 0; i < 30; ++i)
        {
            char line[16];
            snprintf(line, sizeof line, "L%d\r\n", i);
            r.send(line);
        }
        CHECK(r.row(22) == "L29");
        CHECK(r.row(24) == "STATUS");
        // LF on line 25 does nothing; ESC E there clears only line 25
        r.send("\033Y8 \n\033E");
        CHECK_EQ_INT(r.term.cursorRow(), 24);
        CHECK(r.row(24).empty());
        CHECK(r.row(22) == "L29");
        // disabling line 25 erases it
        r.send("\033Y8 X\033y1");
        CHECK(!r.term.line25());
        CHECK(r.row(24).empty());
    }

    {
        // wrap at end of line, only when enabled
        Rig r;
        std::string longline(85, 'w');
        r.send(longline.c_str());
        CHECK_EQ_INT(r.term.cursorRow(), 0);
        r.send("\033E\033v");
        r.send(longline.c_str());
        CHECK_EQ_INT(r.term.cursorRow(), 1);
        CHECK(r.row(1) == "wwwww");
    }

    {
        // the keyboard
        Rig r;
        r.term.postKey('A', false);
        r.term.postKey(H89K_F1, false);
        r.term.postKey(H89K_UP, false);
        CHECK(r.drain() == "A\033S\033A");

        // the keypad: digits unshifted, functions shifted
        r.term.postKey(H89K_KP7, false);
        r.term.postKey(H89K_KP7, true);
        CHECK(r.drain() == "7\033@");

        // keypad-shifted mode swaps them; alternate keypad mode sends ESC ? x
        r.send("\033t");
        r.term.postKey(H89K_KP8, false);
        CHECK(r.drain() == "\033A");
        r.send("\033u\033=");
        r.term.postKey(H89K_KP0, false);
        r.term.postKey(H89K_KP_ENTER, false);
        r.term.postKey(H89K_KP1, true);
        CHECK(r.drain() == "\033?p\033?M\033L");

        // ESC Z identifies; ESC n reports the cursor
        r.send("\033>\033Y%*\033Z\033n");
        CHECK(r.drain() == "\033/K\033Y%*");

        // keyboard disabled
        r.send("\033}");
        r.term.postKey('Q', false);
        CHECK(r.drain().empty());
        r.send("\033{");

        // off line: keys go to the screen, not the line
        r.term.postKey(H89K_OFFLINE, false);
        r.drain();
        r.send("\033E");   // ignored off line
        r.term.postKey('Z', false);
        CHECK(r.drain().empty());
        CHECK(r.term.modes().offline);
        r.term.postKey(H89K_OFFLINE, false);
        r.drain();

        // BREAK reaches the 8250 as a break
        r.term.postKey(H89K_BREAK, false);
        r.term.notification(1);
        BYTE lsr = r.uart.in(0xed);
        CHECK(lsr & 0x10);
        r.uart.in(0xe8);

        // a paste is typed at the line rate, CR for newlines
        r.term.postText("dir\nok");
        CHECK(r.drain() == "dir\rok");

        // key clicks: on by default (SW402 bit 1 clear), off with ESC x2
        r.term.takeClicks();
        r.term.postKey('x', false);
        r.drain();
        CHECK_EQ_INT(r.term.takeClicks(), 1);
        r.send("\033x2");
        r.term.postKey('x', false);
        r.drain();
        CHECK_EQ_INT(r.term.takeClicks(), 0);

        // the bell
        r.send("\007\007");
        CHECK_EQ_INT(r.term.takeBells(), 2);
    }

    {
        // ANSI mode (ESC <): the manual's CSI sequences
        Rig r;
        r.send("\033<");
        CHECK(r.term.modes().ansi);
        r.send("\033[5;10HX");                 // CUP
        CHECK_EQ_INT(r.term.cursorRow(), 4);
        CHECK(r.row(4) == "         X");
        r.send("\033[H\033[2B\033[3C*");      // home, down 2, right 3
        CHECK(r.row(2) == "   *");
        r.send("\033[A\033[D\033[7mR\033[m"); // up, left; reverse on and off
        CHECK_EQ_INT(r.term.cell(3, 1), 0x80 | 'R');
        r.send("\033[10ma\033[11m");          // graphics
        CHECK_EQ_INT(r.term.cell(4, 1), 0x01);
        r.send("\033[3;1HABCDEF\033[3;3H\033[K"); // EL to end
        CHECK(r.row(2) == "AB");
        r.send("\033[3;1H\033[P");            // DCH
        CHECK(r.row(2) == "B");
        r.send("\033[3;1H\033[4hZ\033[4l");   // IRM
        CHECK(r.row(2) == "ZB");
        r.send("\033[3;1H\033[2L");           // IL 2
        CHECK(r.row(2).empty() && r.row(3).empty());
        CHECK(r.row(4) == "ZB");
        r.send("\033[2M");                     // DL 2
        CHECK(r.row(2) == "ZB");
        r.send("\033[7;20H\033[s\033[H\033[u\033[6n"); // save, restore, CPR
        CHECK(r.drain() == "\033[7;20R");
        r.send("\033[>5h");                    // Heath mode 5: cursor off
        CHECK(!r.term.cursorVisible());
        r.send("\033[>5;1l");
        CHECK(r.term.cursorVisible());
        r.send("\033[30;99H");                 // a line too high stays; column clamps
        CHECK_EQ_INT(r.term.cursorRow(), 6);
        CHECK_EQ_INT(r.term.cursorCol(), 79);
        r.send("\033Y  ");                     // Heath DCA is not ANSI: ignored
        CHECK_EQ_INT(r.term.cursorRow(), 6);

        // the keys' ANSI codes
        r.term.postKey(H89K_F1, false);
        r.term.postKey(H89K_UP, false);
        r.term.postKey(H89K_ERASE, true);
        r.send("\033=");
        r.term.postKey(H89K_KP5, false);
        CHECK(r.drain() == "\033OS\033[A\033[2J\033Ou");

        // ED 2 clears and homes; ESC [ ? 2 h goes back to Heath mode
        r.send("\033[2J");
        CHECK(r.row(2).empty());
        CHECK_EQ_INT(r.term.cursorRow(), 0);
        r.send("\033[?2h\033Y%(X");
        CHECK(!r.term.modes().ansi);
        CHECK(r.row(5) == "        X");
    }

    {
        // Heath keys: SHIFT+ERASE clears, IC toggles insert character mode
        Rig r;
        r.term.postKey(H89K_ERASE, false);
        r.term.postKey(H89K_ERASE, true);
        r.term.postKey(H89K_IC, false);
        CHECK(r.drain() == "\033J\033E\033@");
        r.send("\033@");
        r.term.postKey(H89K_IC, false);
        CHECK(r.drain() == "\033O");
    }

    {
        // hold-screen mode: the line holds before a scroll, with XOFF; SCROLL
        // lets one line through, SHIFT+SCROLL a page, then XON
        Rig r;
        r.send("\033[");
        CHECK(r.term.modes().holdScreen);
        for (int i = 0; i < 24; ++i)
        {
            char line[16];
            snprintf(line, sizeof line, "%sL%d", i ? "\r\n" : "", i);
            r.send(line);
        }
        CHECK(r.row(23) == "L23");
        r.send("\r\nL24\r\nL25");          // the LF after L23 would scroll
        CHECK(r.row(0) == "L0");
        CHECK(r.term.modes().held > 0);
        CHECK(r.drain() == "\023");            // XOFF
        r.term.postKey(H89K_SCROLL, false);    // one line: L24 in, then held again
        CHECK(r.drain().empty());
        CHECK(r.row(0) == "L1");
        CHECK(r.row(23) == "L24");
        CHECK(r.term.modes().held > 0);
        r.term.postKey(H89K_SCROLL, true);     // a page: the rest, then XON
        CHECK(r.drain() == "\021");
        CHECK(r.row(23) == "L25");
        CHECK_EQ_INT((int) r.term.modes().held, 0);
        r.send("\033\\");                    // ESC \ : out of hold-screen mode
        CHECK(!r.term.modes().holdScreen);
    }

    {
        // screen text, graphics characters as Unicode
        Rig r;
        r.send("AB\033Fa\033G");
        std::string t = r.term.screenText(false);
        CHECK(t.rfind("AB─", 0) == 0);
    }

    return t_done("h19_escape");
}
