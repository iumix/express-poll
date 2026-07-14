#!/usr/bin/env bash

set -euo pipefail

readonly NFC_CONFIG="/usr/local/etc/nfc/libnfc.conf"

usage() {
  echo "Usage: $0 [/dev/tty.*]"
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

if (( EUID != 0 )); then
  echo "Root privileges are required; requesting sudo..."
  exec sudo -- "$0" "$@"
fi

if (( $# > 1 )); then
  usage >&2
  exit 1
fi

device=${1:-}

if [[ -z "$device" ]]; then
  fzf_command=$(command -v fzf || true)
  if [[ -z "$fzf_command" ]]; then
    for candidate in /opt/homebrew/bin/fzf /usr/local/bin/fzf; do
      if [[ -x "$candidate" ]]; then
        fzf_command=$candidate
        break
      fi
    done
  fi

  if [[ -z "$fzf_command" ]]; then
    echo "Error: fzf is required for interactive selection." >&2
    exit 1
  fi

  shopt -s nullglob
  serial_ports=(/dev/tty.*)
  shopt -u nullglob

  if (( ${#serial_ports[@]} == 0 )); then
    echo "Error: no serial ports matching /dev/tty.* were found." >&2
    exit 1
  fi

  if ! device=$(printf '%s\n' "${serial_ports[@]}" | "$fzf_command" \
    --height='~40%' \
    --reverse \
    --prompt='Select NFC serial port: '); then
    echo "Serial port selection cancelled." >&2
    exit 1
  fi
fi

if [[ $device != /dev/tty.* || ! -e $device ]]; then
  echo "Error: '$device' is not an existing /dev/tty.* device." >&2
  exit 1
fi

if [[ ! -f $NFC_CONFIG ]]; then
  echo "Error: NFC config not found at $NFC_CONFIG." >&2
  exit 1
fi

backup=$(mktemp "/tmp/libnfc.conf.backup.$(date +%Y%m%d%H%M%S).XXXXXX")
temp_file=$(mktemp "${TMPDIR:-/tmp}/libnfc.conf.XXXXXX")
trap 'rm -f "$temp_file"' EXIT

cp -p "$NFC_CONFIG" "$backup"

awk -v connstring="pn532_uart:$device" '
  /^[[:space:]]*device\.connstring[[:space:]]*=/ {
    if (!updated) {
      print "device.connstring = \"" connstring "\""
      updated = 1
    }
    next
  }
  { print }
  END {
    if (!updated) {
      print "device.connstring = \"" connstring "\""
    }
  }
' "$NFC_CONFIG" > "$temp_file"

cat "$temp_file" > "$NFC_CONFIG"

echo "Updated $NFC_CONFIG to use pn532_uart:$device"
echo "Backup saved to $backup"
