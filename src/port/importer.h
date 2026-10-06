/* The importer: finds Red Alert's files in what the player has and puts them where Vanilla Conquer
 * reads them (rafiles.h). A source is a link (http:// or https://), a file or a folder on the
 * console, or one of the free discs ("freeware:allied", "freeware:soviet", see freeware.txt).
 * Files are read out of disc images (ISO, BIN, IMG, MDF), archives (ZIP, 7Z, RAR) nested in each
 * other, folders, and The First Decade's InstallShield cabinets, streamed where they can be: a
 * download is read once, as it arrives. One import runs at a time, on a thread of its own. */
#ifndef PS5_IMPORTER_H
#define PS5_IMPORTER_H

#include <stddef.h>

#include "listing.h"

#define IMPORT_URL LINK_URL
#define IMPORT_MAX_SOURCES MAX_LINKS

/* Deletes each source on the console once it has been read: what was sent or copied into the
 * incoming folder. */
#define IMPORT_REMOVE_SOURCES 1

typedef enum
{
    IMPORT_IDLE,
    IMPORT_RUNNING,
    IMPORT_LISTING, /* the one link was a web page: its links are in import_listing() */
    IMPORT_DONE,
    IMPORT_FAILED, /* nothing was added, and a source could not be read */
    IMPORT_CANCELLED,
} import_state_t;

typedef struct
{
    import_state_t state;
    long long done, total; /* bytes of the source being read; total is -1 when unknown */
    int source_index, source_count;
    char source[128];  /* the source being read */
    char current[160]; /* what is being written out of it */
    unsigned placed;   /* the slots this import filled, 1 << slot */
} import_status_t;

void import_start(char sources[][IMPORT_URL], int count, const char *game_folder, int flags);
void import_status(import_status_t *status);
const listing_t *import_listing(void);
void import_cancel(void);

/* Waits for the import to end and makes the importer idle again. */
void import_finish(void);

/* Takes the oldest note the importer left, in order: what it added, kept or could not read.
 * 1 when there was one. */
int import_take_note(char *text, size_t size, int *ok);

#endif
