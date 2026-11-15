#!/usr/bin/python3 -I
"""W202DS power mechanisms. Status is read-only; writes need root."""
from pathlib import Path
import errno
import fcntl
import json
import os
import re
import subprocess
import sys
import time


def selected(text):
    match = re.search(r'\[([^\]]+)\]', text or '')
    return match[1] if match else (text or '').strip()


class Controller:
    def __init__(self, sysfs=Path('/sys'), runner=None):
        self.sysfs = Path(sysfs)
        self.supplies = self.sysfs / 'class/power_supply'
        self.runner = runner or self.run

    @staticmethod
    def read(path):
        try:
            return path.read_text().strip()
        except OSError:
            return None

    def number(self, path):
        try:
            return int(self.read(path))
        except (ValueError, TypeError):
            return None

    def port(self):
        ports = [p for p in (self.sysfs / 'class/typec').glob('port*')
                 if (p / 'port_type').is_file()]
        if len(ports) > 1:
            raise ValueError('Ambiguous Type-C port selection')
        return ports[0] if ports else None

    def profiles(self, port):
        result = {}
        if port is None:
            return result
        # Local PD objects and the port have the same controller parent.
        def owner(path):
            parent = path.resolve().parent
            return parent.parent if parent.name in ('typec', 'usb_power_delivery') else parent
        for p in (self.sysfs / 'class/usb_power_delivery').glob('pd*'):
            if owner(p) != owner(port):
                continue
            caps = [cap for cap in (p / 'sink-capabilities').glob('*')
                    if re.fullmatch(r'\d+:[a-z_]+', cap.name) and cap.is_dir()]
            voltages = []
            for cap in caps:
                value = self.read(cap / 'voltage') or self.read(cap / 'maximum_voltage')
                match = re.fullmatch(r'(\d+)mV', value or '')
                if not match:
                    break
                voltages.append(int(match[1]))
            else:
                if voltages and min(voltages) >= 5000:
                    kind = 'normal' if max(voltages) == 5000 else 'fast'
                    if kind in result:
                        return {}  # Such a controller has no unambiguous toggle.
                    result[kind] = p.name
        return result

    def status(self):
        battery = self.supplies / 'w202-battery'
        usb = self.supplies / 'w202-usb-c'
        charger = self.supplies / 'w202-bq-main'
        port = self.port()
        profiles = self.profiles(port)
        fast_gate = self.number(charger / 'device/fast_charge')
        choice = None
        if set(profiles) == {'fast', 'normal'}:
            # Older controllers lack selectable profiles; don't probe their selector.
            choice = selected(self.read(port / 'select_usb_power_delivery'))
        fast = None
        if choice == profiles.get('fast') and choice is not None and fast_gate == 1:
            fast = True
        elif choice == profiles.get('normal') and choice is not None and fast_gate == 0:
            fast = False
        mode = None
        if choice and choice == profiles.get('normal') and fast_gate == 0:
            mode = 'pd5v'
        elif choice and choice == profiles.get('fast') and fast_gate in (0, 1):
            fixed = self.fixed_voltages(choice)
            mode, fast = ('pps' if fast_gate else 'pd9v' if fixed == {5000, 9000} else 'pd-fixed'), True
        sources = list(self.supplies.glob('tcpm-source-psy-*'))
        source = sources[0] if len(sources) == 1 else None
        online = self.number(usb / 'online')
        protocol = selected(self.read(usb / 'usb_type')) or 'Unknown'
        pd_online = self.number(source / 'online') if source else None
        if not online:
            protocol = 'Disconnected'
        elif pd_online == 2:
            protocol = 'PD PPS'
        elif protocol in ('PD', 'PD_PPS') and pd_online == 1:
            protocol = 'PD fixed'
        voltage = self.number(source / 'voltage_now') if source else None
        active_mode = ('pps' if protocol == 'PD PPS' else
                       'pd5v' if protocol == 'PD fixed' and voltage == 5000000 else
                       'pd9v' if protocol == 'PD fixed' and voltage == 9000000 else None)
        behaviour = selected(self.read(charger / 'charge_behaviour'))
        port_type = self.read(port / 'port_type') if port else None
        connected = bool(port and port.with_name(port.name + '-partner').exists())
        power_role = selected(self.read(port / 'power_role')) if port else ''
        vbus_uv = self.number(usb / 'voltage_now')
        requested_mode = 'auto' if mode == 'pps' else mode
        try:
            saved = json.loads(Path('/run/w202-power-mode.json').read_text())
            # User intent must not follow transient renegotiation or fallback.
            if saved.get('requested_mode') in ('auto', 'pps', 'pd5v', 'pd9v'):
                requested_mode = saved['requested_mode']
        except (OSError, ValueError, KeyError):
            pass
        return {
            'typec_connected': connected,
            'input_online': bool(online),
            'usb_output_active': connected and power_role == 'source' and
                                 vbus_uv is not None and vbus_uv >= 4000000,
            'requested_mode': requested_mode,
            'battery_percent': self.number(battery / 'capacity'),
            'battery_status': self.read(battery / 'status'),
            'battery_current_ua': self.number(battery / 'current_now'),
            'battery_voltage_uv': self.number(battery / 'voltage_now'),
            'battery_power_uw': self.number(battery / 'power_now'),
            'battery_temperature_decic': self.number(battery / 'temp'),
            'input_voltage_uv': self.number(usb / 'voltage_now'),
            # Read the USB sensor only, never TCPM's negotiated current or IINLIM.
            'input_current_ua': self.number(usb / 'current_now') if online and
                                power_role == 'sink' else None,
            'contract_current_ua': self.number(source / 'current_now') if source else None,
            'contract_voltage_uv': self.number(source / 'voltage_now') if source else None,
            'input_limit_ua': self.number(charger / 'input_current_limit'),
            'protocol': protocol,
            'separation': behaviour == 'inhibit-charge' if behaviour in ('auto', 'inhibit-charge') else None,
            'charge_enabled': self.number(charger / 'device/charge_enabled'),
            'fast_charge': fast,
            'charging_mode': mode,
            'active_mode': active_mode,
            'pps_allowed': bool(fast_gate) if fast_gate in (0, 1) else None,
            'charging_modes': (['pd5v', 'pd9v', 'pps'] if set(profiles) == {'fast', 'normal'} and
                               9000 in self.fixed_voltages(profiles['fast']) else []),
            'pd_profile': choice,
            'port_type': selected(port_type),
            'data_role': selected(self.read(port / 'data_role')) if port else '',
            'power_role': power_role,
            'available': {
                'charging': behaviour in ('auto', 'inhibit-charge'),
                'fast_charge': set(profiles) == {'fast', 'normal'} and fast_gate in (0, 1),
                'usb_role': bool(port_type and 'dual' in port_type and 'source' in port_type),
            },
        }

    @staticmethod
    def run(args):
        result = subprocess.run(args, text=True, capture_output=True, timeout=25,
                                env={'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LANG': 'C.UTF-8'})
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip() or 'Hardware request failed')

    @staticmethod
    def write(path, value):
        path.write_text(str(value) + '\n')

    def select_profile(self, kind, pps):
        port = self.port()
        profiles = self.profiles(port)
        gate = self.supplies / 'w202-bq-main/device/fast_charge'
        if set(profiles) != {'fast', 'normal'} or not gate.is_file():
            raise ValueError('Selectable PD profiles or the PPS permission interface are unavailable')
        # Keep the PPS worker gated while TCPM changes its allowed sink PDOs.
        self.write(gate, 0)
        selector = port / 'select_usb_power_delivery'
        wanted = profiles[kind]
        for attempt in range(5):
            try:
                self.write(selector, wanted)
                break
            except OSError as error:
                if error.errno != errno.EAGAIN or attempt == 4:
                    raise
                time.sleep(0.25)
        if selected(self.read(selector)) != wanted:
            raise RuntimeError('TCPM did not select the requested PD profile')
        if pps:
            self.write(gate, 1)

    def fast(self, enabled):
        self.select_profile('fast' if enabled else 'normal', enabled)

    def fixed_voltages(self, name):
        result = set()
        root = self.sysfs / 'class/usb_power_delivery' / name / 'sink-capabilities'
        for cap in root.glob('*'):
            if re.fullmatch(r'\d+:fixed_supply', cap.name):
                value = re.fullmatch(r'(\d+)mV', self.read(cap / 'voltage') or '')
                if value:
                    result.add(int(value[1]))
        return result

    def mode(self, name):
        if name not in ('pd5v', 'pd9v', 'pps'):
            raise ValueError('Unknown charging mode')
        profiles = self.profiles(self.port())
        if name == 'pd9v' and ('fast' not in profiles or
                              self.fixed_voltages(profiles['fast']) != {5000, 9000}):
            raise ValueError('The controller has no unambiguous 9 V fixed profile')
        self.select_profile('normal' if name == 'pd5v' else 'fast', name == 'pps')

    def apply(self, action):
        actions = {
            'charge-auto': ['/usr/libexec/w202-charging', 'auto'],
            'charge-inhibit': ['/usr/libexec/w202-charging', 'inhibit-charge'],
            'usb-auto': ['/usr/libexec/w202-usb-role', 'auto'],
            'usb-host': ['/usr/libexec/w202-usb-role', 'host'],
            'usb-device': ['/usr/libexec/w202-usb-role', 'device'],
        }
        if action in actions:
            self.runner(actions[action])
        elif action in ('fast-on', 'fast-off'):
            self.fast(action == 'fast-on')
        elif action in ('mode-auto', 'mode-pd5v', 'mode-pd9v', 'mode-pps'):
            # Auto opens the maximum safe sink profile, allowing TCPM's fixed
            # fallback and the charger's temperature-limited PPS worker.
            self.mode('pps' if action == 'mode-auto' else action.removeprefix('mode-'))
        else:
            raise ValueError('Unknown power action')


ACTIONS = {'charge-auto', 'charge-inhibit', 'fast-on', 'fast-off',
           'mode-auto', 'mode-pd5v', 'mode-pd9v', 'mode-pps',
           'usb-auto', 'usb-host', 'usb-device'}


def main(args=None):
    args = sys.argv[1:] if args is None else args
    if len(args) != 1 or args[0] not in ACTIONS | {'status'}:
        print('Usage: w202-power-control {' + '|'.join(['status', *sorted(ACTIONS)]) + '}', file=sys.stderr)
        return 2
    try:
        control = Controller()
        if args[0] != 'status':
            if os.geteuid() != 0:
                raise PermissionError('Changing power settings requires administrator authorization')
            with open('/run/w202-power-control.lock', 'a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                control.apply(args[0])
                if args[0].startswith('mode-'):
                    mode = control.status()['charging_mode']
                    Path('/run/w202-power-mode.json').write_text(json.dumps({
                        'requested_mode': args[0].removeprefix('mode-'), 'hardware_mode': mode}) + '\n')
        print(json.dumps(control.status()))
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(json.dumps({'error': str(error)}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
