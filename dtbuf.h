#ifndef __H_DTBUF
#define __H_DTBUF

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include "time_ms.h"

/*
 * dtbuf means either "direct timestamped buffer" or "delta-t buffer".
 * - direct: it directly reads from and writes to a FILE
 * - timestamped: every chunk is preceded by a header containing the date of
 *                read
 * - delta-t: the purpose of this buffer is to keep a constant delay between
 *            reads from stdin and writes to stdout
 *
 * In order to avoid chunks to be split at the circular buffer boundaries, its
 * real capacity is larger than the declared one, so that every read/write of
 * DTBUF_CHUNK_SIZE can be done in once, without splitting.
 */

typedef uint16_t chunk_length;

struct dtbuf {
  char *data;
  size_t capacity;              // expected capacity
  size_t real_capacity;         // capacity + DTBUF_CHUNK_SIZE - 1
  int head;                     // index of the next chunk
  int tail;                     // index of the oldest chunk in memory
};

// init a dtbuf structure with an expected capacity
int dtbuf_init(struct dtbuf *dtbuf, size_t capacity);

// free dtbuf content (not dtbuf itself)
void dtbuf_free(struct dtbuf *dtbuf);

// read the timestamp from the headers of the next chunk to be read
// assumes !is_empty(dtbuf)
time_ms dtbuf_next_timestamp(struct dtbuf *dtbuf);

// indicates whether the buffer is empty
int dtbuf_is_empty(struct dtbuf *dtbuf);

// indicates whether there is not enough space for writing a new full chunk
int dtbuf_is_full(struct dtbuf *dtbuf);

// read a chunk from fd_in and write it to dtbuf
// assumes fd_in is ready and !is_full(dtbuf)
ssize_t dtbuf_write_chunk(struct dtbuf *dtbuf, int fd_in, time_ms timestamp);

// read a chunk from dtbuf and write it to fd_out
// must be called when the next chunk time is reached
// assumes fd_out is ready and !is_empty(dtbuf)
ssize_t dtbuf_read_chunk(struct dtbuf *dtbuf, int fd_out);

#endif
