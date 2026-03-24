#!/usr/bin/env python3
"""LaMetric Time Weather Widget — fetches weather and pushes to the device via serial.

Uses the free Open-Meteo API (no API key required) for your configured location.
Sends weather icon + temperature to the LaMetric Time display.

Usage:
    python weather_widget.py              # One-shot: fetch weather and display
    python weather_widget.py --loop 300   # Update every 300 seconds (5 minutes)
    python weather_widget.py --port COM4  # Use a different serial port

The device command sent is:
    icontext <icon> <temp>F
    e.g. "icontext sun 52F"
"""

import argparse
import json
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required.  Install with:  pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

OPEN_METEO_URL = (
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=47.6062&longitude=-122.3321&current_weather=true"
)

DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200
SERIAL_TIMEOUT = 2          # seconds
COMMAND_WAIT = 2.0           # seconds to wait for device response
HTTP_TIMEOUT = 10            # seconds for API request


# ---------------------------------------------------------------------------
# WMO Weather Code Mapping
# ---------------------------------------------------------------------------

def wmo_to_icon(code):
    """Map a WMO weather code to a LaMetric icon name.

    WMO weather interpretation codes:
        0     - Clear sky
        1     - Mainly clear
        2     - Partly cloudy
        3     - Overcast
        45,48 - Fog / depositing rime fog
        51-55 - Drizzle (light/moderate/dense)
        56-57 - Freezing drizzle
        61-65 - Rain (slight/moderate/heavy)
        66-67 - Freezing rain
        71-75 - Snow fall (slight/moderate/heavy)
        77    - Snow grains
        80-82 - Rain showers (slight/moderate/violent)
        85-86 - Snow showers
        95    - Thunderstorm (slight/moderate)
        96,99 - Thunderstorm with hail
    """
    if code <= 1:
        return "sun"
    elif code <= 3:
        return "cloud"
    elif 45 <= code <= 48:
        return "cloud"
    elif 51 <= code <= 67:
        return "rain"
    elif 71 <= code <= 77:
        return "snow"
    elif 80 <= code <= 82:
        return "rain"
    elif 85 <= code <= 86:
        return "snow"
    elif 95 <= code <= 99:
        return "thunder"
    else:
        return "cloud"  # fallback for unknown codes


def wmo_to_description(code):
    """Return a human-readable description for a WMO weather code."""
    descriptions = {
        0: "Clear sky",
        1: "Mainly clear",
        2: "Partly cloudy",
        3: "Overcast",
        45: "Fog",
        48: "Rime fog",
        51: "Light drizzle",
        53: "Moderate drizzle",
        55: "Dense drizzle",
        56: "Light freezing drizzle",
        57: "Dense freezing drizzle",
        61: "Slight rain",
        63: "Moderate rain",
        65: "Heavy rain",
        66: "Light freezing rain",
        67: "Heavy freezing rain",
        71: "Slight snow",
        73: "Moderate snow",
        75: "Heavy snow",
        77: "Snow grains",
        80: "Slight showers",
        81: "Moderate showers",
        82: "Violent showers",
        85: "Slight snow showers",
        86: "Heavy snow showers",
        95: "Thunderstorm",
        96: "Thunderstorm + hail",
        99: "Thunderstorm + heavy hail",
    }
    return descriptions.get(code, f"Unknown ({code})")


def celsius_to_fahrenheit(c):
    """Convert Celsius to Fahrenheit, rounded to nearest integer."""
    return round(c * 9.0 / 5.0 + 32)


# ---------------------------------------------------------------------------
# Weather Fetching
# ---------------------------------------------------------------------------

