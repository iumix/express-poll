#include <nfc/nfc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FRAME 264
#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define FELICA_IDM_LEN 8
#define SYSTEM_CODE_TIMEOUT_MS 1000
#define SYSTEM_CODE_RESPONSE_HEADER_LEN 11
#define REMOVAL_CONFIRMATIONS 2

typedef struct {
  uint16_t code;
  const char *name;
} known_system;

static const known_system known_systems[] = {
    {0x0003, "Suica / ICOCA / PASMO"},
    {0x8008, "Octopus"},
    {0xFE00, "FeliCa Common Area"},
    {0x811D, "Rakuten Edy"},
    {0x12FC, "NFC Forum Type 3 Tag / NDEF"},
    {0x88B4, "FeliCa Lite / Lite-S"},
    {0x957A, "FeliCa Secure ID"},
    {0xFEE1, "Sony NFC Dynamic Tag / FeliCa Plug"},
    {0x8620, "Blackboard Campus Card"},
    {0x80DE, "IruCa"},
    {0x83EE, "ayuca"},
    {0x8592, "PASPY"},
    {0x865E, "SAPICA"},
    {0x8B5D, "Ryuto"},
    {0x8FC1, "OKICA"},
    {0x93EC, "N+ Card"},
    {0x90B7, "Kartu Multi Trip"},
    {0x0102, "Legacy EZ-Link"},
    {0x0000, "Sony SDK Sample"},
};

/* Poll every FeliCa system and ask for system-code information. */
static const uint8_t poll_any_system[] = {
    0x00,       /* CMD = Polling */
    0xFF, 0xFF, /* System Code = wildcard */
    0x01,       /* Request Code = system-code information */
    0x00        /* Time Slot = 1 slot */
};

static volatile sig_atomic_t stop = 0;

static void on_sigint(int sig) {
  (void)sig;
  stop = 1;
}

static const char *system_name(uint16_t code) {
  for (size_t i = 0; i < ARRAY_LEN(known_systems); ++i) {
    if (known_systems[i].code == code)
      return known_systems[i].name;
  }
  return NULL;
}

