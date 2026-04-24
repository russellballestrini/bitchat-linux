#!/usr/bin/env python3
"""
fake_peer.py — a minimal BitChat peer for functional tests.

Registers a BlueZ GATT server that advertises the testnet service UUID
and the BitChat characteristic. When a central subscribes (StartNotify),
pushes a canned frame (built by tests/build/make_fixture) once, then
sits idle until killed or the --timeout expires.

Uses dbus-python so there are no extra pip installs — python3-dbus
and python3-gi are both in the Ubuntu base.

Runs only as long as test needs it. Requires a BlueZ 5 adapter and
permissions to register GATT applications + LE advertisements on it.

This is free and unencumbered software released into the public domain.
"""

import argparse
import signal
import subprocess
import sys

import dbus
import dbus.exceptions
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

BLUEZ                        = 'org.bluez'
DBUS_OM_IFACE                = 'org.freedesktop.DBus.ObjectManager'
DBUS_PROP_IFACE              = 'org.freedesktop.DBus.Properties'
GATT_MANAGER_IFACE           = 'org.bluez.GattManager1'
GATT_SERVICE_IFACE           = 'org.bluez.GattService1'
GATT_CHRC_IFACE              = 'org.bluez.GattCharacteristic1'
LE_ADV_MANAGER_IFACE         = 'org.bluez.LEAdvertisingManager1'
LE_ADV_IFACE                 = 'org.bluez.LEAdvertisement1'

SERVICE_UUID_TESTNET = 'f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5a'
CHAR_UUID            = 'a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d'

APP_PATH  = '/fox/bitchat/app0'
SVC_PATH  = APP_PATH + '/service0'
CHAR_PATH = SVC_PATH + '/char0'
ADV_PATH  = '/fox/bitchat/adv0'


def find_adapter(bus, explicit=None):
    om = dbus.Interface(bus.get_object(BLUEZ, '/'), DBUS_OM_IFACE)
    managed = om.GetManagedObjects()
    if explicit:
        ifaces = managed.get(explicit)
        if ifaces and GATT_MANAGER_IFACE in ifaces and LE_ADV_MANAGER_IFACE in ifaces:
            return explicit
        return None
    for path, ifaces in managed.items():
        if GATT_MANAGER_IFACE in ifaces and LE_ADV_MANAGER_IFACE in ifaces:
            return path
    return None


class Characteristic(dbus.service.Object):
    def __init__(self, bus, service, bytes_provider):
        self.bus = bus
        self.service_path = service.get_path()
        self.bytes_provider = bytes_provider
        self.notifying = False
        self.value = bytes_provider()
        dbus.service.Object.__init__(self, bus, CHAR_PATH)

    def get_path(self):
        return dbus.ObjectPath(CHAR_PATH)

    def properties(self):
        return {
            'UUID': CHAR_UUID,
            'Service': self.service_path,
            'Flags': dbus.Array(['notify', 'read', 'write', 'write-without-response'], signature='s'),
            'Notifying': dbus.Boolean(self.notifying),
        }

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != GATT_CHRC_IFACE:
            raise dbus.exceptions.DBusException('no such iface', name='org.bluez.Error.InvalidArgs')
        return self.properties()

    @dbus.service.signal(DBUS_PROP_IFACE, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='a{sv}', out_signature='ay')
    def ReadValue(self, options):
        return dbus.Array([dbus.Byte(b) for b in self.value], signature='y')

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='aya{sv}')
    def WriteValue(self, value, options):
        print(f'[fake-peer] got write: {len(value)} bytes', flush=True)

    @dbus.service.method(GATT_CHRC_IFACE)
    def StartNotify(self):
        if self.notifying:
            return
        self.notifying = True
        print('[fake-peer] subscriber attached — pushing frame', flush=True)
        GLib.timeout_add(250, self._emit_once)

    @dbus.service.method(GATT_CHRC_IFACE)
    def StopNotify(self):
        self.notifying = False

    def _emit_once(self):
        if not self.notifying:
            return False
        frame = self.bytes_provider()
        self.PropertiesChanged(
            GATT_CHRC_IFACE,
            {'Value': dbus.Array([dbus.Byte(b) for b in frame], signature='y')},
            []
        )
        print(f'[fake-peer] emitted {len(frame)}-byte frame', flush=True)
        return False


