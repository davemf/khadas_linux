/*
 * drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Terry <terry@szwesion.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/phy.h>
#include <linux/module.h>

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13
#define RTL8211E_INER_LINK_STATUS 0x400
#define RTL8211F_MAC_ADDR_CTRL0 0x10
#define RTL8211F_MAC_ADDR_CTRL1 0x11
#define RTL8211F_MAC_ADDR_CTRL2 0x12
#define RTL8211F_WOL_CTRL 0x10
#define RTL8211F_WOL_RST 0x11
#define RTL8211F_MAX_PACKET_CTRL 0x11
#define RTL8211F_BMCR   0x00
#define RTL821x_EPAGSR      0x1f

#define RTL8211F_INER_LINK_STATUS 0x0010
#define RTL8211F_INSR		0x1d
#define RTL8211F_PAGE_SELECT	0x1f
#define RTL8211F_TX_DELAY	0x100

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Terry");
MODULE_LICENSE("GPL");
static void rtl8211f_config_mac_addr(struct phy_device *phydev);
static void rtl8211f_config_pin_as_pmeb(struct phy_device *phydev);
static void rtl8211f_config_wakeup_frame_mask(struct phy_device *phydev);
static void rtl8211f_config_max_packet(struct phy_device *phydev);
static void rtl8211f_config_pad_isolation(struct phy_device *phydev, int enable);
static void rtl8211f_config_wol(struct phy_device *phydev, int enable);
static void rtl8211f_config_speed(struct phy_device *phydev, int mode);

static int wol_enable = 0;
static u8 mac_addr[] = {0, 0, 0, 0, 0, 0};
#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
#include <linux/amlogic/pm.h>
struct phy_device *g_phydev;

int get_wol_state(void){
	return wol_enable;
}

void rtl8211f_shutdown(void) {
	if (wol_enable && g_phydev) {
		rtl8211f_config_speed(g_phydev, 0);
		rtl8211f_config_mac_addr(g_phydev);
		rtl8211f_config_max_packet(g_phydev);
		rtl8211f_config_wol(g_phydev, 1);
		rtl8211f_config_wakeup_frame_mask(g_phydev);
		rtl8211f_config_pad_isolation(g_phydev, 1);
	}
}

void rtl8211f_suspend(void) {
	if (wol_enable && g_phydev) {
		rtl8211f_config_mac_addr(g_phydev);
		rtl8211f_config_max_packet(g_phydev);
		rtl8211f_config_wol(g_phydev, 1);
		rtl8211f_config_wakeup_frame_mask(g_phydev);
		rtl8211f_config_pad_isolation(g_phydev, 1);
	}
}

void rtl8211f_resume(void) {
	if (wol_enable && g_phydev) {
		rtl8211f_config_speed(g_phydev, 1);
		rtl8211f_config_wol(g_phydev, 0);
		rtl8211f_config_pad_isolation(g_phydev, 0);
	}
}

static unsigned char chartonum(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return (c - 'A') + 10;
	if (c >= 'a' && c <= 'f')
		return (c - 'a') + 10;
	return 0;
}


static int __init init_wol_state(char *str)
{
	wol_enable = simple_strtol(str, NULL, 0);
	printk("%s, wol_enable=%d\b",__func__, wol_enable);
	return 1;
}
__setup("wol_enable=", init_wol_state);

static int __init init_mac_addr(char *line)
{
	unsigned char mac[6];
	int i = 0;
	for (i = 0; i < 6 && line[0] != '\0' && line[1] != '\0'; i++) {
		mac[i] = chartonum(line[0]) << 4 | chartonum(line[1]);
		line += 3;
	}
	memcpy(mac_addr, mac, 6);
	printk("realtek init mac-addr: %x:%x:%x:%x:%x:%x\n",
			mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
			mac_addr[5]);

	return 1;
}
__setup("androidboot.mac=",init_mac_addr);



static void rtl8211f_early_suspend(struct early_suspend *h)
{
	if (wol_enable && g_phydev) {
		rtl8211f_config_mac_addr(g_phydev);
		rtl8211f_config_max_packet(g_phydev);
		rtl8211f_config_wol(g_phydev, 1);
		rtl8211f_config_wakeup_frame_mask(g_phydev);
		rtl8211f_config_pad_isolation(g_phydev, 1);
	}
}

static void rtl8211f_late_resume(struct early_suspend *h)
{
	if (wol_enable && g_phydev) {
		rtl8211f_config_speed(g_phydev, 1);
		rtl8211f_config_wol(g_phydev, 0);
		rtl8211f_config_pad_isolation(g_phydev, 0);
	}
}

static struct early_suspend rtl8211f_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 15,
	.suspend = rtl8211f_early_suspend,
	.resume = rtl8211f_late_resume,
};

static void rtl8211f_config_speed(struct phy_device *phydev, int mode)
{
	phy_write(phydev, RTL821x_EPAGSR, 0x0); /*set page 0x0*/
	if (mode == 1) {
		phy_write(phydev, RTL8211F_BMCR, 0x1040);  /* 1000Mbps */
	} else {
		phy_write(phydev, RTL8211F_BMCR, 0x0); /* 10Mbps */
	}
}

