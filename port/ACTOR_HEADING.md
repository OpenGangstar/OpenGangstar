# Original actor heading operations

OG043 implements Character's point-facing and incremental direction turn, with
required live scene operations. It does not add an atan2 yaw, a renderer-specific
orientation conversion, scene refresh, actor movement, or another game clock.

## Original evidence

The matching Character.cpp at point overload `0x2f3e99` and direction overload
`0x2f22d5` supplies the complete control flow. Original weak scalar helpers were
checked in the pinned retail binary:

| Operation | Address |
| --- | --- |
| vector3d<float>::normalize | 0x20a2d1 |
| quaternion::rotationFromTo | 0x2f03a1 |
| quaternion::normalize | 0x1c8d11 |
| quaternion::fromAngleAxis | 0x27b345 |
| quaternion::set(float,float,float) | 0x2185c9 |
| quaternion::operator*(quaternion) | 0x24eb35 |
| LevelObject::GetPosition | 0x31684d |
| LevelObject::GetRight / GetFront | 0x317855 / 0x31782d |
| ISceneNode::getRotation / setRotation | 0x272a01 / 0x272a05 |
| ISceneNode::getAbsoluteTransformation / getAbsolutePosition | 0x272875 / 0x273b3d |

Point-facing reads GetPosition once, subtracts only XY, normalizes its temporary
direction and retains the original `dot <= -1` opposite-direction guard. The
opposite branch uses float half-angle/sinf/cosf around the original PI literal.
The other branch executes the original normalized-vector dot/cross/square-root
rotationFromTo path. A final quaternion normalization precedes setRotation.
Actor/destination Z are not used, even if their Z values are nonfinite. The point
argument remains a const reference, read after GetPosition.

There is no geometric epsilon or zero-vector facing fallback. The original zero
point writes quaternion `(-0,-0,+0,0x3f7fffff)` under the tested scalar math path,
rather than retaining the prior orientation or forcing exact identity. Near the
opposite direction, float normalization can round the dot to exactly-1, selecting
the literal PI branch. Preserve those original scalar decisions.

Incremental turning snapshots direction by value and does not normalize it.
It reads GetRight, selects sign+1 only for a strictly positive dot (otherwise-1),
then reads the current local quaternion. The delta angle is evaluated as
`(sign * step) * -0.017453292519943295f`. Euler quaternion construction retains
the original double trig/product path and float normalization, followed by the
original quaternion multiplication convention and float operation order. The
resulting product is not normalized. A zero direction still performs a turn.

Only after setRotation does the function read GetFront and return the strict
`dot(front,direction) < -0.99f`. Nonunit cached columns/direction affect that
literal threshold; they must not be normalized to create an angular predicate.

## Shared math and genuine scene state

The verified OG035 `retail_scene` dependency exports the existing jump producer's
literal EulerZ and quaternion-normalization code as retailSceneYawQuaternion and
retailSceneNormalizeQuaternion. This task reuses those helpers without duplicating
them or changing raw SNode quaternion treatment. The original vector normalization,
angle-axis, rotationFromTo and reversed-cross multiplication operations remain
small private heading helpers. No generic math framework was introduced.

RetailActorHeadingHost requires actual position/right/front/rotation/setRotation
operations. It contains no default or empty implementations and no synthetic
source-node-as-runtime identity. A production scene adapter must implement the
same original getter and setter effects, with its genuine retained actor state.

The ordinary original GetRight/GetFront use negated columns0/1 of the cached
absolute matrix. getAbsoluteTransformation returns that cached matrix directly.
setRotation writes local XYZW and ORs dirty bit0x10 in the original node word at
offset296; it does not update the absolute transform. Thus the final Front query
can legitimately observe the old cached orientation after a new local rotation.
Heading code performs no implicit scene refresh and never derives a fresh front
from the just-written quaternion. The later original scene-update boundary stays
with the actual caller. Native interfaces contain no ARM layout offsets/casts.

The required call order is point: Position, SetRotation; incremental: Right,
Rotation, SetRotation, Front. Scene operations remain live and serial: the Front
query occurs after the setter, and no prefetch of later getters is permitted.
Callbacks must not invalidate the active host or reenter the operation. The
actor and selected node binding stay fixed during that serial operation; this
interface does not claim parity for scene-instance replacement inside a getter.
The original scene/root factory and full pose/application adapter remain separate
production prerequisites. RetailDirectPlayerHost::turnToward can delegate its
point-facing operation through this host when those bindings are genuine.

## Validation boundaries

Native fixtures cover cardinal/opposite/zero/nonunit heading, actor/destination
Z independence, nonunit quaternion preservation, strict sign and-0.99 boundaries,
const-point read timing and by-value direction lifetime. A deliberately stale
cached matrix distinguishes the original behavior from recomputing front after
setRotation. An additional live-setter fixture distinguishes getter-after-setter
from a prefetched Front.

Release and Debug x64 builds pass all 20 configured CTests. Each configuration
also matches actual original execution for 302 point-facing and 163 incremental
cases: every quaternion component is bit-identical and every boolean agrees.
The point corpus includes deterministic noncardinal inputs, large values,
subnormal squared lengths, opposite-direction rounding and signed zeros. The incremental corpus
includes nonunit/zero starting quaternions, signed steps, nonunit directions and
the adjacent floats on both sides of the strict completion threshold. The native
Release/Debug dump files are identical. A separate original-only adversarial
probe validates point-reference read timing and caller-copy direction lifetime.
These focused operations consume no asset format; the full game also compiles
and links in both configurations. No manual gameplay or progression is used.

The ignored reference harness executes both actual Character instruction bodies,
their original quaternion/vector helpers and actual LevelObject/ISceneNode
getters/setter. It verifies the real setter's dirty bit and unchanged cached
matrix and checks the return sentinel. Only external IEEE float/double arithmetic,
sqrt and sin/cos imports use research host math helpers; no heading algorithm or
scene getter/setter is replaced by a reference approximation. This proves the
tested scalar execution path, not every Android-vs-Windows libm input's rounding.
Original inputs/results are compared by component bits and boolean result.

No missions or manual gameplay are used. Full runtime scene identity, pose,
animation/body synchronization and gameplay/controller completion are not inferred
from these heading operations. There is no duration-based movement readiness or
new movement flag in this component.
