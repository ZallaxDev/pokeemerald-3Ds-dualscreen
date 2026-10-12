/*
 * The 2D battle's info boxes at the top screen's resolution (3ds_battle_hud.h).
 *
 * Each box battle_interface.c makes is one slot here, by battler: what the
 * box shows - name and gender, level, HP numbers, the HP and EXP bars, the
 * status pill or the caught ball, the Safari Zone's balls - is told as the
 * game draws it into the box's sprites, and the slot's picture is redrawn
 * from it: the port's own art (romfs:/battle/hud.bin, gen_battle_hud.py), the
 * game's font at 1:1, and the game's own small labels ("HP", "EXP", the
 * status pills' letters, localized in the Spanish game) traced smooth at the
 * screen's 1.4x. A palette fade of the box's sprites is measured here and
 * applied by the compositor as a tint, the picture left as it is.
 *
 * Positions are those of the GBA's box, in its sprite's pixels, times
 * CTR_BATTLE_ZOOM, so the picture covers what the sprites did.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_interface.h"
#include "battle_anim.h"
#include "battle_gfx_sfx_util.h"
#include "fonts.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "battle_message.h"
#include "string_util.h"
#include "strings.h"
#include "constants/characters.h"

#include "3ds_battle_hud.h"
#include "3ds_log.h"
#include "3ds_video.h"
#include "../compat/port_platform.h"

#define ART_MAGIC "EM3DHUD1"
enum
{
    ART_FRAME = 0,           /* + kind */
    ART_HP_TRACK = 8,
    ART_HP_FILL = 9,         /* + colour */
    ART_EXP_FILL = 12,
    ART_PILL = 16,           /* + status - 1 */
    ART_BALL = 21,
    ART_BOX_CAP = 24,
    ART_BOX_MIDDLE = 25,
    ART_COUNT = 32,
};

typedef struct
{
    int x, y, w, h;
    const uint32_t *pixels;  /* RGBA8 0xRRGGBBAA, straight alpha */
} Art;

/* A traced label: alpha at the screen's resolution, from (x, y) of its origin;
 * its ink's box [x0, x1) x [y0, y1) in its own pixels. */
typedef struct
{
    int x, y, w, h;
    int x0, y0, x1, y1;
    uint8_t alpha[48 * 20];
    bool made;
} Label;

typedef struct
{
    uint8_t kind, sprite, battler;
    uint8_t name[POKEMON_NAME_LENGTH + 1];
    uint8_t gender;          /* 0 male, 1 female, 2 none */
    uint8_t level;
    int16_t hp, maxHp;
    uint8_t hpPixels, hpColour, expPixels;
    uint8_t status;
    bool ball;
    uint8_t safariBalls;
    bool dirty;
    uint32_t signature;
} Slot;

/* The layout of each kind, in the box sprite's GBA pixels. */
typedef struct
{
    uint8_t nameX, levelRight;
    uint8_t barX, barY;
    uint8_t statusX, statusY;
    bool exp, hpText, safari, numbers;
} Layout;

static const Layout sLayouts[] = {
    [CTR_HUD_SINGLE_OPPONENT] = {8, 87, 24, 17, 8, 16, false, false, false, false},
    [CTR_HUD_SINGLE_PLAYER] = {16, 95, 32, 17, 16, 24, true, true, false, false},
    [CTR_HUD_DOUBLE_OPPONENT] = {8, 87, 24, 17, 8, 16, false, false, false, false},
    [CTR_HUD_DOUBLE_PLAYER] = {16, 95, 32, 17, 16, 16, false, false, false, true},
    [CTR_HUD_SAFARI] = {16, 0, 0, 0, 0, 0, false, false, true, false},
};

#define ZOOM 1.4f
#define TEXT_FG 0x424242FFu
#define TEXT_SHADOW 0xDED6B5FFu
#define TEXT_MALE 0x42CEFFFFu
#define TEXT_FEMALE 0xFF9C94FFu

static Art sArt[ART_COUNT];
static uint8_t *sArtData;
static bool sArtTried, sArtReady;
static Slot sSlots[CTR_HUD_BOXES];
static CtrHudBox sBoxes[CTR_HUD_BOXES];
static uint32_t sPixels[CTR_HUD_BOXES][CTR_HUD_W * CTR_HUD_H];
static Label sHpLabel, sExpLabel, sPills[5];

/* ------------------------------------------------------------------------ */
/* The art                                                                  */
/* ------------------------------------------------------------------------ */

