// T0 for joinglue.h (the Witness's glue around the shared invite and discovery code): what the paste box recognises and what Join does with each form (B7), old-style invites,
// links, the host's probe answer for "WIT " only (B9: an invite-only lobby is nameless and has no v1 reply), the copied invite (C3), and the Witness's constants.
// Mutants (Makefile JOINGLUE_MUTANTS) must fail it.
#include "joinglue.h"
#include <cstdio>
#include <cstring>

static int fails;
static void check(const char* what, bool ok, const std::string& detail = "") { printf("  %-96s %s  %s\n", what, ok ? "ok" : "FAILED", ok ? "" : detail.c_str()); if (!ok) ++fails; }
static uint32_t IP(unsigned a, unsigned b, unsigned c, unsigned d) { return a << 24 | b << 16 | c << 8 | d; }

int main() {
    // the Witness's constants (common/coop_witness.h)
    check("constants: game id \"WIT \", prefix WIT, port 47100, witness-coop://", !strcmp(LJ_PROBE_ID, "WIT ") && !strcmp(LJ_GAME, "WIT") && LJ_DEFAULT_PORT == 47100u && !strcmp(LJ_SCHEME, "witness-coop://"));
    check("the join code from a secret: 8 hex digits", jg::TokenOf(0x1234ABCDu) == "1234ABCD" && jg::TokenOf(7) == "00000007");

    // a Direct host's invite, decoded by the paste box in every form it arrives in
    lh_addr lan[2] = {{IP(192, 168, 1, 20), 24}, {IP(10, 0, 0, 5), 8}};
    jg::InviteIn in; in.name = "Fox"; in.secret = 0xC0FFEE01u; in.port = 47100; in.lan = lan; in.nlan = 2; in.tailscaleIp = IP(100, 101, 102, 103);
    std::string code, msg; bool made = jg::MakeInvite(in, &code, &msg);
    check("invite: made, WIT- code", made && code.compare(0, 4, "WIT-") == 0, code);
    check("invite: \"Fox is hosting The Witness. Join: https://<join page>/#WIT-...\"", msg == "Fox is hosting The Witness. Join: https://tommodotcommo.github.io/coop-mod-kit/join/#" + code, msg);
    jg::Plan p = jg::PlanPaste(code, 47100);
    check("paste a code: every route tried at once, the secret becomes the join code", p.action == jg::kProbeInvite && p.invite.n == 3 && p.token == "C0FFEE01" && p.invite.port == 47100, p.said);
    check("   routes best first: LAN, LAN, Tailscale", p.invite.route[0].kind == CI_LAN && cd_ip(p.invite.route[0].ip) == IP(192, 168, 1, 20) && p.invite.route[2].kind == CI_VPN);
    check("   it says what it recognised before Join", p.said == "Invite code: 3 routes, port 47100.", p.said);
    jg::Plan pl = jg::PlanPaste("  https://tommodotcommo.github.io/coop-mod-kit/join/#" + code + "\n", 47100);
    check("paste the join page link", pl.action == jg::kProbeInvite && pl.token == "C0FFEE01" && pl.said.compare(0, 11, "Invite link") == 0, pl.said);
    jg::Plan ph = jg::PlanPaste("witness-coop://join/" + code, 47100);
    check("paste a witness-coop:// link", ph.action == jg::kProbeInvite && ph.token == "C0FFEE01", ph.said);
    check("the link's payload from the command line back into the box", jg::PlanPaste(jg::PasteFromLink("join/" + code), 47100).action == jg::kProbeInvite && jg::PasteFromLink(code) == code && jg::PasteFromLink("109775241234567890") == "109775241234567890");
    jg::Plan pm = jg::PlanPaste("whole message: " + msg, 47100);
    check("paste the whole message (the link in it)", pm.action == jg::kProbeInvite && pm.token == "C0FFEE01", pm.said);
    std::string cut = code.substr(0, code.size() - 3);
    jg::Plan pc = jg::PlanPaste(cut, 47100);
    check("a cut code: \"This code is incomplete. Ask for the whole invite again.\"", pc.action == jg::kBad && pc.said == "This code is incomplete. Ask for the whole invite again.", pc.said);
    ci_invite mv; memset(&mv, 0, sizeof mv); mv.secret = 1; mv.port = 27900; mv.n = 1; mv.route[0].kind = CI_LAN; mv.route[0].ip[0] = 10; mv.route[0].ip[3] = 9; char mcode[CI_CODE_MAX]; ci_encode(&mv, "MDK2", mcode, sizeof mcode);
    jg::Plan pw = jg::PlanPaste(mcode, 47100);
    check("an MDK2 code: \"That invite is for MDK2.\"", pw.action == jg::kBad && pw.said == "That invite is for MDK2.", pw.said);

    // a Steam host's invite: the lobby id, no routes
    jg::InviteIn sin; sin.name = "Fox"; sin.secret = 5; sin.port = 47100; sin.steamLobby = 109775241234567890ull;
    std::string scode; check("Steam invite: made without routes", jg::MakeInvite(sin, &scode, nullptr));
    jg::Plan ps = jg::PlanPaste(scode, 47100);
    check("paste a Steam host's code: joins that Steam lobby", ps.action == jg::kSteam && ps.lobby == 109775241234567890ull, ps.said);
    jg::InviteIn adv = in; adv.advertiseIp = IP(127, 0, 0, 1); std::string acode; jg::MakeInvite(adv, &acode, nullptr); jg::Plan pv = jg::PlanPaste(acode, 47100);
    check("\"Friends reach this PC at\": that address is the invite's only route", pv.action == jg::kProbeInvite && pv.invite.n == 1 && cd_ip(pv.invite.route[0].ip) == IP(127, 0, 0, 1) && pv.invite.route[0].kind == CI_LAN, pv.said);
    adv.advertiseIp = IP(100, 70, 1, 2); jg::MakeInvite(adv, &acode, nullptr); pv = jg::PlanPaste(acode, 47100);
    adv.advertiseIp = IP(203, 0, 113, 9); std::string icode; jg::MakeInvite(adv, &icode, nullptr); jg::Plan pi = jg::PlanPaste(icode, 47100);
    check("   a Tailscale address goes as Tailscale, a public one as internet", pv.invite.route[0].kind == CI_VPN && pi.invite.route[0].kind == CI_INTERNET);
    jg::InviteIn none; none.name = "Fox"; none.port = 47100;
    check("no route and no Steam lobby: no invite to copy", !jg::MakeInvite(none, nullptr, nullptr));

    // the other forms
    jg::Plan pn = jg::PlanPaste("109775241234567890", 47100);
    check("a Steam lobby number", pn.action == jg::kSteam && pn.lobby == 109775241234567890ull);
    jg::Plan po = jg::PlanPaste("192.168.1.20:47100/ABC123", 47100);
    check("an old-style invite (address:port/CODE) still joins with its code", po.action == jg::kDirect && po.ip == IP(192, 168, 1, 20) && po.port == 47100 && po.token == "ABC123", po.token);
    jg::Plan pa = jg::PlanPaste("10.0.0.5", 47123);
    check("an address: the lobby port from Settings, no code (a lobby open to my networks)", pa.action == jg::kDirect && pa.port == 47123 && pa.token.empty());
    jg::Plan pp = jg::PlanPaste("10.0.0.5:47200", 47100);
    check("address:port", pp.action == jg::kDirect && pp.port == 47200);
    jg::Plan pt = jg::PlanPaste("fox-pc", 47100), pt2 = jg::PlanPaste("fox-pc.tail1234.ts.net:47101", 47100), pd = jg::PlanPaste("example.org", 47100);
    check("a Tailscale name, a MagicDNS name with a port, a host name: resolved first", pt.action == jg::kResolve && pt.host == "fox-pc" && pt2.port == 47101 && pd.action == jg::kResolve && pt.said == "Tailscale name fox-pc." && pd.said == "Host name example.org.", pt.said + " | " + pd.said);
    check("empty: says what it takes", jg::PlanPaste("   ", 47100).action == jg::kNothing && !jg::PlanPaste("", 47100).said.empty());
    check("nonsense is not recognised", jg::PlanPaste("hello world!", 47100).action == jg::kBad);

    // the host's probe answer
    cd_rate rate; memset(&rate, 0, sizeof rate); uint8_t q[CD_QUERY_LEN], reply[CD_REPLY_MAX]; cd_reply rep;
    jg::HostInfo h; h.name = "Fox"; h.version = "v0.9.0-test"; h.players = 2;
    cd_query(q, 0x11223344u, "WIT ", 0); int n = jg::Answer(&rate, q, sizeof q, IP(10, 0, 0, 9), 1000, h, reply);
    check("probe \"WIT \": answered with the name, version, players of 16, in lobby", n > 0 && cd_parse_reply(reply, (size_t)n, 0x11223344u, "WIT ", &rep) && !strcmp(rep.name, "Fox") && !strcmp(rep.version, "v0.9.0-test") && rep.players == 2 && rep.limit == 16 && rep.state == CD_IN_LOBBY);
    cd_query(q, 5, "MDK2", 0);
    check("probe for MDK2: silent", jg::Answer(&rate, q, sizeof q, IP(10, 0, 0, 9), 1100, h, reply) == 0);
    uint8_t q1[8] = {'C', 'P', 'Q', '1', 1, 2, 3, 4};
    check("CPQ1: still answered (v1)", jg::Answer(&rate, q1, 8, IP(10, 0, 0, 9), 1200, h, reply) > 0 && !memcmp(reply, "CPS1", 4));
    h.phase = 2; cd_query(q, 6, "WIT ", 0); n = jg::Answer(&rate, q, sizeof q, IP(10, 0, 0, 8), 1300, h, reply);
    check("in game: state 1", n > 0 && cd_parse_reply(reply, (size_t)n, 6, "WIT ", &rep) && rep.state == CD_IN_GAME);
    h.inviteOnly = true; cd_query(q, 7, "WIT ", 0); n = jg::Answer(&rate, q, sizeof q, IP(10, 0, 0, 7), 1400, h, reply);
    check("invite only: the answer carries no name (not listed)", n > 0 && cd_parse_reply(reply, (size_t)n, 7, "WIT ", &rep) && !rep.name[0] && (rep.flags & CD_F_INVITE_ONLY));
    check("invite only: no v1 answer", jg::Answer(&rate, q1, 8, IP(10, 0, 0, 7), 1500, h, reply) == 0);
    h.inviteOnly = false; int answered = 0; for (int i = 0; i < 50; ++i) { cd_query(q, 100 + i, "WIT ", 0); answered += jg::Answer(&rate, q, sizeof q, IP(10, 9, 9, 9), 2000, h, reply) > 0; }
    check("a flood from one address: at most 10 answers", answered == 10, std::to_string(answered));
    check("other traffic is not a probe", !jg::IsProbe((const uint8_t*)"WTL1\x01xxxx", 9) && jg::IsProbe(q, sizeof q));

    // the Join list: a listed lobby that turns invite only goes at the next answer, not after CD_STALE_MS (wc_forget_invite_only)
    {
        cd_list l; memset(&l, 0, sizeof l); cd_reply open, inv, other; memset(&open, 0, sizeof open); open.players = 1; open.limit = 16; snprintf(open.name, sizeof open.name, "Fox"); snprintf(open.version, sizeof open.version, "v0.9.0-test");
        inv = open; inv.name[0] = 0; inv.flags = CD_F_INVITE_ONLY; other = open; snprintf(other.name, sizeof other.name, "Owl");
        cd_seen(&l, &open, CD_LAN, IP(10, 0, 0, 5), 47100, -1, 1, 1000, "v0.9.0-test"); cd_seen(&l, &other, CD_LAN, IP(10, 0, 0, 6), 47100, -1, 1, 1000, "v0.9.0-test");
        cd_seen(&l, &open, CD_LAN, IP(10, 0, 0, 5), 47101, -1, 1, 1000, "v0.9.0-test");      // (another lobby of the same name on the next port: a second row)
        check("three open lobbies listed", l.n == 3, std::to_string(l.n));
        check("an open answer drops nothing", wc_forget_invite_only(&l, &open, IP(10, 0, 0, 5), 47100) == 0);
        int dropped = wc_forget_invite_only(&l, &inv, IP(10, 0, 0, 5), 47100); cd_seen(&l, &inv, CD_LAN, IP(10, 0, 0, 5), 47100, -1, 1, 1100, "v0.9.0-test"); cd_expire(&l, 1100);
        bool fox = false, fox2 = false, owl = false; for (int i = 0; i < l.n; ++i) { fox |= !strcmp(l.r[i].name, "Fox") && l.r[i].port == 47100; fox2 |= !strcmp(l.r[i].name, "Fox") && l.r[i].port == 47101; owl |= !strcmp(l.r[i].name, "Owl"); }
        check("Fox's lobby turned invite only: its row is gone at once (and nothing nameless is listed)", dropped == 1 && !fox && l.n == 2, std::to_string(l.n));
        check("the other lobbies (another address, another port) stay", owl && fox2);
    }

    printf(fails ? "FAILED: %d\n" : "ALL OK\n", fails); return fails ? 1 : 0;
}
