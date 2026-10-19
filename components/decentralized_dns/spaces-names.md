# Spaces Protocol — Name Format Reference

Compact reference for parsing, validating and displaying Spaces names (`@space`, `handle@space`, numeric `#…` labels).
Ground truth: `spacesprotocol/spaces` → `protocol/src/slabel.rs`, `protocol/src/sname.rs`, `protocol/src/constants.rs`; spec §2 and §14 at <https://spacesprotocol.org/docs/developers/spec/>. Test vectors below are taken from those files' `#[cfg(test)]` blocks.

## Name forms

| Form | Example | Labels | Meaning |
|---|---|---|---|
| Top-level space | `@bitcoin` | 1 | Auctioned on-chain |
| Handle (subspace) | `alice@bitcoin` | 2 | Issued off-chain by the space operator |
| Nested handle | `hello.bob@bitcoin` | 3+ | Labels left of `@`, dot-separated |
| Numeric (num) | `#800000-3-1` | 1 | `#<block>-<txpos>-<vout>`; created on-chain, never auctioned |
| Handle on a num | `alice#12-12-0`, `key.wallet#800000-3-1` | 2+ | No `@` before a numeric final label |

The **last label is always the space** (alphabetic or numeric); everything before it is the subspace path.

## ABNF (RFC 5234)

```abnf
name         = space-name / handle / num-name / num-handle
space-name   = "@" label
handle       = path "@" label
num-name     = num-label
num-handle   = path num-label
path         = label *( "." label )

label        = puny-label / ldh-label          ; 1–62 octets total
ldh-label    = alnum *( [ "-" ] alnum )        ; no leading/trailing "-", no "--"
puny-label   = "xn--" ldh-label                ; "--" allowed only as this prefix
alnum        = %x61-7A / DIGIT                 ; a–z, 0–9 only (lowercase)

num-label    = "#" 1*DIGIT "-" 1*DIGIT "-" 1*DIGIT   ; block-txpos-vout, ≤ 62 octets
DIGIT        = %x30-39
```

Notes the grammar alone doesn't carry:
- **No case folding.** Uppercase is rejected, not lowercased. Normalize user input *before* parsing.
- **No Unicode.** IDNs must be punycoded by the caller (`xn--…`); `@café` is invalid.
- **`@`-part is a single label** — it may not contain `.`.
- **Reserved names** `example`, `test`, `local` parse fine but cannot be *opened for auction*. Numeric labels likewise cannot be auctioned.
- Numeric parts are digit strings; the parser does not range-check them (`#4294967295-65535-65535` is valid).

## Wire format

DNS-style length-prefixed labels.

```abnf
slabel-wire  = len label-octets                ; a single label, NO terminator
sname-wire   = *( len label-octets ) %x00      ; full name, null-terminated, ≤ 255 octets
len          = %x01-3E                         ; 1–62
```

| Text | Wire |
|---|---|
| `@bitcoin` (SLabel) | `\x07bitcoin` |
| `@example` (OP_OPEN payload after magic) | `\x07example` |
| `bob@bitcoin` (SName) | `\x03bob\x07bitcoin\x00` |
| `hello.bob@bitcoin` (SName) | `\x05hello\x03bob\x07bitcoin\x00` |
| empty/root SName | `\x00` |

Display: join all labels but the last with `.`, then `@` + last label — unless the last label starts with `#`, in which case it is appended directly. Top-level SLabels display as `@name`; numeric SLabels display without `@`.

## Constants

| Constant | Value | Source |
|---|---|---|
| `MAX_LABEL_LEN` | 62 | slabel.rs, sname.rs |
| `MAX_SPACE_LEN` (full SName) | 255 | sname.rs |
| `PUNYCODE_PREFIX` | `xn--` | slabel.rs |
| `RESERVED_SPACES` | `example`, `test`, `local` | constants.rs |
| `OPEN_MAGIC` (precedes wire name in OP_OPEN) | `0xDEDEDEDE01` | spec §4 |
| `NUM_HRP` (bech32m NumId, not a display name) | `num` | spec §10.2 |

## Test vectors

**Valid**
```
@bitcoin            @my-space           @123              @example-ok-ok2-ok3
@xn                 @xn--1              @xn--y9jia        @xn--123-pretty-valid-space-ok
bob@bitcoin         my-handle@bitcoin   a-b-c@my-space    456@123
a1b2@c3d4           hello.bob@bitcoin   alice@xn--y9jia   xn--y9jia@bitcoin
#800000-3-1         #0-0-0              #1-1-0            #4294967295-65535-65535
#12-12-0            alice#12-12-0       key.wallet#800000-3-1
62-char label       (e.g. "@" + "a"×62)
```

**Invalid** (reason)
```
example / bitcoin       no "@" or "#" sigil
@                       empty label
@EXAMPLE  @Bitcoin  Alice@bitcoin    uppercase
@exampl3$  @bit_coin  al!ce@bitcoin  @bit coin   bad character
@café                   non-ASCII
@test\ntest  @test\x00test           control byte
@example--ok  @bit--coin  al--ice@bitcoin  @ab--cd   "--" outside xn-- prefix
@-ok  @ok-  @-bitcoin  @bitcoin-  -alice@bitcoin  alice-@bitcoin   edge hyphen
@xn-  @xn--  @xn---     bad/empty punycode
@0xn--1  @xxn--test     "--" not at the prefix position
hey..bob@bitcoin        empty inner label
63-char label           exceeds MAX_LABEL_LEN
#  #-  #123  #123-4  #12-12            missing parts
#-3-1  #3--1  #3-4-     empty part
#abc-3-1  #3-abc-1  #3-4-abc  # 3-4-1  non-digit
#3-4-5-6                four parts
#12-12-0#3-4-0          two numeric labels
```

**Wire-level**
```
valid:   \x00 (root)   \x03bob\x00   \x03bob\x07bitcoin\x00   \x09xn--y9jia\x00
         \x03bob\x00\x03foo  → parses as "bob" (trailing bytes after terminator ignored)
invalid: (empty)   \x03bob (no terminator)   \x03bob\x04foo\x00 (length overruns)
         \x03Bob\x00   \x04-bob\x00   \x04bob-\x00   \x05b--ob\x00
         SLabel: \x00  \x01  \xff  \x07exam (length > content)
         SLabel: \x02exam → reads as "ex" (extra bytes ignored)
```

## Deep handles and numeric aliases (libveritas)

On-chain/certificate state is flat (2 labels). `libveritas/veritas/src/names.rs` → `NameResolver` maps between human hierarchical names and the flat numeric form using a numeric alias per handle:

- `alice@bitcoin` ↔ `#800-12-12`
- `pancakes.nested1.alice@bitcoin` → `flatten()` → `pancakes#822-88-22`; `expand()` reverses it for display.

Treat the human form as display/input and the flattened form as what goes into messages and proofs.
