#!/usr/bin/env python3
"""LaMetric Time Recovery — REST API Daemon

Runs on the PC, communicates with the LaMetric Time device via USB serial.
The device runs the recovery shell (shell_init.c) which accepts text commands
over /dev/ttyGS0 (USB gadget serial).

=== QUICK START ===

    pip install pyserial
    python lametric_daemon.py

Server starts on http://localhost:8080

=== API EXAMPLES (curl) ===

    # Device info
    curl http://localhost:8080/api/v1/info

    # Show text on display
    curl -X POST http://localhost:8080/api/v1/display -d '{"text": "HELLO"}'

    # Scroll long message
    curl -X POST http://localhost:8080/api/v1/scroll -d '{"text": "Hello World!"}'

    # Push notification (sends notify <icon> <text> to device)
    curl -X POST http://localhost:8080/api/v1/notification \
         -d '{"text": "Alert!", "icon": "warn", "sound": "positive1", "duration": 5}'

    # Custom metric display (one-shot)
    curl -X POST http://localhost:8080/api/v1/metric \
         -d '{"icon": "chart", "value": "42%", "trend": "up"}'

    # Named metric (add/update)
    curl -X POST http://localhost:8080/api/v1/metric \
         -d '{"name": "ARR", "value": "$2.4M", "icon": "chart", "trend": "up"}'

    # List all named metrics
    curl http://localhost:8080/api/v1/metrics

    # Delete a named metric
    curl -X DELETE http://localhost:8080/api/v1/metric/ARR

    # Push all metrics to device
    curl -X POST http://localhost:8080/api/v1/metrics/push

    # Webhook (simplified format for Zapier/IFTTT)
    curl -X POST http://localhost:8080/api/v1/metrics/webhook \
         -d '{"metric": "ARR", "value": "$2.4M", "trend": "up"}'

    # Sync time (carousel handles clock display)
    curl http://localhost:8080/api/v1/clock

    # Set brightness (0-255)
    curl -X POST http://localhost:8080/api/v1/bright -d '{"value": 200}'

    # Widget carousel — add widgets
    curl -X POST http://localhost:8080/api/v1/widget -d '{"type": "clock"}'
    curl -X POST http://localhost:8080/api/v1/widget -d '{"type": "text", "text": "hello"}'
    curl -X POST http://localhost:8080/api/v1/widget -d '{"type": "metric", "value": "99%"}'

    # List active widgets
    curl http://localhost:8080/api/v1/widgets

    # Remove a widget by ID
    curl -X DELETE http://localhost:8080/api/v1/widget/1

    # Get current weather data
    curl http://localhost:8080/api/v1/weather

    # Set weather location
    curl -X POST http://localhost:8080/api/v1/weather \
         -d '{"lat": 40.7128, "lon": -74.0060}'

    # Carousel status
    curl http://localhost:8080/api/v1/carousel

    # Start/restart device carousel
    curl -X POST http://localhost:8080/api/v1/carousel

=== PYTHON EXAMPLES ===

    import requests

    # Show text
    requests.post("http://localhost:8080/api/v1/display", json={"text": "HI"})

    # Notification with sound
    requests.post("http://localhost:8080/api/v1/notification", json={
        "text": "Server Down!", "sound": "positive1", "duration": 3
    })

    # Add clock widget to carousel
    requests.post("http://localhost:8080/api/v1/widget", json={"type": "clock"})

"""

import json
import threading
import time
import sys
import os
import re
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, unquote
from urllib.request import urlopen
from urllib.error import URLError

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial is required.  Install with:  pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

HTTP_HOST = "localhost"
HTTP_PORT = 8080
DEFAULT_BAUD = 115200
SERIAL_TIMEOUT = 2          # seconds
COMMAND_WAIT = 2.0           # seconds to wait for device response
RECONNECT_INTERVAL = 5      # seconds between reconnection attempts
CAROUSEL_INTERVAL = 10      # seconds per widget in carousel mode
WEATHER_INTERVAL = 300       # seconds between weather fetches (5 minutes)
DEFAULT_LATITUDE = 47.6062   # Seattle
DEFAULT_LONGITUDE = -122.3321


# ---------------------------------------------------------------------------
# Serial / Device Manager
# ---------------------------------------------------------------------------

