# Original actor shadow projection

`updateRetailActorShadow` implements the shadow-local portion of
`Character::updatePosition` at `0x2f434c..0x2f4418` (function entry `0x2f42cd`).
It consumes a genuine retained shadow container, the current original
`LevelObject::GetPosition` result, Character's retained floor at offset160, and
the same actor's live `RetailHeightState`.

The projection sets local position to actor worldX/worldY and floor+3cm every
call. Actor worldZ, animation rootHeight at offset156, heightmap presence and
cached triangle Z do not substitute for that floor. It rebuilds rotation only
when the actual height-state `changed` byte is true. Otherwise it preserves
every previous quaternion bit.

The caller first tests the literal raw-normal expression
`(normal.x * 0 + normal.y * 0) + normal.z <= -1`. That branch uses the original
float angle-axis quaternion for PI about +Z. Otherwise original rotationFromTo
normalizes its inputs internally and rotates the normal toward +Z. Raw nonunit
and zero normals are preserved through the original branches; no guessed
identity quaternion or surface-normal correction is introduced. The result is
assigned directly without an extra quaternion normalization.

The existing private heading implementations of vector normalization,
fromAngleAxis and rotationFromTo were moved unchanged into shared scene helpers.
Both heading and shadow projection now use those same functions. Their source
addresses are `0x20a2d1`, `0x27b345` and `0x2f03a1`. Float half-angle sin/cos in
fromAngleAxis remain distinct from the already ported double Euler-yaw path.
Original signed-zero arithmetic and the opposite-vector axis fallback remain.

Only the genuine node position and conditional rotation setters run here.
Absolute matrices remain stale until the later original scene-frame update;
the function does not force a recursive refresh or apply another shadow offset.

## Validation

Original reference ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.

Root independently replayed OG061's 54 original graph/bind/projection/lifecycle
cases. New native tests exercise 24 synthetic and 48 actual Player/jail retained
node projection cases per configuration. All 72 cases compare the actual whole
original Character::updatePosition execution: 504 exact position/quaternion
words per configuration, with original absolute caches unchanged. Native tests
also check dirty flags and byte-identical stale absolute matrices, including
heightmap presence deliberately different from `changed`, nonunit/zero/signed
zero normals, retained old rotations and floor+3 placement. The 465 prior
original point/incremental heading comparisons pass after shared math extraction.

Original projection probes execute genuine original node/root/skin constructors
and hierarchy using zero-buffer geometry to isolate factory I/O. They supply
the typed Character/PhysicInfo fields, original GetPosition dispatch, allocator,
AEABI arithmetic and selected system math imports. Material/driver resource
loading and original Android libm bit-equivalence are not claimed. All test
resources come from the user's supplied assets; none are embedded in the repo.

## Ownership and integration still required

Original bindAnimation reparents the same authored shadow container out of the
actor graph into the global scene root using addChild, preserving local TRS,
reference ownership and cached matrices. Only its first generated mesh child
receives layer1. Pooled shadows remain globally retained across despawn;
unpooled destruction removes them before base teardown. Their visibility is
independent from the former actor parent.

The retained native actor owner does not yet implement that global-root
reparent/lifetime operation. Supplying a node to this projection leaf does not
claim to implement it. The caller must also port actual floor production,
body/actor update order, global late frame dispatch, per-instance materials and
textured-plane render submission before the production actor shadow host is
complete. This change owns no duplicate frame clock, physics world, material
host or fabricated shadow registry. No manual gameplay is used.
