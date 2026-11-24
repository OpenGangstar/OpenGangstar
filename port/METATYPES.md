# Original indexed metatype catalogue

`loadCityMetatypeCatalog` exposes the real ordered `metatypes.gmap` records for
native object construction. It reuses the existing private `CityDataReader` and
its bounded record/string operations; there is no second production parser.
The file begins with a big-endian count and each entry has a big-endian payload
length. Every payload is copied before reading its signed type. The retained
`encoded` bytes therefore include type, serialized name and all remaining object
fields, including kinds not yet implemented by the native object manager.

All196 supplied slots survive in original order. Their payloads total37,000
bytes; the complete file is37,788 bytes including count and record framing.
Three slots have type-1 and retain their payload without attempting to parse a
name. The other193 names are ASCII and do not include a serialized terminator.
Raw factory type tags are not the separate loaded `LevelObject::s_type`
classification. This catalogue does not infer runtime categories from them.

`name` uses the existing reader's optional final-NUL trimming. The independent
`sourceNameLength` is the original serialized length, measured using that
reader's cursor difference. Thus a serialized final NUL participates in lookup
without requiring another raw string decoder. Embedded NULs inside decoded
names remain rejected by the existing reader.

## Lookup behavior

`CityMetatypeCatalog::find` follows actual
`GameObjectManager::GetMetatypeFromName0x315a41`: skip only type-1 entries,
compare names as case-insensitive byte prefixes, and retain the first match of
equal length. A new match must be strictly longer using its serialized length.
Only ASCII A-Z is folded. No locale, Unicode, substring, separator boundary or
exact-name-only policy is added.

A nonterminated source `Actor` matches `ACTOR-instance`. Serialized `Actor\0`
matches the query ending after `Actor`, but not `Actor-instance`, and its length6
outranks nonterminated length5 for that exact query. A zero-length source cannot
beat initial best length0. A NUL-only serialized length1 can match an empty
query. These distinctions are independently checked against original execution.

Queries with embedded NUL are rejected as outside the native C-string value
domain. Manually assembled mutable catalogues also reject more than256 entries,
embedded-NUL active names, or lengths other than name length or name length+1.
Invalid type-1 metadata is skipped like the original. The loader allows a framed
empty standalone catalogue, rejects malformed/truncated records, and preserves
the existing eight-bit index limit256 and64MiB file bound. Outer trailing data
is ignored like the original; it is not treated as an invented EOF requirement.

## Current use and ownership

`bindSpawnGroups` loads this catalogue into `CityAssets.metatypes`, then uses its
single `find` implementation. Existing typed spawn and weapon decoding reads
the retained payloads through the same bounded reader. No separate lowercase
prefix loop or filtered/reindexed name table remains. The original requirement
for a nonempty catalogue during actual spawn binding is unchanged.

The catalogue owns every copied payload. Future native manager pointers/sizes
must derive from those owned bytes, keeping the catalogue alive and avoiding
mutation/reallocation while borrowing. It does not cast original ARM pointers,
retain a loaded game object, instantiate unknown factory kinds, create pools or
replace an unavailable object constructor. A city-loading path without spawn
binding need not have loaded this optional field; a separate manager can call
the standalone loader explicitly.

## Validation

Original execution uses the genuine manager lookup and
`BufferStream::C2/D2` at `0x34e661/0x34e631`, `DataStream::C2` at `0x34eb31`,
`DataStream::ReadInt0x34e961`, `BufferStream::GetPtr0x34e2e5` and actual stream
destruction. The reference manager supplies typed count/payload-pointer/size
fields at ARM offsets1036/1040/1044. Those offsets stay in ignored reference
tooling, not native structures. Imported memcpy/allocation and strncasecmp are
explicit system boundaries; string comparison reads at most n bytes and stops
at NUL, instead of probing an unterminated source past its serialized name.

All740 real original query indices match native in Release and Debug. Eight
additional actual original controls cover ties, case, terminated/zero-length
names, invalid slots and non-ASCII bytes. Independent native fixtures cover
maximum slot255, empty catalogues, arbitrary retained payloads, malformed
framing/strings, native query checks and file bounds. Raw tests compare every
record, type, name, source length and all37,000 payload bytes.

Full Release and Debug builds and all48 CTests pass. Actual city binding retains
19 spawn groups,45 spawn metatypes and11 weapon definitions; every spawn point
keeps its original group index. Both retail audits still decode98 images with
zero opened-image decode failures and the exact same111 absent texture paths.
Their strict failure status remains intact. Reference fixtures/emulator code
are excluded from native builds and no gameplay progression is used.
