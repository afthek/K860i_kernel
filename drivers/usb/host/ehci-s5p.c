/*
 * SAMSUNG S5P USB HOST EHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>

#include <mach/regs-pmu.h>
#include <mach/regs-usb-host.h>
#ifdef CONFIG_SMM6260_MODEM

#include <mach/modem.h>
#include <linux/regulator/consumer.h>
//#include <mach/max8997.h>
#endif
#include <plat/cpu.h>
#include <plat/ehci.h>
#include <plat/usb-phy.h>
#include <plat/gpio-cfg.h>

/* For ehci on/off sysfs */
#ifdef CONFIG_SMM6260_MODEM
#include <mach/board_rev.h>
struct device *s5p_dev=NULL;
#endif

struct s5p_ehci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	int power_on;
};

#ifdef CONFIG_USB_EXYNOS_SWITCH
int s5p_ehci_port_power_off(struct platform_device *pdev)
{
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	(void) ehci_hub_control(hcd,
			ClearPortFeature,
			USB_PORT_FEAT_POWER,
			1, NULL, 0);
	/* Flush those writes */
	ehci_readl(ehci, &ehci->regs->command);
	return 0;
}
EXPORT_SYMBOL_GPL(s5p_ehci_port_power_off);

int s5p_ehci_port_power_on(struct platform_device *pdev)
{
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	(void) ehci_hub_control(hcd,
			SetPortFeature,
			USB_PORT_FEAT_POWER,
			1, NULL, 0);
	/* Flush those writes */
	ehci_readl(ehci, &ehci->regs->command);
	return 0;
}
EXPORT_SYMBOL_GPL(s5p_ehci_port_power_on);
#endif

static int s5p_ehci_configurate(struct usb_hcd *hcd)
{
	/* DMA burst Enable */
	writel(readl(INSNREG00(hcd->regs)) | ENA_DMA_INCR,
			INSNREG00(hcd->regs));
	return 0;
}
#ifdef CONFIG_SMM6260_MODEM
#define _WAIT_FOR_CP_RETRY_CNT 500 //haozz
static int s5p_wait_for_cp_resume(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 __iomem	*portsc ;
	u32 val32, retry_cnt = 0;

	portsc = &ehci->regs->port_status[1];
#ifdef CONFIG_SMM6260_MODEM
	smm6260_set_active_state(1); // CP USB Power On
#endif
	do {
		msleep(2);
		val32 = ehci_readl(ehci, portsc);
	} while (++retry_cnt < _WAIT_FOR_CP_RETRY_CNT && !(val32 & PORT_CONNECT));
	if(unlikely(retry_cnt == _WAIT_FOR_CP_RETRY_CNT)){
	    printk("%s: retry_cnt = %d\n", __func__, retry_cnt);
		return -1;
	}
	return 0;
}


static struct regulator *usb_reg_ehci_analog, *usb_reg_ehci_core ,*usb_reg_hsic;

