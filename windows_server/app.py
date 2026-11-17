"""PCMonitorX local JSON server. Optional hardware sources fail independently."""
from __future__ import annotations

import math
import os
import shutil
import subprocess
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request
import psutil


def _finite(value: Any) -> float | None:
    try:
        number = float(value)
        return number if math.isfinite(number) else None
    except (TypeError, ValueError, OverflowError):
        return None


def _mib(value: Any) -> float | None:
    number = _finite(value)
    return round(number / (1024 * 1024), 1) if number is not None else None


class LibreHardwareMonitor:
    """Optional bridge to LibreHardwareMonitorLib.dll via pythonnet."""

    def __init__(self) -> None:
        self.computer: Any = None
        self.error: str | None = None
        self.available = False
        try:
            # netfx is pythonnet's Windows default and works with LHM net472 builds.
            # Set PCMONITORX_DOTNET_RUNTIME=coreclr for a compatible modern .NET build.
            runtime = os.environ.get("PCMONITORX_DOTNET_RUNTIME", "").strip()
            if runtime:
                from pythonnet import load  # type: ignore[import-not-found]
                load(runtime)
            import clr  # type: ignore[import-not-found]

            candidates = []
            configured = os.environ.get("PCMONITORX_LHM_DLL")
            if configured:
                candidates.append(Path(configured))
            candidates.extend([
                Path(__file__).resolve().parent / "libs" / "LibreHardwareMonitorLib.dll",
                Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LibreHardwareMonitor" / "LibreHardwareMonitorLib.dll",
            ])
            dll = next((p for p in candidates if p.is_file()), None)
            if dll is None:
                raise FileNotFoundError("LibreHardwareMonitorLib.dll not found; see documentation/setup.md")
            clr.AddReference(str(dll))
            from LibreHardwareMonitor.Hardware import Computer  # type: ignore[import-not-found]

            self.computer = Computer()
            self.computer.IsCpuEnabled = True
            self.computer.IsGpuEnabled = True
            self.computer.IsMemoryEnabled = True
            self.computer.IsMotherboardEnabled = True
            self.computer.IsStorageEnabled = True
            self.computer.Open()
            self.available = True
        except Exception as exc:  # optional dependency/runtime/DLL
            self.error = f"{type(exc).__name__}: {exc}"

    def read(self) -> dict[str, Any]:
        result: dict[str, Any] = {"cpu_temperature_c": None, "gpu_temperature_c": None,
                                  "gpu_usage_percent": None, "disk_temperatures": [], "sensors": []}
        if not self.available or self.computer is None:
            return result
        try:
            for hardware in self.computer.Hardware:
                self._visit(hardware, result)
        except Exception as exc:
            self.error = f"{type(exc).__name__}: {exc}"
            self.available = False
        return result

    def _visit(self, hardware: Any, result: dict[str, Any]) -> None:
        hardware.Update()
        hw_name = str(hardware.Name)
        hw_type = str(hardware.HardwareType)
        kind = hw_type.split(".")[-1].lower()
        for sensor in hardware.Sensors:
            name = str(sensor.Name)
            sensor_type = str(sensor.SensorType)
            stype = sensor_type.split(".")[-1].lower()
            value = _finite(sensor.Value)
            units = {"temperature": "°C", "load": "%", "fan": "RPM", "clock": "MHz",
                     "voltage": "V", "power": "W", "current": "A", "flow": "L/h",
                     "control": "%", "level": "%", "factor": "%", "data": "GB", "smalldata": "MB"}
            row = {"name": name, "hardware": hw_name, "hardware_type": hw_type,
                   "sensor_type": sensor_type, "value": value, "unit": units.get(stype, "unknown"),
                   "source": "LibreHardwareMonitor"}
            result["sensors"].append(row)
            normalized = f"{hw_name} {name}".lower()
            if value is None:
                continue
            if stype == "temperature" and value <= 0:
                # LHM reports 0 when it can't read the sensor (no admin rights or no PawnIO driver).
                row["value"] = None
                continue
            if stype == "temperature" and "cpu" in kind:
                # Prefer package/Tctl readings; otherwise retain the hottest CPU reading.
                current = result["cpu_temperature_c"]
                if any(token in normalized for token in ("package", "tctl", "die")):
                    result["cpu_temperature_c"] = value
                elif current is None:
                    result["cpu_temperature_c"] = value
                else:
                    result["cpu_temperature_c"] = max(current, value)
            elif stype == "temperature" and ("gpu" in kind or "graphics" in normalized):
                if result["gpu_temperature_c"] is None or "core" in normalized:
                    result["gpu_temperature_c"] = value
            elif stype == "load" and "gpu" in kind and "core" in normalized:
                result["gpu_usage_percent"] = value
            elif stype == "temperature" and ("storage" in kind or "drive" in normalized or "ssd" in normalized):
                result["disk_temperatures"].append({"name": hw_name, "temperature_c": value})
        for child in hardware.SubHardware:
            self._visit(child, result)


