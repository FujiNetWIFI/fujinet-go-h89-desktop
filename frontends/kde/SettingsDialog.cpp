/*
 * SettingsDialog -- see SettingsDialog.h.
 *
 * The same pages as the GNOME Preferences: Machine, Slots & Drives,
 * Switches (SW501 as the chosen monitor reads it, the H19's SW401 and
 * SW402), CP/NET, Display & Sound, FujiNet. The core says what the choices
 * are and which combinations make sense (h89config.h).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cstdio>
#include <cstring>
#include <vector>

#include "FujiNetWindows.h"

int SettingsDialog::importRoms(QWidget *parent, h89session *session, QStringList paths)
{
    if (paths.isEmpty())
        paths = QFileDialog::getOpenFileNames(
            parent, QStringLiteral("Import System ROMs"), QString(),
            QStringLiteral("MAME ROM sets and ROM images (*.zip *.bin *.rom *.u5* *.u4*);;All files (*)"));
    if (paths.isEmpty())
        return 0;
    int total = 0;
    QStringList log;
    for (const QString &p : paths) {
        char msg[1024];
        const int n = h89session_import_roms(session, p.toLocal8Bit().constData(), msg, sizeof msg);
        if (n > 0)
            total += n;
        log << QString::fromUtf8(msg);
    }
    if (total > 0)
        QMessageBox::information(parent, QStringLiteral("ROMs Imported"), log.join(QStringLiteral("\n\n")));
    else
        QMessageBox::warning(parent, QStringLiteral("Nothing Imported"), log.join(QStringLiteral("\n\n")));
    return total;
}

namespace {

QComboBox *combo(const char *(*names)(int))
{
    auto *c = new QComboBox;
    for (int i = 0; names(i); ++i)
        c->addItem(QString::fromUtf8(names(i)));
    return c;
}

QWidget *scrolled(QWidget *page)
{
    auto *s = new QScrollArea;
    s->setWidget(page);
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    return s;
}

bool hasCard(const h89_config &c, int card)
{
    for (int i = 0; i < H89_SLOTS; ++i)
        if (c.slot[i] == card)
            return true;
    return false;
}

} // namespace

int SettingsDialog::run(QWidget *parent, h89session *session, std::function<void()> applyView)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Preferences"));
    dlg.resize(680, 640);

    h89_config cfg, orig;
    h89session_get_config(session, &cfg);
    orig = cfg;
    bool hostChanged = false;

    auto *outer = new QVBoxLayout(&dlg);
    auto *status = new QLabel;
    status->setWordWrap(true);
    outer->addWidget(status);
    auto *tabs = new QTabWidget;
    outer->addWidget(tabs, 1);

    std::function<void()> changed;     /* set below, once every widget exists */
    auto intCombo = [&](QComboBox *c, int *field, const std::vector<int> &values = {}) {
        int sel = *field;
        if (!values.empty())
            for (size_t i = 0; i < values.size(); ++i)
                if (values[i] == *field)
                    sel = (int)i;
        c->setCurrentIndex(sel);
        QObject::connect(c, &QComboBox::currentIndexChanged, &dlg, [&, field, values](int i) {
            if (i < 0)
                return;
            *field = values.empty() ? i : values[(size_t)i];
            changed();
        });
        return c;
    };
    auto bitBox = [&](const char *text, uint8_t *byte, uint8_t mask) {
        auto *b = new QCheckBox(QString::fromUtf8(text));
        b->setChecked(*byte & mask);
        QObject::connect(b, &QCheckBox::toggled, &dlg, [&, byte, mask](bool on) {
            *byte = on ? (uint8_t)(*byte | mask) : (uint8_t)(*byte & ~mask);
            changed();
        });
        return b;
    };
    auto flagBox = [&](const char *text, int *field) {
        auto *b = new QCheckBox(QString::fromUtf8(text));
        b->setChecked(*field != 0);
        QObject::connect(b, &QCheckBox::toggled, &dlg, [&, field](bool on) {
            *field = on ? 1 : 0;
            changed();
        });
        return b;
    };
    auto spin = [&](int *field, int lo, int hi) {
        auto *s = new QSpinBox;
        s->setRange(lo, hi);
        s->setValue(*field);
        QObject::connect(s, &QSpinBox::valueChanged, &dlg, [&, field](int v) {
            *field = v;
            changed();
        });
        return s;
    };
    /* a settings-store check box: live, or a session restart */
    auto setBox = [&](const char *text, const char *key, int def, bool host, bool view) {
        auto *b = new QCheckBox(QString::fromUtf8(text));
        b->setChecked(h89session_get_int(session, key, def) != 0);
        QObject::connect(b, &QCheckBox::toggled, &dlg, [&, key, host, view](bool on) {
            h89session_set_int(session, key, on ? 1 : 0);
            if (host)
                hostChanged = true;
            if (view) {
                h89session_apply_view(session);
                applyView();
            }
        });
        return b;
    };

    /* ---- Machine */
    auto *machinePage = new QWidget;
    auto *mv = new QVBoxLayout(machinePage);
    auto *machine = new QGroupBox(QStringLiteral("Machine"));
    auto *mf = new QFormLayout(machine);
    mf->addRow(QStringLiteral("Model"), intCombo(combo(h89_model_name), &cfg.model));
    mf->addRow(QStringLiteral("Memory"), intCombo(combo(h89_memory_name), &cfg.memory));
    mf->addRow(QStringLiteral("Memory slot P503"), intCombo(combo(h89_p503_name), &cfg.p503));
    auto *monitor = new QComboBox;
    mf->addRow(QStringLiteral("Monitor ROM"), monitor);
    static const char *const speedNames[] = { "None (2.048 MHz)", "2×", "3×", "4×",
                                              "6×", "8×", "10×", "16×",
                                              "20×", "40×" };
    const std::vector<int> speeds = { 0, 2, 3, 4, 6, 8, 10, 16, 20, 40 };
    auto *speed = new QComboBox;
    for (const char *n : speedNames)
        speed->addItem(QString::fromUtf8(n));
    speed->setToolTip(QStringLiteral("A faster Z80; the disks and the clock keep their own time "
                                     "(not with the MMS 77318)"));
    mf->addRow(QStringLiteral("Speed-up"), intCombo(speed, &cfg.speedup, speeds));
    mv->addWidget(machine);

    auto *romsBox = new QGroupBox(QStringLiteral("System ROMs"));
    auto *rl = new QHBoxLayout(romsBox);
    auto *romsText = new QLabel;
    romsText->setWordWrap(true);
    auto *importBtn = new QPushButton(QStringLiteral("Import..."));
    rl->addWidget(romsText, 1);
    rl->addWidget(importBtn);
    mv->addWidget(romsBox);
    mv->addStretch(1);
    tabs->addTab(scrolled(machinePage), QStringLiteral("Machine"));

    std::vector<int> monitors;
    auto fillMonitors = [&]() {
        QSignalBlocker block(monitor);
        int idx[16];
        const int n = h89session_monitors(session, idx, 16);
        monitor->clear();
        monitors.clear();
        int sel = -1;
        for (int i = 0; i < n; ++i) {
            const h89_rom_info *r = h89_rom_at(idx[i]);
            monitor->addItem(QString::fromUtf8(r->desc));
            monitors.push_back(idx[i]);
            if (std::strcmp(r->file, cfg.monitor) == 0)
                sel = i;
        }
        if (sel < 0 && cfg.monitor[0]) {
            const int i = h89_rom_find_file(cfg.monitor);
            const h89_rom_info *r = i >= 0 ? h89_rom_at(i) : nullptr;
            monitor->addItem(QStringLiteral("%1 (not imported)")
                                 .arg(QString::fromUtf8(r ? r->desc : cfg.monitor)));
            monitors.push_back(i);
            sel = monitor->count() - 1;
        }
        monitor->setCurrentIndex(sel);
        char what[512];
        romsText->setText(h89session_missing_roms(session, what, sizeof what) > 0
                              ? QStringLiteral("Still needed: %1").arg(QString::fromUtf8(what))
                              : QStringLiteral("Everything this machine needs is imported"));
    };
    QObject::connect(monitor, &QComboBox::currentIndexChanged, &dlg, [&](int i) {
        if (i < 0 || i >= (int)monitors.size() || monitors[(size_t)i] < 0)
            return;
        std::strncpy(cfg.monitor, h89_rom_at(monitors[(size_t)i])->file, sizeof cfg.monitor - 1);
        changed();
    });
    QObject::connect(importBtn, &QPushButton::clicked, &dlg, [&] {
        importRoms(&dlg, session);
        fillMonitors();
    });

    /* ---- Slots & Drives */
    auto *slotsPage = new QWidget;
    auto *sv = new QVBoxLayout(slotsPage);
    auto *slotBox = new QGroupBox(QStringLiteral("I/O Slots"));
    auto *sf = new QFormLayout(slotBox);
    static const char *const slotNames[] = { "P504", "P505", "P506" };
    for (int i = 0; i < H89_SLOTS; ++i)
        sf->addRow(QString::fromUtf8(slotNames[i]), intCombo(combo(h89_card_name), &cfg.slot[i]));
    sv->addWidget(slotBox);
    auto *h17 = new QGroupBox(QStringLiteral("H-88-1 (H17) drives"));
    auto *h17f = new QFormLayout(h17);
    for (int i = 0; i < H89_H17_DRIVES; ++i)
        h17f->addRow(QStringLiteral("Drive %1").arg(i + 1),
                     intCombo(combo(h89_h17_drive_name), &cfg.h17_drive[i]));
    sv->addWidget(h17);
    auto *h37 = new QGroupBox(QStringLiteral("Z-89-37 drives"));
    auto *h37f = new QFormLayout(h37);
    for (int i = 0; i < H89_H37_DRIVES; ++i)
        h37f->addRow(QStringLiteral("Drive %1").arg(i + 1), intCombo(combo(h89_fdd_name), &cfg.h37_drive[i]));
    sv->addWidget(h37);
    auto *m316 = new QGroupBox(QStringLiteral("MMS 77316 drives"));
    auto *m316f = new QFormLayout(m316);
    for (int i = 0; i < H89_MMS316_DRIVES; ++i)
        m316f->addRow(QStringLiteral("Unit %1 (%2)").arg(i + 1).arg(i < 4 ? "8\"" : "5.25\""),
                      intCombo(combo(h89_fdd_name), &cfg.mms316_drive[i]));
    sv->addWidget(m316);
    auto *m320 = new QGroupBox(QStringLiteral("MMS 77320 SASI"));
    auto *m320f = new QFormLayout(m320);
    auto *port = new QComboBox;
    port->addItems({ QStringLiteral("170Q (0x78, JP1A)"), QStringLiteral("174Q (0x7C, JP1B)") });
    m320f->addRow(QStringLiteral("Port"), intCombo(port, &cfg.mms320_port, { 0x78, 0x7c }));
    auto *intr = new QComboBox;
    intr->addItems({ QStringLiteral("None"), QStringLiteral("INT 3 (JP2A)"), QStringLiteral("INT 4 (JP2B)"),
                     QStringLiteral("INT 5 (JP2C)") });
    m320f->addRow(QStringLiteral("Interrupt"), intCombo(intr, &cfg.mms320_intr, { 0, 3, 4, 5 }));
    for (int i = 0; i < H89_MMS320_DRIVES; ++i)
        m320f->addRow(QStringLiteral("Unit %1").arg(i), intCombo(combo(h89_sasi_name), &cfg.mms320_drive[i]));
    sv->addWidget(m320);
    sv->addStretch(1);
    tabs->addTab(scrolled(slotsPage), QStringLiteral("Slots && Drives"));

    /* ---- Switches */
    auto *swPage = new QWidget;
    auto *swv = new QVBoxLayout(swPage);
    auto *sw501 = new QGroupBox(QStringLiteral("SW501 (CPU board): read by the monitor at power-up"));
    auto *sw501v = new QVBoxLayout(sw501);
    sw501v->addWidget(flagBox("Set the device switches from the slots", &cfg.sw501_auto));
    auto *sw501fields = new QWidget;
    auto *sw501form = new QFormLayout(sw501fields);
    sw501form->setContentsMargins(0, 0, 0, 0);
    sw501v->addWidget(sw501fields);
    swv->addWidget(sw501);
    auto *sw401 = new QGroupBox(QStringLiteral("SW401 (H19: the line)"));
    auto *sw401f = new QFormLayout(sw401);
    auto *baud = new QComboBox;
    baud->addItems({ QStringLiteral("110"), QStringLiteral("150"), QStringLiteral("300"), QStringLiteral("600"),
                     QStringLiteral("1200"), QStringLiteral("1800"), QStringLiteral("2000"),
                     QStringLiteral("2400"), QStringLiteral("3600"), QStringLiteral("4800"),
                     QStringLiteral("7200"), QStringLiteral("9600"), QStringLiteral("19200") });
    {
        const int code = cfg.sw401 & 0x0f;
        baud->setCurrentIndex(code >= 1 && code <= 13 ? code - 1 : 11);
    }
    QObject::connect(baud, &QComboBox::currentIndexChanged, &dlg, [&](int i) {
        if (i < 0)
            return;
        cfg.sw401 = (uint8_t)((cfg.sw401 & 0xf0) | (i + 1));
        changed();
    });
    sw401f->addRow(QStringLiteral("Baud rate"), baud);
    sw401f->addRow(bitBox("Parity", &cfg.sw401, 0x10));
    sw401f->addRow(bitBox("Even parity", &cfg.sw401, 0x20));
    sw401f->addRow(bitBox("Stick parity", &cfg.sw401, 0x40));
    sw401f->addRow(bitBox("Full duplex", &cfg.sw401, 0x80));
    swv->addWidget(sw401);
    auto *sw402 = new QGroupBox(QStringLiteral("SW402 (H19: the terminal)"));
    auto *sw402f = new QFormLayout(sw402);
    sw402f->addRow(bitBox("Block cursor", &cfg.sw402, 0x01));
    sw402f->addRow(bitBox("No key click", &cfg.sw402, 0x02));
    sw402f->addRow(bitBox("Wrap at the end of a line", &cfg.sw402, 0x04));
    sw402f->addRow(bitBox("Line feed on carriage return", &cfg.sw402, 0x08));
    sw402f->addRow(bitBox("Carriage return on line feed", &cfg.sw402, 0x10));
    sw402f->addRow(bitBox("ANSI mode at power-up (Heath mode otherwise)", &cfg.sw402, 0x20));
    sw402f->addRow(bitBox("Keypad shifted", &cfg.sw402, 0x40));
    sw402f->addRow(bitBox("50 Hz refresh", &cfg.sw402, 0x80));
    swv->addWidget(sw402);
    swv->addStretch(1);
    tabs->addTab(scrolled(swPage), QStringLiteral("Switches"));

    /* SW501's fields are the chosen monitor's: rebuilt when the monitor,
     * the slots or the auto switch change (after the change's own signal) */
    std::vector<QComboBox *> swCombos;
    char builtFor[64] = "";
    int builtAuto = -1;
    auto rebuildSw501 = [&]() {
        const char *mon = cfg.monitor;
        const uint8_t eff = h89_config_effective_sw501(&cfg);
        const uint8_t automask = cfg.sw501_auto ? h89_sw501_auto_mask(mon) : 0;
        const int n = h89_sw501_field_count(mon);
        if (std::strcmp(builtFor, mon) != 0 || builtAuto != cfg.sw501_auto || (int)swCombos.size() != n) {
            while (sw501form->rowCount() > 0)
                sw501form->removeRow(0);
            swCombos.clear();
            for (int i = 0; i < n; ++i) {
                auto *c = new QComboBox;
                for (int o = 0; o < h89_sw501_option_count(mon, i); ++o)
                    c->addItem(QString::fromUtf8(h89_sw501_option_name(mon, i, o)));
                QObject::connect(c, &QComboBox::currentIndexChanged, &dlg, [&, i](int o) {
                    if (o < 0)
                        return;
                    const uint8_t mask = h89_sw501_field_mask(cfg.monitor, i);
                    cfg.sw501 = (uint8_t)((cfg.sw501 & ~mask) | h89_sw501_option_value(cfg.monitor, i, o));
                    changed();
                });
                sw501form->addRow(QString::fromUtf8(h89_sw501_field_name(mon, i)), c);
                swCombos.push_back(c);
            }
            std::snprintf(builtFor, sizeof builtFor, "%s", mon);
            builtAuto = cfg.sw501_auto;
        }
        for (int i = 0; i < n; ++i) {
            QComboBox *c = swCombos[(size_t)i];
            const uint8_t mask = h89_sw501_field_mask(mon, i);
            QSignalBlocker block(c);
            for (int o = 0; o < h89_sw501_option_count(mon, i); ++o)
                if ((eff & mask) == h89_sw501_option_value(mon, i, o))
                    c->setCurrentIndex(o);
            c->setEnabled((mask & automask) == 0);
        }
    };

    /* ---- CP/NET */
    auto *cpPage = new QWidget;
    auto *cpv = new QVBoxLayout(cpPage);
    auto *cp = new QGroupBox(QStringLiteral("CP/NET: a host folder served as CP/M drives"));
    auto *cpf = new QFormLayout(cp);
    cpf->addRow(flagBox("CP/NET device", &cfg.cpnet_enabled));
    QSpinBox *cpPort = spin(&cfg.cpnet_port, 0, 255);
    QSpinBox *cpId = spin(&cfg.cpnet_clientid, 0, 254);
    cpf->addRow(QStringLiteral("Port"), cpPort);
    cpf->addRow(QStringLiteral("Client ID"), cpId);
    auto *cpRoot = new QLabel(cfg.cpnet_root[0] ? QString::fromUtf8(cfg.cpnet_root) : QStringLiteral("(the default)"));
    auto *cpChoose = new QPushButton(QStringLiteral("Choose..."));
    auto *cpRow = new QHBoxLayout;
    cpRow->addWidget(cpRoot, 1);
    cpRow->addWidget(cpChoose);
    cpf->addRow(QStringLiteral("Host folder"), cpRow);
    QObject::connect(cpChoose, &QPushButton::clicked, &dlg, [&] {
        const QString d = QFileDialog::getExistingDirectory(&dlg, QStringLiteral("The CP/NET Host Folder"));
        if (d.isEmpty())
            return;
        std::strncpy(cfg.cpnet_root, d.toLocal8Bit().constData(), sizeof cfg.cpnet_root - 1);
        cpRoot->setText(d);
        changed();
    });
    cpv->addWidget(cp);
    cpv->addStretch(1);
    tabs->addTab(scrolled(cpPage), QStringLiteral("CP/NET"));

    /* ---- Display & Sound (live) */
    auto *dsPage = new QWidget;
    auto *dsv = new QVBoxLayout(dsPage);
    auto *view = new QGroupBox(QStringLiteral("Display"));
    auto *vf = new QFormLayout(view);
    auto *phosphor = new QComboBox;
    phosphor->addItems({ QStringLiteral("White (P4)"), QStringLiteral("Green (P1)"), QStringLiteral("Amber (P3)") });
    phosphor->setCurrentIndex(h89session_get_int(session, "view.phosphor", 0));
    QObject::connect(phosphor, &QComboBox::currentIndexChanged, &dlg, [&](int i) {
        h89session_set_int(session, "view.phosphor", i);
        h89session_apply_view(session);
    });
    vf->addRow(QStringLiteral("Phosphor"), phosphor);
    vf->addRow(setBox("Scanlines", "view.scanlines", 0, false, true));
    vf->addRow(setBox("Blinking cursor", "view.cursor_blink", 1, false, true));
    auto *aspect = new QComboBox;
    aspect->addItems({ QStringLiteral("The tube's 4:3"), QStringLiteral("Square pixels") });
    aspect->setCurrentIndex(h89session_get_int(session, "view.aspect", 0));
    QObject::connect(aspect, &QComboBox::currentIndexChanged, &dlg, [&](int i) {
        h89session_set_int(session, "view.aspect", i);
        applyView();
    });
    vf->addRow(QStringLiteral("Aspect"), aspect);
    vf->addRow(setBox("Smooth scaling", "view.smooth", 1, false, true));
    dsv->addWidget(view);
    auto *sound = new QGroupBox(QStringLiteral("Sound: the H19's bell and key click"));
    auto *sdf = new QFormLayout(sound);
    sdf->addRow(setBox("Enable audio (applied when Preferences closes)", "enable_audio", 1, true, false));
    auto *vol = new QSlider(Qt::Horizontal);
    vol->setRange(0, 100);
    vol->setValue(h89session_get_int(session, "sound.volume", 70));
    QObject::connect(vol, &QSlider::valueChanged, &dlg, [&](int v) { h89session_set_volume(session, v); });
    sdf->addRow(QStringLiteral("Volume"), vol);
    sdf->addRow(setBox("Bell", "sound.bell", 1, false, false));
    sdf->addRow(setBox("Key click (unless SW402 or ESC x2 turns it off)", "sound.click", 1, false, false));
    sdf->addRow(setBox("Visual bell (flash the screen)", "sound.visual_bell", 0, false, false));
    dsv->addWidget(sound);
    dsv->addStretch(1);
    tabs->addTab(scrolled(dsPage), QStringLiteral("Display && Sound"));

    /* ---- FujiNet */
    auto *fnPage = new QWidget;
    auto *fnv = new QVBoxLayout(fnPage);
    auto *fn = new QGroupBox(QStringLiteral("FujiNet: it runs inside the app; the H89's card talks to it"));
    auto *fnf = new QFormLayout(fn);
    fnf->addRow(setBox("Enable FujiNet (applied when Preferences closes)", "enable_fujinet", 1, true, false));
    auto *webui = new QPushButton(QString::fromUtf8(h89session_fujinet_webui_url(session)));
    QObject::connect(webui, &QPushButton::clicked, &dlg, [&] {
        if (!h89session_fujinet_running(session))
            QMessageBox::information(&dlg, QStringLiteral("FujiNet"), QStringLiteral("FujiNet is not running"));
        else
            fujinet_config_show(&dlg, session);
    });
    fnf->addRow(QStringLiteral("Web UI"), webui);
    QSpinBox *ack = spin(&cfg.fujinet_ack_delay, 1, 10000);
    ack->setToolTip(QStringLiteral("Cycles before the ESP32 acknowledges a byte the H89 writes"));
    fnf->addRow(QStringLiteral("Card handshake delay"), ack);
    fnv->addWidget(fn);
    fnv->addStretch(1);
    tabs->addTab(scrolled(fnPage), QStringLiteral("FujiNet"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);

    changed = [&]() {
        char why[256];
        const bool ok = h89_config_validate(&cfg, why, sizeof why);
        if (!ok)
            status->setText(QStringLiteral("<span style=\"color:#d83b3b\">%1</span>").arg(QString::fromUtf8(why)));
        else if (std::memcmp(&cfg, &orig, sizeof cfg) == 0)
            status->setText(QStringLiteral("Configuration: as it is now"));
        else
            status->setText(QStringLiteral("Configuration: changed, applied by a power cycle"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
        h17->setVisible(hasCard(cfg, H89_CARD_H17));
        h37->setVisible(hasCard(cfg, H89_CARD_H37));
        m316->setVisible(hasCard(cfg, H89_CARD_MMS77316));
        m320->setVisible(hasCard(cfg, H89_CARD_MMS77320));
        ack->setEnabled(hasCard(cfg, H89_CARD_FUJINET));
        cpPort->setEnabled(cfg.cpnet_enabled);
        cpId->setEnabled(cfg.cpnet_enabled);
        cpChoose->setEnabled(cfg.cpnet_enabled);
        /* not from inside the signal of a row it would delete */
        QTimer::singleShot(0, &dlg, rebuildSw501);
    };
    fillMonitors();
    changed();
    rebuildSw501();

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    {
        const QString pg = qEnvironmentVariable("H89_PREFS_PAGE");
        const char *const names[] = { "machine", "slots", "switches", "cpnet", "display", "fujinet" };
        for (int i = 0; i < 6; ++i)
            if (pg == QLatin1String(names[i]))
                tabs->setCurrentIndex(i);
    }

    const bool accepted = dlg.exec() == QDialog::Accepted;
    h89session_settings_flush(session);
    int outcome = hostChanged ? Restart : Nothing;
    if (!accepted || std::memcmp(&cfg, &orig, sizeof cfg) == 0)
        return outcome;

    const bool running = h89session_is_running(session);
    const bool needs = running && h89session_config_needs_power_cycle(session, &cfg);
    h89session_set_config(session, &cfg);
    h89session_settings_flush(session);
    if (!running)
        return PowerCycle;          /* the caller starts it */
    if (needs && QMessageBox::question(parent, QStringLiteral("Power Cycle the H89?"),
            QStringLiteral("The new configuration takes effect when the H89 is switched off "
                           "and on. The disks are written back first, and go back in their "
                           "drives."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
        outcome |= PowerCycle;
    return outcome;
}
