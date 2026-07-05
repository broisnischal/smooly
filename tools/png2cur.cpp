// png2cur — convert a PNG into a Windows .cur (32bpp alpha) with a hotspot.
// Self-contained: only needs GDI+ (already a smooly link dependency). No Python.
//
//   png2cur <in.png> <out.cur> <hotspotX> <hotspotY> [size]
//
// hotspotX/Y are given in the OUTPUT pixel space (i.e. relative to `size`).
// If `size` is given the image is high-quality-resampled to size x size; else the
// PNG's own pixel dimensions are used. Part of the smooly cursor-theme pipeline —
// see tools/cursor-art/README.md and build-cursors.ps1.
//
// Build (w64devkit): g++ -O2 png2cur.cpp -o png2cur.exe -lgdiplus -lgdi32 -municode

#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace Gdiplus;

static int fail(const char* msg) { fprintf(stderr, "png2cur: %s\n", msg); return 1; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: png2cur <in.png> <out.cur> <hotspotX> <hotspotY> [size]\n");
        return 2;
    }
    const wchar_t* inPath  = argv[1];
    const wchar_t* outPath = argv[2];
    int hotX = _wtoi(argv[3]);
    int hotY = _wtoi(argv[4]);
    int size = (argc >= 6) ? _wtoi(argv[5]) : 0;   // 0 = keep source size

    ULONG_PTR tok; GdiplusStartupInput gsi;
    if (GdiplusStartup(&tok, &gsi, nullptr) != Ok) return fail("GDI+ init failed");

    int rc = 0;
    {
        Bitmap src(inPath, FALSE);
        if (src.GetLastStatus() != Ok) { GdiplusShutdown(tok); return fail("cannot load PNG"); }

        int W = size > 0 ? size : (int)src.GetWidth();
        int H = size > 0 ? size : (int)src.GetHeight();
        if (W <= 0 || H <= 0 || W > 256 || H > 256) { GdiplusShutdown(tok); return fail("bad size (1..256)"); }

        // Resample into a straight-alpha ARGB canvas at the requested size,
        // preserving aspect ratio (fit + centre) so non-square art isn't stretched.
        Bitmap canvas(W, H, PixelFormat32bppARGB);
        {
            double sw = (double)src.GetWidth(), sh = (double)src.GetHeight();
            double scale = (W / sw < H / sh) ? W / sw : H / sh;
            int dw = (int)(sw * scale + 0.5), dh = (int)(sh * scale + 0.5);
            int dx = (W - dw) / 2, dy = (H - dh) / 2;
            Graphics g(&canvas);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.SetSmoothingMode(SmoothingModeHighQuality);
            g.Clear(Color(0, 0, 0, 0));
            g.DrawImage(&src, Rect(dx, dy, dw, dh), 0, 0, (INT)sw, (INT)sh, UnitPixel);
        }

        // Pull BGRA pixels (GDI+ 32bppARGB is B,G,R,A little-endian in memory).
        BitmapData bd;
        Rect r(0, 0, W, H);
        if (canvas.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) {
            GdiplusShutdown(tok); return fail("LockBits failed");
        }
        std::vector<uint8_t> bgra((size_t)W * H * 4);
        for (int y = 0; y < H; y++)
            memcpy(&bgra[(size_t)y * W * 4], (uint8_t*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)W * 4);
        canvas.UnlockBits(&bd);

        // ---- assemble the .cur (single 32bpp image, empty AND mask) ----
        const int maskRow = ((W + 31) / 32) * 4;          // 1bpp, 32-bit aligned
        const int maskSz  = maskRow * H;
        const int xorSz   = W * H * 4;
        const uint32_t dibSz = 40 + (uint32_t)xorSz + (uint32_t)maskSz;

        std::vector<uint8_t> out;
        auto w16 = [&](uint16_t v){ out.push_back(v & 0xFF); out.push_back(v >> 8); };
        auto w32 = [&](uint32_t v){ for (int i=0;i<4;i++) out.push_back((v>>(8*i)) & 0xFF); };

        // ICONDIR
        w16(0); w16(2); w16(1);                            // reserved, type=2 (cursor), count=1
        // ICONDIRENTRY
        out.push_back(W < 256 ? (uint8_t)W : 0);
        out.push_back(H < 256 ? (uint8_t)H : 0);
        out.push_back(0);                                  // color count
        out.push_back(0);                                  // reserved
        w16((uint16_t)hotX); w16((uint16_t)hotY);          // hotspot (cursor)
        w32(dibSz);                                        // bytes in resource
        w32(6 + 16);                                       // offset to image

        // BITMAPINFOHEADER (biHeight doubled = XOR + AND)
        w32(40); w32((uint32_t)W); w32((uint32_t)(H * 2));
        w16(1); w16(32); w32(0);                           // planes, bpp, BI_RGB
        w32((uint32_t)(xorSz + maskSz)); w32(0); w32(0); w32(0); w32(0);

        // XOR bitmap: bottom-up BGRA
        for (int y = H - 1; y >= 0; y--)
            out.insert(out.end(), &bgra[(size_t)y * W * 4], &bgra[(size_t)y * W * 4] + (size_t)W * 4);
        // AND mask: bottom-up, all zero (alpha in the XOR does the masking)
        out.insert(out.end(), (size_t)maskSz, 0);

        HANDLE hf = CreateFileW(outPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) { GdiplusShutdown(tok); return fail("cannot open output"); }
        DWORD wrote = 0;
        WriteFile(hf, out.data(), (DWORD)out.size(), &wrote, nullptr);
        CloseHandle(hf);
        if (wrote != out.size()) rc = fail("short write");
    }

    GdiplusShutdown(tok);
    return rc;
}
