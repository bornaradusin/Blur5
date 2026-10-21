// Prospero BlackBox - The user's clean copy of a game: found, checked, imported.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/base_game.hpp"
#include "core/i18n.hpp"
#include "library/tree.hpp"
#include "third_party/miniz/miniz.h"
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <tuple>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <cctype>
#include <unistd.h>

namespace store::library
{
namespace
{
// ---- the games ----------------------------------------------------------------

// Blur (2010) for PC: one release, 1.2. The exe is never checked (any exe is
// accepted); cache/root.pak tells a known copy, and an unknown one is installed with a warning.
constexpr const char *kBlurFolders[] = {"cache", "levels", "UI", "UI/movies"};
constexpr const char *kBlurFiles[] = {"Blur.exe", "binkw32.dll", "cache/root.pak",
                                      "cache/gamedata.pak", "cache/boot.pak",
                                      "cache/gamedata.sig", "cache/gamedata_boot.sig"};
constexpr Release kBlurReleases[] = {
    {"1.2", 559652864, 0x1deb5c87, false},
};
constexpr const char *kBlurModFiles[] = {"scripts", "amax", "dinput8.dll", "d3d9.dll", "dsound.dll",
                                         "lua5.1.dll", "discord-rpc.dll"};
constexpr Manifest kBlur = []
{
    Manifest m{"BLUR", kBlurFolders, kBlurFiles, "cache/root.pak", kBlurReleases, kBlurModFiles};
    m.mods_left_out = true;
    m.accept_unknown = true;
    return m;
}();

constexpr const Manifest *kManifests[] = {&kBlur};

// ---- files ----------------------------------------------------------------------

bool is_directory(const std::string &path)
{
    struct stat info{};
    return lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool is_file(const std::string &path, std::uint64_t *size = nullptr)
{
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return false;
    if (size)
        *size = static_cast<std::uint64_t>(info.st_size);
    return true;
}

// A folder with nothing in it (Blur5 makes an empty base folder to show where the game goes).
bool is_empty_directory(const std::string &path)
{
    DIR *folder = opendir(path.c_str());
    if (!folder)
        return false;
    bool empty = true;
    while (const dirent *entry = readdir(folder))
    {
        const std::string_view name = entry->d_name;
        if (name != "." && name != "..")
        {
            empty = false;
            break;
        }
    }
    closedir(folder);
    return empty;
}

// The one folder inside a folder that holds nothing else (names starting with a dot aside),
// or "".
std::string only_folder(const std::string &path)
{
    DIR *folder = opendir(path.c_str());
    if (!folder)
        return "";
    std::string found;
    int count = 0;
    while (const dirent *entry = readdir(folder))
    {
        const std::string name = entry->d_name;
        if (name.empty() || name.front() == '.')
            continue;
        ++count;
        found = path + "/" + name;
    }
    closedir(folder);
    return count == 1 && is_directory(found) ? found : "";
}

// The folder that holds Blur5's own folder (data_root): /data/Blur5 for /data/Blur5/app.
std::string blur5_root(const std::string &data_root)
{
    const auto slash = data_root.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? std::string("/") : data_root.substr(0, slash);
}

// One name in a folder, matched without regard to case ("" when none).
std::string find_name(const std::string &folder, const std::string &name)
{
    DIR *directory = opendir(folder.c_str());
    if (!directory)
        return {};
    std::string found;
    while (const dirent *entry = readdir(directory))
        if (strcasecmp(entry->d_name, name.c_str()) == 0)
        {
            found = entry->d_name;
            if (found == name)
                break; // an exact match wins over one that differs only in case
        }
    closedir(directory);
    return found;
}

bool crc_of(const std::string &path, std::uint32_t &crc)
{
    const int file = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (file < 0)
        return false;
    std::vector<unsigned char> buffer(1u << 20);
    mz_ulong value = mz_crc32(0, nullptr, 0);
    ssize_t count = 0;
    while ((count = read(file, buffer.data(), buffer.size())) > 0)
        value = mz_crc32(value, buffer.data(), static_cast<size_t>(count));
    close(file);
    crc = static_cast<std::uint32_t>(value);
    return count == 0;
}

// The CRC of the version file is read once per (path, size, mtime): the service checks the
// base games every pass, and the file is half a gigabyte. inspect runs on the service thread
// and on install jobs, so the cache is guarded.
struct CrcKey
{
    std::string path;
    std::uint64_t size;
    std::int64_t mtime_sec;
    std::int64_t mtime_nsec;
    bool operator==(const CrcKey &o) const
    {
        return std::tie(path, size, mtime_sec, mtime_nsec) == std::tie(o.path, o.size, o.mtime_sec, o.mtime_nsec);
    }
};
std::mutex crc_guard;
std::map<std::string, std::pair<CrcKey, std::uint32_t>> crc_cache;
std::atomic<unsigned> crc_reads{0};

bool cached_crc_of(const std::string &path, std::uint32_t &crc)
{
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return false;
    const CrcKey key{path, static_cast<std::uint64_t>(info.st_size),
                     static_cast<std::int64_t>(info.st_mtim.tv_sec),
                     static_cast<std::int64_t>(info.st_mtim.tv_nsec)};
    {
        std::lock_guard lock(crc_guard);
        if (const auto it = crc_cache.find(path); it != crc_cache.end() && it->second.first == key)
        {
            crc = it->second.second;
            return true;
        }
    }
    ++crc_reads;
    if (!crc_of(path, crc))
        return false;
    std::lock_guard lock(crc_guard);
    crc_cache[path] = {key, crc};
    return true;
}

std::string join(const std::vector<std::string> &items)
{
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i)
        text += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
    return text;
}

} // namespace

const Manifest *manifest_for(const std::string &folder)
{
    for (const auto *manifest : kManifests)
        if (folder == manifest->folder)
            return manifest;
    return nullptr;
}

unsigned crc_reads_for_test()
{
    return crc_reads;
}

std::string find_entry(const std::string &folder, const std::string &relative)
{
    std::string path = folder;
    std::size_t at = 0;
    while (at <= relative.size())
    {
        const auto slash = std::min(relative.find('/', at), relative.size());
        const auto name = find_name(path, relative.substr(at, slash - at));
        if (name.empty())
            return {};
        path += "/" + name;
        at = slash + 1;
    }
    return path;
}

BaseGame inspect(const std::string &folder, const Manifest &manifest)
{
    BaseGame result;
    result.path = folder;
    if (!is_directory(folder))
        return result;
    std::vector<std::string> missing;
    for (const auto *name : manifest.folders)
        if (const auto path = find_entry(folder, name); path.empty() || !is_directory(path))
            missing.emplace_back(name);
    for (const auto *name : manifest.files)
        if (const auto path = find_entry(folder, name); path.empty() || !is_file(path))
            missing.emplace_back(name);
    if (!missing.empty())
    {
        result.state = State::incomplete;
        result.detail = i18n::trf("Missing {0}. Copy the whole game folder.", {join(missing)});
        return result;
    }
    std::vector<std::string> mods;
    for (const auto *name : manifest.mod_files)
        if (!find_entry(folder, name).empty())
            mods.emplace_back(name);
    std::string left; // what the page adds about mods that are left out
    if (!mods.empty() && manifest.mods_left_out)
        left = " " + i18n::trf("Mods found ({0}) are left out: the install adds its own.", {join(mods)});
    else if (!mods.empty())
    {
        result.state = State::modified;
        result.detail = i18n::trf("Mods are installed in it ({0}). Copy a clean install: the mods are "
                                  "added for you.",
                                  {join(mods)});
        return result;
    }
    const auto version_path = find_entry(folder, manifest.version_file);
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
    if (!is_file(version_path, &size) || !cached_crc_of(version_path, crc))
    {
        result.state = State::incomplete;
        result.detail = i18n::trf("Can't read {0}.", {manifest.version_file});
        return result;
    }
    for (const auto &release : manifest.releases)
        if (release.size == size && release.crc == crc)
        {
            result.state = release.needs_patch ? State::needs_patch : State::ready;
            result.version = release.version;
            result.detail = release.needs_patch
                                ? i18n::trf("Version {0}: upgraded to {1} as it installs.",
                                            {result.version, manifest.releases.front().version})
                                : i18n::trf("Version {0}.", {result.version});
            result.detail += left;
            return result;
        }
    if (manifest.accept_unknown)
    {
        result.state = State::ready;
        result.version = manifest.releases.front().version;
        result.warning =
            i18n::trf("{0} is not the one we know: installing anyway.", {manifest.version_file});
        result.detail = i18n::trf("Version {0} (unchecked).", {result.version}) + left;
        return result;
    }
    result.state = State::unknown;
    result.detail = i18n::trf("{0} matches no release we know: the copy is modified, or it is a "
                              "release we haven't seen. Copy a clean install.",
                              {manifest.version_file});
    return result;
}

std::string base_folder(const std::string &data_root)
{
    return blur5_root(data_root) + "/base";
}

std::string save_folder(const std::string &data_root)
{
    return blur5_root(data_root) + "/save";
}

BaseGame locate(const std::string &data_root, std::span<const std::string> roots,
                const Manifest &manifest)
{
    const auto base = base_folder(data_root);
    // base, Base or BASE: whatever case the folder was made in. An empty one is not a copy yet.
    if (const auto existing = find_entry(blur5_root(data_root), "base");
        !existing.empty() && is_directory(existing) && !is_empty_directory(existing))
    {
        auto found = inspect(existing, manifest);
        // The game folder itself copied into base (base/Blur/Blur.exe): that folder is the copy.
        if (found.state == State::incomplete)
            if (const auto inner = only_folder(existing); !inner.empty())
                if (auto nested = inspect(inner, manifest); nested.state != State::incomplete)
                    return nested;
        return found;
    }
    for (const auto &root : roots)
        if (const auto path = find_entry(root, manifest.folder); !path.empty() && is_directory(path))
        {
            auto found = inspect(path, manifest);
            found.on_usb = true;
            return found;
        }
    BaseGame absent;
    absent.path = base;
    // The folder's name as people write it (BLUR: Blur); any case is found.
    std::string shown = manifest.folder;
    for (std::size_t i = 1; i < shown.size(); i++)
        shown[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(shown[i])));
    absent.detail = i18n::trf("Copy the files of a clean Blur install into {0} over FTP, or put "
                              "them on a USB drive in a folder named {1}.",
                              {base, shown});
    return absent;
}

