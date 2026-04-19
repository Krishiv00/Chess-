#include "GUI/Launcher.hpp"

Launcher::Launcher(const Config& config) {
    m_Window.create(
        sf::VideoMode(sf::Vector2u(config.WindowSize * config.AspectRatio, config.WindowSize)),
        config.WindowTitle,
        config.WindowStyle,
        sf::State::Windowed
    );

    m_Window.clear();
    m_Window.display();

    m_Window.setFramerateLimit(config.Fps);

    m_LastWindowSize = m_Window.getSize();

    onResize();
}

void Launcher::Update() {
    const float deltaTime = std::min(1.f / 60.f, m_Clock.restart().asSeconds());

    m_Application.Update(deltaTime);
}

void Launcher::Render() const {
    m_Window.clear();

    const sf::Vector2i mousePosition = sf::Mouse::getPosition(m_Window);

    m_Application.Render(m_Window, mousePosition);

    m_Window.display();
}

void Launcher::HandleEvents() {
    while (const std::optional<sf::Event> event = m_Window.pollEvent()) {
        onEvent(event.value());
    }
}

bool Launcher::LoadResources(const std::filesystem::path& root) {
    if (!m_Application.LoadResources(root)) [[unlikely]] {
        m_Window.close();

        return false;
    }

    return true;
}

void Launcher::onResize() {
    m_Application.SetTargetSize(m_Window.getSize());
    m_Clock.restart();
}

void Launcher::handleWindowResize(sf::Vector2u size) {
    const float aspect = static_cast<float>(m_LastWindowSize.x) / static_cast<float>(m_LastWindowSize.y);

    const sf::Vector2u desktop = sf::VideoMode::getDesktopMode().size;

    const bool widthChanged = size.x != m_LastWindowSize.x;
    const bool heightChanged = size.y != m_LastWindowSize.y;

    if (widthChanged && !heightChanged) {
        size.y = static_cast<unsigned int>(size.x / aspect);
    } else if (heightChanged && !widthChanged) {
        size.x = static_cast<unsigned int>(size.y * aspect);
    } else {
        if (size.x > size.y * aspect)
            size.y = static_cast<unsigned int>(size.x / aspect);
        else
            size.x = static_cast<unsigned int>(size.y * aspect);
    }

    if (size.x > desktop.x) {
        size.x = desktop.x;
        size.y = static_cast<unsigned int>(size.x / aspect);
        m_Window.setPosition({m_Window.getPosition().x, 0});
    }

    if (size.y > desktop.y) {
        size.y = desktop.y;
        size.x = static_cast<unsigned int>(size.y * aspect);
        m_Window.setPosition({0, m_Window.getPosition().y});
    }

    m_Window.setSize(size);
    m_Window.setView(sf::View(sf::FloatRect(sf::Vector2f(), sf::Vector2f(size))));

    m_LastWindowSize = size;

    onResize();
}

void Launcher::onEvent(const sf::Event& event) {
    if (event.is<sf::Event::Closed>()) {
        m_Window.close();
    }

    else if (event.is<sf::Event::MouseEntered>()) {
        if (auto handCursor = sf::Cursor::createFromSystem(sf::Cursor::Type::Hand)) {
            m_Window.setMouseCursor(handCursor.value());
        }
    }

    else if (event.is<sf::Event::MouseLeft>()) {
        m_Application.HandleMouseLeftWindow();
    }

    else if (const auto* resize = event.getIf<sf::Event::Resized>()) {
        handleWindowResize(resize->size);
    }

    else if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        if (key->scancode == sf::Keyboard::Scancode::Escape) {
            m_Window.close();
        } else {
            m_Application.HandleKeyPressed(key->scancode);
        }
    }

    else if (const auto* button = event.getIf<sf::Event::MouseButtonPressed>()) {
        m_Application.HandleMouseButtonPressed(button->button, button->position);
    }

    else if (const auto* button = event.getIf<sf::Event::MouseButtonReleased>()) {
        m_Application.HandleMouseButtonReleased(button->button, button->position);
    }

    else if (const auto* move = event.getIf<sf::Event::MouseMoved>()) {
        m_Application.HandleMouseMoved(move->position);
    }
}