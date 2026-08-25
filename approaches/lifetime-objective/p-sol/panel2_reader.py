# PanelRecordV2 reader (P-SOL-v1 section 3; layout as implemented by
# approaches/lifetime-objective/sibling-corpus/generate.cpp --panel2).
#
# 992 bytes per record, little-endian:
#   header 96 B: u32 version(0x0200), u32 recordId, u32 originSeed,
#     u16 moveIndex, u8 rootNextDisc, u8 rootMovesToRise, u8 legalMask,
#     u8 chosenColumn, u8 referenceColumn, u8 engineId, u8 K, u8 H,
#     u8 panelFlags, u8 rootBoard[49], pad[24]
#   sibling 128 B x 7: u8 afterBoard[49], u8 afterNextDisc,
#     u8 afterMovesToRise, u8 survived, u8 legal, u8 afterClears,
#     u8 afterReveals, u8 afterMaxDepth, i32 afterScoreDelta,
#     u8 contLifetime[K], u8 contDeathRise[K], u32 contClearsTotal,
#     u32 contRevealsTotal, pad to 128
#
# The K-dependent offsets make a single numpy dtype impossible; records are
# decoded into plain arrays instead.  Refuses any version other than 0x0200.

import os
import struct

import numpy as np

RECORD_BYTES = 992
HEADER_BYTES = 96
SIBLING_BYTES = 128
BOARD = 7
VERSION = 0x0200

HEADER = struct.Struct("<IIIHBBBBBBBBB49s24s")


class Panel2File:
    """Decoded panel2 file: header arrays plus per-sibling label arrays."""

    def __init__(self, path):
        size = os.path.getsize(path)
        if size % RECORD_BYTES != 0:
            raise ValueError(f"{path}: size {size} is not a multiple of {RECORD_BYTES}")
        count = size // RECORD_BYTES
        raw = np.fromfile(path, dtype=np.uint8).reshape(count, RECORD_BYTES)

        self.path = path
        self.count = count
        header = raw[:, :HEADER_BYTES]
        self.version = header[:, 0:4].copy().view("<u4")[:, 0]
        if count and not np.all(self.version == VERSION):
            raise ValueError(f"{path}: unknown panel2 version(s) {set(self.version.tolist())}")
        self.record_id = header[:, 4:8].copy().view("<u4")[:, 0]
        self.origin_seed = header[:, 8:12].copy().view("<u4")[:, 0]
        self.move_index = header[:, 12:14].copy().view("<u2")[:, 0]
        self.root_next_disc = header[:, 14]
        self.root_moves_to_rise = header[:, 15]
        self.legal_mask = header[:, 16]
        self.chosen_column = header[:, 17]
        self.reference_column = header[:, 18]
        self.engine_id = header[:, 19]
        self.k = header[:, 20]
        self.horizon = header[:, 21]
        self.panel_flags = header[:, 22]
        self.root_board = header[:, 23:72]
        self.header_pad = header[:, 72:96]

        if count:
            kset = set(self.k.tolist())
            if len(kset) != 1:
                raise ValueError(f"{path}: mixed K values {kset}")
            self.K = int(self.k[0])
        else:
            self.K = 0
        K = self.K

        sib = raw[:, HEADER_BYTES:].reshape(count, BOARD, SIBLING_BYTES)
        self.after_board = sib[:, :, 0:49]
        self.after_next_disc = sib[:, :, 49]
        self.after_moves_to_rise = sib[:, :, 50]
        self.survived = sib[:, :, 51]
        self.legal = sib[:, :, 52]
        self.after_clears = sib[:, :, 53]
        self.after_reveals = sib[:, :, 54]
        self.after_max_depth = sib[:, :, 55]
        self.after_score_delta = (
            sib[:, :, 56:60].copy().view("<i4")[:, :, 0]
        )
        self.cont_lifetime = sib[:, :, 60:60 + K]
        self.cont_death_rise = sib[:, :, 60 + K:60 + 2 * K]
        self.cont_clears_total = (
            sib[:, :, 60 + 2 * K:64 + 2 * K].copy().view("<u4")[:, :, 0]
        )
        self.cont_reveals_total = (
            sib[:, :, 64 + 2 * K:68 + 2 * K].copy().view("<u4")[:, :, 0]
        )
        self.sibling_pad = sib[:, :, 68 + 2 * K:]

    def km_restricted_mean(self):
        """Per-(record, sibling) restricted-mean lifetime E[min(life, H)].

        Censoring is type I at the fixed horizon H, so the Kaplan-Meier
        restricted mean over the K continuations reduces exactly to the
        sample mean of the capped lifetimes.  Illegal siblings return nan.
        """
        mean = self.cont_lifetime.astype(np.float64).mean(axis=2)
        mean[self.legal == 0] = np.nan
        return mean

    def censor_counts(self):
        """Per-(record, sibling) number of continuations censored at H."""
        counts = (self.cont_death_rise == 0).sum(axis=2)
        return np.where(self.legal == 1, counts, 0)

    def rise_survival_counts(self):
        """KM hazard-vector inputs: counts surviving >= k rises, k = 1..12.

        A continuation with deathRise = d (1..12) survived d - 1 completed
        rises; a censored continuation (deathRise = 0) survived at least
        floor(H / 5) rises and contributes to every bin it is known to have
        survived.
        """
        count, board, K = self.cont_lifetime.shape
        out = np.zeros((count, board, 12), dtype=np.int32)
        death = self.cont_death_rise
        censored = death == 0
        known_rises = int(self.horizon[0]) // 5 if self.count else 0
        for k in range(1, 13):
            survived = (censored & (known_rises >= k)) | (~censored & (death >= k + 1))
            out[:, :, k - 1] = survived.sum(axis=2)
        out[self.legal == 0] = 0
        return out
