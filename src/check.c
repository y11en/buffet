#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "buffet.h"
#include "log.h"
#include "util.h"

#ifdef NDEBUG
#undef NDEBUG
#endif


#define alphalen 1024/2
const char *alpha;
char tmp[alphalen+1];

// put a slice of alpha into tmp
static char* take (size_t off, size_t len) {
    assert(off+len <= alphalen);
    memcpy(tmp, alpha+off, len);
    tmp[len] = 0;
    return tmp;
}

#define assert_int(val, exp) { if ((int)(val) != (int)(exp)) { \
fprintf(stderr, "%d: %s:%d != %d\n", __LINE__, #val, (int)(val), (int)(exp)); \
exit(EXIT_FAILURE);}}

#define assert_str(val, exp) { if (strcmp((val), (exp))) {\
fprintf(stderr, "%d: %s:'%s' != '%s'\n", __LINE__, #val, (val), (exp)); \
exit(EXIT_FAILURE);}}

#define assert_stn(val, exp, n) { if (strncmp((val), (exp), (n))) {\
fprintf(stderr, \
    "%d: %d bytes %s:'%s' != '%s'\n", __LINE__, (int)(n), #val, (val), (exp)); \
exit(EXIT_FAILURE);}}

#define check_cstr(buf, off, len) {\
    bool mustfree; \
    const char *cstr = buffet_cstr(buf, &mustfree); \
    const char *expstr = take(off,len); \
    assert_str (cstr, expstr); \
    if (mustfree) free((char*)cstr); \
}

void check_export (Buffet *buf, size_t off, size_t len) {
    char *export = buffet_export(buf);
    const char *expstr = take(off,len);
    assert_str (export, expstr);
    free(export);
}

#define check(buf, off, len) \
    assert_int (buffet_len(&buf), (len)); \
    assert_stn (buffet_data(&buf), alpha+off, len); \
    check_cstr (&buf, off, len); \
    check_export (&buf, off, len)

#define around(fun, n) fun((n)-1); fun(n); fun((n)+1);

#define serie(fun, off) \
    fun (off, 0); \
    fun (off, 1); \
    fun (off, 8); \
    fun (off, BUFFET_SSOMAX-1); \
    fun (off, BUFFET_SSOMAX); \
    fun (off, BUFFET_SSOMAX+1); \
    fun (off, 24); \
    fun (off, 32); \
    fun (off, 48); \
    fun (off, 64); 

//=============================================================================
void unew (size_t cap) {
    Buffet buf = buffet_new(cap); 
    check(buf, 0, 0);
    buffet_free(&buf); 
}

void new() 
{ 
    unew (0);
    unew (1);
    unew (8);
    around (unew, BUFFET_SSOMAX);
    around (unew, sizeof(Buffet));
    around (unew, 32);
    around (unew, 64);
    around (unew, 1024);
    around (unew, 4096);
}

//=============================================================================
void umemcopy (size_t off, size_t len)
{
    Buffet buf = buffet_memcopy (alpha+off, len);
    check(buf, off, len);    
    buffet_free(&buf);
}

void memcopy() 
{
    serie(umemcopy, 0);
    serie(umemcopy, 8);
}

//=============================================================================
void umemview (size_t off, size_t len)
{
    Buffet buf = buffet_memview (alpha+off, len);
    check(buf, off, len);
    buffet_free(&buf);
}

void memview() 
{
    serie(umemview, 0);
    serie(umemview, 8);
}

//=============================================================================
void ucopy (size_t off, size_t len) {
    Buffet src = buffet_memcopy (alpha, alphalen);
    Buffet buf = buffet_copy(&src, off, len);
    check(buf, off, len);
    buffet_free(&buf);
    buffet_free(&src);
}

void copy() {
    serie(ucopy, 0);
    serie(ucopy, 8);
    ucopy (0, alphalen);
}

//=============================================================================
void uclone (ptrdiff_t off, size_t len) {
    Buffet src = buffet_memcopy (alpha+off, len);
    Buffet buf = buffet_dup(&src);
    check(buf, off, len);
    buffet_free(&buf);
    buffet_free(&src);
}

void clone() {
    serie(uclone, 0);
    serie(uclone, 8);
}