static void rtl8211f_config_mac_addr(struct phy_device *phydev)
{
	phy_write(phydev, RTL821x_EPAGSR, 0xd8c); /*set page 0xd8c*/
	phy_write(phydev, RTL8211F_MAC_ADDR_CTRL0, mac_addr[1] << 8 | mac_addr[0]);
	phy_write(phydev, RTL8211F_MAC_ADDR_CTRL1, mac_addr[3] << 8 | mac_addr[2]);
	phy_write(phydev, RTL8211F_MAC_ADDR_CTRL2, mac_addr[5] << 8 | mac_addr[4]);
	phy_write(phydev, RTL821x_EPAGSR, 0); /*set page 0*/
}

static void rtl8211f_config_pin_as_pmeb(struct phy_device *phydev)
{
	int val;
	phy_write(phydev, RTL821x_EPAGSR, 0xd40); /*set page 0xd40*/
	val = phy_read(phydev, 0x16);
	val = val | 0x20;
	phy_write(phydev, 0x16, val);
	phy_write(phydev, RTL821x_EPAGSR, 0); /*set page 0*/
}

static void rtl8211f_config_wakeup_frame_mask(struct phy_device *phydev)
{
	phy_write(phydev, RTL821x_EPAGSR, 0xd80); /*set page 0xd80*/
	phy_write(phydev, 0x10, 0x3000);
	phy_write(phydev, 0x11, 0x0020);
	phy_write(phydev, 0x12, 0x03c0);
	phy_write(phydev, 0x13, 0x0000);
	phy_write(phydev, 0x14, 0x0000);
	phy_write(phydev, 0x15, 0x0000);
	phy_write(phydev, 0x16, 0x0000);
	phy_write(phydev, 0x17, 0x0000);
	phy_write(phydev, RTL821x_EPAGSR, 0); /*set page 0*/
}

static void rtl8211f_config_max_packet(struct phy_device *phydev)
{
	phy_write(phydev, RTL821x_EPAGSR, 0xd8a); /*set page 0xd8a*/
	phy_write(phydev, RTL8211F_MAX_PACKET_CTRL, 0x9fff);
	phy_write(phydev, RTL821x_EPAGSR, 0); /*set page 0*/
}

static void rtl8211f_config_pad_isolation(struct phy_device *phydev, int enable)
{
	int val;
	phy_write(phydev, RTL821x_EPAGSR, 0xd8a); /*set page 0xd8a*/
	val = phy_read(phydev, 0x13);
	if (enable)
		val = val | 0x1000;
	else
		val = val & 0x7fff;
	phy_write(phydev, 0x13, val);
	phy_write(phydev, RTL821x_EPAGSR, 0); /*set page 0*/
}

