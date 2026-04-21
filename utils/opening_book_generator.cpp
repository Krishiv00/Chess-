#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstring>

#include "../include/Engine/Chess.hpp"

struct BookEntry {
    uint64_t Hash;
    uint8_t StartSquare;
    uint8_t TargetSquare;
    uint16_t Flag;
};

std::vector<BookEntry> g_BookEntries;

static void AddLine(const std::string& moves) {
    Chess::Board board;

    Chess::PieceColor sideToMove = board.LoadFromFen(Chess::DefaultFEN);

    std::istringstream iss(moves);
    std::string uciMove;

    unsigned int numMovesAdded = 0u;

    while (iss >> uciMove) {
        // Parse UCI move
        Chess::Move move(
            Chess::To2DIndex('8' - uciMove[1], uciMove[0] - 'a'),
            Chess::To2DIndex('8' - uciMove[3], uciMove[2] - 'a')
        );

        const std::vector<Chess::Move> legalMoves = board.GetLegalMoves(move.StartingSquare);

        const auto it = std::find(legalMoves.begin(), legalMoves.end(), move);

        // Move is illegal
        if (it == legalMoves.end()) {
            // delete the entire line
            if (numMovesAdded > 0u) {
                g_BookEntries.erase(g_BookEntries.end() - numMovesAdded, g_BookEntries.end());
            }

            break;
        }

        move = *it;

        // Check for promotion
        if (uciMove.length() == 5) {
            switch (uciMove[4]) {
            case 'q': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToQueen); break;
            case 'r': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToRook); break;
            case 'n': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToKnight); break;
            case 'b': move.Flag = Chess::AddFlag(move.Flag, Chess::MoveFlag::PromoteToBishop); break;
            }
        }

        // Add to book
        g_BookEntries.emplace_back(
            board.m_Hash,
            move.StartingSquare, move.TargetSquare,
            static_cast<uint16_t>(move.Flag)
        );

        ++numMovesAdded;

        // Make move
        board.DoMove(move);
        sideToMove = InvertColor(sideToMove);
    }
}

