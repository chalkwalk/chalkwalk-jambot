# Catch2, guarded so a parent that already provides it is reused rather than
# fetching a second copy.
if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1)
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(chalkwalk_jambot_tests
    BandPatchTests.cpp
    BandPlayStateTests.cpp
    BotAddressTests.cpp
    BotAnswerTests.cpp
    BotBandTests.cpp
    BotChatTests.cpp
    BotDspTests.cpp
    BotLanguageTests.cpp
    BotNamesTests.cpp
    ConductorTests.cpp
    IntervalPumpTests.cpp
    FormTests.cpp
    InputCheckTests.cpp
    PracticeBotTests.cpp
    TutorBotTests.cpp)

target_link_libraries(chalkwalk_jambot_tests
    PRIVATE chalkwalk::jambot chalkwalk::dsp::measure Catch2::Catch2WithMain)

# Where the corpora are. Passed in rather than discovered: the suites used to
# walk eight directories up from the executable looking for test/fixtures, and
# a corpus test that cannot find its corpus skips itself, which reads exactly
# like passing.
target_compile_definitions(chalkwalk_jambot_tests
    PRIVATE JAMBOT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# MSVC does not speak GCC's warning flags, and passing them is a hard error
# rather than a warning -- so an unguarded list breaks the Windows build at the
# command line, before a single file is compiled.
if(MSVC)
    # /Zc:preprocessor -- MSVC's traditional preprocessor treats __VA_ARGS__ as
    # a single token, so the argument-count dispatch in the test harness picks
    # the wrong macro and `expect(cond, msg)` becomes `CHECK((cond, msg))` --
    # the comma operator, yielding the message. chalkwalk-ninjam hit this first
    # and its note is worth repeating: it compiled on gcc and clang and failed
    # only here, which is the whole argument for building on three.
    target_compile_options(chalkwalk_jambot_tests PRIVATE /W4 /Zc:preprocessor)
else()
    target_compile_options(chalkwalk_jambot_tests
        PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

add_test(NAME chalkwalk_jambot COMMAND chalkwalk_jambot_tests)
