// Blivzy AI v0.7 - Made by Blivzy
// One universal offline chat for Flipper Zero (Momentum).
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "blivzy_logo.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

#define DATA_DIR "/ext/apps_data/blivzy_ai"
#define KB_EN    DATA_DIR "/kb_en.txt"
#define KB_HU    DATA_DIR "/kb_hu.txt"
#define WORDS_EN DATA_DIR "/words_en.txt"
#define WORDS_HU DATA_DIR "/words_hu.txt"
#define LOG_FILE DATA_DIR "/chat_log.txt"
#define VERSION  "0.7"

#define SUG_N    3
#define SUG_LEN  40
#define BUF_LEN  96
#define LINE_MAX_LEN 460
#define HIST_N   8
#define NUMS_MAX 12

#define T(app, e_, h_) ((app)->hu ? (h_) : (e_))

enum { ViewSplash, ViewMenu, ViewBox, ViewKbd, ViewHist };
enum { MenuChat, MenuHist, MenuExamples, MenuFact, MenuLang, MenuSound, MenuLog, MenuAbout };
enum { EvtInputDone = 1 };

typedef struct {
    char text[BUF_LEN];
    uint8_t len;
    uint8_t row, col;
    uint8_t layer; // 0 letters, 1 symbols, 2 Hungarian accents (hu only)
    bool in_sug;
    uint8_t sug_sel;
    uint8_t sug_count;
    char sug[SUG_N][SUG_LEN];
    bool sug_phrase[SUG_N];
    bool hu;
} KbdModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* vd;
    View* splash;
    View* kbd;
    Submenu* menu;
    Submenu* hist;
    TextBox* box;
    FuriString* out;
    char buf[BUF_LEN];
    char history[HIST_N][BUF_LEN];
    uint8_t hist_count;
    uint32_t questions;
    bool hu;
    bool sound;
    bool log_on;
    char last_q[BUF_LEN];
    char last_related[32];
    char name[16];
} App;

