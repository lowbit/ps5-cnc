/* Runs the importer on a PC: import-test [options] <game folder> <source>...
 *   --remove              delete the sources once read (as for the incoming folder)
 *   --known <kind> <sha1> add a checksum to the table of known files (rafiles.h)
 * Prints the notes and, at the end, "state <name>", "placed <slots>" and "present <slots>" with
 * each slot's path, for test/run-tests.py to check. A listing prints "link <name> <url>" lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "importer.h"
#include "rafiles.h"
#include "sha1.h"

static void print_slots(const char *label, unsigned slots)
{
    printf("%s", label);
    for (int slot = 0; slot < RA_SLOT_COUNT; slot++)
        if (slots & 1u << slot)
            printf(" %s", ra_slot_path((ra_slot_t)slot));
    printf("\n");
}

int main(int argc, char **argv)
{
    static char sources[IMPORT_MAX_SOURCES][IMPORT_URL];
    static const char *names[] = { "idle", "running", "listing", "done", "failed", "cancelled" };
    int flags = 0, count = 0, cancel_after = -1;
    const char *game = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--remove") == 0)
            flags |= IMPORT_REMOVE_SOURCES;
        else if (strcmp(argv[i], "--cancel-after") == 0 && i + 1 < argc)
            cancel_after = atoi(argv[++i]);
        else if (strcmp(argv[i], "--known") == 0 && i + 2 < argc)
        {
            uint8_t digest[20];
            if (sha1_parse(digest, argv[i + 2]) != 0)
            {
                fprintf(stderr, "bad checksum %s\n", argv[i + 2]);
                return 2;
            }
            ra_add_known(argv[i + 1], digest);
            i += 2;
        }
        else if (game == NULL)
            game = argv[i];
        else if (count < IMPORT_MAX_SOURCES)
            snprintf(sources[count++], IMPORT_URL, "%s", argv[i]);
    }
    if (game == NULL || count == 0)
    {
        fprintf(stderr, "usage: import-test [--remove] [--known kind sha1] <game folder> <source>...\n");
        return 2;
    }

    import_start(sources, count, game, flags);
    import_status_t status;
    char text[512];
    int ok, polls = 0;
    for (;;)
    {
        import_status(&status);
        while (import_take_note(text, sizeof(text), &ok))
            printf("note %s %s\n", ok ? "ok" : "bad", text);
        if (status.state != IMPORT_RUNNING)
            break;
        if (cancel_after >= 0 && polls++ == cancel_after)
            import_cancel();
        usleep(10000);
    }
    import_finish();
    while (import_take_note(text, sizeof(text), &ok))
        printf("note %s %s\n", ok ? "ok" : "bad", text);
    printf("state %s\n", names[status.state]);
    if (status.state == IMPORT_LISTING)
    {
        const listing_t *listing = import_listing();
        for (int i = 0; i < listing->count; i++)
            printf("link %s%s %s\n", listing->links[i].name, listing->links[i].directory ? "/" : "",
                   listing->links[i].url);
    }
    print_slots("placed", status.placed);
    print_slots("present", ra_scan(game));
    return 0;
}
