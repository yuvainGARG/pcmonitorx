# HTTP API reference

Base URL: `http://<Windows-PC-LAN-IP>:8765`. All routes return UTF-8 JSON. If `PCMONITORX_API_TOKEN` is set, send `Authorization: Bearer <token>`.

| Route | Response |
|---|---|
| `GET /api/pc` | Current compact metrics object for the ESP32. |
| `GET /api/sensors` | Flat sensor list; each entry includes `name`, `value`, `unit`, `source`, and `available`, plus raw LibreHardwareMonitor readings if present. |
| `GET /api/status` | Server state, sample interval, per-source availability, and source errors. |
| `GET /api` | Short machine-readable endpoint and unit guide. |

## `/api/pc` units

- `cpu.temperature_c`, `gpu.temperature_c`: °C.
- `cpu.usage_percent`, `gpu.usage_percent`, `memory.usage_percent`, disk `usage_percent`, GPU `fan_percent`: percent (0–100 where provided by source).
- `cpu.frequency_mhz`, `gpu.clock_mhz`: MHz.
- `gpu.vram_used_mb`, `gpu.vram_total_mb`, `memory.used_mb`, `memory.total_mb`: MiB (1024² bytes; fields keep `_mb` naming for display compatibility).
- `network.upload_bytes_per_second`, `network.download_bytes_per_second`: bytes/second.
- Disk `used_gb` and `total_gb`: decimal GB (10⁹ bytes).
- `uptime_seconds`: seconds; timestamps use UTC ISO 8601.
- `storage_temperatures`: LibreHardwareMonitor storage sensor names and temperatures in °C; drive labels cannot always be matched to Windows volume letters.
- `cache_age_ms`: age of this cached sample in milliseconds.

Missing readings are JSON `null`; empty lists mean no readable disks/sensors were reported. The server uses a one-second default collection cache, configurable with `PCMONITORX_POLL_SECONDS` (minimum 0.25 seconds). Network throughput is a delta between consecutive samples, so the first sample reports 0 B/s.

## Example

```json
{
  "timestamp_utc": "2026-11-11T12:00:00+00:00",
  "cpu": {"temperature_c": null, "usage_percent": 11.4, "frequency_mhz": 4200.0},
  "gpu": {"name": "NVIDIA GeForce GTX 1660 Ti", "temperature_c": 53.0,
          "usage_percent": 22.0, "vram_used_mb": 900.0, "vram_total_mb": 6144.0,
          "fan_percent": 34.0, "clock_mhz": 1500.0},
  "memory": {"used_mb": 8100.0, "total_mb": 16384.0, "usage_percent": 49.4},
  "disks": [],
  "network": {"upload_bytes_per_second": 1000.0, "download_bytes_per_second": 42000.0},
  "uptime_seconds": 86400.0,
  "boot_time_utc": "2026-11-10T12:00:00+00:00",
  "cache_age_ms": 80
}
```

Example values are illustrative only; they are not readings from the target PC.
