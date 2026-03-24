@echo off
setlocal EnableDelayedExpansion

:: ============================================================================
:: LaMetric Time Recovery — One-Click Boot Script
:: ============================================================================
:: Boots the LaMetric Time from FEL mode via USB, waits for the serial console,
:: syncs the clock, starts the widget carousel, and optionally starts the REST API daemon.
::
:: Place this script alongside sunxi-fel.exe, the kernel, DTB, and u-boot.
:: Run from any directory — all paths are relative to the script location.
:: ============================================================================

:: -- Resolve script directory (works even from a different CWD) ---------------
set "SCRIPTDIR=%~dp0"
:: Remove trailing backslash for cleaner display
if "%SCRIPTDIR:~-1%"=="\" set "SCRIPTDIR=%SCRIPTDIR:~0,-1%"

:: -- File names (edit here if your filenames differ) --------------------------
set "FEL=%SCRIPTDIR%\sunxi-fel.exe"
set "UBOOT=%SCRIPTDIR%\u-boot-AUTOBOOT.bin"
set "KERNEL=%SCRIPTDIR%\lametric_515_wifi.uImage"
set "DTB=%SCRIPTDIR%\sun5i-a13-lametric-515-padded.dtb"
set "DAEMON=%SCRIPTDIR%\lametric_daemon.py"

:: -- Display header -----------------------------------------------------------
echo.
echo  ============================================================
echo   LaMetric Time Recovery — One-Click Boot
echo  ============================================================
echo.
echo  Script directory: %SCRIPTDIR%
echo.

:: ============================================================================
:: STEP 1 — Verify required files exist
:: ============================================================================
echo  [1/6] Checking required files...

set "MISSING=0"

if not exist "%FEL%" (
    echo        MISSING: sunxi-fel.exe
    set "MISSING=1"
)
if not exist "%UBOOT%" (
    echo        MISSING: u-boot-AUTOBOOT.bin
    set "MISSING=1"
)
if not exist "%KERNEL%" (
    echo        MISSING: lametric_515_wifi.uImage
    set "MISSING=1"
)
if not exist "%DTB%" (
    echo        MISSING: sun5i-a13-lametric-515-padded.dtb
    set "MISSING=1"
)

if "!MISSING!"=="1" (
    echo.
    echo  ERROR: One or more required files are missing from:
    echo         %SCRIPTDIR%
    echo.
    echo  See SETUP_OTHER_PC.md for the full list of files needed.
    goto :fail
)

echo        All files found.
echo.

:: ============================================================================
:: STEP 2 — Check for FEL device
:: ============================================================================
echo  [2/6] Checking for device in FEL mode...

:: Try up to 30 times (about 60 seconds) before giving up
set "FEL_FOUND=0"
set "FEL_RETRIES=0"
set "FEL_MAX=30"

:fel_loop
"%FEL%" ver >nul 2>&1
if !ERRORLEVEL! equ 0 (
    set "FEL_FOUND=1"
    goto :fel_done
)

if "!FEL_RETRIES!"=="0" (
    echo.
    echo        Device NOT detected in FEL mode.
    echo.
    echo        To enter FEL mode:
    echo          1. Unplug the LaMetric from USB
    echo          2. Hold the round button on top of the device
    echo          3. While holding, plug the USB cable back in
    echo          4. Release the button after 2 seconds
    echo.
    echo        Waiting for FEL device (timeout: 60 seconds^)...
)

set /a "FEL_RETRIES+=1"
if !FEL_RETRIES! geq !FEL_MAX! goto :fel_done

:: Print a dot every 5 attempts to show progress
set /a "DOT_CHECK=FEL_RETRIES %% 5"
if "!DOT_CHECK!"=="0" (
    <nul set /p "=."
)

timeout /t 2 /nobreak >nul
goto :fel_loop

:fel_done
if "!FEL_FOUND!"=="0" (
    echo.
    echo  ERROR: Timed out waiting for FEL device.
    echo         Make sure the USB driver is set to libusbK (use Zadig^).
    echo         See SETUP_OTHER_PC.md for driver instructions.
    goto :fail
)

echo        FEL device detected!
echo.

:: Show device info
echo  --------------------------------------------------------
"%FEL%" ver
echo  --------------------------------------------------------
echo.

:: ============================================================================
:: STEP 3 — Load u-boot, kernel, and DTB
:: ============================================================================
echo  [3/6] Loading firmware via FEL...
echo.
echo        u-boot : u-boot-AUTOBOOT.bin
echo        kernel : lametric_515_wifi.uImage
echo        DTB    : sun5i-a13-lametric-515-padded.dtb
echo.

