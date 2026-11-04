# The Ceo audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins (Lubadh-, Morphagene-, Mimeophon-style) can reuse this folder.

add_library(ceo_engine STATIC
    src/Engine.cpp
    src/Window.cpp
    src/Fft.cpp
    src/OnsetDetector.cpp
    src/Effects.cpp
    src/Wavetable.cpp
)

target_include_directories(ceo_engine PUBLIC include)
target_compile_features(ceo_engine PUBLIC cxx_std_17)

if(CEO_BUILD_TESTS)
    add_executable(ceo_engine_test tests/engine_test.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(ceo_engine_test PRIVATE ceo_engine Threads::Threads)
    add_test(NAME engine COMMAND ceo_engine_test)
endif()