std::vector<std::string> usb_roots()
{
    std::vector<std::string> roots;
    for (int i = 0; i < 8; ++i)
        if (const auto path = "/mnt/usb" + std::to_string(i); is_directory(path))
            roots.push_back(path);
    for (int i = 0; i < 2; ++i)
        if (const auto path = "/mnt/ext" + std::to_string(i); is_directory(path))
            roots.push_back(path);
    return roots;
}

bool import_base(const std::string &from, const std::string &data_root, const Manifest &manifest,
                 const std::function<void(const ImportProgress &)> &progress,
                 const std::atomic<bool> &cancel, std::string &error)
{
    const auto games = blur5_root(data_root);
    const auto target = base_folder(data_root), work = target + ".importing";
    if (const auto existing = find_entry(games, "base"); !existing.empty() && is_directory(existing))
    {
        // The empty base folder Blur5 makes gives way to the copy.
        if (!is_empty_directory(existing) || rmdir(existing.c_str()) != 0)
        {
            error = existing + " already has files in it";
            return false;
        }
    }
    if (mkdir(games.c_str(), 0777) != 0 && errno != EEXIST)
    {
        error = "Can't make " + games + ": " + std::strerror(errno);
        return false;
    }
    // A copy left by an earlier attempt is started again.
    if (!copy_tree(from, work, progress, cancel, error))
        return false;
    // Checked as a whole before it takes the place of the base copy.
    const auto copy = inspect(work, manifest);
    if (copy.state != State::ready && copy.state != State::needs_patch)
        error = "The copy didn't check out: " + copy.detail;
    else if (rename(work.c_str(), target.c_str()) != 0)
        error = "Can't move the copy into place: " + std::string(std::strerror(errno));
    else
        return true;
    remove_all(work);
    return false;
}
} // namespace store::library
