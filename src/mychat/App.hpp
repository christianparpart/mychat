// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>
#include <mychat/Config.hpp>

#include <memory>

namespace mychat
{

/// @brief Main application orchestrator that wires all components together.
class App
{
  public:
    /// @brief Constructs the application with the given configuration.
    /// @param config The application configuration.
    explicit App(AppConfig config);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// @brief Initializes all components (model loading, MCP servers, audio).
    /// @return Success or an error.
    [[nodiscard]] auto initialize() -> VoidResult;

    /// @brief Runs the main interactive loop.
    /// @return Exit code (0 for success).
    [[nodiscard]] auto run() -> int;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
