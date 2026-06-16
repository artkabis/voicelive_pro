# SPDX-License-Identifier: MIT
#
# Barrière de qualité n°2 : sanitizers runtime (ASan/UBSan) activables en CI et
# en debug pour attraper les comportements indéfinis, débordements et fuites
# avant qu'ils n'atteignent un utilisateur.

function(voicelive_enable_sanitizers target)
    if(NOT VOICELIVE_ENABLE_SANITIZERS)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(flags -fsanitize=address,undefined -fno-sanitize-recover=all
                  -fno-omit-frame-pointer)
        target_compile_options(${target} PRIVATE ${flags})
        target_link_options(${target} PRIVATE ${flags})
    endif()
endfunction()
