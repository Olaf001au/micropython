"""
sensors.py — Dual-core sensor polling framework for WarpPig ESP32-S3.

Architecture:
    Core 0: WiFi + HTTP server + INDI protocol (networking)
    Core 1: Sensor polling with real preemption (I2C, SPI, ADC)

    Sensors → Queue → Network tasks
    I2C bus protected by Semaphore (multiple sensors, one bus)
    EventGroup coordinates startup ("all sensors ready")

Usage:
    import sensors

    # Register sensors
    sensors.add("bme280", sensors.I2CSensor(0x76, read_bme280, interval_ms=10000))
    sensors.add("gps",    sensors.I2CSensor(0x10, read_gps, interval_ms=1000))
    sensors.add("light",  sensors.I2CSensor(0x39, read_light, interval_ms=5000))

    # Start polling (runs on core 1, data flows to queue)
    sensors.start()

    # Consume from network task (core 0)
    while True:
        name, data, ts = sensors.get(timeout_ms=1000)
        if data:
            publish_indi(name, data)

    # Or use callback mode
    sensors.on_data(lambda name, data, ts: print(name, data))
    sensors.start()
"""

import _thread
import time
import micropython
import warpig_rtos as rtos

# ── Globals ──────────────────────────────────────────────────────────────────

_sensors = {}           # name -> SensorDef
_data_queue = None      # rtos.Queue for sensor→network
_i2c_lock = None        # rtos.Semaphore for I2C bus
_events = None          # rtos.EventGroup for coordination
_running = False
_callback = None

# Event bits
EVT_SENSORS_READY = 0x01
EVT_WIFI_READY    = 0x02
EVT_STOP          = 0x80

# ── Sensor definition ────────────────────────────────────────────────────────

class SensorDef:
    __slots__ = ('addr', 'read_fn', 'interval_ms', 'errors', 'last_data',
                 'last_ts', 'active', 'name', 'init_fn')

    def __init__(self, addr, read_fn, interval_ms=5000, init_fn=None):
        self.addr = addr
        self.read_fn = read_fn
        self.interval_ms = interval_ms
        self.init_fn = init_fn
        self.errors = 0
        self.last_data = None
        self.last_ts = 0
        self.active = True
        self.name = ""


def I2CSensor(addr, read_fn, interval_ms=5000, init_fn=None):
    """Create an I2C sensor definition."""
    return SensorDef(addr, read_fn, interval_ms, init_fn)


# ── Registration ─────────────────────────────────────────────────────────────

def add(name, sensor_def):
    """Register a sensor for polling."""
    sensor_def.name = name
    _sensors[name] = sensor_def


def on_data(callback):
    """Set callback for sensor data: callback(name, data, timestamp_ms)."""
    global _callback
    _callback = callback


# ── Queue access ─────────────────────────────────────────────────────────────

def get(timeout_ms=1000):
    """Get next sensor reading from queue. Returns (name, data, ts) or (None, None, 0)."""
    if _data_queue is None:
        return (None, None, 0)
    raw = _data_queue.get(timeout_ms=timeout_ms)
    if raw is None:
        return (None, None, 0)
    # Decode: first 16 bytes = name (padded), rest = data as string
    name = raw[:16].decode().strip('\x00')
    data_str = raw[16:].decode().strip('\x00')
    return (name, data_str, rtos.uptime_ms())


def pending():
    """Number of readings waiting in queue."""
    return _data_queue.available() if _data_queue else 0


# ── Core 1: Sensor polling thread ────────────────────────────────────────────

def _poll_thread(i2c):
    """Main sensor polling loop. Runs on Core 1."""
    global _running

    # Init all sensors (with I2C lock)
    for name, s in _sensors.items():
        if s.init_fn:
            _i2c_lock.acquire(timeout_ms=5000)
            try:
                s.init_fn(i2c, s.addr)
                print(f"[sensors] {name} @ 0x{s.addr:02x} init OK")
            except Exception as e:
                print(f"[sensors] {name} init failed: {e}")
                s.active = False
            finally:
                _i2c_lock.release()

    _events.set(EVT_SENSORS_READY)
    print(f"[sensors] {sum(1 for s in _sensors.values() if s.active)} sensors ready on core 1")

    # Watchdog for this thread
    rtos.wdt_init(30000)  # 30s timeout — generous for slow sensors

    # Main poll loop
    while _running:
        now = rtos.uptime_ms()

        for name, s in _sensors.items():
            if not s.active:
                continue
            if (now - s.last_ts) < s.interval_ms:
                continue

            # Read sensor with I2C lock
            if _i2c_lock.acquire(timeout_ms=100):
                try:
                    data = s.read_fn(i2c, s.addr)
                    s.last_data = data
                    s.last_ts = now
                    s.errors = 0

                    # Push to queue (name:16 + data)
                    if _data_queue and data is not None:
                        payload = name.ljust(16, '\x00') + str(data)
                        raw = payload.encode()[:64]
                        raw = raw + b'\x00' * (64 - len(raw))
                        _data_queue.put(raw, timeout_ms=10)

                    # Callback
                    if _callback:
                        try:
                            micropython.schedule(_callback, (name, data, now))
                        except RuntimeError:
                            pass  # schedule queue full

                except Exception as e:
                    s.errors += 1
                    if s.errors >= 5:
                        print(f"[sensors] {name} disabled: {e}")
                        s.active = False
                finally:
                    _i2c_lock.release()

            rtos.wdt_feed()

        # Sleep 1ms between scan cycles
        time.sleep_ms(1)

    rtos.wdt_stop()
    print("[sensors] poll thread stopped")


# ── Control ──────────────────────────────────────────────────────────────────

def start(i2c=None):
    """Start sensor polling on Core 1.

    Args:
        i2c: machine.I2C or machine.SoftI2C instance.
             If None, creates I2C(0) with board defaults.
    """
    global _data_queue, _i2c_lock, _events, _running

    if _running:
        print("[sensors] already running")
        return

    if i2c is None:
        from machine import I2C, Pin
        import board
        i2c = I2C(0, scl=Pin(board.I2C_SCL), sda=Pin(board.I2C_SDA), freq=400000)

    # Scan I2C
    addrs = i2c.scan()
    print(f"[sensors] I2C scan: {[hex(a) for a in addrs]}")

    # Disable sensors not found on bus
    for name, s in _sensors.items():
        if s.addr not in addrs:
            print(f"[sensors] {name} @ 0x{s.addr:02x} not found — disabled")
            s.active = False

    # Create RTOS primitives
    _data_queue = rtos.Queue(length=32, item_size=64)
    _i2c_lock = rtos.Semaphore()
    _events = rtos.EventGroup()

    _running = True

    # Start poll thread on Core 1
    _thread.start_new_thread(_poll_thread, (i2c,))
    print("[sensors] poll thread started on core 1")


def stop():
    """Stop sensor polling."""
    global _running
    _running = False
    if _events:
        _events.set(EVT_STOP)


def wait_ready(timeout_ms=10000):
    """Block until all sensors are initialized."""
    if _events:
        _events.wait(EVT_SENSORS_READY, timeout_ms=timeout_ms)


def status():
    """Return dict of sensor statuses."""
    return {
        name: {
            "active": s.active,
            "errors": s.errors,
            "last_ts": s.last_ts,
            "addr": hex(s.addr),
            "interval": s.interval_ms,
        }
        for name, s in _sensors.items()
    }


def last(name):
    """Get last reading for a sensor."""
    s = _sensors.get(name)
    if s and s.last_data is not None:
        return s.last_data
    return None
