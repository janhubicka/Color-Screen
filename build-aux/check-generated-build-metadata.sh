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

# Application-owned dialogs, message boxes and popup menus must not enter
# secondary event loops. A stack QObject parented to a document/window can be
# destroyed by its parent while exec() is still unwinding. Keep continuations
# asynchronous and receiver-bound instead. QApplication's top-level loop and
# QDrag's platform drag loop are the two intentional exceptions.
secondary_loops=$(
  grep -nHE '([.]|->)exec[[:space:]]*[(]' src/qtgui/*.cpp \
    | grep -Ev 'drag->exec[[:space:]]*[(]|app[.]exec[[:space:]]*[(]' \
    || true
)
if test -n "$secondary_loops"; then
  echo "error: unexpected secondary event loop in Qt GUI source:" >&2
  echo "$secondary_loops" >&2
  echo "Use open()/popup() with receiver-bound continuations instead." >&2
  exit 1
fi

# Contact Copy is a complete stable-parameter-key migration. Its H&D curve is
# a compound saved value, but each direct editing surface/coordinate is a
# separate user gesture and therefore has its own merge identity. Keep this
# deterministic source check beside the other cheap Qt GUI invariants so an
# accidental rollback is caught before the expensive sanitizer builds.
contact_copy_source=src/qtgui/ContactCopyPanel.cpp
contact_copy_keys='contact_copy.simulate
contact_copy.curve.graph
contact_copy.curve.min.x
contact_copy.curve.min.y
contact_copy.curve.linear1.x
contact_copy.curve.linear1.y
contact_copy.curve.linear2.x
contact_copy.curve.linear2.y
contact_copy.curve.max.x
contact_copy.curve.max.y
contact_copy.curve.richards.min_density
contact_copy.curve.richards.max_density
contact_copy.curve.richards.slope
contact_copy.curve.richards.offset
contact_copy.curve.richards.asymmetry
contact_copy.curve.inverse
contact_copy.preflash
contact_copy.exposure
contact_copy.density_boost'

contact_copy_missing=
for key in $contact_copy_keys; do
  if ! grep -Fq "\"$key\"" "$contact_copy_source"; then
    contact_copy_missing="$contact_copy_missing $key"
  fi
done
if test -n "$contact_copy_missing"; then
  echo "error: Contact Copy stable parameter key(s) missing:$contact_copy_missing" >&2
  exit 1
fi

source_count=$(printf '%s\n' "$sources" | wc -l | tr -d ' ')
dist_count=$(printf '%s\n' "$extra_dist" | wc -l | tr -d ' ')
contact_copy_key_count=$(printf '%s\n' "$contact_copy_keys" | wc -l | tr -d ' ')
echo "Qt GUI build/source invariants are current ($source_count sources; $dist_count distributed inputs; $contact_copy_key_count Contact Copy keys)."
