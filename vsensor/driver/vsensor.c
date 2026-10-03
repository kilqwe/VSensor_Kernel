#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/ioctl.h>
#include "../include/vsensor_ioctl.h"
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define DEVICE_NAME "vsensor"
#define SENSOR_DATA_SIZE 64
#define SENSOR_BUFFER_SIZE 8

static char sensor_buffer[SENSOR_BUFFER_SIZE][SENSOR_DATA_SIZE];
static unsigned int buffer_head;
static unsigned int buffer_tail;
static DEFINE_MUTEX(sensor_lock);
static dev_t vsensor_dev;
static struct cdev vsensor_cdev;
static struct class *vsensor_class;
static struct timer_list sensor_timer;
static struct work_struct sensor_work;
static int sensor_counter;
static unsigned int sensor_interval_ms = 100;
static DECLARE_WAIT_QUEUE_HEAD(sensor_wait_queue);
static struct dentry *vsensor_debugfs_dir;
static unsigned int dropped_samples;


static bool sensor_buffer_empty(void)
{
    return buffer_head == buffer_tail;
}

static bool sensor_buffer_full(void)
{
    return ((buffer_head + 1) % SENSOR_BUFFER_SIZE) == buffer_tail;
}

static int vsensor_stats_show(struct seq_file *m, void *v)
{
    unsigned int buffered_samples;

    mutex_lock(&sensor_lock);

    if (buffer_head >= buffer_tail)
        buffered_samples = buffer_head - buffer_tail;
    else
        buffered_samples = SENSOR_BUFFER_SIZE
                           - buffer_tail
                           + buffer_head;

    seq_printf(m, "counter: %u\n", sensor_counter);
    seq_printf(m, "interval_ms: %u\n", sensor_interval_ms);
    seq_printf(m, "buffer_head: %u\n", buffer_head);
    seq_printf(m, "buffer_tail: %u\n", buffer_tail);
    seq_printf(m, "buffered_samples: %u\n", buffered_samples);
    seq_printf(m, "dropped_samples: %u\n", dropped_samples);

    mutex_unlock(&sensor_lock);

    return 0;
}

static int vsensor_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, vsensor_stats_show, NULL);
}

static ssize_t vsensor_read(struct file *file,
                            char __user *buffer,
                            size_t count,
                            loff_t *ppos)
{
    char temp[SENSOR_DATA_SIZE];
    size_t message_len;

    if (count == 0)
        return 0;

    if (wait_event_interruptible(sensor_wait_queue,
                                  !sensor_buffer_empty()))
        return -ERESTARTSYS;

    mutex_lock(&sensor_lock);

    message_len = strlen(sensor_buffer[buffer_tail]);

    if (count > message_len)
        count = message_len;

    memcpy(temp, sensor_buffer[buffer_tail], count);

    buffer_tail = (buffer_tail + 1) % SENSOR_BUFFER_SIZE;

    mutex_unlock(&sensor_lock);

    if (copy_to_user(buffer, temp, count))
        return -EFAULT;

    return count;
}

static ssize_t vsensor_write(struct file *file,
                             const char __user *buffer,
                             size_t count,
                             loff_t *ppos)
{
    char temp[SENSOR_DATA_SIZE];

    if (count == 0)
        return 0;

    if (count >= SENSOR_DATA_SIZE)
        return -EINVAL;

    if (copy_from_user(temp, buffer, count))
        return -EFAULT;

    temp[count] = '\0';

    mutex_lock(&sensor_lock);

    if (sensor_buffer_full()) {
        mutex_unlock(&sensor_lock);
        return -ENOSPC;
    }

    strcpy(sensor_buffer[buffer_head], temp);

    buffer_head = (buffer_head + 1) % SENSOR_BUFFER_SIZE;

    mutex_unlock(&sensor_lock);

    wake_up_interruptible(&sensor_wait_queue);

    return count;
}

static void vsensor_work_fn(struct work_struct *work)
{
    mutex_lock(&sensor_lock);

    sensor_counter++;

    if (sensor_buffer_full()) {
        dropped_samples++;
        pr_info("vsensor: buffer full, dropping reading\n");
        mutex_unlock(&sensor_lock);
        return;
    }

    snprintf(sensor_buffer[buffer_head],
             SENSOR_DATA_SIZE,
             "temperature=%d.%d C\n",
             25 + (sensor_counter % 5),
             (sensor_counter * 3) % 10);

    pr_info("vsensor: generated %s",
            sensor_buffer[buffer_head]);

    buffer_head = (buffer_head + 1) % SENSOR_BUFFER_SIZE;

    mutex_unlock(&sensor_lock);

    wake_up_interruptible(&sensor_wait_queue);
}

static void vsensor_timer_fn(struct timer_list *timer)
{
    schedule_work(&sensor_work);

    mod_timer(&sensor_timer,
              jiffies + msecs_to_jiffies(sensor_interval_ms));
}

static long vsensor_ioctl(struct file *file,
                          unsigned int cmd,
                          unsigned long arg)
{
    unsigned int interval;

    switch (cmd) {

    case VSENSOR_SET_INTERVAL:
        if (copy_from_user(&interval,
                           (unsigned int __user *)arg,
                           sizeof(interval)))
            return -EFAULT;

        if (interval < 100 || interval > 10000)
            return -EINVAL;

        mutex_lock(&sensor_lock);

        sensor_interval_ms = interval;

        mutex_unlock(&sensor_lock);

        mod_timer(&sensor_timer,
                  jiffies + msecs_to_jiffies(sensor_interval_ms));

        pr_info("vsensor: interval set to %u ms\n",
                sensor_interval_ms);

        return 0;

    case VSENSOR_GET_INTERVAL:
        mutex_lock(&sensor_lock);

        interval = sensor_interval_ms;

        mutex_unlock(&sensor_lock);

        if (copy_to_user((unsigned int __user *)arg,
                         &interval,
                         sizeof(interval)))
            return -EFAULT;

        return 0;

    default:
        return -ENOTTY;
    }
}

static __poll_t vsensor_poll(struct file *file,
                             poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &sensor_wait_queue, wait);

    mutex_lock(&sensor_lock);

    if (!sensor_buffer_empty())
        mask |= POLLIN | POLLRDNORM;

    mutex_unlock(&sensor_lock);

    return mask;
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
    .poll = vsensor_poll,
    .unlocked_ioctl = vsensor_ioctl,
};

static const struct file_operations vsensor_stats_fops = {
    .owner = THIS_MODULE,
    .open = vsensor_stats_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init vsensor_init(void)
{
    int ret;
    sensor_counter = 0;
    dropped_samples = 0;
    buffer_head = 0;
    buffer_tail = 0;
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

    timer_setup(&sensor_timer, vsensor_timer_fn, 0);
    INIT_WORK(&sensor_work, vsensor_work_fn);


    pr_info("vsensor: device created\n");
    mod_timer(&sensor_timer, jiffies + msecs_to_jiffies(sensor_interval_ms));
    vsensor_debugfs_dir = debugfs_create_dir("vsensor", NULL);

    debugfs_create_file("stats", 0444, vsensor_debugfs_dir, NULL, &vsensor_stats_fops);

    pr_info("vsensor: debugfs initialized\n");
    return 0;
}

static void __exit vsensor_exit(void)
{
    timer_shutdown_sync(&sensor_timer);
    cancel_work_sync(&sensor_work);

    debugfs_remove_recursive(vsensor_debugfs_dir);

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
