// Blur5 - The words the page shows. Blur5's page is in English only.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

// Every sentence the page writes for a person goes through tr() and the functions below. The
// page is English only, so tr() gives back the English as it is; the functions stay so a
// sentence and its {0}, {1}... placeholders are written the same way everywhere. (The game
// itself still plays in the PS5's language when your copy has it: see
// library::apply_system_language.)
namespace store::i18n
{

// The sentence as it is.
std::string_view tr(std::string_view english);
// The same as a string, for what takes ownership of its text.
std::string trs(std::string_view english);
// Capitals for small tracked labels ("READY TO INSTALL"), accented letters and ß included.
std::string upper(std::string_view text);
// For an API that wants a C string. Every English text is a literal, so the result is one too:
// it ends where the text does and lives as long as the program.
const char *trc(const char *english);
// The sentence with {0}, {1}... replaced by the arguments.
std::string trf(std::string_view english, std::initializer_list<std::string_view> arguments);

} // namespace store::i18n
