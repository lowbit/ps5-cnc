/* Runs the launcher on a PC: launcher-test <game folder> <incoming folder> <settings folder>. With
 * RA_LAUNCHER_KEYS and RA_LAUNCHER_SHOTS (see launcher.c) it plays a script and saves its views. */
#include <stdio.h>

#include "launcher.h"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: launcher-test <game folder> <incoming folder> <settings folder>\n");
        return 2;
    }
    int play = launcher_run(argv[1], argv[2], argv[3]);
    printf("launcher returned %d\n", play);
    return 0;
}