static void LoadArt(void)
{
    FILE *f;
    long size;

    if (sArtTried)
        return;
    sArtTried = true;
    f = fopen("romfs:/battle/hud.bin", "rb");
    if (!f)
    {
        CtrLog_Write(CTR_LOG_ERROR, "battle hud: battle/hud.bin missing, info boxes stay as the GBA's");
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    sArtData = malloc((size_t)size);
    if (!sArtData || fread(sArtData, 1, (size_t)size, f) != (size_t)size || size < 10
        || memcmp(sArtData, ART_MAGIC, 8))
    {
        fclose(f);
        free(sArtData);
        sArtData = NULL;
        CtrLog_Write(CTR_LOG_ERROR, "battle hud: battle/hud.bin unreadable");
        return;
    }
    fclose(f);
    {
        const uint8_t *p = sArtData + 10, *end = sArtData + size;
        unsigned count = sArtData[8] | (sArtData[9] << 8);

        for (unsigned i = 0; i < count && p + 10 <= end; ++i)
        {
            unsigned id = p[0];
            int x = (int16_t)(p[2] | (p[3] << 8)), y = (int16_t)(p[4] | (p[5] << 8));
            int w = p[6] | (p[7] << 8), h = p[8] | (p[9] << 8);

            p += 10;
            if (p + (size_t)w * h * 4 > end)
                break;
            if (id < ART_COUNT && !sArt[id].pixels)
            {
                /* Bytes R, G, B, A: one aligned word each, 0xRRGGBBAA. */
                uint32_t *words = malloc((size_t)w * h * 4);

                if (!words)
                    break;
                for (int k = 0; k < w * h; ++k)
                {
                    const uint8_t *c = p + k * 4;
                    words[k] = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) | ((uint32_t)c[2] << 8) | c[3];
                }
                sArt[id] = (Art){x, y, w, h, words};
            }
            p += (size_t)w * h * 4;
        }
    }
    free(sArtData);
    sArtData = NULL;
    sArtReady = sArt[ART_FRAME + CTR_HUD_SINGLE_OPPONENT].pixels && sArt[ART_FRAME + CTR_HUD_SINGLE_PLAYER].pixels
             && sArt[ART_HP_TRACK].pixels && sArt[ART_BOX_CAP].pixels && sArt[ART_BOX_MIDDLE].pixels;
    if (!sArtReady)
        CtrLog_Write(CTR_LOG_ERROR, "battle hud: battle/hud.bin incomplete");
}

static void MakeLabels(const uint16_t *boxTileVram);

bool CtrBattleHud_Ready(void)
{
    LoadArt();
    return sArtReady;
}

/*
 * The art read and the labels traced ahead of the first box's picture, as
 * the first battle makes its boxes (CtrBattleHud_BoxCreated), under the
 * battle's opening. Not at start-up: its 200 KB taken there left the intro
 * with 3D 0.3 ms dearer on the console (runs I-L). The EXP label, from the
 * player's box in VRAM, is traced at its first picture, small.
 */
void CtrBattleHud_Prepare(void)
{
    if (CtrBattleHud_Ready())
        MakeLabels(NULL);
}

const uint32_t *CtrBattleHud_BoxArt(bool middle, int *x, int *y, int *w, int *h)
{
    const Art *art;

    if (!CtrBattleHud_Ready())
        return NULL;
    art = &sArt[middle ? ART_BOX_MIDDLE : ART_BOX_CAP];
    *x = art->x;
    *y = art->y;
    *w = art->w;
    *h = art->h;
    return art->pixels;
}

/* ------------------------------------------------------------------------ */
/* Drawing into a picture                                                   */
/* ------------------------------------------------------------------------ */

static inline uint32_t Over(uint32_t dst, uint32_t src, unsigned coverage)
{
    unsigned sa = ((src & 0xFF) * coverage + 127) / 255, da, oa;

    if (sa == 0)
        return dst;
    da = dst & 0xFF;
    /* What the general case below gives, without its divisions (the ARM11
     * has none in hardware): over nothing, the source at its coverage; an
     * opaque source, the source. */
    if (da == 0)
        return (src & ~0xFFu) | sa;
    if (sa == 255)
        return src | 0xFF;
    oa = sa + da * (255 - sa) / 255;
    if (oa == 0)
        return 0;
    {
        unsigned out = oa;

        for (int shift = 24; shift >= 8; shift -= 8)
        {
            unsigned s = (src >> shift) & 0xFF, d = (dst >> shift) & 0xFF;
            unsigned c = (s * sa + d * da * (255 - sa) / 255) / oa;

            out |= (c > 255 ? 255u : c) << shift;
        }
        return out;
    }
}

