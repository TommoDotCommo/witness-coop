# Vendored from MDK2

These files come from the MDK2 co-op mod (`bridge/`, branch integrate/standard-20261103, commit fc592db5b271).
Changes are made there first, with tests and mutants, and then copied here again. Do not edit the copies on their own.
They are MIT-licensed like the rest of this repository.

| File | What | Change here |
|---|---|---|
| `coop_invite.h` | invite code v1, paste box recogniser, clipboard finder, invite wording | none |
| `coop_discover.h` | probe v2 (CPQ2/CPS2), candidates, rows, Tailscale status parser, recents store | none |
| `coop_discover_win.h` | Windows adapters, `tailscale status --json`, the UDP prober | none |
| `launcher_join.h` | the Join list model: host answer, scan candidates, sources, rows, failures, invite routes at once, recents | game constants are `#ifndef` defaults |
| `launcher_host_core.h` | router wire formats (NAT-PMP, UPnP-IGD), CGNAT, firewall verdicts, invite routes | the mapping's name is an `#ifndef` default |
| `launcher_host_win.h` | router mapping and the read-only firewall check on Windows | none |
| `tests/*` | their T0 tests (`launcher_profiles.h` only for the CPQ1 check of `coop_discover_test.c`) | none |

The Witness sets its constants in `coop_witness.h` (game id `WIT `, prefix `WIT`, port 47100, `witness-coop://`) and includes the
shared headers only through it. Tests: `python3 tools/test_coop_discovery.py`, `python3 tools/test_coop_join.py`,
`python3 tools/test_coop_host.py` (each `--quick` for fewer fuzz rounds), `python3 tools/test_coop_witness.py` (the Witness glue).
