#pragma once
// The Witness's constants for the shared invite and discovery code (common/VENDORED.md): include the shared headers only through this file,
// so every one of them sees these values. Invite prefix WIT, probe game id "WIT " (standard B, C), the lobby's default port, the link handler Setup registers.
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
