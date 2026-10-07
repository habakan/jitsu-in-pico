# /// script
# dependencies = ["pyserial"]
# ///
"""Debug Probe の UART を受けて表示し、build/monitor.log にも残す。
使い方: uv run tools/device/monitor.py [秒数]（省略すると Ctrl-C まで）。ポートは自動で選ぶ。"""
import sys, time
import serial
from serial.tools import list_ports

BAUD = 115200
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 0
ports = [p for p in list_ports.comports() if "usbmodem" in p.device or "usbserial" in p.device]
probe = [p for p in ports if "Debug Probe" in (p.description or "") or (p.vid, p.pid) == (0x2E8A, 0x000C)]
if not ports:
    sys.exit("シリアルポートが見つかりません。Debug Probe を USB でつないでください")
port = (probe or ports)[0].device
print(f"# {port} {BAUD}bps ({(probe or ports)[0].description})", flush=True)

deadline = time.time() + seconds if seconds else None
with serial.Serial(port, BAUD, timeout=0.5) as ser, open("build/monitor.log", "ab") as log:
    while deadline is None or time.time() < deadline:
        data = ser.read(4096)
        if not data:
            continue
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
        log.write(data)
