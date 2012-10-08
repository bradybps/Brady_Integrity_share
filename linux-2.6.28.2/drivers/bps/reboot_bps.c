#include<linux/init.h>
#include<linux/module.h>
#include <mach/hardware.h>
#include <mach/cgu.h>
#include <asm/delay.h>


static int __init reboot_init(void)
{
	/* enable WDT clock */
	cgu_clk_en_dis(CGU_SB_WDOG_PCLK_ID, 1);

	/* Disable watchdog */
	WDT_TCR = 0;
	WDT_MCR = WDT_MCR_STOP_MR1 | WDT_MCR_INT_MR1;

	/*  If TC and MR1 are equal a reset is generated. */
	WDT_PR  = 0x00000002;
	WDT_TC  = 0x00000FF0;
	WDT_MR0 = 0x0000F000;
	WDT_MR1 = 0x00001000;
	WDT_EMR = WDT_EMR_CTRL1(0x3);
	/* Enable watchdog timer; assert reset at timer timeout */
	WDT_TCR = WDT_TCR_CNT_EN;
	
	udelay(100);

	return 0;
}

static void __exit reboot_exit(void)
{

}

MODULE_LICENSE("GPL");
module_init(reboot_init);
module_exit(reboot_exit);
