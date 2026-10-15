#!/bin/sh
set -eu
DATE=${1:-12302019}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/data"
mkdir -p "$DIR"
BASE="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH"
NAME="$DATE.NASDAQ_ITCH50.gz"
GZ="$DIR/$NAME"
WANT=""
if curl -fsSL "$BASE/$NAME.md5sum" -o "$GZ.md5sum" 2>/dev/null; then
    WANT=$(awk '{print $1}' "$GZ.md5sum")
else
    rm -f "$GZ.md5sum"
    echo "no md5sum published for $NAME, will verify with gzip -t"
fi
verify() {
    if [ -n "$WANT" ]; then
        HAVE=$(md5 -q "$GZ")
        [ "$HAVE" = "$WANT" ]
    else
        gzip -t "$GZ"
    fi
}
if [ -f "$GZ" ] && verify; then
    echo "already present and verified: $GZ"
    exit 0
fi
echo "downloading $NAME"
curl -fL --retry 3 -C - -sS "$BASE/$NAME" -o "$GZ"
if verify; then
    echo "verified $GZ"
else
    echo "verification failed for $GZ" >&2
    exit 1
fi
