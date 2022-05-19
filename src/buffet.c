/*
Buffet - All-inclusive Buffer for C
Copyright (C) 2022 - Francois Alcover <francois [on] alcover [dot] fr>
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>

#include "buffet.h"
#include "log.h"

typedef enum {SSO=0, OWN, REF, VUE} Tag;

typedef struct {
    uint32_t refcnt;
    volatile
    uint32_t canary;
    char     data[];
} Store;

#define CANARY 0xaaaaaaaa   // prevents accessing stale data
#define OVERALLOC 2         // growth factor
#define ZERO BUFFET_ZERO
#define DATAOFF offsetof(Store,data)
#define TAG(buf) ((buf)->sso.tag)
#define DATA(buf) ((buf)->ptr.data)
#define CAP(buf) ((buf)->ptr.aux)   
#define OFF(buf) ((buf)->ptr.aux)
#define TARGET(buf) ((Buffet*)DATA(buf))
#define STORE(buf) (Store*)(DATA(buf)-DATAOFF)
#define MEM(cap) (DATAOFF+cap+1)
#define ERASE(buf) memset(buf, 0, sizeof(Buffet))

#define ERR_ALLOC ERR("Failed allocation\n")
#define ERR_CANARY WARN("Bad canary. Double free ?\n")
#define ERR_REFCNT WARN("Bad refcnt. Rogue alias ?\n")

static const char*
getdata (const Buffet *buf, Tag tag) {
    return
    tag==SSO ? buf->sso.data :
    tag==REF ? DATA(TARGET(buf)) + OFF(buf) :
    DATA(buf);
}

static size_t
getlen (const Buffet *buf, Tag tag) {
    return tag==SSO ? buf->sso.len : buf->ptr.len;
}

static void 
setlen (Buffet *buf, size_t len, Tag tag) {
    if (tag==SSO) buf->sso.len = len; else buf->ptr.len = len;
}


static Buffet
new_own (size_t cap, const char *src, size_t len)
{
    Store *store = malloc(MEM(cap));

    if (!store) {ERR_ALLOC; return ZERO;}

    *store = (Store){.refcnt=1, .canary=CANARY};

    char *data = store->data;
    if (src) memcpy(data, src, len);
    data[len] = 0;

    return (Buffet) {
        .ptr.data = data,
        .ptr.len = len,
        .ptr.aux = cap,
        .ptr.tag = OWN
    };
}


static Buffet
new_vue (const char *src, size_t len)
{
    return (Buffet) {
        .ptr.data = (char*)src,
        .ptr.len = len,
        .ptr.aux = 0,
        .ptr.tag = VUE
    };
}


static char*
grow (Buffet *buf, size_t newcap)
{
    Tag tag = TAG(buf);
    char *data = NULL;

    if (tag==SSO) {

        Buffet own = new_own (newcap, buf->sso.data, buf->sso.len);
        data = own.ptr.data;
        if (data) *buf = own;

    } else {

        assert(tag==OWN);

        Store *store = STORE(buf);

        if (store->canary != CANARY) {
            ERR_CANARY; 
            return NULL;
        }
        
        store = realloc(store, MEM(newcap));
        if (store) {
            data = store->data;
            DATA(buf) = data;
            buf->ptr.aux = newcap;
        }
    }
    
    return data;
}


static void 
dbg (const Buffet* buf) 
{
    char tag[10];
    switch(TAG(buf)) {
        case SSO: sprintf(tag,"SSO"); break;
        case OWN: sprintf(tag,"OWN"); break;
        case REF: sprintf(tag,"REF"); break;
        case VUE: sprintf(tag,"VUE"); break;
    }

    bool mustfree;
    const char *cstr = buffet_cstr(buf, &mustfree);

    LOG("%s cap:%zu len:%zu cstr:'%s'", 
    tag, buffet_cap(buf), buffet_len(buf), cstr);

    if (mustfree) free((char*)cstr);
}

//=============================================================================
// Public
//=============================================================================

Buffet
buffet_new (size_t cap)
{
    return (cap <= BUFFET_SSOMAX) ? ZERO : new_own(cap, NULL, 0);
}


Buffet
buffet_memcopy (const char *src, size_t len)
{
    if (len <= BUFFET_SSOMAX) {
        Buffet ret = ZERO;
        memcpy(ret.sso.data, src, len);
        ret.sso.len = len;
        return ret;
    }

    return new_own(len, src, len);
}


Buffet
buffet_memview (const char *src, size_t len) {
    return new_vue(src, len);
}


Buffet
buffet_copy (const Buffet *src, ptrdiff_t off, size_t len)
{
    Tag tag = TAG(src);

    if (off+len > getlen(src,tag)) {
        return ZERO;
    } else {
        return buffet_memcopy (getdata(src,tag)+off, len);
    }
}


Buffet
buffet_dup (const Buffet *src)
{
    Tag tag = TAG(src);
    
    if (tag==OWN) {

        size_t len = src->ptr.len;
        return new_own (len, DATA(src), len); // or inc rc, ret src + cow?
    
    } else if (tag==REF) { 
    
        Store *store = STORE(TARGET(src));
        if (store->canary != CANARY) {ERR_CANARY; return ZERO;}
        ++ store->refcnt;
    }

    return *src;
}
 

Buffet
buffet_view (Buffet *src, ptrdiff_t off, size_t len)
{
    if (!len) return ZERO;

    Tag tag = TAG(src);
    size_t srclen = getlen(src,tag);

    // Enforce clipping
    if ((size_t)off >= srclen) return ZERO;
    if (off+len > srclen) len = srclen-off;

    if (tag==VUE) {
        return new_vue (DATA(src) + off, len); 
    }

    if (tag==SSO) {

        // convert src to OWN to get a refcnt.
        // (SSO refcnt was tested and seemed too slow and branchy)
        // todo: implications
        //   ex: if SSO had a VUE on it through `view(sso.data)`
        //       warn bad practice ? user should `view(sso)`
        
        Buffet own = new_own (srclen, src->sso.data, srclen);
        if (!own.ptr.data) return ZERO;
        *src = own;
    
    } else if (tag==REF) {
    
        off += OFF(src);
        src = TARGET(src);
    }

    Store *store = STORE(src);

    if (tag==OWN && store->canary != CANARY) {
        ERR_CANARY; return ZERO;
    }

    ++ store->refcnt; 

    return (Buffet) {
        .ptr.data = (char*)(src),
        .ptr.len = len,
        .ptr.aux = off,
        .ptr.tag = REF     
    };              
}


bool
buffet_free (Buffet *buf)
{   
    Tag tag = TAG(buf);

    if (tag==OWN) {
            
        Store *store = STORE(buf);

        if (store->canary != CANARY) {
            ERR_CANARY;
        } else if (!store->refcnt) {
            ERR_REFCNT;
        } else {
            --store->refcnt;
            if (!store->refcnt) {
                store->canary = 0;
                free(store);
            } else {
                return false;
            }
        }

    } else if (tag==REF) {
    
        Buffet *target = TARGET(buf);            
        Store *store = STORE(target);
     
        if (store->canary != CANARY) {
            ERR_CANARY;
        } else if (!store->refcnt) {
            ERR_REFCNT;
        } else {    
            -- store->refcnt;
            if (!store->refcnt) {
                // LOG("free ref : rid store");
                store->canary = 0;
                free(store);
                ERASE(target);             
            }
        }
    }

    ERASE(buf);
    return true;
}



// todo: other crazy self-appends
size_t 
buffet_append (Buffet *buf, const char *src, size_t srclen) 
{
    const Tag tag = TAG(buf);
    const size_t curlen = tag ? buf->ptr.len : buf->sso.len;
    const size_t newlen = curlen + srclen;
    const size_t newcap = OVERALLOC*newlen;
    char *curdata = (char*)getdata(buf,tag);

    if (tag==SSO||tag==OWN) {

        size_t cap = tag ? buf->ptr.aux : BUFFET_SSOMAX;

        if (newlen <= cap) {

            memcpy (curdata+curlen, src, srclen);
            curdata[newlen] = 0;
            setlen(buf, newlen, tag);
        
        } else {

            ptrdiff_t diff = src - curdata;
            bool selfsrc = diff >= 0 && (size_t)diff <= cap;
            // if (selfsrc) LOG("selfsrc");

            char *data = grow(buf, newcap);
            if (!data) {return 0;}

            // update if self-append invalid
            if (selfsrc && (data != curdata)) {
                // LOG("selfsrc reloc");
                src = data;
            }

            memcpy (data+curlen, src, srclen);
            data[newlen] = 0;
            buf->ptr.len = newlen;
        }

    } else {

        if (tag==REF) {

            Buffet *target = TARGET(buf);
            Store *store = STORE(target);
            
            // if invalid ref, erase it
            if (store->canary != CANARY) {
                ERR_CANARY;
                ERASE(buf); // then append ?
                return 0;
            } else if (!store->refcnt) {
                ERR_REFCNT;
                ERASE(buf); // then append ?
                return 0;
            } else { 
                -- store->refcnt;
            }
        }

        *buf = buffet_memcopy (curdata, curlen); 
        buffet_append (buf, src, srclen);
    }

    return newlen;
}



#define LIST_STACK_MAX (BUFFET_STACK_MEM/sizeof(Buffet))

Buffet*
buffet_split (const char* src, size_t srclen, const char* sep, size_t seplen, 
    int *outcnt)
{
    int curcnt = 0; 
    Buffet *ret = NULL;
    
    Buffet  parts_local[LIST_STACK_MAX]; 
    Buffet *parts = parts_local;
    bool local = true;
    int partsmax = LIST_STACK_MAX;

    const char *beg = src;
    const char *end = beg;

    while ((end = strstr(end, sep))) {

        if (curcnt >= partsmax-1) {

            partsmax *= 2;
            size_t newsz = partsmax * sizeof(Buffet);

            if (local) {
                parts = malloc(newsz); 
                if (!parts) {curcnt = 0; goto fin;}
                memcpy (parts, parts_local, curcnt * sizeof(Buffet));
                local = false;
            } else {
                parts = realloc(parts, newsz); 
                if (!parts) {curcnt = 0; goto fin;}
            }
        }

        parts[curcnt++] = new_vue(beg, end-beg);
        beg = end+seplen;
        end = beg;
    };
    
    // last part
    parts[curcnt++] = new_vue(beg, src+srclen-beg);

    if (local) {
        size_t outlen = curcnt * sizeof(Buffet);
        ret = malloc(outlen);
        memcpy (ret, parts, outlen);
    } else {
        ret = parts;  
    }

    fin:
    *outcnt = curcnt;
    return ret;
}


Buffet*
buffet_splitstr (const char* src, const char* sep, int *outcnt) {
    return buffet_split(src, strlen(src), sep, strlen(sep), outcnt);
}


Buffet 
buffet_join (const Buffet *parts, int cnt, const char* sep, size_t seplen)
{
    // optim: local if small; none if too big ?
    size_t *lengths = malloc(cnt*sizeof(*lengths));
    size_t totlen = 0;

    for (int i=0; i < cnt; ++i) {
        const Buffet *part = &parts[i];
        size_t len = getlen(part,TAG(part));
        totlen += len;
        lengths[i] = len;
    }
    
    totlen += (cnt-1)*seplen;
    
    Buffet ret = buffet_new(totlen);
    const Tag rettag = TAG(&ret);
    char* cur = (char*)getdata(&ret,rettag);
    cur[totlen] = 0; 

    for (int i=0; i < cnt; ++i) {
        const Buffet *part = &parts[i];
        const char *eltdata = getdata(part,TAG(part));
        size_t eltlen = lengths[i];
        memcpy(cur, eltdata, eltlen);
        cur += eltlen;
        if (i<cnt-1) {
            memcpy(cur, sep, seplen);
            cur += seplen;
        }
    }    

    setlen(&ret, totlen, rettag);
    free(lengths);

    return ret;
}


const char*
buffet_cstr (const Buffet *buf, bool *mustfree) 
{
    const Tag tag = TAG(buf);
    const char *data = getdata(buf, tag);
    bool tofree = false;
    char *ret = (char*)data;

    if (tag>OWN) {

        const size_t len = buf->ptr.len;
        if (data[len]==0) goto fin;                

        ret = malloc(len+1);

        if (!ret) {
            ERR_ALLOC;
            ret = calloc(1,1); // "\0"
        } else {
            memcpy (ret, data, len);
            ret[len] = 0;
        }
        
        tofree = true;
    }

    fin:
    *mustfree = tofree;
    return ret;
}


char*
buffet_export (const Buffet* buf) 
{
    const Tag tag = TAG(buf);
    const size_t len = getlen(buf,tag);
    const char* data = getdata(buf,tag);
    char* ret = malloc(len+1);

    if (!ret) {
        ERR_ALLOC; return NULL;
    } else {
        memcpy (ret, data, len);
    }

    ret[len] = 0;
    return ret;
}

const char*
buffet_data (const Buffet* buf) {
    return getdata(buf, TAG(buf));
}

size_t
buffet_len (const Buffet* buf) {
    return getlen(buf, TAG(buf));
}

size_t
buffet_cap (const Buffet* buf) {
    Tag tag = TAG(buf);
    return 
        tag == OWN ? CAP(buf)
    :   tag == SSO ? BUFFET_SSOMAX
    :   0;
}

void
buffet_print (const Buffet *buf) {
    Tag tag = TAG(buf);
    LOG("%.*s", (int)getlen(buf,tag), getdata(buf,tag));
}

void 
buffet_debug (const Buffet* buf) {
    dbg(buf);
}
