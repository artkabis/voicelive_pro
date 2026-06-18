// SPDX-License-Identifier: MIT
#include "MainComponent.h"

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Music.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/engine/ChannelUtils.hpp"

namespace channels = voicelive::engine::channels;
namespace music = voicelive::core::music;
using voicelive::core::Bpm;
using voicelive::core::SampleRate;
using voicelive::core::TrackState;
using voicelive::engine::EngineCommand;

namespace {
const char* actionName(EngineCommand::Action action) {
    switch (action) {
        case EngineCommand::Action::Record:
            return "Record";
        case EngineCommand::Action::FinishRecording:
            return "Finish";
        case EngineCommand::Action::Play:
            return "Play";
        case EngineCommand::Action::Stop:
            return "Stop";
        case EngineCommand::Action::StartOverdub:
            return "Overdub";
        case EngineCommand::Action::StopOverdub:
            return "StopOverdub";
        case EngineCommand::Action::Clear:
            return "Clear";
        case EngineCommand::Action::SetGain:
            return "SetGain";
        case EngineCommand::Action::SetMuted:
            return "SetMuted";
        case EngineCommand::Action::SelectTrack:
            return "Select";
    }
    return "?";
}

// FFT Cooley-Tukey radix-2 DIT, in-place, sur tableau complexe de taille 2^N.
// Applique une fenetre de Hann sur l'entree et calcule les magnitudes en sortie.
// input  : kFftSize echantillons reels
// output : kFftSize/2 magnitudes normalisees [0..1]
template <int kFftSize>
void magnitudeSpectrum(const float* input, float* output) {
    using C = std::complex<float>;
    std::array<C, kFftSize> buf;

    for (int i = 0; i < kFftSize; ++i) {
        const float w =
            0.5F * (1.0F - std::cos(2.0F * juce::MathConstants<float>::pi * i / (kFftSize - 1)));
        buf[static_cast<std::size_t>(i)] = C(input[i] * w, 0.0F);
    }

    // Permutation bit-reverse
    for (int i = 1, j = 0; i < kFftSize; ++i) {
        int bit = kFftSize >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(buf[static_cast<std::size_t>(i)], buf[static_cast<std::size_t>(j)]);
        }
    }

    // Papillons
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const float ang = -2.0F * juce::MathConstants<float>::pi / static_cast<float>(len);
        const C wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < kFftSize; i += len) {
            C w(1.0F, 0.0F);
            for (int j = 0; j < len / 2; ++j) {
                const C u = buf[static_cast<std::size_t>(i + j)];
                const C v = buf[static_cast<std::size_t>(i + j + len / 2)] * w;
                buf[static_cast<std::size_t>(i + j)] = u + v;
                buf[static_cast<std::size_t>(i + j + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }

    const float scale = 2.0F / static_cast<float>(kFftSize);
    for (int i = 0; i < kFftSize / 2; ++i) {
        output[i] = std::abs(buf[static_cast<std::size_t>(i)]) * scale;
    }
}
}  // namespace

// ─── TrackWaveform ────────────────────────────────────────────────────────────

void MainComponent::TrackWaveform::setAudio(const voicelive::engine::LoopAudio* audio) noexcept {
    audio_ = audio;
}

void MainComponent::TrackWaveform::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF0D1117));
    g.fillRoundedRectangle(bounds, 4.0F);

    if (audio_ == nullptr || audio_->length() == 0) {
        g.setColour(juce::Colours::grey.withAlpha(0.35F));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0F)));
        g.drawText("vide", bounds, juce::Justification::centred, false);
        g.setColour(juce::Colours::grey.withAlpha(0.25F));
        g.drawRoundedRectangle(bounds.reduced(0.5F), 4.0F, 1.0F);
        return;
    }

    const int w = getWidth();
    const int h = getHeight();
    const float midY = static_cast<float>(h) * 0.5F;
    const std::size_t len = audio_->loopLength();

    // Ligne centrale
    g.setColour(juce::Colours::grey.withAlpha(0.25F));
    g.drawHorizontalLine(static_cast<int>(midY), 2.0F, static_cast<float>(w) - 2.0F);

    // Forme d'onde (peak-detection par pixel)
    g.setColour(juce::Colour(0xFF00D4FF));
    const int drawW = w - 4;
    for (int px = 0; px < drawW; ++px) {
        const std::size_t sBegin =
            static_cast<std::size_t>(px) * len / static_cast<std::size_t>(drawW);
        const std::size_t sEnd =
            static_cast<std::size_t>(px + 1) * len / static_cast<std::size_t>(drawW);
        float peak = 0.0F;
        for (std::size_t s = sBegin; s < sEnd && s < len; ++s) {
            peak = std::max(peak, std::abs(audio_->sampleAt(s)));
        }
        const float half = juce::jmin(peak, 1.0F) * (midY - 2.0F);
        if (half > 0.5F) {
            g.drawVerticalLine(px + 2, midY - half, midY + half);
        }
    }

    g.setColour(juce::Colours::grey.withAlpha(0.25F));
    g.drawRoundedRectangle(bounds.reduced(0.5F), 4.0F, 1.0F);
}

