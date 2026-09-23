# worker

A queue of deferred work: post from anywhere, run it all on the context that owns the worker.

Part of [integra-lib](https://github.com/integra-lib) — architecture-independent C++20
components shared between firmware projects. Header-only,
no exceptions, no RTTI.

## Use it

```bash
git submodule add git@github.com:integra-lib/worker.git external/integra/worker
```

```cmake
add_subdirectory(external/integra/worker)
target_link_libraries(app PRIVATE Integra::worker)
```

```cpp
#include <integra/worker.hpp>
```

Each component carries its own include directory, so this header stays unreachable
until the component is linked: a forgotten dependency is a compile error rather than
a build that happens to work.

## What it does

```cpp
integra::Worker<ZephyrMutex> worker;   // or Worker<std::mutex> where the C++ library has threads

// Any context: a callback, another task, a piece of work already running.
worker.Post([this] { OnStopped(); });

// The one context that owns the worker, typically its main loop.
worker.Update();   // runs everything posted so far, in order
```

The lock is held only while the queue is touched, never while a piece of work runs.
That is what lets work post more work — it joins the end of the same `Update()` — and
what makes the worker usable with a mutex that is not recursive, which a kernel mutex
usually is not.

The queue grows on the heap, one `std::function` per posted item. A producer that
outpaces `Update()` grows it without bound.

## The mutex is a template parameter

Anything with `lock()` and `unlock()` will do, and there is deliberately no default.
`std::mutex` exists only where the C++ library has thread support — on a host and on
ESP-IDF, but not in the Zephyr SDK or a bare-metal arm-none-eabi toolchain, where naming
it is a compile error. So the header does not name it, and does not include `<mutex>`:
the worker takes the platform's mutex, which on an RTOS is a wrapper around the
kernel's. The wrapper is also where the kernel object gets initialised, so the worker
needs no initialisation hook of its own:

```cpp
class ZephyrMutex
{
public:
    ZephyrMutex() { k_mutex_init(&m_mutex); }
    void lock() { k_mutex_lock(&m_mutex, K_FOREVER); }
    void unlock() { k_mutex_unlock(&m_mutex); }

private:
    k_mutex m_mutex{};
};

integra::Worker<ZephyrMutex> worker;
```

## Coming from a138-ble-gateway's worker

The class is `BaseWorker` from a138-ble-gateway's `lib/shared-slip`, which the ESP32
and nRF halves of the project each aliased with their own mutex. What changed:

* **`Post(const Work&& work)` copied every callable.** `std::move` of a `const&&` is
  still `const`, so the push picked the copy constructor — captures and all. `Post`
  now takes the callable by value and moves it; so does the drain, which copied the
  front of the queue before popping it.
* **The `LockGuard` template parameter is gone.** It existed because the nRF side
  locked a raw `k_mutex` through a project `LockGuard`; a `lock()`/`unlock()` wrapper
  does the same, and the nRF worker's initialiser callback — which threw from a
  constructor — goes with it.
* **`GetInstance()` is gone.** Whether there is one worker per process is the
  project's decision, not the library's; a one-line accessor in the project keeps
  every `Worker::GetInstance().Post(...)` call site as it is.
* **`IUpdateableObject` is gone.** It is a project interface; `Update()` keeps its
  name so a project that drives the worker through it can still do so.

## Versioning

Every component is released on its own, tagged `vX.Y.Z`. Pre-1.0, a minor release may
break the API, which is why dependants accept a single minor.

```bash
git -C external/integra/worker fetch --tags
git -C external/integra/worker checkout v0.2.0
git add external/integra/worker && git commit -m "build: bump worker to v0.2.0"
```

## In a consumer's CI

The component is an ordinary submodule, so the build needs it checked out. On GitLab
that means `GIT_SUBMODULE_STRATEGY: normal` (or `recursive`) on every job that builds —
not only on the ones that run unit tests.

## Develop it

```bash
git submodule update --init          # ci-shared, needed by pre-commit
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

Tests are built only when this repository is the top-level project, so a consumer
never builds them and never fetches GoogleTest.

The style configs are symlinks into the `ci-shared` submodule, and the pipeline comes
from the same place. On GitHub this repository carries a self-contained build-and-test
workflow instead: a workflow token cannot read another private repository, so neither
a shared workflow nor the submodule is reachable there. The shared setup is what
GitLab will use.