class Service(dbus.service.Object):
    def __init__(self, bus, bytes_provider):
        dbus.service.Object.__init__(self, bus, SVC_PATH)
        self.chrc = Characteristic(bus, self, bytes_provider)

    def get_path(self):
        return dbus.ObjectPath(SVC_PATH)

    def properties(self):
        return {
            'UUID': SERVICE_UUID_TESTNET,
            'Primary': dbus.Boolean(True),
            'Characteristics': dbus.Array([self.chrc.get_path()], signature='o'),
        }

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != GATT_SERVICE_IFACE:
            raise dbus.exceptions.DBusException('no such iface', name='org.bluez.Error.InvalidArgs')
        return self.properties()


class Application(dbus.service.Object):
    def __init__(self, bus, bytes_provider):
        dbus.service.Object.__init__(self, bus, APP_PATH)
        self.service = Service(bus, bytes_provider)

    @dbus.service.method(DBUS_OM_IFACE, out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        return {
            self.service.get_path(): {
                GATT_SERVICE_IFACE: self.service.properties(),
            },
            self.service.chrc.get_path(): {
                GATT_CHRC_IFACE: self.service.chrc.properties(),
            },
        }


class Advertisement(dbus.service.Object):
    def __init__(self, bus):
        dbus.service.Object.__init__(self, bus, ADV_PATH)

    def properties(self):
        return {
            'Type': 'peripheral',
            'ServiceUUIDs': dbus.Array([SERVICE_UUID_TESTNET], signature='s'),
            'LocalName': dbus.String('bitchat-fake'),
            'Includes': dbus.Array([], signature='s'),
        }

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != LE_ADV_IFACE:
            raise dbus.exceptions.DBusException('no such iface', name='org.bluez.Error.InvalidArgs')
        return self.properties()

    @dbus.service.method(LE_ADV_IFACE)
    def Release(self):
        print('[fake-peer] advertisement released', flush=True)


def load_fixture_bytes(name, fixture_bin):
    res = subprocess.run([fixture_bin, name], capture_output=True, check=True, text=True)
    return bytes.fromhex(res.stdout.strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='announce', help='fixture name for make_fixture')
    ap.add_argument('--fixture-bin', default='tests/build/make_fixture')
    ap.add_argument('--adapter', default=None,
                    help='BlueZ adapter path (e.g. /org/bluez/hci1); default auto-pick')
    ap.add_argument('--timeout', type=float, default=30.0, help='auto-exit seconds')
    args = ap.parse_args()

    frame = load_fixture_bytes(args.fixture, args.fixture_bin)
    print(f'[fake-peer] loaded fixture "{args.fixture}" ({len(frame)} bytes)', flush=True)

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()

    adapter_path = find_adapter(bus, explicit=args.adapter)
    if not adapter_path:
        which = args.adapter or '(any)'
        print(f'[fake-peer] no GATT-capable adapter at {which}', file=sys.stderr, flush=True)
        sys.exit(2)
    print(f'[fake-peer] adapter: {adapter_path}', flush=True)

    app = Application(bus, lambda: frame)
    adv = Advertisement(bus)
    adapter = bus.get_object(BLUEZ, adapter_path)
    gatt_mgr = dbus.Interface(adapter, GATT_MANAGER_IFACE)
    adv_mgr  = dbus.Interface(adapter, LE_ADV_MANAGER_IFACE)

    loop = GLib.MainLoop()

    def reg_err(what, err):
        print(f'[fake-peer] {what} register error: {err}', file=sys.stderr, flush=True)
        loop.quit()

    gatt_mgr.RegisterApplication(
        app._object_path, {},
        reply_handler=lambda: print('[fake-peer] GATT app registered', flush=True),
        error_handler=lambda e: reg_err('GATT', e),
    )
    adv_mgr.RegisterAdvertisement(
        adv._object_path, {},
        reply_handler=lambda: print('[fake-peer] advertisement registered', flush=True),
        error_handler=lambda e: reg_err('ADV', e),
    )

    def cleanup(*_):
        print('[fake-peer] shutting down', flush=True)
        try: adv_mgr.UnregisterAdvertisement(adv._object_path)
        except Exception: pass
        try: gatt_mgr.UnregisterApplication(app._object_path)
        except Exception: pass
        loop.quit()

    signal.signal(signal.SIGINT,  lambda *_: GLib.idle_add(cleanup))
    signal.signal(signal.SIGTERM, lambda *_: GLib.idle_add(cleanup))
    GLib.timeout_add(int(args.timeout * 1000), lambda: (cleanup(), False)[1])

    loop.run()


if __name__ == '__main__':
    main()
