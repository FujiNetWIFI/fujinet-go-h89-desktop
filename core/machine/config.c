/*
 * config -- see h89config.h.
 *
 * The SW501 tables are MAME's (src/mame/heathzenith/h89.cpp, the per-BIOS
 * input port conditions), restated as data.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89config.h"

#include <stdio.h>
#include <string.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- names ------------------------------------------------------------- */

static const char *const s_models[] = {"H89 (ORG-0)", "H88 (no ORG-0)"};
static const char *const s_memory[] = {"16K", "32K", "48K", "64K"};
static const char *const s_p503[] = {"None", "WH-88-16 (64K)", "MMS 77318 (128K)"};
static const char *const s_cards[] = {
    "Empty", "FujiNet (H89-ESP32)", "H-88-1 (H17 hard-sectored)",
    "Z-89-37 (H37 soft-sectored)", "MMS 77316 (double density)",
    "MMS 77320 (SASI hard disk)", "HA-88-3 (3-port serial)",
};
static const char *const s_card_keys[] = {
    "empty", "fujinet", "h17", "h37", "mms77316", "mms77320", "h88-3",
};
static const char *const s_h17drv[] = {"None", "H-17-1 (SS, 40 tracks)", "H-17-4 (DS, 80 tracks)"};
static const char *const s_fdd[] = {
    "None", "5.25\" SS 40 tracks", "5.25\" SS 80 tracks", "5.25\" DS 40 tracks",
    "5.25\" DS 80 tracks", "8\" SS", "8\" DS",
};
static const char *const s_fdd_ids[] = {
    "", "FDD_5_25_SS_ST", "FDD_5_25_SS_DT", "FDD_5_25_DS_ST", "FDD_5_25_DS_DT",
    "FDD_8_SS", "FDD_8_DS",
};
static const char *const s_sasi[] = {
    "None", "XEBEC ST506 (5MB)", "XEBEC ST412 (10MB)", "XEBEC CM5206 (5MB)",
    "XEBEC CM5410 (10MB)", "XEBEC CM5616 (15MB)", "XEBEC RO201 (5MB)",
    "XEBEC RO202 (10MB)", "XEBEC RO203 (15MB)", "XEBEC RO204 (20MB)",
};
static const char *const s_sasi_ids[] = {
    "", "XEBEC_ST506", "XEBEC_ST412", "XEBEC_CM5206", "XEBEC_CM5410",
    "XEBEC_CM5616", "XEBEC_RO201", "XEBEC_RO202", "XEBEC_RO203", "XEBEC_RO204",
};

#define NAME(tab, i) (((i) >= 0 && (i) < COUNT(tab)) ? (tab)[i] : NULL)

const char *h89_model_name(int i) { return NAME(s_models, i); }
const char *h89_memory_name(int i) { return NAME(s_memory, i); }
const char *h89_p503_name(int i) { return NAME(s_p503, i); }
const char *h89_card_name(int i) { return NAME(s_cards, i); }
const char *h89_h17_drive_name(int i) { return NAME(s_h17drv, i); }
const char *h89_fdd_name(int i) { return NAME(s_fdd, i); }
const char *h89_sasi_name(int i) { return NAME(s_sasi, i); }
const char *h89_fdd_type_id(int i) { return NAME(s_fdd_ids, i); }
const char *h89_sasi_type_id(int i) { return NAME(s_sasi_ids, i); }
const char *h89_card_key(int i) { return NAME(s_card_keys, i); }

int h89_card_from_key(const char *key)
{
    for (int i = 0; key && i < COUNT(s_card_keys); i++)
        if (strcmp(key, s_card_keys[i]) == 0)
            return i;
    return -1;
}

/* ---- defaults ------------------------------------------------------------ */

