# 4. Development Journey, Bugs and Hardships

This file intentionally documents the messy parts of the project. These are useful interview material because they show how problems were approached rather than only showing the final code.

## 4.1 Initial constraint

The project had a limited time budget.

The goal was therefore not to build a production-quality driver with every kernel feature. The goal was to build one coherent system deeply enough to explain it.

This affected later scope decisions: robustness work and deliberate bug-hunting beyond what was useful for learning were eventually stopped.

## 4.2 First environment: WSL2

The first attempt used WSL2.

The running kernel was:

```text
6.6.87.2-microsoft-standard-WSL2
```

A Microsoft WSL kernel source tree was cloned and prepared.

The source tree produced a different kernel release:

```text
6.6.123.2...
```

The module build then encountered modpost/symbol issues involving missing `Module.symvers` / undefined symbols.

### Lesson

Kernel modules are tightly coupled to the kernel they are built for.

A useful mental model:

```text
running kernel
     |
     +--> matching build directory/headers
     |
     +--> matching kernel configuration/symbol information
```

For a learning project with limited time, fighting the WSL kernel build environment was not productive.

## 4.3 Switch to VirtualBox Ubuntu

The project moved to an Ubuntu 24.04 AMD64 VM.

There were some setup mistakes, including downloading the wrong architecture ISO and unrelated downloads while trying to get the VM environment working.

Once the VM was running, the guest kernel was:

```text
7.0.0-34-generic
```

and matching headers were available at:

```text
/usr/src/linux-headers-7.0.0-34-generic
```

The kernel build directory was:

```text
/lib/modules/$(uname -r)/build
```

This environment was much easier to use for the project.

## 4.4 Minimal kernel module

The first successful milestone was simply:

```text
insmod
  |
  v
module init
  |
  v
pr_info()
```

and then:

```text
rmmod
  |
  v
module exit
```

This proved the toolchain and kernel-module workflow before adding complexity.

## 4.5 Character device

The next milestone was `/dev/vsensor`.

The driver needed:

```text
alloc_chrdev_region()
cdev_init()
cdev_add()
class_create()
device_create()
```

This taught the difference between:

- a kernel device number
- a `struct cdev`
- the VFS callbacks
- the `/dev` device node

## 4.6 First read problem

When `cat /dev/vsensor` initially failed with an invalid-argument style error, kernel logging was added.

The logs showed that `open()` and `release()` were being called but the intended read path was not yet correctly wired.

The fix was to explicitly implement and register `.read`.

### Lesson

When debugging a kernel driver, instrument the boundary where you think execution should arrive.

The first question was not "why is cat broken?" but:

> Did VFS actually reach my driver's read callback?

## 4.7 `/dev/vsensor` permission problem

A normal shell command:

```bash
echo "temperature=31.2 C" > /dev/vsensor
```

returned permission denied.

The initial instinct was to try:

```bash
sudo echo "temperature=31.2 C" > /dev/vsensor
```

That still does not work because the shell performs `>` redirection before `sudo` executes `echo`.

The working pattern was:

```bash
echo "temperature=31.2 C" | sudo tee /dev/vsensor
```

### Lesson

`sudo` applies to the command, not to shell redirection performed by the parent shell.

## 4.8 Mutex and race experiment

After introducing shared state, synchronization was added with a mutex.

To make the value of the mutex obvious, a separate experimental driver intentionally removed the lock and inserted a 100 ms delay between two buffer modifications.

Running a reader loop and writer loop concurrently made inconsistent intermediate state visible.

### Lesson

A race condition is easier to understand when it is reproducible.

The experiment also demonstrated why timing-sensitive bugs can be difficult in real systems: without an artificial delay, the race might be rare and difficult to observe.

## 4.9 Timer/workqueue

The driver initially needed periodic sample generation.

A kernel timer was used to trigger work.

A key design correction was made during implementation:

Timer/work initialization should happen only after successful device setup, and cleanup must stop asynchronous activity before device state is destroyed.

## 4.10 Timer cleanup API problem

The kernel version in the VM is modern enough that an attempt to use:

```c
del_timer_sync()
```

caused an implicit-declaration compilation error in this environment.

The final cleanup uses:

```c
timer_shutdown_sync(&sensor_timer);
cancel_work_sync(&sensor_work);
```

The important lesson is not to memorize one API blindly.

Kernel APIs evolve. Always interpret the compiler error in the context of the running kernel version and its headers.

## 4.11 ioctl

Once the basic sensor worked, an ioctl interface was added to control the sampling interval.

A shared header was created:

```text
include/vsensor_ioctl.h
```

This prevented driver and userspace from independently inventing command numbers.

Commands:

```c
VSENSOR_SET_INTERVAL
VSENSOR_GET_INTERVAL
```

The driver validates the interval:

```text
100 ms <= interval <= 10000 ms
```

## 4.12 Blocking I/O

The first userspace application repeatedly closed and reopened the device because the initial read implementation used file-position/EOF behavior.

That was a useful intermediate experiment, but it was not a natural sensor interface.

The driver was then changed to a blocking-read model using a wait queue.

The final userspace application opens once and repeatedly calls `read()`.

### Lesson

The interface should reflect the semantics of the device.

A streaming sensor naturally fits:

```text
open once
read repeatedly
close once
```

## 4.13 poll and epoll

After blocking reads worked, readiness notification was added.

This showed the relationship:

```text
wait queue
   |
   +--> blocking read
   |
   +--> poll
   |
   +--> epoll
```

The same readiness condition can support several userspace I/O models.

## 4.14 Ring buffer

The single sensor-data string was then replaced with a ring buffer.

This introduced:

- producer/consumer reasoning
- head/tail indices
- wrap-around
- capacity semantics
- overflow behavior

An overflow test intentionally stopped the reader and allowed the producer to fill the buffer.

Debugfs then showed:

```text
buffered_samples: 7
dropped_samples: >0
```

This proved that the overflow policy was actually being exercised.

## 4.15 Debugfs

Debugging became easier after exposing:

```text
counter
interval_ms
buffer_head
buffer_tail
buffered_samples
dropped_samples
```

through:

```text
/sys/kernel/debug/vsensor/stats
```

This was preferable to relying exclusively on `dmesg` because it exposed current state rather than only historical event messages.

## 4.16 Virtual memory exploration

A small userspace program was created to print addresses of:

```text
global variable
stack variable
heap allocation
```

and `/proc/<pid>/maps` was inspected.

This connected the driver work to:

- virtual addresses
- page tables
- MMU
- page faults
- user/kernel memory boundaries

## 4.17 Scope reduction

At this point the project already demonstrated the core concepts required for the learning objective.

Additional work such as extensive robustness testing, lock-free design, advanced tracing, real hardware IRQ registration, and production-grade error recovery was deliberately not added.

That was a time-management decision, not an indication that those topics are unimportant.

## 4.18 General debugging workflow learned

The most reusable workflow was:

```text
observe symptom
    |
identify subsystem/path
    |
instrument boundary
    |
reproduce
    |
narrow the failure
    |
form hypothesis
    |
change one thing
    |
rebuild/reload
    |
reproduce
```

This is more important than memorizing individual commands.
