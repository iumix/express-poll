/* Compatibility functions needed by the pinned libnfc Windows build. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int setenv(const char *name, const char *value, int overwrite) {
  if (!name || !*name || strchr(name, '=') || !value) {
    errno = EINVAL;
    return -1;
  }
  if (!overwrite && getenv(name)) return 0;
  int error = _putenv_s(name, value);
  if (!error) return 0;
  errno = error;
  return -1;
}

void unsetenv(const char *name) {
  if (name && *name && !strchr(name, '=')) _putenv_s(name, "");
}
