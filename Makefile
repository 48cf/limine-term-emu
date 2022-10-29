LIBS = sdl2
CFLAGS = -ggdb -Wall -Wextra -Wpedantic -std=c11 -O3 $(shell pkg-config --cflags $(LIBS))
LDFLAGS = $(CFLAGS) $(shell pkg-config --libs $(LIBS))
CFILES = $(shell find . -name '*.c')
OBJFILES = $(patsubst %.c,%.o,$(CFILES))
DEPFILES = $(patsubst %.c,%.d,$(CFILES))

.PHONY: all
all: limine-term

.PHONY: clean
clean:
	rm -f $(OBJFILES) $(DEPFILES) limine-term

limine-term: $(OBJFILES)
	gcc $(LDFLAGS) -o $@ $^

%.o: %.c
	gcc $(CFLAGS) -I./terminal -MMD -MF $(patsubst %.c,%.d,$<) -o $@ -c $<

-include $(DEPFILES)
