CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Isrc -D_POSIX_C_SOURCE=200809L
TARGET  = mdpdf
SRCS    = src/main.c src/input.c src/paper.c src/image.c src/pdf.c src/markdown.c
OBJS    = $(SRCS:.c=.o)
LDLIBS  = -lz -lm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all clean install