static void AddAllOpenings() {
    // ========================================
    // KING'S PAWN OPENINGS (1.e4) - EXPANDED
    // ========================================

    // ========== RUY LOPEZ (Spanish Opening) ==========

    // Main Line - Closed Ruy Lopez
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6b8");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6a5 b3c2 c7c5 d2d4 d8c7");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 h7h6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 e8g8 c2c3 d7d6 h2h3 c6a5");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 e8g8 a2a4");

    // Open Ruy Lopez - Marshall Attack
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 e8g8 c2c3 d7d5 e4d5 f6d5 f3e5 c6e5 e1e5 c7c6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 e8g8 c2c3 d7d5 e4d5 f6d5 f3e5 c6e5 e1e5 d5f6");

    // Breyer Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6b8 d2d4 b8d7");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6b8 d2d4 b8d7");

    // Chigorin Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6a5 b3c2 c7c5 d2d4 d8c7 b1d2");

    // Zaitsev Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 e7b7 d2d4");

    // Berlin Defense - Main Lines
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 d2d4 e4d6 b5c6 d7c6 d4e5 d6f5 d1d8 e8d8");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 d2d4 e4d6 b5c6 d7c6 d4e5 d6f5 d1d8 e8d8 b1c3");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 d2d4 f8e7 f1e1 e4d6 f3e5 e8g8 b5f1");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 f1e1 e4d6 f3e5 f8e7 b5f1 c6e5 e1e5 e8g8");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 d2d3 f8c5 c2c3 e8g8 e1g1 d7d6");

    // Schliemann (Jaenisch) Gambit
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 f7f5 b1c3 f5e4 c3e4 g8f6 d1e2 d7d5");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 f7f5 d2d4 f5e4 f3e5 c6e5 d4e5 c7c6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 f7f5 e1g1 f5e4 f3e5 c6e5 d2d4 e5g6");

    // Exchange Variation
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 e1g1 f7f6 d2d4 e5d4 f3d4 c7c5");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 e1g1 f8g4 h2h3 g4h5 d2d3");

    // ========== ITALIAN GAME ==========

    // Giuoco Piano - Main Lines
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4 e1g1 b4c3");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4 e1g1 e4c3 b2c3 b4c3");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6 b2b4 c5b6 a2a4 a7a6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 a7a6 c4b3 b8a5 b1d2 d7d6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4 c5b6 b4b5 c6a5 f3e5 g8h6");

    // Giuoco Pianissimo
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 d2d3 g8f6 e1g1 d7d6 c2c3 e8g8 f1e1 a7a6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6 b2b4 c5b6 a2a4 a7a5");

    // Two Knights Defense - Fried Liver Attack
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 f6d5 g5f7 e8f7 d1f3 f7e6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 c6a5 c4b5 c7c6 d5c6 b7c6 b5e2");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4 e5d4 e1g1 f6e4 f1e1 d7d5 c4d5 d8d5 b1c3");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4 e5d4 e4e5 d7d5 c4b5 f6e4 f3d4 f8c5");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8e7 e1g1 e8g8 c2c3 d7d6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 b1c3 f6e4 c3e4 d7d5 c4d3");

    // Evans Gambit
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4 c5b4 c2c3 b4a5 d2d4 e5d4 e1g1 d7d6");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4 c5b4 c2c3 b4c5 d2d4 e5d4 c3d4 c5b4 b1c3");
    AddLine("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4 c5b4 c2c3 b4e7 d2d4 c6a5 f3e5");

    // ========== SCOTCH GAME ==========

    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6 e4e5 d8e7 d1e2 f6d5 c2c4");
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5 d4b3 c5b6 b1c3 g8f6 d1e2 d7d6");
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 d8h4 d4b5 f8b4 c1d2 d8e4 f1e2 e4g2");
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6 b1c3 f8b4 f1d3 d7d5");
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 c2c3 d4c3 f1c4 c3b2 c1b2 f8b4 b1c3 g8f6");
    AddLine("e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f1c4 g8f6 e4e5 d7d5 c4b5 f6e4 f3d4 f8c5");

    // ========== PETROV'S DEFENSE (Russian Game) ==========

    AddLine("e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3 b8c6 e1g1 f8e7");
    AddLine("e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3 f8d6 e1g1 e8g8");
    AddLine("e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 b1c3 e4c3 d2c3 f8e7 f1e2 e8g8");
    AddLine("e2e4 e7e5 g1f3 g8f6 d2d4 f6e4 f1d3 d7d5 f3e5 b8d7 e5d7 c8d7 e1g1");
    AddLine("e2e4 e7e5 g1f3 g8f6 d2d4 e5d4 e4e5 f6e4 d1d4 d7d5 e5d6 e4d6");

    // ========== PHILIDOR DEFENSE ==========

    AddLine("e2e4 e7e5 g1f3 d7d6 d2d4 g8f6 b1c3 b8d7 f1c4 f8e7 e1g1 e8g8");
    AddLine("e2e4 e7e5 g1f3 d7d6 d2d4 e5d4 f3d4 g8f6 b1c3 f8e7 f1e2 e8g8");
    AddLine("e2e4 e7e5 g1f3 d7d6 d2d4 g8f6 d4e5 f6e4 f1d3 d6d5 e1g1 f8e7");
    AddLine("e2e4 e7e5 g1f3 d7d6 f1c4 f8e7 d2d4 e5d4 f3d4 g8f6 b1c3 e8g8");

    // ========== VIENNA GAME ==========

    AddLine("e2e4 e7e5 b1c3 g8f6 f2f4 d7d5 f4e5 f6e4 g1f3 f8b4 d1e2 b4c3");
    AddLine("e2e4 e7e5 b1c3 g8f6 f2f4 d7d5 e4d5 e5e4 d2d3 f8b4 f1d2 e4e3");
    AddLine("e2e4 e7e5 b1c3 b8c6 f2f4 e5f4 g1f3 g7g5 h2h4 g5g4 f3g5");
    AddLine("e2e4 e7e5 b1c3 b8c6 g2g3 g8f6 f1g2 f8c5 b1a3 d7d6");
    AddLine("e2e4 e7e5 b1c3 g8f6 g2g3 f8c5 f1g2 b8c6 b1a3");

    // ========== KING'S GAMBIT ==========

    AddLine("e2e4 e7e5 f2f4 e5f4 g1f3 g7g5 h2h4 g5g4 f3g5 h7h6 g5f7 e8f7 d2d4 d7d5");
    AddLine("e2e4 e7e5 f2f4 e5f4 g1f3 g7g5 f1c4 g5g4 e1g1 g4f3 d1f3 d8f6");
    AddLine("e2e4 e7e5 f2f4 e5f4 g1f3 d7d6 d2d4 g7g5 h2h4 g5g4 f3g5");
    AddLine("e2e4 e7e5 f2f4 e5f4 f1c4 g8f6 b1c3 c7c6 d2d4");
    AddLine("e2e4 e7e5 f2f4 d7d5 e4d5 e5e4 b1c3 g8f6 d1e2");

    // ========================================
    // SICILIAN DEFENSE - MASSIVELY EXPANDED
    // ========================================

    // Najdorf - Main Lines (6.Bg5)
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 f2f4 d8b6 d1d2 b6b2");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 f2f4 f8e7 d1f3 d8c7");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 f2f4 b8d7 d1f3 d8c7");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 d1f3 b8c6");

    // Najdorf - English Attack
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f1e3 e7e5 d4b3 f8e7 f2f3 e8g8");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f1e3 e7e5 d4b3 f8e7 d1d2 e8g8");

    // Dragon - Yugoslav Attack
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3 e8g8 d1d2 b8c6 e1c1");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3 b8c6 d1d2 e8g8 f1c4");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3 e8g8 d1d2 b8c6 e1c1 d6d5");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 f2f3 f8g7 c1e3 b8c6 d1d2 e8g8");

    // Dragon - Classical
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 f1e2 f8g7 e1g1 e8g8 f1e1 b8c6");
    AddLine("e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 f1e2 f8g7 e1g1 b8c6 c1e3");

    // Sveshnikov - Main Lines
    AddLine("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6 c1g5 a7a6 b5a3 b7b5");
    AddLine("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6 b1d5 f6d5 e4d5 c6b8");
    AddLine("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4f5 d7d6 c1g5 a7a6");

    // Accelerated Dragon - Maroczy Bind
    AddLine("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6 c2c4 g8f6 b1c3 d7d6 f1e2 f8g7");
    AddLine("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6 c2c4 f8g7 c1e3 g8f6 b1c3 e8g8");
    AddLine("e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6 b1c3 f8g7 c1e3 g8f6 f1c4 e8g8");

    // Taimanov
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7 c1e3 a7a6 f1d3");
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 a7a6 f1e2 g8f6 e1g1 f8e7");
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7 f1e2 g8f6 e1g1 a7a6");

    // Scheveningen
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 g8f6 b1c3 d7d6 f2f4 a7a6 f1e2 d8c7");
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 g8f6 b1c3 d7d6 c1e3 a7a6 f2f4 b8c6");
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 g8f6 b1c3 d7d6 g2g4 e6e5 d4f5 g7g6");

    // Kan Variation
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 b1c3 d8c7 f1d3 g8f6 e1g1 b8c6");
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 c2c4 g8f6 b1c3 f8b4");
    AddLine("e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 f1d3 b8c6 d4c6 b7c6");

    // Alapin Variation
    AddLine("e2e4 c7c5 c2c3 g8f6 e4e5 f6d5 d2d4 c5d4 g1f3 e7e6 c3d4 b7b6");
    AddLine("e2e4 c7c5 c2c3 d7d5 e4d5 d8d5 d2d4 g8f6 g1f3 c8g4 f1e2 e7e6");
    AddLine("e2e4 c7c5 c2c3 g8f6 e4e5 f6d5 g1f3 b8c6 b1a3 d5c7 d2d4 c5d4");

    // Moscow Variation
    AddLine("e2e4 c7c5 g1f3 d7d6 f1b5 c8d7 b5d7 d8d7 e1g1 b8c6 c2c3 g8f6");
    AddLine("e2e4 c7c5 g1f3 d7d6 f1b5 b8d7 e1g1 a7a6 b5d7 c8d7");

    // Closed Sicilian
    AddLine("e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 d7d6 c1e3 e7e6");
    AddLine("e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 d7d6 f2f4 e7e5");

    // ========================================
    // FRENCH DEFENSE - EXPANDED
    // ========================================

    // Winawer - Poisoned Pawn
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3 b2c3 g8e7 d1g4 e8f8 g4g7 h8g8 g7h7 c5d4");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3 b2c3 g8e7 g1f3 b8c6 a3a4");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 c5d4 a3b4 d4c3 g1f3");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 d1g4 g8e7 d4c5 b4c5 g4g7 h8g8");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 c1d2 d5e4 d1g4 g8f6 g4g7 h8g8 g7h7");

    // Classical - Burn Variation
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 d5e4 c3e4 f8e7 e4f6 e7f6 g5f6 d8f6 g1f3 e8g8");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7 g5e7 d8e7 f2f4 e8g8");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 e4e5 f6d7 f2f4 c7c5 g1f3 b8c6 f1e2");

    // Tarrasch
    AddLine("e2e4 e7e6 d2d4 d7d5 b1d2 c7c5 e4d5 e6d5 g1f3 b8c6 f1b5 f8d6 d4c5 d6c5");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1d2 g8f6 e4e5 f6d7 f1d3 c7c5 c2c3 b8c6 g1e2");
    AddLine("e2e4 e7e6 d2d4 d7d5 b1d2 c7c5 g1f3 c5d4 e4d5 d8d5 f1c4 d5d6");

    // Advance - Milner-Barry Gambit
    AddLine("e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6 f1d3 c5d4 c3d4 f8d7");
    AddLine("e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6 a2a3 c5c4");
    AddLine("e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 d8b6 g1f3 b8c6 f1d3 c8d7");

    // Exchange
    AddLine("e2e4 e7e6 d2d4 d7d5 e4d5 e6d5 b1c3 g8f6 c1g5 f8e7 f1d3 b8c6 g1e2");
    AddLine("e2e4 e7e6 d2d4 d7d5 e4d5 e6d5 c2c4 g8f6 b1c3 f8e7 c4d5 f6d5");

    // ========================================
    // CARO-KANN - EXPANDED
    // ========================================

    // Classical - Main Lines
    AddLine("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3 b8d7 h4h5 g6h7");
    AddLine("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 g1f3 b8d7 f1d3 g6d3 d1d3");
    AddLine("e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 g8f6 e4f6 e7f6 c2c3 f8d6 f1d3");

    // Advance Variation - Short
    AddLine("e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3 e7e6 f1e2 b8d7 e1g1 f8e7 b1d2");
    AddLine("e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1e2 e7e6 g1f3 b8d7 e1g1 f8e7 c2c3");
    AddLine("e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1e2 e7e6 e2g3 f5g6 h2h4 h7h6");

    // Panov-Botvinnik
    AddLine("e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c2c4 g8f6 b1c3 e7e6 g1f3 f8e7 c4d5 f6d5 f1d3");
    AddLine("e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c2c4 g8f6 b1c3 b8c6 c1g5 e7e6 c4c5");
    AddLine("e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c2c4 g8f6 b1c3 g7g6 d1b3 f8g7");

    // Two Knights
    AddLine("e2e4 c7c6 b1c3 d7d5 g1f3 c8g4 h2h3 g4f3 d1f3 e7e6 d2d4 g8f6");
    AddLine("e2e4 c7c6 b1c3 d7d5 d2d4 d5e4 c3e4 b8d7 e4g5 g8f6");

    // ========================================
    // PIRC AND MODERN DEFENSES
    // ========================================

    AddLine("e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 f2f4 f8g7 g1f3 e8g8 f1d3 b8a6");
    AddLine("e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 c1e3 f8g7 d1d2 e8g8 e1c1");
    AddLine("e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 g1f3 f8g7 f1e2 e8g8 e1g1 c7c6");
    AddLine("e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 f2f4 f8g7 e2e5 f6d7 f1e2 e8g8");
    AddLine("e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 f1e3 g8f6 d1d2 e8g8 e1c1");
    AddLine("e2e4 g7g6 d2d4 f8g7 g1f3 d7d6 f1c4 g8f6 d1e2 e8g8");

    // ========================================
    // ALEKHINE'S AND SCANDINAVIAN
    // ========================================

    AddLine("e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 g1f3 g7g6 c2c4 d5b6 e5d6 c7d6 b1c3");
    AddLine("e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 c2c4 d5b6 f2f4 d6e5 f4e5 b8c6");
    AddLine("e2e4 g8f6 e4e5 f6d5 c2c4 d5b6 d2d4 d7d6 f2f4 d6e5 f4e5 c8f5");
    AddLine("e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c8f5 f1c4 e7e6");
    AddLine("e2e4 d7d5 e4d5 g8f6 d2d4 f6d5 g1f3 g7g6 f1e2 f8g7 e1g1 e8g8");
    AddLine("e2e4 d7d5 e4d5 d8d5 b1c3 d5d8 d2d4 g8f6 g1f3 c8f5 f1c4");

    // ========================================
    // QUEEN'S PAWN OPENINGS - EXPANDED
    // ========================================

    // ========== QUEEN'S GAMBIT DECLINED ==========

    // Orthodox Variation
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 b8d7 a1c1 c7c6 f1d3");
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 b8d7 d1c2 c7c6 f1d3 d5c4");
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 h7h6 g5h4 b7b6");

    // Tartakower Variation
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 h7h6 g5h4 b7b6 c4d5 f6d5");
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 h7h6 g5h4 b7b6 f1e2 c8b7");

    // Cambridge Springs
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 b8d7 e2e3 c7c6 g1f3 d8a5 c4d5 f6d5");

    // Semi-Slav
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3 b8d7 f1d3 d5c4 d3c4 b7b5");
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 g1f3 g8f6 e2e3 b8d7 f1d3 d5c4 d3c4 b7b5 c4d3");
    AddLine("d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 e2e4 d5e4 c3e4 f8b4 c1d2 d8d4");

    // Queen's Gambit Accepted
    AddLine("d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1 a7a6 d1e2");
    AddLine("d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 c8g4 f1c4 e7e6 h2h3 g4h5");
    AddLine("d2d4 d7d5 c2c4 d5c4 e2e4 e7e5 g1f3 e5d4 f1c4 f8b4 c1d2");

    // Slav Defense
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 e2e3 e7e6 f1c4 f8b4");
    AddLine("d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 f3e5 e7e6 f2f3");
    AddLine("d2d4 d7d5 c2c4 c7c6 b1c3 g8f6 e2e3 e7e6 g1f3 b8d7 f1d3 d5c4");

    // ========== NIMZO-INDIAN DEFENSE ==========

    // Classical Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 e8g8 a2a3 b4c3 c2c3 b7b6 c1g5");
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 c7c5 d4c5 e8g8 a2a3 b4c5");
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 d7d5 a2a3 b4c3 c2c3 b8c6");

    // Rubinstein Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5 g1f3 c7c5 e1g1");
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 b7b6 g1e2 c8a6 a2a3 b4c3");

    // Samisch Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 a2a3 b4c3 b2c3 c7c5 f2f3 d7d5 c4d5 f6d5");
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 a2a3 b4c3 b2c3 e8g8 f2f3 d7d5 c4d5 e6d5");

    // Leningrad Variation
    AddLine("d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 c1g5 h7h6 g5h4 c7c5 d4d5 b7b5");

    // ========== KING'S INDIAN DEFENSE ==========

    // Classical Main Line
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6 d4d5 c6e7");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6 d4d5 c6e7 f3e1");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8d7 c1e3");

    // Samisch Variation
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f3 e8g8 c1e3 e7e5 g1e2 c7c6");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f3 e8g8 c1e3 b8c6 g1e2 a7a6");

    // Four Pawns Attack
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f4 e8g8 g1f3 c7c5 d4d5 e7e6");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f4 e8g8 f1e2 c7c5 g1f3 e7e6");

    // Fianchetto System
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 g2g3 e8g8 f1g2 d7d6 g1f3 b8c6 e1g1 a7a6");
    AddLine("d2d4 g8f6 c2c4 g7g6 g2g3 f8g7 f1g2 e8g8 b1c3 d7d6 g1f3 b8d7");

    // ========== GRUNFELD DEFENSE ==========

    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 f8g7 f1c4 e8g8");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 g1f3 f8g7 d1b3 d5c4 b3c4 e8g8 e2e4");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 f8g7 c1e3");
    AddLine("d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 g1f3 f8g7 c1f4 e8g8 e2e3 c7c5");

    // ========== DUTCH DEFENSE ==========

    AddLine("d2d4 f7f5 g2g3 g8f6 f1g2 g7g6 g1f3 f8g7 e1g1 e8g8 c2c4 d7d6");
    AddLine("d2d4 f7f5 c2c4 g8f6 g2g3 e7e6 f1g2 f8e7 g1f3 e8g8 e1g1 d7d5");
    AddLine("d2d4 f7f5 g1f3 g8f6 c1g5 e7e6 e2e3 f8e7 f1d3 e8g8");

    // ========== BENONI DEFENSE ==========

    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 g1f3 g7g6 e2e4 f8g7");
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 g1f3 f8g7 f1e2");
    AddLine("d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 f2f4 f8g7");

    // ========== BOGO-INDIAN ==========

    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 c1d2 d8e7 g2g3 b8c6 f1g2 b4d2");
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 f8b4 b1d2 b7b6 a2a3 b4d2 c1d2");

    // ========== CATALAN OPENING ==========

    AddLine("d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 f8e7 g1f3 e8g8 e1g1 d5c4 d1c2");
    AddLine("d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 d5c4 g1f3 f8e7 e1g1 e8g8");
    AddLine("d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 g2g3 f8e7 f1g2 e8g8 e1g1 b8d7");

    // ========================================
    // ENGLISH OPENING - EXPANDED
    // ========================================

    AddLine("c2c4 c7c5 g1f3 g8f6 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 e1g1 e8g8");
    AddLine("c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 f8b4 f1g2 e8g8 e1g1 e5e4");
    AddLine("c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 e2e3 f8b4 d1c2 e8g8 a2a3 b4c3");
    AddLine("c2c4 g8f6 b1c3 e7e5 g1f3 b8c6 g2g3 f8b4 f1g2 e8g8 e1g1 f8e8");
    AddLine("c2c4 e7e5 g2g3 g8f6 f1g2 d7d5 c4d5 f6d5 b1c3 d5b6 g1f3");
    AddLine("c2c4 g8f6 g2g3 c7c6 f1g2 d7d5 g1f3 f8g4 e1g1 b8d7");
    AddLine("c2c4 c7c5 g1f3 b8c6 b1c3 g7g6 g2g3 f8g7 f1g2 g8f6");
    AddLine("c2c4 e7e5 b1c3 b8c6 g1f3 f7f5 d2d4 e5e4 f3g5 g8f6");

    // ========================================
    // RETI AND FLANK OPENINGS
    // ========================================

    AddLine("g1f3 d7d5 c2c4 e7e6 g2g3 g8f6 f1g2 f8e7 e1g1 e8g8 b2b3");
    AddLine("g1f3 d7d5 c2c4 c7c6 b2b3 g8f6 c1b2 c8f5 g2g3 b8d7");
    AddLine("g1f3 g8f6 c2c4 g7g6 b2b4 f8g7 c1b2 e8g8 g2g3");
    AddLine("g1f3 g8f6 g2g3 g7g6 f1g2 f8g7 e1g1 e8g8 d2d4 d7d6");
    AddLine("g1f3 g8f6 c2c4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7");
    AddLine("g1f3 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 f8e7 e1g1 e8g8");

    // Bird's Opening
    AddLine("f2f4 d7d5 g1f3 g8f6 e2e3 g7g6 f1e2 f8g7 e1g1 e8g8");
    AddLine("f2f4 g8f6 g1f3 g7g6 e2e3 f8g7 f1e2 e8g8 e1g1 d7d6");
}

