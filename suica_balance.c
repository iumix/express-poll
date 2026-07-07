#include <nfc/nfc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_FRAME 264
static const bool VERBOSE = false;

static const uint8_t sensf_req_0003[5] = {
    0x00,       /* CMD = Polling */
    0x00, 0x03, /* System Code 0x0003 (Japan transit IC common area) */
    0x01,       /* Request Code: system code info */
    0x00        /* Time Slot: 1 slot */
};

static volatile sig_atomic_t stop = 0;

static void on_sigint(int sig) {
  (void)sig;
  stop = 1;
}

static void clear_screen(void) {
  fputs("\033[2J\033[H", stdout);
  fflush(stdout);
}

static void print_ready(void) {
  printf("Waiting for Suica / ICOCA / PASMO compatible card (FeliCa 0x0003)...\n\n");
}

static void hexdump(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; ++i)
    printf("%02x%c", d[i], (i + 1) % 16 ? ' ' : '\n');
  if (n % 16)
    puts("");
}

static uint16_t suica_date_year(const uint8_t *block_data) {
  uint16_t raw_date = ((uint16_t)block_data[4] << 8) | block_data[5];
  return (uint16_t)(2000 + ((raw_date >> 9) & 0x7f));
}

static uint8_t suica_date_month(const uint8_t *block_data) {
  uint16_t raw_date = ((uint16_t)block_data[4] << 8) | block_data[5];
  return (uint8_t)((raw_date >> 5) & 0x0f);
}

static uint8_t suica_date_day(const uint8_t *block_data) {
  uint16_t raw_date = ((uint16_t)block_data[4] << 8) | block_data[5];
  return (uint8_t)(raw_date & 0x1f);
}

int main(void) {
  signal(SIGINT, on_sigint);

  nfc_context *ctx = NULL;
  nfc_init(&ctx);
  if (!ctx) {
    fprintf(stderr, "Unable to init libnfc\n");
    return 1;
  }

  nfc_device *dev = nfc_open(ctx, NULL);
  if (!dev) {
    fprintf(stderr, "Unable to open NFC device\n");
    nfc_exit(ctx);
    return 1;
  }

  if (nfc_initiator_init(dev) < 0) {
    nfc_perror(dev, "nfc_initiator_init");
    nfc_close(dev);
    nfc_exit(ctx);
    return 1;
  }

  clear_screen();
  if (VERBOSE)
    printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
  print_ready();

  const nfc_modulation felica212 = (nfc_modulation){NMT_FELICA, NBR_212};
  nfc_target nt;

  while (!stop) {
    int sel = nfc_initiator_select_passive_target(
        dev, felica212, sensf_req_0003, sizeof(sensf_req_0003), &nt);

    if (sel <= 0) {
      usleep(100000);
      continue;
    }

    clear_screen();

    uint8_t idm[8];
    memcpy(idm, nt.nti.nfi.abtId, sizeof(idm));

    if (VERBOSE) {
      printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
      printf("Suica / ICOCA / PASMO compatible card detected\n\n");
      printf("Detected FeliCa system 0x0003\n");

      printf("IDm (NFCID2): ");
      hexdump(idm, sizeof(idm));
    }

    /*
     * Read Without Encryption:
     *   Service 0x008B = SF/card transaction history in the 0x0003 common area.
     *   Block 0 is the newest history entry and contains the current balance.
     */
    uint8_t read_cmd[] = {
        0x10, 0x06,                   /* LEN=16, CMD=0x06 (Read w/o Enc) */
        0,    0,    0, 0, 0, 0, 0, 0, /* IDm (8 bytes) */
        0x01,                         /* Number of services: 1 */
        0x8b, 0x00,                   /* Service Code 0x008B (little-endian) */
        0x01,                         /* Number of blocks: 1 */
        0x80, 0x00                    /* Block List: 0x8000 (2-byte format) */
    };
    memcpy(read_cmd + 2, idm, sizeof(idm));

    uint8_t read_rsp[MAX_FRAME];
    int read_len = nfc_initiator_transceive_bytes(
        dev, read_cmd, sizeof(read_cmd), read_rsp, sizeof(read_rsp), 0);

    if (VERBOSE)
      printf("\nRead Without Encryption response (%d bytes):\n", read_len);
    if (read_len <= 0) {
      printf("Read Without Encryption failed\n");
      goto release_and_wait;
    }

    if (VERBOSE)
      hexdump(read_rsp, read_len);

    if (read_len >= 29 && read_rsp[1] == 0x07) {
      uint8_t status1 = read_rsp[10];
      uint8_t status2 = read_rsp[11];
      uint8_t num_blocks = read_rsp[12];

      if (VERBOSE)
        printf("Status: %02x %02x, Blocks: %d\n", status1, status2,
               num_blocks);

      if (status1 == 0x00 && status2 == 0x00 && num_blocks > 0) {
        const uint8_t *block_data = read_rsp + 13;
        uint16_t balance = (uint16_t)block_data[11] |
                           ((uint16_t)block_data[12] << 8);

        if (VERBOSE) {
          printf("\nBlock data (16 bytes):\n");
          hexdump(block_data, 16);
          printf("\n=== Balance Information ===\n");
        }

        printf("Balance: %u JPY\n", balance);

        if (VERBOSE && (block_data[4] || block_data[5])) {
          printf("Latest history date: %04u-%02u-%02u\n",
                 suica_date_year(block_data), suica_date_month(block_data),
                 suica_date_day(block_data));
        }
        if (VERBOSE)
          printf("\a");
      } else {
        printf("Read error: Status indicates failure\n");
      }
    } else {
      printf("Unexpected response format\n");
    }

  release_and_wait: {
    uint8_t release_cmd[] = {
        0x0A, 0x04,                  /* LEN=10, CMD=0x04 (Release) */
        0,    0,    0, 0, 0, 0, 0, 0 /* IDm (8 bytes) */
    };
    memcpy(release_cmd + 2, idm, sizeof(idm));

    uint8_t release_rsp[MAX_FRAME];
    int release_len =
        nfc_initiator_transceive_bytes(dev, release_cmd, sizeof(release_cmd),
                                       release_rsp, sizeof(release_rsp), 0);

    if (VERBOSE && release_len > 0) {
      printf("\nRelease response (%d bytes):\n", release_len);
      hexdump(release_rsp, release_len);
    } else if (VERBOSE) {
      printf("Release command failed\n");
    }
  }

    if (VERBOSE)
      printf("\nWaiting for card removal...\n");
    while (!stop && nfc_initiator_target_is_present(dev, &nt) == 0)
      usleep(200000);

    if (VERBOSE) {
      printf("Card removed.\n");
      printf("--------------------------------\n\n");
    }
    print_ready();
  }

  nfc_close(dev);
  nfc_exit(ctx);
  if (VERBOSE)
    printf("Program terminated.\n");
  return 0;
}
