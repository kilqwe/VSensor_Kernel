# 2. Kernel Concepts

## 2.1 User space vs kernel space

Linux separates normal applications from privileged kernel code.

```text
USERSPACE
  sensor_app
     |
     | system calls
     v
KERNEL
  VFS
  driver
  scheduler
  memory management
  etc.
```

The separation provides isolation and protection.

A normal application cannot simply access arbitrary kernel memory.

## 2.2 Kernel module lifecycle

The first milestone was a minimal module:

```c
static int __init hello_init(void)
{
    pr_info("vsensor: hello from the kernel!\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("vsensor: goodbye from the kernel!\n");
}

module_init(hello_init);
module_exit(hello_exit);
```

Build:

```bash
make
```

Load:

```bash
sudo insmod hello_kernel.ko
```

Unload:

```bash
sudo rmmod hello_kernel
```

Inspect:

```bash
dmesg | tail
```

The BTF-related build message encountered during development was not a compilation failure; the module still built and loaded.

## 2.3 Kernel logging

Userspace commonly uses:

```c
printf()
```

Kernel code uses kernel logging APIs:

```c
pr_info()
pr_warn()
pr_err()
pr_debug()
```

Example:

```c
pr_info("vsensor: generated %s", sensor_data);
```

Observe logs:

```bash
dmesg
dmesg -w
```

Logging is useful, but excessive logging can alter timing and make concurrency bugs harder to reproduce.

## 2.4 VFS

The Virtual File System provides a common interface to many kinds of objects.

The application sees:

```c
fd = open("/dev/vsensor", O_RDONLY);
read(fd, buffer, 128);
```

The VFS uses the file descriptor and associated file object to reach the driver's callbacks.

The driver therefore fits into the normal Unix file abstraction.

## 2.5 System calls

Important userspace calls in this project:

```text
open()
read()
write()
ioctl()
poll()
epoll_wait()
close()
```

These cross from userspace into the kernel through system-call machinery.

A useful interview mental model is:

```text
userspace API
    |
system call
    |
kernel
    |
subsystem/VFS
    |
driver callback
```

## 2.6 Interrupts

A hardware interrupt is an event delivered to the CPU by hardware.

The conceptual path is:

```text
hardware
   |
   v
IRQ
   |
   v
interrupt handler
   |
   v
defer work if necessary
```

Interrupt handlers should be short and cannot arbitrarily sleep.

The project did not implement a real hardware IRQ because the virtual sensor has no physical hardware.

The VM's existing interrupt infrastructure was inspected with:

```bash
cat /proc/interrupts
```

This showed interrupts for devices such as timers, storage, USB/network interfaces, and VirtualBox-related devices.

## 2.7 Timer vs interrupt vs workqueue

These are different mechanisms:

### Hardware interrupt

Hardware tells the CPU that an event occurred.

### Kernel timer

The kernel invokes a callback at a scheduled time.

### Workqueue

The kernel schedules work to execute in worker-thread context.

For this project:

```text
timer
  |
  v
schedule_work()
  |
  v
workqueue
```

## 2.8 Why not do everything in the timer callback?

The timer callback is not the place for arbitrary sleepable work.

The callback only schedules the worker:

```c
static void vsensor_timer_fn(struct timer_list *timer)
{
    schedule_work(&sensor_work);

    mod_timer(&sensor_timer,
              jiffies + msecs_to_jiffies(sensor_interval_ms));
}
```

The worker can use the mutex and update driver state.

## 2.9 Module cleanup

The driver must not be unloaded while asynchronous callbacks can still execute.

The final cleanup uses:

```c
timer_shutdown_sync(&sensor_timer);
cancel_work_sync(&sensor_work);
```

This is particularly important because the timer callback re-arms itself with `mod_timer()`.

The lesson is broader than this API:

> Asynchronous work creates object-lifetime problems. Cleanup must stop and synchronize with all asynchronous execution before freeing the associated state.

## 2.10 Error codes

Kernel functions communicate failures through negative errno values.

Examples used in this project:

```text
-EFAULT  invalid/failing userspace memory access
-EINVAL  invalid argument
-ENOTTY  unsupported ioctl command
-ERESTARTSYS  interrupted wait that can be restarted
```