static void rtl8211f_config_wol(struct phy_device *phydev, int enable)
{
	int val;
	phy_write(phydev, RTL821x_EPAGSR, 0xd8a); /*set page 0xd8a*/
	if (enable)
		phy_write(phydev, RTL8211F_WOL_CTRL, 0x1000);
	else {
			phy_write(phydev, RTL8211F_WOL_CTRL, 0);
			val =  phy_read(phydev,  RTL8211F_WOL_RST);
			phy_write(phydev, RTL8211F_WOL_RST, val & 0x7fff);
	}
	phy_write(phydev, RTL821x_EPAGSR, 0); /*set page 0*/
}
#endif

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8211f_ack_interrupt(struct phy_device *phydev)
{
	int err;

	phy_write(phydev, RTL8211F_PAGE_SELECT, 0xa43);
	err = phy_read(phydev, RTL8211F_INSR);
	/* restore to default page 0 */
	phy_write(phydev, RTL8211F_PAGE_SELECT, 0x0);

	return (err < 0) ? err : 0;
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL821x_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STATUS);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211f_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211F_INER_LINK_STATUS);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211f_config_init(struct phy_device *phydev)
{
	int ret;
	u16 reg;

	ret = genphy_config_init(phydev);
	if (ret < 0)
		return ret;

	phy_write(phydev, RTL8211F_PAGE_SELECT, 0xd08);
	reg = phy_read(phydev, 0x11);

	/* enable TX-delay for rgmii-id and rgmii-txid, otherwise disable it */
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
		reg |= RTL8211F_TX_DELAY;
	else
		reg &= ~RTL8211F_TX_DELAY;

	phy_write(phydev, 0x11, reg);
	/* restore to default page 0 */
	phy_write(phydev, RTL8211F_PAGE_SELECT, 0x0);
	rtl8211f_config_pin_as_pmeb(phydev);
	rtl8211f_config_speed(phydev, 1);
#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
	g_phydev = kzalloc(sizeof(struct phy_device), GFP_KERNEL);
	if (g_phydev == NULL)
		return -ENOMEM;
	g_phydev = phydev;
	register_early_suspend(&rtl8211f_early_suspend_handler);
#endif

	return 0;
}

static struct phy_driver realtek_drvs[] = {
	{
		.phy_id         = 0x00008201,
		.name           = "RTL8201CP Ethernet",
		.phy_id_mask    = 0x0000ffff,
		.features       = PHY_BASIC_FEATURES,
		.flags          = PHY_HAS_INTERRUPT,
		.config_aneg    = &genphy_config_aneg,
		.read_status    = &genphy_read_status,
	}, {
		.phy_id		= 0x001cc912,
		.name		= "RTL8211B Gigabit Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.config_aneg	= &genphy_config_aneg,
		.read_status	= &genphy_read_status,
		.ack_interrupt	= &rtl821x_ack_interrupt,
		.config_intr	= &rtl8211b_config_intr,
	}, {
		.phy_id		= 0x001cc914,
		.name		= "RTL8211DN Gigabit Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl8211e_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= 0x001cc915,
		.name		= "RTL8211E Gigabit Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.config_aneg	= &genphy_config_aneg,
		.read_status	= &genphy_read_status,
		.ack_interrupt	= &rtl821x_ack_interrupt,
		.config_intr	= &rtl8211e_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= 0x001cc916,
		.name		= "RTL8211F Gigabit Ethernet",
		.phy_id_mask	= 0x001fffff,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,
		.config_aneg	= &genphy_config_aneg,
		.config_init	= &rtl8211f_config_init,
		.read_status	= &genphy_read_status,
		.ack_interrupt	= &rtl8211f_ack_interrupt,
		.config_intr	= &rtl8211f_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	},
};

module_phy_driver(realtek_drvs);

static struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ 0x001cc912, 0x001fffff },
	{ 0x001cc914, 0x001fffff },
	{ 0x001cc915, 0x001fffff },
	{ 0x001cc916, 0x001fffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
