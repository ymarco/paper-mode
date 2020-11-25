CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-parameter -fpic
CFLAGS += `pkg-config --cflags gtk+-3.0 --libs cairo`
CFLAGS += -I/usr/local/include
CFLAGS += -lm

# all : mupdf-module.so
mupdf-gtk: mupdf-gtk.c
	$(CC) $(CFLAGS) -o $@ $^ /usr/local/lib/libmupdf.a /usr/local/lib/libmupdf-third.a

debug: CFLAGS += -DDEBUG -g
debug: mupdf-module.so

mupdf-module.so : mupdf-module.c
	$(CC) -shared $(CFLAGS) -o $@  $^

clean :
	$(RM) mupdf-module.so

.PHONY : clean all
