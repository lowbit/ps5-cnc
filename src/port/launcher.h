/* The launcher: the title's first screen. It shows which of Red Alert's files are there and gets
 * the missing ones (importer.h): EA's free discs downloaded, files sent from a PC or phone, a link,
 * or a USB drive. It draws with SDL and its own fonts (tools/make-font.py), since the game can draw
 * nothing before its files are there, and leaves SDL running without its window, so that Vanilla
 * Conquer opens its own. */
#ifndef PS5_LAUNCHER_H
#define PS5_LAUNCHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs until the player chooses to play (returns 1) or to quit (0). Files are imported into
 * game_folder; incoming_folder is where sent and copied files wait to be imported, and the
 * launcher keeps its settings in settings_folder. */
int launcher_run(const char *game_folder, const char *incoming_folder, const char *settings_folder);

#ifdef __cplusplus
}
#endif

#endif
