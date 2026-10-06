/* Red Alert's files, see rafiles.h. The checksums come from OpenRA's content installer
 * (the .yaml files in mods/ra-content/installer), which lists every source it knows. */
#include "rafiles.h"

#include "sha1.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_EXTRA 8

typedef struct
{
    const char *kind, *hex;
} known_t;

/* REDALERT.MIX is the same file in every English source: both freeware discs, The Ultimate
 * Collection and the Remastered Collection. The discs' MAIN.MIX are told apart by their first 4096
 * bytes. */
static const known_t known[] = {
    { "redalert", "0e58f4b54f44f6cd29fecf8cf379d33cf2d4caef" },
    { "allied", "20ebe16f91ff79be2d672f1db5bae9048ff9357c" },
    { "soviet", "9d108f18560716b684ab8b1da42cc7f3d1b52519" },
    { "aftermath-patch", "5bce93f834f9322ddaa7233242e5b6c7fea0bf17" },
    { "counterstrike-patch", "fae8ba82db71574f6ecd8fb4ff4026fcb65d2adc" },
    { "counterstrike-readme", "0efe8087383f0b159a9633f891fb5f53c6097cd4" },
    { "aftermath-readme", "9902fb74c019df1b76ff5634e68f0371d790b5e0" },
};

static struct
{
    char kind[24];
    uint8_t sha1[20];
} extra[MAX_EXTRA];
static int extra_count;

static const char *const paths[RA_SLOT_COUNT] = {
    "redalert.mix", "main.mix", "allied/main.mix", "soviet/main.mix", "counterstrike/main.mix",
    "aftermath/main.mix", "expand.mix", "expand2.mix", "hires1.mix", "lores1.mix",
};

static const char *const titles[RA_SLOT_COUNT] = {
    "the game's files",
    "every disc it holds",
    "the Allied campaign and its movies",
    "the Soviet campaign and its movies",
    "Counterstrike's music",
    "Aftermath's music and sounds",
    "Counterstrike's missions",
    "Aftermath's missions and units",
    "Aftermath's graphics",
    "Aftermath's low-resolution graphics",
};

const char *ra_slot_path(ra_slot_t slot)
{
    return slot >= 0 && slot < RA_SLOT_COUNT ? paths[slot] : "";
}

const char *ra_slot_title(ra_slot_t slot)
{
    return slot >= 0 && slot < RA_SLOT_COUNT ? titles[slot] : "";
}

static int is_known(const char *kind, const uint8_t sha1[20])
{
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
    {
        uint8_t digest[20];
        if (strcmp(known[i].kind, kind) == 0 && sha1_parse(digest, known[i].hex) == 0 &&
            memcmp(digest, sha1, 20) == 0)
            return 1;
    }
    for (int i = 0; i < extra_count; i++)
        if (strcmp(extra[i].kind, kind) == 0 && memcmp(extra[i].sha1, sha1, 20) == 0)
            return 1;
    return 0;
}

void ra_add_known(const char *kind, const uint8_t sha1[20])
{
    if (extra_count == MAX_EXTRA)
        return;
    snprintf(extra[extra_count].kind, sizeof(extra[extra_count].kind), "%s", kind);
    memcpy(extra[extra_count++].sha1, sha1, 20);
}

static int ends_with(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);
    return length >= suffix_length && strcasecmp(name + length - suffix_length, suffix) == 0;
}

int ra_wanted(const char *name)
{
    static const char *const names[] = {
        "REDALERT.MIX", "MAIN.MIX", "MAIN1.MIX", "MAIN2.MIX", "MAIN3.MIX", "MAIN4.MIX", "EXPAND.MIX",
        "EXPAND2.MIX", "HIRES1.MIX", "LORES1.MIX", "PATCH.RTP", "CSTRIKE.RTP", "README.TXT",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(name, names[i]) == 0)
            return 1;
    return 0;
}

int ra_container(const char *name)
{
    static const char *const extensions[] = { ".iso", ".bin", ".img", ".mdf", ".zip", ".7z", ".rar" };
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
        if (ends_with(name, extensions[i]))
            return 1;
    return 0;
}

