/* The launcher, see launcher.h. One view at a time, drawn every frame; imports run on the
 * importer's thread and the views show their progress and notes. */
#include <SDL.h>
#include <dirent.h>
#include <qrcodegen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "launcher_font.h"
#include "importer.h"
#include "launcher.h"
#include "rafiles.h"
#include "upload.h"
#include "url.h"

#ifdef __PROSPERO__
/* The SDL port's: the next on-screen keyboard starts with this text. */
void PS5_SetScreenKeyboardText(const char *text, int max_length);
#endif

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define MARGIN 140
#define ROW_HEIGHT 64
#define NOTES_KEPT 8
#define NOTE_TEXT 240
#define LIST_ROWS 11
#define REPEAT_DELAY 18
#define REPEAT_RATE 5
#define STICK 16000
#define MB (1024.0 * 1024.0)
#define MAX_FILES 256
#define PATH_SIZE 1024
#define QR_MODULE 8
#define USB_DRIVES 8
#define FOOTER_Y (SCREEN_HEIGHT - 76)

typedef struct
{
    SDL_Texture *texture;
    const short (*glyphs)[4];
    int height;
} font_t;

typedef enum
{
    VIEW_MENU,
    VIEW_FREEWARE,
    VIEW_SEND,
    VIEW_LINK,
    VIEW_BROWSE,
    VIEW_FILES,
    VIEW_IMPORT,
} view_t;

enum
{
    INPUT_UP = 1,
    INPUT_DOWN = 2,
    INPUT_CROSS = 4,
    INPUT_CIRCLE = 8,
};

typedef enum
{
    ITEM_PLAY,
    ITEM_FREEWARE,
    ITEM_SEND,
    ITEM_LINK,
    ITEM_USB,
    ITEM_QUIT,
    ITEM_COUNT
} item_t;

typedef struct
{
    char text[NOTE_TEXT];
    int ok;
} note_t;

typedef struct
{
    char name[256];
    int folder;
} file_entry_t;

static const SDL_Color white = { 0xf1, 0xe9, 0xe6, 0xff };
static const SDL_Color muted = { 0xb0, 0x9f, 0x9a, 0xff };
static const SDL_Color faint = { 0x6e, 0x60, 0x5c, 0xff };
static const SDL_Color accent = { 0xe0, 0x44, 0x3a, 0xff };
static const SDL_Color good = { 0x58, 0xc2, 0x6e, 0xff };
static const SDL_Color bad = { 0xff, 0x6b, 0x5f, 0xff };
static const SDL_Color panel = { 0x22, 0x1a, 0x18, 0xff };
static const SDL_Color background = { 0x14, 0x10, 0x0f, 0xff };

static SDL_Window *window;
static SDL_Renderer *renderer;
static font_t small_font, body, title;
static const char *game, *incoming, *settings;
static view_t view, import_return;
static int row, quit, chosen;
static unsigned slots;
static note_t notes[NOTES_KEPT];
static int note_count;
static char link_text[IMPORT_URL], typed[IMPORT_URL];
static int typing, typing_shown;
static Uint32 typing_started;
static listing_t browse;
static int browse_row;
static char files_path[PATH_SIZE];
static file_entry_t files[MAX_FILES];
static int files_count, files_row, files_dirty;
static int importing;      /* an import of ours is running */
static int incoming_batch; /* it reads the incoming folder */
static int send_ready, batches_seen, batch_waiting;
static unsigned held, pressed;
static int repeat_timer;
static SDL_GameController *controllers[4];

/* Settings: the last link typed, kept between runs. */

static void settings_path(char *out, size_t size)
{
    snprintf(out, size, "%s/launcher.txt", settings);
}

static void load_settings(void)
{
    char path[PATH_SIZE], line[IMPORT_URL + 16];
    FILE *file;

    settings_path(path, sizeof(path));
    if ((file = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "link=", 5) == 0)
            snprintf(link_text, sizeof(link_text), "%s", line + 5);
    }
    fclose(file);
}

static void save_settings(void)
{
    char path[PATH_SIZE];
    FILE *file;

    mkdir(settings, 0755);
    settings_path(path, sizeof(path));
    if ((file = fopen(path, "w")) == NULL)
        return;
    fprintf(file, "link=%s\n", link_text);
    fclose(file);
}

/* Notes: what imports did, on the screen and on the upload page. */

static void add_note(const char *text, int ok)
{
    if (note_count == NOTES_KEPT)
    {
        memmove(notes, notes + 1, sizeof(notes[0]) * (NOTES_KEPT - 1));
        note_count--;
    }
    snprintf(notes[note_count].text, sizeof(notes[0].text), "%s", text);
    notes[note_count++].ok = ok;
    if (upload_running())
        upload_note(text, ok);
}