class DeviceManager:
    """Manages the serial connection to the LaMetric Time device.

    Handles auto-detection of COM port, sending commands, reading responses,
    automatic time sync on connect, and graceful reconnection.
    """

    def __init__(self):
        self._port = None          # serial.Serial instance
        self._lock = threading.Lock()
        self._connected = False
        self._port_name = None
        self._device_info = {}
        self._reconnect_thread = None
        self._stopping = False

    # -- Connection management ------------------------------------------------

    def auto_detect_port(self):
        """Scan COM ports for a USB Serial Device and return the port name."""
        try:
            ports = serial.tools.list_ports.comports()
            for p in ports:
                desc = (p.description or "").lower()
                # Match common USB-serial chip descriptions
                if "usb serial" in desc or "usb-serial" in desc:
                    return p.device
                # Also match FTDI / CP210x / CH340 / CDC ACM
                if any(kw in desc for kw in ("ftdi", "cp210", "ch340", "cdc", "gadget")):
                    return p.device
            # Fallback: if only one port exists, try it
            if len(ports) == 1:
                return ports[0].device
        except Exception as exc:
            _log(f"Port scan error: {exc}")
        return None

    def connect(self, port_name=None):
        """Open serial connection. Auto-detects port if *port_name* is None."""
        with self._lock:
            if self._connected:
                return True

            if port_name is None:
                port_name = self.auto_detect_port()
            if port_name is None:
                port_name = "COM3"  # hard fallback

            try:
                _log(f"Connecting to {port_name} at {DEFAULT_BAUD} baud...")
                self._port = serial.Serial(
                    port_name, DEFAULT_BAUD, timeout=SERIAL_TIMEOUT
                )
                time.sleep(0.5)
                # Flush stale data
                self._port.read(self._port.in_waiting)
                self._port_name = port_name
                self._connected = True
                _log(f"Connected to {port_name}")
            except Exception as exc:
                _log(f"Connection failed ({port_name}): {exc}")
                self._port = None
                self._connected = False
                return False

        # Sync PC time to device (outside lock — send_command acquires it)
        self._sync_time()
        # Grab device info for /api/v1/info
        self._refresh_info()
        return True

    def disconnect(self):
        """Close the serial port."""
        with self._lock:
            if self._port and self._port.is_open:
                try:
                    self._port.close()
                except Exception:
                    pass
            self._port = None
            self._connected = False
            self._port_name = None

    def ensure_connected(self):
        """Return True if connected; attempt one reconnect if not."""
        if self._connected:
            return True
        return self.connect()

    def start_reconnect_loop(self):
        """Start a background thread that reconnects when the device drops."""
        self._stopping = False
        self._reconnect_thread = threading.Thread(
            target=self._reconnect_worker, daemon=True
        )
        self._reconnect_thread.start()

    def stop(self):
        """Signal background threads to stop."""
        self._stopping = True
        self.disconnect()

    def _reconnect_worker(self):
        """Background loop: reconnect if disconnected."""
        while not self._stopping:
            if not self._connected:
                _log("Device disconnected — attempting reconnect...")
                self.connect()
            time.sleep(RECONNECT_INTERVAL)

    # -- Command interface ----------------------------------------------------

    def send_command(self, cmd, wait=None):
        """Send a text command to the device and return the response string.

        Returns (success: bool, response: str).
        """
        if wait is None:
            wait = COMMAND_WAIT
        with self._lock:
            if not self._connected or not self._port:
                return False, "Device not connected"
            try:
                self._port.write((cmd + "\r").encode())
                time.sleep(wait)
                raw = self._port.read(self._port.in_waiting)
                resp = raw.decode(errors="ignore")
                return True, resp.strip()
            except Exception as exc:
                _log(f"Command error: {exc}")
                self._connected = False
                try:
                    self._port.close()
                except Exception:
                    pass
                self._port = None
                return False, f"Serial error: {exc}"

    # -- Convenience helpers --------------------------------------------------

    def _sync_time(self):
        """Send the current PC time to the device and start carousel."""
        now = datetime.now()
        cmd = f"time {now.hour:02d}:{now.minute:02d}:{now.second:02d}"
        ok, resp = self.send_command(cmd, wait=1.0)
        if ok:
            _log(f"Time synced: {now.strftime('%H:%M:%S')}")
        else:
            _log(f"Time sync failed: {resp}")
        # Auto-start the device's production widget carousel
        ok2, resp2 = self.send_command("carousel", wait=1.0)
        if ok2:
            _log("Carousel started on device")
        else:
            _log(f"Carousel start failed: {resp2}")

    def _refresh_info(self):
        """Query device info and cache it."""
        ok, resp = self.send_command("info", wait=2.0)
        self._device_info = {
            "connected": self._connected,
            "port": self._port_name,
            "raw_info": resp if ok else "",
        }

    @property
    def info(self):
        return {
            "connected": self._connected,
            "port": self._port_name,
            "baud": DEFAULT_BAUD,
            "raw_info": self._device_info.get("raw_info", ""),
        }


# ---------------------------------------------------------------------------
# Widget Carousel
# ---------------------------------------------------------------------------

