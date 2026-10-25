// Amiibo file store (hle_nfp.c): new file, Ryujinx's VirtualAmiiboFile form read and written back, unparseable file moved aside
#include "../hle_nfp.c"

static int checks, fails;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define ID "0100000000040002"
static char g_dir[1024];

static void new_dir(void) {
    const char *t = getenv("TMPDIR"); snprintf(g_dir, sizeof g_dir, "%s/t_nfp.XXXXXX", t && *t ? t : "/tmp");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); exit(2); }
    setenv("DM_AMIIBO_DIR", g_dir, 1);
}
static void put_file(const char *name, const char *text) {
    char p[1200]; snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *f = fopen(p, "wb"); if (!f) { perror("fopen"); exit(2); }
    fputs(text, f); fclose(f);
}
static char *get_file(const char *name) {
    static char buf[4096]; char p[1200]; snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); return buf;
}

static void test_new_file(void) {
    new_dir(); amiibo_file f;
    CHECK(amiibo_load(ID, "Wolf Link", &f) == 0, "load of a missing file succeeds");
    CHECK(get_file(ID ".json") != NULL, "the new file is saved");
    CHECK(f.uuid_len == 0 && f.writes == 0 && f.n_areas == 0, "new file is empty");
    CHECK(strcmp(f.nick, "Wolf Link") == 0, "nickname is the default");
    amiibo_free(&f);
    CHECK(amiibo_load(ID, "x", &f) == 0 && strcmp(f.nick, "Wolf Link") == 0, "the saved file loads again");
    amiibo_free(&f);
    CHECK(amiibo_load(ID, "x", &f) == 0, "reload");
    amiibo_free(&f);
    CHECK(amiibo_load("0100000000040003", "Ä\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4", &f) == 0 && strlen(f.nick) == 20, "default nickname is cut to 10 characters");
    amiibo_free(&f);
}

static void test_ryujinx_file(void) {
    new_dir();
    const char *src = "{\"FileVersion\":0,\"TagUuid\":\"AQIDBAUGBwgJ\",\"AmiiboId\":\"" ID "\",\"FirstWriteDate\":\"2024-05-01T12:34:56.789+02:00\",\"LastWriteDate\":\"2024-05-02T08:00:00+02:00\",\"WriteCounter\":3,\"ApplicationAreas\":[{\"ApplicationAreaId\":1234,\"ApplicationArea\":\"3q2+7w==\"}],\"NickName\":null,\"Extra\":{\"a\":[1,2]}}";
    put_file(ID ".json", src);
    amiibo_file f, g;
    CHECK(amiibo_load(ID, "Wolf Link", &f) == 0, "load");
    static const uint8_t u[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    CHECK(f.uuid_len == 9 && !memcmp(f.uuid, u, 9), "uuid");
    CHECK(f.writes == 3, "write counter");
    CHECK(f.n_areas == 1 && f.areas[0].id == 1234 && f.areas[0].size == 4 && !memcmp(f.areas[0].data, "\xde\xad\xbe\xef", 4), "application area");
    CHECK(f.nick[0] == 0, "null nickname is empty");
    CHECK(amiibo_save(&f) == 0, "save");
    CHECK(amiibo_load(ID, "", &g) == 0, "load again");
    CHECK(!strcmp(g.first, f.first) && !strcmp(g.first, "2024-05-01T12:34:56.789+02:00") && !strcmp(g.last, "2024-05-02T08:00:00+02:00"), "dates");
    CHECK(g.uuid_len == 9 && !memcmp(g.uuid, u, 9) && g.writes == 3 && g.nick[0] == 0, "fields");
    CHECK(g.n_areas == 1 && g.areas[0].id == 1234 && g.areas[0].size == 4 && !memcmp(g.areas[0].data, "\xde\xad\xbe\xef", 4), "area after save");
    char *t = get_file(ID ".json");
    CHECK(t && strstr(t, "\"Extra\":{\"a\":[1,2]}"), "extra member kept");
    amiibo_free(&f); amiibo_free(&g);
}

static void test_bad_file(void) {
    new_dir();
    const char *bad = "{\"FileVersion\":0,\"TagUu";
    put_file(ID ".json", bad);
    amiibo_file f;
    CHECK(amiibo_load(ID, "Wolf Link", &f) == 0, "load of a bad file succeeds");
    CHECK(f.n_areas == 0 && f.writes == 0 && !strcmp(f.nick, "Wolf Link"), "new file");
    char *t = get_file(ID ".json.bad");
    CHECK(t && !strcmp(t, bad), "old text in .bad");
    amiibo_free(&f);
}

int main(void) {
    test_new_file(); test_ryujinx_file(); test_bad_file();
    if (fails) { fprintf(stderr, "t_nfp: %d of %d checks failed\n", fails, checks); return 1; }
    printf("t_nfp: %d checks ok\n", checks);
    return 0;
}
