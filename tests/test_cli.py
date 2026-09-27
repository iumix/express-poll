"""No hardware needed: invalid options and explicit missing-reader behavior."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
binary_dir = Path(os.environ.get("TRANSIT_TEST_BIN", root / "bin"))
suffix = ".exe" if os.name == "nt" else ""
for program in ("suica_balance", "octopus_balance"):
    executable = str(binary_dir / (program + suffix))
    result = subprocess.run([executable, "-x"], capture_output=True, timeout=5)
    assert result.returncode == 2, result
    assert b"Usage:" in result.stderr, result
for program in ("suica_balance", "octopus_balance", "felica_poll", "probe_system"):
    env = dict(os.environ, LIBNFC_DEFAULT_DEVICE="pn532_uart:missing-test-reader")
    result = subprocess.run([str(binary_dir / (program + suffix))], env=env,
                            capture_output=True, timeout=5)
    assert result.returncode == 1, result
    assert b"Unable to open NFC device" in result.stderr, result
    assert b"NFC reader found" not in result.stderr, result
if os.name == "nt":
    for selection in ("0\n", "21\n", "hello\n", "1\n"):
        result = subprocess.run([str(binary_dir / "express_poll.exe")],
                                input=selection, text=True, capture_output=True,
                                env=env, timeout=5)
        assert result.returncode == 1, result
        expected = ("Unable to open NFC device" if selection == "1\n"
                    else "Invalid selection.")
        assert expected in result.stderr, result
print("CLI and explicit-reader tests passed.")
