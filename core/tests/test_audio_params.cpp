// SPDX-License-Identifier: MIT
#include "testing.hpp"
#include "voicelive/core/AudioParams.hpp"

using voicelive::core::ErrorCode;
using voicelive::core::Gain;
using voicelive::core::SampleRate;

TEST(SampleRate, accepte_valeurs_valides) {
    const auto sr = SampleRate::create(SampleRate::kStudio);
    REQUIRE(sr.ok());
    CHECK(sr.value().hz() == 48'000U);
}

TEST(SampleRate, refuse_zero) {
    const auto sr = SampleRate::create(0);
    CHECK(!sr.ok());
    CHECK(sr.error().code == ErrorCode::OutOfRange);
}

TEST(Gain, unite_et_silence) {
    CHECK(Gain::unity().linear() == 1.0F);
    CHECK(Gain::silence().isSilent());
}

TEST(Gain, clampe_les_bornes) {
    CHECK(Gain::fromLinear(-5.0F).isSilent());                     // sous 0 → silence
    CHECK(Gain::fromLinear(100.0F).linear() == Gain::kMaxLinear);  // au-dessus → max
    CHECK(Gain::fromLinear(2.0F).linear() == 2.0F);                // dans les bornes → inchangé
}
