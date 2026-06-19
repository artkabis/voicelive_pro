// SPDX-License-Identifier: MIT
#include "MainComponent.h"

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Music.hpp"
#include "voicelive/core/ProjectSerializer.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/engine/ChannelUtils.hpp"
#include "voicelive/engine/WavFile.hpp"

namespace channels = voicelive::engine::channels;
namespace music = voicelive::core::music;
namespace project_io = voicelive::core::project_io;
namespace wav = voicelive::engine::wav;
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

    // Forme d'onde (peak-detection par pixel, avec cache pour eviter O(N) par frame).
    g.setColour(juce::Colour(0xFF00D4FF));
    const int drawW = w - 4;
    if (drawW > 0) {
        if (len != cachedLoopLength_ || w != cachedWidth_) {
            peakCache_.resize(static_cast<std::size_t>(drawW), 0.0F);
            for (int px = 0; px < drawW; ++px) {
                const std::size_t sBegin =
                    static_cast<std::size_t>(px) * len / static_cast<std::size_t>(drawW);
                const std::size_t sEnd =
                    static_cast<std::size_t>(px + 1) * len / static_cast<std::size_t>(drawW);
                float peak = 0.0F;
                for (std::size_t s = sBegin; s < sEnd && s < len; ++s) {
                    peak = std::max(peak, std::abs(audio_->sampleAt(s)));
                }
                peakCache_[static_cast<std::size_t>(px)] = peak;
            }
            cachedLoopLength_ = len;
            cachedWidth_ = w;
        }
        for (int px = 0; px < drawW; ++px) {
            const float half =
                juce::jmin(peakCache_[static_cast<std::size_t>(px)], 1.0F) * (midY - 2.0F);
            if (half > 0.5F) {
                g.drawVerticalLine(px + 2, midY - half, midY + half);
            }
        }
    }

    // Surbrillance de la selection (zone orange semi-transparente)
    if (selActive_ || std::abs(selEnd_ - selStart_) > 0.01F) {
        const float x0 = std::min(selStart_, selEnd_) * static_cast<float>(w);
        const float x1 = std::max(selStart_, selEnd_) * static_cast<float>(w);
        g.setColour(juce::Colour(0xFFFFAA00).withAlpha(0.28F));
        g.fillRect(x0, 0.0F, x1 - x0, static_cast<float>(h));
        g.setColour(juce::Colour(0xFFFFAA00).withAlpha(0.85F));
        g.drawVerticalLine(static_cast<int>(x0), 2.0F, static_cast<float>(h) - 2.0F);
        g.drawVerticalLine(static_cast<int>(x1), 2.0F, static_cast<float>(h) - 2.0F);
    }

    g.setColour(juce::Colours::grey.withAlpha(0.25F));
    g.drawRoundedRectangle(bounds.reduced(0.5F), 4.0F, 1.0F);
}

void MainComponent::TrackWaveform::mouseDown(const juce::MouseEvent& e) {
    selStart_ = juce::jlimit(0.0F, 1.0F, static_cast<float>(e.x) / static_cast<float>(getWidth()));
    selEnd_ = selStart_;
    selActive_ = false;
    repaint();
}

void MainComponent::TrackWaveform::mouseDrag(const juce::MouseEvent& e) {
    selEnd_ = juce::jlimit(0.0F, 1.0F, static_cast<float>(e.x) / static_cast<float>(getWidth()));
    repaint();
}

void MainComponent::TrackWaveform::mouseUp(const juce::MouseEvent& /*e*/) {
    selActive_ = std::abs(selEnd_ - selStart_) > 0.02F;
    repaint();
}

bool MainComponent::TrackWaveform::hasSelection() const noexcept {
    return selActive_ && std::abs(selEnd_ - selStart_) > 0.02F;
}

std::pair<float, float> MainComponent::TrackWaveform::selectionNormalized() const noexcept {
    return {std::min(selStart_, selEnd_), std::max(selStart_, selEnd_)};
}

void MainComponent::TrackWaveform::clearSelection() noexcept {
    selActive_ = false;
    selStart_ = 0.0F;
    selEnd_ = 0.0F;
    repaint();
}

// ─── SpectrumView ─────────────────────────────────────────────────────────────