class WidgetCarousel:
    """Maintains an ordered list of widgets and auto-rotates on the display.

    Each widget is a dict:
        {
            "id": int,
            "type": "clock" | "text" | "metric" | "notification",
            "content": {...},
            "update_interval": int (seconds),
        }
    """

    def __init__(self, device):
        self._device = device
        self._widgets = []          # list of widget dicts
        self._next_id = 1
        self._lock = threading.Lock()
        self._current_index = 0
        self._interval = CAROUSEL_INTERVAL
        self._running = False
        self._thread = None

    # -- Public API -----------------------------------------------------------

    def add(self, widget_type, content=None, update_interval=None):
        """Add a widget. Returns the new widget dict."""
        if content is None:
            content = {}
        if update_interval is None:
            update_interval = self._interval
        with self._lock:
            widget = {
                "id": self._next_id,
                "type": widget_type,
                "content": content,
                "update_interval": update_interval,
            }
            self._next_id += 1
            self._widgets.append(widget)
        self._ensure_running()
        return widget

    def remove(self, widget_id):
        """Remove widget by ID. Returns True if found."""
        with self._lock:
            before = len(self._widgets)
            self._widgets = [w for w in self._widgets if w["id"] != widget_id]
            removed = len(self._widgets) < before
            if self._current_index >= len(self._widgets):
                self._current_index = 0
        if not self._widgets:
            self._running = False
        return removed

    def list_widgets(self):
        """Return a snapshot of the widget list."""
        with self._lock:
            return list(self._widgets)

    def advance(self, direction=1):
        """Manually advance carousel (direction: +1 forward, -1 backward)."""
        with self._lock:
            if not self._widgets:
                return
            self._current_index = (
                (self._current_index + direction) % len(self._widgets)
            )
            widget = self._widgets[self._current_index]
        self._show_widget(widget)

    @property
    def interval(self):
        return self._interval

    @interval.setter
    def interval(self, value):
        self._interval = max(1, int(value))

    # -- Internal -------------------------------------------------------------

    def _ensure_running(self):
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._carousel_loop, daemon=True)
        self._thread.start()

    def _carousel_loop(self):
        """Background thread: rotate through widgets at the configured interval."""
        while self._running:
            with self._lock:
                if not self._widgets:
                    self._running = False
                    break
                widget = self._widgets[self._current_index]
            self._show_widget(widget)
            # Sleep in small increments so we can detect stop quickly
            slept = 0
            while slept < self._interval and self._running:
                time.sleep(0.5)
                slept += 0.5
            with self._lock:
                if self._widgets:
                    self._current_index = (
                        (self._current_index + 1) % len(self._widgets)
                    )

    def _show_widget(self, widget):
        """Render a single widget on the device."""
        wtype = widget["type"]
        content = widget.get("content", {})

        if not self._device.ensure_connected():
            return

        if wtype == "clock":
            # Sync time; the device carousel handles clock display
            now = datetime.now()
            self._device.send_command(
                f"time {now.hour:02d}:{now.minute:02d}:{now.second:02d}",
                wait=0.5,
            )

        elif wtype == "text":
            text = content.get("text", "")
            if text:
                self._device.send_command(f"text {text}", wait=1.0)

        elif wtype == "metric":
            # Use icontext <icon> <value> for metric widgets
            value = content.get("value", "")
            icon = content.get("icon", "")
            if icon:
                self._device.send_command(
                    f"icontext {icon} {value}", wait=1.0
                )
            else:
                self._device.send_command(f"text {value}", wait=1.0)

        elif wtype == "notification":
            text = content.get("text", "")
            icon = content.get("icon", "bell")
            if text:
                self._device.send_command(
                    f"notify {icon} {text}", wait=1.0
                )


# ---------------------------------------------------------------------------
# Weather Fetcher
# ---------------------------------------------------------------------------

# WMO weather code to icon name mapping
_WMO_ICON_MAP = {
    0: "sun", 1: "sun",
    2: "cloud", 3: "cloud",
    45: "cloud", 48: "cloud",
    51: "rain", 53: "rain", 55: "rain",
    56: "rain", 57: "rain",
    61: "rain", 63: "rain", 65: "rain",
    66: "rain", 67: "rain",
    71: "snow", 73: "snow", 75: "snow",
    77: "snow",
    80: "rain", 81: "rain", 82: "rain",
    85: "snow", 86: "snow",
    95: "thunder", 96: "thunder", 99: "thunder",
}


def _wmo_to_icon(code):
    """Map a WMO weather code to an icon name."""
    if code in _WMO_ICON_MAP:
        return _WMO_ICON_MAP[code]
    # Range-based fallback
    if 0 <= code <= 1:
        return "sun"
    if 2 <= code <= 3:
        return "cloud"
    if 45 <= code <= 48:
        return "cloud"
    if 51 <= code <= 67:
        return "rain"
    if 71 <= code <= 86:
        return "snow"
    if 95 <= code <= 99:
        return "thunder"
    return "cloud"


