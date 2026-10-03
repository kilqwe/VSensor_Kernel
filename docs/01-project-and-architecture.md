# 1. Project and Architecture

## 1.1 Why this project exists

The project was chosen to have a hands-on experience on OS internals, system software, C/C++, device drivers, debugging, embedded systems, etc.

Rather than building a large application, the goal was to build a small system where every layer could be understood.

## 1.2 What is a character device?

A character device provides a byte-oriented interface through a file descriptor.

Userspace can interact with it using familiar operations:

```text
open()
read()
write()
ioctl()
poll()
close()
```

Linux represents the device through a device node:

```text
/dev/vsensor
```

The application does not call the driver function directly. The path is approximately:

```text
application
   |
system call
   |
VFS
   |
file_operations
   |
vsensor driver callback
```

## 1.3 Major/minor numbers

Linux identifies device instances with a `dev_t`, containing major and minor numbers.

The driver uses:

```c
alloc_chrdev_region(&vsensor_dev, 0, 1, DEVICE_NAME);
```

This dynamically allocates a device number.

The major number broadly identifies the driver/device class, while the minor number identifies an instance.

For this project there is one virtual device.

## 1.4 cdev

The driver creates a `struct cdev`:

```c
static struct cdev vsensor_cdev;
```

It is initialized with:

```c
cdev_init(&vsensor_cdev, &vsensor_fops);
```

and registered with:

```c
cdev_add(&vsensor_cdev, vsensor_dev, 1);
```

This connects the character device to its `file_operations`.

## 1.5 /dev node

The driver creates the device node with the kernel device model:

```c
device_create(vsensor_class, NULL,
              vsensor_dev, NULL,
              DEVICE_NAME);
```

The result is:

```text
/dev/vsensor
```

The node is the userspace-visible handle used by `open()`.

## 1.6 file_operations

The driver provides callbacks such as:

```c
static const struct file_operations vsensor_fops = {
    .owner = THIS_MODULE,
    .open = vsensor_open,
    .release = vsensor_release,
    .read = vsensor_read,
    .write = vsensor_write,
    .unlocked_ioctl = vsensor_ioctl,
    .poll = vsensor_poll,
};
```

This is the core VFS-to-driver dispatch mechanism.

## 1.7 Read path

The final conceptual read path is:

```text
userspace read()
    |
    v
system call
    |
    v
VFS
    |
    v
vsensor_read()
    |
    v
wait_event_interruptible()
    |
    +-- no sample --> sleep
    |
    +-- sample available
             |
             v
        acquire mutex
             |
             v
       copy ring entry
       to local kernel buffer
             |
             v
        advance tail
             |
             v
        release mutex
             |
             v
        copy_to_user()
             |
             v
         userspace
```

## 1.8 Producer path

The timer is not responsible for heavy processing.

```text
kernel timer
    |
    v
timer callback
    |
schedule_work()
    |
    v
workqueue
    |
    v
vsensor_work_fn()
    |
    v
generate sensor reading
    |
    v
put reading in ring buffer
    |
    v
wake_up_interruptible()
```

## 1.9 Why timer + workqueue?

A timer callback should remain lightweight.

The timer is used as a trigger:

```c
schedule_work(&sensor_work);
```

The workqueue performs the actual sensor-state update.

This separation makes the execution contexts explicit:

```text
timer callback
    -> lightweight, cannot perform arbitrary sleeping work

workqueue
    -> process context, can sleep/use mutexes
```

## 1.10 Real hardware comparison

A physical driver could have:

```text
hardware event
    |
    v
IRQ handler
    |
    v
schedule_work()
    |
    v
workqueue
    |
    v
process data
```

This project substitutes a software timer for the hardware event:

```text
software timer
    |
    v
schedule_work()
```