void h89_config_default(h89_config *c)
{
    memset(c, 0, sizeof *c);
    c->model = H89_MODEL_H89;
    c->memory = H89_MEM_64K;
    c->p503 = H89_P503_NONE;
    snprintf(c->monitor, sizeof c->monitor, "%s", "2732_444-142_mtr90.u518");
    c->speedup = 0;
    c->sw501 = 0x20;            /* skip the memory test; 9600 baud; no auto-boot */
    c->sw501_auto = 1;
    c->sw401 = 0x8c;            /* 9600 baud, full duplex (MAME's default) */
    c->sw402 = 0x00;            /* underline cursor, key click, no wrap */
    c->slot[0] = H89_CARD_FUJINET;
    c->slot[1] = H89_CARD_EMPTY;
    c->slot[2] = H89_CARD_H17;
    for (int i = 0; i < H89_H17_DRIVES; i++)
        c->h17_drive[i] = H89_H17DRV_H17_1;
    for (int i = 0; i < H89_H37_DRIVES; i++)
        c->h37_drive[i] = H89_FDD_5_DS_DT;
    /* units 1-4 are the 8" bank, 5-8 the 5.25" one */
    for (int i = 0; i < 4; i++)
        c->mms316_drive[i] = i < 2 ? H89_FDD_8_DS : H89_FDD_NONE;
    for (int i = 4; i < H89_MMS316_DRIVES; i++)
        c->mms316_drive[i] = H89_FDD_5_DS_DT;
    c->mms320_port = 0x7c;
    c->mms320_intr = 5;
    c->mms320_dipsw = 0;
    c->mms320_drive[0] = H89_SASI_XEBEC_ST506;
    c->cpnet_enabled = 0;
    c->cpnet_port = 0x18;
    c->cpnet_clientid = 0xfe;
    c->fujinet_ack_delay = 40;
}

/* ---- validation ---------------------------------------------------------- */

static int count_card(const h89_config *c, int card)
{
    int n = 0;
    for (int i = 0; i < H89_SLOTS; i++)
        n += c->slot[i] == card;
    return n;
}

typedef struct {
    int lo, hi;              /* inclusive port range */
    const char *who;
} port_use;

static int add_use(port_use *u, int n, int lo, int hi, const char *who)
{
    u[n].lo = lo;
    u[n].hi = hi;
    u[n].who = who;
    return n + 1;
}

int h89_config_validate(const h89_config *c, char *why, int whysz)
{
    port_use use[16];
    int n = 0;

#define FAIL(...) do { if (why && whysz > 0) snprintf(why, whysz, __VA_ARGS__); return 0; } while (0)

    if ((unsigned)c->model >= H89_MODEL_COUNT || (unsigned)c->memory >= H89_MEM_COUNT ||
        (unsigned)c->p503 >= H89_P503_COUNT)
        FAIL("The machine model or memory size is not one this app knows.");
    if (!c->monitor[0])
        FAIL("No monitor ROM is selected. Import the H89 ROMs, then choose a monitor.");
    if (c->speedup > 40 || c->speedup < 0)
        FAIL("The CPU speed-up must be between 2x and 40x.");
    if (c->speedup > 1 && c->p503 == H89_P503_MMS77318)
        FAIL("The CPU speed-up board and the MMS 77318 both use GPP bits 2-5; "
             "they cannot be fitted together.");

    for (int card = H89_CARD_FUJINET; card < H89_CARD_COUNT; card++)
        if (count_card(c, card) > 1)
            FAIL("%s is in more than one slot.", s_cards[card]);
    for (int i = 0; i < H89_SLOTS; i++)
        if ((unsigned)c->slot[i] >= H89_CARD_COUNT)
            FAIL("Slot P%d holds a card this app does not know.", 504 + i);

    /* the motherboard's own ports */
    n = add_use(use, n, 0xe8, 0xef, "the console port");
    n = add_use(use, n, 0xf0, 0xf1, "the H8 front-panel (NMI) ports");
    n = add_use(use, n, 0xf2, 0xf2, "the general-purpose port");
    n = add_use(use, n, 0xfa, 0xfb, "the H8 front-panel (NMI) ports");

    for (int i = 0; i < H89_SLOTS; i++)
    {
        switch (c->slot[i])
        {
        case H89_CARD_FUJINET:
            n = add_use(use, n, 0xd0, 0xd3, "the FujiNet card");
            break;
        case H89_CARD_H17:
            n = add_use(use, n, 0x7c, 0x7f, "the H17 controller");
            break;
        case H89_CARD_H37:
            n = add_use(use, n, 0x78, 0x7b, "the H37 controller");
            break;
        case H89_CARD_MMS77316:
            n = add_use(use, n, 0x38, 0x3f, "the MMS 77316");
            break;
        case H89_CARD_MMS77320:
            if (c->mms320_port != 0x78 && c->mms320_port != 0x7c)
                FAIL("The MMS 77320's port must be 0x78 (JP1A) or 0x7C (JP1B).");
            n = add_use(use, n, c->mms320_port, c->mms320_port + 2, "the MMS 77320");
            break;
        case H89_CARD_H88_3:
            n = add_use(use, n, 0xd0, 0xd7, "the HA-88-3's AUX port");
            n = add_use(use, n, 0xd8, 0xdf, "the HA-88-3's modem port");
            n = add_use(use, n, 0xe0, 0xe7, "the HA-88-3's printer port");
            break;
        default:
            break;
        }
    }
    if (c->cpnet_enabled)
    {
        if (c->cpnet_port < 0 || c->cpnet_port > 0xfe)
            FAIL("The CP/NET device's port must be 0x00-0xFE.");
        if (c->cpnet_clientid < 1 || c->cpnet_clientid > 0xfe)
            FAIL("The CP/NET client ID must be 01-FE.");
        if (!c->cpnet_root[0])
            FAIL("CP/NET is on but no host folder is chosen.");
        n = add_use(use, n, c->cpnet_port, c->cpnet_port + 1, "the CP/NET device");
    }

    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (use[i].lo <= use[j].hi && use[j].lo <= use[i].hi)
                FAIL("Port %02XH is claimed by both %s and %s.",
                     use[i].lo > use[j].lo ? use[i].lo : use[j].lo, use[i].who, use[j].who);

    if (count_card(c, H89_CARD_MMS77320) && c->mms320_intr)
    {
        if (c->mms320_intr != 3 && c->mms320_intr != 4 && c->mms320_intr != 5)
            FAIL("The MMS 77320's interrupt must be 3, 4 or 5 (JP2A-C), or none.");
        if (c->mms320_intr == 3)
            FAIL("Interrupt 3 is the console's; strap the MMS 77320 to 4 or 5, or none.");
        if (c->mms320_intr == 4 && count_card(c, H89_CARD_H37))
            FAIL("Interrupt 4 is the H37's; strap the MMS 77320 to 5, or none.");
    }
    return 1;
#undef FAIL
}

