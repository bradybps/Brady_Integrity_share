/*  arch/arm/mach-lpc313x/bps313x1.c
 *
 *  bps313x1 board init routines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smsc911x.h>
#include <linux/wl12xx.h>
#include <linux/delay.h>

#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include <asm/system.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/sizes.h>

#include <asm/mach/map.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <mach/gpio.h>
#include <mach/board.h>


static int mci_get_cd(u32 slot_id)
{
	if (0 == slot_id)
		return 0;

	return -1;
}


static int mci_init(u32 slot_id, irq_handler_t irqhdlr, void *data)
{
	LPC313X_GPIO_OUT_HIGH(IOCONF_GPIO,_BIT(5));	
	gpio_set_value(GPIO_GPIO11, 1);				//Enable Wifi

	return 0;
}

static int mci_get_ro(u32 slot_id)
{
	return 0;
}

static int mci_get_ocr(u32 slot_id)
{
	return MMC_VDD_32_33 | MMC_VDD_33_34;
}


static void mci_setpower(u32 slot_id, u32 power_on)
{
#if 1
	/* power is always on for both slots nothing to do*/
	static bool power_state;

	printk(KERN_INFO "Powering %s wl12xx", power_on ? "on" : "off");

	if (power_on == power_state)
		return;
	power_state = power_on;

	if (power_on) {
		/* Power up sequence required for wl127x devices */
		gpio_set_value(GPIO_GPIO11, 1);
		usleep_range(15000, 15000);
		gpio_set_value(GPIO_GPIO11, 0);
		usleep_range(1000, 1000);
		gpio_set_value(GPIO_GPIO11, 1);
		msleep(70);
	} else {
		//gpio_set_value(GPIO_GPIO11, 0);
	}
#endif 
}

static int mci_get_bus_wd(u32 slot_id)
{
	return 4;
}

static void mci_exit(u32 slot_id)
{

	gpio_set_value(GPIO_GPIO11, 1);				//Enable Wifi
//	gpio_set_value(GPIO_GPIO11, 0); 

}

static struct resource lpc313x_mci_resources[] = {
	[0] = {
		.start  = IO_SDMMC_PHYS,
		.end	= IO_SDMMC_PHYS + IO_SDMMC_SIZE,
		.flags	= IORESOURCE_MEM,
//                .name   = "wl1271",

	},
	[1] = {
		.start	= IRQ_MCI,
		.end	= IRQ_MCI,
		.flags	= IORESOURCE_IRQ,

	},
};
static struct lpc313x_mci_board bps313x_mci_platform_data = {
	.num_slots		= 1,
	.detect_delay_ms	= 250,
	.init 			= mci_init,
	.get_ro			= mci_get_ro,
	.get_cd 		= mci_get_cd,
	.get_ocr		= mci_get_ocr,
	.get_bus_wd		= mci_get_bus_wd,
	.setpower 		= mci_setpower,
	.exit			= mci_exit,
};

static u64 mci_dmamask = 0xffffffffUL;
static struct platform_device	lpc313x_mci_device = {
	.name		= "lpc313x_mmc",
	.num_resources	= ARRAY_SIZE(lpc313x_mci_resources),
	.dev		= {
		.dma_mask		= &mci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &bps313x_mci_platform_data,
	},
	.resource	= lpc313x_mci_resources,
};


#if defined (CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
static struct resource bps_smsc911x_resources[] = { 
	[0] = { 
		.start  = EXT_SRAM0_PHYS,
		.end    = EXT_SRAM0_PHYS + SZ_4K,
		.flags  = IORESOURCE_MEM,
	},  
	[1] = { 
		.start  = IRQ_LAN9200_ETH_INT,
		.end    = IRQ_LAN9200_ETH_INT,
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},  
};

static struct smsc911x_platform_config bps_smsc911x_config = { 
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type   = SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags      = SMSC911X_USE_16BIT,
	.phy_interface  = PHY_INTERFACE_MODE_MII,
};

static struct platform_device bps_smsc911x_device = {
	.name       = "smsc911x",
	.id     = -1,
	.num_resources  = ARRAY_SIZE(bps_smsc911x_resources),
	.resource   = bps_smsc911x_resources,
	.dev        = {
		.platform_data = &bps_smsc911x_config,
	},
};


static void __init bps_add_lan9220_device(void)
{
	/*
	 * Note: These timings were calculated for MASTER_CLOCK = 90000000
	 */
	MPMC_STCONFIG0 = 0x81;
	MPMC_STWTWEN0 = 0;
	MPMC_STWTOEN0 = 1;
	MPMC_STWTRD0 = 15;
	MPMC_STWTPG0 = 1;
	MPMC_STWTWR0 = 12;
	MPMC_STWTTURN0 = 2;

	/* enable oe toggle between consec reads */
	SYS_MPMC_WTD_DEL0 = _BIT(5) | 15;
	SYS_MPMC_MISC = 0x00000100;
	MPMC_STEXDWT = 0x0;

	LPC313X_GPIO_OUT_HIGH(IOCONF_I2SRX_0, 0x4);	//I2SRX_WS0

	platform_device_register(&bps_smsc911x_device);
}

