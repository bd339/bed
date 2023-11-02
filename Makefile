.POSIX:
.SUFFIXES:
CC     = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Wconversion -fsanitize=undefined -fsanitize-trap -g3
LDLIBS = -lgdi32 -luser32

windows: main_win32.o buffer.o gui.o util.o
	$(CC) $(LDFLAGS) -mwindows -o bed$(EXE) $^ $(LDLIBS)
test: test.o buffer.o util.o
	$(CC) -o testrunner$(EXE) $^ && ./testrunner$(EXE)
clean:
	rm -f bed$(EXE) *.o

main_win32.o: main_win32.c gui.h buffer.h util.h
buffer.o: buffer.c buffer.h util.h
gui.o: gui.c gui.h buffer.h util.h
util.o: util.c util.h
test.o: test.c clove-unit.h
	$(CC) -c $<

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<
