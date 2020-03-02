# Target library
lib := libfs.a
objs	:= fs.o disk.o
CC	:= gcc
CFLAGS	:= -Wall -Wextra -Werror

ifneq ($(V),1)
Q = @
endif
all: $(lib)

## TODO: Phase 1 and Phase 2

deps := $(patsubst %.o,%.d,$(objs))
-include $(deps)
DEPFLAGS = -MMD -MF $(@:.o=.d)

## TODO: Phase 1.1

%.a: $(objs) queue.o thread.o
	@echo "AR $@"
	$(Q)ar rcs $@ $^
%.o: %.c
	@echo "CC $@"
	$(Q)$(CC) $(CFLAGS) -g -c -o $@ $< $(DEPFLAGS)
clean:
	@echo "clean"
	$(Q)rm -f $(lib) $(objs) $(deps)