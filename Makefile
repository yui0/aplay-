# ©2017-2026 YUICHIRO NAKADA

PROGRAM = aplay+
UI_PROGRAM = aplay+ui

ifneq (, $(shell which gcc))
CC = gcc
endif
ifneq (, $(shell which clang))
CC = clang
endif
ifneq (, $(shell which icc))
CC = icc
endif
# clang 22 can ICE on this TU; prefer gcc when available for reliable builds.
ifneq (, $(shell which gcc))
CC = gcc
endif
CFLAGS = -O3 -ffunction-sections -fdata-sections -funroll-loops -finline-functions -ftree-vectorize
#CFLAGS = -Os -ffunction-sections -fdata-sections -funroll-loops -finline-functions -ftree-vectorize -march=native
LDFLAGS = -lasound -lm -Wl,-s -Wl,--gc-sections
UI_LDFLAGS = -lasound -lglfw -lGL -lpthread -ldl -lm
#LDFLAGS = -lasound -lm -Wl,-s -Wl,-dead_strip

.PHONY: all
all: $(PROGRAM)

$(PROGRAM): % : %.o
	$(CC) $< -o $@ $(LDFLAGS)

.PHONY: ui
ui: $(UI_PROGRAM)

$(UI_PROGRAM): aplay+ui.c aplay+engine.h luna-ui.h
	$(CC) $(CFLAGS) -o $@ aplay+ui.c $(UI_LDFLAGS)

%.o : %.c $(HEAD)
	$(CC) $(CFLAGS) -c $(@F:.o=.c) -o $@

.PHONY: clean
clean:
	$(RM) $(PROGRAM) $(UI_PROGRAM) $(OBJS) _depend.inc *.o

.PHONY: depend
depend: $(OBJS:.o=.c)
	-@ $(RM) _depend.inc
	-@ for i in $^; do cpp -MM $$i | sed "s/\ [_a-zA-Z0-9][_a-zA-Z0-9]*\.c//g" >> _depend.inc; done

-include _depend.inc
