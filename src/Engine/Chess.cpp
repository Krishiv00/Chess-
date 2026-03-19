#include <iostream>
#include <cstring>
#include <random>
#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_set>

#include "Engine/Chess.hpp"

using namespace Chess;

#pragma region Utils

namespace Utils {
    [[nodiscard]]
    static std::vector<std::string> Tokenize(const std::string& string, char sep = ' ') {
        std::vector<std::string> splitted;

        std::string buffer;
        buffer.reserve(string.size());

        for (const char c : string) {
            if (c == sep) {
                if (!buffer.empty()) {
                    splitted.push_back(buffer);
                    buffer.clear();
                }
            } else {
                buffer.push_back(c);
            }
        }

        if (!buffer.empty()) splitted.push_back(buffer);

        return splitted;
    }

    [[nodiscard]]
    static constexpr inline int PieceToBitIdx(PieceType type, PieceColor color) noexcept {
        const int pt = static_cast<int>(type);
        return color == PieceColor::Black ? (pt + 6) : pt;
    }

    [[nodiscard]]
    static constexpr inline PieceType PromotionPiece(MoveFlag flag) noexcept {
        if (HasFlag(flag, MoveFlag::PromoteToQueen)) return PieceType::Queen;
        if (HasFlag(flag, MoveFlag::PromoteToRook)) return PieceType::Rook;
        if (HasFlag(flag, MoveFlag::PromoteToKnight)) return PieceType::Knight;
        return PieceType::Bishop;
    }

    [[nodiscard]]
    static constexpr inline int Mirror(int idx) noexcept {
        return idx ^ 56;
    }

    [[nodiscard]]
    static constexpr inline uint64_t FileMask(int file) noexcept {
        return 0x0101010101010101ull << file;
    }

    [[nodiscard]]
    static constexpr inline int IntLerp(int start, int end, float t) noexcept {
        return start + (end - start) * t;
    }
}

#pragma region Options

namespace Options {
    static int Contempt{50};
};

#pragma region Hashing

class ZobristHasher {
private:
    static inline uint64_t PieceHash[12][Ranks * Files];
    static inline uint64_t SideHash;
    static inline uint64_t CastlingHash[16];
    static inline uint64_t EnPassantHash[Files];

public:
    static void Init() {
        std::unordered_set<uint64_t> usedHashes;
        usedHashes.reserve(12 * Ranks * Files + 1 + 16 + Files);

        std::seed_seq seed{0x517CC1B727220A95ull, 0x3C6EF372FE94F82Aull};
        std::mt19937_64 gen(seed);

        std::random_device rd;

        const auto GetUniqueRandomHash = [&]() -> uint64_t {
            uint64_t hash;

            do {
                hash = gen();
            } while (usedHashes.contains(hash));

            usedHashes.insert(hash);

            return hash;
            };

        for (int pieceType = 0; pieceType < 12; ++pieceType) {
            for (int square = 0; square < Ranks * Files; ++square) {
                PieceHash[pieceType][square] = GetUniqueRandomHash();
            }
        }

        SideHash = GetUniqueRandomHash();

        for (auto& c : CastlingHash) {
            c = GetUniqueRandomHash();
        }

        for (auto& c : EnPassantHash) {
            c = GetUniqueRandomHash();
        }
    }

    static inline uint64_t HashSideToMove(uint64_t hash) noexcept {
        return hash ^ SideHash;
    }

    static inline uint64_t HashCastlingRights(uint64_t hash, uint8_t castlingRights) {
        return hash ^ CastlingHash[castlingRights];
    }

    static inline uint64_t HashEnPassantSquare(uint64_t hash, std::size_t enPassantSquare) noexcept {
        const int enPassantFile = enPassantSquare % Files;
        return hash ^ EnPassantHash[enPassantFile];
    }

    static inline uint64_t HashPiece(uint64_t hash, Piece piece, int sq) {
        return hash ^ PieceHash[Utils::PieceToBitIdx(piece.Type, piece.Color)][sq];
    }

    static uint64_t GetHash(const Board& board, PieceColor sideToMove) {
        uint64_t hash = 0ull;

        // hash for all piece types
        for (int pieceIdx = 0; pieceIdx < 12; ++pieceIdx) {
            for (uint64_t mask = board.GetPieceMask(pieceIdx); mask; mask &= mask - 1) {
                hash ^= PieceHash[pieceIdx][MaskToIndex(mask)];
            }
        }

        // side to move hash
        if (sideToMove == PieceColor::Black) hash ^= SideHash;

        // castling hash
        hash ^= CastlingHash[board.GetCastlingRights()];

        // en-passant hash
        if (board.GetEnPassantSquare() != NullPos) {
            hash = HashEnPassantSquare(hash, board.GetEnPassantSquare());
        }

        return hash;
    }

    static uint64_t GetPawnAndKingHash(const Board& board) {
        uint64_t hash = 0ull;

        // white pawns
        for (uint64_t mask = board.GetPieceMask(0); mask; mask &= mask - 1) {
            hash ^= PieceHash[0][MaskToIndex(mask)];
        }

        // white king
        hash ^= PieceHash[5][MaskToIndex(board.GetPieceMask(5))];

        // black pawns
        for (uint64_t mask = board.GetPieceMask(6); mask; mask &= mask - 1) {
            hash ^= PieceHash[6][MaskToIndex(mask)];
        }

        // black king
        hash ^= PieceHash[11][MaskToIndex(board.GetPieceMask(11))];

        return hash;
    }
};

#pragma region Transposition Table

class TranspositionTable {
public:
    enum class TTFlag : uint8_t {
        Exact,
        LowerBound,
        UpperBound
    };

    struct TTMove {
        uint8_t StartingSquare{NullPos};
        uint8_t TargetSquare{NullPos};
    };

    struct TTEntry {
        uint64_t Key{0};
        int32_t Score{0};
        int8_t Depth{-1};
        TTFlag NodeFlag;
        TTMove BestMove;
    };

private:
    std::vector<TTEntry> m_Entries;

public:
    void Clear() {
        std::memset(m_Entries.data(), 0, m_Entries.size() * sizeof(TTEntry));
    }

    const TTEntry* Peek(uint64_t key) const noexcept {
        const TTEntry& e = m_Entries[key % m_Entries.size()];
        return (e.Key == key) ? &e : nullptr;
    }

    bool Probe(uint64_t key, int depth, int alpha, int beta, int& score, Move& bestMove) const {
        const TTEntry& entry = m_Entries[key % m_Entries.size()];

        if (entry.Key != key) return false;

        // move isn't strict to depth since it's used for move ordering
        // even if it's not good it has a higher chance of being better than the other moves
        bestMove.StartingSquare = entry.BestMove.StartingSquare;
        bestMove.TargetSquare = entry.BestMove.TargetSquare;

        if (entry.Depth < depth) return false;

        if (entry.NodeFlag == TTFlag::Exact) {
            score = entry.Score;
            return true;
        } else if (entry.NodeFlag == TTFlag::LowerBound) {
            if (entry.Score >= beta) {
                score = entry.Score;
                return true;
            }
        } else if (entry.NodeFlag == TTFlag::UpperBound) {
            if (entry.Score <= alpha) {
                score = entry.Score;
                return true;
            }
        }

        return false;
    }

    void Store(uint64_t key, int depth, int score, TTFlag flag, Move bestMove) {
        TTEntry& entry = m_Entries[key % m_Entries.size()];

        // only update if it comes from a deeper search than whats already present
        if (entry.Depth <= depth) {
            entry = TTEntry(key, score, depth, flag, TTMove(bestMove.StartingSquare, bestMove.TargetSquare));
        }
    }

    void SetSize(unsigned int sizeMb) {
        m_Entries.resize((sizeMb * 1024 * 1024) / sizeof(TTEntry));
        Clear();
    }

    [[nodiscard]]
    inline unsigned int GetSize() const noexcept {
        return (m_Entries.size() * sizeof(TTEntry)) / 1024 / 1024;
    }
};


class PawnTranspositionTable {
public:
    struct PawnTTEntry {
        uint64_t Key{0};
        int32_t Score{0};
    };

private:
    std::vector<PawnTTEntry> m_Entries;

public:
    void Clear() {
        std::memset(m_Entries.data(), 0, m_Entries.size() * sizeof(PawnTTEntry));
    }

    const PawnTTEntry* Probe(uint64_t key) const noexcept {
        const PawnTTEntry& e = m_Entries[key % m_Entries.size()];
        return (e.Key == key) ? &e : nullptr;
    }

    void Store(uint64_t key, int score) noexcept {
        m_Entries[key % m_Entries.size()] = PawnTTEntry(key, score);
    }

    void SetSize(unsigned int sizeMb) {
        m_Entries.resize((sizeMb * 1024 * 1024) / sizeof(PawnTTEntry));
        Clear();
    }

    [[nodiscard]]
    inline unsigned int GetSize() const noexcept {
        return (m_Entries.size() * sizeof(PawnTTEntry)) / 1024 / 1024;
    }
};

#pragma region Magic Bitboard

namespace MagicBitboard {
    struct Magic {
        uint64_t Mask;
        uint64_t Number;
        int Shift;
        uint64_t* Attacks;
    };

    static constexpr inline const uint64_t OrthogonalMagicNumbers[64] = {
        0x808010a180024002, 0x8040002000401005, 0x0080200028805000, 0x0680080080100004,
        0x4200040820100200, 0x4600020090080904, 0x0200080402004081, 0x0100010008204082,
        0x2000802040008002, 0x0802804000200280, 0x0001003020010048, 0x0005000900201000,
        0x0061000800850050, 0x4802000200083004, 0x485200410c282200, 0x0000800850800100,
        0x0200208005400183, 0x0040010020410a80, 0x0408120040802200, 0x8001030022100088,
        0x0028008008800c00, 0x0000808002002c00, 0x40800400100b0688, 0x8020220034014181,
        0x0500229080004001, 0x4000200080400082, 0x0003001100200040, 0x1050001080800800,
        0x0514008080040800, 0x8040040080020080, 0x18420002000803e4, 0x00004842000400a9,
        0x4810400064800090, 0x0000201000400044, 0x1282801001802007, 0x1000821000800800,
        0x0000980081800400, 0x0800800a00804400, 0x4400084264001110, 0x0acd04410a00048c,
        0x1088804001208008, 0x08b000c0a0104004, 0x8010012000808010, 0x420100241001000a,
        0x0804008028008016, 0x40820090940a0008, 0x0012021008040001, 0x0880244d038a0004,
        0x0440224100820200, 0x4c01082083400500, 0x0408408010220200, 0x8024900080080280,
        0x0820841008010100, 0x8051000804000700, 0x40441002080d0400, 0x1a40800100004080,
        0x8441008000419163, 0x0080c1108100a206, 0x0001a00842001082, 0x20e0500005000921,
        0x0001001008000245, 0x9105000400460801, 0x08060000a4080102, 0x0011802041041882
    };

    static constexpr inline const int OrthogonalShifts[64] = {
        52, 53, 53, 53, 53, 53, 53, 52,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        52, 53, 53, 53, 53, 53, 53, 52
    };

    static constexpr inline const uint64_t DiagonalMagicNumbers[64] = {
        0x0090240108046100, 0x0010161800508102, 0x0249480501008004, 0x802220820000a000,
        0x241088200c000000, 0x410208064a00c002, 0x0204190808240001, 0x00a0440201822000,
        0x8c00050802080203, 0x0040204201820184, 0x0024040810891210, 0x2056840400800080,
        0x2100420210000200, 0x2000050908c00010, 0x8100040084900810, 0x00100484c1101001,
        0x1710904050820090, 0x02c8022001010200, 0x0810040800202020, 0x0408400c04020888,
        0xa840800400a00000, 0x004280811000a000, 0x00009082c8080800, 0x0001000201820122,
        0x0020060150900208, 0x00501c1052240400, 0x0043100021004200, 0x304200600a008200,
        0x0209001005024000, 0x8041410226100201, 0x030c044201084629, 0x00040840888a0090,
        0x2044024104200404, 0x0088089844240100, 0x0001080210050400, 0x8480401808008200,
        0x2141010400020120, 0x0804010201a84804, 0x2004408200040100, 0x0090810040210400,
        0x0000882010004a02, 0x0030c8421008082c, 0x80013a4050000800, 0x0104482011001800,
        0x0080081020918400, 0x004010a480808500, 0x4128020c10480408, 0x0005560082020100,
        0x0004040402089001, 0x0002004404042000, 0x00611300a0902008, 0x0081801084110101,
        0x8181882020450004, 0x0000400408108081, 0x00100410a2820040, 0x2004034802108000,
        0x0809440084104200, 0x01002200540a0800, 0x803004808400c800, 0x0080000010228800,
        0x8100400018302400, 0x0001000408102110, 0x000020080e10c400, 0x440808010803210a
    };

    static constexpr inline const int DiagonalShifts[64] = {
        58, 59, 59, 59, 59, 59, 59, 58,
        59, 59, 59, 59, 59, 59, 59, 59,
        59, 59, 57, 57, 57, 57, 59, 59,
        59, 59, 57, 55, 55, 57, 59, 59,
        59, 59, 57, 55, 55, 57, 59, 59,
        59, 59, 57, 57, 57, 57, 59, 59,
        59, 59, 59, 59, 59, 59, 59, 59,
        58, 59, 59, 59, 59, 59, 59, 58
    };

    static uint64_t OrthogonalTable[64][4096];
    static uint64_t DiagonalTable[64][512];

    static Magic OrthogonalMagics[64];
    static Magic DiagonalMagics[64];

    static uint64_t OrthogonalMask(int sq) {
        uint64_t m = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1; nr < Ranks - 1; ++nr) {
            m |= IndexToMask(To2DIndex(nr, f));
        }
        for (int nr = r - 1; nr > 0; --nr) {
            m |= IndexToMask(To2DIndex(nr, f));
        }
        for (int nf = f + 1; nf < Files - 1; ++nf) {
            m |= IndexToMask(To2DIndex(r, nf));
        }
        for (int nf = f - 1; nf > 0; --nf) {
            m |= IndexToMask(To2DIndex(r, nf));
        }

        return m;
    }

    static uint64_t DiagonalMask(int sq) {
        uint64_t m = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1, nf = f + 1; nr < Ranks - 1 && nf < Files - 1; ++nr, ++nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }
        for (int nr = r + 1, nf = f - 1; nr < Ranks - 1 && nf > 0; ++nr, --nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }
        for (int nr = r - 1, nf = f + 1; nr > 0 && nf < Files - 1; --nr, ++nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }
        for (int nr = r - 1, nf = f - 1; nr > 0 && nf > 0; --nr, --nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }

        return m;
    }

    static uint64_t SlowOrthogonal(int sq, uint64_t occ) {
        uint64_t a = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1; nr < Ranks; ++nr) {
            a |= IndexToMask(To2DIndex(nr, f)); if (occ & IndexToMask(To2DIndex(nr, f))) break;
        }
        for (int nr = r - 1; nr >= 0; --nr) {
            a |= IndexToMask(To2DIndex(nr, f)); if (occ & IndexToMask(To2DIndex(nr, f))) break;
        }
        for (int nf = f + 1; nf < Files; ++nf) {
            a |= IndexToMask(To2DIndex(r, nf)); if (occ & IndexToMask(To2DIndex(r, nf))) break;
        }
        for (int nf = f - 1; nf >= 0; --nf) {
            a |= IndexToMask(To2DIndex(r, nf)); if (occ & IndexToMask(To2DIndex(r, nf))) break;
        }

        return a;
    }

    static uint64_t SlowDiagonal(int sq, uint64_t occ) {
        uint64_t a = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1, nf = f + 1; nr < Ranks && nf < Files; ++nr, ++nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }
        for (int nr = r + 1, nf = f - 1; nr < Ranks && nf >= 0; ++nr, --nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }
        for (int nr = r - 1, nf = f + 1; nr >= 0 && nf < Files; --nr, ++nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }
        for (int nr = r - 1, nf = f - 1; nr >= 0 && nf >= 0; --nr, --nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }

        return a;
    }

    static void Init() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const auto Populate = [sq](
                Magic& m, uint64_t* table, uint64_t magic, int shift, uint64_t mask, bool isRook
            ) -> void {
                m.Mask = mask;
                m.Number = magic;
                m.Shift = shift;
                m.Attacks = table;

                uint64_t occ = 0ull;

                do {
                    table[(occ * magic) >> shift] = isRook ? SlowOrthogonal(sq, occ) : SlowDiagonal(sq, occ);
                    occ = (occ - mask) & mask;
                } while (occ);
                };

            Populate(OrthogonalMagics[sq], OrthogonalTable[sq], OrthogonalMagicNumbers[sq], OrthogonalShifts[sq], OrthogonalMask(sq), true);
            Populate(DiagonalMagics[sq], DiagonalTable[sq], DiagonalMagicNumbers[sq], DiagonalShifts[sq], DiagonalMask(sq), false);
        }
    }

    [[nodiscard]]
    static inline uint64_t OrthogonalAttacks(int sq, uint64_t occ) noexcept {
        const Magic& m = OrthogonalMagics[sq];
        return m.Attacks[((occ & m.Mask) * m.Number) >> m.Shift];
    }

    [[nodiscard]]
    static inline uint64_t DiagonalAttacks(int sq, uint64_t occ) noexcept {
        const Magic& m = DiagonalMagics[sq];
        return m.Attacks[((occ & m.Mask) * m.Number) >> m.Shift];
    }

    [[nodiscard]]
    static inline uint64_t CombinedAttacks(int sq, uint64_t occ) noexcept {
        return OrthogonalAttacks(sq, occ) | DiagonalAttacks(sq, occ);
    }
}

#pragma region Opening Book

