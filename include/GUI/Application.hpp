#pragma once

#include <vector>
#include <array>
#include <filesystem>
#include <thread>
#include <chrono>
#include <memory>

#include "SFML/Graphics.hpp"

#include "Engine/Chess.hpp"

#include "GUI/Themes.hpp"
#include "GUI/SoundSystem.hpp"

class Application final {
private:
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

    enum class GameOverResult {
        None,
        Checkmate,
        Stalemate,
        DrawFiftyMove,
        DrawRepetition,
        DrawInsufficient
    };

    struct GameOverParticle {
        sf::Vector2f Position;
        sf::Vector2f Velocity;
        float Rotation{0.f};
        float RotationSpeed{0.f};
        float Size{0.f};
        float Life{1.f};
        uint8_t ColorIndex{0};
        uint8_t Shape{0}; // 0=square  1=diamond  2=circle
    };

    struct GameState {
        std::string Fen{Chess::DefaultFEN};
        Chess::Move LastMove;
    };

    struct Arrow {
        uint8_t Start{Chess::NullPos};
        uint8_t End{Chess::NullPos};

        [[nodiscard]]
        inline operator bool() const noexcept {
            return Start != Chess::NullPos && End != Chess::NullPos && Start != End;
        }
    };

    [[nodiscard]]
    inline bool hasSelectedPiece() const noexcept {
        return m_SelectedPiece != Chess::NullPos;
    }

    [[nodiscard]]
    inline int mapRank(int rank) const noexcept {
        return m_Flipped ? (Chess::Ranks - 1 - rank) : rank;
    }

    [[nodiscard]]
    inline int mapFile(int file) const noexcept {
        return m_Flipped ? (Chess::Files - 1 - file) : file;
    }

    [[nodiscard]]
    std::pair<int, int> mapMousePosToCoordinates(sf::Vector2i position) const noexcept {
        return std::make_pair(
            mapRank(position.y / m_SquareSize),
            mapFile((position.x - m_EvaluationBarWidth) / m_SquareSize)
        );
    }

    void joinThreads();

    void startPonder();
    void stopPonder();

    void loadFen(const std::string& fen);

    void spawnGameOverParticles();

    void doMove(Chess::Move move, bool animate);
    void onMouseButtonSignal(sf::Vector2i position, bool released);
    void pickPiece(int idx);
    void dropPiece(int idx, bool animate = false);

    void commitPromotion(Chess::MoveFlag promotionFlag);
    int hitTestPromotionMenu(sf::Vector2i mousePos) const;

    void pollEngineMove();
    void updateEvaluation();

    void initUserInterface(sf::Vector2u windowSize);

    void renderCheckerboard(sf::RenderTarget& target) const;
    void renderRanksAndFiles(sf::RenderTarget& target) const;
    void renderSquareHighlight(sf::RenderTarget& target, uint8_t square, sf::Color color) const;
    void renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y, float angle = 0.f) const;
    void renderPieces(sf::RenderTarget& target, bool mouseHeld) const;
    void renderEvaluationBar(sf::RenderTarget& target) const;
    void renderLegalMoves(sf::RenderTarget& target, sf::Vector2i mousePosition) const;
    void renderButton(sf::RenderTarget& target, const Button& button) const;
    void renderBitboard(sf::RenderTarget& target, uint64_t bitboard, sf::Color color) const;
    void renderPopup(sf::RenderTarget& target) const;
    void renderCheckmateOverlay(sf::RenderTarget& target) const;
    void renderPromotionMenu(sf::RenderTarget& target, sf::Vector2i mousePos) const;
    void renderArrow(sf::RenderTarget& target, Arrow arrow) const;

    Chess::Board m_Board;
    Chess::Move m_LastMove;
    Chess::PieceColor m_SideToMove;
    Chess::PieceColor m_LastEngineColor;

    uint16_t m_EngineThinkTimeMs{250};
    uint8_t m_EvaluationDepth{12};
    bool m_UseOwnBook{true};
    bool m_Ponder{false};

    // Invariant: only valid when not equal to `Chess::NullPos`
    uint8_t m_SelectedPiece{Chess::NullPos};
    std::vector<Chess::Move> m_LegalMovesForSelectedPiece;

    float m_EvaluationBarWidth;
    float m_SquareSize;
    float m_PanelWidth;
    float m_PanelBrightness{0.f};
    float m_InspectionModeOverlay_t{0.f};
    float m_DragTilt{0.f};

    float m_CurrentEvaluation{0.5f};
    float m_LatestEvaluation{0.5f};

    float m_GameOverTimer{0.f};
    GameOverResult m_GameOverResult{GameOverResult::None};
    std::vector<GameOverParticle> m_GameOverParticles;

    GameState m_InspectionEntryState;

    sf::Vector2i m_LastMousePosition;

    // Multithreading
    std::thread m_SearchThread;
    std::thread m_PonderThread;
    Chess::Move m_PendingEngineMove;

    // Game state
    bool m_GameOver{false};
    bool m_Flipped{false};
    bool m_PromotionSelectionActive{false};
    bool m_EngineThinking{false};
    bool m_HoveringPanel{false};
    bool m_InspectionMode{false};

    // UI / UX
    std::size_t m_CurrentThemeIdx{0};
    std::unique_ptr<Themes::Theme> m_Theme{Themes::Factories[m_CurrentThemeIdx]()};
    std::vector<PieceMoveAnim> m_PieceAnimations;
    std::vector<Button> m_Buttons;
    std::vector<Arrow> m_Arrows;
    Popup m_Popup;
    uint64_t m_Markers{0ull};
    Arrow m_CurrentlyDrawingArrow;

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
    void HandleMouseButtonPressed(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMouseButtonReleased(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMouseMoved(sf::Vector2i position);
    void HandleMouseLeftWindow();
};