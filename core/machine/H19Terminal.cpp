/*
 * H19Terminal -- see H19Terminal.h.
 *
 * The escape-sequence interpreter below is VirtualH89's (h19.cpp,
 * BSD-3-Clause, Copyright (c) 2009-2016 Mark Garlanger), kept close to the
 * original so its comments from the H19 ROM listing still apply.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "H19Terminal.h"

#include "INS8250.h"
#include "ascii.h"

#include <cstdio>
#include <cstring>

namespace
{

/// SW401 bits 0-3 -> baud (MAME's table; 0 and 14/15 are not valid settings)
const unsigned baudTable[16] = {
    9600, 110, 150, 300, 600, 1200, 1800, 2000,
    2400, 3600, 4800, 7200, 9600, 19200, 9600, 9600,
};

const unsigned deviceHz_c = 2048000;

}

H19Terminal::H19Terminal(BYTE sw401, BYTE sw402): uart_m(nullptr),
                                                  generation_m(0),
                                                  sw401_m(sw401),
                                                  sw402_m(sw402),
                                                  ansiMode_m(false),
                                                  csiPrivate_m(0),
                                                  csiCount_m(0),
                                                  scrollCredit_m(0),
                                                  blocked_m(false),
                                                  countdown_m(0),
                                                  keyHead_m(0),
                                                  keyTail_m(0),
                                                  textPos_m(0),
                                                  textPending_m(false),
                                                  bells_m(0),
                                                  clicks_m(0)
{
    for (auto& k : keyRing_m)
    {
        k.store(0, std::memory_order_relaxed);
    }
    offline_m = false;
    powerOn();
}

H19Terminal::~H19Terminal()
{
}

void
H19Terminal::attachUart(INS8250* uart)
{
    uart_m = uart;
}

void
H19Terminal::setSW401(BYTE sw401)
{
    // ------------------------
    // | Bit  | Meaning       |
    // +======+===============+
    // |   7  | Duplex        |
    // |   6  | Stick Parity  |
    // |   5  | Even Parity   |
    // |   4  | Parity        |
    // |  3-0 | Baud Rate     |
    // +======+===============+
    sw401_m      = sw401;
    baud_m       = baudTable[sw401 & 0x0f];
    // start bit, 7 or 8 data bits, parity, stop: ten bit times
    charCycles_m = deviceHz_c * 10 / baud_m;
}

void
H19Terminal::setSW402(BYTE sw402)
{
    // ------------------------
    // | Bit  | Meaning       |
    // +======+===============+
    // |   7  | Refresh Freq  |
    // |   6  | Keypad shifted|
    // |   5  | Zenith/ANSI   |
    // |   4  | Auto CR on LF |
    // |   3  | Auto LF on CR |
    // |   2  | Wrap at EOL   |
    // |   1  | NO key click  |
    // |   0  | Block cursor  |
    // +======+===============+
    sw402_m         = sw402;
    keypadShifted_m = (sw402 & 0x40) != 0;
    ansiMode_m      = (sw402 & 0x20) != 0;
    autoCR_m        = (sw402 & 0x10) != 0;
    autoLF_m        = (sw402 & 0x08) != 0;
    wrapEOL_m       = (sw402 & 0x04) != 0;
    noKeyClick_m    = (sw402 & 0x02) != 0;
    cursorBlock_m   = (sw402 & 0x01) != 0;
}

void
H19Terminal::powerOn()
{
    mode_m            = Normal;
    reverseVideo_m    = false;
    graphicMode_m     = false;
    insertMode_m      = false;
    line25_m          = false;
    holdScreen_m      = false;
    cursorOff_m       = false;
    altKeypadMode_m   = false;
    keyboardEnabled_m = true;
    csiPrivate_m      = 0;
    csiCount_m        = 0;
    held_m.clear();
    scrollCredit_m    = 0;
    blocked_m         = false;

    setSW401(sw401_m);
    setSW402(sw402_m);

    posX_m  = 0;
    posY_m  = 0;
    saveX_m = 0;
    saveY_m = 0;

    for (unsigned y = 0; y < rows_c; ++y)
    {
        eraseLine(y);
    }
    touched();
}

unsigned int
H19Terminal::getBaudRate()
{
    return baud_m;
}

// ---------------------------------------------------------------------------
// The keyboard

void
H19Terminal::postKey(unsigned code, bool shift)
{
    unsigned head = keyHead_m.load(std::memory_order_relaxed);
    unsigned next = (head + 1) % keyRing_c;
    if (next == keyTail_m.load(std::memory_order_acquire))
    {
        return; // full: a real H19 would drop it too
    }
    keyRing_m[head].store((code & 0xffff) | (shift ? 0x10000u : 0u), std::memory_order_relaxed);
    keyHead_m.store(next, std::memory_order_release);
}

size_t
H19Terminal::postText(const std::string& text)
{
    std::string ascii;
    ascii.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = (unsigned char) text[i];
        if (c == '\r')
        {
            ascii += '\r';
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                ++i;
            }
        }
        else if (c == '\n')
        {
            ascii += '\r';
        }
        else if (c == '\t' || (c >= 0x20 && c < 0x7f))
        {
            ascii += (char) c;
        }
        else if (c >= 0xc0)
        {
            // a non-ASCII character: skip its continuation bytes, type '?'
            while (i + 1 < text.size() && ((unsigned char) text[i + 1] & 0xc0) == 0x80)
            {
                ++i;
            }
            ascii += '?';
        }
    }

    std::lock_guard<std::mutex> g(textLock_m);
    if (textPos_m >= text_m.size())
    {
        text_m.clear();
        textPos_m = 0;
    }
    text_m += ascii;
    textPending_m.store(!text_m.empty());
    return ascii.size();
}

void
H19Terminal::cancelText()
{
    std::lock_guard<std::mutex> g(textLock_m);
    text_m.clear();
    textPos_m = 0;
    textPending_m.store(false);
}

bool
H19Terminal::textPending()
{
    return textPending_m.load();
}

void
H19Terminal::setOffline(bool off)
{
    offline_m = off;
    touched();
}

void
H19Terminal::queueByte(BYTE b)
{
    toSend_m.push_back(b);
}

void
H19Terminal::sendSeq(const char* s)
{
    while (*s)
    {
        queueByte((BYTE) *s++);
    }
}

void
H19Terminal::encodeKey(unsigned code, bool shift)
{
    if (!noKeyClick_m)
    {
        clicks_m.fetch_add(1);
    }

    switch (code)
    {
        case H89K_OFFLINE:
            setOffline(!offline_m);
            return;
        case H89K_SCROLL:
            // hold-screen mode: one more line, or (shifted) a page
            if (holdScreen_m)
            {
                scrollCredit_m += shift ? rowsMain_c : 1;
                if (!held_m.empty())
                {
                    releaseHeld();
                }
            }
            return;
        case H89K_BREAK:
            if (uart_m && !offline_m)
            {
                uart_m->receiveBreak();
            }
            return;
        case H89K_RESET:
            // handled by the host (it resets the whole machine)
            return;
        default:
            break;
    }

    if (!keyboardEnabled_m && !offline_m)
    {
        return;
    }

    char seq[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    if (code < 0x80)
    {
        seq[0] = (char) code;
    }
    else if (code >= H89K_KP0 && code <= H89K_KP_ENTER)
    {
        // The keypad. Shift (the key, or keypad-shifted mode, which inverts
        // it) gives the editing functions; alternate keypad mode gives
        // ESC ? x for the unshifted keys.
        static const char digits[]   = "0123456789.\r";
        static const char altCodes[] = "pqrstuvwxynM";
        static const unsigned funcs[] = {
            0, H89K_IL, H89K_DOWN, H89K_DL, H89K_LEFT, H89K_HOME,
            H89K_RIGHT, H89K_IC, H89K_UP, H89K_DC, 0, 0,
        };
        unsigned k       = code - H89K_KP0;
        bool     shifted = shift != keypadShifted_m;

        if (shifted && funcs[k])
        {
            encodeKey(funcs[k], false);
            return;
        }
        if (altKeypadMode_m && !shifted)
        {
            seq[0] = ascii::ESC;
            seq[1] = ansiMode_m ? 'O' : '?';
            seq[2] = altCodes[k];
        }
        else
        {
            seq[0] = digits[k];
        }
    }
    else
    {
        // Heath: ESC x. ANSI: the function keys are ESC O x (the manual's
        // table); ERASE and SHIFT+ERASE are annotated on ESC [ J and
        // ESC [ 2 J in its ANSI summary, and the editing keys send their
        // functions' ANSI sequences likewise (the manual tabulates only the
        // Heath codes for the shifted keypad -- which the terminal itself,
        // in ANSI mode, would not act on off line).
        static const struct { unsigned code; const char* heath; const char* ansi; } keys[] = {
            {H89K_F1, "\033S", "\033OS"}, {H89K_F2, "\033T", "\033OT"},
            {H89K_F3, "\033U", "\033OU"}, {H89K_F4, "\033V", "\033OV"},
            {H89K_F5, "\033W", "\033OW"}, {H89K_BLUE, "\033P", "\033OP"},
            {H89K_RED, "\033Q", "\033OQ"}, {H89K_GRAY, "\033R", "\033OR"},
            {H89K_UP, "\033A", "\033[A"}, {H89K_DOWN, "\033B", "\033[B"},
            {H89K_RIGHT, "\033C", "\033[C"}, {H89K_LEFT, "\033D", "\033[D"},
            {H89K_HOME, "\033H", "\033[H"}, {H89K_DC, "\033N", "\033[P"},
            {H89K_IL, "\033L", "\033[L"}, {H89K_DL, "\033M", "\033[M"},
        };
        const char* out = nullptr;
        if (code == H89K_LINEFEED)
        {
            out = "\n";
        }
        else if (code == H89K_ERASE)
        {
            out = shift ? (ansiMode_m ? "\033[2J" : "\033E") : (ansiMode_m ? "\033[J" : "\033J");
        }
        else if (code == H89K_IC)
        {
            // IC enters insert character mode, and leaves it again
            out = insertMode_m ? (ansiMode_m ? "\033[4l" : "\033O") : (ansiMode_m ? "\033[4h" : "\033@");
        }
        for (const auto& e : keys)
        {
            if (e.code == code)
            {
                out = ansiMode_m ? e.ansi : e.heath;
            }
        }
        if (!out)
        {
            return;
        }
        snprintf(seq, sizeof seq, "%s", out);
    }

    if (offline_m)
    {
        // local mode: the terminal talks to itself
        for (char* p = seq; *p; ++p)
        {
            processCharacter((BYTE) *p);
        }
        if (seq[0] == 0)
        {
            processCharacter(0);
        }
        return;
    }

    if (seq[0] == 0)
    {
        queueByte(0); // CTRL-@
        return;
    }
    sendSeq(seq);
}

void
H19Terminal::notification(unsigned int cycleCount)
{
    // the keyboard first: whatever was pressed is encoded now, on the
    // emulator thread, against the terminal's current modes
    unsigned tail = keyTail_m.load(std::memory_order_relaxed);
    while (tail != keyHead_m.load(std::memory_order_acquire))
    {
        uint32_t ev = keyRing_m[tail].load(std::memory_order_relaxed);
        tail = (tail + 1) % keyRing_c;
        keyTail_m.store(tail, std::memory_order_release);
        encodeKey(ev & 0xffff, (ev & 0x10000) != 0);
    }

    if (countdown_m > cycleCount)
    {
        countdown_m -= cycleCount;
        return;
    }
    countdown_m = 0;

    if (toSend_m.empty() && textPending_m.load(std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> g(textLock_m);
        if (textPos_m < text_m.size())
        {
            if (offline_m)
            {
                processCharacter((BYTE) text_m[textPos_m]);
            }
            else
            {
                queueByte((BYTE) text_m[textPos_m]);
            }
            ++textPos_m;
        }
        if (textPos_m >= text_m.size())
        {
            text_m.clear();
            textPos_m = 0;
            textPending_m.store(false);
        }
    }

    // one character per character time, and only into an empty receiver
    if (!toSend_m.empty() && sendReady())
    {
        sendData(toSend_m.front());
        toSend_m.pop_front();
        countdown_m = charCycles_m;
    }
}

// ---------------------------------------------------------------------------
// From the H89

void
H19Terminal::receiveData(BYTE ch)
{
    if (offline_m)
    {
        return;
    }
    // hold-screen mode is holding the line: this waits behind the rest (a
    // host that ignores the XOFF loses what overflows, as on the terminal)
    if (!held_m.empty())
    {
        if (held_m.size() < heldMax_c)
        {
            held_m.push_back(ch);
        }
        return;
    }
    blocked_m = false;
    processCharacter(ch);
    if (blocked_m)
    {
        held_m.push_back(ch);
        queueByte(ascii::DC3);   // XOFF
    }
}

bool
H19Terminal::mayScroll()
{
    // keys typed off line are the terminal talking to itself: never held
    if (!holdScreen_m || offline_m)
    {
        return true;
    }
    if (scrollCredit_m > 0)
    {
        --scrollCredit_m;
        return true;
    }
    blocked_m = true;
    return false;
}

void
H19Terminal::releaseHeld()
{
    while (!held_m.empty())
    {
        blocked_m = false;
        // a character held part-done (CR with auto LF) is processed again
        // from the start: what it did already does not change for that
        processCharacter(held_m.front());
        if (blocked_m)
        {
            return;   // the next SCROLL
        }
        held_m.pop_front();
    }
    queueByte(ascii::DC1);   // XON
}

void
H19Terminal::processCharacter(BYTE ch)
{
    // mask off the high bit just in case, the real H19 would not have it set.
    ch &= 0x7f;

    if (mode_m == Normal)
    {
        switch (ch)
        {
            case ascii::NUL:
            case ascii::SOH:
            case ascii::STX:
            case ascii::ETX:
            case ascii::EOT:
            case ascii::ENQ:
            case ascii::ACK:
            case ascii::VT:
            case ascii::FF:
            case ascii::SO:
            case ascii::SI:
            case ascii::DLE:
            case ascii::DC1:
            case ascii::DC2:
            case ascii::DC3:
            case ascii::DC4:
            case ascii::NAK:
            case ascii::SYN:
            case ascii::ETB:
            case ascii::EM:
            case ascii::SUB:
            case ascii::FS:
            case ascii::GS:
            case ascii::RS:
            case ascii::US:
            case ascii::DEL:
                // From manual, these characters are not processed by the terminal
                break;

            case ascii::BEL: // Rings the bell.
                bell();
                break;

            case ascii::BS: // Backspace
                processBS();
                break;

            case ascii::HT: // Horizontal Tab
                processTAB();
                break;

            case ascii::LF: // Line Feed
                processLF();
                if (autoCR_m)
                {
                    processCR();
                }
                break;

            case ascii::CR: // Carriage Return
                processCR();
                if (autoLF_m)
                {
                    processLF();
                }
                break;

            case ascii::CAN: // Cancel.
                break;

            case ascii::ESC: // Escape
                mode_m = Escape;
                break;

            default:
                displayCharacter(ch);
                break;
        }
    }
    else if (mode_m == Escape)
    {
        if (ansiMode_m)
        {
            processAnsiEscape(ch);
            return;
        }
        // Assume we go back to Normal, so only the few that don't need to set the mode.
        mode_m = Normal;

        switch (ch)
        {
            case ascii::CAN: // CAN - Cancel
                break;
            case ascii::ESC: // Escape
                // From the ROM listing, stay in this mode.
                mode_m = Escape;
                break;

            // Cursor Functions
            case 'H': // Cursor Home
                posX_m = posY_m = 0;
                touched();
                break;
            case 'C': // Cursor Forward
                cursorForward();
                break;
            case 'D':        // Cursor Backward
                processBS(); // same processing as cursor backward
                break;
            case 'B': // Cursor Down
                cursorDown();
                break;
            case 'A': // Cursor Up
                cursorUp();
                break;
            case 'I': // Reverse Index
                reverseIndex();
                break;
            case 'n': // Cursor Position Report
                cursorPositionReport();
                break;
            case 'j': // Save cursor position
                saveX_m = posX_m;
                saveY_m = posY_m;
                break;
            case 'k': // Restore cursor position
                posX_m = saveX_m;
                posY_m = saveY_m;
                touched();
                break;
            case 'Y': // Direct Cursor Addressing
                mode_m = DCA_1;
                break;

            // Erase and Editing
            case 'E': // Clear Display
                clearDisplay();
                break;
            case 'b': // Erase Beginning of Display
                eraseBOD();
                break;
            case 'J': // Erase to End of Page
                eraseEOP();
                break;
            case 'l': // Erase entire Line
                eraseEL();
                break;
            case 'o': // Erase Beginning Of Line
                eraseBOL();
                break;
            case 'K': // Erase To End Of Line
                eraseEOL();
                break;
            case 'L': // Insert Line
                insertLine();
                break;
            case 'M': // Delete Line
                deleteLine();
                break;
            case 'N': // Delete Character
                deleteChar();
                break;
            case '@': // Enter Insert Character Mode
                insertMode_m = true;
                break;
            case 'O': // Exit Insert Character Mode
                insertMode_m = false;
                break;

            // Configuration
            case 'z': // Reset To Power-Up Configuration
                powerOn();
                break;
            case 'r': // Modify the Baud Rate (the next character)
                break;
            case 'x': // Set Mode
                mode_m = SetMode;
                break;
            case 'y': // Reset Mode
                mode_m = ResetMode;
                break;
            case '<': // Enter ANSI Mode
                ansiMode_m = true;
                break;

            // Modes of operation
            case '[': // Enter Hold Screen Mode
                setHeathMode(3, true);
                break;
            case '\\': // Exit Hold Screen Mode
                setHeathMode(3, false);
                break;
            case 'p': // Enter Reverse Video Mode
                reverseVideo_m = true;
                break;
            case 'q': // Exit Reverse Video Mode
                reverseVideo_m = false;
                break;
            case 'F': // Enter Graphics Mode
                graphicMode_m = true;
                break;
            case 'G': // Exit Graphics Mode
                graphicMode_m = false;
                break;
            case 't': // Enter Keypad Shifted Mode
                keypadShifted_m = true;
                break;
            case 'u': // Exit Keypad Shifted Mode
                keypadShifted_m = false;
                break;
            case '=': // Enter Alternate Keypad Mode
                altKeypadMode_m = true;
                break;
            case '>': // Exit Alternate Keypad Mode
                altKeypadMode_m = false;
                break;

            // Additional Functions
            case '}': // Keyboard Disable
                keyboardEnabled_m = false;
                break;
            case '{': // Keyboard Enable
                keyboardEnabled_m = true;
                break;
            case 'v': // Wrap Around at End Of Line
                wrapEOL_m = true;
                break;
            case 'w': // Discard At End Of Line
                wrapEOL_m = false;
                break;
            case 'Z': // Identify as VT52 (Data: ESC / K)
                sendSeq("\033/K");
                break;
            case ']': // Transmit 25th Line
                transmitLine25();
                break;
            case '#': // Transmit Page
                transmitPage();
                break;

            default:
                break;
        }
    }
    else if (mode_m == SetMode || mode_m == ResetMode)
    {
        bool on = mode_m == SetMode;
        mode_m  = Normal;
        if (ch >= '1' && ch <= '9')
        {
            setHeathMode(ch - '0', on);
        }
    }
    else if (mode_m == Csi)
    {
        processCsi(ch);
    }
    else if (mode_m == DCA_1)
    {
        // From actual H19, once the line is specified, the cursor
        // immediately moves to that line, so no need to save the
        // position and wait for the entire command.
        if (ch == ascii::CAN)
        {
            mode_m = Normal;
        }
        else
        {
            int pos = ch - 31;

            if (((pos > 0) && (pos < (signed) rows_c)) || ((pos == (signed) rows_c) && line25_m))
            {
                posY_m = pos - 1;
            }
            mode_m = DCA_2;
        }
    }
    else if (mode_m == DCA_2)
    {
        if (ch == ascii::CAN)
        {
            mode_m = Normal;
        }
        else
        {
            int pos = ch - 31;

            if ((pos > 0) && (pos < 81))
            {
                posX_m = pos - 1;
            }
            else
            {
                posX_m = cols_c - 1;
            }
            touched();
            mode_m = Normal;
        }
    }
}

/// ESC x Ps / ESC y Ps (Heath) and ESC [ > Ps h / l (ANSI): modes 1-9.
void
H19Terminal::setHeathMode(int n, bool on)
{
    switch (n)
    {
        case 1: // 25th line
            if (!on)
            {
                eraseLine(rowsMain_c);
                if (posY_m == rowsMain_c)
                {
                    posY_m = rowsMain_c - 1;
                }
            }
            line25_m = on;
            touched();
            break;
        case 2: // no key click
            noKeyClick_m = on;
            break;
        case 3: // hold screen: it holds from the next scroll on
            if (on && !holdScreen_m)
            {
                scrollCredit_m = 0;
            }
            holdScreen_m = on;
            break;
        case 4: // block cursor
            cursorBlock_m = on;
            touched();
            break;
        case 5: // cursor off
            cursorOff_m = on;
            touched();
            break;
        case 6: // keypad shifted
            keypadShifted_m = on;
            break;
        case 7: // alternate keypad
            altKeypadMode_m = on;
            break;
        case 8: // auto LF on CR
            autoLF_m = on;
            break;
        case 9: // auto CR on LF
            autoCR_m = on;
            break;
        default:
            break;
    }
}

/// ESC in ANSI mode: the terminal answers only ANSI's syntax -- CSI, RI and
/// the keypad modes.
void
H19Terminal::processAnsiEscape(BYTE ch)
{
    mode_m = Normal;
    switch (ch)
    {
        case ascii::ESC:
            mode_m = Escape;
            break;
        case '[': // CSI
            mode_m       = Csi;
            csiPrivate_m = 0;
            csiCount_m   = 0;
            break;
        case 'M': // RI: Reverse Index
            reverseIndex();
            break;
        case '=': // Enter Alternate Keypad Mode
            altKeypadMode_m = true;
            break;
        case '>': // Exit Alternate Keypad Mode
            altKeypadMode_m = false;
            break;
        default:
            break;
    }
}

void
H19Terminal::processCsi(BYTE ch)
{
    if (ch == ascii::CAN)
    {
        mode_m = Normal;
        return;
    }
    if (ch == ascii::ESC)
    {
        mode_m = Escape;
        return;
    }
    if (ch >= '0' && ch <= '9')
    {
        if (csiCount_m == 0)
        {
            csiParam_m[csiCount_m++] = 0;
        }
        int& p = csiParam_m[csiCount_m - 1];
        if (p < 10000)
        {
            p = p * 10 + (ch - '0');
        }
        return;
    }
    if (ch == ';')
    {
        if (csiCount_m == 0)
        {
            csiParam_m[csiCount_m++] = 0;
        }
        if (csiCount_m < csiMax_c)
        {
            csiParam_m[csiCount_m++] = 0;
        }
        return;
    }
    if ((ch == '>' || ch == '?') && csiCount_m == 0 && !csiPrivate_m)
    {
        csiPrivate_m = (char) ch;
        return;
    }
    mode_m = Normal;
    if (ch >= 0x40 && ch <= 0x7e)
    {
        executeCsi(ch);
    }
    // anything else is not a sequence the H19 knows: dropped
}

/// Parameter i; absent or zero is the default (the manual's note 2).
int
H19Terminal::csiParam(unsigned i, int def) const
{
    return (i < csiCount_m && csiParam_m[i] != 0) ? csiParam_m[i] : def;
}

void
H19Terminal::executeCsi(BYTE final)
{
    const int n = csiParam(0, 1);

    if (csiPrivate_m == '>')
    {
        // SM / RM: the Heath modes
        if (final == 'h' || final == 'l')
        {
            for (unsigned i = 0; i < csiCount_m; ++i)
            {
                setHeathMode(csiParam_m[i], final == 'h');
            }
        }
        return;
    }
    if (csiPrivate_m == '?')
    {
        for (unsigned i = 0; i < csiCount_m; ++i)
        {
            if (csiParam_m[i] == 2 && final == 'h')
            {
                ansiMode_m = false;   // PEHM: Enter Heath Mode
            }
            else if (csiParam_m[i] == 7 && (final == 'h' || final == 'l'))
            {
                wrapEOL_m = final == 'h';   // wrap around / discard at end of line
            }
        }
        return;
    }

    switch (final)
    {
        case 'H': // CUP
        case 'f': // HVP
        {
            // a line too high leaves the cursor's line alone; a column too
            // high is the end of the line. Only this reaches line 25.
            int row = csiParam(0, 1);
            int col = csiParam(1, 1);
            if (row <= (int) rowsMain_c)
            {
                posY_m = (unsigned) row - 1;
            }
            else if (row == (int) rows_c && line25_m)
            {
                posY_m = rows_c - 1;
            }
            posX_m = col > (int) cols_c ? cols_c - 1 : (unsigned) col - 1;
            touched();
            break;
        }
        case 'A': // CUU
            for (int i = 0; i < n; ++i)
            {
                cursorUp();
            }
            break;
        case 'B': // CUD
            for (int i = 0; i < n; ++i)
            {
                cursorDown();
            }
            break;
        case 'C': // CUF
            for (int i = 0; i < n; ++i)
            {
                cursorForward();
            }
            break;
        case 'D': // CUB
            for (int i = 0; i < n; ++i)
            {
                processBS();
            }
            break;
        case 'J': // ED
            switch (csiParam(0, 0))
            {
                case 0:
                    eraseEOP();
                    break;
                case 1:
                    eraseBOD();
                    break;
                case 2:
                    clearDisplay();
                    break;
                default:
                    break;
            }
            break;
        case 'K': // EL
            switch (csiParam(0, 0))
            {
                case 0:
                    eraseEOL();
                    break;
                case 1:
                    eraseBOL();
                    break;
                case 2:
                    eraseEL();
                    break;
                default:
                    break;
            }
            break;
        case 'L': // IL
            for (int i = 0; i < n; ++i)
            {
                insertLine();
            }
            break;
        case 'M': // DL
            for (int i = 0; i < n; ++i)
            {
                deleteLine();
            }
            break;
        case 'P': // DCH
            for (int i = 0; i < n; ++i)
            {
                deleteChar();
            }
            break;
        case 'h': // SM
        case 'l': // RM
            for (unsigned i = 0; i < csiCount_m; ++i)
            {
                switch (csiParam_m[i])
                {
                    case 2: // KAM: keyboard disabled
                        keyboardEnabled_m = final == 'l';
                        break;
                    case 4: // IRM: insert character mode
                        insertMode_m = final == 'h';
                        break;
                    case 20: // LNM: new line mode (auto LF on CR)
                        autoLF_m = final == 'h';
                        break;
                    default:
                        break;
                }
            }
            break;
        case 'm': // SGR
            if (csiCount_m == 0)
            {
                reverseVideo_m = false;
            }
            for (unsigned i = 0; i < csiCount_m; ++i)
            {
                switch (csiParam_m[i])
                {
                    case 0:
                        reverseVideo_m = false;
                        break;
                    case 7:
                        reverseVideo_m = true;
                        break;
                    case 10:
                        graphicMode_m = true;
                        break;
                    case 11:
                        graphicMode_m = false;
                        break;
                    default:
                        break;
                }
            }
            break;
        case 'n': // CPR: ESC [ Pl ; Pc R
            if (csiParam(0, 0) == 6)
            {
                char rep[24];
                snprintf(rep, sizeof rep, "\033[%u;%uR", posY_m + 1, cursorCol() + 1);
                sendSeq(rep);
            }
            break;
        case 's': // PSCP
            saveX_m = posX_m;
            saveY_m = posY_m;
            break;
        case 'u': // PRCP
            posX_m = saveX_m;
            posY_m = saveY_m;
            touched();
            break;
        case 'z': // PRAM: Reset to Power-Up Configuration
            powerOn();
            break;
        case 'r': // PMBR: the baud rate (as ESC r: not emulated)
            break;
        case 'p': // PXMT: Transmit Page
            transmitPage();
            break;
        case 'q': // PX25: Transmit 25th Line
            transmitLine25();
            break;
        default:
            break;
    }
}

void
H19Terminal::displayCharacter(unsigned ch)
{
    // when in graphic mode, use this lookup table to determine character to display,
    // note: although entries 0 to 31 are defined, they are not used, since the control
    // characters are specifically checked in the switch statement.
    static const unsigned char graphicLookup[0x80] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,  31,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,  47,
        48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,  63,
        64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,  79,
        80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 127, 31,
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,  32
    };
    unsigned symbol = graphicMode_m ? graphicLookup[ch & 0x7f] : (ch & 0x7f);

    if (reverseVideo_m)
    {
        // set the high-bit for reverse video.
        symbol |= 0x80;
    }

    if (!((posX_m <= cols_c) && (posY_m < rows_c)))
    {
        posX_m = 0;
        posY_m = 0;
    }

    if (posX_m >= cols_c)
    {
        if (wrapEOL_m)
        {
            if (posY_m == (rowsMain_c - 1) && !mayScroll())
            {
                return;
            }
            posX_m = 0;

            if (posY_m < (rowsMain_c - 1))
            {
                posY_m++;
            }
            else if (posY_m == (rowsMain_c - 1))
            {
                scroll();
            }
            // On a real H19, on line 25 it just wraps back to column 0.
        }
        else
        {
            posX_m = cols_c - 1;
        }
    }

    if (insertMode_m)
    {
        for (unsigned x = cols_c - 1; x > posX_m; --x)
        {
            screen_m[posY_m][x] = screen_m[posY_m][x - 1];
        }
    }

    screen_m[posY_m][posX_m] = (uint8_t) symbol;
    posX_m++;
    touched();
}

void
H19Terminal::scroll()
{
    memmove(&screen_m[0][0], &screen_m[1][0], (rowsMain_c - 1) * cols_c);
    eraseLine(rowsMain_c - 1);
}

void
H19Terminal::processCR()
{
    if (posX_m)
    {
        posX_m = 0;
        touched();
    }
}

void
H19Terminal::processLF()
{
    if (onLine25())
    {
        // On line 25, don't do anything
        return;
    }

    // Determine if we can just move down a row, or if we have to scroll.
    if (posY_m < (rowsMain_c - 1))
    {
        ++posY_m;
    }
    else
    {
        if (!mayScroll())
        {
            return;
        }
        scroll();
    }
    touched();
}

void
H19Terminal::processBS()
{
    if (posX_m)
    {
        if (posX_m >= cols_c)
        {
            posX_m = cols_c - 1;
        }
        --posX_m;
        touched();
    }
}

void
H19Terminal::processTAB()
{
    if (posX_m < 72)
    {
        posX_m += 8;
        // mask off the lower 3 bits to get the correct column.
        posX_m &= 0xf8;
        touched();
    }
    else if (posX_m < (cols_c - 1))
    {
        posX_m++;
        touched();
    }
}

void
H19Terminal::cursorForward()
{
    // ROM comment says that it will wrap around when at pos 80, but the code does not do that,
    // and verifying on a real H89, even with wrap-around enabled, the cursor will not wrap.
    if (posX_m < (cols_c - 1))
    {
        ++posX_m;
        touched();
    }
}

void
H19Terminal::cursorDown()
{
    // ROM - Moves the cursor down one line on the display but does not cause a scroll past
    // the last line
    if (posY_m < (rowsMain_c - 1))
    {
        ++posY_m;
        touched();
    }
}

void
H19Terminal::cursorUp()
{
    if (posY_m && !onLine25())
    {
        --posY_m;
        touched();
    }
}

void
H19Terminal::reverseIndex()
{
    if (onLine25())
    {
        return;
    }

    if (posY_m)
    {
        --posY_m;
    }
    else
    {
        // must be line 0 - have to scroll down.
        memmove(&screen_m[1][0], &screen_m[0][0], (rowsMain_c - 1) * cols_c);
        eraseLine(0);
    }
    touched();
}

void
H19Terminal::cursorPositionReport()
{
    // Send ESC Y <posY_m+0x20> <posX_m+0x20>
    queueByte(ascii::ESC);
    queueByte('Y');
    queueByte((BYTE) (posY_m + 0x20));
    queueByte((BYTE) (cursorCol() + 0x20));
}

void
H19Terminal::clearDisplay()
{
    // if on line 25, then only erase line 25
    if (onLine25())
    {
        eraseEL();
    }
    else
    {
        for (unsigned y = 0; y < rowsMain_c; ++y)
        {
            eraseLine(y);
        }
        posX_m = 0;
        posY_m = 0;
    }
    touched();
}

void
H19Terminal::eraseBOD()
{
    eraseBOL();

    // if on line 25 just erase to beginning of line
    if (!onLine25())
    {
        for (int y = (int) posY_m - 1; y >= 0; --y)
        {
            eraseLine((unsigned) y);
        }
    }
    touched();
}

void
H19Terminal::eraseEOP()
{
    eraseEOL();

    for (unsigned y = posY_m + 1; y < rowsMain_c; ++y)
    {
        eraseLine(y);
    }
    touched();
}

void
H19Terminal::eraseEL()
{
    eraseLine(posY_m);
    touched();
}

void
H19Terminal::eraseBOL()
{
    for (unsigned x = 0; x <= cursorCol(); ++x)
    {
        screen_m[posY_m][x] = ascii::SP;
    }
    touched();
}

void
H19Terminal::eraseEOL()
{
    for (unsigned x = cursorCol(); x < cols_c; ++x)
    {
        screen_m[posY_m][x] = ascii::SP;
    }
    touched();
}

void
H19Terminal::insertLine()
{
    if (!onLine25())
    {
        for (unsigned y = rowsMain_c - 1; y > posY_m; --y)
        {
            memcpy(&screen_m[y][0], &screen_m[y - 1][0], cols_c);
        }
    }
    eraseLine(posY_m);
    posX_m = 0;
    touched();
}

void
H19Terminal::deleteLine()
{
    posX_m = 0;

    if (onLine25())
    {
        eraseLine(posY_m);
    }
    else
    {
        for (unsigned y = posY_m; y < (rowsMain_c - 1); ++y)
        {
            memcpy(&screen_m[y][0], &screen_m[y + 1][0], cols_c);
        }
        eraseLine(rowsMain_c - 1);
    }
    touched();
}

void
H19Terminal::deleteChar()
{
    for (unsigned x = cursorCol(); x < (cols_c - 1); x++)
    {
        screen_m[posY_m][x] = screen_m[posY_m][x + 1];
    }
    screen_m[posY_m][cols_c - 1] = ascii::SP;
    touched();
}

void
H19Terminal::eraseLine(unsigned line)
{
    if (line < rows_c)
    {
        memset(&screen_m[line][0], ascii::SP, cols_c);
    }
}

void
H19Terminal::transmitLines(int start, int end)
{
    static const char characterLookup[0x80] = {
        '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
        'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~', '_',
        32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
        48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
        64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
        80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
        96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
        112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, '^'
    };

    bool reverse  = false;
    bool graphics = false;

    for (int line = start; line <= end; line++)
    {
        for (unsigned col = 0; col < cols_c; col++)
        {
            BYTE c           = screen_m[line][col];
            bool newReverse  = (c & 0x80) != 0;
            bool newGraphics = ((c & 0x7f) < 0x20) || ((c & 0x7f) == 0x7f);

            // the sequences of the mode the terminal is in
            if (newReverse != reverse)
            {
                sendSeq(ansiMode_m ? (newReverse ? "\033[7m" : "\033[m")
                                   : (newReverse ? "\033p" : "\033q"));
                reverse = newReverse;
            }
            if (newGraphics != graphics)
            {
                sendSeq(ansiMode_m ? (newGraphics ? "\033[10m" : "\033[11m")
                                   : (newGraphics ? "\033F" : "\033G"));
                graphics = newGraphics;
            }

            BYTE ch = c & 0x7f;
            if (graphics)
            {
                ch = (BYTE) characterLookup[ch];
            }
            queueByte(ch);
        }
    }
}

void
H19Terminal::transmitLine25()
{
    // if line 25 is not enabled, only send CR and ring bell.
    if (line25_m)
    {
        transmitLines(rowsMain_c, rowsMain_c);
    }
    queueByte(ascii::CR);
    bell();
}

void
H19Terminal::transmitPage()
{
    transmitLines(0, rowsMain_c - 1);
    queueByte(ascii::CR);
    bell();
}

void
H19Terminal::bell()
{
    bells_m.fetch_add(1);
}

H19Terminal::Modes
H19Terminal::modes() const
{
    Modes m;
    m.offline          = offline_m;
    m.holdScreen       = holdScreen_m;
    m.keypadShifted    = keypadShifted_m;
    m.altKeypad        = altKeypadMode_m;
    m.keyboardDisabled = !keyboardEnabled_m;
    m.reverseVideo     = reverseVideo_m;
    m.graphics         = graphicMode_m;
    m.insert           = insertMode_m;
    m.wrapEOL          = wrapEOL_m;
    m.autoLF           = autoLF_m;
    m.autoCR           = autoCR_m;
    m.noKeyClick       = noKeyClick_m;
    m.ansi             = ansiMode_m;
    m.escState         = (int) mode_m;
    m.sw401            = sw401_m;
    m.sw402            = sw402_m;
    m.baud             = baud_m;
    m.queued           = toSend_m.size();
    m.held             = held_m.size();
    return m;
}

std::string
H19Terminal::screenText(bool includeLine25) const
{
    // The H19's graphics characters, as the box-drawing and block characters
    // that look most like them: glyphs 0x00-0x1f and 0x7f of 444-29, checked
    // against the ROM's bitmaps.
    static const char* const graphics[32] = {
        "\u2502", "\u2500", "\u253c", "\u2510", "\u2518", "\u2514", "\u250c", "\u00b1",
        "\u2192", "\u2592", "\u00f7", "\u2193", "\u2597", "\u2596", "\u2598", "\u259d",
        "\u2580", "\u2590", "\u25e4", "\u252c", "\u2524", "\u2534", "\u251c", "\u2573",
        "\u2571", "\u2572", "\u2594", "\u2581", "\u258e", "\u2595", "\u00b6", "\u25e5",
    };

    std::string out;
    unsigned    rows = includeLine25 ? rows_c : rowsMain_c;

    for (unsigned y = 0; y < rows; ++y)
    {
        std::string line;
        for (unsigned x = 0; x < cols_c; ++x)
        {
            BYTE c = screen_m[y][x] & 0x7f;
            if (c < 0x20)
            {
                line += graphics[c];
            }
            else if (c == 0x7f)
            {
                line += "\u25cf";
            }
            else
            {
                line += (char) c;
            }
        }
        while (!line.empty() && line.back() == ' ')
        {
            line.pop_back();
        }
        out += line;
        out += '\n';
    }
    // no trailing blank lines (and nothing at all for a blank screen)
    while (!out.empty() && out.back() == '\n')
    {
        out.pop_back();
    }
    if (!out.empty())
    {
        out += '\n';
    }
    return out;
}
