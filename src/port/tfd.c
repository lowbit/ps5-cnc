/* The First Decade's cabinets, see tfd.h. unshield reads and writes through the callbacks here:
 * files below VIRTUAL are the image's, any other path is a real file, and files unshield writes
 * are checksummed on the way. */
#include "tfd.h"

#include <dirent.h>
#include <libunshield.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#include "sha1.h"

#define SECTOR 2048
#define MAX_ENTRIES 128
#define VIRTUAL "/tfd-image"
#define HEAD 4096

typedef struct
{
    char name[64]; /* without its ";1" */
    long long offset, size;
    int directory;
} entry_t;

typedef struct
{
    tfd_read_fn read;
    void *read_user;
    entry_t entries[MAX_ENTRIES]; /* the image's folder that holds data1.hdr */
    int count;
    tfd_progress_fn progress;
    void *user;
    long long written; /* by every file so far */
    int stopped;
    /* The last file written, once unshield closes it. */
    long long last_size;
    uint8_t last_sha1[20], last_head[20];
} context_t;

typedef struct
{
    FILE *real;
    const entry_t *entry; /* when it is in the image */
    long long position;
    int writing;
    sha1_t whole, head;
    long long size;
} handle_t;

typedef struct
{
    DIR *real;
    int next; /* the image folder's next entry */
    struct dirent dirent;
} folder_t;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Reads an ISO 9660 folder's records (the plain names; data1.hdr and the cabinets have 8.3 names). */
static int read_folder(tfd_read_fn read, void *user, long long offset, long long size, entry_t *entries, int max)
{
    uint8_t sector[SECTOR];
    int count = 0;

    if (size > 64 * SECTOR)
        size = 64 * SECTOR;
    for (long long at = 0; at < size; at += SECTOR)
    {
        if (read(user, offset + at, sector, SECTOR) != 0)
            return -1;
        for (int i = 0; i < SECTOR;)
        {
            const uint8_t *record = sector + i;
            int length = record[0], name_length = record[32];
            /* A record never crosses a sector; zeros pad the rest of one. */
            if (length == 0 || length < 34 || i + length > SECTOR)
                break;
            i += length;
            if (33 + name_length > length || (name_length == 1 && record[33] <= 1) || count == max ||
                (record[25] & 0x80) != 0)
                continue;
            entry_t *entry = &entries[count++];
            int n = 0;
            for (; n < name_length && n < (int)sizeof(entry->name) - 1 && record[33 + n] != ';'; n++)
                entry->name[n] = (char)record[33 + n];
            if (n > 0 && entry->name[n - 1] == '.')
                n--;
            entry->name[n] = '\0';
            entry->offset = (long long)le32(record + 2) * SECTOR;
            entry->size = le32(record + 10);
            entry->directory = (record[25] & 0x02) != 0;
        }
    }
    return count;
}

static int find_entry(const entry_t *entries, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (strcasecmp(entries[i].name, name) == 0)
            return i;
    return -1;
}

