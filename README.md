# 4d-plugin-jpegoptim

Reduce the size of JPEG images directly inside 4D. The plugin wraps [jpegoptim 1.4.3 / libjpeg](https://github.com/tjko/jpegoptim) and exposes a single function that can do both lossless optimization and lossy recompression while selectively stripping metadata (EXIF, IPTC, ICC, XMP, COM).

4D `Picture` values can contain multiple image encodings at once. This plugin looks for a `.jpeg` representation inside the picture. If found, it optimizes that representation and returns a new picture containing the optimized JPEG. If no JPEG representation exists, the command returns an empty picture.

> **Important:** The result is always a 4D Picture whose internal codec is JPEG. The byte size is smaller, but the image type stays `.jpeg`.

---

## Summary

| Command | Theme | Description |
| :--- | :--- | :--- |
| [Jpegoptim](#jpegoptim) | Jpegoptim | Lossless or lossy JPEG optimization with metadata stripping |

**Platforms:** macOS (carbon, cocoa) and Windows (win32, win64) - 64-bit Intel and Apple Silicon via universal binary when libjpeg is rebuilt (see CI notes).

---

## Manual source versioning and Build

The two Actions workflows are independent:

1. **Bump version**: select a branch using GitHub's built-in ref selector, then
   choose `patch`, `minor`, or `major`. It validates the selected immutable
   `github.sha` and a clean checkout before writing, and pushes one nonempty,
   source-only direct-child commit to that exact branch. It creates no tag,
   release, build, or dispatch. If the branch moves, dispatch again rather than
   retrying the old selection.
2. **Build**: select the source ref using the built-in selector. There are **no
   custom inputs**. Every job checks out `github.sha`; source metadata determines
   `vX.Y.Z`, `Jpegoptim-X.Y.Z.zip`, and `Jpegoptim-X.Y.Z.dmg`. Build never bumps a
   version and runs on neither branch/PR pushes nor tag pushes. Publication is
   serialized across refs with cancellation disabled.

### Version authority and intake repair

The original committed Bump header at `9cbe65f` explicitly coordinates
`MARKETING_VERSION`, `CURRENT_PROJECT_VERSION`, and the source plist display/build
versions. Its marketing-first implementation did not fulfill that contract.
Rename-follow history contains no later superseding authority header (the
`ae8027f` "Update bump-version.yml" commit has no tree change).

At migration intake `aa95f9650740ef32252ad1281fac3f74c759c484`, all six marketing
assignments and source `Info.plist` short version already agree on **1.0.8**.
Only six stale current-project literals (`1.0.0`) and the two display-version
tokens in source `English.lproj/InfoPlist.strings` (`1.0`) are repaired to that
existing authority. This is **not a version increment**. The localization's UTF-8
BOM, CR endings, 2016 copyright, and executable file modes are preserved.

Subsequent bumps coordinate only `Jpegoptim.xcodeproj/project.pbxproj`,
`Info.plist`, and `English.lproj/InfoPlist.strings`. Versions must be canonical
`X.Y.Z`, consistent across configurations and localized display fields.
`CFBundleShortVersionString` is required; **only `CFBundleVersion` is optional**
and remains absent when absent. Supported `$(MARKETING_VERSION)` /
`${MARKETING_VERSION}` and current-project references are validated and preserved;
cycles, unknown references, duplicates (including quoted/inline project
assignments), missing fields, and drift fail before writes. Unrelated bytes,
individual mixed line endings, BOMs, quoting, and modes are preserved.

Prebuilt bundle metadata/binaries, `locversion.plist`, manifest/schema/4D
counters, constants, and dependency versions are not version authority and are
never co-bumped. The required executable pre-push guard is checked both before
preparation and immediately before push. It validates Git's advertised old
SHA/ref, literal new SHA/HEAD and single parent, and exactly one update (including
rejecting zero-update same-result races). Push is non-force with both
`push.followTags=false` and `--no-follow-tags`. Generated and migration commits
include the Copilot App co-author trailer.

### Native topology and publication gates

The old workflow's `/test/` header did not describe this repository.
`Jpegoptim.vcxproj` Release|x64 OutDir is
`$(ProjectDir)$(ProjectName).4dbase/plugins/$(ProjectName).bundle/contents/windows64/`;
Xcode SYMROOT is `$(SRCROOT)/$(PRODUCT_NAME).4dbase/Plugins`. Build uses
`Jpegoptim.4dbase/Plugins/Jpegoptim.bundle`, requiring the exact Windows output and
the exact downloaded `winbuild/Jpegoptim.4DX` rather than a recursive stale match.

Windows retains Release|x64, v143 and Windows SDK 10.0 overrides. macOS retains
target Jpegoptim, configuration Default, `arm64 x86_64`, `ONLY_ACTIVE_ARCH=NO`,
deployment override 11.0, and disabled build-time signing. Native project
platforms/toolsets, 11.5/10.8 settings, Cocoa/AppKit/CoreGraphics/static libjpeg,
and resource phases are unchanged. Source localized strings are copied by the
Resources phase; manifest/constants by CopyFiles (Resources).

Cleanup invalidates only generated executable, copied plist/localization,
signature, and intermediates; committed resources, QuickLook, documentation,
dependencies and Windows contents survive the macOS build. Cleanup failures are
fatal. Exact required architecture tokens reject subtype substitutions, without
inventing an extra-slice prohibition. Built plist (XML/binary) and localized
strings (normal UTF-8, UTF-16, XML/binary plist) must match selected source.

Developer ID signing and strict deep verification remain required, as do
successful ZIP **and** DMG notarization commands with JSON status `Accepted`,
followed by successful DMG stapling. Secret **names** are unchanged:
`APPLE_DEVELOPER_ID_CERTIFICATE`, `APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD`,
`KEYCHAIN_PASSWORD`, `NOTARYTOOL_APPLE_ID`, `NOTARYTOOL_TEAM_ID`,
`NOTARYTOOL_PASSWORD`.

Authenticated availability checks run early and again **after all native gates**.
They require exact remote tag absence and paginate every releases page, including
drafts; HTTP/auth/network/malformed/later-page failures are fatal, not absence.
Publication separately POSTs a new `git/refs` tag at the selected SHA, verifies
the advertised remote SHA, POSTs a new public non-prerelease release with explicit
tag/name/target and the original empty body/no generated notes, then uploads
assets separately to the **returned new release ID**. Existing tags/releases,
even at the same SHA, are never reused or updated.

### Partial-publication recovery

Publication is create-only, **not transactional**. A failed operation retains its
tag, release and already uploaded assets; diagnostics print the created tag/SHA
and release ID when known. There is no PATCH, DELETE, clobber, automatic retry,
rollback, or combined release-with-assets creation.

After failure, stop and inspect the Actions log, exact remote tag SHA, draft and
public releases, returned release ID and uploaded assets. A transport failure may
have created remote state even without a response. Do not rerun Build expecting
repair. Preserve that evidence for manual maintainer recovery. A new source-only
bump and explicitly selected Build can publish a different unused version after
the failure is understood; nothing in these workflows deletes or repairs the
partial version.

### Offline verification

Run `actionlint .github/workflows/*.yml` and
`PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s .github/tests -v`.
PowerShell (`pwsh`) is required to exercise the Windows harness. Tests execute
actual extracted workflow blocks with a closed PATH and native stubs only,
including Git advertised-state races in local bare repositories, paginated API
failures, retained partial publication, no-write invalid metadata, and byte/mode
preservation. They do not compile, sign, notarize, run 4D, dispatch Actions, or
modify production tags/releases. Fixtures derive versions from source and remain
valid after a genuine guarded bump.

The adaptation was reviewed against pinned `.github` workflows, helpers, hooks
and tests from `miyako/4d-plugin-native-encoding-detection` at
`c42ca565857f674bde5ea6ed3435c87375eda4a7`, `miyako/4d-plugin-file-information` at
`19848973601acfd3a2b5070230fa40cdc7e4edb2`,
`miyako/4d-plugin-common-string-algorithm` at
`2606a68201866ec9fcbb38b51462d79215f504ef`, `miyako/4d-plugin-window-button` at
`3f14facb9aaaaff0e22eeb8e07531163680c3a8a`, `miyako/4d-plugin-raw-picture-data` at
`8144537fdc8dcc3d0c053dfe97aff0a8c0c3a6f9`, and `miyako/4d-plugin-PRNG` at
`522f62e1e9f388be0f3f226c1d4c39362dad2491`; repository-specific authority,
localization, resource topology, and native gates are preserved rather than
assuming another plugin's layout.

---

## Native ownership maintenance

The bounded audit at source baseline
`72a947143a0d5478a604aff446b848eccadef381` covers all tracked native entry,
helper, wrapper and header surfaces, including dormant code compiled by both
projects. It does not certify every allocation-failure path.

| Surface | Reachability and ownership findings |
| :--- | :--- |
| `4DPlugin.cpp`, `4DPlugin.h` | Both projects compile the entry, dispatcher, JPEG helpers and command. `FourDPackex` in the SDK (Windows `.def`, macOS visibility attribute) calls `PluginMain`; selector 1 alone dispatches `Jpegoptim(&P;&L;&L):P`, matching source and bundled manifests. Its final created picture is handed to the caller; no text-array command is exported. Picture-data type strings are used only after a successful error check; their ownership is deferred below. |
| `Classes/C_PICTURE.{cpp,h}` | Compiled but not called by the sole dispatcher command. Path input now has exactly one argument owner; the loaded output is retained before its argument is cleared. `setBytes` releases the temporary after retaining. Conversion, grayscale, size and thumbnail inputs start Undefined before shallow setters, avoiding overwritten empty owners. Conversion divides returned bytes by UTF-16 unit size. Borrowed size/thumbnail inputs still detach before cleanup; conversion/grayscale/thumbnail results still transfer an owning reference. |
| `Classes/C_TEXT.{cpp,h}`, `ARRAY_TEXT.{cpp,h}` | Compiled, transitively included by the umbrella SDK header. Inputs copy borrowed text into C++ strings. Scalar outputs and returns use the engine content setter, not descriptor transfer. Array output independently creates each element and correctly leaves accepted transfers owned by the array; there is no immediate post-transfer disposal defect here. Initial creation and resize failure handling remain deferred. |
| `Classes/C_POINTER.{cpp,h}` | Compiled dormant metadata/reference wrapper. `GetPointerValue` ownership and relabeling are ambiguous; unchanged, not certified safe. |
| `Classes/C_BLOB.{cpp,h}` | Compiled dormant byte-vector/handle wrapper and hex/base64 helpers. Text uses copied `C_TEXT` contents; no string descriptor transfer. General BLOB allocation/algorithm hardening is outside this audit. |
| `Classes/C_DATE`, `C_INTEGER`, `C_LONGINT`, `C_REAL`, `C_TIME` (`.cpp` and `.h`) | All compiled. Primitive scalar parameter/return wrappers, no owning string descriptor. Only `C_LONGINT` is used directly by the command. |
| `Classes/ARRAY_BOOLEAN`, `ARRAY_DATE`, `ARRAY_INTEGER`, `ARRAY_LONGINT`, `ARRAY_REAL`, `ARRAY_TIME` (`.cpp` and `.h`) | All compiled dormant numeric/date array wrappers; copied primitive contents, no string descriptor cleanup. Initial creation/resize failure concerns are not excluded by their scalar element kinds. |
| `Classes/C_TYPES.h`, `4D Plugin_Prefix.pch` | Standard string/vector typedefs, platform framework imports and Carbon prefix; no runtime string owner. |
| `4DPluginAPI.{c,h,def}`, `EntryPoints.h`, `PrivateTypes.h`, `PublicTypes.h`, `Flags.h`, SDK licence | Bundled SDK engine/ABI boundary, excluded from production edits. The full engine implementation is compiled unchanged by the regression, not replaced with API snippets. |
| `include/{jconfig,jerror,jmorecfg,jpeglib,jpegoptim}.h`, `include/{jpegdest,jpegoptim,misc}.c`, `a/libjpeg.a`, `lib/windows64/jpeg.lib` | Third-party codec/configuration inputs and libraries, excluded from production fixes. The native projects compile the first-party plugin implementation and link static libjpeg; the alternative codec C files are tracked but not native Sources/ClCompile entries. |
| Both native projects, solution/filters, plist/localization, constants, source/bundled manifests and prebuilt plugin | Inspected for build/include/export reachability; unchanged. Example databases, user preferences, private documents and image files are not audit inputs. |

**SDK provenance:** the exact bundled engine Git blob is
`478081f0238f6bc5393222f574bc2656f010b67b`; its umbrella header blob is
`9054b45ffc7ff6f5b8d57529a3788a55d977b314`. Rename-follow history traces the
engine through `9cbe65fe157d81420f380268600923aba4e459e1` to repository
introduction `b3b9e9759c9f2e1413b4e51720c38f09908baac5`. The engine's `v14`
and header's `v13` comments do not identify a verified upstream release. This
engine differs from the Poppler skill reference blob
`62f745e91d81ef2c3c901c656ea8e175d3b173e8`. Guidance was read from
`miyako/skills/4d-skills/skills/4dplugin/SKILL.md` at `e6b492f`.

**Exact local contracts:** `PA_SetStringInArray` accepts only
`eVK_ArrayUnicode`, a non-null data handle, an index from zero through
`fNbElements` inclusive, and a non-null string descriptor pointer. It disposes
the old element and shallow-transfers the descriptor. Rejection retains the
caller owner. `PA_GetStringInArray` is borrowed. Scalar string/picture variable
setters shallow-alias and do not dispose old contents. Creating a typed
string/picture variable can allocate an empty owner; use Undefined when the
next operation is a shallow setter. `PA_ReturnString` instead calls
`PA_SetUnistring`, forwarding content to the engine. All wrapper command calls
are synchronous procedures; there are no first-party method callbacks, JSON
Parse inputs, or object/collection getter calls in this older SDK integration.
`PA_ExecuteCommandByID` returns raw kind 0 if its argument-handle allocation
fails (normalizing it can incorrectly classify it as string); do not clear
that sentinel. Method allocation failure returns Undefined instead.

**Regression:** on macOS, run
`PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s .github/tests -p test_ownership.py -v`.
The harness compiles all 16 actual wrapper translation units and the full,
hash-pinned SDK engine with ASan/UBSan. Alignment instrumentation alone is
disabled for the SDK's intentional packed ABI; macOS LSan is unavailable,
so explicit string-allocation, picture-reference and handle balances must
all reach zero. The engine-boundary shim preserves byte input/output counts,
UTF-16 units, capacity including room for termination, allocated/null empties,
shallow scalar ownership and conditional array transfer. It models synchronous
picture commands with synthetic objects; it never opens a path or image.
ASCII, BMP, supplementary and empty text, independent outputs, borrowed copies,
replacement/shrink/clear/rejections, scalar content returns, surviving picture
handoffs and command-handle failures are exercised.

The same harness can target an immutable `git archive` extraction with
`OWNERSHIP_SOURCE_ROOT=/absolute/path/to/extraction`. Baseline has eight named
defect-test failures: path single-disposal/output retention, `setBytes`
temporary release, copy byte/unit boundary (ASan heap-buffer-overflow for
`.png`), copy initial owners, grayscale initial owner, size initial owner,
thumbnail initial owner, and command-handle failure argument cleanup. Normal arrays and scalar content copies already
pass baseline. The fix passes these same assertions. The `Native ownership`
workflow runs only this unsigned shim on `macos-latest`; existing manual Build
and Bump workflows and their signing/publication behavior are untouched.

**Deferred, not certified:** `ARRAY_TEXT::toParamAtIndex` and the numeric array
family publish created descriptors without a reliable initial-allocation error
boundary. On initial failure a null array handle rejects string insertions;
the wrapper must not publish success or lose the original parameter. The void
wrappers have no established error return, and `PluginMain` swallows exceptions;
safe error propagation requires a separate design. SDK resize updates the count
even when handle resize fails, so normal array tests do **not** certify
resize-OOM safety. SDK string creation does not update `PA_GetLastError`;
string/picture creation failures are not covered by the command-handle failure
test. Pointer metadata, SDK-only application-path outputs and picture-data
returned-string ownership remain unresolved. No SDK internals, codec algorithms,
manifests, projects or binaries were changed. These shim results do not certify
Windows or an actual 4D host, and no release build/sign/notarize/package/upload
or manual workflow dispatch is performed by this validation.

## Requirements

- 4D v15 or later (uses `PA_CreatePicture`, `PA_GetPictureData`, `PA_YieldAbsolute`).
- Input picture must contain a `.jpeg` encoding. Use `READ PICTURE FILE` or `CONVERT PICTURE` to ensure JPEG data exists.
- No special macOS permissions required - pure CPU codec, no screen capture.
- Output picture encoding is JPEG. Use `WRITE PICTURE FILE`, `CONVERT PICTURE`, or `TRANSFORM PICTURE` afterwards if you need scaling or format conversion.

---

## Constants for metadata stripping

You build the second parameter as a bitmask. Setting a bit **means strip** that chunk.

| Constant | Value | What it strips |
| :--- | :--- | :--- |
| `JPEG_STRIP_XMP` | 1 | Adobe XMP metadata (`http://ns.adobe.com/xap/1.0/`) |
| `JPEG_STRIP_ICC` | 2 | ICC color profile (`ICC_PROFILE`) |
| `JPEG_STRIP_IPTC` | 4 | IPTC metadata (APP13) |
| `JPEG_STRIP_EXIF` | 8 | EXIF camera data |
| `JPEG_STRIP_COM` | 16 | JPEG comment markers |

To strip everything, combine all:

```4d
$strip:=JPEG_STRIP_COM | JPEG_STRIP_EXIF | JPEG_STRIP_ICC | JPEG_STRIP_IPTC | JPEG_STRIP_XMP
```

To **keep** EXIF but strip the rest:

```4d
$strip:=JPEG_STRIP_COM | JPEG_STRIP_ICC | JPEG_STRIP_IPTC | JPEG_STRIP_XMP  // EXIF not included = kept
```

If you pass `0`, the plugin treats it as "strip all" for compatibility with the original jpegoptim behavior.

---

## Jpegoptim

### Syntax

```4d
$result:=Jpegoptim (picture ; stripOptions ; quality) -> Picture
```

C signature from manifest: `Jpegoptim(&P;&L;&L):P`

### Parameters

| Parameter | Type | Description |
| :--- | :--- | :--- |
| `picture` | Picture | Source picture. Must contain a `.jpeg` representation. Mandatory. |
| `stripOptions` | Longint | Bitmask of `JPEG_STRIP_*` constants to strip. Pass `0` to strip all. |
| `quality` | Longint | `0` = lossless optimization. `1`..`100` = lossy recompression. Pass `0` for best quality preservation. |
| `Result` | Picture | Optimized JPEG picture. Empty picture if input has no JPEG data or on failure. |

### Description

**On all platforms**, `Jpegoptim` does:

1. Extracts the `.jpeg` bytes from the 4D picture via `PA_GetPictureData`. If the picture holds only PNG/TIFF, it returns empty - it does **not** convert formats.
2. If `quality = 0` (lossless): reads DCT coefficients with `jpeg_read_coefficients`, copies critical parameters, writes markers selectively based on `stripOptions`, and writes coefficients with `optimize_coding=TRUE` and progressive enabled. No pixel data is re-decoded, so quality is preserved.
3. If `quality = 1..100`: fully decodes the JPEG to bitmap (`jpeg_start_decompress` / `jpeg_read_scanlines`), then re-encodes with `jpeg_set_quality(quality, TRUE)`, `jpeg_simple_progression`, and `optimize_coding=TRUE`. This is lossy and reduces size further but degrades image data.
4. Returns a new picture via `PA_CreatePicture`. The underlying bytes are JPEG.

**Metadata handling:** For each marker in the source file (COM, APP1 EXIF/XMP, APP2 ICC, APP13 IPTC), the plugin checks `save_* = !(stripOptions & STRIP_*)`. JFIF APP0 and Adobe APP14 markers are always skipped to avoid duplicates because libjpeg emits them automatically.

**Yielding:** Inside both decompression and compression scanline loops the plugin calls `PA_YieldAbsolute()` every 16 lines to keep 4D responsive on large images.

**Limits (fixed version):** Rejects images with width/height 0 or >20000 px, input >100 MB, or output >200 MB to prevent decompression bombs. All `malloc`/`realloc` results are checked.

### Example

From the plugin's own `README.md` test:

```4d
  //read jpeg file
$path:=Get 4D folder(Current resources folder)+"image.jpg"
READ PICTURE FILE($path;$image)

  //by default, all are stripped
$strip:=JPEG_STRIP_COM | JPEG_STRIP_EXIF | JPEG_STRIP_ICC | JPEG_STRIP_IPTC | JPEG_STRIP_XMP

  //0:lossless optimisation. 1...100:lossy optimisation
$quality:=0

$folderPath:=System folder(Desktop)+Generate UUID+Folder separator
CREATE FOLDER($folderPath;*)

For ($quality;0;100)
  $jpeg:=Jpegoptim($image;$strip;$quality)
  WRITE PICTURE FILE($folderPath+String($quality)+".jpg";$jpeg)
End for
```

#### Keeping EXIF for photos while stripping the rest (common production use)

```4d
$srcPath:="Macintosh HD:Images:photo.jpg"
READ PICTURE FILE($srcPath;$pic)

If (Picture type($pic)=".jpeg")
  $strip:=JPEG_STRIP_COM | JPEG_STRIP_ICC | JPEG_STRIP_IPTC | JPEG_STRIP_XMP  // keep EXIF
  $optimized:=Jpegoptim($pic;$strip;0)  // lossless

  If (Picture size($optimized)>0)
    WRITE PICTURE FILE($srcPath;$optimized)
  End if
End if
```

#### Batch lossy recompression to 75 quality

```4d
C_COLLECTION($files)
$files:=Folder($inFolder).files()

For each ($file;$files)
  READ PICTURE FILE($file.platformPath;$p)
  $small:=Jpegoptim($p;0;75)  // 0 = strip all, 75 = quality 75
  WRITE PICTURE FILE($outFolder+$file.name;$small)
End for each
```

---

## Error handling & troubleshooting

- **Empty picture returned:** Means the source picture had no `.jpeg` codec. Check with `Picture type` or force conversion: `CONVERT PICTURE($pic;".jpeg")` before calling. Also returned on corrupt JPEG in fixed version (previous version would crash).
- **Still large file after lossless:** JPEG already optimized. Try lossy `quality 80..90`. Lossless can only remove metadata and optimize Huffman tables (~5-15% savings).
- **EXIF orientation lost after stripping:** If you strip EXIF, you also strip orientation flag. Image may appear rotated. Either keep EXIF or apply `TRANSFORM PICTURE` with orientation correction before optimizing.
- **Freeze on huge image:** Fixed version caps at 20000 px side and 100 MB input. Old version called `malloc(width*height*components)` without checks - a malicious 100k x 100k JPEG would attempt ~10 GB allocation and freeze 4D. Update to fixed version.
- **Crash on malformed JPEG:** Original `my_error_exit` was empty. Any corrupt file would continue execution with invalid struct -> access violation. Fixed version restores `longjmp` error handling and safely destroys libjpeg objects.
- **Progressive JPEG output:** The plugin always enables `jpeg_simple_progression` and `optimize_coding`. Result is progressive JPEG, which is smaller but some very old viewers may not support it.
- **Picture not smaller after stripping:** If source had no metadata, stripping does nothing. Check marker presence with external tool like `exiftool`.

---

## Quick reference

```4d
  // Constants - define once in a startup method
  // JPEG_STRIP_XMP:=1; JPEG_STRIP_ICC:=2; JPEG_STRIP_IPTC:=4; JPEG_STRIP_EXIF:=8; JPEG_STRIP_COM:=16

  // Lossless, strip all metadata (smallest, same visual quality)
$lossless:=Jpegoptim($picture;0;0)

  // Lossless, keep EXIF
$strip:=JPEG_STRIP_COM | JPEG_STRIP_ICC | JPEG_STRIP_IPTC | JPEG_STRIP_XMP
$keepExif:=Jpegoptim($picture;$strip;0)

  // Lossy at quality 80, strip all
$lossy80:=Jpegoptim($picture;0;80)

  // Batch
For ($q;0;100;10)
  $out:=Jpegoptim($in;$strip;$q)
  WRITE PICTURE FILE($folder+String($q)+".jpg";$out)
End for
```
