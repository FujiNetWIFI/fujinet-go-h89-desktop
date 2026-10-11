/*
 * H19KeysWindow -- see H19KeysWindow.h.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "H19KeysWindow.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "FujiNetWindows.h"
#include "KeyForward.h"

namespace {

struct KeyDef {
    int key;
    const char *label;
    const char *tip;
};

const KeyDef kFunction[] = {
    { H89K_F1, "f1", "ESC S" }, { H89K_F2, "f2", "ESC T" }, { H89K_F3, "f3", "ESC U" },
    { H89K_F4, "f4", "ESC V" }, { H89K_F5, "f5", "ESC W" },
    { H89K_ERASE, "ERASE", "ESC J (F6)" }, { H89K_BLUE, "BLUE", "ESC P (F7)" },
    { H89K_RED, "RED", "ESC Q (F8)" }, { H89K_GRAY, "GRAY", "ESC R (F10)" },
};
const KeyDef kKeys[] = {
    { 0x1b, "ESC", nullptr }, { 0x09, "TAB", nullptr }, { 0x08, "BACK SPACE", nullptr },
    { 0x7f, "DELETE", nullptr }, { H89K_LINEFEED, "LINE FEED", nullptr }, { 0x0d, "RETURN", nullptr },
};
const KeyDef kTerminal[] = {
    { H89K_BREAK, "BREAK", "A break on the line (Pause)" },
    { H89K_OFFLINE, "OFF LINE", "Keys go to the screen, not the computer" },
    { H89K_SCROLL, "SCROLL", "Releases hold-screen mode (Scroll Lock); with SHIFT, a page" },
    { H89K_RESET, "SHIFT+RESET", "Resets the H89: the CPU, its cards and the terminal" },
};
const KeyDef kKeypad[] = {
    { H89K_KP7, "7\nIC", nullptr }, { H89K_KP8, "8\n↑", nullptr }, { H89K_KP9, "9\nDC", nullptr },
    { H89K_KP4, "4\n←", nullptr }, { H89K_KP5, "5\nHOME", nullptr }, { H89K_KP6, "6\n→", nullptr },
    { H89K_KP1, "1\nIL", nullptr }, { H89K_KP2, "2\n↓", nullptr }, { H89K_KP3, "3\nDL", nullptr },
    { H89K_KP0, "0", nullptr }, { H89K_KP_DOT, ".", nullptr }, { H89K_KP_ENTER, "ENTER", nullptr },
};
const char *const kLights[8] = {
    "Off line", "Hold screen", "Keypad shifted", "Alternate keypad",
    "Keyboard off", "Graphics", "Insert", "Reverse",
};

} // namespace

H19KeysWindow::H19KeysWindow(h89session *session, QWidget *parent)
    : QWidget(parent, Qt::Window), m_session(session)
{
    setWindowTitle(QStringLiteral("H19 Keys"));
    auto *outer = new QVBoxLayout(this);

    auto button = [this](const KeyDef &k) {
        auto *b = new QPushButton(QString::fromUtf8(k.label));
        b->setFocusPolicy(Qt::NoFocus);
        if (k.tip)
            b->setToolTip(QString::fromUtf8(k.tip));
        const int key = k.key;
        connect(b, &QPushButton::clicked, this, [this, key] { press(key); });
        return b;
    };
    auto row = [&](const char *title, const KeyDef *keys, int n, QWidget *first = nullptr) {
        auto *g = new QGroupBox(QString::fromUtf8(title));
        auto *l = new QHBoxLayout(g);
        if (first)
            l->addWidget(first);
        for (int i = 0; i < n; ++i)
            l->addWidget(button(keys[i]));
        outer->addWidget(g);
    };
    row("Function keys", kFunction, (int)(sizeof kFunction / sizeof kFunction[0]));
    row("Keys", kKeys, (int)(sizeof kKeys / sizeof kKeys[0]));
    m_shift = new QToolButton;
    m_shift->setText(QStringLiteral("SHIFT"));
    m_shift->setCheckable(true);
    m_shift->setFocusPolicy(Qt::NoFocus);
    m_shift->setToolTip(QStringLiteral("Held for the next key: the keypad's functions, a page of SCROLL"));
    row("Terminal", kTerminal, (int)(sizeof kTerminal / sizeof kTerminal[0]), m_shift);

    auto *bottom = new QHBoxLayout;
    auto *pad = new QGroupBox(QStringLiteral("Keypad"));
    auto *grid = new QGridLayout(pad);
    for (int i = 0; i < (int)(sizeof kKeypad / sizeof kKeypad[0]); ++i) {
        QPushButton *b = button(kKeypad[i]);
        b->setMinimumSize(64, 48);
        grid->addWidget(b, i / 3, i % 3);
    }
    bottom->addWidget(pad, 1);
    auto *modes = new QGroupBox(QStringLiteral("Modes"));
    auto *ml = new QVBoxLayout(modes);
    for (int i = 0; i < 8; ++i) {
        m_lights[i] = new QLabel;
        ml->addWidget(m_lights[i]);
    }
    ml->addStretch(1);
    bottom->addWidget(modes);
    outer->addLayout(bottom);

    connect(&m_timer, &QTimer::timeout, this, &H19KeysWindow::refreshLights);
    m_timer.start(250);
    refreshLights();
}

void H19KeysWindow::press(int key)
{
    const bool shift = m_shift->isChecked();
    h89session_key(m_session, key, shift);
    /* a latched SHIFT is for one key, as on the keyboard's own */
    if (shift && key != H89K_RESET)
        m_shift->setChecked(false);
}

void H19KeysWindow::refreshLights()
{
    const unsigned m = h89session_h19_modes(m_session);
    const QString on = h89AccentColor().name();
    for (int i = 0; i < 8; ++i) {
        const bool lit = (m >> i) & 1;
        m_lights[i]->setText(QStringLiteral("<span style=\"color:%1\">●</span> %2")
                                 .arg(lit ? on : QStringLiteral("gray"),
                                      QString::fromUtf8(kLights[i])));
    }
}

void H19KeysWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F9) {
        hide();
        return;
    }
    if (!h89ForwardKey(m_session, e))
        QWidget::keyPressEvent(e);
}
