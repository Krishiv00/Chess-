#pragma once

#include <array>

#include "SFML/Graphics.hpp"

class Theme {
protected:
    std::array<sf::Color, 2> m_Checkerboard;
    std::array<sf::Color, 2> m_EvaluationBar;
    std::array<sf::Color, 5> m_Particles;
    sf::Color m_LastMoveHighlight;
    sf::Color m_LegalMoveHighlight;
    sf::Color m_MarkerHighlight;
    sf::Color m_Background;
    sf::Color m_SurfaceRaised;
    sf::Color m_SurfaceOverlay;
    sf::Color m_TextPrimary;
    sf::Color m_TextSecondary;
    sf::Color m_OverlayPill;
    sf::Color m_Arrow;

public:
    virtual ~Theme() = default;

    virtual void RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const = 0;

    virtual void RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const = 0;

    [[nodiscard]]
    virtual sf::Color GetSelectedPiece() const = 0;

    [[nodiscard]]
    virtual bool DrawLastMoveTargetIfCapturable() const noexcept = 0;

    [[nodiscard]]
    virtual bool DrawSelectedPieceGhost() const noexcept = 0;

    [[nodiscard]]
    virtual bool DrawKnightArrowAsL() const noexcept = 0;

    [[nodiscard]]
    virtual bool DrawHoveredLegalSq() const noexcept = 0;

    [[nodiscard]]
    virtual bool DrawHoveredSqOutline() const noexcept = 0;

    [[nodiscard]]
    inline const std::array<sf::Color, 2>& GetCheckerboard() const noexcept {
        return m_Checkerboard;
    }

    [[nodiscard]]
    inline const std::array<sf::Color, 2>& GetEvaluationBar() const noexcept {
        return m_EvaluationBar;
    }

    [[nodiscard]]
    inline const std::array<sf::Color, 5>& GetParticles() const noexcept {
        return m_Particles;
    }

    [[nodiscard]]
    inline sf::Color GetLastMoveHighlight() const noexcept {
        return m_LastMoveHighlight;
    }

    [[nodiscard]]
    inline sf::Color GetLegalMoveHighlight() const noexcept {
        return m_LegalMoveHighlight;
    }

    [[nodiscard]]
    inline sf::Color GetMarkerHighlight() const noexcept {
        return m_MarkerHighlight;
    }

    [[nodiscard]]
    inline sf::Color GetBackground() const noexcept {
        return m_Background;
    }

    [[nodiscard]]
    inline sf::Color GetSurfaceRaised() const noexcept {
        return m_SurfaceRaised;
    }

    [[nodiscard]]
    inline sf::Color GetSurfaceOverlay() const noexcept {
        return m_SurfaceOverlay;
    }

    [[nodiscard]]
    inline sf::Color GetTextPrimary() const noexcept {
        return m_TextPrimary;
    }

    [[nodiscard]]
    inline sf::Color GetTextSecondary() const noexcept {
        return m_TextSecondary;
    }

    [[nodiscard]]
    inline sf::Color GetOverlayPill() const noexcept {
        return m_OverlayPill;
    }

    [[nodiscard]]
    inline sf::Color GetArrow() const noexcept {
        return m_Arrow;
    }
};

class LichessTheme final : public Theme {
public:
    LichessTheme();

    virtual void RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

    virtual void RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

    [[nodiscard]]
    virtual sf::Color GetSelectedPiece() const override;

    [[nodiscard]]
    virtual bool DrawLastMoveTargetIfCapturable() const noexcept override {
        return false;
    }

    [[nodiscard]]
    virtual bool DrawSelectedPieceGhost() const noexcept override {
        return true;
    }

    [[nodiscard]]
    virtual bool DrawKnightArrowAsL() const noexcept override {
        return false;
    }

    [[nodiscard]]
    virtual bool DrawHoveredLegalSq() const noexcept override {
        return true;
    }

    [[nodiscard]]
    virtual bool DrawHoveredSqOutline() const noexcept override {
        return false;
    }
};

class ChesscomTheme final : public Theme {
public:
    ChesscomTheme();

    virtual void RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

    virtual void RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

    [[nodiscard]]
    virtual sf::Color GetSelectedPiece() const override;

    [[nodiscard]]
    virtual bool DrawLastMoveTargetIfCapturable() const noexcept override {
        return true;
    }

    [[nodiscard]]
    virtual bool DrawSelectedPieceGhost() const noexcept override {
        return false;
    }

    [[nodiscard]]
    virtual bool DrawKnightArrowAsL() const noexcept override {
        return true;
    }

    [[nodiscard]]
    virtual bool DrawHoveredLegalSq() const noexcept override {
        return false;
    }

    [[nodiscard]]
    virtual bool DrawHoveredSqOutline() const noexcept override {
        return true;
    }
};