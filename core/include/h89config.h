/*
 * h89config -- the machine a session builds: model, memory, monitor, the
 * switches, and what is in each slot. One plain struct, filled from the
 * settings store by the session (core/src/config.c) and laid out by every
 * frontend's Preferences, so the frontends never have to know which
 * combinations make sense -- h89_config_validate() says.
 *
 * Disks are not part of the configuration: they are inserted into and
 * ejected from the drives the configuration provides, at any time
 * (h89session.h, "drives").
 *
 * Every option here is one VirtualH89 supports (its .v89rc keys are noted),
 * plus the FujiNet card.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef H89CONFIG_H
#define H89CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The CPU board's memory decoding. */
typedef enum {
    H89_MODEL_H89 = 0,       /* ORG-0 decoder: CP/M can map RAM at 0 (GPP bit 5) */
    H89_MODEL_H88,           /* no ORG-0 (VirtualH89's ORG0=False) */
    H89_MODEL_COUNT
} h89_model;

/* RAM on the CPU board (CPU_Memory). */
typedef enum {
    H89_MEM_16K = 0,
    H89_MEM_32K,
    H89_MEM_48K,
    H89_MEM_64K,
    H89_MEM_COUNT
} h89_memory;

/* The memory slot P503 (slot_p503). */
typedef enum {
    H89_P503_NONE = 0,       /* only what CPU_Memory says */
    H89_P503_WH8816,         /* WH-88-16 / MMS 77311: makes it 64K */
    H89_P503_MMS77318,       /* MMS 77318: 128K, banked through the GPP */
    H89_P503_COUNT
} h89_p503;

/* The right-side I/O slots P504, P505, P506 (slot_p504..506). */
typedef enum {
    H89_CARD_EMPTY = 0,
    H89_CARD_FUJINET,        /* the H89-ESP32 FujiNet card, 0xD0-0xD3 */
    H89_CARD_H17,            /* H-88-1 hard-sectored controller, 0x7C */
    H89_CARD_H37,            /* Z-89-37 soft-sectored controller, 0x78 */
    H89_CARD_MMS77316,       /* MMS 77316 double-density controller, 0x38 */
    H89_CARD_MMS77320,       /* MMS 77320 SASI hard disk, 0x78 or 0x7C */
    H89_CARD_H88_3,          /* HA-88-3 three-port serial: 0xD0, 0xD8, 0xE0 */
    H89_CARD_COUNT
} h89_card;

#define H89_SLOTS 3          /* P504, P505, P506 */

/* H17 drives (h17_drive1..3). */
typedef enum {
    H89_H17DRV_NONE = 0,
    H89_H17DRV_H17_1,        /* H-17-1: 40 tracks, single sided */
    H89_H17DRV_H17_4,        /* H-17-4: 80 tracks, double sided */
    H89_H17DRV_COUNT
} h89_h17_drive;

/* Soft-sectored drives on the H37 and the MMS 77316 (h37_drive1..4,
 * mms77316_drive1..8). */
typedef enum {
    H89_FDD_NONE = 0,
    H89_FDD_5_SS_ST,         /* 5.25" single sided, 40 tracks */
    H89_FDD_5_SS_DT,         /* 5.25" single sided, 80 tracks */
    H89_FDD_5_DS_ST,         /* 5.25" double sided, 40 tracks */
    H89_FDD_5_DS_DT,         /* 5.25" double sided, 80 tracks */
    H89_FDD_8_SS,            /* 8" single sided, 77 tracks */
    H89_FDD_8_DS,            /* 8" double sided */
    H89_FDD_COUNT
} h89_fdd;

/* SASI drives behind the MMS 77320 (mms77320_drive0..7). */
typedef enum {
    H89_SASI_NONE = 0,
    H89_SASI_XEBEC_ST506,
    H89_SASI_XEBEC_ST412,
    H89_SASI_XEBEC_CM5206,
    H89_SASI_XEBEC_CM5410,
    H89_SASI_XEBEC_CM5616,
    H89_SASI_XEBEC_RO201,
    H89_SASI_XEBEC_RO202,
    H89_SASI_XEBEC_RO203,
    H89_SASI_XEBEC_RO204,
    H89_SASI_COUNT
} h89_sasi;

#define H89_H17_DRIVES     3
#define H89_H37_DRIVES     4
#define H89_MMS316_DRIVES  8  /* units 1-4: 8", 5-8: 5.25" */
#define H89_MMS320_DRIVES  8