class OpeningBook {
private:
    static constexpr inline const struct {
        uint64_t Hash;
        uint8_t StartSquare;
        uint8_t TargetSquare;
        uint16_t Flag;
    } OPENING_BOOK[] = {
        {0xa75f52d1e18b27, 6, 21, 0}, {0xa75f52d1e18b27, 10, 26, 2}, {0xa75f52d1e18b27, 12, 28, 2}, {0x15745392d606b97, 53, 37, 2}, {0x15745392d606b97, 58, 44, 0}, {0x15745392d606b97, 62, 45, 0}, {0x1f34dbfa4b0d4c8, 57, 42, 0}, {0x23715eb9a167e7e, 52, 36, 2}, {0x23715eb9a167e7e, 62, 45, 0}, {0x2bee5d7b4561cc2, 28, 35, 4}, {0x2d5eb2fce610f48, 28, 35, 4}, {0x31b590e453f7e8d, 10, 18, 0}, {0x39016ffa3210662, 35, 27, 0}, {0x3982eeba5ef04ad, 11, 19, 0}, {0x412ce6b2789ad0c, 61, 43, 0}, {0x44990b985645305, 25, 61, 0}, {0x45c44f1d0ad2086, 57, 42, 0}, {0x4b4bef35e1ce371, 4, 6, 16}, {0x4bd3ca0fa80e994, 21, 36, 0}, {0x4d2c900ff4ac248, 11, 19, 0}, {0x4d39fcceeb63842, 12, 20, 0}, {0x53f79f9e44c819b, 4, 13, 4}, {0x5695168525ca06e, 12, 20, 0}, {0x5b3d661dfb95a6c, 29, 36, 4}, {0x5c2b942db85231b, 4, 6, 16}, {0x5cf09086b1e4b04, 62, 45, 0}, {0x615104003e70b1c, 5, 33, 0}, {0x69908c9720747a9, 57, 40, 0}, {0x6f90155cd10cfd4, 18, 28, 4}, {0x75657b5937dc80f, 62, 45, 0}, {0x75b7c74b3c69cf1, 58, 44, 0}, {0x788d8b34af6134e, 15, 23, 0}, {0x788d8b34af6134e, 18, 1, 0}, {0x788d8b34af6134e, 18, 24, 0},
        {0x7d3e280321b2dc0, 51, 35, 2}, {0x7d3e280321b2dc0, 57, 42, 0}, {0x8997ba98383ce0f, 5, 14, 0}, {0x8b4b28c69244b0b, 38, 14, 4}, {0x8bf66262cfbc744, 9, 18, 4}, {0x8ce06501790d3f4, 4, 6, 16}, {0x970e908159fd063, 53, 37, 2}, {0x970e908159fd063, 58, 44, 0}, {0x9940dff67ce2e22, 10, 26, 2}, {0x9d84d6aea494341, 51, 42, 4}, {0xb2bd5cc08626a28, 59, 51, 0}, {0xb3db31b3856596a, 28, 35, 4}, {0xb732b11a9516884, 51, 35, 2}, {0xb732b11a9516884, 51, 43, 0}, {0xb7372bb01b6dc59, 51, 35, 2}, {0xb784f20f53c7894, 51, 35, 2}, {0xb784f20f53c7894, 61, 60, 0}, {0xb823fa4b44ab02b, 60, 58, 32}, {0xb823fa4b44ab02b, 61, 34, 0}, {0xba181ede61f0025, 5, 14, 0}, {0xc2ee0ec03523815, 61, 52, 0}, {0xc2ee0ec03523815, 62, 45, 0}, {0xc2ee0ec03523815, 62, 52, 0}, {0xc36d4696a3412f4, 6, 21, 0}, {0xc38931cc51336e6, 4, 6, 16}, {0xc67d52a4b0ef6ae, 58, 30, 0}, {0xc68617e049fcab9, 57, 42, 0}, {0xd301551a7b16008, 60, 62, 16}, {0xd350246887e2f7d, 6, 21, 0}, {0xd3c1ab25292f9bb, 5, 14, 0}, {0xd4392956fbd6c42, 4, 6, 16}, {0xd7e3d7241e922f3, 6, 21, 0}, {0xdb4814479e7f5cb, 5, 33, 0}, {0xdb4dbf0d6abf235, 3, 27, 4},
        {0xdbf4352c837657f, 5, 14, 0}, {0xe6d6546ed65562a, 6, 21, 0}, {0xe8b2a2bb10ca2a8, 62, 52, 0}, {0xec604555881bccc, 5, 12, 0}, {0xecb02786815791f, 36, 28, 0}, {0xf10d27d368d3573, 12, 20, 0}, {0xf2df5e549c8f8e6, 21, 27, 4}, {0xf37200f34e58789, 14, 30, 2}, {0xf60f41ca0084fb1, 5, 12, 0}, {0xfc2c33c5eeca3ff, 50, 42, 0}, {0xffbe719da023c21, 33, 25, 0}, {0x1036248e68a92d0c, 5, 12, 0}, {0x10d22389c613c54a, 52, 36, 2}, {0x10d22389c613c54a, 54, 46, 0}, {0x111f18aeac65b34e, 1, 16, 0}, {0x116d1ab91540c8b7, 30, 13, 4}, {0x11771a67f340b69e, 59, 50, 0}, {0x12ce58fb9659aba9, 10, 18, 0}, {0x12ce58fb9659aba9, 27, 21, 0}, {0x13164ba0fb76e249, 28, 36, 0}, {0x132d7b00d4c5b8d9, 36, 28, 0}, {0x132d7b00d4c5b8d9, 58, 30, 0}, {0x1340ceb55baacec9, 61, 43, 0}, {0x135188dfdcabe56b, 62, 45, 0}, {0x13791ab0cd7dd3a4, 1, 18, 0}, {0x1390c4c45e1a96d2, 1, 11, 0}, {0x13e2b6ec99ef1ded, 26, 17, 0}, {0x1450d63d9a17f23b, 14, 22, 0}, {0x1475200cc5af800d, 56, 58, 0}, {0x1475200cc5af800d, 59, 50, 0}, {0x14a52a7dc01fd816, 1, 11, 0}, {0x14a52a7dc01fd816, 15, 23, 0}, {0x1511e1979235f273, 28, 35, 4}, {0x159e07eadae30d95, 8, 16, 0},
        {0x16abbe3e031d98a9, 1, 18, 0}, {0x16abbe3e031d98a9, 6, 21, 0}, {0x171cb8fd334aaf03, 11, 27, 2}, {0x1749fdb9d72f3de8, 1, 18, 0}, {0x1787206dd499fda7, 45, 35, 4}, {0x179b8dfffd580f79, 35, 26, 4}, {0x17a1c96ac856e2e6, 48, 32, 2}, {0x17e859e33b72fbbc, 5, 26, 0}, {0x181b917466572720, 58, 30, 0}, {0x183f91f954c78a70, 27, 34, 4}, {0x1849eb177dfa4750, 6, 21, 0}, {0x1849f46ef21ceb91, 45, 30, 0}, {0x18580319d9bed406, 6, 21, 0}, {0x18704c48ce62ed57, 4, 6, 16}, {0x187f1e6fa589d0da, 4, 6, 16}, {0x18803601c2e139ad, 50, 34, 2}, {0x18e8a3baa768a648, 12, 28, 2}, {0x1939330ac0d4fbbe, 53, 37, 2}, {0x1939330ac0d4fbbe, 54, 38, 2}, {0x1939330ac0d4fbbe, 58, 44, 0}, {0x195b45d846502852, 21, 11, 0}, {0x19abfe93ee3ab1b9, 33, 42, 4}, {0x19ba44b1e70fc04b, 51, 35, 2}, {0x19c183af08383ab6, 52, 44, 0}, {0x19f01f3095a9e2a0, 62, 45, 0}, {0x19f399cf8b1348a4, 1, 18, 0}, {0x1a0167a8f3b0f610, 4, 6, 16}, {0x1a1680d37d7519b1, 1, 18, 0}, {0x1a6a67a37cb1eabc, 6, 21, 0}, {0x1a808a61f91a3fc2, 53, 45, 0}, {0x1ab9b062b54cc2c5, 61, 43, 0}, {0x1ad43b278a0c9f6c, 20, 27, 4}, {0x1ae187a3931a8dd1, 58, 49, 4}, {0x1b0eb3826e26489e, 10, 26, 2},
        {0x1b1f402b4dc531b8, 61, 52, 0}, {0x1b58d91e16685942, 21, 27, 4}, {0x1ba26c708ef0a250, 61, 54, 0}, {0x1bb3308ca1b0370d, 12, 21, 4}, {0x1bc4fb1daf51b398, 12, 20, 0}, {0x1c5dbbd1d608b996, 5, 12, 0}, {0x1d6326ad29c5f347, 1, 11, 0}, {0x1d6539fd706b240e, 51, 35, 2}, {0x1dafced1b402ccca, 55, 39, 2}, {0x1db32cdc2c75ab5c, 61, 43, 0}, {0x1e0ab4d128dd3b9d, 2, 38, 0}, {0x1eaaf3affc6bb78b, 11, 19, 0}, {0x1ee942f46fd1de08, 1, 11, 0}, {0x1ee942f46fd1de08, 27, 34, 4}, {0x1f8a2bf96df71a68, 18, 28, 4}, {0x2024f746f1b5b223, 5, 33, 0}, {0x20371de4ea35f89b, 10, 26, 2}, {0x207f4eb79fb667e7, 26, 35, 4}, {0x20d209f158cc1a4b, 58, 49, 0}, {0x214d7b45fc8c7699, 3, 27, 4}, {0x2216b08ee1b38cf8, 5, 12, 0}, {0x2224c1e2535e2ea9, 62, 45, 0}, {0x2283ead7631c704e, 50, 42, 0}, {0x22945a0893677992, 6, 21, 0}, {0x22ecdfd2f894709e, 11, 19, 0}, {0x23d094534740accb, 61, 60, 0}, {0x23e83ef2c8f52a3e, 54, 46, 0}, {0x24ba85d8e774a79f, 1, 11, 0}, {0x25316ce06d28ce10, 57, 42, 0}, {0x253e400ebc5156b0, 37, 28, 4}, {0x25a125a8dc6094a7, 51, 35, 2}, {0x25c8c3f6f5011681, 11, 19, 0}, {0x26623849d14ddf12, 6, 21, 0}, {0x26a86f77714a2e72, 4, 6, 16},
        {0x276bb6549427367c, 36, 27, 4}, {0x2774bef9af5a23fb, 33, 42, 4}, {0x2774bef9af5a23fb, 36, 42, 4}, {0x27dad845c0053dee, 28, 36, 0}, {0x27e3f28d5ac7fd7c, 62, 45, 0}, {0x289a3863b6967663, 59, 45, 0}, {0x28bbd57cd2c92d71, 54, 46, 0}, {0x28cf3ccd4d62c08a, 33, 42, 12}, {0x28de78e3d9541165, 18, 12, 0}, {0x28ff1783372fdf7e, 38, 14, 4}, {0x291a7c9f1e0abbdb, 51, 35, 2}, {0x291a7c9f1e0abbdb, 57, 42, 0}, {0x291a7c9f1e0abbdb, 60, 62, 16}, {0x292b71ca34dc796e, 19, 26, 4}, {0x29922e210e02b1d3, 60, 62, 16}, {0x2a43e8cb6213014e, 12, 21, 4}, {0x2a8474262d1475a7, 51, 35, 2}, {0x2a8474262d1475a7, 61, 34, 0}, {0x2b5ecc6024af7c64, 62, 52, 0}, {0x2c327a557ad560d8, 30, 38, 0}, {0x2cb9e99e5f87b6b5, 5, 14, 0}, {0x2d8a91f310636888, 34, 27, 4}, {0x2daaff9456e99dc6, 14, 22, 0}, {0x2db4f8d0646d1bdb, 8, 16, 0}, {0x2e118256bf2eeaf5, 51, 35, 2}, {0x2e16d4aaf06237a9, 4, 6, 16}, {0x2e16d4aaf06237a9, 10, 26, 2}, {0x2e16d4aaf06237a9, 11, 27, 2}, {0x2e98145ccf24c6a9, 49, 42, 4}, {0x2ea9c6d3e55d4b59, 36, 27, 4}, {0x2ea9c6d3e55d4b59, 36, 28, 0}, {0x2ea9c6d3e55d4b59, 57, 42, 0}, {0x2ea9c6d3e55d4b59, 57, 51, 0}, {0x2ecaf8dfb68824ed, 4, 6, 16},
        {0x2eeaabed9eafb752, 12, 20, 0}, {0x2f4d004506dad5ba, 59, 52, 0}, {0x2fec01cb7122fc5c, 14, 22, 0}, {0x2ff43857896b8a87, 14, 22, 0}, {0x301954aa6af416f2, 35, 27, 0}, {0x3035b93eb5562147, 57, 42, 0}, {0x30532614569c7695, 2, 29, 0}, {0x31a11292ab7b2346, 6, 21, 0}, {0x31a11292ab7b2346, 10, 18, 0}, {0x31ac6f5f08889d16, 36, 28, 0}, {0x31f3aa1062152b0e, 34, 26, 0}, {0x321c88cdd70165b7, 60, 62, 16}, {0x3255cce892843891, 26, 35, 4}, {0x325f399644bb0c34, 60, 62, 16}, {0x3284673fbe0b2ea8, 61, 52, 0}, {0x32d93cac0af237fe, 61, 54, 0}, {0x33026d87f0aa9cda, 11, 19, 0}, {0x331457a0b9fb0821, 6, 21, 0}, {0x331457a0b9fb0821, 11, 27, 2}, {0x331457a0b9fb0821, 13, 29, 2}, {0x3328bf1912c52ed0, 28, 22, 0}, {0x3427d7ac7ca30336, 53, 37, 2}, {0x347cbd74bc6671b4, 45, 28, 0}, {0x347cbd74bc6671b4, 52, 44, 0}, {0x3481efc9aca67958, 6, 21, 0}, {0x34dde160c97a50c9, 8, 16, 0}, {0x350036b722a08351, 42, 36, 4}, {0x3507cff80b24d0d8, 51, 35, 2}, {0x3507cff80b24d0d8, 57, 42, 0}, {0x352d608764af2eca, 21, 36, 4}, {0x353b72157ea666c6, 25, 32, 0}, {0x353bef1411d98265, 9, 17, 0}, {0x355f4d19a560a38a, 15, 23, 0}, {0x35926083c2e542ee, 59, 41, 0},
        {0x3608c6f42e4a9dd1, 59, 51, 0}, {0x361330c7ea1cf924, 1, 18, 0}, {0x361330c7ea1cf924, 4, 6, 16}, {0x36595ea5576dea33, 19, 27, 0}, {0x366904b90c7ef2f1, 60, 62, 16}, {0x3690df3200a212cc, 6, 21, 0}, {0x3690df3200a212cc, 11, 27, 2}, {0x36a12c9c926247d2, 60, 58, 32}, {0x36c09400132c769b, 51, 35, 2}, {0x372b1e3caabe7117, 50, 34, 2}, {0x372b1e3caabe7117, 57, 42, 0}, {0x372b1e3caabe7117, 61, 43, 0}, {0x377896dd4cd94e06, 5, 26, 0}, {0x377896dd4cd94e06, 6, 21, 0}, {0x37d1447a2bdd227c, 1, 18, 0}, {0x37d4c132b1eca618, 4, 6, 16}, {0x38a1830e060ac3ae, 5, 14, 0}, {0x396fcb1eed636d8f, 11, 27, 2}, {0x3978ada870f2c929, 48, 40, 0}, {0x3978ada870f2c929, 52, 44, 0}, {0x3978ada870f2c929, 58, 30, 0}, {0x3978ada870f2c929, 59, 50, 0}, {0x39c5d5e403968fc3, 57, 42, 0}, {0x39c92604b9fa2600, 36, 27, 4}, {0x3a63997f51b80ffd, 1, 18, 0}, {0x3a63997f51b80ffd, 4, 6, 16}, {0x3a6bca9f69067e48, 14, 15, 4}, {0x3a7339e037fe0b8d, 62, 45, 0}, {0x3a83a038c652d95a, 5, 14, 0}, {0x3aca7317ed90d5fe, 1, 18, 0}, {0x3ae0c09fcb1d9339, 6, 21, 0}, {0x3b6710d1c639c0a0, 11, 19, 0}, {0x3bb93afb45242d46, 51, 35, 2}, {0x3bc6af36a91dbbf2, 53, 37, 2},
        {0x3bda51879a1d0d9d, 21, 27, 0}, {0x3c07214e15c8ad49, 59, 35, 4}, {0x3c3586255eeb5071, 59, 45, 0}, {0x3c4515c57063197d, 53, 37, 2}, {0x3c9accd49c9e4979, 29, 22, 0}, {0x3ca36fbd046f03d9, 28, 19, 4}, {0x3cc5c2cd55a90cec, 53, 37, 2}, {0x3cc5c2cd55a90cec, 62, 45, 0}, {0x3cf5a8901477d18b, 45, 28, 4}, {0x3dd59385acf02ffe, 58, 44, 0}, {0x3ddb94d1342eb8c2, 5, 12, 0}, {0x3e32b0f308d33ad1, 51, 35, 2}, {0x3e4e9e2aa2a1cab0, 62, 45, 0}, {0x3eea62a52b6a746d, 45, 30, 0}, {0x3f3770f992200b02, 51, 35, 2}, {0x3f3770f992200b02, 62, 45, 0}, {0x3fa0edf08d80e4cd, 54, 46, 0}, {0x3fc36698e8d6c77d, 1, 18, 0}, {0x3fd3e849b6bb6d16, 61, 34, 4}, {0x3fd498ddfe1b1b11, 19, 27, 0}, {0x3fe1d0b01f5e3188, 60, 62, 16}, {0x3fe84e8550450c0e, 2, 11, 4}, {0x400f4a186e93696a, 28, 37, 4}, {0x40175fe1d64078b8, 59, 52, 0}, {0x4156918359e12a8b, 21, 36, 4}, {0x41b6682970443492, 48, 40, 0}, {0x41b6682970443492, 61, 43, 0}, {0x41d4c313c2f1dd47, 42, 35, 4}, {0x41e2c34399790e43, 8, 16, 0}, {0x41fe319a2c79041a, 27, 34, 4}, {0x4206d024b37019e5, 57, 42, 0}, {0x4242a9bbfe361874, 57, 42, 0}, {0x42791db2d8ed4700, 34, 27, 4}, {0x42791db2d8ed4700, 62, 45, 0},
        {0x42e069e8695d383f, 51, 35, 2}, {0x42e069e8695d383f, 62, 45, 0}, {0x42e5eca0f36cbc5b, 62, 45, 0}, {0x42f4c4f457c12dfd, 2, 38, 0}, {0x42f4c4f457c12dfd, 12, 20, 0}, {0x43430c64165579b9, 1, 18, 0}, {0x43430c64165579b9, 6, 21, 0}, {0x43430c64165579b9, 11, 19, 0}, {0x4389e1ca759ea172, 11, 18, 4}, {0x439d1a19e91c2e7e, 51, 35, 2}, {0x43a1f2a04222088f, 50, 34, 2}, {0x43a1f2a04222088f, 51, 35, 2}, {0x43a1f2a04222088f, 52, 36, 2}, {0x43a1f2a04222088f, 53, 37, 2}, {0x43a1f2a04222088f, 62, 45, 0}, {0x43d6bc77118dc8ee, 36, 27, 4}, {0x43d6bc77118dc8ee, 62, 45, 0}, {0x43e3f1a18a39f2b9, 57, 42, 0}, {0x4405afc44051cd61, 36, 27, 4}, {0x444e08e920ba1805, 4, 6, 16}, {0x445b690deecdca16, 4, 6, 16}, {0x44cb1dfb42f0bc99, 36, 28, 0}, {0x44cb1dfb42f0bc99, 57, 42, 0}, {0x45b26af8f0fdd076, 11, 27, 2}, {0x4672a19fea969529, 8, 16, 0}, {0x4672a19fea969529, 8, 24, 2}, {0x46753100e8f57635, 1, 11, 0}, {0x468b20988b75b029, 6, 21, 0}, {0x46b35675cc97a4bd, 9, 25, 2}, {0x472ce29c7f9ffa5b, 61, 54, 0}, {0x479ade46ae1125df, 4, 6, 16}, {0x47c68d9a2d17e086, 1, 18, 0}, {0x47c68d9a2d17e086, 12, 20, 0}, {0x47c68d9a2d17e086, 14, 22, 0},
        {0x481de799f807a69c, 30, 39, 0}, {0x4849a2b341e99cda, 54, 46, 0}, {0x4849a2b341e99cda, 57, 42, 0}, {0x4851da6a3d998062, 52, 44, 0}, {0x4881d01b3829d879, 1, 11, 0}, {0x4881d01b3829d879, 5, 12, 0}, {0x48a9a6cc8b0381cb, 45, 60, 0}, {0x4931a7f301260be5, 48, 40, 0}, {0x49e2691a970da780, 2, 11, 4}, {0x49f4ef5f172916af, 5, 14, 0}, {0x4a63047735db6719, 61, 34, 0}, {0x4a7e1381104dc2ba, 6, 21, 0}, {0x4b0c9ffbbefd2de8, 6, 21, 0}, {0x4b2d7e306753c0e7, 4, 6, 16}, {0x4b6e412e5ad47c58, 59, 45, 8}, {0x4b98163f4acbea91, 6, 21, 0}, {0x4c1b2b959e5ba9b4, 1, 18, 0}, {0x4c1b2b959e5ba9b4, 6, 21, 0}, {0x4c45a3a4932c1ad7, 60, 62, 16}, {0x4c4be49674db12b3, 35, 27, 0}, {0x4c4ce416017d57cf, 17, 49, 4}, {0x4dc53e61792f95af, 3, 10, 0}, {0x4dc7da67461f567f, 12, 20, 0}, {0x4de18b888fc5b772, 61, 54, 0}, {0x4dfd3c239ec67fdf, 21, 36, 4}, {0x4e4eddcfb62dc978, 1, 18, 0}, {0x4e606909b0e86c8c, 41, 34, 4}, {0x4e8715f3f30a3a7f, 6, 21, 0}, {0x4ebd2323d0652c12, 3, 12, 0}, {0x4f08149cb602946c, 45, 35, 4}, {0x4f23e7786495924a, 4, 6, 16}, {0x4f2456364c361a69, 6, 21, 0}, {0x4f421472757e11ce, 1, 11, 0}, {0x4f82d5f969f90bac, 27, 36, 4},
        {0x4f921e0370ce49d5, 60, 62, 16}, {0x4fa919b936ee8a18, 3, 27, 4}, {0x5001f94c4dd43d31, 26, 34, 0}, {0x501ea4c4a3beebee, 61, 43, 0}, {0x5063159efac6957f, 61, 60, 0}, {0x50caebb7646f6749, 51, 35, 2}, {0x510787fda2504d6d, 27, 10, 0}, {0x5149feac050a9f76, 60, 62, 16}, {0x5152f898f747bf86, 33, 26, 4}, {0x5188ffc2ea8a0427, 52, 36, 2}, {0x518dac7d8de64dea, 49, 41, 0}, {0x51beea5f88a50275, 6, 21, 0}, {0x51df8723dedc0097, 4, 6, 16}, {0x5234223e61a51c31, 5, 14, 0}, {0x52597ad2034d7030, 51, 35, 2}, {0x52a06893fd78ccbe, 61, 52, 0}, {0x52b8650282d05d09, 62, 45, 0}, {0x52d2df1f1847e73f, 13, 29, 2}, {0x535315db93cdc551, 52, 36, 2}, {0x535315db93cdc551, 62, 45, 0}, {0x53c056e29a3aedd7, 58, 51, 0}, {0x54ac2c78a20df877, 60, 62, 16}, {0x54c18972b84d017b, 35, 25, 0}, {0x54c18972b84d017b, 35, 29, 0}, {0x55093beeeef5795a, 58, 44, 0}, {0x550e892a9e86795e, 49, 41, 0}, {0x551480a827b99409, 5, 14, 0}, {0x5564b18db9e4a00b, 50, 34, 2}, {0x5564b18db9e4a00b, 54, 46, 0}, {0x5564b18db9e4a00b, 62, 45, 0}, {0x55ba36b7eae546b6, 5, 26, 0}, {0x55ba36b7eae546b6, 6, 21, 0}, {0x55d9319feb452141, 1, 11, 0}, {0x55d9319feb452141, 1, 18, 0},
        {0x561ee028e22e53e6, 61, 54, 0}, {0x56ab1898d98fccdb, 41, 50, 0}, {0x56c77988421ad8df, 6, 21, 0}, {0x57a0109bdc3626d3, 11, 27, 2}, {0x5839b85ff156c626, 4, 6, 16}, {0x584d9e7ae4056cca, 59, 50, 0}, {0x588636963cb60246, 33, 42, 12}, {0x58af1da418e42e85, 62, 52, 0}, {0x591ab0bfc64fa434, 58, 30, 0}, {0x5a31d126d6cd7509, 6, 21, 0}, {0x5a31d126d6cd7509, 28, 35, 4}, {0x5ac515c5cbeb90c4, 52, 36, 2}, {0x5ac515c5cbeb90c4, 62, 45, 0}, {0x5add2c5933a2e61f, 62, 45, 0}, {0x5aee30b549fbfbc2, 60, 28, 4}, {0x5b26b8414a317924, 11, 27, 2}, {0x5b4f46e5ee39c4f8, 50, 34, 2}, {0x5b4f46e5ee39c4f8, 57, 42, 0}, {0x5b6bc45c848995f9, 60, 62, 16}, {0x5b869bf35122ba05, 11, 19, 0}, {0x5bdfc88195f11379, 10, 26, 2}, {0x5bea4bbb68bfc1fe, 42, 36, 4}, {0x5c067040fbd20bf0, 62, 45, 0}, {0x5c39607b66e5031e, 6, 21, 0}, {0x5c7150a7b0349e8a, 34, 27, 4}, {0x5cfe642cd5e4a670, 52, 44, 0}, {0x5d018731fa62449a, 34, 43, 0}, {0x5d30071f1b8a6a4e, 5, 14, 0}, {0x5d897e118ef39d36, 60, 62, 16}, {0x5da0d54e669928ef, 52, 36, 2}, {0x5db8ecd29ed05e34, 52, 44, 0}, {0x5e474b7d24e10100, 50, 42, 0}, {0x5e53861ad3a46b3c, 54, 46, 0}, {0x5e53861ad3a46b3c, 57, 42, 0},
        {0x5e6c96214e9363d2, 4, 6, 16}, {0x5e9b1a1b69aaa35b, 61, 54, 0}, {0x5ea4275644f7ea5b, 11, 27, 2}, {0x5ee2d6b875ebbb1e, 57, 42, 0}, {0x5eeedaa4ca0f9508, 25, 61, 0}, {0x5f183a81d6f4d51c, 21, 27, 4}, {0x5f32a0593ab2b55e, 3, 35, 4}, {0x5ffe128db91b3fae, 61, 52, 0}, {0x6040ba8101b61736, 4, 13, 4}, {0x6053aae7d6bfa843, 5, 12, 0}, {0x6086577c7f362840, 62, 45, 0}, {0x60adc3fc20d13c42, 33, 51, 12}, {0x60b7914a2ededc7d, 11, 19, 0}, {0x60f983aa6131e973, 1, 11, 0}, {0x60f983aa6131e973, 3, 17, 0}, {0x60f983aa6131e973, 5, 12, 0}, {0x610565c633594703, 14, 22, 0}, {0x61737b5aea86f085, 11, 27, 2}, {0x619407ef829fc255, 62, 45, 0}, {0x61e95e477d89989a, 61, 43, 0}, {0x62d4094c9f6349ad, 54, 46, 0}, {0x6428113962abdf5f, 1, 11, 0}, {0x6428113962abdf5f, 2, 29, 0}, {0x6428113962abdf5f, 6, 21, 0}, {0x64917d2cb29f4494, 35, 27, 0}, {0x64f81b48671b8744, 36, 30, 0}, {0x65a4449769ecf2dd, 51, 35, 2}, {0x6654c9d3858883fd, 36, 19, 4}, {0x6660a54d9e2bff28, 5, 14, 0}, {0x66a1e9561c9a8c91, 62, 45, 0}, {0x671738fa18a90645, 57, 42, 0}, {0x672023929710aeff, 5, 33, 0}, {0x67656684e2e5de92, 61, 34, 0}, {0x67b56cf5e7558689, 1, 11, 0},
        {0x67eef22e1b8d3c60, 45, 28, 4}, {0x68520b3f6b45e904, 55, 39, 2}, {0x68520b3f6b45e904, 62, 45, 0}, {0x6852fd591ed30b04, 45, 28, 4}, {0x685a7a9c9d1a80f7, 11, 27, 2}, {0x685c94b02260b054, 51, 43, 0}, {0x68c4a7f90830a7a6, 10, 26, 2}, {0x690d2990f932e640, 22, 15, 0}, {0x6941a99958b85bca, 34, 27, 4}, {0x69809ff5b8ddb7be, 5, 12, 0}, {0x698e3f9beb4c7a2e, 59, 52, 0}, {0x69a272882d0a3bdc, 60, 62, 16}, {0x69f6816961020038, 14, 22, 0}, {0x6a41639cc7634247, 38, 45, 4}, {0x6a5f56a99eae08b3, 49, 33, 2}, {0x6abb5dcce4ac3bb0, 57, 42, 0}, {0x6b3b99436a5d21de, 62, 45, 0}, {0x6b65c775b9a50129, 36, 27, 4}, {0x6bed7c1eedb159ec, 51, 35, 2}, {0x6c451f616b9456aa, 21, 27, 4}, {0x6cadeb0ae92f2fe2, 5, 33, 8}, {0x6cc132a2d729f8e3, 1, 11, 0}, {0x6cc132a2d729f8e3, 1, 18, 0}, {0x6d0ebc6b16479033, 52, 44, 0}, {0x6d15b142c40bce56, 58, 30, 0}, {0x6d4161da8ce2f714, 53, 37, 2}, {0x6d4161da8ce2f714, 53, 45, 0}, {0x6d4161da8ce2f714, 62, 45, 0}, {0x6d971566e0376127, 57, 42, 0}, {0x6dd09cfd8bb224a0, 11, 19, 0}, {0x6e5169c745207f68, 50, 42, 0}, {0x6e7891e43ea80e1f, 50, 42, 0}, {0x6e7891e43ea80e1f, 57, 42, 0}, {0x6e7891e43ea80e1f, 62, 45, 0},
        {0x6f0f07ce7d3b64d5, 1, 18, 0}, {0x6f2cef547cd8d9b2, 60, 62, 16}, {0x6f5b4a313e31f0ff, 57, 42, 0}, {0x6f5b4a313e31f0ff, 62, 45, 0}, {0x6fb33faf735cb050, 32, 41, 0}, {0x6fd245972d8f20f2, 57, 42, 0}, {0x6ff7a22c2e4227cd, 5, 12, 0}, {0x700f83fbc763a475, 6, 21, 0}, {0x70151c38deec96dc, 28, 19, 5}, {0x70376772aee8eebe, 11, 19, 0}, {0x70539e70701798e0, 27, 36, 4}, {0x7086a619a00cde2b, 11, 27, 2}, {0x70bbd13b07fb80bd, 6, 21, 0}, {0x70bbd13b07fb80bd, 8, 16, 0}, {0x70bbd13b07fb80bd, 13, 29, 2}, {0x70fe249a299e5147, 1, 18, 0}, {0x70fe4b48fb95f7a0, 11, 19, 0}, {0x711a52991d4135e1, 48, 40, 0}, {0x712ddc8fce86739b, 59, 3, 12}, {0x713f0573a5994f0a, 8, 16, 0}, {0x714dd1e217aaec3e, 9, 18, 4}, {0x715d424eb027b6b2, 25, 11, 12}, {0x718729eae208c28f, 55, 47, 0}, {0x71dce9365d162b3e, 11, 19, 0}, {0x720b40d74f8f1c6c, 51, 35, 2}, {0x720b40d74f8f1c6c, 61, 25, 0}, {0x720b40d74f8f1c6c, 61, 34, 0}, {0x721c61a8e15e157b, 49, 33, 2}, {0x72677a27d1fdd24c, 2, 29, 0}, {0x72abbd12d3e3976c, 1, 18, 0}, {0x72abbd12d3e3976c, 6, 21, 0}, {0x72abbd12d3e3976c, 8, 16, 0}, {0x7303a820c292c9ff, 14, 22, 0}, {0x735c29b7541f43b0, 5, 14, 0},
        {0x73e06f38a08499c6, 30, 12, 4}, {0x7412d9c1f0ba5e1a, 30, 13, 4}, {0x742c1ed03b72d64a, 61, 60, 0}, {0x742db3ce397df2bf, 6, 21, 0}, {0x7448b58e5ec5dd56, 1, 18, 0}, {0x744d4bb78f0cb47e, 6, 21, 0}, {0x746668ab6fe071d4, 5, 14, 0}, {0x74e6da8a2ace4562, 34, 27, 4}, {0x751eb41ebd1f6e0f, 34, 27, 4}, {0x751eb41ebd1f6e0f, 61, 52, 0}, {0x753544ccf0dfad83, 9, 18, 4}, {0x7579a1234c2cad50, 57, 42, 0}, {0x75c5a5018caef0fb, 11, 27, 2}, {0x75c6bdc379e12a63, 10, 26, 2}, {0x75d716e2c94e820d, 2, 38, 0}, {0x762d7f4140d52c6c, 8, 16, 0}, {0x768ec12974cd8553, 58, 30, 0}, {0x77063879d896643d, 6, 21, 0}, {0x77664780c9c22d6e, 19, 27, 0}, {0x77c987a416695417, 61, 43, 0}, {0x783e35388b42994d, 20, 28, 0}, {0x7846e759e3d00201, 11, 19, 0}, {0x78c276bef374874a, 3, 12, 4}, {0x791482d3d863ca85, 54, 46, 0}, {0x791fcd7e41d22256, 12, 20, 0}, {0x7a060d9d76659dca, 62, 45, 0}, {0x7a5be5221cb7f03e, 6, 21, 0}, {0x7a5be5221cb7f03e, 10, 26, 2}, {0x7af098c132994ea0, 36, 28, 0}, {0x7af098c132994ea0, 60, 62, 16}, {0x7afa0e665eb2a397, 4, 6, 16}, {0x7b6e40119fde2688, 62, 52, 0}, {0x7b88161bc38f59c4, 51, 35, 2}, {0x7bc68e115288682a, 28, 35, 4},
        {0x7bcdea200ee5783a, 5, 12, 0}, {0x7bcdea200ee5783a, 36, 19, 0}, {0x7bd2ce719822fd63, 26, 35, 4}, {0x7bd2ce719822fd63, 33, 42, 12}, {0x7c4993e1c10e5b51, 6, 21, 0}, {0x7ca5206d254e3461, 61, 52, 0}, {0x7ca5206d254e3461, 62, 45, 0}, {0x7d0def17caf67f02, 1, 18, 0}, {0x7d0def17caf67f02, 5, 19, 0}, {0x7d536726c781cc61, 57, 42, 0}, {0x7d7b5d245af02b3b, 10, 18, 0}, {0x7d85e6e01a64fb40, 49, 42, 4}, {0x7dbe58110fb3a7db, 57, 42, 0}, {0x7de3b5b9592f2691, 53, 45, 0}, {0x7dff2bc25510c9b7, 62, 45, 0}, {0x7e7a27dbe49713b6, 37, 28, 4}, {0x7ecb9a17bb39f39d, 52, 46, 0}, {0x7f03be24c8727cd5, 42, 36, 4}, {0x7f06917ceff7acad, 54, 46, 0}, {0x7f70dddf8118fe51, 4, 6, 16}, {0x7fbfbf6d626fe2d2, 59, 38, 0}, {0x8023b312f2f995b5, 1, 18, 0}, {0x8023b312f2f995b5, 3, 17, 0}, {0x806a63dcabfc7e07, 2, 9, 0}, {0x809782fc609e10ea, 10, 18, 0}, {0x80a586e1479dd732, 45, 35, 4}, {0x80caff6fa876d129, 6, 21, 0}, {0x80caff6fa876d129, 14, 22, 0}, {0x80d44b249a193d1f, 36, 27, 4}, {0x80d78199150bbf50, 10, 18, 0}, {0x80dc44626af3c613, 5, 12, 0}, {0x8123f6074cd8bc62, 15, 23, 0}, {0x8133e92626119914, 3, 21, 0}, {0x813d349f66cb2da3, 58, 30, 0},
        {0x814b2ec7390f8486, 62, 45, 0}, {0x814d3efeef8eeb93, 62, 45, 0}, {0x819aa95fb176b22a, 40, 33, 4}, {0x81ab48f60bdf8651, 53, 37, 2}, {0x81ab48f60bdf8651, 57, 42, 0}, {0x81ab48f60bdf8651, 62, 45, 0}, {0x81bca04b3cad1112, 38, 31, 0}, {0x81ddca3a758e0e6d, 61, 34, 4}, {0x81f734af3c798886, 11, 27, 2}, {0x827236024832da5f, 14, 22, 0}, {0x82975a96d116c87f, 26, 17, 0}, {0x829f09358000b1e5, 12, 28, 2}, {0x82dd2a07bf1218a8, 25, 52, 0}, {0x82e7b1eb6f8eb7a1, 4, 6, 16}, {0x82fdcddde0befed7, 12, 20, 0}, {0x8306aac00d9f4b1d, 36, 28, 0}, {0x830bf9c589119511, 5, 12, 0}, {0x832928f2dd5f8bc3, 59, 45, 4}, {0x83d2d94f4532c289, 19, 28, 4}, {0x845163bc9c4c6006, 4, 6, 16}, {0x8487f137be566892, 61, 43, 0}, {0x84ca494400e5d1b8, 13, 20, 0}, {0x84d1b4b4b9867597, 5, 14, 0}, {0x84e9c47a1ccf25ad, 45, 28, 4}, {0x84ef83540f58c316, 62, 45, 0}, {0x850314773c8f466d, 10, 18, 0}, {0x850314773c8f466d, 12, 20, 0}, {0x850314773c8f466d, 27, 34, 4}, {0x8504f21dc85b5b38, 11, 27, 2}, {0x8521e9085907c3c4, 27, 18, 4}, {0x852473bfac809a66, 5, 26, 0}, {0x857b5439d180b47d, 58, 44, 0}, {0x857b5439d180b47d, 61, 52, 0}, {0x8597483cb6bb13cf, 34, 25, 0},
        {0x8628fe7542fc53b8, 61, 52, 0}, {0x86a87cfd2111c399, 54, 46, 0}, {0x86c9f07582490910, 34, 27, 4}, {0x872f425cbb7286cc, 36, 28, 0}, {0x87843fbf955c3852, 6, 21, 0}, {0x880f8d8926c67d86, 4, 6, 16}, {0x880f8d8926c67d86, 10, 26, 2}, {0x88aeb856251817c7, 9, 25, 2}, {0x88d7ef2fd965c4c1, 60, 62, 16}, {0x88e2d5be34a4e796, 10, 26, 2}, {0x88e2d5be34a4e796, 12, 20, 0}, {0x88e2d5be34a4e796, 14, 22, 0}, {0x897094189f0adb96, 21, 11, 0}, {0x89a9730c00d6c856, 62, 45, 0}, {0x89d1f7af33ad2469, 10, 26, 2}, {0x89d67f0283c3f4c9, 62, 45, 0}, {0x89dc60d9cbb74256, 8, 16, 0}, {0x8a272245abd34cfc, 29, 36, 4}, {0x8a58f007c39fbb6e, 12, 28, 2}, {0x8a5fd2216e7460ab, 14, 22, 0}, {0x8a729f077f5312cf, 52, 44, 0}, {0x8a91383b86b84869, 26, 35, 4}, {0x8a9ce5effef1a546, 52, 36, 2}, {0x8aa92c5914ac4977, 14, 22, 0}, {0x8b03398057cef91b, 60, 62, 16}, {0x8c056cf6ced40761, 5, 33, 8}, {0x8c056cf6ced40761, 11, 27, 2}, {0x8c22f9044d21dfc1, 57, 42, 0}, {0x8c33a1d5908c7a4d, 11, 27, 2}, {0x8cccb1a5cc9c6109, 50, 42, 0}, {0x8d04404845d8e5ed, 18, 28, 4}, {0x8d19bb8665f0d9ad, 62, 45, 0}, {0x8d597c8429e71647, 21, 11, 0}, {0x8d5a07ef1f05123a, 1, 18, 0},
        {0x8d5a07ef1f05123a, 2, 29, 0}, {0x8d7b248560a6d287, 60, 62, 16}, {0x8dcf7645a03ef64f, 51, 43, 0}, {0x8dcf7645a03ef64f, 60, 62, 16}, {0x8e3f8f3dacaa5367, 50, 34, 2}, {0x8e66578fb8fa1a81, 11, 27, 2}, {0x8e66578fb8fa1a81, 28, 37, 4}, {0x8f163656215bf880, 57, 42, 0}, {0x8f68bafab090ce62, 5, 33, 8}, {0x8f72cf54ffa48ede, 36, 27, 4}, {0x8f72cf54ffa48ede, 36, 28, 0}, {0x8f72cf54ffa48ede, 57, 42, 0}, {0x8f7c40bc75bcdbc8, 18, 24, 0}, {0x8fc852d1146e0d6b, 62, 45, 0}, {0x8fdf1a6c7426e19e, 57, 42, 0}, {0x907a1b15b182e6c1, 6, 21, 0}, {0x91b216ea2603d27f, 5, 14, 0}, {0x91bb3713b39a3a90, 51, 35, 2}, {0x9291898512247ad3, 2, 11, 0}, {0x9295e03a274bda58, 9, 25, 2}, {0x92b644426f847719, 5, 12, 0}, {0x93e3e579462044d9, 1, 18, 0}, {0x93e3e579462044d9, 11, 19, 0}, {0x93e3e579462044d9, 12, 20, 0}, {0x93f30dca524ff862, 59, 52, 0}, {0x9462957ffc313d61, 61, 54, 0}, {0x94783fdffd769942, 4, 6, 16}, {0x94dcc67f11dd155f, 51, 35, 2}, {0x94fa98e54c890cdc, 6, 21, 0}, {0x953eb721b6426106, 5, 14, 0}, {0x954b7f24703e388b, 36, 19, 0}, {0x965a9437c141265c, 35, 26, 4}, {0x9699db604a742f1e, 3, 27, 4}, {0x9699db604a742f1e, 6, 21, 0},
        {0x9714ab426f3d4304, 3, 24, 0}, {0x9732cda6bb38024c, 62, 45, 0}, {0x97744df264dceb18, 50, 42, 0}, {0x97744df264dceb18, 57, 51, 0}, {0x97791ef7e0523514, 5, 12, 0}, {0x97791ef7e0523514, 27, 36, 4}, {0x97f2239a7d56493d, 11, 19, 0}, {0x98041db00e416f53, 34, 43, 0}, {0x9835cbbe5bd224b3, 4, 6, 16}, {0x983669de971a107b, 27, 17, 0}, {0x985b00ae17177e26, 5, 14, 0}, {0x98943dd168e690b7, 35, 28, 4}, {0x98943dd168e690b7, 57, 42, 0}, {0x993319e61e918190, 4, 6, 16}, {0x995cb647c56ea9ad, 36, 21, 12}, {0x9995badb1efc698b, 12, 20, 0}, {0x9a1aa3497b3a2b8f, 1, 18, 0}, {0x9a78dd44f49103f3, 45, 35, 4}, {0x9a7aea3d19c691da, 55, 47, 0}, {0x9aaeead86d8169da, 51, 35, 2}, {0x9acbc48a664166bc, 27, 17, 0}, {0x9b22c83ea6d5e303, 28, 35, 4}, {0x9b3612fdcfeabce3, 61, 34, 0}, {0x9bbfdfeac20c0a32, 20, 27, 4}, {0x9c4ff48352c0aaed, 8, 16, 0}, {0x9c5047bc4e0f962e, 11, 27, 2}, {0x9c654676a0223b19, 61, 34, 0}, {0x9c7722b6761f554e, 18, 28, 4}, {0x9c788ce5fe49f3c3, 62, 52, 0}, {0x9ca85b1834a8bac8, 5, 12, 0}, {0x9d012a5fba4e433d, 11, 19, 0}, {0x9d397026ce1b96f4, 58, 44, 0}, {0x9d397026ce1b96f4, 61, 34, 0}, {0x9d4b94b98bad4d13, 45, 28, 4},
        {0x9d633f5d79332286, 50, 42, 0}, {0x9d7600234739946c, 6, 21, 0}, {0x9db214d54be580cb, 12, 20, 0}, {0x9e2ad5353280d158, 10, 26, 2}, {0x9e742950af12cccd, 11, 19, 0}, {0x9e742950af12cccd, 11, 27, 2}, {0x9e883695e606efc2, 58, 44, 0}, {0x9eee34d2e658f418, 1, 18, 0}, {0x9f07a7d2e51d5ed7, 50, 42, 4}, {0x9f1b53100e438c00, 53, 45, 0}, {0x9f909eea9bc63e55, 58, 30, 0}, {0xa02805f8f9f51239, 28, 45, 0}, {0xa02b3a4e95ad5b68, 50, 42, 4}, {0xa0f7511706c2d0b6, 21, 27, 0}, {0xa12d45732cfb82c5, 8, 16, 0}, {0xa14dc705c12075ec, 57, 42, 0}, {0xa1b7c1f26341dbab, 26, 33, 8}, {0xa1ba46fab4aca595, 12, 28, 2}, {0xa1bd8d0ea9a32c5c, 4, 6, 16}, {0xa1bd8d0ea9a32c5c, 11, 19, 0}, {0xa1c3879cfa58ad20, 21, 36, 4}, {0xa22c979b84073ea7, 59, 43, 4}, {0xa2aba9ca1ffa210c, 51, 35, 2}, {0xa2e426942525fb2b, 36, 27, 4}, {0xa396e00884e4b811, 60, 62, 16}, {0xa3b5089285070576, 38, 45, 4}, {0xa3e1f510f2f0966f, 62, 45, 0}, {0xa46e17c1618ad2c6, 6, 21, 0}, {0xa4ee660874091a56, 10, 26, 2}, {0xa5363ed50b8a2e30, 9, 17, 0}, {0xa588e7983ed5af01, 12, 20, 0}, {0xa5b06ce0a238be56, 26, 17, 0}, {0xa5b06ce0a238be56, 26, 33, 4}, {0xa6149187853083b2, 59, 52, 0},
        {0xa6c6b16f6771d477, 4, 6, 16}, {0xa6fd9803941f3369, 5, 12, 0}, {0xa6fd9803941f3369, 27, 34, 4}, {0xa7a78c603fbae988, 60, 62, 16}, {0xa7d4bf51eb9bdcb6, 5, 14, 0}, {0xa8106914ecd5897e, 11, 27, 2}, {0xa844d0dd0d697a46, 53, 45, 0}, {0xa844d0dd0d697a46, 58, 44, 0}, {0xa844d0dd0d697a46, 61, 52, 0}, {0xa89945dad9cdf87c, 51, 35, 2}, {0xa8ac6e0006cd9fee, 36, 21, 12}, {0xa8ce91c94d203044, 35, 18, 4}, {0xa963c090088e5bff, 62, 45, 0}, {0xa978cc780e5be5ba, 11, 19, 0}, {0xa987563795e11531, 6, 23, 0}, {0xa99e75b4d476c460, 6, 21, 0}, {0xaa05934cc07e2ad1, 58, 44, 0}, {0xaa816fcf9457403d, 36, 28, 0}, {0xaa86caac5adbb615, 58, 44, 0}, {0xaa8ac54faff82fa6, 5, 14, 0}, {0xab6a475645eb2514, 6, 21, 0}, {0xab784e2d26ecc0fe, 33, 42, 4}, {0xaba2b57267df82db, 21, 27, 0}, {0xabb3def6e5dfae2d, 61, 54, 0}, {0xacca4d212f607487, 60, 62, 16}, {0xad30c056a7939616, 1, 18, 0}, {0xad30c056a7939616, 12, 28, 2}, {0xad59382b87493e2a, 61, 34, 4}, {0xadd5806f8ebf9d31, 48, 40, 0}, {0xae2a0ebba8963143, 61, 43, 0}, {0xae3756645c86b4c0, 61, 54, 0}, {0xaf23abb78bca81a1, 58, 37, 0}, {0xaf23abb78bca81a1, 59, 41, 0}, {0xaf408540c6606ac3, 61, 54, 0},
        {0xaf5061aa99d0f66d, 21, 36, 4}, {0xafa67861bf8291cd, 52, 44, 0}, {0xafa67861bf8291cd, 54, 46, 0}, {0xb0050712c3850c57, 57, 42, 0}, {0xb05caefa3e971cb9, 62, 45, 0}, {0xb104746ab18a7bbe, 9, 25, 2}, {0xb1649f71f9b0a918, 12, 28, 2}, {0xb16bffa1ab23f060, 62, 45, 0}, {0xb16f8ceb399edf46, 57, 42, 0}, {0xb1805f08e6d3e372, 62, 45, 0}, {0xb182d4dc2bfc4556, 53, 45, 0}, {0xb198bf84d447f0e9, 5, 14, 0}, {0xb198bf84d447f0e9, 6, 21, 0}, {0xb1b7513d91249101, 60, 62, 16}, {0xb20aa55a7fd4fe0c, 15, 23, 0}, {0xb2822c3479d24d60, 62, 45, 0}, {0xb2ae2724c047e11f, 12, 20, 0}, {0xb2b716a594136e37, 10, 26, 2}, {0xb2b716a594136e37, 12, 20, 0}, {0xb2b716a594136e37, 14, 22, 0}, {0xb2b7a9d0265beafc, 45, 30, 0}, {0xb2c0daac0076a14c, 4, 6, 16}, {0xb2cebfeb1fe9d6db, 27, 34, 4}, {0xb30515fbca2d8198, 61, 60, 0}, {0xb35f7098e8a91ea1, 11, 19, 0}, {0xb368d11ebdc1c554, 57, 51, 0}, {0xb368d11ebdc1c554, 58, 51, 0}, {0xb46f100aa00cee21, 61, 34, 0}, {0xb46f100aa00cee21, 62, 45, 0}, {0xb47e299a04ff79b3, 48, 32, 2}, {0xb47e299a04ff79b3, 50, 42, 0}, {0xb4a9b6d0f548e92a, 18, 24, 0}, {0xb4a9b6d0f548e92a, 21, 27, 4}, {0xb5461e42d68f2d1f, 51, 35, 2},
        {0xb5ffad3d303a6262, 51, 35, 2}, {0xb6342c3172fdd702, 10, 19, 4}, {0xb6697f07cb469264, 21, 36, 4}, {0xb6ecb141ed0e9c63, 61, 54, 0}, {0xb768f397ce5a8ae4, 5, 14, 0}, {0xb768f397ce5a8ae4, 11, 27, 2}, {0xb81f9b0fa8d593ef, 21, 36, 0}, {0xb84e36ad749e52a8, 11, 19, 0}, {0xb8b1e9a36fcc85a7, 61, 52, 0}, {0xb8e7dcbea2a4f6b8, 57, 42, 0}, {0xb909364ec2bcd6db, 59, 51, 0}, {0xb9433340c14f12ec, 5, 14, 0}, {0xb9526098250b2761, 59, 45, 4}, {0xb99f94a7ddbf8606, 29, 36, 4}, {0xb9a93d15e1d59f9a, 48, 32, 2}, {0xb9aa71bc04c6a617, 57, 42, 0}, {0xb9e4987e4e3e4ff7, 10, 18, 0}, {0xba36c3b0639d01c6, 61, 52, 0}, {0xbab994a310c5385a, 28, 35, 4}, {0xbacfdf481b7cc95d, 48, 40, 0}, {0xbad5ba0574df8d19, 2, 29, 0}, {0xbae668173b2b6d2c, 34, 27, 4}, {0xbb0d92ca1245416b, 27, 17, 0}, {0xbb0f32a090f55aeb, 18, 27, 4}, {0xbb7d9b30ff7aceb6, 61, 43, 0}, {0xbb8f827370d41ed0, 42, 35, 4}, {0xbbcf2497462a14ff, 29, 22, 0}, {0xbbea4cdfcd43aaa7, 5, 33, 0}, {0xbbff87e62cb0c6db, 59, 52, 0}, {0xbc62110ab32f9492, 33, 26, 4}, {0xbc685241bf37f940, 27, 17, 0}, {0xbc939caf2c626e0f, 61, 43, 0}, {0xbcc2a085058ae487, 3, 10, 0}, {0xbcc2a085058ae487, 8, 16, 0},
        {0xbcd3da37eb00e1e7, 5, 12, 0}, {0xbd2781003cc30fdb, 27, 3, 0}, {0xbd2781003cc30fdb, 27, 24, 0}, {0xbd2cc85f446544e4, 15, 23, 0}, {0xbd63f89f71850e4a, 6, 21, 0}, {0xbde294d99976d00d, 12, 20, 0}, {0xbe37ab1ab1900f4b, 45, 28, 4}, {0xbe37ab1ab1900f4b, 51, 35, 2}, {0xbe4dc8ea69058228, 26, 35, 4}, {0xbecf2ccf0345a3b8, 35, 41, 0}, {0xbf56d76c9c38cfcc, 10, 18, 0}, {0xbf56d76c9c38cfcc, 12, 20, 0}, {0xbf9a27a51d499ddd, 3, 12, 0}, {0xc03dafbdd3ebf0a0, 62, 45, 0}, {0xc05150845737bf9e, 6, 12, 0}, {0xc0601356f2f46158, 35, 28, 4}, {0xc0672f1939ca8088, 27, 34, 4}, {0xc0c6f06959062dab, 1, 18, 0}, {0xc0d053055d3adf12, 20, 27, 4}, {0xc12e799f8523b52a, 3, 21, 4}, {0xc15e3d98e28cf462, 11, 19, 0}, {0xc15e3d98e28cf462, 14, 30, 2}, {0xc1a9f2975e6ebccc, 4, 6, 16}, {0xc1da75539662b086, 60, 62, 16}, {0xc238f902ce05fd0f, 61, 43, 0}, {0xc29577ceb89b477a, 6, 21, 0}, {0xc2d39493f8a127d8, 21, 27, 4}, {0xc2e73459e7a254dd, 5, 12, 0}, {0xc2f7e9b5960a6a38, 45, 35, 4}, {0xc2f7e9b5960a6a38, 50, 42, 0}, {0xc2f7e9b5960a6a38, 61, 34, 0}, {0xc34d14aced8b1cc5, 4, 6, 16}, {0xc361827f2ce39483, 54, 46, 0}, {0xc38131171615df28, 50, 34, 2},
        {0xc3d5eab7da298076, 1, 18, 0}, {0xc415283653021ad8, 25, 11, 12}, {0xc44641f5498ad69e, 57, 42, 0}, {0xc45ad233e9a3ea23, 3, 17, 0}, {0xc49c20cea6706740, 61, 34, 0}, {0xc4a79fd94ce9aaa5, 61, 43, 0}, {0xc4f8f10975ca8ccf, 30, 38, 0}, {0xc5324fe62255fb10, 61, 25, 0}, {0xc54a083dcbe362cc, 26, 35, 4}, {0xc5f3bb422d562db1, 6, 21, 0}, {0xc6405fa6701383fe, 52, 44, 0}, {0xc6852927b94c1df7, 58, 49, 0}, {0xc6953934d1ab5c9b, 33, 51, 12}, {0xc70c5c7c8fff98ae, 50, 42, 0}, {0xc7754a3ff2e20316, 12, 20, 0}, {0xc782de0a6a2167c1, 5, 19, 0}, {0xc79467e16cd8e5cb, 61, 34, 0}, {0xc7b301a63b525723, 4, 6, 16}, {0xc7f0b0fda8e83ea0, 4, 6, 16}, {0xc80a4f18250d6729, 3, 10, 0}, {0xc82d5935a7c33fb5, 60, 62, 16}, {0xc839abb5d31974e9, 4, 6, 16}, {0xc87af54c92e22042, 50, 42, 0}, {0xc931617f1f1557f0, 61, 43, 0}, {0xc95370d2db6675c4, 1, 18, 0}, {0xc95370d2db6675c4, 6, 21, 0}, {0xc960daa77eaaa63b, 40, 32, 0}, {0xc981b3088b9c7737, 61, 43, 0}, {0xc9913b62fd6bb5eb, 7, 6, 0}, {0xc99e51ced72c8c84, 4, 6, 16}, {0xc9ef7dbac2b41e27, 11, 19, 0}, {0xca0c31a3eb1c38f4, 45, 30, 0}, {0xca0c31a3eb1c38f4, 51, 35, 2}, {0xca0c31a3eb1c38f4, 51, 43, 0},
        {0xca0c31a3eb1c38f4, 57, 42, 0}, {0xca269edc8497c6e6, 11, 27, 2}, {0xca4d7dffb21b651d, 33, 42, 12}, {0xcb021207ba638757, 58, 51, 4}, {0xcb74bf791a0a2f90, 45, 35, 4}, {0xcbe4784ca767643e, 50, 34, 2}, {0xcbe4784ca767643e, 54, 46, 0}, {0xcc5cc3f7b2eaa42d, 53, 37, 2}, {0xcc7e253bfbd4a06b, 34, 27, 4}, {0xccd5b5ec0cbe55b4, 58, 30, 0}, {0xccd5b5ec0cbe55b4, 62, 45, 0}, {0xccd8c821af4debe4, 6, 21, 0}, {0xccd8c821af4debe4, 11, 27, 2}, {0xcd3922cf3f69d0cf, 6, 21, 0}, {0xcd7f1b3f361e48eb, 62, 45, 0}, {0xce50259a7fc4d560, 36, 19, 0}, {0xce8043b624c83578, 57, 42, 0}, {0xce820e1a4a490fe5, 21, 36, 4}, {0xce820e1a4a490fe5, 28, 35, 4}, {0xce94cd704e15e51d, 22, 43, 4}, {0xceb623fd9f2516d6, 60, 62, 16}, {0xcf60ae74a3fc41f2, 26, 33, 8}, {0xcf8fa14f2e5154ea, 59, 51, 0}, {0xcfc19b2a4da64aec, 5, 12, 0}, {0xcfe644ae2a29fffa, 5, 14, 0}, {0xcff9778aa55be81e, 43, 34, 4}, {0xd0c45c172fd76552, 60, 62, 16}, {0xd11371a8c133d1fb, 60, 28, 4}, {0xd134619600150c1e, 36, 28, 0}, {0xd134619600150c1e, 58, 51, 0}, {0xd147d85c20aa0584, 10, 26, 2}, {0xd1894eef4fdf5eb6, 61, 34, 0}, {0xd194450243135adf, 11, 27, 2}, {0xd1ecddd2f6f534a0, 33, 42, 4},
        {0xd21e0ccae42321a2, 26, 35, 4}, {0xd239a73ba11fa348, 6, 21, 0}, {0xd2988d1d949fb779, 50, 34, 2}, {0xd2d34bd015266a53, 36, 46, 0}, {0xd34457470ea37565, 60, 62, 16}, {0xd3c6d346626bbf21, 55, 39, 2}, {0xd3c6d346626bbf21, 61, 34, 0}, {0xd3dcb8b567a93e7a, 61, 52, 0}, {0xd4e6895fa9c7c721, 6, 21, 0}, {0xd4e6f9a95c560c1e, 60, 62, 16}, {0xd5420990e167cf33, 1, 11, 0}, {0xd544b32391045a83, 35, 18, 4}, {0xd55517a360cd8e6f, 4, 6, 16}, {0xd558edf5b0698489, 42, 35, 4}, {0xd58bb0fd90eaa98c, 6, 21, 0}, {0xd59203e1e4d79728, 28, 11, 4}, {0xd59555377bb56f7c, 21, 27, 4}, {0xd60406c2b71c7129, 4, 6, 16}, {0xd65e4037f8bb5b44, 49, 42, 4}, {0xd698806e0a6f2ad4, 60, 62, 16}, {0xd6d0d882b0708c2c, 5, 33, 0}, {0xd7139e30c24b1d3f, 35, 42, 4}, {0xd7352fc745e9e34d, 11, 27, 2}, {0xd75040d8d9d56342, 4, 3, 4}, {0xd78cc7eaca2ef87b, 30, 21, 4}, {0xd82ce0da2214f8d2, 14, 30, 2}, {0xd845cb9ecaedb8e2, 6, 21, 0}, {0xd84aa62ccd4dc49e, 4, 6, 16}, {0xd84bb9665dc5ed7f, 9, 17, 0}, {0xd86c9a1f2389cb57, 19, 29, 0}, {0xd89b7fd606d4a56c, 11, 27, 2}, {0xd8edf5d38d1e9008, 53, 37, 2}, {0xd8edf5d38d1e9008, 59, 45, 0}, {0xd916907ca3746973, 42, 49, 4},
        {0xd92bd0b42fafc4ce, 51, 35, 2}, {0xd9a984afdb551c52, 12, 20, 0}, {0xd9cd6978f582e514, 50, 34, 2}, {0xd9cd6978f582e514, 51, 35, 2}, {0xd9cf57ad2b9d4c96, 25, 40, 0}, {0xda103dc4e29bd82e, 8, 16, 0}, {0xda103dc4e29bd82e, 14, 22, 0}, {0xdb073a8fe6d5fa4d, 48, 40, 0}, {0xdb127f35f386406f, 6, 12, 0}, {0xdb169f377688a9e0, 52, 44, 0}, {0xdb425f4e92809c95, 10, 26, 2}, {0xdb4ffaae6556c755, 5, 14, 0}, {0xdbbe2f1da06b34e5, 61, 52, 0}, {0xdbfaec9a8f8b6cbe, 48, 40, 0}, {0xdc0d8ca5a579ab08, 49, 33, 2}, {0xdc0d8ca5a579ab08, 50, 42, 0}, {0xdc0d8ca5a579ab08, 51, 43, 0}, {0xdc4f5fe1fd9bb691, 9, 17, 0}, {0xdc52ed024a0e41bc, 5, 12, 0}, {0xdc98fa108f06aa30, 5, 4, 0}, {0xdc98fa108f06aa30, 28, 36, 0}, {0xdd1468592992c8ab, 14, 22, 0}, {0xdd4ed548f2d829f9, 5, 12, 0}, {0xdda6ae8fff096cb9, 6, 21, 0}, {0xddc6972da09d5497, 27, 36, 4}, {0xddd70e23c1c8f7fe, 58, 51, 0}, {0xde2c3c911416bd4f, 50, 42, 0}, {0xde45c4f7ccbcee89, 27, 19, 0}, {0xde5f8097e6c5045d, 45, 28, 4}, {0xde619be614d48818, 10, 18, 0}, {0xdf74e7808ad73970, 21, 27, 4}, {0xdfe0fd7634a20f60, 60, 62, 16}, {0xe02ca1b16c98d364, 6, 21, 0}, {0xe02ca1b16c98d364, 10, 18, 0},
        {0xe02ca1b16c98d364, 10, 26, 2}, {0xe02ca1b16c98d364, 11, 19, 0}, {0xe02ca1b16c98d364, 11, 27, 2}, {0xe02ca1b16c98d364, 12, 20, 0}, {0xe02ca1b16c98d364, 12, 28, 2}, {0xe02ca1b16c98d364, 14, 22, 0}, {0xe096fae9222b3f61, 57, 42, 0}, {0xe19405bb7c7d3a6e, 30, 39, 0}, {0xe1a5b8be26de9060, 3, 10, 0}, {0xe22586d02dbd5947, 62, 45, 0}, {0xe25b67c2acbc1333, 30, 38, 0}, {0xe270f586390f953c, 4, 6, 16}, {0xe2bab3cbb2dd35cb, 5, 33, 8}, {0xe2cf061b9fbaa445, 48, 40, 0}, {0xe2cf061b9fbaa445, 59, 38, 0}, {0xe39432a9fc6d65c9, 8, 16, 0}, {0xe3cbbef879de6a02, 53, 37, 2}, {0xe40cff90c8de3f56, 1, 18, 0}, {0xe469637fea0415f1, 11, 27, 2}, {0xe4d7307f07e83dcf, 5, 12, 0}, {0xe50bc9e7f7dd6d17, 11, 19, 0}, {0xe52ca4677e7ba2f4, 52, 44, 0}, {0xe57e078a208f4d54, 42, 36, 4}, {0xe57ebf09350da472, 61, 43, 0}, {0xe58a76fb663136bf, 34, 25, 8}, {0xe5a11af1a015a75c, 34, 27, 4}, {0xe5e66210af0cbbe2, 45, 35, 4}, {0xe6f8fee9cdb8013f, 1, 18, 0}, {0xe71ec0dddb749c4e, 57, 42, 0}, {0xe73e3009eca7db83, 3, 10, 0}, {0xe7cd078beb3a53d6, 57, 51, 0}, {0xe7d3ee4161b125d3, 10, 18, 0}, {0xe87a55e0c315e855, 2, 29, 0}, {0xe8fde97f3de7df46, 35, 42, 4},
        {0xe941d4b5f09c439b, 8, 16, 0}, {0xe963699182c0c69d, 2, 16, 0}, {0xe9bd656ac3654e19, 59, 52, 0}, {0xe9cf2bb35e2dc470, 21, 36, 4}, {0xe9da9e6da360219e, 7, 6, 0}, {0xea1b4fd896586974, 18, 24, 0}, {0xea5b5b96878cf8dc, 5, 14, 0}, {0xeb976d3e5d0ce3ad, 51, 35, 2}, {0xebaff416053b4422, 45, 28, 4}, {0xebdf1940a4d8ee5b, 62, 45, 0}, {0xebed593f5685ff3f, 60, 62, 16}, {0xec2dbb9f77d161a1, 4, 6, 16}, {0xed065cb624122dd0, 5, 19, 0}, {0xed9c7478b18bb640, 61, 52, 0}, {0xeda2761d73420276, 25, 18, 4}, {0xedc3a523bce094c2, 51, 35, 2}, {0xedfbfdb9ab0bbde9, 42, 36, 4}, {0xee231d6502dae0b4, 35, 26, 4}, {0xee4e3f411f605901, 61, 52, 0}, {0xee59dc7e7300ce2b, 5, 33, 0}, {0xee59dc7e7300ce2b, 6, 21, 0}, {0xee6b97e95b2621b0, 4, 6, 16}, {0xef07e37cb1f1b482, 55, 39, 2}, {0xefa8c664d2bfad8d, 35, 28, 4}, {0xf026db1ba7c07837, 54, 46, 0}, {0xf02e8172a46c780c, 60, 62, 16}, {0xf041a5382fbb598f, 57, 42, 0}, {0xf0837f32fd030786, 45, 30, 0}, {0xf0a88448ea2543c6, 61, 54, 0}, {0xf0de46cabd771eba, 27, 36, 4}, {0xf11cc600a35abc4b, 6, 21, 0}, {0xf1427317cdf16406, 60, 62, 16}, {0xf18ebcda00dc487e, 61, 54, 0}, {0xf1a7480ef855284a, 61, 52, 0},
        {0xf1b1bb5707d0ed9f, 50, 34, 2}, {0xf1b3bba4f10737d3, 61, 43, 0}, {0xf1deaec64dd8d955, 5, 12, 0}, {0xf1fef65b5860f61b, 4, 6, 16}, {0xf1fef65b5860f61b, 9, 17, 0}, {0xf205acd3c430dfcc, 27, 42, 4}, {0xf29da60483f3e5a3, 61, 52, 0}, {0xf29e127647847341, 50, 34, 2}, {0xf29e127647847341, 57, 42, 0}, {0xf2daa4de405a089e, 12, 20, 0}, {0xf319c2384aa020d8, 57, 42, 0}, {0xf349305a8b2cdc87, 10, 18, 0}, {0xf3783776e681ed4c, 4, 6, 16}, {0xf3918e4f04dafd91, 3, 10, 0}, {0xf39aff4f821ad4a3, 35, 29, 0}, {0xf4263c5bbffb0831, 33, 12, 0}, {0xf4263c5bbffb0831, 33, 24, 0}, {0xf4263c5bbffb0831, 33, 26, 0}, {0xf44c53ab0a8225ba, 21, 27, 4}, {0xf47a03557b8f9ec5, 1, 18, 0}, {0xf47c136cad0ef1d0, 14, 22, 0}, {0xf4d580ad847fbcd9, 54, 46, 0}, {0xf4fc086a3b371386, 12, 20, 0}, {0xf561e063c2ab60a2, 33, 42, 12}, {0xf565458dbb1d20dd, 55, 39, 2}, {0xf5761e9cd0d333a7, 49, 42, 4}, {0xf58ef22069ec539f, 61, 54, 0}, {0xf6078c6f0e941e76, 6, 21, 0}, {0xf7b351129453c0d3, 55, 47, 0}, {0xf7cce04fa23ee494, 62, 45, 0}, {0xf80b3f388186c2c3, 50, 34, 2}, {0xf81b3c6182bc1011, 53, 37, 2}, {0xf82896142770c3ee, 1, 18, 0}, {0xf84406a630a58c0d, 60, 62, 16},
        {0xf8940cd73515d416, 1, 11, 0}, {0xf8940cd73515d416, 2, 11, 0}, {0xf8f2b3a4bda83bfe, 36, 42, 4}, {0xf8fdc140fb5eeb1f, 61, 54, 0}, {0xf93441648c541d22, 54, 46, 0}, {0xf93441648c541d22, 57, 42, 0}, {0xf93441648c541d22, 62, 45, 0}, {0xf94203ab7cd702fc, 61, 52, 0}, {0xf94846d4e5514b2f, 14, 22, 0}, {0xf95a372e15b1281d, 12, 20, 0}, {0xf9d4f20cb6a25689, 50, 34, 2}, {0xfa249d3b7d6148c7, 51, 35, 2}, {0xfa249d3b7d6148c7, 61, 25, 8}, {0xfa25f7ee61cf0bd7, 30, 38, 0}, {0xfa7d8fd78259f783, 3, 11, 4}, {0xfa8ad70113c325e0, 60, 62, 16}, {0xfadc96e836fcf481, 45, 28, 4}, {0xfaf0e37ff5a12e35, 4, 6, 16}, {0xfaf97f4356ee1728, 4, 6, 16}, {0xfb7d15d55f1e36b8, 20, 27, 4}, {0xfbb584d9ae0d71f4, 6, 21, 0}, {0xfbb584d9ae0d71f4, 11, 27, 2}, {0xfc3758e1c4f3b691, 39, 31, 0}, {0xfc47a1a4679bc07b, 61, 60, 0}, {0xfc985e9e4256d215, 2, 29, 0}, {0xfce75290c143ee8a, 1, 11, 0}, {0xfcfab18e769e9ff9, 21, 27, 4}, {0xfcfae68794f44e86, 12, 20, 0}, {0xfd223e60687988e3, 60, 62, 16}, {0xfd4dacc4f5d3eafe, 61, 34, 4}, {0xfe2d865f4823f304, 5, 14, 0}, {0xfe6e0794cba3eb3c, 4, 6, 16}, {0xfed21c9b2b03c2f6, 5, 33, 0}, {0xff6effb32cf47ae8, 50, 34, 2},
        {0xff6effb32cf47ae8, 62, 45, 0}, {0xfff22c2525c9a759, 10, 18, 0}
    };

