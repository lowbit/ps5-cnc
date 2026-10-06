/* The First Decade's DVD keeps Red Alert's files inside InstallShield cabinets (data1.hdr with
 * data1.cab, data2.cab ...). They are read with unshield, either from a folder on the console
 * (the DVD's files copied or sent) or straight out of a disc image, without unpacking the image:
 * the image's ISO 9660 folder that holds data1.hdr is looked up, and unshield reads the cabinets'
 * bytes from where they are in the image. */
#ifndef PS5_TFD_H
#define PS5_TFD_H

#include <stddef.h>
#include <stdint.h>

/* Reads size bytes at offset of a disc image's data (as in an .iso file: 2048-byte sectors);
 * 0 on success. */
typedef int (*tfd_read_fn)(void *user, long long offset, void *buffer, int size);

/* A file unshield wrote: its name in upper case ("MAIN.MIX"), its folder in the cabinets
 * ("Red Alert"), where it was written, and what was learnt writing it. */
typedef struct
{
    char name[64];
    char folder[256];
    const char *written;
    long long size;
    uint8_t sha1[20]; /* of the whole file */
    uint8_t head[20]; /* of its first 4096 bytes */
} tfd_file_t;

/* Hands over a written file; it now belongs to the caller. */
typedef void (*tfd_found_fn)(const tfd_file_t *file, void *user);

/* Bytes written so far; return non-zero to stop. */
typedef int (*tfd_progress_fn)(long long written, void *user);

/* Whether the disc image holds data1.hdr, in its top folder or one below it; 1 when it does. */
int tfd_image_has_cabinets(tfd_read_fn read, void *user);

/* Writes the cabinets' Red Alert files that wanted() accepts into staging (as staging/<n>.tfd,
 * n from *serial) and hands each to found(). The cabinets are in a folder on the console when read
 * is NULL (hdr_path is then the path of data1.hdr), or else in the disc image read through read.
 * Returns how many files were written, or -1 with the reason in error. */
int tfd_extract(const char *hdr_path, tfd_read_fn read, void *read_user, const char *staging, int *serial,
                int (*wanted)(const char *name), tfd_found_fn found, tfd_progress_fn progress, void *user,
                char *error, size_t error_size);

#endif