/* Part [sx0, sx1) of a piece of art at (x, y) of the picture. */
static void BlitPart(uint32_t *pic, const Art *art, int x, int y, int sx0, int sx1)
{
    if (!art->pixels)
        return;
    for (int r = 0; r < art->h; ++r)
    {
        int py = y + r;

        if (py < 0 || py >= CTR_HUD_H)
            continue;
        for (int c = sx0; c < sx1 && c < art->w; ++c)
        {
            int px = x + c - sx0;

            if (c < 0 || px < 0 || px >= CTR_HUD_W)
                continue;
            pic[py * CTR_HUD_W + px] = Over(pic[py * CTR_HUD_W + px], art->pixels[r * art->w + c], 255);
        }
    }
}

static void Blit(uint32_t *pic, const Art *art, int x, int y)
{
    BlitPart(pic, art, x + art->x, y + art->y, 0, art->w);
}

/*
 * A bar's fill `width` pixels long: its art is the full fill, a capsule;
 * the left end and the right end are kept, the middle shortened.
 */
static void BlitFill(uint32_t *pic, const Art *art, int x, int y, int width)
{
    int cap = art->h / 2, left, right;

    if (!art->pixels || width <= 0)
        return;
    if (width > art->w)
        width = art->w;
    left = width < 2 * cap ? width / 2 : width - cap;
    right = width - left;
    BlitPart(pic, art, x + art->x, y + art->y, 0, left);
    BlitPart(pic, art, x + art->x + left, y + art->y, art->w - right, art->w);
}

/* A traced label, coloured by a gradient from top to bottom. */
static void BlitLabel(uint32_t *pic, const Label *label, int x, int y, uint32_t top, uint32_t bottom)
{
    if (!label->made)
        return;
    for (int r = 0; r < label->h; ++r)
    {
        float t = label->h > 1 ? (float)r / (label->h - 1) : 0;
        uint32_t colour = 0xFF;

        for (int shift = 24; shift >= 8; shift -= 8)
        {
            float a = (top >> shift) & 0xFF, b = (bottom >> shift) & 0xFF;

            colour |= (uint32_t)(a + (b - a) * t + 0.5f) << shift;
        }
        for (int c = 0; c < label->w; ++c)
        {
            int px = x + label->x + c, py = y + label->y + r;
            unsigned a = label->alpha[r * label->w + c];

            if (a && px >= 0 && py >= 0 && px < CTR_HUD_W && py < CTR_HUD_H)
                pic[py * CTR_HUD_W + px] = Over(pic[py * CTR_HUD_W + px], colour, a);
        }
    }
}

/* A traced label with its ink centred on (cx, cy) of the picture. */
static void BlitLabelCentred(uint32_t *pic, const Label *label, float cx, float cy, uint32_t top, uint32_t bottom)
{
    if (!label->made)
        return;
    BlitLabel(pic, label, (int)lroundf(cx - (label->x0 + label->x1) / 2.0f) - label->x,
              (int)lroundf(cy - (label->y0 + label->y1) / 2.0f) - label->y, top, bottom);
}

/*
 * The game's small labels traced smooth: the bits of a GBA mask (w x h, at
 * most 24 x 8) drawn 7 to a GBA pixel, blurred, cut at half and filtered
 * down by 5 - the screen's 1.4 - with two screen pixels of margin.
 */
