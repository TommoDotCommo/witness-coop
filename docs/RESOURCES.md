# Optional QA resource provider

Store personal configuration in ignored `local/env.sh`, or export environment
variables. `PYW` selects Windows Python; `WITNESS_DIR1/2`, `WITNESS_PORT1/2` and
`WITNESS_RUN` select disposable copies, distinct UDP endpoints and a private run
folder. Never point these at an original installation or another session's copies.

For automated allocation, set `WITNESS_RESOURCES` to an executable implementing:

- `acquire --slots N --ttl SECONDS --wait SECONDS`: reserve instances; exit 3 if busy.
- `provision witness`: prepare only the current owner's disposable copies.
- `env witness`: print shell-quoted exports for WITNESS_DIR1..N, WITNESS_PORT1..N,
  WITNESS_N, WITNESS_RUN and comma-separated WITNESS_SLOTS.
- `register-pid PID`: record the launched process with image and creation time.
- `release`: stop only registered matching processes and free this owner's instances.
- `rect SLOT`: print x, y, width and height for non-activating window placement.
- `status`: report allocation and a `RAM free NUMBER` line in GiB for preflight checks.

`WITNESS_RESOURCE_OWNER` is an opaque, unique session identity. Shell wrappers
create one when absent; parallel wrappers derive distinct shard owners and clear
inherited instance paths. Do not run two independent jobs under the same owner.
The provider must reject unsafe reuse and must never kill processes by image name.
QA entry points release their allocations in EXIT traps. Busy means do game-free
work and retry; do not silently fall back to shared game copies.

Without a provider, configure separate copies and run folders explicitly. Parallel
group tests require a provider. Build and game-free tests do not require one.