class WeatherFetcher:
    """Background daemon thread that fetches weather from Open-Meteo API
    and sends it to the LaMetric device at regular intervals."""

    def __init__(self, device):
        self._device = device
        self._latitude = DEFAULT_LATITUDE
        self._longitude = DEFAULT_LONGITUDE
        self._interval = WEATHER_INTERVAL
        self._lock = threading.Lock()
        self._running = False
        self._thread = None
        self._cached_weather = {}

    @property
    def cached_weather(self):
        with self._lock:
            return dict(self._cached_weather)

    def set_location(self, latitude, longitude):
        """Update the location for weather fetches."""
        with self._lock:
            self._latitude = latitude
            self._longitude = longitude
        _log(f"Weather location updated: lat={latitude}, lon={longitude}")
        # Fetch immediately with new location
        self._fetch_and_send()

    def get_location(self):
        with self._lock:
            return self._latitude, self._longitude

    def start(self):
        """Start the background weather fetch loop."""
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._weather_loop, daemon=True)
        self._thread.start()
        _log("Weather fetcher started")

    def stop(self):
        self._running = False

    def _weather_loop(self):
        """Background loop: fetch weather at regular intervals."""
        # Initial fetch after a short delay to let device settle
        time.sleep(5)
        while self._running:
            self._fetch_and_send()
            # Sleep in small increments
            slept = 0
            while slept < self._interval and self._running:
                time.sleep(1)
                slept += 1

    def _fetch_and_send(self):
        """Fetch weather from Open-Meteo and send to device."""
        with self._lock:
            lat = self._latitude
            lon = self._longitude

        url = (
            f"https://api.open-meteo.com/v1/forecast"
            f"?latitude={lat}&longitude={lon}&current_weather=true"
        )
        try:
            resp = urlopen(url, timeout=10)
            data = json.loads(resp.read().decode("utf-8"))
            current = data.get("current_weather", {})

            temp_c = current.get("temperature", 0)
            wmo_code = current.get("weathercode", 0)

            # Convert C to F
            temp_f = round(temp_c * 9.0 / 5.0 + 32)
            icon = _wmo_to_icon(wmo_code)

            with self._lock:
                self._cached_weather = {
                    "temperature_c": temp_c,
                    "temperature_f": temp_f,
                    "icon": icon,
                    "wmo_code": wmo_code,
                    "latitude": lat,
                    "longitude": lon,
                    "timestamp": datetime.now().isoformat(),
                }

            # Send to device
            cmd = f"weather {icon} {temp_f}F"
            ok, resp_text = self._device.send_command(cmd, wait=1.0)
            if ok:
                _log(f"Weather sent: {icon} {temp_f}F (WMO {wmo_code})")
            else:
                _log(f"Weather send failed: {resp_text}")

        except (URLError, OSError, json.JSONDecodeError, KeyError) as exc:
            _log(f"Weather fetch error: {exc}")


# ---------------------------------------------------------------------------
# Metrics Manager
# ---------------------------------------------------------------------------

class MetricsManager:
    """Stores named metrics and pushes them to the device via serial.

    Each metric is a dict:
        {"name": str, "value": str, "icon": str, "trend": str}

    When metrics change, they are automatically pushed to the device using
    the serial command:  metric <index> <icon> <value> <trend>
    """

    def __init__(self, device):
        self._device = device
        self._metrics = []      # list of metric dicts
        self._lock = threading.Lock()

    def set(self, name, value, icon="", trend=""):
        """Add or update a metric by name. Auto-pushes to device."""
        with self._lock:
            # Look for existing metric with same name
            for m in self._metrics:
                if m["name"] == name:
                    m["value"] = value
                    m["icon"] = icon
                    m["trend"] = trend
                    break
            else:
                # New metric
                self._metrics.append({
                    "name": name,
                    "value": value,
                    "icon": icon,
                    "trend": trend,
                })
        # Auto-push after change
        self.push_to_device()

    def remove(self, name):
        """Remove a metric by name. Returns True if found."""
        with self._lock:
            before = len(self._metrics)
            self._metrics = [m for m in self._metrics if m["name"] != name]
            removed = len(self._metrics) < before
        if removed:
            self.push_to_device()
        return removed

    def list_metrics(self):
        """Return a snapshot of all metrics."""
        with self._lock:
            return list(self._metrics)

    def push_to_device(self):
        """Send all metrics to the device via serial.

        Command format: metric <index> <icon> <value> <trend>
        """
        if not self._device.ensure_connected():
            _log("Metrics push failed: device not connected")
            return False

        with self._lock:
            snapshot = list(self._metrics)

        for idx, m in enumerate(snapshot):
            icon = m.get("icon", "")
            value = m.get("value", "")
            trend = m.get("trend", "")
            cmd = f"metric {idx} {icon} {value} {trend}".strip()
            ok, resp = self._device.send_command(cmd, wait=0.5)
            if ok:
                _log(f"Metric pushed: {cmd}")
            else:
                _log(f"Metric push failed: {cmd} -> {resp}")
                return False
        return True


# ---------------------------------------------------------------------------
# HTTP Request Handler
# ---------------------------------------------------------------------------