void MainComponent::SpectrumView::update(std::span<const float> analysis) {
    if (static_cast<int>(analysis.size()) < kFftSize) {
        return;
    }
    std::array<float, kFftSize / 2> mag;
    magnitudeSpectrum<kFftSize>(analysis.data() + analysis.size() - kFftSize, mag.data());

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
    constexpr int kBins = 192;
    const float barW = static_cast<float>(w) / static_cast<float>(kBins);

    for (int i = 0; i < kBins && i < kFftSize / 2; ++i) {
        const float level = smoothed_[static_cast<std::size_t>(i)];
        const float barH = level * static_cast<float>(h - 2);
        const float x = static_cast<float>(i) * barW;
        const float y = static_cast<float>(h) - barH - 1.0F;

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
    juce::Logger::outputDebugString(message);
}

juce::String MainComponent::AppLogger::snapshot() const {
    const juce::ScopedLock scoped(lock_);
    return lines_.joinIntoString("\n");
}

// ─── MainComponent ────────────────────────────────────────────────────────────

MainComponent::MainComponent() {
    juce::Logger::setCurrentLogger(&appLogger_);
    juce::Logger::writeToLog("VoiceLive Pro demarre");

    headphoneMonitor_.attach(deviceManager);
    contentPane_.addAndMakeVisible(headphoneLed_);

    viewport_.setScrollBarsShown(true, false);
    viewport_.setViewedComponent(&contentPane_, false);
    addAndMakeVisible(viewport_);

    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [](bool granted) {
        juce::Logger::writeToLog(granted ? "Permission micro : OK" : "Permission micro : REFUSEE");
    });

    titleLabel_.setText("VoiceLive Pro", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0F)));
    contentPane_.addAndMakeVisible(titleLabel_);

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

    for (std::size_t i = 0; i < kTrackCount; ++i) {
        setupTrackStrip(i);
        contentPane_.addAndMakeVisible(waveforms_[i]);
        setupFxPanel(i);

        cutBtns_[i].setButtonText("Cut Sel");
        cutBtns_[i].onClick = [this, i] { cutSelection(i); };
        contentPane_.addAndMakeVisible(cutBtns_[i]);

        trimBtns_[i].setButtonText("Trim Sel");
        trimBtns_[i].onClick = [this, i] { trimToSelection(i); };
        contentPane_.addAndMakeVisible(trimBtns_[i]);

        exportTrackBtns_[i].setButtonText("Export " + juce::String(static_cast<int>(i) + 1));
        exportTrackBtns_[i].onClick = [this, i] { exportTrack(i); };
        contentPane_.addAndMakeVisible(exportTrackBtns_[i]);

        includeBtns_[i].setButtonText("MIX");
        includeBtns_[i].setToggleState(true, juce::dontSendNotification);
        contentPane_.addAndMakeVisible(includeBtns_[i]);
    }

    // Transport
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

    // EQ master
    masterLabel_.setText("Mastering EQ 3 bandes", juce::dontSendNotification);
    contentPane_.addAndMakeVisible(masterLabel_);
    {
        auto equalizer = std::make_unique<voicelive::dsp::Equalizer>();
        masterEq_ = equalizer.get();
        engine_.masterEffects().add(std::move(equalizer));
    }

    const auto setupEqSlider = [this](juce::Slider& slider) {
        slider.setRange(-12.0, 12.0, 0.5);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
        contentPane_.addAndMakeVisible(slider);
    };

    lowEqLabel_.setText("Grave", juce::dontSendNotification);
    lowEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(lowEqLabel_);
    setupEqSlider(lowEqSlider_);
    lowEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setLowGain(static_cast<float>(lowEqSlider_.getValue()));
        }
    };

    midEqLabel_.setText("Medium", juce::dontSendNotification);
    midEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(midEqLabel_);
    setupEqSlider(midEqSlider_);
    midEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setMidGain(static_cast<float>(midEqSlider_.getValue()));
        }
    };

    highEqLabel_.setText("Aigu", juce::dontSendNotification);
    highEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(highEqLabel_);
    setupEqSlider(highEqSlider_);
    highEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setHighGain(static_cast<float>(highEqSlider_.getValue()));
        }
    };

    // Spectre
    contentPane_.addAndMakeVisible(spectrumView_);

    // Mix Master section
    mixLabel_.setText("Mix Master", juce::dontSendNotification);
    mixLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(16.0F)));
    contentPane_.addAndMakeVisible(mixLabel_);

    renderMixBtn_.setButtonText("Render Mix");
    renderMixBtn_.onClick = [this] { renderMixToTrack(); };
    contentPane_.addAndMakeVisible(renderMixBtn_);

    mixWaveform_.setAudio(&mixTrackAudio_);
    contentPane_.addAndMakeVisible(mixWaveform_);

    cutMixBtn_.setButtonText("Cut Sel");
    cutMixBtn_.onClick = [this] { cutMixSelection(); };
    contentPane_.addAndMakeVisible(cutMixBtn_);

    trimMixBtn_.setButtonText("Trim Sel");
    trimMixBtn_.onClick = [this] { trimMixSelection(); };
    contentPane_.addAndMakeVisible(trimMixBtn_);

    exportMixBtn_.setButtonText("Export Mix WAV");
    exportMixBtn_.onClick = [this] { exportMix(); };
    contentPane_.addAndMakeVisible(exportMixBtn_);

    // I/O section
    ioLabel_.setText("Projet", juce::dontSendNotification);
    ioLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(16.0F)));
    contentPane_.addAndMakeVisible(ioLabel_);

    saveProjectBtn_.setButtonText("Sauvegarder");
    saveProjectBtn_.onClick = [this] { saveProject(); };
    contentPane_.addAndMakeVisible(saveProjectBtn_);

    loadProjectBtn_.setButtonText("Charger");
    loadProjectBtn_.onClick = [this] { loadProject(); };
    contentPane_.addAndMakeVisible(loadProjectBtn_);

    // Diagnostic
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
    setSize(400, 800);
    setAudioChannels(2, 2);

