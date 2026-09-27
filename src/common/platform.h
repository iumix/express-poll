#ifndef TRANSIT_PLATFORM_H
#define TRANSIT_PLATFORM_H
#include <nfc/nfc.h>
#include <stdbool.h>
void transit_sleep_ms(unsigned int milliseconds);
void transit_console_init(void);
nfc_device *transit_open(nfc_context *context);
int transit_verbose_args(int argc, char **argv, bool *verbose);
#endif
