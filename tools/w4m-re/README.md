# w4m-re: read-only reverse-engineering helpers for Worms 4 Mayhem (PC)

Python 3 tools that read the user's own install. They embed no game data, and their output never goes into the repository.

- Install: `$W4M_DIR`, by default `~/snap/steam/common/.local/share/Steam/steamapps/common/WormsXHD`.
- Cache and output: `$W4M_CACHE`, by default `~/.cache/w4m-re`.
- Dependency: `pip install --user capstone`, needed by `scan.py`, `xref.py` and `disasm.py`.

| tool | use |
|---|---|
| `pe.py` | `sections`, `str REGEX` (strings with their VA), `va2off` / `off2va`, `words VA N` (annotated dwords), `rtti REGEX` (MSVC classes, bases, vtables), `schema CLASS_RE` (serialised XOM fields: index, name, type, struct offset, record, push site) |
| `scan.py` | builds the capstone linear-sweep index (calls, imm/disp refs, function starts) in about 3 s; the other tools load it |
| `xref.py [-r] [--callers] TARGET` | code and data references to a string or a VA |
| `disasm.py VA [--before N --after N]` | disassembles the function containing VA, annotated with strings, float constants and callee `.cpp` names (not `dis.py`, which would shadow the stdlib `dis` that capstone imports) |
| `xom.py types/list/dump/check FILE` | XOM type table, containers, one container decoded with its field names (JSON) |
| `tweak.py [-g REGEX]` | every `Tweak/*.XOM` as JSON, keyed by resource name, refs resolved, written to `$W4M_CACHE/tweaks/` |
| `lua.py FILE.lub [--code] [RE]`, `lua.py --all RE` | Lua 5.0 functions, constants, pseudo-code |

How the schema works (see `PE.xclasses` / `PE.schema`):

- The static init stubs call `0x6c3c99` (or `0x6c5015`) once per XOM class, with its name, its parent type and its `Serialize` function. This gives 486 classes, with full names: the XOM type table truncates names to 31 characters.
- Each `Serialize` pushes its field records in file order, then calls its parent's. A container is therefore laid out as `CTNR`, 3 bytes, the derived class's fields, then the base class's fields.
- Every container of every `Tweak/*.XOM`, level databank and language file decodes to its exact end. The one exception is `PERSIST.XOM`'s `WXFE_SoftwareKeyBoardData`.
- Bundles (`Bundl*.xom`) are not split correctly on `CTNR`. Use `tools/w4m-models` for those.
