/*
 * SettingsDialog -- the Preferences. The view and sound settings apply as
 * they change; the machine's configuration is edited as a whole and checked
 * (h89_config_validate) on OK, which will not close on one that cannot be
 * built. What the caller should then do comes back from run().
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>

#include "h89session.h"

class SettingsDialog {
public:
    enum Outcome {
        Nothing = 0,      /* nothing to apply now (or Later was chosen) */
        PowerCycle = 1,   /* the configuration changed and a power cycle was asked for */
        Restart = 2,      /* FujiNet or audio was switched: stop and start the session */
    };
    /* `applyView` re-reads the view settings (aspect, smoothing) as they
     * change. Returns an Outcome. */
    static int run(QWidget *parent, h89session *session, std::function<void()> applyView);
    /* Imports ROMs from `paths` (a file chooser when empty), shows what
     * happened, and returns how many images were imported. */
    static int importRoms(QWidget *parent, h89session *session, QStringList paths = QStringList());
};
