/*
 * FujiNet Go H89 -- the KDE (Qt6 Widgets) frontend.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QIcon>

#include "MainWindow.h"
#include "h89session.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("fujinet-go-h89-kde"));
    app.setApplicationDisplayName(QStringLiteral("FujiNet Go H89"));
    app.setDesktopFileName(QStringLiteral("online.fujinet.go.h89.kde"));
    /* The installed icon is named after the desktop-entry id; running out
     * of the build tree, the in-tree artwork stands in. */
    {
        QIcon icon = QIcon::fromTheme(QStringLiteral("online.fujinet.go.h89.kde"));
        if (icon.isNull()) {
            QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(H89_SOURCE_ICON_DIR));
            icon = QIcon::fromTheme(QStringLiteral("fujinet-go-h89"));
        }
        if (!icon.isNull())
            app.setWindowIcon(icon);
    }

    h89session *session = h89session_new(nullptr);
    if (!session) {
        qCritical("Could not create the session (unusable config/data dirs?)");
        return 1;
    }

    int rc;
    /* the windows go before the session does: they call into it as they
     * close */
    {
        MainWindow win(session);
        win.show();
        /* FujiNet first, then the machine; without its ROMs the window asks
         * for them instead */
        win.start();
        /* a path on the command line is routed like a dropped file */
        if (argc > 1)
            win.loadMedia(QString::fromLocal8Bit(argv[1]));
        rc = app.exec();
    }
    h89session_stop(session);
    h89session_free(session);
    return rc;
}
