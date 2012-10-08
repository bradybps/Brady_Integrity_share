/*
 *  Author:	Durgesh Pattamatta
 *  Copyright (C) 2009 NXP semiconductors
 *
 * Description:
 * Helper routines for LPC313x/4x/5x SoCs from NXP, needed by the fsl_udc_core.c
 * driver to function correctly on these systems.
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
 */
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/fsl_devices.h>

#include <asm/io.h>
#include <asm/delay.h>

#include "fsl_usb2_udc.h"

static struct fsl_udc *temp_udc;
static struct usb_dr_device *udc_regs;
static unsigned int saved_irq_mask;
static unsigned int saved_otg_irq_mask;

int fsl_udc_clk_init(struct platform_device *pdev, struct usb_dr_device *regs)
{
	udc_regs = regs;
	return 0;
}

void lpc_on_suspend(struct fsl_udc *udc)
{
	temp_udc = udc;
}

void lpc_udc_suspend(struct platform_device *pdev)
{
	unsigned long flags;
	unsigned int otgsc_temp;
	unsigned int usbintr_temp;
	unsigned int port_sc;
	unsigned int tout = 1000;

	/* save irq mask */
	spin_lock_irqsave(&temp_udc->lock, flags);
	saved_irq_mask = readl(&udc_regs->usbintr);

	otgsc_temp = readl(&udc_regs->otgsc);
	saved_otg_irq_mask =  otgsc_temp & 0xFF000000; 
	otgsc_temp &= ~0xFF000000;
	writel(otgsc_temp, &udc_regs->otgsc);

	/* enable only needed irqs to wake */
	writel(USB_INTR_PTC_DETECT_EN | USB_INTR_RESET_EN | USB_INTR_DEVICE_SUSPEND, &udc_regs->usbintr);

	/* Clear notification bits */
	writel(0xFFFFFFFF, &udc_regs->usbsts);

	/* Put PHY to low power state. This will automatically power down USB
	PLL. */
	do {
		port_sc = readl(&udc_regs->portsc1);
		writel((port_sc | PORTSCX_PHY_LOW_POWER_SPD | PORTSCX_PORT_SUSPEND), &udc_regs->portsc1);
		usbintr_temp = readl(&udc_regs->usbintr);

		/* wait of HW to tell when to switch off AHB clock */
		/* wait for PLL to lock */
		udelay(5);
		if (!tout--) break;
	} while( port_sc != usbintr_temp);

	enable_irq_wake(IRQ_USB);
	spin_unlock_irqrestore(&temp_udc->lock, flags);
}

void lpc_udc_resume(struct platform_device *pdev)
{
	unsigned int port_sc;
	unsigned long flags;

	spin_lock_irqsave(&temp_udc->lock, flags);

	/* Bring PHY to active state. This should be done automatically by HW
	when it see resume signalling. Also the USB PLL is powered-up in HW.*/
	port_sc = readl(&udc_regs->portsc1);
	if (port_sc & PORTSCX_PHY_LOW_POWER_SPD) {
		port_sc &= ~PORTSCX_PHY_LOW_POWER_SPD;
		writel(port_sc, &udc_regs->portsc1);
	}

	/*restore irq mask */
	writel(saved_irq_mask, &udc_regs->usbintr);
	saved_otg_irq_mask |= readl(&udc_regs->otgsc);
	writel(saved_otg_irq_mask, &udc_regs->otgsc);

	disable_irq_wake(IRQ_USB);
	spin_unlock_irqrestore(&temp_udc->lock, flags);
}

