# 7. Commands and Code Reference

## 7.1 Environment

Check kernel:

```bash
uname -r
uname -m
```

Check matching build directory:

```bash
ls -l /lib/modules/$(uname -r)/build
```

Install/build prerequisites:

```bash
sudo apt install build-essential linux-headers-$(uname -r) gcc make git
```

## 7.2 Build driver

```bash
cd ~/vsensor/driver
make
```

Clean:

```bash
make clean
```

Load:

```bash
sudo insmod vsensor.ko
```

Check:

```bash
lsmod | grep vsensor
ls -l /dev/vsensor
```

Unload:

```bash
sudo rmmod vsensor
```

Logs:

```bash
dmesg | tail -50
dmesg -w
```

## 7.3 Permissions

Correct pattern for writing to a root-owned device node:

```bash
echo "temperature=31.2 C" | sudo tee /dev/vsensor
```

Why this works:

```text
echo
  |
pipe
  |
sudo tee
  |
/dev/vsensor
```

With:

```bash
sudo echo "..." > /dev/vsensor
```

the shell performs `>` before `sudo` runs.

## 7.4 Interrupt inspection

```bash
cat /proc/interrupts
```

This shows interrupt counters and associated handlers/devices.

## 7.5 Timer inspection

```bash
sudo cat /proc/timer_list | head -30
```

This shows kernel timer information.

## 7.6 Debugfs

Check:

```bash
sudo cat /sys/kernel/debug/vsensor/stats
```

Example fields:

```text
counter: ...
interval_ms: ...
buffer_head: ...
buffer_tail: ...
buffered_samples: ...
dropped_samples: ...
```

## 7.7 Userspace programs

Typical programs created during the project:

```text
sensor_app.c
poll_test.c
epoll_test.c
memory_demo.c
```

Compile:

```bash
gcc -o sensor_app sensor_app.c
gcc -o poll_test poll_test.c
gcc -o epoll_test epoll_test.c
gcc -o memory_demo memory_demo.c
```

## 7.8 strace

Observe system calls:

```bash
strace -e trace=openat,read,close ./sensor_app
```

Poll:

```bash
strace -e trace=poll,read ./poll_test
```

This is useful for connecting application-level calls to kernel activity.

## 7.9 Virtual memory

Inspect the current shell:

```bash
cat /proc/$$/maps
```

Inspect the current process:

```bash
cat /proc/self/maps
```

The `memory_demo` program prints addresses for global, stack, and heap variables.

## 7.10 Important kernel APIs

### Module lifecycle

```c
module_init()
module_exit()
```

### Device registration

```c
alloc_chrdev_region()
cdev_init()
cdev_add()
cdev_del()
unregister_chrdev_region()
```

### Device model

```c
class_create()
device_create()
device_destroy()
class_destroy()
```

### User memory

```c
copy_to_user()
copy_from_user()
```

### Synchronization

```c
mutex_lock()
mutex_unlock()
```

### Timer

```c
timer_setup()
mod_timer()
timer_shutdown_sync()
```

### Workqueue

```c
INIT_WORK()
schedule_work()
cancel_work_sync()
```

### Wait queue

```c
DECLARE_WAIT_QUEUE_HEAD()
wait_event_interruptible()
wake_up_interruptible()
```

### Readiness

```c
poll_wait()
POLLIN
POLLRDNORM
```

### Debugfs

```c
debugfs_create_dir()
debugfs_create_file()
debugfs_remove_recursive()
```

### Sequential file output

```c
single_open()
seq_read()
seq_lseek()
single_release()
seq_printf()
```

## 7.11 Useful conceptual source layout

```text
~/vsensor/
├── driver/
│   ├── vsensor.c
│   ├── vsensor_mutex.c      # experimental race-condition variant
│   └── Makefile
├── include/
│   └── vsensor_ioctl.h
└── userspace/
    ├── sensor_app.c
    ├── poll_test.c
    ├── epoll_test.c
    └── memory_demo.c
```

## 7.12 Final read architecture

The most important sequence to remember:

```text
workqueue
   |
generate sample
   |
mutex
   |
ring buffer
   |
unlock
   |
wake_up_interruptible()
   |
read/poll/epoll wakes
   |
vsensor_read()
   |
mutex
   |
copy ring entry -> local kernel buffer
   |
advance tail
   |
unlock
   |
copy_to_user()
   |
application
```
