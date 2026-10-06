/* The importer, see importer.h. A source is read as a stream: a file, a download, an archive's
 * entry, or the 2048 data bytes of each sector of a raw disc image. Its first 64 KiB tell what it
 * is. The files the game wants are written to a staging folder inside the game folder while their
 * checksums are taken; once a source has been read they are told apart (rafiles.c) and renamed
 * into place, so that a source that fails halfway changes nothing. */
#include <archive.h>
#include <archive_entry.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "http.h"
#include "importer.h"
#include "rafiles.h"
#include "sha1.h"
#include "tfd.h"
#include "url.h"

#define CHUNK (256 * 1024)
#define PEEK (64 * 1024)
#define HEAD 4096
#define SECTOR 2048
#define LISTING_LIMIT (2 * 1024 * 1024)
#define DISCARD_LIMIT (4 * 1024 * 1024)
#define MAX_DEPTH 4
#define MAX_FOLDER_DEPTH 8
#define MAX_STAGED 64
#define MAX_GROUPS 64
#define MAX_NOTES 64
#define MAX_ENTRIES 512
#define NOTE_TEXT 240
#define PATH_SIZE 1024
#define NAME_SIZE 128
#define MAX_ALTERNATIVES 8
#define FREEWARE_LIST_LIMIT (64 * 1024)
#define WORKER_STACK (8 * 1024 * 1024)
#define STAGING ".import"
/* A main.mix in the game folder itself this small is the demo's: the discs' are hundreds of MB. */
#define DEMO_MAIN_LIMIT (150LL * 1024 * 1024)

#define FORMAT_ISO 1
#define FORMAT_ZIP 2
#define FORMAT_7Z 4
#define FORMAT_RAR 8

/* The list of places the free discs can be downloaded from, read from the project's repository at
 * the time of the download, so that sources can change without a new build of the title. */
#define FREEWARE_LIST "https://raw.githubusercontent.com/lowbit/ps5-cnc/HEAD/data/freeware.txt"

/* Electronic Arts gave Red Alert away in 2008 from its own server, as two RAR files holding the
 * discs' images. The server is gone; the Internet Archive's Wayback Machine keeps what it served
 * (an "id_" address returns the file as it was). Tried after the list above. */
static const struct
{
    const char *disc, *title, *url;
} freeware_builtin[] = {
    { "allied", "the free Allied disc",
      "https://web.archive.org/web/2008id_/http://na.llnet.cnc3tv.ea.com/u/f/eagames/cnc3/cnc3tv/RedAlert/"
      "RedAlert1_AlliedDisc.rar" },
    { "soviet", "the free Soviet disc",
      "https://web.archive.org/web/2008id_/http://na.llnet.cnc3tv.ea.com/u/f/eagames/cnc3/cnc3tv/RedAlert/"
      "RedAlert1_SovietDisc.rar" },
};

typedef enum
{
    STREAM_FILE,
    STREAM_HTTP,
    STREAM_ENTRY,
    STREAM_SECTORS,
} stream_kind_t;

typedef struct stream
{
    stream_kind_t kind;
    long long size;     /* -1 when unknown */
    long long position; /* of the next byte from below (past the peeked ones) */
    /* The first bytes, read to tell what the stream holds and handed out again first. While any
     * are left, nothing past them has been read, so they can be gone back to. */
    uint8_t *peeked;
    int peeked_length, peeked_offset;
    uint8_t *scratch;     /* for bytes skipped */
    int shown;            /* its reads are the progress shown */
    long long shown_base; /* added to its position for the progress: a folder's files before it */
    char error[160];
    /* STREAM_FILE */
    int fd;
    /* STREAM_HTTP */
    char url[IMPORT_URL]; /* where the last request ended up */
    http_t *http;
    int ranges; /* whether the server answers requests from an offset; -1 not known yet */
    char type[64];
    /* STREAM_ENTRY */
    struct archive *archive;
    /* STREAM_SECTORS */
    struct stream *inner;
    int sector_size, data_offset;
    uint8_t *sector;
    int sector_length, sector_used;
} stream_t;

typedef struct
{
    stream_t *stream;
    uint8_t *buffer;
} reader_t;

/* A disc's files are told apart together: a README.TXT or an installer next to a MAIN.MIX says
 * which disc it came from. A group is a disc: a container, and the folder in it that holds the
 * disc's top (an installer's SETUP/INSTALL folders count as their disc's top). */
typedef struct
{
    char key[PATH_SIZE];
    ra_context_t context;
} group_t;

typedef struct
{
    ra_file_t file;
    char staged[PATH_SIZE]; /* "" once it has been placed or dropped */
    int group;
    ra_slot_t slot;
    const char *why;
    char origin[NAME_SIZE]; /* the source it came from, for notes */
} staged_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int worker_running;
static volatile int cancelled;
static import_status_t status;
static listing_t listing;
static char sources[IMPORT_MAX_SOURCES][IMPORT_URL];
static int source_count, import_flags;
static char game[PATH_SIZE], staging[PATH_SIZE];

static struct
{
    char text[NOTE_TEXT];
    int ok;
} notes[MAX_NOTES];
static int note_first, note_count;

/* Only the worker touches these. */
static staged_t staged[MAX_STAGED];
static int staged_count, serial;
static group_t groups[MAX_GROUPS];
static int group_count;
static char source_name[NAME_SIZE]; /* the source being read, for notes */
static int write_failed;            /* writing on the console failed: the import stops */
static int read_failed;             /* a source or part of one could not be read */
static int counterstrike_patch;     /* the source had Counterstrike's installer */
static int cabinets_noted;
static uint8_t data_buffer[CHUNK];
static char *freeware_list;
static int freeware_list_loaded;

