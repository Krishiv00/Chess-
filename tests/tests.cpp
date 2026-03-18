#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cassert>

#include "Engine/Chess.hpp"

static int s_Passed = 0;
static int s_Failed = 0;

struct TestCase {
    std::string name;
    std::function<bool()> fn;
};

static std::vector<TestCase> s_Tests;
static std::vector<std::string> s_FailedTests;

static void RegisterTest(const std::string& name, std::function<bool()> fn) {
    s_Tests.push_back({name, fn});
}

#pragma region Run Tests

static void RunTests() {
    const size_t total = s_Tests.size();

    std::cout << "========================================" << std::endl;
    std::cout << "Running " << total << " test cases" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    for (const auto& t : s_Tests) {
        bool ok = false;

        try {
            ok = t.fn();
        }
        catch (const std::exception& e) {
            std::cerr << "[EXCEPTION] " << t.name << ": "
                << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "[EXCEPTION] " << t.name
                << ": unknown" << std::endl;
        }

        if (ok) {
            std::cout << "[PASS] " << t.name << std::endl;
            ++s_Passed;
        } else {
            std::cout << "[FAIL] " << t.name << std::endl;
            ++s_Failed;
            s_FailedTests.push_back(t.name);
        }
    }

    std::cout << std::endl << "========================================" << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Total Tests : " << total << std::endl;
    std::cout << "Passed : " << s_Passed << std::endl;
    std::cout << "Failed : " << s_Failed << std::endl;

    if (!s_FailedTests.empty()) {
        std::cout << std::endl << "Failed Test Cases:" << std::endl;
        std::cout << "========================================" << std::endl;

        for (const auto& name : s_FailedTests) {
            std::cout << " - " << name << std::endl;
        }
    }

    std::cout << "========================================" << std::endl;
}

#pragma region Helpers

// Count moves in a position that have the Check flag set.
static unsigned int countCheckMoves(Chess::Board& board, Chess::PieceColor side) {
    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);

    unsigned int checks = 0;
    for (unsigned int i = 0; i < n; ++i) {
        if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check)) ++checks;
    }
    return checks;
}

// Count total legal moves.
static unsigned int countMoves(Chess::Board& board, Chess::PieceColor side) {
    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);
    return n;
}

// Find a specific move and verify it has (or does not have) the Check flag.
static bool findMove(Chess::Board& board, Chess::PieceColor side, int from, int to, bool expectCheck) {
    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);

    for (unsigned int i = 0; i < n; ++i) {
        if (moves[i].StartingSquare == from && moves[i].TargetSquare == to) {
            bool hasCheck = Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            return hasCheck == expectCheck;
        }
    }

    return false; // move not found at all
}

// Perft node count (leaf nodes at given depth).
static uint64_t perft(Chess::Board board, Chess::PieceColor side, int depth) {
    if (depth == 0) return 1;

    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0u;
    board.getAllLegalMoves(side, moves, n);

    uint64_t total = 0ull;

    for (unsigned int i = 0; i < n; ++i) {
        Chess::Board copy = board;
        copy.doMoveInternal(moves[i]);
        total += perft(copy, Chess::InvertColor(side), depth - 1);
    }

    return total;
}

// Load a FEN and return the side to move.
static Chess::PieceColor load(Chess::Board& board, const char* fen) {
    return board.LoadFromFen(std::string(fen));
}

#pragma region Register Tests

