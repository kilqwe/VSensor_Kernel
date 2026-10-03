# 3. I/O, Concurrency and Memory

## 3.1 read()

The driver implements:

```c
static ssize_t vsensor_read(struct file *file,
                            char __user *buffer,
                            size_t count,
                            loff_t *ppos)
```

Important point:

```c
char __user *buffer
```

explicitly indicates that the pointer refers to userspace memory.

## 3.2 copy_to_user / copy_from_user

Kernel memory and userspace memory are different protection domains.

The driver uses:

```c
copy_to_user()
```

to send data to userspace and:

```c
copy_from_user()
```

to receive data from userspace.

The application:

```c
read(fd, buffer, 127);
```

does not give the kernel a normal trusted kernel pointer.

If the userspace address is invalid, `copy_to_user()` can fail and the driver returns an error such as `-EFAULT`.

## 3.3 Why not memcpy()?

Blindly treating a userspace pointer like a kernel pointer is unsafe.

The user/kernel boundary is a security and correctness boundary.

For an interview:

> Userspace pointers are untrusted from the kernel's perspective, so drivers use the user-copy APIs rather than blindly dereferencing them.

## 3.4 write()

The driver initially copied user data into a temporary kernel buffer:

```c
char temp[SENSOR_DATA_SIZE];

copy_from_user(temp, buffer, count);
```

It then updated driver state.

Bounds checking is essential:

```c
if (count >= SENSOR_DATA_SIZE)
    return -EINVAL;
```

This ensures there is room for a terminating `'\0'`.

## 3.5 Mutex

The driver has shared state:

```text
ring buffer
sensor counter
sampling interval
debug statistics
```

Multiple execution paths can access this state.

The driver uses:

```c
static DEFINE_MUTEX(sensor_lock);
```

Critical code follows:

```c
mutex_lock(&sensor_lock);

/* shared state */

mutex_unlock(&sensor_lock);
```

A mutex prevents multiple threads/process contexts from entering the protected critical section simultaneously.

## 3.6 Why a mutex?

The important context is that the relevant driver paths execute in sleepable contexts.

A mutex is appropriate when sleeping is allowed.

A mutex is not appropriate in interrupt/atomic context.

The project therefore uses:

```text
timer callback
    -> schedule work
    -> does not take mutex

workqueue
    -> takes mutex

read()
    -> takes mutex

ioctl()
    -> takes mutex
```

## 3.7 Deliberate race experiment

A separate experimental module, `vsensor_mutex.c`, intentionally removed synchronization.

The write path was made artificially slow:

```c
memset(sensor_data, 'X', SENSOR_DATA_SIZE / 2);

msleep(100);

memset(sensor_data + SENSOR_DATA_SIZE / 2,
       'Y',
       SENSOR_DATA_SIZE / 2 - 1);
```

At the same time, another process repeatedly read the device.

This produced observations where readers could see partially updated shared state.

The experiment demonstrated:

```text
writer:
XXXXXX...
       |
       | 100 ms
       |
       v
XXXXXXXXYYYY...

reader can run during the gap
```

The point was not to create a production feature. It was to make the race condition visible and understand why synchronization is required.

## 3.8 Critical-section design

An early version held the mutex while calling `copy_to_user()`.

The design was improved to:

```text
lock
  inspect ring buffer
  copy sample -> local kernel buffer
  advance tail
unlock

copy_to_user()
```

Why?

Because the mutex should protect shared state for the shortest practical duration.

Also, user-copy operations may sleep, so avoiding them inside a shared-state critical section is a cleaner design.

There is a deliberate tradeoff: the sample is considered consumed before `copy_to_user()` completes. If user-copy fails, that sample is lost. This is acceptable for this learning project.

## 3.9 Blocking reads

A reader should not busy-loop waiting for a sensor reading.

The driver uses a wait queue:

```c
static DECLARE_WAIT_QUEUE_HEAD(sensor_wait_queue);
```

