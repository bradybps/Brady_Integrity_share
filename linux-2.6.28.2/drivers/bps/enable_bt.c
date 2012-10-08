#include<linux/init.h>
#include<linux/module.h>
#include <mach/gpio.h>


static int __init bt_init(void)
{
	gpio_set_value(GPIO_GPIO12, 1);
	gpio_set_value(GPIO_GPIO13, 1);
	gpio_set_value(GPIO_GPIO19, 0);
	return 0;
}

static void __exit bt_exit(void)
{
	gpio_set_value(GPIO_GPIO12, 0);
	gpio_set_value(GPIO_GPIO13, 0);
	gpio_set_value(GPIO_GPIO19, 1);
}

MODULE_LICENSE("GPL");
module_init(bt_init);
module_exit(bt_exit);
