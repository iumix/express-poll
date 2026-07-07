#include <nfc/nfc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_FRAME 264

// 0x8008 = Octopus
// 0x0003 = Suica/ICOCA/PASMO (Japan transit cards)

uint8_t sensf_req_8008[5] = {
    0x00,       /* CMD = Polling */
    0x80, 0x08, /* Octopus System Code Poll */
    // 0x00, 0x03, /* JP Transit Code Poll */
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
  fputs("\033[2J\033[H", stdout);
  fflush(stdout);
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

  printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
  printf("Polling 0x%04x\n\n", sensf_req_8008[1] << 8 | sensf_req_8008[2]);

  const nfc_modulation felica212 = (nfc_modulation){NMT_FELICA, NBR_212};
  nfc_target nt;

  while (!stop) {
    int sel = nfc_initiator_select_passive_target(
        dev, felica212, sensf_req_8008, sizeof(sensf_req_8008), &nt);

    if (sel <= 0) {
      usleep(100000);
      continue;
    }

    /* Clear before showing data for this scan */
    clear_screen();
    printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
    printf("Apple Pay Octopus (0x%04x) detected\n\n",
           sensf_req_8008[1] << 8 | sensf_req_8008[2]);
    printf("Detected Felica\n");

    uint8_t idm[8];
    memcpy(idm, nt.nti.nfi.abtId, 8);

    printf("IDm (NFCID2): ");
    hexdump(idm, 8);

    // Release card
    uint8_t release_cmd[] = {
        0x0A, 0x04,                  /* LEN=10, CMD=0x04 (Release)         */
        0,    0,    0, 0, 0, 0, 0, 0 /* IDm (8 bytes)                      */
    };
    memcpy(release_cmd + 2, idm, 8);

    uint8_t release_rsp[MAX_FRAME];
    int release_len =
        nfc_initiator_transceive_bytes(dev, release_cmd, sizeof(release_cmd),
                                       release_rsp, sizeof(release_rsp), 0);

    if (release_len > 0) {
      printf("\nRelease response (%d bytes):\n", release_len);
      hexdump(release_rsp, release_len);
    } else {
      printf("Release command failed\n");
    }

    // Wait for card removal
    printf("\nWaiting for card removal...\n");
    while (!stop && nfc_initiator_target_is_present(dev, &nt) == 0)
      usleep(200000);

    printf("Card removed.\n");
    printf("--------------------------------\n\n");

    /* After a scan completes, show the waiting header again */
    printf("Polling 0x%04x\n\n", sensf_req_8008[1] << 8 | sensf_req_8008[2]);
  }

  nfc_close(dev);
  nfc_exit(ctx);
  printf("Program terminated.\n");
  return 0;
}