    static constexpr inline const size_t BOOK_SIZE = sizeof(OPENING_BOOK) / sizeof(OPENING_BOOK[0]);

public:
    [[nodiscard]]
    static bool Probe(uint64_t hash, Move& move) noexcept {
        // Binary search to find the FIRST entry with this hash
        int left = 0;
        int right = BOOK_SIZE - 1;
        int firstMatch = -1;

        // Find first occurrence
        while (left <= right) {
            int mid = left + (right - left) / 2;

            if (OPENING_BOOK[mid].Hash == hash) {
                firstMatch = mid;
                right = mid - 1;
            } else if (OPENING_BOOK[mid].Hash < hash) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        // Not found
        if (firstMatch == -1) {
            return false;
        }

        // Count how many entries have this hash
        int count = 1;
        while (firstMatch + count < BOOK_SIZE && OPENING_BOOK[firstMatch + count].Hash == hash) {
            ++count;
        }

        // Randomly select one of the moves for this position
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, count - 1);

        const int selected = dist(rng);
        const int idx = firstMatch + selected;

        // Construct the move
        move = Move(
            OPENING_BOOK[idx].StartSquare,
            OPENING_BOOK[idx].TargetSquare,
            static_cast<MoveFlag>(OPENING_BOOK[idx].Flag)
        );

        return true;
    }
};

