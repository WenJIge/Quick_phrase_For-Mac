// ============================================================================
// QuickPhrase - macOS Liquid Glass Edition (Enhanced Animations)
// ============================================================================
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "platform_mac.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ==================== 常量 ====================
constexpr int DEF_W = 900;
constexpr int DEF_H = 820;
constexpr int MIN_W = 760;
constexpr int MIN_H = 600;
constexpr int TITLEBAR_H = 48;
constexpr int ROW_H = 68;
constexpr int HEADER_H = TITLEBAR_H + 138;
constexpr int FOOTER_H = 76;
constexpr int MAX_HOTKEYS = 9;
constexpr int SETTINGS_TABBAR_Y = TITLEBAR_H + 74;
constexpr int SETTINGS_CONTENT_TOP = TITLEBAR_H + 128;
constexpr int EDGE_MARGIN = 24;

constexpr int PZ_MODE_TOP   = 60;
constexpr int PZ_THEME_TOP  = 150;
constexpr int PZ_FAMILY_TOP = 320;
constexpr int PZ_FONT_TOP   = 400;
constexpr int PZ_STYLE_TOP  = 476;
constexpr int PZ_AE_TOP     = 568;
constexpr int PZ_AS_TOP     = 644;
constexpr int PZ_FPS_TOP    = 720;
constexpr int PZ_DP_TOP     = 796;
constexpr int PZ_CONTENT_H  = 990;

constexpr int HK_HINT_H = 60;
constexpr int HK_ROW_H  = 64;

// ==================== 字体候选 ====================
struct FontDef { const char* display; const char* path; };
const FontDef FONTS_CANDIDATES[] = {
    {"苹方",            "/System/Library/Fonts/PingFang.ttc"},
    {"苹方",            "/System/Library/Fonts/Supplemental/PingFang.ttc"},
    {"宋体",            "/System/Library/Fonts/Supplemental/Songti.ttc"},
    {"黑体-简",         "/System/Library/Fonts/STHeiti Light.ttc"},
    {"黑体-中黑",       "/System/Library/Fonts/STHeiti Medium.ttc"},
    {"楷体",            "/Library/Fonts/Kaiti.ttc"},
    {"仿宋",            "/Library/Fonts/STFangsong.ttf"},
    {"Helvetica",       "/System/Library/Fonts/Helvetica.ttc"},
    {"Arial",           "/Library/Fonts/Arial.ttf"},
    {"Times New Roman", "/System/Library/Fonts/Times.ttc"},
    {"Avenir Next",     "/System/Library/Fonts/Avenir Next.ttc"},
    {"Menlo",           "/System/Library/Fonts/Menlo.ttc"},
};
constexpr int FONTS_CANDIDATES_COUNT =
    sizeof(FONTS_CANDIDATES) / sizeof(FONTS_CANDIDATES[0]);

// ==================== 全局 ====================
int   g_winW = DEF_W;
int   g_winH = DEF_H;
float g_dpiScale = 1.0f;
float g_dt = 1.f / 60.f;   // ★ 帧间隔（秒）

SDL_Renderer* g_renderer = nullptr;
std::unordered_map<std::string, SDL_Texture*> g_texCache;

void clearTexCache() {
    for (auto& kv : g_texCache) SDL_DestroyTexture(kv.second);
    g_texCache.clear();
}

enum class Page { Main, Settings };
enum class Tab  { Hotkeys, Personalize, About };

// ==================== 缓动函数 ====================
inline float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

inline float easeOutCubic(float t) {
    float p = 1.f - t;
    return 1.f - p * p * p;
}
inline float easeOutQuint(float t) {
    float p = 1.f - t;
    return 1.f - p * p * p * p * p;
}
inline float easeInOutCubic(float t) {
    return t < 0.5f
        ? 4.f * t * t * t
        : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
}
inline float easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.f;
    float p = t - 1.f;
    return 1.f + c3 * p * p * p + c1 * p * p;
}
inline float easeOutElastic(float t) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    const float c4 = (2.f * 3.14159265f) / 3.f;
    return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
}

// ★ 帧率无关的指数平滑：cur 趋近 target，rate 越大越快
inline float smoothTo(float cur, float target, float rate, float dt) {
    if (rate <= 0.f || dt <= 0.f) return cur;
    float k = 1.f - std::exp(-rate * dt);
    return cur + (target - cur) * k;
}

// ★ 弹簧（可产生自然过冲）
inline void springTo(float& value, float& velocity, float target,
                     float stiffness, float damping, float dt) {
    float accel = (target - value) * stiffness - velocity * damping;
    velocity += accel * dt;
    value += velocity * dt;
}

// ==================== 动画状态 ====================
struct AnimState {
    float value = 0.f;
    float velocity = 0.f;
};

// ==================== 窗口缩放边缘 ====================
enum class ResizeEdge {
    None,
    Left, Right, Top, Bottom,
    TopLeft, TopRight, BottomLeft, BottomRight
};
constexpr int RESIZE_BORDER = 8;

// ==================== 光标缓存 ====================
static SDL_Cursor* g_cursorArrow = nullptr;
static SDL_Cursor* g_cursorHand  = nullptr;
static SDL_Cursor* g_cursorNWSE  = nullptr;
static SDL_Cursor* g_cursorNESW  = nullptr;
static SDL_Cursor* g_cursorWE    = nullptr;
static SDL_Cursor* g_cursorNS    = nullptr;
static SDL_SystemCursor g_currentCursor = SDL_SYSTEM_CURSOR_ARROW;

static void initCursors() {
    g_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    g_cursorHand  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    g_cursorNWSE  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    g_cursorNESW  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    g_cursorWE    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    g_cursorNS    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
}
static void freeCursors() {
    if (g_cursorArrow) { SDL_FreeCursor(g_cursorArrow); g_cursorArrow = nullptr; }
    if (g_cursorHand)  { SDL_FreeCursor(g_cursorHand);  g_cursorHand  = nullptr; }
    if (g_cursorNWSE)  { SDL_FreeCursor(g_cursorNWSE);  g_cursorNWSE  = nullptr; }
    if (g_cursorNESW)  { SDL_FreeCursor(g_cursorNESW);  g_cursorNESW  = nullptr; }
    if (g_cursorWE)    { SDL_FreeCursor(g_cursorWE);    g_cursorWE    = nullptr; }
    if (g_cursorNS)    { SDL_FreeCursor(g_cursorNS);    g_cursorNS    = nullptr; }
    g_currentCursor = SDL_SYSTEM_CURSOR_ARROW;
}
static void setCursorType(SDL_SystemCursor t) {
    if (g_currentCursor == t) return;
    g_currentCursor = t;
    SDL_Cursor* c = g_cursorArrow;
    switch (t) {
        case SDL_SYSTEM_CURSOR_HAND:     c = g_cursorHand;  break;
        case SDL_SYSTEM_CURSOR_SIZENWSE: c = g_cursorNWSE;  break;
        case SDL_SYSTEM_CURSOR_SIZENESW: c = g_cursorNESW;  break;
        case SDL_SYSTEM_CURSOR_SIZEWE:   c = g_cursorWE;    break;
        case SDL_SYSTEM_CURSOR_SIZENS:   c = g_cursorNS;    break;
        default:                         c = g_cursorArrow; break;
    }
    if (c) SDL_SetCursor(c);
}

// ==================== 调色板 ====================
struct Palette {
    SDL_Color bg, card, cardHover, border, borderSoft;
    SDL_Color text, muted, subtle;
    SDL_Color primary, primaryHover, primaryActive;
    SDL_Color danger, dangerHover;
    SDL_Color tagBg, tagHover, tagActive;
    SDL_Color ok, warn, shadow;
    SDL_Color glassFill, glassBorder, glassHighlight;
};

Palette lightPalette() {
    Palette p;
    p.bg           = {244,246,250,140};
    p.card         = {255,255,255,80};
    p.cardHover    = {255,255,255,120};
    p.border       = {226,232,240,160};
    p.borderSoft   = {236,240,246,100};
    p.text         = {22,32,48,255};
    p.muted        = {75,90,110,255};
    p.subtle       = {120,135,155,255};
    p.danger       = {239,68,68,255};
    p.dangerHover  = {248,113,113,255};
    p.tagBg        = {255,255,255,60};
    p.tagHover     = {255,255,255,120};
    p.tagActive    = {255,255,255,160};
    p.ok           = {34,197,94,255};
    p.warn         = {234,179,8,255};
    p.shadow       = {0,0,0,40};
    p.primary      = {37,99,235,255};
    p.primaryHover = {59,130,246,255};
    p.primaryActive= {29,78,216,255};
    p.glassFill    = {255,255,255,70};
    p.glassBorder  = {255,255,255,180};
    p.glassHighlight = {255,255,255,220};
    return p;
}
Palette darkPalette() {
    Palette p;
    p.bg           = {20,22,26,140};
    p.card         = {255,255,255,22};
    p.cardHover    = {255,255,255,40};
    p.border       = {255,255,255,45};
    p.borderSoft   = {255,255,255,25};
    p.text         = {240,242,246,255};
    p.muted        = {170,178,190,255};
    p.subtle       = {120,128,140,255};
    p.danger       = {248,113,113,255};
    p.dangerHover  = {252,165,165,255};
    p.tagBg        = {255,255,255,18};
    p.tagHover     = {255,255,255,40};
    p.tagActive    = {255,255,255,65};
    p.ok           = {74,222,128,255};
    p.warn         = {251,191,36,255};
    p.shadow       = {0,0,0,100};
    p.primary      = {59,130,246,255};
    p.primaryHover = {96,165,250,255};
    p.primaryActive= {37,99,235,255};
    p.glassFill    = {255,255,255,20};
    p.glassBorder  = {255,255,255,65};
    p.glassHighlight = {255,255,255,110};
    return p;
}

// ==================== 主题色 ====================
struct ThemeDef { const char* name; SDL_Color normal, hover, active; };
const ThemeDef THEMES[] = {
    {"蓝色", {37,99,235,255},   {59,130,246,255}, {29,78,216,255}},
    {"绿色", {34,197,94,255},   {74,222,128,255}, {22,163,74,255}},
    {"紫色", {147,51,234,255},  {168,85,247,255}, {126,34,206,255}},
    {"橙色", {249,115,22,255},  {251,146,60,255}, {234,88,12,255}},
    {"红色", {239,68,68,255},   {248,113,113,255},{220,38,38,255}},
    {"青色", {6,182,212,255},   {34,211,238,255}, {8,145,178,255}},
    {"粉色", {236,72,153,255},  {244,114,182,255},{219,39,119,255}},
    {"靛蓝", {99,102,241,255},  {129,140,248,255},{79,70,229,255}},
    {"青绿", {20,184,166,255},  {45,212,191,255}, {13,148,136,255}},
    {"金色", {245,158,11,255},  {251,191,36,255}, {217,119,6,255}},
};
constexpr int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

SDL_Color findThemeColor(const std::string& name) {
    for (int i = 0; i < THEME_COUNT; ++i)
        if (name == THEMES[i].name) return THEMES[i].normal;
    return THEMES[0].normal;
}

// ★ 主题过渡：带高亮脉冲
struct ThemeTransition {
    SDL_Color current{};
    SDL_Color target{};
    float pulse = 0.f;   // 切换时短暂变亮
    bool active = false;

    void setTarget(SDL_Color c) {
        target = c;
        active = true;
        pulse = 1.f;
    }
    void snapTo(SDL_Color c) {
        current = target = c;
        active = false;
        pulse = 0.f;
    }
    void update(float rate, float dt) {
        if (active) {
            current.r = (Uint8)std::round(smoothTo(current.r, target.r, rate, dt));
            current.g = (Uint8)std::round(smoothTo(current.g, target.g, rate, dt));
            current.b = (Uint8)std::round(smoothTo(current.b, target.b, rate, dt));
            current.a = 255;
            if (std::abs((int)current.r - (int)target.r) < 1 &&
                std::abs((int)current.g - (int)target.g) < 1 &&
                std::abs((int)current.b - (int)target.b) < 1) {
                current = target;
                active = false;
            }
        }
        pulse = smoothTo(pulse, 0.f, 5.f, dt);
        if (pulse < 0.01f) pulse = 0.f;
    }
    SDL_Color displayColor() const {
        if (pulse <= 0.f) return current;
        int d = (int)(30.f * pulse);
        return {
            (Uint8)std::clamp(current.r + d, 0, 255),
            (Uint8)std::clamp(current.g + d, 0, 255),
            (Uint8)std::clamp(current.b + d, 0, 255),
            255
        };
    }
};

bool systemIsDark() {
    FILE* f = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
    if (!f) return false;
    char buf[64] = {0};
    bool dark = false;
    if (fgets(buf, sizeof(buf), f)) if (strstr(buf, "Dark")) dark = true;
    pclose(f);
    return dark;
}

// ==================== 颜色工具 ====================
SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {
        (Uint8)std::round(a.r + (b.r - a.r) * t),
        (Uint8)std::round(a.g + (b.g - a.g) * t),
        (Uint8)std::round(a.b + (b.b - a.b) * t),
        (Uint8)std::round(a.a + (b.a - a.a) * t),
    };
}
SDL_Color adjustBright(SDL_Color c, int d) {
    return {
        (Uint8)std::clamp(c.r + d, 0, 255),
        (Uint8)std::clamp(c.g + d, 0, 255),
        (Uint8)std::clamp(c.b + d, 0, 255),
        c.a
    };
}
SDL_Color withAlpha(SDL_Color c, int a) {
    c.a = (Uint8)std::clamp(a, 0, 255);
    return c;
}