#if JUCE_ANDROID
    // Request a small buffer to engage AAudio low-latency path (~5 ms at 48 kHz).
    // Only applied when the default buffer is larger to avoid a needless device restart.
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        if (setup.bufferSize > 256) {
            setup.bufferSize = 256;
            deviceManager.setAudioDeviceSetup(setup, true);
        }
    }
#endif

    startTimerHz(10);
}

MainComponent::~MainComponent() {
    stopTimer();
    headphoneMonitor_.detach(deviceManager);  // avant shutdownAudio pour eviter callbacks tardifs
    shutdownAudio();
    juce::Logger::setCurrentLogger(nullptr);
}

// ─── Setup helpers ────────────────────────────────────────────────────────────

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

void MainComponent::setupFxPanel(std::size_t index) {
    auto& fx = fxPanels_[index];

    // Helper : configure un slider de parametre d'effet (compact, pas de textbox)
    const auto makeParamSlider = [this](juce::Slider& s, double lo, double hi, double init) {
        s.setRange(lo, hi, 0.0);
        s.setValue(init, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        contentPane_.addAndMakeVisible(s);
    };

    // Reverb
    fx.reverbBtn.setButtonText("REV");
    fx.reverbBtn.setClickingTogglesState(true);
    fx.reverbBtn.onClick = [this, index] {
        auto& f = fxPanels_[index];
        if (!f.reverb)
            return;
        const bool on = f.reverbBtn.getToggleState();
        f.reverb->setWet(on ? static_cast<float>(f.reverbWetSlider.getValue()) : 0.0F);
        f.reverb->setDry(on ? 0.7F : 1.0F);
    };
    contentPane_.addAndMakeVisible(fx.reverbBtn);

    makeParamSlider(fx.reverbWetSlider, 0.0, 1.0, 0.35);
    fx.reverbWetSlider.onValueChange = [this, index] {
        auto& f = fxPanels_[index];
        if (f.reverb && f.reverbBtn.getToggleState()) {
            f.reverb->setWet(static_cast<float>(f.reverbWetSlider.getValue()));
        }
    };

    // Delay
    fx.delayBtn.setButtonText("DLY");
    fx.delayBtn.setClickingTogglesState(true);
    fx.delayBtn.onClick = [this, index] {
        auto& f = fxPanels_[index];
        if (!f.delay)
            return;
        f.delay->setMix(f.delayBtn.getToggleState() ? 0.45F : 0.0F);
    };
    contentPane_.addAndMakeVisible(fx.delayBtn);

    makeParamSlider(fx.delayTimeSlider, 0.05, 1.0, 0.25);
    fx.delayTimeSlider.onValueChange = [this, index] {
        auto& f = fxPanels_[index];
        if (f.delay) {
            f.delay->setDelaySeconds(static_cast<float>(f.delayTimeSlider.getValue()));
        }
    };

    // Wah
    fx.wahBtn.setButtonText("WAH");
    fx.wahBtn.setClickingTogglesState(true);
    fx.wahBtn.onClick = [this, index] {
        auto& f = fxPanels_[index];
        if (!f.wah)
            return;
        f.wah->setMix(f.wahBtn.getToggleState() ? 1.0F : 0.0F);
    };
    contentPane_.addAndMakeVisible(fx.wahBtn);

    makeParamSlider(fx.wahRateSlider, 0.1, 5.0, 1.5);
    fx.wahRateSlider.onValueChange = [this, index] {
        auto& f = fxPanels_[index];
        if (f.wah) {
            f.wah->setRate(static_cast<float>(f.wahRateSlider.getValue()));
        }
    };

    // Chorus
    fx.chorusBtn.setButtonText("CHR");
    fx.chorusBtn.setClickingTogglesState(true);
    fx.chorusBtn.onClick = [this, index] {
        auto& f = fxPanels_[index];
        if (!f.chorus)
            return;
        f.chorus->setMix(f.chorusBtn.getToggleState()
                             ? static_cast<float>(f.chorusDepthSlider.getValue())
                             : 0.0F);
    };
    contentPane_.addAndMakeVisible(fx.chorusBtn);

    makeParamSlider(fx.chorusDepthSlider, 0.0, 1.0, 0.5);
    fx.chorusDepthSlider.onValueChange = [this, index] {
        auto& f = fxPanels_[index];
        if (f.chorus && f.chorusBtn.getToggleState()) {
            f.chorus->setMix(static_cast<float>(f.chorusDepthSlider.getValue()));
        }
    };
}

void MainComponent::setupEffects() {
    if (effectsSetup_)
        return;
    effectsSetup_ = true;

    for (std::size_t i = 0; i < kTrackCount; ++i) {
        auto* chain = engine_.effectsForTrack(i);
        if (!chain)
            continue;

        auto reverb = std::make_unique<voicelive::dsp::Reverb>();
        reverb->setWet(0.0F);
        reverb->setDry(1.0F);
        reverb->setRoomSize(0.7F);
        reverb->setDamping(0.5F);
        fxPanels_[i].reverb = reverb.get();
        chain->add(std::move(reverb));

        auto delay = std::make_unique<voicelive::dsp::Delay>();
        delay->setMix(0.0F);
        delay->setDelaySeconds(0.25F);
        delay->setFeedback(0.4F);
        fxPanels_[i].delay = delay.get();
        chain->add(std::move(delay));

        auto wah = std::make_unique<voicelive::dsp::Wah>();
        wah->setMix(0.0F);
        wah->setRate(1.5F);
        wah->setMinFrequency(400.0F);
        wah->setMaxFrequency(2000.0F);
        wah->setResonance(4.0F);
        fxPanels_[i].wah = wah.get();
        chain->add(std::move(wah));

        auto chorus = std::make_unique<voicelive::dsp::Chorus>();
        chorus->setMix(0.0F);
        chorus->setRate(1.5F);
        chorus->setDepth(0.5F);
        fxPanels_[i].chorus = chorus.get();
        chain->add(std::move(chorus));
    }
}

// ─── Audio ────────────────────────────────────────────────────────────────────

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    try {
        const auto blockSize = static_cast<std::size_t>(juce::jmax(1, samplesPerBlockExpected));
        const auto rate = static_cast<unsigned>(juce::jmax(1.0, sampleRate));
        const auto capacity = static_cast<std::size_t>(juce::jmax(1.0, sampleRate) * 30.0);

        if (const auto sr = SampleRate::create(rate); sr.ok()) {
            static_cast<void>(engine_.prepare(sr.value(), kTrackCount, capacity, blockSize));
            sampleRate_ = sampleRate;
            setupEffects();
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

    // Alimente la fenetre d'analyse pour l'accordeur et le spectre.
    if (numSamples < analysis_.size()) {
        const std::size_t keep = analysis_.size() - numSamples;
        std::copy(analysis_.begin() + static_cast<std::ptrdiff_t>(numSamples), analysis_.end(),
                  analysis_.begin());
        std::copy(monoIn_.begin(), monoIn_.begin() + static_cast<std::ptrdiff_t>(numSamples),
                  analysis_.begin() + static_cast<std::ptrdiff_t>(keep));
    }

    engine_.process(monoOut, monoIn);

    // Anti-larsen : couper le haut-parleur pendant l'enregistrement SAUF si un
    // casque est detecte (casque = pas de reinjection micro, monitoring OK).
    if (anyTrackRecording_.load(std::memory_order_acquire) && !headphoneMonitor_.isConnected()) {
        std::fill(monoOut.begin(), monoOut.end(), 0.0F);
    }

    channels::spreadToChannels(outputPtrs.data(), static_cast<std::size_t>(numChannels), monoOut);
}

void MainComponent::releaseResources() {}

// ─── Timer ────────────────────────────────────────────────────────────────────

void MainComponent::timerCallback() {
    if (analysis_.empty())
        return;

    // Re-sonder le peripherique de sortie avant de lire l'etat : sur Android,
    // le hotplug USB-C ne remonte pas toujours via AudioDeviceManager::ChangeListener.
    headphoneMonitor_.poll(deviceManager);
    headphoneLed_.setConnected(headphoneMonitor_.isConnected());

    checkPendingEdit();

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

    spectrumView_.update(std::span<const float>{analysis_.data(), analysis_.size()});

    for (std::size_t i = 0; i < kTrackCount; ++i) {
        if (const auto* proc = engine_.track(i); proc != nullptr) {
            waveforms_[i].setAudio(&proc->audio());
        }
        waveforms_[i].repaint();
    }

    if (mixTrackAudio_.length() > 0) {
        mixWaveform_.repaint();
    }

    updateDiagnostics();
}

void MainComponent::updateDiagnostics() {
    juce::String text;
    text << "VoiceLive Pro v2.0.0  |  JUCE " << juce::SystemStats::getJUCEVersion() << "\n";
    text << "Build : " << __DATE__ << " " << __TIME__ << "\n";

    const bool headphones = headphoneMonitor_.isConnected();
    text << "Casque : "
         << (headphones ? "DETECTE (monitoring actif)" : "NON detecte (speaker coupe pendant rec)")
         << "\n";

    if (anyTrackRecording_.load(std::memory_order_relaxed)) {
        if (headphones) {
            text << "[REC EN COURS] Monitoring casque actif\n";
        } else {
            text << "[REC EN COURS] Speaker coupe - brancher un casque USB-C pour le retour\n";
        }
    }
    if (pendingEdit_) {
        text << "[EDIT EN ATTENTE] piste " << static_cast<int>(pendingEdit_->trackIndex + 1)
             << "\n";
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

    const juce::File dir =
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    text << "Dossier exports : " << dir.getFullPathName() << "\n";

    for (std::size_t i = 0; i < diag.trackCount; ++i) {
        const auto* processor = engine_.track(i);
        if (processor == nullptr)
            continue;
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

// ─── Layout ───────────────────────────────────────────────────────────────────

void MainComponent::resized() {
    viewport_.setBounds(getLocalBounds());

    const int usableW = juce::jmax(300, viewport_.getMaximumVisibleWidth());
    const int pad = 12;

    constexpr int kTitleH = 34;
    constexpr int kTunerH = 34;
    constexpr int kRowH = 44;
    constexpr int kWaveH = 60;
    constexpr int kFxRowH = 40;
    constexpr int kEditRowH = 36;
    constexpr int kTransH = 48;
    constexpr int kEqLblH = 26;
    constexpr int kEqRowH = 44;
    constexpr int kSpecH = 90;
    constexpr int kMixLblH = 26;
    constexpr int kMixWaveH = 60;
    constexpr int kMixEditH = 36;
    constexpr int kIoLblH = 26;
    constexpr int kIoRowH = 44;
    constexpr int kCopyH = 40;
    constexpr int kDiagH = 200;
    constexpr int kGap = 10;

    // Hauteur d'un bloc piste : 2 rangees de controles + waveform + 2 rangees FX + rangee edition
    constexpr int kTrackH = kRowH + 6 + kRowH + 4 + kWaveH + 4 + kFxRowH + kFxRowH + kEditRowH;

    const int trackCount = static_cast<int>(kTrackCount);
    const int totalH = pad + kTitleH + kGap / 2 + kTunerH + kGap +
                       trackCount * (kTrackH + kGap / 2) + kGap + kTransH + kGap + kEqLblH + 4 +
                       3 * (kEqRowH + 4) + kGap + kSpecH + kGap + kMixLblH + 4 + kMixWaveH + 4 +
                       kMixEditH + kGap + kIoLblH + 4 + kIoRowH + kGap + kCopyH + kGap / 2 +
                       kDiagH + pad;

    contentPane_.setSize(usableW, totalH);
    auto area = contentPane_.getLocalBounds().reduced(pad);

    // Titre : [label] [LED casque 26px]
    {
        auto row = area.removeFromTop(kTitleH);
        headphoneLed_.setBounds(row.removeFromRight(26).reduced(3));
        titleLabel_.setBounds(row);
    }
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
        auto trackArea = area.removeFromTop(kTrackH);

        // Rangee 1 : label + boutons transport
        auto row1 = trackArea.removeFromTop(kRowH);
        const int labelW = 56;
        const int btnW = juce::jmax(42, (w - labelW) / 4);
        strips_[i].label.setBounds(row1.removeFromLeft(labelW));
        strips_[i].recordButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strips_[i].playButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strips_[i].stopButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strips_[i].clearButton.setBounds(row1.removeFromLeft(btnW).reduced(2));

        trackArea.removeFromTop(6);

        // Rangee 2 : gain + [MIX] [Mute]
        auto row2 = trackArea.removeFromTop(kRowH);
        strips_[i].muteButton.setBounds(row2.removeFromRight(64).reduced(2));
        includeBtns_[i].setBounds(row2.removeFromRight(56).reduced(2));
        strips_[i].gainSlider.setBounds(row2.reduced(2));

        trackArea.removeFromTop(4);

        // Waveform (avec selection interactive)
        waveforms_[i].setBounds(trackArea.removeFromTop(kWaveH));

        trackArea.removeFromTop(4);

        // FX Rangee 1 : [REV | wet] | [DLY | time]
        {
            auto fxRow = trackArea.removeFromTop(kFxRowH);
            const int halfW = fxRow.getWidth() / 2;
            const int fxBtnW = 52;
            auto left = fxRow.removeFromLeft(halfW);
            fxPanels_[i].reverbBtn.setBounds(left.removeFromLeft(fxBtnW).reduced(2));
            fxPanels_[i].reverbWetSlider.setBounds(left.reduced(2));
            fxPanels_[i].delayBtn.setBounds(fxRow.removeFromLeft(fxBtnW).reduced(2));
            fxPanels_[i].delayTimeSlider.setBounds(fxRow.reduced(2));
        }

        // FX Rangee 2 : [WAH | rate] | [CHR | depth]
        {
            auto fxRow = trackArea.removeFromTop(kFxRowH);
            const int halfW = fxRow.getWidth() / 2;
            const int fxBtnW = 52;
            auto left = fxRow.removeFromLeft(halfW);
            fxPanels_[i].wahBtn.setBounds(left.removeFromLeft(fxBtnW).reduced(2));
            fxPanels_[i].wahRateSlider.setBounds(left.reduced(2));
            fxPanels_[i].chorusBtn.setBounds(fxRow.removeFromLeft(fxBtnW).reduced(2));
            fxPanels_[i].chorusDepthSlider.setBounds(fxRow.reduced(2));
        }

        // Rangee edition : [Cut Sel] [Trim Sel] [Export N]
        {
            auto editRow = trackArea.removeFromTop(kEditRowH);
            const int third = editRow.getWidth() / 3;
            cutBtns_[i].setBounds(editRow.removeFromLeft(third).reduced(2));
            trimBtns_[i].setBounds(editRow.removeFromLeft(third).reduced(2));
            exportTrackBtns_[i].setBounds(editRow.reduced(2));
        }

        area.removeFromTop(kGap / 2);
    }

    // Transport
    area.removeFromTop(kGap);
    {
        auto row = area.removeFromTop(kTransH);
        metronomeButton_.setBounds(row.removeFromLeft(120).reduced(2));
        bpmSlider_.setBounds(row.reduced(4));
    }

    // EQ master
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

    // Spectre temps-reel
    area.removeFromTop(kGap);
    spectrumView_.setBounds(area.removeFromTop(kSpecH));

    // Mix Master section : [label] [Render Mix] / [waveform] / [Cut] [Trim] [Export]
    area.removeFromTop(kGap);
    {
        auto row = area.removeFromTop(kMixLblH);
        mixLabel_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
        renderMixBtn_.setBounds(row.reduced(2));
    }
    area.removeFromTop(4);
    mixWaveform_.setBounds(area.removeFromTop(kMixWaveH));
    area.removeFromTop(4);
    {
        auto editRow = area.removeFromTop(kMixEditH);
        const int third = editRow.getWidth() / 3;
        cutMixBtn_.setBounds(editRow.removeFromLeft(third).reduced(2));
        trimMixBtn_.setBounds(editRow.removeFromLeft(third).reduced(2));
        exportMixBtn_.setBounds(editRow.reduced(2));
    }

    // Projet section
    area.removeFromTop(kGap);
    ioLabel_.setBounds(area.removeFromTop(kIoLblH));
    area.removeFromTop(4);
    {
        auto row = area.removeFromTop(kIoRowH);
        saveProjectBtn_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
        loadProjectBtn_.setBounds(row.reduced(2));
    }

    // Diagnostic
    area.removeFromTop(kGap);
    copyButton_.setBounds(area.removeFromTop(kCopyH).reduced(2));
    area.removeFromTop(kGap / 2);
    diagView_.setBounds(area.removeFromTop(kDiagH));
}

// ─── Controle des pistes ──────────────────────────────────────────────────────

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

    if (action == EngineCommand::Action::Clear || action == EngineCommand::Action::Stop) {
        anyTrackRecording_.store(false, std::memory_order_release);
    }

    juce::Logger::writeToLog(juce::String("Clic piste ") +
                             juce::String(static_cast<int>(track) + 1) + " : " +
                             actionName(action) + (accepted ? "" : "  (FILE PLEINE)"));
}

// ─── Edition de piste ─────────────────────────────────────────────────────────

void MainComponent::cutSelection(std::size_t index) {
    auto& wf = waveforms_[index];
    if (!wf.hasSelection()) {
        juce::Logger::writeToLog("Cut : pas de selection sur piste " +
                                 juce::String(static_cast<int>(index) + 1));
        return;
    }
    if (pendingEdit_) {
        juce::Logger::writeToLog("Edit deja en attente, annule");
        return;
    }

    const auto* proc = engine_.track(index);
    if (!proc)
        return;
    const auto& audio = proc->audio();
    const std::size_t len = audio.loopLength();
    if (len == 0)
        return;

    auto [n0, n1] = wf.selectionNormalized();
    const std::size_t s0 = static_cast<std::size_t>(n0 * static_cast<float>(len));
    const std::size_t s1 = static_cast<std::size_t>(n1 * static_cast<float>(len));

    std::vector<float> edited;
    edited.reserve(len - (s1 - s0));
    for (std::size_t s = 0; s < s0; ++s)
        edited.push_back(audio.sampleAt(s));
    for (std::size_t s = s1; s < len; ++s)
        edited.push_back(audio.sampleAt(s));

    postCommand(EngineCommand::Action::Stop, index, 1.0F, false);
    pendingEdit_ = PendingEdit{index, std::move(edited)};
    wf.clearSelection();
    juce::Logger::writeToLog("Cut programme sur piste " +
                             juce::String(static_cast<int>(index) + 1));
}

void MainComponent::trimToSelection(std::size_t index) {
    auto& wf = waveforms_[index];
    if (!wf.hasSelection()) {
        juce::Logger::writeToLog("Trim : pas de selection sur piste " +
                                 juce::String(static_cast<int>(index) + 1));
        return;
    }
    if (pendingEdit_) {
        juce::Logger::writeToLog("Edit deja en attente, annule");
        return;
    }

    const auto* proc = engine_.track(index);
    if (!proc)
        return;
    const auto& audio = proc->audio();
    const std::size_t len = audio.loopLength();
    if (len == 0)
        return;

    auto [n0, n1] = wf.selectionNormalized();
    const std::size_t s0 = static_cast<std::size_t>(n0 * static_cast<float>(len));
    const std::size_t s1 = static_cast<std::size_t>(n1 * static_cast<float>(len));

    std::vector<float> edited;
    edited.reserve(s1 - s0);
    for (std::size_t s = s0; s < s1; ++s)
        edited.push_back(audio.sampleAt(s));

    postCommand(EngineCommand::Action::Stop, index, 1.0F, false);
    pendingEdit_ = PendingEdit{index, std::move(edited)};
    wf.clearSelection();
    juce::Logger::writeToLog("Trim programme sur piste " +
                             juce::String(static_cast<int>(index) + 1));
}

void MainComponent::checkPendingEdit() {
    if (!pendingEdit_)
        return;

    const auto& edit = *pendingEdit_;
    const auto* proc = engine_.track(edit.trackIndex);
    if (!proc) {
        pendingEdit_.reset();
        return;
    }

    const auto state = proc->track().state();
    if (state != TrackState::Stopped && state != TrackState::Empty)
        return;

    // La piste est arretee : process() n'accede plus a audio_, on peut editer.
    applyTrackEdit(edit.trackIndex, std::move(pendingEdit_->newSamples));
    pendingEdit_.reset();
}

void MainComponent::applyTrackEdit(std::size_t index, std::vector<float> newSamples) {
    wav::AudioData data;
    data.samples = std::move(newSamples);
    data.sampleRate = static_cast<unsigned>(sampleRate_);
    data.channels = 1;

    const auto status = engine_.importTrack(index, data);
    if (!status.ok()) {
        juce::Logger::writeToLog("Edit echoue piste " + juce::String(static_cast<int>(index) + 1));
    } else {
        postCommand(EngineCommand::Action::Play, index, 1.0F, false);
        juce::Logger::writeToLog("Edit applique piste " +
                                 juce::String(static_cast<int>(index) + 1) + " (" +
                                 juce::String(static_cast<int>(data.samples.size())) + " samples)");
    }
}

// ─── Mix Master ───────────────────────────────────────────────────────────────

void MainComponent::renderMixToTrack() {
    std::size_t mixLen = 0;
    for (std::size_t i = 0; i < kTrackCount; ++i) {
        if (!includeBtns_[i].getToggleState())
            continue;
        if (const auto* proc = engine_.track(i); proc != nullptr)
            mixLen = std::max(mixLen, proc->audio().loopLength());
    }
    if (mixLen == 0) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Render Mix",
            "Aucune piste incluse ou toutes vides.\n"
            "Activez le bouton MIX sur au moins une piste enregistree.");
        return;
    }

    std::vector<float> mixData(mixLen, 0.0F);
    for (std::size_t i = 0; i < kTrackCount; ++i) {
        if (!includeBtns_[i].getToggleState())
            continue;
        const auto* proc = engine_.track(i);
        if (!proc)
            continue;
        const auto& audio = proc->audio();
        const std::size_t trackLen = audio.loopLength();
        if (trackLen == 0 || strips_[i].muteButton.getToggleState())
            continue;
        const float gain = static_cast<float>(strips_[i].gainSlider.getValue());
        for (std::size_t s = 0; s < mixLen; ++s)
            mixData[s] += audio.sampleAt(s % trackLen) * gain;
    }
    for (float& s : mixData)
        s = juce::jlimit(-1.0F, 1.0F, s);

    mixTrackAudio_.prepare(mixLen);
    mixTrackAudio_.append(std::span<const float>{mixData.data(), mixData.size()});
    mixWaveform_.setAudio(&mixTrackAudio_);
    mixWaveform_.clearSelection();
    mixWaveform_.repaint();
    juce::Logger::writeToLog("Mix rendu : " + juce::String(static_cast<int>(mixLen)) + " samples");
}

void MainComponent::cutMixSelection() {
    if (!mixWaveform_.hasSelection()) {
        juce::Logger::writeToLog("Cut mix : pas de selection");
        return;
    }
    const std::size_t len = mixTrackAudio_.loopLength();
    if (len == 0)
        return;

    auto [n0, n1] = mixWaveform_.selectionNormalized();
    const std::size_t s0 = static_cast<std::size_t>(n0 * static_cast<float>(len));
    const std::size_t s1 = static_cast<std::size_t>(n1 * static_cast<float>(len));

    std::vector<float> edited;
    edited.reserve(len - (s1 - s0));
    for (std::size_t s = 0; s < s0; ++s)
        edited.push_back(mixTrackAudio_.sampleAt(s));
    for (std::size_t s = s1; s < len; ++s)
        edited.push_back(mixTrackAudio_.sampleAt(s));

    mixTrackAudio_.prepare(edited.size());
    mixTrackAudio_.append(std::span<const float>{edited.data(), edited.size()});
    mixWaveform_.clearSelection();
    juce::Logger::writeToLog("Cut mix applique");
}

void MainComponent::trimMixSelection() {
    if (!mixWaveform_.hasSelection()) {
        juce::Logger::writeToLog("Trim mix : pas de selection");
        return;
    }
    const std::size_t len = mixTrackAudio_.loopLength();
    if (len == 0)
        return;

    auto [n0, n1] = mixWaveform_.selectionNormalized();
    const std::size_t s0 = static_cast<std::size_t>(n0 * static_cast<float>(len));
    const std::size_t s1 = static_cast<std::size_t>(n1 * static_cast<float>(len));

    std::vector<float> edited;
    edited.reserve(s1 - s0);
    for (std::size_t s = s0; s < s1; ++s)
        edited.push_back(mixTrackAudio_.sampleAt(s));

    mixTrackAudio_.prepare(edited.size());
    mixTrackAudio_.append(std::span<const float>{edited.data(), edited.size()});
    mixWaveform_.clearSelection();
    juce::Logger::writeToLog("Trim mix applique");
}

// ─── Export / Sauvegarde ──────────────────────────────────────────────────────

void MainComponent::exportMix() {
    const std::size_t len = mixTrackAudio_.loopLength();
    if (len == 0) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Export Mix",
            "Le mix n'a pas encore ete rendu.\n"
            "Appuyez d'abord sur [Render Mix], editez si besoin,\npuis exportez.");
        return;
    }

    wav::AudioData data;
    data.sampleRate = static_cast<unsigned>(sampleRate_);
    data.channels = 1;
    data.samples.resize(len);
    for (std::size_t s = 0; s < len; ++s)
        data.samples[s] = mixTrackAudio_.sampleAt(s);

    const juce::File dir =
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    dir.createDirectory();
    const juce::String filename =
        "mix_" + juce::String(juce::Time::getCurrentTime().toMilliseconds()) + ".wav";
    const juce::File outFile = dir.getChildFile(filename);
    const std::string path = outFile.getFullPathName().toStdString();

    const auto status = wav::write(path, data);
    if (!status.ok()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Mix",
                                               "Echec de l'ecriture du fichier WAV.");
        juce::Logger::writeToLog("Export mix echoue");
    } else {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Export Mix",
            "Mix exporte avec succes !\n\n" + outFile.getFullPathName() +
                "\n\nAccessible depuis le gestionnaire de fichiers Android\n"
                "sous Android > data > [package] > files");
        juce::Logger::writeToLog("Mix exporte : " + juce::String(path.c_str()));
    }
}

