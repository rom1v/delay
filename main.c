#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dtbuf.h"
#include "errno.h"
#include "time_ms.h"

/*
 * Write to stdout DELAY ms later the data received on stdin.
 */

struct dtbuf dtbuf;

// stdout is at index 0 because we will always poll it
struct pollfd fds_all[] = {
  { 1 /* stdout */ , POLLOUT },
  { 0 /* stdin */ , POLLIN }
};

// default values, changed by parse_cli()
int delay = 5000;               // in ms
size_t dtbufsize = 1024 * 1024; // in bytes

void print_syntax(char *arg0) {
  fprintf(stderr, "Syntax: %s [-b <dtbufsize>] <delay>\n", arg0);
}

// parse command: delay [-b <dtbufsize>] <delay_ms>
// these commands should work:
//   - delay 5s
//   - delay -b 10m 4000
//   - delay 4k -b 10000k
void parse_cli(int argc, char *argv[]) {
#define SYNTAX_ERROR 9
#define OPT_NONE 0
#define OPT_DTBUFSIZE 1
#define ARG_VALUE 1
#define ARG_DTBUFSIZE (1 << 1)
  int ctx = OPT_NONE;
  char *arg;
  int args_handled = 0;
  char *garbage;
  int i;
  for (i = 1; i < argc; i++) {
    arg = argv[i];
  handle_arg:
    if (ctx == OPT_DTBUFSIZE) {
      if (args_handled & ARG_DTBUFSIZE) {
        // dtbufsize parameter present twice
        print_syntax(argv[0]);
        exit(SYNTAX_ERROR);
      }
      // parse dtbufsize parameter
      errno = 0;
      dtbufsize = strtol(arg, &garbage, 0);
      if (errno) {
        perror("strtol()");
        exit(10);
      }
      // handle modifiers for Kb, Mb and Gb
      if (strcmp("k", garbage) == 0) {
        dtbufsize <<= 10;
      } else if (strcmp("m", garbage) == 0) {
        dtbufsize <<= 20;
      } else if (strcmp("g", garbage) == 0) {
        dtbufsize <<= 30;
      } else if (garbage[0] != '\0') {
        fprintf(stderr, "dtbufsize value contains garbage: %s\n", garbage);
        exit(SYNTAX_ERROR);
      }
      args_handled |= ARG_DTBUFSIZE;
      ctx = OPT_NONE;
    } else if (strncmp("-b", arg, 2) == 0) {
      ctx = OPT_DTBUFSIZE;
      if (strlen(&arg[2]) > 0) {
        // handle "-b12" like "-b 12"
        arg = &arg[2];
        goto handle_arg;        // yeah, this is ugly, but straightforward!
      }
    } else {
      if (args_handled & ARG_VALUE) {
        // delay value present twice
        print_syntax(argv[0]);
        exit(SYNTAX_ERROR);
      }
      // parse delay value
      errno = 0;
      delay = strtol(arg, &garbage, 0);
      if (errno) {
        perror("strtol()");
        exit(10);
      }
      // handle modifiers for seconds, minutes and hours
      if (strcmp("s", garbage) == 0) {
        delay *= 1000;
      } else if (strcmp("m", garbage) == 0) {
        delay *= 60 * 1000;
      } else if (strcmp("h", garbage) == 0) {
        delay *= 60 * 1000 * 1000;      // likely to be useless
      } else if (garbage[0] != '\0') {
        fprintf(stderr, "delay value contains garbage: %s\n", garbage);
        exit(SYNTAX_ERROR);
      }
      args_handled |= ARG_VALUE;
    }
  }
}

int main(int argc, char *argv[]) {
  int r;                        // returned by poll()
  int fds_count;
  int has_next_chunk = 0;
  time_ms next_chunk_timestamp = 0;
  time_ms now;
  time_ms wait_delay;
  // Note that stdin and stdout polling is not symetrical: we always poll
  // stdout for detecting POLLERR or POLLHUP (in that case, we want to stop
  // immediately), and we set POLLOUT only when we need to write.
  // On the contrary, we poll stdin only when we need to read (we don't want
  // to stop if we have chunks to write, even if stdin is closed).
  int poll_stdin;
  int pollout_stdout;
  int timeout;
  int in_closed = 0;
  int out_closed = 0;

  parse_cli(argc, argv);

  if (dtbuf_init(&dtbuf, dtbufsize)) {
    fprintf(stderr, "dtbuf initialization failed\n");
    exit(1);
  }

  do {
    // current time
    now = get_time_ms();

    // we want to poll stdin when we can store what we will read
    poll_stdin = !in_closed && !dtbuf_is_full(&dtbuf);

    // here, out_closed is always false
    // we want to pollout stdout if we have a next chunk to write now
    // if we only have a next chunk to write later, we set a timeout instead
    if (has_next_chunk) {
      wait_delay = next_chunk_timestamp + delay - now;
      if (wait_delay <= 0) {
        // data to write as soon as possible
        pollout_stdout = 1;
        timeout = -1;
      } else {
        // data to write later
        pollout_stdout = 0;
        timeout = wait_delay;
      }
    } else {
      // no data to write at all
      pollout_stdout = 0;
      timeout = -1;
    }

    // we always want to poll stdout for detecting POLLERR or POLLHUP
    fds_count = poll_stdin ? 2 : 1;
    fds_all[0].events = pollout_stdout ? POLLOUT : 0;

    // poll() the selected fds
    if ((r = poll(fds_all, fds_count, timeout)) == -1) {
      perror("poll()");
      exit(3);
    }

    if (r == 0) {
      // timeout occurs: we need to write the next chunk to stdout
      // read from dtbuf and write to stdout
      if (dtbuf_read_chunk(&dtbuf, 1 /* stdout */ ) <= 0) {
        out_closed = 1;
      }
    } else {
      if (fds_all[0].revents) {
        // stdout has revents
        if (fds_all[0].revents & (POLLOUT | POLLHUP)) {
          // read from dtbuf and write to stdout
          if (dtbuf_read_chunk(&dtbuf, 1 /* stdout */ ) <= 0) {
            out_closed = 1;
          }
        } else {
          out_closed = 1;
        }
      }
      if (poll_stdin && fds_all[1].revents) {
        // stdin has revents
        if (fds_all[1].revents & (POLLIN | POLLHUP)) {
          // we may have waited, get the new current time
          now = get_time_ms();
          // read from stdin and write to dtbuf
          if (dtbuf_write_chunk(&dtbuf, 0 /* stdin */ , now) <= 0) {
            in_closed = 1;
          }
        } else {
          in_closed = 1;
        }
      }
    }

    // update next_chunk state
    has_next_chunk = !dtbuf_is_empty(&dtbuf);
    if (has_next_chunk) {
      next_chunk_timestamp = dtbuf_next_timestamp(&dtbuf);
    }
    // always stop on out_closed
    // also stop when there will be no more chunks anymore
  } while (!out_closed && (!in_closed || has_next_chunk));

  dtbuf_free(&dtbuf);
  return 0;
}
