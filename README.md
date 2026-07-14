To list the available serial ports and update the NFC port interactively, run:

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