#endif





#if defined (CONFIG_MTD_NAND_LPC313X)
static struct resource lpc313x_nand_resources[] = {
	[0] = {
		.start  = IO_NAND_PHYS,
		.end	= IO_NAND_PHYS + IO_NAND_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IO_NAND_BUF_PHYS,
		.end 	= IO_NAND_BUF_PHYS + IO_NAND_BUF_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start 	= IRQ_NAND_FLASH,
		.end 	= IRQ_NAND_FLASH,
		.flags	= IORESOURCE_IRQ,
	}
};


#define BLK_SIZE (512 * 32)
static struct mtd_partition bps313x_nand0_partitions[] = {
	{
		.name	= "apex1",
		.offset	= (BLK_SIZE * 1),
		.size	= (BLK_SIZE * 5)	//80K
	},
	{
		.name	= "apex2",
		.offset	= (BLK_SIZE * 6),
		.size	= (BLK_SIZE * 5)	//80K
	},
	{
		.name	= "apex-env",
		.offset	= (BLK_SIZE * 11),
		.size	= (BLK_SIZE * 1)	//16K
	},
	{
		.name	= "sysconfig",
		.offset	= (BLK_SIZE * 12),
		.size	= (BLK_SIZE *  8) 	//128K
	},
	{
		.name	= "kernel1",
		.offset	= (BLK_SIZE *  20),
		.size	= (BLK_SIZE * 110) 	//1280k
	},
	{
		.name	= "kernel2",
		.offset	= (BLK_SIZE * 130),
		.size	= (BLK_SIZE * 110) 	//1280k
	},
	{
		.name	= "configfs",
		.offset	= (BLK_SIZE * 240),
		.size	= (BLK_SIZE * 50) 	//256k = 0.25M
	},
	{
		.name	= "rootfs",
		.offset	= (BLK_SIZE * 290),
		.size	= (BLK_SIZE * 600) 	//(8352 + 1024 + 512 + 1856)k
	},
	{
		.name	= "safefs",
		.offset	= (BLK_SIZE * 890),
		.size	= (BLK_SIZE * 100) 	//1536k = 2M
	},
	{
		.name	= "log",
		.offset	= (BLK_SIZE * 1018),
		.size	= (BLK_SIZE *    2) //32K
	},
};

static struct lpc313x_nand_timing bps313x_nanddev_timing = {
	.ns_trsd	= 36,
	.ns_tals	= 36,
	.ns_talh	= 12,
	.ns_tcls	= 36,
	.ns_tclh	= 12,
	.ns_tdrd	= 36,
	.ns_tebidel	= 12,
	.ns_tch		= 12,
	.ns_tcs		= 48,
	.ns_treh	= 24,
	.ns_trp		= 48,
	.ns_trw		= 24,
	.ns_twp		= 36
};

static struct lpc313x_nand_dev_info bps313x_ndev[] = {
	{
		.name		= "nand0",
		.nr_partitions	= ARRAY_SIZE(bps313x_nand0_partitions),
		.partitions	= bps313x_nand0_partitions
	}
};

static struct lpc313x_nand_cfg bps313x_plat_nand = {
	.nr_devices	= ARRAY_SIZE(bps313x_ndev),
	.devices	= bps313x_ndev,
	.timing		= &bps313x_nanddev_timing,
	.support_16bit	= 0,
};

static struct platform_device	lpc313x_nand_device = {
	.name		= "lpc313x_nand",
	.dev		= {
				.platform_data	= &bps313x_plat_nand,
	},
	.num_resources	= ARRAY_SIZE(lpc313x_nand_resources),
	.resource	= lpc313x_nand_resources,
};
#endif

#ifdef CONFIG_PM
static struct platform_device	tiwlan_pm_device = {
	.name		= "tiwlan_pm_driver",
	.id     = -1,
	.dev		= {
				.platform_data	= NULL,
	},
};
#endif


#ifdef CONFIG_WL12XX_PLATFORM_DATA

#define LPC313X_WLAN_PMENA_GPIO        (11)
#define LPC313X_WLAN_IRQ_GPIO          (15)
#if 1
static struct regulator_consumer_supply lpc313x_vmmc2_supply[] = {
        REGULATOR_SUPPLY("vmmc", "lpc313x_mmc.0"),
};

/* VMMC2 for driving the WL12xx module */
static struct regulator_init_data lpc313x_vmmc2 = {
        .constraints = {
                .valid_ops_mask = REGULATOR_CHANGE_STATUS,
        },
        .num_consumer_supplies  = ARRAY_SIZE(lpc313x_vmmc2_supply),
        .consumer_supplies      = lpc313x_vmmc2_supply,
};

