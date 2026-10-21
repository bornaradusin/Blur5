// Prospero BlackBox - The built-in catalog: Blur.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog/blackbox.hpp"

namespace store::catalog
{
namespace
{
constexpr Game kGames[] = {
    {"PPSA11016", "BLUR", "Blur", "2010-05-25", "blur5",
     "Arcade racing with power-ups, from your own copy of Blur. Installed with online play "
     "through Amax and a 4K picture.",
     true, {0x0a0a20, 0x2a1f5c, 0xc86bff, 0x1c0630}}, // night blue, violet: the Blur background
};
} // namespace

std::span<const Game> blackbox_games()
{
    return kGames;
}

const Palette &blackbox_palette(std::string_view id)
{
    for (const auto &game : kGames)
        if (id == game.id)
            return game.palette;
    return kManagerPalette;
}

Snapshot blackbox_snapshot(const std::string &app_folder)
{
    Snapshot snapshot;
    for (const auto &game : kGames)
    {
        Entry entry;
        entry.id = game.id;
        entry.name = game.name;
        entry.author = "Blur5";
        entry.kind = "game";
        entry.status = game.available ? "available" : "coming_soon";
        entry.released = game.released;
        entry.description = game.description;
        entry.icon = app_folder + "/assets/games/" + game.art + ".png";
        snapshot.entries.push_back(std::move(entry));
    }
    // Built into the app: nothing to verify or download.
    snapshot.verified = true;
    snapshot.accepted = true;
    snapshot.online = false;
    return snapshot;
}
} // namespace store::catalog
