/*
 * MainWindow -- see MainWindow.h.
 *
 * Plain Qt6 Widgets, deliberately not KDE Frameworks: it picks up Breeze
 * through the platform theme anyway, and staying framework-free keeps this
 * frontend usable outside a KDE session.
 *
 * Keys go to the H19 as characters (KeyForward); CTRL with a letter is the
 * H19's control code, so the app's shortcuts are CTRL+SHIFT ones, plus F9,
 * F11 and F12.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include "DisplayWidget.h"
#include "FujiNetWindows.h"
#include "H19KeysWindow.h"
#include "KeyForward.h"
#include "SettingsDialog.h"
#include "debugger/DebuggerWindow.h"

static const char *const kH17Filter =
    "H17 disk images (*.h8d *.h17 *.h17disk *.h17raw *.rawdisk *.tmpdisk);;All files (*)";
static const char *const kSoftFilter =
    "Soft-sectored disk images (*.imd *.td0 *.mmsdisk *.logdisk *.rawfloppy);;All files (*)";
static const char *const kAnyFilter =
    "Disk images (*.h8d *.h17 *.h17disk *.h17raw *.rawdisk *.tmpdisk *.imd *.td0 *.mmsdisk "
    "*.logdisk *.rawfloppy);;All files (*)";

MainWindow::MainWindow(h89session *session, QWidget *parent)
    : QMainWindow(parent), m_session(session)
{
    setWindowTitle(QStringLiteral("FujiNet Go H89"));
    /* the H19's 640 x 500 at 4:3, half again */
    resize(1000, 750 + 60);
    setAcceptDrops(true);

    m_display = new DisplayWidget(session, this);
    setCentralWidget(m_display);
    m_display->setFocus();

    m_diskDot = new QLabel(QStringLiteral("●"));
    m_diskDot->setToolTip(QStringLiteral("Drive activity"));
    m_dot = new QLabel(QStringLiteral("●"));
    m_dot->setToolTip(QStringLiteral("FujiNet link"));
    m_status = new QLabel(QStringLiteral("Starting..."));
    statusBar()->addWidget(m_diskDot);
    statusBar()->addWidget(m_dot);
    statusBar()->addWidget(m_status, 1);

    buildMenus();
    applyView();
    h89session_apply_view(session);

    connect(&m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer.start(1000);
    connect(&m_fastTimer, &QTimer::timeout, this, &MainWindow::fastTick);
    m_fastTimer.start(250);
    updateStatus();

    if (qEnvironmentVariableIsSet("H89_OPEN_KEYS"))
        toggleKeys();
    if (qEnvironmentVariableIsSet("H89_OPEN_DEBUGGER"))
        QTimer::singleShot(0, this, [this] { DebuggerWindow::showFor(this, m_session); });
    if (qEnvironmentVariableIsSet("H89_OPEN_SETTINGS"))
        QTimer::singleShot(0, this, &MainWindow::showSettings);
}

