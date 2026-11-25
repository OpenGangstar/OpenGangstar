# Canonical actor loaded-resource storage

`RetailActorLoadedResource` represents one explicit physical loaded-resource
identity. It owns a shared immutable `RetailActorSceneSource` and one mutable
position/normal storage bundle per exact decoded source geometry. Fresh actors
and morph target roles using that load must reuse the same shared resource
handle. Their materials, weights, process outputs and palettes remain separate
owners; this component does not retain any of them.

## Creation and identity

`load(files, path)` delegates to the existing
`loadRetailActorSceneSource` decoder. `create(source)` accepts already decoded
immutable metadata. Every call creates an explicitly separate load, including
when two calls share the same immutable metadata object. Those separate loads
have independent mutable arrays. Sharing parsed metadata alone does not merge
their runtime identities.

A composing resource manager must create the resource once for a particular
load and distribute that same handle. This class has no hidden global registry,
filename/UID cache, per-actor initialization or automatic canonicalization.
Repeatedly calling `create` for fresh actors would describe separate loads and
would fail to preserve the original same-load sharing relationship.

`source()` returns the original retained immutable source handle. Geometry
lookup compares actual object addresses against its ordered mesh catalogue.
`currentStreams(geometry)` rejects a copied or foreign geometry even if its
name and values are identical. Separate records with equal names and values
remain distinct. Native pointers and ownership use full-width C++ types; no
source index is promoted into a runtime address or used as a global name key.

An empty valid source catalogue is allowed, including animation/library-only
resources. It simply cannot produce a stream for an unavailable geometry.

## Storage and lifetime

Initial position and normal vectors are copied once per explicit load from the
authoritative decoder. Every subsequent access to a particular geometry in the
same load returns an alias of that same mutable `RetailSkinningOutput` object.
This is the existing output/current-storage type accepted by the deformation
owner; no alternate kernel representation or second decoder is introduced.

The storage bank is completely populated before publishing an alias and is
never resized afterward. Each returned alias retains the bank and its immutable
source, so it remains valid after the resource wrapper and every other alias
are released. The wrapper itself need not stay alive. The final alias releases
the bank/source if no other owner remains. No actor, material, node, palette,
dirty flag or bbox cache is retained or duplicated.

Mutable normal effects are visible to all roles sharing the same load. Separate
loads remain isolated, and immutable decoded arrays are never written. The
original normal-format declaration remains solely in `CityMesh::floatNormals`;
there is no independently mutable format flag that could drift from metadata.

Callers must preserve each geometry's valid current vertex counts. The legacy
public vector representation permits resizing, but that is outside the valid
geometry domain; existing deformation-view/kernel checks remain authoritative.
This storage component does not duplicate their validators or infer normals.
Concurrent writes still require caller synchronization.

## Original evidence

Original buffer construction at `0x2a17b1` builds a distinct embedded SMaterial
at buffer+12, while the vertex descriptors begin at buffer+104. Position and
normal descriptor pointers are at descriptor+4 and +16. The loaded interleaved
array selection at `0x2a18e8..0x2a18f6` and `0x2a192c..0x2a1942` stores loaded
source addresses instead of copying each actor's vertex arrays.

The independently replayed OG083 probes construct two fresh DefaultFactory
instances from each of the 21 supplied nested vehicle BRES resources. All 63
corresponding base/damage target buffer pairs have distinct runtime
mesh/material identities but identical position and normal pointers inside the
same loaded BRES. A normal write through the first instance is visible through
the second, while material and runtime weight writes remain isolated. The real
root finalizer `CRootSceneNode::onPostLoad` at `0x293da9` executes for both instances.
Default morph creation at `0x29d27d` uses defer=true/NULL driver; this component
does not substitute that path for the separate CarMorph factory.

The ignored OG088 reference inventory independently reads all 359 supplied
BRES files and their 1,877 interleaved source mesh records. It checks both exact
data-pointer sharing and overlaps of the complete
`[data, data + vertexCount * stride)` spans within each file. Both sets are
empty, matching the root's independently generated inventories. Consequently,
one bundle per exact source geometry preserves the supplied catalogue's
between-geometry sharing relationships, while repeated instances share the
same bundle. This is evidence for the supplied domain, not a universal promise
that arbitrary future formats cannot alias partially.

Native storage is deinterleaved decoded float vectors. It preserves logical
source-array sharing and initial decoded bits; it does not claim the original
raw byte layout, allocation addresses or GPU descriptor layout.

## Validation and remaining composition

Focused tests cover same-load identity and mutation visibility, separate-load
isolation despite shared metadata, equal-name source record separation,
foreign equal-value rejection, empty/null source handling, signed-zero/NaN
payload copying and retained source/stream lifetimes. A complete supplied-asset
gate compares initial position/normal bits and normal-format flags against a
separate authoritative decode for every source geometry. Both Player models
and all 21 nested vehicle metadata catalogues are retained by the loader.

The explicit loaded `RetailActorScene` constructor now consumes this handle.
Every constructed ordinary resource retains the exact current-stream alias for
its source geometry, including separate base/target roles and transferable
ordinary shadows. Extraction therefore preserves loaded storage without
retaining the old actor. The metadata-only constructor remains a separate
compatibility domain and does not invent a physical-load identity.

Scene owns canonical morph weights; the separate
`RetailActorDeformationPipeline` composes actual source/morph/skin resources
without duplicating this bank. See ACTOR_DEFORMATION.md. This storage owner does
not schedule deformation, implement CarFactory damage policy or upload/draw
Vulkan data. A resource manager must still distribute the same genuine load
handle rather than recreate a bank for each actor.
