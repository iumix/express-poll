/*
 * suica_balance.c  –  loop-read Suica balance
 * gcc suica_balance.c -lnfc -o suica_balance
 */

#include <nfc/nfc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_FRAME 264
static volatile sig_atomic_t stop = 0;
static void on_sigint(int sig) { stop = 1; }

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
    perror("nfc_init");
    return 1;
  }
  nfc_device *dev = nfc_open(ctx, NULL);
  if (!dev) {
    fputs("No NFC reader\n", stderr);
    return 1;
  }

  if (nfc_initiator_init(dev) < 0) {
    nfc_perror(dev, "init");
    return 1;
  }
  printf("Reader: %s opened\n\n", nfc_device_get_name(dev));

  const nfc_modulation felica212 = {NMT_FELICA, NBR_212};
  nfc_target nt; /* lives for the whole loop   */

  while (!stop) {
    /* ---- Wait for a FeliCa card ---- */
    if (nfc_initiator_select_passive_target(dev, felica212, NULL, 0, &nt) <=
        0) {
      usleep(10000); /* no card, back off 0.1 s    */
      continue;
    }

    printf("Card detected. IDm: ");
    hexdump(nt.nti.nfi.abtId, 8);

    /* ---- Build Read-Without-Encryption (svc 0x008B, block 0) ---- */
    uint8_t cmd[] = {
        0x10, 0x06,                      /* LEN=16, CMD=0x06           */
        0,    0,    0,    0, 0, 0, 0, 0, /* IDm (place-holder)         */
        0x01, 0x8B, 0x00,                /* 1 svc: 0x008B              */
        0x01, 0x80, 0x00                 /* 1 blk: block 0             */
    };
    memcpy(cmd + 2, nt.nti.nfi.abtId, 8);

    uint8_t rsp[MAX_FRAME];
    int rlen = nfc_initiator_transceive_bytes(dev, cmd, sizeof cmd, rsp,
                                              sizeof rsp, 0);

    printf("Raw response (%d bytes): \n", rlen);
    hexdump(rsp, rlen);

    if (rlen >= 29 && rsp[1] == 0x07 && rsp[10] == 0 && rsp[11] == 0) {
      const uint8_t *blk = rsp + 13;
      uint16_t bal = blk[11] | (blk[12] << 8); /* bytes 11-12 */
      printf("Balance: %u JPY\n", bal);
    } else {
      puts("Read error.");
    }

    /* ---- Stay here until card leaves the field ---- */
    while (!stop && nfc_initiator_target_is_present(dev, &nt) == 0)
      usleep(200000); /* poll every 0.2 s           */

    puts("Card removed.\n");
  }
  

  nfc_close(dev);
  nfc_exit(ctx);
  puts("Bye!");
  return 0;
}