// ─── SpectrumView ─────────────────────────────────────────────────────────────

void MainComponent::SpectrumView::update(std::span<const float> analysis) {
    if (static_cast<int>(analysis.size()) < kFftSize) {
        return;
    }
    std::array<float, kFftSize / 2> mag;
    magnitudeSpectrum<kFftSize>(analysis.data() + analysis.size() - kFftSize, mag.data());

    // Lissage exponentiel
    constexpr float kAlpha = 0.25F;
    for (int i = 0; i < kFftSize / 2; ++i) {
        smoothed_[static_cast<std::size_t>(i)] =
            (1.0F - kAlpha) * smoothed_[static_cast<std::size_t>(i)] +
            kAlpha * juce::jlimit(0.0F, 1.0F, mag[static_cast<std::size_t>(i)]);
    }
    hasData_ = true;
    repaint();
}

void MainComponent::SpectrumView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF0D1117));
    g.fillRoundedRectangle(bounds, 4.0F);

    if (!hasData_) {
        g.setColour(juce::Colours::grey.withAlpha(0.35F));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0F)));
        g.drawText("Spectre temps-reel", bounds, juce::Justification::centred, false);
        g.setColour(juce::Colours::grey.withAlpha(0.25F));
        g.drawRoundedRectangle(bounds.reduced(0.5F), 4.0F, 1.0F);
        return;
    }

    const int w = getWidth();
    const int h = getHeight();
    // Affiche les 192 premieres bandes (0-18 kHz @ 48kHz) sur echelle log.
    constexpr int kBins = 192;
    const float barW = static_cast<float>(w) / static_cast<float>(kBins);

    for (int i = 0; i < kBins && i < kFftSize / 2; ++i) {
        const float level = smoothed_[static_cast<std::size_t>(i)];
        const float barH = level * static_cast<float>(h - 2);
        const float x = static_cast<float>(i) * barW;
        const float y = static_cast<float>(h) - barH - 1.0F;

        // Degrade couleur : bleu (grave) -> cyan -> vert (aigu)
        const float t = static_cast<float>(i) / static_cast<float>(kBins);
        const juce::Colour col =
            juce::Colour(0xFF00D4FF).interpolatedWith(juce::Colour(0xFF00FF88), t);
        g.setColour(col.withAlpha(0.85F));
        g.fillRect(x, y, juce::jmax(1.0F, barW - 1.0F), barH);
    }

    // Graduations de frequence
    g.setColour(juce::Colours::grey.withAlpha(0.4F));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0F)));
    for (int khz : {1, 2, 4, 8}) {
        const int bin = khz * 1000 * kFftSize / 48000;
        if (bin < kBins) {
            const float x = static_cast<float>(bin) * barW;
            g.drawVerticalLine(static_cast<int>(x), 0.0F, static_cast<float>(h));
            g.drawText(juce::String(khz) + "k", static_cast<int>(x) + 2, 2, 28, 14,
                       juce::Justification::left, false);
        }
    }

    g.setColour(juce::Colours::grey.withAlpha(0.25F));
    g.drawRoundedRectangle(bounds.reduced(0.5F), 4.0F, 1.0F);
}

// ─── AppLogger ────────────────────────────────────────────────────────────────

void MainComponent::AppLogger::logMessage(const juce::String& message) {
    {
        const juce::ScopedLock scoped(lock_);
        lines_.add(message);
        while (lines_.size() > 200) {
            lines_.remove(0);
        }
    }
    juce::Logger::outputDebugString(message);  // → logcat sur Android
}

juce::String MainComponent::AppLogger::snapshot() const {
    const juce::ScopedLock scoped(lock_);
    return lines_.joinIntoString("\n");
}

// ─── MainComponent ────────────────────────────────────────────────────────────

