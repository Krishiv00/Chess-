#pragma once

#include <vector>
#include <filesystem>
#include <thread>

#include "SFML/Graphics.hpp"

#include "Engine/Chess.hpp"

#include "GUI/SoundSystem.hpp"

struct Button {
    float Position_X;
    float Position_Y;
    float Width;
    float Height;

    uint8_t TextureIndex{0};

    std::function<void()> Callback;
    bool Hovered{false};
};

class Application final {
private:
    struct PieceMoveAnim {
        float Timer{0.f};
        Chess::Move Move;

        PieceMoveAnim() = default;
        PieceMoveAnim(Chess::Move move) : Move(move), Timer(1.f) {}
    };

    struct Popup {
        float Timer{0.f};
        std::string Info;

        Popup() = default;
        Popup(const std::string& info) : Info(info), Timer(1.f) {
            for (char& c : Info) c = std::toupper(c);
        }

        [[nodiscard]]
        inline bool isActive() const noexcept {
            return Timer;
        }
    };

    [[nodiscard]]
    inline bool hasSelectedPiece() const noexcept {
        return m_SelectedPiece != Chess::NullPos;
    }

    [[nodiscard]]
    inline unsigned int mapRank(unsigned int rank) const noexcept {
        return m_Flipped ? (Chess::Ranks - 1 - rank) : rank;
    }

    [[nodiscard]]
    inline unsigned int mapFile(unsigned int file) const noexcept {
        return m_Flipped ? (Chess::Files - 1 - file) : file;
    }

    [[nodiscard]]
    std::pair<unsigned int, unsigned int> mapMousePosToCoordinates(sf::Vector2i position) const noexcept {
        const unsigned int rank = mapRank(position.y / m_SquareSize);
        const unsigned int file = mapFile((position.x - m_EvaluationBarWidth) / m_SquareSize);

        return std::make_pair(rank, file);
    }

    void startNewGame();

    void doMove(Chess::Move move, bool animate);
    void onMouseButtonSignal(sf::Vector2i position, bool released);
    void pickPiece(int idx);
    void dropPiece(int idx, bool animate = false);

    void pollEngineMove();
    void updateEvaluation();

    void initUserInterface(sf::Vector2u windowSize);
    sf::VertexArray generateCheckerboardMesh(float squareSize, float offset_x, const sf::Color* colors);

    void renderRanksAndFiles(sf::RenderTarget& target) const;
    void renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y) const;
    void renderPieces(sf::RenderTarget& target, bool mouseHeld) const;
    void renderEvaluationBar(sf::RenderTarget& target) const;
    void renderLegalMoves(sf::RenderTarget& target, sf::Vector2i mousePosition) const;
    void renderButton(sf::RenderTarget& target, const Button& button) const;
    void renderBitboard(sf::RenderTarget& target, uint64_t bitboard, sf::Color color) const;

    Chess::Board m_Board;
    Chess::Move m_LastMove;
    Chess::PieceColor m_SideToMove;
    uint16_t m_EngineThinkTimeMs{750};
    Chess::PieceColor m_LastEngineColor;

    // Invariant: only valid when not equal to `Chess::NullPos`
    uint8_t m_SelectedPiece{Chess::NullPos};
    std::vector<Chess::Move> m_LegalMovesForSelectedPiece;

    float m_EvaluationBarWidth;
    float m_SquareSize;
    float m_PanelWidth;
    float m_PanelBrightness{0.f};

    float m_CurrentEvaluation{0.5f};
    float m_LatestEvaluation{0.5f};

    sf::VertexArray m_CheckerboardMesh;

    // Multithreading
    std::thread m_SearchThread;
    std::thread m_PonderThread;
    Chess::Move m_PendingEngineMove;

    // Game state
    bool m_GameOver{false};
    bool m_Flipped{false};
    bool m_EngineThinking{false};
    bool m_HoveringPanel{false};

    // UI / UX
    std::vector<PieceMoveAnim> m_PieceAnimations;
    std::vector<Button> m_Buttons;
    Popup m_Popup;

    // Resources
    sf::Texture m_PieceTexture;
    sf::Texture m_IconTexture;
    sf::Font m_Font;

    SfxPlayer m_SfxPlayer;

public:
    Application();
    ~Application();

    void SetTargetSize(sf::Vector2u size);

    [[nodiscard]]
    bool LoadResources(const std::filesystem::path& root);

    void Update(float deltaTime);
    void Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const;

    void HandleKeyPressed(sf::Keyboard::Scancode key);
    void HandleMouseButtonPressed(sf::Vector2i position);
    void HandleMouseButtonReleased(sf::Vector2i position);
    void HandleMouseMoved(sf::Vector2i position);
    void HandleMouseLeftWindow();
};