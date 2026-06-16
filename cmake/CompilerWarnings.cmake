# SPDX-License-Identifier: MIT
#
# Barrière de qualité n°1 : un mur d'avertissements, traités comme des erreurs.
# Aucun code ne peut être mergé s'il déclenche le moindre warning.

function(voicelive_set_target_warnings target)
    set(gcc_clang_warnings
        -Wall
        -Wextra            # avertissements raisonnables au-delà de -Wall
        -Wpedantic         # respect strict du standard ISO C++
        -Wshadow           # variable qui en masque une autre
        -Wconversion       # conversions implicites qui perdent de l'info
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast   # interdit les casts à la C
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )

    if(VOICELIVE_WARNINGS_AS_ERRORS)
        list(APPEND gcc_clang_warnings -Werror)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE ${gcc_clang_warnings})
    elseif(MSVC)
        set(msvc_warnings /W4 /permissive-)
        if(VOICELIVE_WARNINGS_AS_ERRORS)
            list(APPEND msvc_warnings /WX)
        endif()
        target_compile_options(${target} PRIVATE ${msvc_warnings})
    endif()
endfunction()
