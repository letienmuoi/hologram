/* hologram – animated GIF desktop pet (C / Win32 / MinGW)
 * --------------------------------------------------------
 * Build: MSYS2 MinGW-w64, CMake
 *   mkdir build && cd build
 *   cmake -G "MinGW Makefiles" ..
 *   cmake --build .
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <time.h>

/* ============================================================
 * Resource ID (must match resource.rc)
 * ============================================================ */
#define IDR_GIF_DATA 101

/* ============================================================
 * Constants
 * ============================================================ */
#define WINDOW_SIZE       120
#define TIMER_JIGGLE      1
#define TIMER_ANIMATION   2
#define IDM_START_WINDOWS 1001
#define IDM_EXIT          1002
#define BLACK_THRESHOLD   30

static const wchar_t *CLASS_NAME     = L"HologramCClass";
static const wchar_t *REG_KEY_PATH   = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t *REG_VALUE_NAME = L"hologram-c";
static const char    *LOCATION_FILE  = "hologram_c_location.txt";

/* ============================================================
 * Minimal GIF decoder (LZW)
 * ============================================================ */

#define GIF_MAX_FRAMES 512

typedef struct {
    int x, y, w, h;
    int delay_ms;           /* delay in milliseconds */
    unsigned char disposal; /* disposal method */
    unsigned char *rgba;    /* w*h*4 RGBA pixels (frame sub-image) */
} GifFrame;

typedef struct {
    int width, height;
    int frame_count;
    GifFrame frames[GIF_MAX_FRAMES];
} GifImage;

/* LZW decode state */
typedef struct {
    const unsigned char *data;
    int len;
    int pos;
    int bit_pos;
} BitReader;

static int bit_read(BitReader *br, int bits) {
    int val = 0;
    for (int i = 0; i < bits; i++) {
        if (br->pos >= br->len) return -1;
        val |= ((br->data[br->pos] >> br->bit_pos) & 1) << i;
        br->bit_pos++;
        if (br->bit_pos >= 8) {
            br->bit_pos = 0;
            br->pos++;
        }
    }
    return val;
}

/* Collect sub-blocks into a contiguous buffer */
static unsigned char *gif_collect_sub_blocks(const unsigned char *data, int data_len, int *pos, int *out_len) {
    int cap = 4096;
    int len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    while (*pos < data_len) {
        int block_size = data[(*pos)++];
        if (block_size == 0) break;
        if (*pos + block_size > data_len) break;
        if (len + block_size > cap) {
            cap = (len + block_size) * 2;
            buf = (unsigned char *)realloc(buf, cap);
        }
        memcpy(buf + len, data + *pos, block_size);
        len += block_size;
        *pos += block_size;
    }
    *out_len = len;
    return buf;
}

