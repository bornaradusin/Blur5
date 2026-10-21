// Blur5 - The page's words: English as written, placeholders and capitals.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/early_exit.hpp"
#include "core/i18n.hpp"
#include <cassert>
#include <string>

int main()
{
    using namespace store::i18n;

    // The page is English only: every sentence comes back as it was written.
    assert(tr("Install") == "Install");
    assert(trs("Repair") == "Repair");
    assert(std::string(trc("Play")) == "Play");
    assert(tr(store::kEarlyExitHint) == store::kEarlyExitHint);

    // {0} and the arguments.
    assert(trf("Version {0}.", {"1.0"}) == "Version 1.0.");
    assert(trf("{0} of {1}", {"1 MB", "4 MB"}) == "1 MB of 4 MB");
    // A {0} the call has no argument for is dropped, not read out of bounds.
    assert(trf("{0} of {1}", {"1 MB"}) == "1 MB of ");
    assert(trf("No placeholder", {}) == "No placeholder");

    // Capitals for the small tracked labels: accented letters too, and the sharp s as two.
    assert(upper("ready to install") == "READY TO INSTALL");
    assert(upper("Français à ñ ü ç œ") == "FRANÇAIS À Ñ Ü Ç Œ");
    assert(upper("Straße") == "STRASSE");
    assert(upper("a÷b") == "A÷B"); // the division sign is not a letter
    return 0;
}
