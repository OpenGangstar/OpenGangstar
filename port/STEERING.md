# Native destination steering

OG-017 reconstructs both original LevelObject::canGoToPoint overloads as native
retailCanGoToPoint functions. The implementation uses the pinned real Box2D
2.0.1 world, body transforms, shape queries and TestSegment methods, with
RetailPhysicsBodyData pointers. It does not use ARM class layouts or execute
ARM code at runtime. Production actor/world ownership and controller binding
remain incomplete; these functions do not by themselves move an actor.

## Source evidence and native boundary

Original Thumb entries are 0x00317fb5, size 0xc30 (three-output overload), and
0x00318be5, size 0x308 (point-only overload). Corresponding original sources are
the two canGoToPoint assembly files under games/gangstar-wch/asm/world. The
matching ARM project is unchanged. Both complete functions, including their
literal pools and late branches, were inspected before implementation.

RetailSteeringActor supplies a borrowed native owner-state identity, XY position
in centimetres and the actual PhysicAttributes pool index. Z comes from the
existing owner's positionZCm. owner.levelCategory must be the live
LevelObject::s_type classification, separately from factory tags or shape
categoryBits. Category 5 is NPC, 3 Vehicle, and 8 shares the query margin for
characters. No guessed category conversion, movement duration, arrival flag or
synthetic callback supplies missing world state.

The API has separate point-only and three-output overloads. Input/output points
are centimetres. Real Box2D query and segment XY coordinates are metres. Nodes
use their supplied bounding-box Z interval, not the character/root-height
interval from ContactFilter::ShouldCollide: these are separate original rules.

## Literal query behavior

| Behavior | Three-output, 0x00317fb5 | Point-only, 0x00318be5 |
| --- | --- | --- |
| AABB | Encloses current/requested XY, margin 0.5 m for categories 5/8, 1.5 for 3, otherwise zero. | Encloses unexpanded current/requested XY. Equal axes increase the upper endpoint by exactly 0.01 m. |
| Candidate storage | Zeroed native array of 100 shape pointers; Query cap 100; preserve returned order and stop at first null. | Same cap/storage/order. |
| Direction | Normalize requested-current as a full 3D float vector. | Computes that same temporary, but uses the unoffset centre XY segment. |
| Rays | q=(direction.y,-direction.x); first ray is segment XY minus margin*q, second is plus margin*q. | Single centre segment. |
| Second ray | Tried only if the first TestSegment returns false. Height rejection or non-nearest first hit skips the entire shape. | None. |
| Sensor/self filtering | Skip sensors and data.owner==actor identity. Also skip ownerless metadata. | Skip sensors/self, but retain ownerless metadata. |
| Category-zero shapes | Require signed group == physicsIndex+1 and nonzero mask. Nonzero-category shapes do not use a mask intersection test here. | Same. |
| Heights | Node interval or unequal first-pair heights are inclusive. Equal node-less heights bypass interpolation and height rejection. | Same height acceptance, but retains TestSegment's output fraction after height rejection. |
| Selection/result | Strict fraction < closest, initialized to 1. Equal fractions retain first query-order hit; reachable is closest==1. | Return fraction>=1 immediately for the first height-accepted hit. At loop end return retained fraction==1. Never sort by nearest hit. |

Full normalization is the original irr::core::vector3d<float>::normalize at
0x0020a2d1: squared length is (x*x+y*y)+z*z in float, compare exactly with zero,
otherwise sqrtf, reciprocal and coordinate multiplication. It is not Box2D's
epsilon-based 2D Normalize. A destination with changed Z changes ray offsets.

Three-output key branches: category margins 0x317fdc..0x318004 and 0x3184a4 /
0x3185f8; query 0x31809c; normalization and segments 0x3180be..0x3181e0;
candidate gates 0x318214..0x318258 / 0x3184aa; first/second segment paths
0x31825a..0x31833a and 0x3184b4..0x31859e; inclusive differing-height branches
0x318824..0x3188b8 and 0x3189f2..0x318a8a. Both set max TestSegment fraction to
one, using the candidate body's actual transform.

Point-only gates and return behavior are at 0x318d42..0x318e32. Node-less equal
heights at 0x318e34 bypass Z checks. A point-only hit rejected on height can
still leave fraction<1 and make the final result false if no later accepted
hit returns first. This original behavior is retained, not corrected into a
different obstruction algorithm.

## Outputs and destination adjustment

Three-output always writes hit=current+closest*(destination-current), including
Z. If closest==1 it leaves adjusted output untouched. Character::CheckDestination
uses the requested destination on true and adjusted output on false; these
functions do not overwrite the actor's requested destination or update movement.
On an exception no output should be consumed or committed to controller state.

