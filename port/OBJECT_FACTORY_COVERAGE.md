# Original object factory coverage

This records original factory dispatch and remaining native ownership work. It
does not claim that native concrete object constructors or loading are complete.
The complete indexed payloads are retained by `CityMetatypeCatalog`; existing
typed asset decoding and bounded subsystem implementations are separate from a
genuine game object with its full lifecycle.

## Original entry points

- `GameObjectManager::LoadGameObject(DataStream&)`: `0x315675`.
- `GameObjectManager::CreateGameObjectFromMetatype(int)`: `0x315969`.
- `GameObjectManager::SpawnGameObject(int)`: `0x315a15`.
- `GameObjectManager::Load(char*)`: `0x315d41`.
- `Structs::GameObjectBasic::Read(DataStream&)`: `0x3150f1`.

These are Thumb symbol addresses, not addresses or layouts used by the native
runtime. Native C++ objects must use full-width pointers and actual inheritance.

## Supplied catalogue and real dispatch

| Raw serialized tag | Slots | Original constructed kind | Constructor | Load |
| --- | ---: | --- | --- | --- |
| -1 | 3 | Invalid indexed slots, retained without a name | — | — |
| 0 | 1 | LevelObject | `0x3167a5` | `0x316fed` |
| 1 | 2 | LevelObject fallback | `0x3167a5` | `0x316fed` |
| 2 | 1 | LevelObject fallback | `0x3167a5` | `0x316fed` |
| 3 | 44 | Vehicle | `0x30a955` | `0x30ff41` |
| 4 | 11 | Weapon | `0x330549` | `0x330665` |
| 5 | 80 | NPC | `0x2fe3c9` | `0x2fdcfd` |
| 6 | 14 | Checkpoint | `0x2f76bd` | `0x2f7601` |
| 9 | 21 | SpawnPoint side effect | `0x306df1` | `0x306b11` |
| 10 | 2 | Transformation | `0x3079ed` | `0x30785d` |
| 12 | 1 | Rocket | `0x32fd91` | `0x32fb19` |
| 13 | 4 | Shop | `0x305d55` | `0x305d81` |
| 14 | 1 | Path | `0x300c8d` | `0x300bc5` |
| 15 | 11 | Item | `0x2fc3e5` | `0x2fc6bd` |

The additional explicit tag7 branch constructs SceneCamera at `0x334fa1` and
loads it at `0x334fe9`. No supplied metatype has that tag. Other signed tags use
the original basic LevelObject fallback; that is an observed original branch,
not permission to fabricate implementations for unknown specialized classes.

`LoadGameObject` first peeks the tag, executes its actual typed `Structs::Read`,
allocates and constructs the selected kind, calls its Load, then invokes virtual
WarmUp and returns its LevelObject identity. SpawnPoint is different: it is
constructed and loaded, then the function returns NULL without WarmUp.

SceneCamera uses multiple inheritance. The original dispatch loads the complete
object but invokes WarmUp and returns its LevelObject base at object+28. Native
code must express the actual base conversion rather than copy that ARM offset.

The metatype wrapper reads the selected indexed payload through BufferStream
and BigEndian DataStream. Only after LoadGameObject, including WarmUp, returns
does it overwrite objectType with the indexed metatype ID at original byte36.
That ID is distinct from the raw constructor/serialized tag and must not be
stored prematurely. Actual LevelObject Load performs its own name lookup first.
The wrapper has no NULL guard before that byte store: SpawnPoint is outside its
valid return-object domain. Native checked bounds and lifetime rules must be
explicit rather than reproduce invalid memory access.

`SpawnGameObject` then adds the object using its loaded list selector and deletes
and clears its loaded name. Full `Load` separately skips raw SpawnPoint records,
seeks to each framed record's end, and adds returned objects to their loaded list
when either flags bit0 or bit1 is set, otherwise list2. `LoadSpawnPoints` handles
SpawnPoint records through their separate side-effect path.

## Record consumption and retained tails

Actual original typed reads consume 36,760 bytes from the193 active payloads.
Three base-fallback records leave authored opaque tails:

| Indexed slot | Tag | Payload bytes | Consumed bytes | Retained tail bytes |
| --- | ---: | ---: | ---: | ---: |
| 17 | 1 | 97 | 81 | 16 |
| 94 | 1 | 113 | 85 | 28 |
| 158 | 2 | 249 | 93 | 156 |

All other190 active payloads are consumed completely by their original typed
reader. Catalogue retention keeps all200 tail bytes and all invalid-slot bytes.
No additional field meanings are inferred from the tails. The ordinary framed
loader's explicit seek permits those original partial reads.

## Validation boundaries and remaining implementation

Root executed the actual ARM dispatch and typed stream readers for all193 active
records, then all172 valid return-object metatype wrappers. Five synthetic
controls cover SceneCamera's adjusted base and signed/unknown basic fallbacks.
Constructors, concrete Load methods, factory lookup and virtual WarmUp are
explicit captured leaf endpoints in this reference probe. Their implementations,
gameplay side effects and destruction are not validated by this dispatch proof.
Actual BufferStream/DataStream construction, peeking, typed reads, cursor access
and buffer destruction execute through the reviewed reference infrastructure.

The reference ELF SHA256 is
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Ignored reproducible artifacts are in `port/build/reviews/OG-098`: `probe.py`,
`dispatch-results.json`, symbol inventory and disassembly windows. Retail bytes,
original binaries and reference execution dependencies are not native runtime
dependencies and are not committed.

Remaining work includes genuine canonical LevelObject state and lifecycle,
ordered intrusive lists, actual concrete class construction and Load/WarmUp,
scene pools, physics/contact teardown, all specialized behavior, and the real
ordered world/VM/Player host. Existing native scalar weapon, actor scene,
animation, deformation, target and asset components must compose into those
same objects; they do not substitute for complete factories. There is no new
production stub, callback-only factory or second entity registry in this change.
