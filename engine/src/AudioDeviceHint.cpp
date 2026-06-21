// SPDX-License-Identifier: MIT
#include "voicelive/engine/AudioDeviceHint.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace voicelive::engine {

namespace {
bool containsCI(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > hay.size()) {
        return false;
    }
    const auto* it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                                 [](unsigned char a, unsigned char b) noexcept {
                                     return std::tolower(a) == std::tolower(b);
                                 });
    return it != hay.end();
}
}  // namespace

bool looksLikeHeadphones(std::string_view deviceName, std::string_view typeName) noexcept {
    // USB audio (USB-C DAC, casque USB, adaptateur USB-C -> jack)
    if (containsCI(deviceName, "usb") || containsCI(typeName, "usb")) {
        return true;
    }
    // Casque / ecouteurs filaires
    if (containsCI(deviceName, "headset") || containsCI(deviceName, "headphone")) {
        return true;
    }
    if (containsCI(deviceName, "earphone") || containsCI(deviceName, "earpiece")) {
        return true;
    }
    // Sortie filaire generique
    if (containsCI(deviceName, "wired")) {
        return true;
    }
    return false;
}

}  // namespace voicelive::engine
