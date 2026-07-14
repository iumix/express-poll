#include <nfc/nfc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_FRAME 264
#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
  uint16_t code;
  const char *name;
} felica_system;

/* Add new system codes here; the picker and status output use this table. */
static const felica_system felica_systems[] = {
    {0x8008, "Octopus"},
    {0x0003, "Suica / ICOCA / PASMO"},
};

uint8_t sensf_req[5] = {
    0x00,       /* CMD = Polling */
    0x00, 0x00, /* System code, populated after the fzf selection */
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

static int choose_system(const felica_system **selected_system) {
  char choices_path[] = "/tmp/express_poll_systems.XXXXXX";
  int choices_fd = mkstemp(choices_path);
  if (choices_fd < 0) {
    perror("Unable to create system code list");
    return -1;
  }

  FILE *choices = fdopen(choices_fd, "w");
  if (!choices) {
    perror("Unable to write system code list");
    close(choices_fd);
    unlink(choices_path);
    return -1;
  }

  int write_failed = 0;
  for (size_t i = 0; i < ARRAY_LEN(felica_systems); ++i) {
    if (fprintf(choices, "0x%04x  %s\n", felica_systems[i].code,
                felica_systems[i].name) < 0) {
      write_failed = 1;
      break;
    }
  }
  if (fclose(choices) != 0)
    write_failed = 1;
  if (write_failed) {
    fprintf(stderr, "Unable to write system code list.\n");
    unlink(choices_path);
    return -1;
  }

  char command[1024];
  int command_length = snprintf(
      command, sizeof(command),
      "fzf_command=$(command -v fzf 2>/dev/null || true); "
      "if [ -z \"$fzf_command\" ]; then "
      "if [ -x /opt/homebrew/bin/fzf ]; then "
      "fzf_command=/opt/homebrew/bin/fzf; "
      "elif [ -x /usr/local/bin/fzf ]; then "
      "fzf_command=/usr/local/bin/fzf; "
      "else echo 'Error: fzf is required to select a system code.' >&2; "
      "exit 127; fi; fi; "
      "\"$fzf_command\" --height='~40%%' --reverse --no-multi "
      "--prompt='Select FeliCa system code: ' < '%s'",
      choices_path);
  if (command_length < 0 || (size_t)command_length >= sizeof(command)) {
    fprintf(stderr, "Unable to construct fzf command.\n");
    unlink(choices_path);
    return -1;
  }

  FILE *fzf = popen(command, "r");
  if (!fzf) {
    perror("Unable to start fzf");
    unlink(choices_path);
    return -1;
  }

  char selection[256];
  char *result = fgets(selection, sizeof(selection), fzf);
  int status = pclose(fzf);
  unlink(choices_path);

  if (!result) {
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 127)
      fprintf(stderr, "System code selection cancelled.\n");
    return -1;
  }
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "fzf failed while selecting a system code.\n");
    return -1;
  }

  unsigned int selected_code;
  if (sscanf(selection, "0x%x", &selected_code) != 1 ||
      selected_code > UINT16_MAX) {
    fprintf(stderr, "Invalid system code selected: %s", selection);
    return -1;
  }

  for (size_t i = 0; i < ARRAY_LEN(felica_systems); ++i) {
    if (felica_systems[i].code == selected_code) {
      *selected_system = &felica_systems[i];
      return 0;
    }
  }

  fprintf(stderr, "Unknown system code selected: 0x%04x\n", selected_code);
  return -1;
}

int main(void) {
  signal(SIGINT, on_sigint);

  const felica_system *system;
  if (choose_system(&system) < 0)
    return 1;

  uint16_t system_code = system->code;
  sensf_req[1] = (uint8_t)(system_code >> 8);
  sensf_req[2] = (uint8_t)system_code;

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
  printf("Polling 0x%04x\n\n", sensf_req[1] << 8 | sensf_req[2]);

  const nfc_modulation felica212 = (nfc_modulation){NMT_FELICA, NBR_212};
  nfc_target nt;

  while (!stop) {
    int sel = nfc_initiator_select_passive_target(
        dev, felica212, sensf_req, sizeof(sensf_req), &nt);

    if (sel <= 0) {
      usleep(100000);
      continue;
    }

    /* Clear before showing data for this scan */
    clear_screen();
    printf("NFC reader: %s opened\n", nfc_device_get_name(dev));
    printf("%s (0x%04x) detected\n\n", system->name, system_code);
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
    printf("Polling 0x%04x\n\n", sensf_req[1] << 8 | sensf_req[2]);
  }

  nfc_close(dev);
  nfc_exit(ctx);
  printf("Program terminated.\n");
  return 0;
}