#pragma region Context

struct GameContext {
    static inline void BeginNew() {
        GameHistory.clear();
        MoveCount = 0;
    }

    static inline TranspositionTable TT;
    static inline PawnTranspositionTable PawnTT;
    static inline std::vector<uint64_t> GameHistory;
    static inline uint16_t MoveCount;
};

struct SearchContext {
    static inline void BeginNew(uint64_t currentHash) {
        for (auto& h : HistoryTable) h >>= 2;

        std::memset(killerMoves, 0, sizeof(killerMoves));
        std::memset(PositionHistory, 0, sizeof(PositionHistory));
        std::memset(ExcludedMove, 0, sizeof(ExcludedMove));

        PositionHistory[0] = currentHash;

        Cancelled.store(false, std::memory_order_relaxed);
    }

    static inline std::atomic<bool> Cancelled;

    // maximum ply that can be reached
    static constexpr inline const int MaxPly = 256;
    // maximum number of legal moves
    static constexpr inline const int MaxMoves = 256;
    // maximum number of ply extensions allowed
    static constexpr inline const int MaxExt = 6;

    static inline uint32_t HistoryTable[Ranks * Files * Ranks * Files];
    static inline uint64_t PositionHistory[MaxPly];
    static inline Move killerMoves[MaxPly][2];
    static inline Move ExcludedMove[MaxPly];
};

#pragma region Scoring

struct ScoredMove {
    Move Move;
    int Score;
};

constexpr int INF = 1'000'000;
constexpr int SearchCancelScore = 2'000'000;
constexpr int MateThreshold = INF - 512;

#pragma region Lookup Tables

namespace LUTs {
    // only diagonal captures
    static inline uint64_t PawnAttacks[2][64];
    // single and double pushes + attacks
    static inline uint64_t PawnMoves[2][64];
    // only attacks
    static inline uint64_t KingAttacks[64];
    // only attacks
    static inline uint64_t KnightAttacks[64];
    // squares strictly between a and b, 0 if not aligned
    static inline uint64_t RayBetween[64][64];
    // check masks for passed pawns
    static inline uint64_t PassedPawn[2][64];
    // pawn shield squares (immediate rank in front of king, 3 files wide)
    static inline uint64_t PawnShield[2][64];
    // pawn shield second rank
    static inline uint64_t PawnShield2[2][64];
    // precomputed reductions for LMR
    static inline int Reduction[SearchContext::MaxPly][SearchContext::MaxMoves];

    // scores for knight mobility, legal moves
    static constexpr inline int KnightMobilityScore[] = {
        -24, -12, -4, 0, 6, 10, 12, 14, 16
    };
    // scores for bishop mobility, legal moves
    static constexpr inline int BishopMobilityScore[] = {
        -10, -5, -2, 0, 2, 4, 6, 7, 8, 9, 10, 10, 10, 10
    };
    // scores for rook mobility, legal moves
    static constexpr inline int RookMobilityScore[] = {
        -8, -4, -2, 0, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8
    };
    // scores for queen mobility, legal moves
    static constexpr inline int QueenMobilityScore[] = {
        -6, -3, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10
    };

    static inline void InitAttks() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            /* Pawn moves and attacks */
            for (const int col : {0, 1}) {
                uint64_t attacks = 0ull;
                uint64_t moves = 0ull;

                const int step = col ? Files : -Files;
                const int homeRank = col ? 1 : (Ranks - 2);

                if (col ? (r < Ranks - 1) : (r > 0)) {
                    // left capture
                    if (f > 0) attacks |= IndexToMask(sq + step - 1);
                    // right capture
                    if (f < Files - 1) attacks |= IndexToMask(sq + step + 1);

                    // single push
                    moves |= IndexToMask(sq + step);
                    // double push, only from home rank
                    if (r == homeRank) moves |= IndexToMask(sq + step + step);
                }

                PawnAttacks[col][sq] = attacks;
                PawnMoves[col][sq] = moves | attacks;
            }

            /* King attacks */ {
                uint64_t attacks = 0ull;

                for (int dr = -1; dr <= 1; ++dr) {
                    for (int df = -1; df <= 1; ++df) {
                        if (dr == 0 && df == 0) continue;

                        const int nr = r + dr;
                        const int nf = f + df;

                        if (nr >= 0 && nr < Ranks && nf >= 0 && nf < Files) {
                            attacks |= IndexToMask(To2DIndex(nr, nf));
                        }
                    }
                }

                KingAttacks[sq] = attacks;
            }

