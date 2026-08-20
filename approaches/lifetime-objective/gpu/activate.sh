# shellcheck shell=bash
#
# ROCm / PyTorch environment for the Drop7 lifetime-objective work.
#
#   source approaches/lifetime-objective/gpu/activate.sh
#
# Machine: AMD Ryzen AI MAX+ 395 "Strix Halo", integrated Radeon 8060S
#          (RDNA 3.5, gfx1151), unified system memory (no dedicated VRAM).
#
# THE TWO THINGS THAT MATTER
# --------------------------
# torch 2.13.0+rocm7.1 bundles its own copies of the ROCm userspace libraries
# under torch/lib/. Two of them misbehave on this host:
#
#   1. libhsa-runtime64.so -- the bundled ROCr SEGFAULTS while creating the
#      first HSA hardware queue, i.e. on the very first .cuda() tensor
#      operation. FIX: LD_PRELOAD the system ROCr (ROCm 7.13) below. This is
#      safe because ROCr is a leaf library -- it pulls in no other ROCm
#      component -- so interposing it does not duplicate the HIP runtime.
#      Without this preload, every GPU operation dies with SIGSEGV.
#
#   2. libMIOpen.so -- the bundled MIOpen selects a GFX9-only solver for the
#      BatchNorm *training* kernel and emits inline asm
#      ("v_add_f32 ... row_bcast:31") that the gfx1151 assembler rejects, so
#      any training step through a BatchNorm layer fails with
#      miopenStatusUnknownError. Eval/inference mode is unaffected, which makes
#      this very easy to miss. There is NO env-var fix: preloading the system
#      MIOpen is NOT a valid workaround (see the note further down).
#      FIX: use GroupNorm instead of BatchNorm (bench.py --norm group, the
#      default). GroupNorm is a native PyTorch kernel and never enters MIOpen.
#
# HSA_OVERRIDE_GFX_VERSION is deliberately NOT set. gfx1151 is natively present
# in torch.cuda.get_arch_list() for this wheel; an override fixes neither issue
# above and only risks running mismatched code objects.

# --- resolve repo root from this script's location ----------------------------
_d7_src="${BASH_SOURCE[0]:-$0}"
_D7_GPU_DIR="$(cd "$(dirname "$_d7_src")" && pwd)"
_D7_ROOT="$(cd "$_D7_GPU_DIR/../../.." && pwd)"

_D7_ROCM="${ROCM_PATH:-/opt/rocm}"

# Venv discovery. The venv does NOT have to live inside the repository -- and
# preferably should not, because `make research-validate` walks the tree and
# trips over the README files bundled inside site-packages. Resolution order:
#   1. $D7_VENV, if the caller sets it (always wins);
#   2. the first candidate below that exists.
# Relocating the venv therefore needs no edit to this file: either export
# D7_VENV, or move it to one of the out-of-tree locations below.
_d7_pick_venv() {
    [ -n "${D7_VENV:-}" ] && { printf '%s' "$D7_VENV"; return; }
    local c
    for c in "$_D7_ROOT/.venv-rocm" \
             "${XDG_DATA_HOME:-$HOME/.local/share}/drop7/venv-rocm" \
             "$HOME/.venvs/drop7-rocm" \
             "$(dirname "$_D7_ROOT")/drop7-venvs/.venv-rocm"; do
        [ -x "$c/bin/python" ] && { printf '%s' "$c"; return; }
    done
    printf '%s' "$_D7_ROOT/.venv-rocm"   # fall through: report this one as missing
}
_D7_VENV="$(_d7_pick_venv)"

# --- REQUIRED: system ROCr overrides torch's bundled copy ---------------------
_d7_add_preload() {
    # $1 = soname candidates (first existing wins), $2 = what breaks without it
    local found="" c
    for c in $1; do [ -e "$c" ] && { found="$c"; break; }; done
    if [ -z "$found" ]; then
        echo "activate.sh: WARNING: none of [$1] found under $_D7_ROCM." >&2
        echo "             Without it: $2" >&2
        return 1
    fi
    case ":${LD_PRELOAD:-}:" in
        *":$found:"*) : ;;
        *) export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$found" ;;
    esac
}
_d7_add_preload \
    "$_D7_ROCM/lib/libhsa-runtime64.so.1 $_D7_ROCM/lib/libhsa-runtime64.so" \
    "every .cuda() operation segfaults (bundled ROCr cannot create an HSA queue)."
