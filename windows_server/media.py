"""Now playing + media controls for PCMonitorX (Windows).

Reads Windows' media sessions (the same thing the volume/media pop-up shows): YouTube Music or YouTube in a
browser, the YouTube Music desktop app, Spotify, ... Gives title, artist, progress and a small album-art JPEG,
and can play/pause, skip and change the master volume.

Optional: needs the winrt-* packages (and pycaw for volume, Pillow for album art). Without them the server
still runs and just reports media as unavailable.
"""
from __future__ import annotations

import asyncio
import concurrent.futures
import hashlib
import io
import threading
import time
from datetime import datetime, timezone
from typing import Any

FRIENDLY = {  # AppUserModelID fragments -> display name
    "youtube-music": "YouTube Music", "youtubemusic": "YouTube Music", "chrome": "Chrome", "msedge": "Edge",
    "308046b0af4a39cb": "Firefox", "firefox": "Firefox", "opera": "Opera", "brave": "Brave", "spotify": "Spotify",
    "zunemusic": "Media Player", "vlc": "VLC",
}
STATUS = {0: "closed", 1: "opened", 2: "changing", 3: "stopped", 4: "playing", 5: "paused"}
COMMANDS = ("play_pause", "play", "pause", "next", "previous", "vol_up", "vol_down", "mute", "volume", "source")


def _friendly(app_id: str) -> str:
    low = app_id.lower()
    for key, name in FRIENDLY.items():
        if key in low:
            return name
    # "com.riotgames.RiotGames.RiotClient" -> "RiotClient", "C:\...\foo.exe" -> "foo", "Pkg_x!App" -> "App"
    name = app_id.split("!")[-1].split("\\")[-1].removesuffix(".exe").split(".")[-1]
    return name[:20] or "Media"


def _ascii(text: str) -> str:
    """The display's fonts are ASCII only: fold accents ("Beyoncé" -> "Beyonce"), drop emoji, tidy spaces."""
    import unicodedata
    folded = unicodedata.normalize("NFKD", text or "").encode("ascii", "ignore").decode()
    return " ".join(folded.replace("|", "-").split())


def _seconds(td: Any) -> float:
    try:
        return max(0.0, td.total_seconds())
    except Exception:
        return 0.0


class Volume:
    """Master speaker volume via pycaw (Core Audio)."""

    def __init__(self) -> None:
        self.ep = None
        try:
            import comtypes
            comtypes.CoInitialize()  # this thread is only used for volume (see Media._vol)
            from pycaw.pycaw import AudioUtilities
            dev = AudioUtilities.GetSpeakers()
            ep = getattr(dev, "EndpointVolume", None)
            if ep is None:  # older pycaw
                from comtypes import CLSCTX_ALL
                from pycaw.pycaw import IAudioEndpointVolume
                from ctypes import POINTER, cast
                ep = cast(dev.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None), POINTER(IAudioEndpointVolume))
            self.ep = ep
        except Exception:
            self.ep = None

    def get(self) -> tuple[int | None, bool | None]:
        if not self.ep:
            return None, None
        try:
            return round(self.ep.GetMasterVolumeLevelScalar() * 100), bool(self.ep.GetMute())
        except Exception:
            return None, None

    def set(self, level: int) -> None:
        if self.ep:
            self.ep.SetMasterVolumeLevelScalar(max(0, min(100, level)) / 100.0, None)
            if level > 0:
                self.ep.SetMute(0, None)

    def toggle_mute(self) -> None:
        if self.ep:
            self.ep.SetMute(0 if self.ep.GetMute() else 1, None)