MainComponent::MainComponent() {
    juce::Logger::setCurrentLogger(&appLogger_);
    juce::Logger::writeToLog("VoiceLive Pro demarre");

    // Viewport de défilement vertical : tout le contenu UI est dans contentPane_.
    viewport_.setScrollBarsShown(true, false);
    viewport_.setViewedComponent(&contentPane_, false);
    addAndMakeVisible(viewport_);

    // Demande explicite de la permission micro (indispensable à l'entrée audio).
    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [](bool granted) {
        juce::Logger::writeToLog(granted ? "Permission micro : OK" : "Permission micro : REFUSEE");
    });

    titleLabel_.setText("VoiceLive Pro", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0F)));
    contentPane_.addAndMakeVisible(titleLabel_);

    // Accordeur + toggle ON/OFF
    tunerLabel_.setText("Accordeur : ---", juce::dontSendNotification);
    tunerLabel_.setJustificationType(juce::Justification::centredLeft);
    contentPane_.addAndMakeVisible(tunerLabel_);

    tunerActiveButton_.setButtonText("Accordeur");
    tunerActiveButton_.setToggleState(false, juce::dontSendNotification);
    tunerActiveButton_.onClick = [this] {
        if (!tunerActiveButton_.getToggleState()) {
            tunerLabel_.setText("Accordeur : ---", juce::dontSendNotification);
        }
    };
    contentPane_.addAndMakeVisible(tunerActiveButton_);

    // Pistes
    for (std::size_t i = 0; i < kTrackCount; ++i) {
        setupTrackStrip(i);
        contentPane_.addAndMakeVisible(waveforms_[i]);
    }

    // Transport : métronome + tempo.
    metronomeButton_.setButtonText("Metronome");
    metronomeButton_.onClick = [this] {
        engine_.setMetronomeEnabled(metronomeButton_.getToggleState());
    };
    contentPane_.addAndMakeVisible(metronomeButton_);
    bpmSlider_.setRange(40.0, 240.0, 1.0);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.onValueChange = [this] {
        engine_.transport().setBpm(Bpm::fromValue(bpmSlider_.getValue()));
    };
    contentPane_.addAndMakeVisible(bpmSlider_);

    // Mastering : égaliseur 3 bandes inséré une fois dans le bus master.
    masterLabel_.setText("Mastering - EQ 3 bandes", juce::dontSendNotification);
    contentPane_.addAndMakeVisible(masterLabel_);
    {
        auto equalizer = std::make_unique<voicelive::dsp::Equalizer>();
        masterEq_ = equalizer.get();
        engine_.masterEffects().add(std::move(equalizer));
    }
    const auto setupEq = [this](juce::Slider& slider, const juce::String& name) {
        slider.setRange(-12.0, 12.0, 0.5);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
        slider.setName(name);
        contentPane_.addAndMakeVisible(slider);
    };

    lowEqLabel_.setText("Grave", juce::dontSendNotification);
    lowEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(lowEqLabel_);
    setupEq(lowEqSlider_, "Grave");
    lowEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setLowGain(static_cast<float>(lowEqSlider_.getValue()));
        }
    };

    midEqLabel_.setText("Medium", juce::dontSendNotification);
    midEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(midEqLabel_);
    setupEq(midEqSlider_, "Medium");
    midEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setMidGain(static_cast<float>(midEqSlider_.getValue()));
        }
    };

    highEqLabel_.setText("Aigu", juce::dontSendNotification);
    highEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(highEqLabel_);
    setupEq(highEqSlider_, "Aigu");
    highEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setHighGain(static_cast<float>(highEqSlider_.getValue()));
        }
    };

    // Spectre temps-réel
    contentPane_.addAndMakeVisible(spectrumView_);

    // Panneau de diagnostic : rend l'app observable sur mobile (pas de console).
    diagView_.setMultiLine(true);
    diagView_.setReadOnly(true);
    diagView_.setCaretVisible(false);
    diagView_.setScrollbarsShown(true);
    diagView_.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0F)));
    contentPane_.addAndMakeVisible(diagView_);

    copyButton_.setButtonText("Copier le diagnostic");
    copyButton_.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(diagView_.getText());
        juce::Logger::writeToLog("Diagnostic copie dans le presse-papier");
    };
    contentPane_.addAndMakeVisible(copyButton_);

    analysis_.assign(kAnalysisSize, 0.0F);
    setSize(400, 700);
    setAudioChannels(2, 2);
    startTimerHz(10);
}

MainComponent::~MainComponent() {
    stopTimer();
    shutdownAudio();
    juce::Logger::setCurrentLogger(nullptr);
}

