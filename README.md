# express-poll

1. Connect the PN532 in UART/HSU mode.
2. Build all binaries:

   ```sh
   make
   ```

3. Run a binary below, then tap your card/phone. Press `Ctrl+C` to stop.

<details>
<summary>suica_balance</summary>

Read the balance in JPY and latest history date from Suica / ICOCA / PASMO compatible cards.

```sh
./bin/suica_balance
```

</details>

<details>
<summary>octopus_balance</summary>

Read the balance in HKD from Apple Pay Octopus in Express mode.

```sh
./bin/octopus_balance
```

</details>

<details>
<summary>felica_poll</summary>

Detect FeliCa cards and display their ID and system code.

```sh
./bin/felica_poll
```

</details>

<details>
<summary>express_poll</summary>

Choose a FeliCa system to poll and display detected card IDs. Requires `fzf` on macOS/Linux.

```sh
./bin/express_poll
```

</details>

<details>
<summary>probe_system</summary>

List the FeliCa system codes exposed by a card/phone.

```sh
./bin/probe_system
```

</details>

4. Clean build files when needed:

   ```sh
   make clean
   ```

   Run `make` again to rebuild.

## Disclaimer

This project is not affiliated with or endorsed by Octopus Cards Limited,
JR East, Sony, or any other card issuer. The programs only read data that cards
expose without encryption and never write to a card. They are provided for
educational and personal use, at your own risk.

## License

[MIT](LICENSE)
