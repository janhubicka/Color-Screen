CFLAGS= -ffast-math -funroll-all-loops -Wno-deprecated-declarations -Ofast `pkg-config gtk+-x11-2.0 --cflags` `pkg-config gtk+-x11-2.0 --libs`

all: barveni-bin

barveni-bin: barveni.o gtk-image-viewer.o
	$(CC) -o $@ $^ $(CFLAGS)  `pkg-config gtk+-x11-2.0 --cflags` `pkg-config gtk+-x11-2.0 --libs` -lnetpbm -export-dynamic -lm

gtk-image-viewer.c: gtk-image-viewer.gob
	gob2 gtk-image-viewer.gob
	patch -p0 <viewer-patch

%.o: %.C $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
