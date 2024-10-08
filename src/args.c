/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "./smog-meter.h"

static const char doc[] = "A dirty page counter";
static const char args_doc[] = "PID [VMA_NAME]";

static struct argp_option options[] = {
    { "monitor-interval", 'M', "INTERVAL", 0,
      "monitor and reporting interval in milliseconds", 0 },
    { "max-frames", 'n', "FRAMES", 0,
      "limit the number of frames captured", 0},
    { "track-softdirty", 'D', 0, 0,
      "track the softdirty bits for all pages", 0},
    { "track-accessed", 'T', 0, 0,
      "track the access bits for all pages (expensive)", 0},
    { "min-vma-reserved", 'r', "PAGES", 0,
      "the minimum reserved pages of a VMA to be reported", 1 },
    { "min-vma-committed", 'c', "PAGES", 0,
      "the minimum committed pages of a VMA to be reported", 1 },
    { "min-vma-accessed", 'a', "PAGES", 0,
      "the minimum dirty pages of a VMA to be reported", 1 },
    { "min-vma-dirty", 'd', "PAGES", 0,
      "the minimum dirty pages of a VMA to be reported", 1 },
    { "tracefile", 't', "FILE", 0,
      "an output file for detailed page trace data", 2 },
    { "verbose", 'v', 0, 0,
      "show additional output, pass multiple times for even more output", 3 },
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = (struct arguments*)state->input;

    switch (key) {
        case 'M':
            errno = 0;
            uint64_t millis = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid interval: %s", arg);
            arguments->delay = millis;
            break;
        case 'n':
            errno = 0;
            uint64_t num_frames = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid number of frames: %s", arg);
            arguments->frames = num_frames;
            break;
        case 'T':
            arguments->track_accessed = 1;
            break;
        case 'D':
            arguments->track_softdirty = 1;
            break;
        case 'r':
            errno = 0;
            arguments->min_vma_reserved = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid number: %s", arg);
            break;
        case 'c':
            errno = 0;
            arguments->min_vma_committed = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid number: %s", arg);
            break;
        case 'a':
            errno = 0;
            arguments->min_vma_accessed = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid number: %s", arg);
            break;
        case 'd':
            errno = 0;
            arguments->min_vma_dirty = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid number: %s", arg);
            break;
        case 't':
            // avoid leaking memory if -t passed multiple times
            free(arguments->tracefile);
            arguments->tracefile = strdup(arg);
            if (!arguments->tracefile)
                argp_failure(state, 1, errno, "unable to allocate memory");
            break;
        case 'v':
            arguments->verbose += 1;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 2)
                argp_usage(state);
            if (state->arg_num >= 1) {
                free(arguments->vma);
                arguments->vma = strdup(arg);
                if (!arguments->vma)
                    argp_failure(state, 1, errno, "unable to allocate memory");
            } else {
                if (!strcmp(arg, "self")) {
                    arguments->pid = getpid();
                    arguments->self_map = 1;
                } else {
                    errno = 0;
                    arguments->pid = strtoll(arg, NULL, 0);
                    if (errno != 0)
                        argp_failure(state, 1, errno, "invalid pid: %s", arg);
                }
            }
            break;

        case ARGP_KEY_END:
            if (state->arg_num < 1)
                argp_usage(state);

            if (arguments->self_map && !arguments->vma)
                argp_failure(state, 1, 0, "PID of self requires a VMA_NAME parameter to be set.");

            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

struct argp argp = { options, parse_opt, args_doc, doc, NULL, NULL, NULL };
