#include <assert.h>
#include <stdlib.h>
#include <string.h>

int setenv(const char *, const char *, int);
void unsetenv(const char *);

int main(void) {
  const char *name = "TRANSIT_ENV_TEST";
  char long_value[4096];
  memset(long_value, 'x', sizeof(long_value) - 1);
  long_value[sizeof(long_value) - 1] = '\0';
  assert(setenv(name, long_value, 1) == 0);
  assert(!strcmp(getenv(name), long_value));
  assert(setenv(name, "preserved", 0) == 0);
  assert(!strcmp(getenv(name), long_value));
  assert(setenv(name, "replaced", 1) == 0);
  assert(!strcmp(getenv(name), "replaced"));
  assert(setenv("bad=name", "value", 1) == -1);
  unsetenv(name);
  assert(!getenv(name) || !*getenv(name));
  return 0;
}
