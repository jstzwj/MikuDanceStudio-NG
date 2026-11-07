# CPU/Null MME schedule fixtures

These original examples are dedicated to the public domain under CC0-1.0.
They contain no external model or texture assets and no RHI-specific annotations.

- `Material.fx`: ordinary material, texture/subset selection and an empty edge technique.
- `Child.fx`: half-size offscreen child with a material DefaultEffect and explicit automatic mip chain.
- `OuterScene.fx`: shared parent target, descendant assignment, capture and two Buffer passes in a loop.
- `InnerScene.fx`: second scene carrier nested inside the outer capture.
- `ReaderMaterial.fx`: reader-only shared reference to the completed parent target.

`mme_schedule_test` loads the sources through the production frontend and uses
generated object records to assert the complete sequence and replay its resource
dependencies through Null. It writes `mme-schedule.json` for cross-platform comparison.
These tests qualify scheduling only; they do not claim shader execution or editor integration.
See [the supported contract and reproduction commands](../../../docs/MME_SCHEDULER.md).
