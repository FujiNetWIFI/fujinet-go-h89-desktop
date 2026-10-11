/*
 * H19Terminal -- the H89's built-in Heath H19 terminal, at the level of its
 * serial line: characters from the H89's console 8250 are interpreted (Heath
 * mode escape sequences) into an 80x25 character screen; keys are encoded
 * the way the H19's keyboard firmware encodes them and sent back to the 8250
 * at the line rate.
 *
 * Derived from VirtualH89's H19 class (h19.cpp/h19.h, BSD-3-Clause,
 * Copyright (c) 2009-2016 Mark Garlanger), with the GUI coupling, the
 * global instance and the mutex removed -- it runs on the emulator thread,
 * and other threads reach it only through the key ring and the host's run
 * lock -- and these fixes and additions:
 *   - onLine25() tested the column, not the row;
 *   - ESC = / ESC > (alternate keypad) set the keypad-shifted mode;
 *   - the keyboard encodes the keypad (shifted, alternate), f1-f5, ERASE,
 *     the blue/red/gray keys, IC/DC/IL/DL, BREAK and OFF LINE itself;
 *   - characters wait for the 8250's receiver to be empty, so typing ahead
 *     and pasting never overrun it;
 *   - the bell and the key click are reported for the host's audio;
 *   - SW401's baud rate sets the character time; SW402 follows MAME's
 *     switch polarity (bit 1 set = NO key click);
 *   - ANSI mode (ESC <, or SW402 at power-up): the CSI sequences of the
 *     Operation Manual's appendix (595-2284-05, pp. 81-90), and the keys'
 *     ANSI codes;
 *   - hold-screen mode holds the line before a scroll and sends XOFF, as
 *     the manual describes it; SCROLL lets one line through, SHIFT+SCROLL a
 *     page, and XON follows once nothing is held.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89_H19TERMINAL_H
#define H89_H19TERMINAL_H

#include "ClockUser.h"
#include "SerialPortDevice.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "h89keys.h"

class INS8250;

class H19Terminal: public SerialPortDevice, public ClockUser
{
  public:
    static const unsigned cols_c     = 80;
    static const unsigned rows_c     = 25;
    static const unsigned rowsMain_c = 24;

    H19Terminal(BYTE sw401, BYTE sw402);
    ~H19Terminal() override;

    /// The console port; BREAK is signalled to it directly.
    void attachUart(INS8250* uart);

    // ---- the serial line (emulator thread) ----
    void receiveData(BYTE data) override;
    unsigned int getBaudRate() override;
    void notification(unsigned int cycleCount) override;

    // ---- the keyboard (any thread) ----
    /// A key: an ASCII code (0x00-0x7f) or an H89K_* code; `shift` is the
    /// SHIFT key (the keypad and SCROLL use it).
    void postKey(unsigned code, bool shift);
    /// Type a string (UTF-8; CR/LF as RETURN); returns the bytes queued.
    size_t postText(const std::string& text);
    /// Abandon a paste in progress.
    void cancelText();
    bool textPending();

    /// Power-on state (also ESC z).
    void powerOn();

    // ---- what the screen shows (emulator thread or under the run lock) ----
    /// cell: bit 7 = reverse video, bits 6-0 = the character generator
    /// index (graphics characters are 0x00-0x1f and 0x7f).
    uint8_t cell(unsigned col, unsigned row) const
    {
        return screen_m[row][col];
    }
    unsigned cursorCol() const
    {
        return posX_m >= cols_c ? cols_c - 1 : posX_m;
    }
    unsigned cursorRow() const
    {
        return posY_m;
    }
    bool cursorVisible() const
    {
        return !cursorOff_m;
    }
    bool cursorBlock() const
    {
        return cursorBlock_m;
    }
    bool line25() const
    {
        return line25_m;
    }
    /// Bumped on every change of the screen or the cursor.
    uint64_t generation() const
    {
        return generation_m;
    }

    struct Modes
    {
        bool offline, holdScreen, keypadShifted, altKeypad, keyboardDisabled,
             reverseVideo, graphics, insert, wrapEOL, autoLF, autoCR, noKeyClick,
             ansi;
        int  escState;
        BYTE sw401, sw402;
        unsigned baud;
        size_t queued;
        size_t held;          ///< characters held by hold-screen mode
    };
    Modes modes() const;

    /// The screen as text (graphics characters as the H19's ASCII for them).
    std::string screenText(bool includeLine25) const;

    // ---- sounds for the host (any thread) ----
    unsigned takeBells()
    {
        return bells_m.exchange(0);
    }
    unsigned takeClicks()
    {
        return clicks_m.exchange(0);
    }

    /// OFF LINE (local mode), toggled by the key of that name.
    void setOffline(bool off);

  private:
    enum InputMode
    {
        Normal,
        Escape,
        DCA_1,
        DCA_2,
        SetMode,
        ResetMode,
        Csi,        ///< ANSI mode: ESC [ (parameters) final
    };

    // keyboard
    void encodeKey(unsigned code, bool shift);
    void sendSeq(const char* s);
    void queueByte(BYTE b);

    // character processing (VirtualH89's)
    void processCharacter(BYTE ch);
    void displayCharacter(unsigned ch);
    void processBS();
    void processTAB();
    void processLF();
    void processCR();
    void cursorForward();
    void cursorDown();
    void cursorUp();
    void reverseIndex();
    void cursorPositionReport();
    void clearDisplay();
    void eraseBOD();
    void eraseEOP();
    void eraseEL();
    void eraseBOL();
    void eraseEOL();
    void insertLine();
    void deleteLine();
    void deleteChar();
    void eraseLine(unsigned line);
    void scroll();
    void transmitLines(int start, int end);
    void transmitLine25();
    void transmitPage();
    void bell();

    // ANSI mode
    void processAnsiEscape(BYTE ch);
    void processCsi(BYTE ch);
    void executeCsi(BYTE final);
    int  csiParam(unsigned i, int def) const;
    // ESC x / ESC y and ESC [ > Ps h / l: the Heath modes 1-9
    void setHeathMode(int n, bool on);

    // hold-screen mode: may the screen scroll up now?
    bool mayScroll();
    void releaseHeld();
    bool onLine25() const
    {
        return posY_m == rows_c - 1;
    }
    void touched()
    {
        ++generation_m;
    }

    void setSW401(BYTE sw401);
    void setSW402(BYTE sw402);

    INS8250*  uart_m;

    uint8_t   screen_m[rows_c][cols_c];
    unsigned  posX_m, posY_m;
    unsigned  saveX_m, saveY_m;
    uint64_t  generation_m;

    InputMode mode_m;
    BYTE      sw401_m, sw402_m;
    unsigned  baud_m;
    unsigned  charCycles_m;

    bool      reverseVideo_m;
    bool      graphicMode_m;
    bool      insertMode_m;
    bool      line25_m;
    bool      holdScreen_m;
    bool      wrapEOL_m;
    bool      autoLF_m;
    bool      autoCR_m;
    bool      keyboardEnabled_m;
    bool      noKeyClick_m;
    bool      keypadShifted_m;
    bool      altKeypadMode_m;
    bool      offline_m;
    bool      cursorBlock_m;
    bool      cursorOff_m;
    bool      ansiMode_m;

    // an ANSI control sequence being received
    static const unsigned csiMax_c = 16;
    char      csiPrivate_m;          ///< '>' or '?' (the Heath extensions), or 0
    int       csiParam_m[csiMax_c];
    unsigned  csiCount_m;

    // hold-screen mode: what waits for SCROLL, and how many lines it allows
    static const size_t heldMax_c = 4096;
    std::deque<BYTE> held_m;
    unsigned  scrollCredit_m;
    bool      blocked_m;

    // the line back to the H89
    std::deque<BYTE> toSend_m;
    unsigned long    countdown_m;

    // the keyboard ring: one producer (the UI), one consumer (us)
    static const unsigned keyRing_c = 256;
    std::atomic<uint32_t> keyRing_m[keyRing_c];
    std::atomic<unsigned> keyHead_m, keyTail_m;

    // pasted text, typed as the line allows
    std::mutex       textLock_m;
    std::string      text_m;
    size_t           textPos_m;
    std::atomic<bool> textPending_m;

    std::atomic<unsigned> bells_m, clicks_m;
};

#endif /* H89_H19TERMINAL_H */
