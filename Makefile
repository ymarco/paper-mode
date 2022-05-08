CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wshadow
CFLAGS += -std=c99 -fpic
LIBS = gtk+-3.0 cairo
CFLAGS += `pkg-config --cflags $(LIBS)`
LDFLAGS += `pkg-config --libs $(LIBS)`
CFLAGS += -lm
CFLAGS += -lmupdf

O_DEBUG := 0  # debug binary
O_RELEASE := 0  # debug binary


ifeq ($(O_DEBUG),1)
	CFLAGS += -DDEBUG -g -Og
endif
ifeq ($(O_RELEASE),1)
	CFLAGS += -O3 -flto
endif


paper-module.so: paper-module.o PaperView.o symbols.o
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -o $@ $^

paper-module.o: PaperView.h from-webkit.h emacs-module.h
symbols.o: CFLAGS += -fvisibility=hidden
symbols.o: symbols.h

PaperView.o: PaperView.h PaperView.c

PaperView: PaperView.c PaperView.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ /usr/local/lib/libmupdf.a /usr/local/lib/libmupdf-third.a

clean :
	$(RM) paper-module.so PaperView *.o

.PHONY : clean all
