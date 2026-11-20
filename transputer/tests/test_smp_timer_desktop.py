#!/usr/bin/env python3
"""Run under xvfb-run; requires xdotool and a prepared protected integration initramfs.

The init program must include TP_TEST_SMP_CPUINFO, libc/event-time tests,
two-context CoreMark and fbwm. Pixel captures pause all emulator threads;
counter timings are diagnostic measurements, not hardware latency claims.
"""
import os
import re
import signal
import struct
import shutil
import subprocess
import tempfile
import sys
import time
from pathlib import Path
from collections import Counter

def capture_spine(child, elf_path, destination):
    image = elf_path.read_bytes()
    phoff = struct.unpack_from("<I", image, 28)[0]
    stride, count = struct.unpack_from("<HH", image, 42)
    segments = [struct.unpack_from("<8I", image, phoff + n * stride) for n in range(count)]
    load = next(ph for ph in segments if ph[0] == 1 and ph[3] == 0x80000070)
    signatures = []
    for offset in range(0x10000, 0x20000, 0x101):
        signature = image[load[1] + offset:load[1] + offset + 32]
        if image.count(signature) == 1:
            signatures.append((offset + load[3] - 0x80000000, signature))
            if len(signatures) == 3:
                break
    assert len(signatures) == 3
    candidates = []
    os.kill(child.pid, signal.SIGSTOP)
    try:
        with open(f"/proc/{child.pid}/mem", "rb", buffering=0) as memory:
            for line in Path(f"/proc/{child.pid}/maps").read_text().splitlines():
                bounds, permissions, *_ = line.split()
                low, high = (int(value, 16) for value in bounds.split("-"))
                if permissions != "rw-p" or not 32 * 1024 * 1024 <= high - low <= 256 * 1024 * 1024:
                    continue
                try:
                    memory.seek(low)
                    chunk = memory.read(high - low)
                except OSError:
                    continue
                offset, signature = signatures[0]
                at = chunk.find(signature)
                if at < 0:
                    continue
                base = at - offset
                if base < 0 or any(chunk[base + off:base + off + len(sig)] != sig
                                   for off, sig in signatures[1:]):
                    continue
                ram = low + base
                if ram < low or ram + 32 * 1024 * 1024 > high:
                    continue
                memory.seek(ram + 0x1000000)
                pixels = memory.read(800 * 600 * 4)
                assert len(pixels) == 800 * 600 * 4
                candidates.append(pixels)
        assert len(candidates) == 1, f"expected one emulator RAM mapping, found {len(candidates)}"
        raw = candidates[0]
        (destination / "spine.xrgb").write_bytes(raw)
        # A top-down 32-bit BMP preserves the native B,G,R,X memory bytes.
        header = struct.pack("<2sIHHI", b"BM", 54 + len(raw), 0, 0, 54)
        dib = struct.pack("<IiiHHIIiiII", 40, 800, -600, 1, 32, 0, len(raw), 0, 0, 0, 0)
        (destination / "spine.bmp").write_bytes(header + dib + raw)
    finally:
        os.kill(child.pid, signal.SIGCONT)


def capture_stats(child, elf_path, destination):
    image = elf_path.read_bytes()
    phoff = struct.unpack_from("<I", image, 28)[0]
    stride, count = struct.unpack_from("<HH", image, 42)
    segments = [struct.unpack_from("<8I", image, phoff + n * stride) for n in range(count)]
    load = next(ph for ph in segments if ph[0] == 1 and ph[3] == 0x80000070)
    signatures = []
    for offset in range(0x10000, 0x20000, 0x101):
        signature = image[load[1] + offset:load[1] + offset + 32]
        if image.count(signature) == 1:
            signatures.append((offset + load[3] - 0x80000000, signature))
            if len(signatures) == 3:
                break
    assert len(signatures) == 3
    candidates = []
    os.kill(child.pid, signal.SIGSTOP)
    try:
        with open(f"/proc/{child.pid}/mem", "rb", buffering=0) as memory:
            for line in Path(f"/proc/{child.pid}/maps").read_text().splitlines():
                bounds, permissions, *_ = line.split()
                low, high = (int(value, 16) for value in bounds.split("-"))
                if permissions != "rw-p" or not 32 * 1024 * 1024 <= high - low <= 256 * 1024 * 1024:
                    continue
                try:
                    memory.seek(low)
                    chunk = memory.read(high - low)
                except OSError:
                    continue
                offset, signature = signatures[0]
                at = chunk.find(signature)
                if at < 0:
                    continue
                base = at - offset
                if base < 0 or any(chunk[base + off:base + off + len(sig)] != sig
                                   for off, sig in signatures[1:]):
                    continue
                ram = low + base
                if ram < low or ram + 32 * 1024 * 1024 > high:
                    continue
                memory.seek(ram + stats_addr - 0x80000000)
                pixels = memory.read(6 * 7 * 4)
                assert len(pixels) == 6 * 7 * 4
                candidates.append(pixels)
        assert len(candidates) == 1, f"expected one emulator RAM mapping, found {len(candidates)}"
        return [dict(zip(('polls','events','max_late_us','stopped_polls','handler_us','idle_waits','idle_us'), struct.unpack_from('<7I', candidates[0], cpu*28))) for cpu in range(6)]
    finally:
        os.kill(child.pid, signal.SIGCONT)




