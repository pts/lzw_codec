CWFLAGS = -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations
CC = gcc

.PHONY: clean

lzw_codec: lzw_codec.c
	$(CC) -DNDEBUG -s -O3 -ansi -pedantic $(CWFLAGS) $< -o $@

clean:
	rm -f lzw_codec