//=============================================================================
#define viewcheck(src, srclen, off, len) \
    Buffet view = buffet_view (&src, off, len); \
    size_t explen = len; \
    if (off>=srclen) explen = 0; \
    else if (off+len>=srclen) explen = srclen-off; \
    assert_int (buffet_len(&view), explen); \
    assert_stn (buffet_data(&view), alpha+off, explen); \
    buffet_free(&view); \
    buffet_free(&src);

#define view_own(srclen, off, len) { \
    Buffet src = buffet_memcopy (alpha, srclen); \
    viewcheck(src, srclen, off, len) \
}

#define view_ref(srclen, off, len) { \
    Buffet buf = buffet_memcopy (alpha, alphalen); \
    Buffet src = buffet_view (&buf, 0, srclen); \
    viewcheck(src, srclen, off, len) \
    buffet_free(&buf); \
}

#define view_vue(srclen, off, len) { \
    Buffet src = buffet_memview (alpha, srclen); \
    viewcheck(src, srclen, off, len) \
}

#define VIEWCASES(fun, srclen)\
fun (srclen, 0, 0); \
fun (srclen, 0, srclen/2);  /* part */ \
fun (srclen, 0, srclen);    /* full */ \
fun (srclen, 0, srclen+1);  /* beyond */ \
fun (srclen, 2, srclen/2);  /* part */ \
fun (srclen, 2, srclen-2);  /* till end */ \
fun (srclen, 2, srclen+1);  /* beyond */ \
fun (srclen, srclen, 0);    /* bad off */ \
fun (srclen, srclen, 1);    /* bad off */ \
fun (srclen, srclen+1, 0);  /* bad off */ \
fun (srclen, srclen+1, 1);  /* bad off */ \

void view() {
    view_own (0, 0, 0);
    VIEWCASES (view_own, 8) // sso
    VIEWCASES (view_own, 60) // own
    VIEWCASES (view_ref, 8)
    VIEWCASES (view_ref, 60)
    VIEWCASES (view_vue, 8)
    VIEWCASES (view_vue, 60)
}

//=============================================================================

void append_new (size_t cap, size_t len)
{
    Buffet buf = buffet_new (cap);
    buffet_append (&buf, alpha, len);
    check(buf, 0, len);
    buffet_free(&buf);    
}

#define append_memcopy(initlen, len) {\
    Buffet buf = buffet_memcopy (alpha, initlen); \
    buffet_append (&buf, alpha+initlen, len); \
    size_t totlen = initlen+len;\
    check(buf, 0, totlen);\
    buffet_free(&buf);    \
}

void append_memview (size_t initlen, size_t len)
{
    size_t totlen = initlen+len;
    Buffet buf = buffet_memview (alpha, initlen);
    buffet_append (&buf, alpha+initlen, len);

    check(buf, 0, totlen);
    
    buffet_free(&buf);    
}

void append_view (size_t initlen, size_t len)
{
    size_t totlen = initlen+len;
    Buffet src = buffet_memcopy (alpha, initlen);
    Buffet ref = buffet_view (&src, 0, initlen);
    buffet_append (&ref, alpha+initlen, len);

    check(ref, 0, totlen); // 
    
    buffet_free(&ref); 
    buffet_free(&src);    
}
// justas idea
void append_self (size_t len)
{
    Buffet buf = buffet_memcopy (alpha, len);
    size_t finlen = 2*len;
    memcpy(tmp, alpha, len);
    memcpy(tmp+len, alpha, len);
    tmp[finlen] = 0;

    buffet_append (&buf, buffet_data(&buf), buffet_len(&buf));
    assert_int(buffet_len(&buf), finlen);
    assert_str(buffet_data(&buf), tmp);

    buffet_free(&buf);
}