Original scratch XYZ initializes to zero. Any node-backed or unequal-height
TestSegment hit computes scratch=current+fraction*(destination-current) before
height and nearest-fraction rejection. Equal node-less heights do not update it.
The selected shape, normal, ray index and closest fraction are stored separately;
scratch is not saved with them. Thus a later rejected/farther candidate can
change adjustment scratch, while hit still uses the earlier closest fraction.
Equal heights with no previous interpolation can produce adjusted Z zero even
when hit Z is nonzero. These literal effects have explicit regression fixtures.

Let M=margin*100, q=(direction.y,-direction.x), scratch=S, and C the selected
body origin in centimetres. For a selected circle:

- Non-vehicle owner: first-ray adjustment XY is C+q*(M+75); second-ray is
  C-q*(M+75). Z is S.z plus/minus the original zero-Z product.
- Category-3 owner: compute cross(rayDeltaXY, bodyOriginXY-rayStartXY), with
  both vectors converted to centimetres. If cross<=0, XY is S-q*(M+50),
  otherwise S+q*(M+50). Z retains the corresponding original zero-Z product.

This circle logic uses body origin, not a guessed circle radius or local circle
centre. Branches are 0x318370..0x3183f6, 0x3188ba..0x31897e,
0x318992..0x3189f0 and 0x318ae2..0x318b6c.

For a polygon (0x318618..0x318824 and 0x318aa0..0x318be0):

1. Transform exactly the first four local normals by body rotation and find
   exact XY equality with the stored TestSegment normal.
2. Compute the same selected-ray/body-origin cross product. If strictly >0,
   use corner (normalIndex+1)%4; otherwise use corner normalIndex.
3. Transform that local vertex by the body's real transform to world XY.
4. Normalize the full 3D vector (cornerCM-bodyOriginCM, S.z-S.z).
5. Write adjusted=(cornerCM,S.z)+normalizedVector*(M+50).

This is an outward corner extension. It is not a wall slide or the first hit
point. The literal modulo is four, not native polygon vertex count. At least
four valid entries and a matching first-four normal are required. Polygons with
more vertices work when these accessed entries/normal meet that original
contract; no uninitialized extra normal or vertex is synthesized.

## Explicit unsupported host inputs

An intersected shape without metadata would be dereferenced by the original.
Native code reports a logic_error instead. Polygon adjustment with fewer than
four authored entries, no matching first-four normal, or unsupported shape type
also reports an explicit binding error; it does not silently choose another
steering algorithm. An original loop could otherwise access unused polygon
storage. A zero-margin degenerate three-output query reports domain_error before
triggering Box2D's strict broad-phase AABB assertion. Only point-only queries
have the original equal-axis padding. These checks delimit undefined/unsupported
native host inputs rather than fabricate game geometry.

These are synchronous queries, not world stepping or contact callbacks. Invoke
them from the native controller's world-query phase. Do not resume a controller
update with partially established output after a binding failure. This API does
not establish a general recovery policy for exceptions inside a Box2D Step.

## Validation

Real native Box2D fixtures passed in isolated x64 Release and Debug builds with
strict FP and Debug assertions enabled. Tests cover clear/zero-length output,
both offset rays, all category margins, 3D direction, analytic circle/body-origin
and polygon corner positions, rotated polygons, both cross-product signs,
supported five-vertex and unsupported fifth-normal geometry, sensors/self/
ownerless/group/mask distinctions, inclusive Z endpoints, equal/unequal heights,
ignored jump-surface extra heights, first-ray rejection suppressing the second,
strict endpoint fractions, ties, first-accepted versus nearest behavior, retained
rejected fractions/scratch XYZ, actual 101-shape query cap and missing metadata.

An ignored research-only ARM oracle executed the two actual original routines
in Unicorn with bounded host hooks for queried shapes, TestSegment responses,
position/bbox access and scalar float helpers. It independently confirmed equal
height scratch, point-only rejected fraction, first-ray-only height handling,
later rejected scratch, both polygon corner signs, first accepted endpoint and
strict endpoint behavior. The oracle does not claim to validate Box2D geometry:
the separate native tests use actual engine geometry and independent analytic
expected positions. No emulator or new external dependency enters the port.

Integration includes retail_steering.cpp in opengangstar_physics and registers
retail_steering_test. Full Release and Debug builds and all nine CTests pass.
Independent source-copy harnesses also pass in both configurations. Both native
test executables are AMD64 PE32+. The orchestrator independently reran all nine
original ARM oracle fixtures with an additional return-sentinel assertion.
Exact harness commands and oracle evidence remain ignored build artifacts. No production physics/controller installation,
ContactListener Add/Persist, terrain lifecycle, runtime motion flags or mission
progression is claimed. No manual gameplay was used.
