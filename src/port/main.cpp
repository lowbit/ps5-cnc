// Entry point of the PS5 title: sets the console up, shows the launcher (which gets the game files)
// and runs Vanilla Conquer's Red Alert.
//   /app0/ra          the game data (redalert.mix, allied/main.mix, soviet/main.mix ...)
//   /app0/import      files sent from a PC or copied over FTP, imported at the next start
//   /download0/ra     settings (redalert.ini, launcher.txt) and saves
#include "launcher.h"
#include "ps5runtime.h"

#include <SDL.h>
#include <cstdio>
#include <sys/stat.h>

int VanillaRA_Main(int argc, char* argv[]);

namespace
{
    constexpr const char* kDataPath = "/app0/ra";
    constexpr const char* kIncomingPath = "/app0/import";
    constexpr const char* kUserPath = "/download0/ra";
    constexpr const char* kConfigPath = "/download0/ra/redalert.ini";

    // On first run: the DualSense moves the game's own pointer (left stick, right stick scrolls), and
    // the picture keeps its shape in the middle of the TV. Everything else stays at Vanilla
    // Conquer's defaults and can be changed in the file.
    void WriteDefaultConfig()
    {
        struct stat info{};
        if (stat(kConfigPath, &info) == 0)
            return;
        mkdir(kUserPath, 0755);
        if (std::FILE* file = std::fopen(kConfigPath, "w"))
        {
            std::fputs(
                "[Mouse]\n"
                "ControllerEnabled=yes\n"
                "RawInput=no\n"
                "\n"
                "[Video]\n"
                "Boxing=yes\n"
                "HardwareCursor=no\n",
                file);
            std::fclose(file);
        }
    }
} // namespace

int main()
{
    // The title folder, where the PC can read the log even after a crash, unless it is read-only.
    const char* logPaths[] = {"/app0/vanillara.log", "/download0/vanillara.log"};
    ps5_log_open(logPaths, 2);
    WriteDefaultConfig();
    // The PS5 SDL has only the software renderer.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    if (!launcher_run(kDataPath, kIncomingPath, kUserPath))
    {
        std::printf("leaving from the launcher\n");
        return 0;
    }

    char program[] = "/app0/eboot.bin";
    char* argv[] = {program, nullptr};
    int result = VanillaRA_Main(1, argv);
    std::printf("vanillara returned %d\n", result);
    return result;
}