void append()
{
    append_new (0, 0);
    append_new (0, 8);
    append_new (0, 40);
    append_new (8, 0);
    append_new (8, 5);
    append_new (8, 6);
    append_new (8, 7);
    append_new (8, 8);
    append_new (40, 0);    
    append_new (40, 8);
    append_new (40, 40);

    append_memcopy (4, 4);
    append_memcopy (8, 5);
    append_memcopy (8, 6);
    append_memcopy (8, 7);
    append_memcopy (8, 8);
    append_memcopy (8, 20);
    append_memcopy (20, 20);

    append_memview (4, 4);
    append_memview (8, 5);
    append_memview (8, 6);
    append_memview (8, 7);
    append_memview (8, 8);
    append_memview (8, 20);
    append_memview (20, 20);

    append_view (8, 4);
    append_view (8, 20);
    append_view (20, 20);

    // append_self (0);
    // append_self (4);
    // append_self (10);
    // append_self (16);

    // todo crazy self cases : crossing NUL, garbage only, ...
}

//==============================================================================

#define usploin(src, sep) { \
    size_t srclen = strlen(src);\
    size_t seplen = strlen(sep);\
    int cnt; \
    Buffet* parts = buffet_split (src, srclen, sep, seplen, &cnt); \
    Buffet joined = buffet_join (parts, cnt, sep, seplen); \
    assert_str (buffet_data(&joined), src); \
    assert_int (buffet_len(&joined), srclen); \
    free(parts);\
    buffet_free(&joined);\
}

