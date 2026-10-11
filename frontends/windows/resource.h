/* Menu command and resource IDs.
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#define IDI_APPICON        101

#define IDM_RESET          1001
#define IDM_POWER_CYCLE    1002
#define IDM_WARP           1003
#define IDM_IMPORT_ROMS    1004
#define IDM_SETTINGS       1005
#define IDM_EXIT           1006
#define IDM_INSERT_DISK    1010
#define IDM_NEW_DISK       1011
#define IDM_DISKS_FOLDER   1012
#define IDM_COPY_SCREEN    1020
#define IDM_PASTE          1021
#define IDM_KEYS           1030
#define IDM_DEBUGGER       1031
#define IDM_TV_ASPECT      1032
#define IDM_SMOOTH         1033
#define IDM_FULLSCREEN     1034
#define IDM_FUJINET_CONFIG 1040
#define IDM_FUJINET_LOG    1041
#define IDM_FUJINET_RESTART 1042
#define IDM_ABOUT          1050

/* the Drives menu's per-drive items: IDM_DRIVE_BASE + id * 4 + action */
#define IDM_DRIVE_BASE     4000
#define DRIVE_INSERT       0
#define DRIVE_EJECT        1
#define DRIVE_WP           2
/* the "which drive?" popup: IDM_CHOOSE_BASE + id */
#define IDM_CHOOSE_BASE    6000

#define IDC_STATUSBAR      2200
#define IDC_LOG_EDIT       2100
#define IDT_STATUS         1
#define IDT_FAST           2
#define IDT_LOG_REFRESH    3
#define IDT_FLASH          4