int main() {
    Chess::Init();

    AddAllOpenings();

    // Sort by hash
    std::sort(g_BookEntries.begin(), g_BookEntries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            if (a.Hash != b.Hash) return a.Hash < b.Hash;
            if (a.StartSquare != b.StartSquare) return a.StartSquare < b.StartSquare;
            else return a.TargetSquare < b.TargetSquare;
        }
    );

    // Remove duplicates
    auto last = std::unique(g_BookEntries.begin(), g_BookEntries.end(),
        [](const BookEntry& a, const BookEntry& b) {
            return (
                a.Hash == b.Hash &&
                a.StartSquare == b.StartSquare &&
                a.TargetSquare == b.TargetSquare
            );
        }
    );

    g_BookEntries.erase(last, g_BookEntries.end());

    // Calculate optimal column count based on sqrt of number of entries
    const std::size_t numEntries = g_BookEntries.size();
    const std::size_t colSize = static_cast<std::size_t>(std::sqrt(numEntries));

    std::cout << "{" << std::endl;

    for (std::size_t i = 0; i < numEntries; ++i) {
        const auto& e = g_BookEntries[i];

        // Print indentation at start of row
        if (i % colSize == 0) {
            std::cout << "    ";
        }

        // Print entry
        std::cout << "{0x" << std::hex << e.Hash << std::dec
            << ", " << +e.StartSquare
            << ", " << +e.TargetSquare
            << ", " << e.Flag << "}";

        // Add comma if not the last entry
        if (i < numEntries - 1) {
            std::cout << ", ";
        }

        // Newline at end of row or at the end
        if ((i + 1) % colSize == 0 || i == numEntries - 1) {
            std::cout << std::endl;
        }
    }

    std::cout << "};" << std::endl << std::endl;

    // Print statistics
    std::cerr << "Opening book generated successfully!" << std::endl;
    std::cerr << "Total entries: " << numEntries << std::endl;
    std::cerr << "Entries per row: " << colSize << std::endl;

    return 0;
}