static void note(int ok, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void note(int ok, const char *format, ...)
{
    char text[NOTE_TEXT];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    printf("launcher: %s\n", text);
    add_note(text, ok);
}

/* Drawing */

static font_t load_font(const unsigned char *pixels, int width, int height, const short (*glyphs)[4])
{
    font_t font = { NULL, glyphs, height };
    Uint32 *argb = malloc((size_t)width * height * sizeof(Uint32));
    if (argb == NULL)
        return font;
    for (int i = 0; i < width * height; i++)
        argb[i] = (Uint32)pixels[i] << 24 | 0xffffff;
    font.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (font.texture != NULL)
    {
        SDL_UpdateTexture(font.texture, NULL, argb, width * (int)sizeof(Uint32));
        SDL_SetTextureBlendMode(font.texture, SDL_BLENDMODE_BLEND);
    }
    free(argb);
    return font;
}

static const short *glyph(const font_t *font, char c)
{
    return font->glyphs[(c >= ' ' && c <= '~' ? c : '?') - ' '];
}

static int text_width(const font_t *font, const char *text, size_t length)
{
    int width = 0;
    for (size_t i = 0; i < length && text[i] != '\0'; i++)
        width += glyph(font, text[i])[2];
    return width;
}

static void draw_text(const font_t *font, int x, int y, const char *text, SDL_Color colour)
{
    SDL_SetTextureColorMod(font->texture, colour.r, colour.g, colour.b);
    for (; *text != '\0'; text++)
    {
        const short *g = glyph(font, *text);
        SDL_Rect source = { g[0], 0, g[1], font->height };
        SDL_Rect target = { x + g[3], y, g[1], font->height };
        SDL_RenderCopy(renderer, font->texture, &source, &target);
        x += g[2];
    }
}

/* Draws text cut to width, with "..." where it was cut. */
static void draw_text_fit(const font_t *font, int x, int y, int width, const char *text, SDL_Color colour)
{
    char line[512];
    size_t length = strlen(text);

    if (text_width(font, text, length) <= width)
    {
        draw_text(font, x, y, text, colour);
        return;
    }
    const int dots = text_width(font, "...", 3);
    while (length > 0 && text_width(font, text, length) + dots > width)
        length--;
    snprintf(line, sizeof(line), "%.*s...", (int)length, text);
    draw_text(font, x, y, line, colour);
}

/* Draws text wrapped at width; returns the height it took. */
static int draw_wrapped(const font_t *font, int x, int y, int width, const char *text, SDL_Color colour)
{
    int top = y;
    char line[512];
    while (*text != '\0')
    {
        size_t length = 0, fits = 0;
        while (text[length] != '\0' && length < sizeof(line) - 1)
        {
            size_t word = length;
            while (text[word] == ' ')
                word++;
            while (text[word] != '\0' && text[word] != ' ')
                word++;
            if (text_width(font, text, word) > width && fits > 0)
                break;
            length = fits = word;
        }
        if (fits == 0)
            fits = length;
        memcpy(line, text, fits);
        line[fits] = '\0';
        draw_text(font, x, y, line, colour);
        y += font->height + 4;
        text += fits;
        while (*text == ' ')
            text++;
    }
    return y - top;
}

static void fill(int x, int y, int w, int h, SDL_Color colour)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_qr(const char *text, int x, int y)
{
    uint8_t code[qrcodegen_BUFFER_LEN_MAX], buffer[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text, buffer, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return;
    int size = qrcodegen_getSize(code);
    fill(x, y, (size + 8) * QR_MODULE, (size + 8) * QR_MODULE, (SDL_Color){ 255, 255, 255, 255 });
    for (int r = 0; r < size; r++)
        for (int c = 0; c < size; c++)
            if (qrcodegen_getModule(code, c, r))
                fill(x + (c + 4) * QR_MODULE, y + (r + 4) * QR_MODULE, QR_MODULE, QR_MODULE, (SDL_Color){ 0, 0, 0, 255 });
}

static void draw_bar(int x, int y, int width, long long done, long long total)
{
    char line[96];
    fill(x, y, width, 20, panel);
    if (total > 0)
    {
        if (done > total)
            done = total;
        fill(x, y, (int)(width * (double)done / (double)total), 20, accent);
        snprintf(line, sizeof(line), "%.1f of %.1f MB", done / MB, total / MB);
    }
    else
        snprintf(line, sizeof(line), "%.1f MB", done / MB);
    draw_text(&small_font, x, y + 28, line, muted);
}

static void draw_title(const char *text)
{
    draw_text(&title, MARGIN, 90, text, white);
}

static void draw_footer(const char *text)
{
    draw_text(&small_font, MARGIN, FOOTER_Y, text, muted);
}

static int draw_row(int y, const char *text, int selected, int enabled)
{
    if (selected)
        fill(MARGIN - 20, y - 10, 860, ROW_HEIGHT - 8, accent);
    draw_text_fit(&body, MARGIN, y, 820, text, !enabled ? faint : selected ? white : muted);
    return y + ROW_HEIGHT;
}

static int draw_notes(int y, int bottom)
{
    /* The newest ones that fit, oldest first. */
    int first = note_count;
    for (int height = 0; first > 0; first--)
    {
        int lines = text_width(&small_font, notes[first - 1].text, strlen(notes[first - 1].text)) /
                        (SCREEN_WIDTH - 2 * MARGIN - 40) +
                    1;
        height += lines * (small_font.height + 4) + 8;
        if (y + height > bottom)
            break;
    }
    for (int i = first; i < note_count; i++)
    {
        fill(MARGIN, y + 8, 14, 14, notes[i].ok ? good : bad);
        y += draw_wrapped(&small_font, MARGIN + 30, y, SCREEN_WIDTH - 2 * MARGIN - 40, notes[i].text,
                          notes[i].ok ? white : bad) +
             8;
    }
    return y;
}

/* Input */

static void open_controller(int index)
{
    for (int i = 0; i < 4; i++)
    {
        if (controllers[i] == NULL)
        {
            controllers[i] = SDL_GameControllerOpen(index);
            return;
        }
    }
}

static void start_typing(void)
{
    typed[0] = '\0';
    typing = 1;
    typing_shown = 0;
    typing_started = SDL_GetTicks();
#ifdef __PROSPERO__
    PS5_SetScreenKeyboardText(link_text[0] != '\0' ? link_text : "http://", IMPORT_URL - 8);
#endif
    SDL_StartTextInput();
}

static void stop_typing(int keep)
{
    SDL_StopTextInput();
    typing = 0;
    if (keep && typed[0] != '\0')
    {
        url_clean(link_text, sizeof(link_text), typed);
        save_settings();
    }
}

static void read_input(void)
{
    SDL_Event event;
    unsigned now = 0, stick = 0;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            quit = 1;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_controller(event.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (typing)
                break;
            switch (event.cbutton.button)
            {
            case SDL_CONTROLLER_BUTTON_A:
                now |= INPUT_CROSS;
                break;
            case SDL_CONTROLLER_BUTTON_B:
                now |= INPUT_CIRCLE;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                now |= INPUT_UP;
                held |= INPUT_UP;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                now |= INPUT_DOWN;
                held |= INPUT_DOWN;
                break;
            }
            break;
        case SDL_CONTROLLERBUTTONUP:
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                held &= ~INPUT_UP;
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                held &= ~INPUT_DOWN;
            break;
        case SDL_TEXTINPUT:
            if (typing)
                snprintf(typed + strlen(typed), sizeof(typed) - strlen(typed), "%s", event.text.text);
            break;
        case SDL_KEYDOWN:
            if (typing)
            {
                if (event.key.keysym.sym == SDLK_RETURN)
                    stop_typing(1);
                else if (event.key.keysym.sym == SDLK_ESCAPE)
                    stop_typing(0);
                break;
            }
            if (event.key.repeat)
                break;
            switch (event.key.keysym.sym)
            {
            case SDLK_UP:
                now |= INPUT_UP;
                held |= INPUT_UP;
                break;
            case SDLK_DOWN:
                now |= INPUT_DOWN;
                held |= INPUT_DOWN;
                break;
            case SDLK_RETURN:
                now |= INPUT_CROSS;
                break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                now |= INPUT_CIRCLE;
                break;
            }
            break;
        case SDL_KEYUP:
            if (event.key.keysym.sym == SDLK_UP)
                held &= ~INPUT_UP;
            if (event.key.keysym.sym == SDLK_DOWN)
                held &= ~INPUT_DOWN;
            break;
        }
    }

#ifdef __PROSPERO__
    /* The on-screen keyboard: its text came as text events; it is done once it has closed. */
    if (typing)
    {
        if (SDL_IsScreenKeyboardShown(window))
            typing_shown = 1;
        else if (typing_shown || SDL_GetTicks() - typing_started > 3000)
            stop_typing(typing_shown);
        held = 0;
        pressed = 0;
        return;
    }
#endif

    for (int i = 0; i < 4; i++)
    {
        if (controllers[i] == NULL)
            continue;
        int y = SDL_GameControllerGetAxis(controllers[i], SDL_CONTROLLER_AXIS_LEFTY);
        if (y < -STICK)
            stick |= INPUT_UP;
        if (y > STICK)
            stick |= INPUT_DOWN;
    }
    static unsigned stick_before;
    now |= stick & ~stick_before;
    stick_before = stick;

    /* Held directions repeat. */
    const unsigned directions = (held | stick) & (INPUT_UP | INPUT_DOWN);
    if (directions == 0 || (now & (INPUT_UP | INPUT_DOWN)))
        repeat_timer = REPEAT_DELAY;
    else if (--repeat_timer <= 0)
    {
        now |= directions;
        repeat_timer = REPEAT_RATE;
    }
    pressed = typing ? 0 : now;
}

static void move(int *selected, int count)
{
    if (count <= 0)
        return;
    if (pressed & INPUT_UP)
        *selected = (*selected + count - 1) % count;
    if (pressed & INPUT_DOWN)
        *selected = (*selected + 1) % count;
}

/* Imports */

static void start_import(char sources[][IMPORT_URL], int count, int flags, view_t back)
{
    import_start(sources, count, game, flags);
    importing = 1;
    incoming_batch = (flags & IMPORT_REMOVE_SOURCES) != 0;
    if (view != VIEW_SEND)
    {
        import_return = back;
        view = VIEW_IMPORT;
    }
    upload_set_busy(1);
}

/* Whether the incoming folder holds anything to import. */
static int incoming_waiting(void)
{
    DIR *dir = opendir(incoming);
    int found = 0;
    if (dir == NULL)
        return 0;
    for (struct dirent *entry; (entry = readdir(dir)) != NULL && !found;)
        found = entry->d_name[0] != '.';
    closedir(dir);
    return found;
}

static void start_incoming_import(view_t back)
{
    char source[1][IMPORT_URL];
    snprintf(source[0], IMPORT_URL, "%s", incoming);
    start_import(source, 1, IMPORT_REMOVE_SOURCES, back);
}

/* Takes the importer's notes, and its result once it has finished. */
static void poll_import(void)
{
    import_status_t status;
    char text[NOTE_TEXT];
    int ok;

    if (!importing)
        return;
    while (import_take_note(text, sizeof(text), &ok))
        add_note(text, ok);
    import_status(&status);
    if (status.state == IMPORT_RUNNING)
        return;
    importing = 0;
    upload_set_busy(0);
    if (status.state == IMPORT_LISTING)
    {
        browse = *import_listing();
        import_finish();
        browse_row = 0;
        view = VIEW_BROWSE;
        return;
    }
    import_finish();
    while (import_take_note(text, sizeof(text), &ok))
        add_note(text, ok);
    slots = ra_scan(game);
    if (status.state == IMPORT_CANCELLED)
        note(0, "Stopped");
    else if (status.placed != 0 && ra_playable(slots))
        note(1, "Red Alert is ready to play");
    else if (status.placed != 0)
        note(0, "Still missing: %s", (slots & 1u << RA_SLOT_REDALERT) ? "a disc's MAIN.MIX" : "redalert.mix");
    if (view == VIEW_IMPORT)
    {
        view = status.placed != 0 ? VIEW_MENU : import_return;
        row = 0;
    }
}

/* The menu */

static const char *const item_names[ITEM_COUNT] = {
    "Play", "Download the free Red Alert", "Send files from a PC or phone", "Download from a link",
    "Import from a USB drive", "Quit",
};

static void draw_status_panel(int x, int y)
{
    static const struct
    {
        const char *label;
        unsigned any; /* present when any of these slots is */
    } parts[] = {
        { "Game files (redalert.mix)", 1u << RA_SLOT_REDALERT },
        { "Allied campaign", 1u << RA_SLOT_ALLIED | 1u << RA_SLOT_MAIN },
        { "Soviet campaign", 1u << RA_SLOT_SOVIET | 1u << RA_SLOT_MAIN },
        { "Counterstrike missions", 1u << RA_SLOT_EXPAND },
        { "Counterstrike music", 1u << RA_SLOT_COUNTERSTRIKE },
        { "Aftermath missions", 1u << RA_SLOT_EXPAND2 },
        { "Aftermath music and sounds", 1u << RA_SLOT_AFTERMATH },
    };
    fill(x - 30, y - 30, SCREEN_WIDTH - MARGIN - x + 30, 420, panel);
    draw_text(&body, x, y, "Installed", white);
    y += body.height + 20;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
    {
        const int present = (slots & parts[i].any) != 0;
        fill(x, y + 10, 18, 18, present ? good : faint);
        draw_text(&body, x + 36, y, parts[i].label, present ? white : muted);
        y += body.height + 12;
    }
}

static void menu_view(void)
{
    const int playable = ra_playable(slots) && !importing;

    move(&row, ITEM_COUNT);
    if (pressed & INPUT_CROSS)
    {
        switch ((item_t)row)
        {
        case ITEM_PLAY:
            if (playable)
                chosen = 1;
            break;
        case ITEM_FREEWARE:
            view = VIEW_FREEWARE;
            row = 0;
            return;
        case ITEM_SEND:
            send_ready = upload_start(incoming) == 0;
            if (send_ready)
            {
                upload_progress_t progress;
                upload_progress(&progress);
                batches_seen = progress.batches;
            }
            batch_waiting = 0;
            view = VIEW_SEND;
            return;
        case ITEM_LINK:
            view = VIEW_LINK;
            row = link_text[0] != '\0' ? 1 : 0;
            return;
        case ITEM_USB:
            files_path[0] = '\0';
            files_row = 0;
            files_dirty = 1;
            view = VIEW_FILES;
            return;
        default:
            quit = 1;
            return;
        }
    }
    if (pressed & INPUT_CIRCLE)
        quit = 1;

    draw_title("PS5 Native RA");
    draw_text(&body, MARGIN, 160, "Red Alert, played with Vanilla Conquer", muted);
    int y = 280;
    for (int i = 0; i < ITEM_COUNT; i++)
        y = draw_row(y, item_names[i], i == row, i != ITEM_PLAY || playable);
    draw_status_panel(1100, 300);
    if (!ra_playable(slots) && note_count == 0)
        draw_wrapped(&small_font, MARGIN, y + 30, SCREEN_WIDTH - 2 * MARGIN,
                     "Red Alert's game files are not here yet. Download the free release, or add your own copy "
                     "(discs, The Ultimate Collection, the Remastered Collection or The First Decade).",
                     muted);
    else
        draw_notes(y + 30, FOOTER_Y - 20);
    draw_footer("Cross: select      Circle: quit");
}

/* EA's free release */

static void freeware_view(void)
{
    move(&row, 2);
    if ((pressed & INPUT_CROSS) && row == 0)
    {
        char sources[2][IMPORT_URL];
        snprintf(sources[0], IMPORT_URL, "freeware:allied");
        snprintf(sources[1], IMPORT_URL, "freeware:soviet");
        start_import(sources, 2, 0, VIEW_MENU);
        return;
    }
    if (((pressed & INPUT_CROSS) && row == 1) || (pressed & INPUT_CIRCLE))
    {
        view = VIEW_MENU;
        row = ITEM_FREEWARE;
        return;
    }

    draw_title("Download the free Red Alert");
    int y = 200;
    y += draw_wrapped(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN,
                      "In 2008 Electronic Arts made Red Alert free to download, as the Allied and Soviet discs "
                      "(RedAlert1_AlliedDisc.rar and RedAlert1_SovietDisc.rar, about 500 MB each). This downloads "
                      "them from the sources in the project's freeware.txt, by default the Internet Archive's copy "
                      "of EA's own download, and keeps their game files, about 1 GB.",
                      muted);
    y += 20;
    y += draw_wrapped(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN,
                      "The Counterstrike and Aftermath expansions were not part of the free release: add them from "
                      "your own copy with the other options.",
                      muted);
    y += 40;
    y = draw_row(y, "Download both discs", row == 0, 1);
    draw_row(y, "Back", row == 1, 1);
    draw_footer("Cross: select      Circle: back");
}

/* Sending from a PC or phone */

static void leave_send(void)
{
    upload_stop();
    view = VIEW_MENU;
    row = ITEM_SEND;
}

static void send_view(void)
{
    upload_progress_t progress;
    import_status_t status;
    const char *address = upload_address();
    char line[UPLOAD_PATH + 64];

    upload_progress(&progress);
    /* Each finished send is imported; one that ends while the last is still being imported waits. */
    if (progress.batches != batches_seen)
    {
        batches_seen = progress.batches;
        batch_waiting = 1;
    }
    if (batch_waiting && !importing && !progress.receiving)
    {
        batch_waiting = 0;
        if (incoming_waiting())
            start_incoming_import(VIEW_SEND);
    }
    if (pressed & INPUT_CIRCLE)
    {
        if (importing)
        {
            import_cancel();
            note(0, "Stopped; what was sent is added the next time PS5 Native RA starts");
        }
        leave_send();
        return;
    }

    draw_title("Send files from a PC or phone");
    int y = 200, text_width_limit = 1000;
    if (!send_ready || address[0] == '\0')
        y += draw_wrapped(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN,
                          !send_ready ? "The console could not start receiving files. Go back and try again."
                                      : "The console has no network address. Connect it to your network, then "
                                        "open this screen again.",
                          bad);
    else
    {
        y += draw_wrapped(&body, MARGIN, y, text_width_limit,
                          "On a PC or phone on the same network, open this address or scan the code:", muted);
        y += 16;
        draw_text(&title, MARGIN, y, address, accent);
        y += title.height + 24;
        y += draw_wrapped(&body, MARGIN, y, text_width_limit,
                          "Then drop your disc images, archives (ZIP, 7Z, RAR) or the game folder of your copy "
                          "on the page. The console adds the files once they have arrived.",
                          muted);
        draw_qr(address, 1300, 180);
    }
    y = y < 560 ? 560 : y + 20;
    if (progress.receiving)
    {
        snprintf(line, sizeof(line), "Receiving %s", progress.name);
        draw_text_fit(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN, line, white);
        draw_bar(MARGIN, y + body.height + 12, SCREEN_WIDTH - 2 * MARGIN, progress.done, progress.total);
        y += body.height + 80;
    }
    else if (importing)
    {
        import_status(&status);
        snprintf(line, sizeof(line), "Adding %s%s%s", status.source, status.current[0] != '\0' ? ": " : "",
                 status.current);
        draw_text_fit(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN, line, white);
        draw_bar(MARGIN, y + body.height + 12, SCREEN_WIDTH - 2 * MARGIN, status.done, status.total);
        y += body.height + 80;
    }
    else if (progress.files > 0)
    {
        snprintf(line, sizeof(line), "%d files received, %.1f MB", progress.files, progress.bytes / MB);
        draw_text(&body, MARGIN, y, line, white);
        y += body.height + 20;
    }
    draw_notes(y, FOOTER_Y - 20);
    draw_footer(importing ? "Circle: stop and go back" : "Circle: back");
}

/* A link */

static void link_view(void)
{
    if (!typing)
    {
        move(&row, 3);
        if ((pressed & INPUT_CROSS) && row == 0)
            start_typing();
        else if ((pressed & INPUT_CROSS) && row == 1 && link_text[0] != '\0')
        {
            char source[1][IMPORT_URL];
            snprintf(source[0], IMPORT_URL, "%s", link_text);
            start_import(source, 1, 0, VIEW_LINK);
            return;
        }
        else if (((pressed & INPUT_CROSS) && row == 2) || (pressed & INPUT_CIRCLE))
        {
            view = VIEW_MENU;
            row = ITEM_LINK;
            return;
        }
    }

    char line[IMPORT_URL + 16];
    draw_title("Download from a link");
    int y = 200;
    y += draw_wrapped(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN,
                      "A link to a disc image, an archive or one of Red Alert's files, or to a folder a PC shares "
                      "over HTTP (its list of files). Only download what you own or what is free to share.",
                      muted);
    y += 40;
    snprintf(line, sizeof(line), "Link: %s", link_text[0] != '\0' ? link_text : "none yet");
    y = draw_row(y, line, row == 0, 1);
    y = draw_row(y, "Download", row == 1, link_text[0] != '\0');
    y = draw_row(y, "Back", row == 2, 1);
    draw_notes(y + 30, FOOTER_Y - 20);
    draw_footer(typing ? "Type the link on the keyboard" : row == 0 ? "Cross: edit the link      Circle: back"
                                                                   : "Cross: select      Circle: back");
}

/* A folder listing a link led to */

static int browse_files(void)
{
    int count = 0;
    for (int i = 0; i < browse.count; i++)
        count += !browse.links[i].directory;
    return count;
}

static void browse_view(void)
{
    static char sources[MAX_LINKS][IMPORT_URL];
    const int file_count = browse_files(), offset = file_count > 1 ? 1 : 0, count = browse.count + offset;

    move(&browse_row, count);
    if ((pressed & INPUT_CROSS) && count > 0)
    {
        int n = 0;
        if (offset && browse_row == 0)
        {
            for (int i = 0; i < browse.count; i++)
                if (!browse.links[i].directory)
                    snprintf(sources[n++], IMPORT_URL, "%s", browse.links[i].url);
        }
        else
            snprintf(sources[n++], IMPORT_URL, "%s", browse.links[browse_row - offset].url);
        start_import(sources, n, 0, VIEW_BROWSE);
        return;
    }
    if (pressed & INPUT_CIRCLE)
    {
        view = VIEW_LINK;
        row = 1;
        return;
    }

    draw_text_fit(&body, MARGIN, 100, SCREEN_WIDTH - 2 * MARGIN, browse.base, accent);
    if (count == 0)
        draw_text(&body, MARGIN, 200, "No disc images, archives or Red Alert files here", bad);
    int first = browse_row >= LIST_ROWS ? browse_row - LIST_ROWS + 1 : 0, y = 180;
    for (int i = first; i < count && i < first + LIST_ROWS; i++)
    {
        char line[LINK_NAME + 32];
        if (offset && i == 0)
            snprintf(line, sizeof(line), "Import all %d files", file_count);
        else
            snprintf(line, sizeof(line), "%s%s", browse.links[i - offset].name,
                     browse.links[i - offset].directory ? "/" : "");
        y = draw_row(y, line, i == browse_row, 1);
    }
    draw_footer("Cross: open      Circle: back");
}

/* A USB drive */

static int compare_entries(const void *a, const void *b)
{
    const file_entry_t *x = a, *y = b;
    if (x->folder != y->folder)
        return y->folder - x->folder;
    return strcasecmp(x->name, y->name);
}

/* Lists files_path: the USB drives when it is "", else its folders and the files worth importing. */
static void list_files(void)
{
    files_count = 0;
    if (files_path[0] == '\0')
    {
        for (int i = 0; i < USB_DRIVES; i++)
        {
            char path[32];
            struct stat info;
            snprintf(path, sizeof(path), "/mnt/usb%d", i);
            DIR *dir = opendir(path);
            if (dir == NULL || stat(path, &info) != 0)
            {
                if (dir != NULL)
                    closedir(dir);
                continue;
            }
            closedir(dir);
            snprintf(files[files_count].name, sizeof(files[0].name), "%s", path);
            files[files_count++].folder = 1;
        }
        return;
    }
    DIR *dir = opendir(files_path);
    if (dir == NULL)
        return;
    for (struct dirent *entry; (entry = readdir(dir)) != NULL && files_count < MAX_FILES;)
    {
        char child[PATH_SIZE];
        struct stat info;
        if (entry->d_name[0] == '.')
            continue;
        snprintf(child, sizeof(child), "%s/%s", files_path, entry->d_name);
        if (stat(child, &info) != 0)
            continue;
        if (!S_ISDIR(info.st_mode) && !upload_wanted(entry->d_name))
            continue;
        snprintf(files[files_count].name, sizeof(files[0].name), "%s", entry->d_name);
        files[files_count++].folder = S_ISDIR(info.st_mode);
    }
    closedir(dir);
    qsort(files, (size_t)files_count, sizeof(files[0]), compare_entries);
}

static void files_view(void)
{
    static char listed_path[PATH_SIZE];

    if (files_dirty || strcmp(listed_path, files_path) != 0)
    {
        list_files();
        files_dirty = 0;
        snprintf(listed_path, sizeof(listed_path), "%s", files_path);
    }
    const int root = files_path[0] == '\0', offset = root ? 0 : 1, count = files_count + offset;
    move(&files_row, count);
    if ((pressed & INPUT_CROSS) && count > 0)
    {
        char source[1][IMPORT_URL];
        if (!root && files_row == 0)
        {
            snprintf(source[0], IMPORT_URL, "%s", files_path);
            start_import(source, 1, 0, VIEW_FILES);
            return;
        }
        file_entry_t *entry = &files[files_row - offset];
        char path[PATH_SIZE];
        if (root)
            snprintf(path, sizeof(path), "%s", entry->name);
        else
            snprintf(path, sizeof(path), "%s/%s", files_path, entry->name);
        if (entry->folder)
        {
            snprintf(files_path, sizeof(files_path), "%s", path);
            files_row = 0;
        }
        else
        {
            snprintf(source[0], IMPORT_URL, "%s", path);
            start_import(source, 1, 0, VIEW_FILES);
        }
        return;
    }
    if (pressed & INPUT_CIRCLE)
    {
        char *slash = strrchr(files_path, '/');
        if (root)
        {
            view = VIEW_MENU;
            row = ITEM_USB;
        }
        else if (slash != NULL && slash - files_path >= 9)
            *slash = '\0';
        else
            files_path[0] = '\0';
        files_row = 0;
        return;
    }

    draw_title(root ? "Import from a USB drive" : "");
    if (!root)
        draw_text_fit(&body, MARGIN, 100, SCREEN_WIDTH - 2 * MARGIN, files_path, accent);
    if (root && files_count == 0)
        draw_wrapped(&body, MARGIN, 200, SCREEN_WIDTH - 2 * MARGIN,
                     "No USB drive is to be seen. Connect one (exFAT or FAT32) with your disc images, archives or "
                     "game folder on it. A title sees USB drives with ShadowMountPlus 1.7 or later; with another "
                     "loader, copy the files over FTP into the title's import folder instead.",
                     muted);
    int first = files_row >= LIST_ROWS ? files_row - LIST_ROWS + 1 : 0, y = 180;
    for (int i = first; i < count && i < first + LIST_ROWS; i++)
    {
        char line[300];
        if (!root && i == 0)
            snprintf(line, sizeof(line), "Import everything in this folder");
        else
            snprintf(line, sizeof(line), "%s%s", files[i - offset].name, files[i - offset].folder ? "/" : "");
        y = draw_row(y, line, i == files_row, 1);
    }
    draw_footer("Cross: open      Circle: back");
}

/* An import's progress */

static void import_view(void)
{
    import_status_t status;
    char line[256];

    import_status(&status);
    if (pressed & INPUT_CIRCLE)
        import_cancel();

    draw_title(incoming_batch ? "Adding the files sent" : "Adding Red Alert's files");
    int y = 200;
    draw_text_fit(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN, status.source, white);
    y += body.height + 10;
    if (status.source_count > 1)
    {
        snprintf(line, sizeof(line), "Source %d of %d", status.source_index + 1, status.source_count);
        draw_text(&small_font, MARGIN, y, line, muted);
    }
    y += small_font.height + 20;
    draw_bar(MARGIN, y, SCREEN_WIDTH - 2 * MARGIN, status.done, status.total);
    y += 90;
    if (status.current[0] != '\0')
    {
        snprintf(line, sizeof(line), "Writing %s", status.current);
        draw_text_fit(&body, MARGIN, y, SCREEN_WIDTH - 2 * MARGIN, line, white);
    }
    y += body.height + 30;
    draw_notes(y, FOOTER_Y - 20);
    draw_footer("Circle: stop");
}

#ifndef __PROSPERO__
/* On a PC: RA_LAUNCHER_KEYS="down,cross,..." presses one key every few frames, and
 * RA_LAUNCHER_SHOTS=<folder> saves each frame a key is pressed in, for looking at the views. */
static void script_input(int frame)
{
    static const char *next;
    static int started;
    const char *keys = getenv("RA_LAUNCHER_KEYS");

    if (keys == NULL)
        return;
    if (!started)
    {
        next = keys;
        started = 1;
    }
    if (frame % 20 != 19)
        return;
    if (*next == '\0')
    {
        quit = 1;
        return;
    }
    size_t length = strcspn(next, ",");
    const struct
    {
        const char *name;
        unsigned input;
    } names[] = { { "up", INPUT_UP }, { "down", INPUT_DOWN }, { "cross", INPUT_CROSS }, { "circle", INPUT_CIRCLE } };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strlen(names[i].name) == length && strncmp(next, names[i].name, length) == 0)
            pressed |= names[i].input;
    if (strncmp(next, "wait", 4) == 0 && length == 4)
        pressed = 0;
    if (strncmp(next, "link=", 5) == 0)
        snprintf(link_text, sizeof(link_text), "%.*s", (int)(length - 5), next + 5);
    next += length;
    if (*next == ',')
        next++;
}

