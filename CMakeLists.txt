# chalkwalk-jambot -- the bots that play in a NINJAM room. JUCE-free, MIT.
#
# Written to work BOTH nested inside a parent project and standalone, so that
# `cmake -B build && ctest` works from this directory with nothing else on the
# machine. That is not a convenience: a library that only builds inside its
# parent has not actually been extracted, and the standalone build is the test
# of the boundary.

cmake_minimum_required(VERSION 3.22)
project(chalkwalk_jambot VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# The three shared libraries the bots are built out of.
#
# `chalkwalk-music` is the theory -- keys, chords, charts, roman numerals.
# `chalkwalk-ninjam` is the wire protocol and the room conventions the bots
# read out of chat. `chalkwalk-dsp` is the synthesis primitives under the
# voices, and its `measure` target is the instruments the suite asserts with.
#
# Submodules rather than a vendored copy, and the same OVERRIDE mechanism
# Antiphon uses, so a change to one can be tried here before it is pushed.
# ---------------------------------------------------------------------------
include(cmake/ChalkwalkLibrary.cmake)
chalkwalk_add_library(music libs/music)
chalkwalk_add_library(dsp libs/dsp)
chalkwalk_add_library(ninjam libs/ninjam)

add_library(chalkwalk_jambot STATIC
    src/BandPatch.cpp
    src/BotAddress.cpp
    src/BotAnswer.cpp
    src/BotBand.cpp
    src/BotChat.cpp
    src/BotLanguage.cpp
    src/BotNames.cpp
    src/Conductor.cpp
    src/InputCheck.cpp
    src/PracticeBot.cpp
    src/TutorBot.cpp)
add_library(chalkwalk::jambot ALIAS chalkwalk_jambot)

# The headers are in src/ beside their sources rather than under
# include/chalkwalk/jambot/, and the namespaces are still `BotBand`,
# `PracticeBot` and the rest rather than `chalkwalk::jambot`. Both are
# deliberate, and both are temporary: this repository's first job was to carry
# its history across intact, and renaming several hundred call sites in the
# same change would have made the move and the rename impossible to review
# separately. See README.md.
target_include_directories(chalkwalk_jambot
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

target_link_libraries(chalkwalk_jambot
    PUBLIC chalkwalk::music chalkwalk::ninjam chalkwalk::dsp)

target_compile_features(chalkwalk_jambot PUBLIC cxx_std_17)

# A plugin is a shared module, and a static library linked into one must be
# position-independent or the link fails outright on Linux. Its siblings all
# set this; it only showed here when Antiphon first linked the band into its
# VST3 and CLAP builds.
set_target_properties(chalkwalk_jambot PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(chalkwalk_jambot PRIVATE /W4)
else()
    target_compile_options(chalkwalk_jambot
        PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# Tests only when built on its own, or when a parent explicitly asks. A parent
# that pulls this in for the bots should not have to build Catch2.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHALKWALK_JAMBOT_TESTS_DEFAULT ON)
else()
    set(CHALKWALK_JAMBOT_TESTS_DEFAULT OFF)
endif()
option(CHALKWALK_JAMBOT_TESTS "Build chalkwalk-jambot tests"
       ${CHALKWALK_JAMBOT_TESTS_DEFAULT})

if(CHALKWALK_JAMBOT_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
