# Virtual Linux Sensor Driver — Documentation

A learning-oriented Linux kernel character-device driver written in C. It simulates a periodic sensor and exposes it through `/dev/vsensor`.

## Project goal

The project was built to understand Linux systems programming and low-level software concepts through one coherent artifact:

- Linux kernel modules and lifecycle
- Character devices and `/dev`
- VFS and `file_operations`
- System calls and the userspace/kernel boundary
- `copy_to_user()` / `copy_from_user()`
- Mutexes and concurrency
- Race conditions
- Kernel timers and workqueues
- `ioctl`
- Blocking I/O and wait queues
- `poll` / `epoll`
- Ring buffers and producer/consumer design
- Debugfs and kernel observability
- Virtual-memory concepts

## Final architecture

```text
                    USERSPACE
             +---------------------+
             |    sensor_app       |
             | read / ioctl        |
             | poll / epoll        |
             +----------+----------+
                        |
                   system calls
                        |
                        v
                     VFS
                        |
                file_operations
                        |
                        v
             +----------------------+
             |   vsensor driver     |
             |                      |
             | mutex                |
             | ring buffer          |
             | wait queue           |
             | ioctl                |
             +----------+-----------+
                        ^
                        |
                 wake_up()
                        |
                 generated sample
                        |
                 +------+------+
                 |             |
               timer      workqueue
                 |             |
                 +------+------+
                        |
                 generate sample
```

## Important scope clarification

This is a **virtual sensor driver**, not a physical hardware driver.

The project uses a kernel software timer to simulate periodic sensor activity. Hardware interrupts were studied conceptually and inspected through `/proc/interrupts`, but the driver does not register a real hardware IRQ.

## Project story

The implementation evolved from a minimal kernel module into a character device, then gained userspace I/O, synchronization, timer/workqueue processing, ioctl control, blocking reads, poll/epoll support, a ring buffer, and debugfs instrumentation.

The development journey and failures are documented in `docs/04-development-journey.md`.

## Build

```bash
cd ~/vsensor/driver
make
sudo insmod vsensor.ko
ls -l /dev/vsensor
```

Build userspace programs from `~/vsensor/userspace` with `gcc`.

Unload:

```bash
sudo rmmod vsensor
```

## Documentation map

- `01-project-and-architecture.md` — project purpose, architecture, data/control flow
- `02-kernel-concepts.md` — kernel/userspace, modules, VFS, devices, interrupts
- `03-io-concurrency-memory.md` — read/write, user memory, mutexes, wait queues, poll/epoll, ring buffer, virtual memory
- `04-development-journey.md` — setup problems, bugs, experiments, decisions and lessons
- `05-debugging-and-design-decisions.md` — observability, debugfs, design reasoning, limitations
- `06-commands-and-code-reference.md` — commands, important APIs and final-code concepts