static uint16_t read_be16(const uint8_t bytes[2]) {
  return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void print_idm(const uint8_t idm[FELICA_IDM_LEN]) {
  for (size_t i = 0; i < FELICA_IDM_LEN; ++i)
    printf("%02x%s", idm[i], i + 1 == FELICA_IDM_LEN ? "\n" : " ");
}

static void print_system(uint16_t code) {
  const char *name = system_name(code);

  printf("  0x%04x", code);
  if (name)
    printf("  %s", name);
  else
    printf("  Unknown system");
  putchar('\n');
}

static int select_felica_target(nfc_device *dev, nfc_modulation modulation,
                                nfc_target *target) {
  int selected = nfc_initiator_select_passive_target(
      dev, modulation, poll_any_system, sizeof(poll_any_system), target);

  /* Some drivers report a bounded, empty selection as a timeout. */
  return selected == NFC_ETIMEOUT ? 0 : selected;
}

static int request_system_codes(nfc_device *dev,
                                const uint8_t idm[FELICA_IDM_LEN]) {
  uint8_t request[2 + FELICA_IDM_LEN] = {
      sizeof(request), 0x0C /* Request System Code */};
  memcpy(request + 2, idm, FELICA_IDM_LEN);

  uint8_t response[MAX_FRAME];
  int response_len = nfc_initiator_transceive_bytes(
      dev, request, sizeof(request), response, sizeof(response),
      SYSTEM_CODE_TIMEOUT_MS);

  if (response_len < 0) {
    nfc_perror(dev, "Request System Code");
    return -1;
  }

  if (response_len < SYSTEM_CODE_RESPONSE_HEADER_LEN) {
    fprintf(stderr, "Request System Code returned a short response (%d bytes)\n",
            response_len);
    return -1;
  }

  if (response[0] != response_len) {
    fprintf(stderr,
            "Request System Code length mismatch (frame says %u, received %d)\n",
            response[0], response_len);
    return -1;
  }

  if (response[1] != 0x0D) {
    fprintf(stderr,
            "Unexpected Request System Code response command: 0x%02x\n",
            response[1]);
    return -1;
  }

  if (memcmp(response + 2, idm, FELICA_IDM_LEN) != 0) {
    fprintf(stderr, "Request System Code response IDm does not match card\n");
    return -1;
  }

  uint8_t count = response[10];
  if (count == 0) {
    fprintf(stderr, "Request System Code returned no system codes\n");
    return -1;
  }

  size_t expected_len = SYSTEM_CODE_RESPONSE_HEADER_LEN + 2u * count;
  if ((size_t)response_len != expected_len) {
    fprintf(stderr,
            "Request System Code count/length mismatch "
            "(%u codes require %zu bytes, received %d)\n",
            count, expected_len, response_len);
    return -1;
  }

  printf("Exposed system%s (%u):\n", count == 1 ? "" : "s", count);
  for (uint8_t i = 0; i < count; ++i) {
    size_t offset = SYSTEM_CODE_RESPONSE_HEADER_LEN + 2u * i;
    uint16_t code = read_be16(response + offset);
    print_system(code);
  }
  putchar('\n');

  return 0;
}

static int wait_for_card_removal(nfc_device *dev, nfc_modulation modulation,
                                 const uint8_t idm[FELICA_IDM_LEN]) {
  while (!stop) {
    if (nfc_initiator_target_is_present(dev, NULL) == 0) {
      usleep(200000);
      continue;
    }

    /*
     * PN53x presence checks cannot ping FeliCa Lite. Re-poll before declaring
     * removal so a held card is not immediately reported again as a new card.
     */
    unsigned int misses = 0;
    while (!stop && misses < REMOVAL_CONFIRMATIONS) {
      nfc_target observed = {0};
      int selected = select_felica_target(dev, modulation, &observed);

      if (selected < 0) {
        nfc_perror(dev, "Unable to confirm card removal");
        return -1;
      }
      if (selected == 0) {
        ++misses;
        usleep(200000);
        continue;
      }

      if (memcmp(observed.nti.nfi.abtId, idm, FELICA_IDM_LEN) != 0)
        return 1;

      /* The same card answered, so the presence check was a false negative. */
      break;
    }

    if (misses == REMOVAL_CONFIRMATIONS)
      return 1;
  }

  return 0;
}

int main(void) {
  signal(SIGINT, on_sigint);

  nfc_context *ctx = NULL;
  nfc_init(&ctx);
  if (!ctx) {
    fprintf(stderr, "Unable to init libnfc\n");
    return EXIT_FAILURE;
  }

  nfc_device *dev = nfc_open(ctx, NULL);
  if (!dev) {
    fprintf(stderr, "Unable to open NFC device\n");
    nfc_exit(ctx);
    return EXIT_FAILURE;
  }

  if (nfc_initiator_init(dev) < 0) {
    nfc_perror(dev, "nfc_initiator_init");
    nfc_close(dev);
    nfc_exit(ctx);
    return EXIT_FAILURE;
  }

  if (nfc_device_set_property_bool(dev, NP_INFINITE_SELECT, false) < 0) {
    nfc_perror(dev, "Unable to configure bounded card polling");
    nfc_close(dev);
    nfc_exit(ctx);
    return EXIT_FAILURE;
  }

  printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
  printf("Waiting for a FeliCa card...\n");

  const nfc_modulation felica212 = {NMT_FELICA, NBR_212};
  int exit_status = EXIT_SUCCESS;

  while (!stop) {
    nfc_target target = {0};
    int selected = select_felica_target(dev, felica212, &target);

    if (selected < 0) {
      if (!stop) {
        nfc_perror(dev, "nfc_initiator_select_passive_target");
        exit_status = EXIT_FAILURE;
      }
      break;
    }
    if (selected == 0) {
      usleep(100000);
      continue;
    }

    uint8_t idm[FELICA_IDM_LEN];
    memcpy(idm, target.nti.nfi.abtId, sizeof(idm));

    printf("\nFeliCa card detected\n");
    printf("IDm (NFCID2): ");
    print_idm(idm);

    bool has_polling_system = target.nti.nfi.szLen > 18;
    if (has_polling_system) {
      uint16_t polling_system = read_be16(target.nti.nfi.abtSysCode);
      printf("Polling response system (System 0):\n");
      print_system(polling_system);
    } else {
      printf("Polling response did not include a system code.\n");
    }

    if (request_system_codes(dev, idm) < 0) {
      if (has_polling_system) {
        fprintf(stderr,
                "Unable to enumerate every system; only System 0 from the "
                "polling response is available.\n");
      } else {
        fprintf(stderr,
                "Unable to enumerate systems, and the polling response did "
                "not provide a fallback system code.\n");
      }
    }

    printf("Waiting for card removal...\n");
    int removal = wait_for_card_removal(dev, felica212, idm);
    if (removal < 0) {
      exit_status = EXIT_FAILURE;
      break;
    }

    if (!stop) {
      printf("Card removed.\n\n");
      printf("Waiting for a FeliCa card...\n");
    }
  }

  nfc_close(dev);
  nfc_exit(ctx);
  return exit_status;
}