void MainComponent::exportTrack(std::size_t index) {
    const auto* proc = engine_.track(index);
    if (!proc)
        return;
    const auto& audio = proc->audio();
    const std::size_t len = audio.loopLength();
    if (len == 0) {
        juce::Logger::writeToLog("Export piste " + juce::String(static_cast<int>(index) + 1) +
                                 " : vide");
        return;
    }

    wav::AudioData data;
    data.sampleRate = static_cast<unsigned>(sampleRate_);
    data.channels = 1;
    data.samples.resize(len);
    for (std::size_t s = 0; s < len; ++s) {
        data.samples[s] = audio.sampleAt(s);
    }

    const juce::File dir =
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    dir.createDirectory();
    const juce::String filename = "track" + juce::String(static_cast<int>(index) + 1) + "_" +
                                  juce::String(juce::Time::getCurrentTime().toMilliseconds()) +
                                  ".wav";
    const juce::File outFile = dir.getChildFile(filename);
    const std::string path = outFile.getFullPathName().toStdString();

    const auto status = wav::write(path, data);
    if (!status.ok()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Export Piste",
            "Echec de l'export de la piste " + juce::String(static_cast<int>(index) + 1));
        juce::Logger::writeToLog("Export piste " + juce::String(static_cast<int>(index) + 1) +
                                 " echoue");
    } else {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Export Piste",
            "Piste " + juce::String(static_cast<int>(index) + 1) + " exportee !\n\n" +
                outFile.getFullPathName() +
                "\n\nAccessible depuis le gestionnaire de fichiers Android\n"
                "sous Android > data > [package] > files");
        juce::Logger::writeToLog("Piste " + juce::String(static_cast<int>(index) + 1) +
                                 " exportee : " + juce::String(path.c_str()));
    }
}

