add_executable(mychat_tests
    Main.cpp
    ConfigTests.cpp
    ChatSessionTests.cpp
    JsonRpcTests.cpp
    McpClientTests.cpp
    StdioTransportTests.cpp
    AgentLoopTests.cpp
    TuiTests.cpp
)

target_link_libraries(mychat_tests PRIVATE
    mychat::app
    Catch2::Catch2
)

mychat_pedantic_compiler(mychat_tests)
mychat_enable_sanitizers(mychat_tests)

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(mychat_tests)