#define TRACE_K 7
#define TRACE_PAD 10
#define TRACE_RADIUS 6
static void Trace(Label *label, const uint8_t *bits, int w, int h)
{
    static float canvas[(24 * TRACE_K + 2 * TRACE_PAD) * (8 * TRACE_K + 2 * TRACE_PAD)];
    static float blurred[(24 * TRACE_K + 2 * TRACE_PAD) * (8 * TRACE_K + 2 * TRACE_PAD)];
    float kernel[2 * TRACE_RADIUS + 1], sum = 0;
    int cw = w * TRACE_K + 2 * TRACE_PAD, ch = h * TRACE_K + 2 * TRACE_PAD;

    label->made = false;
    if (w > 24 || h > 8)
        return;
    for (int i = -TRACE_RADIUS; i <= TRACE_RADIUS; ++i)
        sum += kernel[i + TRACE_RADIUS] = expf(-(float)(i * i) / (2 * 2.2f * 2.2f));
    for (int i = 0; i <= 2 * TRACE_RADIUS; ++i)
        kernel[i] /= sum;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
        {
            int gx = x - TRACE_PAD, gy = y - TRACE_PAD;

            canvas[y * cw + x] = gx >= 0 && gy >= 0 && gx < w * TRACE_K && gy < h * TRACE_K
                && bits[(gy / TRACE_K) * w + gx / TRACE_K] ? 1.0f : 0.0f;
        }
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
        {
            float v = 0;

            for (int i = -TRACE_RADIUS; i <= TRACE_RADIUS; ++i)
                if (x + i >= 0 && x + i < cw)
                    v += canvas[y * cw + x + i] * kernel[i + TRACE_RADIUS];
            blurred[y * cw + x] = v;
        }
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
        {
            float v = 0;

            for (int i = -TRACE_RADIUS; i <= TRACE_RADIUS; ++i)
                if (y + i >= 0 && y + i < ch)
                    v += blurred[(y + i) * cw + x] * kernel[i + TRACE_RADIUS];
            canvas[y * cw + x] = v >= 0.5f ? 1.0f : 0.0f;
        }
    label->w = cw / 5;
    label->h = ch / 5;
    label->x = -TRACE_PAD / 5;
    label->y = -TRACE_PAD / 5;
    for (int y = 0; y < label->h; ++y)
        for (int x = 0; x < label->w; ++x)
        {
            float v = 0;

            for (int j = 0; j < 5; ++j)
                for (int i = 0; i < 5; ++i)
                    v += canvas[(y * 5 + j) * cw + x * 5 + i];
            label->alpha[y * label->w + x] = (uint8_t)(v * 255 / 25 + 0.5f);
        }
    label->x0 = label->w;
    label->y0 = label->h;
    label->x1 = label->y1 = 0;
    for (int y = 0; y < label->h; ++y)
        for (int x = 0; x < label->w; ++x)
            if (label->alpha[y * label->w + x] >= 64)
            {
                if (x < label->x0) label->x0 = x;
                if (y < label->y0) label->y0 = y;
                if (x + 1 > label->x1) label->x1 = x + 1;
                if (y + 1 > label->y1) label->y1 = y + 1;
            }
    label->made = label->x1 > label->x0;
}

/* A pixel of 4bpp tile data (8x8, two pixels a byte, the left one low). */
static unsigned TilePixel(const uint8_t *tile, int x, int y)
{
    uint8_t b = tile[y * 4 + x / 2];

    return x & 1 ? b >> 4 : b & 15;
}

/* The game's labels, from its own graphics, once. */
static void MakeLabels(const uint16_t *boxTileVram)
{
    uint8_t bits[24 * 8];

    if (!sHpLabel.made)
    {
        /* "HP" (Spanish "PS"): the bar's first two tiles, its orange colours. */
        const uint8_t *h = CtrBattleHud_ElementGfx(1), *p = CtrBattleHud_ElementGfx(2);

        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
            {
                unsigned v = TilePixel(x < 8 ? h : p, x & 7, y);

                bits[y * 16 + x] = v >= 7 && v <= 9;
            }
        Trace(&sHpLabel, bits, 16, 8);
    }
    for (unsigned s = 0; s < 5; ++s)
    {
        /* A pill's letters: the light colour inside its first 20 columns. */
        static const uint8_t elements[5] = {21, 24, 27, 30, 33};   /* PSN PRZ SLP FRZ BRN, battler 0 */

        if (sPills[s].made)
            continue;
        memset(bits, 0, sizeof(bits));
        for (int y = 1; y < 7; ++y)
            for (int x = 1; x < 19; ++x)
                bits[y * 24 + x] = TilePixel(CtrBattleHud_ElementGfx(elements[s] + x / 8), x & 7, y) == 2;
        Trace(&sPills[s], bits, 24, 8);
    }
    if (!sExpLabel.made && boxTileVram)
    {
        /* "EXP": the player's box, row 4 of its tiles, columns 2 and 3,
         * lines 2-4 - the label's olive top and yellow letters. */
        memset(bits, 0, sizeof(bits));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const uint8_t *tile = (const uint8_t *)boxTileVram + (34 + x / 8) * 32;
                unsigned v = TilePixel(tile, x & 7, y + 2);

                bits[y * 16 + x] = (v == 4 || v == 9) && x >= 1 && x <= 12;
            }
        Trace(&sExpLabel, bits, 16, 3);
    }
}

/* ------------------------------------------------------------------------ */
/* Text: the game's normal font, 1:1                                        */
/* ------------------------------------------------------------------------ */