class Media:
    def __init__(self) -> None:
        self.state: dict[str, Any] = {"available": False}
        self.art: bytes | None = None
        self.error: str | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._pick: str | None = None  # app id chosen with the "source" command
        self._art_key = ""
        threading.Thread(target=self._run, daemon=True, name="media").start()

    # ------------------------------------------------------------ background loop
    def _run(self) -> None:
        try:
            from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as Manager  # noqa
        except Exception as exc:
            self.error = f"media controls unavailable ({type(exc).__name__}: {exc}); pip install winrt-Windows.Media.Control"
            return
        self._loop = asyncio.new_event_loop()
        self._loop.run_until_complete(self._main())

    async def _main(self) -> None:
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as Manager
        self.mgr = await Manager.request_async()
        # Core Audio (pycaw/comtypes) gets its own thread: loading it on the asyncio thread stalls the
        # WinRT stream reads used for album art.
        self._vol_pool = concurrent.futures.ThreadPoolExecutor(1, thread_name_prefix="volume")
        self.volume = await self._vol(Volume)
        while True:
            try:
                await self._refresh()
                self.error = None
            except Exception as exc:
                self.error = f"{type(exc).__name__}: {exc}"
            await asyncio.sleep(1.0)

    async def _vol(self, fn: Any, *args: Any) -> Any:
        return await asyncio.get_running_loop().run_in_executor(self._vol_pool, fn, *args)

    def _session(self) -> Any:
        sessions = list(self.mgr.get_sessions())
        if not sessions:
            return None, sessions
        if self._pick:
            for s in sessions:
                if s.source_app_user_model_id == self._pick:
                    return s, sessions
        playing = [s for s in sessions if s.get_playback_info().playback_status == 4]
        cur = self.mgr.get_current_session()
        if playing:
            return (cur if cur in playing else playing[0]), sessions
        return (cur or sessions[0]), sessions

    async def _refresh(self) -> None:
        s, sessions = self._session()
        vol, muted = await self._vol(self.volume.get)
        if s is None:
            self.state = {"available": True, "active": False, "volume": vol, "muted": muted, "sources": 0}
            return
        props = await s.try_get_media_properties_async()
        info = s.get_playback_info()
        tl = s.get_timeline_properties()
        status = STATUS.get(int(info.playback_status), "unknown")
        pos, dur = _seconds(tl.position), _seconds(tl.end_time)
        try:  # the timeline is a snapshot: move it forward while playing
            if status == "playing" and dur > 0:
                pos = min(dur, pos + max(0.0, (datetime.now(timezone.utc) - tl.last_updated_time).total_seconds()))
        except Exception:
            pass
        title, artist = props.title or "", props.artist or ""
        key = hashlib.md5(f"{s.source_app_user_model_id}|{title}|{artist}|{props.album_title}".encode()).hexdigest()[:8]
        if key != self._art_key:
            self._art_key = key
            self.art = await self._thumbnail(props)
        c = info.controls
        self.state = {
            "available": True, "active": True, "app": _friendly(s.source_app_user_model_id),
            "title": _ascii(title), "artist": _ascii(artist), "album": _ascii(props.album_title), "status": status,
            "position_s": round(pos, 1), "duration_s": round(dur, 1),
            "can_prev": bool(c.is_previous_enabled), "can_next": bool(c.is_next_enabled),
            "can_play_pause": bool(c.is_play_pause_toggle_enabled or c.is_play_enabled or c.is_pause_enabled),
            "art_id": key if self.art else "", "volume": vol, "muted": muted, "sources": len(sessions),
        }

    async def _thumbnail(self, props: Any) -> bytes | None:
        """Album art as a 120x120 baseline JPEG (what the display's decoder handles)."""
        if not props.thumbnail:
            return None
        try:
            from PIL import Image
            from winrt.windows.storage.streams import Buffer, InputStreamOptions
            stream = await props.thumbnail.open_read_async()
            buf = Buffer(stream.size)
            await stream.read_async(buf, stream.size, InputStreamOptions.READ_AHEAD)
            img = Image.open(io.BytesIO(bytes(buf))).convert("RGB")
            w, h = img.size
            if w > h * 1.2:  # 16:9 video thumbnail: the artwork is usually centred
                img = img.crop(((w - h) // 2, 0, (w + h) // 2, h))
            img = img.resize((120, 120), Image.LANCZOS)
            out = io.BytesIO()
            img.save(out, "JPEG", quality=80, progressive=False, optimize=False)
            return out.getvalue()
        except Exception:
            return None

    # ------------------------------------------------------------ commands (called from Flask threads)
    def command(self, cmd: str, arg: int | None = None) -> dict[str, Any]:
        if cmd not in COMMANDS:
            raise ValueError(f"unknown command {cmd}")
        if self._loop is None:
            raise RuntimeError(self.error or "media controls not ready")
        fut = asyncio.run_coroutine_threadsafe(self._do(cmd, arg), self._loop)
        fut.result(timeout=5)
        return self.state

    async def _do(self, cmd: str, arg: int | None) -> None:
        vol, _ = await self._vol(self.volume.get)
        if cmd in ("vol_up", "vol_down", "volume", "mute"):
            if cmd == "mute":
                await self._vol(self.volume.toggle_mute)
            elif cmd == "volume" and arg is not None:
                await self._vol(self.volume.set, arg)
            elif vol is not None:
                await self._vol(self.volume.set, vol + (5 if cmd == "vol_up" else -5))
        elif cmd == "source":  # cycle between apps that have a media session
            sessions = list(self.mgr.get_sessions())
            if sessions:
                ids = [s.source_app_user_model_id for s in sessions]
                cur = self._pick if self._pick in ids else (self._session()[0].source_app_user_model_id)
                self._pick = ids[(ids.index(cur) + 1) % len(ids)] if cur in ids else ids[0]
        else:
            s, _ = self._session()
            if s is not None:
                if cmd == "play_pause":
                    await s.try_toggle_play_pause_async()
                elif cmd == "play":
                    await s.try_play_async()
                elif cmd == "pause":
                    await s.try_pause_async()
                elif cmd == "next":
                    await s.try_skip_next_async()
                elif cmd == "previous":
                    await s.try_skip_previous_async()
        await asyncio.sleep(0.15)  # let the app react before reading the new state
        await self._refresh()
