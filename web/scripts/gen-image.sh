#!/usr/bin/env bash
# Generate a raster image for the console through the Codex CLI's image tool.
#
#   web/scripts/gen-image.sh <output.png> "<image prompt>"
#
# Most artwork on this site is inline SVG drawn by hand, because it has to
# animate, read a design token and stay honest about the research. Reach for
# this only where a raster is the right answer: a share card's background, an
# illustration on a learn page, a texture. Style guidance and the house prompt
# recipe live in .agents/skills/drop7-social-cards/SKILL.md.
#
# One run takes a minute or two and spends the user's Codex quota. Look at the
# result before regenerating, and stop after about three attempts per image.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <output.png> \"<image prompt>\"" >&2
  exit 2
fi

out="$1"
prompt="$2"

command -v codex >/dev/null 2>&1 || {
  echo "error: codex CLI not found (https://developers.openai.com/codex/cli)" >&2
  exit 1
}
codex features list 2>/dev/null | grep -E '^image_generation[[:space:]].*[[:space:]]true$' >/dev/null || {
  echo "error: the codex image_generation feature is disabled." >&2
  echo "enable it with: codex features enable image_generation" >&2
  exit 1
}

mkdir -p "$(dirname "$out")"
dir=$(cd "$(dirname "$out")" && pwd)
name=$(basename "$out")

codex exec -s workspace-write --skip-git-repo-check -C "$dir" \
  "Use your image generation tool to generate this image and save it as $name in the current directory: $prompt

Just generate and save the image; do not write any code or any other files."

if [ ! -f "$dir/$name" ]; then
  echo "error: codex finished but $dir/$name was not created" >&2
  exit 1
fi

# Size and transparency report, so the caller can judge the result without opening it.
node -e '
const sharp = require("sharp");
const file = process.argv[1];
sharp(file).metadata().then((meta) => {
  const kb = (require("node:fs").statSync(file).size / 1024).toFixed(0);
  console.log(`generated: ${file} — ${meta.width}x${meta.height} ${meta.format}, ${kb} KB, alpha ${meta.hasAlpha ? "yes" : "no"}`);
}).catch(() => {});
' "$dir/$name" 2>/dev/null || true
