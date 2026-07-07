#include <nfc/nfc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Define a raw structure to match how FeliCa data is laid out
typedef struct {
  uint8_t id[8];          // NFCID2
  uint8_t pad[8];         // PAD (Polling Response Data)
  uint8_t system_code[2]; // System Code
} felica_raw_t;

int main(void) {
  nfc_device *pnd = NULL;
  nfc_context *context;
  nfc_target nt;

  nfc_init(&context);
  if (context == NULL) {
    fprintf(stderr, "Unable to init libnfc\n");
    return EXIT_FAILURE;
  }

  pnd = nfc_open(context, NULL);
  if (pnd == NULL) {
    fprintf(stderr, "Unable to open NFC device\n");
    nfc_exit(context);
    return EXIT_FAILURE;
  }

  if (nfc_initiator_init(pnd) < 0) {
    nfc_perror(pnd, "nfc_initiator_init");
    nfc_close(pnd);
    nfc_exit(context);
    return EXIT_FAILURE;
  }

  printf("NFC reader: %s opened\n", nfc_device_get_name(pnd));

  const nfc_modulation nm = {.nmt = NMT_FELICA, .nbr = NBR_212};

  while (1) {
    int res = nfc_initiator_poll_target(pnd, &nm, 1, 1, 200, &nt);
    if (res > 0) {
      printf("FeliCa card detected!\n");

      felica_raw_t *f = (felica_raw_t *)&nt.nti;

      printf("  NFCID2: ");
      for (int i = 0; i < 8; i++) {
        printf("%02x ", f->id[i]);
      }
      printf("\n");

      printf("  System Code: %02x %02x\n", f->system_code[0],
             f->system_code[1]);

      printf("Waiting for card removal...\n");
      while (nfc_initiator_target_is_present(pnd, NULL) == 0) {
        usleep(500000);
      }
      printf("Card removed.\n\n");
    } else {
      usleep(200000);
    }
  }

  nfc_close(pnd);
  nfc_exit(context);
  return EXIT_SUCCESS;
}