"%FEL%" uboot "%UBOOT%" write 0x42000000 "%KERNEL%" write 0x43000000 "%DTB%"
if !ERRORLEVEL! neq 0 (
    echo.
    echo  ERROR: FEL upload failed.
    echo         Try unplugging and re-entering FEL mode.
    goto :fail
)

echo.
echo        Firmware loaded successfully! Device is booting...
echo.

:: ============================================================================
:: STEP 4 — Wait for COM port to appear
:: ============================================================================
echo  [4/6] Waiting for USB serial port (COM port^)...
echo        The kernel takes ~10 seconds to boot and enable USB gadget serial.
echo.

set "COM_PORT="
set "COM_RETRIES=0"
set "COM_MAX=30"

:com_loop
:: Scan for USB Serial Device in the device list
for /f "tokens=1,* delims=(" %%a in ('mode 2^>nul ^| findstr /i "COM"') do (
    set "LINE=%%a"
)

:: Use PowerShell for reliable COM port detection (matches the daemon's approach)
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "Get-WmiObject Win32_PnPEntity | Where-Object { $_.Name -match 'USB Serial Device.*\(COM\d+\)' -or $_.Name -match 'USB-SERIAL\|CDC\|gadget' } | ForEach-Object { if ($_.Name -match '\(COM(\d+)\)') { 'COM' + $Matches[1] } }" 2^>nul`) do (
    set "COM_PORT=%%p"
    goto :com_found
)

:: Fallback: just check if COM3 exists (the most common port)
mode COM3 >nul 2>&1
if !ERRORLEVEL! equ 0 (
    set "COM_PORT=COM3"
    goto :com_found
)

set /a "COM_RETRIES+=1"
if !COM_RETRIES! geq !COM_MAX! goto :com_timeout

:: Progress dots
set /a "DOT_CHECK=COM_RETRIES %% 3"
if "!DOT_CHECK!"=="0" (
    <nul set /p "=."
)

timeout /t 2 /nobreak >nul
goto :com_loop

:com_timeout
echo.
echo  WARNING: Timed out waiting for COM port (60 seconds^).
echo           The device may still be booting. You can:
echo             - Check Device Manager for a new COM port
echo             - Run: python lametric_daemon.py
echo.
goto :skip_serial

:com_found
echo.
echo        Found serial port: !COM_PORT!
echo.

:: Brief pause to let the device finish init
timeout /t 3 /nobreak >nul

:: ============================================================================
:: STEP 5 — Sync time and start carousel
:: ============================================================================
echo  [5/6] Syncing time and starting carousel on !COM_PORT!...
echo.

:: Build the time sync command: "time HH:MM:SS"
for /f "tokens=1-3 delims=:." %%a in ("%TIME: =0%") do (
    set "TIMESTR=%%a:%%b:%%c"
)

:: Send commands via PowerShell (more reliable than raw mode/echo for serial)
powershell -NoProfile -Command ^
    "$port = New-Object System.IO.Ports.SerialPort('!COM_PORT!', 115200, 'None', 8, 'One');" ^
    "$port.Open();" ^
    "Start-Sleep -Milliseconds 500;" ^
    "$port.WriteLine('time !TIMESTR!');" ^
    "Start-Sleep -Milliseconds 500;" ^
    "$port.WriteLine('carousel');" ^
    "Start-Sleep -Milliseconds 500;" ^
    "$port.Close();" ^
    "Write-Host '        Time synced: !TIMESTR!';" ^
    "Write-Host '        Carousel mode started.'"

if !ERRORLEVEL! neq 0 (
    echo.
    echo  WARNING: Could not send commands to !COM_PORT!.
    echo           The port may be in use or the device is still initializing.
    echo           Try: python lametric_daemon.py
)

echo.

:skip_serial

:: ============================================================================
:: STEP 6 — Offer to start the daemon
:: ============================================================================
echo  [6/6] Boot complete!
echo.
echo  ============================================================
echo   LaMetric Time is RUNNING
echo  ============================================================
echo.

if exist "%DAEMON%" (
    echo  The REST API daemon provides a web interface on port 8080.
    echo  To start it, run:
    echo.
    echo      python "%DAEMON%"
    echo.
    choice /c YN /m "  Start the REST API daemon now?"
    if !ERRORLEVEL! equ 1 (
        echo.
        echo  Starting daemon...
        echo.
        python "%DAEMON%"
    )
) else (
    echo  (lametric_daemon.py not found — skipping daemon prompt^)
)

echo.
echo  Done. Press any key to exit.
pause >nul
exit /b 0

:: ============================================================================
:: Error exit
:: ============================================================================
:fail
echo.
echo  Boot aborted. Press any key to exit.
pause >nul
exit /b 1
