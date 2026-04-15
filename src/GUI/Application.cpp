#include <iostream>

#include "GUI/Application.hpp"

#pragma region Utils

namespace Utils {
    [[nodiscard]]
    static float ExponentiallyMoveTo(float from, float to, float speed, float snapThreshold) noexcept {
        const float diff = to - from;
        return std::fabs(diff) < snapThreshold ? to : (from + diff * (1.f - std::exp(-speed)));
    }

    [[nodiscard]]
    static sf::Color ConvertAlpha(sf::Color color, float factor) noexcept {
        color.a *= factor;
        return color;
    }
}

#pragma region Themes

namespace LichessTheme {
    constexpr sf::Color Checkerboard[] = {
        sf::Color(240, 217, 181), sf::Color(181, 136, 99)
    };

    constexpr sf::Color EvaluationBar[] = {
        sf::Color(255, 255, 255), sf::Color(64, 61, 57)
    };

    constexpr sf::Color LastMoveHighlight = sf::Color(155, 200, 0, 105);
    constexpr sf::Color LegalMoveHighlight = sf::Color(20, 85, 29, 128);

    constexpr sf::Color PopupBackground = sf::Color(28, 27, 25, 230);
    constexpr sf::Color PopupText = sf::Color(240, 217, 181);

    constexpr sf::Color Background = sf::Color(48, 46, 43);

    constexpr sf::Color GameOverOverlayBg = sf::Color(18, 17, 15, 195);
    constexpr sf::Color GameOverCardBg = sf::Color(42, 40, 37, 248);
    constexpr sf::Color GameOverTitleText = sf::Color(240, 217, 181, 255);
    constexpr sf::Color GameOverSubText = sf::Color(180, 160, 130, 255);
    constexpr sf::Color GameOverParticle[] = {
        sf::Color(240, 217, 181),
        sf::Color(181, 136, 99),
        sf::Color(200, 185, 150),
        sf::Color(255, 230, 180),
        sf::Color(120, 100, 75),
    };
}

namespace ChesscomTheme {
    constexpr sf::Color Checkerboard[] = {
        sf::Color(235, 236, 208), sf::Color(115, 149, 82)
    };

    constexpr sf::Color EvaluationBar[] = {
        sf::Color(255, 255, 255), sf::Color(64, 61, 57)
    };

    constexpr sf::Color LastMoveHighlight = sf::Color(255, 255, 52, 128);
    constexpr sf::Color LegalMoveHighlight = sf::Color(2, 1, 0, 36);

    constexpr sf::Color PopupBackground = sf::Color(28, 27, 25, 230);
    constexpr sf::Color PopupText = sf::Color(235, 236, 208);

    constexpr sf::Color Background = sf::Color(48, 46, 43);

    constexpr sf::Color GameOverOverlayBg = sf::Color(18, 17, 15, 195);
    constexpr sf::Color GameOverCardBg = sf::Color(40, 46, 36, 248);
    constexpr sf::Color GameOverTitleText = sf::Color(235, 236, 208, 255);
    constexpr sf::Color GameOverSubText = sf::Color(160, 185, 130, 255);
    constexpr sf::Color GameOverParticle[] = {
        sf::Color(235, 236, 208),
        sf::Color(115, 149, 82),
        sf::Color(180, 200, 140),
        sf::Color(255, 240, 180),
        sf::Color(80, 110, 55),
    };
}

namespace Theme = ChesscomTheme;

#pragma region Setup

Application::Application() {
    Chess::Init();

    m_Board.SetHashSize(128);
    m_Board.SetCatchAll(false);

    m_SideToMove = m_Board.LoadFromFen(Chess::DefaultFEN);
    m_LatestEvaluation = 0.5f;
}

void Application::startNewGame() {
    joinThreads();

    m_SideToMove = m_Board.LoadFromFen(Chess::DefaultFEN);
    m_LatestEvaluation = 0.5f;
    m_GameOver = false;
    m_Flipped = false;
    m_LastMove = Chess::Move();

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

    m_CheckerboardMesh = generateCheckerboardMesh(m_SquareSize, m_EvaluationBarWidth, Theme::Checkerboard);
}

