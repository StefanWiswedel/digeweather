"""Renault ZOE remote climate control (unofficial MyRenault API via renault-api).

Usage:
  python car.py check   # log in, print vehicle and battery status (changes nothing)
  python car.py start   # start pre-conditioning (climate) now; records the outcome
                        # in data/preheat.json for the phone notification
  python car.py status  # write battery status to data/car.json (for the phone widget)
  python car.py pair    # interactive, run on your own computer: handles the
                        # emailed two-factor code and prints RENAULT_DEVICE_IDS

Environment:
  RENAULT_EMAIL, RENAULT_PASSWORD   MyRenault login (required)
  RENAULT_DEVICE_IDS                JSON from `pair`, only if your account uses 2FA
  RENAULT_VIN                       optional; defaults to the first car on the account
  RENAULT_LOCALE                    optional; default da_DK
  RENAULT_TEMPERATURE               optional; target cabin temperature, default 21
  RENAULT_MIN_BATTERY               optional; skip pre-conditioning at or below this %, default 20
"""

import asyncio
import json
import os
import sys

import aiohttp
from renault_api.gigya.exceptions import PendingTwoFactorAuthenticationException
from renault_api.renault_client import RenaultClient


def env(name, default=None):
    value = os.environ.get(name, "").strip()
    if not value and default is None:
        sys.exit(f"Missing environment variable {name}")
    return value or default


async def login(websession, interactive=False):
    client = RenaultClient(websession=websession, locale=env("RENAULT_LOCALE", "da_DK"))
    device_ids = os.environ.get("RENAULT_DEVICE_IDS", "").strip()
    if device_ids:
        client.session.set_device_ids(json.loads(device_ids))
    try:
        await client.session.login(env("RENAULT_EMAIL"), env("RENAULT_PASSWORD"))
    except PendingTwoFactorAuthenticationException:
        if not interactive:
            sys.exit(
                "Renault asked for a two-factor code. Run `python car.py pair` on your "
                "own computer and save the printed value as the RENAULT_DEVICE_IDS secret."
            )
        await client.session.request_two_factor_auth_code()
        code = input("Enter the code Renault just emailed you: ").strip()
        await client.session.complete_two_factor_auth(code)
    return client


async def find_vehicle(client):
    wanted_vin = os.environ.get("RENAULT_VIN", "").strip()
    for account in await client.get_api_accounts():
        vehicles = await account.get_vehicles()
        for link in vehicles.vehicleLinks or []:
            if link.vin and (not wanted_vin or link.vin == wanted_vin):
                return await account.get_api_vehicle(link.vin)
    sys.exit("No vehicle found on this MyRenault account" + (f" with VIN {wanted_vin}" if wanted_vin else ""))


def record_preheat(result, battery=None):
    """Write today's pre-heat outcome for the phone app (sent / skipped_battery / failed)."""
    low = os.environ.get("PREHEAT_LOW", "")
    date = os.environ.get("PREHEAT_DATE", "")
    path = os.path.join("data", "preheat.json")
    attempts = 1
    try:
        with open(path) as f:
            prev = json.load(f)
        if prev.get("date") == date:
            attempts = int(prev.get("attempts", 1)) + 1
    except (OSError, ValueError):
        pass
    record = {
        "date": date,
        "time": os.environ.get("PREHEAT_TIME", ""),
        "result": result,
        "overnightLow": float(low) if low else None,
        "battery": battery,
        "attempts": attempts,
    }
    with open(path, "w") as f:
        json.dump(record, f)
    print("Recorded:", json.dumps(record))


async def main(mode):
    async with aiohttp.ClientSession() as websession:
        client = await login(websession, interactive=(mode == "pair"))

        if mode == "pair":
            print("\nLogged in. Save this as the GitHub secret RENAULT_DEVICE_IDS:\n")
            print(json.dumps(client.session.device_ids))
            return

        vehicle = await find_vehicle(client)
        vin = vehicle.vin
        print(f"Vehicle: ...{vin[-6:]}")

        if mode == "check":
            battery = await vehicle.get_battery_status()
            print(f"Battery: {battery.batteryLevel}%  range: {battery.batteryAutonomy} km  "
                  f"plugged in: {bool(battery.plugStatus)}  (as of {battery.timestamp})")
            print("Login and car access OK. Nothing was changed.")
        elif mode == "status":
            # Deliberately no location or mileage: the repo is public / not wanted.
            path = os.path.join("data", "car.json")
            try:
                with open(path) as f:
                    previous = json.load(f)
            except (OSError, ValueError):
                previous = {}

            battery = await vehicle.get_battery_status()
            status = {
                "timestamp": battery.timestamp,
                "batteryLevel": battery.batteryLevel,
                "range": battery.batteryAutonomy,
                "plugged": bool(battery.plugStatus),
                "charging": (battery.chargingStatus or 0) > 0,
                "chargingRemainingMinutes": battery.chargingRemainingTime,
            }

            # Extra details only change when the car reports in, so only ask Renault for
            # them when the battery report is new (keeps API calls down).
            if battery.timestamp and battery.timestamp == previous.get("timestamp") and "extrasFetched" in previous:
                for k in ("climate", "tyres", "extrasFetched"):
                    if k in previous:
                        status[k] = previous[k]
            else:
                try:
                    hvac = await vehicle.get_hvac_status()
                    status["climate"] = {
                        "status": hvac.hvacStatus,
                        "outsideTemp": hvac.externalTemperature,
                        "insideTemp": hvac.internalTemperature,
                        "minBattery": hvac.socThreshold,
                        "updated": hvac.lastUpdateTime,
                    }
                except Exception as e:  # noqa: BLE001
                    print(f"Climate status not available: {e}")
                try:
                    p = await vehicle.get_tyre_pressure()
                    status["tyres"] = {"fl": p.flPressure, "fr": p.frPressure,
                                       "rl": p.rlPressure, "rr": p.rrPressure}
                except Exception as e:  # noqa: BLE001
                    print(f"Tyre pressure not available: {e}")
                status["extrasFetched"] = True

            with open(path, "w") as f:
                json.dump(status, f)
            print(json.dumps(status))
        elif mode == "start":
            # Don't drain a nearly empty battery: only pre-condition above MIN_BATTERY %.
            min_battery = int(env("RENAULT_MIN_BATTERY", "20"))
            battery = await vehicle.get_battery_status()
            if battery.batteryLevel is not None and battery.batteryLevel <= min_battery:
                print(f"Battery {battery.batteryLevel}% (<= {min_battery}%): not pre-conditioning.")
                record_preheat("skipped_battery", battery.batteryLevel)
                return
            temperature = float(env("RENAULT_TEMPERATURE", "21"))
            # Retry a couple of times; the Renault API is occasionally flaky.
            for attempt in range(1, 4):
                try:
                    await vehicle.set_ac_start(temperature)
                    print(f"Pre-conditioning started (target {temperature} °C).")
                    record_preheat("sent", battery.batteryLevel)
                    return
                except Exception as e:  # noqa: BLE001
                    print(f"Attempt {attempt} failed: {e}")
                    await asyncio.sleep(10)
            record_preheat("failed", battery.batteryLevel)
            sys.exit("Could not start pre-conditioning.")
        else:
            sys.exit(f"Unknown mode {mode!r}; use check, status, start or pair")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    try:
        asyncio.run(main(mode))
    except Exception:
        # e.g. Renault login refused: still tell the phone it failed.
        if mode == "start":
            record_preheat("failed")
        raise
