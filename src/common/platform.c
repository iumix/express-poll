#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#include <errno.h>
#include <time.h>
#endif

void transit_sleep_ms(unsigned int milliseconds) {
#ifdef _WIN32
  Sleep(milliseconds);
#else
  struct timespec delay = {milliseconds / 1000,
                          (long)(milliseconds % 1000) * 1000000L};
  while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
#endif
}

void transit_console_init(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode;
  if (GetConsoleMode(output, &mode))
    SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  SetConsoleOutputCP(CP_UTF8);
#endif
}

int transit_verbose_args(int argc, char **argv, bool *verbose) {
  *verbose = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--") && i == argc - 1) break;
    if (argv[i][0] != '-' || argv[i][1] != 'v') goto usage;
    for (const char *p = argv[i] + 1; *p; ++p)
      if (*p != 'v') goto usage;
    *verbose = true;
  }
  return 0;
usage:
  fprintf(stderr, "Usage: %s [-v]\n", argv[0]);
  return -1;
}

static nfc_device *quiet_open(nfc_context *context, const char *connection) {
  const char *level = getenv("LIBNFC_LOG_LEVEL");
  /* Failed probes are expected; preserve detailed logging when requested. */
  if (!level || atoi(level) > 1) return nfc_open(context, connection);
  char *saved = malloc(strlen(level) + 1);
  if (!saved) return nfc_open(context, connection);
  strcpy(saved, level);
#ifdef _WIN32
  _putenv_s("LIBNFC_LOG_LEVEL", "0");
#else
  setenv("LIBNFC_LOG_LEVEL", "0", 1);
#endif
  nfc_device *device = nfc_open(context, connection);
#ifdef _WIN32
  _putenv_s("LIBNFC_LOG_LEVEL", saved);
#else
  setenv("LIBNFC_LOG_LEVEL", saved, 1);
#endif
  free(saved);
  return device;
}

static nfc_device *try_port(nfc_context *context, const char *port) {
  nfc_connstring connection;
  int n = snprintf(connection, sizeof(connection), "pn532_uart:%s", port);
  if (n < 0 || (size_t)n >= sizeof(connection)) return NULL;
  /* libnfc verifies PN532 communication before accepting the port. */
  nfc_device *device = quiet_open(context, connection);
  if (device) fprintf(stderr, "NFC reader found: %s\n", connection);
  return device;
}

nfc_device *transit_open(nfc_context *context) {
  const char *explicit_device = getenv("LIBNFC_DEFAULT_DEVICE");
  /* Never silently substitute another reader for an explicit override. */
  if (explicit_device && *explicit_device)
    return nfc_open(context, explicit_device);
  nfc_device *device = quiet_open(context, NULL);
  if (device) return device;
#ifdef _WIN32
  /* DOS device names avoid stale/duplicate SERIALCOMM registry entries and
     include COM numbers above 255. Grow the buffer for heavily loaded hosts. */
  DWORD size = 4096;
  char *ports = NULL;
  for (;;) {
    ports = malloc(size);
    if (!ports) return NULL;
    if (QueryDosDeviceA(NULL, ports, size)) break;
    DWORD error = GetLastError();
    free(ports);
    ports = NULL;
    if (error != ERROR_INSUFFICIENT_BUFFER || size >= 1048576) break;
    size *= 2;
  }
  if (ports) {
    for (const char *port = ports; *port; port += strlen(port) + 1) {
      if (strncmp(port, "COM", 3) || !port[3] ||
          strspn(port + 3, "0123456789") != strlen(port + 3)) continue;
      device = try_port(context, port);
      if (device) break;
    }
    free(ports);
  }
#else
#ifdef __APPLE__
  const char *patterns[] = {"/dev/cu.usbserial*", "/dev/cu.usbmodem*",
                           "/dev/cu.SLAB_USBtoUART*", "/dev/cu.wchusbserial*"};
#else
  const char *patterns[] = {"/dev/ttyUSB*", "/dev/ttyACM*", "/dev/ttyAMA*"};
#endif
  for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
    glob_t ports = {0};
    if (glob(patterns[i], 0, NULL, &ports) == 0) {
      for (size_t j = 0; j < ports.gl_pathc; ++j) {
        device = try_port(context, ports.gl_pathv[j]);
        if (device) break;
      }
    }
    globfree(&ports);
    if (device) break;
  }
#endif
  if (!device)
    fprintf(stderr, "No PN532 reader found. Check UART mode, serial permissions, "
                    "and whether another program has the port open.\n"
                    "Set LIBNFC_DEFAULT_DEVICE to select a reader explicitly.\n");
  return device;
}
