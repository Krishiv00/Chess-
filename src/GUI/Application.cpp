#include <iostream>
#include <sstream>

#include "GUI/Application.hpp"

#pragma region Utils

namespace Utils {
    [[nodiscard]]
    static float ExponentiallyMoveTo(float from, float to, float speed, float snapThreshold = 0.002f) noexcept {
        const float diff = to - from;
        return std::fabs(diff) < snapThreshold ? to : (from + diff * (1.f - std::exp(-speed)));
    }

    [[nodiscard]]
    static sf::Color ConvertAlpha(sf::Color color, float factor) noexcept {
        color.a *= factor;
        return color;
    }
}

#pragma region Setup

Application::Application() {
    Chess::Init();

    m_Board.SetHashSize(128);
    m_Board.SetCatchAll(false);

    loadFen(Chess::DefaultFEN);
}

void Application::loadFen(const std::string& fen) {
    if (fen.empty()) return;

    joinThreads();

    m_Board.NewGame();

    m_SideToMove = m_Board.LoadFromFen(fen);
    m_Board.SetEngineColor(m_SideToMove);
    m_Board.ClearHash();

    updateEvaluation();

    m_GameOver = m_Flipped = false;
    m_LastMove = Chess::Move();

    startPonder();

    m_GameOverTimer = 0.f;
    m_GameOverResult = GameOverResult::None;
    m_GameOverParticles.clear();
}

Application::~Application() {
    joinThreads();
}

void Application::SetTargetSize(sf::Vector2u size) {
    m_SquareSize = static_cast<float>(size.y) / static_cast<float>(Chess::Ranks);

    initUserInterface(size);
}