import json
from hashlib import sha256

import argparse

parser = argparse.ArgumentParser(description="Validate six-CPU T4 timer/libc/CoreMark/desktop integration")
parser.add_argument("--t4", type=Path, required=True)
parser.add_argument("--kernel", type=Path, required=True, help="Unstripped vmlinux used for framebuffer location")
parser.add_argument("--btl", type=Path, required=True)
parser.add_argument("--map", type=Path, required=True, help="System.map for tp_tick_stats")
parser.add_argument("--net", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--timeout", type=int, default=1800)
parser.add_argument("--userspace-smp", action="store_true",
                    help="Require migration and pinned two/six-context CoreMark timer tests")
parser.add_argument("--input-ownership", action="store_true",
                    help="Exercise evdev input before returning the UART to raw desktop applications")
args = parser.parse_args()
for name in ("t4", "kernel", "btl", "map", "net"):
    setattr(args, name, getattr(args, name).resolve(strict=True))
saved = args.output.resolve()
saved.mkdir(parents=True, exist_ok=True)
stats_addr = int(next(line.split()[0] for line in args.map.read_text().splitlines()
                      if line.endswith(" tp_tick_stats")), 16)
run_started = time.monotonic()
last_sample = run_started
out = Path(tempfile.mkdtemp(prefix="smp-timer-"))
(saved / "live-path.txt").write_text(str(out))
uart = out / "uart.log"
env = dict(os.environ, T4_HEADLESS="0", T4_PRESENT_ON_FLIP="0",
           T4_PROTECT="1", T4_PROTECT_DEBUG="0", T4_UART_CONSOLE="0",
           T4_UART_LOG=str(uart), SDL_VIDEODRIVER="x11")
env.pop("T4_NODE_BTL", None)
env.update(T4_DATA_LINKS="6", T4_SHARED_KERNEL="1",
           T4_SHARED_BOARD="0:5,1:5,2:5,3:5,4:5,5:5", SPYNET=str(args.net))
t4 = args.t4

def uart_text():
    return uart.read_text(encoding="latin1") if uart.exists() else ""

def wait_for(marker, maximum=15, since=0):
    global last_sample
    deadline = time.monotonic() + maximum
    while time.monotonic() < deadline:
        if time.monotonic()-last_sample >= 30:
            print(json.dumps({'seconds':round(time.monotonic()-run_started,2),'cpu':capture_stats(child,args.kernel,saved)}),flush=True)
            last_sample=time.monotonic()
        text = uart_text()
        if marker in text[since:]:
            return text
        failures = ("Kernel panic", "TPKG_UI_FAILED", "PTHREAD_LIBC_TEST: FAIL",
                    "LIBC_THREADS_TEST: FAIL", "EVENT_TIME_TEST: FAIL",
                    "COREMARK SMP FAIL", "INPUT_EVDEV_FAIL", "Transputer: no tick",
                    "P-session end cause=")
        if child.poll() is not None or any(marker in text for marker in failures):
            break
        time.sleep(0.015)
    raise RuntimeError(f"emulator did not reach {marker!r}, exit {child.poll()}")

def frame(name):
    destination = saved / name
    destination.mkdir(exist_ok=True)
    capture_spine(child, args.kernel, destination)
    raw = (destination / "spine.xrgb").read_bytes()
    print(name, sha256(raw).hexdigest(), flush=True)
    return raw

def key_until(window, value, marker, since):
    for _ in range(5):
        if marker in uart_text()[since:]:
            return
        subprocess.run(["xdotool", "windowfocus", "--sync", window,
                        "keydown", value], check=True, timeout=4)
        time.sleep(.15)
        subprocess.run(["xdotool", "keyup", value], check=True, timeout=4)
        time.sleep(.4)
    wait_for(marker, 120, since=since)

def files_frame(maximum=120):
    # TFILES_START precedes directory enumeration and the first redraw.
    # Require the large Files title/pane regions and a stable completed frame
    # before sending input; a changed desktop alone can be a partial clear.
    destination = saved / "files-open"
    destination.mkdir(exist_ok=True)
    previous = None
    deadline = time.monotonic() + maximum
    while time.monotonic() < deadline:
        capture_spine(child, args.kernel, destination)
        raw = (destination / "spine.xrgb").read_bytes()
        colors = Counter(p[0] & 0xffffff for p in struct.iter_unpack("<I", raw))
        ready = colors[0x00897b] > 5000 and colors[0x1a2332] > 200000
        if ready and raw == previous:
            print("files-open", sha256(raw).hexdigest(), flush=True)
            return raw
        previous = raw if ready else None
        time.sleep(2)
    raise RuntimeError("Files did not finish rendering its window")

def mask_clock(raw):
    result = bytearray(raw)
    for y in range(568, 600):
        start = (y * 800 + 720) * 4
        result[start:start + 72 * 4] = bytes(72 * 4)
    return result

with (out / "emulator.log").open("wb") as log:
    child = subprocess.Popen([t4, "-s9", "-st", "-sn", "-6", "-sm", "25", "-sb",
                              args.btl], env=env, cwd=out,
                             stdout=log, stderr=log)
    try:
        if args.input_ownership:
            wait_for("INPUT_EVDEV_WAIT", args.timeout)
            window = subprocess.check_output(
                ["xdotool", "search", "--onlyvisible", "--name", "Transputer T4"],
                text=True, timeout=4).splitlines()[0]
            key_until(window, "a", "INPUT_EVDEV_PASS", 0)
        text = wait_for("DESKTOP_LOOP", args.timeout)
        assert "Transputer timer deadline selftest: PASS" in text
        assert "Transputer SMP atomic/futex selftest: PASS" in text
        assert "Transputer SMP: 6 Linux CPU online" in text
        assert "Transputer SMP worker start barrier: 6 CPUs ready" in text
        assert set(re.findall(r"^processor\s*:\s*(\d+)", text, re.M)) == set(map(str, range(6)))
        assert "Transputer SMP cross-CPU callback selftest: PASS" in text
        assert "Transputer SMP worker/allocator selftest: PASS cpus=6 allocations=768" in text
        print("Six Linux CPUs, cross-CPU callbacks and shared allocator PASS", flush=True)
        window = subprocess.check_output(["xdotool","search","--onlyvisible","--name","Transputer T4"],
                                         text=True, timeout=4).splitlines()[0]
        subprocess.run(["xdotool","windowfocus","--sync",window],check=True,timeout=4)
        time.sleep(0.12)
        assert "PTHREAD_LIBC_TEST: PASS" in text
        assert "LIBC_THREADS_TEST: PASS" in text
        assert "EPOLL_TEST: PASS" in text
        assert "POSIX_TIMER_TEST: PASS" in text
        assert "SIGNAL_HANDLER_TEST: PASS" in text
        assert "SIGNAL_TIMESLICE_TEST: PASS" in text
        assert "SIGNAL_STATE_TEST: PASS" in text
        assert "SIGNAL_THREAD_0: PASS" in text
        assert "SIGNAL_THREAD_1: PASS" in text
        assert "SIGNAL_THREADS_TEST: PASS" in text
        assert "SIGNAL_NESTED_THREADS_TEST: PASS" in text
        assert "Correct operation validated" in text
        assert "Parallel PThreads" in text
        if args.userspace_smp:
            assert "USERSPACE_MIGRATION: PASS cpus=0,1,2,3,4,5 rounds=2 errno=isolated" in text
            for contexts in (2, 6):
                assert f"COREMARK SMP PASS contexts={contexts}" in text
            assert text.count("Correct operation validated") == 2
        desktop = frame("desktop-before")
        offset=len(uart_text())
        key_until(window, "f", "TFILES_START", offset)
        files=files_frame()
        assert mask_clock(files) != mask_clock(desktop)
        offset=len(uart_text())
        key_until(window, "q", "FILES_DESKTOP", offset)
        time.sleep(.3)
        restored=frame("desktop-restored")
        assert mask_clock(restored)==mask_clock(desktop)
        offset=len(uart_text())
        key_until(window, "Escape", "processor\t: 5", offset)
        print("Thread-targeted FP/MOVE2D timer state, " +
              ("two- and six-worker" if args.userspace_smp else "two-worker") +
              " CoreMark and desktop checks passed.", flush=True)
    finally:
        if child.poll() is None:
            child.terminate()
        try:
            child.wait(5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
        for name in ("emulator.log", "uart.log"):
            if (out / name).exists():
                shutil.copyfile(out / name, saved / name)
        print(uart_text()[-4500:])