void MainWindow::buildMenus()
{
    QMenu *machine = menuBar()->addMenu(QStringLiteral("&Machine"));
    machine->addAction(QStringLiteral("&Reset (SHIFT+RESET)"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                       this, [this] { h89session_reset(m_session); });
    machine->addAction(QStringLiteral("&Power Cycle"), this, [this] { restart(false); });
    m_warpAction = machine->addAction(QStringLiteral("&Warp Speed"));
    m_warpAction->setCheckable(true);
    m_warpAction->setChecked(h89session_get_int(m_session, "machine.warp", 0) != 0);
    connect(m_warpAction, &QAction::triggered, this, [this](bool on) {
        h89session_set_warp(m_session, on);
        updateStatus();
    });
    machine->addAction(QStringLiteral("Import System &ROMs..."), this, [this] { importRoms(); });
    machine->addSeparator();
    machine->addAction(QStringLiteral("P&references..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), this,
                       &MainWindow::showSettings);
    machine->addSeparator();
    machine->addAction(QStringLiteral("&Quit"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), this,
                       [this] { close(); });

    m_drives = menuBar()->addMenu(QStringLiteral("&Drives"));
    connect(m_drives, &QMenu::aboutToShow, this, &MainWindow::fillDrivesMenu);
    fillDrivesMenu();

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(QStringLiteral("&Copy Screen Text"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this, [this] {
        static char buf[8192];
        if (h89session_screen_text(m_session, buf, sizeof buf, 1) == 0)
            return;
        QApplication::clipboard()->setText(QString::fromUtf8(buf));
        statusBar()->showMessage(QStringLiteral("The screen's text is on the clipboard"), 3000);
    });
    edit->addAction(QStringLiteral("&Paste"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), this, [this] {
        const QString t = QApplication::clipboard()->text();
        if (!t.isEmpty())
            h89session_paste(m_session, t.toUtf8().constData());
    });

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(QStringLiteral("H19 &Keys"), QKeySequence(Qt::Key_F9), this, &MainWindow::toggleKeys);
    view->addAction(QStringLiteral("&Debugger"), QKeySequence(Qt::Key_F12), this,
                    [this] { DebuggerWindow::toggleFor(this, m_session); });
    view->addSeparator();
    m_tvAction = view->addAction(QStringLiteral("&TV Aspect"));
    m_tvAction->setCheckable(true);
    m_tvAction->setToolTip(QStringLiteral("The tube's 4:3; off shows square pixels"));
    connect(m_tvAction, &QAction::triggered, this, [this](bool on) {
        h89session_set_int(m_session, "view.aspect", on ? 0 : 1);
        applyView();
    });
    m_smoothAction = view->addAction(QStringLiteral("&Smooth Scaling"));
    m_smoothAction->setCheckable(true);
    connect(m_smoothAction, &QAction::triggered, this, [this](bool on) {
        h89session_set_int(m_session, "view.smooth", on ? 1 : 0);
        applyView();
    });
    view->addAction(QStringLiteral("&Fullscreen"), QKeySequence(Qt::Key_F11), this, [this] {
        if (isFullScreen()) showNormal(); else showFullScreen();
    });

    QMenu *fuji = menuBar()->addMenu(QStringLiteral("&FujiNet"));
    fuji->addAction(QStringLiteral("&Configuration"), this, [this] {
        if (!h89session_fujinet_running(m_session)) {
            statusBar()->showMessage(QStringLiteral("FujiNet is not running"), 4000);
            return;
        }
        fujinet_config_show(this, m_session);
    });
    fuji->addAction(QStringLiteral("Console &Log"), this, [this] { fujinet_log_show(this, m_session); });
    fuji->addAction(QStringLiteral("&Restart FujiNet"), this, [this] {
        if (h89session_fujinet_restart(m_session) != 0)
            QMessageBox::warning(this, QStringLiteral("Cannot Restart FujiNet"),
                                 QString::fromUtf8(h89session_last_error(m_session)));
        else
            statusBar()->showMessage(QStringLiteral("FujiNet restarted"), 3000);
    });

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("&About FujiNet Go H89"), this, &MainWindow::showAbout);
}

/* Rebuilt each time it opens: the drives are the running machine's. */
void MainWindow::fillDrivesMenu()
{
    m_drives->clear();
    const int n = h89session_drive_count(m_session);
    int shown = 0;
    for (int i = 0; i < n; ++i) {
        h89_drive_info d;
        if (h89session_drive_info_at(m_session, i, &d) != 0 || !d.removable)
            continue;
        const QString file = d.loaded ? QFileInfo(QString::fromUtf8(d.path)).fileName()
                                      : QStringLiteral("empty");
        QMenu *sub = m_drives->addMenu(QStringLiteral("%1: %2").arg(QString::fromUtf8(d.label), file));
        const int id = d.id;
        sub->addAction(QStringLiteral("&Insert..."), this, [this, id] { chooseDisk(id); });
        if (d.loaded) {
            sub->addAction(QStringLiteral("&Eject"), this, [this, id] {
                char err[512];
                if (h89session_drive_eject(m_session, id, err, sizeof err) != 0)
                    QMessageBox::warning(this, QStringLiteral("Cannot Eject the Disk"), QString::fromUtf8(err));
            });
            QAction *wp = sub->addAction(QStringLiteral("&Write Protect"));
            wp->setCheckable(true);
            wp->setChecked(d.write_protected);
            wp->setEnabled(!d.read_only_format);
            connect(wp, &QAction::triggered, this,
                    [this, id](bool on) { h89session_drive_set_write_protect(m_session, id, on); });
        }
        ++shown;
    }
    if (!shown)
        m_drives->addAction(QStringLiteral("No drives (fit a controller in Preferences)"), this,
                            &MainWindow::showSettings);
    m_drives->addSeparator();
    m_drives->addAction(QStringLiteral("Insert &Disk..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this,
                        [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Insert a Disk"), disksFolder(),
                                                       QString::fromUtf8(kAnyFilter));
        if (!f.isEmpty()) loadMedia(f);
    });
    m_drives->addAction(QStringLiteral("&New Blank Disk..."), this, &MainWindow::newBlankDisk);
    m_drives->addAction(QStringLiteral("Open Disks &Folder"), this, [this] {
        const QString dir = QString::fromUtf8(h89session_disks_path(m_session));
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
}

QString MainWindow::disksFolder() const
{
    const QString last = QString::fromUtf8(h89session_get_str(m_session, "drives.last_dir", ""));
    if (!last.isEmpty() && QFileInfo(last).isDir())
        return last;
    return QString::fromUtf8(h89session_disks_path(m_session));
}

void MainWindow::chooseDisk(int id)
{
    h89_drive_info d;
    if (h89session_drive_info(m_session, id, &d) != 0)
        return;
    const bool h17 = H89_DRIVE_CTRL(id) == H89_CTRL_H17;
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("Insert a Disk in %1").arg(QString::fromUtf8(d.label)), disksFolder(),
        QString::fromUtf8(h17 ? kH17Filter : kSoftFilter));
    if (!f.isEmpty())
        insertInto(id, f);
}

void MainWindow::insertInto(int id, const QString &path)
{
    char err[512];
    if (h89session_drive_insert(m_session, id, path.toLocal8Bit().constData(), 0, err, sizeof err) != 0) {
        QMessageBox::warning(this, QStringLiteral("Cannot Insert the Disk"), QString::fromUtf8(err));
        return;
    }
    h89session_set_str(m_session, "drives.last_dir", QFileInfo(path).absolutePath().toLocal8Bit().constData());
    h89_drive_info d;
    if (h89session_drive_info(m_session, id, &d) == 0)
        statusBar()->showMessage(QStringLiteral("%1 is in %2")
                                     .arg(QFileInfo(path).fileName(), QString::fromUtf8(d.label)), 4000);
}

void MainWindow::loadMedia(const QString &path)
{
    const QByteArray p = path.toLocal8Bit();
    const int kind = h89session_media_kind(p.constData());
    if (kind == H89_MEDIA_ROMS) {
        importRoms({ path });
        return;
    }
    if (!h89session_is_running(m_session) && kind != H89_MEDIA_UNKNOWN) {
        statusBar()->showMessage(QStringLiteral("The H89 is off: import its ROMs first"), 5000);
        return;
    }
    char msg[1024];
    int ids[16], nids = 0;
    const int rc = h89session_import_media(m_session, p.constData(), msg, sizeof msg, ids, 16, &nids);
    if (rc < 0) {
        QMessageBox::warning(this, QStringLiteral("Cannot Use the File"), QString::fromUtf8(msg));
        return;
    }
    if (rc == 1) {
        QStringList labels;
        for (int i = 0; i < nids; ++i) {
            h89_drive_info d;
            labels << (h89session_drive_info(m_session, ids[i], &d) == 0 ? QString::fromUtf8(d.label)
                                                                         : QString::number(ids[i]));
        }
        bool ok = false;
        const QString pick = QInputDialog::getItem(
            this, QStringLiteral("Which Drive?"),
            QStringLiteral("Every drive that takes %1 has a disk in it; the one chosen is ejected first.")
                .arg(QFileInfo(path).fileName()),
            labels, 0, false, &ok);
        if (ok)
            insertInto(ids[labels.indexOf(pick)], path);
        return;
    }
    if (kind != H89_MEDIA_UNKNOWN)
        h89session_set_str(m_session, "drives.last_dir", QFileInfo(path).absolutePath().toLocal8Bit().constData());
    statusBar()->showMessage(QString::fromUtf8(msg), 5000);
}

void MainWindow::newBlankDisk()
{
    static const char *const kinds[] = {
        "H17: sector image (.h8d) — format it with HDOS INIT",
        "H17: H17Disk (.h17)",
        "H17: raw tracks (.h17raw)",
        "Soft-sectored: ImageDisk (.imd) — format it in the H89",
    };
    static const char *const exts[] = { "h8d", "h17", "h17raw", "imd" };
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("New Blank Disk"));
    auto *form = new QFormLayout(&dlg);
    auto *kind = new QComboBox;
    for (const char *k : kinds)
        kind->addItem(QString::fromUtf8(k));
    kind->setCurrentIndex(h89session_get_int(m_session, "blank.kind", 0));
    auto *sides = new QComboBox;
    sides->addItems({ QStringLiteral("1"), QStringLiteral("2") });
    sides->setCurrentIndex(h89session_get_int(m_session, "blank.sides", 1) == 2);
    auto *tracks = new QComboBox;
    tracks->addItems({ QStringLiteral("40"), QStringLiteral("80") });
    tracks->setCurrentIndex(h89session_get_int(m_session, "blank.tracks", 40) == 80);
    auto sync = [&] {
        const bool imd = kind->currentIndex() == H89_BLANK_IMD;
        sides->setEnabled(!imd);
        tracks->setEnabled(!imd);
    };
    connect(kind, &QComboBox::currentIndexChanged, &dlg, sync);
    sync();
    form->addRow(QStringLiteral("Kind"), kind);
    form->addRow(QStringLiteral("Sides"), sides);
    form->addRow(QStringLiteral("Tracks"), tracks);
    auto *note = new QLabel(QStringLiteral("An H17 disk is 40 tracks single sided for the H-17-1 drive, "
                                           "80 tracks double sided for the H-17-4. An ImageDisk takes "
                                           "whatever the H89 formats on it."));
    note->setWordWrap(true);
    form->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const int k = kind->currentIndex();
    const int s = sides->currentIndex() ? 2 : 1;
    const int t = tracks->currentIndex() ? 80 : 40;
    h89session_set_int(m_session, "blank.kind", k);
    h89session_set_int(m_session, "blank.sides", s);
    h89session_set_int(m_session, "blank.tracks", t);
    const QString dir = QString::fromUtf8(h89session_disks_path(m_session));
    QDir().mkpath(dir);
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Save the New Disk"),
                                                   dir + QStringLiteral("/blank.") + QString::fromUtf8(exts[k]));
    if (f.isEmpty())
        return;
    char err[512];
    if (h89session_new_blank_disk(m_session, f.toLocal8Bit().constData(), k, s, t, err, sizeof err) != 0) {
        QMessageBox::warning(this, QStringLiteral("Cannot Create the Disk"), QString::fromUtf8(err));
        return;
    }
    loadMedia(f);
}

/* ---- starting, ROMs, restarting -------------------------------------------- */

void MainWindow::start()
{
    if (h89session_is_running(m_session))
        return;
    h89session_start_opts opts;
    h89session_default_opts(m_session, &opts);
    if (h89session_start(m_session, &opts) == 0) {
        updateStatus();
        return;
    }
    char what[512];
    if (h89session_missing_roms(m_session, what, sizeof what) > 0) {
        QTimer::singleShot(0, this, &MainWindow::showRomSetup);
        return;
    }
    QMessageBox::warning(this, QStringLiteral("Cannot Start the H89"), QString::fromUtf8(h89session_last_error(m_session)));
    updateStatus();
}

void MainWindow::showRomSetup()
{
    char what[512];
    h89session_missing_roms(m_session, what, sizeof what);
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Import the H89's ROMs"));
    box.setText(QStringLiteral("<b>Import the H89's ROMs</b>"));
    box.setInformativeText(
        QStringLiteral("FujiNet Go H89 does not come with the Heath ROMs. Import them from MAME's h89.zip "
                       "and heath_tlb.zip, or from ROM image files: they are recognised by their contents "
                       "and kept in %1\n\nStill needed: %2.")
            .arg(QString::fromUtf8(h89session_roms_path(m_session)), QString::fromUtf8(what)));
    QPushButton *import = box.addButton(QStringLiteral("Import ROMs..."), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(import);
    box.exec();
    if (box.clickedButton() == import)
        importRoms();
    updateStatus();
}

void MainWindow::importRoms(const QStringList &paths)
{
    SettingsDialog::importRoms(this, m_session, paths);
    if (h89session_is_running(m_session))
        return;
    char what[512];
    if (h89session_missing_roms(m_session, what, sizeof what) == 0)
        start();
    else
        QTimer::singleShot(0, this, &MainWindow::showRomSetup);
}

void MainWindow::restart(bool host)
{
    if (!h89session_is_running(m_session)) {
        start();
        return;
    }
    if (host) {
        h89session_stop(m_session);
        start();
        if (h89session_is_running(m_session))
            statusBar()->showMessage(QStringLiteral("Restarted with the new options"), 4000);
        return;
    }
    if (h89session_power_cycle(m_session) != 0) {
        QMessageBox::warning(this, QStringLiteral("Cannot Power Cycle"),
                             QString::fromUtf8(h89session_last_error(m_session)));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Power cycled"), 4000);
}

void MainWindow::showSettings()
{
    const int r = SettingsDialog::run(this, m_session, [this] { applyView(); });
    applyView();
    if (r & SettingsDialog::Restart)
        restart(true);
    else if (r & SettingsDialog::PowerCycle)
        restart(false);
}

/* ---- view, status ------------------------------------------------------------ */

void MainWindow::applyView()
{
    const bool tv = h89session_get_int(m_session, "view.aspect", 0) == 0;
    const bool smooth = h89session_get_int(m_session, "view.smooth", 1) != 0;
    m_display->setTvAspect(tv);
    m_display->setSmooth(smooth);
    if (m_tvAction) m_tvAction->setChecked(tv);
    if (m_smoothAction) m_smoothAction->setChecked(smooth);
}

void MainWindow::updateStatus()
{
    QString text;
    bool on = false;
    if (!h89session_is_running(m_session)) {
        char what[512];
        text = h89session_missing_roms(m_session, what, sizeof what) > 0
                   ? QStringLiteral("Off — ROMs needed (Machine ▸ Import System ROMs)")
                   : QStringLiteral("Off");
    } else {
        const int link = h89session_card_link(m_session);
        on = link == 2;
        text = QString::fromUtf8(h89session_card_link_text(link));
        if (h89session_warp(m_session))
            text += QStringLiteral("  ·  WARP");
    }
    m_status->setText(text);
    m_dot->setStyleSheet(on ? QStringLiteral("color: %1;").arg(h89AccentColor().name())
                            : QStringLiteral("color: gray;"));
}

void MainWindow::fastTick()
{
    bool busy = false;
    const int n = h89session_drive_count(m_session);
    for (int i = 0; i < n && !busy; ++i) {
        h89_drive_info d;
        if (h89session_drive_info_at(m_session, i, &d) == 0 && d.active) {
            m_diskDot->setToolTip(QString::fromUtf8(d.label));
            busy = true;
        }
    }
    m_diskDot->setStyleSheet(busy ? QStringLiteral("color: %1;").arg(h89AccentColor().name())
                                  : QStringLiteral("color: gray;"));
    if (h89session_take_visual_bells(m_session) > 0 &&
        h89session_get_int(m_session, "sound.visual_bell", 0))
        m_display->flash();
}

void MainWindow::toggleKeys()
{
    if (!m_keys) m_keys = new H19KeysWindow(m_session, this);
    if (m_keys->isVisible()) m_keys->hide();
    else m_keys->show();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("About FujiNet Go H89"),
        QStringLiteral("<b>FujiNet Go H89</b> %1<br><br>"
                       "A Heathkit H89 with a FujiNet card.<br>"
                       "The emulator is VirtualH89 (BSD-3-Clause, Mark Garlanger and Douglas Miller), "
                       "adapted, with the FujiNet firmware (GPL-3.0-or-later) as the card's ESP32. "
                       "The Heath ROMs are not included: they are imported from MAME's sets.<br><br>"
                       "Copyright © 2026 Thomas Cherryhomes — GPL-3.0-or-later<br>"
                       "<a href=\"https://fujinet.online/\">fujinet.online</a>")
            .arg(QStringLiteral(H89_VERSION_STRING)));
}

/* ---- events ------------------------------------------------------------------- */

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (!h89ForwardKey(m_session, e))
        QMainWindow::keyPressEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (!path.isEmpty()) loadMedia(path);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_statusTimer.stop();
    m_fastTimer.stop();
    QMainWindow::closeEvent(e);
}
