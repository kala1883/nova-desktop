# SSH chunk protocol

The Windows engine is `src/platform/sync_*.c`, the native UI is
`src/ui/sync_manager.c`, and portable bounded rules are in `src/core/sync_model.c`.
The broker remains compatible with ChunkSync v2.

A dedicated host-pinned libssh2 connection opens a non-PTY exec channel. A fixed
shell bootstrap reads octal-encoded project/root paths from stdin, decodes them
as data and executes `python3 -B sync_server.py`. Paths are not interpolated into
shell syntax. This is a trusted single-user SSH account, not a multi-tenant service;
node identity alone is not authentication. No local HTTP listener is involved.

Each frame starts with two big-endian uint32 values (JSON length, payload length),
followed by UTF-8 JSON and payload. Limits: 16 MiB JSON and 128 KiB payload. Zlib payloads
carry `_raw_size` and `_encoding: "zlib"`. Responses contain `ok` plus `result`
or `error`. Identity headers are `device` (A–P, allocated automatically) and `node_id`.
Existing A/B identities and chunksync-v2 manifests remain valid.

Actions: register/ping/status, dataset_create, bind/unbind, prepare/put/commit,
pending/get/ack. Bindings record local paths as metadata only. Native prepare
requests include `native: true` and may omit `path_key`; the server validates,
casefolds the path and returns the canonical manifest. Legacy requests retain
their response shape.

`chunksync-v2` manifests include immutable ID, dataset, origin, relative path,
path_key, parent version, PUT/DELETE kind, size, mtime_ns, whole SHA-256 and ordered
hash/size chunks. Canonical JSON uses sorted keys, compact separators and raw
UTF-8 Unicode; `mhash` is SHA-256 of these bytes. Gear[i] is the first eight SHA-256
bytes of byte i, little-endian. For each input byte, unsigned `h=(h<<1)+gear[b]`;
cut at 2 MiB or at 128 KiB and above when `(h & ((1<<19)-1)) == 0`, then reset h.
Retain the final nonempty block.

Immutable outbox manifests and snapshots survive source edits and network loss.
Upload offsets belong to the broker; download partials are flushed and keyed by
hash. The receiver validates lineage and current content, reconstructs an adjacent
temporary file, verifies SHA-256 and atomically replaces the target. Disk SQLite
journals DOWNLOADING → READY → APPLIED before ack. APPLIED retries recheck retained
chunks and ack without overwriting subsequent edits. Outbox retries finish before
receiving and scanning, reusing IDs for uncertain operations.

Receipts advertise the receiver's durable chunk inventory. Only missing chunks
occupy cloud storage. Ack collects unreferenced objects and retains metadata.
Quota/free-space and metadata bounds reject allocations without discarding an
unreceived version. Clients must preserve their identities and durable cache.
Automatic local cache pruning, client replacement, metadata compaction,
device replacement and arbitrary historical checkout are outside this version's scope.

## Embedded administration protocol

The EXE embeds sync_server.py plus common/wire/broker/control/__init__ modules as
RCDATA. A fixed Python -c bootstrap reads one base64 source line from stdin and
serves the same bounded JSON/binary frames. All project/queue parameters remain
JSON data. probe, install_begin/put/commit, export_begin/info/get/commit/abort and
import_begin/put/prepare/activate are administrator operations on that SSH account.
Immutable release source is verified before an atomic active-version pointer.

A lock outside the replaceable queue protects each broker request and control
transition. Source ACTIVE → EXPORTING → REDIRECT, destination ACTIVE(empty) →
IMPORTING → PREPARED → ACTIVE. The native coordinator persists a recovery token
before freeze, transfers SHA-verified files with offsets, verifies SQLite, commits
source redirect, activates target, then commits the local server selection.
Restore is permitted only before redirect. Node IDs, queue versions and the
receiver inventory remain identical; target instances retain source ancestry.
Normal replies can include structured moved/frozen errors and a destination.
The peer follows through its own login/host verification; no secrets are routed.

## Publication, inspection and conflict choices

`register` with `automatic:true` retains the node's existing role or allocates a
free role up to 16. Additive `publishers` and `recipients` tables retain directory
origin and per-version intended receivers; upgrades happen transactionally under
the ACTIVE gate. Acks reclaim chunks only after all recipients acknowledge, with
at least one subscriber. Unbinding never discards pending receipts.

Binding a late subscriber adds it to historical recipients and reactivates
DELIVERED versions. `overview` returns current file heads and whether all of that
receiver's missing lineage is available. `repairs` lists source-owned versions
with missing required chunks; `repair_prepare` stages them from the source cache
without changing the immutable version or file head. Existing receivers' durable
inventories still avoid unnecessary retransmission.

Native CHECK builds a bounded temporary SQLite inspection table using hashes and
never binds/publishes/applies work files. PUSH resumes outgoing work and scans;
PULL receives only. Local/server resolution is an explicit per-file request with
the inspected local SHA and remote version. It saves a verified recovery copy,
receives and acknowledges lineage, then either applies the server version or
publishes the retained local content as a child. Local `versions` stores replaced
manifests; `files/outbox/inbox` remain compatible. Interrupted choices preserve
checkpoints; stale content or a changed pending head refuses the choice.

Configure-server bootstrap may install remote Python using fixed package-manager
commands only when the authenticated SSH account already has uid 0. Package output
uses stderr; paths and source remain stdin data. There is no sudo, local subprocess,
or implicit installation during probes.
