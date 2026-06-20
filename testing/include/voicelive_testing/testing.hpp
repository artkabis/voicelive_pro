// SPDX-License-Identifier: MIT
//
// Micro-framework de test, header-only, sans dépendance.
//
// Pourquoi pas Catch2/GoogleTest tout de suite ? Pour garantir un build vert
// hors-ligne et zéro surface d'attaque externe au démarrage. L'API
// (TEST/CHECK/REQUIRE) est volontairement compatible Catch2 : la migration se
// fera par simple remplacement de ce header, sans toucher aux tests.
#pragma once

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace vltest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> instance;
    return instance;
}

/// Compteur d'échecs du test courant (remis à zéro avant chaque cas).
inline int& currentFailures() {
    static int failures = 0;
    return failures;
}

/// Levé par REQUIRE pour interrompre immédiatement le cas courant.
struct AbortTest {};

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline int runAll() {
    std::size_t passed = 0;
    for (const auto& testCase : registry()) {
        currentFailures() = 0;
        try {
            testCase.fn();
        } catch (const AbortTest&) {
            // REQUIRE a déjà comptabilisé l'échec.
        } catch (const std::exception& ex) {
            ++currentFailures();
            std::cout << "  exception non attendue : " << ex.what() << "\n";
        }
        if (currentFailures() == 0) {
            ++passed;
            std::cout << "[  ok  ] " << testCase.name << "\n";
        } else {
            std::cout << "[ FAIL ] " << testCase.name << "\n";
        }
    }
    std::cout << "\n" << passed << "/" << registry().size() << " tests passés\n";
    return passed == registry().size() ? 0 : 1;
}

}  // namespace vltest

#define VL_CONCAT_INNER(a, b) a##b
#define VL_CONCAT(a, b) VL_CONCAT_INNER(a, b)

/// Déclare et enregistre un cas de test.
#define TEST(suite, name)                                               \
    static void VL_CONCAT(vl_test_fn_, __LINE__)();                     \
    static const ::vltest::Registrar VL_CONCAT(vl_test_reg_, __LINE__){ \
        #suite "." #name, &VL_CONCAT(vl_test_fn_, __LINE__)};           \
    static void VL_CONCAT(vl_test_fn_, __LINE__)()

/// Vérifie une condition ; en cas d'échec, comptabilise et continue le test.
#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            ++::vltest::currentFailures();                                                       \
            std::cout << "  CHECK échec : " #cond "  (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                                        \
    } while (false)

/// Comme CHECK mais interrompt le test (préconditions indispensables).
#define REQUIRE(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++::vltest::currentFailures();                                                         \
            std::cout << "  REQUIRE échec : " #cond "  (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            throw ::vltest::AbortTest{};                                                           \
        }                                                                                          \
    } while (false)

/// Égalité approchée pour les comparaisons flottantes (DSP).
#define CHECK_NEAR(actual, expected, tol)                                                       \
    do {                                                                                        \
        const double vl_a = static_cast<double>(actual);                                        \
        const double vl_e = static_cast<double>(expected);                                      \
        if (!(std::abs(vl_a - vl_e) <= static_cast<double>(tol))) {                                                \
            ++::vltest::currentFailures();                                                      \
            std::cout << "  CHECK_NEAR échec : " #actual " ≈ " #expected " (" << vl_a << " vs " \
                      << vl_e << ", tol " << (tol) << ")  (" << __FILE__ << ":" << __LINE__     \
                      << ")\n";                                                                 \
        }                                                                                       \
    } while (false)