/* LZW decode */
static int gif_lzw_decode(const unsigned char *compressed, int comp_len,
                          int min_code_size, unsigned char **out, int *out_len) {
    #define LZW_MAX_CODES 4096
    int clear_code = 1 << min_code_size;
    int eoi_code = clear_code + 1;
    int code_size = min_code_size + 1;
    int next_code = eoi_code + 1;

    /* Table: each entry is a prefix code + suffix byte */
    static int prefix[LZW_MAX_CODES];
    static unsigned char suffix[LZW_MAX_CODES];
    static int lengths[LZW_MAX_CODES];

    for (int i = 0; i < clear_code; i++) {
        prefix[i] = -1;
        suffix[i] = (unsigned char)i;
        lengths[i] = 1;
    }

    int cap = 65536;
    int pos = 0;
    unsigned char *output = (unsigned char *)malloc(cap);

    BitReader br = { compressed, comp_len, 0, 0 };

    /* Stack for outputting codes */
    static unsigned char stack[LZW_MAX_CODES];

    int old_code = -1;
    while (1) {
        int code = bit_read(&br, code_size);
        if (code < 0 || code == eoi_code) break;

        if (code == clear_code) {
            code_size = min_code_size + 1;
            next_code = eoi_code + 1;
            old_code = -1;
            continue;
        }

        int emit_code;
        unsigned char first_char;

        if (code < next_code) {
            emit_code = code;
        } else if (code == next_code && old_code >= 0) {
            /* Special case: code not yet in table */
            int c = old_code;
            while (prefix[c] >= 0) c = prefix[c];
            first_char = suffix[c];
            /* Build: old_code + first_char */
            emit_code = code; /* will be added below */
            /* Pre-add to table */
            if (next_code < LZW_MAX_CODES) {
                prefix[next_code] = old_code;
                suffix[next_code] = first_char;
                lengths[next_code] = (old_code >= 0 ? lengths[old_code] : 0) + 1;
                next_code++;
                if (next_code >= (1 << code_size) && code_size < 12)
                    code_size++;
            }
            /* Now emit old_code + first_char */
            /* Unroll old_code string */
            int slen = lengths[old_code];
            if (pos + slen + 1 > cap) {
                while (pos + slen + 1 > cap) cap *= 2;
                output = (unsigned char *)realloc(output, cap);
            }
            int c2 = old_code;
            for (int i = slen - 1; i >= 0; i--) {
                stack[i] = suffix[c2];
                c2 = prefix[c2];
            }
            memcpy(output + pos, stack, slen);
            pos += slen;
            output[pos++] = first_char;
            old_code = code;
            continue;
        } else {
            break; /* error */
        }

        /* Emit the string for emit_code */
        int slen = lengths[emit_code];
        if (pos + slen > cap) {
            while (pos + slen > cap) cap *= 2;
            output = (unsigned char *)realloc(output, cap);
        }
        {
            int c = emit_code;
            for (int i = slen - 1; i >= 0; i--) {
                stack[i] = suffix[c];
                c = prefix[c];
            }
        }
        memcpy(output + pos, stack, slen);
        pos += slen;

        /* Add to table: old_code + first char of current code */
        if (old_code >= 0 && next_code < LZW_MAX_CODES) {
            int c = emit_code;
            while (prefix[c] >= 0) c = prefix[c];
            prefix[next_code] = old_code;
            suffix[next_code] = suffix[c];
            lengths[next_code] = lengths[old_code] + 1;
            next_code++;
            if (next_code >= (1 << code_size) && code_size < 12)
                code_size++;
        }
        old_code = emit_code;
    }

    *out = output;
    *out_len = pos;
    return 0;
    #undef LZW_MAX_CODES
}