/* ================= sound ================= */
static void beep(App* a, float f, uint32_t ms) {
    if(!a->sound) return;
    if(furi_hal_speaker_acquire(100)) {
        furi_hal_speaker_start(f, 0.4f);
        furi_delay_ms(ms);
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
}

// "Voice": talking-blip effect, one pitch per letter (not real speech).
static void blip_speak(App* a, const char* t) {
    if(!a->sound) return;
    if(!furi_hal_speaker_acquire(200)) return;
    int n = 0;
    for(; *t && n < 40; t++) {
        char c = (char)tolower((unsigned char)*t);
        if(!isalnum((unsigned char)c)) {
            furi_hal_speaker_stop();
            furi_delay_ms(25);
            continue;
        }
        bool vowel = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u');
        float f = vowel ? 620.0f + (float)((c * 31) % 7) * 40.0f :
                          280.0f + (float)((c * 17) % 11) * 28.0f;
        furi_hal_speaker_start(f, 0.4f);
        furi_delay_ms(vowel ? 50 : 32);
        n++;
    }
    furi_hal_speaker_stop();
    furi_hal_speaker_release();
}

static uint32_t rnd(uint32_t n) {
    return n ? furi_hal_random_get() % n : 0;
}

/* ================= math engine ================= */
static const char* s;
static bool perr;
static double g_ans = 0;
static double g_x = 0;
static double g_y = 0;
static bool g_have_ans = false;
static bool g_hu = false; // Hungarian suffix tolerance while scanning the Hungarian KB

static double parse_expr(void);
static double parse_unary(void);

static void skip(void) {
    while(*s == ' ') s++;
}

static double fact(double x) {
    if(x < 0 || x > 170 || x != floor(x)) {
        perr = true;
        return 0;
    }
    double r = 1;
    for(int i = 2; i <= (int)x; i++) r *= i;
    return r;
}

static double gcd_d(double a, double b) {
    a = fabs(floor(a));
    b = fabs(floor(b));
    while(b > 0.5) {
        double t = fmod(a, b);
        a = b;
        b = t;
    }
    return a;
}

// returns false for unknown function. w is the optional 2nd argument.
static bool apply_fn(const char* n, double v, double w, int nargs, double* r) {
    if(nargs == 2) {
        if(!strcmp(n, "gcd")) *r = gcd_d(v, w);
        else if(!strcmp(n, "lcm")) {
            double g = gcd_d(v, w);
            if(g == 0) perr = true;
            else *r = fabs(v * w) / g;
        } else if(!strcmp(n, "ncr") || !strcmp(n, "npr")) {
            if(w > v || w < 0 || v > 170) perr = true;
            else *r = (!strcmp(n, "ncr")) ? fact(v) / (fact(w) * fact(v - w)) : fact(v) / fact(v - w);
        } else if(!strcmp(n, "min")) *r = v < w ? v : w;
        else if(!strcmp(n, "max")) *r = v > w ? v : w;
        else if(!strcmp(n, "pow")) *r = pow(v, w);
        else if(!strcmp(n, "mod")) {
            if(w == 0) perr = true;
            else *r = fmod(v, w);
        } else if(!strcmp(n, "hypot")) *r = sqrt(v * v + w * w);
        else if(!strcmp(n, "root")) {
            if(w == 0) perr = true;
            else *r = pow(v, 1.0 / w);
        } else if(!strcmp(n, "logb")) {
            if(v <= 0 || w <= 0 || w == 1) perr = true;
            else *r = log(v) / log(w);
        } else return false;
        return true;
    }
    if(!strcmp(n, "sqrt")) {
        if(v < 0) perr = true;
        *r = sqrt(v);
    } else if(!strcmp(n, "cbrt")) *r = cbrt(v);
    else if(!strcmp(n, "sin")) *r = sin(v);
    else if(!strcmp(n, "cos")) *r = cos(v);
    else if(!strcmp(n, "tan")) *r = tan(v);
    else if(!strcmp(n, "sind")) *r = sin(v * M_PI / 180.0);
    else if(!strcmp(n, "cosd")) *r = cos(v * M_PI / 180.0);
    else if(!strcmp(n, "tand")) *r = tan(v * M_PI / 180.0);
    else if(!strcmp(n, "asin")) *r = asin(v);
    else if(!strcmp(n, "acos")) *r = acos(v);
    else if(!strcmp(n, "atan")) *r = atan(v);
    else if(!strcmp(n, "sinh")) *r = sinh(v);
    else if(!strcmp(n, "cosh")) *r = cosh(v);
    else if(!strcmp(n, "tanh")) *r = tanh(v);
    else if(!strcmp(n, "exp")) *r = exp(v);
    else if(!strcmp(n, "abs")) *r = fabs(v);
    else if(!strcmp(n, "floor")) *r = floor(v);
    else if(!strcmp(n, "ceil")) *r = ceil(v);
    else if(!strcmp(n, "round")) *r = round(v);
    else if(!strcmp(n, "sign")) *r = (v > 0) - (v < 0);
    else if(!strcmp(n, "fact")) *r = fact(v);
    else if(!strcmp(n, "rad")) *r = v * M_PI / 180.0;
    else if(!strcmp(n, "deg")) *r = v * 180.0 / M_PI;
    else if(!strcmp(n, "ln")) {
        if(v <= 0) perr = true;
        *r = log(v);
    } else if(!strcmp(n, "log")) {
        if(v <= 0) perr = true;
        *r = log10(v);
    } else if(!strcmp(n, "log2")) {
        if(v <= 0) perr = true;
        *r = log2(v);
    } else return false;
    return true;
}

static double parse_primary(void) {
    skip();
    if(*s == '(') {
        s++;
        double v = parse_expr();
        skip();
        if(*s == ')') s++;
        else perr = true;
        return v;
    }
    if(isdigit((unsigned char)*s) || *s == '.') {
        char* e;
        double v = strtod(s, &e);
        s = e;
        return v;
    }
    if(isalpha((unsigned char)*s)) {
        char name[8];
        size_t n = 0;
        while(isalnum((unsigned char)*s)) {
            if(n < 7) name[n++] = (char)tolower((unsigned char)*s);
            s++;
        }
        name[n] = 0;
        if(!strcmp(name, "pi")) return M_PI;
        if(!strcmp(name, "e")) return M_E;
        if(!strcmp(name, "tau")) return 2 * M_PI;
        if(!strcmp(name, "phi")) return 1.6180339887498949;
        if(!strcmp(name, "ans")) return g_ans;
        if(!strcmp(name, "x")) return g_x;
        if(!strcmp(name, "y")) return g_y;
        skip();
        double v = 0, w = 0, r = 0;
        int nargs = 1;
        if(*s == '(') {
            s++;
            v = parse_expr();
            skip();
            if(*s == ',') {
                s++;
                w = parse_expr();
                nargs = 2;
                skip();
            }
            if(*s == ')') s++;
            else perr = true;
        } else {
            v = parse_unary(); // "sqrt 144"
        }
        if(!apply_fn(name, v, w, nargs, &r)) perr = true;
        return r;
    }
    perr = true;
    return 0;
}

static double parse_power(void) {
    double v = parse_primary();
    skip();
    while(*s == '!') {
        s++;
        v = fact(v);
        skip();
    }
    if(*s == '^') {
        s++;
        return pow(v, parse_unary());
    }
    return v;
}

static double parse_unary(void) {
    skip();
    if(*s == '-') {
        s++;
        return -parse_unary();
    }
    if(*s == '+') {
        s++;
        return parse_unary();
    }
    return parse_power();
}

static double parse_term(void) {
    double v = parse_unary();
    for(;;) {
        skip();
        if(*s == '*') {
            s++;
            v *= parse_unary();
        } else if(*s == '/') {
            s++;
            double d = parse_unary();
            if(d == 0) perr = true;
            else v /= d;
        } else if(*s == '%') {
            s++;
            double d = parse_unary();
            if(d == 0) perr = true;
            else v = fmod(v, d);
        } else if(*s == '(' || isalpha((unsigned char)*s)) {
            v *= parse_unary(); // implicit multiplication: 2x, 3(4+1), 2pi
        } else {
            break;
        }
    }
    return v;
}

static double parse_expr(void) {
    double v = parse_term();
    for(;;) {
        skip();
        if(*s == '+') {
            s++;
            v += parse_term();
        } else if(*s == '-') {
            s++;
            v -= parse_term();
        } else {
            break;
        }
    }
    return v;
}

static bool eval_str(const char* str, double* out) {
    s = str;
    perr = false;
    double v = parse_expr();
    skip();
    if(*s != 0) perr = true;
    if(!isfinite(v)) perr = true;
    *out = v;
    return !perr;
}

// number formatting: trims zeros, switches to scientific for huge/tiny values
static void cat_num(FuriString* o, double v) {
    if(v != 0 && (fabs(v) >= 1e12 || fabs(v) < 1e-6)) {
        furi_string_cat_printf(o, "%.6g", v);
        return;
    }
    FuriString* n = furi_string_alloc_printf("%.8f", v);
    while(furi_string_size(n) > 1 && furi_string_get_char(n, furi_string_size(n) - 1) == '0')
        furi_string_left(n, furi_string_size(n) - 1);
    if(furi_string_get_char(n, furi_string_size(n) - 1) == '.')
        furi_string_left(n, furi_string_size(n) - 1);
    if(!strcmp(furi_string_get_cstr(n), "-0")) furi_string_set_str(n, "0");
    furi_string_cat(o, n);
    furi_string_free(n);
}

/* ---- natural-language -> expression ---- */
static void lower_copy(char* d, size_t cap, const char* in) {
    size_t i = 0;
    for(; in[i] && i + 1 < cap; i++) d[i] = (char)tolower((unsigned char)in[i]);
    d[i] = 0;
}

// replace first occurrence of `from` (word-ish) in buf by `to`
static bool repl(char* buf, size_t cap, const char* from, const char* to) {
    char* p = strstr(buf, from);
    if(!p) return false;
    size_t fl = strlen(from), tl = strlen(to), rest = strlen(p + fl);
    if(strlen(buf) - fl + tl + 1 > cap) return false;
    memmove(p + tl, p + fl, rest + 1);
    memcpy(p, to, tl);
    return true;
}

static void repl_all(char* buf, size_t cap, const char* from, const char* to) {
    int guard = 8;
    while(guard-- && repl(buf, cap, from, to)) {
    }
}

static bool extract_math(const char* input, char* expression, size_t cap) {
    char t[BUF_LEN + 24];
    lower_copy(t, sizeof(t), input);
    repl_all(t, sizeof(t), "% of ", "*0.01*");
    repl_all(t, sizeof(t), " percent of ", "*0.01*");
    repl_all(t, sizeof(t), "square root of ", "sqrt ");
    repl_all(t, sizeof(t), "cube root of ", "cbrt ");
    repl_all(t, sizeof(t), " to the power of ", "^");
    repl_all(t, sizeof(t), " squared", "^2");
    repl_all(t, sizeof(t), " cubed", "^3");
    repl_all(t, sizeof(t), " multiplied by ", "*");
    repl_all(t, sizeof(t), " divided by ", "/");
    repl_all(t, sizeof(t), " plus ", "+");
    repl_all(t, sizeof(t), " minus ", "-");
    repl_all(t, sizeof(t), " times ", "*");
    repl_all(t, sizeof(t), " over ", "/");
    repl_all(t, sizeof(t), " mod ", "%");
    repl_all(t, sizeof(t), " and ", "+");
    char* p = t;
    while(*p == ' ') p++;
    const char* pre[] = {"calculate ", "what is ", "what's ", "compute ", "evaluate ", "work out ", "how much is ", "whats ", "solve ", "calc "};
    for(size_t i = 0; i < sizeof(pre) / sizeof(pre[0]); i++) {
        size_t n = strlen(pre[i]);
        if(strncmp(p, pre[i], n) == 0) {
            p += n;
            break;
        }
    }
    bool digit = false, op = false, fn = false;
    for(const char* c = p; *c; c++) {
        if(isdigit((unsigned char)*c)) digit = true;
        if(strchr("+-*/%^()!", *c)) op = true;
    }
    if(strstr(p, "sqrt") || strstr(p, "cbrt") || strstr(p, "sin") || strstr(p, "cos") ||
       strstr(p, "tan") || strstr(p, "log") || strstr(p, "ln") || strstr(p, "gcd") || strstr(p, "lcm") ||
       strstr(p, "ncr") || strstr(p, "npr") || strstr(p, "abs") || strstr(p, "exp") || strstr(p, "fact"))
        fn = true;
    if(!digit && !strstr(p, "pi") && !strstr(p, "ans")) return false;
    if(!op && !fn) return false;
    size_t j = 0;
    for(; *p && j + 1 < cap; p++) {
        if(*p == '?' || *p == '=' || *p == '$') continue;
        if(isalpha((unsigned char)*p)) {
            // only allow letters that belong to a known function/constant token
            char* e = p;
            while(isalpha((unsigned char)*e)) e++;
            char w[10];
            size_t wl = (size_t)(e - p);
            if(wl > 7) return false;
            memcpy(w, p, wl);
            w[wl] = 0;
            double dummy;
            bool ok = !strcmp(w, "pi") || !strcmp(w, "e") || !strcmp(w, "ans") || !strcmp(w, "tau") || !strcmp(w, "phi") ||
                      apply_fn(w, 1, 1, 1, &dummy) || apply_fn(w, 1, 1, 2, &dummy);
            perr = false;
            if(!ok) return false;
            for(size_t k = 0; k < wl && j + 1 < cap; k++) expression[j++] = w[k];
            p = e - 1;
        } else {
            expression[j++] = *p;
        }
    }
    expression[j] = 0;
    return j > 0;
}

static void do_calc(App* a, const char* expr) {
    double v;
    bool ok = eval_str(expr, &v);
    furi_string_printf(a->out, "%s\n\n= ", expr);
    if(!ok) {
        furi_string_cat_str(a->out, T(a, "Error - check the expression (division by zero? missing bracket?)", "Hiba - ellenorizd a kifejezest"));
        return;
    }
    g_ans = v;
    g_have_ans = true;
    cat_num(a->out, v);
    furi_string_cat_str(a->out, T(a, "\n\n(saved as ans)", "\n\n(mentve: ans)"));
}

/* ---- equation solver: numeric root finding on f(x)=lhs-rhs ---- */
static bool f_eval(const char* l, const char* r, double x, double* out) {
    double a, b;
    g_x = x;
    if(!eval_str(l, &a)) return false;
    if(!eval_str(r, &b)) return false;
    *out = a - b;
    return true;
}

static bool do_solve(App* a, const char* input) {
    char t[BUF_LEN + 8];
    lower_copy(t, sizeof(t), input);
    char* p = t;
    const char* pre[] = {"solve ", "find x in ", "find x: ", "what is x in ", "equation "};
    for(size_t i = 0; i < sizeof(pre) / sizeof(pre[0]); i++) {
        size_t n = strlen(pre[i]);
        if(strncmp(p, pre[i], n) == 0) {
            p += n;
            break;
        }
    }
    char* eq = strchr(p, '=');
    if(!eq) return false;
    *eq = 0;
    char* lhs = p;
    char* rhs = eq + 1;
    if(!strchr(lhs, 'x') && !strchr(rhs, 'x')) return false;
    for(char* q = rhs; *q; q++)
        if(*q == '?') *q = ' ';
    double roots[6];
    int nr = 0;
    double prev, cur, x0 = -200, step = 0.25;
    if(!f_eval(lhs, rhs, x0, &prev)) prev = NAN;
    for(double x = x0 + step; x <= 200 + 1e-9 && nr < 6; x += step) {
        if(!f_eval(lhs, rhs, x, &cur)) {
            prev = NAN;
            continue;
        }
        if(!isnan(prev)) {
            if(fabs(cur) < 1e-12) {
                if(nr == 0 || fabs(roots[nr - 1] - x) > 1e-4) roots[nr++] = x;
            } else if(prev * cur < 0 && fabs(prev) < 1e12 && fabs(cur) < 1e12) {
                double lo = x - step, hi = x, flo = prev, fm = 0, mid = x;
                for(int i = 0; i < 60; i++) {
                    mid = (lo + hi) / 2;
                    if(!f_eval(lhs, rhs, mid, &fm)) break;
                    if(flo * fm <= 0) hi = mid;
                    else {
                        lo = mid;
                        flo = fm;
                    }
                }
                if(nr == 0 || fabs(roots[nr - 1] - mid) > 1e-4) roots[nr++] = mid;
            }
        }
        prev = cur;
    }
    furi_string_printf(a->out, "%s = %s\n\n", lhs, rhs);
    if(nr == 0) {
        furi_string_cat_str(a->out, T(a, "No real solution found for x in -200..200.", "Nincs valos megoldas -200..200 kozott."));
        return true;
    }
    for(int i = 0; i < nr; i++) {
        double r = roots[i];
        if(fabs(r - round(r)) < 1e-6) r = round(r);
        furi_string_cat_printf(a->out, "x%s = ", nr > 1 ? (i == 0 ? "1" : i == 1 ? "2" : i == 2 ? "3" : i == 3 ? "4" : i == 4 ? "5" : "6") : "");
        cat_num(a->out, r);
        furi_string_cat_str(a->out, "\n");
    }
    furi_string_cat_str(a->out, T(a, "\n(numeric search, range -200..200)", "\n(numerikus keresesi tartomany -200..200)"));
    g_ans = roots[0];
    return true;
}

static void do_quad(App* a, double qa, double qb, double qc) {
    furi_string_set_str(a->out, "a=");
    cat_num(a->out, qa);
    furi_string_cat_str(a->out, "  b=");
    cat_num(a->out, qb);
    furi_string_cat_str(a->out, "  c=");
    cat_num(a->out, qc);
    furi_string_cat_str(a->out, "\n\n");
    if(qa == 0) {
        if(qb == 0) furi_string_cat_str(a->out, T(a, "Not an equation (a=0, b=0)", "Nem egyenlet (a=0, b=0)"));
        else {
            furi_string_cat_str(a->out, "x = ");
            cat_num(a->out, -qc / qb);
        }
        return;
    }
    double d = qb * qb - 4 * qa * qc;
    furi_string_cat_str(a->out, "D = ");
    cat_num(a->out, d);
    furi_string_cat_str(a->out, "\n");
    if(d >= 0) {
        furi_string_cat_str(a->out, "x1 = ");
        cat_num(a->out, (-qb + sqrt(d)) / (2 * qa));
        furi_string_cat_str(a->out, "\nx2 = ");
        cat_num(a->out, (-qb - sqrt(d)) / (2 * qa));
    } else {
        furi_string_cat_str(a->out, "x = ");
        cat_num(a->out, -qb / (2 * qa));
        furi_string_cat_str(a->out, " +- ");
        cat_num(a->out, sqrt(-d) / (2 * fabs(qa)));
        furi_string_cat_str(a->out, "i");
    }
    furi_string_cat_str(a->out, "\nVertex x = ");
    cat_num(a->out, -qb / (2 * qa));
}

/* ================= number extraction ================= */
static int extract_nums(const char* in, double* v, int max) {
    int n = 0;
    const char* p = in;
    while(*p && n < max) {
        bool start = isdigit((unsigned char)*p) ||
                     ((*p == '-' || *p == '.') && isdigit((unsigned char)p[1]) && (p == in || !isalnum((unsigned char)p[-1])));
        if(start) {
            char* e;
            v[n++] = strtod(p, &e);
            p = (e > p) ? e : p + 1;
        } else {
            p++;
        }
    }
    return n;
}

/* ================= unit conversion ================= */
typedef struct {
    const char* names; // '|' separated
    uint8_t cat;       // 0 len 1 mass 2 vol 3 time 4 temp 5 speed 6 data
    double f;          // factor to base unit
} Unit;

static const Unit units[] = {
    {"m|meter|meters|metre|metres", 0, 1}, {"km|kilometer|kilometers|kilometre|kilometres", 0, 1000},
    {"cm|centimeter|centimeters|centimetre", 0, 0.01}, {"mm|millimeter|millimeters", 0, 0.001},
    {"mi|mile|miles|merfold", 0, 1609.344}, {"ft|foot|feet|lab", 0, 0.3048}, {"in|inch|inches|hvelyk|col", 0, 0.0254},
    {"yd|yard|yards", 0, 0.9144},
    {"kg|kilogram|kilograms|kilo|kilos|kilogramm", 1, 1}, {"g|gram|grams|gramm", 1, 0.001}, {"mg|milligram|milligrams|milligramm", 1, 1e-6},
    {"lb|lbs|pound|pounds|font", 1, 0.45359237}, {"oz|ounce|ounces|uncia", 1, 0.028349523}, {"t|tonne|tonnes|tonna", 1, 1000},
    {"l|liter|liters|litre|litres", 2, 1}, {"ml|milliliter|milliliters|milliliter", 2, 0.001},
    {"dl|deciliter|deciliters", 2, 0.1}, {"cl|centiliter|centiliters", 2, 0.01}, {"month|months|honap", 3, 2629800}, {"gal|gallon|gallons", 2, 3.785411784}, {"cup|cups", 2, 0.2365882365}, {"floz", 2, 0.0295735},
    {"s|sec|second|seconds|masodperc", 3, 1}, {"min|minute|minutes|mins|perc", 3, 60}, {"h|hr|hour|hours|ora", 3, 3600},
    {"day|days|nap", 3, 86400}, {"week|weeks|het", 3, 604800}, {"year|years|ev", 3, 31557600},
    {"c|celsius", 4, 0}, {"f|fahrenheit", 4, 1}, {"k|kelvin", 4, 2},
    {"ms|mps", 5, 1}, {"kmh|kph|kmph", 5, 0.2777777778}, {"mph", 5, 0.44704}, {"knot|knots|kn|csomo", 5, 0.5144444},
    {"b|byte|bytes|bajt", 6, 1}, {"kb|kilobyte|kilobytes|kilobajt", 6, 1024}, {"mb|megabyte|megabytes|megabajt", 6, 1048576},
    {"gb|gigabyte|gigabytes|gigabajt", 6, 1073741824.0}, {"bit|bits", 6, 0.125},
};

static int find_unit(const char* w) {
    size_t wl = strlen(w);
    for(size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        const char* p = units[i].names;
        while(*p) {
            const char* e = strchr(p, '|');
            size_t l = e ? (size_t)(e - p) : strlen(p);
            if(l == wl && strncmp(p, w, l) == 0) return (int)i;
            if(!e) break;
            p = e + 1;
        }
    }
    if(wl > 4) { // Hungarian case suffixes: merfoldben, fontra, kilometerre, fokot ...
        static const char* const suf[] = {"ben", "ban", "bol", "rol", "nal", "nel", "be", "ba", "re", "ra", "et", "ot", "at", "t"};
        for(size_t k = 0; k < sizeof(suf) / sizeof(suf[0]); k++) {
            size_t sl = strlen(suf[k]);
            if(wl > sl + 2 && !strcmp(w + wl - sl, suf[k])) {
                char tmp[24];
                if(wl - sl < sizeof(tmp)) {
                    memcpy(tmp, w, wl - sl);
                    tmp[wl - sl] = 0;
                    int r = find_unit(tmp);
                    if(r >= 0) return r;
                }
            }
        }
    }
    return -1;
}

static const char* unit_label(int i) {
    static char lab[12];
    const char* n = units[i].names;
    const char* e = strchr(n, '|');
    size_t l = e ? (size_t)(e - n) : strlen(n);
    if(l > 10) l = 10;
    memcpy(lab, n, l);
    lab[l] = 0;
    return lab;
}

static bool do_convert(App* a, const char* input) {
    char t[BUF_LEN + 8];
    char src[BUF_LEN + 8];
    lower_copy(src, sizeof(src), input);
    repl_all(src, sizeof(src), "km/h", "kmh");
    repl_all(src, sizeof(src), "mi/h", "mph");
    repl_all(src, sizeof(src), "m/s", "ms");
    size_t j = 0;
    for(size_t i = 0; src[i] && j + 1 < sizeof(t); i++) {
        char c = src[i];
        t[j++] = (isalnum((unsigned char)c) || c == '.' || c == '-') ? c : ' ';
    }
    t[j] = 0;
    char* words[14];
    int nw = 0;
    for(char* tok = strtok(t, " "); tok && nw < 14; tok = strtok(NULL, " ")) words[nw++] = tok;
    for(int i = 0; i + 1 < nw; i++) {
        char* e;
        double v = strtod(words[i], &e);
        if(e == words[i]) continue;
        int u1;
        int k = i + 1;
        if(*e) { // "5km"
            u1 = find_unit(e);
        } else {
            if(k >= nw) continue;
            u1 = find_unit(words[k]);
            k++;
        }
        if(u1 < 0) continue;
        if(k < nw && (!strcmp(words[k], "to") || !strcmp(words[k], "in") || !strcmp(words[k], "into") || !strcmp(words[k], "as") || !strcmp(words[k], "is")))
            k++;
        else if(a->hu && k < nw && find_unit(words[k]) >= 0) {
            // Hungarian: "5 km merfoldben" - the unit with a suffix says it all
        } else
            continue;
        if(k >= nw) continue;
        int u2 = find_unit(words[k]);
        if(u2 < 0 || units[u1].cat != units[u2].cat) continue;
        double r;
        if(units[u1].cat == 4) {
            int uc = find_unit("c"), uf = find_unit("f");
            double c = (u1 == uc) ? v : (u1 == uf) ? (v - 32) * 5.0 / 9.0 : v - 273.15;
            r = (u2 == uc) ? c : (u2 == uf) ? c * 9.0 / 5.0 + 32 : c + 273.15;
        } else {
            r = v * units[u1].f / units[u2].f;
        }
        furi_string_reset(a->out);
        cat_num(a->out, v);
        furi_string_cat_printf(a->out, " %s\n\n= ", unit_label(u1));
        cat_num(a->out, r);
        furi_string_cat_printf(a->out, " %s", unit_label(u2));
        g_ans = r;
        return true;
    }
    return false;
}

/* ================= number tools & geometry ================= */
static bool has_word(const char* q, const char* w);

static void cat_int(FuriString* o, double v) {
    furi_string_cat_printf(o, "%.0f", v);
}

static void roman(FuriString* o, int n) {
    static const int val[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* sym[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    for(int i = 0; i < 13; i++)
        while(n >= val[i]) {
            furi_string_cat_str(o, sym[i]);
            n -= val[i];
        }
}

static int cmp_d(const void* x, const void* y) {
    double a = *(const double*)x, b = *(const double*)y;
    return (a > b) - (a < b);
}

static bool do_numbers(App* a, const char* in, const char* q) {
    double v[NUMS_MAX];
    int n = extract_nums(in, v, NUMS_MAX);
    FuriString* o = a->out;
    // statistics on lists
    if((has_word(q, "average") || has_word(q, "mean") || has_word(q, "median") || has_word(q, "sum") ||
        has_word(q, "total") || has_word(q, "stats") || has_word(q, "atlag") ||
        has_word(q, "deviation") || has_word(q, "stdev") || has_word(q, "std") || has_word(q, "variance") || has_word(q, "mode") || has_word(q, "szoras")) && n >= 2) {
        double sum = 0, mn = v[0], mx = v[0];
        for(int i = 0; i < n; i++) {
            sum += v[i];
            if(v[i] < mn) mn = v[i];
            if(v[i] > mx) mx = v[i];
        }
        double sorted[NUMS_MAX];
        memcpy(sorted, v, sizeof(double) * (size_t)n);
        qsort(sorted, (size_t)n, sizeof(double), cmp_d);
        double med = (n % 2) ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
        furi_string_printf(o, "%d numbers\nSum = ", n);
        cat_num(o, sum);
        furi_string_cat_str(o, "\nMean = ");
        cat_num(o, sum / n);
        furi_string_cat_str(o, "\nMedian = ");
        cat_num(o, med);
        furi_string_cat_str(o, "\nMin = ");
        cat_num(o, mn);
        furi_string_cat_str(o, "\nMax = ");
        cat_num(o, mx);
        furi_string_cat_str(o, "\nRange = ");
        cat_num(o, mx - mn);
        double var = 0;
        for(int i = 0; i < n; i++) var += (v[i] - sum / n) * (v[i] - sum / n);
        var /= n;
        furi_string_cat_str(o, "\nVariance = ");
        cat_num(o, round(var * 1e6) / 1e6);
        furi_string_cat_str(o, "\nStd dev = ");
        cat_num(o, round(sqrt(var) * 1e6) / 1e6);
        int bestc = 0;
        double mode = sorted[0];
        for(int i = 0; i < n; i++) {
            int c = 0;
            for(int k = 0; k < n; k++) c += sorted[k] == sorted[i];
            if(c > bestc) {
                bestc = c;
                mode = sorted[i];
            }
        }
        if(bestc > 1) {
            furi_string_cat_str(o, "\nMode = ");
            cat_num(o, mode);
        }
        g_ans = sum / n;
        return true;
    }
    if(n >= 2 && (has_word(q, "gcd") || has_word(q, "hcf") || has_word(q, "lcm"))) {
        double g = gcd_d(v[0], v[1]);
        furi_string_printf(o, "GCD(");
        cat_int(o, v[0]);
        furi_string_cat_str(o, ", ");
        cat_int(o, v[1]);
        furi_string_cat_str(o, ") = ");
        cat_int(o, g);
        furi_string_cat_str(o, "\nLCM = ");
        if(g > 0) cat_int(o, fabs(v[0] * v[1]) / g);
        return true;
    }
    if(n >= 2 && has_word(q, "bmi")) { // bmi 70 kg 175 cm
        double h = v[1] > 3 ? v[1] / 100.0 : v[1];
        double b = v[0] / (h * h);
        furi_string_printf(o, "BMI = ");
        cat_num(o, round(b * 10) / 10);
        furi_string_cat_str(o, b < 18.5 ? "\nUnderweight range" : b < 25 ? "\nHealthy range" : b < 30 ? "\nOverweight range" : "\nObese range");
        furi_string_cat_str(o, "\n(general guide only, not medical advice)");
        return true;
    }
    if(n >= 1 && (has_word(q, "prime") || has_word(q, "factors") || has_word(q, "factorize") || has_word(q, "factorise"))) {
        double x = v[0];
        if(x < 2 || x > 2e9 || x != floor(x)) {
            furi_string_set_str(o, "Give a whole number from 2 to 2,000,000,000.");
            return true;
        }
        uint32_t m = (uint32_t)x, r = m;
        furi_string_reset(o);
        cat_int(o, x);
        furi_string_cat_str(o, " = ");
        bool first = true;
        int count = 0;
        for(uint32_t d = 2; (uint64_t)d * d <= r; d += (d == 2 ? 1 : 2)) {
            while(r % d == 0) {
                furi_string_cat_printf(o, "%s%lu", first ? "" : " x ", (unsigned long)d);
                first = false;
                r /= d;
                count++;
            }
        }
        if(r > 1 || first) {
            furi_string_cat_printf(o, "%s%lu", first ? "" : " x ", (unsigned long)r);
            count++;
        }
        if(has_word(q, "prime")) furi_string_cat_str(o, count == 1 ? "\n\nYes, it is prime." : "\n\nNot prime.");
        return true;
    }
    if(n >= 1 && has_word(q, "fibonacci")) {
        int k = (int)v[0];
        if(k < 0 || k > 78) {
            furi_string_set_str(o, "Pick n from 0 to 78.");
            return true;
        }
        double f0 = 0, f1 = 1;
        for(int i = 0; i < k; i++) {
            double t = f0 + f1;
            f0 = f1;
            f1 = t;
        }
        furi_string_printf(o, "Fibonacci(%d) = ", k);
        cat_int(o, f0);
        return true;
    }
    if(n >= 1 && (has_word(q, "binary") || has_word(q, "hex") || has_word(q, "hexadecimal") || has_word(q, "octal") || has_word(q, "roman"))) {
        double x = v[0];
        if(x < 0 || x != floor(x) || x > 4e9) {
            furi_string_set_str(o, "Give a whole number from 0 to 4,000,000,000.");
            return true;
        }
        uint32_t u = (uint32_t)x;
        furi_string_printf(o, "%lu\n\n", (unsigned long)u);
        furi_string_cat_printf(o, "Hex: 0x%lX\nOctal: 0%lo\nBinary: ", (unsigned long)u, (unsigned long)u);
        if(u == 0) furi_string_cat_str(o, "0");
        else {
            bool started = false;
            for(int b = 31; b >= 0; b--) {
                bool on = (u >> b) & 1;
                if(on) started = true;
                if(started) furi_string_push_back(o, on ? '1' : '0');
            }
        }
        if(u > 0 && u < 4000) {
            furi_string_cat_str(o, "\nRoman: ");
            roman(o, (int)u);
        }
        return true;
    }
    if(n >= 1 && (has_word(q, "even") || has_word(q, "odd")) && has_word(q, "is")) {
        furi_string_reset(o);
        cat_int(o, v[0]);
        furi_string_cat_str(o, fmod(fabs(v[0]), 2) < 0.5 ? " is even." : " is odd.");
        return true;
    }
    // geometry
    bool area = has_word(q, "area"), vol = has_word(q, "volume"), per = has_word(q, "perimeter") || has_word(q, "circumference");
    if(has_word(q, "hypotenuse") && n >= 2) {
        furi_string_set_str(o, "Hypotenuse = ");
        cat_num(o, sqrt(v[0] * v[0] + v[1] * v[1]));
        return true;
    }
    if(has_word(q, "circle") && n >= 1 && (area || per)) {
        bool dia = has_word(q, "diameter");
        double r = dia ? v[0] / 2 : v[0];
        furi_string_printf(o, "Circle r = ");
        cat_num(o, r);
        furi_string_cat_str(o, "\nArea = ");
        cat_num(o, M_PI * r * r);
        furi_string_cat_str(o, "\nCircumference = ");
        cat_num(o, 2 * M_PI * r);
        return true;
    }
    if(has_word(q, "sphere") && n >= 1 && (vol || area)) {
        furi_string_printf(o, "Sphere r = ");
        cat_num(o, v[0]);
        furi_string_cat_str(o, "\nVolume = ");
        cat_num(o, 4.0 / 3.0 * M_PI * v[0] * v[0] * v[0]);
        furi_string_cat_str(o, "\nSurface = ");
        cat_num(o, 4 * M_PI * v[0] * v[0]);
        return true;
    }
    if(has_word(q, "cylinder") && n >= 2) {
        furi_string_printf(o, "Cylinder r=");
        cat_num(o, v[0]);
        furi_string_cat_str(o, " h=");
        cat_num(o, v[1]);
        furi_string_cat_str(o, "\nVolume = ");
        cat_num(o, M_PI * v[0] * v[0] * v[1]);
        furi_string_cat_str(o, "\nSurface = ");
        cat_num(o, 2 * M_PI * v[0] * (v[0] + v[1]));
        return true;
    }
    if(has_word(q, "cube") && n >= 1 && (vol || area)) {
        furi_string_printf(o, "Cube side = ");
        cat_num(o, v[0]);
        furi_string_cat_str(o, "\nVolume = ");
        cat_num(o, v[0] * v[0] * v[0]);
        furi_string_cat_str(o, "\nSurface = ");
        cat_num(o, 6 * v[0] * v[0]);
        return true;
    }
    if((has_word(q, "rectangle") || has_word(q, "square")) && n >= 1 && (area || per)) {
        double w = (n >= 2) ? v[1] : v[0];
        furi_string_printf(o, "Sides ");
        cat_num(o, v[0]);
        furi_string_cat_str(o, " x ");
        cat_num(o, w);
        furi_string_cat_str(o, "\nArea = ");
        cat_num(o, v[0] * w);
        furi_string_cat_str(o, "\nPerimeter = ");
        cat_num(o, 2 * (v[0] + w));
        return true;
    }
    if(has_word(q, "triangle") && n >= 2 && area) {
        furi_string_set_str(o, "Triangle area = 1/2 x ");
        cat_num(o, v[0]);
        furi_string_cat_str(o, " x ");
        cat_num(o, v[1]);
        furi_string_cat_str(o, " = ");
        cat_num(o, 0.5 * v[0] * v[1]);
        return true;
    }
    // tip / discount / percent change
    if(has_word(q, "tip") && n >= 2) {
        double tip = v[1] / 100.0 * v[0];
        furi_string_set_str(o, "Tip = ");
        cat_num(o, round(tip * 100) / 100);
        furi_string_cat_str(o, "\nTotal = ");
        cat_num(o, round((v[0] + tip) * 100) / 100);
        return true;
    }
    if((has_word(q, "discount") || has_word(q, "off")) && n >= 2) {
        furi_string_set_str(o, "Price after discount = ");
        cat_num(o, round(v[0] * (1 - v[1] / 100.0) * 100) / 100);
        furi_string_cat_str(o, "\nYou save = ");
        cat_num(o, round(v[0] * v[1]) / 100);
        return true;
    }
    if(has_word(q, "increase") && has_word(q, "percent") && n >= 2) {
        furi_string_set_str(o, "Percent change = ");
        cat_num(o, round((v[1] - v[0]) / v[0] * 10000) / 100);
        furi_string_cat_str(o, "%");
        return true;
    }
    return false;
}

/* ================= text helpers ================= */
static bool phrase_match(const char* text, const char* phrase) {
    size_t n = strlen(phrase);
    if(n == 0) return false;
    const char* p = text;
    while((p = strstr(p, phrase)) != NULL) {
        bool left = (p == text) || !isalnum((unsigned char)p[-1]);
        bool right = !isalnum((unsigned char)p[n]) ||
                     (p[n] == 's' && !isalnum((unsigned char)p[n + 1])) ||
                     (p[n] == 'e' && p[n + 1] == 's' && !isalnum((unsigned char)p[n + 2]));
        if(!right && g_hu && n >= 4) { // fotoszintezis -> fotoszintezisrol, gravitacio -> gravitaciot
            size_t k = 0;
            while(isalpha((unsigned char)p[n + k]) && k < 7) k++;
            if(!isalnum((unsigned char)p[n + k])) right = true;
        }
        if(left && right) return true;
        p++;
    }
    return false;
}

static bool has_word(const char* q, const char* w) {
    return phrase_match(q, w);
}

static bool has_any(const char* q, const char* const* words, size_t count) {
    for(size_t i = 0; i < count; i++)
        if(phrase_match(q, words[i])) return true;
    return false;
}
#define HAS_ANY(q, arr) has_any((q), (arr), sizeof(arr) / sizeof((arr)[0]))

static void normalize_question(char* out, size_t cap, const char* in) {
    size_t j = 0;
    bool space = true;
    for(size_t i = 0; in[i] && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if(isalnum(c)) {
            out[j++] = (char)tolower(c);
            space = false;
        } else if(!space && j + 1 < cap) {
            out[j++] = ' ';
            space = true;
        }
    }
    while(j && out[j - 1] == ' ') j--;
    out[j] = 0;
}

// small edit-distance check (<= maxd) for fuzzy keyword matching
static bool near_word(const char* a, size_t al, const char* b, size_t bl, int maxd) {
    if(al > 20 || bl > 20) return false;
    if((al > bl ? al - bl : bl - al) > (size_t)maxd) return false;
    uint8_t prev[22], cur[22];
    for(size_t j = 0; j <= bl; j++) prev[j] = (uint8_t)j;
    for(size_t i = 1; i <= al; i++) {
        cur[0] = (uint8_t)i;
        for(size_t j = 1; j <= bl; j++) {
            uint8_t c = a[i - 1] == b[j - 1] ? 0 : 1;
            uint8_t m = prev[j - 1] + c;
            if(prev[j] + 1 < m) m = prev[j] + 1;
            if(cur[j - 1] + 1 < m) m = cur[j - 1] + 1;
            cur[j] = m;
        }
        memcpy(prev, cur, bl + 1);
    }
    return prev[bl] <= maxd;
}

/* ================= Hungarian support: accent folding, translation, number words ================= */
// The Flipper font has no accented letters, so every input is folded to plain ASCII
// (a/a, e/e, i/i, o/o/o/o, u/u/u/u) before matching. UTF-8 pasted text is folded too.
static void fold_utf8(char* out, size_t cap, const char* in) {
    size_t j = 0;
    for(size_t i = 0; in[i] && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if(c < 0x80) {
            out[j++] = (char)c;
            continue;
        }
        unsigned char d = (unsigned char)in[i + 1];
        char r = 0;
        if(c == 0xC3) {
            switch(d) {
            case 0x80: case 0x81: case 0x84: case 0xA0: case 0xA1: case 0xA4: r = 'a'; break;
            case 0x88: case 0x89: case 0xA8: case 0xA9: r = 'e'; break;
            case 0x8D: case 0xAD: r = 'i'; break;
            case 0x93: case 0x96: case 0xB3: case 0xB6: r = 'o'; break;
            case 0x9A: case 0x9C: case 0xBA: case 0xBC: r = 'u'; break;
            default: break;
            }
        } else if(c == 0xC5) {
            if(d == 0x90 || d == 0x91) r = 'o';
            else if(d == 0xB0 || d == 0xB1) r = 'u';
        }
        size_t extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
        while(extra-- && in[i + 1]) i++;
        if(r) out[j++] = r;
    }
    out[j] = 0;
}

typedef struct {
    const char* hu;
    const char* en;
} HuMap;

// Hungarian command words -> the English words the built-in tools understand.
// A trailing '*' means "stem": it also matches inflected forms (terulete, teruletet, ...).
static const HuMap hu_map[] = {
    {"negyzetgyok*", "square root of"}, {"kobgyok*", "cube root of"}, {"gyok", "sqrt"},
    {"plusz", "plus"}, {"per", "over"}, {"minusz", "minus"}, {"szorozva", "times"}, {"szor", "times"},
    {"osztva", "divided by"}, {"kivonva", "minus"}, {"hozzaadva", "plus"}, {"modulo", "mod"},
    {"szazaleka", "% of"}, {"szazalek*", "percent"}, {"negyzeten", "squared"}, {"kobon", "cubed"},
    {"hatvanyon", "to the power of"},
    {"mennyi", "calculate"}, {"szamold", "calculate"}, {"szamolj", "calculate"}, {"szamitsd", "calculate"},
    {"szamitas", "calculate"}, {"oldd", "solve"}, {"megoldas*", "solve"},
    {"valtsd", "convert"}, {"atvaltas*", "convert"}, {"konvertald", "convert"}, {"hany", "to"},
    {"primszam*", "prime"}, {"prim", "prime"}, {"faktorizald", "factorize"}, {"osztoi", "factors"},
    {"binaris*", "binary"}, {"hexa", "hex"}, {"hexadecimalis", "hexadecimal"}, {"oktalis*", "octal"},
    {"romai", "roman"}, {"osszeg*", "sum"}, {"atlag*", "average"}, {"median*", "median"},
    {"terulet*", "area"}, {"terfogat*", "volume"}, {"kerulet*", "perimeter"}, {"kor*", "circle"},
    {"gomb*", "sphere"}, {"henger*", "cylinder"}, {"kocka*", "cube"}, {"teglalap*", "rectangle"},
    {"negyzet*", "square"}, {"haromszog*", "triangle"}, {"atfogo*", "hypotenuse"},
    {"sugar*", "radius"}, {"atmero*", "diameter"}, {"paros", "is even"}, {"paratlan", "is odd"},
    {"lnko", "gcd"}, {"lkkt", "lcm"}, {"borravalo*", "tip"}, {"kedvezmeny*", "discount"},
    {"masodfoku", "quadratic"}, {"fibonacci*", "fibonacci"},
};

static void hu_prepare(char* out, size_t cap, const char* in) {
    char t[BUF_LEN + 40];
    size_t n = 0;
    t[n++] = ' ';
    for(size_t i = 0; in[i] && n + 3 < sizeof(t); i++) {
        char c = (char)tolower((unsigned char)in[i]);
        char nx = in[i + 1];
        if(c == ',' && i > 0 && isdigit((unsigned char)in[i - 1]) && isdigit((unsigned char)nx)) {
            c = '.'; // Hungarian decimal comma: 3,5 -> 3.5
        } else if(c == ',' || c == '?' || c == '!' || c == ';') {
            c = ' ';
        } else if(c == '-' && i > 0 && isalpha((unsigned char)in[i - 1]) && isalpha((unsigned char)nx)) {
            if(nx == 'e' && !isalnum((unsigned char)in[i + 2])) { // "primszam-e"
                i++;
                continue;
            }
            c = ' ';
        }
        t[n++] = c;
    }
    t[n++] = ' ';
    t[n] = 0;
    repl_all(t, sizeof(t), " szamold ki ", " calculate ");
    repl_all(t, sizeof(t), " szamitsd ki ", " calculate ");
    repl_all(t, sizeof(t), " oldd meg ", " solve ");
    repl_all(t, sizeof(t), " valtsd at ", " convert ");
    repl_all(t, sizeof(t), " legnagyobb kozos oszto ", " gcd ");
    repl_all(t, sizeof(t), " legkisebb kozos tobbszoros ", " lcm ");
    size_t j = 0;
    for(char* tok = strtok(t, " "); tok; tok = strtok(NULL, " ")) {
        const char* r = tok;
        bool alpha = true;
        for(const char* c = tok; *c; c++)
            if(!isalpha((unsigned char)*c)) alpha = false;
        if(alpha) {
            size_t tl = strlen(tok);
            for(size_t k = 0; k < sizeof(hu_map) / sizeof(hu_map[0]); k++) {
                const char* h = hu_map[k].hu;
                size_t hl = strlen(h);
                bool stem = hl && h[hl - 1] == '*';
                if(stem) hl--;
                bool hit = stem ? (tl >= hl && tl <= hl + (hl <= 4 ? 3 : 5) && !strncmp(tok, h, hl)) :
                                  (tl == hl && !strcmp(tok, h));
                if(hit) {
                    r = hu_map[k].en;
                    break;
                }
            }
        }
        size_t rl = strlen(r);
        if(j + rl + 2 >= cap) break;
        if(j) out[j++] = ' ';
        memcpy(out + j, r, rl);
        j += rl;
    }
    out[j] = 0;
}

typedef struct {
    const char* w;
    int v;
} NumW;
static const NumW numw[] = {
    {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7},
    {"eight", 8}, {"nine", 9}, {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13},
    {"fourteen", 14}, {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18},
    {"nineteen", 19}, {"twenty", 20}, {"thirty", 30}, {"forty", 40}, {"fifty", 50}, {"sixty", 60},
    {"seventy", 70}, {"eighty", 80}, {"ninety", 90}, {"hundred", 100}, {"thousand", 1000},
    {"nulla", 0}, {"ketto", 2}, {"harom", 3}, {"negy", 4}, {"nyolc", 8}, {"kilenc", 9}, {"tiz", 10},
    {"husz", 20}, {"harminc", 30}, {"negyven", 40}, {"otven", 50}, {"hatvan", 60}, {"hetven", 70},
    {"nyolcvan", 80}, {"kilencven", 90}, {"szaz", 100}, {"ezer", 1000},
};

// "twenty five plus thirty" -> "25 plus 30"   (only when the text has no digits at all)
static void numwords(char* buf, size_t cap) {
    for(const char* c = buf; *c; c++)
        if(isdigit((unsigned char)*c)) return;
    char t[BUF_LEN + 40];
    strlcpy(t, buf, sizeof(t));
    size_t j = 0;
    long cur = 0;
    bool have = false;
    for(char* tok = strtok(t, " "); tok; tok = strtok(NULL, " ")) {
        int v = -1;
        for(size_t k = 0; k < sizeof(numw) / sizeof(numw[0]); k++)
            if(!strcmp(tok, numw[k].w)) {
                v = numw[k].v;
                break;
            }
        if(v < 0) { // Hungarian compounds: tizenot, huszonketto, harmincnyolc ...
            static const NumW tens[] = {{"tizen", 10}, {"huszon", 20}, {"harminc", 30}, {"negyven", 40}, {"otven", 50}, {"hatvan", 60}, {"hetven", 70}, {"nyolcvan", 80}, {"kilencven", 90}};
            static const NumW ones[] = {{"egy", 1}, {"ketto", 2}, {"harom", 3}, {"negy", 4}, {"ot", 5}, {"hat", 6}, {"het", 7}, {"nyolc", 8}, {"kilenc", 9}};
            for(size_t k = 0; k < 9 && v < 0; k++) {
                size_t tl2 = strlen(tens[k].w);
                if(strncmp(tok, tens[k].w, tl2) != 0) continue;
                for(size_t m2 = 0; m2 < 9; m2++)
                    if(!strcmp(tok + tl2, ones[m2].w)) {
                        v = tens[k].v + ones[m2].v;
                        break;
                    }
            }
        }
        if(v >= 0) {
            if(v == 100 || v == 1000) cur = (cur ? cur : 1) * v;
            else cur += v;
            have = true;
            continue;
        }
        if(have) {
            j += (size_t)snprintf(buf + j, cap - j, "%s%ld", j ? " " : "", cur);
            have = false;
            cur = 0;
        }
        size_t tl = strlen(tok);
        if(j + tl + 2 >= cap) break;
        if(j) buf[j++] = ' ';
        memcpy(buf + j, tok, tl);
        j += tl;
    }
    if(have && j + 14 < cap) j += (size_t)snprintf(buf + j, cap - j, "%s%ld", j ? " " : "", cur);
    buf[j < cap ? j : cap - 1] = 0;
}

/* ================= fun content ================= */
static const char* const jokes[] = {
    "Why do programmers prefer dark mode? Because light attracts bugs.",
    "I would tell you a UDP joke, but you might not get it.",
    "Why was the math book sad? It had too many problems.",
    "There are 10 kinds of people: those who understand binary and those who don't.",
    "Why did the Flipper cross the road? To sniff the other side's sub-GHz.",
    "A SQL query walks into a bar, sees two tables and asks: can I join you?",
    "Parallel lines have so much in common. It's a shame they'll never meet.",
    "Why is 6 afraid of 7? Because 7 8 9.",
    "I'm reading a book on anti-gravity. It's impossible to put down.",
    "What do you call a fake noodle? An impasta.",
    "Why don't scientists trust atoms? They make up everything.",
    "My code has no bugs, only undocumented features.",
    "How does a programmer shower? Shampoo, rinse, repeat.",
    "Why was the equal sign humble? It knew it wasn't less than or greater than anyone.",
    "Why did the computer go to the doctor? It had a virus.",
    "I asked the computer for a break. It said: sure, going to sleep.",
};
static const char* const facts[] = {
    "Honey never spoils. Edible honey was found in ancient Egyptian tombs.",
    "A day on Venus is longer than its year.",
    "Octopuses have three hearts and blue blood.",
    "Bananas are slightly radioactive because of potassium-40.",
    "The Eiffel Tower grows about 15 cm taller in summer heat.",
    "There are more possible chess games than atoms in the observable universe.",
    "Lightning is about five times hotter than the surface of the Sun.",
    "Hungary's Rubik's Cube inventor Erno Rubik took a month to solve his own puzzle.",
    "Water can boil and freeze at once at the triple point (0.01 C, low pressure).",
    "Your brain uses about 20% of your body's energy while being 2% of its weight.",
    "A teaspoon of neutron star would weigh about a billion tonnes.",
    "Sharks are older than trees: they appeared about 450 million years ago.",
    "The first computer bug was a real moth found in a Harvard Mark II in 1947.",
    "Light from the Sun takes about 8 minutes 20 seconds to reach Earth.",
    "A group of flamingos is called a flamboyance.",
    "A day on Mercury (sunrise to sunrise) lasts about 176 Earth days.",
    "Wombat droppings are cube-shaped.",
    "Venus spins backwards compared to most planets.",
    "Earth has roughly 3 trillion trees - more than stars in the Milky Way.",
    "Sound travels about four times faster in water than in air.",
    "The first underground railway on mainland Europe opened in Budapest in 1896.",
    "Lake Balaton is the largest lake in Central Europe.",
    "Antarctica is the largest desert on Earth.",
    "Water expands by about 9% when it freezes, so ice floats.",
};
static const char* const quotes[] = {
    "The only way to do great work is to love what you do. - Steve Jobs",
    "Stay hungry, stay foolish. - Whole Earth Catalog",
    "Well begun is half done. - Aristotle",
    "It always seems impossible until it's done. - Nelson Mandela",
    "Simplicity is the ultimate sophistication. - Leonardo da Vinci (attributed)",
    "The best way to predict the future is to invent it. - Alan Kay",
    "Whether you think you can or can't, you're right. - Henry Ford (attributed)",
    "A journey of a thousand miles begins with a single step. - Lao Tzu",
    "The unexamined life is not worth living. - Socrates",
    "I think, therefore I am. - Descartes",
    "Knowledge is power. - Francis Bacon",
    "Fall seven times, stand up eight. - Japanese proverb",
    "Imagination is more important than knowledge. - Albert Einstein",
};
static const char* const st_who[] = {"a curious hacker", "an old robot", "a young pilot", "a lost courier", "a quiet inventor"};
static const char* const st_where[] = {"a neon-lit city", "an abandoned space station", "a forest that hums at night", "a mountain village", "an underground library"};
static const char* const st_twist[] = {"a message arrived from the future", "every clock stopped at once", "a door appeared where no door had been", "the map began to redraw itself", "a tiny device started to whisper"};

#define PICK(arr) arr[rnd(sizeof(arr) / sizeof(arr[0]))]

/* ================= small talk & device info ================= */
static const char* const jokes_hu[] = {
    "Miert fazik a szamitogep? Mert kinyitotta az ablakokat.",
    "Mit mond a nulla a nyolcasnak? Szep az ov!",
    "Miert szomoru a matekkonyv? Mert tele van problemakkal.",
    "Ketfele ember van: aki erti a binarist, es aki nem.",
    "A parhuzamos egyenesek kozott sok a kozos. Kar, hogy sosem talalkoznak.",
    "A kodomban nincs hiba, csak dokumentalatlan funkciok.",
    "Mit csinal a programozo a zuhany alatt? Samponoz, oblit, ismetel.",
    "Mi az algoritmusok kedvenc zenei stilusa? Az algo-ritmus.",
};
static const char* const facts_hu[] = {
    "A Rubik-kockat Rubik Erno talalta fel 1974-ben Budapesten.",
    "A Balaton Kozep-Europa legnagyobb tava.",
    "Budapesten 1896-ban nyilt meg a kontinentalis Europa elso foldalatti vasutja.",
    "A golyostollat Biro Laszlo talalta fel.",
    "Szent-Gyorgyi Albert paprikabol vonta ki a C-vitamint.",
    "A hologramot Gabor Denes talalta fel 1947-ben.",
    "A Nap fenye kb. 8 perc 20 masodperc alatt er a Foldre.",
    "A polipnak harom szive es kek verre van.",
    "A mez gyakorlatilag sosem romlik meg.",
    "A Venuszon hosszabb egy nap, mint egy ev.",
    "A banan enyhen radioaktiv a kalium-40 miatt.",
    "Az emberi agy a testsuly kb. 2%-a, de az energia kb. 20%-at hasznalja.",
    "Az Eiffel-torony nyaron a hotagulas miatt akar 15 cm-rel is magasabb.",
    "A capak regebbiek a faknal: kb. 450 millio eve jelentek meg.",
    "Jedlik Anyos az 1820-as evekben talalta fel a szodavizet.",
    "Neumann Janos a modern szamitogepek egyik atyja.",
    "A viz fagyaskor kb. 9%-kal tagul, ezert uszik a jeg.",
    "Az Antarktisz a vilag legnagyobb sivataga.",
};
static const char* const quotes_hu[] = {
    "Aki mer, az nyer.",
    "Lassan jarj, tovabb ersz. - magyar kozmondas",
    "Ismetles a tudas anyja. - magyar kozmondas",
    "Ki koran kel, aranyat lel. - magyar kozmondas",
    "Mondottam, ember: kuzdj es bizva bizzal! - Madach Imre",
    "Mindenki a maga szerencsejenek kovacsa. - kozmondas",
    "Ezer merfoldes ut is egyetlen lepessel kezdodik. - Lao-ce",
};
static const char* const st_who_hu[] = {"egy kivancsi hacker", "egy oreg robot", "egy fiatal pilota", "egy eltevedt futar", "egy csendes feltalalo"};
static const char* const st_where_hu[] = {"egy neonfenyes varos", "egy elhagyott urallomas", "egy erdo, amely ejjel zsong", "egy hegyi falu", "egy foldalatti konyvtar"};
static const char* const st_twist_hu[] = {"uzenet erkezett a jovobol", "minden ora megallt", "ajto jelent meg ott, ahol soha nem volt", "a terkep ujrarajzolta magat", "egy apro keszulek suttogni kezdett"};

static bool exact_any(const char* q, const char* const* words, size_t count) {
    for(size_t i = 0; i < count; i++)
        if(!strcmp(q, words[i])) return true;
    return false;
}
#define EXACT_ANY(q, arr) exact_any((q), (arr), sizeof(arr) / sizeof((arr)[0]))

static bool do_smalltalk(App* a, const char* q, const char* raw) {
    UNUSED(raw);
    FuriString* o = a->out;
    const char* hi[] = {"hello", "hi", "hey", "hey there", "good morning", "good evening", "good afternoon", "yo", "sup", "szia", "udv", "udvozlet", "jo napot", "jo reggelt", "jo estet", "jo delutant", "szervusz", "szevasz", "sziasztok", "csao", "hali", "szia blivzy", "hello blivzy"};
    const char* bye[] = {"bye", "goodbye", "see you", "see you later", "see ya", "good night", "viszlat", "viszontlatasra", "jo ejszakat", "szia szia", "csumi", "pa pa"};
    const char* thanks[] = {"thanks", "thank you", "thx", "cheers", "koszi", "koszonom", "koszonjuk", "nagyon szep koszonet"};
    const char* sorry[] = {"sorry", "my bad", "bocs", "bocsi", "bocsanat", "elnezest"};
    const char* praise[] = {"good bot", "well done", "nice one", "great job", "good job", "ugyes", "remek", "szuper", "jo vagy", "kiraly", "tokeletes"};
    const char* bored[] = {"i am bored", "im bored", "bored", "unatkozom", "unalmas"};
    const char* who[] = {"who are you", "what are you", "your name", "who made you", "ki vagy", "mi vagy", "who built you", "hogy hivnak", "mi a neved", "ki keszitett"};
    const char* helpw[] = {"help", "what can you do", "commands", "features", "segitseg", "mit tudsz", "mit kerdezhetek", "sugo"};
    const char* howru[] = {"how are you", "how are you doing", "hogy vagy", "whats up", "what's up", "mizu", "hogy megy sorod", "mi ujsag"};
    const char* jokew[] = {"joke", "make me laugh", "funny", "vicc", "meselj egy viccet", "mondj egy viccet", "nevettess meg"};
    const char* factw[] = {"fun fact", "random fact", "tell me a fact", "interesting fact", "erdekesseg", "erdekes tenyek", "mondj egy erdekesseget", "tenyek"};
    const char* quotew[] = {"quote", "inspire me", "motivation", "motivate me", "idezet", "motivalj", "mondj egy idezetet"};
    const char* timew[] = {"what time is it", "current time", "time now", "what is the time", "mennyi az ido", "hany ora van", "hany ora", "pontos ido", "mennyi az ora"};
    const char* datew[] = {"what day is it", "todays date", "what is the date", "date today", "current date", "milyen nap van", "milyen napunk van", "mai datum", "hanyadika van ma", "melyik nap van"};
    const char* batw[] = {"battery", "charge level", "how much battery", "akku", "akkumulator"};
    const char* coinw[] = {"flip a coin", "coin flip", "heads or tails", "toss a coin", "fej vagy iras", "dobj fel egy erment", "fejet vagy irast"};
    const char* dicew[] = {"roll a dice", "roll dice", "roll a die", "kocka dobas", "dobj kockaval", "dobj egy kockat", "dobj kockat", "kockadobas"};
    const char* randw[] = {"random number", "pick a number", "veletlen szam", "valassz egy szamot", "random szam"};
    const char* storyw[] = {"tell me a story", "short story", "story starter", "mese", "meset", "tortenet", "kezdj egy tortenetet"};
    const char* nmq[] = {"what is my name", "whats my name", "do you know my name", "mi a nevem", "tudod a nevem", "hogy hivnak engem"};
    const char* ncue[] = {"my name is ", "call me ", "a nevem ", "nevem ", "hivj "};

    size_t ql = strlen(q);
    // remember the user's name
    for(size_t i = 0; i < sizeof(ncue) / sizeof(ncue[0]); i++) {
        size_t cl = strlen(ncue[i]);
        if(!strncmp(q, ncue[i], cl) && q[cl] && ql < cl + 15 && !strchr(q + cl, ' ')) {
            strlcpy(a->name, q + cl, sizeof(a->name));
            a->name[0] = (char)toupper((unsigned char)a->name[0]);
            furi_string_printf(o, T(a, "Nice to meet you, %s! I'll use your name while this app is open.", "Orulok, %s! Amig az app nyitva van, igy fogok szolitani."), a->name);
            return true;
        }
    }
    if(HAS_ANY(q, nmq) && ql < 32) {
        if(a->name[0]) furi_string_printf(o, T(a, "Your name is %s.", "A neved %s."), a->name);
        else furi_string_set_str(o, T(a, "You haven't told me yet. Say: my name is Sam", "Meg nem mondtad. Irj: a nevem Anna"));
        return true;
    }
    if(ql <= 22 && HAS_ANY(q, hi) && !has_word(q, "time") && !has_word(q, "date")) {
        const char* gh[] = {"I'm Blivzy AI. Ask me maths, science, conversions, or ask for an essay.", "What shall we figure out today?", "Type a question, a sum, or say help."};
        const char* gu[] = {"Blivzy AI vagyok. Kerdezz matekot, tudomanyt, atvaltast, vagy kerj esszet.", "Miben segithetek?", "Irj egy kerdest, egy szamitast, vagy azt: segitseg."};
        uint32_t k = rnd(3);
        furi_string_printf(o, "%s%s%s! %s", T(a, "Hello", "Szia"), a->name[0] ? ", " : "", a->name[0] ? a->name : "", a->hu ? gu[k] : gh[k]);
        return true;
    }
    if(ql <= 24 && HAS_ANY(q, bye)) {
        furi_string_set_str(o, T(a, "Bye! Come back any time - I work offline.", "Viszlat! Gyere vissza barmikor, offline is itt vagyok."));
        return true;
    }
    if(ql <= 30 && HAS_ANY(q, thanks)) {
        furi_string_set_str(o, T(a, "You're welcome! Ask me anything else.", "Szivesen! Kerdezz barmit."));
        return true;
    }
    if(ql <= 18 && HAS_ANY(q, sorry)) {
        furi_string_set_str(o, T(a, "No problem at all!", "Semmi gond!"));
        return true;
    }
    if(ql <= 20 && HAS_ANY(q, praise)) {
        furi_string_set_str(o, T(a, "Thank you! Beep boop. What next?", "Koszi! Bip-bop. Mi a kovetkezo?"));
        return true;
    }
    if(ql <= 22 && HAS_ANY(q, bored)) {
        furi_string_set_str(o, T(a, "Try: tell me a joke - fun fact - roll a dice - a story - solve 3x+4=19 - or ask why the sky is blue.", "Probald: vicc - erdekesseg - kocka dobas - mese - oldd meg 3x+4=19 - vagy kerdezd: miert kek az eg."));
        return true;
    }
    if(HAS_ANY(q, who)) {
        furi_string_set_str(o, T(a, "I'm Blivzy AI v" VERSION ", made by Blivzy. I run fully offline on your Flipper Zero: I do maths, conversions, equations, writing templates and I search a local knowledge base in English and Hungarian. I'm not a cloud AI, so my answers come from my built-in tools and KB files.", "Blivzy AI v" VERSION " vagyok, keszitette a Blivzy. Teljesen offline futok a Flipper Zero-n: matek, atvaltasok, egyenletek, irasi sablonok es helyi tudastar magyarul es angolul. Nem felho-AI vagyok: a valaszaim a beepitett eszkozokbol es a KB fajlokbol szarmaznak."));
        return true;
    }
    if(ql <= 26 && HAS_ANY(q, howru)) {
        furi_string_set_str(o, T(a, "Running at 64 MHz and feeling electric. How can I help?", "Remekul, 64 MHz-en porgok. Miben segithetek?"));
        return true;
    }
    if(ql <= 34 && HAS_ANY(q, helpw)) {
        furi_string_set_str(o, T(a, "TRY ASKING:\n- 25*48+7, 15% of 80, sqrt 144\n- twelve plus thirty\n- solve 2x+3=11\n- convert 5 km to miles\n- is 97 prime / fibonacci 20\n- area of circle radius 5\n- average of 4 8 15 16\n- write an essay about space\n- what is photosynthesis\n- why is the sky blue\n- my name is Sam\n- time / battery / roll dice\n- joke / fun fact / quote\n- more (related topic) / again\nUse # on the keyboard for symbols.", "KERDEZD:\n- 25*48+7, 15 szazaleka 80, gyok 144\n- harminc plusz negy\n- oldd meg 2x+3=11\n- 5 km merfoldben\n- primszam-e 97\n- kor terulete sugar 5\n- atlag 4 8 15 16\n- irj esszet a vilagurrol\n- mi a fotoszintezis\n- miert kek az eg\n- a nevem Anna\n- ido / akku / kocka dobas\n- vicc / erdekesseg / idezet\n- tovabb / megint\nA # gomb: szimbolumok, majd ekezetes betuk. Ekezettel is irhatsz."));
        return true;
    }
    if(HAS_ANY(q, jokew) && ql < 40) {
        furi_string_set_str(o, a->hu ? PICK(jokes_hu) : PICK(jokes));
        return true;
    }
    if(HAS_ANY(q, factw) && ql < 40) {
        furi_string_set_str(o, a->hu ? PICK(facts_hu) : PICK(facts));
        return true;
    }
    if(HAS_ANY(q, quotew) && ql < 40) {
        furi_string_set_str(o, a->hu ? PICK(quotes_hu) : PICK(quotes));
        return true;
    }
    if(HAS_ANY(q, storyw) && ql < 40 && !has_word(q, "essay") && !has_word(q, "esszet")) {
        if(a->hu) furi_string_printf(o, "Mesekezdet:\n\nEgyszer volt, hol nem volt: %s elt %s kozeleben. Egyik este %s. Meg senki sem tudta, de ez csak a kezdet volt...", PICK(st_who_hu), PICK(st_where_hu), PICK(st_twist_hu));
        else furi_string_printf(o, "Story starter:\n\nOnce, %s lived near %s. One evening, %s. Nobody knew it yet, but that was only the beginning...", PICK(st_who), PICK(st_where), PICK(st_twist));
        return true;
    }
    if((HAS_ANY(q, timew) || !strcmp(q, "time") || !strcmp(q, "ido") || !strcmp(q, "ora")) && ql < 36 && !has_word(q, "convert")) {
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        furi_string_printf(o, "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
        return true;
    }
    if((HAS_ANY(q, datew) || !strcmp(q, "date") || !strcmp(q, "datum")) && ql < 36) {
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        static const char* const wd[] = {"?", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
        static const char* const wdh[] = {"?", "Hetfo", "Kedd", "Szerda", "Csutortok", "Pentek", "Szombat", "Vasarnap"};
        uint8_t w = dt.weekday < 8 ? dt.weekday : 0;
        furi_string_printf(o, "%s\n%04u-%02u-%02u", a->hu ? wdh[w] : wd[w], dt.year, dt.month, dt.day);
        return true;
    }
    if(HAS_ANY(q, batw) && ql < 40) {
        furi_string_printf(o, "%s: %u%%", T(a, "Battery", "Akku"), (unsigned)furi_hal_power_get_pct());
        return true;
    }
    if((HAS_ANY(q, coinw) || !strcmp(q, "coin") || !strcmp(q, "erme")) && ql < 30) {
        furi_string_set_str(o, rnd(2) ? T(a, "Heads", "Fej") : T(a, "Tails", "Iras"));
        return true;
    }
    if((HAS_ANY(q, dicew) || !strcmp(q, "dice") || !strcmp(q, "kocka") || !strncmp(q, "roll d", 6)) && ql < 30) {
        double v[2];
        int n = extract_nums(q, v, 2);
        uint32_t sides = (n && v[0] >= 2 && v[0] <= 1000) ? (uint32_t)v[0] : 6;
        furi_string_printf(o, T(a, "Rolled d%lu: %lu", "Dobas (d%lu): %lu"), (unsigned long)sides, (unsigned long)(rnd(sides) + 1));
        return true;
    }
    if((HAS_ANY(q, randw) || !strcmp(q, "random")) && ql < 40) {
        double v[2];
        int n = extract_nums(raw, v, 2);
        long lo = 1, hi2 = 100;
        if(n == 1) hi2 = (long)v[0];
        if(n >= 2) {
            lo = (long)v[0];
            hi2 = (long)v[1];
        }
        if(hi2 < lo) {
            long t = lo;
            lo = hi2;
            hi2 = t;
        }
        furi_string_printf(o, T(a, "Random %ld..%ld: %ld", "Veletlen %ld..%ld: %ld"), lo, hi2, lo + (long)rnd((uint32_t)(hi2 - lo + 1)));
        return true;
    }
    return false;
}

/* ================= writing helper ================= */
static void extract_topic(char* out, size_t cap, const char* in) {
    char t[BUF_LEN];
    lower_copy(t, sizeof(t), in);
    const char* cues[] = {" about ", " on ", " for ", " regarding ", " the topic of "};
    const char* p = NULL;
    for(size_t i = 0; i < 5 && !p; i++) {
        const char* f = strstr(t, cues[i]);
        if(f) p = f + strlen(cues[i]);
    }
    if(!p) {
        p = t;
        const char* lead[] = {"write ", "give me ", "make ", "create ", "an ", "a ", "essay ", "paragraph ", "email ", "speech ", "outline ", "pros and cons ", "story ", "please ", "of ", "the ", "irj ", "esszet ", "bekezdest ", "emailt ", "beszedet ", "verset ", "vazlatot ", "tezist ", "levelet ", "level ", "fogalmazz ", "keszits ", "elonyok es hatranyok: ", "elonyok es hatranyok ", "mellette es ellene ", "egy ", "a ", "az "};
        bool moved = true;
        while(moved) {
            moved = false;
            for(size_t i = 0; i < sizeof(lead) / sizeof(lead[0]); i++) {
                size_t n = strlen(lead[i]);
                if(strncmp(p, lead[i], n) == 0) {
                    p += n;
                    moved = true;
                }
            }
        }
    }
    strlcpy(out, p, cap);
    size_t l = strlen(out);
    while(l && (out[l - 1] == ' ' || out[l - 1] == '.' || out[l - 1] == '?')) out[--l] = 0;
    if(!out[0]) strlcpy(out, "my topic", cap);
}

static bool kb_fact(App* a, const char* topic, FuriString* out);

static bool do_writing(App* a, const char* q, const char* raw) {
    const char* essayw[] = {"essay", "esszet"};
    const char* parw[] = {"paragraph", "bekezdes", "bekezdest"};
    const char* mailw[] = {"email", "emailt", "e mail", "letter", "level", "levelet"};
    const char* proconw[] = {"pros and cons", "advantages and disadvantages", "for and against", "elonyok es hatranyok", "elonyei es hatranyai", "mellette es ellene"};
    const char* outw[] = {"outline", "plan for", "structure for", "vazlat", "vazlatot"};
    const char* speechw[] = {"speech", "presentation intro", "beszed", "beszedet"};
    const char* thesisw[] = {"thesis", "thesis statement", "tezis", "tezist", "tezismondat"};
    const char* poemw[] = {"poem", "haiku", "vers", "verset", "versezet"};
    if(!(has_word(q, "write") || has_word(q, "essay") || has_word(q, "outline") || has_word(q, "draft") || has_word(q, "irj") || has_word(q, "fogalmazz") || has_word(q, "keszits") ||
         HAS_ANY(q, proconw) || HAS_ANY(q, thesisw)))
        return false;
    if(!strncmp(q, "how ", 4) || !strncmp(q, "what ", 5) || !strncmp(q, "why ", 4) || !strncmp(q, "hogyan ", 7)) return false;
    char tp[BUF_LEN];
    extract_topic(tp, sizeof(tp), raw);
    const char* t = tp;
    FuriString* o = a->out;
    FuriString* fact = furi_string_alloc();
    bool has_fact = false;
    if(HAS_ANY(q, essayw) || HAS_ANY(q, parw)) has_fact = kb_fact(a, t, fact);
    bool done = true;
    if(has_fact && HAS_ANY(q, parw)) {
        furi_string_printf(o, "%s: %s\n\n%s %s", a->hu ? "BEKEZDES" : "PARAGRAPH", t, furi_string_get_cstr(fact),
            a->hu ? "Ez azert fontos, mert [jelentoseg]. Peldaul [pelda]. Ezert ez a tema figyelmet erdemel." : "This matters because [significance]. For example, [evidence]. Therefore it deserves careful attention.");
    } else if(has_fact && HAS_ANY(q, essayw)) {
        if(a->hu)
            furi_string_printf(o, "ESSZE: %s\n\n[1] Bevezetes\n%s temaja sokakat foglalkoztat. Ebben az esszeben bemutatom, mit jelent, miert fontos, es mit tanulhatunk belole.\n\n[2] Hatter\n%s\n\n[3] Jelentoseg\n%s azert fontos, mert [ok 1] es [ok 2]. Peldaul [pelda].\n\n[4] Ellenerv\nVannak, akik szerint [ellenerv]. Ennek ellenere [valaszod].\n\n[5] Befejezes\nOsszegezve, %s azert fontos, mert [fo ok]. [Zaro gondolat.]\n\n(a hatter a tudastarbol szarmazik)", t, t, furi_string_get_cstr(fact), t, t);
        else
            furi_string_printf(o, "ESSAY: %s\n\n[1] Introduction\nFew subjects are as worth exploring as %s. In this essay I will explain what it is, why it matters, and what it teaches us. Thesis: %s shapes how we think and act because [main reason].\n\n[2] Body 1 - Background\n%s\n\n[3] Body 2 - Importance\nNext, %s matters because [reason 1] and [reason 2]. For example, [real example].\n\n[4] Body 3 - Other view\nSome people argue that [counterargument]. However, [your reply with evidence].\n\n[5] Conclusion\nIn conclusion, %s is important because [main reason]. Looking ahead, [final thought].\n\n(background filled from my knowledge base)", t, t, t, furi_string_get_cstr(fact), t, t);
    } else done = false;
    furi_string_free(fact);
    if(done) return true;
    if(a->hu && HAS_ANY(q, proconw)) {
        furi_string_printf(o, "ELONYOK ES HATRANYOK: %s\n\nELONYOK\n+ [elony 1]\n+ [elony 2]\n+ [hosszu tavu nyereseg]\n\nHATRANYOK\n- [kockazat vagy koltseg 1]\n- [kockazat vagy koltseg 2]\n- [kit erinthet hatranyosan]\n\nOSSZEGZES\nMerd meg, melyik oldal fontosabb szamodra, es miert.", t);
    } else if(a->hu && HAS_ANY(q, thesisw)) {
        furi_string_printf(o, "TEZIS-OTLETEK: %s\n\n1) %s azert fontos, mert [ok 1], [ok 2] es [ok 3].\n2) Bar egyesek ketelkednek: %s, a bizonyitekok szerint [allitasod].\n3) Ahhoz, hogy megertsuk ezt: %s, meg kell vizsgalnunk az okait, hatasait es jovojet.", t, t, t, t);
    } else if(a->hu && HAS_ANY(q, outw)) {
        furi_string_printf(o, "VAZLAT: %s\n\nI. Bevezetes\n   A. Felkelto kezdes\n   B. Hatter: %s\n   C. Tezis\nII. Elso fo gondolat - [otlet]\n   A. Bizonyitek  B. Magyarazat\nIII. Masodik fo gondolat - [otlet]\n   A. Bizonyitek  B. Magyarazat\nIV. Ellenerv es valasz\nV. Befejezes\n   A. Tezis ujrafogalmazasa  B. Zaro gondolat", t, t);
    } else if(a->hu && HAS_ANY(q, speechw)) {
        furi_string_printf(o, "BESZED: %s\n\nFelvezetes: Kepzeld el a vilagot ezek nelkul: %s...\nUdvozles: Jo napot kivanok mindenkinek, ma errol szeretnek beszelni: %s.\n1. pont: Mi ez?\n2. pont: Miert fontos nekunk?\n3. pont: Mit tehetunk?\nZaras: Ne feledjuk: %s ertekes tema. Koszonom a figyelmet!", t, t, t, t);
    } else if(a->hu && HAS_ANY(q, mailw)) {
        furi_string_printf(o, "Targy: %s\n\nTisztelt [Nev]!\n\nRemelem, jol van. Az alabbi ugyben irok: %s. [Egy mondatban irja le a celt.]\n\n[Fo reszletek: ki, mit, mikor.]\n\nKerem, [konkret keres] [hatarido] elott. Szivesen rendelkezesre allok barmi mas informacioval.\n\nUdvozlettel,\n[Az On neve]", t, t);
    } else if(a->hu && HAS_ANY(q, parw)) {
        furi_string_printf(o, "BEKEZDES: %s\n\n%s lenyege, hogy [meghatarozas]. Egyik fontos pont [fo gondolat], ami [magyarazat]. Peldaul [bizonyitek]. Ez azert lenyeges, mert [jelentoseg]. Ezert ez a tema komoly figyelmet erdemel: %s.", t, t, t);
    } else if(a->hu && HAS_ANY(q, poemw)) {
        furi_string_printf(o, "Vers: %s\n\nCsendes gondolat, %s,\naz ido lassan, fenyesen jar;\na kis dolgok tanitjak meg:\nhogyan fogadjuk az estet es a ma.", t, t);
    } else if(HAS_ANY(q, proconw)) {
        furi_string_printf(o, "PROS & CONS: %s\n\nPROS\n+ [benefit 1]\n+ [benefit 2]\n+ [long-term gain]\n\nCONS\n- [risk or cost 1]\n- [risk or cost 2]\n- [who could be hurt]\n\nVERDICT\nWeigh which side matters more to you and why.", t);
    } else if(HAS_ANY(q, thesisw)) {
        furi_string_printf(o, "THESIS IDEAS: %s\n\n1) %s matters because [reason 1], [reason 2] and [reason 3].\n2) Although some doubt %s, evidence shows [your claim].\n3) To understand %s we must examine its causes, effects and future.", t, t, t, t);
    } else if(HAS_ANY(q, outw)) {
        furi_string_printf(o, "OUTLINE: %s\n\nI. Introduction\n   A. Hook\n   B. Background on %s\n   C. Thesis\nII. Main point 1 - [idea]\n   A. Evidence  B. Explanation\nIII. Main point 2 - [idea]\n   A. Evidence  B. Explanation\nIV. Counterargument and reply\nV. Conclusion\n   A. Restate thesis  B. Closing thought", t, t);
    } else if(HAS_ANY(q, speechw)) {
        furi_string_printf(o, "SPEECH: %s\n\nHook: Imagine a world without %s...\nGreeting: Good day everyone, today I'd like to talk about %s.\nPoint 1: What it is.\nPoint 2: Why it matters to us.\nPoint 3: What we can do.\nClose: So remember - %s is worth caring about. Thank you.", t, t, t, t);
    } else if(HAS_ANY(q, mailw)) {
        furi_string_printf(o, "Subject: %s\n\nDear [Name],\n\nI hope you are well. I am writing about %s. [State your purpose in one sentence.]\n\n[Give the key details: who, what, when.]\n\nCould you please [specific request] by [date]? I'm happy to provide anything else you need.\n\nKind regards,\n[Your name]", t, t);
    } else if(HAS_ANY(q, parw)) {
        furi_string_printf(o, "PARAGRAPH: %s\n\n%s is [definition]. One key point is [main idea], which shows [explanation]. For example, [evidence]. This matters because [significance]. Therefore, %s deserves careful attention.", t, t, t);
    } else if(HAS_ANY(q, poemw)) {
        furi_string_printf(o, "Verse about %s:\n\nA quiet thought of %s,\nthe light turns slow and bright;\nwhat small things teach us all\nis how to meet the night.", t, t);
    } else if(HAS_ANY(q, essayw) || has_word(q, "write")) {
        if(a->hu) {
            furi_string_printf(o, "ESSZE: %s\n\n[1] Bevezetes\n%s temaja sokakat foglalkoztat. Ebben az esszeben bemutatom, mit jelent, miert fontos, es mit tanulhatunk belole.\n\n[2] Hatter\nEloszor erdemes megismerni, honnan ered %s. [Meghatarozas, evszam, tenyek.]\n\n[3] Jelentoseg\n%s azert fontos, mert [ok 1] es [ok 2]. Peldaul [pelda].\n\n[4] Ellenerv\nVannak, akik szerint [ellenerv]. Ennek ellenere [valaszod].\n\n[5] Befejezes\nOsszegezve, %s azert fontos, mert [fo ok]. [Zaro gondolat.]", t, t, t, t, t);
        } else {
            furi_string_printf(o, "ESSAY: %s\n\n[1] Introduction\nFew subjects are as worth exploring as %s. In this essay I will explain what it is, why it matters, and what it teaches us. Thesis: %s shapes how we think and act because [main reason].\n\n[2] Body 1 - Background\nTo begin, it helps to know where %s comes from. [Add a definition, a date or a key fact.]\n\n[3] Body 2 - Importance\nNext, %s matters because [reason 1] and [reason 2]. For example, [real example].\n\n[4] Body 3 - Other view\nSome people argue that [counterargument]. However, [your reply with evidence].\n\n[5] Conclusion\nIn conclusion, %s is important because [main reason]. Looking ahead, [final thought].\n\nTip: replace each [bracket] with your own facts.", t, t, t, t, t, t);
        }
    } else {
        return false;
    }
    return true;
}

/* ================= offline knowledge search ================= */
typedef struct {
    int best, second;
    FuriString* ans;
    char best_topic[32];
    char second_topic[32];
    bool no_sample;
    // reservoir of topics for the "try asking" fallback
    char sample[4][32];
    uint32_t seen;
} Match;

// every word of the keyword must appear in the question, allowing small typos
static bool kw_fuzzy(const char* kw, const char* qw[], size_t nqw, const size_t qwl[]) {
    char tmp[48];
    strlcpy(tmp, kw, sizeof(tmp));
    bool any = false;
    for(char* tok = strtok(tmp, " "); tok; tok = strtok(NULL, " ")) {
        size_t tl = strlen(tok);
        if(tl < 3) continue; // skip tiny words like "of", "a"
        any = true;
        bool hit = false;
        for(size_t i = 0; i < nqw && !hit; i++) {
            if(qwl[i] == tl && !strcmp(qw[i], tok)) hit = true;
            else if(tl >= 5 && qwl[i] >= 4 && near_word(tok, tl, qw[i], qwl[i], tl >= 9 ? 2 : 1)) hit = true;
        }
        if(!hit) return false;
    }
    return any;
}

static void score_line(const char* l, const char* q, const char* qw[], size_t nqw, size_t qwl[], Match* m) {
    const char* bar = strchr(l, '|');
    if(!bar) return;
    int score = 0;
    char first[32];
    first[0] = 0;
    const char* p = l;
    bool is_first = true;
    while(p < bar) {
        char kw[48];
        size_t n = 0;
        while(p < bar && *p != ',') {
            if(n < sizeof(kw) - 1) kw[n++] = (char)tolower((unsigned char)*p);
            p++;
        }
        kw[n] = 0;
        while(n && kw[n - 1] == ' ') kw[--n] = 0;
        if(is_first) {
            strlcpy(first, kw, sizeof(first));
            is_first = false;
        }
        if(n > 1) {
            if(phrase_match(q, kw)) {
                score += (int)(n * n);
            } else if(n >= 5 && kw_fuzzy(kw, qw, nqw, qwl)) {
                score += (int)(n * n / 2); // typo-tolerant match
            }
        }
        if(p < bar) p++;
    }
    // reservoir sampling of topics (only for the language you picked)
    if(!m->no_sample) m->seen++;
    if(first[0] && !m->no_sample) {
        if(m->seen <= 4) strlcpy(m->sample[m->seen - 1], first, 32);
        else {
            uint32_t r = rnd(m->seen);
            if(r < 4) strlcpy(m->sample[r], first, 32);
        }
    }
    if(score > m->best) {
        m->second = m->best;
        strlcpy(m->second_topic, m->best_topic, sizeof(m->second_topic));
        m->best = score;
        strlcpy(m->best_topic, first, sizeof(m->best_topic));
        furi_string_set_str(m->ans, bar + 1);
    } else if(score > m->second) {
        m->second = score;
        strlcpy(m->second_topic, first, sizeof(m->second_topic));
    }
}

// scan one knowledge file and keep the best-scoring entries in *m
static bool kb_scan(const char* path, const char* q, const char* qw[], size_t nqw, size_t qwl[], Match* m) {
    FuriString* line = furi_string_alloc();
    bool opened = false;
    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        opened = true;
        uint8_t chunk[128];
        size_t r;
        while((r = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
            for(size_t k = 0; k < r; k++) {
                if(chunk[k] == '\n') {
                    score_line(furi_string_get_cstr(line), q, qw, nqw, qwl, m);
                    furi_string_reset(line);
                } else if(chunk[k] != '\r' && furi_string_size(line) < LINE_MAX_LEN) {
                    furi_string_push_back(line, (char)chunk[k]);
                }
            }
        }
        score_line(furi_string_get_cstr(line), q, qw, nqw, qwl, m);
    }
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(line);
    return opened;
}

static bool kb_find(App* a, const char* raw, Match* m, bool* cross) {
    char q[BUF_LEN];
    normalize_question(q, sizeof(q), raw);
    char qcopy[BUF_LEN];
    strlcpy(qcopy, q, sizeof(qcopy));
    const char* qw[16];
    size_t qwl[16];
    size_t nqw = 0;
    for(char* tok = strtok(qcopy, " "); tok && nqw < 16; tok = strtok(NULL, " ")) {
        qw[nqw] = tok;
        qwl[nqw++] = strlen(tok);
    }
    memset(m, 0, sizeof(*m));
    m->ans = furi_string_alloc();
    g_hu = a->hu;
    bool opened = kb_scan(a->hu ? KB_HU : KB_EN, q, qw, nqw, qwl, m);
    *cross = false;
    if(m->best == 0) {
        g_hu = !a->hu;
        m->no_sample = true;
        bool o2 = kb_scan(a->hu ? KB_EN : KB_HU, q, qw, nqw, qwl, m);
        opened = opened || o2;
        *cross = m->best > 0;
    }
    g_hu = false;
    return opened;
}

static bool kb_fact(App* a, const char* topic, FuriString* out) {
    Match m;
    bool cross;
    kb_find(a, topic, &m, &cross);
    bool ok = m.best >= 16 && !cross;
    if(ok) furi_string_set(out, m.ans);
    furi_string_free(m.ans);
    return ok;
}

static void do_kb(App* a, const char* raw) {
    Match m;
    bool cross;
    bool opened = kb_find(a, raw, &m, &cross);
    a->last_related[0] = 0;
    FuriString* o = a->out;
    if(!opened) {
        furi_string_set_str(o, "Knowledge files missing. Copy sdcard/apps_data/blivzy_ai/ to SD/apps_data/blivzy_ai/.");
    } else if(m.best == 0) {
        furi_string_printf(o, "YOU: %s\n\n", a->buf);
        furi_string_cat_str(o, T(a, "I don't have a reliable answer for that offline yet. Try different keywords.\n\nTry asking about:\n", "Erre meg nincs megbizhato offline valaszom. Probald mas szavakkal.\n\nKerdezz peldaul:\n"));
        for(int i = 0; i < 4 && i < (int)m.seen; i++) furi_string_cat_printf(o, "- %s\n", m.sample[i]);
        furi_string_cat_str(o, T(a, "\nTip: add your own facts to kb_en.txt.", "\nTipp: bovitsd a kb_hu.txt fajlt."));
    } else {
        furi_string_printf(o, "YOU: %s\n\nBLIVZY AI:\n%s", a->buf, furi_string_get_cstr(m.ans));
        if(cross) furi_string_cat_str(o, T(a, "\n\n[from the Hungarian knowledge base]", "\n\n[angol tudastarbol]"));
        if(m.second > 0 && m.second * 3 >= m.best && strcmp(m.second_topic, m.best_topic) != 0) {
            furi_string_cat_printf(o, "\n\n%s %s\n(%s)", T(a, "Related:", "Kapcsolodo:"), m.second_topic, T(a, "type: more", "irj: tovabb"));
            strlcpy(a->last_related, m.second_topic, sizeof(a->last_related));
        }
    }
    furi_string_free(m.ans);
}

/* Hungarian labels for the tool answers (maths, numbers, geometry) + decimal comma */
static void hu_localize(App* a) {
    static const char* const tbl[][2] = {
        {"Yes, it is prime.", "Igen, primszam."}, {"Not prime.", "Nem primszam."},
        {" numbers\nSum = ", " szam\nOsszeg = "}, {"\nMean = ", "\nAtlag = "}, {"\nRange = ", "\nTerjedelem = "},
        {"GCD(", "LNKO("}, {"\nLCM = ", "\nLKKT = "},
        {"\nUnderweight range", "\nAlulsuly"}, {"\nHealthy range", "\nEgeszseges tartomany"},
        {"\nOverweight range", "\nTulsuly"}, {"\nObese range", "\nElhizas"},
        {"(general guide only, not medical advice)", "(csak tajekoztato, nem orvosi tanacs)"},
        {"Give a whole number from 2 to 2,000,000,000.", "Egesz szamot adj meg 2 es 2 000 000 000 kozott."},
        {"Give a whole number from 0 to 4,000,000,000.", "Egesz szamot adj meg 0 es 4 000 000 000 kozott."},
        {"Pick n from 0 to 78.", "Valassz n-et 0 es 78 kozott."},
        {"\nOctal: ", "\nOktalis: "}, {"\nBinary: ", "\nBinaris: "}, {"\nRoman: ", "\nRomai: "},
        {" is even.", " paros."}, {" is odd.", " paratlan."},
        {"Hypotenuse = ", "Atfogo = "}, {"Circle r = ", "Kor r = "}, {"\nCircumference = ", "\nKerulet = "},
        {"\nArea = ", "\nTerulet = "}, {"Sphere r = ", "Gomb r = "}, {"\nVolume = ", "\nTerfogat = "},
        {"\nSurface = ", "\nFelszin = "}, {"Cylinder r=", "Henger r="}, {"Cube side = ", "Kocka oldal = "},
        {"Sides ", "Oldalak "}, {"\nPerimeter = ", "\nKerulet = "}, {"Triangle area = 1/2 x ", "Haromszog terulete = 1/2 x "},
        {"Tip = ", "Borravalo = "}, {"\nTotal = ", "\nOsszesen = "}, {"Price after discount = ", "Ar kedvezmeny utan = "},
        {"\nYou save = ", "\nMegtakaritas = "}, {"Percent change = ", "Szazalekos valtozas = "},
        {"\nVertex x = ", "\nCsucs x = "}, {"Final amount = ", "Vegosszeg = "}, {"\nInterest earned = ", "\nKamat = "},
        {"Monthly payment = ", "Havi torleszto = "}, {"\nTotal paid = ", "\nOsszes fizetes = "}, {"\nTotal interest = ", "\nOsszes kamat = "},
        {"\nStd dev = ", "\nSzoras = "}, {"\nVariance = ", "\nVariancia = "}, {"\nMode = ", "\nModusz = "},
    };
    char buf[700];
    strlcpy(buf, furi_string_get_cstr(a->out), sizeof(buf));
    for(size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) repl_all(buf, sizeof(buf), tbl[i][0], tbl[i][1]);
    for(char* p = buf + 1; *p; p++) // 3.14 -> 3,14
        if(*p == '.' && isdigit((unsigned char)p[-1]) && isdigit((unsigned char)p[1])) *p = ',';
    furi_string_set_str(a->out, buf);
}

/* ================= follow-ups: more / again ================= */
static void do_universal(App* a);

static bool do_followup(App* a, const char* q) {
    const char* morew[] = {"more", "tell me more", "go on", "continue", "related", "and more", "tovabb", "tobbet", "tobbet rola", "folytasd", "kapcsolodo", "meg"};
    const char* againw[] = {"again", "repeat", "say that again", "once more", "megint", "ismet", "ismetles", "mondd ujra", "meg egyszer"};
    if(EXACT_ANY(q, againw)) {
        if(!a->last_q[0]) {
            furi_string_set_str(a->out, T(a, "Nothing to repeat yet.", "Meg nincs mit megismetelni."));
            return true;
        }
        strlcpy(a->buf, a->last_q, sizeof(a->buf));
        do_universal(a);
        return true;
    }
    if(EXACT_ANY(q, morew)) {
        if(!a->last_related[0]) {
            furi_string_set_str(a->out, T(a, "Ask me about a topic first - I'll suggest related ones to continue with.", "Kerdezz eloszor egy temarol - javaslok kapcsolodo temakat a folytatashoz."));
            return true;
        }
        strlcpy(a->buf, a->last_related, sizeof(a->buf));
        a->last_related[0] = 0;
        do_universal(a);
        return true;
    }
    return false;
}

/* ================= v0.6 tools ================= */
static void set_ans(double v) {
    g_ans = v;
    g_have_ans = true;
}

static void chain_ans(char* t, size_t cap) {
    if(!g_have_ans) return;
    char* p = t;
    while(*p == ' ') p++;
    static const char* const lead[] = {"and then ", "then ", "now ", "and "};
    for(size_t i = 0; i < sizeof(lead) / sizeof(lead[0]); i++) {
        size_t n = strlen(lead[i]);
        if(!strncmp(p, lead[i], n)) {
            p += n;
            break;
        }
    }
    static const char* const words[][2] = {
        {"times ", "*"}, {"multiplied by ", "*"}, {"plus ", "+"}, {"minus ", "-"},
        {"divided by ", "/"}, {"over ", "/"}, {"to the power of ", "^"},
    };
    char tmp[BUF_LEN + 40];
    for(size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        size_t n = strlen(words[i][0]);
        if(!strncmp(p, words[i][0], n) && (isdigit((unsigned char)p[n]) || p[n] == '(' || p[n] == '.')) {
            snprintf(tmp, sizeof(tmp), "ans%s%s", words[i][1], p + n);
            strlcpy(t, tmp, cap);
            return;
        }
    }
    if(*p && strchr("+*/^", *p) && (isdigit((unsigned char)p[1]) || p[1] == '(' || p[1] == ' ' || p[1] == '.')) {
        snprintf(tmp, sizeof(tmp), "ans%s", p);
        strlcpy(t, tmp, cap);
    }
}

/* ---- two equations, two unknowns ---- */
static bool f2(const char* l, const char* r, double x, double y, double* out) {
    double u, w;
    g_x = x;
    g_y = y;
    if(!eval_str(l, &u) || !eval_str(r, &w)) return false;
    *out = u - w;
    return true;
}

static bool eq_coef(char* eq, double* ax, double* by, double* c) {
    char* e = strchr(eq, '=');
    if(!e) return false;
    *e = 0;
    double f00, f10, f01, f23;
    if(!f2(eq, e + 1, 0, 0, &f00) || !f2(eq, e + 1, 1, 0, &f10) || !f2(eq, e + 1, 0, 1, &f01) ||
       !f2(eq, e + 1, 2, 3, &f23))
        return false;
    *ax = f10 - f00;
    *by = f01 - f00;
    *c = -f00;
    return fabs(f23 - (f00 + 2 * *ax + 3 * *by)) < 1e-9 * (1 + fabs(f23));
}

static bool do_system(App* a, const char* input) {
    char t[BUF_LEN + 8];
    lower_copy(t, sizeof(t), input);
    for(char* c = t; *c; c++)
        if(*c == '?') *c = ' ';
    char* p = t;
    while(*p == ' ') p++;
    if(!strncmp(p, "solve ", 6)) p += 6;
    char* e1 = strchr(p, '=');
    char* e2 = e1 ? strchr(e1 + 1, '=') : NULL;
    if(!e2 || !strchr(p, 'y')) return false;
    char* r = e1 + 1;
    while(*r == ' ') r++;
    while(*r && *r != ' ' && *r != ',' && *r != ';') r++;
    if(r >= e2) return false;
    char* second = r;
    while(*second == ' ' || *second == ',' || *second == ';') second++;
    if(!strncmp(second, "and ", 4)) second += 4;
    else if(!strncmp(second, "es ", 3)) second += 3;
    *r = 0;
    double a1, b1, c1, a2, b2, c2;
    if(!eq_coef(p, &a1, &b1, &c1) || !eq_coef(second, &a2, &b2, &c2)) {
        furi_string_set_str(a->out, T(a, "Only linear equations in x and y, e.g. x+y=10, x-y=2", "Csak linearis egyenletek x-szel es y-nal, pl. x+y=10, x-y=2"));
        return true;
    }
    double det = a1 * b2 - a2 * b1;
    if(fabs(det) < 1e-12) {
        furi_string_set_str(a->out, T(a, "No single solution (lines are parallel or the same).", "Nincs egyetlen megoldas (parhuzamos vagy azonos egyenesek)."));
        return true;
    }
    double x = (c1 * b2 - c2 * b1) / det, y = (a1 * c2 - a2 * c1) / det;
    furi_string_set_str(a->out, "x = ");
    cat_num(a->out, round(x * 1e8) / 1e8);
    furi_string_cat_str(a->out, "\ny = ");
    cat_num(a->out, round(y * 1e8) / 1e8);
    set_ans(x);
    return true;
}

/* ---- calculus: derivative at a point, definite integral, series sum ---- */
static char* split_at(char* t, const char* const* cues, size_t n) {
    for(size_t i = 0; i < n; i++) {
        char* p = strstr(t, cues[i]);
        if(p) {
            *p = 0;
            return p + strlen(cues[i]);
        }
    }
    return NULL;
}

static void trim_both(char* t) {
    char* s0 = t;
    while(*s0 == ' ') s0++;
    if(s0 != t) memmove(t, s0, strlen(s0) + 1);
    size_t l = strlen(t);
    while(l && t[l - 1] == ' ') t[--l] = 0;
}

static bool bounds(char* b, double* lo, double* hi) {
    repl_all(b, strlen(b) + 1, "-tol", " ");
    repl_all(b, strlen(b) + 1, "-ig", " ");
    repl_all(b, strlen(b) + 1, " kozott", " ");
    repl_all(b, strlen(b) + 1, "between ", " ");
    repl_all(b, strlen(b) + 1, "from ", " ");
    trim_both(b);
    static const char* const seps[] = {" to ", " and ", " es ", " ig ", " "};
    char* h = split_at(b, seps, 5);
    if(!h) return false;
    trim_both(b);
    trim_both(h);
    return eval_str(b, lo) && eval_str(h, hi);
}

static bool do_calculus(App* a, const char* input) {
    char t[BUF_LEN + 8];
    lower_copy(t, sizeof(t), input);
    for(char* c = t; *c; c++)
        if(*c == '?' || *c == ',') *c = ' ';
    bool deriv = strstr(t, "deriv") || strstr(t, "differentiate");
    bool integ = strstr(t, "integr");
    bool sumw = !strncmp(t, "sum ", 4) || strstr(t, " sum ");
    if(!(deriv || integ || sumw) || !strchr(t, 'x') || !strpbrk(t, "0123456789")) return false;
    char* kw = deriv ? (strstr(t, "deriv") ? strstr(t, "deriv") : strstr(t, "differentiate")) : integ ? strstr(t, "integr") : (strstr(t, "sum ") ? strstr(t, "sum ") : t);
    char* tail = strchr(kw, ' ');
    if(!tail) return false;
    trim_both(tail);
    static const char* const skipw[] = {"of ", "the ", "function ", "for "};
    for(int g = 0; g < 4; g++)
        for(size_t i = 0; i < sizeof(skipw) / sizeof(skipw[0]); i++) {
            size_t n = strlen(skipw[i]);
            if(!strncmp(tail, skipw[i], n)) tail += n;
        }
    char* eq = strchr(tail, '=');
    if(eq && strchr(tail, 'x') < eq && !strstr(tail, " at ") && !strstr(tail, " x=")) {
        char* br = strstr(tail, "(x)");
        if(br && br + 3 == eq) tail = eq + 1;
    }
    FuriString* o = a->out;
    if(deriv) {
        static const char* const cues[] = {" at x = ", " at x=", " at ", " when x=", " for x=", " in x=", " x=", "@"};
        char* pt = split_at(tail, cues, 8);
        if(!pt) {
            char* sp = strrchr(tail, ' ');
            if(sp && (isdigit((unsigned char)sp[1]) || (sp[1] == '-' && isdigit((unsigned char)sp[2])))) {
                *sp = 0;
                pt = sp + 1;
            }
        }
        if(!pt) {
            furi_string_set_str(o, T(a, "Add a point, e.g. derivative of x^2 at 3", "Adj meg pontot, pl. derivalt x^2 at 3"));
            return true;
        }
        for(char* c = pt; *c; c++)
            if(*c == '-' && isalpha((unsigned char)c[1])) {
                *c = 0;
                break;
            }
        trim_both(tail);
        trim_both(pt);
        double x0, fm2, fm1, fp1, fp2;
        if(!eval_str(pt, &x0)) return false;
        double h = 1e-3 * (fabs(x0) > 1 ? fabs(x0) : 1);
        double xs[4] = {x0 - 2 * h, x0 - h, x0 + h, x0 + 2 * h}, fv[4];
        for(int i = 0; i < 4; i++) {
            g_x = xs[i];
            if(!eval_str(tail, &fv[i])) {
                furi_string_set_str(o, T(a, "Cannot differentiate there - check the expression.", "Itt nem derivalhato - ellenorizd a kifejezest."));
                return true;
            }
        }
        fm2 = fv[0];
        fm1 = fv[1];
        fp1 = fv[2];
        fp2 = fv[3];
        double d = (fm2 - 8 * fm1 + 8 * fp1 - fp2) / (12 * h);
        d = round(d * 1e6) / 1e6;
        furi_string_printf(o, "d/dx [%s] at x = ", tail);
        cat_num(o, x0);
        furi_string_cat_str(o, "\n\n= ");
        cat_num(o, d);
        set_ans(d);
        return true;
    }
    static const char* const fcue[] = {" from ", " between ", " on "};
    char* bd = split_at(tail, fcue, 3);
    if(!bd) {
        char* sp = strrchr(tail, ' ');
        char* sp2 = NULL;
        if(sp) {
            *sp = 0;
            sp2 = strrchr(tail, ' ');
            *sp = ' ';
        }
        if(sp2) {
            *sp2 = 0;
            bd = sp2 + 1;
        }
    }
    double lo, hi;
    if(!bd || !bounds(bd, &lo, &hi)) {
        furi_string_set_str(o, T(a, "Say the range, e.g. integrate x^2 from 0 to 3", "Add meg a tartomanyt, pl. integral x^2 from 0 to 3"));
        return true;
    }
    trim_both(tail);
    if(sumw) {
        if(hi - lo > 100000 || hi < lo) {
            furi_string_set_str(o, T(a, "Range must be ascending and at most 100000 terms.", "A tartomany novekvo legyen, legfeljebb 100000 tag."));
            return true;
        }
        double total = 0, v;
        for(double k = floor(lo + 0.5); k <= hi + 1e-9; k += 1) {
            g_x = k;
            if(!eval_str(tail, &v)) {
                furi_string_set_str(o, T(a, "Cannot sum - check the expression.", "Nem osszegezheto - ellenorizd a kifejezest."));
                return true;
            }
            total += v;
        }
        furi_string_printf(o, "sum of [%s]\nx = ", tail);
        cat_num(o, floor(lo + 0.5));
        furi_string_cat_str(o, "..");
        cat_num(o, floor(hi + 1e-9));
        furi_string_cat_str(o, "\n\n= ");
        cat_num(o, round(total * 1e8) / 1e8);
        set_ans(total);
        return true;
    }
    const int N = 1000;
    double step = (hi - lo) / N, acc = 0, v;
    for(int i = 0; i <= N; i++) {
        g_x = lo + step * i;
        if(!eval_str(tail, &v)) {
            furi_string_set_str(o, T(a, "Cannot integrate over that range - check the expression.", "Ezen a tartomanyon nem integralhato - ellenorizd a kifejezest."));
            return true;
        }
        acc += v * ((i == 0 || i == N) ? 1 : (i % 2 ? 4 : 2));
    }
    acc = acc * step / 3;
    acc = round(acc * 1e6) / 1e6;
    furi_string_printf(o, "integral of [%s]\nfrom ", tail);
    cat_num(o, lo);
    furi_string_cat_str(o, " to ");
    cat_num(o, hi);
    furi_string_cat_str(o, "\n\n= ");
    cat_num(o, acc);
    furi_string_cat_str(o, T(a, "\n(numeric, Simpson)", "\n(numerikus, Simpson)"));
    set_ans(acc);
    return true;
}

/* ---- dates ---- */
static long days_from_civil(long y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, long* y, int* m, int* d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

static bool parse_date(const char* p, long* out, const char** end) {
    char* e;
    long y = strtol(p, &e, 10);
    if(e - p != 4 || (*e != '-' && *e != '/' && *e != '.')) return false;
    char sep = *e;
    const char* q = e + 1;
    long m = strtol(q, &e, 10);
    if(e == q || e - q > 2 || *e != sep) return false;
    q = e + 1;
    long d = strtol(q, &e, 10);
    if(e == q || e - q > 2 || m < 1 || m > 12 || d < 1) return false;
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if(d > dm[m - 1] + (m == 2 && leap)) return false;
    *out = days_from_civil(y, (int)m, (int)d);
    *end = e;
    return true;
}

static int find_dates(const char* s0, long* days, int max, char* blank) {
    int n = 0;
    strlcpy(blank, s0, BUF_LEN + 40);
    for(const char* p = s0; *p && n < max; p++) {
        if(!isdigit((unsigned char)*p) || (p > s0 && (isdigit((unsigned char)p[-1]) || p[-1] == '-'))) continue;
        const char* e;
        if(parse_date(p, &days[n], &e)) {
            for(const char* c = p; c < e; c++) blank[c - s0] = ' ';
            n++;
            p = e - 1;
        }
    }
    return n;
}

static void cat_date(FuriString* o, long z) {
    long y;
    int m, d;
    civil_from_days(z, &y, &m, &d);
    furi_string_cat_printf(o, "%04ld-%02d-%02d", y, m, d);
}

static const char* weekday_name(App* a, long z) {
    static const char* const en[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const hu[] = {"Vasarnap", "Hetfo", "Kedd", "Szerda", "Csutortok", "Pentek", "Szombat"};
    int w = (int)(((z % 7) + 7 + 4) % 7);
    return a->hu ? hu[w] : en[w];
}

static bool do_dates(App* a, const char* tr, const char* q) {
    long dd[2];
    char blank[BUF_LEN + 40];
    int nd = find_dates(tr, dd, 2, blank);
    if(nd == 0) return false;
    FuriString* o = a->out;
    furi_string_reset(o);
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    long today = days_from_civil(now.year, now.month, now.day);
    bool wk = has_word(q, "weekday") || has_word(q, "day of the week") || (has_word(q, "what") && has_word(q, "day")) ||
              has_word(q, "melyik") || has_word(q, "milyen");
    if(nd == 2) {
        long diff = dd[1] - dd[0];
        long ad = diff < 0 ? -diff : diff;
        furi_string_printf(o, "%ld %s\n%ld %s %ld %s", ad, T(a, "days", "nap"), ad / 7, T(a, "weeks", "het"), ad % 7, T(a, "days", "nap"));
        long y1, y2;
        int m1, m2, d1, d2;
        civil_from_days(diff < 0 ? dd[1] : dd[0], &y1, &m1, &d1);
        civil_from_days(diff < 0 ? dd[0] : dd[1], &y2, &m2, &d2);
        long months = (y2 - y1) * 12 + (m2 - m1) - (d2 < d1 ? 1 : 0);
        furi_string_cat_printf(o, "\n%ld %s %d %s", months / 12, T(a, "years", "ev"), (int)(months % 12), T(a, "months", "honap"));
        set_ans((double)ad);
        return true;
    }
    if(has_word(q, "until") || has_word(q, "left") || has_word(q, "hatra") || has_word(q, "since") || has_word(q, "ago") || has_word(q, "countdown")) {
        long diff = dd[0] - today;
        furi_string_printf(o, "%ld %s", diff < 0 ? -diff : diff, T(a, "days", "nap"));
        furi_string_cat_str(o, diff < 0 ? T(a, " ago", " napja volt") : diff == 0 ? T(a, " (today!)", " (ma!)") : T(a, " to go", " van hatra"));
        set_ans((double)(diff < 0 ? -diff : diff));
        return true;
    }
    double nv[4];
    int n = extract_nums(blank, nv, 4);
    bool shift = n >= 1 && (has_word(q, "plus") || has_word(q, "after") || has_word(q, "add") || has_word(q, "minus") || has_word(q, "before") || has_word(q, "later") || has_word(q, "elott") || has_word(q, "utan") || has_word(q, "mulva"));
    if(shift) {
        double k = fabs(nv[0]);
        if(has_word(q, "week") || has_word(q, "weeks") || has_word(q, "het")) k *= 7;
        else if(has_word(q, "month") || has_word(q, "months") || has_word(q, "honap")) k *= 30.4375;
        else if(has_word(q, "year") || has_word(q, "years") || has_word(q, "ev")) k *= 365.25;
        if(has_word(q, "minus") || has_word(q, "before") || has_word(q, "elott")) k = -k;
        long z = dd[0] + (long)round(k);
        cat_date(o, z);
        furi_string_cat_printf(o, "\n%s", weekday_name(a, z));
        return true;
    }
    if(wk || n == 0) {
        cat_date(o, dd[0]);
        furi_string_cat_printf(o, "\n%s", weekday_name(a, dd[0]));
        return true;
    }
    return false;
}

/* ---- money and percentages ---- */
static bool do_money(App* a, const char* in, const char* q) {
    double v[NUMS_MAX];
    int n = extract_nums(in, v, NUMS_MAX);
    FuriString* o = a->out;
    furi_string_reset(o);
    if(n >= 3 && (has_word(q, "compound") || has_word(q, "kamatos"))) {
        double per = (has_word(q, "monthly") || has_word(q, "havi")) ? 12 : (has_word(q, "daily") ? 365 : 1);
        double fin = v[0] * pow(1 + v[1] / 100.0 / per, per * v[2]);
        furi_string_set_str(o, "Final amount = ");
        cat_num(o, round(fin * 100) / 100);
        furi_string_cat_str(o, "\nInterest earned = ");
        cat_num(o, round((fin - v[0]) * 100) / 100);
        set_ans(fin);
        return true;
    }
    if(n >= 3 && (has_word(q, "loan") || has_word(q, "mortgage") || has_word(q, "hitel") || has_word(q, "emi"))) {
        bool months = has_word(q, "months") || has_word(q, "month") || has_word(q, "honap");
        double N = months ? v[2] : v[2] * 12, r = v[1] / 1200.0;
        if(N < 1 || N > 1200) return false;
        double m = r == 0 ? v[0] / N : v[0] * r / (1 - pow(1 + r, -N));
        furi_string_set_str(o, "Monthly payment = ");
        cat_num(o, round(m * 100) / 100);
        furi_string_cat_str(o, "\nTotal paid = ");
        cat_num(o, round(m * N * 100) / 100);
        furi_string_cat_str(o, "\nTotal interest = ");
        cat_num(o, round((m * N - v[0]) * 100) / 100);
        set_ans(m);
        return true;
    }
    if(n == 2 && (has_word(q, "percent") || has_word(q, "percentage") || has_word(q, "szazalek")) && (has_word(q, "what") || has_word(q, "hany") || has_word(q, "which")) && v[1] != 0) {
        double p = v[0] / v[1] * 100;
        cat_num(o, v[0]);
        furi_string_cat_str(o, " / ");
        cat_num(o, v[1]);
        furi_string_cat_str(o, " = ");
        cat_num(o, round(p * 1e4) / 1e4);
        furi_string_cat_str(o, "%");
        set_ans(p);
        return true;
    }
    bool up = has_word(q, "increase") || has_word(q, "raise") || has_word(q, "grow") || has_word(q, "noveld") || has_word(q, "novelj");
    bool down = has_word(q, "decrease") || has_word(q, "reduce") || has_word(q, "cut") || has_word(q, "csokkents") || has_word(q, "csokkentsd");
    if(n == 2 && (up || down) && (has_word(q, "by") || strchr(in, '%')) && !has_word(q, "from")) {
        double r = v[0] * (1 + (up ? 1 : -1) * v[1] / 100.0);
        cat_num(o, v[0]);
        furi_string_cat_str(o, up ? " + " : " - ");
        cat_num(o, v[1]);
        furi_string_cat_str(o, "% = ");
        cat_num(o, round(r * 1e6) / 1e6);
        set_ans(r);
        return true;
    }
    return false;
}

static bool do_tools2(App* a, const char* tr, const char* qt, const char* q) {
    return do_calculus(a, tr) || do_dates(a, tr, q) || do_money(a, tr, qt) ||
           (strstr(q, "hany szazalek") && do_money(a, tr, q));
}

/* ================= the router ================= */
static void do_universal(App* a) {
    char folded[BUF_LEN];
    fold_utf8(folded, sizeof(folded), a->buf); // accents -> plain ASCII
    strlcpy(a->buf, folded, sizeof(a->buf));
    char q[BUF_LEN];
    char tr[BUF_LEN + 40];  // text for the tools (Hungarian command words translated)
    char qt[BUF_LEN + 40];
    char expression[BUF_LEN + 24];
    normalize_question(q, sizeof(q), a->buf);
    a->questions++;
    if(q[0] == 0) {
        furi_string_set_str(a->out, T(a, "Type something first.", "Irj be valamit."));
        return;
    }
    if(do_smalltalk(a, q, a->buf)) return;
    if(do_followup(a, q)) return;
    strlcpy(a->last_q, a->buf, sizeof(a->last_q));

    if(a->hu) hu_prepare(tr, sizeof(tr), a->buf);
    else strlcpy(tr, a->buf, sizeof(tr));
    numwords(tr, sizeof(tr));
    chain_ans(tr, sizeof(tr));
    normalize_question(qt, sizeof(qt), tr);

    if(do_system(a, a->buf) || do_tools2(a, tr, qt, q)) {
        if(a->hu) hu_localize(a);
        return;
    }
    if(do_convert(a, tr) || (strchr(tr, '=') && do_solve(a, tr))) {
        if(a->hu) hu_localize(a);
        return;
    }
    if(has_word(qt, "quadratic")) {
        double c[3];
        if(extract_nums(tr, c, 3) == 3) {
            do_quad(a, c[0], c[1], c[2]);
            if(a->hu) hu_localize(a);
            return;
        }
    }
    if(do_writing(a, q, a->buf)) return;
    if(do_numbers(a, tr, qt)) {
        if(a->hu) hu_localize(a);
        return;
    }
    if(extract_math(tr, expression, sizeof(expression))) {
        do_calc(a, expression);
        if(a->hu) hu_localize(a);
        return;
    }
    do_kb(a, a->buf);
}

static void append_log(App* a, const char* q) {
    if(!a->log_on) return;
    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, LOG_FILE, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(f, "Q: ", 3);
        storage_file_write(f, q, strlen(q));
        storage_file_write(f, "\nA: ", 4);
        const char* ans = furi_string_get_cstr(a->out);
        storage_file_write(f, ans, strlen(ans));
        storage_file_write(f, "\n---\n", 5);
    }
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

/* ================= predictive keyboard ================= */
typedef struct {
    char ph[3][SUG_LEN];
    uint8_t nph;
    char wd[3][SUG_LEN];
    uint8_t nwd;
    char st[3][SUG_LEN];
    uint8_t nst;
} Scan;

// Logical layers: 0 letters, 1 symbols for maths, 2 Hungarian accents (only in Hungarian mode).
// '#' cycles through them, '<' backspace, '>' send, '_' space.
// Table rows: [0] English QWERTY, [1] symbols, [2] accents, [3] Hungarian QWERTZ.
// The accent layer shows a o u etc. with the accent mark drawn on top; the letter is typed
// without the accent because the Flipper font has none (matching ignores accents anyway).
static const char* const kbd_layers[4][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl<", "zxcvbnm#_>"},
    {"1234567890", "+-*/^()=.,", "%!?:'x$&@<", "e;[]{}|#_>"},
    {"1234567890", "aeiououou<", ".,?!-':;/&", "()+=%\"@#_>"},
    {"1234567890", "qwertzuiop", "asdfghjkl<", "yxcvbnm#_>"},
};

static const char* kbd_row(const KbdModel* m, uint8_t r) {
    uint8_t t = m->layer;
    if(t == 0 && m->hu) t = 3; // QWERTZ
    return kbd_layers[t][r];
}

// accent mark drawn on a key of layer 2, row 1: 0 none, 1 acute, 2 umlaut, 3 double acute
static uint8_t kbd_mark(const KbdModel* m, uint8_t r, uint8_t c) {
    if(m->layer != 2 || r != 1 || c > 8) return 0;
    return c <= 4 ? 1 : (c <= 6 ? 2 : 3);
}

static void scan_line(const char* line, const char* text, size_t tlen, const char* lw, size_t lwlen, Scan* sc) {
    if(!line[0]) return;
    if(line[0] == '>') {
        if(sc->nst < 3) {
            strlcpy(sc->st[sc->nst], line + 1, SUG_LEN);
            sc->nst++;
        }
        return;
    }
    size_t ll = strlen(line);
    if(tlen > 0 && ll > tlen && strncmp(line, text, tlen) == 0) {
        if(sc->nph < 3) {
            strlcpy(sc->ph[sc->nph], line, SUG_LEN);
            sc->nph++;
        }
        return;
    }
    if(lwlen > 0 && !strchr(line, ' ') && ll > lwlen && strncmp(line, lw, lwlen) == 0 && sc->nwd < 3) {
        strlcpy(sc->wd[sc->nwd], line, SUG_LEN);
        sc->nwd++;
    }
}

static void suggest_update(KbdModel* m) {
    Scan sc;
    memset(&sc, 0, sizeof(sc));
    size_t tlen = m->len;
    const char* lw = m->text;
    for(size_t i = 0; i < tlen; i++)
        if(m->text[i] == ' ') lw = m->text + i + 1;
    size_t lwlen = strlen(lw);

    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, m->hu ? WORDS_HU : WORDS_EN, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[SUG_LEN];
        size_t ll = 0;
        uint8_t chunk[64];
        size_t r;
        bool full = false;
        while(!full && (r = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
            for(size_t k = 0; k < r; k++) {
                char ch = (char)chunk[k];
                if(ch == '\n') {
                    line[ll] = 0;
                    scan_line(line, m->text, tlen, lw, lwlen, &sc);
                    ll = 0;
                } else if(ch != '\r') {
                    if(ll < SUG_LEN - 1) line[ll++] = (char)tolower((unsigned char)ch);
                }
            }
            if(sc.nph >= 3 || (tlen == 0 && sc.nst >= 3)) full = true;
        }
        if(!full && ll > 0) {
            line[ll] = 0;
            scan_line(line, m->text, tlen, lw, lwlen, &sc);
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);

    m->sug_count = 0;
    if(tlen == 0) {
        for(uint8_t i = 0; i < sc.nst && m->sug_count < SUG_N; i++) {
            strlcpy(m->sug[m->sug_count], sc.st[i], SUG_LEN);
            m->sug_phrase[m->sug_count++] = true;
        }
    } else {
        for(uint8_t i = 0; i < sc.nph && m->sug_count < SUG_N; i++) {
            strlcpy(m->sug[m->sug_count], sc.ph[i], SUG_LEN);
            m->sug_phrase[m->sug_count++] = true;
        }
        for(uint8_t i = 0; i < sc.nwd && m->sug_count < SUG_N; i++) {
            strlcpy(m->sug[m->sug_count], sc.wd[i], SUG_LEN);
            m->sug_phrase[m->sug_count++] = false;
        }
    }
    if(m->sug_sel >= m->sug_count) m->sug_sel = 0;
    if(m->sug_count == 0) m->in_sug = false;
}

static void apply_sug(KbdModel* m) {
    if(m->sug_sel >= m->sug_count) return;
    if(m->sug_phrase[m->sug_sel]) {
        strlcpy(m->text, m->sug[m->sug_sel], BUF_LEN);
    } else {
        size_t start = 0;
        for(size_t k = 0; k < m->len; k++)
            if(m->text[k] == ' ') start = k + 1;
        m->text[start] = 0;
        strlcat(m->text, m->sug[m->sug_sel], BUF_LEN);
    }
    size_t l = strlen(m->text);
    if(l < BUF_LEN - 2) {
        m->text[l] = ' ';
        m->text[l + 1] = 0;
    }
    m->len = (uint8_t)strlen(m->text);
}

static const char* tail_fit(Canvas* c, const char* t, int maxw) {
    while(*t && canvas_string_width(c, t) > maxw) t++;
    return t;
}

static void kbd_draw(Canvas* c, void* model) {
    KbdModel* m = model;
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontSecondary);

    for(uint8_t i = 0; i < SUG_N; i++) {
        int x = i * 43;
        if(i >= m->sug_count) continue;
        char t[SUG_LEN];
        strlcpy(t, m->sug[i], SUG_LEN);
        while(strlen(t) > 1 && canvas_string_width(c, t) > 37) t[strlen(t) - 1] = 0;
        bool sel = m->in_sug && m->sug_sel == i;
        if(sel) {
            canvas_draw_rbox(c, x, 0, 41, 11, 2);
            canvas_set_color(c, ColorWhite);
        } else {
            canvas_draw_rframe(c, x, 0, 41, 11, 2);
        }
        canvas_draw_str(c, x + 3, 9, t);
        canvas_set_color(c, ColorBlack);
    }

    canvas_draw_frame(c, 0, 12, 128, 11);
    char tmp[SUG_LEN + BUF_LEN + 4];
    if(m->in_sug && m->sug_sel < m->sug_count) snprintf(tmp, sizeof(tmp), "> %s", m->sug[m->sug_sel]);
    else snprintf(tmp, sizeof(tmp), "%s_", m->text);
    canvas_draw_str(c, 2, 21, tail_fit(c, tmp, 108));
    canvas_draw_str(c, 112, 21, m->layer == 1 ? "#+" : (m->layer == 2 ? "a'" : (m->hu ? "hu" : "ab")));

    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t col = 0; col < 10; col++) {
            int x = 4 + col * 12;
            int y = 24 + r * 10;
            char ch = kbd_row(m, r)[col];
            bool cur = !m->in_sug && m->row == r && m->col == col;
            bool special = (ch == '<' || ch == '_' || ch == '>' || ch == '#');
            if(cur) {
                canvas_draw_rbox(c, x, y, 12, 10, 2);
                canvas_set_color(c, ColorWhite);
            } else if(special) {
                canvas_draw_rframe(c, x, y, 12, 10, 2);
            }
            canvas_draw_glyph(c, x + 3, y + 8, (uint16_t)ch);
            uint8_t mk = kbd_mark(m, r, col);
            if(mk == 1) {
                canvas_draw_line(c, x + 6, y + 2, x + 8, y);
            } else if(mk == 2) {
                canvas_draw_dot(c, x + 4, y + 1);
                canvas_draw_dot(c, x + 8, y + 1);
            } else if(mk == 3) {
                canvas_draw_line(c, x + 4, y + 2, x + 6, y);
                canvas_draw_line(c, x + 7, y + 2, x + 9, y);
            }
            canvas_set_color(c, ColorBlack);
        }
    }
}

static void kbd_handle(KbdModel* m, InputEvent* e, bool* done, bool* click) {
    switch(e->key) {
    case InputKeyUp:
        if(!m->in_sug) {
            if(m->row > 0) m->row--;
            else if(m->sug_count > 0) {
                m->in_sug = true;
                if(m->sug_sel >= m->sug_count) m->sug_sel = 0;
            }
        }
        break;
    case InputKeyDown:
        if(m->in_sug) m->in_sug = false;
        else if(m->row < 3) m->row++;
        break;
    case InputKeyLeft:
        if(m->in_sug) m->sug_sel = (m->sug_sel > 0) ? m->sug_sel - 1 : m->sug_count - 1;
        else m->col = (m->col > 0) ? m->col - 1 : 9;
        break;
    case InputKeyRight:
        if(m->in_sug) m->sug_sel = (m->sug_sel + 1 < m->sug_count) ? m->sug_sel + 1 : 0;
        else m->col = (m->col < 9) ? m->col + 1 : 0;
        break;
    case InputKeyOk:
        if(e->type == InputTypeRepeat && !(!m->in_sug && kbd_row(m, m->row)[m->col] == '<')) break;
        *click = true;
        if(m->in_sug) {
            apply_sug(m);
            m->in_sug = false;
            suggest_update(m);
        } else {
            char ch = kbd_row(m, m->row)[m->col];
            if(ch == '#') {
                m->layer = (uint8_t)((m->layer + 1) % (m->hu ? 3 : 2));
            } else if(ch == '<') {
                if(m->len > 0) {
                    m->len--;
                    m->text[m->len] = 0;
                }
                suggest_update(m);
            } else if(ch == '>') {
                if(m->len > 0) {
                    *done = true;
                    *click = false;
                }
            } else {
                if(ch == '_') ch = ' ';
                if(m->len < BUF_LEN - 1) {
                    m->text[m->len++] = ch;
                    m->text[m->len] = 0;
                }
                suggest_update(m);
            }
        }
        break;
    default:
        break;
    }
}

static bool kbd_input(InputEvent* e, void* ctx) {
    App* a = ctx;
    if(e->key == InputKeyBack) return false;
    if(e->type != InputTypeShort && e->type != InputTypeRepeat) return true;
    bool done = false;
    bool click = false;
    with_view_model(a->kbd, KbdModel * m, { kbd_handle(m, e, &done, &click); }, true);
    if(done) {
        with_view_model(a->kbd, KbdModel * m, { strlcpy(a->buf, m->text, sizeof(a->buf)); }, false);
        view_dispatcher_send_custom_event(a->vd, EvtInputDone);
    } else if(click) {
        beep(a, 1200.0f, 8);
    }
    return true;
}

static void kbd_reset(App* a, bool keep_layer) {
    with_view_model(
        a->kbd, KbdModel * m, {
            uint8_t layer = keep_layer ? m->layer : 0;
            memset(m, 0, sizeof(KbdModel));
            m->layer = layer;
            m->hu = a->hu;
            if(!m->hu && m->layer > 1) m->layer = 0;
            suggest_update(m);
        },
        true);
}

static void enter_kbd(App* a) {
    kbd_reset(a, false);
    view_dispatcher_switch_to_view(a->vd, ViewKbd);
}

/* ================= screens & navigation ================= */
static void splash_draw(Canvas* c, void* model) {
    UNUSED(model);
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);
    canvas_draw_xbm(c, 0, 0, 128, 64, blivzy_logo);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 58, AlignCenter, AlignBottom, "Made by Blivzy  v" VERSION);
}

static bool splash_input(InputEvent* e, void* ctx) {
    App* a = ctx;
    if(e->key == InputKeyBack) return false;
    if(e->type == InputTypeShort) {
        view_dispatcher_switch_to_view(a->vd, ViewMenu);
        return true;
    }
    return false;
}

static void menu_cb(void* ctx, uint32_t idx);
static void hist_cb(void* ctx, uint32_t idx);

static void build_menu(App* a, uint32_t sel) {
    submenu_reset(a->menu);
    submenu_set_header(a->menu, "Blivzy AI");
    submenu_add_item(a->menu, T(a, "Ask anything", "Kerdezz barmit"), MenuChat, menu_cb, a);
    submenu_add_item(a->menu, T(a, "History", "Elozmenyek"), MenuHist, menu_cb, a);
    submenu_add_item(a->menu, T(a, "What can I ask?", "Mit kerdezhetek?"), MenuExamples, menu_cb, a);
    submenu_add_item(a->menu, T(a, "Fun fact", "Erdekesseg"), MenuFact, menu_cb, a);
    submenu_add_item(a->menu, T(a, "Language: English", "Nyelv: Magyar"), MenuLang, menu_cb, a);
    submenu_add_item(a->menu, a->sound ? T(a, "Sound: On", "Hang: Be") : T(a, "Sound: Off", "Hang: Ki"), MenuSound, menu_cb, a);
    submenu_add_item(a->menu, a->log_on ? T(a, "Chat log: On", "Naplo: Be") : T(a, "Chat log: Off", "Naplo: Ki"), MenuLog, menu_cb, a);
    submenu_add_item(a->menu, T(a, "About", "Nevjegy"), MenuAbout, menu_cb, a);
    submenu_set_selected_item(a->menu, sel);
}

static void build_hist(App* a) {
    submenu_reset(a->hist);
    submenu_set_header(a->hist, T(a, "Recent questions", "Elozmenyek"));
    if(a->hist_count == 0) {
        submenu_add_item(a->hist, T(a, "(nothing yet)", "(meg ures)"), 99, hist_cb, a);
        return;
    }
    for(uint8_t i = 0; i < a->hist_count; i++) {
        submenu_add_item(a->hist, a->history[(a->hist_count - 1) - i], i, hist_cb, a);
    }
}

static void hist_push(App* a, const char* q) {
    if(a->hist_count && !strcmp(a->history[a->hist_count - 1], q)) return;
    if(a->hist_count == HIST_N) {
        memmove(a->history[0], a->history[1], (HIST_N - 1) * BUF_LEN);
        a->hist_count--;
    }
    strlcpy(a->history[a->hist_count++], q, BUF_LEN);
}

static void show_text(App* a, const char* txt) {
    text_box_reset(a->box);
    text_box_set_font(a->box, TextBoxFontText);
    text_box_set_text(a->box, txt);
    view_dispatcher_switch_to_view(a->vd, ViewBox);
}

// run the router on a->buf and display the answer
static void run_query(App* a) {
    char question[BUF_LEN];
    strlcpy(question, a->buf, sizeof(question));
    do_universal(a);
    hist_push(a, question);
    append_log(a, question);
    show_text(a, furi_string_get_cstr(a->out));
    const char* txt = furi_string_get_cstr(a->out);
    const char* sep = strstr(txt, "BLIVZY AI:\n");
    blip_speak(a, sep ? sep + 11 : txt);
}

static void menu_cb(void* ctx, uint32_t idx) {
    App* a = ctx;
    switch(idx) {
    case MenuChat:
        enter_kbd(a);
        break;
    case MenuHist:
        build_hist(a);
        view_dispatcher_switch_to_view(a->vd, ViewHist);
        break;
    case MenuExamples:
        strlcpy(a->buf, "help", sizeof(a->buf));
        run_query(a);
        break;
    case MenuFact:
        strlcpy(a->buf, "fun fact", sizeof(a->buf));
        run_query(a);
        break;
    case MenuLang:
        a->hu = !a->hu;
        build_menu(a, MenuLang);
        break;
    case MenuSound:
        a->sound = !a->sound;
        build_menu(a, MenuSound);
        beep(a, 880.0f, 40);
        break;
    case MenuLog:
        a->log_on = !a->log_on;
        build_menu(a, MenuLog);
        break;
    default:
        furi_string_printf(
            a->out,
            "BLIVZY AI v" VERSION "\nMade by Blivzy\n\nOne universal offline chat in English and Hungarian: maths (also in words), equations, unit conversion, number tools, geometry, writing templates, small talk and a big typo-tolerant knowledge base. Hungarian mode has a QWERTZ keyboard with accent letters.\n\nThis session: %lu questions.\n\nNot a cloud AI: answers come from built-in tools and kb_en.txt / kb_hu.txt. Add your own lines to teach it more. Chat log (optional) is saved to SD/apps_data/blivzy_ai/chat_log.txt.",
            (unsigned long)a->questions);
        show_text(a, furi_string_get_cstr(a->out));
        break;
    }
}

static void hist_cb(void* ctx, uint32_t idx) {
    App* a = ctx;
    if(idx == 99 || idx >= a->hist_count) {
        view_dispatcher_switch_to_view(a->vd, ViewMenu);
        return;
    }
    strlcpy(a->buf, a->history[(a->hist_count - 1) - idx], sizeof(a->buf));
    run_query(a);
}

static bool custom_cb(void* ctx, uint32_t evt) {
    App* a = ctx;
    if(evt != EvtInputDone) return false;
    run_query(a);
    kbd_reset(a, true); // clear the input for the next message
    return true;
}

static uint32_t nav_menu(void* ctx) {
    UNUSED(ctx);
    return ViewMenu;
}
static uint32_t nav_kbd(void* ctx) {
    UNUSED(ctx);
    return ViewKbd; // Back from an answer returns to the chat keyboard
}
static uint32_t nav_exit(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

int32_t blivzy_ai_main(void* p) {
    UNUSED(p);
    App* a = malloc(sizeof(App));
    memset(a, 0, sizeof(App));
    a->sound = true;
    a->out = furi_string_alloc();

    Storage* st = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(st, DATA_DIR);
    furi_record_close(RECORD_STORAGE);

    a->gui = furi_record_open(RECORD_GUI);
    a->vd = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(a->vd, a);
    view_dispatcher_set_custom_event_callback(a->vd, custom_cb);

    a->splash = view_alloc();
    view_set_context(a->splash, a);
    view_set_draw_callback(a->splash, splash_draw);
    view_set_input_callback(a->splash, splash_input);
    view_set_previous_callback(a->splash, nav_exit);

    a->kbd = view_alloc();
    view_set_context(a->kbd, a);
    view_allocate_model(a->kbd, ViewModelTypeLocking, sizeof(KbdModel));
    view_set_draw_callback(a->kbd, kbd_draw);
    view_set_input_callback(a->kbd, kbd_input);
    view_set_previous_callback(a->kbd, nav_menu);

    a->menu = submenu_alloc();
    build_menu(a, 0);
    view_set_previous_callback(submenu_get_view(a->menu), nav_exit);

    a->hist = submenu_alloc();
    view_set_previous_callback(submenu_get_view(a->hist), nav_menu);

    a->box = text_box_alloc();
    view_set_previous_callback(text_box_get_view(a->box), nav_kbd);

    view_dispatcher_add_view(a->vd, ViewSplash, a->splash);
    view_dispatcher_add_view(a->vd, ViewMenu, submenu_get_view(a->menu));
    view_dispatcher_add_view(a->vd, ViewBox, text_box_get_view(a->box));
    view_dispatcher_add_view(a->vd, ViewKbd, a->kbd);
    view_dispatcher_add_view(a->vd, ViewHist, submenu_get_view(a->hist));

    view_dispatcher_attach_to_gui(a->vd, a->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(a->vd, ViewSplash);
    view_dispatcher_run(a->vd);

    view_dispatcher_remove_view(a->vd, ViewSplash);
    view_dispatcher_remove_view(a->vd, ViewMenu);
    view_dispatcher_remove_view(a->vd, ViewBox);
    view_dispatcher_remove_view(a->vd, ViewKbd);
    view_dispatcher_remove_view(a->vd, ViewHist);
    view_free(a->splash);
    view_free_model(a->kbd);
    view_free(a->kbd);
    submenu_free(a->menu);
    submenu_free(a->hist);
    text_box_free(a->box);
    view_dispatcher_free(a->vd);
    furi_record_close(RECORD_GUI);
    furi_string_free(a->out);
    free(a);
    return 0;
}
