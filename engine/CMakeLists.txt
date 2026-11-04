# The Ceo audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins (Lubadh-, Morphagene-, Mimeophon-style) can reuse this folder.

add_library(ceo_engine STATIC
    src/Engine.cpp
    src/Window.cpp
    src/Fft.cpp
    src/OnsetDetector.cpp
    src/Effects.cpp
    src/Wavetable.cpp
    src/LayerAudio.cpp
    src/LayerLibrary.cpp
)

target_include_directories(ceo_engine PUBLIC include)
target_compile_features(ceo_engine PUBLIC cxx_std_17)

if(CEO_BUILD_TESTS)
    add_executable(ceo_engine_test tests/engine_test.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(ceo_engine_test PRIVATE ceo_engine Threads::Threads)
    add_test(NAME engine COMMAND ceo_engine_test)

    add_executable(ceo_library_test tests/library_test.cpp)
    target_link_libraries(ceo_library_test PRIVATE ceo_engine Threads::Threads)
    target_compile_definitions(ceo_library_test PRIVATE
        CEO_ENGINE_CPP="${CMAKE_CURRENT_SOURCE_DIR}/src/Engine.cpp"
        CEO_ENGINE_H="${CMAKE_CURRENT_SOURCE_DIR}/include/ceo/Engine.h")
    add_test(NAME library COMMAND ceo_library_test)
endif()
