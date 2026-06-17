// SPDX-License-Identifier: MIT
#include "voicelive/core/ProjectSerializer.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/core/Transport.hpp"

namespace voicelive::core::project_io {
namespace {

constexpr std::string_view kHeader = "VOICELIVE_PROJECT v1";

struct TrackSettings {
    float gain = 1.0F;
    bool muted = false;
};

struct ParsedProject {
    std::string name;
    std::optional<double> bpm;
    std::optional<unsigned> signatureNumerator;
    std::optional<unsigned> signatureDenominator;
    std::optional<unsigned> sampleRate;
    std::optional<std::size_t> selected;
    std::optional<std::size_t> trackCount;
    std::vector<TrackSettings> tracks;
};

std::optional<double> toDouble(std::string_view text) {
    double value = 0.0;
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<unsigned long long> toUnsigned(std::string_view text) {
    unsigned long long value = 0;
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

void parseTimeSignature(std::string_view value, ParsedProject& parsed) {
    const auto slash = value.find('/');
    if (slash == std::string_view::npos) {
        return;
    }
    if (const auto num = toUnsigned(value.substr(0, slash))) {
        parsed.signatureNumerator = static_cast<unsigned>(*num);
    }
    if (const auto den = toUnsigned(value.substr(slash + 1))) {
        parsed.signatureDenominator = static_cast<unsigned>(*den);
    }
}

void parseTrack(std::string_view value, std::vector<TrackSettings>& tracks) {
    const auto comma = value.find(',');
    if (comma == std::string_view::npos) {
        return;
    }
    const auto gain = toDouble(value.substr(0, comma));
    if (!gain) {
        return;
    }
    tracks.push_back(TrackSettings{static_cast<float>(*gain), value.substr(comma + 1) == "1"});
}

void parseLine(std::string_view line, ParsedProject& parsed) {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) {
        return;
    }
    const std::string_view key = line.substr(0, equals);
    const std::string_view value = line.substr(equals + 1);

    if (key == "name") {
        parsed.name = std::string{value};
    } else if (key == "bpm") {
        parsed.bpm = toDouble(value);
    } else if (key == "time_signature") {
        parseTimeSignature(value, parsed);
    } else if (key == "sample_rate") {
        if (const auto rate = toUnsigned(value)) {
            parsed.sampleRate = static_cast<unsigned>(*rate);
        }
    } else if (key == "selected") {
        if (const auto sel = toUnsigned(value)) {
            parsed.selected = static_cast<std::size_t>(*sel);
        }
    } else if (key == "track_count") {
        if (const auto count = toUnsigned(value)) {
            parsed.trackCount = static_cast<std::size_t>(*count);
        }
    } else if (key == "track") {
        parseTrack(value, parsed.tracks);
    }
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto newline = text.find('\n', start);
        const auto end = (newline == std::string_view::npos) ? text.size() : newline;
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);  // tolère les fins de ligne Windows
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

Result<Project> build(const ParsedProject& parsed) {
    if (!parsed.trackCount || !parsed.bpm || !parsed.signatureNumerator ||
        !parsed.signatureDenominator || !parsed.sampleRate || !parsed.selected) {
        return Error{ErrorCode::InvalidArgument, "Champ de projet requis manquant ou invalide"};
    }
    if (parsed.tracks.size() != *parsed.trackCount) {
        return Error{ErrorCode::InvalidArgument, "Nombre de pistes incohérent avec track_count"};
    }

    auto projectResult = Project::create(parsed.name, *parsed.trackCount);
    if (!projectResult.ok()) {
        return projectResult.error();
    }
    Project& project = projectResult.value();

    project.transport().setBpm(Bpm::fromValue(*parsed.bpm));

    auto signature =
        TimeSignature::create(*parsed.signatureNumerator, *parsed.signatureDenominator);
    if (!signature.ok()) {
        return signature.error();
    }
    project.transport().setSignature(signature.value());

    auto sampleRate = SampleRate::create(*parsed.sampleRate);
    if (!sampleRate.ok()) {
        return sampleRate.error();
    }
    project.transport().setSampleRate(sampleRate.value());

    for (std::size_t i = 0; i < parsed.tracks.size(); ++i) {
        LooperTrack* track = project.track(i);
        if (track == nullptr) {
            return Error{ErrorCode::OutOfRange, "Index de piste hors bornes au chargement"};
        }
        track->setGain(Gain::fromLinear(parsed.tracks[i].gain));
        track->setMuted(parsed.tracks[i].muted);
    }

    if (const Status selection = project.selectTrack(*parsed.selected); !selection.ok()) {
        return selection.error();
    }
    return projectResult;
}

}  // namespace

std::string serialize(const Project& project) {
    const Transport& transport = project.transport();
    std::string out;
    out += kHeader;
    out += '\n';
    out += "name=" + project.name() + '\n';
    out += "bpm=" + std::to_string(transport.bpm().value()) + '\n';
    out += "time_signature=" + std::to_string(transport.signature().numerator()) + "/" +
           std::to_string(transport.signature().denominator()) + '\n';
    out += "sample_rate=" + std::to_string(transport.sampleRate().hz()) + '\n';
    out += "selected=" + std::to_string(project.selectedIndex()) + '\n';
    out += "track_count=" + std::to_string(project.trackCount()) + '\n';
    for (std::size_t i = 0; i < project.trackCount(); ++i) {
        const LooperTrack* track = project.track(i);
        const float gain = (track != nullptr) ? track->gain().linear() : 1.0F;
        const bool muted = (track != nullptr) && track->isMuted();
        out += "track=" + std::to_string(gain) + "," + (muted ? "1" : "0") + '\n';
    }
    return out;
}

Result<Project> deserialize(std::string_view text) {
    const std::vector<std::string_view> lines = splitLines(text);
    if (lines.empty() || lines.front() != kHeader) {
        return Error{ErrorCode::InvalidArgument, "En-tête de projet invalide"};
    }
    ParsedProject parsed;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        parseLine(lines[i], parsed);
    }
    return build(parsed);
}

Status saveToFile(const std::string& path, const Project& project) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return Status::failure(ErrorCode::InvalidArgument,
                               "Ouverture en écriture impossible : " + path);
    }
    const std::string data = serialize(project);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file) {
        return Status::failure(ErrorCode::InvalidArgument, "Échec d'écriture : " + path);
    }
    return Status::success();
}

Result<Project> loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Error{ErrorCode::InvalidArgument, "Ouverture en lecture impossible : " + path};
    }
    const std::string data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return deserialize(data);
}

}  // namespace voicelive::core::project_io
