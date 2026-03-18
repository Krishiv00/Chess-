#pragma once

#include <filesystem>

#include "SFML/Graphics.hpp"

#include "GUI/Application.hpp"

class Launcher final {
public:
    struct Config {
        unsigned int WindowSize;
        float AspectRatio;
        unsigned int Fps;
        const char* WindowTitle;
        unsigned int WindowStyle;
    };

private:
    void onResize();
    void handleWindowResize(sf::Vector2u size);
    void onEvent(const sf::Event& event);

    mutable sf::RenderWindow m_Window;
    sf::Vector2u m_LastWindowSize;

    sf::Clock m_Clock;

    Application m_Application;

public:
    Launcher(const Config& config);

    [[nodiscard]]
    bool LoadResources(const std::filesystem::path& root);

    void HandleEvents();
    void Update();
    void Render() const;

    [[nodiscard]]
    inline bool isRunning() const noexcept {
        return m_Window.isOpen();
    }
};