def fetch_weather():
    """Fetch current weather from Open-Meteo API.

    Returns a dict with keys: temp_f, icon, description, wind_speed, wind_unit,
    or None on failure.
    """
    try:
        req = urllib.request.Request(
            OPEN_METEO_URL,
            headers={"User-Agent": "LaMetricWeatherWidget/1.0"},
        )
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
            data = json.loads(resp.read().decode("utf-8"))

        current = data["current_weather"]
        temp_c = current["temperature"]
        weather_code = int(current["weathercode"])
        wind_speed = current["windspeed"]

        temp_f = celsius_to_fahrenheit(temp_c)
        icon = wmo_to_icon(weather_code)
        description = wmo_to_description(weather_code)

        return {
            "temp_c": temp_c,
            "temp_f": temp_f,
            "icon": icon,
            "weather_code": weather_code,
            "description": description,
            "wind_speed": wind_speed,
            "wind_unit": "km/h",
        }

    except urllib.error.URLError as exc:
        _log(f"Network error: {exc.reason}")
        return None
    except (json.JSONDecodeError, KeyError, TypeError) as exc:
        _log(f"API parse error: {exc}")
        return None
    except Exception as exc:
        _log(f"Unexpected error fetching weather: {exc}")
        return None


# ---------------------------------------------------------------------------
# Serial Communication
# ---------------------------------------------------------------------------

def send_to_device(port_name, command):
    """Open serial port, send command + \\r, read response, close port.

    Returns (success: bool, response: str).
    """
    ser = None
    try:
        ser = serial.Serial(port_name, DEFAULT_BAUD, timeout=SERIAL_TIMEOUT)
        time.sleep(0.3)
        # Flush stale data
        if ser.in_waiting:
            ser.read(ser.in_waiting)

        ser.write((command + "\r").encode())
        time.sleep(COMMAND_WAIT)
        raw = ser.read(ser.in_waiting)
        response = raw.decode(errors="ignore").strip()
        return True, response

    except serial.SerialException as exc:
        _log(f"Serial error ({port_name}): {exc}")
        return False, str(exc)
    except Exception as exc:
        _log(f"Unexpected serial error: {exc}")
        return False, str(exc)
    finally:
        if ser and ser.is_open:
            try:
                ser.close()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Main Logic
# ---------------------------------------------------------------------------

def update_weather(port_name):
    """Fetch weather and push to LaMetric. Returns True on success."""
    weather = fetch_weather()
    if weather is None:
        _log("Failed to fetch weather data.")
        return False

    # Print weather info to console
    _log(f"Weather: {weather['description']}")
    _log(f"  Temperature: {weather['temp_f']}F ({weather['temp_c']}C)")
    _log(f"  Wind: {weather['wind_speed']} {weather['wind_unit']}")
    _log(f"  Icon: {weather['icon']} (WMO code {weather['weather_code']})")

    # Build the device command
    command = f"icontext {weather['icon']} {weather['temp_f']}F"
    _log(f"  Sending: {command}")

    ok, resp = send_to_device(port_name, command)
    if ok:
        _log(f"  Device response: {resp if resp else '(none)'}")
    else:
        _log(f"  Device send failed: {resp}")

    return ok


# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------

def _log(msg):
    """Print a timestamped log message."""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {msg}")


# ---------------------------------------------------------------------------
# Entry Point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="LaMetric Time Weather Widget — fetch weather and display on device.",
    )
    parser.add_argument(
        "--loop",
        type=int,
        default=0,
        metavar="SECONDS",
        help="Run continuously, updating every SECONDS seconds (e.g. --loop 300 for 5 min).",
    )
    parser.add_argument(
        "--port",
        type=str,
        default=DEFAULT_PORT,
        help=f"Serial port name (default: {DEFAULT_PORT}).",
    )
    args = parser.parse_args()

    _log("LaMetric Weather Widget")
    _log(f"  Location: Your location (configure lat/lon)")
    _log(f"  Serial port: {args.port}")
    if args.loop > 0:
        _log(f"  Update interval: {args.loop}s")
    _log("")

    if args.loop > 0:
        # Loop mode: update at the configured interval
        _log("Starting weather loop (Ctrl+C to stop)...")
        try:
            while True:
                update_weather(args.port)
                _log(f"Next update in {args.loop}s...\n")
                time.sleep(args.loop)
        except KeyboardInterrupt:
            _log("\nStopped by user.")
    else:
        # One-shot mode
        ok = update_weather(args.port)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
