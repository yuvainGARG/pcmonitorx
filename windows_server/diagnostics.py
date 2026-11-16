"""Read-only dependency, sensor, and Flask JSON diagnostics for PCMonitorX."""
from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path


REQUIRED = ("flask", "psutil", "waitress")
OPTIONAL = ("clr",)


def main() -> int:
    print("PCMonitorX diagnostic report")
    print(f"Python: {sys.version.split()[0]} ({sys.executable})")
    missing = []
    for name in REQUIRED:
        ok = importlib.util.find_spec(name) is not None
        print(f"DEPENDENCY {name}: {'available' if ok else 'MISSING'}")
        if not ok:
            missing.append(name)
    for name in OPTIONAL:
        print(f"OPTIONAL {name}: {'available' if importlib.util.find_spec(name) else 'not installed'}")
    if missing:
        print("Install project dependencies with windows_server\\install.ps1, then rerun.")
        return 2

    from app import create_app, SystemMonitor
    monitor = SystemMonitor()
    sample = monitor.snapshot(force=True)
    pc = sample["pc"]
    print(f"CPU usage: {pc['cpu']['usage_percent']} % (psutil)")
    print(f"CPU temperature: {pc['cpu']['temperature_c']} °C (null means unavailable)")
    gpu = pc["gpu"]
    print(f"GPU: {gpu['name'] or 'unavailable'}; temperature={gpu['temperature_c']} °C; usage={gpu['usage_percent']} %")
    print(f"GPU VRAM: {gpu['vram_used_mb']} / {gpu['vram_total_mb']} MB; fan={gpu['fan_percent']} %; clock={gpu['clock_mhz']} MHz")
    print(f"LibreHardwareMonitor: {'available' if monitor.lhm.available else 'unavailable'} ({monitor.lhm.error or 'sensor read completed'})")
    print("Source errors: " + json.dumps(sample["status"]["errors"], ensure_ascii=False))

    client = create_app(monitor, token="").test_client()
    for endpoint, required_keys in (("/api/pc", ("cpu", "gpu", "memory")),
                                    ("/api/sensors", ("sensors",)),
                                    ("/api/status", ("components", "server"))):
        response = client.get(endpoint)
        try:
            body = response.get_json()
        except Exception:
            body = None
        valid = response.status_code == 200 and isinstance(body, dict) and all(key in body for key in required_keys)
        print(f"API {endpoint}: {'PASS' if valid else 'FAIL'} (HTTP {response.status_code}, JSON object={isinstance(body, dict)})")
        if not valid:
            return 1
    print("Hardware caveat: a null value is a graceful unavailable reading, not a passed sensor test.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
