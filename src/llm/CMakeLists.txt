add_library(mychat_llm
    ChatSession.cpp
    LlmEngine.cpp
)
add_library(mychat::llm ALIAS mychat_llm)

target_link_libraries(mychat_llm PUBLIC
    mychat::core
    llama
)

mychat_pedantic_compiler(mychat_llm)
mychat_enable_sanitizers(mychat_llm)