The read path waits:

```c
wait_event_interruptible(sensor_wait_queue,
                         !sensor_buffer_empty());
```

If there is no data:

```text
read()
  |
  v
wait
  |
  | process sleeps
  |
  v
worker generates sample
  |
  v
wake_up_interruptible()
  |
  v
reader wakes
```

This is much more efficient than repeatedly polling the driver.

## 3.10 poll()

The driver implements:

```c
static __poll_t vsensor_poll(struct file *file,
                             poll_table *wait)
```

It calls:

```c
poll_wait(file, &sensor_wait_queue, wait);
```

and reports readable state when the ring buffer is non-empty:

```c
if (!sensor_buffer_empty())
    mask |= POLLIN | POLLRDNORM;
```

`poll()` asks:

> Can I perform the requested I/O without blocking?

It does not itself return the sensor data.

## 3.11 epoll()

Userspace can scale readiness monitoring with:

```text
epoll_create1()
epoll_ctl()
epoll_wait()
```

The driver does not need a separate "epoll callback".

The kernel's epoll implementation uses the driver's `.poll` interface.

This gives:

```text
application
   |
epoll_wait()
   |
kernel epoll
   |
driver .poll
   |
wait queue
```

## 3.12 Ring buffer

The driver moved from a single shared sensor string to:

```c
static char sensor_buffer[SENSOR_BUFFER_SIZE][SENSOR_DATA_SIZE];

static unsigned int buffer_head;
static unsigned int buffer_tail;
```

Conceptually:

```text
+----+----+----+----+----+----+----+----+
|    |    |    |    |    |    |    |    |
+----+----+----+----+----+----+----+----+
  ^                        ^
 tail                     head
```

The producer writes at `head`.

The consumer reads at `tail`.

Indices wrap using:

```c
(head + 1) % SENSOR_BUFFER_SIZE
```

## 3.13 Why ring buffer?

A single variable is insufficient if the producer generates data faster than the consumer reads it.

A ring buffer allows multiple samples to wait.

It also naturally models producer/consumer behavior:

```text
timer/workqueue
     |
     v
 producer
     |
     v
 ring buffer
     |
     v
 consumer
     |
     v
userspace
```

## 3.14 Why does size 8 give only 7 usable entries?

The implementation distinguishes full and empty using:

```c
(head + 1) % SIZE == tail
```

If head equals tail, the buffer is empty.

To avoid the full state looking identical to the empty state, one slot is intentionally left unused.

Therefore:

```text
allocated slots = 8
usable samples  = 7
```

Alternative designs could maintain an explicit count, but reserving one slot is simple.

## 3.15 Overflow policy

When the ring buffer is full, the current driver drops the newly generated sample:

```text
buffer full
    |
    v
drop newest
```

It also increments:

```c
dropped_samples
```

This is a deliberate simple policy.

Other real systems might overwrite the oldest sample, block the producer, or apply backpressure depending on requirements.

## 3.16 Virtual memory

A process sees virtual addresses, not raw physical addresses.

Conceptually:

```text
virtual address
      |
      v
     MMU
      |
  page tables
      |
      v
physical address
```

Memory is divided into pages, commonly 4 KiB.

A process has its own virtual address space, which provides isolation.

## 3.17 Page faults

A page fault is not automatically an error.

A missing page mapping can cause a page fault, after which the kernel may establish the required mapping and retry the access.

A fault can also be invalid and result in a process error.

The key distinction:

```text
page fault
   |
   +-- valid memory-management event
   |
   +-- invalid access -> error/crash
```

## 3.18 Why virtual memory matters to this driver

It explains why the driver must treat the `read()` destination as a userspace pointer.

It also explains why user-copy operations may need a sleepable execution context: accessing user memory can interact with virtual-memory mechanisms.

Useful inspection commands:

```bash
cat /proc/$$/maps
cat /proc/self/maps
```

These show the process's mapped virtual-memory regions.
