add_library(mychat_agent
    AgentLoop.cpp
)
add_library(mychat::agent ALIAS mychat_agent)

target_link_libraries(mychat_agent PUBLIC
    mychat::core
    mychat::llm
    mychat::mcp
)

mychat_pedantic_compiler(mychat_agent)
mychat_enable_sanitizers(mychat_agent)
