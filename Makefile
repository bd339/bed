.POSIX:
.SUFFIXES:
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Wconversion -fsanitize=undefined -fsanitize-trap -g3
LDLIBS = -lgdi32 -luser32

windows: main_win32.o buffer.o gui.o util.o
	$(CC) $(LDFLAGS) -mwindows -o bed$(EXE) $^ $(LDLIBS)
test: test.o buffer_test.o util_test.o
	$(CC) -o testrunner$(EXE) $^ && ./testrunner$(EXE)
clean:
	rm -f bed$(EXE) *.o

main_win32.o: main_win32.c gui.h buffer.h util.h
buffer.o: buffer.c buffer.h util.h
gui.o: gui.c gui.h buffer.h util.h
util.o: util.c util.h
buffer_test.o: buffer.c buffer.h util.h
	$(CC) -DTEST $(CFLAGS) -o $@ -c $<
util_test.o: util.c util.h
	$(CC) -DTEST $(CFLAGS) -o $@ -c $<
test.o: test.c clove-unit.h
	$(CC) -c $<

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<
