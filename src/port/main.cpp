// Entry point of the PS5 title: sets the console up and runs Vanilla Conquer's Red Alert.
//   /app0/ra          the game data (redalert.mix, allied/main.mix, soviet/main.mix ...)
//   /download0/ra     settings (redalert.ini) and saves
#include "ps5runtime.h"

#include <SDL.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

int VanillaRA_Main(int argc, char* argv[]);

extern "C" int sceKernelSendNotificationRequest(int device, void* request, size_t size, int blocking);

namespace
{
    constexpr const char* kDataPath = "/app0/ra";
    constexpr const char* kUserPath = "/download0/ra";
    constexpr const char* kConfigPath = "/download0/ra/redalert.ini";

    void Notify(const char* text)
    {
        struct
        {
            uint8_t reserved[45];
            char message[3075];
        } request{};
        std::snprintf(request.message, sizeof(request.message), "%s", text);
        sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    }

    // Whether folder holds a file of that name, in any case (Vanilla Conquer finds them in any case).
    bool HasFile(const char* folder, const char* name)
    {
        DIR* dir = opendir(folder);
        if (dir == nullptr)
            return false;
        bool found = false;
        while (dirent* entry = readdir(dir))
        {
            if (strcasecmp(entry->d_name, name) == 0)
            {
                found = true;
                break;
            }
        }
        closedir(dir);
        return found;
    }

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

    if (!HasFile(kDataPath, "redalert.mix"))
    {
        std::printf("no game data: %s/redalert.mix is missing\n", kDataPath);
        Notify("PS5 Native RA: the Red Alert game files are missing. Copy them into the title's ra "
               "folder (see the README).");
        return 1;
    }

    char program[] = "/app0/eboot.bin";
    char* argv[] = {program, nullptr};
    int result = VanillaRA_Main(1, argv);
    std::printf("vanillara returned %d\n", result);
    return result;
}