void usb_host_phy_power_init(struct platform_device *pdev)
{
	int retval;



	if (!usb_reg_ehci_core) {
		usb_reg_ehci_core = regulator_get(&pdev->dev, "vdd10_ush");
		if (IS_ERR(usb_reg_ehci_core)) {
			retval = PTR_ERR(usb_reg_ehci_core);
			dev_err(&pdev->dev, "No VDD_USB_1.0V regualtor: %d\n",
				retval);
			usb_reg_ehci_core = NULL;
		}
	}

	if (!usb_reg_hsic) {
		usb_reg_hsic = regulator_get(&pdev->dev, "vdd18_hsic");
		if (IS_ERR(usb_reg_hsic)) {
			retval = PTR_ERR(usb_reg_hsic);
			dev_err(&pdev->dev, "No VDD_USB_1.8V regualtor: %d\n",
				retval);
			usb_reg_hsic = NULL;
		}
	}

	if (!usb_reg_ehci_analog) {
		usb_reg_ehci_analog = regulator_get(&pdev->dev, "vdd33_uotg");
		if (IS_ERR(usb_reg_ehci_analog)) {
			retval = PTR_ERR(usb_reg_ehci_analog);
			dev_err(&pdev->dev, "No VDD_USB_3.3V regualtor: %d\n",
				retval);
			usb_reg_ehci_analog = NULL;
		}
	}


	if (usb_reg_ehci_core)
		regulator_enable(usb_reg_ehci_core);
	if (usb_reg_hsic)
		regulator_enable(usb_reg_hsic);
	if (usb_reg_ehci_analog)
		regulator_enable(usb_reg_ehci_analog);

	printk(KERN_DEBUG "%s: ldo on\n", __func__);
	retval = regulator_is_enabled(usb_reg_ehci_core);
	printk(KERN_DEBUG "ehci check ldo usb_reg_ehci_core(%d)\n", retval);
	retval = regulator_is_enabled(usb_reg_hsic);
	printk(KERN_DEBUG "ehci check ldo usb_reg_hsic(%d)\n", retval);
	retval = regulator_is_enabled(usb_reg_ehci_analog);
	printk(KERN_DEBUG "ehci check ldo usb_reg_ehci_analog(%d)\n", retval);

}
void usb_host_phy_power_off(void)
{
	int retval;

	if (usb_reg_ehci_analog)
		regulator_disable(usb_reg_ehci_analog);

	if (usb_reg_hsic)
		regulator_disable(usb_reg_hsic);
	
	if (usb_reg_ehci_core)
			regulator_disable(usb_reg_ehci_core);


	printk(KERN_DEBUG "%s: ldo off\n", __func__);
	retval = regulator_is_enabled(usb_reg_ehci_analog);
	printk(KERN_DEBUG "ehci check ldo usb_reg_ehci_analog(%d)\n", retval);
	retval = regulator_is_enabled(usb_reg_hsic);
	printk(KERN_DEBUG "ehci check ldo usb_reg_hsic(%d)\n", retval);
	retval = regulator_is_enabled(usb_reg_ehci_core);
	printk(KERN_DEBUG "ehci check ldo usb_reg_ehci_core(%d)\n", retval);
}

/*
void usb_host_phy_power_init(struct platform_device *pdev)
{
	int retval;

	PMIC_Control_Set(PMIC_CONTROL_VDD10_USH,POWERON);
	PMIC_Control_Set(PMIC_CONTROL_VDD18_HSIC,POWERON);
	PMIC_Control_Set(PMIC_CONTROL_VDD33_UOTG,POWERON);




	printk(KERN_DEBUG "%s: ldo on\n", __func__);


}
void usb_host_phy_power_off(void)
{
	int retval;

	PMIC_Control_Set(PMIC_CONTROL_VDD33_UOTG,POWEROFF);
	PMIC_Control_Set(PMIC_CONTROL_VDD18_HSIC,POWEROFF);
	PMIC_Control_Set(PMIC_CONTROL_VDD10_USH,POWEROFF);

	printk(KERN_DEBUG "%s: ldo off\n", __func__);

}
*/
#endif

#ifdef CONFIG_PM
static int s5p_ehci_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	unsigned long flags;
	int rc = 0;

	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */

	spin_lock_irqsave(&ehci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED && hcd->state != HC_STATE_HALT) {
		spin_unlock_irqrestore(&ehci->lock, flags);
		return -EINVAL;
	}
	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&ehci->lock, flags);
#ifdef CONFIG_SMM6260_MODEM
	smm6260_set_active_state(0);
#endif
	//here add sleep for reproduce charege problem
	//msleep(1000*5);
	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, S5P_USB_PHY_HOST);

	clk_disable(s5p_ehci->clk);
#ifdef CONFIG_SMM6260_MODEM
	usb_host_phy_power_off();
#endif
	return rc;
}

static int s5p_ehci_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int err = 0;
	printk("%s,%d\n",__FUNCTION__,__LINE__);
	clk_enable(s5p_ehci->clk);
#ifdef CONFIG_SMM6260_MODEM	
	usb_host_phy_power_init(pdev);
#endif
    pm_runtime_resume(&pdev->dev);

	if (pdata->phy_init)
		err = pdata->phy_init(pdev, S5P_USB_PHY_HOST);  //haozz
	/*diog.zhao,0614*/
	//if(err<0)//if phy is already on,but in suspend,do resume atfirst
		//err = pdata->phy_resume(pdev, S5P_USB_PHY_HOST);
	s5p_ehci_configurate(hcd);

	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	if (ehci_readl(ehci, &ehci->regs->configured_flag) == FLAG_CF) {
		int	mask = INTR_MASK;

		if (!hcd->self.root_hub->do_remote_wakeup)
			mask &= ~STS_PCD;
		ehci_writel(ehci, mask, &ehci->regs->intr_enable);
		ehci_readl(ehci, &ehci->regs->intr_enable);
		return 0;
	}

	ehci_dbg(ehci, "lost power, restarting\n");
	usb_root_hub_lost_power(hcd->self.root_hub);

	(void) ehci_halt(ehci);
	(void) ehci_reset(ehci);

	/* emptying the schedule aborts any urbs */
	spin_lock_irq(&ehci->lock);
	if (ehci->reclaim)
		end_unlink_async(ehci);
	ehci_work(ehci);
	spin_unlock_irq(&ehci->lock);

	ehci_writel(ehci, ehci->command, &ehci->regs->command);
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	ehci_readl(ehci, &ehci->regs->command);	/* unblock posted writes */

	/* here we "know" root ports should always stay powered */
	ehci_port_power(ehci, 1);

	hcd->state = HC_STATE_SUSPENDED;

	return 0;
}