# NOTE: do NOT also preload $ROCM_PATH/lib/libMIOpen.so.1 to work around the
# BatchNorm bug. Unlike ROCr, MIOpen is not a leaf library: it pulls in the
# system libamdhip64.so.7, so the process ends up with TWO HIP runtimes and two
# device contexts mapped at once. That corrupts the heap
# ("malloc_consolidate(): invalid chunk size") and segfaults nondeterministically
# a few training steps in. Use a norm layer MIOpen does not own instead --
# see D7_NORM in bench.py.

export ROCM_PATH="$_D7_ROCM"
export HIP_PATH="$_D7_ROCM"
export PATH="$_D7_ROCM/bin:$PATH"

# --- allocator ---------------------------------------------------------------
# Unified memory APU: the "GPU" pool IS system RAM. expandable_segments keeps
# the caching allocator from pinning a large high-water mark that the host then
# cannot use. Raise the split threshold so big activation buffers are reused.
export PYTORCH_HIP_ALLOC_CONF="${PYTORCH_HIP_ALLOC_CONF:-expandable_segments:True,max_split_size_mb:512}"

# --- MIOpen ------------------------------------------------------------------
# Persist the tuned-kernel database next to the venv, so a cold first run is
# paid once per checkout rather than once per user session. It lives INSIDE
# .venv-rocm/ deliberately: `python -m venv` writes a ".gitignore" containing
# "*" at the venv root, so anything under it is already excluded from git and
# no change to the repository .gitignore is required.
export MIOPEN_USER_DB_PATH="${MIOPEN_USER_DB_PATH:-$_D7_VENV/miopen-cache}"
export MIOPEN_CUSTOM_CACHE_DIR="$MIOPEN_USER_DB_PATH"
mkdir -p "$MIOPEN_USER_DB_PATH" 2>/dev/null || true

# --- CPU threading -----------------------------------------------------------
# 16 physical cores / 32 threads. Default OMP to the physical core count; going
# past it costs more than it gains for this model size. Override by exporting
# OMP_NUM_THREADS before sourcing.
export OMP_NUM_THREADS="${OMP_NUM_THREADS:-16}"

# CORRECTNESS, NOT TUNING. The OpenBLAS build shipped with numpy (and with the
# AMD "TheRock" torch wheels) has a threading race in its fp32 SGEMM kernel on
# this CPU: at >= 4 threads roughly 0.5% of output elements come back as
# non-deterministic garbage, with a different wrong answer on every run. fp64
# GEMM is bit-exact and 1-2 threads are clean, so this is a kernel race, not
# faulty hardware. Capping OpenBLAS to one thread makes numpy fp32 linear
# algebra trustworthy again. It does NOT slow down torch on the pytorch.org
# wheel, which uses its own oneDNN CPU GEMM (unaffected, and still threaded
# across all cores). Verify with: bench.py --correctness.
export OPENBLAS_NUM_THREADS="${OPENBLAS_NUM_THREADS:-1}"

# --- virtualenv --------------------------------------------------------------
if [ -f "$_D7_VENV/bin/activate" ]; then
    # shellcheck disable=SC1091
    . "$_D7_VENV/bin/activate"
else
    echo "activate.sh: venv not found at $_D7_VENV" >&2
    echo "  create it with:" >&2
    echo "    /usr/bin/python3.13 -m venv $_D7_VENV" >&2
    echo "  or point at an existing one:  export D7_VENV=/path/to/venv" >&2
    echo "    $_D7_VENV/bin/python -m pip install --upgrade pip" >&2
    echo "    $_D7_VENV/bin/python -m pip install \\" >&2
    echo "        --index-url https://download.pytorch.org/whl/rocm7.1 torch torchvision" >&2
fi

if [ -z "${D7_GPU_QUIET:-}" ]; then
    echo "drop7 ROCm env ready"
    echo "  venv        : $_D7_VENV"
    echo "  ROCM_PATH   : $ROCM_PATH"
    echo "  LD_PRELOAD  : ${LD_PRELOAD:-<none>}"
    echo "                ^ required; without it every .cuda() call segfaults"
    echo "  alloc conf  : $PYTORCH_HIP_ALLOC_CONF"
    echo "  OMP threads : $OMP_NUM_THREADS"
    echo "  benchmark   : python $_D7_GPU_DIR/bench.py --all"
fi

unset -f _d7_add_preload _d7_pick_venv
unset _d7_src _D7_ROCM _D7_VENV