/* ---- SW501, per monitor -------------------------------------------------- */

typedef enum {
    MON_GENERIC = 0, MON_MTR88, MON_MTR89, MON_MTR90, MON_MMS, MON_KMR,
    MON_ULT4K, MON_ULT2K, MON_SIGMA,
} mon_family;

static mon_family family_of(const char *monitor)
{
    static const struct { const char *file; mon_family f; } map[] = {
        {"2716_444-40_mtr88.u518", MON_MTR88},
        {"2716_444-62_mtr89.u518", MON_MTR89},
        {"2732_444-142_mtr90.u518", MON_MTR90},
        {"2732_444-84_mtr90.u518", MON_MTR90},
        {"2732_444_84b_mms.u518", MON_MMS},
        {"2732_444_84a_mms.u518", MON_MMS},
        {"2732_kmr100_v3_a_02.u518", MON_KMR},
        {"2732_mtrhex_4k.u518", MON_ULT4K},
        {"2732_ets_4k.u518", MON_ULT4K},
        {"2716_mtrhex.u518", MON_ULT2K},
        {"2732_sigma_rom_v_1.3.bin", MON_SIGMA},
    };
    for (int i = 0; monitor && i < COUNT(map); i++)
        if (strcmp(monitor, map[i].file) == 0)
            return map[i].f;
    return MON_GENERIC;
}

typedef struct {
    const char *name;
    uint8_t mask;
    int nopts;
    const char *opt[16];
    uint8_t val[16];
} sw_field;

#define NOYES(mask_) {"Perform memory test at start", mask_, 2, {"No", "Yes"}, {mask_, 0}}
#define BOOTMODE(mask_) {"Boot mode", mask_, 2, {"Normal", "Auto"}, {0, mask_}}
#define BAUD2(mask_) {"Console baud rate", mask_, 2, {"9600", "19200"}, {0, mask_}}

