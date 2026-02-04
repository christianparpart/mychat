add_library(mychat_mcp
    JsonRpc.cpp
    StdioTransport.cpp
    McpClient.cpp
    ServerManager.cpp
)
add_library(mychat::mcp ALIAS mychat_mcp)

target_link_libraries(mychat_mcp PUBLIC
    mychat::core
)

mychat_pedantic_compiler(mychat_mcp)
mychat_enable_sanitizers(mychat_mcp)
