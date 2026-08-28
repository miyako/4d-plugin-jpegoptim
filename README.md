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