void MainComponent::setupTrackStrip(std::size_t index) {
    TrackStrip& strip = strips_[index];
    strip.label.setText("Piste " + juce::String(static_cast<int>(index) + 1),
                        juce::dontSendNotification);
    contentPane_.addAndMakeVisible(strip.label);

    strip.recordButton.setButtonText("Rec");
    strip.recordButton.onClick = [this, index] { recordOrFinish(index); };
    contentPane_.addAndMakeVisible(strip.recordButton);

    strip.playButton.setButtonText("Play");
    strip.playButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Play, index, 1.0F, false);
    };
    contentPane_.addAndMakeVisible(strip.playButton);

    strip.stopButton.setButtonText("Stop");
    strip.stopButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Stop, index, 1.0F, false);
    };
    contentPane_.addAndMakeVisible(strip.stopButton);

    strip.clearButton.setButtonText("Clear");
    strip.clearButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Clear, index, 1.0F, false);
    };
    contentPane_.addAndMakeVisible(strip.clearButton);

    strip.gainSlider.setRange(0.0, 2.0, 0.01);
    strip.gainSlider.setValue(1.0, juce::dontSendNotification);
    strip.gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    strip.gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    strip.gainSlider.onValueChange = [this, index] {
        postCommand(EngineCommand::Action::SetGain, index,
                    static_cast<float>(strips_[index].gainSlider.getValue()), false);
    };
    contentPane_.addAndMakeVisible(strip.gainSlider);

    strip.muteButton.setButtonText("Mute");
    strip.muteButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::SetMuted, index, 1.0F,
                    strips_[index].muteButton.getToggleState());
    };
    contentPane_.addAndMakeVisible(strip.muteButton);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    try {
        const auto blockSize = static_cast<std::size_t>(juce::jmax(1, samplesPerBlockExpected));
        const auto rate = static_cast<unsigned>(juce::jmax(1.0, sampleRate));
        const auto capacity = static_cast<std::size_t>(juce::jmax(1.0, sampleRate) * 30.0);

        if (const auto sr = SampleRate::create(rate); sr.ok()) {
            static_cast<void>(engine_.prepare(sr.value(), kTrackCount, capacity, blockSize));
            juce::Logger::writeToLog("VoiceLive: audio pret " + juce::String(rate) + " Hz, bloc " +
                                     juce::String(static_cast<int>(blockSize)));
        } else {
            juce::Logger::writeToLog("VoiceLive: ERREUR frequence " + juce::String(rate));
        }
        monoIn_.assign(blockSize, 0.0F);
        monoOut_.assign(blockSize, 0.0F);
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("ERREUR prepareToPlay: ") + e.what());
    } catch (...) {
        juce::Logger::writeToLog("ERREUR inconnue dans prepareToPlay");
    }
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    if (bufferToFill.buffer == nullptr) {
        return;
    }
    juce::AudioBuffer<float>* buffer = bufferToFill.buffer;
    const auto numSamples = static_cast<std::size_t>(bufferToFill.numSamples);
    if (numSamples == 0 || numSamples > monoIn_.size()) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    const int numChannels = juce::jmin(buffer->getNumChannels(), kMaxChannels);
    std::array<const float*, kMaxChannels> inputPtrs{};
    std::array<float*, kMaxChannels> outputPtrs{};
    for (int channel = 0; channel < numChannels; ++channel) {
        float* data = buffer->getWritePointer(channel, bufferToFill.startSample);
        outputPtrs[static_cast<std::size_t>(channel)] = data;
        inputPtrs[static_cast<std::size_t>(channel)] = data;
    }

    const std::span<float> monoIn{monoIn_.data(), numSamples};
    const std::span<float> monoOut{monoOut_.data(), numSamples};
    channels::downmixToMono(monoIn, inputPtrs.data(), static_cast<std::size_t>(numChannels));

    // Alimente la fenêtre d'analyse pour l'accordeur et le spectre (course bénigne avec Timer).
    if (numSamples < analysis_.size()) {
        const std::size_t keep = analysis_.size() - numSamples;
        std::copy(analysis_.begin() + static_cast<std::ptrdiff_t>(numSamples), analysis_.end(),
                  analysis_.begin());
        std::copy(monoIn_.begin(), monoIn_.begin() + static_cast<std::ptrdiff_t>(numSamples),
                  analysis_.begin() + static_cast<std::ptrdiff_t>(keep));
    }

    engine_.process(monoOut, monoIn);

    // Couper la sortie haut-parleur pendant l'enregistrement pour éviter le larsen.
    if (anyTrackRecording_.load(std::memory_order_acquire)) {
        std::fill(monoOut.begin(), monoOut.end(), 0.0F);
    }

    channels::spreadToChannels(outputPtrs.data(), static_cast<std::size_t>(numChannels), monoOut);
}

