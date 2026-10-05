#ifndef PARAMETER_ARCHIVE_H
#define PARAMETER_ARCHIVE_H

#include <string>

namespace colorscreen
{

/* Parsed compatibility information from a Color-Screen parameter archive.  */
struct parameter_archive_manifest
{
  int schema_version = 0;
  std::string legacy_csp_path;
};

/* Return true if NAME starts with a ZIP signature and may therefore be a
   Color-Screen parameter archive.  This is only format dispatch; callers must
   still validate the archive with read_parameter_archive.  */
bool parameter_archive_signature_p (const char *name);

/* Read and validate parameter archive NAME.

   On success copy the schema-v1 legacy CSP payload to LEGACY_CSP and, when
   non-null, parsed manifest information to MANIFEST.  ERROR receives a
   user-facing diagnostic on failure.  No archive entry is extracted to the
   filesystem.  */
bool read_parameter_archive (const char *name, std::string *legacy_csp,
                             parameter_archive_manifest *manifest,
                             std::string *error);

/* Write one schema-v1 archive to NAME.

   LEGACY_CSP is the complete CSP/Qt payload to place in state/legacy.par.
   GENERATOR_VERSION is informational and may be null.  NAME is a staging
   destination: this function may create/truncate it and deliberately does not
   implement the caller's final atomic replacement policy.  ERROR receives a
   diagnostic on failure.  */
bool write_parameter_archive (const char *name, const std::string &legacy_csp,
                              const char *generator_version,
                              std::string *error);

}

#endif