void ra_learn(const ra_file_t *file, ra_context_t *context)
{
    if (strcmp(file->name, "CSTRIKE.RTP") == 0 || (strcmp(file->name, "README.TXT") == 0 &&
                                                   is_known("counterstrike-readme", file->sha1)))
        context->counterstrike = 1;
    if ((strcmp(file->name, "PATCH.RTP") == 0 && is_known("aftermath-patch", file->sha1)) ||
        (strcmp(file->name, "README.TXT") == 0 && is_known("aftermath-readme", file->sha1)))
        context->aftermath = 1;
}

/* Which disc a folder or container name on the way to the file says it is, looking from the
 * nearest one out. Words are looked for anywhere in a name ("RedAlert1_AlliedDisc.iso"), disc
 * numbers only as whole words ("CD1", the Remastered Collection's name for the Allied disc). A
 * name that speaks of two discs ("Counterstrike & Aftermath") says nothing, and ends the search:
 * the folders around it hold more than one disc. */
static ra_slot_t disc_from_path(const char *path)
{
    static const struct
    {
        const char *word;
        ra_slot_t slot;
    } words[] = {
        { "aftermath", RA_SLOT_AFTERMATH }, { "counterstrike", RA_SLOT_COUNTERSTRIKE },
        { "counter strike", RA_SLOT_COUNTERSTRIKE }, { "counter-strike", RA_SLOT_COUNTERSTRIKE },
        { "cstrike", RA_SLOT_COUNTERSTRIKE }, { "soviet", RA_SLOT_SOVIET }, { "sowjet", RA_SLOT_SOVIET },
        { "allied", RA_SLOT_ALLIED }, { "allies", RA_SLOT_ALLIED }, { "alliierte", RA_SLOT_ALLIED },
    }, numbers[] = {
        { "cd1", RA_SLOT_ALLIED }, { "disc1", RA_SLOT_ALLIED }, { "disk1", RA_SLOT_ALLIED },
        { "cd2", RA_SLOT_SOVIET }, { "disc2", RA_SLOT_SOVIET }, { "disk2", RA_SLOT_SOVIET },
    };
    char lower[512];
    const char *last = strrchr(path, '/');
    size_t length;

    /* Only the parts before the file's own name. */
    if (last == NULL)
        return RA_SLOT_NONE;
    length = (size_t)(last - path) < sizeof(lower) ? (size_t)(last - path) : sizeof(lower) - 1;
    for (size_t i = 0; i < length; i++)
        lower[i] = (char)tolower((unsigned char)path[i]);
    lower[length] = '\0';

    while (length > 0)
    {
        char *part = strrchr(lower, '/');
        part = part != NULL ? part + 1 : lower;
        ra_slot_t found = RA_SLOT_NONE;
        int ambiguous = 0;

        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        {
            if (strstr(part, words[i].word) == NULL)
                continue;
            ambiguous |= found != RA_SLOT_NONE && found != words[i].slot;
            found = words[i].slot;
        }
        for (const char *at = part; *at != '\0';)
        {
            size_t word = 0;
            while (isalnum((unsigned char)at[word]))
                word++;
            for (size_t i = 0; word > 0 && i < sizeof(numbers) / sizeof(numbers[0]); i++)
            {
                if (strlen(numbers[i].word) != word || strncmp(at, numbers[i].word, word) != 0)
                    continue;
                ambiguous |= found != RA_SLOT_NONE && found != numbers[i].slot;
                found = numbers[i].slot;
            }
            at += word > 0 ? word : 1;
        }
        if (ambiguous)
            return RA_SLOT_NONE;
        if (found != RA_SLOT_NONE)
            return found;
        /* On to the name around it. */
        length = part > lower ? (size_t)(part - lower - 1) : 0;
        lower[length] = '\0';
    }
    return RA_SLOT_NONE;
}