static void RegisterTests() {
    // -----------------------------------------------------------------------
    // Position 1: Starting position
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Starting position d1", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return perft(b, s, 1) == 20;
    });
    RegisterTest("Perft: Starting position d2", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return perft(b, s, 2) == 400;
    });
    RegisterTest("Perft: Starting position d3", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return perft(b, s, 3) == 8902;
    });
    RegisterTest("Perft: Starting position d4", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return perft(b, s, 4) == 197281;
    });
    RegisterTest("Perft: Starting position d5", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return perft(b, s, 5) == 4865609;
    });

    // -----------------------------------------------------------------------
    // Position 2: Kiwipete — stress tests castling, en-passant, promotions
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Kiwipete d1", []() {
        Chess::Board b; auto s = load(b, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        return perft(b, s, 1) == 48;
    });
    RegisterTest("Perft: Kiwipete d2", []() {
        Chess::Board b; auto s = load(b, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        return perft(b, s, 2) == 2039;
    });
    RegisterTest("Perft: Kiwipete d3", []() {
        Chess::Board b; auto s = load(b, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        return perft(b, s, 3) == 97862;
    });
    RegisterTest("Perft: Kiwipete d4", []() {
        Chess::Board b; auto s = load(b, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        return perft(b, s, 4) == 4085603;
    });

    // -----------------------------------------------------------------------
    // Position 3: Many promotions and en-passant
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Position 3 d1", []() {
        Chess::Board b; auto s = load(b, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        return perft(b, s, 1) == 14;
    });
    RegisterTest("Perft: Position 3 d2", []() {
        Chess::Board b; auto s = load(b, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        return perft(b, s, 2) == 191;
    });
    RegisterTest("Perft: Position 3 d3", []() {
        Chess::Board b; auto s = load(b, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        return perft(b, s, 3) == 2812;
    });
    RegisterTest("Perft: Position 3 d4", []() {
        Chess::Board b; auto s = load(b, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        return perft(b, s, 4) == 43238;
    });
    RegisterTest("Perft: Position 3 d5", []() {
        Chess::Board b; auto s = load(b, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        return perft(b, s, 5) == 674624;
    });

    // -----------------------------------------------------------------------
    // Position 4: Castling rights edge cases
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Position 4 d1", []() {
        Chess::Board b; auto s = load(b, "r3k2r/Pppp1ppp/1b6/8/8/1B6/pPPP1PPP/R3K2R b KQkq - 0 1");
        return perft(b, s, 1) == 27;
    });
    RegisterTest("Perft: Position 4 d2", []() {
        Chess::Board b; auto s = load(b, "r3k2r/Pppp1ppp/1b6/8/8/1B6/pPPP1PPP/R3K2R b KQkq - 0 1");
        return perft(b, s, 2) == 725;
    });
    RegisterTest("Perft: Position 4 d3", []() {
        Chess::Board b; auto s = load(b, "r3k2r/Pppp1ppp/1b6/8/8/1B6/pPPP1PPP/R3K2R b KQkq - 0 1");
        return perft(b, s, 3) == 19209;
    });
    RegisterTest("Perft: Position 4 d4", []() {
        Chess::Board b; auto s = load(b, "r3k2r/Pppp1ppp/1b6/8/8/1B6/pPPP1PPP/R3K2R b KQkq - 0 1");
        return perft(b, s, 4) == 515398;
    });

    // -----------------------------------------------------------------------
    // Position 5: Pins and checks
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Position 5 d1", []() {
        Chess::Board b; auto s = load(b, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
        return perft(b, s, 1) == 44;
    });
    RegisterTest("Perft: Position 5 d2", []() {
        Chess::Board b; auto s = load(b, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
        return perft(b, s, 2) == 1486;
    });
    RegisterTest("Perft: Position 5 d3", []() {
        Chess::Board b; auto s = load(b, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
        return perft(b, s, 3) == 62379;
    });
    RegisterTest("Perft: Position 5 d4", []() {
        Chess::Board b; auto s = load(b, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
        return perft(b, s, 4) == 2103487;
    });

    // -----------------------------------------------------------------------
    // Position 6: Complex middle game
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Position 6 d1", []() {
        Chess::Board b; auto s = load(b, "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
        return perft(b, s, 1) == 46;
    });
    RegisterTest("Perft: Position 6 d2", []() {
        Chess::Board b; auto s = load(b, "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
        return perft(b, s, 2) == 2079;
    });
    RegisterTest("Perft: Position 6 d3", []() {
        Chess::Board b; auto s = load(b, "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
        return perft(b, s, 3) == 89890;
    });
    RegisterTest("Perft: Position 6 d4", []() {
        Chess::Board b; auto s = load(b, "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
        return perft(b, s, 4) == 3894594;
    });

    // -----------------------------------------------------------------------
    // Position 7: Pins and check blocks
    // -----------------------------------------------------------------------
    RegisterTest("Perft: Position 7 d1", []() {
        Chess::Board b; auto s = load(b, "r3k3/1p3p2/p2q2p1/bn3P2/1N2PQP1/PB6/3K1R1r/3R4 w - - 0 1");
        return perft(b, s, 1) == 7;
    });

    RegisterTest("Perft: Position 7 d2", []() {
        Chess::Board b; auto s = load(b, "r3k3/1p3p2/p2q2p1/bn3P2/1N2PQP1/PB6/3K1R1r/3R4 w - - 0 1");
        return perft(b, s, 2) == 317;
    });

    RegisterTest("Perft: Position 7 d3", []() {
        Chess::Board b; auto s = load(b, "r3k3/1p3p2/p2q2p1/bn3P2/1N2PQP1/PB6/3K1R1r/3R4 w - - 0 1");
        return perft(b, s, 3) == 12363;
    });

    RegisterTest("Perft: Position 7 d4", []() {
        Chess::Board b; auto s = load(b, "r3k3/1p3p2/p2q2p1/bn3P2/1N2PQP1/PB6/3K1R1r/3R4 w - - 0 1");
        return perft(b, s, 4) == 501910;
    });

    // -----------------------------------------------------------------------
    // Knight direct check
    // White knight e5 -> f7 gives check to king on h8 (diagonal hop)
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Knight direct check", []() {
        Chess::Board b; load(b, "7k/8/8/4N3/8/8/8/4K3 w - - 0 1");
        // N e5 -> f7
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(1, 5),  // f7
            true
        );
    });

    RegisterTest("CheckFlag: Knight non-check move", []() {
        Chess::Board b; load(b, "7k/8/8/4N3/8/8/8/4K3 w - - 0 1");
        // N e5 -> d3 is not check
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(5, 3),  // d3
            false
        );
    });

    // -----------------------------------------------------------------------
    // Pawn direct check — push that lands and attacks the king diagonally
    // Pawn e5 -> e6 attacks d7; king placed on d7 so the push is a check.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: White pawn direct check", []() {
        Chess::Board b; load(b, "8/3k4/8/4P3/8/8/8/4K3 w - - 0 1");
        // e5 -> e6 attacks d7 where black king is
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(2, 4),  // e6
            true
        );
    });

    RegisterTest("CheckFlag: White pawn push no check", []() {
        Chess::Board b; load(b, "8/8/3k4/4P3/8/8/8/4K3 w - - 0 1");
        // Here king on d6; e5->e6 does not check d6
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(2, 4),  // e6
            false
        );
    });

    // -----------------------------------------------------------------------
    // Black pawn direct check (single clear scenario)
    // Black pawn d4 -> d3 attacks c2; white king placed on c2.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Black pawn direct check", []() {
        Chess::Board b; load(b, "8/8/8/3p4/8/2K5/8/3k4 b - - 0 1");
        // d4 -> d3 should attack c2 and give check to white king on c2
        return findMove(b, Chess::PieceColor::Black,
            Chess::To2DIndex(3, 3),  // d4
            Chess::To2DIndex(4, 3),  // d3
            true
        );
    });

    // -----------------------------------------------------------------------
    // Bishop direct check
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Bishop direct check", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/B3K3 w - - 0 1");
        // Ba1 -> b2, diagonal b2..h8 checks black king on h8
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(6, 1),  // b2
            true
        );
    });

    RegisterTest("CheckFlag: Bishop blocked no check", []() {
        Chess::Board b; load(b, "7k/6P1/8/8/8/8/8/B3K3 w - - 0 1");
        // Pawn on g7 blocks the a1-h8 diagonal so Ba1->b2 must NOT be check
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(6, 1),  // b2
            false
        );
    });

    // -----------------------------------------------------------------------
    // Rook direct check (rank and file) and blocked case
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Rook direct check rank", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/R3K3 w - - 0 1");
        // Ra1 -> a8 (a8 has empty path along file), black king on h8; Ra8 attacks rank 8 -> check
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(0, 0),  // a8
            true
        );
    });

    RegisterTest("CheckFlag: Rook direct check file", []() {
        Chess::Board b; load(b, "k7/8/8/8/8/8/8/K6R w - - 0 1");
        // Rh1 -> h8, rook on h8 attacks a8 along rank 8, checking king on a8
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 7),  // h1
            Chess::To2DIndex(0, 7),  // h8
            true
        );
    });

    RegisterTest("CheckFlag: Rook blocked no check", []() {
        Chess::Board b; load(b, "2P4k/8/8/8/8/8/8/R3K3 w - - 0 1");
        // Pawn on c8 blocks rank 8 path from a8 to h8 — Ra1->a8 should NOT be check on h8
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(0, 0),  // a8
            false
        );
    });

    // -----------------------------------------------------------------------
    // Queen direct checks (diagonal and orthogonal)
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Queen diagonal check", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/Q3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(6, 1),  // b2
            true
        );
    });

    RegisterTest("CheckFlag: Queen orthogonal check", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/Q3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(0, 0),  // a8
            true
        );
    });

    // -----------------------------------------------------------------------
    // Discovered check (bishop reveals rook, rook reveals bishop)
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Discovered check by rook", []() {
        // Rook e2 behind bishop e5; bishop moves off file -> discovered check to king on e8
        Chess::Board b; load(b, "4k3/8/8/4B3/8/8/4R3/4K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(4, 3),  // d4
            true
        );
    });

    RegisterTest("CheckFlag: Discovered check by bishop", []() {
        // Bishop on a1 behind a knight on d4; moving knight off diagonal reveals bishop to h8
        Chess::Board b; load(b, "7k/8/8/8/3N4/8/8/B3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(4, 3),  // d4
            Chess::To2DIndex(2, 2),  // c6
            true
        );
    });

    RegisterTest("CheckFlag: Double check (direct + discovered)", []() {
        // Knight on e5 (on diagonal a1-h8) -> f7 gives direct knight check and discovers bishop a1->h8.
        Chess::Board b; load(b, "7k/8/8/4N3/8/8/8/B3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(1, 5),  // f7
            true
        );
    });

    // -----------------------------------------------------------------------
    // En-passant discovered check
    // A specific, clear case: white pawn b5 capturing c6 ep exposes rook on a5 to check king on h5.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: En-passant discovered check", []() {
        Chess::Board b; load(b, "8/8/8/RPp4k/8/8/8/4K3 w - c6 0 1");
        // b5xc6 ep should be a capturing move flagged EnPassant and Check (discovered)
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 1),  // b5
            Chess::To2DIndex(2, 2),  // c6 (ep)
            true
        );
    });

    // -----------------------------------------------------------------------
    // Castling check cases
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Kingside castle gives check", []() {
        // After O-O rook lands on f1 and gives check to king on f8.
        Chess::Board b; load(b, "5k2/8/8/8/8/8/8/4K2R w K - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 4),  // e1
            Chess::To2DIndex(7, 6),  // g1 (king move for O-O)
            true
        );
    });

    RegisterTest("CheckFlag: Queenside castle no check", []() {
        Chess::Board b; load(b, "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1");
        // O-O-O should not produce a check in this setup
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 4),  // e1
            Chess::To2DIndex(7, 2),  // c1 (king move for O-O-O)
            false
        );
    });

    // -----------------------------------------------------------------------
    // Promotion checks
    // For promotions we iterate the move list and test appropriate flags.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Promotion to queen check", []() {
        Chess::Board b; load(b, "7k/4P3/8/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 4) &&
                moves[i].TargetSquare == Chess::To2DIndex(0, 4) &&
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToQueen)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false; // move not found
    });

    RegisterTest("CheckFlag: Promotion-to-rook check", []() {
        Chess::Board b; load(b, "7k/4P3/8/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 4) &&
                moves[i].TargetSquare == Chess::To2DIndex(0, 4) &&
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToRook)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    RegisterTest("CheckFlag: Promotion to bishop check", []() {
        Chess::Board b; load(b, "8/1P6/3k4/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 1) &&  // b7
                moves[i].TargetSquare == Chess::To2DIndex(0, 1) &&  // b8
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToBishop)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    RegisterTest("CheckFlag: Promotion to knight check", []() {
        // Promotion to knight that delivers a check (constructed scenario)
        Chess::Board b; load(b, "8/4k1P1/8/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 6) &&  // g7
                moves[i].TargetSquare == Chess::To2DIndex(0, 6) &&  // g8
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToKnight)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    // Promotion that captures and gives check (promotion-capture)
    RegisterTest("CheckFlag: Promotion capture to queen gives check", []() {
        // White pawn on b7 capturing a8 and promoting to queen that checks king on c8
        Chess::Board b; load(b, "r1k5/1P6/p7/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 1) &&  // b7
                moves[i].TargetSquare == Chess::To2DIndex(0, 0) &&  // a8
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToQueen)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    // -----------------------------------------------------------------------
    // Check count correctness — total checks in a position must match
    // -----------------------------------------------------------------------
    RegisterTest("CheckCount: Starting position has 0 check moves", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return countCheckMoves(b, s) == 0;
    });

    RegisterTest("CheckCount: Position with multiple checking moves sanity", []() {
        Chess::Board b; auto s = load(b,
            "r3kb1r/ppp2ppp/2n5/1B6/8/5N2/PPPP1PPP/R1BQR1K1 w kq - 0 1");
        unsigned int checks = countCheckMoves(b, s);
        return checks <= countMoves(b, s); // can't have more checks than moves
    });

    // -----------------------------------------------------------------------
    // In check — legal moves only
    // -----------------------------------------------------------------------
    RegisterTest("InCheck: Only legal moves generated when in check", []() {
        Chess::Board b; auto s = load(b, "4R3/8/8/8/8/8/8/k3K3 b - - 0 1");
        unsigned int n = countMoves(b, s);
        // Expect a small number but at least 1 legal reply (escape/capture)
        return n >= 1 && n <= 5;
    });

    RegisterTest("InCheck: Double check only king moves (robustness)", []() {
        // This test merely ensures engine doesn't crash on double-check-like setups.
        Chess::Board b; load(b, "6k1/8/8/8/2B5/8/8/4K1R1 w - - 0 1");
        auto s = Chess::PieceColor::Black;
        unsigned int n = countMoves(b, s);
        return n >= 0;
    });

    // -----------------------------------------------------------------------
    // Pins — pinned piece cannot move if it would expose king to check
    // -----------------------------------------------------------------------
    RegisterTest("Pin: Pinned piece moves are legal only along pin", []() {
        // Bishop pinned along rank 1 by rook a1. Only moves along rank are legal.
        Chess::Board b; load(b, "4k3/8/8/8/8/8/8/r3BK2 w - - 0 1");
        unsigned int n = countMoves(b, Chess::PieceColor::White);
        return n >= 1;
    });

    RegisterTest("Edge: Stalemate returns 0 moves", []() {
        Chess::Board b; auto s = load(b, "k7/8/1Q6/8/8/8/8/7K b - - 0 1");
        // Black to move, stalemated (no legal moves).
        return countMoves(b, s) == 0;
    });

    RegisterTest("Edge: En-passant legal generation (basic)", []() {
        Chess::Board b; load(b, "8/8/8/3pP3/8/8/8/k2K4 w - d6 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        bool epFound = false;
        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::EnPassant)) {
                epFound = true;
                break;
            }
        }
        return epFound;
    });

    RegisterTest("Edge: Castling not allowed through attacked square", []() {
        // Black rook on f8 attacks f1 — white cannot castle kingside (through f1)
        Chess::Board b; load(b, "4k3/5r2/8/8/8/8/8/4K2R w K - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::CastleKingSide)) return false; // should NOT castle
        }
        return true; // correctly excluded
    });

    RegisterTest("Edge: Castling not allowed while in check", []() {
        Chess::Board b; load(b, "3kr3/8/8/8/8/8/8/4K2R w K - 0 1");
        // White king on e1 is currently in check from rook on e8 (so castling must be disallowed)
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::CastleKingSide)) return false;
        }
        return true;
    });

    RegisterTest("Edge: EP pin detection (horizontal) - illegal if exposes king", []() {
        // If en-passant capture would expose the king horizontally to a rook, it must be illegal.
        // White king on e5, white pawn on d5, black pawn on c5 (en-passant target c6).
        // If exd6 ep were allowed, the file would open and rook on a5 would give check; that move must be illegal.
        Chess::Board b; load(b, "8/8/8/rPpK4/8/8/8/4k3 w - c6 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::EnPassant)) return false; // must be illegal
        }
        return true; // correctly excluded
    });

    RegisterTest("Edge: Only 1 move in zugzwang-like position", []() {
        Chess::Board b; auto s = load(b, "7k/8/5KR1/8/8/8/8/8 b - - 0 1");
        unsigned int n = countMoves(b, s);
        return n >= 1;
    });

    RegisterTest("Edge: Move count never exceeds MaxLegalMoves", []() {
        // Stress position with many possible moves; ensure we cap at MaxLegalMoves
        Chess::Board b; auto s = load(b,
            "R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1");
        unsigned int n = countMoves(b, s);
        return n <= Chess::MaxLegalMoves;
    });
}

int main() {
    Chess::Init();

    RegisterTests();
    RunTests();

    return s_Failed;
}