#else
#define s5p_ehci_suspend	NULL
#define s5p_ehci_resume		NULL
#endif

#ifdef CONFIG_USB_SUSPEND
static int s5p_ehci_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
#ifdef CONFIG_USB_EXYNOS_SWITCH
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
#endif
	printk("enter ehci runtime suspend function.\n");
	if (pdata && pdata->phy_suspend)
		pdata->phy_suspend(pdev, S5P_USB_PHY_HOST);

#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_board_rev_is_0_0()) {
		ehci_hub_control(hcd,
			ClearPortFeature,
			USB_PORT_FEAT_POWER,
			1, NULL, 0);
		/* Flush those writes */
		ehci_readl(ehci, &ehci->regs->command);

		msleep(20);
	}
#endif
	return 0;
}

static int s5p_ehci_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int rc = 0;
	printk("enter ehci runtime resume function.\n");
	if (dev->power.is_suspended)
		return 0;

	/* platform device isn't suspended */
	if (pdata && pdata->phy_resume)
		rc = pdata->phy_resume(pdev, S5P_USB_PHY_HOST);

	if (rc) {
		s5p_ehci_configurate(hcd);

		/* emptying the schedule aborts any urbs */
		spin_lock_irq(&ehci->lock);
		if (ehci->reclaim)
			end_unlink_async(ehci);
		ehci_work(ehci);
		spin_unlock_irq(&ehci->lock);

		usb_root_hub_lost_power(hcd->self.root_hub);

		ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
		ehci_writel(ehci, INTR_MASK, &ehci->regs->intr_enable);
		(void)ehci_readl(ehci, &ehci->regs->intr_enable);

		/* here we "know" root ports should always stay powered */
		ehci_port_power(ehci, 1);

		hcd->state = HC_STATE_SUSPENDED;
#ifdef CONFIG_USB_EXYNOS_SWITCH
	} else {
		if (samsung_board_rev_is_0_0()) {
			ehci_hub_control(ehci_to_hcd(ehci),
					SetPortFeature,
					USB_PORT_FEAT_POWER,
					1, NULL, 0);
			/* Flush those writes */
			ehci_readl(ehci, &ehci->regs->command);
			msleep(20);
		}
#endif
	}

	return 0;
}
int s5p_ehci_device_suspend()
{
	int err;
	if(s5p_dev){
		//err = s5p_ehci_suspend(s5p_dev);
		//s5p_dev->power.is_suspended = 1;
		return  s5p_ehci_suspend(s5p_dev);
		}	
}
int s5p_ehci_device_resume()
{
	int err;
	if(s5p_dev){
		err = s5p_ehci_resume(s5p_dev);
		//s5p_dev->power.is_suspended = 0;
		return err;
		}
}
#else
#define s5p_ehci_runtime_suspend	NULL
#define s5p_ehci_runtime_resume		NULL
#endif

static const struct hc_driver s5p_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "S5P EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	.reset			= ehci_init,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	.get_frame_number	= ehci_get_frame,

	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,

	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,
#ifdef CONFIG_SMM6260_MODEM
	.wait_for_device	= s5p_wait_for_cp_resume,
#endif
	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static ssize_t show_ehci_power(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);

	return sprintf(buf, "EHCI Power %s\n", (s5p_ehci->power_on) ? "on" : "off");
}

