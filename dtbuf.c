#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dtbuf.h"

#define DTBUF_CHUNK_PAYLOAD_SIZE 4000

struct header {
  time_ms timestamp;
  chunk_length data_length;
};

#define DTBUF_CHUNK_SIZE (sizeof (struct header) + DTBUF_CHUNK_PAYLOAD_SIZE)

int dtbuf_init(struct dtbuf *dtbuf, size_t capacity) {
  // to avoid splitting a chunk on the circular buffer boundaries, add
  // (DTBUF_CHUNK_SIZE-1) bytes at the end: a chunk starting at (capacity-1)
  // will still fit
  dtbuf->real_capacity = capacity + DTBUF_CHUNK_SIZE - 1;
  if (!(dtbuf->data = malloc(dtbuf->real_capacity))) {
    return 1;
  }
  dtbuf->capacity = capacity;
  dtbuf->head = 0;
  dtbuf->tail = 0;
  return 0;
}

void dtbuf_free(struct dtbuf *dtbuf) {
  free(dtbuf->data);
}

int dtbuf_is_empty(struct dtbuf *dtbuf) {
  return dtbuf->head == dtbuf->tail;
}

int dtbuf_is_full(struct dtbuf *dtbuf) {
  // When dtbuf->head >= dtbuf->capacity, it "cycles" (reset to 0) if and
  // only if there is enough space at the start for a full chunk.
  // Thus, if dtbuf->head has not cycled while it is after capacity, then the
  // buffer is full.
  // Else, if head >= tail, there is always enough space (by design).
  // Else (if head < tail), there is enough space only if dtbuf->tail is far
  // enough (ie we can put a full chunk at the start).
  return dtbuf->head >= dtbuf->capacity || (dtbuf->head < dtbuf->tail
                                            && dtbuf->tail - dtbuf->head <=
                                            DTBUF_CHUNK_SIZE);
}

time_ms dtbuf_next_timestamp(struct dtbuf *dtbuf) {
  struct header *header = (struct header *) &dtbuf->data[dtbuf->tail];
  return header->timestamp;
}

ssize_t dtbuf_write_chunk(struct dtbuf *dtbuf, int fd_in, time_ms timestamp) {
  ssize_t r;
  struct header header;
  // directly write to dtbuf, at the right index
  int payload_index = dtbuf->head + sizeof(struct header);
  if ((r =
       read(fd_in, &dtbuf->data[payload_index],
            DTBUF_CHUNK_PAYLOAD_SIZE)) > 0) {
    // write headers
    header.timestamp = timestamp;
    header.data_length = (chunk_length) r;
    memcpy(&dtbuf->data[dtbuf->head], &header, sizeof(header));
    dtbuf->head = payload_index + r;
    if (dtbuf->head >= dtbuf->capacity && dtbuf->tail >= DTBUF_CHUNK_SIZE) {
      // not enough space at the end of the buffer, cycle if there is enough
      // at the start
      dtbuf->head = 0;
    }
  } else if (r == -1) {
    perror("read()");
  }
  return r;
}

ssize_t dtbuf_read_chunk(struct dtbuf * dtbuf, int fd_out) {
  ssize_t w;
  struct header *pheader = (struct header *) &dtbuf->data[dtbuf->tail];
  struct header header;
  chunk_length length = pheader->data_length;
  // directly read from dtbuf, at the right index
  int payload_index = dtbuf->tail + sizeof(struct header);
  if ((w = write(fd_out, &dtbuf->data[payload_index], length)) > 0) {
    if (w == length) {
      // we succeed to write all the data
      dtbuf->tail = payload_index + w;
      if (dtbuf->tail >= dtbuf->capacity) {
        // the next chunk cannot be after capacity
        dtbuf->tail = 0;
        if (dtbuf->head >= dtbuf->capacity) {
          // can happen if capacity < DTBUF_CHUNK_SIZE
          dtbuf->head = 0;
        }
      }
    } else {
      dtbuf->tail += w;
      // set the timestamp for writing at the new tail position
      header.timestamp = pheader->timestamp;
      // set the remaining length
      header.data_length = length - w;
      memcpy(&dtbuf->data[dtbuf->tail], &header, sizeof(header));
    }
    if (dtbuf->head >= dtbuf->capacity && dtbuf->tail >= DTBUF_CHUNK_SIZE) {
      // there is enough space at the start now, head can cycle
      dtbuf->head = 0;
    }
  } else if (w == -1) {
    perror("write()");
  }
  return w;
}
