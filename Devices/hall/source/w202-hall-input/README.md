# W202DS Hall sensor module

The module binds the stock `ah,hall_ic` node and `ah,gpio_irq` property. It validates GPIO controller address `0x64170000`, line 148 and flags 0 before acquiring the line. Pin bias and voltage remain unchanged; an optional DT `vddio-supply` has a managed lifetime.

The measured signal is active low: `raw_level=0` means the magnetic sensor is active. The read-only platform attributes are `raw_level`, `active` and `irq_count`. GPIO reads occur on initial setup, rising/falling IRQs, or an explicit attribute read. `raw_level` and `active` support `poll(POLLPRI)` change notification. After notification, seek to offset zero and read the attribute again. `active` replaces the former lid-specific `closed` attribute.

By default `switch_code=-1`: no input switch device is registered, so the Hall sensor is not classified as a power or lid switch. An integrator can select a standard `EV_SW` code at module load. For example, 发行版集成层 configures `switch_code=0` to interpret the active signal as `SW_LID`; `1` then means closed. The parameter is read-only after loading; reject values outside `-1..SW_MAX`. The driver reports the initial switch state and every subsequent edge. It never chooses display, suspend, shutdown or wakeup actions.

Build against matching kernel headers with a separate `MO` output directory. GPIO polarity and the former fixed `SW_LID` event path were verified on hardware. The configurable mapping requires a new module build and device validation. Do not load this module where another driver owns the GPIO, or run raw GPIO diagnostics against an owned line.

Hardware diagnostics are in [userspace](../../userspace/README.md). Application-facing interfaces and the system-integration boundary are described in [INTERFACES.md](../../../INTERFACES.md).