ra_slot_t ra_classify(const ra_file_t *file, const ra_context_t *context, const char **why)
{
    static const struct
    {
        const char *name;
        ra_slot_t slot;
    } fixed[] = {
        { "REDALERT.MIX", RA_SLOT_REDALERT }, { "EXPAND.MIX", RA_SLOT_EXPAND },
        { "EXPAND2.MIX", RA_SLOT_EXPAND2 },   { "HIRES1.MIX", RA_SLOT_HIRES1 },
        { "LORES1.MIX", RA_SLOT_LORES1 },     { "MAIN1.MIX", RA_SLOT_ALLIED },
        { "MAIN2.MIX", RA_SLOT_SOVIET },      { "MAIN3.MIX", RA_SLOT_COUNTERSTRIKE },
        { "MAIN4.MIX", RA_SLOT_AFTERMATH },
    };
    const char *unused;
    if (why == NULL)
        why = &unused;
    *why = "";

    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
    {
        if (strcmp(file->name, fixed[i].name) == 0)
        {
            if (fixed[i].slot == RA_SLOT_REDALERT && !is_known("redalert", file->sha1))
                *why = "a version not in the table, the demo's perhaps";
            else if (strncmp(file->name, "MAIN", 4) == 0)
                *why = "The Ultimate Collection's numbering";
            return fixed[i].slot;
        }
    }
    if (strcmp(file->name, "MAIN.MIX") != 0)
        return RA_SLOT_NONE;

    if (is_known("allied", file->head))
    {
        *why = "the freeware or retail Allied disc";
        return RA_SLOT_ALLIED;
    }
    if (is_known("soviet", file->head))
    {
        *why = "the freeware or retail Soviet disc";
        return RA_SLOT_SOVIET;
    }
    ra_slot_t slot = disc_from_path(file->path);
    if (slot != RA_SLOT_NONE)
    {
        *why = "named so by its folder";
        return slot;
    }
    if (context->aftermath)
    {
        *why = "it came with the Aftermath disc's other files";
        return RA_SLOT_AFTERMATH;
    }
    if (context->counterstrike)
    {
        *why = "it came with the Counterstrike disc's other files";
        return RA_SLOT_COUNTERSTRIKE;
    }
    *why = "no disc it is known from, so the game uses it for every disc it holds";
    return RA_SLOT_MAIN;
}

int ra_known_good(ra_slot_t slot, const ra_file_t *file)
{
    switch (slot)
    {
    case RA_SLOT_REDALERT:
        return is_known("redalert", file->sha1);
    case RA_SLOT_ALLIED:
        return is_known("allied", file->head);
    case RA_SLOT_SOVIET:
        return is_known("soviet", file->head);
    default:
        return 0;
    }
}

int ra_aftermath_ranges(const ra_file_t *file, ra_slot_t slots[3], long long offsets[3], long long lengths[3])
{
    static const ra_slot_t which[3] = { RA_SLOT_EXPAND2, RA_SLOT_HIRES1, RA_SLOT_LORES1 };
    static const long long at[3] = { 4712984, 5182981, 5273320 }, size[3] = { 469922, 90264, 57076 };
    if (strcmp(file->name, "PATCH.RTP") != 0 || !is_known("aftermath-patch", file->sha1))
        return 0;
    for (int i = 0; i < 3; i++)
    {
        slots[i] = which[i];
        offsets[i] = at[i];
        lengths[i] = size[i];
    }
    return 3;
}

int ra_counterstrike_patch(const ra_file_t *file)
{
    return strcmp(file->name, "CSTRIKE.RTP") == 0;
}

/* Whether folder/relative exists as a regular file, each part matched in any case, as Vanilla
 * Conquer matches them. */
static int exists_any_case(const char *folder, const char *relative)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s", folder);
    for (const char *part = relative; *part != '\0';)
    {
        size_t length = strcspn(part, "/");
        DIR *dir = opendir(path);
        int found = 0;
        if (dir == NULL)
            return 0;
        for (struct dirent *entry; (entry = readdir(dir)) != NULL;)
        {
            if (strlen(entry->d_name) == length && strncasecmp(entry->d_name, part, length) == 0)
            {
                size_t used = strlen(path);
                snprintf(path + used, sizeof(path) - used, "/%s", entry->d_name);
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
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

unsigned ra_scan(const char *folder)
{
    unsigned slots = 0;
    for (int slot = 0; slot < RA_SLOT_COUNT; slot++)
        if (exists_any_case(folder, paths[slot]))
            slots |= 1u << slot;
    return slots;
}

int ra_playable(unsigned slots)
{
    const unsigned discs = 1u << RA_SLOT_MAIN | 1u << RA_SLOT_ALLIED | 1u << RA_SLOT_SOVIET |
                           1u << RA_SLOT_COUNTERSTRIKE | 1u << RA_SLOT_AFTERMATH;
    return (slots & 1u << RA_SLOT_REDALERT) && (slots & discs);
}
