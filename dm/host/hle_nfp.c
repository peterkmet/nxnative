// Virtual amiibo: the amiibo file store (this part) and the nn::nfp take-overs (added later).
// One JSON file per amiibo, <amiibo folder>/<amiibo ID>.json, in the format of Ryujinx's VirtualAmiiboFile
// (FileVersion, TagUuid, AmiiboId, FirstWriteDate, LastWriteDate, WriteCounter, ApplicationAreas, NickName), so that Ryujinx's files can be used as they are.
// The folder is $DM_AMIIBO_DIR, else <$DM_SAVE>_amiibo, else ./amiibo. Unknown top-level members are kept verbatim and written back.
#include "hle.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

typedef struct { uint32_t id; uint8_t *data; size_t size; } amiibo_area;
typedef struct { char id[17]; uint8_t uuid[9]; int uuid_len; char first[40], last[40]; uint16_t writes; char nick[64]; amiibo_area *areas; int n_areas; char *extra; } amiibo_file;

// ---- base64 ----
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

typedef struct { char *p; size_t n, cap; } sbuf;
static void sb_put(sbuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void sb_str(sbuf *b, const char *s) { sb_put(b, s, strlen(s)); }

static void b64_encode(sbuf *b, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        char o[4] = { B64[v >> 18 & 63], B64[v >> 12 & 63], i + 1 < n ? B64[v >> 6 & 63] : '=', i + 2 < n ? B64[v & 63] : '=' };
        sb_put(b, o, 4);
    }
}
// Returns a heap buffer (never NULL on success) and its size; NULL on bad input.
static uint8_t *b64_decode(const char *s, size_t *out) {
    size_t L = strlen(s); uint8_t *r = malloc(L / 4 * 3 + 3); size_t n = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < L; i++) {
        if (s[i] == '=') { if (i + 2 < L || (i + 1 < L && s[i + 1] != '=')) goto bad; break; }
        const char *q = s[i] ? strchr(B64, s[i]) : NULL;
        if (!q) goto bad;
        acc = acc << 6 | (uint32_t)(q - B64); bits += 6;
        if (bits >= 8) { bits -= 8; r[n++] = acc >> bits & 255; }
    }
    *out = n; return r;
bad: free(r); return NULL;
}

// ---- JSON reader for one object ----
static void js_ws(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++; }
static int js_hex4(const char *p, uint32_t *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) { char c = p[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (d < 0) return -1; *v = *v << 4 | d; }
    return 0;
}
static int utf8_put(char *o, uint32_t c) {
    if (c < 0x80) { o[0] = c; return 1; }
    if (c < 0x800) { o[0] = 0xc0 | c >> 6; o[1] = 0x80 | (c & 63); return 2; }
    if (c < 0x10000) { o[0] = 0xe0 | c >> 12; o[1] = 0x80 | (c >> 6 & 63); o[2] = 0x80 | (c & 63); return 3; }
    o[0] = 0xf0 | c >> 18; o[1] = 0x80 | (c >> 12 & 63); o[2] = 0x80 | (c >> 6 & 63); o[3] = 0x80 | (c & 63); return 4;
}
// Parses a string at *p into a heap buffer; NULL on a syntax error.
static char *js_string(const char **p) {
    if (**p != '"') return NULL;
    const char *s = *p + 1; char *r = malloc(strlen(s) + 1); size_t n = 0;
    while (*s && *s != '"') {
        if (*s != '\\') { r[n++] = *s++; continue; }
        s++;
        switch (*s) {
        case '"': case '\\': case '/': r[n++] = *s++; break;
        case 'b': r[n++] = '\b'; s++; break;
        case 'f': r[n++] = '\f'; s++; break;
        case 'n': r[n++] = '\n'; s++; break;
        case 'r': r[n++] = '\r'; s++; break;
        case 't': r[n++] = '\t'; s++; break;
        case 'u': {
            uint32_t c, lo;
            if (js_hex4(s + 1, &c)) goto bad;
            s += 5;
            if (c >= 0xd800 && c < 0xdc00 && s[0] == '\\' && s[1] == 'u' && !js_hex4(s + 2, &lo) && lo >= 0xdc00 && lo < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00); s += 6; }
            else if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
            n += utf8_put(r + n, c); break; }
        default: goto bad;
        }
    }
    if (*s != '"') goto bad;
    r[n] = 0; *p = s + 1; return r;
