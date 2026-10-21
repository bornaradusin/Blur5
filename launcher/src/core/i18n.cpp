// Blur5 - The words the page shows. Blur5's page is in English only.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/i18n.hpp"

namespace store::i18n
{

std::string_view tr(std::string_view english)
{
    return english;
}

std::string trs(std::string_view english)
{
    return std::string(english);
}

std::string upper(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 4);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte >= 'a' && byte <= 'z')
        {
            out += static_cast<char>(byte - 'a' + 'A');
        }
        else if ((byte == 0xC3 || byte == 0xC5) && i + 1 < text.size())
        {
            // Latin-1 (U+00E0..U+00FE, but not the division sign) and the ligature oe.
            auto next = static_cast<unsigned char>(text[i + 1]);
            ++i;
            if (byte == 0xC3 && next == 0x9F)
                out += "SS"; // the sharp s has no capital in the page's fonts
            else if (byte == 0xC3 && next >= 0xA0 && next <= 0xBE && next != 0xB7)
            {
                out += static_cast<char>(byte);
                out += static_cast<char>(next - 0x20);
            }
            else if (byte == 0xC5 && next == 0x93)
            {
                out += static_cast<char>(byte);
                out += static_cast<char>(0x92);
            }
            else
            {
                out += static_cast<char>(byte);
                out += static_cast<char>(next);
            }
        }
        else
        {
            out += static_cast<char>(byte);
        }
    }
    return out;
}

const char *trc(const char *english)
{
    return english;
}

std::string trf(std::string_view english, std::initializer_list<std::string_view> arguments)
{
    std::string out;
    out.reserve(english.size() + 16);
    for (std::size_t i = 0; i < english.size(); ++i)
    {
        if (english[i] == '{' && i + 2 < english.size() && english[i + 1] >= '0' &&
            english[i + 1] <= '9' && english[i + 2] == '}')
        {
            const auto number = static_cast<std::size_t>(english[i + 1] - '0');
            if (number < arguments.size())
                out += *(arguments.begin() + static_cast<std::ptrdiff_t>(number));
            i += 2;
            continue;
        }
        out += english[i];
    }
    return out;
}

} // namespace store::i18n
