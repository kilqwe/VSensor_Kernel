#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#define DEVICE_NAME "vsensor"
#define SENSOR_DATA_SIZE 64


static char sensor_data[SENSOR_DATA_SIZE] = "temperatureEeeeEEEEeEEEeEEE5 C\n";
//static DEFINE_MUTEX(sensor_lock);
static dev_t vsensor_dev;
static struct cdev vsensor_cdev;
static struct class *vsensor_class;

static ssize_t vsensor_read(struct file *file,
                            char __user *buffer,
                            size_t count,
                            loff_t *ppos)
{
    size_t message_len;

  //  mutex_lock(&sensor_lock);

    message_len = strlen(sensor_data);

    pr_info("vsensor: read called, count=%zu, pos=%lld\n",
            count, *ppos);

    if (*ppos >= message_len) {
    //    mutex_unlock(&sensor_lock);
        return 0;
    }

    if (count > message_len - *ppos)
        count = message_len - *ppos;

    if (copy_to_user(buffer, sensor_data + *ppos, count)) {
        pr_err("vsensor: copy_to_user failed\n");
      //  mutex_unlock(&sensor_lock);
        return -EFAULT;
    }

    *ppos += count;

    //mutex_unlock(&sensor_lock);

    pr_info("vsensor: returned %zu bytes\n", count);

    return count;
}

static ssize_t vsensor_write(struct file *file,
                             const char __user *buffer,
                             size_t count,
                             loff_t *ppos)
{
    char temp[SENSOR_DATA_SIZE];

    if (count >= SENSOR_DATA_SIZE)
        return -EINVAL;

    if (copy_from_user(temp, buffer, count)) {
        pr_err("vsensor: copy_from_user failed\n");
        return -EFAULT;
    }

    temp[count] = '\0';

    /*
     * DELIBERATELY UNSAFE:
     * We modify shared sensor_data in two stages,
     * giving another thread/process a chance to read
     * the buffer in the middle.
     */

    memset(sensor_data, 'X', SENSOR_DATA_SIZE / 2);

    msleep(100);

    memset(sensor_data + SENSOR_DATA_SIZE / 2,
           'Y',
           SENSOR_DATA_SIZE / 2 - 1);

    sensor_data[SENSOR_DATA_SIZE - 1] = '\0';

    pr_info("vsensor: new sensor data written\n");

    return count;
}

static int vsensor_open(struct inode *inode, struct file *file)
{
    pr_info("vsensor: device opened\n");
    return 0;
}

static int vsensor_release(struct inode *inode, struct file *file)
{
    pr_info("vsensor: device closed\n");
    return 0;
}

static const struct file_operations vsensor_fops = {
    .owner = THIS_MODULE,
    .open = vsensor_open,
    .release = vsensor_release,
    .read = vsensor_read,
    .write = vsensor_write,
};

static int __init vsensor_init(void)
{
    int ret;

    pr_info("vsensor: initializing\n");

    ret = alloc_chrdev_region(&vsensor_dev, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&vsensor_cdev, &vsensor_fops);

    ret = cdev_add(&vsensor_cdev, vsensor_dev, 1);
    if (ret < 0) {
        unregister_chrdev_region(vsensor_dev, 1);
        return ret;
    }

    vsensor_class = class_create(DEVICE_NAME);
    if (IS_ERR(vsensor_class)) {
        cdev_del(&vsensor_cdev);
        unregister_chrdev_region(vsensor_dev, 1);
        return PTR_ERR(vsensor_class);
    }

    if (IS_ERR(device_create(vsensor_class, NULL,
                             vsensor_dev, NULL, DEVICE_NAME))) {
        class_destroy(vsensor_class);
        cdev_del(&vsensor_cdev);
        unregister_chrdev_region(vsensor_dev, 1);
        return -EINVAL;
    }

    pr_info("vsensor: device created\n");

    return 0;
}

static void __exit vsensor_exit(void)
{
    device_destroy(vsensor_class, vsensor_dev);
    class_destroy(vsensor_class);

    cdev_del(&vsensor_cdev);
    unregister_chrdev_region(vsensor_dev, 1);

    pr_info("vsensor: device removed\n");
}

module_init(vsensor_init);
module_exit(vsensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shrey");
MODULE_DESCRIPTION("Virtual sensor character device");

