"""
perf.py — Lightweight profiler for MicroPython on ESP32-S3.

Frozen into WarpPig firmware. No cProfile/py-spy on MCUs, so we roll our own.

Usage:
    import perf

    # Time a function
    @perf.timed
    def my_func():
        ...

    # Profile a block
    with perf.Profile("my_operation"):
        ...

    # Benchmark comparison
    perf.bench("list_comp", lambda: [i*i for i in range(1000)])
    perf.bench("ulab", lambda: np.array(range(1000))**2)
    perf.report()

    # Memory snapshot
    perf.memsnap("before")
    ...do work...
    perf.memsnap("after")
    perf.memdiff()

    # GC tuning
    perf.gc_tune(threshold=50000)  # raise GC threshold for throughput
"""

import time
import gc

_benchmarks = {}
_memsnaps = {}


class Profile:
    """Context manager for timing code blocks."""
    __slots__ = ('name', 't0', 'elapsed_us')

    def __init__(self, name="block"):
        self.name = name
        self.elapsed_us = 0

    def __enter__(self):
        self.t0 = time.ticks_us()
        return self

    def __exit__(self, *_):
        self.elapsed_us = time.ticks_diff(time.ticks_us(), self.t0)
        ms = self.elapsed_us / 1000
        print(f"[perf] {self.name}: {ms:.2f}ms")


def timed(fn):
    """Decorator to print execution time of a function."""
    def wrapper(*args, **kwargs):
        t0 = time.ticks_us()
        result = fn(*args, **kwargs)
        dt = time.ticks_diff(time.ticks_us(), t0)
        print(f"[perf] {fn.__name__}: {dt/1000:.2f}ms")
        return result
    return wrapper


def bench(name, fn, n=100):
    """Run fn() n times, store result for report()."""
    gc.collect()
    t0 = time.ticks_us()
    for _ in range(n):
        fn()
    dt = time.ticks_diff(time.ticks_us(), t0)
    per_call = dt / n
    _benchmarks[name] = {"total_us": dt, "n": n, "per_call_us": per_call}
    print(f"[bench] {name}: {per_call:.1f}us/call ({dt/1000:.1f}ms total, {n} runs)")


def report():
    """Print all benchmark results sorted by speed."""
    if not _benchmarks:
        print("[bench] No benchmarks recorded")
        return
    print(f"\n{'Name':<30} {'per call':>12} {'total':>12} {'runs':>6}")
    print("-" * 62)
    for name, r in sorted(_benchmarks.items(), key=lambda x: x[1]["per_call_us"]):
        print(f"{name:<30} {r['per_call_us']:>10.1f}us {r['total_us']/1000:>10.1f}ms {r['n']:>6}")


def memsnap(label="snap"):
    """Take a memory snapshot."""
    gc.collect()
    _memsnaps[label] = {"free": gc.mem_free(), "alloc": gc.mem_alloc()}
    print(f"[mem] {label}: free={gc.mem_free()} alloc={gc.mem_alloc()}")


def memdiff():
    """Print differences between memory snapshots."""
    labels = list(_memsnaps.keys())
    if len(labels) < 2:
        print("[mem] Need at least 2 snapshots")
        return
    for i in range(1, len(labels)):
        a, b = _memsnaps[labels[i-1]], _memsnaps[labels[i]]
        df = b["free"] - a["free"]
        da = b["alloc"] - a["alloc"]
        print(f"[mem] {labels[i-1]} -> {labels[i]}: free {df:+d}, alloc {da:+d}")


def gc_tune(threshold=50000):
    """Raise GC threshold for throughput (default MicroPython is very aggressive)."""
    gc.threshold(threshold)
    print(f"[gc] threshold set to {threshold}")


def native_check():
    """Test if @micropython.native works on this firmware."""
    try:
        @micropython.native
        def _test():
            return 42
        r = _test()
        print(f"[perf] @native: OK (result={r})")
        return True
    except Exception as e:
        print(f"[perf] @native: FAIL ({e})")
        return False


def viper_check():
    """Test if @micropython.viper works on this firmware."""
    try:
        @micropython.viper
        def _test() -> int:
            x: int = 42
            return x
        r = _test()
        print(f"[perf] @viper: OK (result={r})")
        return True
    except Exception as e:
        print(f"[perf] @viper: FAIL ({e})")
        return False
