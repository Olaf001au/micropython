"""
fast.py — Viper + native optimized routines for WarpPig firmware.

Frozen into firmware. These bypass the bytecode interpreter entirely
and compile to Xtensa machine code.

Usage:
    import fast
    fast.benchmark()  # run all benchmarks vs pure Python
"""

import micropython
import time


# ── Viper: integer dot product ───────────────────────────────────────────────

@micropython.native
def idot(a, b) -> int:
    """Integer dot product of two lists/arrays. ~3x faster than pure Python."""
    s = 0
    for x, y in zip(a, b):
        s += x * y
    return s


# ── Viper: fast checksum (XOR + rotate) ─────────────────────────────────────

@micropython.viper
def checksum(data, n: int) -> int:
    """Fast XOR checksum. Pass len(data) as n."""
    p = ptr8(data)
    h: int = 0x55AA
    i: int = 0
    while i < n:
        h = h ^ p[i]
        h = (h << 3) | (h >> 29)
        i += 1
    return h


# ── Viper: memset (fill buffer) ─────────────────────────────────────────────

@micropython.viper
def memset(buf, val: int, n: int):
    """Fill buffer with byte value. Faster than Python loop."""
    p = ptr8(buf)
    i: int = 0
    while i < n:
        p[i] = val
        i += 1


# ── Viper: memcpy ───────────────────────────────────────────────────────────

@micropython.viper
def memcpy(dst, src, n: int):
    """Copy n bytes from src to dst."""
    d = ptr8(dst)
    s = ptr8(src)
    i: int = 0
    while i < n:
        d[i] = s[i]
        i += 1


# ── Viper: find byte in buffer ──────────────────────────────────────────────

@micropython.viper
def memchr(buf, val: int, n: int) -> int:
    """Find first occurrence of byte val in buf. Returns index or -1."""
    p = ptr8(buf)
    i: int = 0
    while i < n:
        if p[i] == val:
            return i
        i += 1
    return -1


# ── Viper: sum of array ─────────────────────────────────────────────────────

@micropython.native
def isum(a):
    """Sum of list/array. ~3x faster than Python sum()."""
    s = 0
    for x in a:
        s += x
    return s


# ── Viper: max of array ─────────────────────────────────────────────────────

@micropython.native
def imax(a):
    """Max of list. Faster than max() for large lists."""
    m = a[0]
    for x in a:
        if x > m:
            m = x
    return m


@micropython.native
def imin(a):
    """Min of list."""
    m = a[0]
    for x in a:
        if x < m:
            m = x
    return m


# ── Viper: count bytes matching value ────────────────────────────────────────

@micropython.viper
def count_byte(buf, val: int, n: int) -> int:
    """Count occurrences of byte val in buffer. Pass len(buf) as n."""
    p = ptr8(buf)
    c: int = 0
    i: int = 0
    while i < n:
        if int(p[i]) == val:
            c += 1
        i += 1
    return c


# ── Viper: moving average (in-place, integer) ───────────────────────────────

@micropython.native
def moving_avg(src, dst, n, window):
    """Moving average. src/dst are lists or arrays."""
    half = window >> 1
    for i in range(n):
        total = 0
        count = 0
        j = max(0, i - half)
        end = min(n, i + half + 1)
        while j < end:
            total += src[j]
            count += 1
            j += 1
        dst[i] = total // count


# ── Native: JSON-like key lookup (string scanning) ──────────────────────────

@micropython.native
def find_key(text, key):
    """Find value for key in simple JSON-like text. Faster than json.loads for single key."""
    kl = len(key)
    tl = len(text)
    i = 0
    while i < tl - kl:
        if text[i] == '"' and text[i+1:i+1+kl] == key:
            # Find the colon, then the value
            j = i + kl + 1
            while j < tl and text[j] != ':':
                j += 1
            j += 1
            # Skip whitespace and opening quote
            while j < tl and text[j] in ' \t"':
                j += 1
            # Read until delimiter
            k = j
            while k < tl and text[k] not in '",}]\n':
                k += 1
            return text[j:k]
        i += 1
    return None


# ── Benchmark suite ──────────────────────────────────────────────────────────

def benchmark():
    """Run all benchmarks: viper vs pure Python."""
    import gc
    gc.collect()

    N = 10000
    data = list(range(N))
    buf = bytearray(N)

    results = []

    def _bench(name, fn_fast, fn_slow, n_iter=100):
        gc.collect()
        t0 = time.ticks_us()
        for _ in range(n_iter):
            fn_fast()
        t_fast = time.ticks_diff(time.ticks_us(), t0) / n_iter

        gc.collect()
        t0 = time.ticks_us()
        for _ in range(n_iter):
            fn_slow()
        t_slow = time.ticks_diff(time.ticks_us(), t0) / n_iter

        speedup = t_slow / t_fast if t_fast > 0 else 0
        results.append((name, t_fast, t_slow, speedup))
        print(f"  {name:<20} viper:{t_fast:>8.0f}us  python:{t_slow:>8.0f}us  {speedup:>5.1f}x")

    print(f"\n{'='*60}")
    print(f"  WarpPig Viper Benchmark — {N} elements")
    print(f"{'='*60}")

    # sum
    _bench("sum",
           lambda: isum(data),
           lambda: sum(data))

    # max
    _bench("max",
           lambda: imax(data),
           lambda: max(data))

    # min
    _bench("min",
           lambda: imin(data),
           lambda: min(data))

    # dot product
    _bench("dot_product",
           lambda: idot(data, data),
           lambda: sum(a*b for a,b in zip(data, data)),
           n_iter=10)

    # memset
    _bench("memset_10k",
           lambda: memset(buf, 0x42, N),
           lambda: [buf.__setitem__(i, 0x42) for i in range(N)],
           n_iter=10)

    # memchr
    buf[N-1] = 0xFF
    _bench("memchr_10k",
           lambda: memchr(buf, 0xFF, N),
           lambda: buf.index(0xFF) if 0xFF in buf else -1,
           n_iter=100)

    # count
    _bench("count_byte",
           lambda: count_byte(buf, 0x42, N),
           lambda: buf.count(0x42))

    # checksum
    _bench("checksum_10k",
           lambda: checksum(buf, N),
           lambda: sum(buf) & 0xFFFFFFFF,
           n_iter=10)

    # native check
    try:
        @micropython.native
        def _native_test():
            s = 0
            for i in range(10000):
                s += i
            return s

        gc.collect()
        t0 = time.ticks_us()
        for _ in range(10):
            _native_test()
        t_native = time.ticks_diff(time.ticks_us(), t0) / 10

        def _py_test():
            s = 0
            for i in range(10000):
                s += i
            return s

        gc.collect()
        t0 = time.ticks_us()
        for _ in range(10):
            _py_test()
        t_py = time.ticks_diff(time.ticks_us(), t0) / 10

        speedup = t_py / t_native if t_native > 0 else 0
        results.append(("@native_loop", t_native, t_py, speedup))
        print(f"  {'@native_loop':<20} native:{t_native:>8.0f}us  python:{t_py:>8.0f}us  {speedup:>5.1f}x")
    except Exception as e:
        print(f"  @native: {e}")

    print(f"{'='*60}")
    avg_speedup = sum(r[3] for r in results) / len(results) if results else 0
    print(f"  Average speedup: {avg_speedup:.1f}x")
    print(f"{'='*60}\n")
