#!/bin/sh
# Generate flac.h — a single-file header library wrapping foxen-flac.
# Usage: ./make_flac_h.sh
# Output: flac.h

set -e

OUT=flac.h

{
cat <<'EOF'
/* flac.h — single-file header library generated from foxen-flac.
 *
 * Usage:
 *   #include "flac.h"               // declarations only
 *   #define FLAC_IMPLEMENTATION
 *   #include "flac.h"               // also compile the implementation
 *
 * See foxen-flac.h for the full API documentation.
 */
EOF

# --- header (declarations) ---
cat foxen-flac.h

# --- implementation (compiled only when FLAC_IMPLEMENTATION is defined) ---
cat <<'EOF'

#ifdef FLAC_IMPLEMENTATION
EOF

# foxen-flac.c #includes foxen-flac.h itself; drop that line to avoid re-inclusion.
sed 's|#include "foxen-flac.h"||' foxen-flac.c

cat <<'EOF'
#endif /* FLAC_IMPLEMENTATION */
EOF

} > "$OUT"

echo "Generated $OUT"
