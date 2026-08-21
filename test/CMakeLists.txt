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
    PracticeBotTests.cpp)

target_link_libraries(chalkwalk_jambot_tests
    PRIVATE chalkwalk::jambot chalkwalk::dsp::measure Catch2::Catch2WithMain)

# Where the corpora are. Passed in rather than discovered: the suites used to
# walk eight directories up from the executable looking for test/fixtures, and
# a corpus test that cannot find its corpus skips itself, which reads exactly
# like passing.
target_compile_definitions(chalkwalk_jambot_tests
    PRIVATE JAMBOT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

if(MSVC)
    target_compile_options(chalkwalk_jambot_tests PRIVATE /W4)
else()
    target_compile_options(chalkwalk_jambot_tests
        PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

add_test(NAME chalkwalk_jambot COMMAND chalkwalk_jambot_tests)