static void save_shot(int frame)
{
    const char *folder = getenv("RA_LAUNCHER_SHOTS");
    static int count;
    if (folder == NULL || frame % 20 != 18)
        return;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL)
        return;
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0)
    {
        char path[PATH_SIZE];
        snprintf(path, sizeof(path), "%s/shot-%03d.bmp", folder, count++);
        SDL_SaveBMP(surface, path);
    }
    SDL_FreeSurface(surface);
}
#endif

int launcher_run(const char *game_folder, const char *incoming_folder, const char *settings_folder)
{
    game = game_folder;
    incoming = incoming_folder;
    settings = settings_folder;
    load_settings();
    mkdir(incoming, 0755);
    slots = ra_scan(game);

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("launcher: no video: %s\n", SDL_GetError());
        return ra_playable(slots);
    }
#ifdef __PROSPERO__
    const Uint32 flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
    const Uint32 flags = 0;
#endif
    window = SDL_CreateWindow("PS5 Native RA", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
                              SCREEN_HEIGHT, flags);
    renderer = window != NULL ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (renderer == NULL)
    {
        printf("launcher: no renderer: %s\n", SDL_GetError());
        if (window != NULL)
            SDL_DestroyWindow(window);
        return ra_playable(slots);
    }
    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
    /* No pointer here, though going full screen put it at 0, 0, where it would show; the game gets
     * it back. */
    SDL_ShowCursor(SDL_DISABLE);
    small_font = load_font(font_small_pixels, font_small_width, font_small_height, font_small_glyphs);
    body = load_font(font_body_pixels, font_body_width, font_body_height, font_body_glyphs);
    title = load_font(font_title_pixels, font_title_width, font_title_height, font_title_glyphs);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            open_controller(i);

    view = VIEW_MENU;
    row = ra_playable(slots) ? ITEM_PLAY : ITEM_FREEWARE;
    /* What was copied into the incoming folder while the title was closed. */
    if (incoming_waiting())
        start_incoming_import(VIEW_MENU);

    for (int frame = 0; !quit && !chosen; frame++)
    {
        read_input();
#ifndef __PROSPERO__
        script_input(frame);
#endif
        poll_import();
        SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, 0xff);
        SDL_RenderClear(renderer);
        switch (view)
        {
        case VIEW_MENU:
            menu_view();
            break;
        case VIEW_FREEWARE:
            freeware_view();
            break;
        case VIEW_SEND:
            send_view();
            break;
        case VIEW_LINK:
            link_view();
            break;
        case VIEW_BROWSE:
            browse_view();
            break;
        case VIEW_FILES:
            files_view();
            break;
        case VIEW_IMPORT:
            import_view();
            break;
        }
#ifndef __PROSPERO__
        save_shot(frame);
#endif
        SDL_RenderPresent(renderer);
    }

    if (importing)
    {
        import_cancel();
        import_finish();
    }
    upload_stop();
    if (typing)
        SDL_StopTextInput();
    for (int i = 0; i < 4; i++)
        if (controllers[i] != NULL)
            SDL_GameControllerClose(controllers[i]);
    SDL_DestroyTexture(small_font.texture);
    SDL_DestroyTexture(body.texture);
    SDL_DestroyTexture(title.texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_ShowCursor(SDL_ENABLE);
    return chosen;
}
