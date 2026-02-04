add_library(mychat_core
    Log.cpp
)
add_library(mychat::core ALIAS mychat_core)

target_include_directories(mychat_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(mychat_core PUBLIC
    nlohmann_json::nlohmann_json
)

mychat_pedantic_compiler(mychat_core)
mychat_enable_sanitizers(mychat_core)
