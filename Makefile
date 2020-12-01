CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-parameter -fpic
CFLAGS += `pkg-config --cflags gtk+-3.0 --libs cairo`
CFLAGS += -I/usr/local/include
CFLAGS += -lm

# all : paper-module.so
paper-gtk: paper-gtk.c
	$(CC) $(CFLAGS) -o $@ $^ /usr/local/lib/libmupdf.a /usr/local/lib/libmupdf-third.a

debug: CFLAGS += -DDEBUG -g
debug: paper-module.so

paper-module.so : paper-module.c
	$(CC) -shared $(CFLAGS) -o $@  $^

clean :
	$(RM) paper-module.so paper-gtk

.PHONY : clean all
