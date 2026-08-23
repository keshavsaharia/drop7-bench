# G0 structural gates for panel2 files (P-SOL-v1): legality, header
# invariants, and pad hygiene.  Seed-free: reads only already-written panel2
# artifacts.  Exit 0 iff every check passes.
#
# Checks per record:
#   * version 0x0200, K and H match the file-level values, engineId in {0,1,2}
#   * recordId is the record's index (seed-order sequential)
#   * legalMask agrees with the root board's top row (column c legal iff
#     board[0][c] is empty)
#   * per-sibling `legal` byte equals the mask bit; illegal sibling slots are
#     all-zero (labels only for legal columns)
#   * header pad and sibling tail pad are zero
#   * chosenColumn is legal; referenceColumn is 255 or legal, and computed
#     exactly when recordId % 16 == 0 (panelFlags bit0 in agreement)
#   * contLifetime <= H; deathRise <= 12; censored (deathRise 0) iff
#     lifetime == H

import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from panel2_reader import Panel2File  # noqa: E402


def check(path):
    f = Panel2File(path)
    errors = []

    def require(condition, label):
        if not bool(condition):
            errors.append(label)

    idx = np.arange(f.count, dtype=np.uint32)
    require(np.all(f.record_id == idx), "recordId sequential in file order")
    require(np.all((f.engine_id >= 0) & (f.engine_id <= 2)), "engineId in 0..2")
    require(np.all(f.horizon == f.horizon[0]) if f.count else True, "uniform H")
    require(np.all(f.header_pad == 0), "header pad zero")
    require(np.all(f.sibling_pad == 0), "sibling pad zero")

    top_row_empty = f.root_board[:, :7] == 0
    mask_bits = ((f.legal_mask[:, None] >> np.arange(7)[None, :]) & 1).astype(bool)
    require(np.all(mask_bits == top_row_empty), "legalMask matches root top row")
    require(np.all((f.legal == 1) == mask_bits), "sibling legal byte matches mask")

    sib_raw = np.concatenate(
        [
            f.after_board.reshape(f.count, 7, -1),
            f.after_next_disc[:, :, None],
            f.after_moves_to_rise[:, :, None],
            f.survived[:, :, None],
            f.after_clears[:, :, None],
            f.after_reveals[:, :, None],
            f.after_max_depth[:, :, None],
            np.abs(f.after_score_delta)[:, :, None].astype(np.int64),
            f.cont_lifetime.astype(np.int64),
            f.cont_death_rise.astype(np.int64),
            f.cont_clears_total[:, :, None].astype(np.int64),
            f.cont_reveals_total[:, :, None].astype(np.int64),
        ],
        axis=2,
    )
    illegal = ~mask_bits
    require(np.all(sib_raw[illegal] == 0), "illegal sibling slots fully zero")

    rows, cols = np.nonzero(mask_bits)
    require(
        np.all(mask_bits[np.arange(f.count), f.chosen_column]),
        "chosenColumn legal",
    )
    ref_computed = f.reference_column != 255
    require(np.all(ref_computed == (f.record_id % 16 == 0)), "reference on recordId%16==0")
    ref_idx = np.where(ref_computed, f.reference_column, 0)
    require(
        np.all(mask_bits[np.arange(f.count), ref_idx][ref_computed])
        if ref_computed.any()
        else True,
        "referenceColumn legal when computed",
    )
    require(
        np.all(((f.panel_flags & 1) == 1) == ref_computed),
        "panelFlags bit0 = reference computed",
    )

    H = int(f.horizon[0]) if f.count else 0
    legal3 = mask_bits[:, :, None]
    life = f.cont_lifetime.astype(np.int64)
    death = f.cont_death_rise.astype(np.int64)
    require(np.all(life[np.broadcast_to(legal3, life.shape)] <= H), "lifetime <= H")
    require(np.all(death <= 12), "deathRise <= 12")
    censored = death == 0
    require(
        np.all((life[np.broadcast_to(legal3, life.shape)] == H)
               == censored[np.broadcast_to(legal3, censored.shape)]),
        "censored iff lifetime == H",
    )

    status = "PASS" if not errors else "FAIL"
    print(f"{status} {path}: {f.count} records, K={f.K}, "
          f"H={H}, engineId={sorted(set(f.engine_id.tolist()))}, "
          f"legalSiblings={int(mask_bits.sum())}, "
          f"censoredContinuations={int((censored & legal3[:, :, 0][:, :, None]).sum())}")
    for e in errors:
        print(f"  FAILED: {e}")
    return not errors


def main():
    ok = True
    for path in sys.argv[1:]:
        ok = check(path) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