static struct fixed_voltage_config lpc313x_vwlan = {
        .supply_name            = "vwl1271",
        .microvolts             = 1800000, /* 1.80V */
        .gpio                   = GPIO_GPIO11,
        .startup_delay          = 70000, /* 70ms */
        .enable_high            = 1,
        .enabled_at_boot        = 0,
        .init_data              = &lpc313x_vmmc2,
};
#endif

static struct resource lpc313x_wla12xx_resources[] = {
	[0] = {
		.start	= IRQ_WLAN_INT,
		.end	= IRQ_WLAN_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

struct wl12xx_platform_data lpc313x_wlan_data __initdata = {
        .irq = IRQ_WLAN_INT,
	.board_ref_clock = WL12XX_REFCLOCK_38, /* 38.4 MHz */
};

static struct platform_device lpc313x_wlan_regulator = {

	.name = "reg-fixed-voltage",

	.id = 1,

	.dev = {

		.platform_data = &lpc313x_vwlan,
	},

};

static struct platform_device wl12xx_device = {
        .name           = "wl1271",

        .id             = 1,
        .dev = {
                .platform_data  = &lpc313x_wlan_data,
//	          .platform_data  = NULL,
        },
	.num_resources	= ARRAY_SIZE(lpc313x_wla12xx_resources),
	.resource	= lpc313x_wla12xx_resources,
};

#endif //CONFIG_WL12XX_PLATFORM_DATA

static struct platform_device *devices[] __initdata = {
	&lpc313x_mci_device,
#if defined (CONFIG_MTD_NAND_LPC313X)
	&lpc313x_nand_device,
#endif
#ifdef CONFIG_PM
//	&tiwlan_pm_device,
	&wl12xx_device,
#endif
};

static struct map_desc bps313x_io_desc[] __initdata = {
	{
		.virtual	= io_p2v(EXT_SRAM0_PHYS),
		.pfn		= __phys_to_pfn(EXT_SRAM0_PHYS),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
	{
		.virtual	= io_p2v(EXT_SRAM1_PHYS + 0x10000),
		.pfn		= __phys_to_pfn(EXT_SRAM1_PHYS + 0x10000),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
	{
		.virtual	= io_p2v(IO_SDMMC_PHYS),
		.pfn		= __phys_to_pfn(IO_SDMMC_PHYS),
		.length		= IO_SDMMC_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= io_p2v(IO_USB_PHYS),
		.pfn		= __phys_to_pfn(IO_USB_PHYS),
		.length		= IO_USB_SIZE,
		.type		= MT_DEVICE
	},
};



static void __init bps313x_init(void)
{
	lpc313x_init();
	
	LPC313X_GPIO_IN(IOCONF_GPIO, 0x200);	//GPIO15
	LPC313X_GPIO_OUT_HIGH(IOCONF_GPIO, 0x45E0);	//GPIO11-GPIO14,GPIO16,GPIO20

//	gpio_set_value(GPIO_GPIO11, 0);
	gpio_set_value(GPIO_GPIO12, 0);
	
	gpio_set_value(GPIO_GPIO13, 0); //Disable BTM330
	

	LPC313X_GPIO_IN(IOCONF_PWM, 0x1);			//PWM_DATA
	
	LPC313X_GPIO_IN(IOCONF_I2SRX_0, 0x3);		//I2SRX_DATA0,I2SRX_BCK0
	
	LPC313X_GPIO_IN(IOCONF_I2SRX_1, 0x3);		//I2SRX_DATA1,I2SRX_BCK1
	
	LPC313X_GPIO_IN(IOCONF_I2STX_1, 0x7);		//I2STX_DATA1,I2STX_BCK1,I2STX_WS1
	
	LPC313X_GPIO_IN(IOCONF_EBI_I2STX_0, 0x20);	//mI2STX_DATA0

#ifdef CONFIG_WL12XX_PLATFORM_DATA
		/* WL12xx WLAN Init */
		if (wl12xx_set_platform_data(&lpc313x_wlan_data))
				pr_err("error setting wl12xx data\n");
		platform_device_register(&lpc313x_wlan_regulator);
#endif

	
	platform_add_devices(devices, ARRAY_SIZE(devices));

#if defined (CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
	bps_add_lan9220_device();
#endif




}

static void __init bps313x_map_io(void)
{
	lpc313x_map_io();
	iotable_init(bps313x_io_desc, ARRAY_SIZE(bps313x_io_desc));
}


MACHINE_START(BPS313XV1, "Brady BPS313XV1")
	.phys_io	= IO_APB01_PHYS,
	.io_pg_offst	= (io_p2v(IO_APB01_PHYS) >> 18) & 0xfffc,
	.boot_params	= 0x30000100,
	.map_io		= bps313x_map_io,
	.init_irq	= lpc313x_init_irq,
	.timer		= &lpc313x_timer,
	.init_machine	= bps313x_init,
MACHINE_END
