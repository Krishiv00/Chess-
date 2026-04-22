#include "GUI/Themes.hpp"

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

using namespace Themes;

#pragma region Chess.com

Chesscom::Chesscom() {
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

void Chesscom::RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    const sf::Vertex vertices[] = {
        sf::Vertex(position, m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y), m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize), m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize), m_MarkerHighlight)
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip);
}

void Chesscom::RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    RenderRing(target, m_LegalMoveHighlight, position, squareSize);
}

sf::Color Chesscom::GetSelectedPiece() const {
    return m_LastMoveHighlight;
}

#pragma region Chesscom Variants

Themes::ChesscomGlass::ChesscomGlass() {
    m_Checkerboard = {
        sf::Color(105, 114, 131),
        sf::Color(37, 43, 55)
    };

    m_LastMoveHighlight = sf::Color(40, 86, 114, 127);
}

Themes::ChesscomBrown::ChesscomBrown() {
    m_Checkerboard = {
        sf::Color(237, 214, 176),
        sf::Color(184, 135, 98)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 51, 128);
}

Themes::ChesscomSky::ChesscomSky() {
    m_Checkerboard = {
        sf::Color(240, 241, 240),
        sf::Color(196, 216, 228)
    };

    m_LastMoveHighlight = sf::Color(104, 217, 248, 129);
}

Themes::Chesscom8Bit::Chesscom8Bit() {
    m_Checkerboard = {
        sf::Color(243, 243, 244),
        sf::Color(106, 155, 65)
    };
}

Themes::ChesscomPurple::ChesscomPurple() {
    m_Checkerboard = {
        sf::Color(240, 241, 240),
        sf::Color(132, 118, 186)
    };

    m_LastMoveHighlight = sf::Color(126, 171, 202, 128);
}

Themes::ChesscomLight::ChesscomLight() {
    m_Checkerboard = {
        sf::Color(216, 217, 216),
        sf::Color(168, 169, 168)
    };

    m_LastMoveHighlight = sf::Color(131, 142, 149, 127);
}

Themes::ChesscomDark::ChesscomDark() {
    m_Checkerboard = {
        sf::Color(144, 143, 141),
        sf::Color(110, 109, 107)
    };

    m_LastMoveHighlight = sf::Color(92, 145, 181, 128);
}

Themes::ChesscomDarkBlue::ChesscomDarkBlue() {
    m_Checkerboard = {
        sf::Color(234, 233, 210),
        sf::Color(75, 115, 153)
    };

    m_LastMoveHighlight = sf::Color(0, 165, 255, 127);
}

Themes::ChesscomBubblegum::ChesscomBubblegum() {
    m_Checkerboard = {
        sf::Color(254, 255, 254),
        sf::Color(251, 217, 225)
    };

    m_LastMoveHighlight = sf::Color(218, 71, 92, 112);
}

Themes::ChesscomCheckers::ChesscomCheckers() {
    m_Checkerboard = {
        sf::Color(199, 76, 81),
        sf::Color(48, 48, 48)
    };

    m_LastMoveHighlight = sf::Color(248, 224, 136, 129);
}

Themes::ChesscomOrange::ChesscomOrange() {
    m_Checkerboard = {
        sf::Color(250, 228, 174),
        sf::Color(209, 136, 21)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 0, 128);
}

Themes::ChesscomRed::ChesscomRed() {
    m_Checkerboard = {
        sf::Color(245, 219, 195),
        sf::Color(187, 87, 70)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 95, 128);
}

Themes::ChesscomTan::ChesscomTan() {
    m_Checkerboard = {
        sf::Color(237, 203, 165),
        sf::Color(216, 164, 109)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 60, 128);
}

Themes::ChesscomBlue::ChesscomBlue() {
    m_Checkerboard = {
        sf::Color(242, 246, 250),
        sf::Color(85, 150, 242)
    };

    m_LastMoveHighlight = sf::Color(0, 255, 242, 127);
}

Themes::ChesscomPink::ChesscomPink() {
    m_Checkerboard = {
        sf::Color(245, 240, 241),
        sf::Color(236, 148, 164)
    };

    m_LastMoveHighlight = sf::Color(255, 115, 58, 128);
}

Themes::ChesscomTheMusical::ChesscomTheMusical() {
    m_Checkerboard = {
        sf::Color(215, 212, 212),
        sf::Color(128, 123, 118)
    };

    m_LastMoveHighlight = sf::Color(255, 0, 255, 128);
}

#pragma region Lichess

Lichess::Lichess() {
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

void Lichess::RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    RenderRing(target, m_MarkerHighlight, position, squareSize);
}

void Lichess::RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
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

sf::Color Lichess::GetSelectedPiece() const {
    return m_LegalMoveHighlight;
}

#pragma region Lichess Variants

Themes::LichessBlue::LichessBlue() {
    m_Checkerboard = {
        sf::Color(222, 227, 230),
        sf::Color(140, 162, 173)
    };

    m_LastMoveHighlight = sf::Color(156, 200, 0, 104);
}

Themes::LichessGreen::LichessGreen() {
    m_Checkerboard = {
        sf::Color(255, 255, 221),
        sf::Color(134, 166, 102)
    };

    m_LastMoveHighlight = sf::Color(1, 156, 199, 105);
}

Themes::LichessDark::LichessDark() {
    m_Checkerboard = {
        sf::Color(167, 167, 167),
        sf::Color(135, 135, 135)
    };

    m_LastMoveHighlight = sf::Color(155, 208, 0, 88);
}

Themes::LichessPurple::LichessPurple() {
    m_Checkerboard = {
        sf::Color(159, 144, 176),
        sf::Color(125, 74, 141)
    };

    m_LastMoveHighlight = sf::Color(157, 200, 0, 104);
}