#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "dtbuf.h"
#include "time_ms.h"

/*
 * Write to stdout DELAY ms later the data received on stdin.
 */

struct dtbuf dtbuf;

struct pollfd fds_all[] = {
  {0 /* stdin */ , POLLIN | POLLHUP},
  {1 /* stdout */ , POLLOUT | POLLHUP}
};

#define DELAY 1000              // 1 second

int main(int argc, char *argv[]) {
  int r;                        // returned by poll()
  struct pollfd *fds;           // selected fds
  int fds_count;
  int has_next_chunk = 0;
  time_ms next_chunk_timestamp;
  time_ms now;
  time_ms wait_delay;
  int poll_stdin;
  int poll_stdout;
  int timeout;
  int in_closed = 0;
  int out_closed = 0;

  if (dtbuf_init(&dtbuf, 1024 * 1024)) {
    fprintf(stderr, "dtbuf initialization failed\n");
    exit(1);
  }

  do {
    // current time
    now = get_time_ms();

    // we want to poll stdin when we can store what we will read
    poll_stdin = !in_closed && !dtbuf_is_full(&dtbuf);

    // we want to poll stdout if we have a next chunk to write now
    // if we only have a next chunk to write later, we set a timeout instead
    if (!out_closed && has_next_chunk) {
      wait_delay = next_chunk_timestamp + DELAY - now;
      if (wait_delay <= 0) {
        // data to write as soon as possible
        poll_stdout = 1;
        timeout = -1;
      } else {
        // data to write later
        poll_stdout = 0;
        timeout = wait_delay;
      }
    } else {
      // no data to write at all
      poll_stdout = 0;
      timeout = -1;
    }

    if (!poll_stdin) {
      if (!poll_stdout) {
        // we do not want to poll at all, simply wait for timeout
        if (timeout <= 0) {
          // no poll, no timeout: nothing to do, we reached the end
          goto end;
        }
        if (usleep(timeout * 1000)) {
          perror("usleep()");
          exit(2);
        }
        fds_count = 0;
        // we won't call poll(), so we init the expected result
        r = 0;
      } else {
        // we do not want to poll stdin, only stdout
        fds = &fds_all[1];
        fds_count = 1;
      }
    } else if (!poll_stdout) {
      // we want to poll stdin only
      fds = fds_all;
      fds_count = 1;
    } else {
      // we want to poll both stdin and stdout
      fds = fds_all;
      fds_count = 2;
    }

    // poll() if needed
    if (fds_count > 0 && (r = poll(fds, fds_count, timeout)) == -1) {
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
      if (poll_stdout && (fds_all[1].revents & (POLLOUT | POLLHUP))) {
        // read from dtbuf and write to stdout
        if (dtbuf_read_chunk(&dtbuf, 1 /* stdout */ ) <= 0) {
          out_closed = 1;
        }
      }
      if (poll_stdin && (fds_all[0].revents & (POLLIN | POLLHUP))) {
        // we may have waited, get the new current time
        now = get_time_ms();
        // read from stdin and write to dtbuf
        if (dtbuf_write_chunk(&dtbuf, 0 /* stdin */ , now) <= 0) {
          in_closed = 1;
        }
      }
    }

    // update next_chunk state
    has_next_chunk = !dtbuf_is_empty(&dtbuf);
    if (has_next_chunk) {
      dtbuf_peek_headers(&dtbuf, &next_chunk_timestamp, NULL);
    }
  } while (!in_closed || (has_next_chunk && !out_closed));

end:
  dtbuf_free(&dtbuf);
  return 0;
}