static GifImage *gif_decode(const unsigned char *data, int data_len) {
    if (data_len < 13) return NULL;
    if (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)
        return NULL;

    GifImage *gif = (GifImage *)calloc(1, sizeof(GifImage));
    gif->width = data[6] | (data[7] << 8);
    gif->height = data[8] | (data[9] << 8);

    int packed = data[10];
    int has_gct = (packed >> 7) & 1;
    int gct_size = has_gct ? (1 << ((packed & 7) + 1)) : 0;

    unsigned char gct[256][3];
    int pos = 13;
    if (has_gct) {
        for (int i = 0; i < gct_size && pos + 2 < data_len; i++) {
            gct[i][0] = data[pos++];
            gct[i][1] = data[pos++];
            gct[i][2] = data[pos++];
        }
    }

    /* GCE state */
    int gce_delay = 100;
    int gce_disposal = 0;
    int gce_transparent = -1;

    while (pos < data_len) {
        unsigned char block = data[pos++];

        if (block == 0x3B) break; /* trailer */

        if (block == 0x21) {
            /* Extension */
            if (pos >= data_len) break;
            unsigned char ext_label = data[pos++];
            if (ext_label == 0xF9 && pos + 4 < data_len) {
                /* Graphic Control Extension */
                /* int block_size = data[pos]; */
                pos++;
                int gce_packed = data[pos++];
                gce_disposal = (gce_packed >> 2) & 7;
                int has_transparent = gce_packed & 1;
                gce_delay = (data[pos] | (data[pos + 1] << 8)) * 10;
                if (gce_delay <= 0) gce_delay = 100;
                pos += 2;
                gce_transparent = has_transparent ? data[pos] : -1;
                pos++;
                if (pos < data_len && data[pos] == 0) pos++; /* block terminator */
            } else {
                /* Skip other extensions */
                while (pos < data_len) {
                    int sz = data[pos++];
                    if (sz == 0) break;
                    pos += sz;
                }
            }
            continue;
        }

        if (block == 0x2C) {
            /* Image descriptor */
            if (pos + 8 >= data_len) break;
            int ix = data[pos] | (data[pos + 1] << 8); pos += 2;
            int iy = data[pos] | (data[pos + 1] << 8); pos += 2;
            int iw = data[pos] | (data[pos + 1] << 8); pos += 2;
            int ih = data[pos] | (data[pos + 1] << 8); pos += 2;
            int img_packed = data[pos++];
            int has_lct = (img_packed >> 7) & 1;
            int interlaced = (img_packed >> 6) & 1;
            int lct_size = has_lct ? (1 << ((img_packed & 7) + 1)) : 0;

            unsigned char lct[256][3];
            unsigned char (*palette)[3] = has_lct ? lct : gct;
            int palette_size = has_lct ? lct_size : gct_size;

            if (has_lct) {
                for (int i = 0; i < lct_size && pos + 2 < data_len; i++) {
                    lct[i][0] = data[pos++];
                    lct[i][1] = data[pos++];
                    lct[i][2] = data[pos++];
                }
            }

            if (pos >= data_len) break;
            int min_code_size = data[pos++];

            int comp_len;
            unsigned char *compressed = gif_collect_sub_blocks(data, data_len, &pos, &comp_len);

            unsigned char *indices = NULL;
            int indices_len = 0;
            gif_lzw_decode(compressed, comp_len, min_code_size, &indices, &indices_len);
            free(compressed);

            if (gif->frame_count < GIF_MAX_FRAMES) {
                GifFrame *f = &gif->frames[gif->frame_count];
                f->x = ix;
                f->y = iy;
                f->w = iw;
                f->h = ih;
                f->delay_ms = gce_delay;
                f->disposal = (unsigned char)gce_disposal;
                f->rgba = (unsigned char *)calloc(iw * ih * 4, 1);

                int idx = 0;
                if (interlaced) {
                    /* Interlace passes: 0,8 / 4,8 / 2,4 / 1,2 */
                    static const int pass_start[] = {0, 4, 2, 1};
                    static const int pass_step[]  = {8, 8, 4, 2};
                    for (int pass = 0; pass < 4; pass++) {
                        for (int row = pass_start[pass]; row < ih; row += pass_step[pass]) {
                            for (int col = 0; col < iw; col++) {
                                if (idx < indices_len) {
                                    int ci = indices[idx++];
                                    int off = (row * iw + col) * 4;
                                    if (ci == gce_transparent) {
                                        f->rgba[off + 3] = 0;
                                    } else if (ci < palette_size) {
                                        f->rgba[off + 0] = palette[ci][0];
                                        f->rgba[off + 1] = palette[ci][1];
                                        f->rgba[off + 2] = palette[ci][2];
                                        f->rgba[off + 3] = 255;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    for (int p = 0; p < iw * ih && idx < indices_len; p++, idx++) {
                        int ci = indices[idx];
                        int off = p * 4;
                        if (ci == gce_transparent) {
                            f->rgba[off + 3] = 0;
                        } else if (ci < palette_size) {
                            f->rgba[off + 0] = palette[ci][0];
                            f->rgba[off + 1] = palette[ci][1];
                            f->rgba[off + 2] = palette[ci][2];
                            f->rgba[off + 3] = 255;
                        }
                    }
                }

                gif->frame_count++;
            }
            free(indices);

            /* Reset GCE for next frame */
            gce_delay = 100;
            gce_disposal = 0;
            gce_transparent = -1;
            continue;
        }

        /* Unknown block, try to skip */
        if (block == 0x00) continue;
    }

    return gif;
}

static void gif_free(GifImage *gif) {
    if (!gif) return;
    for (int i = 0; i < gif->frame_count; i++)
        free(gif->frames[i].rgba);
    free(gif);
}

/* ============================================================
 * Global state
 * ============================================================ */
static HWND     g_hwnd = NULL;
static HBITMAP  g_hBitmap = NULL;
static BOOL     g_startWithWindows = FALSE;
static int      g_windowSize = WINDOW_SIZE;

/* Animation */
static HBITMAP *g_frameBitmaps = NULL;
static int     *g_frameDelays  = NULL;
static int      g_frameCount   = 0;
static int      g_currentFrame = 0;

/* ============================================================
 * RGBA image buffer helpers
 * ============================================================ */
typedef struct {
    int w, h;
    unsigned char *pixels; /* RGBA, row-major */
} RGBAImage;

static RGBAImage *rgba_new(int w, int h) {
    RGBAImage *img = (RGBAImage *)malloc(sizeof(RGBAImage));
    img->w = w;
    img->h = h;
    img->pixels = (unsigned char *)calloc(w * h * 4, 1);
    return img;
}

static void rgba_free(RGBAImage *img) {
    if (img) { free(img->pixels); free(img); }
}

static RGBAImage *rgba_clone(const RGBAImage *src) {
    RGBAImage *dst = rgba_new(src->w, src->h);
    memcpy(dst->pixels, src->pixels, src->w * src->h * 4);
    return dst;
}

/* Composite frame onto canvas (draw over) */
static void rgba_draw_over(RGBAImage *canvas, const unsigned char *frame_rgba,
                           int fx, int fy, int fw, int fh) {
    for (int y = 0; y < fh; y++) {
        int cy = fy + y;
        if (cy < 0 || cy >= canvas->h) continue;
        for (int x = 0; x < fw; x++) {
            int cx = fx + x;
            if (cx < 0 || cx >= canvas->w) continue;
            int si = (y * fw + x) * 4;
            int di = (cy * canvas->w + cx) * 4;
            unsigned char sa = frame_rgba[si + 3];
            if (sa == 255) {
                canvas->pixels[di + 0] = frame_rgba[si + 0];
                canvas->pixels[di + 1] = frame_rgba[si + 1];
                canvas->pixels[di + 2] = frame_rgba[si + 2];
                canvas->pixels[di + 3] = 255;
            } else if (sa > 0) {
                unsigned char da = canvas->pixels[di + 3];
                int outA = sa + da * (255 - sa) / 255;
                if (outA > 0) {
                    canvas->pixels[di + 0] = (unsigned char)((frame_rgba[si + 0] * sa + canvas->pixels[di + 0] * da * (255 - sa) / 255) / outA);
                    canvas->pixels[di + 1] = (unsigned char)((frame_rgba[si + 1] * sa + canvas->pixels[di + 1] * da * (255 - sa) / 255) / outA);
                    canvas->pixels[di + 2] = (unsigned char)((frame_rgba[si + 2] * sa + canvas->pixels[di + 2] * da * (255 - sa) / 255) / outA);
                    canvas->pixels[di + 3] = (unsigned char)outA;
                }
            }
        }
    }
}

/* Clear a region to transparent */
static void rgba_clear_rect(RGBAImage *canvas, int rx, int ry, int rw, int rh) {
    for (int y = 0; y < rh; y++) {
        int cy = ry + y;
        if (cy < 0 || cy >= canvas->h) continue;
        memset(canvas->pixels + (cy * canvas->w + rx) * 4, 0, rw * 4);
    }
}

/* ============================================================
 * Make black/near-black pixels transparent (gradient alpha)
 * ============================================================ */
static void make_black_transparent(RGBAImage *img, int threshold) {
    double t = (double)threshold;
    int total = img->w * img->h * 4;
    for (int i = 0; i < total; i += 4) {
        unsigned char r = img->pixels[i + 0];
        unsigned char g = img->pixels[i + 1];
        unsigned char b = img->pixels[i + 2];
        unsigned char mx = r;
        if (g > mx) mx = g;
        if (b > mx) mx = b;
        if (mx <= threshold) {
            double alpha = (double)mx / t;
            img->pixels[i + 3] = (unsigned char)(alpha * img->pixels[i + 3] + 0.5);
            img->pixels[i + 0] = (unsigned char)(r * alpha + 0.5);
            img->pixels[i + 1] = (unsigned char)(g * alpha + 0.5);
            img->pixels[i + 2] = (unsigned char)(b * alpha + 0.5);
        }
    }
}

/* ============================================================
 * Area-average (box filter) downscale – anti-aliased
 * ============================================================ */
static unsigned char clamp_byte(double v) {
    v += 0.5;
    if (v > 255.0) return 255;
    if (v < 0.0) return 0;
    return (unsigned char)v;
}

static RGBAImage *scale_image(const RGBAImage *src, int dstW, int dstH) {
    RGBAImage *dst = rgba_new(dstW, dstH);
    int srcW = src->w, srcH = src->h;

    for (int y = 0; y < dstH; y++) {
        double srcY0 = (double)y * srcH / dstH;
        double srcY1 = (double)(y + 1) * srcH / dstH;
        int iy0 = (int)srcY0;
        int iy1 = (int)srcY1;
        if (iy1 >= srcH) iy1 = srcH - 1;

        for (int x = 0; x < dstW; x++) {
            double srcX0 = (double)x * srcW / dstW;
            double srcX1 = (double)(x + 1) * srcW / dstW;
            int ix0 = (int)srcX0;
            int ix1 = (int)srcX1;
            if (ix1 >= srcW) ix1 = srcW - 1;

            double rSum = 0, gSum = 0, bSum = 0, aSum = 0, wSum = 0;

            for (int sy = iy0; sy <= iy1; sy++) {
                double wy = 1.0;
                if (sy == iy0) wy = 1.0 - (srcY0 - sy);
                if (sy == iy1) { double end = srcY1 - sy; if (end < wy) wy = end; }

                for (int sx = ix0; sx <= ix1; sx++) {
                    double wx = 1.0;
                    if (sx == ix0) wx = 1.0 - (srcX0 - sx);
                    if (sx == ix1) { double end = srcX1 - sx; if (end < wx) wx = end; }

                    double w = wx * wy;
                    int off = (sy * srcW + sx) * 4;
                    rSum += src->pixels[off + 0] * w;
                    gSum += src->pixels[off + 1] * w;
                    bSum += src->pixels[off + 2] * w;
                    aSum += src->pixels[off + 3] * w;
                    wSum += w;
                }
            }

            if (wSum > 0) {
                int off = (y * dstW + x) * 4;
                dst->pixels[off + 0] = clamp_byte(rSum / wSum);
                dst->pixels[off + 1] = clamp_byte(gSum / wSum);
                dst->pixels[off + 2] = clamp_byte(bSum / wSum);
                dst->pixels[off + 3] = clamp_byte(aSum / wSum);
            }
        }
    }
    return dst;
}

/* ============================================================
 * Premultiply alpha
 * ============================================================ */
static void premultiply_alpha(RGBAImage *img) {
    int total = img->w * img->h * 4;
    for (int i = 0; i < total; i += 4) {
        unsigned int a = img->pixels[i + 3];
        img->pixels[i + 0] = (unsigned char)(img->pixels[i + 0] * a / 255);
        img->pixels[i + 1] = (unsigned char)(img->pixels[i + 1] * a / 255);
        img->pixels[i + 2] = (unsigned char)(img->pixels[i + 2] * a / 255);
    }
}

/* ============================================================
 * Create HBITMAP from RGBA
 * ============================================================ */
static HBITMAP create_hbitmap_from_rgba(const RGBAImage *img) {
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img->w;
    bmi.bmiHeader.biHeight = -img->h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm) return NULL;

    /* RGBA → BGRA */
    const unsigned char *src = img->pixels;
    unsigned char *dst = (unsigned char *)bits;
    int total = img->w * img->h * 4;
    for (int i = 0; i < total; i += 4) {
        dst[i + 0] = src[i + 2]; /* B */
        dst[i + 1] = src[i + 1]; /* G */
        dst[i + 2] = src[i + 0]; /* R */
        dst[i + 3] = src[i + 3]; /* A */
    }
    return hbm;
}

/* ============================================================
 * Load & process embedded GIF
 * ============================================================ */
static void load_image(HINSTANCE hInst) {
    HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(IDR_GIF_DATA), RT_RCDATA);
    if (!hRes) { MessageBoxW(NULL, L"FindResource failed", L"Error", MB_OK); ExitProcess(1); }
    HGLOBAL hGlobal = LoadResource(hInst, hRes);
    DWORD gifSize = SizeofResource(hInst, hRes);
    const unsigned char *gifBytes = (const unsigned char *)LockResource(hGlobal);

    GifImage *gif = gif_decode(gifBytes, (int)gifSize);
    if (!gif || gif->frame_count == 0) {
        MessageBoxW(NULL, L"GIF decode failed", L"Error", MB_OK);
        ExitProcess(1);
    }

    g_frameCount = gif->frame_count;
    g_frameBitmaps = (HBITMAP *)calloc(g_frameCount, sizeof(HBITMAP));
    g_frameDelays = (int *)calloc(g_frameCount, sizeof(int));

    RGBAImage *canvas = rgba_new(gif->width, gif->height);
    RGBAImage *backup = rgba_new(gif->width, gif->height);

    for (int i = 0; i < gif->frame_count; i++) {
        GifFrame *f = &gif->frames[i];

        /* Save canvas for DisposalPrevious */
        memcpy(backup->pixels, canvas->pixels, canvas->w * canvas->h * 4);

        /* Draw frame onto canvas */
        rgba_draw_over(canvas, f->rgba, f->x, f->y, f->w, f->h);

        /* Clone, make black transparent, scale, premultiply */
        RGBAImage *clean = rgba_clone(canvas);
        make_black_transparent(clean, BLACK_THRESHOLD);
        RGBAImage *scaled = scale_image(clean, g_windowSize, g_windowSize);
        premultiply_alpha(scaled);
        g_frameBitmaps[i] = create_hbitmap_from_rgba(scaled);
        g_frameDelays[i] = f->delay_ms > 0 ? f->delay_ms : 100;

        rgba_free(scaled);
        rgba_free(clean);

        /* Handle disposal */
        switch (f->disposal) {
        case 2: /* background */
            rgba_clear_rect(canvas, f->x, f->y, f->w, f->h);
            break;
        case 3: /* previous */
            memcpy(canvas->pixels, backup->pixels, canvas->w * canvas->h * 4);
            break;
        }
    }

    g_hBitmap = g_frameBitmaps[0];
    g_currentFrame = 0;

    rgba_free(canvas);
    rgba_free(backup);
    gif_free(gif);
}

/* ============================================================
 * Update layered window (per-pixel alpha)
 * ============================================================ */
static void update_window_bitmap(void) {
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, g_hBitmap);

    RECT r;
    GetWindowRect(g_hwnd, &r);

    POINT ptDst = { r.left, r.top };
    POINT ptSrc = { 0, 0 };
    SIZE sz = { g_windowSize, g_windowSize };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(g_hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
}

/* ============================================================
 * Animation
 * ============================================================ */
static void advance_frame(void) {
    g_currentFrame = (g_currentFrame + 1) % g_frameCount;
    g_hBitmap = g_frameBitmaps[g_currentFrame];
    KillTimer(g_hwnd, TIMER_ANIMATION);
    SetTimer(g_hwnd, TIMER_ANIMATION, g_frameDelays[g_currentFrame], NULL);
    update_window_bitmap();
    /* Re-assert topmost to prevent taskbar from covering the window */
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* ============================================================
 * Mouse jiggle
 * ============================================================ */
static void jiggle_mouse(void) {
    int n = (rand() % 4) + 2;
    mouse_event(MOUSEEVENTF_MOVE, n, n, 0, 0);
    Sleep(10);
    mouse_event(MOUSEEVENTF_MOVE, (DWORD)(-n), (DWORD)(-n), 0, 0);
}

/* ============================================================
 * Registry: Start with Windows
 * ============================================================ */
static BOOL check_start_with_windows(void) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;
    DWORD type, size = 0;
    LONG ret = RegQueryValueExW(hKey, REG_VALUE_NAME, NULL, &type, NULL, &size);
    RegCloseKey(hKey);
    return (ret == ERROR_SUCCESS);
}

static void toggle_start_with_windows(void) {
    g_startWithWindows = !g_startWithWindows;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (g_startWithWindows) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        RegSetValueExW(hKey, REG_VALUE_NAME, 0, REG_SZ,
                       (const BYTE *)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, REG_VALUE_NAME);
    }
    RegCloseKey(hKey);
}

/* ============================================================
 * Context menu
 * ============================================================ */
static void show_context_menu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    UINT flags = MF_STRING;
    if (g_startWithWindows) flags |= MF_CHECKED;
    AppendMenuW(hMenu, flags, IDM_START_WINDOWS, L"Start with Windows");
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                             pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_START_WINDOWS: toggle_start_with_windows(); break;
    case IDM_EXIT:          DestroyWindow(hwnd); break;
    }
}