void Application::initUserInterface(sf::Vector2u windowSize) {
    const float margin = windowSize.x - (m_SquareSize * Chess::Ranks);

    // allocate window space into 3 sections
    // evaluation bar, chess board, button panel
    const float Dist = 0.6f;

    m_EvaluationBarWidth = margin * Dist;
    m_PanelWidth = margin * (1.f - Dist);

    const float padding_y = static_cast<float>(windowSize.y * 0.006f);
    const float padding_x = m_PanelWidth * 0.11f;

    const float panelStartPos = static_cast<float>(windowSize.x) - m_PanelWidth;

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
            m_SfxPlayer.Play(Sfx::BoardFlip);
        }
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // get engine move button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        1,
        [this](Button&) -> void {
            if (!m_EngineThinking && !m_GameOver) pollEngineMove();
        }
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // cancel search button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        2,
        [this](Button&) -> void {
            if (m_EngineThinking) m_Board.CancelSearch();
        }
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // own book button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        5 - m_UseOwnBook,
        [this](Button& button) -> void {
            m_UseOwnBook ^= 1;

            button.TextureIndex = 5 - m_UseOwnBook;
            m_Popup = Popup("own book: " + std::string(m_UseOwnBook ? "true" : "false"));
        }
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // ponder button
    m_Buttons.emplace_back(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        7 - m_Ponder,
        [this](Button& button) -> void {
            m_Ponder ^= 1;

            if (!m_EngineThinking) {
                if (m_Ponder) startPonder();
                else stopPonder();
            }

            button.TextureIndex = 7 - m_Ponder;
            m_Popup = Popup("ponder: " + std::string(m_Ponder ? "true" : "false"));
        }
    );

    currentYPos += buttonSize + padding_y * 2.f;

    // board reset button
    m_Buttons.emplace_back(
        currentXPos, static_cast<float>(windowSize.y) - buttonSize - padding_y,
        buttonSize, buttonSize,
        3,
        [this](Button&) -> void {
            startNewGame();
        }
    );
}

sf::VertexArray Application::generateCheckerboardMesh(float squareSize, float offset_x, const sf::Color* colors) {
    constexpr std::size_t VertexCount = Chess::Files * Chess::Ranks * 6u;
    sf::VertexArray vertices(sf::PrimitiveType::Triangles, VertexCount);

    unsigned int vertexIndex = 0u;

    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(rank) * m_SquareSize;

        for (int file = 0; file < Chess::Files; ++file) {
            const float x = static_cast<float>(file) * m_SquareSize + offset_x;

            const sf::Color color = colors[(file + rank) % 2];

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

    return vertices;
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

    if (!m_SfxPlayer.LoadFromFile(Sfx::Place1, soundPath / "Place1.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Place2, soundPath / "Place2.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Promotion, soundPath / "Promotion.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::SpecialMove, soundPath / "SpecialMove.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    // no errors in loading the assets
    return true;
}

#pragma region Events

void Application::HandleKeyPressed(sf::Keyboard::Scancode key) {
    if (key == sf::Keyboard::Scancode::V) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            joinThreads();
            m_SideToMove = m_Board.LoadFromFen(sf::Clipboard::getString());
            m_Board.SetEngineColor(m_SideToMove);
            updateEvaluation();
            m_GameOver = m_Flipped = false;
            m_LastMove = Chess::Move();
            m_Board.ClearHash();
            startPonder();
        }
    }

    else if (key == sf::Keyboard::Scancode::C) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            sf::Clipboard::setString(m_Board.GetFen(m_SideToMove));
        }
    }

    else if (key == sf::Keyboard::Scancode::T) {
        joinThreads();

        m_Board.SetCatchAll(!m_Board.GetCatchAll());
        m_Board.ClearHash();
        updateEvaluation();
        m_Popup = Popup("catch all mode: " + std::string(m_Board.GetCatchAll() ? "true" : "false"));
    }

    else if (
        key == sf::Keyboard::Scancode::Up ||
        key == sf::Keyboard::Scancode::Down
    ) {
        const char dir = key == sf::Keyboard::Scancode::Up ? 1 : -1;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            m_EvaluationDepth = std::max(1, m_EvaluationDepth + dir);
            updateEvaluation();
            m_Popup = Popup("evaluation depth: " + std::to_string(m_EvaluationDepth));
        } else {
            m_EngineThinkTimeMs = std::max(50, m_EngineThinkTimeMs + dir * 50);
            m_Popup = Popup("think time: " + std::to_string(m_EngineThinkTimeMs) + " ms");
        }
    }
}

void Application::HandleMouseButtonPressed(sf::Vector2i position) {
    for (Button& button : m_Buttons) {
        if (button.Hovered) {
            if (button.Callback) button.Callback(button);
            return;
        }
    }

    onMouseButtonSignal(position, false);
}

void Application::HandleMouseButtonReleased(sf::Vector2i position) {
    onMouseButtonSignal(position, true);
}

