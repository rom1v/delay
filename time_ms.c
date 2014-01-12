#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "time_ms.h"

time_ms get_time_ms() {
  struct timeval timeval;
  if (gettimeofday(&timeval, NULL)) {
    perror("gettimeofday()");
    exit(42);
  }
  return timeval.tv_sec * 1000 + timeval.tv_usec / 1000;
}