static ssize_t store_ehci_power(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	int power_on;
	int irq;
	int retval;

	if (sscanf(buf, "%d", &power_on) != 1)
		return -EINVAL;

	device_lock(dev);

	pm_runtime_get_sync(dev);
	if (!power_on && s5p_ehci->power_on) {
		printk(KERN_DEBUG "%s: EHCI turns off\n", __func__);
#ifdef CONFIG_USB_SUSPEND
		pm_runtime_disable(dev);
#endif
		s5p_ehci->power_on = 0;
		usb_remove_hcd(hcd);
		if (pdata && pdata->phy_exit)
			pdata->phy_exit(pdev, S5P_USB_PHY_HOST);	
#ifdef CONFIG_SMM6260_MODEM
		smm6260_set_active_state(0);
#endif
	} else if (power_on) {
		printk(KERN_DEBUG "%s: EHCI turns on\n", __func__);
		if (s5p_ehci->power_on) {
			usb_remove_hcd(hcd);
		}

		if (pdata && pdata->phy_init)
			pdata->phy_init(pdev, S5P_USB_PHY_HOST);

		irq = platform_get_irq(pdev, 0);
		retval = usb_add_hcd(hcd, irq,
				IRQF_DISABLED | IRQF_SHARED);
		if (retval < 0) {
			dev_err(dev, "Power On Fail\n");
			goto exit;
		}
		s5p_ehci_configurate(hcd);
#ifdef CONFIG_SMM6260_MODEM
		smm6260_set_active_state(1);
#endif
		s5p_ehci->power_on = 1;
#ifdef CONFIG_USB_SUSPEND
//		pm_runtime_set_active(dev);
		if(power_on == 1)
		pm_runtime_enable(dev);			
#endif
	}
exit:
	pm_runtime_put_sync(dev);
	device_unlock(dev);
	return count;
}

#ifdef CONFIG_SMM6260_MODEM
 int s5p_ehci_power(int value)
{
	struct platform_device *pdev = to_platform_device(s5p_dev);
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	int power_on= value;
	int irq;
	int retval;
	if(!s5p_dev)
		return -1;

	device_lock(s5p_dev);
	pm_runtime_get_sync(s5p_dev);
	if (!power_on && s5p_ehci->power_on) {
		printk(KERN_DEBUG "%s: EHCI turns off\n", __func__);
#ifdef CONFIG_USB_SUSPEND
		pm_runtime_disable(s5p_dev);
#endif
		s5p_ehci->power_on = 0;
		usb_remove_hcd(hcd);
		if (pdata && pdata->phy_exit)
			pdata->phy_exit(pdev, S5P_USB_PHY_HOST);	
#ifdef CONFIG_SMM6260_MODEM
		smm6260_set_active_state(0);
#endif
	} else if (power_on) {
		printk(KERN_DEBUG "%s: EHCI turns on\n", __func__);
		if (s5p_ehci->power_on) {
			usb_remove_hcd(hcd);
		}

		if (pdata && pdata->phy_init)
			pdata->phy_init(pdev, S5P_USB_PHY_HOST);
 

		irq = platform_get_irq(pdev, 0);
		retval = usb_add_hcd(hcd, irq,
				IRQF_DISABLED | IRQF_SHARED);
		if (retval < 0) {
			dev_err(s5p_dev, "Power On Fail\n");
			goto exit;
		}
		s5p_ehci_configurate(hcd);
#ifdef CONFIG_SMM6260_MODEM
		smm6260_set_active_state(1);
#endif
		s5p_ehci->power_on = 1;
#ifdef CONFIG_USB_SUSPEND
//		pm_runtime_set_active(dev);
		if(power_on == 1)
		pm_runtime_enable(s5p_dev);			
#endif
	}
exit:
	pm_runtime_put_sync(s5p_dev);
	device_unlock(s5p_dev);
	return 0;
}
#endif

static DEVICE_ATTR(ehci_power, 0664, show_ehci_power, store_ehci_power);
#if 0
static ssize_t store_ehci_suspend_resume(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int value;
	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;
	switch(value)
	{
		case 0:
			s5p_ehci_runtime_suspend(dev);
			break;
		case 1:
			s5p_ehci_runtime_resume(dev);
			break;
		case 2:
			s5p_ehci_suspend(dev);
			break;
		case 3:
			s5p_ehci_resume(dev);
			break;
		default:
			break;			
	}	
	return count;
}

static DEVICE_ATTR(ehci_suspend, 0664, NULL, store_ehci_suspend_resume);
#endif

static inline int create_ehci_sys_file(struct ehci_hcd *ehci)
{
#if 0
		device_create_file(ehci_to_hcd(ehci)->self.controller,
			&dev_attr_ehci_suspend);			
#endif
	return device_create_file(ehci_to_hcd(ehci)->self.controller,
			&dev_attr_ehci_power);
}

static inline void remove_ehci_sys_file(struct ehci_hcd *ehci)
{

	device_remove_file(ehci_to_hcd(ehci)->self.controller,
			&dev_attr_ehci_power);
#if 0
	device_remove_file(ehci_to_hcd(ehci)->self.controller,
			&dev_attr_ehci_suspend);
#endif
}