void Application::initUserInterface(sf::Vector2u windowSize) {
    const float windowHeight = static_cast<float>(windowSize.y);
    const float windowWidth = static_cast<float>(windowSize.x);

    const float boardWidth = Chess::Files * m_SquareSize;

    m_EvaluationBarWidth = windowHeight * 0.075f;
    m_PanelWidth = windowHeight * 0.05f;

    const float padding_y = windowHeight * 0.006f;
    const float padding_x = m_PanelWidth * 0.11f;

    const float panelStartPos = m_EvaluationBarWidth + boardWidth;
    const float buttonSize = m_PanelWidth - 2.f * padding_x;

    const float currentXPos = panelStartPos + padding_x;
    float currentYPos = 0.f + padding_y;

    m_Buttons.clear();

    // flip board button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        0,
        [this](Button&) -> void {
            m_Flipped ^= true;
        },
        std::array<std::string, 2>{"Flip Board", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // get engine move button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        1,
        [this](Button&) -> void {
            if (!m_EngineThinking && !m_GameOver) pollEngineMove();
        },
        std::array<std::string, 2>{"Play Best Move", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // skip search button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        2,
        [this](Button&) -> void {
            if (m_EngineThinking) m_Board.CancelSearch();
        },
        std::array<std::string, 2>{"Skip Search", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // up button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        4,
        [this](Button&) -> void {
            if (
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::RControl)
            ) {
                ++m_EvaluationDepth;
                updateEvaluation();
                m_Popup = Popup("evaluation depth: " + std::to_string(m_EvaluationDepth));
            } else {
                m_EngineThinkTimeMs += 50;
                m_Popup = Popup("think time: " + std::to_string(m_EngineThinkTimeMs));
            }
        },
        std::array<std::string, 2>{"Search Time +", "Eval Depth +"}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // down button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        5,
        [this](Button&) -> void {
            if (
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::RControl)
            ) {
                if (m_EvaluationDepth > 1) --m_EvaluationDepth;
                updateEvaluation();
                m_Popup = Popup("evaluation depth: " + std::to_string(m_EvaluationDepth));
            } else {
                m_EngineThinkTimeMs = std::max(50, m_EngineThinkTimeMs - 50);
                m_Popup = Popup("think time: " + std::to_string(m_EngineThinkTimeMs));
            }
        },
        std::array<std::string, 2>{"Search Time -", "Eval Depth -"}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // own book button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        7 - m_UseOwnBook,
        [this](Button& button) -> void {
            m_UseOwnBook ^= 1;

            button.TextureIndex = 7 - m_UseOwnBook;
            m_Popup = Popup("own book: " + std::string(m_UseOwnBook ? "true" : "false"));
        },
        std::array<std::string, 2>{"Own Book", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // ponder button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        9 - m_Ponder,
        [this](Button& button) -> void {
            m_Ponder ^= 1;

            if (!m_EngineThinking) {
                if (m_Ponder) startPonder();
                else stopPonder();
            }

            button.TextureIndex = 9 - m_Ponder;
            m_Popup = Popup("ponder: " + std::string(m_Ponder ? "true" : "false"));
        },
        std::array<std::string, 2>{"Ponder", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // catch all button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        11 - m_Board.GetCatchAll(),
        [this](Button& button) -> void {
            joinThreads();

            m_Board.SetCatchAll(!m_Board.GetCatchAll());
            m_Board.ClearHash();

            updateEvaluation();

            button.TextureIndex = 11 - m_Board.GetCatchAll();
            m_Popup = Popup("catch all mode: " + std::string(m_Board.GetCatchAll() ? "true" : "false"));
        },
        std::array<std::string, 2>{"Gotta Catch'em All!", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // inspection mode button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        13 - m_InspectionMode,
        [this](Button& button) -> void {
            if (!m_EngineThinking && (!m_GameOver || m_InspectionMode)) {
                joinThreads();

                if (m_InspectionMode) {
                    const bool oldFlipped = m_Flipped;

                    loadFen(m_InspectionEntryState.Fen);
                    m_LastMove = m_InspectionEntryState.LastMove;
                    stopPonder();

                    m_Flipped = oldFlipped;
                } else {
                    m_InspectionEntryState = GameState(m_Board.GetFen(m_SideToMove), m_LastMove);
                }

                m_InspectionMode ^= 1;
                button.TextureIndex = 13 - m_InspectionMode;

                m_Popup = Popup("inspection mode: " + std::string(m_InspectionMode ? "true" : "false"));
            }
        },
        std::array<std::string, 2>{"Inspection Mode", ""}
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // board reset / theme cycle button
    m_Buttons.emplace_back(
        currentXPos, windowHeight - buttonSize - padding_y,
        buttonSize, buttonSize,
        3,
        [this](Button&) -> void {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
                m_CurrentThemeIdx = (m_CurrentThemeIdx + 1) % Themes::Count;
                m_Theme = Themes::Factories[m_CurrentThemeIdx]();
                m_Popup = Popup("Theme: " + std::string(Themes::Names[m_CurrentThemeIdx]));
            } else {
                m_PromotionSelectionActive = false;
                loadFen(Chess::DefaultFEN);
            }
        },
        std::array<std::string, 2>{"Reset Board", "Cycle Themes"}
    );
}

void Application::joinThreads() {
    m_Board.CancelSearch();
    if (m_PonderThread.joinable()) m_PonderThread.join();
    if (m_SearchThread.joinable()) m_SearchThread.join();
}

#pragma region Resources

bool Application::LoadResources(const std::filesystem::path& root) {
    const std::filesystem::path texturePath = root / "Textures";

    if (!m_PieceTexture.loadFromFile(texturePath / "Pieces.png")) [[unlikely]] {
        std::cerr << "Failed to load piece texture" << std::endl;
        return false;
    }

    if (!m_IconTexture.loadFromFile(texturePath / "Icons.png")) [[unlikely]] {
        std::cerr << "Failed to load piece texture" << std::endl;
        return false;
    }

    if (!m_Font.openFromFile(root / "Fonts" / "Font.ttf")) [[unlikely]] {
        std::cerr << "Failed to open font" << std::endl;
        return false;
    }

    m_Font.setSmooth(false);

    const std::filesystem::path soundPath = root / "Sounds";

    if (!m_SfxPlayer.LoadFromFile(Sfx::Capture, soundPath / "Capture.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Check, soundPath / "Check.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::GameEnd, soundPath / "Game End.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::MoveSelf, soundPath / "Move Self.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::MoveOpponent, soundPath / "Move Opp.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Promotion, soundPath / "Promotion.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Notify, soundPath / "Notify.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::SpecialMove, soundPath / "Special Move.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    // no errors in loading the assets
    return true;
}

#pragma region Events

void Application::HandleKeyPressed(sf::Keyboard::Scancode key) {
    if (key == sf::Keyboard::Scancode::V) {
        if (!m_PromotionSelectionActive && sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            loadFen(sf::Clipboard::getString());
        }
    }

    else if (key == sf::Keyboard::Scancode::C) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            sf::Clipboard::setString(m_Board.GetFen(m_SideToMove));
        }
    }
}

void Application::HandleMouseButtonPressed(sf::Mouse::Button button, sf::Vector2i position) {
    if (button == sf::Mouse::Button::Left) {
        m_Markers = 0ull;
        m_Arrows.clear();

        if (m_PromotionSelectionActive) {
            if (const int hit = hitTestPromotionMenu(position)) {
                commitPromotion(Chess::PromotionFlags[hit - 1]);
            }

            m_PromotionSelectionActive = false;
            m_SelectedPiece = Chess::NullPos;
            m_LegalMovesForSelectedPiece.clear();
        } else {
            for (Button& button : m_Buttons) {
                if (button.Hovered) {
                    if (button.Callback) button.Callback(button);
                    return;
                }
            }

            onMouseButtonSignal(position, false);

            if (m_GameOver && m_GameOverParticles.empty()) m_GameOverResult = GameOverResult::None;
        }
    }

    else if (button == sf::Mouse::Button::Right) {
        const auto [rank, file] = mapMousePosToCoordinates(position);
        const int idx = Chess::To2DIndex(rank, file);

        m_CurrentlyDrawingArrow.Start = idx;
    }
}

void Application::HandleMouseButtonReleased(sf::Mouse::Button button, sf::Vector2i position) {
    if (button == sf::Mouse::Button::Left) {
        if (!m_PromotionSelectionActive) onMouseButtonSignal(position, true);
    }

    else if (button == sf::Mouse::Button::Right) {
        const auto [rank, file] = mapMousePosToCoordinates(position);
        const int idx = Chess::To2DIndex(rank, file);

        m_CurrentlyDrawingArrow.End = idx;

        if (m_CurrentlyDrawingArrow.Start == m_CurrentlyDrawingArrow.End) {
            m_Markers ^= Chess::IndexToMask(idx); // toggle marker
        } else if (m_CurrentlyDrawingArrow) {
            auto it = std::find_if(m_Arrows.begin(), m_Arrows.end(),
                [this](const Arrow& arrow) -> bool {
                    return (
                        arrow.Start == m_CurrentlyDrawingArrow.Start && arrow.End == m_CurrentlyDrawingArrow.End
                    );
                }
            );

            if (it != m_Arrows.end()) {
                m_Arrows.erase(it);
            } else {
                m_Arrows.push_back(m_CurrentlyDrawingArrow);
            }

            m_Markers |= (
                // enable start
                Chess::IndexToMask(m_CurrentlyDrawingArrow.Start) |
                // enable end
                Chess::IndexToMask(m_CurrentlyDrawingArrow.End)
            );
        }

        m_CurrentlyDrawingArrow = Arrow();
    }
}

void Application::HandleMouseMoved(sf::Vector2i position) {
    if (m_PromotionSelectionActive) return;

    if (hasSelectedPiece()) {
        const sf::Vector2f delta(
            static_cast<float>(position.x - m_LastMousePosition.x),
            static_cast<float>(position.y - m_LastMousePosition.y)
        );

        const float speed = std::sqrtf(delta.x * delta.x + delta.y * delta.y);

        if (speed > 0.5f) {
            constexpr float MaxTilt = 0.45f; // ~26 degrees
            const float targetTilt = (delta.x / speed) * std::min(speed / 18.f, 1.f) * MaxTilt;
            m_DragTilt = targetTilt;
        }
    } else {
        const float panelLeft = m_EvaluationBarWidth + Chess::Files * m_SquareSize;

        const bool hoveringPanel = (
            position.x > panelLeft &&
            position.x < panelLeft + m_PanelWidth
        );

        Button* nowHovered = nullptr;

        if (hoveringPanel) {
            for (Button& button : m_Buttons) {
                if (
                    position.x >= button.Position_X && position.x <= (button.Position_X + button.Width) &&
                    position.y >= button.Position_Y && position.y <= (button.Position_Y + button.Height)
                ) {
                    nowHovered = &button;
                    break;
                }
            }
        }

        for (Button& button : m_Buttons) {
            const bool wasHovered = button.Hovered;
            button.Hovered = &button == nowHovered;

            if (button.Hovered && !wasHovered) {
                button.HoverEnterTime = std::chrono::steady_clock::now();
            }
        }

        m_HoveringPanel = hoveringPanel;
    }

    if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right)) {
        const auto [rank, file] = mapMousePosToCoordinates(position);
        m_CurrentlyDrawingArrow.End = Chess::To2DIndex(rank, file);
    }

    m_LastMousePosition = position;
}

void Application::HandleMouseLeftWindow() {
    for (Button& button : m_Buttons) {
        button.Hovered = false;
    }

    m_HoveringPanel = false;
}

void Application::commitPromotion(Chess::MoveFlag promotionFlag) {
    const uint8_t landingPos = m_SelectedPiece + (m_SideToMove == Chess::PieceColor::White ? -Chess::Files : Chess::Files);

    for (const Chess::Move& m : m_LegalMovesForSelectedPiece) {
        if (
            m.StartingSquare == m_SelectedPiece && m.TargetSquare == landingPos &&
            Chess::HasFlag(m.Flag, promotionFlag)
        ) {
            doMove(m, true);
            if (!m_GameOver && !m_InspectionMode) pollEngineMove();

            return;
        }
    }
}

int Application::hitTestPromotionMenu(sf::Vector2i mousePos) const {
    if (!m_PromotionSelectionActive) return -1;

    const float menuX = mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth;
    const float visualRank = mapRank(Chess::ToRank(m_SelectedPiece) + (m_SideToMove == Chess::PieceColor::White ? -1 : 1));

    const bool openDown = visualRank < Chess::Ranks / 2;
    const float cardTop = openDown ? visualRank * m_SquareSize : (visualRank - 3.f) * m_SquareSize;

    for (int i = 0; i < 4; ++i) {
        const float slotY = openDown ? (visualRank + i) * m_SquareSize : (visualRank - i) * m_SquareSize;

        if (
            mousePos.x >= menuX && mousePos.x < menuX + m_SquareSize &&
            mousePos.y >= slotY && mousePos.y < slotY + m_SquareSize
        ) {
            return i + 1;
        }
    }

    return 0;
}

#pragma region Gameplay

void Application::pollEngineMove() {
    // return;

    if (m_GameOver || m_EngineThinking) return;

    joinThreads();

    m_EngineThinking = true;

    m_Board.SetEngineColor(m_SideToMove);

    // clear hash if engine color has changed this game
    // since hash values are misleading now
    if (m_Board.GetCatchAll()) {
        if (m_SideToMove != m_LastEngineColor) {
            m_Board.ClearHash();
        }

        m_LastEngineColor = m_SideToMove;
    }

    Chess::Board searchBoard = m_Board;
    Chess::PieceColor searchSideToMove = m_SideToMove;

    m_SearchThread = std::thread([searchBoard, searchSideToMove, this]() -> void {
        const Chess::Move bestMove = searchBoard.FindBestMoveByTime(searchSideToMove, m_EngineThinkTimeMs, m_UseOwnBook);

        m_PendingEngineMove = bestMove;
        m_EngineThinking = false;
    });
}

void Application::updateEvaluation() {
    m_LatestEvaluation = m_Board.GetConfidence(m_SideToMove, m_EvaluationDepth);
}

void Application::doMove(Chess::Move move, bool animate) {
    m_Board.DoMove(move);
    m_SideToMove = Chess::InvertColor(m_SideToMove);

    // check endgame
    if (
        m_Board.FiftyMoveRule() ||
        m_Board.isThreefoldRepetition() ||
        m_Board.HasInsufficientMaterial() ||
        !m_Board.HasAnyLegalMoves(m_SideToMove)
    ) {
        m_SfxPlayer.Play(Sfx::GameEnd);

        if (!m_InspectionMode) {
            if (m_Board.FiftyMoveRule()) {
                m_GameOverResult = GameOverResult::DrawFiftyMove;
            } else if (m_Board.HasInsufficientMaterial()) {
                m_GameOverResult = GameOverResult::DrawInsufficient;
            } else if (m_Board.isThreefoldRepetition()) {
                m_GameOverResult = GameOverResult::DrawRepetition;
            } else if (!m_Board.isUnderCheck(m_SideToMove)) {
                m_GameOverResult = GameOverResult::Stalemate;
            } else {
                m_GameOverResult = GameOverResult::Checkmate;
            }

            m_GameOverTimer = 0.001f;
            spawnGameOverParticles();
        }

        m_GameOver = true;
    }

    // play sound effects
    if (HasFlag(move.Flag, Chess::MoveFlag::Check)) {
        m_SfxPlayer.Play(Sfx::Check);
    } else if (isCastle(move.Flag)) {
        m_SfxPlayer.Play(Sfx::SpecialMove);

        const bool isKingSide = HasFlag(move.Flag, Chess::MoveFlag::CastleKingSide);
        const int rank = Chess::ToRank(move.StartingSquare);

        const int rookFrom = Chess::To2DIndex(rank, isKingSide ? 7 : 0);
        const int rookTo = Chess::To2DIndex(rank, isKingSide ? 5 : 3);

        m_PieceAnimations.emplace_back(Chess::Move(rookFrom, rookTo));
    } else if (isPromotion(move.Flag)) {
        m_SfxPlayer.Play(Sfx::Promotion);
    } else if (HasFlag(move.Flag, Chess::MoveFlag::Capture)) {
        m_SfxPlayer.Play(Sfx::Capture);
    } else {
        const bool isSelfMove = (m_SideToMove == Chess::PieceColor::Black) ^ m_Flipped;
        m_SfxPlayer.Play(isSelfMove ? Sfx::MoveSelf : Sfx::MoveOpponent);
    }

    m_LastMove = move;

    if (Chess::HasFlag(move.Flag, Chess::MoveFlag::Capture)) {
        const auto it = std::find_if(m_PieceAnimations.begin(), m_PieceAnimations.end(),
            [&move](const PieceMoveAnim& anim) {
                return anim.Move.TargetSquare == move.TargetSquare;
            }
        );

        if (it != m_PieceAnimations.end()) {
            m_PieceAnimations.erase(it);
        }
    }

    if (animate) m_PieceAnimations.emplace_back(move);

    updateEvaluation();
}

void Application::onMouseButtonSignal(sf::Vector2i position, bool released) {
    if (
        m_GameOver ||
        position.x < m_EvaluationBarWidth ||
        position.x > m_EvaluationBarWidth + Chess::Files * m_SquareSize
    ) return;

    const auto [rank, file] = mapMousePosToCoordinates(position);
    const uint8_t idx = Chess::To2DIndex(rank, file);

    if (released) {
        if (hasSelectedPiece() && idx != m_SelectedPiece) dropPiece(idx);
    } else {
        if (hasSelectedPiece()) dropPiece(idx, true);

        pickPiece(idx);
    }
}

void Application::pickPiece(int idx) {
    if (m_EngineThinking) return;

    // if current idx has a piece of mover's color
    if (m_Board.GetOccupancyMap(m_SideToMove) & Chess::IndexToMask(idx)) {
        m_SelectedPiece = idx;
        m_LegalMovesForSelectedPiece = m_Board.GetLegalMoves(idx);
    }
}

void Application::dropPiece(int idx, bool animate) {
    const auto it = std::find(m_LegalMovesForSelectedPiece.begin(), m_LegalMovesForSelectedPiece.end(),
        Chess::Move(m_SelectedPiece, idx)
    );

    if (it != m_LegalMovesForSelectedPiece.end()) {
        if (Chess::isPromotion(it->Flag)) {
            m_PromotionSelectionActive = true;
            m_SfxPlayer.Play(Sfx::Notify);
            return;
        }

        doMove(*it, animate);

        if (!m_GameOver && !m_InspectionMode) pollEngineMove();
    }

    m_SelectedPiece = Chess::NullPos;
    m_LegalMovesForSelectedPiece.clear();
}

#pragma region Ponder

void Application::startPonder() {
    if (!m_Ponder || m_GameOver || m_InspectionMode) return;

    Chess::Board ponderBoard = m_Board;
    Chess::PieceColor ponderSideToMove = m_SideToMove;
    Chess::Move bestMove = m_PendingEngineMove;

    // do opponents best response first
    ponderBoard.DoMove(ponderBoard.FindBestMoveByDepth(ponderSideToMove, 1));
    ponderSideToMove = Chess::InvertColor(ponderSideToMove);

    m_PonderThread = std::thread([ponderBoard, ponderSideToMove, bestMove]() -> void {
        (void)ponderBoard.FindBestMoveByTime(ponderSideToMove, std::numeric_limits<int>::max(), false);
    });
}

void Application::stopPonder() {
    m_Board.CancelSearch();
    if (m_PonderThread.joinable()) m_PonderThread.join();
}

#pragma region Update

void Application::Update(float deltaTime) {
    if (m_PendingEngineMove) {
        doMove(m_PendingEngineMove, true);
        startPonder();

        m_PendingEngineMove = Chess::Move();
    }

    const float decrement = deltaTime / 0.16f;

    for (auto it = m_PieceAnimations.begin(); it != m_PieceAnimations.end(); ) {
        PieceMoveAnim& anim = *it;

        anim.Timer -= decrement;

        if (anim.Timer <= 0.f) {
            it = m_PieceAnimations.erase(it);
        } else {
            ++it;
        }
    }

    if (m_Popup.Timer > 0.f) {
        m_Popup.Timer -= deltaTime;
        if (m_Popup.Timer <= 0.f) m_Popup.Info.clear();
    }

    if (m_GameOverTimer > 0.f) {
        m_GameOverTimer += deltaTime;
    }

    constexpr float Gravity = 400.f;
    constexpr float AirResistance = 0.98f;

    for (auto it = m_GameOverParticles.begin(); it != m_GameOverParticles.end(); ) {
        GameOverParticle& p = *it;

        p.Velocity.y += Gravity * deltaTime;
        p.Velocity *= AirResistance;
        p.Position += p.Velocity * deltaTime;
        p.Rotation += p.RotationSpeed * deltaTime;
        p.Life -= deltaTime * 0.32f;

        if (p.Life <= 0.f) it = m_GameOverParticles.erase(it);
        else ++it;
    }

    m_CurrentEvaluation = Utils::ExponentiallyMoveTo(
        m_CurrentEvaluation, m_LatestEvaluation, 7.5f * deltaTime
    );

    m_PanelBrightness = Utils::ExponentiallyMoveTo(
        m_PanelBrightness, m_HoveringPanel, 20.f * deltaTime
    );

    m_InspectionModeOverlay_t = Utils::ExponentiallyMoveTo(
        m_InspectionModeOverlay_t, m_InspectionMode, 7.5f * deltaTime
    );

    m_DragTilt = Utils::ExponentiallyMoveTo(
        m_DragTilt, 0.f, 7.5f * deltaTime
    );
}

void Application::spawnGameOverParticles() {
    m_GameOverParticles.clear();

    const float boardWidth = Chess::Files * m_SquareSize;
    const float boardCenterX = m_EvaluationBarWidth + boardWidth * 0.5f;

    for (int i = 0; i < 90; ++i) {
        GameOverParticle& p = m_GameOverParticles.emplace_back();

        const float spawnSpread = boardWidth * 0.4f;

        p.Position = sf::Vector2f(
            boardCenterX + (std::rand() / static_cast<float>(RAND_MAX) - 0.5f) * spawnSpread * 2.f,
            Chess::Ranks * m_SquareSize - (std::rand() % 10)
        );

        const float angle = -60.f - (std::rand() / static_cast<float>(RAND_MAX)) * 60.f;
        const float angleRad = angle * 3.14159265f / 180.f;
        const float speed = 220.f + (std::rand() / static_cast<float>(RAND_MAX)) * 500.f;

        p.Velocity = sf::Vector2f(
            std::cos(angleRad) * speed,
            std::sin(angleRad) * speed * 2.f
        );

        p.Rotation = static_cast<float>(std::rand() % 360);
        p.RotationSpeed = -300.f + (std::rand() % 600);

        const float sizeRand = std::rand() / static_cast<float>(RAND_MAX);
        p.Size = 4.f + sizeRand * sizeRand * 12.f;

        p.Life = 0.7f + 0.5f * (std::rand() / static_cast<float>(RAND_MAX));
        p.ColorIndex = std::rand() % 5;

        const int shapeRoll = std::rand() % 10;

        if (shapeRoll < 4) p.Shape = 2; // 40% circles
        else if (shapeRoll < 7) p.Shape = 0; // 30% squares
        else p.Shape = 1; // 30% diamonds
    }
}

#pragma region Render

static void RenderQuad(
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

static void RenderRoundedQuad(
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

static void RenderFrame(
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

void Application::renderCheckerboard(sf::RenderTarget& target) const {
    sf::VertexArray vertices(sf::PrimitiveType::Triangles, Chess::Files * Chess::Ranks * 6u);

    unsigned int vertexIndex = 0u;

    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(rank) * m_SquareSize;

        for (int file = 0; file < Chess::Files; ++file) {
            const float x = static_cast<float>(file) * m_SquareSize + m_EvaluationBarWidth;

            const sf::Color color = m_Theme->GetCheckerboard()[(file + rank) % 2];

            const sf::Vertex topleft = sf::Vertex(sf::Vector2f(x, y), color);
            const sf::Vertex topright = sf::Vertex(sf::Vector2f(x + m_SquareSize, y), color);
            const sf::Vertex bottomright = sf::Vertex(sf::Vector2f(x + m_SquareSize, y + m_SquareSize), color);
            const sf::Vertex bottomleft = sf::Vertex(sf::Vector2f(x, y + m_SquareSize), color);

            vertices[vertexIndex++] = topleft;
            vertices[vertexIndex++] = topright;
            vertices[vertexIndex++] = bottomright;

            vertices[vertexIndex++] = topleft;
            vertices[vertexIndex++] = bottomright;
            vertices[vertexIndex++] = bottomleft;
        }
    }

    target.draw(vertices);
}

void Application::renderRanksAndFiles(sf::RenderTarget& target) const {
    const unsigned int charSize = static_cast<unsigned int>(m_SquareSize * 0.175f);
    const float padding = m_SquareSize * 0.06f;

    // ranks
    for (unsigned int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(rank) * m_SquareSize;
        const char rankChar = (m_Flipped ? rank : Chess::Ranks - 1 - rank) + '1';

        sf::Text text(m_Font, rankChar, charSize);
        text.setStyle(sf::Text::Style::Bold);
        text.setFillColor(m_Theme->GetCheckerboard()[(rank + m_Flipped) % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        const float x = m_Flipped
            ? m_EvaluationBarWidth + padding
            : m_EvaluationBarWidth + Chess::Files * m_SquareSize - bounds.size.x - padding;

        text.setPosition(sf::Vector2f(x, y + padding));
        target.draw(text);
    }

    // files
    for (unsigned int file = 0; file < Chess::Files; ++file) {
        const float x = m_EvaluationBarWidth + static_cast<float>(file) * m_SquareSize;
        const char fileChar = (m_Flipped ? Chess::Files - 1 - file : file) + 'a';

        sf::Text text(m_Font, fileChar, charSize);
        text.setStyle(sf::Text::Style::Bold);
        text.setFillColor(m_Theme->GetCheckerboard()[file % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        text.setPosition(sf::Vector2f(
            x + m_SquareSize - bounds.size.x - padding,
            Chess::Ranks * m_SquareSize - bounds.size.y - padding
        ));

        target.draw(text);
    }
}

void Application::renderSquareHighlight(sf::RenderTarget& target, uint8_t square, sf::Color color) const {
    if ((m_Markers & Chess::IndexToMask(square)) == 0ull) {
        RenderQuad(
            target, color,
            sf::Vector2f(
                mapFile(Chess::ToFile(square)) * m_SquareSize + m_EvaluationBarWidth,
                mapRank(Chess::ToRank(square)) * m_SquareSize
            ),
            sf::Vector2f(m_SquareSize, m_SquareSize)
        );
    }
}

void Application::renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y, float angle) const {
    const float PieceTextureWidth = static_cast<float>(m_PieceTexture.getSize().x) / 6.f;
    const float PieceTextureHeight = static_cast<float>(m_PieceTexture.getSize().y) / 2.f;

    const float tx = static_cast<float>(piece.Type) * PieceTextureWidth;
    const float ty = static_cast<float>(piece.Color) * PieceTextureHeight;

    const float halfSquareSize = m_SquareSize * 0.5f;

    const float cx = x + halfSquareSize;
    const float cy = y + halfSquareSize;

    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);

    const auto rotate = [&](float px, float py) -> sf::Vector2f {
        const float dx = px - cx;
        const float dy = py - cy;

        const float rx = dx * cosA - dy * sinA;
        const float ry = dx * sinA + dy * cosA;

        return sf::Vector2f(cx + rx, cy + ry);
        };

    const sf::Vertex vertices[] = {
        sf::Vertex(rotate(x, y), sf::Color::White, sf::Vector2f(tx, ty)),
        sf::Vertex(rotate(x + m_SquareSize, y), sf::Color::White, sf::Vector2f(tx + PieceTextureWidth, ty)),
        sf::Vertex(rotate(x, y + m_SquareSize), sf::Color::White, sf::Vector2f(tx, ty + PieceTextureHeight)),
        sf::Vertex(rotate(x + m_SquareSize, y + m_SquareSize), sf::Color::White, sf::Vector2f(tx + PieceTextureWidth, ty + PieceTextureHeight))
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip, &m_PieceTexture);
}

void Application::renderPieces(sf::RenderTarget& target, bool mouseHeld) const {
    const uint64_t occupancyMap = m_Board.GetOccupancyMap();

    const int selectedPiece = mouseHeld ? m_SelectedPiece : Chess::NullPos;

    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(mapRank(rank)) * m_SquareSize;
        const int rankIdx = Chess::Files * rank;

        for (int file = 0; file < Chess::Files; ++file) {
            const float x = static_cast<float>(mapFile(file)) * m_SquareSize + m_EvaluationBarWidth;
            const int idx = rankIdx + file;

            // skip rendering this piece if it's selected
            // it will be rendered at the mouse position instead
            if (idx == selectedPiece) continue;

            // skip empty cell
            if ((occupancyMap & Chess::IndexToMask(idx)) == 0ull) continue;

            // skip rendering this piece if its being animated
            if (std::any_of(m_PieceAnimations.begin(), m_PieceAnimations.end(),
                [idx](const PieceMoveAnim& anim) -> bool {
                    return anim.Move.TargetSquare == idx;
                }
            )) continue;

            renderPiece(target, m_Board.GetPieceAt(idx), x, y);
        }
    }
}

void Application::renderEvaluationBar(sf::RenderTarget& target) const {
    const float padding_y = target.getSize().y * 0.006f;
    const float padding_x = m_EvaluationBarWidth * 0.11f;

    const float barHeight = static_cast<float>(target.getSize().y) - 2.f * padding_y;
    const float barWidth = m_EvaluationBarWidth - 2.f * padding_x;

    const float eval = m_Flipped ? m_CurrentEvaluation : (1.f - m_CurrentEvaluation);
    const float midPos = barHeight * eval;

    sf::RenderTexture barRT(sf::Vector2u(barWidth, barHeight));
    barRT.clear(sf::Color::Transparent);

    RenderRoundedQuad(barRT, sf::Color::White, sf::Vector2f(), sf::Vector2f(barWidth, barHeight), 0.4f);

    sf::RectangleShape topFill(sf::Vector2f(barWidth, midPos));
    topFill.setFillColor(m_Theme->GetEvaluationBar()[!m_Flipped]);
    barRT.draw(topFill, sf::BlendMultiply);

    sf::RectangleShape bottomFill(sf::Vector2f(barWidth, barHeight - midPos));
    bottomFill.setPosition(sf::Vector2f(0.f, midPos));
    bottomFill.setFillColor(m_Theme->GetEvaluationBar()[m_Flipped]);
    barRT.draw(bottomFill, sf::BlendMultiply);

    barRT.display();

    sf::Sprite bar(barRT.getTexture());
    bar.setPosition(sf::Vector2f(padding_x, padding_y));
    target.draw(bar);
}

void Application::renderLegalMoves(sf::RenderTarget& target, sf::Vector2i mousePosition) const {
    const auto [mouseRank, mouseFile] = mapMousePosToCoordinates(mousePosition);
    const std::size_t hoveredIdx = Chess::To2DIndex(mouseRank, mouseFile);

    for (const Chess::Move& move : m_LegalMovesForSelectedPiece) {
        const int rank = mapRank(Chess::ToRank(move.TargetSquare));
        const int file = mapFile(Chess::ToFile(move.TargetSquare));

        const float y = rank * m_SquareSize;
        const float x = file * m_SquareSize + m_EvaluationBarWidth;

        if (m_Theme->DrawHoveredLegalSq() && move.TargetSquare == hoveredIdx) {
            RenderQuad(target, m_Theme->GetLegalMoveHighlight(), sf::Vector2f(x, y), sf::Vector2f(m_SquareSize, m_SquareSize));
        } else if (m_Board.GetOccupancyMap(Chess::InvertColor(m_SideToMove)) & Chess::IndexToMask(move.TargetSquare)) {
            m_Theme->RenderCaptureOverlay(target, sf::Vector2f(x, y), m_SquareSize);
        } else {
            const float circleRadius = m_SquareSize * 0.15f;

            sf::CircleShape circle(circleRadius);

            const float offset = m_SquareSize * 0.5f - circleRadius;
            circle.setPosition(sf::Vector2f(x + offset, y + offset));
            circle.setFillColor(m_Theme->GetLegalMoveHighlight());

            target.draw(circle);
        }
    }

    if (
        hasSelectedPiece() &&
        m_Theme->DrawHoveredSqOutline() &&
        mousePosition.x > m_EvaluationBarWidth &&
        mousePosition.x < (Chess::Files * m_SquareSize + m_EvaluationBarWidth) &&
        sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)
    ) {
        const int file = mapFile(Chess::ToFile(hoveredIdx));
        const int rank = mapRank(Chess::ToRank(hoveredIdx));

        RenderFrame(
            target, sf::Color(255, 255, 255, 166),
            sf::Vector2f(m_EvaluationBarWidth + file * m_SquareSize, rank * m_SquareSize),
            sf::Vector2f(m_SquareSize, m_SquareSize),
            0.05f
        );
    }
}

void Application::renderButton(sf::RenderTarget& target, const Button& button) const {
    const float IconTextureWidth = static_cast<float>(m_IconTexture.getSize().x) / static_cast<float>(m_Buttons.size() + 4);
    const float IconTextureHeight = static_cast<float>(m_IconTexture.getSize().y);

    const float tx = static_cast<float>(button.TextureIndex) * IconTextureWidth;
    const float ty = 0.f;

    sf::Vertex vertices[] = {
        sf::Vertex(sf::Vector2f(button.Position_X, button.Position_Y), sf::Color::White, sf::Vector2f(tx, ty)),
        sf::Vertex(sf::Vector2f(button.Position_X + button.Width, button.Position_Y), sf::Color::White, sf::Vector2f(tx + IconTextureWidth, ty)),
        sf::Vertex(sf::Vector2f(button.Position_X, button.Position_Y + button.Height), sf::Color::White, sf::Vector2f(tx, ty + IconTextureHeight)),
        sf::Vertex(sf::Vector2f(button.Position_X + button.Width, button.Position_Y + button.Height), sf::Color::White, sf::Vector2f(tx + IconTextureWidth, ty + IconTextureHeight))
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip, &m_IconTexture);

    if (button.Hovered) {
        RenderRoundedQuad(
            target, sf::Color(100, 100, 100, 100),
            sf::Vector2f(button.Position_X, button.Position_Y),
            sf::Vector2f(button.Width, button.Height),
            0.4f
        );
    }

    // button is focused
    if (
        button.Hovered &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - button.HoverEnterTime
        ).count() >= Button::TooltipThreshold
    ) {
        const std::string& note = button.Note[
            !button.Note[1].empty() && sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)
        ];

        if (!note.empty()) {
            const sf::Vector2u windowSize = target.getSize();

            const float paddingH = m_SquareSize * 0.09f;
            const float paddingV = m_SquareSize * 0.07f;
            const float gap = m_SquareSize * 0.08f;
            const float arrowHalf = m_SquareSize * 0.04f;
            const float arrowDepth = m_SquareSize * 0.04f;
            const float maxBubbleW = m_SquareSize * 1.1f;

            const unsigned int charSize = static_cast<unsigned int>(m_SquareSize * 0.175f);
            const unsigned int fontSize = static_cast<unsigned int>(m_SquareSize * 0.145f);

            sf::Text text(m_Font, note, fontSize);
            text.setStyle(sf::Text::Style::Bold);

            std::string wrappedNote;
            {
                std::istringstream words(note);
                std::string word, line;

                while (words >> word) {
                    const std::string test = line.empty() ? word : (line + ' ' + word);
                    sf::Text probe(m_Font, test, fontSize);
                    probe.setStyle(sf::Text::Style::Bold);
                    if (!line.empty() && word.size() > 1 && probe.getLocalBounds().size.x + paddingH * 2.f > maxBubbleW) {
                        wrappedNote += line + '\n';
                        line = word;
                    } else {
                        line = test;
                    }
                }

                wrappedNote += line;
            }

            text.setString(wrappedNote);

            const sf::FloatRect bounds = text.getLocalBounds();
            const float bubbleW = bounds.size.x + paddingH * 2.f;
            const float bubbleH = bounds.size.y + paddingV * 2.f;

            const float buttonCY = button.Position_Y + button.Height * 0.5f;

            float bubbleX = button.Position_X - gap - arrowDepth - bubbleW;
            float bubbleY = buttonCY - bubbleH * 0.5f;

            const float margin = 4.f;
            bubbleY = std::max(margin, std::min(bubbleY, static_cast<float>(windowSize.y) - bubbleH - margin));
            bubbleX = std::max(margin, bubbleX);

            const float arrowTipY = std::clamp(
                buttonCY,
                bubbleY + arrowHalf + 6.f,
                bubbleY + bubbleH - arrowHalf - 6.f
            );

            RenderRoundedQuad(target, m_Theme->GetOverlayPill(), sf::Vector2f(bubbleX, bubbleY), sf::Vector2f(bubbleW, bubbleH), 0.18f);

            const float arrowBaseX = bubbleX + bubbleW;
            const float arrowTipX = arrowBaseX + arrowDepth;

            const sf::Vertex arrow[] = {
                sf::Vertex(sf::Vector2f(arrowBaseX, arrowTipY - arrowHalf), m_Theme->GetOverlayPill()),
                sf::Vertex(sf::Vector2f(arrowBaseX, arrowTipY + arrowHalf), m_Theme->GetOverlayPill()),
                sf::Vertex(sf::Vector2f(arrowTipX, arrowTipY), m_Theme->GetOverlayPill()),
            };

            target.draw(arrow, 3, sf::PrimitiveType::Triangles);

            text.setFillColor(m_Theme->GetTextPrimary());
            text.setOrigin(bounds.position);
            text.setPosition(sf::Vector2f(bubbleX + paddingH, bubbleY + paddingV));

            target.draw(text);
        }
    }
}

void Application::renderBitboard(sf::RenderTarget& target, uint64_t bitboard, sf::Color color) const {
    for (int idx = 0; idx < 64; ++idx) {
        if (!(bitboard & Chess::IndexToMask(idx))) continue;

        const int rank = mapRank(Chess::ToRank(idx));
        const int file = mapFile(Chess::ToFile(idx));

        const float y = rank * m_SquareSize;
        const float x = file * m_SquareSize + m_EvaluationBarWidth;

        RenderQuad(
            target, color,
            sf::Vector2f(x, y),
            sf::Vector2f(m_SquareSize, m_SquareSize)
        );
    }
}

void Application::renderPopup(sf::RenderTarget& target) const {
    const float progress = std::min(1.f, m_Popup.Timer / 0.15f);
    const float eased = progress * progress * (3.f - 2.f * progress);

    sf::Text text(m_Font, m_Popup.Info, 15);
    text.setStyle(sf::Text::Style::Bold);

    const sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin(bounds.position);

    const float paddingH = 18.f;
    const float paddingV = 24.f;
    const float pillW = bounds.size.x + paddingH * 2.f;
    const float pillH = bounds.size.y + paddingV * 2.f;
    const float margin = 12.f;

    const float targetX = static_cast<float>(target.getSize().x) - pillW - margin;
    const float targetY = static_cast<float>(target.getSize().y) - pillH - margin;

    const float slideOffset = (1.f - eased) * 18.f;
    const sf::Vector2f pillPos(targetX, targetY + slideOffset);

    RenderRoundedQuad(target, Utils::ConvertAlpha(m_Theme->GetOverlayPill(), eased), pillPos, sf::Vector2f(pillW, pillH), 0.4f);

    text.setFillColor(Utils::ConvertAlpha(m_Theme->GetTextPrimary(), eased));
    text.setPosition(sf::Vector2f(pillPos.x + paddingH, pillPos.y + paddingV));

    target.draw(text);
}

void Application::renderCheckmateOverlay(sf::RenderTarget& target) const {
    const float W = static_cast<float>(target.getSize().x);
    const float H = static_cast<float>(target.getSize().y);

    const float rawP = std::min(1.f, m_GameOverTimer / 0.45f);
    const float eased = rawP * rawP * (3.f - 2.f * rawP);

    // bg overlay
    RenderQuad(target, Utils::ConvertAlpha(m_Theme->GetSurfaceOverlay(), eased),
        sf::Vector2f(m_EvaluationBarWidth, 0.f),
        sf::Vector2f(Chess::Files * m_SquareSize, H)
    );

    // particles
    for (const GameOverParticle& p : m_GameOverParticles) {
        const float alpha = std::max(0.f, p.Life);
        sf::Color col = m_Theme->GetParticles()[p.ColorIndex];
        col.a = 255 * alpha * std::min(1.f, eased * 2.f);

        if (p.Shape == 2) {
            sf::CircleShape circle(p.Size * 0.5f);

            circle.setOrigin(sf::Vector2f(p.Size * 0.5f, p.Size * 0.5f));
            circle.setPosition(p.Position);
            circle.setFillColor(col);

            target.draw(circle);
        } else {
            sf::RectangleShape rect(sf::Vector2f(p.Size, p.Size));

            rect.setOrigin(sf::Vector2f(p.Size * 0.5f, p.Size * 0.5f));
            rect.setPosition(p.Position);
            rect.setRotation(sf::degrees(p.Shape == 1 ? p.Rotation + 45.f : p.Rotation));
            rect.setFillColor(col);

            target.draw(rect);
        }
    }

    // result card
    std::string titleStr, subStr;
    switch (m_GameOverResult) {
    case GameOverResult::Checkmate: titleStr = "CHECKMATE"; subStr = "The game is over."; break;
    case GameOverResult::Stalemate: titleStr = "STALEMATE"; subStr = "No legal moves - it's a draw."; break;
    case GameOverResult::DrawFiftyMove: titleStr = "DRAW"; subStr = "50-move rule."; break;
    case GameOverResult::DrawRepetition: titleStr = "DRAW"; subStr = "Threefold repetition."; break;
    case GameOverResult::DrawInsufficient: titleStr = "DRAW"; subStr = "Insufficient material."; break;
    default: return;
    }

    const unsigned int titleSize = static_cast<unsigned int>(m_SquareSize * 0.55f);
    const unsigned int subSize = static_cast<unsigned int>(m_SquareSize * 0.22f);

    sf::Text titleText(m_Font, titleStr, titleSize);
    titleText.setStyle(sf::Text::Style::Bold);
    sf::Text subText(m_Font, subStr, subSize);

    const sf::FloatRect tb = titleText.getLocalBounds();
    const sf::FloatRect sb = subText.getLocalBounds();

    const float cardPadH = m_SquareSize * 0.60f;
    const float cardPadV = m_SquareSize * 0.50f;
    const float cardGap = m_SquareSize * 0.20f;
    const float cardW = std::max(tb.size.x, sb.size.x) + cardPadH * 2.f;
    const float cardH = tb.size.y + sb.size.y + cardGap + cardPadV * 2.f;

    const float boardCX = m_EvaluationBarWidth + Chess::Files * m_SquareSize * 0.5f;
    const float boardCY = H * 0.5f;

    const float slideY = (1.f - eased) * m_SquareSize * 0.55f;

    const sf::Vector2f cardPos(boardCX - cardW * 0.5f, boardCY - cardH * 0.5f + slideY);

    RenderRoundedQuad(target, Utils::ConvertAlpha(m_Theme->GetSurfaceRaised(), eased), cardPos, sf::Vector2f(cardW, cardH), 0.1f);

    // decorative separator line
    const float lineW = cardW * 0.5f;
    const float lineY = cardPos.y + cardPadV + tb.size.y + cardGap * 0.45f;

    RenderQuad(target, Utils::ConvertAlpha(m_Theme->GetTextSecondary(), eased * 0.45f),
        sf::Vector2f(boardCX - lineW * 0.5f, lineY),
        sf::Vector2f(lineW, 1.5f)
    );

    // title text - horizontally centred
    titleText.setOrigin(sf::Vector2f(tb.position.x + tb.size.x * 0.5f, tb.position.y));
    titleText.setPosition(sf::Vector2f(boardCX, cardPos.y + cardPadV));
    titleText.setFillColor(Utils::ConvertAlpha(m_Theme->GetTextPrimary(), eased));
    target.draw(titleText);

    // subtitle text
    subText.setOrigin(sf::Vector2f(sb.position.x + sb.size.x * 0.5f, sb.position.y));
    subText.setPosition(sf::Vector2f(boardCX, cardPos.y + cardPadV + tb.size.y + cardGap));
    subText.setFillColor(Utils::ConvertAlpha(m_Theme->GetTextSecondary(), eased));
    target.draw(subText);
}

void Application::renderPromotionMenu(sf::RenderTarget& target, sf::Vector2i mousePos) const {
    const int boardRank = Chess::ToRank(m_SelectedPiece) + (m_SideToMove == Chess::PieceColor::White ? -1 : 1);
    const int visualRank = mapRank(boardRank);

    const float menuX = mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth;

    const bool openDown = visualRank < Chess::Ranks / 2;

    const float boardLeft = m_EvaluationBarWidth;
    const float boardRight = boardLeft + Chess::Files * m_SquareSize;
    const float boardHeight = static_cast<float>(target.getSize().y);

    const float cardTop = openDown ? visualRank * m_SquareSize : (visualRank - 3.f) * m_SquareSize;
    const float cardBottom = cardTop + 4.f * m_SquareSize;

    const sf::Color dim(0, 0, 0, 130);

    if (menuX > boardLeft) {
        RenderQuad(target, dim,
            sf::Vector2f(boardLeft, 0.f), sf::Vector2f(menuX - boardLeft, boardHeight)
        );
    }

    if (menuX + m_SquareSize < boardRight) {
        RenderQuad(target, dim,
            sf::Vector2f(menuX + m_SquareSize, 0.f), sf::Vector2f(boardRight - menuX - m_SquareSize, boardHeight)
        );
    }

    if (cardTop > 0.f) {
        RenderQuad(target, dim,
            sf::Vector2f(menuX, 0.f), sf::Vector2f(m_SquareSize, cardTop)
        );
    }

    if (cardBottom < boardHeight) {
        RenderQuad(target, dim,
            sf::Vector2f(menuX, cardBottom), sf::Vector2f(m_SquareSize, boardHeight - cardBottom)
        );
    }

    RenderRoundedQuad(
        target, sf::Color(255, 255, 255, 245),
        sf::Vector2f(menuX, cardTop), sf::Vector2f(m_SquareSize, 4.f * m_SquareSize),
        0.12f
    );

    unsigned int i = 0u;
    for (Chess::PieceType promotionType : Chess::PromotionTypes) {
        const float y = openDown ? (visualRank + i) * m_SquareSize : (visualRank - i) * m_SquareSize;
        renderPiece(target, Chess::Piece{promotionType, m_SideToMove}, menuX, y);

        ++i;
    }
}

void Application::renderArrow(sf::RenderTarget& target, Arrow arrow) const {
    const float arrowWidth = m_SquareSize * 0.185f;
    const float headLength = m_SquareSize * 0.40f;
    const float headWidth = m_SquareSize * 0.45f;

    const float halfSquareSize = m_SquareSize * 0.5f;

    const sf::Vector2f startPos(
        mapFile(Chess::ToFile(arrow.Start)) * m_SquareSize + m_EvaluationBarWidth + halfSquareSize,
        mapRank(Chess::ToRank(arrow.Start)) * m_SquareSize + halfSquareSize
    );

    const sf::Vector2f endPos(
        mapFile(Chess::ToFile(arrow.End)) * m_SquareSize + m_EvaluationBarWidth + halfSquareSize,
        mapRank(Chess::ToRank(arrow.End)) * m_SquareSize + halfSquareSize
    );

    const int rankDiff = std::abs(
        Chess::ToRank(arrow.End) - Chess::ToRank(arrow.Start)
    );

    const int fileDiff = std::abs(
        Chess::ToFile(arrow.End) - Chess::ToFile(arrow.Start)
    );

    // knight move
    if (
        m_Theme->DrawKnightArrowAsL() &&
        ((rankDiff == 2 && fileDiff == 1) || (rankDiff == 1 && fileDiff == 2))
    ) {
        const sf::Vector2f midPos = (rankDiff == 2 && fileDiff == 1)
            ? sf::Vector2f(startPos.x, endPos.y) : sf::Vector2f(endPos.x, startPos.y);

        const sf::Vector2f dir1 = midPos - startPos;
        const float len1 = std::sqrtf(dir1.x * dir1.x + dir1.y * dir1.y);

        const sf::Vector2f dir2 = endPos - midPos;
        const float len2 = std::sqrtf(dir2.x * dir2.x + dir2.y * dir2.y);

        if (len1 > 0.f && len2 > 0.f) {
            const sf::Vector2f norm1 = sf::Vector2f(dir1.x / len1, dir1.y / len1);
            const sf::Vector2f perp1 = sf::Vector2f(-norm1.y, norm1.x);

            const sf::Vector2f norm2 = sf::Vector2f(dir2.x / len2, dir2.y / len2);
            const sf::Vector2f perp2 = sf::Vector2f(-norm2.y, norm2.x);

            const sf::Vector2f seg1End = midPos + norm1 * (arrowWidth * 0.5f);

            const sf::Vector2f seg2Start = midPos + norm2 * (arrowWidth * 0.5f);
            const sf::Vector2f shaftEnd = endPos - norm2 * headLength;

            // first segment
            const sf::Vertex shaft1[] = {
                sf::Vertex(startPos + perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(startPos - perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(seg1End + perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(seg1End - perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow())
            };

            target.draw(shaft1, 4, sf::PrimitiveType::TriangleStrip);

            // second segment
            const sf::Vertex shaft2[] = {
                sf::Vertex(seg2Start + perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(seg2Start - perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(shaftEnd + perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(shaftEnd - perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow())
            };

            target.draw(shaft2, 4, sf::PrimitiveType::TriangleStrip);

            // head
            const sf::Vertex head[] = {
                sf::Vertex(shaftEnd + perp2 * (headWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(endPos, m_Theme->GetArrow()),
                sf::Vertex(shaftEnd - perp2 * (headWidth * 0.5f), m_Theme->GetArrow())
            };

            target.draw(head, 3, sf::PrimitiveType::Triangles);
        }
    } else {
        const sf::Vector2f dir = endPos - startPos;
        const float len = std::sqrtf(dir.x * dir.x + dir.y * dir.y);

        if (len == 0.f) return;

        const sf::Vector2f norm = sf::Vector2f(dir.x / len, dir.y / len);
        const sf::Vector2f perp = sf::Vector2f(-norm.y, norm.x);

        const float shortenStart = m_SquareSize * 0.15f;
        const float shortenEnd = m_SquareSize * 0.15f;

        const sf::Vector2f adjustedStart = startPos + norm * shortenStart;
        const sf::Vector2f adjustedEnd = endPos - norm * shortenEnd;
        const sf::Vector2f shaftEnd = adjustedEnd - norm * headLength;

        // shaft
        const sf::Vertex shaft[] = {
            sf::Vertex(adjustedStart + perp * (arrowWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(adjustedStart - perp * (arrowWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(shaftEnd + perp * (arrowWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(shaftEnd - perp * (arrowWidth * 0.5f), m_Theme->GetArrow())
        };

        target.draw(shaft, 4, sf::PrimitiveType::TriangleStrip);

        // head
        const sf::Vertex head[] = {
            sf::Vertex(shaftEnd + perp * (headWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(adjustedEnd, m_Theme->GetArrow()),
            sf::Vertex(shaftEnd - perp * (headWidth * 0.5f), m_Theme->GetArrow())
        };

        target.draw(head, 3, sf::PrimitiveType::Triangles);
    }
}

void Application::Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const {
    const float halfSquareSize = m_SquareSize * 0.5f;
    const bool mouseHeld = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    target.clear(m_Theme->GetBackground());

    /* Board */ {
        renderCheckerboard(target);

        renderRanksAndFiles(target);

        renderEvaluationBar(target);

        if (m_InspectionMode) {
            RenderQuad(
                target, sf::Color(120, 120, 120, 90 * m_InspectionModeOverlay_t),
                sf::Vector2f(m_EvaluationBarWidth, 0.f), sf::Vector2f(Chess::Files, Chess::Ranks) * m_SquareSize
            );
        }
    }

    /* Highlights */ {
        // markers
        for (uint64_t tmp = m_Markers; tmp; tmp &= tmp - 1) {
            const int idx = Chess::MaskToIndex(tmp);

            m_Theme->RenderMarker(
                target,
                sf::Vector2f(
                    mapFile(Chess::ToFile(idx)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(idx)) * m_SquareSize
                ),
                m_SquareSize
            );
        }

        // move history
        if (m_LastMove) {
            renderSquareHighlight(target, m_LastMove.StartingSquare, m_Theme->GetLastMoveHighlight());

            if (m_Theme->DrawLastMoveTargetIfCapturable() || !std::any_of(
                m_LegalMovesForSelectedPiece.begin(), m_LegalMovesForSelectedPiece.end(),
                [this](Chess::Move move) -> bool {
                    return move.TargetSquare == m_LastMove.TargetSquare;
                }
            )) {
                renderSquareHighlight(target, m_LastMove.TargetSquare, m_Theme->GetLastMoveHighlight());
            }
        }

        // selected piece
        if (hasSelectedPiece()) {
            if (m_Theme->DrawSelectedPieceGhost()) {
                renderPiece(
                    target, m_Board.GetPieceAt(m_SelectedPiece),
                    mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(m_SelectedPiece)) * m_SquareSize
                );
            }

            renderSquareHighlight(target, m_SelectedPiece, m_Theme->GetSelectedPiece());
        }

        renderLegalMoves(target, mousePosition);
    }

    /* Pieces */ {
        // all pieces
        renderPieces(target, mouseHeld);

        // piece animations
        for (const PieceMoveAnim& anim : m_PieceAnimations) {
            const sf::Vector2f start = sf::Vector2f(
                mapFile(Chess::ToFile(anim.Move.TargetSquare)), mapRank(Chess::ToRank(anim.Move.TargetSquare))
            );

            const sf::Vector2f end = sf::Vector2f(
                mapFile(Chess::ToFile(anim.Move.StartingSquare)), mapRank(Chess::ToRank(anim.Move.StartingSquare))
            );

            const sf::Vector2f dir = end - start;
            const sf::Vector2f position = (start + dir * anim.Timer) * m_SquareSize;

            const float len = std::sqrtf(dir.x * dir.x + dir.y * dir.y);
            const float animTilt = len ? -dir.x / len * 1.2f * anim.Timer * (1.f - anim.Timer) : 0.f;

            renderPiece(
                target, m_Board.GetPieceAt(anim.Move.TargetSquare),
                position.x + m_EvaluationBarWidth, position.y,
                animTilt
            );
        }
    }

    /* UI */ {
        // arrows
        if (m_CurrentlyDrawingArrow) renderArrow(target, m_CurrentlyDrawingArrow);

        for (Arrow arrow : m_Arrows) {
            renderArrow(target, arrow);
        }

        // buttons
        for (const Button& button : m_Buttons) {
            renderButton(target, button);
        }

        // panel mask (for fade effect)
        RenderQuad(
            target, Utils::ConvertAlpha(m_Theme->GetBackground(), 1.f - m_PanelBrightness),
            sf::Vector2f(m_EvaluationBarWidth + Chess::Files * m_SquareSize, 0.f),
            sf::Vector2f(m_PanelWidth, target.getSize().y)
        );

        // dragged piece
        if (mouseHeld && hasSelectedPiece()) {
            renderPiece(
                target, m_Board.GetPieceAt(m_SelectedPiece),
                mousePosition.x - halfSquareSize, mousePosition.y - halfSquareSize,
                m_DragTilt
            );
        }

        if (m_GameOverResult != GameOverResult::None) {
            renderCheckmateOverlay(target);
        }

        if (m_PromotionSelectionActive) {
            renderPromotionMenu(target, mousePosition);
        }

        if (m_Popup.isActive()) {
            renderPopup(target);
        }
    }
}