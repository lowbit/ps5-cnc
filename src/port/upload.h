/* A small HTTP server for sending Red Alert's files from a browser on the same network (adapted from
 * the OpenRCT2 port's, itself from the DOOM port's). It serves the page in upload.html and stores
 * each file the page sends, with the folders it was in, below the incoming folder, where the
 * importer reads them once the page says it has sent everything. Only the files the importer reads
 * are taken: disc images, archives, Red Alert's own files and The First Decade's cabinets. It runs
 * only between upload_start and upload_stop. */
#ifndef PS5_UPLOAD_H
#define PS5_UPLOAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The first port tried. A title's sandbox refuses some ports (8666 and 50000 on FW 13.00) and
 * others may be taken, so the next few are tried after it. */
#define UPLOAD_PORT 9666
#define UPLOAD_PORT_TRIES 8
#define UPLOAD_PATH 512

typedef struct
{
    int receiving;          /* a file is arriving */
    char name[UPLOAD_PATH]; /* its path as sent */
    long long done, total;  /* bytes of it */
    int files;              /* files stored */
    long long bytes;        /* bytes stored */
    int batches;            /* times the page said it had sent everything it was given */
} upload_progress_t;

/* Starts listening and storing below folder; 0 on success. The address is "" when the console has
 * no network address. */
int upload_start(const char *folder);
void upload_stop(void);
int upload_running(void);
const char *upload_address(void);

void upload_progress(upload_progress_t *progress);

/* Whether a file of this name is one the importer reads, so worth sending. */
int upload_wanted(const char *name);

/* Turns a path as the browser sent it ("Red Alert/MAIN1.MIX") into the path to store it under: each
 * part a plain name, the file one the importer reads. 0 on success. */
int upload_clean_path(char *out, size_t size, const char *path);

/* Deletes the partial files (<name>.upload) an interrupted send left below folder. */
void upload_remove_partial(const char *folder);

/* Adds a line to the log the page shows, for what the console did with the files. */
void upload_note(const char *text, int ok);

/* Tells the page whether the console is still adding what it was sent. */
void upload_set_busy(int busy);

#ifdef __cplusplus
}
#endif

#endif
