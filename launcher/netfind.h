/* Finding each other and hosting checks on Windows (co-op standard B, C), for the launcher (main.cpp): a C file because the shared
 * Windows headers it uses (common/coop_discover_win.h, common/launcher_host_win.h, vendored from MDK2) are C.
 *   Join list   a worker scans while the list is shown (B6: at once, then every 5 s; nothing while hidden): every adapter's directed broadcast
 *               (B1), online Tailscale peers (B2), recent hosts (B4), on the lobby port and the default, each with the next three.
 *   invite      every route of an invite at once, the first answer, a better route within 300 ms instead (C4).
 *   recents     remembered after each successful join (recents.tsv in the mod's user data folder).
 *   host        "Friends can join by" (C1): Steam, Same network (this PC's lobby asked on its own LAN address), Tailscale, Internet (the router
 *               port by UPnP-IGD or NAT-PMP while the lobby is open, CGNAT check, C2), Windows Firewall (read only).
 * Wire formats and decisions are the shared headers' (their T0 tests: tools/test_coop_*.py); main.cpp draws. MIT licence. */
#ifndef WITNESS_NETFIND_H
#define WITNESS_NETFIND_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include "../common/coop_witness.h"
#ifdef __cplusplus
extern "C" {
#endif

struct nf_settings { int find_lan, find_tailscale, upnp, internet_in_invite; unsigned port; };   /* Settings > Network; port: the lobby port */
extern void (*nf_log)(const char *line);

/* ---- Join list ---- */
void nf_join_show(int shown, const struct nf_settings *s, const char *our_version);   /* every UI tick: is the list on screen */
struct nf_row { char name[CI_NAME_MAX + 1], version[CD_VERSION_MAX + 1], line[200]; int state, players, limit, mismatch, route, rtt, full; uint32_t ip; unsigned port; };
int nf_join_rows(struct nf_row *out, int max);          /* shown order (lj_order) */
void nf_join_sources(char *out, size_t cap);            /* "Network 1 (done) - Tailscale off - Recent 0 (done) - Steam: invites" */
int nf_join_source_states(int *states, int *found);     /* LJ_SRCS entries (tests) */
int nf_join_scans(void);                                /* scans finished (tests) */

/* ---- recents (B4) ---- */
void nf_recents_load(const wchar_t *path);
void nf_recent_add(const char *name, uint32_t ip, unsigned port, int recent_route);       /* CD_R_*; saved at once */
int nf_recents_count(void);

/* ---- C4: an invite's routes at once ---- */
void nf_invite_probe_start(const struct ci_invite *v);
int nf_invite_probe_result(uint32_t *ip, unsigned *port, int *route);   /* -1 running, 0 nobody answered, 1 found */

/* ---- hosting (C1, C2) ---- */
enum { NF_STEAM, NF_LAN, NF_TAILSCALE, NF_INTERNET, NF_FIREWALL, NF_ROWS };
enum { NF_CHECKING, NF_OK, NF_WARN, NF_OFF };
struct nf_host_row { int state; char title[24], result[200], fix[240]; };
void nf_host_open(unsigned lobby_port, unsigned game_port, int steam, int invite_only, const struct nf_settings *s, const char *launcher_exe, const char *game_exe,
                  uint32_t advertise_ip);   /* advertise_ip: "Friends reach this PC at" (0: this PC's own addresses) */
void nf_host_close(void);                               /* removes the router mappings; the launcher closing calls it too */
int nf_host_rows(struct nf_host_row *out);              /* NF_ROWS rows */
/* the routes an invite may carry: LAN addresses (best first), this PC's Tailscale address, the internet address and whether it may go in */
int nf_host_routes(struct lh_addr *lan, int max, int *nlan, uint32_t *tailscale_ip, uint32_t *internet_ip, int *internet_ok);
void nf_host_dump(char *out, size_t cap);               /* "hostcheck ..." lines for the test stat file */

#ifdef __cplusplus
}
#endif
#endif