            /* Knight attacks */ {
                constexpr int KnightOffsets[8][2] = {
                    {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                    {1, -2}, {1, 2}, {2, -1}, {2, 1}
                };

                uint64_t attacks = 0ull;

                for (auto [dr, df] : KnightOffsets) {
                    const int nr = r + dr;
                    const int nf = f + df;

                    if (nr >= 0 && nr < Ranks && nf >= 0 && nf < Files) {
                        attacks |= IndexToMask(To2DIndex(nr, nf));
                    }
                }

                KnightAttacks[sq] = attacks;
            }
        }
    }

    static inline void InitRayTable() {
        for (int a = 0; a < 64; ++a) {
            for (int b = 0; b < 64; ++b) {
                if (a == b) continue;

                const int ar = ToRank(a);
                const int af = ToFile(a);
                const int br = ToRank(b);
                const int bf = ToFile(b);

                const int dr = (br > ar) - (br < ar);
                const int df = (bf > af) - (bf < af);

                const bool aligned = (dr == 0) || (df == 0) || (std::abs(br - ar) == std::abs(bf - af));
                if (!aligned) continue;

                uint64_t ray = 0ull;
                int r = ar + dr;
                int f = af + df;

                while (r != br || f != bf) {
                    ray |= IndexToMask(To2DIndex(r, f));
                    r += dr;
                    f += df;
                }

                RayBetween[a][b] = ray;
            }
        }
    }

    static inline void InitPassedPawnTable() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            for (const int col : {0, 1}) {
                uint64_t mask = 0ull;

                const int step = col ? 1 : -1;

                for (int nr = r + step; nr >= 0 && nr < Ranks; nr += step) {
                    for (int df = -1; df <= 1; ++df) {
                        const int nf = f + df;
                        if (nf >= 0 && nf < Files) mask |= IndexToMask(To2DIndex(nr, nf));
                    }
                }

                PassedPawn[col][sq] = mask;
            }
        }
    }

    static inline void InitPawnShieldTable() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            uint64_t shield1W = 0ull;
            uint64_t shield2W = 0ull;
            uint64_t shield1B = 0ull;
            uint64_t shield2B = 0ull;

            for (int df = -1; df <= 1; ++df) {
                const int nf = f + df;
                if (nf < 0 || nf >= Files) continue;

                // white shields
                if (r - 1 >= 0) shield1W |= IndexToMask(To2DIndex(r - 1, nf));
                if (r - 2 >= 0) shield2W |= IndexToMask(To2DIndex(r - 2, nf));

                // black shields
                if (r + 1 < Ranks) shield1B |= IndexToMask(To2DIndex(r + 1, nf));
                if (r + 2 < Ranks) shield2B |= IndexToMask(To2DIndex(r + 2, nf));
            }

            PawnShield[0][sq] = shield1W;
            PawnShield2[0][sq] = shield2W;
            PawnShield[1][sq] = shield1B;
            PawnShield2[1][sq] = shield2B;
        }
    }

    static inline void InitLMRTable() {
        for (int p = 1; p < SearchContext::MaxPly; ++p) {
            for (int m = 1; m < SearchContext::MaxMoves; ++m) {
                Reduction[p][m] = std::max(0, int(0.5f + std::log(p) * std::log(m) * 0.5f));
            }
        }
    }

    static inline void Init() {
        InitAttks();
        InitRayTable();
        InitPassedPawnTable();
        InitPawnShieldTable();
        InitLMRTable();
    }
}

void Chess::Init() {
    ZobristHasher::Init();
    MagicBitboard::Init();
    LUTs::Init();

    GameContext::TT.SetSize(64);
}

void Chess::Board::NewGame() {
    GameContext::TT.Clear();
    GameContext::PawnTT.Clear();

    std::memset(SearchContext::HistoryTable, 0, sizeof(SearchContext::HistoryTable));
}

#pragma region Fen String

PieceColor Board::LoadFromFen(const std::string& fen) {
    const std::vector<std::string> tokens = Utils::Tokenize(fen);

    // validate token
    if (tokens.size() != 6) {
        std::cerr << "Invalid fen string" << std::endl;
        return PieceColor::White;
    }

    GameContext::BeginNew();

    // refer all tokens
    const std::string& pieceStr = tokens[0];
    const std::string& sideToMoveStr = tokens[1];
    const std::string& castlingRightsStr = tokens[2];
    const std::string& enPassantSquareStr = tokens[3];
    const std::string& halfMoveClockStr = tokens[4];
    const std::string& moveCountStr = tokens[5];

    /* Parse pieces */ {
        // reset
        std::memset(m_PieceMask, 0ull, sizeof(m_PieceMask));
        for (Piece& p : m_Mailbox) p.Type = PieceType::None;

        int idx = 0;

        for (const char c : pieceStr) {
            // safety check
            if (idx >= Ranks * Files) break;

            // line break
            if (c == '/') continue;

            // skip empty
            if (std::isdigit(c)) {
                idx += c - '0';
                continue;
            }

            const int lower_c = std::tolower(c);

            PieceType type;
            const bool isBlack = c == lower_c;
            const int colorOff = isBlack * 6;

            // get type
            switch (lower_c) {
            case 'p': type = PieceType::Pawn; break;
            case 'b': type = PieceType::Bishop; break;
            case 'n': type = PieceType::Knight; break;
            case 'q': type = PieceType::Queen; break;
            case 'r': type = PieceType::Rook; break;
            case 'k': type = PieceType::King; break;
            default: continue;
            }

            m_Mailbox[idx] = Piece(type, isBlack ? PieceColor::Black : PieceColor::White);
            m_PieceMask[static_cast<int>(type) + colorOff] |= IndexToMask(idx++);
        }
    }

    // Parse side to move
    const PieceColor sideToMove = sideToMoveStr[0] == 'w' ? PieceColor::White : PieceColor::Black;

    /* Parse castling */ {
        if (castlingRightsStr[0] == '-') {
            m_CastlingRights = 0;
        } else {
            m_CastlingRights = (
                (CastlingRights::WhiteKingSideBit * (castlingRightsStr.find('K') != std::string::npos)) |
                (CastlingRights::WhiteQueenSideBit * (castlingRightsStr.find('Q') != std::string::npos)) |
                (CastlingRights::BlackKingSideBit * (castlingRightsStr.find('k') != std::string::npos)) |
                (CastlingRights::BlackQueenSideBit * (castlingRightsStr.find('q') != std::string::npos))
            );
        }
    }

    /* Parse En-Passant square */ {
        if (enPassantSquareStr[0] == '-') {
            m_EnPassantSquare = NullPos;
        } else {
            m_EnPassantSquare = StringNotationToIndex(enPassantSquareStr[0], enPassantSquareStr[1]);
        }
    }

    // Parse half-move clock
    m_HalfMoveClock = std::stoi(halfMoveClockStr);

    // Parse full move count (number of times black has moved)
    GameContext::MoveCount = std::stoi(moveCountStr);

    m_Hash = ZobristHasher::GetHash(*this, sideToMove);
    m_PawnAndKingHash = ZobristHasher::GetPawnAndKingHash(*this);

    // push current position in history
    GameContext::GameHistory.push_back(m_Hash);

    return sideToMove;
}

std::string Chess::Board::GetFen(PieceColor sideToMove) const {
    const char sep = ' ';

    std::string fen;
    fen.reserve(80); // average length of a fen string

    /* Serialize pieces */ {
        for (int rank = 0; rank < Ranks; ++rank) {
            int empty = 0;

            for (int file = 0; file < Files; ++file) {
                const int idx = To2DIndex(rank, file);
                const Piece& p = m_Mailbox[idx];

                if (p.Type == PieceType::None) {
                    ++empty;
                    continue;
                }

                if (empty) {
                    fen.push_back('0' + empty);
                    empty = 0;
                }

                fen.push_back(PieceToChar(p));
            }

            if (empty) fen.push_back('0' + empty);
            if (rank != Ranks - 1) fen.push_back('/');
        }
    }

    // side to move
    fen.push_back(sep);
    fen.push_back(sideToMove == PieceColor::White ? 'w' : 'b');

    /* Serialize Castling rights */ {
        fen.push_back(sep);

        if (!m_CastlingRights) {
            fen.push_back('-');
        } else {
            if (m_CastlingRights & CastlingRights::WhiteKingSideBit) fen.push_back('K');
            if (m_CastlingRights & CastlingRights::WhiteQueenSideBit) fen.push_back('Q');
            if (m_CastlingRights & CastlingRights::BlackKingSideBit) fen.push_back('k');
            if (m_CastlingRights & CastlingRights::BlackQueenSideBit) fen.push_back('q');
        }
    }

    /* Serialize En-passant square */ {
        fen.push_back(sep);

        if (m_EnPassantSquare == NullPos) {
            fen.push_back('-');
        } else {
            fen.push_back('a' + ToFile(m_EnPassantSquare));
            fen.push_back('1' + (Ranks - 1 - ToRank(m_EnPassantSquare)));
        }
    }

    // half move clock
    fen.push_back(sep);
    fen += std::to_string(m_HalfMoveClock);

    // full move count
    fen.push_back(sep);
    fen += std::to_string(GameContext::MoveCount);

    return fen;
}

#pragma region Gameplay

bool Board::isUnderAttack(int position, PieceColor attacker) const {
    const bool isBlack = attacker == PieceColor::Black;
    const int atkOff = isBlack ? 6 : 0;

    const uint64_t occ = GetOccupancyMap();

    return (
        // pawn attacks
        (LUTs::PawnAttacks[!isBlack][position] & m_PieceMask[atkOff]) ||
        // knight attacks
        (LUTs::KnightAttacks[position] & m_PieceMask[atkOff + 2]) ||
        // king attacks
        (LUTs::KingAttacks[position] & m_PieceMask[atkOff + 5]) ||
        // bishop / queen attacks
        (MagicBitboard::DiagonalAttacks(position, occ) & (m_PieceMask[atkOff + 1] | m_PieceMask[atkOff + 4])) ||
        // rook / queen attacks
        (MagicBitboard::OrthogonalAttacks(position, occ) & (m_PieceMask[atkOff + 3] | m_PieceMask[atkOff + 4]))
    );
}

bool Board::isUnderCheck(PieceColor sideToMove) const {
    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, sideToMove)]);
    return isUnderAttack(kingSq, InvertColor(sideToMove));
}

#pragma region Forced Draw

bool Board::FiftyMoveRule() const noexcept {
    return m_HalfMoveClock >= 100;
}

bool Board::isThreefoldRepetition() const noexcept {
    int count = 1;

    for (int i = GameContext::GameHistory.size() - 2; i >= 0; --i) {
        if (GameContext::GameHistory[i] == m_Hash && ++count >= 3) return true;
    }

    return false;
}

bool Board::isThreefoldRepetition(int ply) const {
    int count = 1;

    // check within current search
    for (int i = ply - 2; i >= 0; i -= 2) {
        // intentionally used threshold of 2 here instead of 3 to avoid repetitions in search
        // with the value of 3, the engine sometimes did get stuck in a repetition loop
        if (SearchContext::PositionHistory[i] == m_Hash && ++count >= 2) return true;
    }

    // check game history
    return isThreefoldRepetition();
}

bool Board::HasInsufficientMaterial() const noexcept {
    // any pawns, rooks or queens = mating material always possible
    if (
        m_PieceMask[0] || m_PieceMask[6] ||
        m_PieceMask[3] || m_PieceMask[9] ||
        m_PieceMask[4] || m_PieceMask[10]
    ) {
        return false;
    }

    const uint64_t whiteBishops = m_PieceMask[1];
    const uint64_t blackBishops = m_PieceMask[7];
    const uint64_t whiteKnights = m_PieceMask[2];
    const uint64_t blackKnights = m_PieceMask[8];

    const int whiteBishopCount = NumBitsOn(whiteBishops);
    const int blackBishopCount = NumBitsOn(blackBishops);
    const int whiteKnightCount = NumBitsOn(whiteKnights);
    const int blackKnightCount = NumBitsOn(blackKnights);

    const int whiteMinors = whiteBishopCount + whiteKnightCount;
    const int blackMinors = blackBishopCount + blackKnightCount;

    // K vs K
    if (whiteMinors == 0 && blackMinors == 0) return true;

    // K+minor vs K
    if (whiteMinors == 1 && blackMinors == 0) return true;
    if (blackMinors == 1 && whiteMinors == 0) return true;

    // K+N+N vs K - cannot force checkmate
    if (whiteKnightCount == 2 && blackMinors == 0 && whiteBishopCount == 0) return true;
    if (blackKnightCount == 2 && whiteMinors == 0 && blackBishopCount == 0) return true;

    // K+N vs K+N - neither side has mating potential
    if (
        whiteKnightCount == 1 && blackKnightCount == 1 &&
        whiteBishopCount == 0 && blackBishopCount == 0
    ) {
        return true;
    }

    // K+B vs K+B - only drawn if bishops are on same color
    if (
        whiteBishopCount == 1 && blackBishopCount == 1 &&
        whiteKnightCount == 0 && blackKnightCount == 0
    ) {
        // light square = 1, dark square = 0
        // a square is light if (rank + file) is even
        const auto IsLightSquare = [](int sq) -> bool { return (ToRank(sq) + ToFile(sq)) % 2 == 0; };
        if (IsLightSquare(MaskToIndex(whiteBishops)) == IsLightSquare(MaskToIndex(blackBishops))) return true;
    }

    // K+B* vs K+B* where ALL bishops across both sides are on the same color
    // e.g. white has 2 light bishops, black has 1 light bishop - still a draw
    if (whiteKnightCount == 0 && blackKnightCount == 0 &&
        whiteBishopCount >= 1 && blackBishopCount >= 1) {
        // light squares = (rank + file) even, represented by this mask
        constexpr uint64_t LightSquares = 0x55AA55AA55AA55AAull;

        const bool whiteAllLight = (whiteBishops & ~LightSquares) == 0;
        const bool whiteAllDark = (whiteBishops & LightSquares) == 0;
        const bool blackAllLight = (blackBishops & ~LightSquares) == 0;
        const bool blackAllDark = (blackBishops & LightSquares) == 0;

        // all bishops on the board are on the same color square
        if ((whiteAllLight && blackAllLight) || (whiteAllDark && blackAllDark)) return true;
    }

    return false;
}

bool Board::HasAnyLegalMoves(PieceColor sideToMove) const noexcept {
    const bool isBlack = sideToMove == PieceColor::Black;
    const PieceColor enemyColor = InvertColor(sideToMove);

    const int myOff = isBlack ? 6 : 0;
    const int enmOff = myOff ^ 6;

    const uint64_t friendly = GetOccupancyMap(sideToMove);
    const uint64_t enemy = GetOccupancyMap(enemyColor);
    const uint64_t occ = friendly | enemy;

    const PinMasks pins = getPinMasks(sideToMove, friendly, occ);
    const uint64_t checkMask = getCheckMask(sideToMove, occ);

    /* king */ {
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);
        const uint64_t attacked = getAttackedSquares(enemyColor, occ);

        if (LUTs::KingAttacks[kingSq] & ~friendly & ~attacked) return true;
    }

    // only king moves are legal in double checks
    if (checkMask == 0ull) return false;

    /* pawns */ {
        const int pushDir = isBlack ? Files : -Files;
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

        for (uint64_t tmp = m_PieceMask[myOff]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            // captures
            if (LUTs::PawnAttacks[isBlack][sq] & enemy & allowed) return true;

            // en-passant
            if (m_EnPassantSquare != NullPos) {
                if (LUTs::PawnAttacks[isBlack][sq] & IndexToMask(m_EnPassantSquare)) {
                    const int capturedSq = m_EnPassantSquare - pushDir;
                    const uint64_t epMask = IndexToMask(m_EnPassantSquare) | IndexToMask(capturedSq);

                    // must either capture checker or block check
                    if ((epMask & checkMask) == 0ull) continue;

                    const uint64_t occAfterEP = occ & ~IndexToMask(sq) & ~IndexToMask(capturedSq);
                    const uint64_t enemyRQ = m_PieceMask[enmOff + 3] | m_PieceMask[enmOff + 4];

                    if (!(MagicBitboard::OrthogonalAttacks(kingSq, occAfterEP) & enemyRQ)) return true;
                }
            }

            // pushes: single square must be empty first
            if (!(occ & IndexToMask(sq + pushDir))) {
                if (IndexToMask(sq + pushDir) & allowed) return true;

                // double push
                const uint64_t rawDouble = LUTs::PawnMoves[isBlack][sq] & IndexToMask(sq + pushDir + pushDir);
                if (rawDouble && !(occ & rawDouble) && (rawDouble & allowed)) return true;
            }
        }
    }

    // knights
    for (uint64_t tmp = m_PieceMask[myOff + 2] & ~pins.Pinned; tmp; tmp &= tmp - 1) {
        if (LUTs::KnightAttacks[MaskToIndex(tmp)] & ~friendly & checkMask) return true;
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        if (MagicBitboard::DiagonalAttacks(sq, occ) & ~friendly & allowed) return true;
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        if (MagicBitboard::OrthogonalAttacks(sq, occ) & ~friendly & allowed) return true;
    }

    // queens
    for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        if (MagicBitboard::CombinedAttacks(sq, occ) & ~friendly & allowed) return true;
    }

    return false;
}

#pragma region Move

void Board::doMoveInternal(Move move) {
    const uint64_t fromMask = IndexToMask(move.StartingSquare);
    const uint64_t toMask = IndexToMask(move.TargetSquare);

    const Piece mover = m_Mailbox[move.StartingSquare];
    const bool moverIsBlack = mover.Color == PieceColor::Black;

    const int srcColorOff = moverIsBlack ? 6 : 0;
    const int srcIdx = static_cast<int>(mover.Type) + srcColorOff;

    const bool isCapture = HasFlag(move.Flag, MoveFlag::Capture);

    // XOR out old piece
    m_Hash = ZobristHasher::HashPiece(m_Hash, mover, move.StartingSquare);

    if (mover.Type == PieceType::Pawn || mover.Type == PieceType::King) {
        m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, mover, move.StartingSquare);
    }

    // XOR out captured piece
    if (isCapture && !HasFlag(move.Flag, MoveFlag::EnPassant)) {
        const Piece captured = m_Mailbox[move.TargetSquare];
        const int capIdx = Utils::PieceToBitIdx(captured.Type, captured.Color);

        m_PieceMask[capIdx] &= ~toMask;

        m_Hash = ZobristHasher::HashPiece(m_Hash, captured, move.TargetSquare);

        if (captured.Type == PieceType::Pawn) {
            m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, captured, move.TargetSquare);
        }
    }

    // flip side to move
    m_Hash = ZobristHasher::HashSideToMove(m_Hash);

    // remove old en-passant square from hash
    if (m_EnPassantSquare != NullPos) {
        m_Hash = ZobristHasher::HashEnPassantSquare(m_Hash, m_EnPassantSquare);
    }

    if (m_CastlingRights) {
        // to track whether castling rights have actually changed
        const uint8_t prevCastlingRights = m_CastlingRights;

        if (mover.Type == PieceType::King) {
            // king moved
            if (moverIsBlack) {
                m_CastlingRights &= ~(CastlingRights::BlackKingSideBit | CastlingRights::BlackQueenSideBit);
            } else {
                m_CastlingRights &= ~(CastlingRights::WhiteKingSideBit | CastlingRights::WhiteQueenSideBit);
            }
        } else if (mover.Type == PieceType::Rook) {
            // rook leaves its home square
            if (move.StartingSquare == 63) {
                m_CastlingRights &= ~CastlingRights::WhiteKingSideBit;
            } else if (move.StartingSquare == 56) {
                m_CastlingRights &= ~CastlingRights::WhiteQueenSideBit;
            } else if (move.StartingSquare == 7) {
                m_CastlingRights &= ~CastlingRights::BlackKingSideBit;
            } else if (move.StartingSquare == 0) {
                m_CastlingRights &= ~CastlingRights::BlackQueenSideBit;
            }
        }

        if (isCapture && !HasFlag(move.Flag, MoveFlag::EnPassant)) {
            // a rook on its home square gets captured
            if (move.TargetSquare == 63) {
                m_CastlingRights &= ~CastlingRights::WhiteKingSideBit;
            } else if (move.TargetSquare == 56) {
                m_CastlingRights &= ~CastlingRights::WhiteQueenSideBit;
            } else if (move.TargetSquare == 7) {
                m_CastlingRights &= ~CastlingRights::BlackKingSideBit;
            } else if (move.TargetSquare == 0) {
                m_CastlingRights &= ~CastlingRights::BlackQueenSideBit;
            }
        }

        if (m_CastlingRights != prevCastlingRights) {
            // XOR out last rights
            m_Hash = ZobristHasher::HashCastlingRights(m_Hash, prevCastlingRights);

            // XOR in new rights
            m_Hash = ZobristHasher::HashCastlingRights(m_Hash, m_CastlingRights);
        }
    }

    // remove piece from starting square
    m_PieceMask[srcIdx] &= ~fromMask;

    // move pieces in mailbox
    m_Mailbox[move.TargetSquare].Color = mover.Color; // type is set below after promotions
    m_Mailbox[move.StartingSquare].Type = PieceType::None;

    // reset en-passant square (will be set below if required)
    m_EnPassantSquare = NullPos;

    // 50-move rule
    if (
        mover.Type == PieceType::Pawn ||
        isCapture
    ) {
        m_HalfMoveClock = 0;
    } else {
        ++m_HalfMoveClock;
    }

    // handle move flags
    PieceType landingType = mover.Type;

    if (isCastle(move.Flag)) {
        const bool isKingside = HasFlag(move.Flag, MoveFlag::CastleKingSide);
        const int dir = isKingside ? 1 : -1;
        const int rookOffset = isKingside ? 3 : 4;
        const int rookFrom = move.StartingSquare + rookOffset * dir;
        const int rookTo = move.TargetSquare - dir;

        const int rookIdx = 3 + srcColorOff;
        const Piece rook(PieceType::Rook, mover.Color);

        m_Hash = ZobristHasher::HashPiece(m_Hash, rook, rookFrom);

        m_PieceMask[rookIdx] &= ~IndexToMask(rookFrom);
        m_PieceMask[rookIdx] |= IndexToMask(rookTo);

        m_Mailbox[rookTo] = rook;
        m_Mailbox[rookFrom].Type = PieceType::None;

        m_Hash = ZobristHasher::HashPiece(m_Hash, rook, rookTo);
    }

    else if (HasFlag(move.Flag, MoveFlag::PawnDoublePush)) {
        const int dir = moverIsBlack ? Files : -Files;

        m_EnPassantSquare = move.StartingSquare + dir;
        m_Hash = ZobristHasher::HashEnPassantSquare(m_Hash, m_EnPassantSquare);
    }

    else if (HasFlag(move.Flag, MoveFlag::EnPassant)) {
        const int dir = moverIsBlack ? Files : -Files;
        const Piece enemyPawn(PieceType::Pawn, InvertColor(mover.Color));
        const int enemyPawnIdx = (!moverIsBlack) * 6;
        const int capturedPos = move.TargetSquare - dir;

        m_Hash = ZobristHasher::HashPiece(m_Hash, enemyPawn, capturedPos);
        m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, enemyPawn, capturedPos);

        m_PieceMask[enemyPawnIdx] &= ~IndexToMask(capturedPos);
        m_Mailbox[capturedPos].Type = PieceType::None;
    }

    else if (isPromotion(move.Flag)) {
        if (HasFlag(move.Flag, MoveFlag::PromoteToQueen)) landingType = PieceType::Queen;
        else if (HasFlag(move.Flag, MoveFlag::PromoteToRook)) landingType = PieceType::Rook;
        else if (HasFlag(move.Flag, MoveFlag::PromoteToKnight)) landingType = PieceType::Knight;
        else landingType = PieceType::Bishop;
    }

    // set piece at new position
    m_PieceMask[static_cast<int>(landingType) + srcColorOff] |= toMask;
    m_Mailbox[move.TargetSquare].Type = landingType;

    const Piece lander = Piece(landingType, mover.Color);

    // XOR in the moved piece at its destination
    m_Hash = ZobristHasher::HashPiece(m_Hash, lander, move.TargetSquare);

    if (landingType == PieceType::Pawn || landingType == PieceType::King) {
        m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, lander, move.TargetSquare);
    }
}

