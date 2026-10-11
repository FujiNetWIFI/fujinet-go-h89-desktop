/*
 * The main window.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QTimer>

#include "h89session.h"

class DisplayWidget;
class H19KeysWindow;
class QAction;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(h89session *session, QWidget *parent = nullptr);
    /* Starts the machine (FujiNet first); without its ROMs, asks for them. */
    void start();
    /* A dropped (or command-line) file: ROMs are imported, a disk goes in a
     * drive that takes it (asking which when they are all full), anything
     * else is copied to FujiNet's SD folder. */
    void loadMedia(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void fillDrivesMenu();
    void updateStatus();
    void fastTick();
    void applyView();
    void showSettings();
    void restart(bool host);
    void showRomSetup();
    void importRoms(const QStringList &paths = QStringList());
    void insertInto(int id, const QString &path);
    void chooseDisk(int id);
    void newBlankDisk();
    void toggleKeys();
    void showAbout();
    QString disksFolder() const;

    h89session *m_session;
    DisplayWidget *m_display = nullptr;
    H19KeysWindow *m_keys = nullptr;
    QMenu *m_drives = nullptr;
    QLabel *m_dot = nullptr;
    QLabel *m_diskDot = nullptr;
    QLabel *m_status = nullptr;
    QTimer m_statusTimer;
    QTimer m_fastTimer;
    QAction *m_tvAction = nullptr;
    QAction *m_smoothAction = nullptr;
    QAction *m_warpAction = nullptr;
};
