#include <linux/module.h>
#include <linux/moduleparam.h>
#include <mach/gpio.h>

static int red;
static int green;
module_param(red,int,0);
module_param(green,int,0);


static int on_load(void)
{
	if(red)
		gpio_set_value(GPIO_GPIO14, 1);
	else
		gpio_set_value(GPIO_GPIO14, 0);

	if(green)
		gpio_set_value(GPIO_GPIO16, 1);
	else
		gpio_set_value(GPIO_GPIO16, 0);

	return -EBUSY;
}


module_init(on_load);
MODULE_LICENSE("GPL");
