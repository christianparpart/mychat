add_library(mychat_tui
    Box.cpp
    Dialog.cpp
    InputField.cpp
    List.cpp
    LogPanel.cpp
    MarkdownRenderer.cpp
    Sixel.cpp
    Spinner.cpp
    StatusBar.cpp
    Terminal.cpp
    TerminalInput.cpp
    TerminalOutput.cpp
    Text.cpp
    Theme.cpp
    VtParser.cpp
)
add_library(mychat::tui ALIAS mychat_tui)

target_include_directories(mychat_tui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(mychat_tui PUBLIC
    mychat::core
    unicode::unicode
)

target_compile_features(mychat_tui PUBLIC cxx_std_23)

mychat_pedantic_compiler(mychat_tui)
mychat_enable_sanitizers(mychat_tui)