void Application::HandleMouseMoved(sf::Vector2i position) {
    for (Button& button : m_Buttons) {
        button.Hovered = false;
    }

    const bool hoveringPanel = position.x > m_EvaluationBarWidth + m_SquareSize * Chess::Files;

    if (hoveringPanel) {
        for (Button& button : m_Buttons) {
            if (
                position.x >= button.Position_X && position.x <= (button.Position_X + button.Width) &&
                position.y >= button.Position_Y && position.y <= (button.Position_Y + button.Height)
            ) {
                button.Hovered = true;
                break;
            }
        }
    }

    m_HoveringPanel = hoveringPanel;
}

void Application::HandleMouseLeftWindow() {
    for (Button& button : m_Buttons) {
        button.Hovered = false;
    }

    m_HoveringPanel = false;
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

        m_GameOver = true;
    }

    // play sound effects
    if (HasFlag(move.Flag, Chess::MoveFlag::Check)) {
        m_SfxPlayer.Play(Sfx::Check);
    } else if (
        isCastle(move.Flag)
    ) {
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
        m_SfxPlayer.Play((std::rand() % 2) ? Sfx::Place1 : Sfx::Place2);
    }

    m_LastMove = move;

    if (animate) m_PieceAnimations.emplace_back(move);

    updateEvaluation();
}

void Application::onMouseButtonSignal(sf::Vector2i position, bool released) {
    if (m_GameOver) return;

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
        doMove(*it, animate);

        if (!m_GameOver) pollEngineMove();
    }

    m_SelectedPiece = Chess::NullPos;
    m_LegalMovesForSelectedPiece.clear();
}

#pragma region Ponder

