#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
    pr_info("vsensor: hello from kernel!\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("vsensor: goodbye from kernel!\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shrey");
MODULE_DESCRIPTION("Minimal kernel module for learning");