void Board::DoMove(Move move) {
    doMoveInternal(move);

    GameContext::GameHistory.push_back(m_Hash);
    ++GameContext::MoveCount;
}

void Chess::Board::FlipSideToMove() {
    m_Hash = ZobristHasher::HashSideToMove(m_Hash);
}

#pragma region Move Generation

void Chess::Board::scanPins(int kingPos, uint64_t pinners, uint64_t friendlyOcc, PinMasks& result) const {
    for (uint64_t tmp = pinners; tmp; tmp &= tmp - 1) {
        const int attSq = MaskToIndex(tmp);
        const uint64_t between = LUTs::RayBetween[kingPos][attSq];
        const uint64_t blocker = between & friendlyOcc;

        if (NumBitsOn(blocker) != 1) continue;

        const int pinnedSq = MaskToIndex(blocker);

        result.Pinned |= IndexToMask(pinnedSq);
        result.Rays[pinnedSq] = between | IndexToMask(attSq);
    }
}

Board::PinMasks Board::getPinMasks(PieceColor friendlyColor, uint64_t friendlyOcc, uint64_t totalOcc) const {
    PinMasks result;

    const int enemyOff = friendlyColor == PieceColor::Black ? 0 : 6;

    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, friendlyColor)]);

    const uint64_t rookBlockers = MagicBitboard::OrthogonalAttacks(kingSq, totalOcc) & friendlyOcc;
    const uint64_t rookXray = MagicBitboard::OrthogonalAttacks(kingSq, totalOcc ^ rookBlockers);
    const uint64_t enemyRooksQueens = (
        m_PieceMask[3 + enemyOff] |
        m_PieceMask[4 + enemyOff]
    );

    scanPins(kingSq, rookXray & enemyRooksQueens, friendlyOcc, result);

    const uint64_t bishBlockers = MagicBitboard::DiagonalAttacks(kingSq, totalOcc) & friendlyOcc;
    const uint64_t bishXray = MagicBitboard::DiagonalAttacks(kingSq, totalOcc ^ bishBlockers);
    const uint64_t enemyBishopsQueens = (
        m_PieceMask[1 + enemyOff] |
        m_PieceMask[4 + enemyOff]
    );

    scanPins(kingSq, bishXray & enemyBishopsQueens, friendlyOcc, result);

    return result;
}

Board::PinMasks Board::getPinMasks(PieceColor friendlyColor) const {
    const uint64_t friendly = GetOccupancyMap(friendlyColor);
    const uint64_t occ = GetOccupancyMap();

    return getPinMasks(friendlyColor, friendly, occ);
}

uint64_t Board::getCheckMask(PieceColor friendlyColor, uint64_t occ) const {
    const int enemyOff = (friendlyColor == PieceColor::White) * 6;
    const bool isBlack = friendlyColor == PieceColor::Black;
    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, friendlyColor)]);

    uint64_t checkMask = 0ull;
    int checkerCount = 0;

    // sliders
    const uint64_t enemyQueens = m_PieceMask[enemyOff + 4];
    const uint64_t enemyRooksQueens = m_PieceMask[enemyOff + 3] | enemyQueens;
    const uint64_t enemyBishopsQueens = m_PieceMask[enemyOff + 1] | enemyQueens;

    for (uint64_t tmp = MagicBitboard::OrthogonalAttacks(kingSq, occ) & enemyRooksQueens; tmp; tmp &= tmp - 1) {
        const int attSq = MaskToIndex(tmp);
        checkMask |= LUTs::RayBetween[kingSq][attSq] | IndexToMask(attSq);
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    for (uint64_t tmp = MagicBitboard::DiagonalAttacks(kingSq, occ) & enemyBishopsQueens; tmp; tmp &= tmp - 1) {
        const int attSq = MaskToIndex(tmp);
        checkMask |= LUTs::RayBetween[kingSq][attSq] | IndexToMask(attSq);
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    // knights
    for (uint64_t tmp = LUTs::KnightAttacks[kingSq] & m_PieceMask[enemyOff + 2]; tmp; tmp &= tmp - 1) {
        checkMask |= IndexToMask(MaskToIndex(tmp));
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    // pawns
    for (uint64_t tmp = LUTs::PawnAttacks[isBlack][kingSq] & m_PieceMask[enemyOff]; tmp; tmp &= tmp - 1) {
        checkMask |= IndexToMask(MaskToIndex(tmp));
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    if (checkerCount == 0) return ~0ull;  // not in check, all squares valid

    return checkMask;
}

uint64_t Board::getCheckMask(PieceColor friendlyColor) const {
    return getCheckMask(friendlyColor, GetOccupancyMap());
}

uint64_t Board::getAttackedSquares(PieceColor attacker, uint64_t occ) const {
    uint64_t attacked = 0ull;

    const uint64_t occxking = occ & ~m_PieceMask[Utils::PieceToBitIdx(PieceType::King, InvertColor(attacker))];

    const int off = attacker == PieceColor::Black ? 6 : 0;
    const uint64_t pawns = m_PieceMask[off];

    // pawn
    if (attacker == PieceColor::White) {
        attacked |= ((pawns & ~0x0101010101010101ull) >> 9) | ((pawns & ~0x8080808080808080ull) >> 7);
    } else {
        attacked |= ((pawns & ~0x8080808080808080ull) << 9) | ((pawns & ~0x0101010101010101ull) << 7);
    }

    // knights
    for (uint64_t tmp = m_PieceMask[off + 2]; tmp; tmp &= tmp - 1) {
        attacked |= LUTs::KnightAttacks[MaskToIndex(tmp)];
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[off + 1]; tmp; tmp &= tmp - 1) {
        attacked |= MagicBitboard::DiagonalAttacks(MaskToIndex(tmp), occxking);
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[off + 3]; tmp; tmp &= tmp - 1) {
        attacked |= MagicBitboard::OrthogonalAttacks(MaskToIndex(tmp), occxking);
    }

    // queens
    for (uint64_t tmp = m_PieceMask[off + 4]; tmp; tmp &= tmp - 1) {
        attacked |= MagicBitboard::CombinedAttacks(MaskToIndex(tmp), occxking);
    }

    // king
    attacked |= LUTs::KingAttacks[MaskToIndex(m_PieceMask[off + 5])];

    return attacked;
}

uint64_t Board::getAttackedSquares(PieceColor attacker) const {
    return getAttackedSquares(attacker, GetOccupancyMap());
}

Board::CheckDetector Chess::Board::buildCheckDetector(bool attackerIsBlack, int enemyKingSq, uint64_t occ, uint64_t friendlyOcc) const {
    const int myOff = attackerIsBlack ? 6 : 0;

    CheckDetector cd;
    cd.KingSq = enemyKingSq;
    cd.KingMask = IndexToMask(enemyKingSq);

    // reverse lookup: squares from which our pawn/knight attacks their king
    cd.PawnChecks = LUTs::PawnAttacks[!attackerIsBlack][enemyKingSq];
    cd.KnightChecks = LUTs::KnightAttacks[enemyKingSq];

    // dc candidates: our pieces sitting on a xray line between one of our
    // sliders and the enemy king - moving them may reveal an attack
    const uint64_t ourBQ = m_PieceMask[myOff + 1] | m_PieceMask[myOff + 4];
    const uint64_t ourRQ = m_PieceMask[myOff + 3] | m_PieceMask[myOff + 4];

    uint64_t dc = 0ull;

    const uint64_t diagRays = MagicBitboard::DiagonalAttacks(enemyKingSq, occ);
    const uint64_t diagBlock = diagRays & friendlyOcc;
    if (diagBlock && (MagicBitboard::DiagonalAttacks(enemyKingSq, occ ^ diagBlock) & ourBQ)) dc |= diagBlock;

    const uint64_t orthRays = MagicBitboard::OrthogonalAttacks(enemyKingSq, occ);
    const uint64_t orthBlock = orthRays & friendlyOcc;
    if (orthBlock && (MagicBitboard::OrthogonalAttacks(enemyKingSq, occ ^ orthBlock) & ourRQ)) dc |= orthBlock;

    cd.DcCandidates = dc;
    return cd;
}

bool Board::moveGivesCheck(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd) const {
    const int myOff = isBlack ? 6 : 0;

    const uint64_t fromMask = IndexToMask(move.StartingSquare);
    const uint64_t toMask = IndexToMask(move.TargetSquare);

    PieceType landingType = m_Mailbox[move.StartingSquare].Type;
    if (isPromotion(move.Flag)) landingType = Utils::PromotionPiece(move.Flag);

    const bool isEnPassant = HasFlag(move.Flag, MoveFlag::EnPassant);

    const bool isDC = isEnPassant || (fromMask & cd.DcCandidates);

    // non-sliders: exact via precomputed table, 0 magic lookups
    if (landingType == PieceType::Pawn && (toMask & cd.PawnChecks)) return true;
    if (landingType == PieceType::Knight && (toMask & cd.KnightChecks)) return true;

    // non-slider with no dc potential - done, no magic lookups at all
    if ((landingType == PieceType::Pawn || landingType == PieceType::Knight) && !isDC) return false;

    // build new occupancy once - shared by direct slider check + dc check
    uint64_t newOcc = (occ & ~fromMask) | toMask;

    if (isEnPassant) {
        const int pushDir = isBlack ? Files : -Files;
        newOcc &= ~IndexToMask(move.TargetSquare - pushDir);
    }

    // slider direct check - handles all edge cases (piece blocking its own ray)
    // because we use newOcc which correctly reflects the move
    if (landingType == PieceType::Bishop || landingType == PieceType::Queen)
        if (MagicBitboard::DiagonalAttacks(move.TargetSquare, newOcc) & cd.KingMask) return true;

    if (landingType == PieceType::Rook || landingType == PieceType::Queen)
        if (MagicBitboard::OrthogonalAttacks(move.TargetSquare, newOcc) & cd.KingMask) return true;

    // castle: rook slides to new square, check that
    if (landingType == PieceType::King && isCastle(move.Flag)) {
        const bool isKingside = HasFlag(move.Flag, MoveFlag::CastleKingSide);
        const int rookFromSq = isKingside ? move.StartingSquare + 3 : move.StartingSquare - 4;
        const int rookToSq = isKingside ? move.TargetSquare - 1 : move.TargetSquare + 1;
        const uint64_t cOcc = (newOcc & ~IndexToMask(rookFromSq)) | IndexToMask(rookToSq);
        if (MagicBitboard::OrthogonalAttacks(rookToSq, cOcc) & cd.KingMask) return true;
    }

    // discovered check - only paid for dc candidates (~rare)
    if (isDC) {
        const uint64_t myBQ = (m_PieceMask[myOff + 1] | m_PieceMask[myOff + 4]) & ~fromMask;
        const uint64_t myRQ = (m_PieceMask[myOff + 3] | m_PieceMask[myOff + 4]) & ~fromMask;
        if (MagicBitboard::DiagonalAttacks(cd.KingSq, newOcc) & myBQ) return true;
        if (MagicBitboard::OrthogonalAttacks(cd.KingSq, newOcc) & myRQ) return true;
    }

    return false;
}

void Board::pushMove(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd, Move* out, unsigned int& moveCount) const {
    if (moveGivesCheck(move, isBlack, occ, cd)) {
        move.Flag = AddFlag(move.Flag, MoveFlag::Check);
    }

    out[moveCount++] = move;
}

void Board::pushPromotions(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd, Move* out, unsigned int& moveCount) const {
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToQueen)), isBlack, occ, cd, out, moveCount);
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToRook)), isBlack, occ, cd, out, moveCount);
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToKnight)), isBlack, occ, cd, out, moveCount);
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToBishop)), isBlack, occ, cd, out, moveCount);
}

