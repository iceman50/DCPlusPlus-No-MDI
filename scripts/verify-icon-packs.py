"""Validate pack coverage, ICO transparency and Windows loading (stdlib only).

Copyright (C) 2026 iceman50
Run on Windows: python scripts/verify-icon-packs.py
"""
import argparse
import ctypes
from ctypes import wintypes
import pathlib
import re
import struct
import tempfile
import xml.etree.ElementTree as ET
import zipfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKS = ("Dark", "Aurora", "Copper", "Paper", "Neon-Circuit")
SIZES = (16, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_frames(data, custom):
    reserved, kind, count = struct.unpack_from("<HHH", data)
    require((reserved, kind) == (0, 1) and count > 0, "Invalid ICO header")
    sizes = []
    end = 6 + count * 16
    for i in range(count):
        w, h, colors, reserved, planes, depth, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + i * 16)
        w, h = w or 256, h or 256
        require(w == h and offset == end and length > 0, "Invalid frame directory")
        require(offset + length <= len(data), "Frame outside ICO")
        frame = data[offset:offset + length]
        end = offset + length
        sizes.append(w)
        if not custom:
            continue
        require(planes == 1 and depth == 32 and colors == 0 and reserved == 0, "Invalid RGBA frame")
        if frame.startswith(b"\x89PNG\r\n\x1a\n"):
            require(w == 256, "Only 256 px frames should be PNG")
            pw, ph, bits, color_type = struct.unpack_from(">IIBB", frame, 16)
            require((pw, ph, bits, color_type) == (w, h, 8, 6), "Invalid PNG dimensions or alpha")
        else:
            header, dw, dh, dp, db, compression = struct.unpack_from("<IiiHHI", frame)
            require((header, dw, dh, dp, db, compression) == (40, w, h * 2, 1, 32, 0), "Invalid DIB")
            mask_stride = ((w + 31) // 32) * 4
            require(len(frame) == 40 + w * h * 4 + mask_stride * h, "Invalid DIB length")
            pixels = frame[40:40 + w * h * 4]
            mask = frame[40 + w * h * 4:]
            alpha = pixels[3::4]
            require(max(alpha) > 200 and min(alpha) == 0, "Blank or opaque icon")
            require(any(0 < a < 255 for a in alpha), "Missing antialiasing")
            require(sum(a > 32 for a in alpha) > w * h * .035, "Icon too sparse")
            for y in range(h):
                for x in range(w):
                    a = alpha[y * w + x]
                    transparent = bool(mask[y * mask_stride + x // 8] & (0x80 >> (x % 8)))
                    require(transparent == (a == 0), "Alpha/AND mask mismatch")
                    if x in (0, w - 1) or y in (0, h - 1):
                        require(a < 32, "Artwork clipped at frame boundary")
    require(end == len(data), "Trailing ICO bytes")
    if custom:
        require(tuple(sizes) == SIZES, "Missing DPI sizes")
    return count


def png_rgba(data):
    """Decode the RGBA PNG logo frames without a third-party imaging dependency."""
    w, h, bits, kind, compression, filtering, interlace = struct.unpack_from(">IIBBBBB", data, 16)
    require((bits, kind, compression, filtering, interlace) == (8, 6, 0, 0, 0), "Unsupported logo PNG")
    payload = bytearray()
    offset = 8
    while offset < len(data):
        length, chunk = struct.unpack_from(">I4s", data, offset)
        if chunk == b"IDAT":
            payload.extend(data[offset + 8:offset + 8 + length])
        offset += length + 12
    packed = zlib.decompress(payload)
    stride = w * 4
    require(len(packed) == h * (stride + 1), "Invalid PNG pixels")
    output = bytearray()
    previous = bytearray(stride)
    for y in range(h):
        start = y * (stride + 1)
        method = packed[start]
        row = bytearray(packed[start + 1:start + 1 + stride])
        require(method in range(5), "Invalid PNG filter")
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            up = previous[x]
            corner = previous[x - 4] if x >= 4 else 0
            p = left + up - corner
            distances = (abs(p - left), abs(p - up), abs(p - corner))
            paeth = (left, up, corner)[distances.index(min(distances))]
            predictor = (0, left, up, (left + up) // 2, paeth)[method]
            row[x] = (row[x] + predictor) & 255
        output.extend(row)
        previous = row
    return output


def check_orb_logo(data, pack):
    """Logos retain transparent negative space, colored orbs and a graphite orb."""
    count = struct.unpack_from("<H", data, 4)[0]
    for i in range(count):
        w, h, _, _, _, _, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + i * 16)
        if w or h:
            continue
        rgba = png_rgba(data[offset:offset + length])
        pixels = [tuple(rgba[i:i + 4]) for i in range(0, len(rgba), 4)]
        # These points lay on the old white surround and must now be empty.
        for x, y in ((128, 12), (12, 128), (128, 244)):
            require(pixels[y * 256 + x][3] < 16, "White surround or backing remains")
        opaque = [p for p in pixels if p[3] > 200]
        def theme_color(r, g, b):
            if pack == "Copper":
                return r > 120 and r > g * 1.2 and g > b * 1.2
            if pack == "Aurora":
                return g > 100 and b > 100 and min(g, b) > r * 1.5
            return b > 100 and b > r * 1.3 and b > g * 1.1
        colored = sum(theme_color(r, g, b) for r, g, b, _ in opaque)
        graphite = sum(max(r, g, b) < 95 for r, g, b, _ in opaque)
        require(colored > len(opaque) * .3, f"{pack} colored orbs missing")
        require(graphite > len(opaque) * .08, "Graphite orb missing")
        require(len(opaque) > 256 * 256 * .42, "Orb group is undersized")
        return
    raise ValueError("Orb logo is missing its 256px frame")


def check_corner_overlay(data, name):
    corners = {
        "UserBot.ico": (False, False), "UserOp.ico": (True, False),
        "UserReg.ico": (True, False), "UserNoCon.ico": (False, True),
        "UserNoSlot.ico": (True, True),
    }
    if name not in corners:
        return
    right, bottom = corners[name]
    for i in range(struct.unpack_from("<H", data, 4)[0]):
        w, h, _, _, _, _, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + i * 16)
        w, h = w or 256, h or 256
        frame = data[offset:offset + length]
        if frame.startswith(b"\x89PNG"):
            alpha = png_rgba(frame)[3::4]
        else:
            rows = frame[43:40 + w * h * 4:4]
            alpha = b"".join(rows[y * w:(y + 1) * w] for y in range(h - 1, -1, -1))
        for y in range(h):
            for x in range(w):
                in_corner = (x >= w / 2) == right and (y >= h / 2) == bottom
                require(in_corner or alpha[y * w + x] <= 16,
                        f"{name}: overlay obscures the base outside its corner at {w}px")


class ICONINFO(ctypes.Structure):
    _fields_ = [("fIcon", wintypes.BOOL), ("xHotspot", wintypes.DWORD),
                ("yHotspot", wintypes.DWORD), ("hbmMask", wintypes.HBITMAP),
                ("hbmColor", wintypes.HBITMAP)]


class BITMAP(ctypes.Structure):
    _fields_ = [("bmType", wintypes.LONG), ("bmWidth", wintypes.LONG),
                ("bmHeight", wintypes.LONG), ("bmWidthBytes", wintypes.LONG),
                ("bmPlanes", wintypes.WORD), ("bmBitsPixel", wintypes.WORD),
                ("bmBits", ctypes.c_void_p)]


def native_loader():
    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)
    user.LoadImageW.argtypes = [wintypes.HINSTANCE, wintypes.LPCWSTR, wintypes.UINT,
                               ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user.LoadImageW.restype = wintypes.HANDLE
    user.DestroyIcon.argtypes = [wintypes.HANDLE]
    user.GetIconInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(ICONINFO)]
    gdi.GetObjectW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p]
    gdi.DeleteObject.argtypes = [wintypes.HANDLE]

    def load(path, size):
        handle = user.LoadImageW(None, str(path), 1, size, size, 0x10)
        require(handle, f"LoadImageW failed: {path.name} at {size}px ({ctypes.get_last_error()})")
        info = ICONINFO()
        try:
            require(user.GetIconInfo(handle, ctypes.byref(info)), "GetIconInfo failed")
            bitmap = BITMAP()
            require(gdi.GetObjectW(info.hbmColor, ctypes.sizeof(bitmap), ctypes.byref(bitmap)), "GetObjectW failed")
            require((bitmap.bmWidth, bitmap.bmHeight) == (size, size), "Wrong native image dimensions")
        finally:
            if info.hbmColor:
                gdi.DeleteObject(info.hbmColor)
            if info.hbmMask:
                gdi.DeleteObject(info.hbmMask)
            user.DestroyIcon(handle)
    return load


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-directory", type=pathlib.Path, default=ROOT / "IconPacks")
    parser.add_argument("--pack", choices=PACKS, action="append", help="Check only the selected pack(s)")
    args = parser.parse_args()
    expected = {p.name for p in (ROOT / "res").glob("*.ico")}
    mapped = set(re.findall(r'"([A-Za-z]+\.ico)"', (ROOT / "win32/IconManager.cpp").read_text()))
    require(mapped == expected, "Compiled icon mapping and source files differ")
    load = native_loader()
    total_frames = total_native = 0
    with tempfile.TemporaryDirectory(prefix="dcico-check-") as temp:
        for pack in args.pack or PACKS:
            try:
                path = args.pack_directory / f"{pack}.dcico"
                require(path.stat().st_size < 32 * 1024 * 1024, "Archive exceeds loader limit")
                with zipfile.ZipFile(path) as archive:
                    require(archive.testzip() is None, "Corrupt ZIP")
                    manifest = archive.read("info.xml")
                    require(len(manifest) < 256 * 1024, "Manifest exceeds loader limit")
                    root = ET.fromstring(manifest)
                    require(root.tag == "dcico" and root.findtext("Name") == pack.replace("-", " "), "Wrong manifest identity")
                    require(root.findtext("Scheme") == ("light" if pack == "Paper" else "dark"), "Wrong appearance")
                    require(root.findtext("Version") == ("2.0" if pack in ("Dark", "Neon-Circuit") else "1.0"), "Wrong version")
                    entries = root.findall("./Icons/Icon")
                    require(len(entries) == len(expected) and {e.attrib["Name"] for e in entries} == expected, "Incomplete pack")
                    members = {"info.xml", "README.txt"} | {f"icons/{name}" for name in expected}
                    require(set(archive.namelist()) == members and len(archive.namelist()) == len(members), "Unexpected/duplicate members")
                    for info in archive.infolist():
                        require(info.date_time == (2026, 1, 1, 0, 0, 0), "Unstable ZIP timestamp")
                    for entry in entries:
                        name = entry.attrib["Name"]
                        require(entry.attrib["File"] == f"icons/{name}", "Wrong member mapping")
                        data = archive.read(entry.attrib["File"])
                        require(len(data) < 4 * 1024 * 1024, "ICO exceeds loader limit")
                        try:
                            total_frames += check_frames(data, True)
                            check_corner_overlay(data, name)
                            if name == "DCPlusPlus.ico":
                                check_orb_logo(data, pack)
                            icon_path = pathlib.Path(temp) / name
                            icon_path.write_bytes(data)
                            for size in SIZES:
                                load(icon_path, size)
                                total_native += 1
                        except Exception as error:
                            raise ValueError(f"{name}: {error}") from error
                print(f"PASS {pack}: {len(expected)} icons, all frames valid, all 11 sizes loaded by Windows")
            except Exception as error:
                raise ValueError(f"{pack}: {error}") from error
    print(f"PASS total: {total_frames} embedded frames; {total_native} native Windows loads")


if __name__ == "__main__":
    main()