static const u16 *FontGlyphs(void)
{
    const void *resolved = Port_ResolveFontPointer(gFontNormalLatinGlyphs);

    if (resolved == (const void *)gFontNormalLatinGlyphs)
        resolved = Port_ResolveAssetPointer(gFontNormalLatinGlyphs);
    return resolved;
}

static int GlyphWidth(unsigned glyph)
{
    return gFontNormalLatinGlyphWidths[glyph];
}

/* A game string's next glyph, past its control codes; -1 at its end. */
static int NextGlyph(const uint8_t **str)
{
    for (;;)
    {
        uint8_t c = *(*str)++;

        switch (c)
        {
        case EOS:
            --*str;
            return -1;
        case EXT_CTRL_CODE_BEGIN:
            if (**str == EOS)
                return -1;
            *str += GetExtCtrlCodeLength(**str);
            break;
        case PLACEHOLDER_BEGIN:
        case CHAR_DYNAMIC:
        case CHAR_KEYPAD_ICON:
            if (**str != EOS)
                ++*str;
            break;
        case CHAR_EXTRA_SYMBOL:
            if (**str == EOS)
                return -1;
            ++*str;
            break;
        case CHAR_NEWLINE:
        case CHAR_PROMPT_SCROLL:
        case CHAR_PROMPT_CLEAR:
            break;
        default:
            return c;
        }
    }
}

static int TextWidth(const uint8_t *str)
{
    int width = 0;

    for (int g; (g = NextGlyph(&str)) >= 0;)
        width += GlyphWidth(g);
    return width;
}

static int DrawGlyph(uint32_t *pic, const u16 *glyphs, unsigned glyph, int x, int y, uint32_t fg, uint32_t shadow)
{
    const u16 *base = glyphs + glyph * 0x20;
    int width = GlyphWidth(glyph);

    for (int row = 0; row < 16; ++row)
        for (int half = 0; half < 2 && half * 8 < width; ++half)
        {
            u16 bits = base[(row >= 8 ? 0x10 : 0) + half * 8 + (row & 7)];

            if (!bits)
                continue;
            for (int k = 0; k < 8 && half * 8 + k < width; ++k)
            {
                uint8_t byte = k < 4 ? bits >> 8 : bits & 0xFF;
                unsigned v = (byte >> (6 - 2 * (k & 3))) & 3;
                int px = x + half * 8 + k, py = y + row;

                if ((v == 1 || v == 2) && px >= 0 && py >= 0 && px < CTR_HUD_W && py < CTR_HUD_H)
                    pic[py * CTR_HUD_W + px] = v == 1 ? fg : shadow;
            }
        }
    return width;
}

static int DrawText(uint32_t *pic, const uint8_t *str, int x, int y, uint32_t fg)
{
    const u16 *glyphs = FontGlyphs();

    if (!glyphs)
        return x;
    for (int g; (g = NextGlyph(&str)) >= 0;)
        x += DrawGlyph(pic, glyphs, g, x, y, fg, TEXT_SHADOW);
    return x;
}

static void DrawTextRight(uint32_t *pic, const uint8_t *str, int right, int y)
{
    DrawText(pic, str, right - TextWidth(str), y, TEXT_FG);
}

/* A number in the game's characters; returns the end of the string. */
static uint8_t *Digits(uint8_t *out, int value)
{
    char digits[8];
    int n = snprintf(digits, sizeof(digits), "%d", value < 0 ? 0 : value);

    for (int i = 0; i < n; ++i)
        *out++ = CHAR_0 + (digits[i] - '0');
    *out = EOS;
    return out;
}

/* ------------------------------------------------------------------------ */
/* A box's picture                                                          */
/* ------------------------------------------------------------------------ */

static int Px(float gba)
{
    return (int)lroundf(gba * ZOOM);
}