void MainComponent::releaseResources() {}

void MainComponent::timerCallback() {
    if (analysis_.empty()) {
        return;
    }

    // Accordeur (seulement si activé)
    if (tunerActiveButton_.getToggleState()) {
        const auto note = engine_.tune(std::span<const float>{analysis_.data(), analysis_.size()});
        if (!note.has_value()) {
            tunerLabel_.setText("Accordeur : ---", juce::dontSendNotification);
        } else {
            const int cents = juce::roundToInt(note->cents);
            const juce::String text = juce::String("Accordeur : ") + music::name(note->midi) +
                                      juce::String(music::octave(note->midi)) + "   " +
                                      (cents >= 0 ? "+" : "") + juce::String(cents) + " cents";
            tunerLabel_.setText(text, juce::dontSendNotification);
        }
    }

    // Spectre temps-réel
    spectrumView_.update(std::span<const float>{analysis_.data(), analysis_.size()});

    // Waveforms des pistes
    for (std::size_t i = 0; i < kTrackCount; ++i) {
        if (const auto* proc = engine_.track(i); proc != nullptr) {
            waveforms_[i].setAudio(&proc->audio());
        }
        waveforms_[i].repaint();
    }

    updateDiagnostics();
}

void MainComponent::updateDiagnostics() {
    juce::String text;
    text << "VoiceLive Pro v2.0.0  |  JUCE " << juce::SystemStats::getJUCEVersion() << "\n";
    text << "Build : " << __DATE__ << " " << __TIME__ << "\n";

    if (anyTrackRecording_.load(std::memory_order_relaxed)) {
        text << "[REC EN COURS] Speaker coupe - utiliser des ecouteurs pour le retour\n";
    }

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr) {
        text << "Audio : " << device->getName() << "  "
             << juce::String(device->getCurrentSampleRate(), 0) << " Hz / buffer "
             << device->getCurrentBufferSizeSamples() << "\n";
    } else {
        text << "Audio : NON DEMARRE (verifier les permissions micro)\n";
    }

    const auto diag = engine_.diagnostics();
    text << "Moteur : " << static_cast<int>(diag.trackCount) << " pistes, "
         << static_cast<int>(diag.blocksProcessed) << " blocs, "
         << static_cast<int>(diag.droppedCommands) << " cmd perdues, metronome "
         << (diag.metronomeEnabled ? "ON" : "OFF") << ", master FX "
         << static_cast<int>(diag.masterEffectCount) << "\n";

    for (std::size_t i = 0; i < diag.trackCount; ++i) {
        const auto* processor = engine_.track(i);
        if (processor == nullptr) {
            continue;
        }
        const auto& track = processor->track();
        text << "  Piste " << static_cast<int>(i) + 1 << " : "
             << voicelive::core::toString(track.state()) << "  gain "
             << juce::String(track.gain().linear(), 2) << (track.isMuted() ? "  [MUTE]" : "")
             << "  " << static_cast<int>(processor->audio().length() / 48000) << "s\n";
    }

    text << "\n--- Journal (recent) ---\n" << appLogger_.snapshot();
    diagView_.setText(text, false);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
    viewport_.setBounds(getLocalBounds());

    const int usableW = juce::jmax(300, viewport_.getMaximumVisibleWidth());
    const int pad = 12;

    constexpr int kTitleH = 34;
    constexpr int kTunerH = 34;  // ligne accordeur (toggle + label)
    constexpr int kRowH = 44;
    constexpr int kTrackCtrlH = kRowH * 2 + 6;  // 2 rangees de controles
    constexpr int kWaveH = 52;                  // waveform par piste
    constexpr int kTrackH = kTrackCtrlH + kWaveH + 4;
    constexpr int kTransH = 48;
    constexpr int kEqLblH = 26;
    constexpr int kEqRowH = 44;
    constexpr int kSpecH = 90;  // vue spectre
    constexpr int kCopyH = 40;
    constexpr int kDiagH = 200;
    constexpr int kGap = 10;

    const int trackCount = static_cast<int>(kTrackCount);
    const int totalH = pad + kTitleH + kGap / 2 + kTunerH + kGap +
                       trackCount * (kTrackH + kGap / 2) + kGap + kTransH + kGap + kEqLblH + 4 +
                       3 * (kEqRowH + 4) + kGap + kSpecH + kGap + kCopyH + kGap / 2 + kDiagH + pad;

    contentPane_.setSize(usableW, totalH);
    auto area = contentPane_.getLocalBounds().reduced(pad);

    // Titre
    titleLabel_.setBounds(area.removeFromTop(kTitleH));
    area.removeFromTop(kGap / 2);

    // Accordeur : [toggle] [label]
    {
        auto row = area.removeFromTop(kTunerH);
        tunerActiveButton_.setBounds(row.removeFromLeft(110).reduced(2));
        tunerLabel_.setBounds(row.reduced(2));
    }
    area.removeFromTop(kGap);

    // Pistes
    const int w = area.getWidth();
    for (std::size_t i = 0; i < kTrackCount; ++i) {
        TrackStrip& strip = strips_[i];
        auto trackArea = area.removeFromTop(kTrackH);

        // Rangee 1 : label + boutons
        auto row1 = trackArea.removeFromTop(kRowH);
        const int labelW = 56;
        const int btnW = juce::jmax(42, (w - labelW) / 4);
        strip.label.setBounds(row1.removeFromLeft(labelW));
        strip.recordButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strip.playButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strip.stopButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strip.clearButton.setBounds(row1.removeFromLeft(btnW).reduced(2));

        trackArea.removeFromTop(6);

        // Rangee 2 : gain + mute
        auto row2 = trackArea.removeFromTop(kRowH);
        strip.muteButton.setBounds(row2.removeFromRight(64).reduced(2));
        strip.gainSlider.setBounds(row2.reduced(2));

        trackArea.removeFromTop(4);

        // Waveform
        waveforms_[i].setBounds(trackArea.removeFromTop(kWaveH));

        area.removeFromTop(kGap / 2);
    }

    // Transport
    area.removeFromTop(kGap);
    {
        auto row = area.removeFromTop(kTransH);
        metronomeButton_.setBounds(row.removeFromLeft(120).reduced(2));
        bpmSlider_.setBounds(row.reduced(4));
    }

    // EQ
    area.removeFromTop(kGap);
    masterLabel_.setBounds(area.removeFromTop(kEqLblH));
    area.removeFromTop(4);

    const int eqLabelW = 68;
    const std::pair<juce::Slider*, juce::Label*> eqRows[3] = {
        {&lowEqSlider_, &lowEqLabel_},
        {&midEqSlider_, &midEqLabel_},
        {&highEqSlider_, &highEqLabel_},
    };
    for (auto [slider, label] : eqRows) {
        auto row = area.removeFromTop(kEqRowH);
        label->setBounds(row.removeFromLeft(eqLabelW));
        slider->setBounds(row.reduced(2));
        area.removeFromTop(4);
    }

    // Spectre
    area.removeFromTop(kGap);
    spectrumView_.setBounds(area.removeFromTop(kSpecH));

    // Diagnostic
    area.removeFromTop(kGap);
    copyButton_.setBounds(area.removeFromTop(kCopyH).reduced(2));
    area.removeFromTop(kGap / 2);
    diagView_.setBounds(area.removeFromTop(kDiagH));
}

