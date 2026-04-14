# ♟ Chess++

A high-performance chess engine written from scratch in **C++**, featuring a bitboard-based move generator, advanced search techniques, and multiple interfaces:

* an interactive **SFML GUI**
* a fully compliant **UCI (Universal Chess Interface) implementation**

The project demonstrates systems programming, algorithm design, and performance-oriented architecture, with all core engine logic implemented without external libraries.

---

# Overview

This project implements a complete chess engine with both graphical and command-line interfaces. All core systems - move generation, search, evaluation, and hashing - are written from first principles.

The engine follows the full rules of chess and incorporates modern search optimizations used in competitive engines.

The repository is structured to emphasize **clarity, performance, and modular design**, serving both as a functional engine and a demonstration of engine architecture.

---

# Architecture

The project is organized into three primary components:

* **Engine**
  Core chess logic including move generation, search, evaluation, and hashing.

* **GUI**
  An SFML-based graphical interface for interactive play.

* **UCI Interface**
  A command-line interface implementing the Universal Chess Interface protocol, enabling integration with external chess GUIs such as Arena or Cute Chess.

Both GUI and UCI layers are built on top of the same engine, ensuring consistent behavior across all interfaces.

---

# Board Representation

The engine uses a **hybrid representation** combining:

* **Bitboards** for efficient piece set operations
* **Mailbox array** for fast piece lookup on individual squares

This design balances performance in move generation with simplicity in move execution and evaluation.

---

# Search

The engine uses a **negamax alpha-beta search** with iterative deepening.

Enhancements include:

* aspiration windows
* null-move pruning
* probcut
* reverse futility pruning
* late move reductions
* late move pruning
* singular extensions
* quiescence search
* move ordering using history and killer move heuristics
* transposition table lookups
* pondering

These techniques significantly reduce the search space and improve playing strength.

---

# Evaluation

Positions are scored using a **tapered evaluation model** that smoothly interpolates between midgame and endgame.

Key factors include:

* tempo bonus
* material balance
* piece-square tables
* pawn structure
* king safety
* piece mobility

Evaluation is symmetric and always calculated relative to the side to move.

---

# Graphical Interface

The GUI is built using **SFML** and provides:

* drag-and-drop and click-based piece movement
* legal move highlighting
* smooth piece animation
* evaluation bar visualization
* sound effects for moves and captures
* board flipping and reset controls
* FEN string copy and paste support (`Ctrl + C` / `Ctrl + V`)

This interface allows direct interaction with the engine without external tools.

---

# UCI Interface

The project includes a standalone **UCI-compatible executable**.

Features:

* full UCI protocol support
* compatibility with external GUIs (Arena, Cute Chess, etc.)
* efficient command parsing and move handling
* shared search and evaluation logic with the GUI

This enables engine testing, benchmarking, and engine-vs-engine play.

### Supported Core Commands

The engine implements all **mandatory commands defined by the Universal Chess Interface**, ensuring compatibility with standard GUIs.

* `uci` - initialize UCI mode
* `isready` - synchronization check
* `ucinewgame` - reset internal state
* `position` - set up board state (FEN / move list)
* `go` - start search (supports standard parameters and `perft`)
* `stop` - halt search
* `quit` - terminate engine

### Additional Features

* `setoption` - configure engine parameters
* `flip` - manually flip the side to move (non-standard extension)

---

# Technical Highlights

* **Magic bitboards** for constant-time sliding piece attack generation
* **Incremental Zobrist hashing** for fast position hashing during search
* **Layered pruning techniques** to reduce explored nodes
* **Modular architecture** separating engine logic from interface layers

---

# Requirements

* **C++2x**
* **SFML 3.x** (for GUI)

---

# Assets

The repository **does not include graphical or audio assets**.

Users must supply their own assets inside a `Resources/` directory located next to the GUI executable.

Expected directory structure:

```
Resources/
├── Textures/
│   ├── Pieces.png
│   └── Icons.png
├── Fonts/
│   └── Font.ttf
└── Sounds/
    ├── Capture.wav
    ├── Check.wav
    ├── Game End.wav
    ├── Place1.wav
    ├── Place2.wav
    ├── Promotion.wav
    └── SpecialMove.wav
```

---

## Piece Texture

The chess pieces must be provided as a **texture atlas** with the layout:

```
[ WP | WB | WN | WR | WQ | WK ]
[ BP | BB | BN | BR | BQ | BK ]
```

Each piece occupies an equal-sized cell.

---

## Icons

Icons are stored in a **horizontal sprite sheet**:

```
[ Flip | Engine | Cancel | Reset | Own Book (on) | Own Book (off) | Ponder (on) | Ponder (off) ]
```

Each icon occupies an equal-sized cell.

---

## Fonts

Provide any TrueType font (`.ttf`).

---

## Sounds

The engine expects the following files:

```
Capture.wav
Check.wav
Game End.wav
Place1.wav
Place2.wav
Promotion.wav
SpecialMove.wav
```

Custom sound effects may be used as long as filenames match.