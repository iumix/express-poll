# Transit polling

Read FeliCa transit cards using a PN532 in UART/HSU mode on macOS, Linux,
and Windows. The build-and-run command is the same:

```sh
make clean && make && ./bin/suica_balance
```

On Windows, use PowerShell 7+ or Git Bash for that exact command. The native
`.exe` suffix can be omitted. In Command Prompt, use
`make clean && make && .\bin\suica_balance` instead. Windows PowerShell 5.1
does not support `&&`; run each command separately there. No WSL or USB
forwarding is needed.

## One-time dependencies

- macOS: `brew install libnfc pkg-config` and the Xcode command-line tools.
- Debian/Ubuntu: `sudo apt install build-essential libnfc-dev pkg-config`.
- Windows: install **MinGW-w64 GCC**, **GNU Make**, **CMake 3.16+**, and **Git**,
  and put them on PATH. For example, with Chocolatey:
  `choco install mingw make cmake git -y` from an administrator terminal.
  These tools are already installed on the development PC used for this port.

Windows `make` downloads a pinned upstream libnfc revision on its first build,
builds its PN532 UART backend, and puts `libnfc.dll` beside the executables in
`bin/`. Keep that DLL with the executables and keep the MinGW runtime on PATH.
The dependency source is cached in `.deps/`; `make clean` removes `bin/` and
`build/`, so subsequent builds can reuse the downloaded source. macOS/Linux
continue to use the installed libnfc, including its other reader backends.

## Automatic reader selection

All five programs first try the reader from the existing libnfc configuration
or its normal discovery. If that fails, they automatically probe serial ports
for a responding PN532. No configuration script or fixed COM number is needed.

- Windows: assigned COM ports, including numbers above COM255.
- macOS: USB serial/modem callout ports (`/dev/cu.*`).
- Linux: `/dev/ttyUSB*`, `/dev/ttyACM*`, and `/dev/ttyAMA*`.

Discovery sends PN532 diagnostic commands to candidate serial ports and uses
the first reader that responds. Close other programs using the reader. On Linux,
your user needs access to the serial device (usually membership in `dialout`,
followed by signing out and back in). A missing reader produces an error and exits.

To select one particular reader, set `LIBNFC_DEFAULT_DEVICE`. An explicit value
is authoritative: a failed connection exits instead of selecting a different reader.

```powershell
$env:LIBNFC_DEFAULT_DEVICE = "pn532_uart:COM8"
./bin/suica_balance
# Return to automatic selection:
Remove-Item Env:LIBNFC_DEFAULT_DEVICE
```

```sh
LIBNFC_DEFAULT_DEVICE=pn532_uart:/dev/ttyUSB0 ./bin/suica_balance
```

Use `-v` with `suica_balance` or `octopus_balance` for raw responses and reader
details. `express_poll` retains the `fzf` picker on macOS/Linux and uses a numbered
menu on Windows. Set `LIBNFC_LOG_LEVEL=3` to troubleshoot failed serial probes.

## Verification

`python3 tests/test_cli.py` checks argument errors and explicit missing-reader
behavior without hardware. The CI workflow builds on macOS, Linux, and Windows;
its Unix jobs also test fallback discovery with simulated ports. Real card reads
still require a connected PN532 and a compatible card/phone.

## Optional legacy macOS configuration

The original configuration helper remains available. To list serial ports and
update the NFC port interactively, run:

```sh
./update_nfc_conf.sh
```

You can also provide the serial port directly:

```sh
./update_nfc_conf.sh /dev/tty.usbserial-110
```

Interactive selection requires `fzf`. The script updates `device.connstring` in
`/usr/local/etc/nfc/libnfc.conf` and creates a temporary timestamped backup under
`/tmp`. If needed, it requests `sudo` access automatically.