void MainComponent::recordOrFinish(std::size_t index) {
    const auto* processor = engine_.track(index);
    if (processor != nullptr && processor->track().state() == TrackState::Recording) {
        postCommand(EngineCommand::Action::FinishRecording, index, 1.0F, false);
        anyTrackRecording_.store(false, std::memory_order_release);
    } else {
        postCommand(EngineCommand::Action::Record, index, 1.0F, false);
        anyTrackRecording_.store(true, std::memory_order_release);
    }
}

void MainComponent::postCommand(EngineCommand::Action action, std::size_t track, float gain,
                                bool muted) {
    EngineCommand command;
    command.action = action;
    command.track = track;
    command.gain = gain;
    command.muted = muted;
    const bool accepted = engine_.post(command);

    // Si la piste est effacée ou stoppée, on annule le flag d'enregistrement
    // (cas : Clear/Stop pendant un enregistrement actif).
    if (action == EngineCommand::Action::Clear || action == EngineCommand::Action::Stop) {
        anyTrackRecording_.store(false, std::memory_order_release);
    }

    juce::Logger::writeToLog(juce::String("Clic piste ") +
                             juce::String(static_cast<int>(track) + 1) + " : " +
                             actionName(action) + (accepted ? "" : "  (FILE PLEINE)"));
}
