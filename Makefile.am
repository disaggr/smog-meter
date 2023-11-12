
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = smog-meter

smog_meter_CPPFLAGS = -Isrc/ -Wall -Wextra -Werror

smog_meter_SOURCES = src/smog-meter.c src/smog-meter.h \
                     src/args.c \
                     src/util.c src/util.h \
                     src/vmas.c src/vmas.h

noinst_PROGRAMS = fuzzer
fuzzer_CPPFLAGS = -Wall -Wextra

fuzzer_SOURCES = src/fuzzer.c \
                 src/util.c src/util.h
