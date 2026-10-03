# 5. Debugging, Observability and Design Decisions

## 5.1 Debugging vs observability

They are related but not identical.

### Observability

Seeing what the system is doing.

Examples:

```text
dmesg
debugfs
/proc
tracepoints
```

### Debugging

Using observations to determine why behavior is wrong.

A useful workflow is:

```text
symptom
  ↓
observe
  ↓
identify path
  ↓
instrument
  ↓
reproduce
  ↓
hypothesis
  ↓
change
  ↓
reproduce
```

## 5.2 Kernel logs

The driver uses:

```c
pr_info()
pr_err()
```

Logs are useful for:

- initialization
- unexpected conditions
- generated samples
- ioctl changes
- cleanup

But logging too aggressively can change scheduling/timing and hide concurrency bugs.

This is sometimes described as a Heisenbug-like effect: observing a timing-sensitive system can change its timing.

## 5.3 debugfs

Debugfs is intended for developer/debug-oriented kernel state.

The project exposes:

```text
/sys/kernel/debug/vsensor/stats
```

with:

```text
counter
interval_ms
buffer_head
buffer_tail
buffered_samples
dropped_samples
```

The driver uses `seq_file`/`single_open` to expose the text.

The important distinction:

- `debugfs` — developer/internal debugging interface
- `sysfs` — structured kernel object/device state intended to be a more stable interface
- `/proc` — process/system information and kernel-provided status

## 5.4 Why not just print everything with dmesg?

Logs are historical.

Debugfs exposes current state.

For example:

```text
dmesg
```

can tell you that a sample was dropped at some point.

Debugfs can tell you:

```text
dropped_samples: 37
buffered_samples: 7
```

right now.

## 5.5 Design decision: character device

Why not use a normal file or another interface?

Because the sensor is naturally modeled as a stream of data with operations such as:

```text
read
write/control
poll
ioctl
```

A character device fits this abstraction well.

## 5.6 Design decision: timer

Why?

The sensor needs periodic generation.

A timer provides a simple software mechanism to trigger periodic activity.

It also allowed learning timer APIs without requiring physical hardware.

## 5.7 Design decision: workqueue

Why not update sensor state entirely in the timer callback?

The timer callback should stay lightweight.

The workqueue provides process context where the driver can perform work that may sleep, including taking a mutex.

## 5.8 Design decision: mutex

The ring buffer and statistics are shared state.

A mutex provides mutual exclusion in sleepable contexts.

The project intentionally did not attempt a lock-free ring buffer because that would add substantial memory-ordering complexity without helping the main learning objective.

## 5.9 Design decision: wait queue

A sensor reader should sleep when no sample exists.

A wait queue avoids:

```text
while (no_data)
    keep asking;
```

and instead allows:

```text
sleep
  |
  v
wake when data exists
```

This reduces wasted CPU time.

## 5.10 Design decision: poll/epoll

Blocking `read()` is useful when the application only needs one stream.

`poll()` and `epoll()` allow readiness-driven event loops.

This matters when an application has multiple descriptors or other work to handle.

## 5.11 Design decision: ring buffer

A ring buffer decouples producer and consumer rates.

If the producer generates:

```text
sample 1
sample 2
sample 3
...
```

while the consumer is temporarily slower, samples can wait in the buffer.

The fixed-size buffer also gives predictable memory usage.

## 5.12 Design decision: drop-newest overflow

When full, the driver drops a newly generated sample.

Why?

It is simple and explicit.

But this is not universally correct.

For another application, preserving the newest value might matter more than preserving every historical value, in which case overwriting the oldest sample could be preferable.

For a safety-critical system, blocking or backpressure might be required.

The correct policy depends on requirements.

## 5.13 Design decision: ioctl

`read()` and `write()` naturally represent data transfer.

Changing the sampling interval is control/configuration rather than sensor data.

Therefore:

```text
ioctl(SET_INTERVAL)
ioctl(GET_INTERVAL)
```

is a reasonable interface.

## 5.14 Design decision: shared ioctl header

Both driver and userspace include:

```text
include/vsensor_ioctl.h
```

This ensures both sides agree on command definitions.

## 5.15 Error-path thinking

Important initialization sequence:

```text
alloc device number
    |
    +-- failure -> return
    |
cdev_add
    |
    +-- failure -> unregister device number
    |
class_create
    |
    +-- failure -> cdev_del + unregister
    |
device_create
    |
    +-- failure -> destroy class + cdev_del + unregister
    |
timer/work initialization
    |
device ready
```

The general lesson:

> Every successful resource acquisition creates a cleanup responsibility.

## 5.16 Asynchronous lifetime

The most important cleanup issue is:

```text
timer/work may still be executing
          |
          v
driver state must remain valid
```

Therefore cleanup first synchronizes asynchronous execution:

```c
timer_shutdown_sync(&sensor_timer);
cancel_work_sync(&sensor_work);
```

and only then destroys the device and driver state.

## 5.17 Known limitations

This is a learning driver, not production code.

Limitations include:

- no real hardware sensor
- no real hardware IRQ
- simple fixed-size ring buffer
- one global driver state rather than sophisticated per-file/per-device state
- simple overflow policy
- limited error injection
- no comprehensive lockdep/fuzzing/testing framework
- some state accesses could be made more rigorous with stronger memory-ordering practices
- user-copy failure semantics are intentionally simple
- no persistence
- no hardware-specific register programming

These are reasonable limitations for the project's scope.

## 5.18 What could be added in a production-oriented version?

Potential future work:

- real hardware backend
- interrupt handler
- DMA
- device-tree integration
- sysfs configuration where appropriate
- per-device/per-open state
- more formal memory-ordering design
- richer error injection
- lockdep/KASAN/KCSAN testing
- tracepoints/ftrace
- automated userspace tests
- hotplug support
- power-management support
- actual embedded/RTOS comparison