bad: free(r); return NULL;
}
// Skips one value of any kind; returns 0 on success.
static int js_skip(const char **p, int depth) {
    js_ws(p);
    if (depth > 64) return -1;
    if (**p == '"') { char *s = js_string(p); if (!s) return -1; free(s); return 0; }
    if (**p == '{' || **p == '[') {
        char close = **p == '{' ? '}' : ']'; int obj = **p == '{';
        (*p)++; js_ws(p);
        if (**p == close) { (*p)++; return 0; }
        for (;;) {
            js_ws(p);
            if (obj) { char *k = js_string(p); if (!k) return -1; free(k); js_ws(p); if (*(*p)++ != ':') return -1; }
            if (js_skip(p, depth + 1)) return -1;
            js_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == close) { (*p)++; return 0; }
            return -1;
        }
    }
    const char *s = *p;
    while (**p && strchr("+-.0123456789eEtrufalsn", **p)) (*p)++;
    return *p == s ? -1 : 0;
}
static int js_uint(const char **p, unsigned long long *v) {
    js_ws(p);
    if (**p < '0' || **p > '9') return -1;
    char *e; errno = 0; *v = strtoull(*p, &e, 10);
    if (errno) return -1;
    *p = e; return 0;
}
static int js_null(const char **p) { js_ws(p); if (strncmp(*p, "null", 4)) return 0; *p += 4; return 1; }

static void amiibo_free(amiibo_file *f) {
    for (int i = 0; i < f->n_areas; i++) free(f->areas[i].data);
    free(f->areas); free(f->extra);
    f->areas = NULL; f->extra = NULL; f->n_areas = 0;
}

static int parse_areas(const char **p, amiibo_file *f) {
    js_ws(p);
    if (js_null(p)) return 0;
    if (*(*p)++ != '[') return -1;
    js_ws(p);
    if (**p == ']') { (*p)++; return 0; }
    for (;;) {
        js_ws(p);
        if (*(*p)++ != '{') return -1;
        amiibo_area a = {0}; int have_id = 0;
        for (;;) {
            js_ws(p);
            char *k = js_string(p); if (!k) goto bad;
            js_ws(p);
            if (*(*p)++ != ':') { free(k); goto bad; }
            int rc;
            if (!strcmp(k, "ApplicationAreaId")) { unsigned long long v; rc = js_uint(p, &v); if (!rc && v > 0xffffffffull) rc = -1; a.id = (uint32_t)v; have_id = !rc; }
            else if (!strcmp(k, "ApplicationArea")) {
                if (js_null(p)) rc = 0;
                else { char *s = js_string(p); rc = -1; if (s) { free(a.data); a.data = b64_decode(s, &a.size); rc = a.data ? 0 : -1; free(s); } }
            } else rc = js_skip(p, 0);
            free(k);
            if (rc) goto bad;
            js_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == '}') { (*p)++; break; }
            goto bad;
        }
        if (!have_id) goto bad;
        f->areas = realloc(f->areas, (f->n_areas + 1) * sizeof *f->areas);
        f->areas[f->n_areas++] = a;
        js_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == ']') { (*p)++; return 0; }
        return -1;
    bad: free(a.data); return -1;
    }
}

static void copy_str(char *dst, size_t cap, const char *s) { snprintf(dst, cap, "%s", s); }

// Parses the text of one amiibo file into f (f->id is set by the caller); returns 0 on success.
static int amiibo_parse(const char *text, amiibo_file *f) {
    const char *p = text; sbuf extra = {0};
    js_ws(&p);
    if (*p++ != '{') return -1;
    js_ws(&p);
    if (*p == '}') { p++; goto done; }
    for (;;) {
        js_ws(&p);
        const char *kstart = p;
        char *k = js_string(&p); if (!k) goto bad;
        js_ws(&p);
        if (*p++ != ':') { free(k); goto bad; }
        int rc = 0; unsigned long long v;
        if (!strcmp(k, "TagUuid")) {
            if (js_null(&p)) f->uuid_len = 0;
            else { char *s = (js_ws(&p), js_string(&p)); size_t n; uint8_t *d = s ? b64_decode(s, &n) : NULL; free(s); if (!d || n > sizeof f->uuid) rc = -1; else { memcpy(f->uuid, d, n); f->uuid_len = (int)n; } free(d); }
        } else if (!strcmp(k, "FirstWriteDate") || !strcmp(k, "LastWriteDate") || !strcmp(k, "NickName") || !strcmp(k, "AmiiboId")) {
            if (js_null(&p)) { if (k[0] == 'N') f->nick[0] = 0; }
            else {
                char *s = (js_ws(&p), js_string(&p));
                if (!s) rc = -1;
                else if (k[0] == 'F') copy_str(f->first, sizeof f->first, s);
                else if (k[0] == 'L') copy_str(f->last, sizeof f->last, s);
                else if (k[0] == 'N') copy_str(f->nick, sizeof f->nick, s);
                free(s);
            }
        } else if (!strcmp(k, "WriteCounter")) { rc = js_uint(&p, &v); if (!rc) f->writes = (uint16_t)v; }
        else if (!strcmp(k, "ApplicationAreas")) rc = parse_areas(&p, f);
        else if (!strcmp(k, "FileVersion")) rc = js_uint(&p, &v);
        else {
            js_ws(&p); rc = js_skip(&p, 0);
            if (!rc) { if (extra.n) sb_str(&extra, ","); sb_put(&extra, kstart, (size_t)(p - kstart)); }
        }
        free(k);
        if (rc) goto bad;
        js_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        goto bad;
    }
done:
    js_ws(&p);
    if (*p) goto bad;
    f->extra = extra.p;
    return 0;
bad: free(extra.p); amiibo_free(f); return -1;
}