void MainComponent::saveProject() {
    auto result = engine_.exportSettings();
    if (!result.ok()) {
        juce::Logger::writeToLog("Sauvegarde : echec lecture parametres moteur");
        return;
    }

    const juce::File dir =
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    dir.createDirectory();
    const juce::File outFile = dir.getChildFile("voicelive_project.vlp");
    const std::string path = outFile.getFullPathName().toStdString();

    const auto status = project_io::saveToFile(path, result.value());
    if (!status.ok()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Sauvegarde",
                                               "Echec de la sauvegarde du projet.");
        juce::Logger::writeToLog("Sauvegarde echouee");
    } else {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Sauvegarde",
            "Projet sauvegarde !\n\n" + outFile.getFullPathName());
        juce::Logger::writeToLog("Projet sauvegarde : " + juce::String(path.c_str()));
    }
}

void MainComponent::loadProject() {
    const juce::File dir =
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    const std::string path =
        dir.getChildFile("voicelive_project.vlp").getFullPathName().toStdString();

    auto result = project_io::loadFromFile(path);
    if (!result.ok()) {
        juce::Logger::writeToLog("Chargement echoue (fichier absent ou invalide) : " +
                                 juce::String(path.c_str()));
        return;
    }

    const auto status = engine_.applySettings(result.value());
    if (!status.ok()) {
        juce::Logger::writeToLog("Application des parametres echouee");
        return;
    }

    // Synchroniser les sliders UI avec les valeurs chargees.
    auto& project = result.value();
    bpmSlider_.setValue(project.transport().bpm().value(), juce::sendNotificationSync);
    for (std::size_t i = 0; i < kTrackCount && i < project.trackCount(); ++i) {
        if (auto* t = project.track(i)) {
            strips_[i].gainSlider.setValue(
                juce::jlimit(0.0, 2.0, static_cast<double>(t->gain().linear())),
                juce::sendNotificationSync);
            strips_[i].muteButton.setToggleState(t->isMuted(), juce::sendNotificationSync);
        }
    }
    juce::Logger::writeToLog("Projet charge depuis : " + juce::String(path.c_str()));
}
