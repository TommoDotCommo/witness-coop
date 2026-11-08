#pragma once
// The Witness's constants for the shared invite and discovery code (common/VENDORED.md): include the shared headers only through this file,
// so every one of them sees these values. Invite prefix WIT, probe game id "WIT " (standard B, C), the lobby's default port, the link handler Setup registers.
#if defined(LAUNCHER_JOIN_H) || defined(LAUNCHER_HOST_CORE_H)
#error "include coop_witness.h before the shared headers: they would take MDK2's constants"
#endif
#define LJ_DEFAULT_PORT 47100u
#define LJ_PROBE_ID "WIT "
#define LJ_GAME "WIT"
#define LJ_SCHEME "witness-coop://"
#define LH_MAPPING_NAME "The Witness Co-op"
#define WC_PRODUCT "The Witness"
#define WC_JOIN_PAGE "tommodotcommo.github.io/coop-mod-kit/join"
#include "coop_invite.h"
#include "coop_discover.h"
#include "launcher_join.h"
#include "launcher_host_core.h"

/* A lobby that turned invite only: the address and port of a listed row now answer flagged and nameless, so that lobby is no longer open and its
 * row's routes from that address go at once (cd_expire removes a row with no route left) instead of fading for CD_STALE_MS (standard B9; owner, 2026-11-08).
 * Returns how many routes were dropped. Called by the Join list for every answer, before cd_seen (which ignores the flagged answer itself). */
static inline int wc_forget_invite_only(struct cd_list *l, const struct cd_reply *rep, uint32_t ip, unsigned port)
{
    int i, k, n = 0;
#ifdef JG_MUT_KEEP_INVITE_ONLY_ROW
    return 0;       /* mutant: the open row stays until it is stale */
#endif
    if (!(rep->flags & CD_F_INVITE_ONLY)) return 0;
    for (i = 0; i < l->n; i++) {
        struct cd_row *r = &l->r[i];
        if (r->group >= 0 || r->port != port) continue;
        for (k = 0; k < CD_ROUTES; k++) if (r->has[k] && r->ip_by[k] == ip) { r->has[k] = 0; n++; }
    }
    return n;
}