void Board::generateMoves(
    PieceColor sideToMove, const PinMasks& pins,
    uint64_t checkMask, uint64_t attacked, uint64_t friendlyOcc,
    uint64_t enemyOcc, uint64_t occ,
    int filterIdx,
    Move* out, unsigned int& moveCount
) const {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack ? 6 : 0;
    const int enmOff = myOff ^ 6;

    const int enemyKingSq = MaskToIndex(m_PieceMask[enmOff + 5]);
    const CheckDetector cd = buildCheckDetector(isBlack, enemyKingSq, occ, friendlyOcc);

    static const auto ShouldProcess = [](int sq, int filterIdx) -> bool { return filterIdx < 0 || sq == filterIdx; };

    /* king */ {
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

        if (ShouldProcess(kingSq, filterIdx)) {
            for (uint64_t tmp = LUTs::KingAttacks[kingSq] & ~friendlyOcc & ~attacked; tmp; tmp &= tmp - 1) {
                const int to = MaskToIndex(tmp);
                pushMove(Move(kingSq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
            }

            if (checkMask == ~0ull) {
                const int ksRight = isBlack ? CastlingRights::BlackKingSideBit : CastlingRights::WhiteKingSideBit;
                const int qsRight = isBlack ? CastlingRights::BlackQueenSideBit : CastlingRights::WhiteQueenSideBit;

                // kingside
                if (
                    // has right to castle
                    (m_CastlingRights & ksRight) &&
                    // f and g are empty
                    !(occ & (IndexToMask(kingSq + 1) | IndexToMask(kingSq + 2))) &&
                    // f and g aren't under attack
                    !(attacked & (IndexToMask(kingSq + 1) | IndexToMask(kingSq + 2)))
                ) {
                    pushMove(Move(kingSq, kingSq + 2, MoveFlag::CastleKingSide), isBlack, occ, cd, out, moveCount);
                }

                // queenside
                if (
                    // has right to castle
                    (m_CastlingRights & qsRight) &&
                    // b, c and d are empty
                    !(occ & (IndexToMask(kingSq - 1) | IndexToMask(kingSq - 2) | IndexToMask(kingSq - 3))) &&
                    // c and d aren't under attack
                    !(attacked & (IndexToMask(kingSq - 1) | IndexToMask(kingSq - 2)))
                ) {
                    pushMove(Move(kingSq, kingSq - 2, MoveFlag::CastleQueenSide), isBlack, occ, cd, out, moveCount);
                }
            }
        }
    }

    // in double check, only king moves are legal
    if (checkMask == 0ull) return;

    /* pawns */ {
        const int pushDir = isBlack ? Files : -Files;
        const int promoRankMin = isBlack ? 56 : 0;
        const int promoRankMax = isBlack ? 63 : 7;
        const uint64_t homeRank = isBlack ? 0x000000000000FF00ull : 0x00FF000000000000ull;

        const auto IsPromoSq = [&](int sq) { return sq >= promoRankMin && sq <= promoRankMax; };

        for (uint64_t tmp = m_PieceMask[myOff]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            if (!ShouldProcess(sq, filterIdx)) continue;

            const uint64_t sqMask = IndexToMask(sq);
            const uint64_t allowed = ((pins.Pinned & sqMask) ? pins.Rays[sq] : ~0ull) & checkMask;
            const uint64_t diag = LUTs::PawnAttacks[isBlack][sq];

            // captures
            for (uint64_t cap = diag & allowed & enemyOcc; cap; cap &= cap - 1) {
                const int to = MaskToIndex(cap);

                if (IsPromoSq(to)) pushPromotions(Move(sq, to, MoveFlag::Capture), isBlack, occ, cd, out, moveCount);
                else pushMove(Move(sq, to, MoveFlag::Capture), isBlack, occ, cd, out, moveCount);
            }

            // en-passant
            if (m_EnPassantSquare != NullPos) {
                if (diag & IndexToMask(m_EnPassantSquare)) {
                    const int capturedSq = m_EnPassantSquare - pushDir;
                    const uint64_t epMask = IndexToMask(m_EnPassantSquare) | IndexToMask(capturedSq);

                    // must capture checker or block check
                    if ((epMask & checkMask) == 0ull) continue;

                    // pin restriction only
                    if ((pins.Pinned & sqMask) && !(pins.Rays[sq] & IndexToMask(m_EnPassantSquare))) continue;

                    const uint64_t occAfterEP = (occ & ~sqMask & ~IndexToMask(capturedSq)) | IndexToMask(m_EnPassantSquare);
                    const uint64_t enemyRQ = m_PieceMask[enmOff + 3] | m_PieceMask[enmOff + 4];

                    const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

                    // EP horizontal discovered check test
                    if (!(MagicBitboard::OrthogonalAttacks(kingSq, occAfterEP) & enemyRQ)) {
                        pushMove(Move(sq, m_EnPassantSquare, AddFlag(MoveFlag::EnPassant, MoveFlag::Capture)), isBlack, occ, cd, out, moveCount);
                    }
                }
            }

            // single push
            const int to1 = sq + pushDir;
            const uint64_t to1mask = IndexToMask(to1);

            if (!(occ & to1mask)) {
                if (to1mask & allowed) {
                    if (IsPromoSq(to1)) pushPromotions(Move(sq, to1, MoveFlag::None), isBlack, occ, cd, out, moveCount);
                    else pushMove(Move(sq, to1, MoveFlag::None), isBlack, occ, cd, out, moveCount);
                }

                // double push from home rank
                if (sqMask & homeRank) {
                    const int to2 = to1 + pushDir;
                    const uint64_t to2mask = IndexToMask(to2);

                    if (!(occ & to2mask) && (to2mask & allowed)) {
                        pushMove(Move(sq, to2, MoveFlag::PawnDoublePush), isBlack, occ, cd, out, moveCount);
                    }
                }
            }
        }
    }

    // knights
    for (uint64_t tmp = m_PieceMask[myOff + 2] & ~pins.Pinned; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        for (uint64_t mv = LUTs::KnightAttacks[sq] & ~friendlyOcc & checkMask; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        for (uint64_t mv = MagicBitboard::DiagonalAttacks(sq, occ) & ~friendlyOcc & allowed; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        for (uint64_t mv = MagicBitboard::OrthogonalAttacks(sq, occ) & ~friendlyOcc & allowed; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }

    // queens
    for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        for (uint64_t mv = MagicBitboard::CombinedAttacks(sq, occ) & ~friendlyOcc & allowed; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }
}

void Board::getAllLegalMoves(PieceColor sideToMove, Move* out, unsigned int& moveCount) const {
    const uint64_t friendly = GetOccupancyMap(sideToMove);
    const uint64_t enemy = GetOccupancyMap(InvertColor(sideToMove));
    const uint64_t occ = friendly | enemy;

    const PinMasks pins = getPinMasks(sideToMove, friendly, occ);
    const uint64_t checkMask = getCheckMask(sideToMove, occ);
    const uint64_t attacked = getAttackedSquares(InvertColor(sideToMove), occ);

    generateMoves(sideToMove, pins, checkMask, attacked, friendly, enemy, occ, -1, out, moveCount);
}

void Chess::Board::getAllCaptureMoves(PieceColor sideToMove, Move* out, unsigned int& moveCount) const {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack ? 6 : 0;
    const int enmOff = myOff ^ 6;

    const uint64_t friendly = GetOccupancyMap(sideToMove);
    const uint64_t enemy = GetOccupancyMap(InvertColor(sideToMove));
    const uint64_t occ = friendly | enemy;

    const PinMasks pins = getPinMasks(sideToMove, friendly, occ);
    const uint64_t checkMask = getCheckMask(sideToMove, occ);
    const uint64_t attacked = getAttackedSquares(InvertColor(sideToMove), occ);

    /* king captures */ {
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

        for (uint64_t tmp = LUTs::KingAttacks[kingSq] & enemy & ~attacked; tmp; tmp &= tmp - 1) {
            out[moveCount++] = Move(kingSq, MaskToIndex(tmp), MoveFlag::Capture);
        }
    }

    // double check - only king moves are legal
    if (checkMask == 0ull) return;

    /* pawn captures + en-passant + promotion captures */ {
        const int pushDir = isBlack ? Files : -Files;
        const int promoRankMin = isBlack ? 56 : 0;
        const int promoRankMax = isBlack ? 63 : 7;

        const auto IsPromoSq = [&](int sq) { return sq >= promoRankMin && sq <= promoRankMax; };

        for (uint64_t tmp = m_PieceMask[myOff]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t sqMask = IndexToMask(sq);
            const uint64_t allowed = ((pins.Pinned & sqMask) ? pins.Rays[sq] : ~0ull) & checkMask;
            const uint64_t diag = LUTs::PawnAttacks[isBlack][sq];

            // captures
            for (uint64_t cap = diag & allowed & enemy; cap; cap &= cap - 1) {
                const int to = MaskToIndex(cap);

                if (IsPromoSq(to)) {
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToQueen));
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToRook));
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToKnight));
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToBishop));
                } else {
                    out[moveCount++] = Move(sq, to, MoveFlag::Capture);
                }
            }

            // en-passant
            if (m_EnPassantSquare != NullPos && (diag & IndexToMask(m_EnPassantSquare))) {
                const int capturedSq = m_EnPassantSquare - pushDir;
                const uint64_t epMask = IndexToMask(m_EnPassantSquare) | IndexToMask(capturedSq);

                // must either capture the checker or block the check ray
                if ((epMask & checkMask) == 0ull) continue;

                // respect pin ray
                if ((pins.Pinned & sqMask) && !(pins.Rays[sq] & IndexToMask(m_EnPassantSquare))) continue;

                // horizontal discovered check test - two pawns leave the rank at once
                const uint64_t occAfterEP = occ & ~sqMask & ~IndexToMask(capturedSq);
                const uint64_t enemyRQ = m_PieceMask[enmOff + 3] | m_PieceMask[enmOff + 4];
                const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

                if (!(MagicBitboard::OrthogonalAttacks(kingSq, occAfterEP) & enemyRQ)) {
                    out[moveCount++] = Move(sq, m_EnPassantSquare, AddFlag(MoveFlag::EnPassant, MoveFlag::Capture));
                }
            }
        }
    }

    /* knight captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 2] & ~pins.Pinned; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);

            for (uint64_t mv = LUTs::KnightAttacks[sq] & enemy & checkMask; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }

    /* bishop captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            for (uint64_t mv = MagicBitboard::DiagonalAttacks(sq, occ) & enemy & allowed; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }

    /* rook captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            for (uint64_t mv = MagicBitboard::OrthogonalAttacks(sq, occ) & enemy & allowed; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }

    /* queen captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            for (uint64_t mv = MagicBitboard::CombinedAttacks(sq, occ) & enemy & allowed; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }
}

std::vector<Move> Board::GetLegalMoves(int idx) const {
    const Piece piece = m_Mailbox[idx];
    if (piece.Type == PieceType::None) return {};

    const PieceColor enemy = InvertColor(piece.Color);
    const uint64_t friendly = GetOccupancyMap(piece.Color);
    const uint64_t enemyOcc = GetOccupancyMap(enemy);
    const uint64_t occ = friendly | enemyOcc;

    const PinMasks pins = getPinMasks(piece.Color, friendly, occ);
    const uint64_t checkMask = getCheckMask(piece.Color, occ);
    const uint64_t attacked = getAttackedSquares(enemy, occ);

    Move moves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    generateMoves(piece.Color, pins, checkMask, attacked, friendly, enemyOcc, occ, idx, moves, numMoves);

    return std::vector<Move>(moves, moves + numMoves);
}

#pragma region Occupancy

uint64_t Board::GetOccupancyMap(PieceColor color) const noexcept {
    const int colOffset = static_cast<int>(color) * 6;

    return (
        m_PieceMask[colOffset] | m_PieceMask[colOffset + 1] |
        m_PieceMask[colOffset + 2] | m_PieceMask[colOffset + 3] |
        m_PieceMask[colOffset + 4] | m_PieceMask[colOffset + 5]
    );
}

uint64_t Board::GetOccupancyMap() const noexcept {
    return (
        m_PieceMask[0] | m_PieceMask[1] |
        m_PieceMask[2] | m_PieceMask[3] |
        m_PieceMask[4] | m_PieceMask[5] |
        m_PieceMask[6] | m_PieceMask[7] |
        m_PieceMask[8] | m_PieceMask[9] |
        m_PieceMask[10] | m_PieceMask[11]
    );
}

void Chess::Board::SetContempt(int contempt) const {
    Options::Contempt = contempt;
}

void Chess::Board::SetHashSize(unsigned int size) const {
    GameContext::TT.SetSize(size);
    GameContext::PawnTT.SetSize(std::max<int>(1, size / 64));
}

void Chess::Board::ClearHash() const {
    GameContext::TT.Clear();
    GameContext::PawnTT.Clear();
}

unsigned int Chess::Board::GetHashSize() const noexcept {
    return GameContext::TT.GetSize();
}

#pragma region PST

namespace PieceSquareTables {
    static constexpr int Table[][Ranks * Files] = {
        // pawn midgame: center control, discourage edge pawns
        {
            0, 0, 0, 0, 0, 0, 0, 0,
            50, 50, 50, 50, 50, 50, 50, 50,
            10, 10, 20, 30, 30, 20, 10, 10,
            5, 5, 10, 25, 25, 10, 5, 5,
            0, 0, 0, 20, 20, 0, 0, 0,
            5, -5, -10, 0, 0, -10, -5, 5,
            5, 10, 10, -20, -20, 10, 10, 5,
            0, 0, 0, 0, 0, 0, 0, 0,
        },

        // bishop midgame: diagonals, avoid edges and corners
        {
            -20, -10, -10, -10, -10, -10, -10, -20,
            -10, 0, 0, 0, 0, 0, 0, -10,
            -10, 0, 5, 10, 10, 5, 0, -10,
            -10, 5, 5, 10, 10, 5, 5, -10,
            -10, 0, 10, 10, 10, 10, 0, -10,
            -10, 10, 10, 10, 10, 10, 10, -10,
            -10, 5, 0, 0, 0, 0, 5, -10,
            -20, -10, -10, -10, -10, -10, -10, -20,
        },

        // knight midgame: strong center, terrible on edges
        {
            -50, -40, -30, -30, -30, -30, -40, -50,
            -40, -20, 0, 0, 0, 0, -20, -40,
            -30, 0, 10, 15, 15, 10, 0, -30,
            -30, 5, 15, 20, 20, 15, 5, -30,
            -30, 0, 15, 20, 20, 15, 0, -30,
            -30, 5, 10, 15, 15, 10, 5, -30,
            -40, -20, 0, 5, 5, 0, -20, -40,
            -50, -40, -30, -30, -30, -30, -40, -50,
        },

        // rook midgame: 7th rank, open files
        {
            0, 0, 0, 0, 0, 0, 0, 0,
            5, 10, 10, 10, 10, 10, 10, 5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            0, 0, 0, 5, 5, 0, 0, 0,
        },

        // queen midgame: slight center preference, don't rush out
        {
            -20, -10, -10, -5, -5, -10, -10, -20,
            -10, 0, 0, 0, 0, 0, 0, -10,
            -10, 0, 5, 5, 5, 5, 0, -10,
            -5, 0, 5, 5, 5, 5, 0, -5,
            0, 0, 5, 5, 5, 5, 0, -5,
            -10, 5, 5, 5, 5, 5, 0, -10,
            -10, 0, 5, 0, 0, 0, 0, -10,
            -20, -10, -10, -5, -5, -10, -10, -20,
        },

        // king midgame: stay castled, avoid center
        {
            -30, -40, -40, -50, -50, -40, -40, -30,
            -30, -40, -40, -50, -50, -40, -40, -30,
            -30, -40, -40, -50, -50, -40, -40, -30,
            -30, -40, -40, -50, -50, -40, -40, -30,
            -20, -30, -30, -40, -40, -30, -30, -20,
            -10, -20, -20, -20, -20, -20, -20, -10,
            20, 20, 0, 0, 0, 0, 20, 20,
            20, 30, 10, 0, 0, 10, 30, 20,
        },

        // pawn endgame: advance to promote, very strong rank bonuses
        {
            0, 0, 0, 0, 0, 0, 0, 0,
            80, 80, 80, 80, 80, 80, 80, 80,
            50, 50, 50, 50, 50, 50, 50, 50,
            30, 30, 30, 30, 30, 30, 30, 30,
            20, 20, 20, 20, 20, 20, 20, 20,
            10, 10, 10, 10, 10, 10, 10, 10,
            5, 5, 5, 5, 5, 5, 5, 5,
            0, 0, 0, 0, 0, 0, 0, 0,
        },

        // bishop endgame: long diagonals matter even more
        {
            -20, -10, -10, -10, -10, -10, -10, -20,
            -10, -5, 0, 0, 0, 0, -5, -10,
            -10, 0, 5, 10, 10, 5, 0, -10,
            -10, 0, 10, 15, 15, 10, 0, -10,
            -10, 0, 10, 15, 15, 10, 0, -10,
            -10, 0, 5, 10, 10, 5, 0, -10,
            -10, -5, 0, 0, 0, 0, -5, -10,
            -20, -10, -10, -10, -10, -10, -10, -20,
        },

        // knight endgame: centralize hard, edges are death
        {
            -50, -40, -30, -30, -30, -30, -40, -50,
            -40, -20, 0, 0, 0, 0, -20, -40,
            -30, 0, 10, 15, 15, 10, 0, -30,
            -30, 0, 15, 25, 25, 15, 0, -30,
            -30, 0, 15, 25, 25, 15, 0, -30,
            -30, 0, 10, 15, 15, 10, 0, -30,
            -40, -20, 0, 0, 0, 0, -20, -40,
            -50, -40, -30, -30, -30, -30, -40, -50,
        },

        // rook endgame: 7th rank critical, support passed pawns
        {
            0, 0, 0, 5, 5, 0, 0, 0,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            5, 10, 10, 10, 10, 10, 10, 5,
            0, 0, 0, 0, 0, 0, 0, 0,
        },

        // queen endgame: more central, actively hunt
        {
            -20, -10, -10, -5, -5, -10, -10, -20,
            -10, 0, 5, 0, 0, 0, 0, -10,
            -10, 5, 5, 5, 5, 5, 0, -10,
            0, 0, 5, 5, 5, 5, 0, -5,
            -5, 0, 5, 5, 5, 5, 0, -5,
            -10, 0, 5, 5, 5, 5, 0, -10,
            -10, 0, 0, 0, 0, 0, 0, -10,
            -20, -10, -10, -5, -5, -10, -10, -20,
        },

        // king endgame: centralize, avoid edges
        {
            -50, -40, -30, -20, -20, -30, -40, -50,
            -30, -20, -10, 0, 0, -10, -20, -30,
            -30, -10, 20, 30, 30, 20, -10, -30,
            -30, -10, 30, 40, 40, 30, -10, -30,
            -30, -10, 30, 40, 40, 30, -10, -30,
            -30, -10, 20, 30, 30, 20, -10, -30,
            -30, -30, 0, 0, 0, 0, -30, -30,
            -50, -30, -30, -30, -30, -30, -30, -50,
        },
    };

    [[nodiscard]]
    constexpr inline int GetPSTScore(int pieceTypeIdx, int sq, bool isBlack, float endgameWeight) noexcept {
        if (isBlack) sq = Utils::Mirror(sq);

        const int mid = Table[pieceTypeIdx][sq];
        const int end = Table[pieceTypeIdx + 6][sq];

        return Utils::IntLerp(mid, end, endgameWeight);
    }
}

#pragma region Evaluation

float Board::getEndgameWeight() const {
    // using weighted phase rather than linear as trading off queens or rooks is more
    // "endgameish" than bishops or knights

    constexpr int QueenWeight = 4;
    constexpr int RookWeight = 2;
    constexpr int MinorWeight = 1; // knights + bishops

    constexpr int MaxPhase = 2 * QueenWeight + 4 * RookWeight + 8 * MinorWeight;

    const int phase = (
        NumBitsOn(m_PieceMask[4] | m_PieceMask[10]) * QueenWeight +
        NumBitsOn(m_PieceMask[3] | m_PieceMask[9]) * RookWeight +
        NumBitsOn(m_PieceMask[1] | m_PieceMask[7] | m_PieceMask[2] | m_PieceMask[8]) * MinorWeight
    );

    return 1.f - std::clamp(static_cast<float>(phase) / MaxPhase, 0.f, 1.f);
}

#pragma region King Corner

int Board::evalForceKingToCorner(PieceColor sideToMove) const noexcept {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int oppKing = MaskToIndex(m_PieceMask[isBlack ? 5 : 11]);

    const int oppRank = ToRank(oppKing);
    const int oppFile = ToFile(oppKing);

    // push opponent king toward edge, proximity toward king
    const int myKing = MaskToIndex(m_PieceMask[isBlack ? 11 : 5]);
    const int kingDist = std::abs(ToRank(oppKing) - ToRank(myKing)) + std::abs(ToFile(oppKing) - ToFile(myKing));

    return (
        (std::max(3 - oppRank, oppRank - 4) + std::max(3 - oppFile, oppFile - 4)) * 10 +
        (14 - kingDist) * 5
    );
}

#pragma region King Danger

int Board::evalKingDanger(PieceColor sideToMove, uint64_t occ) const noexcept {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack ? 6 : 0;
    const int oppOff = myOff ^ 6;

    const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);
    const uint64_t kingZone = LUTs::KingAttacks[kingSq];

    int attackWeight = 0;
    int attackerCount = 0;

    // knights attacking king zone
    for (uint64_t tmp = m_PieceMask[oppOff + 2]; tmp; tmp &= tmp - 1) {
        if (LUTs::KnightAttacks[MaskToIndex(tmp)] & kingZone) {
            attackWeight += 20;
            ++attackerCount;
        }
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[oppOff + 1]; tmp; tmp &= tmp - 1) {
        const uint64_t atk = MagicBitboard::DiagonalAttacks(MaskToIndex(tmp), occ);
        if (atk & kingZone) {
            attackWeight += 20;
            ++attackerCount;
        }
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[oppOff + 3]; tmp; tmp &= tmp - 1) {
        const uint64_t atk = MagicBitboard::OrthogonalAttacks(MaskToIndex(tmp), occ);
        if (atk & kingZone) {
            attackWeight += 40;
            ++attackerCount;
        }
    }

    // queens
    for (uint64_t tmp = m_PieceMask[oppOff + 4]; tmp; tmp &= tmp - 1) {
        const uint64_t atk = MagicBitboard::CombinedAttacks(MaskToIndex(tmp), occ);
        if (atk & kingZone) {
            attackWeight += 80;
            ++attackerCount;
        }
    }

    // scale the score non-linearly with a cap of max 8 attackers
    static constexpr int DangerScale[] = {0, 0, 50, 75, 88, 94, 97, 99, 100};
    const int scale = DangerScale[std::min(attackerCount, 8)];

    return (attackWeight * scale) / 100;
}

#pragma region Pawn Shield

int Board::evalPawnShield(PieceColor sideToMove) const noexcept {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack * 6;

    const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);
    const uint64_t myPawns = m_PieceMask[myOff];

    return (
        // each missing pawn in the immediate shield costs 20
        -NumBitsOn(LUTs::PawnShield[isBlack][kingSq] & ~myPawns) * 20
        // each missing pawn on the second rank costs 10
        - NumBitsOn(LUTs::PawnShield2[isBlack][kingSq] & ~myPawns) * 10
    );
}

#pragma region Pawn Structure

int Board::evalOneSidePawnStructure(
    uint64_t pawns, uint64_t enemyPawns, uint64_t allPieces,
    int myKingSq, int oppKingSq,
    bool pawnIsBlack, float endgameWeight
) const noexcept {
    int score = 0;

    int fileCounts[Files] = {};
    int passedOnFile[Files] = {};

    // passed pawn bonus
    for (uint64_t tmp = pawns; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const int f = ToFile(sq);

        ++fileCounts[f];

        if (!(LUTs::PassedPawn[pawnIsBlack][sq] & enemyPawns)) {
            ++passedOnFile[f];

            const int rank = ToRank(sq);
            const int advancement = pawnIsBlack ? rank : (Ranks - 1 - rank);

            // reduce bonus if any piece is blocking the promotion path
            const uint64_t frontSpan = LUTs::PassedPawn[pawnIsBlack][sq] & Utils::FileMask(f);
            const float blockadeFactor = (frontSpan & allPieces) ? 0.4f : 1.0f;

            score += static_cast<int>(5 * advancement * advancement * (1.f + endgameWeight) * blockadeFactor);

            // reward opponent king being far
            const int defenderDist = std::abs(ToRank(oppKingSq) - ToRank(sq)) + std::abs(ToFile(oppKingSq) - ToFile(sq));
            score += static_cast<int>((defenderDist - 3) * 4 * endgameWeight);
        }
    }

    // isolated pawn penalty
    for (int f = 0; f < Files; ++f) {
        if (!fileCounts[f]) continue;

        const bool hasLeft = (f > 0) && fileCounts[f - 1];
        const bool hasRight = (f < Files - 1) && fileCounts[f + 1];

        if (!hasLeft && !hasRight) score -= 20;
    }

    // doubled pawn penalty
    for (int f = 0; f < Files; ++f) {
        const int count = fileCounts[f];
        if (count < 2) continue;

        const int doubled = count - 1;
        const int passedDoubled = std::min(doubled, passedOnFile[f]);
        const int normalDoubled = doubled - passedDoubled;

        score -= normalDoubled * 15 + passedDoubled * 7;
    }

    // connected passed pawn bonus
    for (int f = 0; f < Files; ++f) {
        if (!passedOnFile[f]) continue;
        const bool leftPassed = (f > 0) && passedOnFile[f - 1];
        const bool rightPassed = (f < Files - 1) && passedOnFile[f + 1];

        if (leftPassed || rightPassed) {
            score += static_cast<int>(30 * endgameWeight);
        }
    }

    return score;
}

int Board::evalPawnStructure(PieceColor sideToMove, uint64_t occ, float endgameWeight) const noexcept {
    if (const auto* e = GameContext::PawnTT.Probe(m_PawnAndKingHash)) {
        return sideToMove == PieceColor::White ? e->Score : -e->Score;
    }

    const uint64_t whitePawns = m_PieceMask[0];
    const uint64_t blackPawns = m_PieceMask[6];

    const int whiteKingSq = MaskToIndex(m_PieceMask[5]);
    const int blackKingSq = MaskToIndex(m_PieceMask[11]);

    const int score = (
        evalOneSidePawnStructure(whitePawns, blackPawns, occ, whiteKingSq, blackKingSq, false, endgameWeight) -
        evalOneSidePawnStructure(blackPawns, whitePawns, occ, blackKingSq, whiteKingSq, true, endgameWeight)
    );

    GameContext::PawnTT.Store(m_PawnAndKingHash, score);

    return sideToMove == PieceColor::White ? score : -score;
}

#pragma region Mobility

int Board::evalMobility(PieceColor sideToMove, uint64_t myOcc, uint64_t enemyOcc, uint64_t occ, float endgameWeight) const noexcept {
    const bool isBlack = sideToMove == PieceColor::Black;

    const int myOff = isBlack ? 6 : 0;
    const int oppOff = myOff ^ 6;

    // knight fork targets
    const uint64_t myForkTargets = m_PieceMask[oppOff + 3] | m_PieceMask[oppOff + 4] | m_PieceMask[oppOff + 5];
    const uint64_t oppForkTargets = m_PieceMask[myOff + 3] | m_PieceMask[myOff + 4] | m_PieceMask[myOff + 5];

    int score = 0;

    const uint64_t notMy = ~myOcc;

    // bishops
    for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
        const int moves = NumBitsOn(MagicBitboard::DiagonalAttacks(MaskToIndex(tmp), occ) & notMy);
        score += LUTs::BishopMobilityScore[moves];
    }

    // knights
    for (uint64_t tmp = m_PieceMask[myOff + 2]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);

        const int moves = NumBitsOn(LUTs::KnightAttacks[sq] & notMy);
        score += LUTs::KnightMobilityScore[moves];

        if (NumBitsOn(LUTs::KnightAttacks[sq] & myForkTargets) >= 2) score += 40;
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
        const int moves = NumBitsOn(MagicBitboard::OrthogonalAttacks(MaskToIndex(tmp), occ) & notMy);
        score += LUTs::RookMobilityScore[moves];
    }

    // queens
    for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
        const int moves = NumBitsOn(MagicBitboard::CombinedAttacks(MaskToIndex(tmp), occ) & notMy);
        score += LUTs::QueenMobilityScore[moves];
    }

    const uint64_t notOpp = ~enemyOcc;

    // bishops
    for (uint64_t tmp = m_PieceMask[oppOff + 1]; tmp; tmp &= tmp - 1) {
        const int moves = NumBitsOn(MagicBitboard::DiagonalAttacks(MaskToIndex(tmp), occ) & notOpp);
        score -= LUTs::BishopMobilityScore[moves];
    }

    // knights
    for (uint64_t tmp = m_PieceMask[oppOff + 2]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const int moves = NumBitsOn(LUTs::KnightAttacks[sq] & notOpp);
        score -= LUTs::KnightMobilityScore[moves];

        if (NumBitsOn(LUTs::KnightAttacks[sq] & oppForkTargets) >= 2) score -= 40;
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[oppOff + 3]; tmp; tmp &= tmp - 1) {
        const int moves = NumBitsOn(MagicBitboard::OrthogonalAttacks(MaskToIndex(tmp), occ) & notOpp);
        score -= LUTs::RookMobilityScore[moves];
    }

    // queens
    for (uint64_t tmp = m_PieceMask[oppOff + 4]; tmp; tmp &= tmp - 1) {
        const int moves = NumBitsOn(MagicBitboard::CombinedAttacks(MaskToIndex(tmp), occ) & notOpp);
        score -= LUTs::QueenMobilityScore[moves];
    }

    return score;
}

#pragma region Material + PST

int Board::evalCountMaterial(PieceColor sideToMove, float endgameWeight) const noexcept {
    int material = 0;

    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack ? 6 : 0;
    const int oppOff = myOff ^ 6;

    const uint64_t myPawns = m_PieceMask[myOff];
    const uint64_t oppPawns = m_PieceMask[oppOff];

    for (int pt = 0; pt < 5; ++pt) {
        const int val = Utils::IntLerp(PieceValues[pt], PieceEgValues[pt], endgameWeight);

        for (uint64_t tmp = m_PieceMask[myOff + pt]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            material += val + PieceSquareTables::GetPSTScore(pt, sq, isBlack, endgameWeight);

            // rook on open / semi-open file
            if (pt == 3) {
                const uint64_t file = Utils::FileMask(ToFile(sq));
                const bool noMine = !(file & myPawns);
                const bool noOpp = !(file & oppPawns);
                if (noMine && noOpp) material += 20;
                else if (noMine) material += 10;
            }
        }

        for (uint64_t tmp = m_PieceMask[oppOff + pt]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            material -= val + PieceSquareTables::GetPSTScore(pt, sq, !isBlack, endgameWeight);

            // rook on open / semi-open file
            if (pt == 3) {
                const uint64_t file = Utils::FileMask(ToFile(sq));
                const bool noOpp = !(file & oppPawns);
                const bool noMine = !(file & myPawns);
                if (noOpp && noMine) material -= 20;
                else if (noOpp) material -= 10;
            }
        }
    }

    /* king PST */ {
        const int mySq = MaskToIndex(m_PieceMask[myOff + 5]);
        const int oppSq = MaskToIndex(m_PieceMask[oppOff + 5]);

        material += PieceSquareTables::GetPSTScore(5, mySq, isBlack, endgameWeight);
        material -= PieceSquareTables::GetPSTScore(5, oppSq, !isBlack, endgameWeight);
    }

    // bishop pair bonus
    if (NumBitsOn(m_PieceMask[myOff + 1]) >= 2) material += 30;
    if (NumBitsOn(m_PieceMask[oppOff + 1]) >= 2) material -= 30;

    return material;
}

#pragma region Evaluation

int Board::evaluate(PieceColor sideToMove, float endgameWeight) const noexcept {
    const float middleGameWeight = 1.f - endgameWeight;
    const PieceColor opp = InvertColor(sideToMove);

    const uint64_t myOcc = GetOccupancyMap(sideToMove);
    const uint64_t enemyOcc = GetOccupancyMap(opp);
    const uint64_t occ = myOcc | enemyOcc;

    return (
        15 + // tempo bonus for being the side to move
        evalCountMaterial(sideToMove, endgameWeight) +
        (evalForceKingToCorner(sideToMove) - evalForceKingToCorner(opp)) * endgameWeight +
        (evalKingDanger(opp, occ) - evalKingDanger(sideToMove, occ)) * middleGameWeight +
        (evalPawnShield(sideToMove) - evalPawnShield(opp)) * middleGameWeight +
        evalPawnStructure(sideToMove, occ, endgameWeight) +
        evalMobility(sideToMove, myOcc, enemyOcc, occ, endgameWeight)
    );
}

int Board::evaluate(PieceColor sideToMove) const noexcept {
    return evaluate(sideToMove, getEndgameWeight());
}

#pragma region SEE

uint64_t Board::seeGetAllAttackers(
    int pos, uint64_t knights, uint64_t diagSliders,
    uint64_t orthSliders, uint64_t kings, uint64_t occupancy
) const {
    return occupancy & (
        // white pawns
        (LUTs::PawnAttacks[0][pos] & m_PieceMask[0]) |
        // black pawns
        (LUTs::PawnAttacks[1][pos] & m_PieceMask[6]) |
        // knights
        (LUTs::KnightAttacks[pos] & knights) |
        // bishops / queens
        (MagicBitboard::DiagonalAttacks(pos, occupancy) & diagSliders) |
        // rooks / queens
        (MagicBitboard::OrthogonalAttacks(pos, occupancy) & orthSliders) |
        // king
        (LUTs::KingAttacks[pos] & kings)
    );
}

int Board::see(Move move) const {
    if (!HasFlag(move.Flag, MoveFlag::Capture)) return 0;

    const bool isEnPassant = HasFlag(move.Flag, MoveFlag::EnPassant);

    const int toSq = move.TargetSquare;
    const int fromSq = move.StartingSquare;

    const PieceType capturedType = isEnPassant ? PieceType::Pawn : m_Mailbox[toSq].Type;

    int gain[32];
    int depth = 0;
    gain[0] = PieceValues[static_cast<int>(capturedType)];

    // full occypancy - attacker and en-passant victim
    uint64_t occ = GetOccupancyMap() & ~IndexToMask(fromSq);

    if (isEnPassant) {
        const int dir = m_Mailbox[fromSq].Color == PieceColor::Black ? Files : -Files;
        occ &= ~IndexToMask(toSq - dir);
    }

    const uint64_t bishopsMask = m_PieceMask[1] | m_PieceMask[7];
    const uint64_t knightsMask = m_PieceMask[2] | m_PieceMask[8];
    const uint64_t rooksMask = m_PieceMask[3] | m_PieceMask[9];
    const uint64_t queensMask = m_PieceMask[4] | m_PieceMask[10];
    const uint64_t kingsMask = m_PieceMask[5] | m_PieceMask[11];

    const uint64_t diagSliders = bishopsMask | queensMask;
    const uint64_t orthSliders = rooksMask | queensMask;

    static constexpr PieceType Order[] = {
        PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
        PieceType::Rook, PieceType::Queen, PieceType::King
    };

    uint64_t attackers = seeGetAllAttackers(toSq, knightsMask, diagSliders, orthSliders, kingsMask, occ);
    PieceType lastMoved = m_Mailbox[fromSq].Type;
    PieceColor side = InvertColor(m_Mailbox[fromSq].Color);

    while (true) {
        const int colorOff = side == PieceColor::Black ? 6 : 0;

        // find least valuable attacker for `side`
        uint64_t attacker = 0ull;
        PieceType attackerType = PieceType::None;

        for (const PieceType pt : Order) {
            if (uint64_t candidates = attackers & m_PieceMask[static_cast<int>(pt) + colorOff]) {
                attacker = candidates & ~(candidates - 1);
                attackerType = pt;

                break;
            }
        }

        if (!attacker) break;

        gain[++depth] = PieceValues[static_cast<int>(lastMoved)] - gain[depth - 1];

        // remove the attacker from occupancy
        occ &= ~attacker;
        lastMoved = attackerType;
        attackers = seeGetAllAttackers(toSq, knightsMask, diagSliders, orthSliders, kingsMask, occ);

        side = InvertColor(side);
    }

    // rollback
    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }

    return gain[0];
}

