/* Tiny command adapter. Inputs are hashes and flags; stdout is one integer.
 * The same pure header is exercised by the game-free policy test (tools/test_install_policy.c). */
#include <stdio.h>
#include <stdlib.h>
#include "install-policy.h"
static const char *value(const char *s) { return !strcmp(s, "-") ? "" : s; }
int main(int argc, char **argv)
{
    int result;
    if (argc == 10 && !strcmp(argv[1], "file")) {
        result = ci_file(value(argv[2]), value(argv[3]), atoi(argv[4]), value(argv[5]),
                         value(argv[6]), value(argv[7]), value(argv[8]), atoi(argv[9]));
    } else if (argc == 4 && !strcmp(argv[1], "actions")) {
        result = ci_actions((unsigned)strtoul(argv[2], NULL, 10), atoi(argv[3]));
    } else return 2;
    printf("%d\n", result);
    return 0;
}
