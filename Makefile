.POSIX:
.SUFFIXES:
CC = gcc
CFLAGS = -g3 -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Wconversion -fsanitize=undefined -fsanitize-trap -Itree-sitter/lib/include

windows: main_win32.o buffer.o gui.o util.o log.o vim.o ebuf.o
	$(CC) $(LDFLAGS) -mwindows -o bed $^ $(LDLIBS)
test: util.o buffer_stub.o ebuf.o vim.o vim_test.c
	$(CC) $(CFLAGS) -o test $^
clean:
	rm -f *.exe *.o

main_win32.o: main_win32.c gui.h buffer.h util.h syntax.h log.h
buffer.o: buffer.c buffer.h util.h
gui.o: gui.c gui.h buffer.h util.h syntax.h log.h
util.o: util.c util.h
syntax.o: syntax.c syntax.h buffer.h util.h
log.o: log.c log.h util.h
vim.o: vim.c vim.h
ebuf.o: ebuf.c ebuf.h
buffer_stub.o: buffer_stub.c buffer.h
tree-sitter.o: tree-sitter/lib/src/lib.c
	$(CC) -Itree-sitter/lib/src -Itree-sitter/lib/include -O3 -o $@ -c $<
tree-sitter-c.o: tree-sitter-c/src/parser.c
	$(CC) -Itree-sitter-c/src -O3 -o $@ -c $<

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<