/* Fills the context with the folder holding data1.hdr: the top folder, or one below it. */
static int find_cabinets(context_t *context)
{
    uint8_t descriptor[SECTOR];
    entry_t top[MAX_ENTRIES];

    for (int sector = 16; sector < 32; sector++)
    {
        if (context->read(context->read_user, (long long)sector * SECTOR, descriptor, SECTOR) != 0 ||
            memcmp(descriptor + 1, "CD001", 5) != 0 || descriptor[0] == 255)
            return 0;
        if (descriptor[0] != 1)
            continue;
        const uint8_t *root = descriptor + 156;
        int count = read_folder(context->read, context->read_user, (long long)le32(root + 2) * SECTOR,
                                le32(root + 10), top, MAX_ENTRIES);
        if (count <= 0)
            return 0;
        if (find_entry(top, count, "DATA1.HDR") >= 0)
        {
            memcpy(context->entries, top, sizeof(top[0]) * (size_t)count);
            context->count = count;
            return 1;
        }
        for (int i = 0; i < count; i++)
        {
            if (!top[i].directory)
                continue;
            int below = read_folder(context->read, context->read_user, top[i].offset, top[i].size,
                                    context->entries, MAX_ENTRIES);
            if (below > 0 && find_entry(context->entries, below, "DATA1.HDR") >= 0)
            {
                context->count = below;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

int tfd_image_has_cabinets(tfd_read_fn read, void *user)
{
    context_t *context = calloc(1, sizeof(*context));
    if (context == NULL)
        return 0;
    context->read = read;
    context->read_user = user;
    int found = find_cabinets(context);
    free(context);
    return found;
}

static void *on_fopen(const char *filename, const char *modes, void *userdata)
{
    context_t *context = userdata;
    handle_t *handle = calloc(1, sizeof(*handle));

    if (handle == NULL)
        return NULL;
    if (context->read != NULL && strncmp(filename, VIRTUAL "/", sizeof(VIRTUAL)) == 0)
    {
        int index = strchr(modes, 'w') == NULL
                        ? find_entry(context->entries, context->count, filename + sizeof(VIRTUAL))
                        : -1;
        if (index < 0 || context->entries[index].directory)
        {
            free(handle);
            return NULL;
        }
        handle->entry = &context->entries[index];
        return handle;
    }
    handle->real = fopen(filename, modes);
    if (handle->real == NULL)
    {
        free(handle);
        return NULL;
    }
    if (strchr(modes, 'w') != NULL)
    {
        /* unshield writes in small pieces; the title folder wants large writes. */
        setvbuf(handle->real, NULL, _IOFBF, 1 << 20);
        handle->writing = 1;
        sha1_init(&handle->whole);
        sha1_init(&handle->head);
    }
    return handle;
}

static int on_fseek(void *file, long int offset, int whence, void *userdata)
{
    handle_t *handle = file;
    (void)userdata;
    if (handle->real != NULL)
        return fseeko(handle->real, offset, whence);
    long long target = whence == SEEK_SET ? offset
                     : whence == SEEK_CUR ? handle->position + offset
                                          : handle->entry->size + offset;
    if (target < 0)
        return -1;
    handle->position = target;
    return 0;
}

static long int on_ftell(void *file, void *userdata)
{
    handle_t *handle = file;
    (void)userdata;
    return handle->real != NULL ? (long int)ftello(handle->real) : (long int)handle->position;
}

static size_t on_fread(void *ptr, size_t size, size_t n, void *file, void *userdata)
{
    handle_t *handle = file;
    context_t *context = userdata;

    if (handle->real != NULL)
        return fread(ptr, size, n, handle->real);
    if (size == 0 || context->stopped)
        return 0;
    long long wanted = (long long)(size * n), left = handle->entry->size - handle->position;
    if (wanted > left)
        wanted = left > 0 ? left - left % (long long)size : 0;
    for (long long done = 0; done < wanted;)
    {
        int part = wanted - done > (1 << 30) ? (1 << 30) : (int)(wanted - done);
        if (context->read(context->read_user, handle->entry->offset + handle->position, (uint8_t *)ptr + done, part) != 0)
            return (size_t)(done / (long long)size);
        handle->position += part;
        done += part;
    }
    return (size_t)(wanted / (long long)size);
}

static size_t on_fwrite(const void *ptr, size_t size, size_t n, void *file, void *userdata)
{
    handle_t *handle = file;
    context_t *context = userdata;

    if (handle->real == NULL || context->stopped)
        return 0;
    size_t written = fwrite(ptr, size, n, handle->real);
    if (handle->writing && written > 0)
    {
        size_t bytes = written * size;
        sha1_update(&handle->whole, ptr, bytes);
        if (handle->size < HEAD)
            sha1_update(&handle->head, ptr, bytes < (size_t)(HEAD - handle->size) ? bytes : (size_t)(HEAD - handle->size));
        handle->size += (long long)bytes;
        context->written += (long long)bytes;
        if (context->progress != NULL && context->progress(context->written, context->user) != 0)
            context->stopped = 1;
    }
    return written;
}

static int on_fclose(void *file, void *userdata)
{
    handle_t *handle = file;
    context_t *context = userdata;
    int result = 0;

    if (handle->real != NULL)
        result = fclose(handle->real);
    if (handle->writing)
    {
        sha1_final(&handle->whole, context->last_sha1);
        sha1_final(&handle->head, context->last_head);
        context->last_size = handle->size;
    }
    free(handle);
    return result;
}

static void *on_opendir(const char *name, void *userdata)
{
    context_t *context = userdata;
    folder_t *folder = calloc(1, sizeof(*folder));

    if (folder == NULL)
        return NULL;
    if (context->read != NULL && strcmp(name, VIRTUAL) == 0)
        return folder;
    folder->real = opendir(name);
    if (folder->real == NULL)
    {
        free(folder);
        return NULL;
    }
    return folder;
}

static int on_closedir(void *dir, void *userdata)
{
    folder_t *folder = dir;
    (void)userdata;
    if (folder->real != NULL)
        closedir(folder->real);
    free(folder);
    return 0;
}

static struct dirent *on_readdir(void *dir, void *userdata)
{
    folder_t *folder = dir;
    context_t *context = userdata;

    if (folder->real != NULL)
        return readdir(folder->real);
    while (folder->next < context->count && context->entries[folder->next].directory)
        folder->next++;
    if (folder->next >= context->count)
        return NULL;
    memset(&folder->dirent, 0, sizeof(folder->dirent));
    snprintf(folder->dirent.d_name, sizeof(folder->dirent.d_name), "%s", context->entries[folder->next++].name);
    return &folder->dirent;
}

/* Whether a folder in the cabinets is Red Alert's ("Red Alert"): The First Decade holds every
 * game of the series, and Tiberian Dawn's files have some of the same names. */
static int in_red_alert(const char *folder)
{
    for (const char *at = folder; *at != '\0'; at++)
        if (strncasecmp(at, "red alert", 9) == 0 || strncasecmp(at, "redalert", 8) == 0)
            return 1;
    return 0;
}

static const UnshieldIoCallbacks callbacks = {
    .fopen = on_fopen,
    .fseek = on_fseek,
    .ftell = on_ftell,
    .fread = on_fread,
    .fwrite = on_fwrite,
    .fclose = on_fclose,
    .opendir = on_opendir,
    .closedir = on_closedir,
    .readdir = on_readdir,
};

int tfd_extract(const char *hdr_path, tfd_read_fn read, void *read_user, const char *staging, int *serial,
                int (*wanted)(const char *name), tfd_found_fn found, tfd_progress_fn progress, void *user,
                char *error, size_t error_size)
{
    context_t *context = calloc(1, sizeof(*context));
    char opened[1024], target[1024];
    int extracted = 0;

    if (context == NULL)
    {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    context->read = read;
    context->read_user = read_user;
    context->progress = progress;
    context->user = user;
    if (read != NULL)
    {
        if (!find_cabinets(context))
        {
            snprintf(error, error_size, "no InstallShield cabinets in the image");
            free(context);
            return -1;
        }
        int index = find_entry(context->entries, context->count, "DATA1.HDR");
        snprintf(opened, sizeof(opened), VIRTUAL "/%s", context->entries[index].name);
    }
    else
        snprintf(opened, sizeof(opened), "%s", hdr_path);

    unshield_set_log_level(UNSHIELD_LOG_LEVEL_LOWEST);
    Unshield *unshield = unshield_open2(opened, &callbacks, context);
    if (unshield == NULL)
    {
        snprintf(error, error_size, "the InstallShield cabinets cannot be read");
        free(context);
        return -1;
    }
    for (int i = 0; i < unshield_file_count(unshield) && !context->stopped; i++)
    {
        const char *name = unshield_file_name(unshield, i);
        const char *folder = unshield_directory_name(unshield, unshield_file_directory(unshield, i));
        tfd_file_t file;

        if (name == NULL || !unshield_file_is_valid(unshield, i))
            continue;
        memset(&file, 0, sizeof(file));
        for (size_t c = 0; name[c] != '\0' && c < sizeof(file.name) - 1; c++)
            file.name[c] = (char)(name[c] >= 'a' && name[c] <= 'z' ? name[c] - 'a' + 'A' : name[c]);
        snprintf(file.folder, sizeof(file.folder), "%s", folder != NULL ? folder : "");
        for (char *c = file.folder; *c != '\0'; c++)
            if (*c == '\\')
                *c = '/';
        if (!wanted(file.name) || !in_red_alert(file.folder))
            continue;

        snprintf(target, sizeof(target), "%s/%d.tfd", staging, ++*serial);
        context->last_size = -1;
        if (!unshield_file_save(unshield, i, target) || context->last_size < 0)
        {
            remove(target);
            if (!context->stopped)
                snprintf(error, error_size, "%s could not be unpacked from the cabinets", file.name);
            extracted = -1;
            break;
        }
        file.written = target;
        file.size = context->last_size;
        memcpy(file.sha1, context->last_sha1, sizeof(file.sha1));
        memcpy(file.head, context->last_head, sizeof(file.head));
        found(&file, user);
        extracted++;
    }
    if (context->stopped && extracted >= 0)
    {
        snprintf(error, error_size, "stopped");
        extracted = -1;
    }
    unshield_close(unshield);
    free(context);
    return extracted;
}