static void note(int ok, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void note(int ok, const char *format, ...)
{
    char text[NOTE_TEXT];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    printf("import: %s\n", text);
    pthread_mutex_lock(&lock);
    if (note_count == MAX_NOTES)
    {
        note_first = (note_first + 1) % MAX_NOTES;
        note_count--;
    }
    int slot = (note_first + note_count++) % MAX_NOTES;
    snprintf(notes[slot].text, sizeof(notes[slot].text), "%s", text);
    notes[slot].ok = ok;
    pthread_mutex_unlock(&lock);
}

static void set_current(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void set_current(const char *format, ...)
{
    va_list args;

    pthread_mutex_lock(&lock);
    va_start(args, format);
    vsnprintf(status.current, sizeof(status.current), format, args);
    va_end(args);
    pthread_mutex_unlock(&lock);
}

static void set_progress(long long done, long long total)
{
    pthread_mutex_lock(&lock);
    status.done = done;
    if (total != -2)
        status.total = total;
    pthread_mutex_unlock(&lock);
}

static void upper(char *out, size_t size, const char *text)
{
    size_t i = 0;
    for (; text[i] != '\0' && i + 1 < size; i++)
        out[i] = (char)toupper((unsigned char)text[i]);
    out[i] = '\0';
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static int is_learn_only(const char *name)
{
    return strcmp(name, "README.TXT") == 0 || strcmp(name, "CSTRIKE.RTP") == 0;
}

static int is_cabinet(const char *name)
{
    size_t length = strlen(name);
    return length > 4 && strncasecmp(name, "DATA", 4) == 0 &&
           (strcasecmp(name + length - 4, ".HDR") == 0 || strcasecmp(name + length - 4, ".CAB") == 0);
}

static int write_all(int fd, const uint8_t *data, size_t size)
{
    while (size > 0)
    {
        ssize_t written = write(fd, data, size);
        if (written <= 0)
            return -1;
        data += written;
        size -= (size_t)written;
    }
    return 0;
}

/* Streams */

static long long logical(const stream_t *s)
{
    return s->position - (s->peeked_length - s->peeked_offset);
}

static const char *stream_error(const stream_t *s)
{
    if (cancelled)
        return "stopped";
    if (s->error[0] != '\0')
        return s->error;
    if (s->kind == STREAM_SECTORS && s->inner != NULL)
        return stream_error(s->inner);
    return "reading failed";
}

static int open_remote(stream_t *s, long long offset)
{
    http_info_t info;

    http_close(s->http);
    s->http = http_get(s->url, offset, &info, s->error, sizeof(s->error));
    if (s->http == NULL)
        return -1;
    snprintf(s->url, sizeof(s->url), "%s", info.url);
    if (info.ranges && s->ranges < 0)
        s->ranges = 1;
    if (info.status == 206)
    {
        s->ranges = 1;
        s->position = offset;
        if (s->size < 0 && info.length >= 0)
            s->size = offset + info.length;
    }
    else
    {
        if (offset > 0)
            s->ranges = 0;
        s->position = 0;
        if (info.length >= 0)
            s->size = info.length;
    }
    if (offset == 0)
        snprintf(s->type, sizeof(s->type), "%s", info.type);
    printf("import: request from %lld answered %d with %lld bytes\n", offset, info.status, info.length);
    return 0;
}

static int raw_read(stream_t *s, void *out, int size);

/* Reads until size bytes or the end; the count, or -1. */
static int read_full(stream_t *s, void *out, int size)
{
    int filled = 0;
    while (filled < size)
    {
        int got = raw_read(s, (uint8_t *)out + filled, size - filled);
        if (got < 0)
            return -1;
        if (got == 0)
            break;
        filled += got;
    }
    return filled;
}

static int sectors_read(stream_t *s, uint8_t *out, int size)
{
    if (s->sector_used == s->sector_length)
    {
        uint8_t raw[2448];
        int filled = read_full(s->inner, raw, s->sector_size);
        if (filled < 0)
            return -1;
        /* A sector cut short at the end of the image is left out. */
        if (filled < s->data_offset + SECTOR)
            return 0;
        memcpy(s->sector, raw + s->data_offset, SECTOR);
        s->sector_length = SECTOR;
        s->sector_used = 0;
    }
    int n = s->sector_length - s->sector_used < size ? s->sector_length - s->sector_used : size;
    memcpy(out, s->sector + s->sector_used, (size_t)n);
    s->sector_used += n;
    return n;
}

static int raw_read(stream_t *s, void *out, int size)
{
    int got = -1;

    if (cancelled)
        return -1;
    if (s->peeked_offset < s->peeked_length)
    {
        got = s->peeked_length - s->peeked_offset < size ? s->peeked_length - s->peeked_offset : size;
        memcpy(out, s->peeked + s->peeked_offset, (size_t)got);
        s->peeked_offset += got;
        return got;
    }
    s->peeked_length = s->peeked_offset = 0;
    switch (s->kind)
    {
    case STREAM_FILE:
        do
            got = (int)read(s->fd, out, (size_t)size);
        while (got < 0 && errno == EINTR);
        if (got < 0)
            snprintf(s->error, sizeof(s->error), "reading failed (error %d)", errno);
        break;
    case STREAM_HTTP:
        got = s->http != NULL ? http_read(s->http, out, size) : 0;
        if (got < 0)
            snprintf(s->error, sizeof(s->error), "the download broke off");
        break;
    case STREAM_ENTRY:
    {
        la_ssize_t n = archive_read_data(s->archive, out, (size_t)size);
        got = n < 0 ? -1 : (int)n;
        if (n < 0)
            snprintf(s->error, sizeof(s->error), "%s",
                     archive_error_string(s->archive) != NULL ? archive_error_string(s->archive) : "unreadable");
        break;
    }
    case STREAM_SECTORS:
        got = sectors_read(s, out, size);
        break;
    }
    if (got > 0)
    {
        s->position += got;
        if (s->shown)
            set_progress(s->shown_base + s->position, -2);
    }
    return got;
}

static int discard(stream_t *s, long long count)
{
    if (s->scratch == NULL && (s->scratch = malloc(CHUNK)) == NULL)
        return -1;
    while (count > 0)
    {
        int got = raw_read(s, s->scratch, count < CHUNK ? (int)count : CHUNK);
        if (got <= 0)
            return -1;
        count -= got;
    }
    return 0;
}

static int stream_seekable(const stream_t *s)
{
    switch (s->kind)
    {
    case STREAM_FILE:
    case STREAM_HTTP:
        return 1;
    case STREAM_SECTORS:
        return stream_seekable(s->inner);
    default:
        return 0;
    }
}

/* Whether it reads a file on the console, where going back and forth costs nothing. */
static int stream_local(const stream_t *s)
{
    return s->kind == STREAM_FILE || (s->kind == STREAM_SECTORS && stream_local(s->inner));
}

static int stream_seek(stream_t *s, long long target)
{
    long long here = logical(s);

    if (target == here)
        return 0;
    if (s->peeked_length > 0 && target >= s->position - s->peeked_length && target <= s->position)
    {
        s->peeked_offset = s->peeked_length - (int)(s->position - target);
        return 0;
    }
    if (target > here && target - here <= 64 * 1024)
        return discard(s, target - here);
    switch (s->kind)
    {
    case STREAM_FILE:
        if (lseek(s->fd, target, SEEK_SET) != target)
            return -1;
        s->peeked_length = s->peeked_offset = 0;
        s->position = target;
        return 0;
    case STREAM_HTTP:
        if (target > here && (target - here <= DISCARD_LIMIT || s->ranges == 0))
            return discard(s, target - here);
        if (s->size >= 0 && target >= s->size)
        {
            s->peeked_length = s->peeked_offset = 0;
            http_close(s->http);
            s->http = NULL;
            s->position = target;
            return 0;
        }
        if (s->ranges != 0)
        {
            /* A request from there; a server that answers it from the start instead has it
             * dropped, and the open one goes on. */
            http_info_t info;
            char error[128];
            http_t *next = http_get(s->url, target, &info, error, sizeof(error));
            if (next != NULL && info.status == 206)
            {
                http_close(s->http);
                s->http = next;
                s->ranges = 1;
                s->peeked_length = s->peeked_offset = 0;
                s->position = target;
                printf("import: request from %lld answered 206\n", target);
                return 0;
            }
            http_close(next);
            s->ranges = 0;
            printf("import: the server does not answer requests from an offset\n");
            if (target > here)
                return discard(s, target - here);
        }
        /* Back, without ranges: from the start again. */
        s->peeked_length = s->peeked_offset = 0;
        if (open_remote(s, 0) != 0)
            return -1;
        return discard(s, target);
    case STREAM_SECTORS:
        if (!stream_seekable(s->inner))
            return target > here ? discard(s, target - here) : -1;
        s->peeked_length = s->peeked_offset = 0;
        if (stream_seek(s->inner, target / SECTOR * s->sector_size) != 0)
            return -1;
        s->sector_used = s->sector_length = 0;
        s->position = target / SECTOR * SECTOR;
        return discard(s, target % SECTOR);
    default:
        return target > here ? discard(s, target - here) : -1;
    }
}

/* Reads the first bytes into the peek buffer; how many, or -1. */
static int stream_peek(stream_t *s)
{
    if (s->peeked == NULL && (s->peeked = malloc(PEEK)) == NULL)
        return -1;
    int filled = 0;
    while (filled < PEEK)
    {
        int got = raw_read(s, s->peeked + filled, PEEK - filled);
        if (got < 0)
            return -1;
        if (got == 0)
            break;
        filled += got;
    }
    s->peeked_length = filled;
    s->peeked_offset = 0;
    return filled;
}

static void stream_init(stream_t *s, stream_kind_t kind)
{
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->fd = -1;
    s->size = -1;
    s->ranges = -1;
}

static void stream_close(stream_t *s)
{
    if (s->kind == STREAM_FILE && s->fd >= 0)
        close(s->fd);
    http_close(s->http);
    free(s->peeked);
    free(s->scratch);
    free(s->sector);
    s->http = NULL;
    s->peeked = s->scratch = s->sector = NULL;
    s->fd = -1;
}

static int open_file(stream_t *s, const char *path)
{
    stream_init(s, STREAM_FILE);
    if ((s->fd = open(path, O_RDONLY)) < 0)
        return -1;
    s->size = lseek(s->fd, 0, SEEK_END);
    lseek(s->fd, 0, SEEK_SET);
    return 0;
}

static int read_at(void *user, long long offset, void *buffer, int size)
{
    stream_t *s = user;
    return stream_seek(s, offset) == 0 && read_full(s, buffer, size) == size ? 0 : -1;
}

/* libarchive's side */

static la_ssize_t on_read(struct archive *a, void *user, const void **buffer)
{
    reader_t *reader = user;
    int got = raw_read(reader->stream, reader->buffer, CHUNK);

    if (got < 0)
    {
        archive_set_error(a, EIO, "%s", stream_error(reader->stream));
        return -1;
    }
    *buffer = reader->buffer;
    return got;
}

static la_int64_t on_seek(struct archive *a, void *user, la_int64_t offset, int whence)
{
    reader_t *reader = user;
    stream_t *s = reader->stream;
    long long target = offset;

    (void)a;
    if (whence == SEEK_CUR)
        target += logical(s);
    else if (whence == SEEK_END)
    {
        if (s->size < 0)
            return ARCHIVE_FATAL;
        target += s->size;
    }
    if (target < 0 || stream_seek(s, target) != 0)
        return ARCHIVE_FATAL;
    return target;
}

/* Staging */

/* The group of a file at inner in container. */
static int group_for(const char *container, const char *inner)
{
    char key[PATH_SIZE];
    const char *slash = strrchr(inner, '/');
    int length = snprintf(key, sizeof(key), "%s|", container);
    size_t start = (size_t)length;

    if (slash != NULL)
        snprintf(key + length, sizeof(key) - (size_t)length, "%.*s", (int)(slash - inner), inner);
    for (;;)
    {
        char *last = strrchr(key + start, '/');
        char *tail = last != NULL ? last + 1 : key + start;
        if (*tail == '\0' || (strcasecmp(tail, "INSTALL") != 0 && strcasecmp(tail, "SETUP") != 0))
            break;
        *(last != NULL ? last : tail) = '\0';
    }
    for (int i = 0; i < group_count; i++)
        if (strcmp(groups[i].key, key) == 0)
            return i;
    if (group_count == MAX_GROUPS)
        return MAX_GROUPS - 1;
    memset(&groups[group_count], 0, sizeof(groups[0]));
    snprintf(groups[group_count].key, sizeof(groups[0].key), "%s", key);
    return group_count++;
}

static void join_path(char *out, size_t size, const char *container, const char *inner)
{
    if (container[0] != '\0')
        snprintf(out, size, "%s/%s", container, inner);
    else
        snprintf(out, size, "%s", inner);
}

static void add_staged(const ra_file_t *file, const char *path, int group)
{
    if (staged_count == MAX_STAGED)
    {
        note(0, "Too many Red Alert files in %s: %s is left out", source_name, file->name);
        remove(path);
        return;
    }
    staged_t *entry = &staged[staged_count++];
    memset(entry, 0, sizeof(*entry));
    entry->file = *file;
    snprintf(entry->staged, sizeof(entry->staged), "%s", path);
    entry->group = group;
    entry->slot = RA_SLOT_NONE;
    snprintf(entry->origin, sizeof(entry->origin), "%s", source_name);
}

/* Reads a game file from s, at inner in container, into the staging folder with its checksums.
 * With move_from, the file is that one on the console, which is moved instead of written. */
static int stage_stream(stream_t *s, const char *name, const char *container, const char *inner,
                        const char *move_from)
{
    ra_file_t file;
    char part[PATH_SIZE];
    sha1_t whole, head;
    long long total = 0;
    int fd = -1, got;
    const int learn_only = is_learn_only(name);

    memset(&file, 0, sizeof(file));
    snprintf(file.name, sizeof(file.name), "%s", name);
    join_path(file.path, sizeof(file.path), container, inner);
    if (!learn_only)
        set_current("%s", name);
    snprintf(part, sizeof(part), "%s/%d.part", staging, ++serial);
    if (!learn_only && move_from == NULL && (fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        note(0, "Cannot write in the game folder (error %d)", errno);
        write_failed = 1;
        return -1;
    }
    sha1_init(&whole);
    sha1_init(&head);
    /* Whole chunks are written: decompressors hand data over in smaller pieces, and many small
     * writes to the title folder get slower as the file grows. */
    while ((got = read_full(s, data_buffer, CHUNK)) > 0)
    {
        sha1_update(&whole, data_buffer, (size_t)got);
        if (total < HEAD)
            sha1_update(&head, data_buffer, (size_t)(got < HEAD - total ? got : HEAD - total));
        total += got;
        if (fd >= 0 && write_all(fd, data_buffer, (size_t)got) != 0)
        {
            int error = errno;
            close(fd);
            unlink(part);
            if (error == ENOSPC)
                note(0, "Not enough space on the console for %s", name);
            else
                note(0, "Writing %s failed (error %d)", name, error);
            write_failed = 1;
            set_current("%s", "");
            return -1;
        }
    }
    if (fd >= 0)
        close(fd);
    set_current("%s", "");
    if (got < 0)
    {
        unlink(part);
        if (!cancelled)
        {
            note(0, "%s in %s cannot be read: %s", name, source_name, stream_error(s));
            read_failed = 1;
        }
        return -1;
    }
    sha1_final(&whole, file.sha1);
    sha1_final(&head, file.head);
    file.size = total;

    int group = group_for(container, inner);
    ra_learn(&file, &groups[group].context);
    if (ra_counterstrike_patch(&file))
        counterstrike_patch = 1;
    if (learn_only)
        return 0;
    if (move_from != NULL && rename(move_from, part) != 0)
    {
        /* Not on the same file system after all: copy it. */
        stream_t copy;
        if (open_file(&copy, move_from) != 0)
            return -1;
        int result = stage_stream(&copy, name, container, inner, NULL);
        stream_close(&copy);
        if (result == 0)
            unlink(move_from);
        return result;
    }
    add_staged(&file, part, group);
    return 0;
}

static void on_cabinet_file(const tfd_file_t *found, void *user)
{
    const char *container = user;
    ra_file_t file;
    char inner[PATH_SIZE];

    memset(&file, 0, sizeof(file));
    snprintf(file.name, sizeof(file.name), "%s", found->name);
    snprintf(inner, sizeof(inner), "%s/%s", found->folder, found->name);
    join_path(file.path, sizeof(file.path), container, inner);
    file.size = found->size;
    memcpy(file.sha1, found->sha1, sizeof(file.sha1));
    memcpy(file.head, found->head, sizeof(file.head));
    add_staged(&file, found->written, group_for(container, inner));
}

static int on_cabinet_progress(long long written, void *user)
{
    (void)user;
    set_progress(written, -1);
    return cancelled;
}

/* The First Decade: from a folder on the console (hdr_path), or a disc image (image). */
static int import_cabinets(const char *hdr_path, stream_t *image, const char *container)
{
    char error[160];

    set_current("%s", "The First Decade's cabinets");
    note(1, "Reading The First Decade's installer cabinets in %s", source_name);
    int count = tfd_extract(hdr_path, image != NULL ? read_at : NULL, image, staging, &serial, ra_wanted,
                            on_cabinet_file, on_cabinet_progress, (void *)container, error, sizeof(error));
    set_current("%s", "");
    if (count < 0 && !cancelled)
    {
        note(0, "%s: %s", source_name, error);
        read_failed = 1;
    }
    return count < 0 ? -1 : 0;
}

/* Walking sources */

static int walk_stream(stream_t *s, const char *path, const char *name, int depth, int listing_allowed);

static int walk_archive(stream_t *s, const char *path, int depth, int format)
{
    reader_t reader = { s, malloc(CHUNK) };
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    int result = 0, r = ARCHIVE_OK;

    if (reader.buffer == NULL || a == NULL)
    {
        free(reader.buffer);
        archive_read_free(a);
        return -1;
    }
    /* Only the format the first bytes told: ZIP's reader looks for its end first, which for
     * another format's download would cost a request. */
    switch (format)
    {
    case FORMAT_ISO:
        archive_read_support_format_iso9660(a);
        break;
    case FORMAT_ZIP:
        archive_read_support_format_zip(a);
        break;
    case FORMAT_7Z:
        archive_read_support_format_7zip(a);
        break;
    default:
        archive_read_support_format_rar(a);
        archive_read_support_format_rar5(a);
        break;
    }
    archive_read_set_read_callback(a, on_read);
    /* ZIP is read from its end when it can be gone back and forth in, which costs a download
     * that cannot (or may not) start from an offset the whole file again: it is read in order
     * then. */
    if (stream_seekable(s) && (format != FORMAT_ZIP || stream_local(s) || s->ranges == 1))
        archive_read_set_seek_callback(a, on_seek);
    archive_read_set_callback_data(a, &reader);
    if (archive_read_open1(a) != ARCHIVE_OK)
    {
        if (!cancelled)
        {
            note(0, "%s cannot be read: %s", base_name(path),
                 archive_error_string(a) != NULL ? archive_error_string(a) : "not a format the importer reads");
            read_failed = 1;
        }
        archive_read_free(a);
        free(reader.buffer);
        return -1;
    }
    while (!cancelled && !write_failed &&
           ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN))
    {
        const char *pathname = archive_entry_pathname_utf8(entry);
        char inner[PATH_SIZE], name[NAME_SIZE];

        if (pathname == NULL)
            pathname = archive_entry_pathname(entry);
        if (pathname == NULL || archive_entry_filetype(entry) != AE_IFREG)
            continue;
        while (*pathname == '/' || (pathname[0] == '.' && pathname[1] == '/'))
            pathname += *pathname == '/' ? 1 : 2;
        snprintf(inner, sizeof(inner), "%s", pathname);
        for (char *c = inner; *c != '\0'; c++)
            if (*c == '\\')
                *c = '/';
        upper(name, sizeof(name), base_name(inner));

        if (ra_wanted(name) || (ra_container(name) && depth < MAX_DEPTH))
        {
            stream_t entry_stream;
            stream_init(&entry_stream, STREAM_ENTRY);
            entry_stream.archive = a;
            entry_stream.size = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : -1;
            if (ra_wanted(name))
                stage_stream(&entry_stream, name, path, inner, NULL);
            else
            {
                char nested[PATH_SIZE];
                join_path(nested, sizeof(nested), path, inner);
                walk_stream(&entry_stream, nested, base_name(inner), depth + 1, 0);
            }
            stream_close(&entry_stream);
        }
        else if (is_cabinet(name) && !cabinets_noted)
        {
            cabinets_noted = 1;
            note(0, "%s holds The First Decade's installer cabinets, which are read from the disc image "
                    "itself: send the .iso file on its own, not packed or by link",
                 source_name);
        }
    }
    if (!cancelled && !write_failed && r != ARCHIVE_EOF && r != ARCHIVE_OK && r != ARCHIVE_WARN)
    {
        note(0, "%s cannot be read to its end: %s", base_name(path),
             archive_error_string(a) != NULL ? archive_error_string(a) : "unreadable");
        read_failed = 1;
        result = -1;
    }
    archive_read_free(a);
    free(reader.buffer);
    return result;
}

/* A disc image: its files through libarchive, or The First Decade's cabinets in it. */
static int walk_image(stream_t *s, const char *path, int depth)
{
    if (stream_local(s))
    {
        int cabinets = tfd_image_has_cabinets(read_at, s);
        if (stream_seek(s, 0) != 0)
            return -1;
        if (cabinets)
            return import_cabinets(NULL, s, path);
    }
    return walk_archive(s, path, depth, FORMAT_ISO);
}

/* A raw image (BIN, IMG, MDF) keeps whole CD sectors: 2352 bytes, or 2448 with the subchannel,
 * of which 2048 are data, after a 16-byte (mode 1) or 24-byte (mode 2) header. */
static int raw_image(const uint8_t *p, int length, int *sector_size, int *data_offset)
{
    static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    static const int sizes[] = { 2352, 2448 };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        const uint8_t *descriptor = p + 16 * sizes[i];
        if (length < 17 * sizes[i] || memcmp(p, sync, sizeof(sync)) != 0 ||
            memcmp(descriptor, sync, sizeof(sync)) != 0)
            continue;
        int offset = descriptor[15] == 2 ? 24 : 16;
        if (memcmp(descriptor + offset + 1, "CD001", 5) == 0)
        {
            *sector_size = sizes[i];
            *data_offset = offset;
            return 1;
        }
    }
    return 0;
}

/* A 7z archive inside another one: 7z needs to go back and forth, so it is copied out first. */
static int spool_and_walk(stream_t *s, const char *path, const char *name, int depth)
{
    char spool[PATH_SIZE];
    int fd, got, result = -1;

    snprintf(spool, sizeof(spool), "%s/%d.spool", staging, ++serial);
    if ((fd = open(spool, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        note(0, "Cannot write in the game folder (error %d)", errno);
        write_failed = 1;
        return -1;
    }
    set_current("Unpacking %s", name);
    while ((got = read_full(s, data_buffer, CHUNK)) > 0)
    {
        if (write_all(fd, data_buffer, (size_t)got) != 0)
        {
            note(0, errno == ENOSPC ? "Not enough space on the console to unpack %s" : "Unpacking %s failed",
                 name);
            write_failed = 1;
            got = -1;
            break;
        }
    }
    close(fd);
    set_current("%s", "");
    if (got == 0)
    {
        stream_t file;
        if (open_file(&file, spool) == 0)
            result = walk_stream(&file, path, name, depth, 0);
        stream_close(&file);
    }
    else if (!cancelled && !write_failed)
    {
        note(0, "%s cannot be read: %s", name, stream_error(s));
        read_failed = 1;
    }
    unlink(spool);
    return result;
}

static int read_listing(stream_t *s)
{
    char base[IMPORT_URL];
    char *html = malloc(LISTING_LIMIT + 1);
    int used = 0, got;

    if (html == NULL)
        return -1;
    while (used < LISTING_LIMIT && (got = raw_read(s, html + used, LISTING_LIMIT - used)) > 0)
        used += got;
    html[used] = '\0';
    url_directory(base, sizeof(base), s->url);
    listing_parse(&listing, base, html, (size_t)used);
    free(html);
    pthread_mutex_lock(&lock);
    status.state = IMPORT_LISTING;
    pthread_mutex_unlock(&lock);
    return 0;
}

static int looks_like_html(const stream_t *s)
{
    int i = 0;
    while (i < s->peeked_length && isspace(s->peeked[i]))
        i++;
    return strstr(s->type, "text/html") != NULL || (i < s->peeked_length && s->peeked[i] == '<');
}

/* What a stream holds, told by its first bytes; path is where it is, name its own name. */
static int walk_stream(stream_t *s, const char *path, const char *name, int depth, int listing_allowed)
{
    char upper_name[NAME_SIZE];
    int sector_size, data_offset;

    upper(upper_name, sizeof(upper_name), name);
    int got = stream_peek(s);
    if (got < 0)
    {
        if (!cancelled)
        {
            note(0, "%s cannot be read: %s", name, stream_error(s));
            read_failed = 1;
        }
        return -1;
    }
    const uint8_t *p = s->peeked;
    if (s->kind == STREAM_HTTP && looks_like_html(s))
    {
        if (listing_allowed)
            return read_listing(s);
        note(0, "%s is a web page, not a file", name);
        read_failed = 1;
        return -1;
    }
    if (ra_wanted(upper_name))
    {
        char container[PATH_SIZE];
        snprintf(container, sizeof(container), "%.*s", (int)(base_name(path) - path > 0 ? base_name(path) - path - 1 : 0),
                 path);
        return stage_stream(s, upper_name, container, name, NULL);
    }
    if (got >= 0x8006 && memcmp(p + 0x8001, "CD001", 5) == 0)
        return walk_image(s, path, depth);
    if (raw_image(p, got, &sector_size, &data_offset))
    {
        stream_t sectors;
        stream_init(&sectors, STREAM_SECTORS);
        sectors.inner = s;
        sectors.sector_size = sector_size;
        sectors.data_offset = data_offset;
        sectors.sector = malloc(SECTOR);
        if (s->size >= 0)
            sectors.size = s->size / sector_size * SECTOR;
        int result = sectors.sector != NULL ? walk_image(&sectors, path, depth) : -1;
        sectors.inner = NULL;
        stream_close(&sectors);
        return result;
    }
    const int zip = got >= 4 && p[0] == 'P' && p[1] == 'K' && (p[2] == 3 || p[2] == 5 || p[2] == 7);
    const int rar = got >= 6 && memcmp(p, "Rar!\x1a\x07", 6) == 0;
    const int sevenzip = got >= 6 && memcmp(p, "7z\xbc\xaf\x27\x1c", 6) == 0;
    if (sevenzip && !stream_seekable(s))
        return spool_and_walk(s, path, name, depth);
    if (zip || rar || sevenzip)
        return walk_archive(s, path, depth, zip ? FORMAT_ZIP : sevenzip ? FORMAT_7Z : FORMAT_RAR);
    note(0, "%s is not a disc image, an archive or a Red Alert file", name);
    return 0;
}

typedef struct
{
    char name[256];
    int folder;
} folder_entry_t;

static int read_folder(const char *path, folder_entry_t **out)
{
    DIR *dir = opendir(path);
    folder_entry_t *entries = NULL;
    int count = 0;

    if (dir == NULL)
        return -1;
    for (struct dirent *entry; (entry = readdir(dir)) != NULL && count < MAX_ENTRIES;)
    {
        char child[PATH_SIZE];
        struct stat info;
        if (entry->d_name[0] == '.')
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (stat(child, &info) != 0 || (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)))
            continue;
        folder_entry_t *grown = realloc(entries, sizeof(*entries) * (size_t)(count + 1));
        if (grown == NULL)
            break;
        entries = grown;
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
        entries[count++].folder = S_ISDIR(info.st_mode);
    }
    closedir(dir);
    *out = entries;
    return count;
}

/* The bytes of what a folder walk reads, for its progress. */
static long long folder_bytes(const char *path, int depth)
{
    folder_entry_t *entries = NULL;
    long long total = 0;
    int count = read_folder(path, &entries);

    for (int i = 0; i < count; i++)
    {
        char child[PATH_SIZE];
        struct stat info;
        snprintf(child, sizeof(child), "%s/%s", path, entries[i].name);
        if (entries[i].folder)
            total += depth < MAX_FOLDER_DEPTH ? folder_bytes(child, depth + 1) : 0;
        else if ((ra_wanted(entries[i].name) || ra_container(entries[i].name)) && stat(child, &info) == 0)
            total += info.st_size;
    }
    free(entries);
    return total;
}

static long long folder_done;
static char folder_label[NAME_SIZE]; /* what a folder source is called in notes */

/* path on the console, shown as display; move: files may be moved out, and are deleted once read. */
static int walk_folder(const char *path, const char *display, int depth, int move)
{
    folder_entry_t *entries = NULL;
    int count = read_folder(path, &entries), cabinets = -1;

    if (count < 0)
    {
        note(0, "The folder %s cannot be opened", display[0] != '\0' ? display : path);
        read_failed = 1;
        return -1;
    }
    for (int i = 0; i < count; i++)
        if (!entries[i].folder && strcasecmp(entries[i].name, "DATA1.HDR") == 0)
            cabinets = i;
    if (cabinets >= 0)
    {
        char hdr[PATH_SIZE];
        snprintf(hdr, sizeof(hdr), "%s/%s", path, entries[cabinets].name);
        snprintf(source_name, sizeof(source_name), "%s", folder_label);
        if (import_cabinets(hdr, NULL, display) == 0 && move && !cancelled && !write_failed)
            for (int i = 0; i < count; i++)
                if (!entries[i].folder && is_cabinet(entries[i].name))
                {
                    snprintf(hdr, sizeof(hdr), "%s/%s", path, entries[i].name);
                    unlink(hdr);
                }
    }
    for (int i = 0; i < count && !cancelled && !write_failed; i++)
    {
        char child[PATH_SIZE], shown[PATH_SIZE], name[NAME_SIZE];
        struct stat info;

        snprintf(child, sizeof(child), "%s/%s", path, entries[i].name);
        join_path(shown, sizeof(shown), display, entries[i].name);
        upper(name, sizeof(name), entries[i].name);
        if (entries[i].folder)
        {
            if (depth < MAX_FOLDER_DEPTH)
                walk_folder(child, shown, depth + 1, move);
            if (move)
                rmdir(child);
            continue;
        }
        if (stat(child, &info) != 0 || (cabinets >= 0 && is_cabinet(name)))
            continue;
        if (ra_wanted(name))
        {
            stream_t file;
            if (open_file(&file, child) != 0)
            {
                note(0, "%s cannot be opened", shown);
                read_failed = 1;
                continue;
            }
            file.shown = 1;
            file.shown_base = folder_done;
            snprintf(source_name, sizeof(source_name), "%s", folder_label);
            stage_stream(&file, name, "", shown, move && !is_learn_only(name) ? child : NULL);
            stream_close(&file);
            if (move && is_learn_only(name))
                unlink(child);
            folder_done += info.st_size;
        }
        else if (ra_container(name))
        {
            stream_t file;
            if (open_file(&file, child) != 0)
            {
                note(0, "%s cannot be opened", shown);
                read_failed = 1;
                continue;
            }
            file.shown = 1;
            file.shown_base = folder_done;
            snprintf(source_name, sizeof(source_name), "%s", entries[i].name);
            walk_stream(&file, shown, entries[i].name, 0, 0);
            stream_close(&file);
            if (move && !cancelled && !write_failed)
                unlink(child);
            folder_done += info.st_size;
        }
    }
    free(entries);
    return 0;
}

/* Placing what was found */

static int find_any_case(const char *folder, const char *relative, char *out, size_t size)
{
    snprintf(out, size, "%s", folder);
    for (const char *part = relative; *part != '\0';)
    {
        size_t length = strcspn(part, "/");
        DIR *dir = opendir(out);
        int found = 0;
        if (dir == NULL)
            return 0;
        for (struct dirent *entry; (entry = readdir(dir)) != NULL;)
        {
            if (strlen(entry->d_name) == length && strncasecmp(entry->d_name, part, length) == 0)
            {
                size_t used = strlen(out);
                snprintf(out + used, size - used, "/%s", entry->d_name);
                found = 1;
                break;
            }
        }
        closedir(dir);
        if (!found)
            return 0;
        part += length;
        while (*part == '/')
            part++;
    }
    return 1;
}

/* The checksums of a file in the game folder, for comparing with a new one. */
static int checksum_file(const char *path, ra_file_t *file, int whole)
{
    stream_t s;
    sha1_t full, head;
    long long total = 0;
    int got;

    if (open_file(&s, path) != 0)
        return -1;
    sha1_init(&full);
    sha1_init(&head);
    while ((got = raw_read(&s, data_buffer, whole ? CHUNK : HEAD)) > 0)
    {
        sha1_update(&full, data_buffer, (size_t)got);
        if (total < HEAD)
            sha1_update(&head, data_buffer, (size_t)(got < HEAD - total ? got : HEAD - total));
        total += got;
        if (!whole && total >= HEAD)
            break;
    }
    stream_close(&s);
    sha1_final(&full, file->sha1);
    sha1_final(&head, file->head);
    file->size = total;
    return got < 0 ? -1 : 0;
}

static long long demo_main_limit(void)
{
#ifndef __PROSPERO__
    /* The tests' stand-ins are small. */
    if (getenv("RA_DEMO_MAIN_LIMIT") != NULL)
        return atoll(getenv("RA_DEMO_MAIN_LIMIT"));
#endif
    return DEMO_MAIN_LIMIT;
}

static long long file_size(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 ? (long long)info.st_size : -1;
}

/* Cuts a range out of a staged file into a new one. */
static void cut_range(const staged_t *from, ra_slot_t slot, long long offset, long long length)
{
    char part[PATH_SIZE];
    ra_file_t file;
    sha1_t whole, head;
    long long done = 0;
    int in = open(from->staged, O_RDONLY), out = -1, failed = 0;

    snprintf(part, sizeof(part), "%s/%d.part", staging, ++serial);
    if (in >= 0 && lseek(in, offset, SEEK_SET) == offset)
        out = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    memset(&file, 0, sizeof(file));
    upper(file.name, sizeof(file.name), ra_slot_path(slot));
    snprintf(file.path, sizeof(file.path), "%.*s%s", (int)(base_name(from->file.path) - from->file.path),
             from->file.path, file.name);
    sha1_init(&whole);
    sha1_init(&head);
    while (out >= 0 && done < length)
    {
        int want = length - done < CHUNK ? (int)(length - done) : CHUNK;
        int got = (int)read(in, data_buffer, (size_t)want);
        if (got <= 0 || write_all(out, data_buffer, (size_t)got) != 0)
        {
            failed = 1;
            break;
        }
        sha1_update(&whole, data_buffer, (size_t)got);
        if (done < HEAD)
            sha1_update(&head, data_buffer, (size_t)(got < HEAD - done ? got : HEAD - done));
        done += got;
    }
    if (in >= 0)
        close(in);
    if (out >= 0)
        close(out);
    if (out < 0 || failed)
    {
        unlink(part);
        note(0, "%s could not be cut out of PATCH.RTP", file.name);
        return;
    }
    sha1_final(&whole, file.sha1);
    sha1_final(&head, file.head);
    file.size = length;
    add_staged(&file, part, from->group);
}

static void drop(staged_t *entry)
{
    if (entry->staged[0] != '\0')
        unlink(entry->staged);
    entry->staged[0] = '\0';
}

static int slot_present(ra_slot_t slot)
{
    char found[PATH_SIZE];
    return find_any_case(game, ra_slot_path(slot), found, sizeof(found));
}

static void remove_slot(ra_slot_t slot)
{
    char found[PATH_SIZE];
    while (find_any_case(game, ra_slot_path(slot), found, sizeof(found)))
        if (unlink(found) != 0)
            break;
}

static void place(staged_t *entry, ra_slot_t slot)
{
    char target[PATH_SIZE], existing[PATH_SIZE];
    const char *path = ra_slot_path(slot);
    const int known = ra_known_good(slot, &entry->file);

    if (find_any_case(game, path, existing, sizeof(existing)))
    {
        ra_file_t old;
        memset(&old, 0, sizeof(old));
        if (!known && (slot == RA_SLOT_REDALERT || slot == RA_SLOT_ALLIED || slot == RA_SLOT_SOVIET) &&
            checksum_file(existing, &old, slot == RA_SLOT_REDALERT) == 0 && ra_known_good(slot, &old))
        {
            note(1, "Kept the %s already there, a known copy, over the one in %s", path, entry->origin);
            drop(entry);
            return;
        }
    }
    if (slot == RA_SLOT_MAIN && entry->file.size < demo_main_limit() &&
        (slot_present(RA_SLOT_ALLIED) || slot_present(RA_SLOT_SOVIET)))
    {
        note(1, "Left out the small MAIN.MIX in %s (the demo's): the full game's discs are already there",
             entry->origin);
        drop(entry);
        return;
    }
    remove_slot(slot);
    snprintf(target, sizeof(target), "%s/%s", game, path);
    const char *slash = strrchr(path, '/');
    if (slash != NULL)
    {
        char folder[PATH_SIZE];
        snprintf(folder, sizeof(folder), "%s/%.*s", game, (int)(slash - path), path);
        mkdir(folder, 0755);
    }
    if (rename(entry->staged, target) != 0)
    {
        note(0, "%s could not be put in place (error %d)", path, errno);
        drop(entry);
        write_failed = 1;
        return;
    }
    entry->staged[0] = '\0';
    pthread_mutex_lock(&lock);
    status.placed |= 1u << slot;
    pthread_mutex_unlock(&lock);
    if (slot == RA_SLOT_MAIN)
        note(1, "Added main.mix from %s: it is of no disc the importer knows, so the game uses it for every "
                "disc it holds",
             entry->origin);
    else
        note(1, "Added %s (%s) from %s", path, ra_slot_title(slot), entry->origin);
    if (slot == RA_SLOT_REDALERT && !known)
        note(1, "That redalert.mix is not a version the importer knows (the demo's, or another language's?); "
                "it is used as it is");
}

/* Tells apart and places what a source gave, then clears the staging folder. */
static void finish_source(void)
{
    /* Aftermath's PATCH.RTP: its three files are cut out of it. */
    for (int i = 0, count = staged_count; i < count; i++)
    {
        ra_slot_t slots[3];
        long long offsets[3], lengths[3];
        if (strcmp(staged[i].file.name, "PATCH.RTP") != 0)
            continue;
        if (ra_aftermath_ranges(&staged[i].file, slots, offsets, lengths) == 3)
        {
            if (file_size(staged[i].staged) >= offsets[2] + lengths[2])
                for (int k = 0; k < 3; k++)
                    cut_range(&staged[i], slots[k], offsets[k], lengths[k]);
        }
        else
            note(0, "The PATCH.RTP in %s is not the one on the English Aftermath disc: its files are left out",
                 source_name);
        drop(&staged[i]);
    }

    for (int i = 0; i < staged_count; i++)
        if (staged[i].staged[0] != '\0')
            staged[i].slot = ra_classify(&staged[i].file, &groups[staged[i].group].context, &staged[i].why);

    for (int slot = 0; slot < RA_SLOT_COUNT && !write_failed; slot++)
    {
        int best = -1;
        for (int i = 0; i < staged_count; i++)
        {
            if (staged[i].staged[0] == '\0' || staged[i].slot != (ra_slot_t)slot)
                continue;
            /* A known copy wins; otherwise the last one found. */
            if (best < 0 || ra_known_good(slot, &staged[i].file) >= ra_known_good(slot, &staged[best].file))
                best = i;
        }
        if (best >= 0)
            place(&staged[best], slot);
    }

    /* The discs' MAIN.MIX files replace the demo's. */
    char demo[PATH_SIZE];
    if ((status.placed & (1u << RA_SLOT_ALLIED | 1u << RA_SLOT_SOVIET)) &&
        find_any_case(game, ra_slot_path(RA_SLOT_MAIN), demo, sizeof(demo)) && file_size(demo) < demo_main_limit())
    {
        unlink(demo);
        note(1, "Removed the demo's main.mix: the discs take its place");
    }
    if (counterstrike_patch && !slot_present(RA_SLOT_EXPAND))
        note(0, "Counterstrike's missions are packed inside its installer and cannot be read from the disc: "
                "send EXPAND.MIX from a Red Alert installed with Counterstrike, or from The Ultimate "
                "Collection");

    for (int i = 0; i < staged_count; i++)
        drop(&staged[i]);
    staged_count = 0;
    group_count = 0;
    counterstrike_patch = 0;
    cabinets_noted = 0;
}

/* Clears the staging folder of what an interrupted import left. */
static void clear_staging(void)
{
    DIR *dir = opendir(staging);
    if (dir == NULL)
        return;
    for (struct dirent *entry; (entry = readdir(dir)) != NULL;)
    {
        char path[PATH_SIZE];
        if (entry->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", staging, entry->d_name);
        unlink(path);
    }
    closedir(dir);
}

/* The free discs */

static void load_freeware_list(void)
{
    http_info_t info;
    char error[128];
    int used = 0, got;

    if (freeware_list_loaded)
        return;
    freeware_list_loaded = 1;
    const char *list = FREEWARE_LIST;
#ifndef __PROSPERO__
    /* The tests serve their own. */
    if (getenv("RA_FREEWARE_LIST") != NULL)
        list = getenv("RA_FREEWARE_LIST");
#endif
    http_t *http = http_get(list, 0, &info, error, sizeof(error));
    if (http == NULL)
    {
        printf("import: no list of free disc sources (%s), using the built-in one\n", error);
        return;
    }
    freeware_list = malloc(FREEWARE_LIST_LIMIT + 1);
    while (freeware_list != NULL && used < FREEWARE_LIST_LIMIT &&
           (got = http_read(http, freeware_list + used, FREEWARE_LIST_LIMIT - used)) > 0)
        used += got;
    if (freeware_list != NULL)
        freeware_list[used] = '\0';
    http_close(http);
}

/* The places to download a free disc from, best first: the repository's list, then the built-in
 * ones. Lines of the list are "<disc> <address>"; # starts a comment. */
static int freeware_sources(const char *disc, char out[][IMPORT_URL], int max)
{
    int count = 0;

    load_freeware_list();
    for (const char *line = freeware_list; line != NULL && *line != '\0' && count < max;)
    {
        const char *end = line + strcspn(line, "\r\n");
        char text[IMPORT_URL + 64], word[32], address[IMPORT_URL];
        snprintf(text, sizeof(text), "%.*s", (int)(end - line), line);
        if (text[0] != '#' && sscanf(text, "%31s %767s", word, address) == 2 && strcmp(word, disc) == 0)
            snprintf(out[count++], IMPORT_URL, "%s", address);
        line = end;
        while (*line == '\r' || *line == '\n')
            line++;
    }
    for (size_t i = 0; i < sizeof(freeware_builtin) / sizeof(freeware_builtin[0]) && count < max; i++)
    {
        int duplicate = 0;
#ifndef __PROSPERO__
        if (getenv("RA_FREEWARE_LIST") != NULL)
            break;
#endif
        if (strcmp(freeware_builtin[i].disc, disc) != 0)
            continue;
        for (int k = 0; k < count; k++)
            duplicate |= strcmp(out[k], freeware_builtin[i].url) == 0;
        if (!duplicate)
            snprintf(out[count++], IMPORT_URL, "%s", freeware_builtin[i].url);
    }
    return count;
}

static const char *freeware_title(const char *disc)
{
    for (size_t i = 0; i < sizeof(freeware_builtin) / sizeof(freeware_builtin[0]); i++)
        if (strcmp(freeware_builtin[i].disc, disc) == 0)
            return freeware_builtin[i].title;
    return "a free disc";
}

/* Running sources */

static int open_remote_source(stream_t *s, const char *url)
{
    stream_init(s, STREAM_HTTP);
    snprintf(s->url, sizeof(s->url), "%s", url);
    s->shown = 1;
    if (open_remote(s, 0) != 0)
        return -1;
    set_progress(0, s->size);
    return 0;
}

static int run_freeware(const char *disc)
{
    char alternatives[MAX_ALTERNATIVES][IMPORT_URL], name[NAME_SIZE], last_error[160] = "";
    int count = freeware_sources(disc, alternatives, MAX_ALTERNATIVES);

    for (int i = 0; i < count && !cancelled; i++)
    {
        stream_t s;
        url_file_name(name, sizeof(name), alternatives[i]);
        snprintf(source_name, sizeof(source_name), "%s", name[0] != '\0' ? name : freeware_title(disc));
        pthread_mutex_lock(&lock);
        snprintf(status.source, sizeof(status.source), "%s (%s)", freeware_title(disc), source_name);
        pthread_mutex_unlock(&lock);
        printf("import: %s from %s\n", freeware_title(disc), alternatives[i]);
        if (open_remote_source(&s, alternatives[i]) != 0 || stream_peek(&s) < 8 || looks_like_html(&s))
        {
            snprintf(last_error, sizeof(last_error), "%s", s.error[0] != '\0' ? s.error : "not the disc's archive");
            stream_close(&s);
            continue;
        }
        int result = walk_stream(&s, name, name, 0, 0);
        stream_close(&s);
        return result;
    }
    if (!cancelled)
    {
        note(0, "%s could not be downloaded: %s", freeware_title(disc), last_error[0] != '\0' ? last_error : "no source");
        read_failed = 1;
    }
    return -1;
}

static int run_source(const char *location, int single)
{
    char name[NAME_SIZE];
    struct stat info;
    int result = -1;

    pthread_mutex_lock(&lock);
    status.done = 0;
    status.total = -1;
    pthread_mutex_unlock(&lock);

    if (strncmp(location, "freeware:", 9) == 0)
        result = run_freeware(location + 9);
    else if (strstr(location, "://") != NULL)
    {
        stream_t s;
        url_file_name(name, sizeof(name), location);
        snprintf(source_name, sizeof(source_name), "%s", name[0] != '\0' ? name : location);
        pthread_mutex_lock(&lock);
        snprintf(status.source, sizeof(status.source), "%s", source_name);
        pthread_mutex_unlock(&lock);
        if (open_remote_source(&s, location) != 0)
        {
            if (!cancelled)
            {
                note(0, "%s cannot be downloaded: %s", source_name, s.error);
                read_failed = 1;
            }
        }
        else
            result = walk_stream(&s, name, name, 0, single);
        stream_close(&s);
    }
    else if (stat(location, &info) == 0 && S_ISDIR(info.st_mode))
    {
        if (import_flags & IMPORT_REMOVE_SOURCES)
            snprintf(folder_label, sizeof(folder_label), "the files sent");
        else
            snprintf(folder_label, sizeof(folder_label), "%s", base_name(location));
        snprintf(source_name, sizeof(source_name), "%s", folder_label);
        pthread_mutex_lock(&lock);
        snprintf(status.source, sizeof(status.source), "%s", source_name);
        pthread_mutex_unlock(&lock);
        folder_done = 0;
        set_progress(0, folder_bytes(location, 0));
        result = walk_folder(location, "", 0, (import_flags & IMPORT_REMOVE_SOURCES) != 0);
    }
    else
    {
        stream_t s;
        snprintf(source_name, sizeof(source_name), "%s", base_name(location));
        pthread_mutex_lock(&lock);
        snprintf(status.source, sizeof(status.source), "%s", source_name);
        pthread_mutex_unlock(&lock);
        if (open_file(&s, location) != 0)
        {
            note(0, "%s cannot be opened", source_name);
            read_failed = 1;
        }
        else
        {
            s.shown = 1;
            set_progress(0, s.size);
            result = walk_stream(&s, source_name, source_name, 0, 0);
        }
        stream_close(&s);
        if ((import_flags & IMPORT_REMOVE_SOURCES) && !cancelled && !write_failed)
            unlink(location);
    }
    return result;
}

static void *work(void *argument)
{
    (void)argument;
    mkdir(game, 0755);
    mkdir(staging, 0755);
    clear_staging();
    for (int i = 0; i < source_count && !cancelled && !write_failed; i++)
    {
        unsigned placed_before = status.placed;
        pthread_mutex_lock(&lock);
        status.source_index = i;
        pthread_mutex_unlock(&lock);
        int result = run_source(sources[i], source_count == 1);
        if (status.state == IMPORT_LISTING)
            return NULL;
        int found = staged_count > 0;
        finish_source();
        if (!found && result == 0 && !cancelled && !write_failed && status.placed == placed_before)
            note(0, "Found no Red Alert files in %s", source_name);
    }
    clear_staging();
    rmdir(staging);
    pthread_mutex_lock(&lock);
    status.current[0] = '\0';
    status.state = cancelled                                            ? IMPORT_CANCELLED
                   : (read_failed || write_failed) && status.placed == 0 ? IMPORT_FAILED
                                                                        : IMPORT_DONE;
    pthread_mutex_unlock(&lock);
    return NULL;
}

void import_start(char locations[][IMPORT_URL], int count, const char *game_folder, int flags)
{
    pthread_attr_t attributes;
    int i;

    import_finish();
    memset(&status, 0, sizeof(status));
    for (i = 0; i < count && i < IMPORT_MAX_SOURCES; i++)
        snprintf(sources[i], IMPORT_URL, "%s", locations[i]);
    source_count = i;
    import_flags = flags;
    snprintf(game, sizeof(game), "%s", game_folder);
    snprintf(staging, sizeof(staging), "%s/" STAGING, game_folder);
    status.state = IMPORT_RUNNING;
    status.source_count = source_count;
    status.total = -1;
    cancelled = 0;
    write_failed = read_failed = 0;
    staged_count = group_count = 0;
    counterstrike_patch = cabinets_noted = 0;
    free(freeware_list);
    freeware_list = NULL;
    freeware_list_loaded = 0;

    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, WORKER_STACK);
    if (pthread_create(&worker, &attributes, work, NULL) != 0)
    {
        status.state = IMPORT_FAILED;
        note(0, "The import cannot start");
    }
    else
        worker_running = 1;
    pthread_attr_destroy(&attributes);
}

void import_status(import_status_t *out)
{
    pthread_mutex_lock(&lock);
    *out = status;
    pthread_mutex_unlock(&lock);
}

const listing_t *import_listing(void)
{
    return &listing;
}

void import_cancel(void)
{
    cancelled = 1;
}

void import_finish(void)
{
    if (worker_running)
    {
        pthread_join(worker, NULL);
        worker_running = 0;
    }
    status.state = IMPORT_IDLE;
}

int import_take_note(char *text, size_t size, int *ok)
{
    int taken = 0;
    pthread_mutex_lock(&lock);
    if (note_count > 0)
    {
        snprintf(text, size, "%s", notes[note_first].text);
        *ok = notes[note_first].ok;
        note_first = (note_first + 1) % MAX_NOTES;
        note_count--;
        taken = 1;
    }
    pthread_mutex_unlock(&lock);
    return taken;
}
