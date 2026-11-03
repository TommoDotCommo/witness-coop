#pragma once
// The Witness's glue around the shared invite and discovery code (common/coop_witness.h, vendored from MDK2), game-free: T0 test launcher/joinglue_test.cpp (with mutants).
//   the lobby's join code from the invite's secret, what the one paste box holds and what Join does with it (B7, old-style invites for one more release),
//   the host's answer to a probe (CPQ1 / CPQ2 "WIT ", rate limited, B9), and the invite the host copies (C3).
#include <cstdint>
#include <cstdio>
#include <string>
#include "../common/coop_witness.h"

namespace jg {
// The lobby's join code (Lobby's token) from the invite's secret: 8 hex digits. The old invites carried a 6-letter code; both are only compared.
inline std::string TokenOf(uint32_t secret) { char b[12]; snprintf(b, sizeof b, "%08X", (unsigned)secret); return b; }

// ---- B7: the paste box
enum Action { kNothing, kProbeInvite, kSteam, kDirect, kResolve, kBad };
struct Plan {
    Action action = kNothing;
    std::string said;           // what the box recognised (or why not), shown before Join
    ci_invite invite{};         // kProbeInvite: every route is tried at once (C4)
    uint64_t lobby = 0;         // kSteam
    uint32_t ip = 0;            // kDirect (host order)
    unsigned port = 0;          // kDirect, kResolve
    std::string host;           // kResolve: a host name or Tailscale name
    std::string token;          // the lobby's join code: the invite's secret, an old-style invite's code, or empty (a lobby open to my networks)
    int recentRoute = CD_R_ADDRESS;
};
inline Plan PlanPaste(const std::string& text, unsigned defaultPort) {
    Plan p; cr_result r; char said[256];
    coop_recognise(text.data(), text.size(), LJ_GAME, &r);
    if (r.kind == CR_ERROR && r.error == CI_NOT_RECOGNISED && text.size() <= CR_INPUT_MAX) {      // the whole copied message ("Fox is hosting ... Join: <link>"): the code in it
        ci_invite v; char name[CI_NAME_MAX + 1];
        if (ci_find_invite(text.data(), text.size(), LJ_GAME, &v, name)) { memset(&r, 0, sizeof r); r.kind = CR_INVITE; r.invite = v; }
    }
    cr_describe(&r, said, sizeof said); p.said = said;
    switch (r.kind) {
    case CR_EMPTY: p.action = kNothing; break;
    case CR_INVITE: case CR_LINK:
        p.invite = r.invite; p.token = TokenOf(r.invite.secret);
        if (r.invite.n > 0) p.action = kProbeInvite;
#ifndef JG_MUT_NO_STEAM_INVITE
        else if (r.invite.flags & CI_FLAG_STEAM) { p.action = kSteam; p.lobby = r.invite.steam_lobby; }
#endif
        else { p.action = kBad; p.said = "This invite has no way to reach the host. Ask for a new invite."; }
        break;
    case CR_STEAM_LOBBY: p.action = kSteam; p.lobby = r.lobby; break;
    case CR_ADDRESS:
        p.action = kDirect; p.ip = cd_ip(r.ip); p.port = r.port ? r.port : defaultPort;
#ifndef JG_MUT_OLD_FORM_DROPPED
        p.token = r.code;       // "address:port/CODE": the invite of earlier versions, still accepted for one release
#endif
        break;
    case CR_HOST: case CR_TAILSCALE: p.action = kResolve; p.host = r.host; p.port = r.port ? r.port : defaultPort; break;
    default: p.action = kBad; break;
    }
    return p;
}
// A witness-coop:// link's payload (core.h InviteFromLink drops the scheme) back into something the paste box reads: a code, an old invite or a lobby number as it is, else a link.
inline std::string PasteFromLink(const std::string& payload) {
    cr_result r; coop_recognise(payload.data(), payload.size(), LJ_GAME, &r);
    if (r.kind != CR_ERROR || payload.empty()) return payload;
    return std::string(LJ_SCHEME) + payload;
}

// ---- the host's answer to a probe: the launcher lobby's UDP socket (lobby_win.cpp); B9: an invite-only lobby is not listed
struct HostInfo { std::string name, version; int phase = 0; unsigned players = 1, limit = 16; bool inviteOnly = false; };
inline int Answer(cd_rate* rate, const uint8_t* q, size_t n, uint32_t fromIp, uint32_t nowMs, const HostInfo& h, uint8_t* reply) {
    cd_host me; memset(&me, 0, sizeof me);
#ifdef JG_MUT_WRONG_GAME_ID
    me.game_id = "MDK2";
#else
    me.game_id = LJ_PROBE_ID;
#endif
    me.version = h.version.c_str(); me.name = h.name.c_str(); me.state = h.phase == 0 ? CD_IN_LOBBY : CD_IN_GAME;
    me.invite_only = h.inviteOnly; me.players = h.players; me.limit = h.limit;
    return lj_host_reply(rate, q, n, fromIp, nowMs, &me, reply);
}
inline bool IsProbe(const uint8_t* q, size_t n) { return lj_is_probe(q, n) != 0; }

// ---- C3: the invite the host copies. Direct: every working route best first, the port, the secret. Steam: the Steam lobby (no routes: a Steam lobby has no UDP listener).
struct InviteIn { std::string name; uint32_t secret = 0; unsigned port = 0; bool inviteOnly = false; const lh_addr* lan = nullptr; int nlan = 0; uint32_t tailscaleIp = 0, internetIp = 0; bool internetOk = false; uint64_t steamLobby = 0; };
inline bool MakeInvite(const InviteIn& in, std::string* code, std::string* message) {
    ci_invite v; memset(&v, 0, sizeof v); char c[CI_CODE_MAX], m[CI_CODE_MAX + 160];
    v.secret = in.secret; v.port = (uint16_t)in.port; v.flags = (uint8_t)(in.inviteOnly ? CI_FLAG_INVITE_ONLY : 0);
    if (in.steamLobby) { v.flags |= CI_FLAG_STEAM; v.steam_lobby = in.steamLobby; }
    else lh_invite_routes(&v, in.lan, in.nlan, in.tailscaleIp, in.internetIp, in.internetOk ? 1 : 0);
    if ((!v.n && !in.steamLobby) || !ci_encode(&v, LJ_GAME, c, sizeof c)) return false;
    if (!ci_message(in.name.empty() ? "Player" : in.name.c_str(), WC_PRODUCT, WC_JOIN_PAGE, c, m, sizeof m)) return false;
    if (code) *code = c;
    if (message) *message = m;
    return true;
}
}  // namespace jg
