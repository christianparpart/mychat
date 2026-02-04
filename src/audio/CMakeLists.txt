add_library(mychat_audio
    MiniaudioImpl.cpp
    AudioCapture.cpp
    AudioPlayback.cpp
    VoiceActivityDetector.cpp
    Transcriber.cpp
    AudioPipeline.cpp
    TtsSpeaker.cpp
)
add_library(mychat::audio ALIAS mychat_audio)

target_link_libraries(mychat_audio PUBLIC
    mychat::core
    whisper
    miniaudio
    piper
)

target_compile_definitions(mychat_audio PRIVATE
    PIPER_ESPEAK_DATA_DIR="${PIPER_ESPEAK_DATA_DIR}"
)

mychat_pedantic_compiler(mychat_audio)
mychat_enable_sanitizers(mychat_audio)