static int __devinit s5p_ehci_probe(struct platform_device *pdev)
{
	struct s5p_ehci_platdata *pdata;
	struct s5p_ehci_hcd *s5p_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	int irq;
	int err;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data defined\n");
		return -EINVAL;
	}

	s5p_ehci = kzalloc(sizeof(struct s5p_ehci_hcd), GFP_KERNEL);
	if (!s5p_ehci)
		return -ENOMEM;

	s5p_ehci->dev = &pdev->dev;

	hcd = usb_create_hcd(&s5p_ehci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto fail_hcd;
	}

	s5p_ehci->hcd = hcd;
	s5p_ehci->clk = clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(s5p_ehci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(s5p_ehci->clk);
		goto fail_clk;
	}

	err = clk_enable(s5p_ehci->clk);
	if (err)
		goto fail_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail;
	}
#ifdef CONFIG_SMM6260_MODEM
	usb_host_phy_power_init(pdev);
#endif
	if (pdata->phy_init)
		pdata->phy_init(pdev, S5P_USB_PHY_HOST);

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs +
		HC_LENGTH(ehci, readl(&ehci->caps->hc_capbase));

	s5p_ehci_configurate(hcd);

	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	err = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail;
	}

	platform_set_drvdata(pdev, s5p_ehci);

	create_ehci_sys_file(ehci);
	s5p_ehci->power_on = 1;
#ifdef CONFIG_SMM6260_MODEM
	s5p_dev = &pdev->dev;
#endif
#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (samsung_board_rev_is_0_0())
		ehci_hub_control(ehci_to_hcd(ehci),
				ClearPortFeature,
				USB_PORT_FEAT_POWER,
				1, NULL, 0);
#endif
#ifdef CONFIG_USB_SUSPEND
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
#endif
#if 0 //diog.zhao, this gpio should be request in modem_cfg function.
//#ifdef  CONFIG_SMM6260_MODEM //lisw debug temp
		err = gpio_request(GPIO_ACTIVE_STATE, "ACTIVE_STATE");
		if (err) {
			printk(KERN_ERR "fail to request gpio %s\n", "ACTIVE_STATE");
		} else {
			gpio_direction_output(GPIO_ACTIVE_STATE, 1);
			s3c_gpio_setpull(GPIO_ACTIVE_STATE, S3C_GPIO_PULL_NONE);
		}
#endif
#ifdef CONFIG_SMM6260_MODEM
	smm6260_set_active_state(1);
#endif
	return 0;

fail:
	iounmap(hcd->regs);
fail_io:
	clk_disable(s5p_ehci->clk);
fail_clken:
	clk_put(s5p_ehci->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(s5p_ehci);
	return err;
}

static int __devexit s5p_ehci_remove(struct platform_device *pdev)
{
	struct s5p_ehci_platdata *pdata = pdev->dev.platform_data;
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;

#ifdef CONFIG_USB_SUSPEND
	pm_runtime_disable(&pdev->dev);
#endif
	s5p_ehci->power_on = 0;
	remove_ehci_sys_file(hcd_to_ehci(hcd));
	usb_remove_hcd(hcd);

	if (pdata && pdata->phy_exit)
		pdata->phy_exit(pdev, S5P_USB_PHY_HOST);

	iounmap(hcd->regs);

	clk_disable(s5p_ehci->clk);
	clk_put(s5p_ehci->clk);

	usb_put_hcd(hcd);
	kfree(s5p_ehci);

	return 0;
}

static void s5p_ehci_shutdown(struct platform_device *pdev)
{
	struct s5p_ehci_hcd *s5p_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static const struct dev_pm_ops s5p_ehci_pm_ops = {
	.suspend		= s5p_ehci_suspend,
	.resume			= s5p_ehci_resume,
	.runtime_suspend	= s5p_ehci_runtime_suspend,
	.runtime_resume		= s5p_ehci_runtime_resume,
};

static struct platform_driver s5p_ehci_driver = {
	.probe		= s5p_ehci_probe,
	.remove		= __devexit_p(s5p_ehci_remove),
	.shutdown	= s5p_ehci_shutdown,
	.driver = {
		.name	= "s5p-ehci",
		.owner	= THIS_MODULE,
		.pm = &s5p_ehci_pm_ops,
	}
};

MODULE_ALIAS("platform:s5p-ehci");