#define SPLOIN(a,b,sep) \
    usploin ("", #sep); \
    usploin (#sep, #sep); \
    usploin (#a, #sep); \
    usploin (#a #sep,  #sep);  \
    usploin (#a #sep #b,  #sep);  \
    usploin (#a #sep #b #sep,  #sep);  \
    usploin (#sep #a,  #sep); \
    usploin (#sep #a #sep,  #sep);  \
    usploin (#sep #a #sep #b,  #sep);  \
    usploin (#sep #a #sep #b #sep,  #sep);  \
    usploin (#a #sep #sep,  #sep);  \
    usploin (#a #sep #sep #b,  #sep);  \
    usploin (#a #sep #sep #b #sep #sep,  #sep);  \
    usploin (#sep #sep #a,  #sep); \
    usploin (#sep #sep #a #sep #sep,  #sep);  \
    usploin (#sep #sep #a #sep #sep #b,  #sep);  \
    usploin (#sep #sep #a #sep #sep #b #sep #sep, #sep); 

void splitjoin() 
{ 
    SPLOIN (a, b, |)
    SPLOIN (a, b, ||)
    SPLOIN (foo, bar, |)
    SPLOIN (foo, bar, ||)
}

//=============================================================================

#define check_free(buf, exprc) {\
    bool rc = buffet_free(buf); \
    assert_int (rc, exprc); \
    if (exprc) { \
        assert_int (buffet_len(buf), 0); \
        assert_str (buffet_data(buf), ""); \
        bool mustfree; \
        const char *cstr = buffet_cstr(buf,&mustfree); \
        assert_str (cstr, ""); \
        assert(!mustfree); \
    } \
}

#define free_new(len) { \
    Buffet buf = buffet_new (len); \
    check_free(&buf, true); \
}
#define free_memcopy(len) { \
    Buffet buf = buffet_memcopy (alpha, len); \
    check_free(&buf, true); \
}
#define free_memview(len) { \
    Buffet buf = buffet_memview (alpha, len); \
    check_free(&buf, true); \
}
#define free_view(len) { \
    Buffet own = buffet_memcopy (alpha, 40); \
    Buffet ref = buffet_view (&own, 0, len); \
    check_free(&ref, false); \
    check_free(&own, true); \
}
#define free_copy(len) { \
    Buffet own = buffet_memcopy (alpha, 40); \
    Buffet cpy = buffet_copy (&own, 0, len); \
    check_free(&cpy, true); \
    check_free(&own, true); \
}

void free_()
{
    free_new(0)
    free_new(8)
    free_new(40)
    free_memcopy(0)
    free_memcopy(8)
    free_memcopy(40)
    free_memview(0)
    free_memview(8)
    free_memview(40)
    free_copy(0)
    free_copy(8)
    free_copy(40)
    free_view(0)
    free_view(8)
    free_view(40)
}

//=============================================================================
void double_free(size_t len) {
    Buffet buf = buffet_memcopy (alpha, len);
    check_free(&buf, true);
    check_free(&buf, true);
}
#define double_free_ref(srclen, len) { \
    Buffet src = buffet_memcopy (alpha, srclen); \
    Buffet ref = buffet_view (&src, 0, len); \
    check_free(&ref, false); \
    check_free(&ref, true); \
    check_free(&src, true); \
}
#define free_alias(len, exp) { \
    Buffet src = buffet_memcopy (alpha, len); \
    Buffet alias = src; \
    check_free(&src, true); \
    check_free(&alias, exp); \
}
#define free_ref_alias(len, freeref, freealias, freeown) { \
    Buffet own = buffet_memcopy (alpha, 40); \
    Buffet ref = buffet_view (&own, 0, len); \
    Buffet alias = ref; \
    check_free(&ref, freeref); \
    check_free(&alias, freealias); \
    check_free(&own, freeown); \
}
#define free_own_before_view(len, freeown, freeref) { \
    Buffet own = buffet_memcopy (alpha, 40); \
    Buffet ref = buffet_view (&own, 0, len); \
    check_free(&own, freeown); \
    check_free(&ref, freeref); \
}

void view_after_reloc (size_t initlen)
{
    Buffet src = buffet_memcopy (alpha, initlen);
    Buffet ref = buffet_view (&src, 0, initlen);

    buffet_append (&src, alpha, alphalen);
    check(ref, 0, initlen);
    
    buffet_free(&ref); 
    buffet_free(&src);    
}

void view_after_free (size_t initlen)
{
    Buffet src = buffet_memcopy (alpha, initlen);
    Buffet ref = buffet_view (&src, 0, initlen);

    buffet_free(&src);    
    check(ref, 0, initlen);
    
    buffet_free(&ref); 
}

void view_alias_after_free (size_t initlen)
{
    Buffet src = buffet_memcopy (alpha, initlen);
    Buffet alias = src;
    buffet_free(&src);    
    Buffet ref = buffet_view (&alias, 0, initlen);

    check(ref, 0, 0);
    
    buffet_free(&ref); 
}

void append_view_after_reloc (size_t initlen, size_t len)
{
    Buffet src = buffet_memcopy (alpha, initlen);
    Buffet ref = buffet_view (&src, 0, initlen);
    // buffet_debug(&ref);
    
    // trigger reloc
    const char *loc = buffet_data(&src);
    buffet_append (&src, alpha, alphalen);
    if (buffet_data(&src) == loc) {
        LOG("append_view_after_reloc : not relocated, skipping.");
        goto fin;
    }
    
    buffet_append (&ref, alpha+initlen, len);
    check(ref, 0, initlen+len);
    
    fin:
    buffet_free(&ref); 
    buffet_free(&src);    
}

void danger()
{
    double_free(0);
    double_free(8);
    double_free(40);

    double_free_ref(8, 4);
    double_free_ref(40, 8);
    double_free_ref(40, 20);

    free_alias(8, true);
    free_alias(40, true);
    
    free_own_before_view (0, false, true)
    free_own_before_view (8, false, true)
    free_own_before_view (40, false, true)

    free_ref_alias (0,  false, true, true)
    free_ref_alias (8,  false, true, true)
    free_ref_alias (40, false, true, true)

    view_after_reloc(8);

    view_after_free(8);
    view_after_free(40);

    view_alias_after_free(40);

    append_view_after_reloc(8,4);
    append_view_after_reloc(8,20);
    append_view_after_reloc(20,20);
}

//=============================================================================
void zero()
{
    Buffet buf = BUFFET_ZERO;

    assert (!buf.ptr.data);
    assert (!buf.ptr.len);
    assert (!buf.ptr.off);
    assert (!buf.ptr.tag);

    assert (!strcmp(buf.sso.data, ""));
    assert (!buf.sso.len);
    assert (!buf.sso.tag);
}

//=============================================================================
#define GREEN "\033[32;1m"
#define RESET "\033[0m"

#define run(name) { \
    printf("%-12s ", #name); fflush(stdout);\
    name(); \
    LOG(GREEN "OK" RESET); \
}

int main()
{
    LOG("unit tests... ");
    alpha = repeat(ALPHA64, alphalen);

    run(zero);
    run(new);
    run(memcopy);
    run(memview);
    run(copy);
    run(view);
    run(clone);
    run(append);
    run(splitjoin);
    run(free_);
    run(danger);
    
    LOG(GREEN "unit tests OK" RESET);
    return 0;
}