// ---- files ----
static const char *amiibo_dir(void) {
    static char dir[1024];
    const char *e = getenv("DM_AMIIBO_DIR"), *s = getenv("DM_SAVE");
    if (e && *e) snprintf(dir, sizeof dir, "%s", e);
    else if (s && *s) { size_t n = strlen(s); while (n > 1 && s[n - 1] == '/') n--; snprintf(dir, sizeof dir, "%.*s_amiibo", (int)n, s); }
    else snprintf(dir, sizeof dir, "./amiibo");
    mkdir(dir, 0755);
    return dir;
}

static int amiibo_id_ok(const char *id) {
    if (!id || strlen(id) != 16) return 0;
    for (int i = 0; i < 16; i++) if (!strchr("0123456789abcdefABCDEF", id[i])) return 0;
    return 1;
}

static void amiibo_now(char *out, size_t cap) {
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    long off = tm.tm_gmtoff; char sign = off < 0 ? '-' : '+'; if (off < 0) off = -off;
    snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d%c%02ld:%02ld", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, sign, off / 3600, off / 60 % 60);
}

static void js_put_string(sbuf *b, const char *s) {
    sb_str(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s; char e[8];
        if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = c; sb_put(b, e, 2); }
        else if (c < 0x20) { snprintf(e, sizeof e, "\\u%04x", c); sb_str(b, e); }
        else sb_put(b, s, 1);
    }
    sb_str(b, "\"");
}

static int amiibo_save(const amiibo_file *f) {
    sbuf b = {0}; char num[96];
    sb_str(&b, "{\"FileVersion\":0,\"TagUuid\":\"");
    b64_encode(&b, f->uuid, (size_t)f->uuid_len);
    sb_str(&b, "\",\"AmiiboId\":"); js_put_string(&b, f->id);
    sb_str(&b, ",\"FirstWriteDate\":"); js_put_string(&b, f->first);
    sb_str(&b, ",\"LastWriteDate\":"); js_put_string(&b, f->last);
    snprintf(num, sizeof num, ",\"WriteCounter\":%u,\"ApplicationAreas\":[", f->writes); sb_str(&b, num);
    for (int i = 0; i < f->n_areas; i++) {
        snprintf(num, sizeof num, "%s{\"ApplicationAreaId\":%u,\"ApplicationArea\":\"", i ? "," : "", f->areas[i].id); sb_str(&b, num);
        b64_encode(&b, f->areas[i].data, f->areas[i].size);
        sb_str(&b, "\"}");
    }
    sb_str(&b, "],\"NickName\":"); js_put_string(&b, f->nick);
    if (f->extra && *f->extra) { sb_str(&b, ","); sb_str(&b, f->extra); }
    sb_str(&b, "}");
    char path[1200], tmp[1210];
    snprintf(path, sizeof path, "%s/%s.json", amiibo_dir(), f->id); snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *fp = fopen(tmp, "wb"); int ok = fp && fwrite(b.p, 1, b.n, fp) == b.n;
    if (fp && fclose(fp)) ok = 0;
    free(b.p);
    if (!ok || rename(tmp, path)) { LOG("amiibo: cannot write %s (errno=%d)", path, errno); unlink(tmp); return -1; }
    return 0;
}

// Cuts s to at most n Unicode characters (UTF-8).
static void utf8_cut(char *s, int n) {
    for (; *s; s++) if (((unsigned char)*s & 0xc0) != 0x80 && n-- == 0) { *s = 0; return; }
}

static int amiibo_load(const char *id, const char *default_nick, amiibo_file *f) {
    memset(f, 0, sizeof *f);
    if (!amiibo_id_ok(id)) { LOG("amiibo: bad amiibo ID"); return -1; }
    snprintf(f->id, sizeof f->id, "%s", id);
    char path[1200]; snprintf(path, sizeof path, "%s/%s.json", amiibo_dir(), id);
    FILE *fp = fopen(path, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
        char *text = n >= 0 && n < (16 << 20) ? malloc((size_t)n + 1) : NULL;
        int ok = text && fread(text, 1, (size_t)n, fp) == (size_t)n;
        fclose(fp);
        if (ok) { text[n] = 0; ok = !amiibo_parse(text, f); }
        free(text);
        if (ok) { if (!f->first[0]) amiibo_now(f->first, sizeof f->first); if (!f->last[0]) copy_str(f->last, sizeof f->last, f->first); return 0; }
        char bad[1210]; snprintf(bad, sizeof bad, "%s.bad", path);
        LOG("amiibo: %s does not parse; moved to %s, a new file replaces it", path, bad);
        rename(path, bad);
        memset(f, 0, sizeof *f); snprintf(f->id, sizeof f->id, "%s", id);
    }
    amiibo_now(f->first, sizeof f->first); copy_str(f->last, sizeof f->last, f->first);
    copy_str(f->nick, sizeof f->nick, default_nick ? default_nick : ""); utf8_cut(f->nick, 10);
    return amiibo_save(f);
}
