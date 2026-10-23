#ifndef N64_BOOT_H
#define N64_BOOT_H

// Must be the first thing main() calls after the debug log is up.
// Shows an error screen and never returns if the Expansion Pak is missing.
void boot_require_expansion_pak(void);

#endif
