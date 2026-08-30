#!/bin/sh
# Fail fast when checked-in Automake output no longer contains Qt GUI sources.
set -eu

am=src/qtgui/Makefile.am
generated=src/qtgui/Makefile.in

if test ! -f "$am" || test ! -f "$generated"; then
  echo "error: expected $am and $generated" >&2
  exit 1
fi

sources=$(
  awk '
    /^colorscreen_qt_SOURCES[[:space:]]*=/ {
      in_sources = 1
      sub(/^[^=]*=[[:space:]]*/, "")
    }
    in_sources {
      continued = ($0 ~ /\\[[:space:]]*$/)
      gsub(/\\/, "")
      for (i = 1; i <= NF; ++i)
        if ($i ~ /\.cpp$/)
          print $i
      if (!continued)
        exit
    }
  ' "$am"
)

if test -z "$sources"; then
  echo "error: could not read colorscreen_qt_SOURCES from $am" >&2
  exit 1
fi

missing=
for source in $sources; do
  if ! grep -Fq "$source" "$generated"; then
    missing="$missing $source"
  fi
done

if test -n "$missing"; then
  echo "error: $generated is stale; missing Qt GUI source(s):$missing" >&2
  echo "Run 'autoreconf -fiv' and commit the regenerated $generated." >&2
  exit 1
fi

echo "Qt GUI generated build metadata is current ($(printf '%s\n' "$sources" | wc -l | tr -d ' ') sources)."
