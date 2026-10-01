#!/usr/bin/env bash
# Upload changed client/assets files to a Switch FTP server (sphaira/ftpd), e.g.
#   tools/sync-switch.sh 10.199.4.120:5000 [--all]
set -euo pipefail
export LC_ALL=C  # comm needs sort and the manifest to agree on byte order
HOST="${1:?usage: sync-switch.sh <ip:port> [--all]}"
cd "$(dirname "$0")/../client/assets"
MANIFEST=".synced-${HOST//[:.]/_}"
[ "${2:-}" = "--all" ] && rm -f "$MANIFEST"
touch "$MANIFEST"
find . -type f ! -name '.synced-*' -printf '%P\n' | sort | xargs -d '\n' md5sum > /tmp/sync-now.$$
comm -13 <(sort "$MANIFEST") <(sort /tmp/sync-now.$$) | cut -c35- > /tmp/sync-todo.$$
echo "$(wc -l < /tmp/sync-todo.$$) file(s) to upload"
fail=0
# ponytail: 4 parallel curl uploads; switch to lftp mirror if this gets slow
xargs -d '\n' -P4 -I{} sh -c 'f="$1"; curl -s --max-time 300 --ftp-create-dirs -T "$f" "ftp://'"$HOST"'/switch/worms4nx/assets/$(printf %s "$f" | sed "s/ /%20/g")" || { echo "FAIL $f"; exit 1; }' _ {} < /tmp/sync-todo.$$ || fail=1
[ $fail = 0 ] && cp /tmp/sync-now.$$ "$MANIFEST"
rm -f /tmp/sync-now.$$ /tmp/sync-todo.$$
[ $fail = 0 ] && echo "synced" || { echo "some uploads failed, rerun to retry"; exit 1; }
