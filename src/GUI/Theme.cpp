#include "GUI/Theme.hpp"

static void RenderRing(
    sf::RenderTarget& target,
    sf::Color color,
    sf::Vector2f position,
    float squareSize
) {
    const float thickness = 7.f;
    const float radius = squareSize * 0.5f;

    sf::CircleShape ring(radius);

    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineColor(color);
    ring.setOutlineThickness(-thickness);
    ring.setOrigin(sf::Vector2f(radius, radius));
    ring.setPosition(sf::Vector2f(position.x + squareSize * 0.5f, position.y + squareSize * 0.5f));

    target.draw(ring);
}

#pragma region Lichess

LichessTheme::LichessTheme() {
    m_Checkerboard = {
        sf::Color(240, 217, 181),
        sf::Color(181, 136, 99)
    };

    m_EvaluationBar = {
        sf::Color(255, 255, 255),
        sf::Color(64, 61, 57)
    };

    m_Particles = {
        sf::Color(240, 217, 181),
        sf::Color(181, 136, 99),
        sf::Color(200, 185, 150),
        sf::Color(255, 230, 180),
        sf::Color(120, 100, 75)
    };

    m_LastMoveHighlight = sf::Color(155, 200, 0, 105);
    m_LegalMoveHighlight = sf::Color(70, 115, 65, 166);
    m_MarkerHighlight = sf::Color(19, 119, 24, 151);
    m_Background = sf::Color(48, 46, 43);
    m_SurfaceRaised = sf::Color(42, 40, 37, 248);
    m_SurfaceOverlay = sf::Color(18, 17, 15, 195);
    m_TextPrimary = sf::Color(240, 217, 181);
    m_TextSecondary = sf::Color(180, 160, 130);
    m_OverlayPill = sf::Color(28, 27, 25, 230);
    m_Arrow = sf::Color(19, 119, 24, 151);
}

void LichessTheme::RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    RenderRing(target, m_MarkerHighlight, position, squareSize);
}

void LichessTheme::RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    const sf::Vector2f triangleSize = sf::Vector2f(squareSize, squareSize) * 0.167f;

    const sf::Vertex topLeft[] = {
        sf::Vertex(position, m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + triangleSize.x, position.y), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + triangleSize.y), m_LegalMoveHighlight)
    };

    const sf::Vertex topRight[] = {
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize - triangleSize.x, position.y), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + triangleSize.y), m_LegalMoveHighlight)
    };

    const sf::Vertex bottomLeft[] = {
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + triangleSize.x, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize - triangleSize.y), m_LegalMoveHighlight)
    };

    const sf::Vertex bottomRight[] = {
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize - triangleSize.x, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize - triangleSize.y), m_LegalMoveHighlight)
    };

    target.draw(topLeft, 3, sf::PrimitiveType::Triangles);
    target.draw(topRight, 3, sf::PrimitiveType::Triangles);
    target.draw(bottomLeft, 3, sf::PrimitiveType::Triangles);
    target.draw(bottomRight, 3, sf::PrimitiveType::Triangles);
}

sf::Color LichessTheme::GetSelectedPiece() const {
    return m_LegalMoveHighlight;
}

#pragma region Chess.com

ChesscomTheme::ChesscomTheme() {
    m_Checkerboard = {
        sf::Color(235, 236, 208),
        sf::Color(115, 149, 82)
    };

    m_EvaluationBar = {
        sf::Color(255, 255, 255),
        sf::Color(64, 61, 57)
    };

    m_Particles = {
        sf::Color(235, 236, 208),
        sf::Color(115, 149, 82),
        sf::Color(180, 200, 140),
        sf::Color(255, 240, 180),
        sf::Color(80, 110, 55)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 52, 128);
    m_LegalMoveHighlight = sf::Color(2, 1, 0, 36);
    m_MarkerHighlight = sf::Color(235, 97, 81, 204);
    m_Background = sf::Color(48, 46, 43);
    m_SurfaceRaised = sf::Color(40, 46, 36, 248);
    m_SurfaceOverlay = sf::Color(18, 17, 15, 195);
    m_TextPrimary = sf::Color(235, 236, 208);
    m_TextSecondary = sf::Color(160, 185, 130);
    m_OverlayPill = sf::Color(32, 38, 28, 230);
    m_Arrow = sf::Color(255, 170, 0, 163);
}

void ChesscomTheme::RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    const sf::Vertex vertices[] = {
        sf::Vertex(position, m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y), m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize), m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize), m_MarkerHighlight)
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip);
}

void ChesscomTheme::RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    RenderRing(target, m_LegalMoveHighlight, position, squareSize);
}

sf::Color ChesscomTheme::GetSelectedPiece() const {
    return m_LastMoveHighlight;
}