#define _POSIX_C_SOURCE 200809L

#include "expr.h"
#include "strtab.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum retcode {
    ERC_WRONG_EXPR = 1,
    ERC_NO_IN_FILE = 2,
    ERC_NO_OUT_FILE = 3,
    ERC_WRONG_PARAM = 4,
};

_Noreturn void usage(const char *argv0) {
    printf("Usage:\n"
           "\t%s [-f <format>] [-o <outfile>] [-O] [-d <var>] [-t] [-D <tracefile>] [-F <traceformat>] <expr>\n"
           "\t%s [-f <format>] [-o <outfile>] [-O] [-d <var>] [-t] [-D <tracefile>] [-F <traceformat>] -i <infile>\n"
           "\t%s [-O] [-d <var>] [-t] [-D <tracefile>] [-F <traceformat>] -c<asmfile> <expr>\n"
           "\t%s [-O] [-d <var>] [-t] [-D <tracefile>] [-F <traceformat>] -c<asmfile> -i <infile>\n"
           "<format> is one of tex, string, graph\n"
           "Default <outfile> is stdout\n", argv0, argv0, argv0, argv0);
    exit(ERC_WRONG_PARAM);
}

const char *make_input(const char *input, const char *arg, size_t *size) {
    // Use mmaped file if filename is provides or else just return arg
    if (!input) return arg;

    char *addr = NULL;
    int fd = open(input, O_RDONLY);

    struct stat stt;
    if (fd >= 0 && fstat(fd, &stt) >= 0) {
        *size = stt.st_size + 1;
        char *addr = mmap(NULL, *size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) addr = NULL;
    }

    close(fd);
    return addr;
}

int main(int argc, char **argv) {
    enum format fmt = fmt_string, tracefmt = -1U;
    const char *output = NULL, *input = NULL;
    const char *tracefile = NULL;
    const char *asmout = NULL;
    bool tracesteps = 0;

    for (int c; (c = getopt(argc, argv, "c:OD:d:ti:o:f:")) != -1;) {
        switch (c) {
        case 'F':
        case 'f': {
            enum format res;
            if (!strcmp(optarg, "tex"))
                res = fmt_tex;
            else if (!strcmp(optarg, "string"))
                res = fmt_string;
            else if (!strcmp(optarg, "graph"))
                res = fmt_graph;
            else usage(argv[0]);

            *(c == 'F' ? &tracefmt : &fmt) = res;
            break;
        }
        case 'o':
            output = optarg;
            break;
        case 'O':
            optimize = 1;
            break;
        case 't':
            tracesteps = 1;
            break;
        case 'D':
            tracefile = optarg;
            break;
        case 'i':
            input = optarg;
            break;
        case 'c':
            asmout = optarg;
            break;
        default:
            usage(argv[0]);
        }
    }
    if (!input && (argc <= optind || !argv[optind])) usage(argv[0]);
    if (tracefmt == -1U) tracefmt = fmt;

    size_t size = 0;
    const char *in = make_input(input, argv[optind], &size);
    if (!in) return ERC_NO_IN_FILE;

    FILE *out = output ? fopen(output, "w") : stdout;
    if (!out) return ERC_NO_OUT_FILE;

    struct strtab stab;
    init_strtab(&stab);

    struct expr *exp = parse_tree(&stab, in);
    if (!exp) return ERC_WRONG_EXPR;

    FILE *tfile = NULL;

    if (tracesteps) {
        if (tracefile) {
            tfile = fopen(tracefile, "w");
            if (!tfile) usage(argv[0]);
        }
        set_trace(tfile ? tfile : stdout, tracefmt);
    }

    if (optimize) exp = optimize_tree(exp, &stab);

    if (tfile && tracefmt == fmt_tex)
        fputs("\\bye\n", tfile);

    if (asmout) {
        FILE *as = fopen(asmout, "w");
        if (!as) usage(argv[0]);
        generate_code(exp, &stab, as);
        fclose(as);
    } else dump_tree(out, fmt, exp, &stab, 1);

    if (input) munmap((void *)in, size);
    if (tfile) fclose(tfile);

    free_strtab(&stab);
    free_tree(exp);
    return 0;
}
