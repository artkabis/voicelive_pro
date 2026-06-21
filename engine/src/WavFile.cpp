// SPDX-License-Identifier: MIT
#include "voicelive/engine/WavFile.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "voicelive/core/Result.hpp"

namespace voicelive::engine::wav {
namespace {

using Bytes = std::vector<unsigned char>;

// --- Écriture (little-endian) ----------------------------------------------

void appendU16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8) & 0xFFU));
}

void appendU32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8) & 0xFFU));
    out.push_back(static_cast<char>((value >> 16) & 0xFFU));
    out.push_back(static_cast<char>((value >> 24) & 0xFFU));
}

std::int16_t floatToPcm16(float sample) {
    const float clamped = std::clamp(sample, -1.0F, 1.0F);
    return static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
}

// --- Lecture (little-endian, bornée) ---------------------------------------

std::uint16_t readU16(const Bytes& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] |
                                      (static_cast<unsigned>(data[offset + 1]) << 8));
}

std::uint32_t readU32(const Bytes& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool matchTag(const Bytes& data, std::size_t offset, const char* tag) {
    for (std::size_t i = 0; i < 4; ++i) {
        if (data[offset + i] != static_cast<unsigned char>(tag[i])) {
            return false;
        }
    }
    return true;
}

struct FormatChunk {
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
};

core::Result<AudioData> decode(const FormatChunk& fmt, const Bytes& data, std::size_t dataOffset,
                               std::size_t dataLength) {
    const std::size_t bytesPerSample = fmt.bitsPerSample / 8U;
    if (bytesPerSample == 0) {
        return core::Error{core::ErrorCode::InvalidArgument, "WAV : bitsPerSample invalide"};
    }
    const std::size_t count = dataLength / bytesPerSample;

    AudioData result;
    result.sampleRate = fmt.sampleRate;
    result.channels = fmt.channels;
    result.samples.reserve(count);

    const bool isPcm16 = (fmt.audioFormat == 1 && fmt.bitsPerSample == 16);
    const bool isFloat32 = (fmt.audioFormat == 3 && fmt.bitsPerSample == 32);
    if (!isPcm16 && !isFloat32) {
        return core::Error{core::ErrorCode::InvalidArgument, "WAV : format non supporté"};
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t offset = dataOffset + (i * bytesPerSample);
        if (isPcm16) {
            const auto raw = static_cast<std::int16_t>(readU16(data, offset));
            // Division par 32768 (et non 32767) : convention WAV standard ; évite
            // un dépassement de +1.0 sur le sample positif maximum (0x7FFF = 0,99997).
            result.samples.push_back(static_cast<float>(raw) / 32768.0F);
        } else {
            // IEEE 754 float32 : lecture brute sans conversion numérique.
            result.samples.push_back(std::bit_cast<float>(readU32(data, offset)));
        }
    }
    return result;
}

core::Result<AudioData> parse(const Bytes& data) {
    if (data.size() < 12 || !matchTag(data, 0, "RIFF") || !matchTag(data, 8, "WAVE")) {
        return core::Error{core::ErrorCode::InvalidArgument, "WAV : en-tête RIFF/WAVE invalide"};
    }

    FormatChunk fmt;
    bool haveFmt = false;
    std::size_t dataOffset = 0;
    std::size_t dataLength = 0;
    bool haveData = false;

    std::size_t offset = 12;
    while (offset + 8 <= data.size()) {
        const std::uint32_t chunkSize = readU32(data, offset + 4);
        const std::size_t body = offset + 8;
        const std::size_t available = data.size() - body;
        const std::size_t take = std::min<std::size_t>(chunkSize, available);

        if (matchTag(data, offset, "fmt ") && take >= 16) {
            fmt.audioFormat = readU16(data, body);
            fmt.channels = readU16(data, body + 2);
            fmt.sampleRate = readU32(data, body + 4);
            fmt.bitsPerSample = readU16(data, body + 14);
            haveFmt = true;
        } else if (matchTag(data, offset, "data")) {
            dataOffset = body;
            dataLength = take;
            haveData = true;
        }

        // Le standard RIFF aligne chaque chunk sur 2 octets : si chunkSize est impair,
        // un octet de padding est inséré (non compté dans chunkSize).
        offset = body + chunkSize + (chunkSize & 1U);
    }

    if (!haveFmt || !haveData) {
        return core::Error{core::ErrorCode::InvalidArgument, "WAV : chunk fmt ou data manquant"};
    }
    if (fmt.channels == 0 || fmt.sampleRate == 0) {
        return core::Error{core::ErrorCode::InvalidArgument, "WAV : canaux ou fréquence invalides"};
    }
    return decode(fmt, data, dataOffset, dataLength);
}

}  // namespace

core::Status write(const std::string& path, const AudioData& data) {
    if (data.channels == 0 || data.sampleRate == 0) {
        return core::Status::failure(core::ErrorCode::InvalidArgument,
                                     "WAV : canaux ou fréquence invalides");
    }

    constexpr std::uint16_t kBitsPerSample = 16;
    const auto channels = static_cast<std::uint16_t>(data.channels);
    const auto blockAlign = static_cast<std::uint16_t>(channels * (kBitsPerSample / 8U));
    const std::uint32_t byteRate = data.sampleRate * blockAlign;
    const auto dataSize = static_cast<std::uint32_t>(data.samples.size() * (kBitsPerSample / 8U));

    std::string out;
    out.append("RIFF", 4);
    appendU32(out, 36U + dataSize);  // taille du RIFF
    out.append("WAVE", 4);
    out.append("fmt ", 4);
    appendU32(out, 16U);  // taille du chunk fmt
    appendU16(out, 1U);   // PCM
    appendU16(out, channels);
    appendU32(out, data.sampleRate);
    appendU32(out, byteRate);
    appendU16(out, blockAlign);
    appendU16(out, kBitsPerSample);
    out.append("data", 4);
    appendU32(out, dataSize);
    for (const float sample : data.samples) {
        appendU16(out, static_cast<std::uint16_t>(floatToPcm16(sample)));
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return core::Status::failure(core::ErrorCode::InvalidArgument,
                                     "Ouverture en écriture impossible : " + path);
    }
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!file) {
        return core::Status::failure(core::ErrorCode::InvalidArgument,
                                     "Échec d'écriture : " + path);
    }
    return core::Status::success();
}

core::Result<AudioData> read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           "Ouverture en lecture impossible : " + path};
    }
    const Bytes bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return parse(bytes);
}

}  // namespace voicelive::engine::wav
