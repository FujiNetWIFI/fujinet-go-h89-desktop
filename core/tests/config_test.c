/*
 * config -- the machine configuration (h89config.h).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "h89config.h"
#include "test_util.h"

static int valid(const h89_config *c)
{
    char why[256];
    int ok = h89_config_validate(c, why, sizeof why);
    if (!ok)
        printf("  (invalid: %s)\n", why);
    return ok;
}

int main(void)
{
    h89_config c, d;
    char buf[1100];

    /* the default machine builds, and derives SW501 for MTR-90: H-88-1 at
     * 174Q, nothing at 170Q, boot from 174Q, no memory test */
    h89_config_default(&c);
    CHECK(valid(&c));
    CHECK_EQ_INT(h89_config_effective_sw501(&c), 0x2c);

    /* each card once */
    d = c;
    d.slot[1] = H89_CARD_H17;
    CHECK(!valid(&d));

    /* FujiNet's 0xD0 against the HA-88-3's AUX port */
    d = c;
    d.slot[1] = H89_CARD_H88_3;
    CHECK(!valid(&d));
    d.slot[0] = H89_CARD_EMPTY;
    CHECK(valid(&d));

    /* the MMS 77320 at 0x7C against the H17; at 0x78 against the H37 */
    d = c;
    d.slot[1] = H89_CARD_MMS77320;
    d.mms320_port = 0x7c;
    d.mms320_intr = 0;
    CHECK(!valid(&d));
    d.mms320_port = 0x78;
    CHECK(valid(&d));
    d.slot[0] = H89_CARD_H37;
    CHECK(!valid(&d));
    d.slot[0] = H89_CARD_FUJINET;

    /* interrupt 3 is the console's */
    d.mms320_intr = 3;
    CHECK(!valid(&d));
    d.mms320_intr = 5;
    CHECK(valid(&d));

    /* H37 and MMS 77316 together are fine (the combined controller) */
    d = c;
    d.slot[1] = H89_CARD_H37;
    d.slot[0] = H89_CARD_MMS77316;
    CHECK(valid(&d));
    /* ... and MTR-90 then boots from 174Q still (the H17 is there) */
    CHECK_EQ_INT(h89_config_effective_sw501(&d) & 0x1f, 0x00);
    d.slot[2] = H89_CARD_EMPTY;
    /* without the H17: the H37 at 170Q is the boot device */
    CHECK_EQ_INT(h89_config_effective_sw501(&d) & 0x1f, 0x13);

    /* speed-up and the MMS 77318 share GPP bits */
    d = c;
    d.speedup = 4;
    CHECK(valid(&d));
    d.p503 = H89_P503_MMS77318;
    CHECK(!valid(&d));

    /* no monitor */
    d = c;
    d.monitor[0] = 0;
    CHECK(!valid(&d));

    /* CP/NET needs a folder and a free port */
    d = c;
    d.cpnet_enabled = 1;
    CHECK(!valid(&d));
    snprintf(d.cpnet_root, sizeof d.cpnet_root, "/tmp");
    CHECK(valid(&d));
    d.cpnet_port = 0xd0;
    CHECK(!valid(&d));

    /* MMS: the boot device field follows the controllers */
    d = c;
    snprintf(d.monitor, sizeof d.monitor, "2732_444_84b_mms.u518");
    d.slot[1] = H89_CARD_MMS77316;
    CHECK_EQ_INT(h89_config_effective_sw501(&d) & 0x70, 0x00);   /* 77316 5.25" */
    d.slot[1] = H89_CARD_EMPTY;
    CHECK_EQ_INT(h89_config_effective_sw501(&d) & 0x70, 0x20);   /* the H17 at 0x7C */

    /* the switch tables */
    CHECK(h89_sw501_field_count("2732_444-142_mtr90.u518") == 6);
    CHECK(strcmp(h89_sw501_option_name("2732_444-142_mtr90.u518", 0, 0), "H-88-1") == 0);
    CHECK(h89_sw501_field_count("unknown.rom") == 8);

    /* every key round-trips */
    for (int i = 0; i < h89_config_key_count(); i++)
    {
        const char *k = h89_config_key_at(i);
        CHECK(h89_config_get(&c, k, buf, sizeof buf) == 0);
        CHECK(h89_config_set(&d, k, buf) == 0);
    }
    d = c;
    CHECK(h89_config_set(&d, "slot.p505", "h37") == 0);
    CHECK_EQ_INT(d.slot[1], H89_CARD_H37);
    CHECK(h89_config_set(&d, "mms77320.port", "0x78") == 0);
    CHECK_EQ_INT(d.mms320_port, 0x78);
    CHECK(h89_config_set(&d, "switches.sw402", "05") == 0);
    CHECK_EQ_INT(d.sw402, 5);
    CHECK(h89_config_set(&d, "h37.drive2", "8ds") == 0);
    CHECK_EQ_INT(d.h37_drive[1], H89_FDD_8_DS);
    CHECK(h89_config_set(&d, "no.such.key", "1") != 0);
    CHECK(h89_config_set(&d, "slot.p504", "nonsense") != 0);

    return t_done("config");
}
