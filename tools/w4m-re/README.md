# w4m-re: read-only reverse-engineering helpers for Worms 4 Mayhem (PC)

Python 3 tools that read the user's own install. They embed no game data, and their output never goes into the repository.

- Install: `$W4M_DIR`, by default `~/snap/steam/common/.local/share/Steam/steamapps/common/WormsXHD`.
- Cache and output: `$W4M_CACHE`, by default `~/.cache/w4m-re`.
- Dependency: `pip install --user capstone`, needed by `scan.py`, `xref.py` and `disasm.py`.

| tool | use |
|---|---|
| `pe.py` | `msg REGEX|0xVA` (message handle objects to names, 3635 objects; one name has one object per source unit), `sections`, `str REGEX` (strings with their VA), `va2off` / `off2va`, `words VA N` (annotated dwords), `rtti REGEX` (MSVC classes, bases, vtables), `schema CLASS_RE` (serialised XOM fields: index, name, type, struct offset, record, push site) |
| `scan.py` | builds the capstone linear-sweep index (calls, imm/disp refs, function starts) in about 3 s; the other tools load it |
| `xref.py [-r] [--callers] TARGET` | code and data references to a string or a VA |
| `xref.py --field 0xOFF [--in FUNC...]` | every `[reg+0xOFF]` use (struct field reads/writes), optionally limited to some functions; the register is not tracked, so filter by function |
| `disasm.py VA [--before N --after N]` | disassembles the function containing VA, annotated with strings, float constants and callee `.cpp` names, message handles as `msg Name`; `--after` stops at the function end (not `dis.py`, which would shadow the stdlib `dis` that capstone imports) |
| `xom.py types/list/dump/check FILE` | XOM type table, containers, one container decoded with its field names (JSON) |
| `tweak.py [-g REGEX]` | every `Tweak/*.XOM` as JSON, keyed by resource name, refs resolved, written to `$W4M_CACHE/tweaks/` |
| `fev.py [-g REGEX]`, `--json`, `--check` | `WormsX.fev` (FMOD Ex FEV1) per-event TSV: loop/oneshot, volume dB (event, sound definition, category), 2D/3D, min/max distance, max playbacks, fades, params, sound definitions with bank and FSB sample index; `--json` writes the full parse to `$W4M_CACHE/fev.json`; `--check` asserts the parse ends at EOF |
| `lua.py FILE.lub [--code] [RE]`, `lua.py --all RE` | Lua 5.0 functions, constants, pseudo-code |
| `acting.py [OUT]` | `WORMACTING.XOM` scenes for the client, to `client/assets/acting.txt` (gitignored; docs/worm-reactions.md) |

How the schema works (see `PE.xclasses` / `PE.schema`):

- The static init stubs call `0x6c3c99` (or `0x6c5015`) once per XOM class, with its name, its parent type and its `Serialize` function. This gives 486 classes, with full names: the XOM type table truncates names to 31 characters.
- Each `Serialize` pushes its field records in file order, then calls its parent's. A container is therefore laid out as `CTNR`, 3 bytes, the derived class's fields, then the base class's fields.
- Every container of every `Tweak/*.XOM`, level databank and language file decodes to its exact end. The one exception is `PERSIST.XOM`'s `WXFE_SoftwareKeyBoardData`.
- Bundles (`Bundl*.xom`) hold untagged containers (resource descriptors, `XGraphSet`, `XAnimClipLibrary`) and omit optional fields (flag 0x20), so `xom.py` walks containers in type order instead of splitting on `CTNR`; `check` is exact on all 475 bundles. Untagged containers dump as raw hex; use `tools/w4m-models` for geometry and clips. A full check of the large bundles is slow (about 3 s each).

Notes and known limits:

- Python API: `PE().v2f(va)` / `f2v(off)` (the CLI names are `va2off` / `off2va`), `PE().msgnames()`.
- `disasm.py` decodes jump tables inline as code; read the table with `pe.py words`.
- `pe.py schema` prints the value names under each enum field whose record has a descriptor (91 of 161; `PE().enum_values(rec)`). `tweak.py` and `xom.py` still print enum values as numbers.
- `disasm.py` annotates XOM class descriptors (16-byte GUID, then name pointer at +0x10) pushed before the create call 0x639b83 as `class Name`.
- Menu databanks are loaded in a fixed order (name table 0x91e15c); `tweak.py` reads files independently, and the Xbox menu file defines the same names.
- The RTK hook rejects `find` with `-o`; use `/usr/bin/find`.
