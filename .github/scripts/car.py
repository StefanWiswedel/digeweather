"""Renault ZOE remote climate control (unofficial MyRenault API via renault-api).

Usage:
  python car.py check   # log in, print vehicle and battery status (changes nothing)
  python car.py start   # start pre-conditioning (climate) now
  python car.py status  # write battery status to data/car.json (for the phone widget)
  python car.py pair    # interactive, run on your own computer: handles the
                        # emailed two-factor code and prints RENAULT_DEVICE_IDS

Environment:
  RENAULT_EMAIL, RENAULT_PASSWORD   MyRenault login (required)
  RENAULT_DEVICE_IDS                JSON from `pair`, only if your account uses 2FA
  RENAULT_VIN                       optional; defaults to the first car on the account
  RENAULT_LOCALE                    optional; default da_DK
  RENAULT_TEMPERATURE               optional; target cabin temperature, default 21
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
            # Deliberately no location: the repo is public.
            battery = await vehicle.get_battery_status()
            status = {
                "timestamp": battery.timestamp,
                "batteryLevel": battery.batteryLevel,
                "range": battery.batteryAutonomy,
                "plugged": bool(battery.plugStatus),
                "charging": (battery.chargingStatus or 0) > 0,
                "chargingRemainingMinutes": battery.chargingRemainingTime,
            }
            with open(os.path.join("data", "car.json"), "w") as f:
                json.dump(status, f)
            print(json.dumps(status))
        elif mode == "start":
            temperature = float(env("RENAULT_TEMPERATURE", "21"))
            # Retry a couple of times; the Renault API is occasionally flaky.
            for attempt in range(1, 4):
                try:
                    await vehicle.set_ac_start(temperature)
                    print(f"Pre-conditioning started (target {temperature} °C).")
                    return
                except Exception as e:  # noqa: BLE001
                    print(f"Attempt {attempt} failed: {e}")
                    await asyncio.sleep(10)
            sys.exit("Could not start pre-conditioning.")
        else:
            sys.exit(f"Unknown mode {mode!r}; use check, status, start or pair")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1] if len(sys.argv) > 1 else "check"))