/* ============================================================
 * Position save / load
 * ============================================================ */
static void get_last_location(int *outX, int *outY) {
    *outX = 1800; *outY = 900;
    char path[MAX_PATH];
    char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) return;
    snprintf(path, MAX_PATH, "%s\\%s", tmp, LOCATION_FILE);

    FILE *f = fopen(path, "r");
    if (!f) return;
    int x, y;
    if (fscanf(f, "%d\n%d", &x, &y) == 2) {
        *outX = x; *outY = y;
    }
    fclose(f);
}

static void save_last_position(void) {
    RECT r;
    GetWindowRect(g_hwnd, &r);
    char path[MAX_PATH];
    char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) return;
    snprintf(path, MAX_PATH, "%s\\%s", tmp, LOCATION_FILE);

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%ld\n%ld", r.left, r.top);
    fclose(f);
}

/* ============================================================
 * Window procedure
 * ============================================================ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        ReleaseCapture();
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;

    case WM_RBUTTONDOWN:
        show_context_menu(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_JIGGLE)    jiggle_mouse();
        if (wParam == TIMER_ANIMATION) advance_frame();
        return 0;

    case WM_DESTROY:
        save_last_position();
        for (int i = 0; i < g_frameCount; i++) {
            if (g_frameBitmaps[i]) DeleteObject(g_frameBitmaps[i]);
        }
        free(g_frameBitmaps);
        free(g_frameDelays);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ============================================================
 * WinMain
 * ============================================================ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    srand((unsigned)time(NULL));

    load_image(hInstance);

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    int posX, posY;
    get_last_location(&posX, &posY);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME, L"Hologram",
        WS_POPUP | WS_VISIBLE,
        posX, posY, g_windowSize, g_windowSize,
        NULL, NULL, hInstance, NULL);
    g_hwnd = hwnd;

    update_window_bitmap();
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    /* Mouse jiggle: every 2 minutes */
    SetTimer(hwnd, TIMER_JIGGLE, 120000, NULL);

    /* Animation timer */
    if (g_frameCount > 1)
        SetTimer(hwnd, TIMER_ANIMATION, g_frameDelays[0], NULL);

    g_startWithWindows = check_start_with_windows();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