static const sw_field f_generic[] = {
    {"Switch 1", 0x01, 2, {"Off", "On"}, {0, 0x01}},
    {"Switch 2", 0x02, 2, {"Off", "On"}, {0, 0x02}},
    {"Switch 3", 0x04, 2, {"Off", "On"}, {0, 0x04}},
    {"Switch 4", 0x08, 2, {"Off", "On"}, {0, 0x08}},
    {"Switch 5", 0x10, 2, {"Off", "On"}, {0, 0x10}},
    {"Switch 6", 0x20, 2, {"Off", "On"}, {0, 0x20}},
    {"Switch 7", 0x40, 2, {"Off", "On"}, {0, 0x40}},
    {"Switch 8", 0x80, 2, {"Off", "On"}, {0, 0x80}},
};
static const sw_field f_mtr88[] = {
    NOYES(0x20),
    {"Console baud rate", 0xc0, 4, {"9600", "19200", "38400", "57600"}, {0x00, 0x40, 0x80, 0xc0}},
};
static const sw_field f_mtr89[] = {
    {"Disk I/O #2 (port 174Q)", 0x03, 4, {"H-88-1", "H/Z-47", "Undefined", "Undefined (3)"}, {0, 1, 2, 3}},
    {"Disk I/O #1 (port 170Q)", 0x0c, 4, {"Unused", "H/Z-47", "Undefined", "Undefined (3)"}, {0, 4, 8, 12}},
    {"Primary boot from", 0x10, 2, {"Disk I/O #2", "Disk I/O #1"}, {0, 0x10}},
    NOYES(0x20), BAUD2(0x40), BOOTMODE(0x80),
};
static const sw_field f_mtr90[] = {
    {"Disk I/O #2 (port 174Q)", 0x03, 4, {"H-88-1", "H/Z-47", "Z-67", "Undefined"}, {0, 1, 2, 3}},
    {"Disk I/O #1 (port 170Q)", 0x0c, 4, {"H-89-37", "H/Z-47", "Z-67", "Undefined"}, {0, 4, 8, 12}},
    {"Primary boot from", 0x10, 2, {"Disk I/O #2", "Disk I/O #1"}, {0, 0x10}},
    NOYES(0x20), BAUD2(0x40), BOOTMODE(0x80),
};
static const sw_field f_mms[] = {
    {"Disk I/O #2 (port 174Q)", 0x03, 4,
     {"H-88-1", "H/Z-47", "MMS 77320 SASI or Z-67", "MMS 77422 network"}, {0, 1, 2, 3}},
    {"Disk I/O #1 (port 170Q)", 0x0c, 4,
     {"H-89-37", "H/Z-47", "MMS 77320 SASI or Z-67", "MMS 77422 network"}, {0, 4, 8, 12}},
    {"Default boot device", 0x70, 8,
     {"MMS 77316 5.25\"", "MMS 77316 8\"", "Disk device at 0x7C", "Disk device at 0x78",
      "Reserved (4)", "Reserved (5)", "MMS network (77422)", "MMS I/O board config port"},
     {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70}},
    BOOTMODE(0x80),
};
static const sw_field f_kmr[] = {
    {"Default boot device", 0x0f, 16,
     {"H-17 5.25\" units 0-2", "H-37 5.25\" units 0-3", "Corvus / Magnolia",
      "CDR 5.25\"/8\"", "H-47 at 0x78", "H-47 at 0x7C", "Reserved (6)",
      "Help - list boot devices", "Reserved (8)", "SASI or Z-67 at 0x78",
      "SASI or Z-67 at 0x7C", "Livingston 8\"", "Magnolia 5.25\"/8\" (77316)",
      "Reserved (13)", "Reserved (14)", "Magnolia 128K pseudo disk"},
     {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    {"Map ROM into RAM on boot", 0x10, 2, {"Yes", "No"}, {0, 0x10}},
    NOYES(0x20),
    {"LLL controller installed", 0x40, 2, {"No", "Yes"}, {0, 0x40}},
    BOOTMODE(0x80),
};
static const sw_field f_ult2k[] = {
    {"Default boot device", 0x03, 4,
     {"H-17 5.25\"", "H-47 at 0x78", "H-47 at 0x7C", "Magnolia 5.25\"/8\" (77316)"}, {0, 1, 2, 3}},
    NOYES(0x20),
    {"Boot mode", 0x40, 2, {"Normal", "Auto"}, {0, 0x40}},
    {"Console baud rate", 0x80, 2, {"9600", "19200"}, {0, 0x80}},
};
static const sw_field f_sigma[] = {
    {"Disk I/O #2 (port 174Q)", 0x03, 4,
     {"H-88-1", "H/Z-47", "WD1002 hard disk", "WD1002 floppy"}, {0, 1, 2, 3}},
    {"Disk I/O #1 (port 170Q)", 0x0c, 4,
     {"H-89-37", "H/Z-47", "WD1002 hard disk", "WD1002 floppy"}, {0, 4, 8, 12}},
    {"Primary boot from", 0x10, 2, {"Disk I/O #2", "Disk I/O #1"}, {0, 0x10}},
    {"Reserved (must be selected)", 0x20, 2, {"Selected", "Not selected"}, {0x20, 0}},
    BAUD2(0x40), BOOTMODE(0x80),
};

static const sw_field *fields_of(const char *monitor, int *n)
{
    switch (family_of(monitor))
    {
    case MON_MTR88: *n = COUNT(f_mtr88); return f_mtr88;
    case MON_MTR89: *n = COUNT(f_mtr89); return f_mtr89;
    case MON_MTR90: *n = COUNT(f_mtr90); return f_mtr90;
    case MON_MMS:   *n = COUNT(f_mms);   return f_mms;
    case MON_KMR:
    case MON_ULT4K: *n = COUNT(f_kmr);   return f_kmr;
    case MON_ULT2K: *n = COUNT(f_ult2k); return f_ult2k;
    case MON_SIGMA: *n = COUNT(f_sigma); return f_sigma;
    default:        *n = COUNT(f_generic); return f_generic;
    }
}

int h89_sw501_field_count(const char *monitor)
{
    int n;
    fields_of(monitor, &n);
    return n;
}

static const sw_field *field_at(const char *monitor, int field)
{
    int n;
    const sw_field *f = fields_of(monitor, &n);
    return (field >= 0 && field < n) ? &f[field] : NULL;
}

const char *h89_sw501_field_name(const char *monitor, int field)
{
    const sw_field *f = field_at(monitor, field);
    return f ? f->name : NULL;
}

uint8_t h89_sw501_field_mask(const char *monitor, int field)
{
    const sw_field *f = field_at(monitor, field);
    return f ? f->mask : 0;
}

int h89_sw501_option_count(const char *monitor, int field)
{
    const sw_field *f = field_at(monitor, field);
    return f ? f->nopts : 0;
}

const char *h89_sw501_option_name(const char *monitor, int field, int option)
{
    const sw_field *f = field_at(monitor, field);
    return (f && option >= 0 && option < f->nopts) ? f->opt[option] : NULL;
}

uint8_t h89_sw501_option_value(const char *monitor, int field, int option)
{
    const sw_field *f = field_at(monitor, field);
    return (f && option >= 0 && option < f->nopts) ? f->val[option] : 0;
}

uint8_t h89_sw501_auto_mask(const char *monitor)
{
    switch (family_of(monitor))
    {
    case MON_MTR89:
    case MON_MTR90:
    case MON_SIGMA: return 0x1f;
    case MON_MMS:   return 0x7f;
    case MON_KMR:
    case MON_ULT4K: return 0x0f;
    case MON_ULT2K: return 0x03;
    default:        return 0x00;
    }
}

uint8_t h89_config_auto_sw501(const h89_config *c, uint8_t sw501)
{
    int has_h17 = count_card(c, H89_CARD_H17) > 0;
    int has_h37 = count_card(c, H89_CARD_H37) > 0;
    int has_316 = count_card(c, H89_CARD_MMS77316) > 0;
    int has_320 = count_card(c, H89_CARD_MMS77320) > 0;
    int sasi_at_7c = has_320 && c->mms320_port == 0x7c;
    int sasi_at_78 = has_320 && c->mms320_port == 0x78;
    uint8_t mask = h89_sw501_auto_mask(c->monitor);
    uint8_t v = 0;

    switch (family_of(c->monitor))
    {
    case MON_MTR89:
        v = has_h17 ? 0x00 : 0x02;
        v |= 0x00;                  /* 170Q: unused */
        break;
    case MON_MTR90:
        v = has_h17 ? 0x00 : sasi_at_7c ? 0x02 : 0x03;
        v |= has_h37 ? 0x00 : sasi_at_78 ? 0x08 : 0x0c;
        if (!has_h17 && (has_h37 || sasi_at_78))
            v |= 0x10;              /* boot from 170Q */
        break;
    case MON_SIGMA:
        v = has_h17 ? 0x00 : 0x01;
        v |= has_h37 ? 0x00 : 0x04;
        if (!has_h17 && has_h37)
            v |= 0x10;
        break;
    case MON_MMS:
        v = has_h17 ? 0x00 : sasi_at_7c ? 0x02 : 0x00;
        v |= has_h37 ? 0x00 : sasi_at_78 ? 0x08 : 0x00;
        if (has_316)
        {
            int any5 = 0;
            for (int i = 4; i < H89_MMS316_DRIVES; i++)
                any5 |= c->mms316_drive[i] != H89_FDD_NONE;
            v |= any5 ? 0x00 : 0x10;
        }
        else if (has_h17)
            v |= 0x20;
        else if (has_h37)
            v |= 0x30;
        else if (sasi_at_7c)
            v |= 0x20;
        else if (sasi_at_78)
            v |= 0x30;
        break;
    case MON_KMR:
    case MON_ULT4K:
        v = has_h17 ? 0x0 : has_h37 ? 0x1 : has_316 ? 0xc : sasi_at_78 ? 0x9 : sasi_at_7c ? 0xa : 0x0;
        break;
    case MON_ULT2K:
        v = has_h17 ? 0x0 : has_316 ? 0x3 : 0x0;
        break;
    default:
        break;
    }
    return (uint8_t)((sw501 & ~mask) | (v & mask));
}

uint8_t h89_config_effective_sw501(const h89_config *c)
{
    return c->sw501_auto ? h89_config_auto_sw501(c, c->sw501) : c->sw501;
}

/* ---- key/value access ----------------------------------------------------- */

static const char *const s_fdd_keys[] = {"none", "5ss40", "5ss80", "5ds40", "5ds80", "8ss", "8ds"};
static const char *const s_sasi_keys[] = {
    "none", "st506", "st412", "cm5206", "cm5410", "cm5616", "ro201", "ro202", "ro203", "ro204",
};
static const char *const s_h17drv_keys[] = {"none", "h17-1", "h17-4"};
static const char *const s_model_keys[] = {"h89", "h88"};
static const char *const s_memory_keys[] = {"16", "32", "48", "64"};
static const char *const s_p503_keys[] = {"none", "wh8816", "mms77318"};

typedef enum { K_ENUM, K_INT, K_HEX, K_STR } key_type;

typedef struct {
    char name[24];
    key_type type;
    size_t offset;          /* into h89_config */
    const char *const *values;
    int nvalues;
    int strsize;
} key_def;

#define OFF(f) offsetof(h89_config, f)
#include <stddef.h>
#include <stdlib.h>

static key_def s_keys[64];
static int s_nkeys;

static void add_key(const char *name, key_type type, size_t off, const char *const *values,
                    int nvalues, int strsize)
{
    key_def *k = &s_keys[s_nkeys++];
    snprintf(k->name, sizeof k->name, "%s", name);
    k->type = type;
    k->offset = off;
    k->values = values;
    k->nvalues = nvalues;
    k->strsize = strsize;
}

static void init_keys(void)
{
    char name[24];
    if (s_nkeys)
        return;
    add_key("machine.model", K_ENUM, OFF(model), s_model_keys, COUNT(s_model_keys), 0);
    add_key("machine.memory", K_ENUM, OFF(memory), s_memory_keys, COUNT(s_memory_keys), 0);
    add_key("machine.p503", K_ENUM, OFF(p503), s_p503_keys, COUNT(s_p503_keys), 0);
    add_key("machine.monitor", K_STR, OFF(monitor), NULL, 0, (int)sizeof(((h89_config *)0)->monitor));
    add_key("machine.speedup", K_INT, OFF(speedup), NULL, 0, 0);
    add_key("switches.sw501", K_HEX, OFF(sw501), NULL, 0, 1);
    add_key("switches.sw501_auto", K_INT, OFF(sw501_auto), NULL, 0, 0);
    add_key("switches.sw401", K_HEX, OFF(sw401), NULL, 0, 1);
    add_key("switches.sw402", K_HEX, OFF(sw402), NULL, 0, 1);
    for (int i = 0; i < H89_SLOTS; i++)
    {
        snprintf(name, sizeof name, "slot.p%d", 504 + i);
        add_key(name, K_ENUM, OFF(slot) + i * sizeof(int), s_card_keys, COUNT(s_card_keys), 0);
    }
    for (int i = 0; i < H89_H17_DRIVES; i++)
    {
        snprintf(name, sizeof name, "h17.drive%d", i + 1);
        add_key(name, K_ENUM, OFF(h17_drive) + i * sizeof(int), s_h17drv_keys, COUNT(s_h17drv_keys), 0);
    }
    for (int i = 0; i < H89_H37_DRIVES; i++)
    {
        snprintf(name, sizeof name, "h37.drive%d", i + 1);
        add_key(name, K_ENUM, OFF(h37_drive) + i * sizeof(int), s_fdd_keys, COUNT(s_fdd_keys), 0);
    }
    for (int i = 0; i < H89_MMS316_DRIVES; i++)
    {
        snprintf(name, sizeof name, "mms77316.drive%d", i + 1);
        add_key(name, K_ENUM, OFF(mms316_drive) + i * sizeof(int), s_fdd_keys, COUNT(s_fdd_keys), 0);
    }
    add_key("mms77320.port", K_HEX, OFF(mms320_port), NULL, 0, 4);
    add_key("mms77320.intr", K_INT, OFF(mms320_intr), NULL, 0, 0);
    add_key("mms77320.dipsw", K_HEX, OFF(mms320_dipsw), NULL, 0, 1);
    for (int i = 0; i < H89_MMS320_DRIVES; i++)
    {
        snprintf(name, sizeof name, "mms77320.drive%d", i);
        add_key(name, K_ENUM, OFF(mms320_drive) + i * sizeof(int), s_sasi_keys, COUNT(s_sasi_keys), 0);
    }
    add_key("cpnet.enabled", K_INT, OFF(cpnet_enabled), NULL, 0, 0);
    add_key("cpnet.port", K_HEX, OFF(cpnet_port), NULL, 0, 4);
    add_key("cpnet.clientid", K_HEX, OFF(cpnet_clientid), NULL, 0, 4);
    add_key("cpnet.root", K_STR, OFF(cpnet_root), NULL, 0, (int)sizeof(((h89_config *)0)->cpnet_root));
    add_key("fujinet.ack_delay", K_INT, OFF(fujinet_ack_delay), NULL, 0, 0);
}

int h89_config_key_count(void)
{
    init_keys();
    return s_nkeys;
}

const char *h89_config_key_at(int i)
{
    init_keys();
    return (i >= 0 && i < s_nkeys) ? s_keys[i].name : NULL;
}

static const key_def *find_key(const char *key)
{
    init_keys();
    for (int i = 0; key && i < s_nkeys; i++)
        if (strcmp(s_keys[i].name, key) == 0)
            return &s_keys[i];
    return NULL;
}

int h89_config_set(h89_config *cfg, const char *key, const char *value)
{
    const key_def *k = find_key(key);
    char *base = (char *)cfg + (k ? k->offset : 0);
    char *end;

    if (!k || !value)
        return -1;
    switch (k->type)
    {
    case K_ENUM:
        for (int i = 0; i < k->nvalues; i++)
            if (strcmp(value, k->values[i]) == 0)
            {
                *(int *)base = i;
                return 0;
            }
        return -1;
    case K_INT:
    {
        long v = strtol(value, &end, 0);
        if (end == value)
            return -1;
        *(int *)base = (int)v;
        return 0;
    }
    case K_HEX:
    {
        unsigned long v = strtoul(value, &end, 16);
        if (end == value)
            return -1;
        if (k->strsize == 1)
            *(uint8_t *)base = (uint8_t)v;
        else
            *(int *)base = (int)v;
        return 0;
    }
    case K_STR:
        snprintf(base, (size_t)k->strsize, "%s", value);
        return 0;
    }
    return -1;
}

int h89_config_get(const h89_config *cfg, const char *key, char *buf, int bufsz)
{
    const key_def *k = find_key(key);
    const char *base = (const char *)cfg + (k ? k->offset : 0);

    if (!k || !buf || bufsz <= 0)
        return -1;
    switch (k->type)
    {
    case K_ENUM:
    {
        int v = *(const int *)base;
        snprintf(buf, bufsz, "%s", (v >= 0 && v < k->nvalues) ? k->values[v] : k->values[0]);
        return 0;
    }
    case K_INT:
        snprintf(buf, bufsz, "%d", *(const int *)base);
        return 0;
    case K_HEX:
        if (k->strsize == 1)
            snprintf(buf, bufsz, "%02x", *(const uint8_t *)base);
        else
            snprintf(buf, bufsz, "0x%02x", *(const int *)base);
        return 0;
    case K_STR:
        snprintf(buf, bufsz, "%s", base);
        return 0;
    }
    return -1;
}