/* The fade the box's palette is under: strength 0..1 towards a colour. */
static uint32_t FadeOf(const Slot *slot, float *strength, uint32_t *colour)
{
    const struct Sprite *sprite = &gSprites[slot->sprite];
    unsigned base = 0x100 + sprite->oam.paletteNum * 16;
    /* The panel's cream and the outline's dark green, far apart in every channel. */
    u16 u2 = gPlttBufferUnfaded[base + 2], u7 = gPlttBufferUnfaded[base + 7];
    u16 f2 = gPlttBufferFaded[base + 2], f7 = gPlttBufferFaded[base + 7];
    float k = 0;
    int best = 0;

    *strength = 0;
    *colour = 0;
    for (int ch = 0; ch < 3; ++ch)
    {
        int a = (u2 >> (ch * 5)) & 31, b = (u7 >> (ch * 5)) & 31;
        int d = a > b ? a - b : b - a;

        if (d > best)
        {
            int fa = (f2 >> (ch * 5)) & 31, fb = (f7 >> (ch * 5)) & 31;

            best = d;
            k = 1.0f - (float)(fa - fb) / (float)(a - b);
        }
    }
    if (best == 0 || k < 0.02f)
        return (uint32_t)u2 << 16 | f2;
    if (k > 1)
        k = 1;
    *strength = k;
    {
        uint32_t rgb = 0;

        for (int ch = 0; ch < 3; ++ch)
        {
            float u = (u2 >> (ch * 5)) & 31, f = (f2 >> (ch * 5)) & 31;
            float t = (f - u * (1 - k)) / k;
            int v = (int)(t * 255 / 31 + 0.5f);

            v = v < 0 ? 0 : v > 255 ? 255 : v;
            rgb |= (uint32_t)v << (24 - ch * 8);
        }
        *colour = rgb;
    }
    return (uint32_t)u2 << 16 | f2;
}

