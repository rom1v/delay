.PHONY : release install clean

OUT = delay
OBJ = main.o dtbuf.o time_ms.o
CFLAGS = -Wall -O3

release: delay

delay: $(OBJ)
	$(CC) $(CFLAGS) -o $(OUT) $(OBJ)

$(OBJ): dtbuf.h time_ms.h

clean:
	rm -f $(OBJ) $(OUT)

install: release
	install $(OUT) $(DESTDIR)/usr/local/bin
