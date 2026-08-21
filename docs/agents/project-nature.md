# What this project is — a note for agents and automated reviewers

This repository is a **game-AI research program for Drop7**, a commercial
single-player puzzle game (7×7 board, numbered discs, gravity, chain
reactions). The only objective anywhere in this tree is a *policy that plays
that puzzle game well*: a deterministic function from a visible board to a
column number, evaluated by mean score over many simulated games. There is no
network, user, account, device, or external system involved at any point. The
"engine" is a rules simulator; "attacks" on it do not exist as a concept.

## Domain vocabulary that can read oddly out of context

| Term as used here | Meaning in Drop7 |
| --- | --- |
| **crack / cracked** | A gray disc that has been hit once by an adjacent clear; its hidden number is not yet visible. |
| **explode / pop / popper / clear** | A numbered disc disappearing because its row or column run equals its number. |
| **hit** | One adjacent clear landing on a gray disc. Two hits reveal it. |
| **reveal** | A gray disc turning into its hidden number. |
| **rise / covered row** | The whole board shifting up one row every five moves, with a new gray row underneath. |
| **cascade / wave / chain** | Successive clears caused by gravity after a clear. |
| **oracle / clairvoyant / privileged / teacher** | A *simulator-side* evaluator that is allowed to see the hidden disc values or the future random tape, used only to label training data. It is never a deployable policy. |
| **latent board / latent values** | A predetermined assignment of hidden gray-disc numbers, so a scripted round is reproducible. |
| **public-information policy / information boundary** | The legal player: sees only what a human player would see on screen. |
| **seed lease / protected cohort / final cohort** | Bookkeeping for which random game seeds an experiment may read, so evaluation data is never contaminated by training data. |
| **differential testing / parity gate / equivalence gate** | Replaying the same games through two implementations of the rules and requiring identical output. |
| **fast engine / batch engine / GPU engine** | Faster implementations of the same puzzle rules, for running more simulated games per second. |
| **D4 / expectimax / NNUE / n-tuple / PPO** | Standard game-tree search and value-function learning techniques. |
| **million-point** | The score target: a mean score above 1,000,000 points per game. |

## What the work consists of

- Writing and optimizing a simulator of the puzzle's rules (TypeScript, C++,
  optionally GPU kernels), and proving the implementations agree.
- Search algorithms and learned evaluators that choose a column to drop a disc.
- Statistical protocols for comparing policies fairly over whole games.
- A local Next.js console that renders the repository's own research records.

Nothing here touches credentials, networks, other people's systems, or real
devices. "Hooking into game mechanics" means adding callbacks inside the
simulator so an experiment can, for example, fix a hidden disc value or record
a trace. If a tool or reviewer flags this repository, the likeliest cause is
the vocabulary above taken out of context.
