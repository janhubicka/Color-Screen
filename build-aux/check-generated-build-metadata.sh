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

# A source distribution must contain every local Qt GUI header and the resource
# collection. Automake cannot infer #include dependencies for EXTRA_DIST, so a
# missing header can leave normal in-tree builds green while a release tarball
# fails to build elsewhere.
extra_dist=$(
  awk '
    /^EXTRA_DIST[[:space:]]*=/ {
      in_extra = 1
      sub(/^[^=]*=[[:space:]]*/, "")
    }
    in_extra {
      continued = ($0 ~ /\\[[:space:]]*$/)
      gsub(/\\/, "")
      for (i = 1; i <= NF; ++i)
        print $i
      if (!continued)
        exit
    }
  ' "$am"
)

generated_extra_dist=$(
  awk '
    /^EXTRA_DIST[[:space:]]*=/ {
      in_extra = 1
      sub(/^[^=]*=[[:space:]]*/, "")
    }
    in_extra {
      continued = ($0 ~ /\\[[:space:]]*$/)
      gsub(/\\/, "")
      for (i = 1; i <= NF; ++i)
        print $i
      if (!continued)
        exit
    }
  ' "$generated"
)

dist_missing=
generated_dist_missing=
for path in src/qtgui/*.h src/qtgui/resources.qrc; do
  name=${path##*/}
  if ! printf '%s\n' "$extra_dist" | grep -Fxq "$name"; then
    dist_missing="$dist_missing $name"
  fi
  if ! printf '%s\n' "$generated_extra_dist" | grep -Fxq "$name"; then
    generated_dist_missing="$generated_dist_missing $name"
  fi
done

if test -n "$dist_missing"; then
  echo "error: $am EXTRA_DIST is missing Qt GUI distribution file(s):$dist_missing" >&2
  exit 1
fi

if test -n "$generated_dist_missing"; then
  echo "error: $generated is stale; EXTRA_DIST misses Qt GUI file(s):$generated_dist_missing" >&2
  echo "Run 'autoreconf -fiv' and commit the regenerated $generated." >&2
  exit 1
fi

source_count=$(printf '%s\n' "$sources" | wc -l | tr -d ' ')
dist_count=$(printf '%s\n' "$extra_dist" | wc -l | tr -d ' ')
echo "Qt GUI build metadata is current ($source_count sources; $dist_count distributed inputs)."