typedef struct {
    int model;               /* h89_model */
    int memory;              /* h89_memory */
    int p503;                /* h89_p503 */
    char monitor[64];        /* the monitor ROM's file name (roms table) */
    int speedup;             /* z80_speedup_option: 0 = none, 2..40 */

    uint8_t sw501;           /* the CPU board's DIP switch (GPP input) */
    int sw501_auto;          /* derive SW501's device bits from the slots */
    uint8_t sw401;           /* the H19's switches (MAME's polarity) */
    uint8_t sw402;

    int slot[H89_SLOTS];     /* h89_card */

    int h17_drive[H89_H17_DRIVES];        /* h89_h17_drive */
    int h37_drive[H89_H37_DRIVES];        /* h89_fdd */
    int mms316_drive[H89_MMS316_DRIVES];  /* h89_fdd */

    int mms320_port;         /* 0x78 (JP1A) or 0x7C (JP1B) */
    int mms320_intr;         /* 0, 3 (JP2A), 4 (JP2B), 5 (JP2C) */
    uint8_t mms320_dipsw;
    int mms320_drive[H89_MMS320_DRIVES];  /* h89_sasi */

    int cpnet_enabled;       /* the CP/NET device (cpnetdevice_port) */
    int cpnet_port;
    int cpnet_clientid;
    char cpnet_root[1024];   /* the host folder served as CP/M drives */

    int fujinet_ack_delay;   /* card: ESP32 /ACK latency in cycles */
} h89_config;

/* The default machine: an H89 with 64K and ORG-0, MTR-90, the FujiNet card
 * in P504 and an H17 with three H-17-1 drives in P506; SW501 derived. */
void h89_config_default(h89_config *cfg);

/* 1 if the configuration can be built; else 0 and why (a sentence for the
 * Preferences dialog). Checks port and interrupt conflicts between cards,
 * each card at most once, the speed-up against the MMS 77318, and that the
 * monitor is set. */
int h89_config_validate(const h89_config *cfg, char *why, int whysz);

/* SW501 as MTR-89/MTR-90 want it for the cards in the slots (bits 0-1:
 * the controller at 174Q, bits 2-3: at 170Q, bit 4: boot from 170Q), with
 * the remaining bits (memory test, baud, auto-boot) taken from `sw501`. */
uint8_t h89_config_auto_sw501(const h89_config *cfg, uint8_t sw501);

/* The SW501 the machine is built with (auto or as set). */
uint8_t h89_config_effective_sw501(const h89_config *cfg);

/* Names for the choices, for the Preferences dialogs; NULL past the end. */
const char *h89_model_name(int model);
const char *h89_memory_name(int memory);
const char *h89_p503_name(int p503);
const char *h89_card_name(int card);
const char *h89_h17_drive_name(int drv);
const char *h89_fdd_name(int drv);
const char *h89_sasi_name(int drv);
/* The VirtualH89 type strings (DiskDrive::getInstance and friends). */
const char *h89_fdd_type_id(int drv);
const char *h89_sasi_type_id(int drv);

/* SW501 as the selected monitor reads it, as fields for the Preferences
 * dialogs (MAME's per-monitor tables): each field covers `mask` and takes
 * one of its options' values. `monitor` is the monitor ROM's file name. */
int h89_sw501_field_count(const char *monitor);
const char *h89_sw501_field_name(const char *monitor, int field);
uint8_t h89_sw501_field_mask(const char *monitor, int field);
int h89_sw501_option_count(const char *monitor, int field);
const char *h89_sw501_option_name(const char *monitor, int field, int option);
uint8_t h89_sw501_option_value(const char *monitor, int field, int option);
/* The bits h89_config_auto_sw501 decides for this monitor (the device and
 * boot-device fields); the others stay as set. */
uint8_t h89_sw501_auto_mask(const char *monitor);

/* The configuration as key/value pairs: the session's settings store keeps
 * each field under its key, and h89_headless takes them with --set. Keys:
 *   machine.model (h89|h88)  machine.memory (16|32|48|64)
 *   machine.p503 (none|wh8816|mms77318)  machine.monitor (ROM file name)
 *   machine.speedup (0|2..40)
 *   switches.sw501 / sw401 / sw402 (hex)  switches.sw501_auto (0|1)
 *   slot.p504 / slot.p505 / slot.p506 (empty|fujinet|h17|h37|mms77316|mms77320|h88-3)
 *   h17.drive1..3 (none|h17-1|h17-4)
 *   h37.drive1..4, mms77316.drive1..8 (none|5ss40|5ss80|5ds40|5ds80|8ss|8ds)
 *   mms77320.port (0x78|0x7c)  mms77320.intr (0|3|4|5)  mms77320.dipsw (hex)
 *   mms77320.drive0..7 (none|st506|st412|cm5206|cm5410|cm5616|ro201..ro204)
 *   cpnet.enabled (0|1)  cpnet.port  cpnet.clientid (hex)  cpnet.root (folder)
 *   fujinet.ack_delay (cycles)
 * set returns 0, or -1 for an unknown key or a value it cannot parse. */
int h89_config_key_count(void);
const char *h89_config_key_at(int i);
int h89_config_set(h89_config *cfg, const char *key, const char *value);
int h89_config_get(const h89_config *cfg, const char *key, char *buf, int bufsz);

/* Card names as settings keys ("fujinet", "h17", ...) and back. */
const char *h89_card_key(int card);
int h89_card_from_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* H89CONFIG_H */
