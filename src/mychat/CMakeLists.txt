add_library(mychat_app
    Config.cpp
    App.cpp
)
add_library(mychat::app ALIAS mychat_app)

target_link_libraries(mychat_app PUBLIC
    mychat::core
    mychat::llm
    mychat::audio
    mychat::mcp
    mychat::agent
    mychat::tui
)

mychat_pedantic_compiler(mychat_app)
mychat_enable_sanitizers(mychat_app)

add_executable(mychat_bin
    Main.cpp
)
set_target_properties(mychat_bin PROPERTIES OUTPUT_NAME mychat)

target_link_libraries(mychat_bin PRIVATE
    mychat::app
    CLI11::CLI11
)

mychat_pedantic_compiler(mychat_bin)
mychat_enable_sanitizers(mychat_bin)
