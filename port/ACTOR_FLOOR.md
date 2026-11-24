# Original actor floor commit

`RetailActorFloorState::heightCm` represents Character's retained floor at
offset160, initialized to positive zero. It is separate from animation
rootHeight at offset156. The native object uses ordinary typed state, without
overlaying either original ARM object layout.

`updateRetailActorFloor` ports the Player continuation at `0x30539e..0x3053ee`
and corresponding NPC block `0x2fff64..0x2fffbe`. The actual outer branch calls
it after animation/root-height update and before the existing shared world
advance. It does not decide which outer Player/NPC branch runs.

The operation preserves the source order:

1. Copy cached actor world position for the final XY.
2. Independently copy cached world position for the height query.
3. Call the same live actor height controller at slot0 with detailed=false.
4. Store that actual return value in the retained floor.
5. Read the live animation rootHeight, assign LOCAL position to savedXY and
   floor+rootHeight, then updateAbsolutePosition(false).

The cached-position reads match LevelObject::GetPosition at `0x31684d`. They
do not refresh matrices or return local/body position. No inverse parent
transform is introduced. Consequently a nonidentity parent retains the original
world-as-local behavior. The final nonrecursive update leaves descendant
matrices untouched until the later real global scene frame.

The verified height wrapper owns all selection, cache, normal/changed, ground
and jump behavior. An unselected fresh sentinel cache is primed from queryZ;
an unselected retained cache returns its previous Z. This floor operation does
not lazily search terrain, clamp the return, manufacture a normal, or mark
changed when its number differs. Detailed=false still invokes the original
supported height/jump composition through that real controller.

## Validation

Original ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.

Root independently executes the actual Player continuation, real original
PhysicInfo constructor/GetHeight wrapper/core and scene getters/setters for
fresh-unselected, retained-unselected, selected, stale-absolute and nonidentity
parent cases. Both native configurations match all 60 floor/local/normal/cache/
flag words. Original traces require two ordered position reads, slot0,
detailed=false and nonrecursive final update, with no body teleport/refilter.
Each continuation checks its exact stop PC. Reference source interfaces,
allocation and imported arithmetic are explicit; this is not a whole original
Player Update or Android runtime execution claim.

Native tests additionally check repeated stable root-height addition and four
actual decoded Player/terrain commits against the existing verified height
component, preserving every descendant-cache byte. Retail resources are supplied
at runtime rather than embedded in tests. The focused target is
`retail_actor_floor_test`; `--dump` emits the five original-comparison profiles,
and an optional asset directory runs decoded integration checks.

## Remaining outer integration

The genuine runtime must dispatch this leaf in its original reached branch,
after real pose/animation work. It must synchronize the existing native contact
owner metadata at the actual scene-commit phase before world/contact use.
This operation does not invent automatic refilters or body placement.

Original post-world player readback uses the retained PRE-step floor/normal
with the newly stepped body XY. It performs no second height query and calls
qualified LevelObject placement, rather than Character teleport/SetXForm.
The original outer loop clears player linear XY after all physics steps even
if none ran, applies the live down/flag0x400 scene-dispatch gate, then advances
vehicles/scripts. Target/facing, vehicle/death/visibility branches, global shadow
ownership, late scene update and renderer integration remain separate genuine
host requirements. This leaf supplies no fake full Player/NPC host, additional
world clock, body teleport, shadow registry or manual gameplay validation.
