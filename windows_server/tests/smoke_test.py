"""Contract checks for the three JSON endpoints; run diagnostics.py for live sensors."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app import create_app  # noqa: E402


def main() -> int:
    client = create_app(token="").test_client()
    checks = {
        "/api/pc": ("cpu", "gpu", "memory", "disks", "network", "uptime_seconds"),
        "/api/sensors": ("sensors", "timestamp_utc"),
        "/api/status": ("components", "server", "state"),
        "/api": ("endpoints", "missing_values"),
    }
    for path, keys in checks.items():
        response = client.get(path)
        body = response.get_json(silent=True)
        assert response.status_code == 200, (path, response.status_code)
        assert response.mimetype == "application/json", (path, response.mimetype)
        assert isinstance(body, dict), (path, body)
        assert all(key in body for key in keys), (path, body.keys())
        print(f"PASS {path}: HTTP 200 JSON with keys {', '.join(keys)}")
    protected = create_app(token="pcmonitorx-test-token").test_client()
    denied = protected.get("/api/status")
    allowed = protected.get("/api/status", headers={"Authorization": "Bearer pcmonitorx-test-token"})
    assert denied.status_code == 401 and denied.get_json(silent=True) == {"error": "unauthorized"}
    assert allowed.status_code == 200 and isinstance(allowed.get_json(silent=True), dict)
    print("PASS API token: rejects missing token and accepts Bearer token")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
