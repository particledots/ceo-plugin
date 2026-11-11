# The Ceo plugin wrapper: what Logic sees (AU / VST3) and the controls.
# The sound itself lives in ../engine.

juce_add_plugin(Ceo
    COMPANY_NAME                "particledots"
    BUNDLE_ID                   com.particledots.ceo
    PLUGIN_MANUFACTURER_CODE    Pdot
    PLUGIN_CODE                 Ceo1
    FORMATS                     AU VST3
    PRODUCT_NAME                "Ceo"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect
)

target_sources(Ceo PRIVATE
    Source/Parameters.h
    Source/Parameters.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/CeoLookAndFeel.h
    Source/CeoLookAndFeel.cpp
    Source/CeoControls.h
    Source/CeoControls.cpp
    Source/StreamDisplay.h
    Source/StreamDisplay.cpp
    Source/CeoStyle.h
    Source/CeoArt.h
    Source/CeoArt.cpp
    Source/ReduceMotion.cpp
)

# Option C "Sparks" look (milestone 13): the fonts are built into the plugin (SIL Open Font License, see Fonts/OFL.txt).
juce_add_binary_data(CeoFonts SOURCES
    Fonts/Jost-Regular.ttf
    Fonts/Jost-Medium.ttf
    Fonts/Jost-Italic.ttf
    Fonts/CormorantGaramond-SemiBold-Latin.ttf
    Fonts/EBGaramond-Medium-Greek.ttf
)

# ReduceMotion.cpp asks macOS whether "Reduce motion" is switched on, so it is compiled as Objective-C++ there.
if(APPLE)
    set_source_files_properties(Source/ReduceMotion.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

target_compile_definitions(Ceo PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Ceo
    PRIVATE
        ceo_engine
        CeoFonts
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
