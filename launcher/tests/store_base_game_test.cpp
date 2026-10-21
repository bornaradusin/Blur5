// Prospero BlackBox - Base games: found without regard to case, checked, imported whole.
// Blur5 keeps the copy in /data/Blur5/base, beside its own folder /data/Blur5/app.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/base_game.hpp"
#include "third_party/miniz/miniz.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace store::library;

namespace
{
void put(const fs::path &path, const std::string &body)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << body;
}

std::uint32_t crc(const std::string &body)
{
    return static_cast<std::uint32_t>(
        mz_crc32(mz_crc32(0, nullptr, 0), reinterpret_cast<const unsigned char *>(body.data()),
                 body.size()));
}
} // namespace

int main()
{
    char temporary[] = "/tmp/prospero-base-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path root = temporary;

    // Blur's own manifest: mods are left out, an unknown copy is a warning.
    const Manifest *blur = manifest_for("BLUR");
    assert(blur && blur->mods_left_out && blur->accept_unknown && manifest_for("OTHER") == nullptr);

    // A small game of the same shape, its releases told by data.bin.
    const std::string current(3000, 'c'), older(2000, 'o');
    const char *folders[] = {"DATA", "MOVIES"};
    const char *files[] = {"game.exe", "DATA/data.bin"};
    const Release releases[] = {{"2.0", current.size(), crc(current), false},
                                {"1.0", older.size(), crc(older), true}};
    const char *mod_files[] = {"scripts", "dinput8.dll"};
    const Manifest game{"GAME", folders, files, "DATA/data.bin", releases, mod_files};

    // Blur5's layout: its own folder (data, <root>/Blur5/app) beside the player's base folder.
    const fs::path blur5 = root / "Blur5", data = blur5 / "app", base = blur5 / "base";
    const fs::path usb = root / "usb0";
    fs::create_directories(data);
    assert(base_folder(data.string()) == base.string());
    assert(save_folder(data.string()) == (blur5 / "save").string());
    const std::vector<std::string> roots{usb.string()};

    // Nothing anywhere: absent, and the page says where to put it.
    auto found = locate(data.string(), roots, game);
    assert(found.state == State::absent && found.path == base.string() &&
           found.detail.find(base.string() + " over FTP") != std::string::npos &&
           found.detail.find("named Game.") != std::string::npos);
    // An empty base folder (Blur5 makes one at start) is not a copy yet.
    fs::create_directories(base);
    assert(locate(data.string(), roots, game).state == State::absent);

    // On USB, its names in any case: found there, the older release, to be patched.
    put(usb / "game" / "Game.EXE", "exe");
    put(usb / "game" / "data" / "DATA.BIN", older);
    fs::create_directories(usb / "game" / "Movies");
    put(usb / "game" / "Movies" / "intro.vp6", std::string(5000, 'm'));
    // What a Mac leaves on a USB drive is not copied.
    put(usb / "game" / "Movies" / "._intro.vp6", "resource fork");
    put(usb / "game" / ".DS_Store", "folder view");
    found = locate(data.string(), roots, game);
    assert(found.on_usb && found.state == State::needs_patch && found.version == "1.0");
    assert(find_entry((usb / "game").string(), "data/data.bin") ==
           (usb / "game" / "data" / "DATA.BIN").string());

    // Copied into the base folder whole (the empty one gives way), with progress up to every
    // byte; then found there.
    std::uint64_t last = 0, total = 0;
    std::atomic<bool> cancel{false};
    std::string error;
    assert(import_base(found.path, data.string(), game,
                       [&](const ImportProgress &p)
                       {
                           assert(p.done >= last);
                           last = p.done;
                           total = p.total;
                       },
                       cancel, error));
    assert(total == 3 + older.size() + 5000 && last == total);
    assert(!fs::exists(blur5 / "base.importing") && fs::exists(base / "Game.EXE"));
    assert(!fs::exists(base / ".DS_Store") && !fs::exists(base / "Movies" / "._intro.vp6"));
    found = locate(data.string(), roots, game);
    assert(!found.on_usb && found.state == State::needs_patch && found.path == base.string());
    // Never over an existing copy.
    assert(!import_base((usb / "game").string(), data.string(), game, [](auto &) {}, cancel, error));

    // The current release, missing pieces, mods, an unknown version.
    const fs::path copy = root / "copy";
    put(copy / "game.exe", "exe");
    put(copy / "DATA" / "data.bin", current);
    fs::create_directories(copy / "MOVIES");
    assert(inspect(copy.string(), game).state == State::ready);
    put(copy / "scripts" / "mod.asi", "asi");
    found = inspect(copy.string(), game);
    assert(found.state == State::modified && found.detail.find("scripts") != std::string::npos);
    fs::remove_all(copy / "scripts");
    put(copy / "DATA" / "data.bin", "something else");
    assert(inspect(copy.string(), game).state == State::unknown);
    fs::remove_all(copy / "MOVIES");
    found = inspect(copy.string(), game);
    assert(found.state == State::incomplete && found.detail.find("MOVIES") != std::string::npos);

    // The version file's CRC is read once while it is unchanged; a changed file is read again.
    {
        fs::create_directories(copy / "MOVIES");
        const auto before = crc_reads_for_test();
        put(copy / "DATA" / "data.bin", current);
        assert(inspect(copy.string(), game).state == State::ready);
        assert(crc_reads_for_test() == before + 1);
        assert(inspect(copy.string(), game).state == State::ready);
        assert(crc_reads_for_test() == before + 1);
        put(copy / "DATA" / "data.bin", older); // other size
        assert(inspect(copy.string(), game).state == State::needs_patch);
        assert(crc_reads_for_test() == before + 2);
        std::string same_size = older;
        same_size[0] = 'X'; // same size, later mtime
        struct timespec times[2] = {{1'000'000'000, 0}, {1'000'000'000, 0}};
        put(copy / "DATA" / "data.bin", same_size);
        utimensat(AT_FDCWD, (copy / "DATA" / "data.bin").c_str(), times, 0);
        assert(inspect(copy.string(), game).state == State::unknown);
        assert(crc_reads_for_test() == before + 3);
        put(copy / "DATA" / "data.bin", current);
    }

    // The game folder itself copied into base (base/Game/game.exe) is found inside it.
    {
        const fs::path nested = root / "nested";
        fs::create_directories(nested / "app");
        fs::create_directories(nested / "base");
        fs::rename(copy, nested / "base" / "Game");
        found = locate((nested / "app").string(), roots, game);
        assert(!found.on_usb && found.path == (nested / "base" / "Game").string());
        assert(found.state == State::ready || found.state == State::needs_patch);
        fs::rename(nested / "base" / "Game", copy);
        // A folder that isn't the game stays what it is: incomplete.
        put(nested / "base" / "Other" / "readme.txt", "x");
        assert(locate((nested / "app").string(), roots, game).state == State::incomplete);
    }

    // The base folder in any case (Base) is found, and counts as present for an import.
    {
        const fs::path other = root / "other";
        fs::create_directories(other / "app");
        fs::rename(copy, other / "Base");
        found = locate((other / "app").string(), roots, game);
        assert(!found.on_usb && found.path == (other / "Base").string());
        assert(found.state == State::ready || found.state == State::needs_patch);
        assert(!import_base((usb / "game").string(), (other / "app").string(), game, [](auto &) {},
                            cancel, error));
        assert(error.find("already has files in it") != std::string::npos);
        fs::rename(other / "Base", copy);
    }

    // A cancelled import leaves nothing behind; a link is refused.
    fs::remove_all(base);
    cancel = true;
    assert(!import_base((usb / "game").string(), data.string(), game, [](auto &) {}, cancel, error));
    assert(error == "Cancelled" && !fs::exists(base) && !fs::exists(blur5 / "base.importing"));
    cancel = false;
    fs::create_symlink("/etc/passwd", usb / "game" / "link");
    assert(!import_base((usb / "game").string(), data.string(), game, [](auto &) {}, cancel, error));
    assert(error.find("link") != std::string::npos);

    // A game whose mods are left out and whose unknown copies are accepted.
    {
        const char *lfolders[] = {"cache"};
        const char *lfiles[] = {"game.exe", "cache/root.pak"};
        const std::string known(100, 'k');
        const Release lreleases[] = {{"1.2", known.size(), crc(known), false}};
        const char *lmods[] = {"dsound.dll", "scripts"};
        Manifest lenient{"LEN", lfolders, lfiles, "cache/root.pak", lreleases, lmods};
        lenient.mods_left_out = true;
        lenient.accept_unknown = true;
        const fs::path copy = root / "len";
        put(copy / "game.exe", "x");
        put(copy / "cache" / "root.pak", std::string(7, 'u')); // unknown
        put(copy / "dsound.dll", "mod");
        const auto seen = inspect(copy.string(), lenient);
        assert(seen.state == State::ready && seen.version == "1.2");
        assert(seen.warning.find("root.pak") != std::string::npos);
        assert(seen.detail.find("left out") != std::string::npos);
    }

    fs::remove_all(root);
    std::cout << "Base game checks passed\n";
}
