#include <stdio.h>

#define BENCHBEG \
struct timespec start, stop; \
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);

#define BENCHEND(title) \
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stop); \
double time = ((double)(stop.tv_nsec - start.tv_nsec)) / 1000000L; \
printf("%s %.3f ms\n", #title, time);


static const char*
load (const char* src_path, size_t* outlen)
{
    FILE *file = fopen(src_path, "rb");

    if (!file) {
        perror("fopen");
        *outlen = '\0';
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    size_t len = ftell(file);
    char* out = (char*)malloc(len+1);
    
    rewind(file);
    fread ((char*)out, len, 1, file);
    out[len] = '\0';
    fclose(file);
    
    *outlen = len;
    
    return out;
}

// alco
char**
splitlen (const char* src, size_t srclen, const char* sep, size_t seplen, 
    int *outcnt)
{
    #define LOCAL_MEM 1024
    #define LOCAL_MAX (LOCAL_MEM/sizeof(char*))
    int curcnt = 0; 
    char **ret = NULL;
    
    char*  parts_local[LOCAL_MAX]; 
    char** parts = parts_local;
    bool local = true;
    int partsmax = LOCAL_MAX;

    const char *beg = src;
    const char *end = src;
    size_t len;

    while ((end = strstr(end, sep))) {

        if (curcnt >= partsmax-1) {

            partsmax *= 2;
            size_t newsz = partsmax * sizeof(char*);

            if (local) {
                parts = malloc(newsz); 
                if (!parts) {curcnt = 0; goto fin;}
                memcpy (parts, parts_local, curcnt * sizeof(char*));
                local = false;
            } else {
                parts = realloc(parts, newsz); 
                if (!parts) {curcnt = 0; goto fin;}
            }
        }
        #define TOLIST \
        len = end-beg; \
        char *part = malloc(len+1); \
        memcpy(part, beg, len); \
        part[len] = 0; \
        parts[curcnt++] = part;

        TOLIST
        end += seplen;
        beg = end;
    };
    
    // last part
    end = src+srclen;
    TOLIST

    if (local) {
        ret = malloc(curcnt * sizeof(char*));
        memcpy (ret, parts, curcnt * sizeof(char*));
    } else {
        ret = parts;  
    }

    fin:
    *outcnt = curcnt;
    return ret;

    #undef TOLIST
    #undef LOCAL_MAX
    #undef LOCAL_MEM
}

char**
split (const char* src, const char* sep, int *outcnt) {
    return splitlen(src, strlen(src), sep, strlen(sep), outcnt);
}

// alco
char* 
joinlen (char** parts, int cnt, const char* sep, size_t seplen)
{
    // opt: local if small; none if too big
    size_t *lengths = malloc(cnt*sizeof(*lengths));
    size_t totlen = 0;

    for (int i=0; i < cnt; ++i) {
        size_t len = strlen(parts[i]);
        totlen += len;
        lengths[i] = len;
    }
    totlen += (cnt-1)*seplen;
    
    char* ret = malloc(totlen+1);
    ret[totlen] = 0; 
    char* cur = ret;

    for (int i=0; i < cnt; ++i) {
        size_t eltlen = lengths[i];
        memcpy(cur, parts[i], eltlen);
        cur += eltlen;
        if (i<cnt-1) {
            memcpy(cur, sep, seplen);
            cur += seplen;
        }
    }    

    // setlen(&ret, totlen);
    free(lengths);

    return ret;
}


char* 
join (char** parts, int cnt, const char* sep) {
    return joinlen(parts, cnt, sep, strlen(sep));
}