// ==================== 动画配置 ====================
struct AnimCfg {
    std::string effect = "淡入滑动";
    std::string speed  = "正常";
    std::string fps    = "60";
    bool enabled() const { return speed != "关闭"; }

    // ★ 速率（1/秒，越大越快）
    float rate() const {
        if (!enabled()) return 1000.f;
        if (speed == "慢") return 8.f;
        if (speed == "快") return 22.f;
        return 14.f;
    }
    float hoverRate() const {
        if (!enabled()) return 1000.f;
        if (speed == "慢") return 11.f;
        if (speed == "快") return 26.f;
        return 17.f;
    }
    float pressRate() const {
        if (!enabled()) return 1000.f;
        if (speed == "慢") return 22.f;
        if (speed == "快") return 48.f;
        return 32.f;
    }
    float reboundRate() const {
        if (!enabled()) return 1000.f;
        if (speed == "慢") return 12.f;
        if (speed == "快") return 26.f;
        return 18.f;
    }
    int frameDelay() const {
        if (fps == "30")  return 33;
        if (fps == "120") return 8;
        return 16;
    }
    int reboundDelay() const {
        if (!enabled()) return 0;
        if (speed == "慢") return 90;
        if (speed == "快") return 35;
        return 60;
    }
    float pageOffsetDist() const {
        if (effect == "淡入滑动") return 32.f;
        return 0.f;
    }
    float pageDuration() const {
        if (speed == "慢") return 0.55f;
        if (speed == "快") return 0.20f;
        return 0.36f;
    }
    // 弹簧参数（用于按钮和指示条）
    float springStiffness() const {
        if (!enabled()) return 1000.f;
        if (speed == "慢") return 140.f;
        if (speed == "快") return 260.f;
        return 200.f;
    }
    float springDamping() const {
        if (!enabled()) return 100.f;
        if (speed == "慢") return 20.f;
        if (speed == "快") return 26.f;
        return 22.f;
    }
};

// ==================== 平滑滚动 ====================
struct Scroll {
    float pos = 0.f;
    float target = 0.f;
    float maxScroll = 0.f;
    Uint32 lastInput = 0;
    bool   hasInput = false;

    void reset() { pos = target = 0.f; hasInput = false; }

    void wheel(float delta) {
        target += delta;
        if (target < 0.f) target *= 0.35f;
        else if (target > maxScroll)
            target = maxScroll + (target - maxScroll) * 0.35f;
        lastInput = SDL_GetTicks();
        hasInput = true;
    }

    void update(float rate, int reboundDelay, float reboundRate, float dt) {
        pos = smoothTo(pos, target, rate, dt);

        Uint32 now = SDL_GetTicks();
        if (hasInput && (int)(now - lastInput) > reboundDelay) {
            if (target < 0.f) {
                target = smoothTo(target, 0.f, reboundRate, dt);
                if (target > -0.4f) target = 0.f;
            } else if (target > maxScroll) {
                target = smoothTo(target, maxScroll, reboundRate, dt);
                if (target < maxScroll + 0.4f) target = maxScroll;
            }
        }
        if (std::fabs(target - pos) < 0.3f) pos = target;
    }
    int offset() const { return (int)std::round(pos); }
};

// ==================== 抗锯齿纹理 ====================
SDL_Texture* getRoundRectTex(int w, int h, int radius, SDL_Color fill,
                              int borderW = 0, SDL_Color border = {0,0,0,0}) {
    if (!g_renderer || w <= 0 || h <= 0) return nullptr;
    int pw  = (int)std::round(w * g_dpiScale);
    int ph  = (int)std::round(h * g_dpiScale);
    int pr  = (int)std::round(radius * g_dpiScale);
    int pbw = (int)std::round(borderW * g_dpiScale);
    if (pw <= 0 || ph <= 0) return nullptr;

    char key[220];
    snprintf(key, sizeof(key), "rr_%d_%d_%d_%d_%02x%02x%02x%02x_%02x%02x%02x%02x",
             pw, ph, pr, pbw,
             fill.r, fill.g, fill.b, fill.a,
             border.r, border.g, border.b, border.a);
    auto it = g_texCache.find(key);
    if (it != g_texCache.end()) return it->second;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;

    float halfW = pw * 0.5f, halfH = ph * 0.5f;
    float r = (float)std::min({pr, pw / 2, ph / 2});
    float bx = halfW - r, by = halfH - r;

    SDL_LockSurface(surf);
    Uint32* pixels = (Uint32*)surf->pixels;
    int pitch = surf->pitch / 4;
    SDL_PixelFormat* fmt = surf->format;

    for (int y = 0; y < ph; ++y) {
        for (int x = 0; x < pw; ++x) {
            float px = x + 0.5f - halfW;
            float py = y + 0.5f - halfH;
            float qx = std::fabs(px) - bx;
            float qy = std::fabs(py) - by;
            float ox = std::max(qx, 0.f);
            float oy = std::max(qy, 0.f);
            float d = std::sqrt(ox * ox + oy * oy)
                    + std::min(std::max(qx, qy), 0.f) - r;
            float aOuter = std::clamp(0.5f - d, 0.f, 1.f);

            SDL_Color c;
            if (pbw > 0 && border.a > 0) {
                float aInner = std::clamp(0.5f - (d + pbw), 0.f, 1.f);
                float aBorder = aOuter - aInner;
                float aFill = aInner;
                float ba = aBorder * (border.a / 255.f);
                float fa = aFill * (fill.a / 255.f);
                float fA = ba + fa * (1.f - ba);
                if (fA < 0.003f) {
                    c = {0, 0, 0, 0};
                } else {
                    c.r = (Uint8)std::clamp((border.r*ba + fill.r*fa*(1.f-ba))/fA, 0.f, 255.f);
                    c.g = (Uint8)std::clamp((border.g*ba + fill.g*fa*(1.f-ba))/fA, 0.f, 255.f);
                    c.b = (Uint8)std::clamp((border.b*ba + fill.b*fa*(1.f-ba))/fA, 0.f, 255.f);
                    c.a = (Uint8)std::clamp(fA * 255.f, 0.f, 255.f);
                }
            } else {
                c = fill;
                c.a = (Uint8)std::round(fill.a * aOuter);
            }
            pixels[y * pitch + x] = SDL_MapRGBA(fmt, c.r, c.g, c.b, c.a);
        }
    }
    SDL_UnlockSurface(surf);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);
    g_texCache[key] = tex;
    return tex;
}

SDL_Texture* getCircleTex(int diameter, SDL_Color fill,
                           int borderW = 0, SDL_Color border = {0,0,0,0}) {
    return getRoundRectTex(diameter, diameter, diameter / 2, fill, borderW, border);
}

SDL_Texture* getGlowTex(int radius) {
    if (!g_renderer || radius <= 0) return nullptr;
    char key[64];
    snprintf(key, sizeof(key), "glow_%d", radius);
    auto it = g_texCache.find(key);
    if (it != g_texCache.end()) return it->second;

    int size = radius * 2;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;

    SDL_LockSurface(surf);
    Uint32* pixels = (Uint32*)surf->pixels;
    int pitch = surf->pitch / 4;
    SDL_PixelFormat* fmt = surf->format;
    float cx = size / 2.f, cy = size / 2.f;
    float r2 = (float)(radius * radius);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float d2 = dx * dx + dy * dy;
            float alpha = std::max(0.f, 1.f - d2 / r2);
            alpha = alpha * alpha;
            Uint8 a = (Uint8)(alpha * 255);
            pixels[y * pitch + x] = SDL_MapRGBA(fmt, 255, 255, 255, a);
        }
    }
    SDL_UnlockSurface(surf);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);
    g_texCache[key] = tex;
    return tex;
}

