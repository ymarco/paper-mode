CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-parameter -fpic
CFLAGS += `pkg-config --cflags gtk+-3.0 --libs cairo`
CFLAGS += -I/usr/local/include
CFLAGS += -lm

O_DEBUG := 0  # debug binary
O_RELEASE := 0  # debug binary


ifeq ($(O_DEBUG),1)
	CFLAGS += -DDEBUG -g -Og
endif
ifeq ($(O_RELEASE),1)
	CFLAGS += -O3 -flto
endif


# all : paper-module.so
paper-gtk: paper-gtk.c
	$(CC) $(CFLAGS) -o $@ $^ /usr/local/lib/libmupdf.a /usr/local/lib/libmupdf-third.a

paper-module.so : paper-module.c
	$(CC) -shared $(CFLAGS) -o $@  $^

clean :
	$(RM) paper-module.so paper-gtk

.PHONY : clean all