def _windows_process_table() -> list[tuple[int, str, int, int]]:
    """(pid, name, cpu time in 100 ns, private working set bytes) for every process, in ONE system call.
    psutil opens each process separately and falls back to a full table scan for protected ones,
    which costs seconds of CPU on a busy PC."""
    import ctypes
    from ctypes import wintypes

    class UNICODE_STRING(ctypes.Structure):
        _fields_ = [("Length", ctypes.c_ushort), ("MaximumLength", ctypes.c_ushort), ("Buffer", ctypes.c_void_p)]

    class SPI(ctypes.Structure):  # SYSTEM_PROCESS_INFORMATION (leading part)
        _fields_ = [("NextEntryOffset", ctypes.c_ulong), ("NumberOfThreads", ctypes.c_ulong),
                    ("WorkingSetPrivateSize", ctypes.c_longlong), ("HardFaultCount", ctypes.c_ulong),
                    ("NumberOfThreadsHighWatermark", ctypes.c_ulong), ("CycleTime", ctypes.c_ulonglong),
                    ("CreateTime", ctypes.c_longlong), ("UserTime", ctypes.c_longlong),
                    ("KernelTime", ctypes.c_longlong), ("ImageName", UNICODE_STRING),
                    ("BasePriority", ctypes.c_long), ("UniqueProcessId", ctypes.c_void_p)]

    ntdll = ctypes.WinDLL("ntdll")
    size = 1 << 20
    while True:
        buf = ctypes.create_string_buffer(size)
        ret = wintypes.ULONG(0)
        status = ntdll.NtQuerySystemInformation(5, buf, size, ctypes.byref(ret)) & 0xFFFFFFFF
        if status == 0xC0000004:  # STATUS_INFO_LENGTH_MISMATCH
            size = max(size * 2, ret.value + 65536)
            continue
        if status != 0:
            raise OSError(f"NtQuerySystemInformation failed 0x{status:08x}")
        break
    rows, off, base = [], 0, ctypes.addressof(buf)
    while True:
        e = SPI.from_address(base + off)
        pid = e.UniqueProcessId or 0
        if pid:
            name = ctypes.wstring_at(e.ImageName.Buffer, e.ImageName.Length // 2) if e.ImageName.Buffer else "?"
            name = name[:-4] if name.lower().endswith(".exe") else name
            rows.append((pid, name, e.UserTime + e.KernelTime, e.WorkingSetPrivateSize))
        if not e.NextEntryOffset:
            return rows
        off += e.NextEntryOffset


def _psutil_process_table() -> list[tuple[int, str, int, int]]:
    rows = []
    for p in psutil.process_iter(["name", "cpu_times", "memory_info"]):
        ct, mi = p.info.get("cpu_times"), p.info.get("memory_info")
        if p.pid and ct:
            rows.append((p.pid, p.info.get("name") or "?", int((ct.user + ct.system) * 1e7), mi.rss if mi else 0))
    return rows


class SystemMonitor:
    def __init__(self, cache_seconds: float = 1.0) -> None:
        self.cache_seconds = max(0.25, cache_seconds)
        self._lock = threading.Lock()
        self._cache: dict[str, Any] | None = None
        self._cache_at = 0.0
        self._last_net: tuple[float, int, int] | None = None
        try:
            self._cpu = _finite(psutil.cpu_percent(interval=None))
        except Exception:
            self._cpu = None
        self.lhm = LibreHardwareMonitor()
        self._lhm_data: dict[str, Any] = {}
        self._nvidia_exe = shutil.which("nvidia-smi") or self._find_nvidia_smi()
        self._procs: list[dict[str, Any]] = []
        self._procs_error: str | None = None
        threading.Thread(target=self._process_sampler, daemon=True, name="process-sampler").start()

    @staticmethod
    def _find_nvidia_smi() -> str | None:
        candidates = [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) /
                      "NVIDIA Corporation" / "NVSMI" / "nvidia-smi.exe"]
        return str(next((p for p in candidates if p.is_file()), "")) or None

    @staticmethod
    def _run_nvidia(executable: str) -> tuple[dict[str, Any] | None, str | None]:
        fields = "name,temperature.gpu,utilization.gpu,memory.used,memory.total,fan.speed,clocks.gr"
        try:
            completed = subprocess.run([executable, f"--query-gpu={fields}",
                                        "--format=csv,noheader,nounits"],
                                       capture_output=True, text=True, timeout=2.5, check=False,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if completed.returncode != 0:
                return None, (completed.stderr.strip() or f"nvidia-smi exited {completed.returncode}")
            line = next((line.strip() for line in completed.stdout.splitlines() if line.strip()), "")
            parts = [part.strip() for part in line.split(",")]
            if len(parts) < 7:
                return None, f"Unexpected nvidia-smi output: {line[:180]}"
            def col(index: int) -> float | None:
                return _finite(parts[index]) if parts[index].lower() not in {"n/a", "[n/a]", "not supported"} else None
            return ({"name": parts[0] or None, "temperature_c": col(1), "usage_percent": col(2),
                     "vram_used_mb": col(3), "vram_total_mb": col(4), "fan_percent": col(5),
                     "clock_mhz": col(6)}, None)
        except subprocess.TimeoutExpired:
            return None, "nvidia-smi timed out"
        except Exception as exc:
            return None, f"{type(exc).__name__}: {exc}"

    def _collect(self) -> dict[str, Any]:
        now_mono = time.monotonic()
        errors: dict[str, str] = {}
        try:
            self._cpu = _finite(psutil.cpu_percent(interval=None))
        except Exception as exc:
            errors["cpu"] = f"{type(exc).__name__}: {exc}"
            self._cpu = None
        try:
            freq = psutil.cpu_freq()
            cpu_freq = _finite(freq.current) if freq else None
        except Exception as exc:
            errors["cpu_frequency"] = f"{type(exc).__name__}: {exc}"
            cpu_freq = None
        try:
            mem = psutil.virtual_memory()
            memory = {"used_mb": _mib(mem.used), "total_mb": _mib(mem.total),
                      "usage_percent": _finite(mem.percent)}
        except Exception as exc:
            errors["memory"] = f"{type(exc).__name__}: {exc}"
            memory = {"used_mb": None, "total_mb": None, "usage_percent": None}
        try:
            boot = psutil.boot_time()
            uptime = max(0.0, time.time() - boot)
            boot_iso = datetime.fromtimestamp(boot, timezone.utc).isoformat()
        except Exception as exc:
            errors["uptime"] = f"{type(exc).__name__}: {exc}"
            uptime, boot_iso = None, None
        try:
            counters = psutil.net_io_counters()
            if self._last_net:
                elapsed = max(0.001, now_mono - self._last_net[0])
                upload = max(0, counters.bytes_sent - self._last_net[1]) / elapsed
                download = max(0, counters.bytes_recv - self._last_net[2]) / elapsed
            else:
                upload = download = 0.0
            self._last_net = (now_mono, counters.bytes_sent, counters.bytes_recv)
            network = {"upload_bytes_per_second": round(upload, 1),
                       "download_bytes_per_second": round(download, 1)}
        except Exception as exc:
            errors["network"] = f"{type(exc).__name__}: {exc}"
            network = {"upload_bytes_per_second": None, "download_bytes_per_second": None}
        try:
            partitions = psutil.disk_partitions(all=False)
            disks = []
            for part in partitions:
                try:
                    usage = psutil.disk_usage(part.mountpoint)
                    disks.append({"device": part.device, "mountpoint": part.mountpoint,
                                  "usage_percent": _finite(usage.percent), "used_gb": round(usage.used / 1e9, 2),
                                  "total_gb": round(usage.total / 1e9, 2), "temperature_c": None})
                except Exception:
                    continue
        except Exception as exc:
            errors["disk"] = f"{type(exc).__name__}: {exc}"
            disks = []
        try:
            self._lhm_data = self.lhm.read()
        except Exception as exc:
            errors["libre_hardware_monitor"] = f"{type(exc).__name__}: {exc}"
        for disk in disks:
            matches = [item["temperature_c"] for item in self._lhm_data.get("disk_temperatures", [])
                       if disk["device"].lower() in str(item.get("name", "")).lower()]
            if matches:
                disk["temperature_c"] = matches[0]
        gpu = {"name": None, "temperature_c": self._lhm_data.get("gpu_temperature_c"),
               "usage_percent": self._lhm_data.get("gpu_usage_percent"), "vram_used_mb": None,
               "vram_total_mb": None, "fan_percent": None, "clock_mhz": None}
        nvidia_available = False
        if self._nvidia_exe:
            nvidia, gpu_error = self._run_nvidia(self._nvidia_exe)
            nvidia_available = nvidia is not None
            if nvidia:
                for key, value in nvidia.items():
                    if value is not None:
                        gpu[key] = value
            if gpu_error:
                errors["nvidia_smi"] = gpu_error
        else:
            errors["nvidia_smi"] = "nvidia-smi executable not found"
        cpu_temp = self._lhm_data.get("cpu_temperature_c")
        lhm_sensors = self._lhm_data.get("sensors", [])
        try:
            per_core = [_finite(v) for v in psutil.cpu_percent(interval=None, percpu=True)]
        except Exception as exc:
            errors["cpu_per_core"] = f"{type(exc).__name__}: {exc}"
            per_core = []
        processes = self._top_processes(errors)
        timestamp = datetime.now(timezone.utc).isoformat()
        local_now = datetime.now().astimezone()
        offset = local_now.utcoffset()
        pc = {"timestamp_utc": timestamp,
              "local_time": local_now.strftime("%H:%M"),
              "local_date": local_now.strftime("%a %d %b"),
              "local_epoch": int(time.time() + (offset.total_seconds() if offset else 0)),
              "hostname": os.environ.get("COMPUTERNAME", ""),
              "processes": processes,
              "cpu": {"temperature_c": cpu_temp, "usage_percent": self._cpu,
                      "frequency_mhz": cpu_freq, "per_core_percent": per_core,
                      "cores": len(per_core)},
              "gpu": gpu, "memory": memory, "disks": disks,
              "storage_temperatures": self._lhm_data.get("disk_temperatures", []),
              "network": network, "uptime_seconds": _finite(uptime), "boot_time_utc": boot_iso}
        statuses = {
            "cpu_usage": {"available": self._cpu is not None, "error": errors.get("cpu")},
            "cpu_frequency": {"available": cpu_freq is not None, "error": errors.get("cpu_frequency")},
            "cpu_temperature": {"available": cpu_temp is not None,
                                "error": self.lhm.error if cpu_temp is None else None},
            "memory": {"available": memory["total_mb"] is not None, "error": errors.get("memory")},
            "disk": {"available": bool(disks), "error": errors.get("disk")},
            "network": {"available": network["upload_bytes_per_second"] is not None, "error": errors.get("network")},
            "libre_hardware_monitor": {"available": self.lhm.available and bool(lhm_sensors),
                                       "error": self.lhm.error if not self.lhm.available else errors.get("libre_hardware_monitor")},
            "nvidia_smi": {"available": nvidia_available,
                           "error": errors.get("nvidia_smi")},
            "gpu_temperature": {"available": gpu["temperature_c"] is not None,
                                 "error": errors.get("nvidia_smi") if gpu["temperature_c"] is None else None},
        }
        flat = [
            {"name": "CPU temperature", "value": cpu_temp, "unit": "°C", "source": "LibreHardwareMonitor"},
            {"name": "CPU usage", "value": self._cpu, "unit": "%", "source": "psutil"},
            {"name": "CPU frequency", "value": cpu_freq, "unit": "MHz", "source": "psutil"},
            {"name": "GPU temperature", "value": gpu["temperature_c"], "unit": "°C", "source": "nvidia-smi / LibreHardwareMonitor"},
            {"name": "GPU usage", "value": gpu["usage_percent"], "unit": "%", "source": "nvidia-smi / LibreHardwareMonitor"},
            {"name": "GPU VRAM used", "value": gpu["vram_used_mb"], "unit": "MB", "source": "nvidia-smi"},
            {"name": "GPU VRAM total", "value": gpu["vram_total_mb"], "unit": "MB", "source": "nvidia-smi"},
            {"name": "GPU fan speed", "value": gpu["fan_percent"], "unit": "%", "source": "nvidia-smi"},
            {"name": "GPU graphics clock", "value": gpu["clock_mhz"], "unit": "MHz", "source": "nvidia-smi"},
            {"name": "RAM usage", "value": memory["usage_percent"], "unit": "%", "source": "psutil"},
            {"name": "Network upload", "value": network["upload_bytes_per_second"], "unit": "B/s", "source": "psutil"},
            {"name": "Network download", "value": network["download_bytes_per_second"], "unit": "B/s", "source": "psutil"},
        ]
        for row in flat:
            row["available"] = row["value"] is not None
        for disk in disks:
            flat.append({"name": f"Disk {disk['mountpoint']} usage", "value": disk["usage_percent"],
                         "unit": "%", "source": "psutil", "available": disk["usage_percent"] is not None})
            flat.append({"name": f"Disk {disk['mountpoint']} temperature", "value": disk["temperature_c"],
                         "unit": "°C", "source": "LibreHardwareMonitor", "available": disk["temperature_c"] is not None})
        return {"pc": pc, "sensors": flat, "status": {"state": "degraded" if errors or not self.lhm.available or cpu_temp is None or not all(
                    item["available"] for item in (statuses["cpu_usage"], statuses["memory"])) else "ok",
                    "components": statuses, "errors": errors,
                    "sample_interval_seconds": self.cache_seconds}, "_lhm_sensors": lhm_sensors}

    def _top_processes(self, errors: dict[str, str]) -> list[dict[str, Any]]:
        """Latest top-process list from the background sampler (never blocks a request)."""
        if self._procs_error:
            errors["processes"] = self._procs_error
        return self._procs

    def _process_sampler(self, interval: float = 3.0, count: int = 6) -> None:
        """Every few seconds: CPU % and private memory per app name (processes grouped like Task Manager)."""
        ncpu = psutil.cpu_count() or 1
        last: dict[int, int] | None = None
        last_t = time.monotonic()
        while True:
            try:
                rows = _windows_process_table() if os.name == "nt" else _psutil_process_table()
                now = time.monotonic()
                elapsed_100ns = max(1.0, (now - last_t) * 1e7)
                groups: dict[str, list[float]] = {}
                for pid, name, cpu_time, mem in rows:
                    used = cpu_time - last.get(pid, cpu_time) if last is not None else 0
                    g = groups.setdefault(name, [0.0, 0.0])
                    g[0] += max(0, used) / elapsed_100ns / ncpu * 100
                    g[1] += mem
                last = {pid: cpu_time for pid, _, cpu_time, _ in rows}
                last_t = now
                top = sorted(groups.items(), key=lambda kv: (kv[1][0], kv[1][1]), reverse=True)[:count]
                self._procs = [{"name": n, "cpu_percent": round(c, 1), "memory_mb": round(mb / 1048576, 1)}
                               for n, (c, mb) in top]
                self._procs_error = None
            except Exception as exc:
                self._procs_error = f"{type(exc).__name__}: {exc}"
            time.sleep(interval)

    def snapshot(self, force: bool = False) -> dict[str, Any]:
        with self._lock:
            now = time.monotonic()
            if force or self._cache is None or now - self._cache_at >= self.cache_seconds:
                self._cache = self._collect()
                self._cache_at = time.monotonic()
            return self._cache


SETTINGS_FILE = Path(__file__).with_name("settings.json")   # git-ignored; see settings.example.json
WMO = {0: "Clear", 1: "Mostly clear", 2: "Partly cloudy", 3: "Cloudy", 45: "Fog", 48: "Fog",
       51: "Light drizzle", 53: "Drizzle", 55: "Heavy drizzle", 56: "Freezing drizzle", 57: "Freezing drizzle",
       61: "Light rain", 63: "Rain", 65: "Heavy rain", 66: "Freezing rain", 67: "Freezing rain",
       71: "Light snow", 73: "Snow", 75: "Heavy snow", 77: "Snow grains", 80: "Light showers", 81: "Showers",
       82: "Heavy showers", 85: "Snow showers", 86: "Snow showers", 95: "Thunderstorm", 96: "Thunderstorm, hail",
       99: "Thunderstorm, hail"}


def _load_settings() -> dict[str, Any]:
    try:
        import json
        return json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


class Weather:
    """Current weather + 12 h / 5 day forecast for one city from Open-Meteo (free, no API key).
    Refreshed every 10 minutes in a background thread; requests never wait for it."""

    def __init__(self, city: str, interval: float = 600.0) -> None:
        self.city = city.strip()
        self.interval = interval
        self.data: dict[str, Any] | None = None
        self.error: str | None = None
        self._place: dict[str, Any] | None = None
        if self.city:
            threading.Thread(target=self._loop, daemon=True, name="weather").start()

    @staticmethod
    def _get(url: str, params: dict[str, Any]) -> dict[str, Any]:
        import json
        import urllib.parse
        import urllib.request
        req = urllib.request.Request(url + "?" + urllib.parse.urlencode(params), headers={"User-Agent": "PCMonitorX"})
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read().decode())

    def _locate(self) -> dict[str, Any]:
        res = self._get("https://geocoding-api.open-meteo.com/v1/search",
                        {"name": self.city, "count": 1, "language": "en", "format": "json"}).get("results") or []
        if not res:
            raise ValueError(f"city not found: {self.city}")
        return res[0]

    def _fetch(self) -> dict[str, Any]:
        self._place = self._place or self._locate()
        p = self._place
        f = self._get("https://api.open-meteo.com/v1/forecast", {
            "latitude": p["latitude"], "longitude": p["longitude"], "timezone": "auto", "forecast_days": 5,
            "current": "temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,"
                       "precipitation,weather_code,is_day",
            "hourly": "temperature_2m,precipitation_probability,weather_code",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"})
        cur, hr, dy = f["current"], f["hourly"], f["daily"]
        now = cur["time"][:13]                      # "YYYY-MM-DDTHH"
        start = next((i for i, t in enumerate(hr["time"]) if t[:13] >= now), 0)
        hourly = [{"h": hr["time"][i][11:13], "t": hr["temperature_2m"][i],
                   "p": hr["precipitation_probability"][i], "c": hr["weather_code"][i]}
                  for i in range(start, min(start + 12, len(hr["time"])))]
        daily = []
        for i, d in enumerate(dy["time"]):
            daily.append({"day": "Today" if i == 0 else datetime.strptime(d, "%Y-%m-%d").strftime("%a"),
                          "max": dy["temperature_2m_max"][i], "min": dy["temperature_2m_min"][i],
                          "p": dy["precipitation_probability_max"][i], "c": dy["weather_code"][i]})
        code = cur["weather_code"]
        import unicodedata                  # the display's fonts are ASCII: "Kraków" -> "Krakow"
        name = unicodedata.normalize("NFKD", p.get("name", self.city)).encode("ascii", "ignore").decode() or self.city
        return {"city": name, "country": p.get("country_code", ""),
                "updated": cur["time"][11:16], "temp_c": cur["temperature_2m"],
                "feels_c": cur["apparent_temperature"], "humidity": cur["relative_humidity_2m"],
                "wind_kmh": cur["wind_speed_10m"], "precip_mm": cur["precipitation"], "code": code,
                "is_day": cur["is_day"], "desc": WMO.get(code, "Weather"),
                "sunrise": dy["sunrise"][0][11:16], "sunset": dy["sunset"][0][11:16],
                "hourly": hourly, "daily": daily}

    def _loop(self) -> None:
        while True:
            try:
                self.data = self._fetch()
                self.error = None
            except Exception as exc:     # network down, city typo...: keep the last good data
                self.error = f"{type(exc).__name__}: {exc}"
            time.sleep(self.interval if self.error is None else 60)


def create_app(monitor: SystemMonitor | None = None, token: str | None = None) -> Flask:
    app = Flask(__name__)
    cache_seconds = _finite(os.getenv("PCMONITORX_POLL_SECONDS", "1")) or 1.0
    system_monitor = monitor or SystemMonitor(cache_seconds)
    token = os.environ.get("PCMONITORX_API_TOKEN", "").strip() if token is None else token.strip()
    weather = Weather(os.environ.get("PCMONITORX_WEATHER_CITY") or str(_load_settings().get("weather_city", "")))
    media = None
    if os.name == "nt" and os.environ.get("PCMONITORX_MEDIA", "1") != "0":
        try:
            from media import Media
            media = Media()
        except Exception:  # optional feature
            media = None

    @app.before_request
    def authorize() -> Any:
        if token and request.headers.get("Authorization") != f"Bearer {token}":
            return jsonify({"error": "unauthorized"}), 401
        return None

    @app.get("/api/pc")
    def api_pc() -> Any:
        data = system_monitor.snapshot()
        pc = dict(data["pc"])
        pc["cache_age_ms"] = max(0, round((time.monotonic() - system_monitor._cache_at) * 1000))
        # Fresh (not cached) local wall clock in ms, so displays can show seconds accurately.
        offset = datetime.now().astimezone().utcoffset()
        pc["local_epoch_ms"] = int((time.time() + (offset.total_seconds() if offset else 0)) * 1000)
        pc["weather"] = weather.data
        pc["media"] = media.state if media else None
        return jsonify(pc)

    @app.get("/api/weather")
    def api_weather() -> Any:
        return jsonify({"city": weather.city or None, "weather": weather.data, "error": weather.error})

    @app.get("/api/media")
    def api_media() -> Any:
        if not media:
            return jsonify({"available": False, "error": "media controls need Windows and the winrt packages"})
        return jsonify(dict(media.state, error=media.error))

    @app.get("/api/media/art.jpg")
    def api_media_art() -> Any:
        if not media or not media.art:
            return jsonify({"error": "no album art"}), 404
        resp = app.response_class(media.art, mimetype="image/jpeg")
        resp.headers["Cache-Control"] = "no-store"
        return resp

    @app.post("/api/media/<cmd>")
    def api_media_command(cmd: str) -> Any:
        """play_pause, play, pause, next, previous, vol_up, vol_down, mute, volume?level=0-100, source."""
        if not media:
            return jsonify({"error": "media controls unavailable"}), 503
        level = request.args.get("level", type=int)
        try:
            return jsonify(media.command(cmd, level))
        except ValueError as exc:
            return jsonify({"error": str(exc)}), 400
        except Exception as exc:
            return jsonify({"error": f"{type(exc).__name__}: {exc}"}), 500

    @app.get("/api/sensors")
    def api_sensors() -> Any:
        data = system_monitor.snapshot()
        return jsonify({"timestamp_utc": data["pc"]["timestamp_utc"], "units": "Each sensor includes its own unit.",
                        "sensors": data["sensors"], "libre_hardware_monitor_raw": data["_lhm_sensors"]})

    @app.get("/api/status")
    def api_status() -> Any:
        status = dict(system_monitor.snapshot()["status"])
        status["server"] = "online"
        status["timestamp_utc"] = datetime.now(timezone.utc).isoformat()
        return jsonify(status)

    @app.get("/api")
    def api_documentation() -> Any:
        return jsonify({"name": "PCMonitorX", "version": "1.0.0", "endpoints": {
            "/api/pc": "Current PC metrics; temperatures °C, usages %, frequencies MHz, memory MB, network B/s, uptime seconds.",
            "/api/sensors": "Flattened sensor list; every value carries a unit and availability flag.",
            "/api/status": "Server and per-source availability, errors, and polling interval.",
            "/api/weather": "Weather for the city in settings.json (Open-Meteo); also included in /api/pc as 'weather'.",
            "/api/media": "Now playing on the PC (title, artist, progress, volume); also in /api/pc as 'media'.",
            "/api/media/art.jpg": "Album art of the current track, 120x120 JPEG.",
            "POST /api/media/<cmd>": "play_pause, play, pause, next, previous, vol_up, vol_down, mute, volume?level=0-100, source."},
            "missing_values": "Unavailable readings are JSON null; errors are included in /api/status."})

    return app


if __name__ == "__main__":
    host = os.environ.get("PCMONITORX_HOST", "0.0.0.0")
    port = int(os.environ.get("PCMONITORX_PORT", "8765"))
    app = create_app()
    try:
        from waitress import serve
        serve(app, host=host, port=port, threads=4)
    except ImportError:
        app.run(host=host, port=port, threaded=True)