// ==================== 基础绘图 ====================
void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}
void fillRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int radius, SDL_Color c) {
    if (rc.w <= 0 || rc.h <= 0) return;
    radius = std::min(radius, std::min(rc.w / 2, rc.h / 2));
    if (radius <= 0) { fillRect(r, rc, c); return; }
    SDL_Texture* tex = getRoundRectTex(rc.w, rc.h, radius, c);
    if (tex) {
        SDL_Rect dst = rc;
        SDL_RenderCopy(r, tex, nullptr, &dst);
    }
}
bool pointIn(const SDL_Rect& rc, int x, int y) {
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

// ==================== 图形符号 ====================
void drawThickLine(SDL_Renderer* r, float x1, float y1, float x2, float y2,
                   int thickness, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len, ny = dx / len;
    int half = std::max(0, thickness / 2);
    for (int i = -half; i <= half; ++i) {
        int ox = (int)std::round(nx * i);
        int oy = (int)std::round(ny * i);
        SDL_RenderDrawLine(r,
            (int)std::round(x1) + ox, (int)std::round(y1) + oy,
            (int)std::round(x2) + ox, (int)std::round(y2) + oy);
    }
}
void drawCheckmark(SDL_Renderer* r, int cx, int cy, int size, int thickness,
                    SDL_Color c) {
    float half = size * 0.5f;
    drawThickLine(r, cx - half * 0.85f, cy + half * 0.05f,
                     cx - half * 0.20f, cy + half * 0.55f, thickness, c);
    drawThickLine(r, cx - half * 0.20f, cy + half * 0.55f,
                     cx + half * 0.85f, cy - half * 0.65f, thickness, c);
}
void drawCross(SDL_Renderer* r, int cx, int cy, int size, int thickness,
               SDL_Color c) {
    float half = size * 0.5f * 0.75f;
    drawThickLine(r, cx - half, cy - half, cx + half, cy + half, thickness, c);
    drawThickLine(r, cx - half, cy + half, cx + half, cy - half, thickness, c);
}
void drawCaretDown(SDL_Renderer* r, int cx, int cy, int size, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    int half = size / 2;
    for (int y = 0; y <= half; ++y) {
        int w = half - y;
        SDL_RenderDrawLine(r, cx - w, cy + y, cx + w, cy + y);
    }
}
void drawMinus(SDL_Renderer* r, int cx, int cy, int size, int thickness, SDL_Color c) {
    drawThickLine(r, cx - size/2.f, cy, cx + size/2.f, cy, thickness, c);
}
void drawPlus(SDL_Renderer* r, int cx, int cy, int size, int thickness, SDL_Color c) {
    drawThickLine(r, cx - size/2.f, cy, cx + size/2.f, cy, thickness, c);
    drawThickLine(r, cx, cy - size/2.f, cx, cy + size/2.f, thickness, c);
}

// ==================== 文本绘制 ====================
SDL_Texture* makeText(SDL_Renderer* r, TTF_Font* f, const std::string& s,
                      SDL_Color c, int& w, int& h) {
    w = h = 0;
    if (s.empty() || !f) return nullptr;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) return nullptr;
    w = (int)std::round(surf->w / g_dpiScale);
    h = (int)std::round(surf->h / g_dpiScale);
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return t;
}
void drawTextCenteredInRect(SDL_Renderer* r, TTF_Font* f, const std::string& s,
                             const SDL_Rect& rc, SDL_Color c,
                             bool shadow = false) {
    if (s.empty() || !f) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    int sw = surf->w, sh = surf->h;
    int ascent = TTF_FontAscent(f);
    int cx = rc.x + rc.w / 2;
    int cy = rc.y + rc.h / 2;
    int lw = (int)std::round(sw / g_dpiScale);
    int lh = (int)std::round(sh / g_dpiScale);
    float ascLog = ascent / g_dpiScale;
    int texY = cy - (int)std::round(ascLog * 0.5f);

    if (shadow) {
        SDL_Color shColor = {0, 0, 0, 60};
        SDL_SetTextureColorMod(tex, shColor.r, shColor.g, shColor.b);
        SDL_SetTextureAlphaMod(tex, shColor.a);
        SDL_Rect sdst = { cx - lw / 2, texY + 1, lw, lh };
        SDL_RenderCopy(r, tex, nullptr, &sdst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    SDL_Rect dst = { cx - lw / 2, texY, lw, lh };
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}
void drawText(SDL_Renderer* r, TTF_Font* f, const std::string& s,
              int x, int y, SDL_Color c) {
    int w, h;
    SDL_Texture* t = makeText(r, f, s, c, w, h);
    if (!t) return;
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}
void drawTextCenter(SDL_Renderer* r, TTF_Font* f, const std::string& s,
                    int cx, int cy, SDL_Color c) {
    int w, h;
    SDL_Texture* t = makeText(r, f, s, c, w, h);
    if (!t) return;
    SDL_Rect dst = { cx - w / 2, cy - h / 2, w, h };
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

// ==================== 玻璃卡片 ====================
void drawGlassCard(SDL_Renderer* r, const SDL_Rect& rc, int radius,
                   const Palette& pal, float hoverAmount = 0.f) {
    SDL_Color sh = pal.shadow;
    sh.a = (Uint8)std::clamp((int)(sh.a * (1.f + hoverAmount * 0.7f)), 0, 255);
    int shadowOffset = 2 + (int)(hoverAmount * 2.f);
    fillRoundRect(r, { rc.x, rc.y + shadowOffset, rc.w, rc.h }, radius, sh);

    SDL_Color fill = lerpColor(pal.card, pal.cardHover, hoverAmount);
    SDL_Texture* ft = getRoundRectTex(rc.w, rc.h, radius, fill);
    if (ft) { SDL_Rect dst = rc; SDL_RenderCopy(r, ft, nullptr, &dst); }

    SDL_Rect topHL = { rc.x + 4, rc.y + 1, rc.w - 8, 1 };
    fillRect(r, topHL, withAlpha(pal.glassHighlight, (int)(30 + 30 * hoverAmount)));

    SDL_Color bd = lerpColor(
        withAlpha(pal.glassBorder, pal.glassBorder.a * 3 / 4),
        pal.glassBorder, hoverAmount);
    SDL_Texture* bt = getRoundRectTex(rc.w, rc.h, radius, {0,0,0,0}, 1, bd);
    if (bt) { SDL_Rect dst = rc; SDL_RenderCopy(r, bt, nullptr, &dst); }
}

// ==================== 按钮 ====================
enum class BtnStyle { Normal, Primary, Danger, Tag, Ghost };
struct Button {
    SDL_Rect rect{};
    std::string label;
    std::string id;
    BtnStyle style = BtnStyle::Normal;
    std::function<void()> onClick;
    bool   selected = false;
    float  hover = 0.f;
    float  press = 0.f;
    SDL_Color swatch{0,0,0,0};
    bool     clip = false;
    SDL_Rect clipRect{};
    int      rightIcon = 0;
    int      leftIcon  = 0;
};

// ==================== 列表行布局 ====================
struct RowLayout {
    int hkX, hkW;
    int delX, delW;
    int copyX, copyW;
    int textX, textMaxW;
};

// ==================== App ====================
class App {
public:
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font*     fontSmall = nullptr;
    TTF_Font*     fontNormal= nullptr;
    TTF_Font*     fontBig   = nullptr;
    TTF_Font*     fontHuge  = nullptr;
    std::string   fontPath;

    struct FontEntry {
        std::string display;
        std::string path;
        TTF_Font* preview = nullptr;
    };
    std::vector<FontEntry> fonts;
    int currentFont = 0;
    std::string configFontName;
    bool   fontPickerOpen = false;
    Scroll fontPickerScroll;
    std::vector<std::pair<SDL_Rect, int>> fontPickerHits;
    SDL_Rect fontPickerPanel{};
    SDL_Rect fontPickerCloseBtn{};

    Page page = Page::Main;
    Tab  tab  = Tab::Hotkeys;
    Palette pal;
    ThemeTransition themeAnim;

    std::vector<std::string> phrases;
    std::vector<std::string> hotkeys;

    std::string colorMode   = "system";
    std::string themeName   = "蓝色";
    int         fontSize    = 14;
    bool        fontBold    = false;
    bool        fontItalic  = false;
    AnimCfg     animCfg;
    std::string defaultPage = "快捷键";

    std::string inputBuf;
    bool        inputFocused = false;
    SDL_Rect    inputRect{};
    float       inputFocusAnim = 0.f;

    std::string statusMsg;
    float       statusAnim = 0.f;
    Uint32      statusStart = 0;
    Uint32      statusEnd   = 0;
    bool        statusActive = false;

    int         mouseX = 0, mouseY = 0;
    bool        mouseDown = false;
    std::string pressedId;
    bool        paused = false;
    bool        running = true;
    bool        hiddenToTray = false;

    bool  dragging = false;
    int   dragOffsetX = 0, dragOffsetY = 0;

    ResizeEdge resizeEdge = ResizeEdge::None;
    bool resizing = false;
    int resizeStartX = 0, resizeStartY = 0;
    int resizeStartW = 0, resizeStartH = 0;
    int resizeStartWinX = 0, resizeStartWinY = 0;

    SDL_Rect btnClose{}, btnMin{}, btnMax{};
    float closeHover = 0.f, minHover = 0.f, maxHover = 0.f;

    Scroll mainScroll;
    Scroll settingsScroll;

    // ★ 页面/标签过渡
    float pageTransitionY = 0.f;
    float pageAlpha = 1.f;

    // ★ 页面入场动画计时器（秒）
    float pageEnterTimer = 0.f;
    std::vector<float> cardEntryAnim;   // 每行卡片的入场进度 0..1

    // ★ Tab 指示条弹簧
    float tabIndicatorX = 24.f;
    float tabIndicatorXVel = 0.f;
    float tabIndicatorTargetX = 24.f;
    float tabIndicatorW = 110.f;
    float tabIndicatorWVel = 0.f;
    float tabIndicatorTargetW = 110.f;

    // ★ 悬停/按下动画改为 AnimState
    std::unordered_map<std::string, AnimState> hoverAnim;
    std::unordered_map<std::string, AnimState> pressAnim;

    bool recordingHotkey = false;
    int  recordingIndex  = -1;
    std::string recordingText;

    std::vector<Button> buttons;

    std::string configDir, phrasesFile, hotkeysFile, configFile;

    // ==================== 字体扫描 ====================
    void scanFonts() {
        fonts.clear();
        std::vector<std::string> seenPaths;
        for (int i = 0; i < FONTS_CANDIDATES_COUNT; ++i) {
            const char* p = FONTS_CANDIDATES[i].path;
            std::error_code ec;
            if (!fs::exists(p, ec)) continue;
            bool dup = false;
            for (auto& sp : seenPaths) if (sp == p) { dup = true; break; }
            if (dup) continue;
            seenPaths.push_back(p);
            FontEntry e;
            e.display = FONTS_CANDIDATES[i].display;
            e.path    = p;
            e.preview = nullptr;
            fonts.push_back(e);
        }
        if (fonts.empty()) {
            FontEntry e;
            e.display = "Helvetica";
            e.path    = "/System/Library/Fonts/Helvetica.ttc";
            e.preview = nullptr;
            fonts.push_back(e);
        }
    }

    void selectFontByName(const std::string& name) {
        if (name.empty()) { currentFont = 0; return; }
        for (int i = 0; i < (int)fonts.size(); ++i)
            if (fonts[i].display == name) { currentFont = i; return; }
        currentFont = 0;
    }

    TTF_Font* getFontPreview(int idx) {
        if (idx < 0 || idx >= (int)fonts.size()) return nullptr;
        if (!fonts[idx].preview) {
            fonts[idx].preview = TTF_OpenFont(
                fonts[idx].path.c_str(),
                (int)std::round(14 * g_dpiScale));
        }
        return fonts[idx].preview;
    }

    void openFontPicker() {
        for (int i = 0; i < (int)fonts.size(); ++i) getFontPreview(i);
        fontPickerOpen = true;
        fontPickerScroll.reset();
    }
    void closeFontPicker() { fontPickerOpen = false; }

    void selectFont(int idx) {
        if (idx < 0 || idx >= (int)fonts.size()) return;
        currentFont = idx;
        fontPath = fonts[idx].path;
        loadFonts();
        saveConfig();
    }

    // ==================== 字体加载 ====================
    void loadFonts() {
        if (fontSmall)  TTF_CloseFont(fontSmall);
        if (fontNormal) TTF_CloseFont(fontNormal);
        if (fontBig)    TTF_CloseFont(fontBig);
        if (fontHuge)   TTF_CloseFont(fontHuge);
        int fs = fontSize;
        fontSmall  = TTF_OpenFont(fontPath.c_str(), (int)std::round((fs - 2) * g_dpiScale));
        fontNormal = TTF_OpenFont(fontPath.c_str(), (int)std::round(fs * g_dpiScale));
        fontBig    = TTF_OpenFont(fontPath.c_str(), (int)std::round((fs + 6) * g_dpiScale));
        fontHuge   = TTF_OpenFont(fontPath.c_str(), (int)std::round((fs + 14) * g_dpiScale));
        if (fontNormal && (fontBold || fontItalic)) {
            TTF_SetFontStyle(fontNormal,
                (fontBold ? TTF_STYLE_BOLD : 0) | (fontItalic ? TTF_STYLE_ITALIC : 0));
        }
    }

    void updateWindowMetrics() {
        if (!window || !renderer) return;
        int lw = 0, lh = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(window, &lw, &lh);
        SDL_GetRendererOutputSize(renderer, &pw, &ph);
        if (lw <= 0 || lh <= 0) return;
        g_winW = lw; g_winH = lh;
        float s = (pw > 0) ? (float)pw / (float)lw : 1.0f;
        if (s < 0.1f) s = 1.0f;
        if (std::fabs(s - g_dpiScale) > 0.01f) {
            g_dpiScale = s;
            clearTexCache();
            loadFonts();
            for (auto& f : fonts) {
                if (f.preview) { TTF_CloseFont(f.preview); f.preview = nullptr; }
            }
        }
        SDL_RenderSetLogicalSize(renderer, g_winW, g_winH);
    }

    // ==================== 初始化 ====================
    bool init() {
        char* cd = platform_config_dir();
        configDir = cd ? cd : ".";
        free(cd);
        phrasesFile = configDir + "/phrases.txt";
        hotkeysFile = configDir + "/hotkeys.txt";
        configFile  = configDir + "/config.txt";

        scanFonts();
        loadConfig();
        selectFontByName(configFontName);
        fontPath = fonts[currentFont].path;

        loadPhrases();
        loadHotkeys();

        bool dark = (colorMode == "dark") ||
                    (colorMode == "system" && systemIsDark());
        pal = dark ? darkPalette() : lightPalette();
        SDL_Color tc = findThemeColor(themeName);
        pal.primary       = tc;
        pal.primaryHover  = adjustBright(tc, 35);
        pal.primaryActive = adjustBright(tc, -25);
        themeAnim.snapTo(tc);

        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

        if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
        platform_init();
        if (TTF_Init() != 0) return false;

        window = SDL_CreateWindow("QuickPhrase",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            DEF_W, DEF_H,
            SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
        if (!window) return false;

        renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) return false;
        g_renderer = renderer;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        platform_make_window_transparent();

        updateWindowMetrics();
        loadFonts();

        initCursors();

        SDL_StartTextInput();
        registerHotkeys();
        setupTray();

        // 初始化卡片入场
        cardEntryAnim.assign(phrases.size(), 0.f);
        pageEnterTimer = 0.f;

        return true;
    }

    void shutdown() {
        SDL_StopTextInput();
        platform_unregister_all_hotkeys();
        platform_shutdown();
        clearTexCache();
        for (auto& f : fonts) if (f.preview) TTF_CloseFont(f.preview);
        fonts.clear();
        if (fontSmall)  TTF_CloseFont(fontSmall);
        if (fontNormal) TTF_CloseFont(fontNormal);
        if (fontBig)    TTF_CloseFont(fontBig);
        if (fontHuge)   TTF_CloseFont(fontHuge);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        freeCursors();
        TTF_Quit();
        SDL_Quit();
    }

    // ==================== 配置 ====================
    void loadConfig() {
        std::ifstream f(configFile);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            if (!v.empty() && v.back() == '\r') v.pop_back();
            if (k == "mode")        colorMode = v;
            else if (k == "theme")  themeName = v;
            else if (k == "font")   configFontName = v;
            else if (k == "fontsize") { try { fontSize = std::stoi(v); } catch(...){} }
            else if (k == "bold")   fontBold = (v == "1");
            else if (k == "italic") fontItalic = (v == "1");
            else if (k == "animeffect") animCfg.effect = v;
            else if (k == "animspeed")  animCfg.speed = v;
            else if (k == "animfps")    animCfg.fps = v;
            else if (k == "defaultpage") defaultPage = v;
        }
    }
    void saveConfig() {
        std::ofstream f(configFile);
        f << "mode=" << colorMode << "\n";
        f << "theme=" << themeName << "\n";
        f << "font=" << fonts[currentFont].display << "\n";
        f << "fontsize=" << fontSize << "\n";
        f << "bold=" << (fontBold ? 1 : 0) << "\n";
        f << "italic=" << (fontItalic ? 1 : 0) << "\n";
        f << "animeffect=" << animCfg.effect << "\n";
        f << "animspeed=" << animCfg.speed << "\n";
        f << "animfps=" << animCfg.fps << "\n";
        f << "defaultpage=" << defaultPage << "\n";
    }
    void loadPhrases() {
        std::ifstream f(phrasesFile);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            phrases.push_back(line);
        }
        if (phrases.empty()) {
            phrases = {
                "欢迎使用 QuickPhrase",
                "BUG 请反馈至 stonebey332211@outlook.com",
                "关注 UP 吧，求求您了(B 站: 雲-绮石Stone)",
            };
        }
    }
    void savePhrases() {
        std::ofstream f(phrasesFile);
        for (auto& s : phrases) f << s << "\n";
    }
    void loadHotkeys() {
        std::ifstream f(hotkeysFile);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            hotkeys.push_back(line);
        }
        ensureHotkeys();
    }
    void saveHotkeys() {
        std::ofstream f(hotkeysFile);
        for (auto& s : hotkeys) f << s << "\n";
    }
    void ensureHotkeys() {
        while ((int)hotkeys.size() < (int)phrases.size()) hotkeys.push_back("");
        if ((int)hotkeys.size() > (int)phrases.size()) hotkeys.resize(phrases.size());
    }

    // ==================== 热键/托盘 ====================
    void registerHotkeys() {
        platform_unregister_all_hotkeys();
        int n = std::min((int)phrases.size(), MAX_HOTKEYS);
        for (int i = 0; i < n; ++i) {
            if (i >= (int)hotkeys.size() || hotkeys[i].empty()) continue;
            platform_register_hotkey(i, hotkeys[i].c_str());
        }
    }
    void setupTray() {
        platform_create_tray("QuickPhrase");
        platform_tray_set_callback([](int action) {
            extern App* g_app;
            if (!g_app) return;
            switch (action) {
                case 0:
                    SDL_ShowWindow(g_app->window);
                    SDL_RaiseWindow(g_app->window);
                    g_app->hiddenToTray = false;
                    break;
                case 1:
                    g_app->paused = !g_app->paused;
                    platform_tray_set_paused(g_app->paused ? 1 : 0);
                    g_app->setStatus(g_app->paused ? "已暂停" : "已恢复");
                    break;
                case 2:
                    g_app->running = false;
                    break;
            }
        });
    }

    // ==================== 业务 ====================
    void setStatus(const std::string& s) {
        statusMsg = s;
        statusStart = SDL_GetTicks();
        statusEnd   = statusStart + 1800;
        statusActive = true;
        statusAnim = 0.f;
    }
    void pastePhrase(int idx) {
        if (idx < 0 || idx >= (int)phrases.size()) return;
        platform_set_clipboard(phrases[idx].c_str());
        platform_send_paste();
        setStatus("已粘贴第 " + std::to_string(idx + 1) + " 条");
    }
    void copyPhrase(int idx) {
        if (idx < 0 || idx >= (int)phrases.size()) return;
        platform_set_clipboard(phrases[idx].c_str());
        setStatus("已复制到剪贴板");
    }
    void deletePhrase(int idx) {
        if (idx < 0 || idx >= (int)phrases.size()) return;
        phrases.erase(phrases.begin() + idx);
        if (idx < (int)hotkeys.size()) hotkeys.erase(hotkeys.begin() + idx);
        ensureHotkeys();
        savePhrases(); saveHotkeys();
        registerHotkeys();
        // 卡片动画同步
        if (idx < (int)cardEntryAnim.size())
            cardEntryAnim.erase(cardEntryAnim.begin() + idx);
        setStatus("已删除");
    }
    void addPhrase() {
        if (inputBuf.empty()) return;
        phrases.push_back(inputBuf);
        hotkeys.push_back("");
        cardEntryAnim.push_back(0.f);   // 新卡片从头播放入场
        inputBuf.clear();
        savePhrases(); saveHotkeys();
        setStatus("已添加");
    }
    void togglePaused() {
        paused = !paused;
        platform_tray_set_paused(paused ? 1 : 0);
        setStatus(paused ? "已暂停" : "已恢复");
    }
    void toggleAutostart() {
        int cur = platform_get_launch_at_login();
        platform_set_launch_at_login(cur ? 0 : 1);
        setStatus(cur ? "已关闭开机自启" : "已开启开机自启");
    }
    void setColorMode(const std::string& mode) {
        colorMode = mode;
        bool dark = (colorMode == "dark") ||
                    (colorMode == "system" && systemIsDark());
        pal = dark ? darkPalette() : lightPalette();
        SDL_Color tc = themeAnim.current;
        pal.primary       = tc;
        pal.primaryHover  = adjustBright(tc, 35);
        pal.primaryActive = adjustBright(tc, -25);
        clearTexCache();
        saveConfig();
    }
    void setThemeColor(const std::string& name) {
        themeName = name;
        themeAnim.setTarget(findThemeColor(name));
        clearTexCache();
        saveConfig();
    }
    void setFontSize(int sz) {
        fontSize = std::clamp(sz, 10, 22);
        loadFonts();
        saveConfig();
    }
    void toggleBold()   { fontBold = !fontBold; loadFonts(); saveConfig(); }
    void toggleItalic() { fontItalic = !fontItalic; loadFonts(); saveConfig(); }
    void setAnimEffect(const std::string& s) { animCfg.effect = s; saveConfig(); }
    void setAnimSpeed (const std::string& s) { animCfg.speed  = s; saveConfig(); }
    void setAnimFps   (const std::string& s) { animCfg.fps    = s; saveConfig(); }
    void setDefaultPage(const std::string& p) { defaultPage = p; saveConfig(); }

    // ==================== 滚动上限 ====================
    void updateScrollLimits() {
        {
            int total = (int)phrases.size() * ROW_H;
            int view  = g_winH - HEADER_H - FOOTER_H;
            mainScroll.maxScroll = std::max(0.f, (float)(total - view));
        }
        if (tab == Tab::Hotkeys) {
            int n = std::min((int)phrases.size(), MAX_HOTKEYS);
            int contentH = HK_HINT_H + n * HK_ROW_H + 40;
            int view = g_winH - SETTINGS_CONTENT_TOP;
            settingsScroll.maxScroll = std::max(0.f, (float)(contentH - view));
        } else if (tab == Tab::Personalize) {
            int view = g_winH - SETTINGS_CONTENT_TOP;
            settingsScroll.maxScroll = std::max(0.f, (float)(PZ_CONTENT_H - view));
        } else {
            settingsScroll.maxScroll = 0.f;
        }
    }

    // ==================== 热键记录 ====================
    std::string buildHotkeyString(SDL_Keysym ks) {
        std::string r;
        if (ks.mod & KMOD_CTRL)  r += "ctrl+";
        if (ks.mod & KMOD_SHIFT) r += "shift+";
        if (ks.mod & KMOD_ALT)   r += "alt+";
        if (ks.mod & KMOD_GUI)   r += "cmd+";
        std::string k = SDL_GetKeyName(ks.sym);
        for (auto& c : k) c = (char)tolower((unsigned char)c);
        if (k == "return") k = "enter";
        if (k == " ")      k = "space";
        if (k.empty())     return "";
        return r + k;
    }
    void startRecording(int idx) {
        recordingHotkey = true;
        recordingIndex = idx;
        recordingText = "请按键…";
    }
    void cancelRecording() {
        recordingHotkey = false;
        recordingIndex = -1;
        recordingText.clear();
    }
    void handleHotkeyRecord(SDL_Keysym ks) {
        if (ks.sym == SDLK_ESCAPE) { cancelRecording(); return; }
        if (ks.sym == SDLK_BACKSPACE) {
            if (recordingIndex >= 0 && recordingIndex < (int)hotkeys.size()) {
                hotkeys[recordingIndex] = "";
                saveHotkeys(); registerHotkeys();
            }
            cancelRecording();
            return;
        }
        if (ks.sym == SDLK_LCTRL || ks.sym == SDLK_RCTRL ||
            ks.sym == SDLK_LSHIFT || ks.sym == SDLK_RSHIFT ||
            ks.sym == SDLK_LALT || ks.sym == SDLK_RALT ||
            ks.sym == SDLK_LGUI || ks.sym == SDLK_RGUI) return;
        std::string combo = buildHotkeyString(ks);
        if (combo.empty()) return;
        if (recordingIndex >= 0 && recordingIndex < (int)hotkeys.size()) {
            hotkeys[recordingIndex] = combo;
            saveHotkeys(); registerHotkeys();
        }
        cancelRecording();
    }

    // ==================== 窗口缩放 ====================
    ResizeEdge detectEdge(int mx, int my) {
        const int B = RESIZE_BORDER;
        bool left   = mx < B;
        bool right  = mx >= g_winW - B;
        bool top    = my < B;
        bool bottom = my >= g_winH - B;

        if (top && left)     return ResizeEdge::TopLeft;
        if (top && right)    return ResizeEdge::TopRight;
        if (bottom && left)  return ResizeEdge::BottomLeft;
        if (bottom && right) return ResizeEdge::BottomRight;
        if (left)            return ResizeEdge::Left;
        if (right)           return ResizeEdge::Right;
        if (top)             return ResizeEdge::Top;
        if (bottom)          return ResizeEdge::Bottom;
        return ResizeEdge::None;
    }
    void startResize(int mx, int my, ResizeEdge edge) {
        resizing = true;
        resizeEdge = edge;
        resizeStartX = mx;
        resizeStartY = my;
        SDL_GetWindowSize(window, &resizeStartW, &resizeStartH);
        SDL_GetWindowPosition(window, &resizeStartWinX, &resizeStartWinY);
    }
    void updateResize(int mx, int my) {
        if (!resizing) return;
        int dx = mx - resizeStartX;
        int dy = my - resizeStartY;

        bool isLeft   = resizeEdge == ResizeEdge::Left ||
                        resizeEdge == ResizeEdge::TopLeft ||
                        resizeEdge == ResizeEdge::BottomLeft;
        bool isRight  = resizeEdge == ResizeEdge::Right ||
                        resizeEdge == ResizeEdge::TopRight ||
                        resizeEdge == ResizeEdge::BottomRight;
        bool isTop    = resizeEdge == ResizeEdge::Top ||
                        resizeEdge == ResizeEdge::TopLeft ||
                        resizeEdge == ResizeEdge::TopRight;
        bool isBottom = resizeEdge == ResizeEdge::Bottom ||
                        resizeEdge == ResizeEdge::BottomLeft ||
                        resizeEdge == ResizeEdge::BottomRight;

        int newW = resizeStartW;
        int newH = resizeStartH;
        int newX = resizeStartWinX;
        int newY = resizeStartWinY;

        if (isLeft) {
            int w = resizeStartW - dx;
            if (w < MIN_W) w = MIN_W;
            newX = resizeStartWinX + (resizeStartW - w);
            newW = w;
        }
        if (isRight) {
            int w = resizeStartW + dx;
            if (w < MIN_W) w = MIN_W;
            newW = w;
        }
        if (isTop) {
            int h = resizeStartH - dy;
            if (h < MIN_H) h = MIN_H;
            newY = resizeStartWinY + (resizeStartH - h);
            newH = h;
        }
        if (isBottom) {
            int h = resizeStartH + dy;
            if (h < MIN_H) h = MIN_H;
            newH = h;
        }

        if (newW != resizeStartW || newH != resizeStartH)
            SDL_SetWindowSize(window, newW, newH);
        if (newX != resizeStartWinX || newY != resizeStartWinY)
            SDL_SetWindowPosition(window, newX, newY);
    }

    // ==================== 列表行布局 ====================
    RowLayout calcRowLayout() {
        RowLayout r;
        int cardLeft  = EDGE_MARGIN;
        int cardRight = g_winW - EDGE_MARGIN;

        r.copyW = 92;
        r.delW  = 84;
        int rightGap = 8;

        r.copyX = cardRight - r.copyW;
        r.delX  = r.copyX - rightGap - r.delW;

        r.hkX = cardLeft + 52;
        r.hkW = 110;
        int minSep = 24;
        if (r.hkX + r.hkW + minSep > r.delX)
            r.hkW = std::max(60, r.delX - r.hkX - minSep);

        r.textX = r.hkX + r.hkW + 28;
        r.textMaxW = r.delX - r.textX - 12;
        if (r.textMaxW < 40) r.textMaxW = 40;

        return r;
    }

    // ==================== 按钮工厂 ====================
    void addBtn(const std::string& id, const SDL_Rect& rc,
                const std::string& label, BtnStyle style,
                std::function<void()> cb, bool selected = false,
                const SDL_Rect* clipRect = nullptr,
                int rightIcon = 0, int leftIcon = 0) {
        Button b;
        b.id = id;
        b.rect = rc;
        b.label = label;
        b.style = style;
        b.onClick = std::move(cb);
        b.selected = selected;
        b.rightIcon = rightIcon;
        b.leftIcon = leftIcon;
        if (clipRect) { b.clip = true; b.clipRect = *clipRect; }

        bool hov = pointIn(rc, mouseX, mouseY);
        bool pr  = mouseDown && pressedId == id;

        AnimState& hs = hoverAnim[id];
        float hTarget = hov ? 1.f : 0.f;
        if (animCfg.enabled()) {
            hs.value = smoothTo(hs.value, hTarget, animCfg.hoverRate(), g_dt);
            if (std::fabs(hs.value - hTarget) < 0.005f) hs.value = hTarget;
        } else hs.value = hTarget;
        b.hover = hs.value;

        AnimState& ps = pressAnim[id];
        float pTarget = pr ? 1.f : 0.f;
        if (animCfg.enabled()) {
            // ★ 弹簧产生自然回弹
            springTo(ps.value, ps.velocity, pTarget,
                     animCfg.springStiffness(), animCfg.springDamping(), g_dt);
            ps.value = std::clamp(ps.value, -0.12f, 1.25f);
        } else {
            ps.value = pTarget;
            ps.velocity = 0.f;
        }
        b.press = ps.value;

        buttons.push_back(std::move(b));
    }

    void addSwatch(const std::string& id, const SDL_Rect& rc,
                   SDL_Color color, bool selected,
                   std::function<void()> cb,
                   const SDL_Rect* clipRect = nullptr) {
        Button b;
        b.id = id;
        b.rect = rc;
        b.style = BtnStyle::Tag;
        b.swatch = color;
        b.selected = selected;
        b.onClick = std::move(cb);
        if (clipRect) { b.clip = true; b.clipRect = *clipRect; }

        bool hov = pointIn(rc, mouseX, mouseY);
        bool pr  = mouseDown && pressedId == id;

        AnimState& hs = hoverAnim[id];
        float hTarget = hov ? 1.f : 0.f;
        if (animCfg.enabled()) {
            hs.value = smoothTo(hs.value, hTarget, animCfg.hoverRate(), g_dt);
            if (std::fabs(hs.value - hTarget) < 0.005f) hs.value = hTarget;
        } else hs.value = hTarget;
        b.hover = hs.value;

        AnimState& ps = pressAnim[id];
        float pTarget = pr ? 1.f : 0.f;
        if (animCfg.enabled()) {
            springTo(ps.value, ps.velocity, pTarget,
                     animCfg.springStiffness(), animCfg.springDamping(), g_dt);
            ps.value = std::clamp(ps.value, -0.12f, 1.25f);
        } else {
            ps.value = pTarget;
            ps.velocity = 0.f;
        }
        b.press = ps.value;

        buttons.push_back(std::move(b));
    }

    // ==================== 渲染按钮 ====================
    void renderButton(const Button& b) {
        if (b.clip) SDL_RenderSetClipRect(renderer, &b.clipRect);

        if (b.swatch.a > 0) {
            int cx = b.rect.x + b.rect.w / 2;
            int cy = b.rect.y + b.rect.h / 2;

            float scale = 1.f + b.hover * 0.08f - b.press * 0.12f;
            int size = (int)(40 * scale + 0.5f);

            if (b.selected || b.hover > 0.05f) {
                int glowRadius = size + 20;
                SDL_Texture* glowTex = getGlowTex(glowRadius / 2);
                if (glowTex) {
                    SDL_Rect gr = { cx - glowRadius/2, cy - glowRadius/2,
                                    glowRadius, glowRadius };
                    Uint8 alpha = b.selected ? 140 : (Uint8)(90 * b.hover);
                    SDL_SetTextureColorMod(glowTex, b.swatch.r, b.swatch.g, b.swatch.b);
                    SDL_SetTextureAlphaMod(glowTex, alpha);
                    SDL_RenderCopy(renderer, glowTex, nullptr, &gr);
                    SDL_SetTextureColorMod(glowTex, 255, 255, 255);
                    SDL_SetTextureAlphaMod(glowTex, 255);
                }
            }

            if (b.selected) {
                int outerSize = b.rect.w - 4;
                SDL_Texture* ot = getCircleTex(outerSize, pal.text);
                if (ot) {
                    SDL_Rect dst = { cx - outerSize/2, cy - outerSize/2,
                                     outerSize, outerSize };
                    SDL_RenderCopy(renderer, ot, nullptr, &dst);
                }
                int midSize = outerSize - 8;
                SDL_Texture* mt = getCircleTex(midSize, pal.card);
                if (mt) {
                    SDL_Rect dst = { cx - midSize/2, cy - midSize/2,
                                     midSize, midSize };
                    SDL_RenderCopy(renderer, mt, nullptr, &dst);
                }
            }

            SDL_Texture* mainTex = getCircleTex(size, b.swatch);
            if (mainTex) {
                SDL_Rect dst = { cx - size/2, cy - size/2, size, size };
                SDL_RenderCopy(renderer, mainTex, nullptr, &dst);
            }

            int hlSize = size / 3;
            SDL_Texture* hlTex = getGlowTex(hlSize);
            if (hlTex) {
                SDL_Rect hlRc = { cx - hlSize + 2, cy - size/2 + 2,
                                   hlSize * 2, hlSize };
                SDL_SetTextureAlphaMod(hlTex, 110);
                SDL_RenderCopy(renderer, hlTex, nullptr, &hlRc);
                SDL_SetTextureAlphaMod(hlTex, 255);
            }

            if (b.selected) {
                int ckSize = (int)(size * 0.55f);
                int ckThick = std::max(2, (int)(size * 0.10f));
                drawCheckmark(renderer, cx, cy, ckSize, ckThick,
                              {255, 255, 255, 255});
            }

            if (b.clip) SDL_RenderSetClipRect(renderer, nullptr);
            return;
        }

        SDL_Color fillBase, fillHover, fgNormal, fgHover, bdColor;
        bool hasBorder = false;

        switch (b.style) {
            case BtnStyle::Primary:
                fillBase  = withAlpha(pal.primary, 180);
                fillHover = withAlpha(pal.primaryHover, 220);
                fgNormal = fgHover = {255,255,255,255};
                bdColor = withAlpha(pal.primaryHover, 230);
                hasBorder = true;
                break;
            case BtnStyle::Danger:
                fillBase  = withAlpha(pal.danger, 180);
                fillHover = withAlpha(pal.dangerHover, 220);
                fgNormal = fgHover = {255,255,255,255};
                bdColor = withAlpha(pal.dangerHover, 230);
                hasBorder = true;
                break;
            case BtnStyle::Ghost:
                fillBase  = {0,0,0,0};
                fillHover = pal.tagHover;
                fgNormal = fgHover = pal.muted;
                break;
            case BtnStyle::Tag:
                if (b.selected) {
                    fillBase  = withAlpha(pal.primary, 190);
                    fillHover = withAlpha(pal.primaryHover, 230);
                    fgNormal = fgHover = {255,255,255,255};
                    bdColor = withAlpha(pal.primaryHover, 240);
                    hasBorder = true;
                } else {
                    fillBase  = pal.tagBg;
                    fillHover = pal.tagHover;
                    fgNormal = fgHover = pal.text;
                    bdColor = pal.glassBorder;
                    hasBorder = true;
                }
                break;
            default:
                fillBase  = pal.glassFill;
                fillHover = pal.cardHover;
                fgNormal = fgHover = pal.text;
                bdColor = pal.glassBorder;
                hasBorder = true;
                break;
        }

        SDL_Color base = lerpColor(fillBase, fillHover, b.hover);
        SDL_Color fg   = lerpColor(fgNormal, fgHover, b.hover);
        SDL_Color bd   = bdColor;

        // ★ 按下时缩小，弹簧会有轻微过冲
        float scale = 1.f;
        if (b.style != BtnStyle::Ghost) {
            scale += b.hover * 0.020f;
            scale -= b.press * 0.055f;
        }

        SDL_Rect rc = b.rect;
        if (scale != 1.f) {
            int nw = (int)(b.rect.w * scale + 0.5f);
            int nh = (int)(b.rect.h * scale + 0.5f);
            rc.x = b.rect.x + (b.rect.w - nw) / 2;
            rc.y = b.rect.y + (b.rect.h - nh) / 2;
            rc.w = nw;
            rc.h = nh;
        }

        int radius = rc.h / 3;
        if (radius > 14) radius = 14;

        if (b.hover > 0.05f && b.style != BtnStyle::Ghost) {
            SDL_Color sh = pal.shadow;
            sh.a = (Uint8)std::clamp((int)(sh.a * (0.5f + b.hover * 0.9f)), 0, 255);
            fillRoundRect(renderer, { rc.x, rc.y + 2, rc.w, rc.h }, radius, sh);
        }

        if (base.a > 0) {
            SDL_Texture* ft = getRoundRectTex(rc.w, rc.h, radius, base);
            if (ft) { SDL_Rect dst = rc; SDL_RenderCopy(renderer, ft, nullptr, &dst); }
        }

        if (base.a > 30) {
            SDL_Rect topHL = { rc.x + 4, rc.y + 1, rc.w - 8, 1 };
            fillRect(renderer, topHL, withAlpha(pal.glassHighlight, 60));
        }

        if (hasBorder) {
            SDL_Texture* bt = getRoundRectTex(rc.w, rc.h, radius,
                                               {0,0,0,0}, 1, bd);
            if (bt) { SDL_Rect dst = rc; SDL_RenderCopy(renderer, bt, nullptr, &dst); }
        }

        if (b.leftIcon != 0) {
            int iconCx = rc.x + 16;
            int iconCy = rc.y + rc.h / 2;
            if (b.leftIcon == 1) drawMinus(renderer, iconCx, iconCy, 12, 2, fg);
            else if (b.leftIcon == 2) drawPlus(renderer, iconCx, iconCy, 12, 2, fg);
        }

        if (!b.label.empty()) {
            SDL_Rect textRc = rc;
            if (b.rightIcon != 0) textRc.w -= 24;
            if (b.leftIcon != 0) { textRc.x += 20; textRc.w -= 20; }
            drawTextCenteredInRect(renderer, fontNormal, b.label, textRc, fg, true);
        }

        if (b.rightIcon == 1) {
            drawCaretDown(renderer, rc.x + rc.w - 18, rc.y + rc.h / 2, 10, fg);
        } else if (b.rightIcon == 2) {
            drawCheckmark(renderer, rc.x + rc.w - 18, rc.y + rc.h / 2, 14, 2, fg);
        } else if (b.rightIcon == 3) {
            drawCross(renderer, rc.x + rc.w - 18, rc.y + rc.h / 2, 14, 2, fg);
        }

        if (b.clip) SDL_RenderSetClipRect(renderer, nullptr);
    }

    // ==================== 主页面按钮 ====================
    void buildMainButtons() {
        // 输入框 + 添加
        int inputX = EDGE_MARGIN;
        int inputW = g_winW - 220;
        int minInputW = 120;
        int addW = 152;
        int rightLimit = g_winW - EDGE_MARGIN;
        int addX = rightLimit - addW;
        if (addX < inputX + minInputW + 12) {
            addX = inputX + minInputW + 12;
            addW = rightLimit - addX;
            if (addW < 60) addW = 60;
        }
        inputW = addX - 12 - inputX;
        if (inputW < minInputW) inputW = minInputW;

        inputRect = { inputX, TITLEBAR_H + 84, inputW, 38 };
        addBtn("main_add", { addX, TITLEBAR_H + 84, addW, 38 }, "添加",
               BtnStyle::Primary, [this]() { addPhrase(); });

        // 底栏
        int by = g_winH - FOOTER_H + 18;
        int rightEdge = g_winW - EDGE_MARGIN;

        int quitW = 76;
        int quitX = rightEdge - quitW;
        if (quitX < EDGE_MARGIN) quitX = EDGE_MARGIN;
        addBtn("main_quit", { quitX, by, quitW, 40 }, "退出",
               BtnStyle::Danger, [this]() { running = false; });

        int setW = 90;
        int setX = quitX - 8 - setW;
        if (setX < EDGE_MARGIN) setX = EDGE_MARGIN;
        addBtn("main_settings", { setX, by, setW, 40 }, "设置",
               BtnStyle::Primary, [this]() { switchPage(Page::Settings); });

        int leftX = EDGE_MARGIN;
        int leftEnd = setX - 16;
        int leftAvail = leftEnd - leftX;
        int gap = 10;
        int minEach = 70;
        int eachW = (leftAvail - 2 * gap) / 3;
        eachW = std::clamp(eachW, minEach, 160);

        bool showAuto = true;
        if (leftAvail < 3 * minEach + 2 * gap) {
            eachW = std::max(50, (leftAvail - gap) / 2);
            showAuto = false;
        }

        std::string themeLabel;
        if (colorMode == "dark")        themeLabel = "亮色";
        else if (colorMode == "light")  themeLabel = "暗色";
        else                            themeLabel = "跟随系统";

        int themeX = leftX;
        int pauseX = themeX + eachW + gap;
        int autoX  = pauseX + eachW + gap;

        addBtn("main_theme", { themeX, by, eachW, 40 }, themeLabel,
               BtnStyle::Normal, [this]() {
            if (colorMode == "system") setColorMode("light");
            else if (colorMode == "light") setColorMode("dark");
            else setColorMode("system");
        });
        addBtn("main_pause", { pauseX, by, eachW, 40 },
               paused ? "恢复" : "暂停",
               BtnStyle::Normal, [this]() { togglePaused(); });
        if (showAuto) {
            int autostart = platform_get_launch_at_login();
            addBtn("main_auto", { autoX, by, eachW, 40 },
                   autostart ? "自启：开" : "自启：关",
                   BtnStyle::Normal, [this]() { toggleAutostart(); });
        }

        // 列表行
        RowLayout rl = calcRowLayout();
        SDL_Rect listClip = { 0, HEADER_H, g_winW, g_winH - HEADER_H - FOOTER_H };

        int y = HEADER_H - mainScroll.offset();
        for (int i = 0; i < (int)phrases.size(); ++i) {
            if (y + ROW_H < HEADER_H) { y += ROW_H; continue; }
            if (y > g_winH - FOOTER_H) break;

            // ★ 卡片入场偏移
            float enterP = (i < (int)cardEntryAnim.size())
                            ? cardEntryAnim[i] : 1.f;
            int enterOffset = (int)((1.f - enterP) * 26);

            int baseY = y + 18 + enterOffset;

            std::string hkLabel;
            if (recordingHotkey && recordingIndex == i) hkLabel = recordingText;
            else if (i < (int)hotkeys.size() && !hotkeys[i].empty()) hkLabel = hotkeys[i];
            else hkLabel = "点击设键";
            bool hkActive = (i < (int)hotkeys.size() && !hotkeys[i].empty());

            addBtn("main_hk_" + std::to_string(i),
                   { rl.hkX, baseY, rl.hkW, 34 }, hkLabel,
                   BtnStyle::Tag,
                   [this, i]() { startRecording(i); }, hkActive, &listClip);

            addBtn("main_del_" + std::to_string(i),
                   { rl.delX, baseY, rl.delW, 34 }, "删除",
                   BtnStyle::Danger,
                   [this, i]() { deletePhrase(i); }, false, &listClip);

            addBtn("main_copy_" + std::to_string(i),
                   { rl.copyX, baseY, rl.copyW, 34 }, "复制",
                   BtnStyle::Primary,
                   [this, i]() { copyPhrase(i); }, false, &listClip);

            y += ROW_H;
        }
    }

    // ==================== 设置页按钮 ====================
    void buildSettingsButtons() {
        int backW = 76;
        int backX = g_winW - EDGE_MARGIN - backW;
        if (backX < EDGE_MARGIN) backX = EDGE_MARGIN;
        addBtn("set_back", { backX, TITLEBAR_H + 22, backW, 36 }, "返回",
               BtnStyle::Normal, [this]() { switchPage(Page::Main); });

        addBtn("set_tab_hk", { 24, SETTINGS_TABBAR_Y, 110, 36 }, "快捷键",
               BtnStyle::Tag, [this]() { switchTab(Tab::Hotkeys); },
               tab == Tab::Hotkeys);
        addBtn("set_tab_pz", { 142, SETTINGS_TABBAR_Y, 110, 36 }, "个性化",
               BtnStyle::Tag, [this]() { switchTab(Tab::Personalize); },
               tab == Tab::Personalize);
        addBtn("set_tab_ab", { 260, SETTINGS_TABBAR_Y, 92, 36 }, "关于",
               BtnStyle::Tag, [this]() { switchTab(Tab::About); },
               tab == Tab::About);

        if      (tab == Tab::Hotkeys)    { tabIndicatorTargetX = 24;  tabIndicatorTargetW = 110; }
        else if (tab == Tab::Personalize){ tabIndicatorTargetX = 142; tabIndicatorTargetW = 110; }
        else                              { tabIndicatorTargetX = 260; tabIndicatorTargetW = 92;  }

        if (tab == Tab::Hotkeys)          buildHotkeysTab();
        else if (tab == Tab::Personalize) buildPersonalizeTab();
    }

    void buildHotkeysTab() {
        SDL_Rect clip = { 0, SETTINGS_CONTENT_TOP, g_winW,
                          g_winH - SETTINGS_CONTENT_TOP };

        int contentTop = SETTINGS_CONTENT_TOP + HK_HINT_H;
        int n = std::min((int)phrases.size(), MAX_HOTKEYS);
        int scroll = settingsScroll.offset();

        int hkW = 240;
        int hkRight = g_winW - EDGE_MARGIN;
        int hkX = hkRight - hkW;
        if (hkX < EDGE_MARGIN + 200) {
            hkX = EDGE_MARGIN + 200;
            hkW = hkRight - hkX;
            if (hkW < 100) hkW = 100;
        }

        for (int i = 0; i < n; ++i) {
            int rowY = contentTop + i * HK_ROW_H - scroll;
            std::string hkLabel;
            if (recordingHotkey && recordingIndex == i) hkLabel = recordingText;
            else if (i < (int)hotkeys.size() && !hotkeys[i].empty()) hkLabel = hotkeys[i];
            else hkLabel = "点击设置热键";

            addBtn("set_hk_" + std::to_string(i),
                   { hkX, rowY + 12, hkW, 40 }, hkLabel,
                   BtnStyle::Tag,
                   [this, i]() { startRecording(i); },
                   (i < (int)hotkeys.size() && !hotkeys[i].empty()),
                   &clip);
        }
    }

    void buildPersonalizeTab() {
        SDL_Rect clip = { 0, SETTINGS_CONTENT_TOP, g_winW,
                          g_winH - SETTINGS_CONTENT_TOP };

        int ox = 32;
        int scroll = settingsScroll.offset();
        int baseY = SETTINGS_CONTENT_TOP - scroll;

        {
            int y = baseY + PZ_MODE_TOP;
            addBtn("pz_m1", { ox + 140, y + 26, 90, 38 }, "亮色",
                   BtnStyle::Tag, [this]() { setColorMode("light"); },
                   colorMode == "light", &clip);
            addBtn("pz_m2", { ox + 238, y + 26, 90, 38 }, "暗色",
                   BtnStyle::Tag, [this]() { setColorMode("dark"); },
                   colorMode == "dark", &clip);
            addBtn("pz_m3", { ox + 336, y + 26, 130, 38 }, "跟随系统",
                   BtnStyle::Tag, [this]() { setColorMode("system"); },
                   colorMode == "system", &clip);
        }
        {
            int y = baseY + PZ_THEME_TOP;
            constexpr int CELL = 72;
            constexpr int SWATCH = 64;
            for (int i = 0; i < THEME_COUNT; ++i) {
                int row = i / 5, col = i % 5;
                int sx = ox + 140 + col * CELL;
                int sy = y + 26 + row * CELL;
                addSwatch("pz_th_" + std::to_string(i),
                          { sx, sy, SWATCH, SWATCH }, THEMES[i].normal,
                          themeName == THEMES[i].name,
                          [this, i]() { setThemeColor(THEMES[i].name); },
                          &clip);
            }
        }
        {
            int y = baseY + PZ_FAMILY_TOP;
            std::string fontLabel = fonts[currentFont].display;
            addBtn("pz_family", { ox + 140, y + 26, 220, 38 }, fontLabel,
                   BtnStyle::Tag,
                   [this]() { openFontPicker(); },
                   false, &clip, 1);
        }
        {
            int y = baseY + PZ_FONT_TOP;
            addBtn("pz_fs_m", { ox + 140, y + 26, 40, 38 }, "",
                   BtnStyle::Tag, [this]() { setFontSize(fontSize - 1); },
                   false, &clip, 0, 1);
            addBtn("pz_fs_v", { ox + 188, y + 26, 70, 38 },
                   std::to_string(fontSize), BtnStyle::Tag, [](){}, false, &clip);
            addBtn("pz_fs_p", { ox + 266, y + 26, 40, 38 }, "",
                   BtnStyle::Tag, [this]() { setFontSize(fontSize + 1); },
                   false, &clip, 0, 2);
        }
        {
            int y = baseY + PZ_STYLE_TOP;
            addBtn("pz_bold", { ox + 140, y + 26, 90, 38 }, "粗体",
                   BtnStyle::Tag, [this]() { toggleBold(); }, fontBold, &clip);
            addBtn("pz_italic", { ox + 238, y + 26, 90, 38 }, "斜体",
                   BtnStyle::Tag, [this]() { toggleItalic(); }, fontItalic, &clip);
        }
        {
            int y = baseY + PZ_AE_TOP;
            const char* effects[] = {"无", "淡入", "淡入滑动", "缩放"};
            for (int i = 0; i < 4; ++i) {
                addBtn("pz_ae_" + std::to_string(i),
                       { ox + 140 + i * 86, y + 26, 80, 38 }, effects[i],
                       BtnStyle::Tag,
                       [this, i]() {
                           const char* e[] = {"无","淡入","淡入滑动","缩放"};
                           setAnimEffect(e[i]);
                       },
                       animCfg.effect == effects[i], &clip);
            }
        }
        {
            int y = baseY + PZ_AS_TOP;
            const char* speeds[] = {"关闭", "慢", "正常", "快"};
            for (int i = 0; i < 4; ++i) {
                addBtn("pz_as_" + std::to_string(i),
                       { ox + 140 + i * 82, y + 26, 76, 38 }, speeds[i],
                       BtnStyle::Tag,
                       [this, i]() {
                           const char* s[] = {"关闭","慢","正常","快"};
                           setAnimSpeed(s[i]);
                       },
                       animCfg.speed == speeds[i], &clip);
            }
        }
        {
            int y = baseY + PZ_FPS_TOP;
            const char* fpsList[] = {"30", "60", "120"};
            for (int i = 0; i < 3; ++i) {
                addBtn("pz_fps_" + std::to_string(i),
                       { ox + 140 + i * 82, y + 26, 76, 38 }, fpsList[i],
                       BtnStyle::Tag,
                       [this, i]() {
                           const char* f[] = {"30","60","120"};
                           setAnimFps(f[i]);
                       },
                       animCfg.fps == fpsList[i], &clip);
            }
        }
        {
            int y = baseY + PZ_DP_TOP;
            const char* tabs[] = {"快捷键", "个性化", "关于"};
            for (int i = 0; i < 3; ++i) {
                addBtn("pz_dp_" + std::to_string(i),
                       { ox + 140 + i * 96, y + 26, 90, 38 }, tabs[i],
                       BtnStyle::Tag,
                       [this, i]() {
                           const char* t[] = {"快捷键","个性化","关于"};
                           setDefaultPage(t[i]);
                       },
                       defaultPage == tabs[i], &clip);
            }
        }
    }

    // ==================== 交通灯 ====================
    void renderTrafficLights() {
        btnClose = { 20, TITLEBAR_H / 2 - 6, 12, 12 };
        btnMin   = { 40, TITLEBAR_H / 2 - 6, 12, 12 };
        btnMax   = { 60, TITLEBAR_H / 2 - 6, 12, 12 };

        bool hClose = pointIn(btnClose, mouseX, mouseY);
        bool hMin   = pointIn(btnMin,   mouseX, mouseY);
        bool hMax   = pointIn(btnMax,   mouseX, mouseY);

        closeHover = smoothTo(closeHover, hClose ? 1.f : 0.f, 22.f, g_dt);
        minHover   = smoothTo(minHover,   hMin   ? 1.f : 0.f, 22.f, g_dt);
        maxHover   = smoothTo(maxHover,   hMax   ? 1.f : 0.f, 22.f, g_dt);

        auto drawDot = [&](SDL_Rect rc, SDL_Color base, float hov, int icon) {
            // ★ 悬停时轻微放大
            int grow = (int)(hov * 1.5f);
            SDL_Rect rcS = { rc.x - grow, rc.y - grow, rc.w + grow*2, rc.h + grow*2 };

            SDL_Color c = base;
            if (hov > 0.05f) c = adjustBright(base, (int)(18 * hov));
            SDL_Texture* t = getCircleTex(rcS.w, c);
            if (t) { SDL_Rect dst = rcS; SDL_RenderCopy(renderer, t, nullptr, &dst); }

            if (hov > 0.5f && icon != 0) {
                SDL_Color ic = {60, 40, 40, (Uint8)(200 * hov)};
                int cx = rcS.x + rcS.w / 2;
                int cy = rcS.y + rcS.h / 2;
                if (icon == 1) drawCross(renderer, cx, cy, 8, 2, ic);
                else if (icon == 2) drawMinus(renderer, cx, cy, 7, 2, ic);
                else if (icon == 3) drawPlus(renderer, cx, cy, 7, 2, ic);
            }
        };

        drawDot(btnClose, {255, 95, 86, 255}, closeHover, 1);
        drawDot(btnMin,   {255, 189, 46, 255}, minHover,   2);
        drawDot(btnMax,   {39, 201, 63, 255},  maxHover,   3);
    }

    // ==================== 渲染主页面 ====================
    void renderMain() {
        // ★ 标题入场：从上方滑入 + 淡入
        float titleEnter = easeOutCubic(clamp01(pageEnterTimer / 0.35f));
        int titleOffset = (int)((1.f - titleEnter) * 16);

        drawText(renderer, fontHuge, "快捷短语",
                 24, TITLEBAR_H + 22 - titleOffset, pal.text);
        drawText(renderer, fontSmall,
            "点击热键徽章设置组合键 · 全局粘贴对应的短语",
            24, TITLEBAR_H + 60 - titleOffset, pal.muted);

        // 输入框入场（稍晚一点）
        float inputEnter = easeOutCubic(clamp01((pageEnterTimer - 0.05f) / 0.35f));
        int inputOffset = (int)((1.f - inputEnter) * 20);

        SDL_Rect ir = inputRect;
        ir.y += inputOffset;

        if (inputFocusAnim > 0.01f) {
            SDL_Color glow = pal.primary;
            glow.a = (Uint8)(60 * inputFocusAnim);
            SDL_Rect gr = { ir.x - 3, ir.y - 3, ir.w + 6, ir.h + 6 };
            SDL_Texture* gt = getRoundRectTex(gr.w, gr.h, 12, glow);
            if (gt) { SDL_Rect dst = gr; SDL_RenderCopy(renderer, gt, nullptr, &dst); }
        }

        SDL_Texture* inFt = getRoundRectTex(ir.w, ir.h, 10, pal.card);
        if (inFt) { SDL_Rect dst = ir; SDL_RenderCopy(renderer, inFt, nullptr, &dst); }

        SDL_Rect inHL = { ir.x + 4, ir.y + 1, ir.w - 8, 1 };
        fillRect(renderer, inHL, withAlpha(pal.glassHighlight, 60));

        SDL_Color bc = lerpColor(pal.glassBorder, pal.primary, inputFocusAnim);
        SDL_Texture* inBt = getRoundRectTex(ir.w, ir.h, 10, {0,0,0,0}, 1, bc);
        if (inBt) { SDL_Rect dst = ir; SDL_RenderCopy(renderer, inBt, nullptr, &dst); }

        std::string shown = inputBuf;
        SDL_Color inColor = pal.text;
        if (!inputFocused && inputBuf.empty()) {
            shown = "输入新短语…";
            inColor = pal.subtle;
        }
        drawText(renderer, fontNormal, shown, ir.x + 14, ir.y + 9, inColor);

        // 列表
        int clipTop = HEADER_H;
        int clipH = g_winH - HEADER_H - FOOTER_H;
        SDL_Rect clip = { 0, clipTop, g_winW, clipH };
        SDL_RenderSetClipRect(renderer, &clip);

        RowLayout rl = calcRowLayout();

        int y = HEADER_H - mainScroll.offset();
        for (int i = 0; i < (int)phrases.size(); ++i) {
            if (y + ROW_H < clipTop) { y += ROW_H; continue; }
            if (y > clipTop + clipH) break;

            float enterP = (i < (int)cardEntryAnim.size())
                            ? cardEntryAnim[i] : 1.f;
            int enterOffset = (int)((1.f - enterP) * 26);

            SDL_Rect card = { EDGE_MARGIN, y + enterOffset,
                              g_winW - EDGE_MARGIN * 2, ROW_H - 12 };

            bool cardHover = pointIn(card, mouseX, mouseY);
            std::string cardId = "card_" + std::to_string(i);
            AnimState& cs = hoverAnim[cardId];
            float cTarget = cardHover ? 1.f : 0.f;
            if (animCfg.enabled()) {
                cs.value = smoothTo(cs.value, cTarget, animCfg.hoverRate(), g_dt);
                if (std::fabs(cs.value - cTarget) < 0.005f) cs.value = cTarget;
            } else cs.value = cTarget;

            drawGlassCard(renderer, card, 12, pal, cs.value);

            drawText(renderer, fontSmall, std::to_string(i + 1),
                     card.x + 18, card.y + 22, pal.subtle);

            std::string t = phrases[i];
            int approxCharW = std::max(6, fontSize);
            if ((int)t.size() > rl.textMaxW / approxCharW)
                t = t.substr(0, std::max(4, rl.textMaxW / approxCharW)) + "…";
            drawText(renderer, fontNormal, t, rl.textX, card.y + 20, pal.text);

            y += ROW_H;
        }
        SDL_RenderSetClipRect(renderer, nullptr);

        if (mainScroll.maxScroll > 1.f) {
            float ratio = (float)clipH / (float)(clipH + mainScroll.maxScroll);
            int barH = std::max(30, (int)(clipH * ratio));
            float prog = mainScroll.pos / mainScroll.maxScroll;
            int barY = clipTop + (int)(prog * (clipH - barH));
            SDL_Rect bar = { g_winW - 8, barY, 4, barH };
            fillRoundRect(renderer, bar, 2, withAlpha(pal.glassBorder, 160));
        }
    }

    // ==================== 渲染设置页 ====================
    void renderSettings() {
        float enterP = easeOutCubic(clamp01(pageEnterTimer / 0.35f));
        int enterOffset = (int)((1.f - enterP) * 18);

        drawText(renderer, fontBig, "设置", 24, TITLEBAR_H + 22 - enterOffset, pal.text);
        drawText(renderer, fontSmall, "个性化 QuickPhrase",
                 24, TITLEBAR_H + 56 - enterOffset, pal.muted);

        SDL_SetRenderDrawColor(renderer, pal.glassBorder.r, pal.glassBorder.g,
                                pal.glassBorder.b, 120);
        SDL_RenderDrawLine(renderer, 24, TITLEBAR_H + 118, g_winW - 24, TITLEBAR_H + 118);

        // ★ Tab 指示条弹簧
        {
            SDL_Rect ind = { (int)tabIndicatorX, SETTINGS_TABBAR_Y + 40,
                             (int)tabIndicatorW, 3 };
            fillRoundRect(renderer, ind, 2, pal.primary);
        }

        if (tab == Tab::Hotkeys)          renderHotkeysTab();
        else if (tab == Tab::Personalize) renderPersonalizeTab();
        else                              renderAboutTab();
    }

    void renderHotkeysTab() {
        int clipTop = SETTINGS_CONTENT_TOP;
        SDL_Rect clip = { 0, clipTop, g_winW, g_winH - SETTINGS_CONTENT_TOP };
        SDL_RenderSetClipRect(renderer, &clip);

        drawText(renderer, fontSmall,
                 "点击右侧热键框，按下任意组合键即可绑定；Backspace 或 Esc 清空。",
                 24, clipTop + 20, pal.muted);

        int contentTop = clipTop + HK_HINT_H;
        int n = std::min((int)phrases.size(), MAX_HOTKEYS);
        int scroll = settingsScroll.offset();

        if (n == 0) {
            drawTextCenter(renderer, fontNormal,
                           "还没有短语。请先在主页面添加。",
                           g_winW / 2, contentTop + 100, pal.muted);
        }

        for (int i = 0; i < n; ++i) {
            int rowY = contentTop + i * HK_ROW_H - scroll;

            SDL_Rect card = { EDGE_MARGIN, rowY, g_winW - EDGE_MARGIN * 2, 52 };
            drawGlassCard(renderer, card, 12, pal, 0.f);

            drawText(renderer, fontSmall, std::to_string(i + 1),
                     card.x + 18, card.y + 18, pal.subtle);

            std::string preview = phrases[i];
            if (preview.size() > 30) preview = preview.substr(0, 30) + "…";
            drawText(renderer, fontNormal, preview,
                     card.x + 52, card.y + 15, pal.text);
        }
        SDL_RenderSetClipRect(renderer, nullptr);
    }

    void renderPersonalizeTab() {
        int clipTop = SETTINGS_CONTENT_TOP;
        SDL_Rect clip = { 0, clipTop, g_winW, g_winH - SETTINGS_CONTENT_TOP };
        SDL_RenderSetClipRect(renderer, &clip);

        int ox = 32;
        int scroll = settingsScroll.offset();
        int baseY = clipTop - scroll;

        drawText(renderer, fontNormal, "颜色模式", ox, baseY + PZ_MODE_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "主题色",   ox, baseY + PZ_THEME_TOP + 20, pal.text);
        drawText(renderer, fontNormal, "字体家族", ox, baseY + PZ_FAMILY_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "字号",     ox, baseY + PZ_FONT_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "字体样式", ox, baseY + PZ_STYLE_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "动画效果", ox, baseY + PZ_AE_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "动画速度", ox, baseY + PZ_AS_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "帧率",     ox, baseY + PZ_FPS_TOP + 34, pal.text);
        drawText(renderer, fontNormal, "默认页面", ox, baseY + PZ_DP_TOP + 34, pal.text);

        SDL_RenderSetClipRect(renderer, nullptr);
    }

    void renderAboutTab() {
        int cy = g_winH / 2 - 30;
        float enterP = easeOutCubic(clamp01((pageEnterTimer - 0.05f) / 0.4f));
        int enterOffset = (int)((1.f - enterP) * 22);

        drawTextCenter(renderer, fontHuge, "快捷短语 QuickPhrase",
                       g_winW / 2, cy - 80 + enterOffset, pal.text);

        const char* lines[] = {
            "Liquid Glass Edition",
            "本软件由 DeepSeek 开发",
            "作者 —— 雲-绮石Stone",
            "",
            "禁止商业用途",
            "个人二次使用源码等无需署名",
        };
        int y = cy + 10 + enterOffset;
        for (auto* line : lines) {
            drawTextCenter(renderer, fontNormal, line, g_winW / 2, y, pal.muted);
            y += 30;
        }

        drawTextCenter(renderer, fontSmall, "SDL2 · C++ · macOS 26 Liquid Glass",
                       g_winW / 2, g_winH - 60, pal.subtle);
    }

    // ==================== 字体选择器 ====================
    void renderFontPicker() {
        SDL_Color mask = {0, 0, 0, 100};
        fillRect(renderer, { 0, 0, g_winW, g_winH }, mask);

        int panelW = std::min(560, g_winW - 60);
        int panelH = std::min(640, g_winH - 80);
        if (panelW < 200) panelW = 200;
        if (panelH < 200) panelH = 200;
        int panelX = (g_winW - panelW) / 2;
        int panelY = (g_winH - panelH) / 2;
        if (panelX < 20) panelX = 20;
        if (panelY < 20) panelY = 20;
        fontPickerPanel = { panelX, panelY, panelW, panelH };

        fillRoundRect(renderer, { panelX + 2, panelY + 4, panelW, panelH },
                      18, {0, 0, 0, 100});

        SDL_Color panelFill = pal.card;
        panelFill.a = (Uint8)std::clamp(panelFill.a + 40, 0, 255);
        SDL_Texture* pt = getRoundRectTex(panelW, panelH, 18, panelFill);
        if (pt) { SDL_Rect dst = fontPickerPanel; SDL_RenderCopy(renderer, pt, nullptr, &dst); }

        SDL_Texture* pb = getRoundRectTex(panelW, panelH, 18, {0,0,0,0}, 1, pal.glassBorder);
        if (pb) { SDL_Rect dst = fontPickerPanel; SDL_RenderCopy(renderer, pb, nullptr, &dst); }

        drawText(renderer, fontBig, "选择字体", panelX + 28, panelY + 22, pal.text);

        {
            int bw = 32, bh = 32;
            int bx = panelX + panelW - bw - 20;
            int by = panelY + 20;
            fontPickerCloseBtn = { bx, by, bw, bh };

            bool hov = pointIn(fontPickerCloseBtn, mouseX, mouseY);
            SDL_Color bg = hov ? pal.tagHover : pal.tagBg;
            fillRoundRect(renderer, fontPickerCloseBtn, 8, bg);
            SDL_Texture* bt = getRoundRectTex(bw, bh, 8, {0,0,0,0}, 1, pal.glassBorder);
            if (bt) { SDL_Rect dst = fontPickerCloseBtn; SDL_RenderCopy(renderer, bt, nullptr, &dst); }

            drawCross(renderer, bx + bw/2, by + bh/2, 16, 2,
                      hov ? pal.text : pal.muted);
        }

        int listTop = panelY + 76;
        int listH = panelH - 96;
        SDL_Rect listClip = { panelX + 12, listTop, panelW - 24, listH };
        SDL_RenderSetClipRect(renderer, &listClip);

        int itemH = 72;
        int n = (int)fonts.size();
        int contentH = n * itemH;
        fontPickerScroll.maxScroll = std::max(0.f, (float)(contentH - listH));

        fontPickerHits.clear();

        int startY = listTop - fontPickerScroll.offset();
        for (int i = 0; i < n; ++i) {
            int itemY = startY + i * itemH;
            if (itemY + itemH < listTop) continue;
            if (itemY > listTop + listH) break;

            SDL_Rect itemRect = { panelX + 16, itemY + 4, panelW - 32, itemH - 8 };
            fontPickerHits.push_back(std::make_pair(itemRect, i));

            bool hov = pointIn(itemRect, mouseX, mouseY);
            bool selected = (i == currentFont);

            SDL_Color bgColor;
            if (selected) bgColor = lerpColor(pal.tagBg, pal.primary, 0.15f);
            else          bgColor = hov ? pal.tagHover : pal.tagBg;

            fillRoundRect(renderer, itemRect, 10, bgColor);

            SDL_Color bdColor = selected ? pal.primary : pal.glassBorder;
            SDL_Texture* bt = getRoundRectTex(itemRect.w, itemRect.h, 10,
                                               {0,0,0,0}, selected ? 2 : 1, bdColor);
            if (bt) { SDL_Rect dst = itemRect; SDL_RenderCopy(renderer, bt, nullptr, &dst); }

            drawText(renderer, fontNormal, fonts[i].display,
                     itemRect.x + 20, itemRect.y + 12, pal.text);

            TTF_Font* pf = getFontPreview(i);
            std::string preview = "快捷短语 Aa 123";
            if (pf) {
                int pw = 0, ph = 0;
                SDL_Color pc = selected ? pal.primary : pal.muted;
                SDL_Texture* pt2 = makeText(renderer, pf, preview, pc, pw, ph);
                if (pt2) {
                    int tx = itemRect.x + itemRect.w - pw - 44;
                    int ty = itemRect.y + (itemRect.h - ph) / 2;
                    SDL_Rect dst = { tx, ty, pw, ph };
                    SDL_RenderCopy(renderer, pt2, nullptr, &dst);
                    SDL_DestroyTexture(pt2);
                }
            }

            if (selected) {
                drawCheckmark(renderer, itemRect.x + itemRect.w - 20,
                              itemRect.y + itemRect.h / 2, 16, 2, pal.primary);
            }
        }

        SDL_RenderSetClipRect(renderer, nullptr);

        if (fontPickerScroll.maxScroll > 1.f) {
            float ratio = (float)listH / (float)(listH + fontPickerScroll.maxScroll);
            int barH = std::max(30, (int)(listH * ratio));
            float prog = fontPickerScroll.pos / fontPickerScroll.maxScroll;
            int barY = listTop + (int)(prog * (listH - barH));
            SDL_Rect bar = { panelX + panelW - 8, barY, 4, barH };
            fillRoundRect(renderer, bar, 2, withAlpha(pal.glassBorder, 160));
        }
    }

    // ==================== 页面切换 ====================
    void switchPage(Page p) {
        if (p == page) return;
        page = p;
        pageEnterTimer = 0.f;   // ★ 重置入场动画
        if (animCfg.enabled()) {
            pageTransitionY = animCfg.pageOffsetDist();
            pageAlpha = (animCfg.effect == "无") ? 1.f : 0.f;
        } else {
            pageTransitionY = 0.f;
            pageAlpha = 1.f;
        }
        if (page == Page::Settings) settingsScroll.reset();
    }
    void switchTab(Tab t) {
        if (t == tab) return;
        tab = t;
        pageEnterTimer = 0.f;
        if (animCfg.enabled()) {
            pageTransitionY = animCfg.pageOffsetDist();
            pageAlpha = (animCfg.effect == "无") ? 1.f : 0.f;
        } else {
            pageTransitionY = 0.f;
            pageAlpha = 1.f;
        }
        settingsScroll.reset();
    }

    // ==================== 主渲染 ====================
    void render() {
        buttons.clear();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        fillRect(renderer, { 0, 0, g_winW, g_winH }, pal.bg);

        renderTrafficLights();

        SDL_SetRenderDrawColor(renderer, pal.glassBorder.r, pal.glassBorder.g,
                                pal.glassBorder.b, 60);
        SDL_RenderDrawLine(renderer, 0, TITLEBAR_H - 1, g_winW, TITLEBAR_H - 1);

        if (page == Page::Main) {
            buildMainButtons();
            renderMain();
        } else {
            buildSettingsButtons();
            renderSettings();
        }

        for (auto& b : buttons) renderButton(b);

        // ★ 状态提示：弹跳入场
        if (statusAnim > 0.01f) {
            int w, h;
            SDL_Color stColor = pal.primary;
            stColor.a = (Uint8)(255 * statusAnim);
            SDL_Texture* st = makeText(renderer, fontNormal, statusMsg, stColor, w, h);
            if (st) {
                Uint8 alpha = (Uint8)std::clamp(255.f * statusAnim, 0.f, 255.f);
                // 入场用 elastic，出场用 easeOut
                float enterT = clamp01((SDL_GetTicks() - statusStart) / 400.f);
                float easeT = (statusActive && SDL_GetTicks() < statusEnd)
                              ? easeOutElastic(enterT)
                              : statusAnim;
                int yOffset = (int)((1.f - easeT) * 40);

                int boxW = w + 36, boxH = 38;
                int bx = g_winW / 2 - boxW / 2;
                int by = g_winH - 50 + yOffset;

                SDL_Color sh = pal.shadow; sh.a = (Uint8)(sh.a * statusAnim);
                SDL_Color bg = pal.card;   bg.a = alpha;
                SDL_Color bd = pal.primary; bd.a = alpha;

                fillRoundRect(renderer, { bx, by + 1, boxW, boxH }, 10, sh);
                SDL_Texture* bgT = getRoundRectTex(boxW, boxH, 10, bg);
                if (bgT) { SDL_Rect dst = { bx, by, boxW, boxH };
                            SDL_RenderCopy(renderer, bgT, nullptr, &dst); }

                SDL_Texture* bt = getRoundRectTex(boxW, boxH, 10, {0,0,0,0}, 1, bd);
                if (bt) {
                    SDL_SetTextureAlphaMod(bt, alpha);
                    SDL_Rect dst = { bx, by, boxW, boxH };
                    SDL_RenderCopy(renderer, bt, nullptr, &dst);
                    SDL_SetTextureAlphaMod(bt, 255);
                }

                SDL_SetTextureAlphaMod(st, alpha);
                SDL_Rect dst = { bx + 18, by + (boxH - h) / 2, w, h };
                SDL_RenderCopy(renderer, st, nullptr, &dst);
                SDL_SetTextureAlphaMod(st, 255);
                SDL_DestroyTexture(st);
            }
        }

        if (fontPickerOpen) renderFontPicker();

        SDL_RenderPresent(renderer);
    }

    // ==================== 动画更新 ====================
    void updateAnimations() {
        float rate = animCfg.rate();

        // 主题色：带脉冲
        themeAnim.update(rate * 0.9f, g_dt);
        SDL_Color disp = themeAnim.displayColor();
        pal.primary       = disp;
        pal.primaryHover  = adjustBright(disp, 35);
        pal.primaryActive = adjustBright(disp, -25);

        // 页面过渡
        pageTransitionY = smoothTo(pageTransitionY, 0.f, rate, g_dt);
        if (std::fabs(pageTransitionY) < 0.3f) pageTransitionY = 0.f;

        if (animCfg.effect == "淡入" || animCfg.effect == "淡入滑动") {
            pageAlpha = smoothTo(pageAlpha, 1.f, rate * 1.2f, g_dt);
            if (pageAlpha > 0.99f) pageAlpha = 1.f;
        } else pageAlpha = 1.f;

        // 输入框焦点
        float focusTarget = inputFocused ? 1.f : 0.f;
        inputFocusAnim = smoothTo(inputFocusAnim, focusTarget, animCfg.hoverRate(), g_dt);
        if (std::fabs(inputFocusAnim - focusTarget) < 0.005f) inputFocusAnim = focusTarget;

        // ★ Tab 指示条弹簧（带轻微过冲）
        springTo(tabIndicatorX, tabIndicatorXVel, tabIndicatorTargetX,
                 animCfg.springStiffness() * 1.1f, animCfg.springDamping(), g_dt);
        springTo(tabIndicatorW, tabIndicatorWVel, tabIndicatorTargetW,
                 animCfg.springStiffness() * 1.1f, animCfg.springDamping(), g_dt);

        // 状态提示
        if (statusActive) {
            Uint32 now = SDL_GetTicks();
            float target;
            if (now < statusEnd)            target = 1.f;
            else if (now < statusEnd + 300) target = 0.f;
            else { statusActive = false; statusAnim = 0.f; target = 0.f; }

            if (statusActive) {
                if (animCfg.enabled()) {
                    statusAnim = smoothTo(statusAnim, target, rate * 1.6f, g_dt);
                    if (std::fabs(statusAnim - target) < 0.01f) statusAnim = target;
                } else statusAnim = target;
            }
        }

        // ★ 页面入场计时
        pageEnterTimer += g_dt;

        // ★ 卡片入场动画（每张卡有 45ms 延迟）
        while (cardEntryAnim.size() < phrases.size()) cardEntryAnim.push_back(0.f);
        if (cardEntryAnim.size() > phrases.size())
            cardEntryAnim.resize(phrases.size());

        for (size_t i = 0; i < cardEntryAnim.size(); ++i) {
            float delay = (float)i * 0.045f;
            float t = (pageEnterTimer - delay) / 0.42f;
            float target = easeOutCubic(clamp01(t));
            if (cardEntryAnim[i] < target) {
                cardEntryAnim[i] = target;
            } else if (cardEntryAnim[i] > target) {
                cardEntryAnim[i] = target;
            }
        }

        int reboundDelay = animCfg.reboundDelay();
        float reboundRate = animCfg.reboundRate();

        updateScrollLimits();
        mainScroll.update(rate, reboundDelay, reboundRate, g_dt);
        settingsScroll.update(rate, reboundDelay, reboundRate, g_dt);
        fontPickerScroll.update(rate, reboundDelay, reboundRate, g_dt);

        if (hoverAnim.size() > 300) hoverAnim.clear();
        if (pressAnim.size() > 300) pressAnim.clear();
    }

    // ==================== 交互 ====================
    void handleMouseDown(int mx, int my) {
        ResizeEdge edge = detectEdge(mx, my);
        if (edge != ResizeEdge::None) {
            startResize(mx, my, edge);
            return;
        }

        if (pointIn(btnClose, mx, my) ||
            pointIn(btnMin, mx, my) ||
            pointIn(btnMax, mx, my)) return;

        if (my < TITLEBAR_H) {
            dragging = true;
            int gx, gy;
            SDL_GetGlobalMouseState(&gx, &gy);
            int wx, wy;
            SDL_GetWindowPosition(window, &wx, &wy);
            dragOffsetX = gx - wx;
            dragOffsetY = gy - wy;
            return;
        }

        if (fontPickerOpen) {
            if (pointIn(fontPickerCloseBtn, mx, my)) return;
            for (auto& pr : fontPickerHits) {
                if (pointIn(pr.first, mx, my)) {
                    pressedId = "font_item_" + std::to_string(pr.second);
                    return;
                }
            }
            if (pointIn(fontPickerPanel, mx, my)) return;
            closeFontPicker();
            return;
        }

        if (page == Page::Main && pointIn(inputRect, mx, my)) {
            inputFocused = true;
            return;
        }
        inputFocused = false;

        for (int i = (int)buttons.size() - 1; i >= 0; --i) {
            if (buttons[i].clip && !pointIn(buttons[i].clipRect, mx, my)) continue;
            if (pointIn(buttons[i].rect, mx, my)) {
                pressedId = buttons[i].id;
                return;
            }
        }
    }

    void handleMouseUp(int mx, int my) {
        if (resizing) { resizing = false; resizeEdge = ResizeEdge::None; return; }
        if (dragging) { dragging = false; return; }

        if (pointIn(btnClose, mx, my)) { running = false; return; }
        if (pointIn(btnMin, mx, my))   {
            SDL_HideWindow(window);
            hiddenToTray = true;
            return;
        }

        if (fontPickerOpen) {
            if (pointIn(fontPickerCloseBtn, mx, my)) {
                closeFontPicker();
                pressedId.clear();
                return;
            }
            for (auto& pr : fontPickerHits) {
                if (pointIn(pr.first, mx, my)) {
                    std::string expected = "font_item_" + std::to_string(pr.second);
                    if (pressedId == expected) {
                        selectFont(pr.second);
                        closeFontPicker();
                    }
                    pressedId.clear();
                    return;
                }
            }
            pressedId.clear();
            return;
        }

        for (int i = (int)buttons.size() - 1; i >= 0; --i) {
            if (buttons[i].clip && !pointIn(buttons[i].clipRect, mx, my)) continue;
            if (pointIn(buttons[i].rect, mx, my)) {
                if (!pressedId.empty() && buttons[i].id == pressedId &&
                    buttons[i].onClick) {
                    buttons[i].onClick();
                }
                break;
            }
        }
        pressedId.clear();
    }

    void handleMotion(int mx, int my) {
        mouseX = mx; mouseY = my;

        if (resizing) { updateResize(mx, my); return; }

        if (dragging) {
            int gx, gy;
            SDL_GetGlobalMouseState(&gx, &gy);
            SDL_SetWindowPosition(window, gx - dragOffsetX, gy - dragOffsetY);
            return;
        }

        ResizeEdge edge = detectEdge(mx, my);
        SDL_SystemCursor cursorType = SDL_SYSTEM_CURSOR_ARROW;
        switch (edge) {
            case ResizeEdge::TopLeft:
            case ResizeEdge::BottomRight:
                cursorType = SDL_SYSTEM_CURSOR_SIZENWSE; break;
            case ResizeEdge::TopRight:
            case ResizeEdge::BottomLeft:
                cursorType = SDL_SYSTEM_CURSOR_SIZENESW; break;
            case ResizeEdge::Left:
            case ResizeEdge::Right:
                cursorType = SDL_SYSTEM_CURSOR_SIZEWE; break;
            case ResizeEdge::Top:
            case ResizeEdge::Bottom:
                cursorType = SDL_SYSTEM_CURSOR_SIZENS; break;
            case ResizeEdge::None: {
                bool over = pointIn(btnClose, mx, my) ||
                            pointIn(btnMin, mx, my) ||
                            pointIn(btnMax, mx, my);
                if (!over && fontPickerOpen) {
                    if (pointIn(fontPickerCloseBtn, mx, my)) over = true;
                    for (auto& pr : fontPickerHits)
                        if (pointIn(pr.first, mx, my)) { over = true; break; }
                }
                if (!over) {
                    for (auto& b : buttons) {
                        if (b.clip && !pointIn(b.clipRect, mx, my)) continue;
                        if (pointIn(b.rect, mx, my)) { over = true; break; }
                    }
                }
                if (over) cursorType = SDL_SYSTEM_CURSOR_HAND;
                break;
            }
        }
        setCursorType(cursorType);
    }

    void handleText(const std::string& t) {
        if (fontPickerOpen) return;
        if (page == Page::Main && inputFocused) inputBuf += t;
    }

    void handleKey(SDL_Keysym ks) {
        if (fontPickerOpen) {
            if (ks.sym == SDLK_ESCAPE) closeFontPicker();
            return;
        }
        if (recordingHotkey) { handleHotkeyRecord(ks); return; }
        if (page == Page::Main && inputFocused) {
            if (ks.sym == SDLK_BACKSPACE && !inputBuf.empty()) {
                size_t i = inputBuf.size() - 1;
                while (i > 0 && (inputBuf[i] & 0xC0) == 0x80) --i;
                inputBuf.erase(i);
            } else if (ks.sym == SDLK_RETURN || ks.sym == SDLK_KP_ENTER) {
                addPhrase();
            } else if (ks.sym == SDLK_ESCAPE) {
                inputFocused = false;
            }
        }
    }

    void handleWheel(int dy) {
        float delta = -dy * 42.f;
        if (fontPickerOpen) {
            fontPickerScroll.wheel(delta);
            return;
        }
        if (page == Page::Main) mainScroll.wheel(delta);
        else                    settingsScroll.wheel(delta);
    }

    // ==================== 主循环 ====================
    int run() {
        if (!init()) return 1;

        Uint32 lastFrameTime = SDL_GetTicks();

        SDL_Event e;
        while (running) {
            Uint32 frameStart = SDL_GetTicks();

            // ★ 计算 dt（帧率无关）
            float rawDt = (frameStart - lastFrameTime) / 1000.0f;
            lastFrameTime = frameStart;
            g_dt = std::clamp(rawDt, 1.f / 240.f, 1.f / 30.f);

            int idx;
            while ((idx = platform_pop_hotkey()) >= 0) {
                if (!paused) pastePhrase(idx);
            }

            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                    case SDL_QUIT: running = false; break;
                    case SDL_WINDOWEVENT:
                        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                            e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
                            updateWindowMetrics();
                        break;
                    case SDL_MOUSEBUTTONDOWN:
                        if (e.button.button == SDL_BUTTON_LEFT) {
                            mouseDown = true;
                            handleMouseDown(e.button.x, e.button.y);
                        }
                        break;
                    case SDL_MOUSEBUTTONUP:
                        if (e.button.button == SDL_BUTTON_LEFT) {
                            handleMouseUp(e.button.x, e.button.y);
                            mouseDown = false;
                        }
                        break;
                    case SDL_MOUSEMOTION:
                        handleMotion(e.motion.x, e.motion.y);
                        break;
                    case SDL_MOUSEWHEEL:
                        handleWheel(e.wheel.y);
                        break;
                    case SDL_TEXTINPUT:
                        handleText(e.text.text);
                        break;
                    case SDL_KEYDOWN:
                        handleKey(e.key.keysym);
                        break;
                }
            }

            updateAnimations();
            render();

            Uint32 now = SDL_GetTicks();
            int used = (int)(now - frameStart);
            int delay = animCfg.frameDelay() - used;
            if (delay > 0) SDL_Delay(delay);
        }
        shutdown();
        return 0;
    }
};

// ==================== 入口 ====================
App* g_app = nullptr;

int main() {
    App app;
    g_app = &app;
    int rc = app.run();
    g_app = nullptr;
    return rc;
}