void Application::startPonder() {
    if (!m_Ponder || m_GameOver) return;

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
        m_Popup.Timer = std::max(0.f, m_Popup.Timer - deltaTime);
        if (m_Popup.Timer == 0.f) m_Popup.Info.clear();
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
        m_CurrentEvaluation, m_LatestEvaluation, 7.5f * deltaTime, 0.002f
    );

    m_PanelBrightness = Utils::ExponentiallyMoveTo(
        m_PanelBrightness, m_HoveringPanel, 20.f * deltaTime, 0.002f
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

static void RenderCaptureOverlay(
    sf::RenderTarget& target,
    sf::Color color,
    sf::Vector2f position,
    sf::Vector2f size,
    float triangleLength
) {
    const sf::Vector2f triangleSize = size * triangleLength;

    const sf::Vertex topLeft[] = {
        sf::Vertex(position, color),
        sf::Vertex(sf::Vector2f(position.x + triangleSize.x, position.y), color),
        sf::Vertex(sf::Vector2f(position.x, position.y + triangleSize.y), color)
    };

    const sf::Vertex topRight[] = {
        sf::Vertex(sf::Vector2f(position.x + size.x, position.y), color),
        sf::Vertex(sf::Vector2f(position.x + size.x - triangleSize.x, position.y), color),
        sf::Vertex(sf::Vector2f(position.x + size.x, position.y + triangleSize.y), color)
    };

    const sf::Vertex bottomLeft[] = {
        sf::Vertex(sf::Vector2f(position.x, position.y + size.y), color),
        sf::Vertex(sf::Vector2f(position.x + triangleSize.x, position.y + size.y), color),
        sf::Vertex(sf::Vector2f(position.x, position.y + size.y - triangleSize.y), color)
    };

    const sf::Vertex bottomRight[] = {
        sf::Vertex(sf::Vector2f(position.x + size.x, position.y + size.y), color),
        sf::Vertex(sf::Vector2f(position.x + size.x - triangleSize.x, position.y + size.y), color),
        sf::Vertex(sf::Vector2f(position.x + size.x, position.y + size.y - triangleSize.y), color)
    };

    target.draw(topLeft, 3, sf::PrimitiveType::Triangles);
    target.draw(topRight, 3, sf::PrimitiveType::Triangles);
    target.draw(bottomLeft, 3, sf::PrimitiveType::Triangles);
    target.draw(bottomRight, 3, sf::PrimitiveType::Triangles);
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
        text.setFillColor(Theme::Checkerboard[(rank + m_Flipped) % 2]);

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
        text.setFillColor(Theme::Checkerboard[file % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        text.setPosition(sf::Vector2f(
            x + m_SquareSize - bounds.size.x - padding,
            Chess::Ranks * m_SquareSize - bounds.size.y - padding
        ));

        target.draw(text);
    }
}

void Application::renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y) const {
    const float PieceTextureWidth = static_cast<float>(m_PieceTexture.getSize().x) / 6.f;
    const float PieceTextureHeight = static_cast<float>(m_PieceTexture.getSize().y) / 2.f;

    const float tx = static_cast<float>(piece.Type) * PieceTextureWidth;
    const float ty = static_cast<float>(piece.Color) * PieceTextureHeight;

    const sf::Vertex vertices[] = {
        sf::Vertex(
            sf::Vector2f(x, y),
            sf::Color::White,
            sf::Vector2f(tx, ty)
        ),
            sf::Vertex(
                sf::Vector2f(x + m_SquareSize, y),
                sf::Color::White,
                sf::Vector2f(tx + PieceTextureWidth, ty)
            ),
            sf::Vertex(
                sf::Vector2f(x, y + m_SquareSize),
                sf::Color::White,
                sf::Vector2f(tx, ty + PieceTextureHeight)
            ),
            sf::Vertex(
                sf::Vector2f(x + m_SquareSize, y + m_SquareSize),
                sf::Color::White,
                sf::Vector2f(tx + PieceTextureWidth, ty + PieceTextureHeight)
            )
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
            bool found = false;

            for (const PieceMoveAnim& anim : m_PieceAnimations) {
                if (anim.Move.TargetSquare == idx) {
                    found = true;
                    break;
                }
            }

            if (found) continue;

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
    topFill.setFillColor(Theme::EvaluationBar[!m_Flipped]);
    barRT.draw(topFill, sf::BlendMultiply);

    sf::RectangleShape bottomFill(sf::Vector2f(barWidth, barHeight - midPos));
    bottomFill.setPosition(sf::Vector2f(0.f, midPos));
    bottomFill.setFillColor(Theme::EvaluationBar[m_Flipped]);
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
        const unsigned int rank = mapRank(Chess::ToRank(move.TargetSquare));
        const unsigned int file = mapFile(Chess::ToFile(move.TargetSquare));

        const float y = rank * m_SquareSize;
        const float x = file * m_SquareSize + m_EvaluationBarWidth;

        if (move.TargetSquare == hoveredIdx) {
            RenderQuad(target, Theme::LegalMoveHighlight, sf::Vector2f(x, y), sf::Vector2f(m_SquareSize, m_SquareSize));
        } else if (m_Board.GetOccupancyMap(Chess::InvertColor(m_SideToMove)) & Chess::IndexToMask(move.TargetSquare)) {
            RenderCaptureOverlay(
                target, Theme::LegalMoveHighlight,
                sf::Vector2f(x, y), sf::Vector2f(m_SquareSize, m_SquareSize),
                0.167f
            );
        } else {
            const float circleRadius = m_SquareSize * 0.15f;

            sf::CircleShape circle(circleRadius);

            const float offset = m_SquareSize * 0.5f - circleRadius;
            circle.setPosition(sf::Vector2f(x + offset, y + offset));
            circle.setFillColor(Theme::LegalMoveHighlight);

            target.draw(circle);
        }
    }
}

void Application::renderButton(sf::RenderTarget& target, const Button& button) const {
    const float IconTextureWidth = static_cast<float>(m_IconTexture.getSize().x) / static_cast<float>(m_Buttons.size() + 2);
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
}

void Application::renderBitboard(sf::RenderTarget& target, uint64_t bitboard, sf::Color color) const {
    for (int idx = 0; idx < 64; ++idx) {
        if (!(bitboard & Chess::IndexToMask(idx))) continue;

        const unsigned int rank = mapRank(Chess::ToRank(idx));
        const unsigned int file = mapFile(Chess::ToFile(idx));

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

    const sf::Color bgColor = Utils::ConvertAlpha(Theme::PopupBackground, eased);
    const sf::Color textColor = Utils::ConvertAlpha(Theme::PopupText, eased);

    RenderRoundedQuad(target, bgColor, pillPos, sf::Vector2f(pillW, pillH), 0.4f);

    text.setFillColor(textColor);
    text.setPosition(sf::Vector2f(pillPos.x + paddingH, pillPos.y + paddingV));

    target.draw(text);
}

void Application::renderCheckmateOverlay(sf::RenderTarget& target) const {
    const float W = static_cast<float>(target.getSize().x);
    const float H = static_cast<float>(target.getSize().y);

    const float rawP = std::min(1.f, m_GameOverTimer / 0.45f);
    const float eased = rawP * rawP * (3.f - 2.f * rawP);

    // bg overlay
    const sf::Color bgColor = Utils::ConvertAlpha(Theme::GameOverOverlayBg, eased);

    RenderQuad(target, bgColor,
        sf::Vector2f(m_EvaluationBarWidth, 0.f),
        sf::Vector2f(Chess::Files * m_SquareSize, H)
    );

    // particles
    for (const GameOverParticle& p : m_GameOverParticles) {
        const float alpha = std::max(0.f, p.Life);
        sf::Color col = Theme::GameOverParticle[p.ColorIndex];
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

    const sf::Color cardBg = Utils::ConvertAlpha(Theme::GameOverCardBg, eased);
    RenderRoundedQuad(target, cardBg, cardPos, sf::Vector2f(cardW, cardH), 0.1f);

    // decorative separator line
    const float lineW = cardW * 0.5f;
    const float lineY = cardPos.y + cardPadV + tb.size.y + cardGap * 0.45f;

    const sf::Color lineColor = Utils::ConvertAlpha(Theme::GameOverSubText, eased * 0.45f);

    RenderQuad(target, lineColor,
        sf::Vector2f(boardCX - lineW * 0.5f, lineY),
        sf::Vector2f(lineW, 1.5f)
    );

    // title text - horizontally centred
    const sf::Color titleCol = Utils::ConvertAlpha(Theme::GameOverTitleText, eased);
    titleText.setOrigin(sf::Vector2f(tb.position.x + tb.size.x * 0.5f, tb.position.y));
    titleText.setPosition(sf::Vector2f(boardCX, cardPos.y + cardPadV));
    titleText.setFillColor(titleCol);
    target.draw(titleText);

    // subtitle text
    const sf::Color subCol = Utils::ConvertAlpha(Theme::GameOverSubText, eased);
    subText.setOrigin(sf::Vector2f(sb.position.x + sb.size.x * 0.5f, sb.position.y));
    subText.setPosition(sf::Vector2f(boardCX, cardPos.y + cardPadV + tb.size.y + cardGap));
    subText.setFillColor(subCol);
    target.draw(subText);
}

void Application::Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const {
    const float halfSquareSize = m_SquareSize * 0.5f;
    const bool mouseHeld = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    target.clear(Theme::Background);

    /* Board */ {
        target.draw(m_CheckerboardMesh);

        renderRanksAndFiles(target);

        renderEvaluationBar(target);
    }

    /* Highlights */ {
        // move history
        if (m_LastMove) {
            RenderQuad(
                target, Theme::LastMoveHighlight,
                sf::Vector2f(
                    mapFile(Chess::ToFile(m_LastMove.StartingSquare)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(m_LastMove.StartingSquare)) * m_SquareSize
                ),
                sf::Vector2f(m_SquareSize, m_SquareSize)
            );

            RenderQuad(
                target, Theme::LastMoveHighlight,
                sf::Vector2f(
                    mapFile(Chess::ToFile(m_LastMove.TargetSquare)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(m_LastMove.TargetSquare)) * m_SquareSize
                ),
                sf::Vector2f(m_SquareSize, m_SquareSize)
            );
        }

        // selected piece
        if (hasSelectedPiece()) {
            RenderQuad(
                target, Theme::LastMoveHighlight,
                sf::Vector2f(
                    mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(m_SelectedPiece)) * m_SquareSize
                ),
                sf::Vector2f(m_SquareSize, m_SquareSize)
            );
        }

        // legal move
        if (!m_LegalMovesForSelectedPiece.empty()) {
            renderLegalMoves(target, mousePosition);
        }
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

            const sf::Vector2f position = (start + (end - start) * anim.Timer) * m_SquareSize;

            renderPiece(
                target, m_Board.GetPieceAt(anim.Move.TargetSquare),
                position.x + m_EvaluationBarWidth, position.y
            );
        }

        // dragged piece
        if (mouseHeld && hasSelectedPiece()) {
            renderPiece(
                target, m_Board.GetPieceAt(m_SelectedPiece),
                static_cast<float>(mousePosition.x) - halfSquareSize,
                static_cast<float>(mousePosition.y) - halfSquareSize
            );
        }
    }

    /* UI */ {
        // buttons
        for (const Button& button : m_Buttons) {
            renderButton(target, button);
        }

        // panel mask (for fade effect)
        const sf::Color maskColor = Utils::ConvertAlpha(Theme::Background, 1.f - m_PanelBrightness);

        RenderQuad(
            target, maskColor,
            sf::Vector2f(target.getSize().x - m_PanelWidth, 0.f),
            sf::Vector2f(m_PanelWidth, target.getSize().y)
        );

        if (m_Popup.isActive()) {
            renderPopup(target);
        }

        if (m_GameOverResult != GameOverResult::None) {
            renderCheckmateOverlay(target);
        }
    }
}