# Draft release-note lines: finding each other and the invite (not tagged, not published)

- **Join a friend lists the games around you.** The list fills as soon as you choose Join a friend and refreshes every 5 s while it is on screen: games on every
  network this PC is on, on your Tailscale tailnet (when Tailscale is installed) and among the hosts you joined before, each source named with a tick. A row says
  the host, *in lobby* / *in game* / *full*, the players, how it was reached and the ping; another version is listed greyed with its version. Pick it, press Join.
- **One box for every invite.** Paste the whole message, the join-page link, a `witness-coop://` link, the `WIT-` code, a Steam lobby number, an address or a
  computer name: the box says what it recognised before Join. Old `address:port/CODE` invites still work in this release. An invite on the clipboard is offered
  when the launcher gets the focus (read for that check only, never kept or sent).
- **Copy invite** now copies *Fox is hosting The Witness. Join: https://tommodotcommo.github.io/coop-mod-kit/join/#WIT-...*: every way in that works (LAN,
  Tailscale, the internet address when the router opened the port), the port and the lobby's secret, or the Steam lobby when you host through Steam. A code is tried
  on all its routes at once.
- **Friends can join by** in the lobby: Steam, Same network, Tailscale, Internet, Windows Firewall, each checked, with the fix when one needs a look. While a
  Direct lobby is open the launcher asks the router to open the lobby and game ports (UPnP or NAT-PMP) and removes them when the lobby closes or the launcher
  quits; a provider that shares your address (CGNAT) is named, with Steam or Tailscale as the way out.
- **Who can see my lobby** (Settings, Co-op rules): *Open to my networks* (listed, joinable without a code, the default) or *Invite only* (not listed, the
  invite's code is required). Steam lobbies stay friends-only.
- **Settings, Network**: *Find games on my network*, *Find friends on Tailscale*, *Open the router port while hosting*, *Put my internet address in invites*
  (all on), the lobby port, and *Friends reach this PC at* (now also the invite's only address when set).
- The invite code, the probe and the router and firewall checks are the same code as in the MDK2 co-op mod (common/VENDORED.md); the lobby keeps answering the
  older probe, and Steam hosting and joining are unchanged.

Checks: `python3 tools/test_coop_discovery.py`, `tools/test_coop_join.py`, `tools/test_coop_host.py`, `tools/test_coop_witness.py`, `make -C launcher test lobby-test`
(mutants in each), `tools/qa.sh join` (two real launchers, with a mutant that must fail).
