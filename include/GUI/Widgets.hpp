#pragma once

#include <functional>
#include <string>
#include <cstdint>
#include <chrono>
#include <array>
#include <vector>
#include <cmath>

#include "SFML/Graphics.hpp"

namespace Utils {
    [[nodiscard]]
    inline float ExponentiallyMoveTo(float from, float to, float speed, float snapThreshold = 0.002f) noexcept {
        const float diff = to - from;
        return std::fabs(diff) < snapThreshold ? to : (from + diff * (1.f - std::exp(-speed)));
    }

    [[nodiscard]]
    constexpr inline sf::Color ConvertAlpha(sf::Color color, float factor) noexcept {
        color.a *= factor;
        return color;
    }

    inline void RenderQuad(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size
    ) {
        const sf::Vertex vertices[] = {
            sf::Vertex(position, color),
            sf::Vertex(sf::Vector2f(position.x + size.x, position.y), color),
            sf::Vertex(sf::Vector2f(position.x, position.y + size.y), color),
            sf::Vertex(position + size, color)
        };

        target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip);
    }

    inline void RenderRoundedQuad(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size,
        float roundness
    ) {
        const float radius = std::min(size.x, size.y) * 0.5f * roundness;

        const sf::Vector2f tl(position.x + radius, position.y + radius);
        const sf::Vector2f tr(position.x + size.x - radius, position.y + radius);
        const sf::Vector2f br(position.x + size.x - radius, position.y + size.y - radius);
        const sf::Vector2f bl(position.x + radius, position.y + size.y - radius);

        const int segments = std::max(4, static_cast<int>(8 + roundness * 8));

        sf::VertexArray vertices{sf::PrimitiveType::TriangleFan};

        vertices.append(sf::Vertex(
            {position.x + size.x * 0.5f, position.y + size.y * 0.5f},
            color
        ));

        auto addCorner = [&](sf::Vector2f center, float startAngle) {
            for (int i = 0; i <= segments; ++i) {
                float angle = startAngle + (i / static_cast<float>(segments)) * 90.f;
                float rad = angle * 3.14159265f / 180.f;
                vertices.append(sf::Vertex(
                    {center.x + std::cos(rad) * radius,
                    center.y + std::sin(rad) * radius},
                    color
                ));
            }
            };

        addCorner(tr, -90.f);
        addCorner(br, 0.f);
        addCorner(bl, 90.f);
        addCorner(tl, 180.f);

        vertices.append(vertices[1]);

        target.draw(vertices);
    }

    inline void RenderFrame(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size,
        float thicknessFactor
    ) {
        const float thickness = std::min(size.x, size.y) * thicknessFactor;

        const float left = position.x;
        const float right = position.x + size.x;
        const float top = position.y;
        const float bottom = position.y + size.y;

        const float innerLeft = left + thickness;
        const float innerRight = right - thickness;
        const float innerTop = top + thickness;
        const float innerBottom = bottom - thickness;

        sf::Vertex vertices[] = {
            sf::Vertex(sf::Vector2f(left, top), color),
            sf::Vertex(sf::Vector2f(innerLeft, innerTop), color),

            sf::Vertex(sf::Vector2f(right, top), color),
            sf::Vertex(sf::Vector2f(innerRight, innerTop), color),

            sf::Vertex(sf::Vector2f(right, bottom), color),
            sf::Vertex(sf::Vector2f(innerRight, innerBottom), color),

            sf::Vertex(sf::Vector2f(left, bottom), color),
            sf::Vertex(sf::Vector2f(innerLeft, innerBottom), color),

            sf::Vertex(sf::Vector2f(left, top), color),
            sf::Vertex(sf::Vector2f(innerLeft, innerTop), color)
        };

        target.draw(vertices, 10, sf::PrimitiveType::TriangleStrip);
    }
}

struct PieceMoveAnim {
    float Timer{0.f};
    uint8_t Start{0}, End{0};

    PieceMoveAnim() = default;
    PieceMoveAnim(uint8_t start, uint8_t end) : Start(start), End(end), Timer(1.f) {}
};

struct Popup {
    float Timer{0.f};
    std::string Info;

    Popup() = default;

    Popup(const std::string& info) : Info(info), Timer(2.f) {
        for (char& c : Info) c = std::toupper(c);
    }

    Popup(const std::string& info, float timer) : Info(info), Timer(timer) {
        for (char& c : Info) c = std::toupper(c);
    }

    [[nodiscard]]
    inline bool isActive() const noexcept {
        return Timer;
    }
};

struct Arrow {
    static constexpr inline const uint8_t NullPos = 64;

    uint8_t Start{NullPos};
    uint8_t End{NullPos};

    [[nodiscard]]
    inline operator bool() const noexcept {
        return Start != NullPos && End != NullPos && Start != End;
    }
};

class ParticleSystem final {
public:
    struct Particle {
        enum class Shapes : uint8_t {
            Square, Diamond, Circle
        };

        sf::Vector2f Position;
        sf::Vector2f Velocity;
        float Rotation{0.f};
        float RotationSpeed{0.f};
        float Size{0.f};
        float Life{1.f};
        uint8_t ColorIndex{0};
        Shapes Shape{Shapes::Square};
    };

private:
    std::vector<Particle> m_Particles;

public:
    void RemoveAll();

    void Spawn(std::size_t count, sf::FloatRect area);

    void Update(float deltaTime);

    void Render(
        sf::RenderTarget& target, const std::array<sf::Color, 5>& colors, float opacityFactor
    ) const;

    [[nodiscard]]
    inline const std::vector<Particle>& GetParticles() const noexcept {
        return m_Particles;
    }
};

class EvaluationBar final {
private:
    float m_CurrentEvaluation{0.5f};
    float m_LatestEvaluation{0.5f};

public:
    void SetEval(float eval);

    void Update(float deltaTime);

    void Render(
        sf::RenderTarget& target, sf::Vector2f position, sf::Vector2f size,
        bool flipped, const std::array<sf::Color, 2>& colors
    ) const;
};

class ButtonPanel final {
public:
    struct Button {
        static constexpr inline const unsigned int TooltipThreshold = 350; // ms

        float Position_X{0.f};
        float Position_Y{0.f};
        float Width{0.f};
        float Height{0.f};

        uint8_t TextureIndex{0};

        std::function<void(Button&)> Callback{nullptr};
        std::array<std::string, 2> Note;

        std::chrono::steady_clock::time_point HoverEnterTime;
        bool Hovered{false};
    };

private:
    void renderButton(
        sf::RenderTarget& target, const Button& button,
        sf::Color textColor, sf::Color overlayColor,
        const sf::Font& font, const sf::Texture& texture, float scale
    ) const;

    std::vector<Button> m_Buttons;

    bool m_Hovered{false};
    float m_Opacity{0.f};

public:
    void RemoveAll();

    void AddButton(Button&& button);

    bool Clicked();

    void HandleMouseMoved(sf::Vector2i mousePosition, bool hovering);

    void Update(float deltaTime);

    void Render(
        sf::RenderTarget& target,
        sf::Color backgroundColor, sf::Color textColor, sf::Color overlayColor,
        sf::Vector2f position, sf::Vector2f size,
        const sf::Font& font, const sf::Texture& texture, float scale
    ) const;
};