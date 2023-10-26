/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include <argp.h>
#include <stdlib.h>

#include "./smog-meter.h"

static const char doc[] = "A dirty page counter";
static const char args_doc[] = "PID";

static struct argp_option options[] = {
    { "monitor-interval", 'M', "INTERVAL", 0, "monitor and reporting interval in milliseconds", 0 },
    { "min-vma-size", 'm', "PAGES", 0, "the minimum size of a VMA to be considered", 0 },
    { "verbose", 'v', 0, 0, "show additional output", 0 },
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = (struct arguments*)state->input;

    switch (key) {
        case 'M':
            errno = 0;
            uint64_t millis = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid pid: %s", arg);
            arguments->delay = millis;
            break;
        case 'm':
            errno = 0;
            arguments->min_vma_size = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid size: %s", arg);
            break;
        case 'v':
            arguments->verbose = 1;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1)
                argp_usage(state);
            errno = 0;
            arguments->pid = strtoll(arg, NULL, 0);
            if (errno != 0)
                argp_failure(state, 1, errno, "invalid pid: %s", arg);
            break;

        case ARGP_KEY_END:
            if (state->arg_num < 1)
                argp_usage(state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

struct argp argp = { options, parse_opt, args_doc, doc, NULL, NULL, NULL };
