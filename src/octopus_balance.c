#include <nfc/nfc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_FRAME 264
static const bool VERBOSE = false;

uint8_t sensf_req_8008[5] = {
    0x00,       /* CMD = Polling */
    0x80, 0x08, /* System Code 0x8008 (Octopus Express) */
    0x01,       /* Request Code: system code info */
    0x00        /* Time Slot: 1 slot */
};

static volatile sig_atomic_t stop = 0;
static void on_sigint(int sig) {
  (void)sig;
  stop = 1;
}

/* Clear terminal screen and move cursor to top-left */
static void clear_screen(void) {
  /* ANSI: clear screen + home cursor */
  fputs("\033[2J\033[H", stdout);
  fflush(stdout);
}

static void print_ready(void) {
  printf("Waiting for Apple Pay Octopus Card (Express)…\n\n");
}

static void hexdump(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; ++i)
    printf("%02x%c", d[i], (i + 1) % 16 ? ' ' : '\n');
  if (n % 16)
    puts("");
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

  /* Clear once at startup */
  clear_screen();
  if (VERBOSE)
    printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
  print_ready();

  const nfc_modulation felica212 = (nfc_modulation){NMT_FELICA, NBR_212};
  nfc_target nt;

  while (!stop) {
    int sel = nfc_initiator_select_passive_target(
        dev, felica212, sensf_req_8008,
        sizeof(sensf_req_8008), /* << key difference */
        &nt);

    if (sel <= 0) {
      usleep(100000);
      continue;
    }

    /* Clear before showing data for each (subsequent) scan */
    clear_screen();

    if (VERBOSE) {
      printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
      printf("Apple Pay Octopus Card (Express) detected\n\n");
      printf("Detected Felica\n");
    }

    uint8_t idm[8];
    memcpy(idm, nt.nti.nfi.abtId, 8);

    if (VERBOSE) {
      printf("IDm (NFCID2): ");
      hexdump(idm, 8);
    }

    // Read Balance
    uint8_t read_cmd[] = {
        0x10, 0x06,                   /* LEN=16, CMD=0x06 (Read w/o Enc)    */
        0,    0,    0, 0, 0, 0, 0, 0, /* IDm (8 bytes)                      */
        0x01,                         /* Number of services: 1              */
        0x17, 0x01,                   /* Service Code: 0x0117 (little-endian) */
        0x01,                         /* Number of blocks: 1                */
        0x80, 0x00                    /* Block List: 0x8000 (2-byte format) */
    };
    memcpy(read_cmd + 2, idm, 8);

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
        if (VERBOSE) {
          printf("\nBlock data (16 bytes):\n");
          hexdump(block_data, 16);
        }

        int32_t balance = 0;
        for (int i = 0; i < 4; i++) {
          balance <<= 8;
          balance |= block_data[i];
        }

        int32_t balance_500 = balance - 500;

        if (VERBOSE) {
          printf("\n=== Balance Information ===\n");
          printf("Raw value: %d (0x%08x)\n", balance, (unsigned int)balance);
          printf("\a");
        }

        // Before 2017-10-01, Octopus used to have a HK$350 deposit
        // printf("Balance (offset 350): ");
        // if (balance_350 < 0)
        //   printf("-HK$ %.1f\n", (-balance_350) / 10.0);
        // else
        //   printf("HK$ %.1f\n", balance_350 / 10.0);

        printf("Balance (offset 500): ");
        if (balance_500 < 0)
          printf("-HK$ %.1f\n", (-balance_500) / 10.0);
        else
          printf("HK$ %.1f\n", balance_500 / 10.0);
      } else {
        printf("Read error: Status indicates failure\n");
      }
    } else {
      printf("Unexpected response format\n");
    }

  release_and_wait: {
    uint8_t release_cmd[] = {
        0x0A, 0x04,                  /* LEN=10, CMD=0x04 (Release)         */
        0,    0,    0, 0, 0, 0, 0, 0 /* IDm (8 bytes)                      */
    };
    memcpy(release_cmd + 2, idm, 8);

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

    /* Re-print header after removal so the next loop (waiting) has context */
    print_ready();
  }

  nfc_close(dev);
  nfc_exit(ctx);
  if (VERBOSE)
    printf("Program terminated.\n");
  return 0;
}
