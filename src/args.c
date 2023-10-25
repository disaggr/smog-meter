/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include <argp.h>
#include <stdlib.h>

#include "./smog-meter.h"

static const char doc[] = "A dirty page counter";
static const char args_doc[] = "PID";

static struct argp_option options[] = {
    { "verbose", 'v', 0, 0, "show additional output", 0 },
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = (struct arguments*)state->input;

    switch (key) {
        case 'v':
            arguments->verbose = 1;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1)
                argp_usage(state);
            errno = 0;
            arguments->pid = strtoll(arg, NULL, 10);
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
