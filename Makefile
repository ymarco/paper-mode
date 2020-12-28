CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wshadow
CFLAGS += -std=c99 -fpic
CFLAGS += `pkg-config --cflags gtk+-3.0 --libs cairo`
CFLAGS += -I/usr/local/include
CFLAGS += -lm
CFLAGS += -lmupdf -lmupdf-third

O_DEBUG := 0  # debug binary
O_RELEASE := 0  # debug binary


ifeq ($(O_DEBUG),1)
	CFLAGS += -DDEBUG -g -Og
endif
ifeq ($(O_RELEASE),1)
	CFLAGS += -O3 -flto
endif


paper-module.so: paper-module.o PaperView.o
	$(CC) $(CFLAGS) -shared -o $@ $^ /usr/lib64/libmupdf.a /usr/lib64/libmupdf-third.a

paper-module.o: PaperView.h from-webkit.h emacs-module.h

PaperView: PaperView.c PaperView.h
	$(CC) $(CFLAGS) -o $@ $^ /usr/local/lib/libmupdf.a /usr/local/lib/libmupdf-third.a

clean :
	$(RM) paper-module.so PaperView *.o

.PHONY : clean all