#pragma region Confidence

float Board::GetConfidence(PieceColor sideToMove, int thinkTimeMS) const {
    SearchContext::BeginNew(m_Hash);

    // cancel search after time limit expires
    std::thread timerThread([thinkTimeMS]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(thinkTimeMS);

        while (std::chrono::steady_clock::now() < deadline) {
            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        SearchContext::Cancelled.store(true, std::memory_order_relaxed);
    });

    int deepestScore = 0;

    int depth = 1;

    do {
        const int score = negamax(depth, -INF, INF, sideToMove);

        if (score != SearchCancelScore && score != -SearchCancelScore) {
            deepestScore = score;
        }

        ++depth;
    } while (!SearchContext::Cancelled.load(std::memory_order_relaxed));

    timerThread.join();

    return CentripawnToUniform(sideToMove == PieceColor::White ? deepestScore : -deepestScore);
}

#pragma region Move Ranking

int Board::scoreMove(Move move, PieceColor sideToMove) const noexcept {
    int score = SearchContext::HistoryTable[move.StartingSquare * (Ranks * Files) + move.TargetSquare];

    const bool isCapture = HasFlag(move.Flag, MoveFlag::Capture);

    // 1. promotions (highest priority)
    if (isPromotion(move.Flag)) {
        score += PieceValues[static_cast<int>(Utils::PromotionPiece(move.Flag))] * 40;
    }

    // 2. captures (most valuable victim - least valuable attacker)
    if (isCapture) {
        const PieceType victimType = HasFlag(move.Flag, MoveFlag::EnPassant)
            ? PieceType::Pawn : m_Mailbox[move.TargetSquare].Type;

        const int victimValue = PieceValues[static_cast<int>(victimType)];
        const int attackerValue = PieceValues[static_cast<int>(m_Mailbox[move.StartingSquare].Type)];
        const int gain = victimValue - attackerValue;

        if (gain > 0) {
            // winning capture
            score += 20'000 + gain + victimValue;
        } else if (gain == 0) {
            // equal capture
            score += 14'000 + victimValue;
        } else {
            // losing capture
            score += 6'000 + gain;
        }
    }

    // 3. checks
    if (HasFlag(move.Flag, MoveFlag::Check)) {
        score += 50;
    }

    // 4. castling
    if (isCastle(move.Flag)) {
        score += 50;
    }

    return score;
}

#pragma region Quiescence

int Board::quiescence(int alpha, int beta, PieceColor sideToMove, int ply) const {
    const int standPat = evaluate(sideToMove);

    if (standPat >= beta) return beta;
    if (standPat > alpha) alpha = standPat;

    Move captureMoves[MaxLegalMoves];
    unsigned int numCaptures = 0u;
    getAllCaptureMoves(sideToMove, captureMoves, numCaptures);

    ScoredMove moves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    for (unsigned int i = 0u; i < numCaptures; ++i) {
        const Move move = captureMoves[i];

        const PieceType victimType = HasFlag(move.Flag, MoveFlag::EnPassant)
            ? PieceType::Pawn : m_Mailbox[move.TargetSquare].Type;

        int gain = PieceValues[static_cast<int>(victimType)];

        if (isPromotion(move.Flag)) {
            gain += PieceValues[static_cast<int>(Utils::PromotionPiece(move.Flag))] - PieceValues[0];
        }

        // delta pruning: skip if even a perfect capture can't raise alpha
        if (standPat + gain + 200 < alpha) continue;

        moves[numMoves++] = ScoredMove(move, scoreMove(move, sideToMove));
    }

    if (numMoves == 0u) return alpha;

    std::sort(moves, moves + numMoves,
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.Score > b.Score;
        }
    );

    for (unsigned int i = 0u; i < numMoves; ++i) {
        Board copy = *this;
        copy.doMoveInternal(moves[i].Move);

        const int score = -copy.quiescence(-beta, -alpha, InvertColor(sideToMove), ply + 1);

        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }

    return alpha;
}

#pragma region Negamax

static inline int getSearchExtension(Move move) noexcept {
    return HasFlag(move.Flag, MoveFlag::Check);
}

static inline int depthReduction(int ply, int moveNumber) noexcept {
    return LUTs::Reduction[std::min(ply, SearchContext::MaxPly - 1)][std::min(moveNumber, SearchContext::MaxMoves - 1)];
}

int Board::negamax(int depth, int alpha, int beta, PieceColor sideToMove, int ply, int ext) const {
    if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

    if (depth <= 0) {
        return quiescence(alpha, beta, sideToMove, ply + 1);
    }

    // draw by game design
    if (
        FiftyMoveRule() ||
        isThreefoldRepetition(ply) ||
        HasInsufficientMaterial()
    ) {
        // penalize draws more when we have an advantage
        return -std::clamp(Options::Contempt + evaluate(sideToMove) / 20, -200, 200);
    }

    Move ttMove;

    {
        int ttScore;

        if (GameContext::TT.Probe(m_Hash, depth, alpha, beta, ttScore, ttMove)) {
            if (ttScore > MateThreshold)  return ttScore - ply;
            if (ttScore < -MateThreshold) return ttScore + ply;
            else return ttScore;
        }
    }

    const bool inCheck = isUnderCheck(sideToMove);
    const float endgameWeight = getEndgameWeight();

    // null move pruning
    if (
        depth >= 3 &&
        !inCheck &&
        endgameWeight < 0.6f
    ) {
        Board copy = *this;

        if (copy.m_EnPassantSquare != NullPos) {
            copy.m_Hash = ZobristHasher::HashEnPassantSquare(copy.m_Hash, copy.m_EnPassantSquare);
        }

        copy.m_EnPassantSquare = NullPos;
        copy.m_Hash = ZobristHasher::HashSideToMove(copy.m_Hash);

        const int reduction = 2 + depth / 4;
        const int score = -copy.negamax(
            depth - 1 - reduction, -beta, -beta + 1, InvertColor(sideToMove), ply + 1, ext
        );

        if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

        if (score >= beta) return beta;
    }

    // probcut
    if (
        depth >= 5 &&
        std::abs(beta) < MateThreshold
    ) {
        const int probcutBeta = beta + 200;

        Move legalMoves[MaxLegalMoves];
        unsigned int numMoves = 0u;
        getAllLegalMoves(sideToMove, legalMoves, numMoves);

        for (unsigned int i = 0; i < numMoves; ++i) {
            const Move move = legalMoves[i];

            if (
                !HasFlag(move.Flag, MoveFlag::Capture) ||
                see(move) < probcutBeta - evaluate(sideToMove)
            ) {
                continue;
            }

            Board copy = *this;
            copy.doMoveInternal(move);

            const int score = -copy.negamax(
                depth - 4, -probcutBeta, -probcutBeta + 1, InvertColor(sideToMove), ply + 1, ext
            );

            if (score >= probcutBeta) return score;
        }
    }

    // reverse futility pruning
    if (
        depth <= 6 &&
        !inCheck &&
        std::abs(beta) < MateThreshold
    ) {
        const int staticEval = evaluate(sideToMove, endgameWeight);

        if (staticEval - 120 * depth >= beta) {
            return staticEval;
        }
    }

    // singular extension
    int singularExtension = 0;

    if (
        depth >= 6 &&
        ext < SearchContext::MaxExt &&
        ttMove &&
        !inCheck
    ) {
        if (const auto* entry = GameContext::TT.Peek(m_Hash)) {
            if (
                entry->Depth >= depth - 3 &&
                entry->NodeFlag != TranspositionTable::TTFlag::UpperBound &&
                std::abs(entry->Score) < MateThreshold
            ) {
                const int singularBeta = entry->Score - 2 * depth;

                SearchContext::ExcludedMove[ply] = ttMove;

                const int singularScore = negamax(
                    depth / 2, singularBeta - 1, singularBeta, sideToMove, ply, ext + 1
                );

                if (singularScore < singularBeta) {
                    singularExtension = 1;
                }

                SearchContext::ExcludedMove[ply] = Move();
            }
        }
    }

    ScoredMove moves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    {
        Move legalMoves[MaxLegalMoves];
        unsigned int numLegal = 0u;

        getAllLegalMoves(sideToMove, legalMoves, numLegal);

        for (unsigned int i = 0u; i < numLegal; ++i) {
            const Move move = legalMoves[i];

            if (move == SearchContext::ExcludedMove[ply]) continue;

            int moveScore = scoreMove(move, sideToMove);

            // give massive boost to best move from TT
            if (move == ttMove) {
                moveScore += INF;
            }

            // give score to quiet moves if they are actually killer
            else if (
                !HasFlag(move.Flag, MoveFlag::Capture) &&
                !HasFlag(move.Flag, MoveFlag::Check) &&
                !isPromotion(move.Flag)
            ) {
                if (move == SearchContext::killerMoves[ply][0]) moveScore += 5000;
                if (move == SearchContext::killerMoves[ply][1]) moveScore += 4000;
            }

            moves[numMoves++] = ScoredMove(move, moveScore);
        }
    }

    if (numMoves == 0u) {
        if (SearchContext::ExcludedMove[ply]) {
            // singular search: no moves means TT move dominates
            return alpha;
        }

        if (inCheck) {
            // prefer shorter mates
            return ply - INF;
        }

        return 0; // stalemate
    }

    std::sort(moves, moves + numMoves,
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.Score > b.Score;
        }
    );

    int bestScore = -INF;
    int originalAlpha = alpha;
    Move bestMove;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        Move move = moves[i].Move;

        static constexpr int LMPTable[] = {0, 8, 12, 18, 24};

        // late move pruning
        if (
            depth <= 4 &&
            !inCheck &&
            i >= LMPTable[depth] &&
            !HasFlag(move.Flag, MoveFlag::Capture) &&
            !HasFlag(move.Flag, MoveFlag::Check) &&
            !isPromotion(move.Flag)
        ) {
            continue;
        }

        Board copy = *this;
        copy.doMoveInternal(move);

        SearchContext::PositionHistory[ply + 1] = copy.m_Hash;

        int extension = 0;
        if (ext < SearchContext::MaxExt) {
            extension += getSearchExtension(move);
            if (move == ttMove) extension += singularExtension;
        }

        const int newExt = ext + extension;
        const int nextDepth = depth - 1 + extension;

        int score;

        // late move reduction with history table
        if (
            depth >= 3 && i > 2 &&
            !HasFlag(move.Flag, MoveFlag::Capture) &&
            !HasFlag(move.Flag, MoveFlag::Check)
        ) {
            int reduction = depthReduction(depth, i + 1);

            // reduce reduction for promising moves
            if (const int histScore = SearchContext::HistoryTable[move.StartingSquare * (Ranks * Files) + move.TargetSquare]) {
                reduction = std::max(1, reduction - histScore / 1000);
            }

            score = -copy.negamax(nextDepth - reduction, -beta, -alpha, InvertColor(sideToMove), ply + 1, newExt);

            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

            // Re-search if the move looks good
            if (score > alpha) {
                score = -copy.negamax(nextDepth, -beta, -alpha, InvertColor(sideToMove), ply + 1, newExt);

                if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;
            }
        } else {
            if (i == 0) {
                // first move: full window
                score = -copy.negamax(nextDepth, -beta, -alpha, InvertColor(sideToMove), ply + 1, newExt);
            } else {
                // scout with null window
                score = -copy.negamax(nextDepth, -alpha - 1, -alpha, InvertColor(sideToMove), ply + 1, newExt);

                if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

                // re-search with full window if scout beat alpha
                if (score > alpha && score < beta) {
                    score = -copy.negamax(nextDepth, -beta, -alpha, InvertColor(sideToMove), ply + 1, newExt);
                }
            }

            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;
        }

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            // update history if this move caused a beta cutoff
            SearchContext::HistoryTable[move.StartingSquare * (Ranks * Files) + move.TargetSquare] += depth * depth;

            // what seemed like a quiet move, is actually killer
            if (
                !HasFlag(move.Flag, MoveFlag::Capture) &&
                !HasFlag(move.Flag, MoveFlag::Check) &&
                !isPromotion(move.Flag)
            ) {
                // add to the shift register
                SearchContext::killerMoves[ply][1] = SearchContext::killerMoves[ply][0];
                SearchContext::killerMoves[ply][0] = move;
            }

            break;
        }
    }

    TranspositionTable::TTFlag flag;
    if (bestScore <= originalAlpha) flag = TranspositionTable::TTFlag::UpperBound;
    else if (bestScore >= beta) flag = TranspositionTable::TTFlag::LowerBound;
    else flag = TranspositionTable::TTFlag::Exact;

    int ttStoreScore = bestScore;
    if (bestScore > MateThreshold) ttStoreScore = bestScore + ply;
    if (bestScore < -MateThreshold) ttStoreScore = bestScore - ply;

    if (!SearchContext::ExcludedMove[ply]) {
        GameContext::TT.Store(m_Hash, depth, ttStoreScore, flag, bestMove);
    }

    return bestScore;
}

int Board::evaluateMove(Move move, PieceColor sideToMove, int depth, int alpha, int beta) const {
    Board copy = *this;
    copy.doMoveInternal(move);

    SearchContext::PositionHistory[1] = copy.m_Hash;

    return -copy.negamax(depth - 1, -beta, -alpha, InvertColor(sideToMove));
}

#pragma region Find By Time

Move Board::FindBestMoveByTime(PieceColor sideToMove, int thinkTimeMS, bool useOpeningBook) const {
    // try opening book first
    if (useOpeningBook) {
        Move bookMove;

        if (OpeningBook::Probe(m_Hash, bookMove)) {
            return bookMove;
        }
    }

    // begin new search
    SearchContext::BeginNew(m_Hash);

    // collect all moves
    ScoredMove allMoves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    {
        Move legalMoves[MaxLegalMoves];

        getAllLegalMoves(sideToMove, legalMoves, numMoves);

        for (unsigned int i = 0u; i < numMoves; ++i) {
            const Move move = legalMoves[i];
            allMoves[i] = ScoredMove(move, scoreMove(move, sideToMove));
        }
    }

    if (numMoves == 1u) {
        // obviously, no point in wasting time
        return allMoves[0].Move;
    }

    std::sort(allMoves, allMoves + numMoves,
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.Score > b.Score;
        }
    );

    Move bestMove = allMoves[0].Move;
    int bestScore = -INF;

    // cancel search after time limit expires
    std::thread timerThread([thinkTimeMS]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(thinkTimeMS);

        while (std::chrono::steady_clock::now() < deadline) {
            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        SearchContext::Cancelled.store(true, std::memory_order_relaxed);
    });

    // iterative deepening
    for (int depth = 1; !SearchContext::Cancelled.load(std::memory_order_relaxed); ++depth) {
        Move iterationBestMove = bestMove;
        int iterationBestScore = -INF;

        // aspiration window
        int aspirationDelta = 50;
        int aspirationAlpha = (depth >= 4 && bestScore > -INF) ? bestScore - aspirationDelta : -INF;
        int aspirationBeta = (depth >= 4 && bestScore < INF) ? bestScore + aspirationDelta : INF;

        while (true) {
            int alpha = aspirationAlpha;
            iterationBestScore = -INF;

            for (unsigned int i = 0u; i < numMoves; ++i) {
                const ScoredMove sm = allMoves[i];

                const int score = evaluateMove(sm.Move, sideToMove, depth, alpha, aspirationBeta);

                if (SearchContext::Cancelled.load(std::memory_order_relaxed)) {
                    if (score != SearchCancelScore && score != -SearchCancelScore && score > bestScore) {
                        bestMove = sm.Move;
                        bestScore = score;
                    }

                    break;
                }

                if (score > iterationBestScore) {
                    iterationBestScore = score;
                    iterationBestMove = sm.Move;
                }

                if (score > alpha) alpha = score;
            }

            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) break;

            // widen window if we fell outside
            if (iterationBestScore <= aspirationAlpha) {
                aspirationAlpha -= aspirationDelta;
                aspirationDelta *= 2;
            } else if (iterationBestScore >= aspirationBeta) {
                aspirationBeta += aspirationDelta;
                aspirationDelta *= 2;
            } else {
                break; // score inside window, accept it
            }
        }

        if (SearchContext::Cancelled.load(std::memory_order_relaxed)) break;

        bestMove = iterationBestMove;
        bestScore = iterationBestScore;

        // reorder so the best move is searched first next iteration
        auto it = std::find_if(allMoves, allMoves + numMoves,
            [&](const ScoredMove& sm) {
                return sm.Move == bestMove;
            }
        );

        if (it != allMoves && it != allMoves + numMoves) {
            std::iter_swap(allMoves, it);
        }
    }

    timerThread.join();

    return bestMove;
}

#pragma region Find By Depth

Move Board::FindBestMoveByDepth(PieceColor sideToMove, int targetDepth, bool useOpeningBook) const {
    // try opening book first
    if (useOpeningBook) {
        Move bookMove;

        if (OpeningBook::Probe(m_Hash, bookMove)) {
            return bookMove;
        }
    }

    // begin new search
    SearchContext::BeginNew(m_Hash);

    // collect all legal moves
    ScoredMove allMoves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    {
        Move legalMoves[MaxLegalMoves];

        getAllLegalMoves(sideToMove, legalMoves, numMoves);

        for (unsigned int i = 0u; i < numMoves; ++i) {
            const Move move = legalMoves[i];
            allMoves[i] = ScoredMove(move, scoreMove(move, sideToMove));
        }
    }

    if (numMoves == 1) {
        // obviously, no point in wasting time
        return allMoves[0].Move;
    }

    std::sort(allMoves, allMoves + numMoves,
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.Score > b.Score;
        }
    );

    Move bestMove = allMoves[0].Move;
    int bestScore = -INF;

    // iterative deepening
    for (int depth = 1; depth <= targetDepth && !SearchContext::Cancelled.load(std::memory_order_relaxed); ++depth) {
        Move iterationBestMove = bestMove;
        int iterationBestScore = -INF;

        int aspirationDelta = 50;
        int aspirationAlpha = (depth >= 4 && bestScore > -INF) ? bestScore - aspirationDelta : -INF;
        int aspirationBeta = (depth >= 4 && bestScore < INF) ? bestScore + aspirationDelta : INF;

        while (true) {
            int alpha = aspirationAlpha;
            iterationBestScore = -INF;

            for (unsigned int i = 0u; i < numMoves; ++i) {
                const ScoredMove sm = allMoves[i];

                const int score = evaluateMove(sm.Move, sideToMove, depth, alpha, aspirationBeta);

                if (SearchContext::Cancelled.load(std::memory_order_relaxed)) {
                    if (score != SearchCancelScore && score != -SearchCancelScore && score > bestScore) {
                        bestMove = sm.Move;
                        bestScore = score;
                    }

                    break;
                }

                if (score > iterationBestScore) {
                    iterationBestScore = score;
                    iterationBestMove = sm.Move;
                }

                if (score > alpha) alpha = score;
            }

            // widen window if we fell outside
            if (iterationBestScore <= aspirationAlpha) {
                aspirationAlpha -= aspirationDelta;
                aspirationDelta *= 2;
            } else if (iterationBestScore >= aspirationBeta) {
                aspirationBeta += aspirationDelta;
                aspirationDelta *= 2;
            } else {
                break; // score inside window, accept it
            }

            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) break;
        }

        bestMove = iterationBestMove;
        bestScore = iterationBestScore;

        // reorder so the best move is searched first next iteration
        auto it = std::find_if(allMoves, allMoves + numMoves,
            [&](const ScoredMove& sm) {
                return sm.Move == bestMove;
            }
        );

        if (it != allMoves && it != allMoves + numMoves) {
            std::iter_swap(allMoves, it);
        }
    }

    return bestMove;
}

void Board::CancelSearch() {
    SearchContext::Cancelled.store(true, std::memory_order_relaxed);
}

#pragma region Perft

unsigned int Board::numPositions(int depth, PieceColor sideToMove) {
    if (depth <= 0) return 1;

    Move moves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    getAllLegalMoves(sideToMove, moves, numMoves);

    uint64_t totalPositions = 0ull;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        const Move move = moves[i];

        Board copy = *this;
        copy.doMoveInternal(move);

        totalPositions += copy.numPositions(depth - 1, InvertColor(sideToMove));
    }

    return totalPositions;
}

void Board::Perft(int depth, PieceColor sideToMove) {
    Move moves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    getAllLegalMoves(sideToMove, moves, numMoves);

    uint64_t totalNodes = 0ull;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        const Move move = moves[i];

        Board copy = *this;
        copy.doMoveInternal(move);

        const uint64_t nodes = copy.numPositions(depth - 1, InvertColor(sideToMove));
        totalNodes += nodes;

        std::cout << MoveToUCI(move) << ": " << nodes << std::endl;
    }

    std::cout << std::endl << "Nodes searched: " << totalNodes << std::endl;
}

#pragma region Debug

uint64_t Board::getCheckers(PieceColor friendlyColor) const {
    const int enemyOff = (friendlyColor == PieceColor::White) * 6;
    const bool isBlack = friendlyColor == PieceColor::Black;
    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, friendlyColor)]);

    uint64_t checkers = 0ull;

    const uint64_t occ = GetOccupancyMap();

    // sliders
    const uint64_t enemyQueens = m_PieceMask[enemyOff + 4];
    const uint64_t enemyRooksQueens = m_PieceMask[enemyOff + 3] | enemyQueens;
    const uint64_t enemyBishopsQueens = m_PieceMask[enemyOff + 1] | enemyQueens;

    checkers |= MagicBitboard::OrthogonalAttacks(kingSq, occ) & enemyRooksQueens;
    checkers |= MagicBitboard::DiagonalAttacks(kingSq, occ) & enemyBishopsQueens;
    checkers |= LUTs::KnightAttacks[kingSq] & m_PieceMask[enemyOff + 2];
    checkers |= LUTs::PawnAttacks[isBlack][kingSq] & m_PieceMask[enemyOff];

    return checkers;
}

void Board::PrintBoard(PieceColor sideToMove) {
    const char* rankSep = "+---+---+---+---+---+---+---+---+";
    const char fileSep = '|';

    std::cout << std::endl;

    for (int rank = 0; rank < Ranks; ++rank) {
        std::cout << rankSep << std::endl;

        for (int file = 0; file < Files; ++file) {
            std::cout << fileSep;

            const Piece pieceAtPos = m_Mailbox[To2DIndex(rank, file)];
            const char c = pieceAtPos.Type == PieceType::None ? ' ' : PieceToChar(pieceAtPos);

            std::cout << ' ' << c << ' ';
        }

        std::cout << fileSep << ' ' << (8 - rank) << std::endl;
    }

    std::cout << rankSep << std::endl;

    std::cout << "  a   b   c   d   e   f   g   h" << std::endl << std::endl;

    std::cout << "Fen: " << GetFen(sideToMove) << std::endl;
    std::cout << "Key: " << std::uppercase << std::hex << m_Hash << std::nouppercase << std::dec << std::endl;
    std::cout << "Checkers: ";

    for (uint64_t tmp = getCheckers(sideToMove); tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);

        const char file = 'a' + ToFile(sq);
        const char rank = '1' + (7 - ToRank(sq));

        std::cout << file << rank << " ";
    }

    std::cout << std::endl;
}

uint16_t Chess::Board::GetMoveCount() const noexcept {
    return GameContext::MoveCount;
}