class LaMetricHandler(BaseHTTPRequestHandler):
    """HTTP request handler for the LaMetric REST API."""

    # Shared references (set by main before starting the server)
    device = None           # type: DeviceManager
    carousel = None         # type: WidgetCarousel
    weather_fetcher = None  # type: WeatherFetcher
    metrics_manager = None  # type: MetricsManager

    # Suppress default stderr logging (we log ourselves)
    def log_message(self, format, *args):
        _log(f"HTTP {args[0]}")

    # -- Routing --------------------------------------------------------------

    def do_GET(self):
        path = urlparse(self.path).path.rstrip("/")

        if path == "/api/v1/info":
            self._handle_info()
        elif path == "/api/v1/clock":
            self._handle_clock()
        elif path == "/api/v1/widgets":
            self._handle_list_widgets()
        elif path == "/api/v1/weather":
            self._handle_get_weather()
        elif path == "/api/v1/metrics":
            self._handle_list_metrics()
        elif path == "/api/v1/carousel":
            self._handle_get_carousel()
        elif path == "" or path == "/":
            self._handle_index()
        else:
            self._send_json(404, {"error": "Not found", "path": path})

    def do_POST(self):
        path = urlparse(self.path).path.rstrip("/")
        body = self._read_body()

        if path == "/api/v1/display":
            self._handle_display(body)
        elif path == "/api/v1/scroll":
            self._handle_scroll(body)
        elif path == "/api/v1/notification":
            self._handle_notification(body)
        elif path == "/api/v1/metric":
            self._handle_metric(body)
        elif path == "/api/v1/metrics/push":
            self._handle_metrics_push()
        elif path == "/api/v1/metrics/webhook":
            self._handle_metrics_webhook(body)
        elif path == "/api/v1/bright":
            self._handle_bright(body)
        elif path == "/api/v1/widget":
            self._handle_add_widget(body)
        elif path == "/api/v1/weather":
            self._handle_set_weather(body)
        elif path == "/api/v1/carousel":
            self._handle_post_carousel(body)
        elif path == "/api/v1/scrollmode":
            self._handle_scrollmode(body)
        elif path == "/api/v1/luxrange":
            self._handle_luxrange(body)
        else:
            self._send_json(404, {"error": "Not found", "path": path})

    def do_DELETE(self):
        path = urlparse(self.path).path.rstrip("/")
        # Match /api/v1/widget/{id}
        m = re.match(r"^/api/v1/widget/(\d+)$", path)
        if m:
            self._handle_remove_widget(int(m.group(1)))
            return
        # Match /api/v1/metric/{name}
        m = re.match(r"^/api/v1/metric/(.+)$", path)
        if m:
            self._handle_remove_metric(m.group(1))
            return
        self._send_json(404, {"error": "Not found", "path": path})

    # -- Endpoint handlers ----------------------------------------------------

    def _handle_index(self):
        """Serve a simple landing page listing available endpoints."""
        endpoints = {
            "endpoints": {
                "GET  /api/v1/info": "Device info",
                "POST /api/v1/display": "Show text: {\"text\": \"hello\"}",
                "POST /api/v1/scroll": "Scroll text: {\"text\": \"long message\"}",
                "POST /api/v1/notification": "Push notification: {\"text\": \"Alert!\", \"icon\": \"warn\"}",
                "POST /api/v1/metric": "Add/update named metric or one-shot: {\"name\": \"ARR\", \"value\": \"$2.4M\", \"icon\": \"chart\", \"trend\": \"up\"}",
                "GET  /api/v1/metrics": "List all named metrics",
                "DELETE /api/v1/metric/{name}": "Remove a named metric",
                "POST /api/v1/metrics/push": "Push all metrics to device",
                "POST /api/v1/metrics/webhook": "Webhook (Zapier/IFTTT): {\"metric\": \"ARR\", \"value\": \"$2.4M\", \"trend\": \"up\"}",
                "GET  /api/v1/clock": "Sync time (carousel handles display)",
                "POST /api/v1/bright": "Set brightness: {\"value\": 200}",
                "POST /api/v1/widget": "Add widget to carousel",
                "GET  /api/v1/widgets": "List active widgets",
                "DELETE /api/v1/widget/{id}": "Remove widget",
                "GET  /api/v1/weather": "Current cached weather data",
                "POST /api/v1/weather": "Set weather location: {\"lat\": 47.6, \"lon\": -122.3}",
                "GET  /api/v1/carousel": "Carousel status",
                "POST /api/v1/carousel": "Start/restart device carousel",
            },
            "status": "running",
            "device": self.device.info,
        }
        self._send_json(200, endpoints)

    def _handle_info(self):
        """GET /api/v1/info — return device status and info."""
        if not self.device.ensure_connected():
            self._send_json(503, {
                "error": "Device not connected",
                "connected": False,
            })
            return
        ok, resp = self.device.send_command("info", wait=2.0)
        info = self.device.info
        info["raw_response"] = resp
        self._send_json(200, info)

    def _handle_display(self, body):
        """POST /api/v1/display — show static text."""
        text = body.get("text", "")
        if not text:
            self._send_json(400, {"error": "Missing 'text' field"})
            return
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return
        ok, resp = self.device.send_command(f"text {text}", wait=1.5)
        self._send_json(200 if ok else 500, {
            "command": "text",
            "text": text,
            "success": ok,
            "response": resp,
        })

    def _handle_scroll(self, body):
        """POST /api/v1/scroll — scroll text across the display."""
        text = body.get("text", "")
        if not text:
            self._send_json(400, {"error": "Missing 'text' field"})
            return
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return
        # Scrolling can take a while — estimate based on text length
        wait = max(3.0, len(text) * 0.35)
        ok, resp = self.device.send_command(f"scroll {text}", wait=wait)
        self._send_json(200 if ok else 500, {
            "command": "scroll",
            "text": text,
            "success": ok,
            "response": resp,
        })

    def _handle_notification(self, body):
        """POST /api/v1/notification — push a notification.

        Sends `notify <icon> <text>` to the device.
        Fields: text (required), icon (default: bell), sound, duration (seconds).
        """
        text = body.get("text", "")
        if not text:
            self._send_json(400, {"error": "Missing 'text' field"})
            return
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return

        icon = body.get("icon", "bell")
        sound = body.get("sound", "")
        duration = body.get("duration", 0)

        # Build the notify command with optional sound
        # Format: notify <icon> [sound:<name>] <text>
        sound_part = f"sound:{sound} " if sound else ""
        cmd = f"notify {icon} {sound_part}{text}"

        ok, resp = self.device.send_command(cmd, wait=1.5)

        # Schedule auto-clear after duration (non-blocking)
        if duration and duration > 0:
            def _clear():
                time.sleep(duration)
                # If carousel is running, let it take over; otherwise clear
                if not self.carousel.list_widgets():
                    self.device.send_command("text ", wait=0.5)
            threading.Thread(target=_clear, daemon=True).start()

        self._send_json(200 if ok else 500, {
            "command": "notify",
            "icon": icon,
            "text": text,
            "sound": sound,
            "duration": duration,
            "success": ok,
            "response": resp,
        })

    def _handle_metric(self, body):
        """POST /api/v1/metric — add/update a named metric, or one-shot display.

        If "name" is provided, the metric is stored in MetricsManager and
        auto-pushed to the device.  Otherwise, it is a one-shot display
        (backwards-compatible behaviour).

        Fields: name (optional), value (required), icon, trend (up/down/stable).
        """
        value = body.get("value", "")
        if not value:
            self._send_json(400, {"error": "Missing 'value' field"})
            return

        name = body.get("name", "")
        icon = body.get("icon", "")
        trend = body.get("trend", "")

        # --- Named metric: store + auto-push via MetricsManager ---
        if name:
            self.metrics_manager.set(name, value, icon=icon, trend=trend)
            self._send_json(200, {
                "success": True,
                "metric": {"name": name, "value": value, "icon": icon, "trend": trend},
                "metrics_count": len(self.metrics_manager.list_metrics()),
            })
            return

        # --- One-shot metric display (no name, backwards-compatible) ---
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return

        if icon:
            cmd = f"icontext {icon} {value}"
        else:
            cmd = f"text {value}"

        ok, resp = self.device.send_command(cmd, wait=1.5)
        self._send_json(200 if ok else 500, {
            "command": "metric",
            "display": f"{icon} {value}" if icon else value,
            "success": ok,
            "response": resp,
        })

    def _handle_clock(self):
        """GET /api/v1/clock — sync time (carousel handles clock display)."""
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return

        # Sync time with seconds precision; carousel handles display
        now = datetime.now()
        ok, resp = self.device.send_command(
            f"time {now.hour:02d}:{now.minute:02d}:{now.second:02d}", wait=1.0
        )
        self._send_json(200 if ok else 500, {
            "command": "time",
            "time_synced": now.strftime("%H:%M:%S"),
            "success": ok,
            "response": resp,
        })

    def _handle_bright(self, body):
        """POST /api/v1/bright — set display brightness (0-255)."""
        value = body.get("value")
        if value is None:
            self._send_json(400, {"error": "Missing 'value' field"})
            return
        value = max(0, min(255, int(value)))
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return
        ok, resp = self.device.send_command(f"bright {value}", wait=1.0)
        self._send_json(200 if ok else 500, {
            "command": "bright",
            "value": value,
            "success": ok,
            "response": resp,
        })

    def _handle_scrollmode(self, body):
        """POST /api/v1/scrollmode — set scroll mode (0-4).

        Modes: 0=auto, 1=left-to-right, 2=right-to-left, 3=bounce, 4=no-scroll
        """
        mode = body.get("mode")
        if mode is None:
            self._send_json(400, {"error": "Missing 'mode' field (0-4)"})
            return
        mode = max(0, min(4, int(mode)))
        names = ["auto", "left-to-right", "right-to-left", "bounce", "no-scroll"]
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return
        ok, resp = self.device.send_command(f"scrollmode {mode}", wait=1.0)
        self._send_json(200 if ok else 500, {
            "command": "scrollmode",
            "mode": mode,
            "name": names[mode],
            "success": ok,
            "response": resp,
        })

    def _handle_luxrange(self, body):
        """POST /api/v1/luxrange — set auto-brightness min/max (0-255).

        {"min": 30, "max": 200}
        """
        mn = body.get("min", 30)
        mx = body.get("max", 200)
        mn = max(0, min(255, int(mn)))
        mx = max(mn, min(255, int(mx)))
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return
        ok, resp = self.device.send_command(f"luxrange {mn} {mx}", wait=1.0)
        self._send_json(200 if ok else 500, {
            "command": "luxrange",
            "min": mn,
            "max": mx,
            "success": ok,
            "response": resp,
        })

    def _handle_add_widget(self, body):
        """POST /api/v1/widget — add a widget to the carousel.

        Body: {"type": "clock"} or {"type": "text", "text": "hi"} or
              {"type": "metric", "value": "99%", "trend": "up", "icon": "cpu"}
        """
        wtype = body.get("type", "")
        if wtype not in ("clock", "text", "metric", "notification"):
            self._send_json(400, {
                "error": "Invalid or missing 'type'. "
                         "Must be: clock, text, metric, notification",
            })
            return

        content = {}
        if wtype == "text":
            content["text"] = body.get("text", "")
        elif wtype == "metric":
            content["value"] = body.get("value", "")
            content["trend"] = body.get("trend", "")
            content["icon"] = body.get("icon", "")
        elif wtype == "notification":
            content["text"] = body.get("text", "")
            content["icon"] = body.get("icon", "")
            content["sound"] = body.get("sound", "")

        update_interval = body.get("update_interval")
        widget = self.carousel.add(wtype, content, update_interval)
        self._send_json(201, {"widget": widget})

    def _handle_list_widgets(self):
        """GET /api/v1/widgets — list all carousel widgets."""
        widgets = self.carousel.list_widgets()
        self._send_json(200, {
            "widgets": widgets,
            "count": len(widgets),
            "carousel_interval": self.carousel.interval,
        })

    def _handle_remove_widget(self, widget_id):
        """DELETE /api/v1/widget/{id} — remove a widget by ID."""
        removed = self.carousel.remove(widget_id)
        if removed:
            self._send_json(200, {
                "removed": widget_id,
                "remaining": len(self.carousel.list_widgets()),
            })
        else:
            self._send_json(404, {"error": f"Widget {widget_id} not found"})

    # -- Weather endpoints ----------------------------------------------------

    def _handle_get_weather(self):
        """GET /api/v1/weather — return current cached weather data."""
        if self.weather_fetcher is None:
            self._send_json(503, {"error": "Weather fetcher not initialized"})
            return
        cached = self.weather_fetcher.cached_weather
        if not cached:
            self._send_json(200, {
                "status": "no data yet",
                "message": "Weather data will be available after first fetch",
            })
            return
        self._send_json(200, cached)

    def _handle_set_weather(self, body):
        """POST /api/v1/weather — set weather location (lat/lon)."""
        if self.weather_fetcher is None:
            self._send_json(503, {"error": "Weather fetcher not initialized"})
            return
        lat = body.get("lat") or body.get("latitude")
        lon = body.get("lon") or body.get("longitude")
        if lat is None or lon is None:
            self._send_json(400, {
                "error": "Missing 'lat'/'lon' (or 'latitude'/'longitude') fields",
            })
            return
        try:
            lat = float(lat)
            lon = float(lon)
        except (ValueError, TypeError):
            self._send_json(400, {"error": "lat/lon must be numeric"})
            return
        self.weather_fetcher.set_location(lat, lon)
        self._send_json(200, {
            "success": True,
            "latitude": lat,
            "longitude": lon,
            "message": "Location updated; weather will refresh shortly",
        })

    # -- Carousel endpoints ---------------------------------------------------

    def _handle_get_carousel(self):
        """GET /api/v1/carousel — return carousel status."""
        widgets = self.carousel.list_widgets()
        self._send_json(200, {
            "running": self.carousel._running,
            "interval": self.carousel.interval,
            "widget_count": len(widgets),
            "current_index": self.carousel._current_index,
        })

    def _handle_post_carousel(self, body):
        """POST /api/v1/carousel — send carousel command to device."""
        if not self.device.ensure_connected():
            self._send_json(503, {"error": "Device not connected"})
            return
        ok, resp = self.device.send_command("carousel", wait=1.0)
        self._send_json(200 if ok else 500, {
            "command": "carousel",
            "success": ok,
            "response": resp,
        })

    # -- Metrics endpoints ----------------------------------------------------

    def _handle_list_metrics(self):
        """GET /api/v1/metrics — list all named metrics."""
        metrics = self.metrics_manager.list_metrics()
        self._send_json(200, {
            "metrics": metrics,
            "count": len(metrics),
        })

    def _handle_remove_metric(self, name):
        """DELETE /api/v1/metric/{name} — remove a metric by name."""
        name = unquote(name)  # URL-decode (spaces, special chars)
        removed = self.metrics_manager.remove(name)
        if removed:
            self._send_json(200, {
                "removed": name,
                "remaining": len(self.metrics_manager.list_metrics()),
            })
        else:
            self._send_json(404, {"error": f"Metric '{name}' not found"})

    def _handle_metrics_push(self):
        """POST /api/v1/metrics/push — push all metrics to device."""
        metrics = self.metrics_manager.list_metrics()
        if not metrics:
            self._send_json(200, {
                "success": True,
                "message": "No metrics to push",
                "count": 0,
            })
            return
        ok = self.metrics_manager.push_to_device()
        self._send_json(200 if ok else 500, {
            "success": ok,
            "count": len(metrics),
            "metrics": metrics,
        })

    def _handle_metrics_webhook(self, body):
        """POST /api/v1/metrics/webhook — simplified webhook for Zapier/IFTTT.

        Accepts: {"metric": "ARR", "value": "$2.4M", "trend": "up"}
        The "metric" field is the name; "icon" is optional.
        """
        name = body.get("metric", "")
        value = body.get("value", "")
        if not name or not value:
            self._send_json(400, {
                "error": "Missing required fields: 'metric' and 'value'",
            })
            return
        icon = body.get("icon", "")
        trend = body.get("trend", "")
        self.metrics_manager.set(name, value, icon=icon, trend=trend)
        self._send_json(200, {
            "success": True,
            "metric": {"name": name, "value": value, "icon": icon, "trend": trend},
        })

    # -- Helpers --------------------------------------------------------------

    def _read_body(self):
        """Read and parse JSON request body. Returns dict (empty on error)."""
        content_length = int(self.headers.get("Content-Length", 0))
        if content_length == 0:
            return {}
        try:
            raw = self.rfile.read(content_length)
            return json.loads(raw)
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            _log(f"JSON parse error: {exc}")
            return {}

    def _send_json(self, status, data):
        """Send a JSON HTTP response."""
        body = json.dumps(data, indent=2, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        # CORS headers for browser-based clients
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods",
                         "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        """Handle CORS preflight requests."""
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods",
                         "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------

def _log(msg):
    """Print a timestamped log message to the console."""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {msg}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    _log("=" * 50)
    _log("  LaMetric Time Recovery — REST API Daemon")
    _log("=" * 50)

    # --- Device ---
    device = DeviceManager()
    device.connect()  # try once at startup
    device.start_reconnect_loop()

    # --- Carousel ---
    carousel = WidgetCarousel(device)

    # --- Weather ---
    weather_fetcher = WeatherFetcher(device)
    weather_fetcher.start()

    # --- Metrics ---
    metrics_manager = MetricsManager(device)

    # --- HTTP Server ---
    LaMetricHandler.device = device
    LaMetricHandler.carousel = carousel
    LaMetricHandler.weather_fetcher = weather_fetcher
    LaMetricHandler.metrics_manager = metrics_manager

    server = HTTPServer((HTTP_HOST, HTTP_PORT), LaMetricHandler)
    _log(f"HTTP server listening on http://{HTTP_HOST}:{HTTP_PORT}")
    _log("")
    _log("Endpoints:")
    _log("  GET  /api/v1/info              Device info")
    _log("  POST /api/v1/display           Show text")
    _log("  POST /api/v1/scroll            Scroll text")
    _log("  POST /api/v1/notification      Push notification (notify <icon> <text>)")
    _log("  POST /api/v1/metric            Add/update named metric or one-shot display")
    _log("  GET  /api/v1/metrics           List all named metrics")
    _log("  DELETE /api/v1/metric/{name}   Remove a named metric")
    _log("  POST /api/v1/metrics/push      Push all metrics to device")
    _log("  POST /api/v1/metrics/webhook   Webhook for Zapier/IFTTT")
    _log("  GET  /api/v1/clock             Sync time (carousel handles display)")
    _log("  POST /api/v1/bright            Set brightness")
    _log("  POST /api/v1/widget            Add carousel widget")
    _log("  GET  /api/v1/widgets           List widgets")
    _log("  DELETE /api/v1/widget/{id}     Remove widget")
    _log("  GET  /api/v1/weather           Current weather data")
    _log("  POST /api/v1/weather           Set location: {\"lat\": .., \"lon\": ..}")
    _log("  GET  /api/v1/carousel          Carousel status")
    _log("  POST /api/v1/carousel          Start/restart device carousel")
    _log("")
    _log("Press Ctrl+C to stop.")
    _log("")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        _log("Shutting down...")
        server.shutdown()
        weather_fetcher.stop()
        device.stop()
        _log("Goodbye.")


if __name__ == "__main__":
    main()
