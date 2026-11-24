# Live actor material ownership

The default actor factory now constructs real root-local prepared material
owners and ordered node references. `RetailActorMaterial::current()` returns
the live CityMaterial in the format used by the verified064 rendering equations;
`state()` aliases that same object's RetailMaterialState for063 mutations.
There is one mutable scalar state per prepared owner, with no render snapshot
or second mutation implementation.

Original reference ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.

## Indexed source and root-local identity

`CityScene::materialCatalog` preserves literal source material order, including
duplicate and case-colliding UIDs. The existing renderer `materials` map retains
its compatible first-name entry. `MaterialRef::sourceIndex` preserves original
SInstanceMaterial+8; unknown hand-built metadata is distinct from authored0.
The authoritative reader supplies the index, and the actor never infers it
from a URL, buffer symbol or material name.

Original constructGeometry(instance)0x2966f9 selects a local material by that
index, or enters its external-database branch. Controller binding0x2964c9 uses
the local index even if unused path/name fields disagree. The native ordinary
external-material branch rejects until its actual database owner is supplied;
the controller keeps its source-backed local-index semantics. Existing external
geometry/controller factory restrictions remain unchanged.

After resolving the indexed source record, original prepareMaterial0x2a2cb1
uses IRootSceneNode::hasLibraryMaterial0x2a2971 and addLibraryMaterial0x2a2995.
The first case-sensitive C-string UID in that root's ordered library wins.
Construction order controls first use; the source catalog/map order does not
replace it. Each live mesh node retains its actual ordered shared owners.
Repeated source UIDs may consequently alias between buffers, different fresh
geometry resources, and multiple nodes. Different actor roots retain separate
mutable material libraries even when sharing immutable decoded source.

The native owner retains immutable source and its exact initial source index,
never ActorScene. Scene nodes expose `materialCount()` and `materialOwner(i)`.
A retained shared owner is the actual live identity, not a copied scalar. Its
index remains that of the first selected source record when later indexed
records with the same UID reuse it. Missing/out-of-range indices and missing
catalog entries reject; there is no white-material or name-lookup fallback.

## Source buffers have a different material

Each fresh ordinary runtime mesh owns one independent embedded source-buffer
RetailMaterialState per geometry buffer. Original CColladaMeshBuffer C1
0x2a17b1 calls SMaterial constructor0x20e7ed on buffer+12. Its defaults are
word0=1, flags40=0x387, ambient/specular/current=0xffffffff, emissive=0 and
dirtyFlags=0xfc. These are the source-buffer constructor's real values, not
prepared CityMaterial values. In supplied shadows, prepared flags0x187 differ
from source flags0x387; body prepared colors also differ.

`RetailActorSceneMesh::sourceMaterial(i)` returns that actual embedded state.
Skin access delegates to the identical base-buffer address, matching borrowed
proxy material references. Node scalar changes leave those source states
untouched. References remain borrowed from the actual runtime mesh; retaining
ActorScene alone does not lease a mesh moved out by068. General movable-mesh
borrowing requires a separate genuine lifetime mechanism. The074 scene binding
is restricted to owned live skin bases until that mechanism exists.

## Exact mutation traversal and graph lifetime

GS3DStuff::SetColor0x2ded05 recognizes only ordinary0x6d656164 and
skin0x73656164 nodes. It changes only material0 and stops there; other node
types recurse in child order. SetAlpha0x2df34d uses the same node distinction,
with only selector0 changing the current alpha byte. The native alpha input
narrows through063 to the original unsigned-char domain.

SetMaterialFlag0x2dc0ed follows a different rule: it changes every own material
in index order through ISceneNode::setMaterialFlag0x273d95, then always visits
children. Material-bearing ordinary nodes with a different runtime type still
participate in flags even though color/alpha skip their own material. Shared
identities can receive repeated calls; no identity deduplication is introduced.
The three native recursion functions invoke the existing063 leaves.

Node material references move with the actual068 shadow subtree. They do not
retain a dead actor through a root-library backlink. If another alias remains
under the actor, actor-only traversal can still change a detached shadow via
the shared material. After those aliases are removed, traversal no longer
reaches it. Final actor teardown releases its node/library references while a
pooled shadow keeps its material; final shadow removal releases the owner.
Source-buffer storage remains on its fresh mesh and is not merged with this
prepared-material ownership.

## Checked input domain and remaining hosts

The original allocates one raw binding slot per actual buffer, then writes the
authored binding count. A missing entry can retain uninitialized allocator
bytes; an extra entry can store beyond capacity. Native construction checks
binding count equals buffer count before indexing. All4,235 resolved mesh
instances across the supplied359 libraries satisfy equality, with counts1..11.
The other10 kind1 splines have no mesh. Original adversarial execution with
poisoned allocation proves the missing-slot and extra-store cases. This is an
explicit native safety boundary, not parity with legacy undefined memory use.

This supplies genuine static prepared material ownership and scalar mutation,
not the complete Character animation/material/render host. Original CMaterial
get0x2a2625 can also observe dynamic CImage texture replacement and adjust
texture refs/dirty state; that image owner path is not represented by current().
General reprepare/read-only-material rendering, external material databases,
texture/driver ownership, renderer submission/culling and full world scheduling
remain separate. No empty image callback or fabricated renderer multiplier is
added. Existing055 skin-cache and077/078 scene behavior are preserved.

Validation covers real Player/jail factory materials, root-local sharing,
indexed duplicate/case controls, source/proxy material separation, detached
shadow destruction, literal LevelObject/Character mutation sequences, all359
raw catalogues and1366 original prepared material states. Release/Debug results,
original instruction-window boundaries and exact artifact hashes are recorded
in the ignored OG079 submission report. No manual gameplay is required.