static void Compose(int index)
{
    Slot *slot = &sSlots[index];
    const Layout *layout = &sLayouts[slot->kind];
    uint32_t *pic = sPixels[index];
    int barX = Px(layout->barX), barY = Px(layout->barY);
    bool numbers = layout->numbers && gBattleSpritesDataPtr
                && gBattleSpritesDataPtr->battlerData[slot->battler].hpNumbersNoBars;

    memset(pic, 0, sizeof(sPixels[0]));
    /* The EXP label is only on the player's singles box. */
    MakeLabels(slot->kind == CTR_HUD_SINGLE_PLAYER
               ? (const uint16_t *)(OBJ_VRAM0 + gSprites[slot->sprite].oam.tileNum * TILE_SIZE_4BPP) : NULL);
    Blit(pic, &sArt[ART_FRAME + slot->kind], 0, 0);

    if (!layout->safari)
    {
        if (numbers)
        {
            /* The doubles' HP numbers, in place of the bar (SwapHpBarsWithHpText). */
            uint8_t text[16], *p = Digits(text, slot->hp);

            *p++ = CHAR_SLASH;
            Digits(p, slot->maxHp);
            DrawTextRight(pic, text, barX + Px(64), barY - 5);
        }
        else
        {
            const Art *fill = &sArt[ART_HP_FILL + (slot->hpColour > 2 ? 0 : slot->hpColour)];

            Blit(pic, &sArt[ART_HP_TRACK], barX, barY);
            BlitFill(pic, fill, barX, barY, (fill->w * slot->hpPixels + 24) / 48);
            /* The "HP" label, but where a status pill takes its place. */
            if (slot->status == CTR_HUD_STATUS_NONE || slot->kind == CTR_HUD_SINGLE_PLAYER)
                /* Centred in the bar's dark left end: GBA x 0-15, y 0-6 of the bar. */
                BlitLabelCentred(pic, &sHpLabel, barX + 7.5f * ZOOM, barY + 3.0f * ZOOM, 0xFFC48CFF, 0xE26254FF);
        }
        if (slot->status != CTR_HUD_STATUS_NONE && slot->status <= CTR_HUD_STATUS_BRN)
        {
            int x = Px(layout->statusX), y = Px(layout->statusY);

            Blit(pic, &sArt[ART_PILL + slot->status - 1], x, y);
            /* The letters centred on the pill (GBA 20 x 8). */
            BlitLabelCentred(pic, &sPills[slot->status - 1], x + 10 * ZOOM, y + 4 * ZOOM, 0xFFFFF6FF, 0xF0ECD6FF);
        }
        else if (slot->ball)
            Blit(pic, &sArt[ART_BALL], Px(layout->statusX), Px(layout->statusY));
    }
    if (layout->exp)
    {
        const Art *fill = &sArt[ART_EXP_FILL];

        BlitLabel(pic, &sExpLabel, Px(16), Px(34), 0xFFF478FF, 0xE6B400FF);
        BlitFill(pic, fill, 0, 0, (fill->w * slot->expPixels + 32) / 64);
    }

    /* The texts: the GBA's glyph tops are on its line 7, the font's on its line 3. */
    {
        int y = Px(7) - 3;

        if (layout->safari)
        {
            uint8_t text[24], *p;

            DrawText(pic, gText_SafariBalls, Px(layout->nameX), y, TEXT_FG);
            p = (uint8_t *)StringCopy(text, gText_SafariBallLeft);
            Digits(p, slot->safariBalls);
            DrawTextRight(pic, text, Px(95.5f), Px(23) - 3);
        }
        else
        {
            uint8_t text[16];
            int x = DrawText(pic, slot->name, Px(layout->nameX), y, TEXT_FG);

            if (slot->gender == 0)
                DrawGlyph(pic, FontGlyphs(), CHAR_MALE, x, y, TEXT_MALE, TEXT_SHADOW);
            else if (slot->gender == 1)
                DrawGlyph(pic, FontGlyphs(), CHAR_FEMALE, x, y, TEXT_FEMALE, TEXT_SHADOW);
            text[0] = CHAR_LV;
            Digits(text + 1, slot->level);
            DrawTextRight(pic, text, Px(layout->levelRight + 0.5f), y);
            if (layout->hpText)
            {
                uint8_t *p = Digits(text, slot->hp);

                *p++ = CHAR_SLASH;
                Digits(p, slot->maxHp);
                DrawTextRight(pic, text, Px(96.5f), Px(24) - 3);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* What the game tells                                                      */
/* ------------------------------------------------------------------------ */

static Slot *SlotOf(unsigned char healthboxSpriteId)
{
    for (int i = 0; i < CTR_HUD_BOXES; ++i)
        if (sBoxes[i].used && sSlots[i].sprite == healthboxSpriteId)
            return &sSlots[i];
    return NULL;
}

void CtrBattleHud_BoxCreated(unsigned char healthboxSpriteId, unsigned char battler, unsigned char kind)
{
    const struct Sprite *left = &gSprites[healthboxSpriteId];
    Slot *slot;
    CtrHudBox *box;

    if (battler >= CTR_HUD_BOXES)
        return;
    /* The first box of a battle: the last battle's are gone. */
    if (battler == 0)
    {
        CtrBattleHud_Clear();
        CtrBattleHud_Prepare();
    }
    slot = &sSlots[battler];
    box = &sBoxes[battler];
    memset(slot, 0, sizeof(*slot));
    slot->kind = kind;
    slot->sprite = healthboxSpriteId;
    slot->battler = battler;
    slot->name[0] = EOS;
    slot->gender = 2;
    slot->dirty = true;
    box->used = true;
    box->boxTile = left->oam.tileNum;
    box->boxTiles = (kind == CTR_HUD_SINGLE_PLAYER || kind == CTR_HUD_SINGLE_OPPONENT || kind == CTR_HUD_SAFARI) ? 128 : 64;
    if (kind == CTR_HUD_SAFARI)
    {
        box->barTile = 0;
        box->barTiles = 0;
    }
    else
    {
        box->barTile = gSprites[left->data[5]].oam.tileNum;  /* hMain_HealthBarSpriteId */
        box->barTiles = (kind == CTR_HUD_SINGLE_OPPONENT || kind == CTR_HUD_DOUBLE_OPPONENT) ? 9 : 8;
    }
    box->pixels = sPixels[battler];
    box->version++;
}

void CtrBattleHud_SetName(unsigned char id, const unsigned char *name, unsigned char gender)
{
    Slot *slot = SlotOf(id);

    if (!slot)
        return;
    StringCopyN(slot->name, name, POKEMON_NAME_LENGTH);
    slot->name[POKEMON_NAME_LENGTH] = EOS;
    slot->gender = gender == MON_MALE ? 0 : gender == MON_FEMALE ? 1 : 2;
    slot->dirty = true;
}

void CtrBattleHud_SetLevel(unsigned char id, unsigned char level)
{
    Slot *slot = SlotOf(id);

    if (slot && slot->level != level)
    {
        slot->level = level;
        slot->dirty = true;
    }
}

void CtrBattleHud_SetHp(unsigned char id, short value, bool max)
{
    Slot *slot = SlotOf(id);

    if (!slot)
        return;
    if (max) slot->maxHp = value;
    else slot->hp = value;
    slot->dirty = true;
}

void CtrBattleHud_SetBar(unsigned char id, bool exp, unsigned char pixels, unsigned char colour)
{
    Slot *slot = SlotOf(id);

    if (!slot)
        return;
    if (exp)
    {
        if (slot->expPixels != pixels)
            slot->dirty = true;
        slot->expPixels = pixels;
    }
    else
    {
        if (slot->hpPixels != pixels || slot->hpColour != colour)
            slot->dirty = true;
        slot->hpPixels = pixels;
        slot->hpColour = colour;
    }
}

void CtrBattleHud_SetStatus(unsigned char id, unsigned char status)
{
    Slot *slot = SlotOf(id);

    if (slot)
    {
        slot->status = status;
        slot->dirty = true;
    }
}

void CtrBattleHud_SetBall(unsigned char id, bool shown)
{
    Slot *slot = SlotOf(id);

    if (slot)
    {
        slot->ball = shown;
        slot->dirty = true;
    }
}

void CtrBattleHud_SetSafariBalls(unsigned char id, unsigned char balls)
{
    Slot *slot = SlotOf(id);

    if (slot)
    {
        slot->safariBalls = balls;
        slot->dirty = true;
    }
}

/*
 * The player's trainer, while the text box slides in under it (a battle over
 * the voxel world, 3ds_video.c BoxFrame): moved down with the box, `lift` GBA
 * pixels, so the bottom edge of its picture - the box's to cover - never shows.
 * Its y2 only: the intro's slide across is the game's. Only the trainer's own
 * picture (the battler's sprite, still the trainer before anything is sent
 * out), and only while y2 is what this put there; 0 lets it go.
 *
 * The lift is always even. When the slide across ends while the trainer is
 * still lifted, the game takes y2 for the multi battle's vertical slide
 * (SpriteCB_TrainerSlideVertical: two pixels a frame until y2 is exactly 0)
 * and the picture is let go here; an odd y2 would step past 0 and go on up
 * for ever, and the intro with it (CompleteOnBattlerSpriteCallbackDummy).
 */
void CtrBattleHud_LiftPlayerTrainer(int lift)
{
    static u8 sOwned = 0xFF;
    static s16 sWritten;
    u8 id = gBattlerSpriteIds[GetBattlerAtPosition(B_POSITION_PLAYER_LEFT)];
    struct Sprite *sprite;

    lift = (lift + 1) & ~1;
    if (lift == 0 && sOwned == 0xFF)
        return;
    if (sOwned != 0xFF && sOwned != id)
    {
        if (gSprites[sOwned].inUse && gSprites[sOwned].y2 == sWritten)
            gSprites[sOwned].y2 = 0;
        sOwned = 0xFF;
    }
    if (id >= MAX_SPRITES)
        return;
    sprite = &gSprites[id];
    if (!sprite->inUse || sprite->template != &gMultiuseSpriteTemplate
        || (sprite->callback != SpriteCB_TrainerSlideIn && sprite->callback != SpriteCallbackDummy))
    {
        /* Taken by the game's vertical slide with the lift still on it: an
         * even y2 reaches 0 on its own. */
        if (sOwned == id && sprite->inUse && (sprite->y2 & 1))
            sprite->y2 += 1;
        sOwned = 0xFF;
        return;
    }
    if (sOwned == 0xFF)
    {
        if (sprite->y2 != 0)
            return;
        sOwned = id;
    }
    else if (sprite->y2 != sWritten)
    {
        sOwned = 0xFF;
        return;
    }
    sprite->y2 = (s16)lift;
    sWritten = (s16)lift;
    if (lift == 0)
        sOwned = 0xFF;
}

void CtrBattleHud_Clear(void)
{
    for (int i = 0; i < CTR_HUD_BOXES; ++i)
        sBoxes[i].used = false;
}

const CtrHudBox *CtrBattleHud_Boxes(void)
{
    if (!CtrBattleHud_Ready())
        return NULL;
    for (int i = 0; i < CTR_HUD_BOXES; ++i)
    {
        Slot *slot = &sSlots[i];
        float fade;
        uint32_t fadeColour, signature;

        if (!sBoxes[i].used)
            continue;
        /* What can change without the game telling: the doubles' numbers, the
         * box's palette. The palette fade is the compositor's tint, not a new
         * picture: one each step would be the whole box drawn again. */
        signature = (FadeOf(slot, &fade, &fadeColour) >> 16) * 3u;
        sBoxes[i].fade = fade;
        sBoxes[i].fadeColour = fadeColour;
        if (sLayouts[slot->kind].numbers && gBattleSpritesDataPtr)
            signature += gBattleSpritesDataPtr->battlerData[slot->battler].hpNumbersNoBars;
        if (slot->dirty || signature != slot->signature)
        {
            slot->signature = signature;
            slot->dirty = false;
            Compose(i);
            sBoxes[i].version++;
        }
    }
    return sBoxes;
}
