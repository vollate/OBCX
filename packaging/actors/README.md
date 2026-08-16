# Pinned actor sources

Clean CI and container builds restore their actor repositories from the Git
bundles in `bundles/`; they do not require GitHub credentials or an ignored
`local_actor/` directory. `restore-sources.sh` checks out each fixed archived
revision. Where an actor is archived as an upstream base plus an adaptation
patch, `apply-patches.sh` replays that patch with fixed commit metadata. Bridge
is archived at its merge commit because its actor adaptation and `main` both
form part of the required history. The scripts verify that the resulting
revisions are:

- bridge: `de8c3046c218c9e2a254abe832e91595f4cc629a`
- message store: `d3511ae5950a0e6454458eb763b9947165397d2a`
- actor registry: `ff8a4fecdabd91b2b5e930c39454389bb72109eb`
- actor template: `4bc3c5558a6864d9a067c486a978f943b90cb1f6`

To audit the archived sources without network access:

```sh
OBCX_ACTOR_SOURCE_ROOT=/tmp/obcx-actors \
  sh packaging/actors/restore-sources.sh
```
