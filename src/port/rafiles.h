/* What the importer knows about Red Alert's files: where each one goes in the game folder (the layout
 * Vanilla Conquer reads, see data/ra-readme.txt), how to tell the discs' MAIN.MIX files apart, and
 * the checksums of the files whose sources are known (from OpenRA's content installer). */
#ifndef PS5_RAFILES_H
#define PS5_RAFILES_H

#include <stdint.h>

typedef enum
{
    RA_SLOT_NONE = -1,
    RA_SLOT_REDALERT,      /* redalert.mix */
    RA_SLOT_MAIN,          /* main.mix in the game folder itself: the demo's, or one holding every disc */
    RA_SLOT_ALLIED,        /* allied/main.mix */
    RA_SLOT_SOVIET,        /* soviet/main.mix */
    RA_SLOT_COUNTERSTRIKE, /* counterstrike/main.mix */
    RA_SLOT_AFTERMATH,     /* aftermath/main.mix */
    RA_SLOT_EXPAND,        /* expand.mix: Counterstrike's missions */
    RA_SLOT_EXPAND2,       /* expand2.mix: Aftermath's missions and units */
    RA_SLOT_HIRES1,        /* hires1.mix: Aftermath's graphics */
    RA_SLOT_LORES1,        /* lores1.mix */
    RA_SLOT_COUNT
} ra_slot_t;

/* The slot's path in the game folder ("allied/main.mix"). */
const char *ra_slot_path(ra_slot_t slot);

/* What the slot gives the player, for notes ("the Allied campaign"). */
const char *ra_slot_title(ra_slot_t slot);

/* A file met in a source, with what was learnt reading it. */
typedef struct
{
    char name[64];  /* its own name in upper case ("MAIN.MIX") */
    char path[512]; /* where it was inside its source, folders and containers joined by '/' */
    long long size;
    uint8_t sha1[20]; /* of the whole file */
    uint8_t head[20]; /* of its first 4096 bytes */
} ra_file_t;

/* What a source says about itself through its other files. */
typedef struct
{
    int counterstrike; /* the Counterstrike disc: its installer's CSTRIKE.RTP or its README.TXT */
    int aftermath;     /* the Aftermath disc: PATCH.RTP or its README.TXT */
} ra_context_t;

/* Whether a file of this name (upper case) is one to read: a game file, or one that tells a disc. */
int ra_wanted(const char *name);

/* Whether a file of this name (upper case) holds others: a disc image or an archive. */
int ra_container(const char *name);

/* Learns from a file that is not placed itself (README.TXT, CSTRIKE.RTP, PATCH.RTP). */
void ra_learn(const ra_file_t *file, ra_context_t *context);

/* Where a file goes; RA_SLOT_NONE for one that is not placed. why gets a short reason for notes. */
ra_slot_t ra_classify(const ra_file_t *file, const ra_context_t *context, const char **why);

/* Whether the file is a copy the game is known to work with (its checksum is in the table). */
int ra_known_good(ra_slot_t slot, const ra_file_t *file);

/* The Aftermath disc's PATCH.RTP holds expand2.mix, hires1.mix and lores1.mix uncompressed; with
 * the English disc's file (checked by its SHA-1) the ranges are known. Fills offsets and lengths
 * for the three slots in order and returns 3, or 0 for any other PATCH.RTP. */
int ra_aftermath_ranges(const ra_file_t *file, ra_slot_t slots[3], long long offsets[3], long long lengths[3]);

/* Whether this is the Counterstrike disc's CSTRIKE.RTP, whose missions cannot be read out. */
int ra_counterstrike_patch(const ra_file_t *file);

/* What is in the game folder: a bit per slot, 1 << slot. */
unsigned ra_scan(const char *folder);

/* Whether the game can start with what ra_scan found: redalert.mix and a MAIN.MIX. */
int ra_playable(unsigned slots);

/* Adds a known checksum, for the PC tests (fixtures cannot have the real files' checksums). kind is
 * "redalert", "allied", "soviet", "aftermath-patch" or "counterstrike-patch". */
void ra_add_known(const char *kind, const uint8_t sha1[20]);

#endif
