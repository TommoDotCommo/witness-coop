/* Portable read-only setup decisions. No filesystem or platform dependencies.
 * Shared with the installer's PowerShell through install-policy.exe (one integer on stdout). */
#ifndef COOP_INSTALL_POLICY_H
#define COOP_INSTALL_POLICY_H
#include <string.h>
#include <stddef.h>
enum ci_state { CI_CURRENT, CI_MISSING, CI_OUTDATED, CI_MODIFIED, CI_CONFLICT, CI_UNKNOWN, CI_UNMANAGED };
/* Empty strings mean absent; callers distinguish read failures with unknown. */
static int ci_file(const char *expected, const char *observed, int schema,
                   const char *installed, const char *original, const char *pending,
                   const char *backup, int unknown)
{
    if (unknown || !*expected) return CI_UNKNOWN;
    if (schema) {
        if (schema != 1 || !*installed) return CI_UNKNOWN;
        if (*original && strcmp(backup, original)) return CI_CONFLICT;
        if (!*original && *backup) return CI_CONFLICT;
        if (!*observed) return CI_MISSING;
        if (!strcmp(observed, expected)) return CI_CURRENT;
        if (!strcmp(observed, installed) || (*pending && !strcmp(observed, pending))) return CI_OUTDATED;
        if (*original && !strcmp(observed, original)) return CI_MISSING;
        return CI_MODIFIED;
    }
    if (*backup) return CI_CONFLICT;
    if (!*observed) return CI_MISSING;
    /* Identical unowned files still need a receipt before uninstall is safe. */
    return CI_UNMANAGED;
}
/* Low nibble: no action=0, Install=1, Update=2, Repair=3.
 * 16: safe Uninstall. 32: blocked. Inspection-only rows do not enter this mask. */
static int ci_actions(unsigned states, int owned)
{
    int action = 0;
    if (!states || (states & ~127u) || (states & ((1u << CI_MODIFIED) | (1u << CI_CONFLICT) | (1u << CI_UNKNOWN)))) return 32;
    if (!owned && (states & ((1u << CI_MISSING) | (1u << CI_UNMANAGED)))) action = 1;
    else if (states & ((1u << CI_MISSING) | (1u << CI_UNMANAGED))) action = 3;
    else if (states & (1u << CI_OUTDATED)) action = 2;
    return action | (owned ? 16 : 0);
}
#endif
