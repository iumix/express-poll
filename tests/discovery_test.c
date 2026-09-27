/* Exercise discovery without an NFC reader. Link platform.c against these
 * stand-ins for libnfc and glob; run both Linux and macOS path branches. */
#include "common/platform.h"
#include <assert.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int calls, mode, patterns;
static int sentinel;
static nfc_device *reader = (nfc_device *)&sentinel;

nfc_device *nfc_open(nfc_context *ctx, const nfc_connstring connection) {
  (void)ctx;
  ++calls;
  if (mode == 1) {
    assert(connection && !strcmp(connection, "pn532_uart:chosen"));
    return NULL; /* Explicit overrides must never fall back. */
  }
  if (!connection) return mode == 2 ? reader : NULL;
  assert(!strcmp(getenv("LIBNFC_LOG_LEVEL"), "0"));
  if (strstr(connection, "good") && mode == 3) return reader;
  return NULL;
}

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int), glob_t *result) {
  (void)flags;
  (void)errfunc;
#ifdef __APPLE__
  if (!patterns) assert(!strcmp(pattern, "/dev/cu.usbserial*"));
#else
  if (!patterns) assert(!strcmp(pattern, "/dev/ttyUSB*"));
#endif
  ++patterns;
  static char *ports[] = {"/dev/busy", "/dev/not-a-reader", "/dev/good", NULL};
  result->gl_pathv = ports;
  result->gl_pathc = 3;
  return 0;
}
void globfree(glob_t *result) { (void)result; }

int main(void) {
  setenv("LIBNFC_LOG_LEVEL", "1", 1);
  setenv("LIBNFC_DEFAULT_DEVICE", "pn532_uart:chosen", 1);
  mode = 1;
  assert(!transit_open(NULL));
  assert(calls == 1 && patterns == 0);
  unsetenv("LIBNFC_DEFAULT_DEVICE");
  mode = 2; calls = 0;
  assert(transit_open(NULL) == reader);
  assert(calls == 1 && patterns == 0);
  mode = 3; calls = 0;
  assert(transit_open(NULL) == reader);
  assert(calls == 4 && patterns == 1);
  assert(!strcmp(getenv("LIBNFC_LOG_LEVEL"), "1"));
  mode = 4; calls = 0; patterns = 0;
  assert(!transit_open(NULL));
#ifdef __APPLE__
  assert(patterns == 4);
#else
  assert(patterns == 3);
#endif
  assert(!strcmp(getenv("LIBNFC_LOG_LEVEL"), "1"));
  bool verbose;
  char *valid[] = {"test", "-vv", "--"};
  char *invalid[] = {"test", "-x"};
  assert(transit_verbose_args(3, valid, &verbose) == 0 && verbose);
  assert(transit_verbose_args(2, invalid, &verbose) < 0);
  puts("Discovery and argument tests passed.");
  return 0;
}
