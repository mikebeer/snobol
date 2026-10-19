/* SNOBOL4 interpreter in portable C89 - the parts are inlined by
   tools/amalg.sh */
/* ===== s0_base.c ===== */
/* s0_base.c - types, strings, maps, number formatting, globals.
   Part of the C89 port of the CREXX SNOBOL4 interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <float.h>
#ifdef SNO_POSIX
#include <sys/time.h>
#endif

/* ---- literal character sets (no code point ranges: EBCDIC safe) ----
   */
static const char LOWER[] = "abcdefghijklmnopqrstuvwxyz";
static const char UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char DIGITS[] = "0123456789";
static const char SPACES[] = " \t\n\v\f\r";

/* ---- strings: reference counted, NUL terminated, NULL means "" ----
   */
typedef struct Sv {
  long rc;
  long n;
  char c[1];
} Sv;

#define SP(s) ((s) ? (s)->c : "")
#define SN(s) ((s) ? (s)->n : 0L)

static void die(const char *msg)
{
  fflush(stdout);
  fprintf(stderr, "snobol: %s\n", msg);
  exit(2);
}

static void *xrealloc(void *p, size_t n)
{
  void *q = realloc(p, n ? n : 1);
  if (q == NULL)
    die("out of memory");
  return q;
}

static Sv *sv_new(const char *p, long n)
{
  Sv *s;
  if (n <= 0)
    return NULL;
  s = (Sv *)xrealloc(NULL, sizeof(Sv) + (size_t)n);
  s->rc = 1;
  s->n = n;
  if (p != NULL)
    memcpy(s->c, p, (size_t)n);
  s->c[n] = '\0';
  return s;
}

static Sv *sv_lit(const char *z)
{
  return sv_new(z, (long)strlen(z));
}

static void sv_dec(Sv *s)
{
  if (s != NULL && --s->rc <= 0)
    free(s);
}

static Sv *sv_ref(Sv *s)
{
  if (s != NULL)
    s->rc++;
  return s;
}

static Sv *sv_cat2(const char *a, long an, const char *b, long bn)
{
  Sv *s = sv_new(NULL, an + bn);
  if (s != NULL) {
    if (an > 0)
      memcpy(s->c, a, (size_t)an);
    if (bn > 0)
      memcpy(s->c + an, b, (size_t)bn);
  }
  return s;
}

static int sv_eq(const Sv *a, const Sv *b)
{
  long n = SN(a);
  if (n != SN(b))
    return 0;
  return n == 0 || memcmp(a->c, b->c, (size_t)n) == 0;
}

/* strict string comparison (REXX "<<"): bytewise, shorter prefix is
   less */
static int str_cmp(const char *a, long an, const char *b, long bn)
{
  long m = an < bn ? an : bn;
  int c = m > 0 ? memcmp(a, b, (size_t)m) : 0;
  if (c != 0)
    return c < 0 ? -1 : 1;
  if (an == bn)
    return 0;
  return an < bn ? -1 : 1;
}

/* ---- character helpers ---- */
static char up1(char c)
{
  const char *q;
  if (c == '\0')
    return c;
  q = strchr(LOWER, c);
  return q != NULL ? UPPER[q - LOWER] : c;
}

static Sv *sv_upper(const char *p, long n)
{
  Sv *s = sv_new(p, n);
  long i;
  for (i = 0; i < n; i++)
    s->c[i] = up1(s->c[i]);
  return s;
}

static int is_digit_c(char c)
{
  return c >= '0' && c <= '9';
}

static int is_alpha_c(char c)
{
  return c != '\0'
      && (strchr(LOWER, c) != NULL || strchr(UPPER, c) != NULL);
}

static int is_namech(char c)
{
  return c == '.' || c == '_' || is_digit_c(c) || is_alpha_c(c);
}

/* REXX strip(): blanks and white space at both ends; returns span */
static void strip_span(const char *s, long n, const char **ps,
    long *pn)
{
  long a = 0;
  long b = n;
  while (a < b && s[a] != '\0' && strchr(SPACES, s[a]) != NULL)
    a++;
  while (b > a && s[b - 1] != '\0' && strchr(SPACES, s[b - 1]) != NULL)
    b--;
  *ps = s + a;
  *pn = b - a;
}

/* REXX substr(s, start, n) with blank padding; start >= 1 */
static Sv *crx_substr_n(const char *s, long len, long start, long n)
{
  Sv *r;
  long i;
  if (n <= 0)
    return NULL;
  r = sv_new(NULL, n);
  for (i = 0; i < n; i++) {
    long k = start + i;
    r->c[i] = (k >= 1 && k <= len) ? s[k - 1] : ' ';
  }
  return r;
}

/* ---- growable arrays (1-based, zero filled) ---- */
static void *grow_arr(void *a, long *cap, long need, size_t sz)
{
  long nc;
  if (need < *cap)
    return a;
  nc = *cap ? *cap : 64;
  while (nc <= need)
    nc *= 2;
  a = xrealloc(a, (size_t)nc * sz);
  memset((char *)a + (size_t)*cap * sz, 0, (size_t)(nc - *cap) * sz);
  *cap = nc;
  return a;
}

#define GROW(a, cap, need, T) \
  ((a) = (T *)grow_arr((a), &(cap), (long)(need), sizeof(T)))

/* several arrays sharing one capacity variable */
#define GBEGIN(cap) { long gc_ = (cap), gn_ = (cap);
#define GA(a, need, T) \
  gn_ = gc_; (a) = (T *)grow_arr((a), &gn_, (long)(need), sizeof(T));
#define GEND(cap) (cap) = gn_; }

/* ---- string keyed maps (value 0 means absent) ---- */
typedef struct {
  Sv **k;
  long *v;
  unsigned char *u;
  long cap;
  long n;
} Map;

static unsigned long hash_b(const char *p, long n)
{
  unsigned long h = 2166136261UL;
  long i;
  for (i = 0; i < n; i++) {
    h ^= (unsigned long)(unsigned char)p[i];
    h *= 16777619UL;
    h &= 0xFFFFFFFFUL;
  }
  return h;
}

static long map_slot(Map *m, const char *p, long n, int create)
{
  unsigned long h;
  long i;
  if (m->cap == 0) {
    if (!create)
      return -1;
    m->cap = 256;
    m->k = (Sv **)xrealloc(NULL, 256 * sizeof(Sv *));
    m->v = (long *)xrealloc(NULL, 256 * sizeof(long));
    m->u = (unsigned char *)xrealloc(NULL, 256);
    memset(m->k, 0, 256 * sizeof(Sv *));
    memset(m->v, 0, 256 * sizeof(long));
    memset(m->u, 0, 256);
  }
  h = hash_b(p, n);
  i = (long)(h % (unsigned long)m->cap);
  while (m->u[i]) {
    if (SN(m->k[i]) == n
        && (n == 0 || memcmp(m->k[i]->c, p, (size_t)n) == 0))
      return i;
    i++;
    if (i >= m->cap)
      i = 0;
  }
  return create ? i : -1;
}

static long map_get(Map *m, const char *p, long n)
{
  long i = map_slot(m, p, n, 0);
  return i < 0 ? 0 : m->v[i];
}

static void map_set(Map *m, const char *p, long n, long v)
{
  long i = map_slot(m, p, n, 1);
  if (!m->u[i]) {
    if ((m->n + 1) * 2 > m->cap) {
      Map nm;
      long j;
      nm.cap = m->cap * 2;
      nm.n = 0;
      nm.k = (Sv **)xrealloc(NULL, (size_t)nm.cap * sizeof(Sv *));
      nm.v = (long *)xrealloc(NULL, (size_t)nm.cap * sizeof(long));
      nm.u = (unsigned char *)xrealloc(NULL, (size_t)nm.cap);
      memset(nm.k, 0, (size_t)nm.cap * sizeof(Sv *));
      memset(nm.v, 0, (size_t)nm.cap * sizeof(long));
      memset(nm.u, 0, (size_t)nm.cap);
      for (j = 0; j < m->cap; j++) {
        if (m->u[j]) {
          long q = map_slot(&nm, SP(m->k[j]), SN(m->k[j]), 1);
          nm.k[q] = m->k[j];
          nm.v[q] = m->v[j];
          nm.u[q] = 1;
          nm.n++;
        }
      }
      free(m->k);
      free(m->v);
      free(m->u);
      *m = nm;
      i = map_slot(m, p, n, 1);
    }
    m->u[i] = 1;
    m->k[i] = sv_new(p, n);
    m->n++;
  }
  m->v[i] = v;
}

/* ---- integers: overflow is detected (CREXX leaves the old value)
   ---- */
static int add_ok(long a, long b, long *r)
{
  if ((b > 0 && a > LONG_MAX - b) || (b < 0 && a < LONG_MIN - b))
    return 0;
  *r = a + b;
  return 1;
}

static int sub_ok(long a, long b, long *r)
{
  if ((b < 0 && a > LONG_MAX + b) || (b > 0 && a < LONG_MIN + b))
    return 0;
  *r = a - b;
  return 1;
}

static int mul_ok(long a, long b, long *r)
{
  if (a > 0) {
    if (b > 0) {
      if (a > LONG_MAX / b)
        return 0;
    } else if (b < LONG_MIN / a)
      return 0;
  } else {
    if (b > 0) {
      if (a < LONG_MIN / b)
        return 0;
    } else if (a != 0 && b < LONG_MAX / a)
      return 0;
  }
  *r = a * b;
  return 1;
}

/* decimal text of a long */
static int fmt_long(long v, char *buf)
{
  char tmp[40];
  int n = 0;
  int k = 0;
  int neg = v < 0;
  unsigned long u = neg ? 0UL - (unsigned long)v : (unsigned long)v;
  if (u == 0)
    tmp[n++] = '0';
  while (u > 0) {
    tmp[n++] = DIGITS[u % 10UL];
    u /= 10UL;
  }
  if (neg)
    buf[k++] = '-';
  while (n > 0)
    buf[k++] = tmp[--n];
  buf[k] = '\0';
  return k;
}

static Sv *sv_long(long v)
{
  char b[48];
  int n = fmt_long(v, b);
  return sv_new(b, n);
}

/* parse an optionally signed decimal integer (whole span); 0 if
   bad/overflow */
static int parse_long(const char *s, long n, long *out)
{
  long i = 0;
  int neg = 0;
  unsigned long u = 0;
  unsigned long lim;
  if (n <= 0)
    return 0;
  if (s[0] == '-' || s[0] == '+') {
    neg = s[0] == '-';
    i = 1;
  }
  if (i >= n)
    return 0;
  lim = neg ? (unsigned long)LONG_MAX + 1UL : (unsigned long)LONG_MAX;
  for (; i < n; i++) {
    unsigned long d;
    if (!is_digit_c(s[i]))
      return 0;
    d = (unsigned long)(s[i] - '0');
    if (u > (lim - d) / 10UL)
      return 0;
    u = u * 10UL + d;
  }
  *out = neg ? (long)(0UL - u) : (long)u;
  return 1;
}

/* ---- real numbers: CREXX prints 15 significant digits, plain
   notation up to exponent 17, else d.ddde+XX (see README) ---- */
static void real_text(double v, char *out)
{
  char coef[64];
  char dg[64];
  char *abs_start;
  double av;
  double c;
  long ex;
  int neg;
  int i;
  int n;
  int L;
  char *dot;

  if (v != v) {
    strcpy(out, "nan");
    return;
  }
  if (v > DBL_MAX) {
    strcpy(out, "inf");
    return;
  }
  if (v < -DBL_MAX) {
    strcpy(out, "-inf");
    return;
  }
  if (v == 0.0) {
    strcpy(out, "0");
    return;
  }
  neg = v < 0.0;
  av = neg ? -v : v;
  ex = (long)floor(log10(av));
  c = av / pow(10.0, (double)ex);
  if (c >= 10.0) {
    c /= 10.0;
    ex += 1;
  }
  if (c < 1.0) {
    c *= 10.0;
    ex -= 1;
  }
  sprintf(coef, "%.*f", 14, c);
  abs_start = coef;
  if (strncmp(abs_start, "10.", 3) == 0) {
    abs_start[2] = abs_start[1];
    abs_start[1] = '.';
    ex++;
  } else if (strncmp(abs_start, "0.", 2) == 0 && abs_start[2] != '\0')
      {
    abs_start[0] = abs_start[2];
    memmove(abs_start + 2, abs_start + 3, strlen(abs_start + 3) + 1);
    ex--;
  }
  dot = strchr(coef, '.');
  if (dot != NULL) {
    char *e = coef + strlen(coef) - 1;
    while (e > dot && *e == '0') {
      *e = '\0';
      e--;
    }
    if (e == dot)
      *e = '\0';
  }
  /* digits only */
  n = 0;
  for (i = 0; coef[i] != '\0'; i++)
    if (coef[i] != '.')
      dg[n++] = coef[i];
  dg[n] = '\0';
  L = n;
  out[0] = '\0';
  if (neg)
    strcpy(out, "-");
  if (ex == 0) {
    strcat(out, coef);
    return;
  }
  if (ex + 1 > 18 || ex < -6) {
    char eb[48];
    strcat(out, coef);
    strcat(out, "e");
    if (ex >= 0)
      strcat(out, "+");
    fmt_long(ex, eb);
    strcat(out, eb);
    return;
  }
  if (ex > 0) {
    long k;
    char *o = out + strlen(out);
    if (ex + 1 <= L) {
      for (k = 0; k <= ex; k++)
        *o++ = dg[k];
      if (ex + 1 < L) {
        *o++ = '.';
        for (k = ex + 1; k < L; k++)
          *o++ = dg[k];
      }
      *o = '\0';
    } else {
      for (k = 0; k < L; k++)
        *o++ = dg[k];
      for (k = 0; k < ex + 1 - L; k++)
        *o++ = '0';
      *o = '\0';
    }
  } else {
    long k;
    char *o = out + strlen(out);
    *o++ = '0';
    *o++ = '.';
    for (k = 0; k < -ex - 1; k++)
      *o++ = '0';
    for (k = 0; k < L; k++)
      *o++ = dg[k];
    *o = '\0';
  }
}

/* fmtreal of the CREXX source: E upper case, a "." forced into the
   mantissa */
static Sv *fmtreal(double x)
{
  char t[128];
  char u[160];
  char *e;
  int i;
  if (x != x)
    return sv_lit("nan");
  real_text(x, t);
  for (i = 0; t[i] != '\0'; i++)
    if (t[i] == 'e')
      t[i] = 'E';
  e = strchr(t, 'E');
  if (e == NULL) {
    if (strchr(t, '.') == NULL)
      strcat(t, ".");
    return sv_lit(t);
  }
  {
    char *d = strchr(t, '.');
    if (d == NULL || d > e) {
      long m = (long)(e - t);
      memcpy(u, t, (size_t)m);
      u[m] = '.';
      strcpy(u + m + 1, e);
      return sv_lit(u);
    }
  }
  return sv_lit(t);
}

/* text -> double with strtod; the text must be NUL terminated */
static double text_to_double(const char *z)
{
  return strtod(z, NULL);
}

/* ---- values ---- */
typedef struct {
  int t;
  Sv *s;
  long i;
  double r;
} V;

static void v_set(V *d, const V *s)
{
  Sv *o = d->s;
  if (s->s != NULL)
    s->s->rc++;
  d->t = s->t;
  d->s = s->s;
  d->i = s->i;
  d->r = s->r;
  sv_dec(o);
}

/* ---- AST ---- */
typedef struct {
  int t;
  long a;
  long b;
  long i;
  double r;
  Sv *s;
} ND;

/* ---- pattern nodes ---- */
typedef struct {
  int t;
  long a;
  long b;
  long i;
  Sv *s;
} PN;

/* ---- statements ---- */
typedef struct {
  Sv *label;
  long subj, pat, repl, hasrep, gs, gf, gu, line;
} ST;

/* ---- variables / element pool ---- */
typedef struct {
  V val;
  Sv *name;
  long unit;
  int fl;
} VR;

typedef struct {
  V val;
  Sv *key;
  long nx;
} EL;

/* ---- continuation cells, choice points, conditional assignments ----
   */
typedef struct {
  long node, x, y, next;
} KN;

typedef struct {
  long node, pos, kont, asg, md, ag;
} CS;

typedef struct {
  long nm, a, b;
} AS;

/* ---- global interpreter state ---- */
static int fold = 0;
static int rc_exit = 0;
static FILE *h_out;
static FILE *h_in;
static FILE *h_err;

static long nst = 0, stcap = 0;
static ST *stp;
static Map labels;
static long inq_n = 0, inq_p = 1, inqcap = 0;
static Sv **inq;

static const char *src = "";
static long ps = 1;
static long slen = 0;
static int perr = 0;
static Sv *perr_msg = NULL;
static long cur_line = 0;

static long nn = 0, ndcap = 0;
static ND *ndp;
static long nal = 0, alcap = 0;
static long *al;

static Map vmap;
static long nv = 0, nvcap = 0;
static VR *vrp;

static long ne = 0, necap = 0;
static EL *elp;

static long nar = 0, narcap = 0;
static long *ar_base, *ar_nd, *ar_d0;
static Sv **ar_proto;
static long ndm = 0, dmcap = 0;
static long *dm_lo, *dm_hi;

static long ntb = 0, tbcap = 0;
static long *tb_head, *tb_n;
static Map tb_map;

static long nty = 0, nflds = 0, tycap = 0, fldcap = 0;
static Sv **ty_name;
static long *ty_nf, *ty_f0;
static Sv **ty_fld;
static Map tymap, fldmap;
static long nob = 0, obcap = 0;
static long *ob_ty, *ob_base;

static long nnm = 0, nmcap = 0;
static long *nm_k, *nm_a;
static long ncd = 0, cdcap = 0;
static long *cd_start;

static V R;
#define rt R.t
#define rs R.s
#define ri R.i
#define rr R.r
static int fail = 0;
static int err_flag = 0;
static int halted = 0;

static V *abv;
static long ab_cnt = 0, abcap = 0;

static long npn = 0, pncap = 0;
static PN *pnp;

static const char *ms = "";
static long mlen = 0;
static long mstart = 0;
static long mend = 0;
static long csp = 0, cscap = 0;
static CS *csv;
static long kn_n = 0, kncap = 0;
static KN *knp;
static long asgn_n = 0, ascap = 0;
static AS *asv;
static int abort_m = 0;

static long nfn = 0, nfv = 0, fncap = 0, fvcap = 0;
static Map fnmap, fnalias;
static Sv **fn_name, **fn_entry;
static long *fn_nargs, *fn_nloc, *fn_first;
static long *fn_vars;
static long nsave = 0, svcap = 0;
static V *svv;
static long depth = 0;

static long kw_anchor = 0, kw_trim = 0, kw_fullscan = 0,
    kw_errlimit = 0;
static long kw_errtype = 0, kw_stcount = 0, kw_stlimit = -1;
static long kw_maxlngth = 5000000L, kw_lastno = 0;
static long kw_trace = 0, kw_ftrace = 0, kw_code = 0;
static long cur_st = 0;

static FILE *ou_h[256];
static int ou_m[256];
static int ou_e[256];
static Sv *ou_n[256];

static unsigned long rnd_state = 1013904223UL;
static long lv_k = 0, lv_a = 0;
static long nv_i = 0;
static double nv_r = 0.0;
static Map evc;
static long endpc = 0;

static V *srt;
static long *srt_p, *srt_v;
static long srtcap = 0, srtpcap = 0, srtvcap = 0;

/* ===== s_gen.h ===== */
/* generated by tools/gen_consts.py - do not edit */
#define T_STR 0
#define T_INT 1
#define T_REAL 2
#define T_PAT 3
#define T_ARR 4
#define T_TBL 5
#define T_NAME 6
#define T_CODE 7
#define T_EXPR 8
#define T_DATA 9
#define N_STR 1
#define N_INT 2
#define N_REAL 3
#define N_VAR 4
#define N_KW 5
#define N_CALL 6
#define N_INDEX 7
#define N_UNARY 8
#define N_BIN 9
#define N_CAT 10
#define N_ALT 11
#define N_COND 12
#define N_IMM 13
#define N_NAMEOF 14
#define N_INDIR 15
#define N_DEFER 16
#define N_CURSOR 17
#define N_MATCH 18
#define PT_LIT 1
#define PT_CAT 2
#define PT_ALT 3
#define PT_ANY 4
#define PT_NOTANY 5
#define PT_SPAN 6
#define PT_BREAK 7
#define PT_BREAKX 8
#define PT_LEN 9
#define PT_POS 10
#define PT_RPOS 11
#define PT_TAB 12
#define PT_RTAB 13
#define PT_REM 14
#define PT_ARB 15
#define PT_BAL 16
#define PT_FAIL 17
#define PT_ABORT 18
#define PT_FENCE 19
#define PT_FENCEP 20
#define PT_SUCCEED 21
#define PT_ARBNO 22
#define PT_COND 23
#define PT_IMM 24
#define PT_CURSOR 25
#define PT_DEFER 26
#define PT_EMPTY 27
#define K_CONDEND 40
#define K_IMMEND 41
#define K_ARBNOCHK 42
#define K_FENCEEND 43
#define NM_VAR 1
#define NM_ELEM 2
#define NM_KW 3
#define KW_ANCHOR 1
#define KW_TRIM 2
#define KW_FULLSCAN 3
#define KW_ERRLIMIT 4
#define KW_ERRTYPE 5
#define KW_STCOUNT 6
#define KW_STLIMIT 7
#define KW_STNO 8
#define KW_ALPHABET 9
#define KW_LCASE 10
#define KW_UCASE 11
#define KW_MAXLNGTH 12
#define KW_FNCLEVEL 13
#define KW_LASTNO 14
#define KW_TRACE 15
#define KW_FTRACE 16
#define KW_INPUT 17
#define KW_OUTPUT 18
#define KW_CODE 19
#define KW_DIGITS 20
#define KW_ABORT 21
#define FN_ABS 1
#define FN_ANY 2
#define FN_APPLY 3
#define FN_ARB 4
#define FN_ARBNO 5
#define FN_ARG 6
#define FN_ARRAY 7
#define FN_ATAN 8
#define FN_BACKSPACE 9
#define FN_BAL 10
#define FN_BREAK 11
#define FN_BREAKX 12
#define FN_CHAR 13
#define FN_CHOP 14
#define FN_CLEAR 15
#define FN_CODE 16
#define FN_COLLECT 17
#define FN_CONVERT 18
#define FN_COPY 19
#define FN_COS 20
#define FN_DATA 21
#define FN_DATATYPE 22
#define FN_DATE 23
#define FN_DEFINE 24
#define FN_DETACH 25
#define FN_DIFFER 26
#define FN_DUMP 27
#define FN_DUPL 28
#define FN_EJECT 29
#define FN_ENDFILE 30
#define FN_EQ 31
#define FN_EVAL 32
#define FN_EXIT 33
#define FN_EXP 34
#define FN_FENCE 35
#define FN_FIELD 36
#define FN_GE 37
#define FN_GT 38
#define FN_HOST 39
#define FN_IDENT 40
#define FN_INPUT 41
#define FN_INTEGER 42
#define FN_ITEM 43
#define FN_LE 44
#define FN_LEN 45
#define FN_LEQ 46
#define FN_LGE 47
#define FN_LGT 48
#define FN_LLE 49
#define FN_LLT 50
#define FN_LN 51
#define FN_LNE 52
#define FN_LOAD 53
#define FN_LOCAL 54
#define FN_LPAD 55
#define FN_LT 56
#define FN_NE 57
#define FN_NOTANY 58
#define FN_OPSYN 59
#define FN_OUTPUT 60
#define FN_POS 61
#define FN_PROTOTYPE 62
#define FN_REMDR 63
#define FN_REPLACE 64
#define FN_REVERSE 65
#define FN_REWIND 66
#define FN_RPAD 67
#define FN_RPOS 68
#define FN_RSORT 69
#define FN_RTAB 70
#define FN_SET 71
#define FN_SIN 72
#define FN_SIZE 73
#define FN_SORT 74
#define FN_SPAN 75
#define FN_SQRT 76
#define FN_STOPTR 77
#define FN_SUBSTR 78
#define FN_TAB 79
#define FN_TABLE 80
#define FN_TAN 81
#define FN_TIME 82
#define FN_TRACE 83
#define FN_TRIM 84
#define FN_UNLOAD 85
#define FN_REAL 86
#define FN_RANDOM 87
#define FN_SUCCEED 88
#define FN_FAIL 89
#define FN_ABORT 90
#define FN_REM 91
#define BFN_COUNT 91
static const char *const bfn_w[] = {
  "",
  "ABS",
  "ANY",
  "APPLY",
  "ARB",
  "ARBNO",
  "ARG",
  "ARRAY",
  "ATAN",
  "BACKSPACE",
  "BAL",
  "BREAK",
  "BREAKX",
  "CHAR",
  "CHOP",
  "CLEAR",
  "CODE",
  "COLLECT",
  "CONVERT",
  "COPY",
  "COS",
  "DATA",
  "DATATYPE",
  "DATE",
  "DEFINE",
  "DETACH",
  "DIFFER",
  "DUMP",
  "DUPL",
  "EJECT",
  "ENDFILE",
  "EQ",
  "EVAL",
  "EXIT",
  "EXP",
  "FENCE",
  "FIELD",
  "GE",
  "GT",
  "HOST",
  "IDENT",
  "INPUT",
  "INTEGER",
  "ITEM",
  "LE",
  "LEN",
  "LEQ",
  "LGE",
  "LGT",
  "LLE",
  "LLT",
  "LN",
  "LNE",
  "LOAD",
  "LOCAL",
  "LPAD",
  "LT",
  "NE",
  "NOTANY",
  "OPSYN",
  "OUTPUT",
  "POS",
  "PROTOTYPE",
  "REMDR",
  "REPLACE",
  "REVERSE",
  "REWIND",
  "RPAD",
  "RPOS",
  "RSORT",
  "RTAB",
  "SET",
  "SIN",
  "SIZE",
  "SORT",
  "SPAN",
  "SQRT",
  "STOPTR",
  "SUBSTR",
  "TAB",
  "TABLE",
  "TAN",
  "TIME",
  "TRACE",
  "TRIM",
  "UNLOAD",
  "REAL",
  "RANDOM",
  "SUCCEED",
  "FAIL",
  "ABORT",
  "REM"
};
/* ===== s1_val.c ===== */
/* s1_val.c - value registers, errors, numbers, variables, arrays,
   tables */

static void setnull(void)
{
  Sv *o = rs;
  rt = 0;
  rs = NULL;
  sv_dec(o);
}

static void setstr(Sv *s)
{
  Sv *o = rs;
  if (s != NULL)
    s->rc++;
  rs = s;
  rt = 0;
  sv_dec(o);
}

/* takes over the caller's reference */
static void setstr_own(Sv *s)
{
  Sv *o = rs;
  rs = s;
  rt = 0;
  sv_dec(o);
}

static void setint(long i)
{
  Sv *o = rs;
  rs = NULL;
  rt = 1;
  ri = i;
  sv_dec(o);
}

static void setreal(double x)
{
  Sv *o = rs;
  rs = NULL;
  rt = 2;
  rr = x;
  sv_dec(o);
}

static void setobj(int t, long i)
{
  Sv *o = rs;
  rs = NULL;
  rt = t;
  ri = i;
  sv_dec(o);
}

static double dtrunc(double x)
{
  return x < 0.0 ? ceil(x) : floor(x);
}

static const char *where_am_i(char *buf)
{
  if (cur_st > 0) {
    int n;
    strcpy(buf, "statement ");
    n = (int)strlen(buf);
    n += fmt_long(cur_st, buf + n);
    strcpy(buf + n, " (line ");
    n += 7;
    n += fmt_long(stp[cur_st].line, buf + n);
    strcpy(buf + n, ")");
    return buf;
  }
  return "program";
}

static void snerr(long code, const char *msg)
{
  fail = 1;
  if (kw_errlimit > 0) {
    kw_errlimit = kw_errlimit - 1;
    kw_errtype = code;
    return;
  }
  if (!err_flag) {
    char w[80];
    err_flag = 1;
    fflush(h_out);
    fprintf(h_err, "snobol: error %ld at %s: %s\n", code,
        where_am_i(w), msg);
    fflush(h_err);
  }
  halted = 1;
  rc_exit = 1;
}

/* the text of a number given as C text: "1.5", ".5", "5.", "1.E5" ->
   double */
static double numtofloat(const char *w, long wn)
{
  char *t = (char *)xrealloc(NULL, (size_t)wn + 4);
  long n = 0;
  long i;
  long p;
  double x;
  if (wn > 0 && w[0] == '.')
    t[n++] = '0';
  for (i = 0; i < wn; i++)
    t[n++] = up1(w[i]);
  t[n] = '\0';
  p = -1;
  for (i = 0; i < n; i++)
    if (t[i] == '.') {
      p = i;
      break;
    }
  if (p >= 0) {
    if (p == n - 1) {
      t[n++] = '0';
      t[n] = '\0';
    } else if (t[p + 1] == 'E') {
      memmove(t + p + 2, t + p + 1, (size_t)(n - p));
      t[p + 1] = '0';
    }
  }
  x = text_to_double(t);
  free(t);
  return x;
}

/* string -> number: 0 (not a number), 1 (integer), 2 (real); result in
   nv_i / nv_r */
static int str2num(const char *s0, long n0)
{
  const char *s;
  long n, p, q, q2, q3, e, ndg;
  int isreal, neg;
  char c;
  strip_span(s0, n0, &s, &n);
  nv_i = 0;
  if (n == 0)
    return 1;
  p = 1;
  neg = 0;
  c = s[0];
  if (c == '+' || c == '-') {
    p = 2;
    if (c == '-')
      neg = 1;
  }
  q = p;
  while (q <= n && is_digit_c(s[q - 1]))
    q++;
  ndg = q - p;
  isreal = 0;
  if (q <= n && s[q - 1] == '.') {
    isreal = 1;
    q2 = q + 1;
    while (q2 <= n && is_digit_c(s[q2 - 1]))
      q2++;
    ndg = ndg + (q2 - q - 1);
    q = q2;
  }
  if (ndg == 0)
    return 0;
  if (q <= n && (s[q - 1] == 'e' || s[q - 1] == 'E')) {
    isreal = 1;
    e = q + 1;
    if (e <= n && (s[e - 1] == '+' || s[e - 1] == '-'))
      e++;
    q3 = e;
    while (q3 <= n && is_digit_c(s[q3 - 1]))
      q3++;
    if (q3 == e)
      return 0;
    q = q3;
  }
  if (q <= n)
    return 0;
  if (!isreal && ndg > 18)
    isreal = 1;
  if (!isreal) {
    long v;
    if (!parse_long(s + (p - 1), n - p + 1, &v))
      isreal = 1;
    else {
      nv_i = neg ? 0 - v : v;
      return 1;
    }
  }
  nv_r = numtofloat(s + (p - 1), n - p + 1);
  if (neg)
    nv_r = 0.0 - nv_r;
  return 2;
}

/* current register value as a string (owned) */
static Sv *rstr(void)
{
  switch (rt) {
  case 0:
    return sv_ref(rs);
  case 1:
    return sv_long(ri);
  case 2:
    return fmtreal(rr);
  default:
    break;
  }
  return NULL;
}

static const char *dtype(void)
{
  switch (rt) {
  case 0:
    return "STRING";
  case 1:
    return "INTEGER";
  case 2:
    return "REAL";
  case 3:
    return "PATTERN";
  case 4:
    return "ARRAY";
  case 5:
    return "TABLE";
  case 6:
    return "NAME";
  case 7:
    return "CODE";
  case 8:
    return "EXPRESSION";
  case 9:
    return SP(ty_name[ob_ty[ri]]);
  default:
    break;
  }
  return "STRING";
}

/* convert the registers to a number (rt = 1 or 2); error otherwise */
static void tonum(void)
{
  int k;
  if (rt == 1 || rt == 2)
    return;
  if (rt == 0) {
    k = str2num(SP(rs), SN(rs));
    if (k == 1) {
      setint(nv_i);
      return;
    }
    if (k == 2) {
      setreal(nv_r);
      return;
    }
  }
  snerr(1, "Illegal data type");
}

/* convert to integer (real values must be integral) */
static long toint0(void)
{
  tonum();
  if (fail == 1)
    return 0;
  if (rt == 2) {
    if (rr != dtrunc(rr) || rr > 9.2e18 || rr < -9.2e18 ||
        rr >= (double)LONG_MAX || rr <= (double)LONG_MIN) {
      snerr(1, "Illegal data type");
      return 0;
    }
    return (long)rr;
  }
  return ri;
}

/* ---- argument stack ---- */
static void push_reg(void)
{
  ab_cnt = ab_cnt + 1;
  GROW(abv, abcap, ab_cnt, V);
  v_set(&abv[ab_cnt], &R);
}

static void ld(long k)
{
  v_set(&R, &abv[k]);
}

static Sv *ab_str(long k)
{
  ld(k);
  return rstr();
}

static long ab_int(long k)
{
  ld(k);
  return toint0();
}

static int ident_ab(long a, long b)
{
  if (abv[a].t != abv[b].t)
    return 0;
  if (abv[a].t == 0)
    return sv_eq(abv[a].s, abv[b].s);
  if (abv[a].t == 2)
    return abv[a].r == abv[b].r;
  return abv[a].i == abv[b].i;
}

/* ---- output / input ---- */
static void flush_out(void)
{
  fflush(h_out);
}

static void put_line(long u, const char *p, long n)
{
  FILE *h;
  if (u < 0 || u > 255)
    return;
  h = ou_h[u];
  if (ou_m[u] != 2 || h == NULL) {
    snerr(2, "Output channel not open");
    return;
  }
  if (n > 0)
    fwrite(p, 1, (size_t)n, h);
  fputc('\n', h);
  if (u == 7)
    fflush(h);
}

/* one line (without \n and a trailing \r); 0 at end of file */
static int read_line(FILE *h, Sv **out)
{
  long cap = 128;
  long n = 0;
  char *b = (char *)xrealloc(NULL, (size_t)cap);
  int c;
  int got = 0;
  for (;;) {
    c = fgetc(h);
    if (c == EOF)
      break;
    got = 1;
    if (c == '\n')
      break;
    if (n + 1 >= cap) {
      cap *= 2;
      b = (char *)xrealloc(b, (size_t)cap);
    }
    b[n++] = (char)c;
  }
  if (!got) {
    free(b);
    return 0;
  }
  if (n > 0 && b[n - 1] == '\r')
    n--;
  *out = sv_new(b, n);
  free(b);
  return 1;
}

/* read one line from unit u into the registers; 1 on success, 0 at EOF
   */
static int get_line(long u)
{
  Sv *line = NULL;
  if (u == 5 && inq_p <= inq_n) {
    setstr(inq[inq_p]);
    inq_p = inq_p + 1;
    return 1;
  }
  if (u < 0 || u > 255)
    return 0;
  if (ou_m[u] != 1 || ou_e[u] == 1)
    return 0;
  if (!read_line(ou_h[u], &line)) {
    ou_e[u] = 1;
    return 0;
  }
  setstr_own(line);
  return 1;
}

/* ---- variables, keywords ---- */
static void getvar(long s)
{
  if (vrp[s].fl == 2) {
    if (get_line(vrp[s].unit) == 0)
      fail = 1;
    return;
  }
  v_set(&R, &vrp[s].val);
}

static void setvar(long s)
{
  v_set(&vrp[s].val, &R);
  if (vrp[s].fl == 1) {
    Sv *t = rstr();
    put_line(vrp[s].unit, SP(t), SN(t));
    sv_dec(t);
  }
}

static Sv *alphabet(void)
{
  char b[256];
  int i;
  for (i = 0; i < 256; i++)
    b[i] = (char)i;
  return sv_new(b, 256);
}

static void getkw(long k)
{
  switch (k) {
  case KW_ANCHOR:
    setint(kw_anchor);
    break;
  case KW_TRIM:
    setint(kw_trim);
    break;
  case KW_FULLSCAN:
    setint(kw_fullscan);
    break;
  case KW_ERRLIMIT:
    setint(kw_errlimit);
    break;
  case KW_ERRTYPE:
    setint(kw_errtype);
    break;
  case KW_STCOUNT:
    setint(kw_stcount);
    break;
  case KW_STLIMIT:
    setint(kw_stlimit);
    break;
  case KW_STNO:
    setint(cur_st);
    break;
  case KW_ALPHABET:
    setstr_own(alphabet());
    break;
  case KW_LCASE:
    setstr_own(sv_lit(LOWER));
    break;
  case KW_UCASE:
    setstr_own(sv_lit(UPPER));
    break;
  case KW_MAXLNGTH:
    setint(kw_maxlngth);
    break;
  case KW_FNCLEVEL:
    setint(depth);
    break;
  case KW_LASTNO:
    setint(kw_lastno);
    break;
  case KW_TRACE:
    setint(kw_trace);
    break;
  case KW_FTRACE:
    setint(kw_ftrace);
    break;
  case KW_INPUT:
    setint(1);
    break;
  case KW_OUTPUT:
    setint(1);
    break;
  case KW_DIGITS:
    setint(15);
    break;
  case KW_CODE:
    setint(kw_code);
    break;
  case KW_ABORT:
    setint(0);
    break;
  default:
    setnull();
    break;
  }
}

static void setkw(long k)
{
  long n = toint0();
  if (fail == 1)
    return;
  switch (k) {
  case KW_ANCHOR:
    kw_anchor = n;
    break;
  case KW_TRIM:
    kw_trim = n;
    break;
  case KW_FULLSCAN:
    kw_fullscan = n;
    break;
  case KW_ERRLIMIT:
    kw_errlimit = n;
    break;
  case KW_ERRTYPE:
    kw_errtype = n;
    break;
  case KW_STCOUNT:
    kw_stcount = n;
    break;
  case KW_STLIMIT:
    kw_stlimit = n;
    break;
  case KW_MAXLNGTH:
    kw_maxlngth = n;
    break;
  case KW_TRACE:
    kw_trace = n;
    break;
  case KW_FTRACE:
    kw_ftrace = n;
    break;
  case KW_CODE:
    kw_code = n;
    break;
  default:
    snerr(3, "Erroneous assignment to keyword");
    break;
  }
}

/* ---- names ---- */
static long mkname(long k, long a)
{
  nnm = nnm + 1;
  GBEGIN(nmcap)
    GA(nm_k, nnm, long)
    GA(nm_a, nnm, long)
  GEND(nmcap)
  nm_k[nnm] = k;
  nm_a[nnm] = a;
  return nnm;
}

static void el_get(long x);
static void el_set(long x);

static void fetch_nm(long k, long a)
{
  switch (k) {
  case NM_VAR:
    getvar(a);
    break;
  case NM_ELEM:
    el_get(a);
    break;
  case NM_KW:
    getkw(a);
    break;
  default:
    break;
  }
}

static void store_nm(long k, long a)
{
  switch (k) {
  case NM_VAR:
    setvar(a);
    break;
  case NM_ELEM:
    el_set(a);
    break;
  case NM_KW:
    setkw(a);
    break;
  default:
    break;
  }
}

/* ---- element pool ---- */
static long newelem(void)
{
  ne = ne + 1;
  GROW(elp, necap, ne, EL);
  return ne;
}

static void el_get(long x)
{
  v_set(&R, &elp[x].val);
}

static void el_set(long x)
{
  v_set(&elp[x].val, &R);
}

/* ---- arrays ---- */
static long make_array(Sv *proto, const V *init)
{
  long nd_ = 0;
  long tot = 1;
  long lo = 0;
  long hi = 0;
  long k, p, q;
  const char *rest;
  long restn;
  long tlo[64];
  long thi[64];
  const char *part;
  long partn;
  const char *a;
  long an;
  long b;
  long first;
  strip_span(SP(proto), SN(proto), &rest, &restn);
  for (;;) {
    p = -1;
    for (k = 0; k < restn; k++)
      if (rest[k] == ',') {
        p = k;
        break;
      }
    if (p < 0) {
      strip_span(rest, restn, &part, &partn);
      rest = rest + restn;
      restn = 0;
    } else {
      strip_span(rest, p, &part, &partn);
      rest = rest + p + 1;
      restn = restn - p - 1;
    }
    q = -1;
    for (k = 0; k < partn; k++)
      if (part[k] == ':') {
        q = k;
        break;
      }
    if (q >= 0) {
      if (str2num(part, q) != 1) {
        snerr(8, "Illegal array bound");
        return 0;
      }
      lo = nv_i;
      if (str2num(part + q + 1, partn - q - 1) != 1) {
        snerr(8, "Illegal array bound");
        return 0;
      }
      hi = nv_i;
    } else {
      if (str2num(part, partn) != 1) {
        snerr(8, "Illegal array bound");
        return 0;
      }
      lo = 1;
      hi = nv_i;
    }
    if (hi < lo) {
      snerr(8, "Illegal array bound");
      return 0;
    }
    if (nd_ >= 63) {
      snerr(8, "Illegal array bound");
      return 0;
    }
    tlo[nd_] = lo;
    thi[nd_] = hi;
    nd_ = nd_ + 1;
    b = hi - lo + 1;
    if (b > 20000000L || tot > 20000000L / b) {
      snerr(8, "Array too large");
      return 0;
    }
    tot = tot * b;
    if (p < 0)
      break;
  }
  (void)a;
  (void)an;
  nar = nar + 1;
  GBEGIN(narcap)
    GA(ar_nd, nar, long)
    GA(ar_d0, nar, long)
    GA(ar_base, nar, long)
    GA(ar_proto, nar, Sv *)
  GEND(narcap)
  ar_nd[nar] = nd_;
  ar_d0[nar] = ndm + 1;
  for (k = 0; k < nd_; k++) {
    ndm = ndm + 1;
    GBEGIN(dmcap)
      GA(dm_lo, ndm, long)
      GA(dm_hi, ndm, long)
    GEND(dmcap)
    dm_lo[ndm] = tlo[k];
    dm_hi[ndm] = thi[k];
  }
  ar_proto[nar] = sv_ref(proto);
  ar_base[nar] = ne + 1;
  first = ne + 1;
  GROW(elp, necap, ne + tot, EL);
  for (k = 1; k <= tot; k++) {
    ne = ne + 1;
    v_set(&elp[ne].val, init);
  }
  (void)first;
  return nar;
}

/* pool index of the element addressed by ab[base+1 .. base+na], 0 if
   out of range */
static long arr_elem(long aid, long base, long na)
{
  long d, v, d0, lo, hi, off;
  if (na != ar_nd[aid])
    return 0;
  d0 = ar_d0[aid];
  off = 0;
  for (d = 1; d <= na; d++) {
    v = ab_int(base + d);
    if (fail == 1)
      return 0;
    lo = dm_lo[d0 + d - 1];
    hi = dm_hi[d0 + d - 1];
    if (v < lo || v > hi)
      return 0;
    off = off * (hi - lo + 1) + (v - lo);
  }
  return ar_base[aid] + off;
}

static long arr_size(long aid)
{
  long d;
  long t = 1;
  for (d = 1; d <= ar_nd[aid]; d++)
    t = t
        * (dm_hi[ar_d0[aid] + d - 1] - dm_lo[ar_d0[aid] + d - 1] + 1);
  return t;
}

/* ---- tables ---- */
static Sv *keytext(void)
{
  char b[64];
  int n;
  Sv *t;
  Sv *r;
  switch (rt) {
  case 0:
    return sv_cat2("S", 1, SP(rs), SN(rs));
  case 1:
    b[0] = 'I';
    n = fmt_long(ri, b + 1);
    return sv_new(b, n + 1);
  case 2:
    t = fmtreal(rr);
    r = sv_cat2("R", 1, SP(t), SN(t));
    sv_dec(t);
    return r;
  case 3:
  case 4:
  case 5:
  case 9:
    b[0] = rt == 3 ? 'P' : rt == 4 ? 'A' : rt == 5 ? 'T' : 'D';
    n = fmt_long(ri, b + 1);
    return sv_new(b, n + 1);
  case 6:
    b[0] = 'N';
    n = 1 + fmt_long(nm_k[ri], b + 1);
    b[n++] = ':';
    n += fmt_long(nm_a[ri], b + n);
    return sv_new(b, n);
  default:
    break;
  }
  b[0] = 'X';
  n = fmt_long(ri, b + 1);
  return sv_new(b, n + 1);
}

static long make_table(void)
{
  ntb = ntb + 1;
  GBEGIN(tbcap)
    GA(tb_head, ntb, long)
    GA(tb_n, ntb, long)
  GEND(tbcap)
  tb_head[ntb] = 0;
  tb_n[ntb] = 0;
  return ntb;
}

/* find (or create) the element for the key in the registers; 0 if
   absent */
static long tbl_find(long tid, int create)
{
  char pre[48];
  int pn_;
  Sv *kt = keytext();
  Sv *full;
  long v;
  long x;
  pn_ = fmt_long(tid, pre);
  pre[pn_++] = '|';
  full = sv_cat2(pre, pn_, SP(kt), SN(kt));
  v = map_get(&tb_map, full->c, full->n);
  if (v != 0 || !create) {
    sv_dec(kt);
    sv_dec(full);
    return v;
  }
  x = newelem();
  elp[x].key = kt;
  elp[x].nx = tb_head[tid];
  tb_head[tid] = x;
  tb_n[tid] = tb_n[tid] + 1;
  map_set(&tb_map, full->c, full->n, x);
  sv_dec(full);
  return x;
}

/* load the key stored with a table element into the registers */
static void key_load(long x)
{
  Sv *k = elp[x].key;
  const char *p = SP(k);
  long n = SN(k);
  char c = p[0];
  long v = 0;
  switch (c) {
  case 'S':
    setstr_own(sv_new(p + 1, n - 1));
    break;
  case 'I':
    parse_long(p + 1, n - 1, &v);
    setint(v);
    break;
  case 'R':
    setreal(numtofloat(p + 1, n - 1));
    break;
  case 'P':
  case 'A':
  case 'T':
  case 'D':
    parse_long(p + 1, n - 1, &v);
    setobj(c == 'P' ? 3 : c == 'A' ? 4 : c == 'T' ? 5 : 9, v);
    break;
  case 'N':
    {
      long q;
      long kk = 0;
      long aa = 0;
      for (q = 1; q < n && p[q] != ':'; q++)
        ;
      parse_long(p + 1, q - 1, &kk);
      parse_long(p + q + 1, n - q - 1, &aa);
      setobj(6, mkname(kk, aa));
    }
    break;
  default:
    setnull();
    break;
  }
}

/* ---- data types ---- */
static void define_data(Sv *spec)
{
  const char *p = SP(spec);
  long n = SN(spec);
  long a = -1;
  long b = -1;
  long i;
  const char *rest;
  long restn;
  long t;
  long cnt;
  for (i = 0; i < n; i++)
    if (p[i] == '(') {
      a = i;
      break;
    }
  for (i = n - 1; i >= 0; i--)
    if (p[i] == ')') {
      b = i;
      break;
    }
  if (a < 0 || b < a) {
    snerr(8, "Bad DATA prototype");
    return;
  }
  nty = nty + 1;
  t = nty;
  GBEGIN(tycap)
    GA(ty_name, nty, Sv *)
    GA(ty_nf, nty, long)
    GA(ty_f0, nty, long)
  GEND(tycap)
  {
    const char *nm;
    long nl;
    strip_span(p, a, &nm, &nl);
    ty_name[t] = fold ? sv_upper(nm, nl) : sv_new(nm, nl);
  }
  ty_f0[t] = nflds + 1;
  cnt = 0;
  rest = p + a + 1;
  restn = b - a - 1;
  for (;;) {
    const char *f;
    long fl;
    long c = -1;
    Sv *fs;
    for (i = 0; i < restn; i++)
      if (rest[i] == ',') {
        c = i;
        break;
      }
    if (c < 0) {
      strip_span(rest, restn, &f, &fl);
      rest = rest + restn;
      restn = 0;
    } else {
      strip_span(rest, c, &f, &fl);
      rest = rest + c + 1;
      restn = restn - c - 1;
    }
    fs = fold ? sv_upper(f, fl) : sv_new(f, fl);
    cnt = cnt + 1;
    nflds = nflds + 1;
    GROW(ty_fld, fldcap, nflds, Sv *);
    ty_fld[nflds] = fs;
    map_set(&fldmap, SP(fs), SN(fs), 1);
    if (c < 0)
      break;
  }
  ty_nf[t] = cnt;
  map_set(&tymap, SP(ty_name[t]), SN(ty_name[t]), t);
}

/* index of field named f within type t (0 if none) */
static long field_index(long t, const char *f, long fl)
{
  long i;
  for (i = 1; i <= ty_nf[t]; i++) {
    Sv *x = ty_fld[ty_f0[t] + i - 1];
    if (SN(x) == fl && (fl == 0 || memcmp(x->c, f, (size_t)fl) == 0))
      return i;
  }
  return 0;
}
/* ===== s2_parse.c ===== */
/* s2_parse.c - loading the program text and parsing statements */

static long parse_expr(long minp);
static long parse_operand(void);
static long parse_primary(void);
static long postfix(long base);
static long parse_call(const char *nme, long nl);
static int parse_statement(const char *txt, long n, long li);
static int add_statement(const char *txt, long n, long li);

/* REXX wordpos(phrase, bfn): index of the phrase in the builtin word
   list */
static long bfn_index(const char *p, long n)
{
  const char *w[16];
  long wl[16];
  int nw = 0;
  long i = 0;
  int k;
  int j;
  while (i < n && nw < 16) {
    while (i < n && p[i] != '\0' && strchr(SPACES, p[i]) != NULL)
      i++;
    if (i >= n)
      break;
    w[nw] = p + i;
    while (i < n && !(p[i] != '\0' && strchr(SPACES, p[i]) != NULL))
      i++;
    wl[nw] = (p + i) - w[nw];
    nw++;
  }
  if (nw == 0 || i < n)
    return 0;
  for (k = 1; k + nw - 1 <= BFN_COUNT; k++) {
    for (j = 0; j < nw; j++) {
      const char *b = bfn_w[k + j];
      if ((long)strlen(b) != wl[j]
          || memcmp(b, w[j], (size_t)wl[j]) != 0)
        break;
    }
    if (j == nw)
      return k;
  }
  return 0;
}

static char sch(long p)
{
  return (p >= 1 && p <= slen) ? src[p - 1] : ' ';
}

#define pch() sch(ps)

static void pfail(const char *msg)
{
  if (perr == 0) {
    perr = 1;
    sv_dec(perr_msg);
    perr_msg = sv_lit(msg);
  }
}

static long skipb(void)
{
  long k = 0;
  while (ps <= slen && (src[ps - 1] == ' ' || src[ps - 1] == '\t')) {
    ps = ps + 1;
    k = 1;
  }
  return k;
}

/* node construction; takes over the reference to s */
static long mk(int t, long a, long b, Sv *s, long i)
{
  nn = nn + 1;
  GROW(ndp, ndcap, nn, ND);
  ndp[nn].t = t;
  ndp[nn].a = a;
  ndp[nn].b = b;
  ndp[nn].s = s;
  ndp[nn].i = i;
  ndp[nn].r = 0.0;
  return nn;
}

static long mke(void)
{
  return mk(N_STR, 0, 0, NULL, 0);
}

static long vslot(const char *nme, long nl)
{
  long v = map_get(&vmap, nme, nl);
  if (v != 0)
    return v;
  nv = nv + 1;
  GROW(vrp, nvcap, nv, VR);
  vrp[nv].name = sv_new(nme, nl);
  map_set(&vmap, nme, nl, nv);
  return nv;
}

static long kwcode(const char *s, long n)
{
  static const char *const names[] = {
    "", "ANCHOR", "TRIM", "FULLSCAN", "ERRLIMIT", "ERRTYPE", "STCOUNT",
    "STLIMIT", "STNO", "ALPHABET", "LCASE", "UCASE", "MAXLNGTH",
    "FNCLEVEL", "LASTNO", "TRACE", "FTRACE", "INPUT", "OUTPUT", "CODE",
    "DIGITS", "ABORT"
  };
  int k;
  for (k = 1; k <= 21; k++)
    if ((long)strlen(names[k]) == n
        && memcmp(names[k], s, (size_t)n) == 0)
      return k;
  return 0;
}

/* ---- loading the program text ---- */
static int load_program(const char *fn)
{
  FILE *f;
  Sv **lines = NULL;
  long lcap = 0;
  long n = 0;
  long i, j;
  Sv *line = NULL;
  Sv *cur = NULL;
  int have = 0;
  long curline = 0;
  int ok = 1;
  int isend = 0;
  f = fopen(fn, "r");
  if (f == NULL) {
    printf("snobol: cannot open %s\n", fn);
    return 0;
  }
  while (read_line(f, &line)) {
    n = n + 1;
    GROW(lines, lcap, n, Sv *);
    lines[n] = line;
    line = NULL;
  }
  fclose(f);
  for (i = 1; i <= n; i++) {
    const char *lp;
    long ln;
    char c;
    line = lines[i];
    if (line == NULL)
      continue;
    lp = line->c;
    ln = line->n;
    c = lp[0];
    if (c == '*' || c == '-')
      continue;
    if (c == '+' || c == '.') {
      if (have == 1) {
        Sv *t = sv_cat2(SP(cur), SN(cur), lp + 1, ln - 1);
        sv_dec(cur);
        cur = t;
      }
      continue;
    }
    if (have == 1) {
      if (add_statement(SP(cur), SN(cur), curline) == 0)
        ok = 0;
      sv_dec(cur);
      cur = NULL;
    }
    cur = sv_ref(line);
    curline = i;
    have = 1;
    if (ln >= 3
        && (memcmp(lp, "END", 3) == 0 || memcmp(lp, "end", 3) == 0)) {
      const char *sp0;
      long sn0;
      strip_span(lp, ln, &sp0, &sn0);
      if (sn0 == 3 || (ln >= 4 && (lp[3] == ' ' || lp[3] == '\t'))) {
        isend = 1;
        if (add_statement(SP(cur), SN(cur), curline) == 0)
          ok = 0;
        have = 0;
        for (j = i + 1; j <= n; j++) {
          inq_n = inq_n + 1;
          GROW(inq, inqcap, inq_n, Sv *);
          inq[inq_n] = sv_ref(lines[j]);
        }
        break;
      }
    }
  }
  if (have == 1 && isend == 0) {
    if (add_statement(SP(cur), SN(cur), curline) == 0)
      ok = 0;
  }
  sv_dec(cur);
  for (i = 1; i <= n; i++)
    sv_dec(lines[i]);
  free(lines);
  return ok;
}

/* split at top level ';' and parse each piece */
static int add_statement(const char *txt, long n, long li)
{
  char *piece = (char *)xrealloc(NULL, (size_t)n + 4);
  long pl = 0;
  long i;
  char q = '\0';
  int ok = 1;
  /* copy: txt may be reallocated by nested statement compilation */
  char *own = (char *)xrealloc(NULL, (size_t)n + 1);
  memcpy(own, txt, (size_t)n);
  for (i = 0; i < n; i++) {
    char c = own[i];
    if (q != '\0') {
      if (c == q)
        q = '\0';
      piece[pl++] = c;
    } else if (c == '\'' || c == '"') {
      q = c;
      piece[pl++] = c;
    } else if (c == ';') {
      if (parse_statement(piece, pl, li) == 0)
        ok = 0;
      piece[0] = ' ';
      pl = 1;
    } else
      piece[pl++] = c;
  }
  if (parse_statement(piece, pl, li) == 0)
    ok = 0;
  free(piece);
  free(own);
  return ok;
}

static void parse_body(long sidx);

static int parse_statement(const char *txt, long n, long li)
{
  long p;
  Sv *lab = NULL;
  const char *body = txt;
  long bn = n;
  long sidx;
  if (n > 0 && txt[0] != ' ' && txt[0] != '\t') {
    p = 0;
    while (p < n && txt[p] != ' ' && txt[p] != '\t')
      p++;
    if (p >= n) {
      lab = fold ? sv_upper(txt, n) : sv_new(txt, n);
      body = txt + n;
      bn = 0;
    } else {
      lab = fold ? sv_upper(txt, p) : sv_new(txt, p);
      body = txt + p;
      bn = n - p;
    }
  }
  nst = nst + 1;
  sidx = nst;
  GROW(stp, stcap, nst, ST);
  sv_dec(stp[sidx].label);
  stp[sidx].label = lab;
  stp[sidx].subj = 0;
  stp[sidx].pat = 0;
  stp[sidx].repl = 0;
  stp[sidx].hasrep = 0;
  stp[sidx].gs = 0;
  stp[sidx].gf = 0;
  stp[sidx].gu = 0;
  stp[sidx].line = li;
  if (lab != NULL)
    map_set(&labels, lab->c, lab->n, sidx);
  src = body;
  ps = 1;
  slen = bn;
  perr = 0;
  cur_line = li;
  parse_body(sidx);
  if (perr == 1) {
    printf("snobol: syntax error at line %ld: %s\n", li, SP(perr_msg));
    return 0;
  }
  return 1;
}

/* binary operator at position p? returns length (0 if none) and the
   code */
static int binop_at(long p, long blank, char *op)
{
  char c = sch(p);
  char c2 = sch(p + 1);
  char nxt;
  if (c == '*' && c2 == '*') {
    op[0] = '*';
    op[1] = '*';
    op[2] = '\0';
    return 2;
  }
  op[0] = c;
  op[1] = '\0';
  if (c == '|' || c == '/' || c == '!')
    return 1;
  if (c == '+' || c == '-' || c == '*' || c == '.' || c == '$'
      || c == '?') {
    nxt = sch(p + 1);
    if (blank == 0)
      return 1;
    if (nxt == ' ' || nxt == '\t')
      return 1;
  }
  op[0] = '\0';
  return 0;
}

static long oprec(const char *op)
{
  switch (op[0]) {
  case '?':
    return 1;
  case '|':
    return 3;
  case '+':
  case '-':
    return 6;
  case '/':
    return 8;
  case '*':
    return op[1] == '*' ? 11 : 9;
  case '!':
    return 11;
  case '$':
  case '.':
    return 12;
  default:
    break;
  }
  return 0;
}

static long mk_binary(const char *op, long a, long b)
{
  switch (op[0]) {
  case '|':
    return mk(N_ALT, a, b, NULL, 0);
  case '.':
    return mk(N_COND, a, b, NULL, 0);
  case '$':
    return mk(N_IMM, a, b, NULL, 0);
  case '?':
    return mk(N_MATCH, a, b, NULL, 0);
  case '!':
    return mk(N_BIN, a, b, sv_lit("**"), 5);
  case '+':
    return mk(N_BIN, a, b, sv_lit(op), 1);
  case '-':
    return mk(N_BIN, a, b, sv_lit(op), 2);
  case '*':
    if (op[1] == '\0')
      return mk(N_BIN, a, b, sv_lit(op), 3);
    break;
  case '/':
    return mk(N_BIN, a, b, sv_lit(op), 4);
  default:
    break;
  }
  return mk(N_BIN, a, b, sv_lit(op), 5);
}

static long parse_expr(long minp)
{
  long left0, right0, p, save, blank;
  char c;
  char op[4];
  int ol;
  left0 = parse_operand();
  for (;;) {
    if (perr == 1)
      break;
    save = ps;
    blank = skipb();
    if (ps > slen) {
      ps = save;
      break;
    }
    c = pch();
    if (c == ')' || c == ',' || c == '>' || c == ']' || c == ':'
        || c == '=' ||
        c == ';') {
      ps = save;
      break;
    }
    ol = binop_at(ps, blank, op);
    if (ol > 0) {
      p = oprec(op);
      if (p < minp) {
        ps = save;
        break;
      }
      ps = ps + ol;
      skipb();
      if (strcmp(op, "**") == 0 || op[0] == '!')
        right0 = parse_expr(p);
      else
        right0 = parse_expr(p + 1);
      left0 = mk_binary(op, left0, right0);
      continue;
    }
    if (blank == 1) {
      if (4 < minp) {
        ps = save;
        break;
      }
      right0 = parse_expr(5);
      left0 = mk(N_CAT, left0, right0, NULL, 0);
      continue;
    }
    ps = save;
    break;
  }
  return left0;
}

static long parse_operand(void)
{
  char c;
  long x, nd_;
  skipb();
  c = pch();
  if (c == '-' || c == '+') {
    char s1[2];
    ps = ps + 1;
    x = parse_operand();
    if (c == '-' && ndp[x].t == N_INT) {
      ndp[x].i = 0 - ndp[x].i;
      return x;
    }
    if (c == '-' && ndp[x].t == N_REAL) {
      ndp[x].r = 0.0 - ndp[x].r;
      return x;
    }
    s1[0] = c;
    s1[1] = '\0';
    return mk(N_UNARY, x, 0, sv_lit(s1), 0);
  }
  if (c == '?' || c == '~' || c == '\\') {
    char s1[2];
    ps = ps + 1;
    x = parse_operand();
    s1[0] = c;
    s1[1] = '\0';
    return mk(N_UNARY, x, 0, sv_lit(s1), 0);
  }
  if (c == '.') {
    ps = ps + 1;
    x = parse_operand();
    return mk(N_NAMEOF, x, 0, NULL, 0);
  }
  if (c == '$') {
    ps = ps + 1;
    x = parse_operand();
    return mk(N_INDIR, x, 0, NULL, 0);
  }
  if (c == '*') {
    ps = ps + 1;
    x = parse_operand();
    return mk(N_DEFER, x, 0, NULL, 0);
  }
  if (c == '@') {
    ps = ps + 1;
    x = parse_operand();
    return mk(N_CURSOR, x, 0, NULL, 0);
  }
  if (c == '&') {
    Sv *nm;
    x = ps + 1;
    while (x <= slen && is_namech(sch(x)))
      x = x + 1;
    nm = sv_upper(src + ps, x - ps - 1);
    nd_ = mk(N_KW, 0, 0, nm, 0);
    ndp[nd_].i = kwcode(SP(nm), SN(nm));
    ps = x;
    return nd_;
  }
  return parse_primary();
}

static long parse_primary(void)
{
  char c = pch();
  long q, j, e, nd_;
  int isreal;
  if (c == '\'' || c == '"') {
    q = ps + 1;
    while (q <= slen && src[q - 1] != c)
      q++;
    if (q > slen) {
      pfail("unterminated string");
      ps = slen + 1;
      return mke();
    }
    nd_ = mk(N_STR, 0, 0, sv_new(src + ps, q - ps - 1), 0);
    ps = q + 1;
    return postfix(nd_);
  }
  if (is_digit_c(c)) {
    long v;
    j = ps;
    isreal = 0;
    while (j <= slen && is_digit_c(sch(j)))
      j = j + 1;
    if (j <= slen && sch(j) == '.') {
      isreal = 1;
      j = j + 1;
      while (j <= slen && is_digit_c(sch(j)))
        j = j + 1;
    }
    if (j <= slen && up1(sch(j)) == 'E') {
      e = j + 1;
      if (e <= slen && (sch(e) == '+' || sch(e) == '-'))
        e = e + 1;
      if (e <= slen && is_digit_c(sch(e))) {
        isreal = 1;
        j = e;
        while (j <= slen && is_digit_c(sch(j)))
          j = j + 1;
      }
    }
    if (!isreal && !parse_long(src + ps - 1, j - ps, &v))
      isreal = 1;
    if (isreal) {
      nd_ = mk(N_REAL, 0, 0, NULL, 0);
      ndp[nd_].r = numtofloat(src + ps - 1, j - ps);
    } else
      nd_ = mk(N_INT, 0, 0, NULL, v);
    ps = j;
    return nd_;
  }
  if (c == '(') {
    ps = ps + 1;
    nd_ = parse_expr(1);
    skipb();
    if (pch() == ')')
      ps = ps + 1;
    else
      pfail("missing )");
    return postfix(nd_);
  }
  if (is_alpha_c(c)) {
    Sv *nm0;
    j = ps;
    while (j <= slen && is_namech(sch(j)))
      j = j + 1;
    nm0 = fold ? sv_upper(src + ps - 1, j - ps)
        : sv_new(src + ps - 1, j - ps);
    ps = j;
    if (pch() == '(') {
      ps = ps + 1;
      nd_ = parse_call(SP(nm0), SN(nm0));
      sv_dec(nm0);
    } else
      nd_ = mk(N_VAR, vslot(SP(nm0), SN(nm0)), 0, nm0, 0);
    return postfix(nd_);
  }
  {
    char m[40];
    sprintf(m, "unexpected '%c'", c);
    pfail(m);
  }
  ps = slen + 1;
  return mk(N_STR, 0, 0, NULL, 0);
}

/* function call: name '(' already consumed */
static long parse_call(const char *nme, long nl)
{
  long first, a, na;
  char c;
  long tcap = 8;
  long *tmp = (long *)xrealloc(NULL, 8 * sizeof(long));
  long nd_;
  na = 0;
  for (;;) {
    skipb();
    c = pch();
    if (c == ')') {
      if (na > 0) {
        na = na + 1;
        if (na >= tcap) {
          tcap *= 2;
          tmp = (long *)xrealloc(tmp, (size_t)tcap * sizeof(long));
        }
        tmp[na] = mke();
      }
      ps = ps + 1;
      break;
    }
    if (c == ',') {
      na = na + 1;
      if (na >= tcap) {
        tcap *= 2;
        tmp = (long *)xrealloc(tmp, (size_t)tcap * sizeof(long));
      }
      tmp[na] = mke();
      ps = ps + 1;
      continue;
    }
    if (perr == 1 || ps > slen) {
      char m[200];
      sprintf(m, "missing ) in call to %s", nme);
      pfail(m);
      break;
    }
    a = parse_expr(1);
    na = na + 1;
    if (na >= tcap) {
      tcap *= 2;
      tmp = (long *)xrealloc(tmp, (size_t)tcap * sizeof(long));
    }
    tmp[na] = a;
    skipb();
    c = pch();
    if (c == ',') {
      ps = ps + 1;
      skipb();
      if (pch() == ')') {
        na = na + 1;
        if (na >= tcap) {
          tcap *= 2;
          tmp = (long *)xrealloc(tmp, (size_t)tcap * sizeof(long));
        }
        tmp[na] = mke();
        ps = ps + 1;
        break;
      }
    } else if (c == ')') {
      ps = ps + 1;
      break;
    } else {
      pfail("bad argument list");
      break;
    }
  }
  first = nal + 1;
  for (a = 1; a <= na; a++) {
    nal = nal + 1;
    GROW(al, alcap, nal, long);
    al[nal] = tmp[a];
  }
  free(tmp);
  nd_ = mk(N_CALL, first, na, sv_new(nme, nl), bfn_index(nme, nl));
  return nd_;
}

static long postfix(long base)
{
  long nd_ = base;
  long first, a, na;
  char c, cl;
  long tcap = 8;
  long *tmp = (long *)xrealloc(NULL, 8 * sizeof(long));
  for (;;) {
    c = pch();
    if (c != '<' && c != '[')
      break;
    cl = c == '<' ? '>' : ']';
    ps = ps + 1;
    na = 0;
    for (;;) {
      skipb();
      if (pch() == cl) {
        if (na == 0) {
          na = 1;
          tmp[1] = mke();
        }
        ps = ps + 1;
        break;
      }
      if (perr == 1 || ps > slen) {
        pfail("missing subscript close");
        break;
      }
      a = parse_expr(1);
      na = na + 1;
      if (na >= tcap) {
        tcap *= 2;
        tmp = (long *)xrealloc(tmp, (size_t)tcap * sizeof(long));
      }
      tmp[na] = a;
      skipb();
      if (pch() == ',')
        ps = ps + 1;
    }
    first = nal + 1;
    for (a = 1; a <= na; a++) {
      nal = nal + 1;
      GROW(al, alcap, nal, long);
      al[nal] = tmp[a];
    }
    nd_ = mk(N_INDEX, first, na, NULL, nd_);
    if (perr == 1)
      break;
  }
  free(tmp);
  return nd_;
}

static long goto_target(void)
{
  long e, nd_;
  const char *tp;
  long tn;
  char c;
  skipb();
  e = ps;
  while (e <= slen && src[e - 1] != ')')
    e++;
  if (e > slen) {
    pfail("missing ) in goto");
    return mke();
  }
  strip_span(src + ps - 1, e - ps, &tp, &tn);
  c = tn > 0 ? tp[0] : ' ';
  if (c != '$' && c != '(' && c != '*' && c != '\'' && c != '"') {
    Sv *t = fold ? sv_upper(tp, tn) : sv_new(tp, tn);
    ps = e + 1;
    return mk(N_STR, 0, 0, t, 0);
  }
  nd_ = parse_expr(1);
  skipb();
  if (pch() == ')')
    ps = ps + 1;
  else
    pfail("missing ) in goto");
  return nd_;
}

static void parse_goto(long sidx)
{
  char c, c2;
  long nd_;
  for (;;) {
    skipb();
    if (ps > slen)
      break;
    if (perr == 1)
      break;
    c = up1(pch());
    c2 = sch(ps + 1);
    if ((c == 'S' || c == 'F') && c2 == '(') {
      ps = ps + 2;
      nd_ = goto_target();
      if (c == 'S')
        stp[sidx].gs = nd_;
      else
        stp[sidx].gf = nd_;
    } else if (c == '(') {
      ps = ps + 1;
      nd_ = goto_target();
      stp[sidx].gu = nd_;
    } else {
      pfail("bad goto field");
      break;
    }
  }
}

/* statement body:  [subject [pattern] [= replacement]] [: goto] */
static void parse_body(long sidx)
{
  long subj, pat;
  char c;
  skipb();
  if (ps > slen)
    return;
  c = pch();
  if (c == ':') {
    ps = ps + 1;
    parse_goto(sidx);
    return;
  }
  subj = parse_operand();
  stp[sidx].subj = subj;
  skipb();
  if (ps > slen)
    return;
  c = pch();
  if (c == ':') {
    ps = ps + 1;
    parse_goto(sidx);
    return;
  }
  if (c != '=') {
    pat = parse_expr(1);
    stp[sidx].pat = pat;
    skipb();
    if (ps > slen)
      return;
    c = pch();
  }
  if (c == '=') {
    ps = ps + 1;
    stp[sidx].hasrep = 1;
    skipb();
    if (ps > slen || pch() == ':')
      stp[sidx].repl = mke();
    else
      stp[sidx].repl = parse_expr(1);
    skipb();
    if (ps > slen)
      return;
    c = pch();
  }
  if (c == ':') {
    ps = ps + 1;
    parse_goto(sidx);
  } else {
    Sv *t = crx_substr_n(src, slen, ps, 12);
    Sv *m = sv_cat2("unexpected text '", 17, SP(t), SN(t));
    Sv *m2 = sv_cat2(SP(m), SN(m), "'", 1);
    pfail(SP(m2));
    sv_dec(t);
    sv_dec(m);
    sv_dec(m2);
  }
}
/* ===== s3_pat.c ===== */
/* s3_pat.c - pattern nodes, the backtracking matcher, statement
   execution */

static void eval_node(long nd_);
static void get_lv(long nd_);
static int exec_from(long pc0);

/* takes over the reference to s */
static long pnode(int t, long a, long b, Sv *s, long i)
{
  npn = npn + 1;
  GROW(pnp, pncap, npn, PN);
  pnp[npn].t = t;
  pnp[npn].a = a;
  pnp[npn].b = b;
  pnp[npn].s = s;
  pnp[npn].i = i;
  return npn;
}

/* registers -> pattern root (0 on error) */
static long topat(void)
{
  Sv *t;
  switch (rt) {
  case 3:
    return ri;
  case 0:
    return pnode(PT_LIT, 0, 0, sv_ref(rs), 0);
  case 1:
  case 2:
    t = rstr();
    return pnode(PT_LIT, 0, 0, t, 0);
  case 8:
    return pnode(PT_DEFER, 0, 0, NULL, ri);
  default:
    break;
  }
  snerr(4, "Illegal data type for pattern");
  return 0;
}

static long newcell(long nd_, long x, long y, long nx)
{
  kn_n = kn_n + 1;
  GROW(knp, kncap, kn_n, KN);
  knp[kn_n].node = nd_;
  knp[kn_n].x = x;
  knp[kn_n].y = y;
  knp[kn_n].next = nx;
  return kn_n;
}

static void push_choice(long nd_, long p, long kt, long md, long ag)
{
  csp = csp + 1;
  GROW(csv, cscap, csp, CS);
  csv[csp].node = nd_;
  csv[csp].pos = p;
  csv[csp].kont = kt;
  csv[csp].asg = asgn_n;
  csv[csp].md = md;
  csv[csp].ag = ag;
}

/* next balanced end after prev, for a BAL match starting at start (0
   if none) */
static long bal_next(long start, long prev)
{
  long j;
  long depth0 = 0;
  char c;
  for (j = start + 1; j <= prev; j++) {
    c = ms[j - 1];
    if (c == '(')
      depth0 = depth0 + 1;
    else if (c == ')')
      depth0 = depth0 - 1;
  }
  for (j = prev + 1; j <= mlen; j++) {
    c = ms[j - 1];
    if (c == '(')
      depth0 = depth0 + 1;
    else if (c == ')') {
      depth0 = depth0 - 1;
      if (depth0 < 0)
        return 0;
    }
    if (depth0 == 0)
      return j;
  }
  return 0;
}

static int in_set(char c, const Sv *set)
{
  long n = SN(set);
  return n > 0 && memchr(set->c, c, (size_t)n) != NULL;
}

/* first index >= from (1-based) holding a char in (member=1) / not in
   (member=0) the set; 0 if none */
static long verify_pos(const Sv *set, int member, long from)
{
  long q;
  for (q = from; q <= mlen; q++) {
    if (in_set(ms[q - 1], set) == member)
      return q;
  }
  return 0;
}

/* try to match the pattern at cursor s0: 1 on success (end cursor in
   mend) */
static int match_at(long root, long s0)
{
  long node, pos, kont, mode, csp0, kn0, asg0;
  long md, ag, t, n, q, cell, ky, nm, j;
  csp0 = csp;
  kn0 = kn_n;
  asg0 = asgn_n;
  node = root;
  pos = s0;
  kont = 0;
  mode = 0;
  for (;;) {
    if (mode == 2) {
      if (csp <= csp0 || abort_m == 1 || halted == 1) {
        csp = csp0;
        kn_n = kn0;
        asgn_n = asg0;
        return 0;
      }
      node = csv[csp].node;
      pos = csv[csp].pos;
      kont = csv[csp].kont;
      asgn_n = csv[csp].asg;
      md = csv[csp].md;
      ag = csv[csp].ag;
      csp = csp - 1;
      switch (md) {
      case 0:
        mode = 0;
        break;
      case 1:
        if (pos + ag <= mlen) {
          if (pos + ag < mlen)
            push_choice(node, pos, kont, 1, ag + 1);
          pos = pos + ag;
          mode = 1;
        }
        break;
      case 2:
        kont = newcell(node, pos, K_ARBNOCHK, kont);
        node = pnp[node].a;
        mode = 0;
        break;
      case 4:
        q = verify_pos(pnp[node].s, 1, pos + 2);
        if (q > 0) {
          pos = q - 1;
          push_choice(node, pos, kont, 4, 0);
          mode = 1;
        }
        break;
      case 5:
        abort_m = 1;
        break;
      case 6:
        q = bal_next(pos, ag);
        if (q > 0) {
          push_choice(node, pos, kont, 6, q);
          pos = q;
          mode = 1;
        }
        break;
      default:
        break;
      }
      continue;
    }
    if (mode == 1) {
      if (kont == 0) {
        mend = pos;
        for (j = asg0 + 1; j <= asgn_n; j++) {
          nm = asv[j].nm;
          setstr_own(sv_new(ms + asv[j].a, asv[j].b - asv[j].a));
          store_nm(nm_k[nm], nm_a[nm]);
        }
        csp = csp0;
        kn_n = kn0;
        asgn_n = asg0;
        return 1;
      }
      cell = kont;
      kont = knp[cell].next;
      ky = knp[cell].y;
      if (ky == 0) {
        node = knp[cell].node;
        mode = 0;
        continue;
      }
      switch (ky) {
      case K_CONDEND:
        asgn_n = asgn_n + 1;
        GROW(asv, ascap, asgn_n, AS);
        asv[asgn_n].nm = pnp[knp[cell].node].i;
        asv[asgn_n].a = knp[cell].x;
        asv[asgn_n].b = pos;
        break;
      case K_IMMEND:
        nm = pnp[knp[cell].node].i;
        setstr_own(sv_new(ms + knp[cell].x, pos - knp[cell].x));
        store_nm(nm_k[nm], nm_a[nm]);
        if (halted == 1)
          mode = 2;
        break;
      case K_ARBNOCHK:
        if (pos == knp[cell].x)
          mode = 2;
        else {
          node = knp[cell].node;
          mode = 0;
        }
        break;
      case K_FENCEEND:
        csp = knp[cell].x;
        break;
      default:
        break;
      }
      continue;
    }
    t = pnp[node].t;
    switch (t) {
    case PT_LIT:
      n = SN(pnp[node].s);
      if (n == 0)
        mode = 1;
      else if (pos + n <= mlen
          && memcmp(ms + pos, pnp[node].s->c, (size_t)n) == 0) {
        pos = pos + n;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_CAT:
      kont = newcell(pnp[node].b, 0, 0, kont);
      node = pnp[node].a;
      break;
    case PT_ALT:
      push_choice(pnp[node].b, pos, kont, 0, 0);
      node = pnp[node].a;
      break;
    case PT_ANY:
      if (pos < mlen && in_set(ms[pos], pnp[node].s)) {
        pos = pos + 1;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_NOTANY:
      if (pos < mlen && !in_set(ms[pos], pnp[node].s)) {
        pos = pos + 1;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_SPAN:
      q = verify_pos(pnp[node].s, 0, pos + 1);
      if (q == 0)
        q = mlen + 1;
      n = q - (pos + 1);
      if (n > 0) {
        pos = pos + n;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_BREAK:
    case PT_BREAKX:
      q = 0;
      if (pos < mlen)
        q = verify_pos(pnp[node].s, 1, pos + 1);
      if (q == 0)
        mode = 2;
      else {
        pos = q - 1;
        if (t == PT_BREAKX)
          push_choice(node, pos, kont, 4, 0);
        mode = 1;
      }
      break;
    case PT_LEN:
      n = pnp[node].i;
      if (n >= 0 && pos + n <= mlen) {
        pos = pos + n;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_POS:
      if (pos == pnp[node].i)
        mode = 1;
      else
        mode = 2;
      break;
    case PT_RPOS:
      if (pos == mlen - pnp[node].i)
        mode = 1;
      else
        mode = 2;
      break;
    case PT_TAB:
      n = pnp[node].i;
      if (n >= pos && n <= mlen) {
        pos = n;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_RTAB:
      n = mlen - pnp[node].i;
      if (n >= pos && n >= 0) {
        pos = n;
        mode = 1;
      } else
        mode = 2;
      break;
    case PT_REM:
      pos = mlen;
      mode = 1;
      break;
    case PT_ARB:
      if (pos < mlen)
        push_choice(node, pos, kont, 1, 1);
      mode = 1;
      break;
    case PT_BAL:
      q = bal_next(pos, pos);
      if (q == 0)
        mode = 2;
      else {
        push_choice(node, pos, kont, 6, q);
        pos = q;
        mode = 1;
      }
      break;
    case PT_FAIL:
      mode = 2;
      break;
    case PT_ABORT:
      abort_m = 1;
      mode = 2;
      break;
    case PT_FENCE:
      push_choice(node, pos, kont, 5, 0);
      mode = 1;
      break;
    case PT_FENCEP:
      kont = newcell(0, csp, K_FENCEEND, kont);
      node = pnp[node].a;
      break;
    case PT_SUCCEED:
      push_choice(node, pos, kont, 0, 0);
      mode = 1;
      break;
    case PT_ARBNO:
      push_choice(node, pos, kont, 2, 0);
      mode = 1;
      break;
    case PT_COND:
      kont = newcell(node, pos, K_CONDEND, kont);
      node = pnp[node].a;
      break;
    case PT_IMM:
      kont = newcell(node, pos, K_IMMEND, kont);
      node = pnp[node].a;
      break;
    case PT_CURSOR:
      nm = pnp[node].i;
      setint(pos);
      store_nm(nm_k[nm], nm_a[nm]);
      if (halted == 1)
        mode = 2;
      else
        mode = 1;
      break;
    case PT_DEFER:
      eval_node(pnp[node].i);
      if (fail == 1) {
        if (halted == 0)
          fail = 0;
        mode = 2;
      } else {
        q = topat();
        if (q == 0)
          mode = 2;
        else
          node = q;
      }
      break;
    case PT_EMPTY:
      mode = 1;
      break;
    default:
      mode = 2;
      break;
    }
  }
}

/* match the pattern (root) against the string subj; 1/0.  The matched
   region is [mstart, mend) */
static int pat_match(Sv *subj, long root)
{
  const char *sv_ms = ms;
  long sv_len = mlen;
  long sv_st = mstart;
  long sv_en = mend;
  int sv_ab = abort_m;
  long s0, rs0, re0;
  int ok;
  Sv *hold = sv_ref(subj);
  ms = SP(subj);
  mlen = SN(subj);
  abort_m = 0;
  ok = 0;
  rs0 = 0;
  re0 = 0;
  s0 = 0;
  while (s0 <= mlen) {
    ok = match_at(root, s0);
    if (ok == 1) {
      rs0 = s0;
      re0 = mend;
      break;
    }
    if (abort_m == 1 || halted == 1 || kw_anchor != 0)
      break;
    s0 = s0 + 1;
  }
  ms = sv_ms;
  mlen = sv_len;
  abort_m = sv_ab;
  mstart = rs0;
  if (ok == 1)
    mend = re0;
  else
    mend = sv_en;
  if (ok == 0)
    mstart = sv_st;
  sv_dec(hold);
  return ok;
}

/* ---- statement execution ---- */
static int exec_stmt(long pc)
{
  long sn, pn_, rn, hasrep, k, a, pat;
  Sv *s;
  Sv *r2;
  Sv *res;
  sn = stp[pc].subj;
  pn_ = stp[pc].pat;
  rn = stp[pc].repl;
  hasrep = stp[pc].hasrep;
  fail = 0;
  if (sn == 0)
    return 1;
  if (pn_ == 0 && hasrep == 0) {
    eval_node(sn);
    if (fail == 1)
      return 0;
    return 1;
  }
  if (pn_ == 0) {
    get_lv(sn);
    if (fail == 1)
      return 0;
    k = lv_k;
    a = lv_a;
    eval_node(rn);
    if (fail == 1)
      return 0;
    store_nm(k, a);
    if (fail == 1)
      return 0;
    return 1;
  }
  k = 0;
  a = 0;
  if (hasrep == 1) {
    get_lv(sn);
    if (fail == 1)
      return 0;
    k = lv_k;
    a = lv_a;
    fetch_nm(k, a);
    if (fail == 1)
      return 0;
  } else {
    eval_node(sn);
    if (fail == 1)
      return 0;
  }
  if (rt > 2) {
    snerr(4, "Illegal data type for pattern match subject");
    return 0;
  }
  s = rstr();
  eval_node(pn_);
  if (fail == 1) {
    sv_dec(s);
    return 0;
  }
  pat = topat();
  if (pat == 0) {
    sv_dec(s);
    return 0;
  }
  if (pat_match(s, pat) == 0) {
    sv_dec(s);
    return 0;
  }
  if (hasrep == 0) {
    sv_dec(s);
    return 1;
  }
  eval_node(rn);
  if (fail == 1) {
    sv_dec(s);
    return 0;
  }
  if (rt > 2) {
    snerr(4, "Illegal data type in replacement");
    sv_dec(s);
    return 0;
  }
  r2 = rstr();
  {
    long ls = SN(s);
    long a1 = mstart;
    long a2 = ls - mend;
    if (a1 > ls)
      a1 = ls;
    if (a2 < 0)
      a2 = 0;
    res = sv_new(NULL, a1 + SN(r2) + a2);
    if (res != NULL) {
      if (a1 > 0)
        memcpy(res->c, s->c, (size_t)a1);
      if (SN(r2) > 0)
        memcpy(res->c + a1, r2->c, (size_t)SN(r2));
      if (a2 > 0)
        memcpy(res->c + a1 + SN(r2), s->c + mend, (size_t)a2);
    }
  }
  setstr_own(res);
  sv_dec(r2);
  sv_dec(s);
  store_nm(k, a);
  if (fail == 1)
    return 0;
  return 1;
}

/* run statements from pc0; 0 (end), 1 RETURN, 2 FRETURN, 3 NRETURN */
static int exec_from(long pc0)
{
  long pc = pc0;
  int ok;
  long g, v;
  while (pc <= nst && halted == 0) {
    const char *lab;
    long labn;
    Sv *lsv = NULL;
    cur_st = pc;
    kw_stcount = kw_stcount + 1;
    if (kw_stlimit >= 0 && kw_stcount > kw_stlimit) {
      snerr(22, "Statement limit exceeded");
      break;
    }
    if (pc == endpc) {
      halted = 1;
      break;
    }
    ok = exec_stmt(pc);
    if (halted == 1)
      break;
    g = stp[pc].gu;
    if (g == 0) {
      if (ok == 1)
        g = stp[pc].gs;
      else
        g = stp[pc].gf;
    }
    if (g == 0) {
      pc = pc + 1;
      continue;
    }
    fail = 0;
    if (ndp[g].t == N_STR) {
      lab = SP(ndp[g].s);
      labn = SN(ndp[g].s);
    } else {
      if (ndp[g].t == N_INDIR)
        eval_node(ndp[g].a);
      else
        eval_node(g);
      if (fail == 1) {
        if (halted == 0)
          snerr(24, "Goto target evaluation failed");
        break;
      }
      if (rt == 7) {
        pc = cd_start[ri];
        continue;
      }
      lsv = rstr();
      lab = SP(lsv);
      labn = SN(lsv);
    }
    if (labn == 6 && memcmp(lab, "RETURN", 6) == 0) {
      sv_dec(lsv);
      return 1;
    }
    if (labn == 7 && memcmp(lab, "FRETURN", 7) == 0) {
      sv_dec(lsv);
      return 2;
    }
    if (labn == 7 && memcmp(lab, "NRETURN", 7) == 0) {
      sv_dec(lsv);
      return 3;
    }
    v = map_get(&labels, lab, labn);
    if (v == 0) {
      Sv *m = sv_cat2("Undefined label ", 16, lab, labn);
      snerr(24, SP(m));
      sv_dec(m);
      sv_dec(lsv);
      break;
    }
    sv_dec(lsv);
    kw_lastno = pc;
    pc = v;
  }
  return 0;
}

static void run_program(void)
{
  endpc = map_get(&labels, "END", 3);
  exec_from(1);
}
/* ===== s4_eval.c ===== */
/* s4_eval.c - arithmetic, lvalues, expression evaluation, calls,
   DEFINE */

static void builtin(long bi, long base, long na);
static void call_user(long fi, long base, long na);
static void invoke(const char *nme, long nl, long fi, long base,
    long na);

static void v_free(V *v)
{
  Sv *o = v->s;
  v->s = NULL;
  sv_dec(o);
}

/* CREXX leaves the previous result of the same operation when an
   integer operation overflows (initially 0); these emulate that */
static long last_add = 0;
static long last_sub = 0;
static long last_mul = 0;
static long last_div = 0;

/* ---- arithmetic: op 1 + 2 - 3 * 4 / 5 **; left operand lft, right in
   the registers ---- */
static void do_arith(long op, const V *lft)
{
  V bv;
  int at;
  long ai;
  double ar, x, y, z;
  memset(&bv, 0, sizeof(bv));
  v_set(&bv, &R);
  v_set(&R, lft);
  tonum();
  if (fail == 1) {
    v_free(&bv);
    return;
  }
  at = rt;
  ai = ri;
  ar = rr;
  v_set(&R, &bv);
  v_free(&bv);
  tonum();
  if (fail == 1)
    return;
  if (at == 1 && rt == 1) {
    long t;
    long rv = ri;
    switch (op) {
    case 1:
      if (add_ok(ai, rv, &t)) {
        last_add = t;
        setint(t);
      } else
        setint(last_add);
      break;
    case 2:
      if (sub_ok(ai, rv, &t)) {
        last_sub = t;
        setint(t);
      } else
        setint(last_sub);
      break;
    case 3:
      if (mul_ok(ai, rv, &t)) {
        last_mul = t;
        setint(t);
      } else
        setint(last_mul);
      break;
    case 4:
      if (rv == 0)
        snerr(12, "Division by zero");
      else if (rv == -1) {
        if (sub_ok(0, ai, &t)) {
          last_div = t;
          setint(t);
        } else
          setint(last_div);
      } else {
        last_div = ai / rv;
        setint(last_div);
      }
      break;
    default:
      {
        long e = rv;
        long base = ai;
        long res;
        if (e < 0) {
          if (base == 1)
            setint(1);
          else if (base == -1) {
            if (e % 2 == 0)
              setint(1);
            else
              setint(-1);
          } else if (base == 0)
            snerr(12, "Division by zero");
          else
            setint(0);
        } else {
          res = 1;
          while (e > 0) {
            if (e % 2 == 1) {
              if (mul_ok(res, base, &t))
                res = t;
            }
            e = e / 2;
            if (e > 0) {
              if (mul_ok(base, base, &t))
                base = t;
            }
          }
          setint(res);
        }
      }
      break;
    }
    return;
  }
  if (at == 1)
    x = (double)ai;
  else
    x = ar;
  if (rt == 1)
    y = (double)ri;
  else
    y = rr;
  switch (op) {
  case 1:
    z = x + y;
    break;
  case 2:
    z = x - y;
    break;
  case 3:
    z = x * y;
    break;
  case 4:
    if (y == 0.0) {
      snerr(12, "Division by zero");
      return;
    }
    z = x / y;
    break;
  default:
    if (x < 0.0 && y != dtrunc(y)) {
      snerr(13, "Illegal arithmetic");
      return;
    }
    if (x == 0.0 && y < 0.0) {
      snerr(12, "Division by zero");
      return;
    }
    z = pow(x, y);
    break;
  }
  setreal(z);
}

/* ---- lvalues ---- */
static long vslot_rt(Sv *nme)
{
  if (fold) {
    Sv *u = sv_upper(SP(nme), SN(nme));
    long r = vslot(SP(u), SN(u));
    sv_dec(u);
    return r;
  }
  return vslot(SP(nme), SN(nme));
}

static void eval_args(long first, long n)
{
  long i;
  for (i = 0; i < n; i++) {
    eval_node(al[first + i]);
    if (fail == 1)
      return;
    push_reg();
  }
}

static void get_lv(long nd_)
{
  int t = ndp[nd_].t;
  long base, na, e, tid, ty;
  fail = 0;
  switch (t) {
  case N_VAR:
    lv_k = NM_VAR;
    lv_a = ndp[nd_].a;
    break;
  case N_KW:
    lv_k = NM_KW;
    lv_a = ndp[nd_].i;
    break;
  case N_INDIR:
    eval_node(ndp[nd_].a);
    if (fail == 1)
      return;
    if (rt == 6) {
      lv_k = nm_k[ri];
      lv_a = nm_a[ri];
    } else {
      Sv *s = rstr();
      lv_k = NM_VAR;
      lv_a = vslot_rt(s);
      sv_dec(s);
    }
    break;
  case N_INDEX:
    eval_node(ndp[nd_].i);
    if (fail == 1)
      return;
    base = ab_cnt;
    ty = rt;
    tid = ri;
    eval_args(ndp[nd_].a, ndp[nd_].b);
    if (fail == 1) {
      ab_cnt = base;
      return;
    }
    na = ndp[nd_].b;
    if (ty == 4) {
      e = arr_elem(tid, base, na);
      if (e == 0)
        fail = 1;
      else {
        lv_k = NM_ELEM;
        lv_a = e;
      }
    } else if (ty == 5) {
      ld(base + 1);
      e = tbl_find(tid, 1);
      lv_k = NM_ELEM;
      lv_a = e;
    } else
      snerr(5, "Variable not present where required");
    ab_cnt = base;
    break;
  case N_CALL:
    {
      Sv *fnm = ndp[nd_].s;
      if (map_get(&fldmap, SP(fnm), SN(fnm)) != 0 &&
          map_get(&fnmap, SP(fnm), SN(fnm)) == 0) {
        long i;
        base = ab_cnt;
        eval_args(ndp[nd_].a, ndp[nd_].b);
        if (fail == 1) {
          ab_cnt = base;
          return;
        }
        ld(base + 1);
        ab_cnt = base;
        if (rt != 9) {
          snerr(5, "Variable not present where required");
          return;
        }
        i = field_index(ob_ty[ri], SP(fnm), SN(fnm));
        if (i == 0) {
          snerr(5, "Variable not present where required");
          return;
        }
        lv_k = NM_ELEM;
        lv_a = ob_base[ri] + i - 1;
      } else {
        eval_node(nd_);
        if (fail == 1)
          return;
        if (rt == 6) {
          lv_k = nm_k[ri];
          lv_a = nm_a[ri];
        } else
          snerr(5, "Variable not present where required");
      }
    }
    break;
  default:
    snerr(5, "Variable not present where required");
    break;
  }
}

/* ---- expression evaluation ---- */
static void eval_node(long nd_)
{
  int t;
  V lv;
  long pa, pb, base, na, e, v;
  fail = 0;
  t = ndp[nd_].t;
  switch (t) {
  case N_STR:
    setstr(ndp[nd_].s);
    break;
  case N_INT:
    setint(ndp[nd_].i);
    break;
  case N_REAL:
    setreal(ndp[nd_].r);
    break;
  case N_VAR:
    getvar(ndp[nd_].a);
    break;
  case N_KW:
    getkw(ndp[nd_].i);
    break;
  case N_UNARY:
    {
      char op = SP(ndp[nd_].s)[0];
      eval_node(ndp[nd_].a);
      if (op == '?') {
        if (fail == 0)
          setnull();
      } else if (op == '~' || op == '\\') {
        if (fail == 1) {
          if (halted == 0) {
            fail = 0;
            setnull();
          }
        } else
          fail = 1;
      } else if (fail == 0) {
        tonum();
        if (fail == 0 && op == '-') {
          if (rt == 1) {
            if (ri != LONG_MIN)
              ri = 0 - ri;
          } else
            rr = 0.0 - rr;
        }
      }
    }
    break;
  case N_BIN:
    eval_node(ndp[nd_].a);
    if (fail == 1)
      return;
    memset(&lv, 0, sizeof(lv));
    v_set(&lv, &R);
    eval_node(ndp[nd_].b);
    if (fail == 1) {
      v_free(&lv);
      return;
    }
    do_arith(ndp[nd_].i, &lv);
    v_free(&lv);
    break;
  case N_CAT:
    eval_node(ndp[nd_].a);
    if (fail == 1)
      return;
    memset(&lv, 0, sizeof(lv));
    v_set(&lv, &R);
    eval_node(ndp[nd_].b);
    if (fail == 1) {
      v_free(&lv);
      return;
    }
    if (lv.t == 3 || lv.t == 8 || rt == 3 || rt == 8) {
      pb = topat();
      if (pb == 0) {
        v_free(&lv);
        return;
      }
      v_set(&R, &lv);
      pa = topat();
      v_free(&lv);
      if (pa == 0)
        return;
      setobj(3, pnode(PT_CAT, pa, pb, NULL, 0));
    } else {
      Sv *s;
      Sv *l;
      Sv *res;
      if (rt > 2) {
        snerr(4, "Illegal data type");
        v_free(&lv);
        return;
      }
      s = rstr();
      v_set(&R, &lv);
      v_free(&lv);
      if (rt > 2) {
        snerr(4, "Illegal data type");
        sv_dec(s);
        return;
      }
      l = rstr();
      res = sv_cat2(SP(l), SN(l), SP(s), SN(s));
      sv_dec(l);
      sv_dec(s);
      setstr_own(res);
    }
    break;
  case N_ALT:
    eval_node(ndp[nd_].a);
    if (fail == 1)
      return;
    pa = topat();
    if (pa == 0)
      return;
    eval_node(ndp[nd_].b);
    if (fail == 1)
      return;
    pb = topat();
    if (pb == 0)
      return;
    setobj(3, pnode(PT_ALT, pa, pb, NULL, 0));
    break;
  case N_COND:
  case N_IMM:
    eval_node(ndp[nd_].a);
    if (fail == 1)
      return;
    pa = topat();
    if (pa == 0)
      return;
    get_lv(ndp[nd_].b);
    if (fail == 1)
      return;
    v = mkname(lv_k, lv_a);
    if (t == N_COND)
      setobj(3, pnode(PT_COND, pa, 0, NULL, v));
    else
      setobj(3, pnode(PT_IMM, pa, 0, NULL, v));
    break;
  case N_NAMEOF:
    get_lv(ndp[nd_].a);
    if (fail == 1)
      return;
    setobj(6, mkname(lv_k, lv_a));
    break;
  case N_INDIR:
    get_lv(nd_);
    if (fail == 1)
      return;
    fetch_nm(lv_k, lv_a);
    break;
  case N_DEFER:
    setobj(8, ndp[nd_].a);
    break;
  case N_CURSOR:
    get_lv(ndp[nd_].a);
    if (fail == 1)
      return;
    setobj(3, pnode(PT_CURSOR, 0, 0, NULL, mkname(lv_k, lv_a)));
    break;
  case N_MATCH:
    {
      Sv *s;
      eval_node(ndp[nd_].a);
      if (fail == 1)
        return;
      s = rstr();
      eval_node(ndp[nd_].b);
      if (fail == 1) {
        sv_dec(s);
        return;
      }
      pa = topat();
      if (pa == 0) {
        sv_dec(s);
        return;
      }
      if (pat_match(s, pa) == 1)
        setnull();
      else
        fail = 1;
      sv_dec(s);
    }
    break;
  case N_INDEX:
    {
      long lt;
      eval_node(ndp[nd_].i);
      if (fail == 1)
        return;
      base = ab_cnt;
      v = ri;
      lt = rt;
      eval_args(ndp[nd_].a, ndp[nd_].b);
      if (fail == 1) {
        ab_cnt = base;
        return;
      }
      na = ndp[nd_].b;
      if (lt == 4) {
        e = arr_elem(v, base, na);
        if (e == 0)
          fail = 1;
        else
          el_get(e);
      } else if (lt == 5) {
        ld(base + 1);
        e = tbl_find(v, 0);
        if (e == 0)
          setnull();
        else
          el_get(e);
      } else
        snerr(5, "Illegal data type for subscripting");
      ab_cnt = base;
    }
    break;
  case N_CALL:
    base = ab_cnt;
    eval_args(ndp[nd_].a, ndp[nd_].b);
    if (fail == 1) {
      ab_cnt = base;
      return;
    }
    invoke(SP(ndp[nd_].s), SN(ndp[nd_].s), ndp[nd_].i, base,
        ndp[nd_].b);
    ab_cnt = base;
    break;
  default:
    snerr(5, "Bad expression node");
    break;
  }
}

/* ---- function dispatch ---- */
static void invoke(const char *nme, long nl, long fi, long base,
    long na)
{
  long u, bi, i, ty;
  setnull();
  u = map_get(&fnmap, nme, nl);
  if (u != 0) {
    call_user(u, base, na);
    return;
  }
  u = map_get(&tymap, nme, nl);
  if (u != 0) {
    ty = u;
    nob = nob + 1;
    GBEGIN(obcap)
      GA(ob_ty, nob, long)
      GA(ob_base, nob, long)
    GEND(obcap)
    ob_ty[nob] = ty;
    ob_base[nob] = ne + 1;
    GROW(elp, necap, ne + ty_nf[ty], EL);
    for (i = 1; i <= ty_nf[ty]; i++) {
      ne = ne + 1;
      if (i <= na)
        v_set(&elp[ne].val, &abv[base + i]);
      else {
        v_free(&elp[ne].val);
        elp[ne].val.t = 0;
        elp[ne].val.i = 0;
        elp[ne].val.r = 0.0;
      }
    }
    setobj(9, nob);
    return;
  }
  if (map_get(&fldmap, nme, nl) != 0) {
    if (na < 1) {
      snerr(5, "Undefined function called");
      return;
    }
    ld(base + 1);
    if (rt != 9) {
      snerr(5, "Illegal data type for field function");
      return;
    }
    i = field_index(ob_ty[ri], nme, nl);
    if (i == 0) {
      snerr(5, "Illegal data type for field function");
      return;
    }
    el_get(ob_base[ri] + i - 1);
    return;
  }
  bi = fi;
  u = map_get(&fnalias, nme, nl);
  if (u != 0)
    bi = u;
  if (bi == 0) {
    Sv *m = sv_cat2("Undefined function called: ", 27, nme, nl);
    snerr(5, SP(m));
    sv_dec(m);
    return;
  }
  while (ab_cnt < base + na + 6) {
    setnull();
    push_reg();
  }
  setnull();
  builtin(bi, base, na);
}

static void call_user(long fi, long base, long na)
{
  long nvs, i, slot, f0, np, code, pc, v;
  Sv *lab;
  nvs = 1 + fn_nargs[fi] + fn_nloc[fi];
  f0 = fn_first[fi];
  np = fn_nargs[fi];
  if (depth > 6000) {
    snerr(15, "Stack overflow");
    return;
  }
  for (i = 0; i < nvs; i++) {
    slot = fn_vars[f0 + i];
    nsave = nsave + 1;
    GROW(svv, svcap, nsave, V);
    v_set(&svv[nsave], &vrp[slot].val);
  }
  for (i = 0; i < nvs; i++) {
    slot = fn_vars[f0 + i];
    if (i >= 1 && i <= np && i <= na)
      v_set(&vrp[slot].val, &abv[base + i]);
    else {
      v_free(&vrp[slot].val);
      vrp[slot].val.t = 0;
      vrp[slot].val.i = 0;
      vrp[slot].val.r = 0.0;
    }
  }
  lab = fn_entry[fi];
  v = map_get(&labels, SP(lab), SN(lab));
  if (v == 0) {
    Sv *m = sv_cat2("Undefined function entry label ", 31, SP(lab),
        SN(lab));
    snerr(5, SP(m));
    sv_dec(m);
    code = 2;
  } else {
    depth = depth + 1;
    code = exec_from(v);
    depth = depth - 1;
  }
  pc = 0;
  (void)pc;
  slot = fn_vars[f0];
  if (code == 2 || halted == 1)
    fail = 1;
  else {
    fail = 0;
    v_set(&R, &vrp[slot].val);
    if (code == 3 && rt == 6)
      fetch_nm(nm_k[ri], nm_a[ri]);
  }
  for (i = nvs - 1; i >= 0; i--) {
    slot = fn_vars[f0 + i];
    v_set(&vrp[slot].val, &svv[nsave]);
    v_free(&svv[nsave]);
    nsave = nsave - 1;
  }
}

/* ---- DEFINE ---- */
static void define_fn(Sv *proto, Sv *entry)
{
  const char *pp = SP(proto);
  long pl = SN(proto);
  long p = -1;
  long q = -1;
  long i, n, fi, v;
  const char *nme;
  long nl;
  const char *plist;
  long plen;
  const char *loc;
  long locn;
  Sv *nm;
  Sv *ent;
  for (i = 0; i < pl; i++)
    if (pp[i] == '(') {
      p = i;
      break;
    }
  for (i = 0; i < pl; i++)
    if (pp[i] == ')') {
      q = i;
      break;
    }
  if (p < 0 || q < p) {
    strip_span(pp, pl, &nme, &nl);
    plist = "";
    plen = 0;
    loc = "";
    locn = 0;
  } else {
    strip_span(pp, p, &nme, &nl);
    plist = pp + p + 1;
    plen = q - p - 1;
    loc = pp + q + 1;
    locn = pl - q - 1;
  }
  if (fold) {
    nm = sv_upper(nme, nl);
    ent = sv_upper(SP(entry), SN(entry));
  } else {
    nm = sv_new(nme, nl);
    ent = sv_ref(entry);
  }
  if (nm == NULL) {
    snerr(8, "DEFINE: null function name");
    sv_dec(ent);
    return;
  }
  v = map_get(&fnmap, nm->c, nm->n);
  if (v != 0)
    fi = v;
  else {
    nfn = nfn + 1;
    fi = nfn;
    map_set(&fnmap, nm->c, nm->n, fi);
  }
  GBEGIN(fncap)
    GA(fn_name, fi, Sv *)
    GA(fn_entry, fi, Sv *)
    GA(fn_nargs, fi, long)
    GA(fn_nloc, fi, long)
    GA(fn_first, fi, long)
  GEND(fncap)
  sv_dec(fn_name[fi]);
  fn_name[fi] = sv_ref(nm);
  sv_dec(fn_entry[fi]);
  {
    const char *ep;
    long en;
    strip_span(SP(ent), SN(ent), &ep, &en);
    if (en == 0)
      fn_entry[fi] = sv_ref(nm);
    else
      fn_entry[fi] = sv_new(ep, en);
  }
  fn_first[fi] = nfv + 1;
  nfv = nfv + 1;
  GROW(fn_vars, fvcap, nfv, long);
  fn_vars[nfv] = vslot(nm->c, nm->n);
  for (n = 0; n < 2; n++) {
    const char *lst = n == 0 ? plist : loc;
    long ln = n == 0 ? plen : locn;
    long cnt = 0;
    const char *f;
    long fl;
    strip_span(lst, ln, &lst, &ln);
    while (ln > 0) {
      long c = -1;
      Sv *fs;
      for (i = 0; i < ln; i++)
        if (lst[i] == ',') {
          c = i;
          break;
        }
      if (c < 0) {
        strip_span(lst, ln, &f, &fl);
        lst = lst + ln;
        ln = 0;
      } else {
        strip_span(lst, c, &f, &fl);
        lst = lst + c + 1;
        ln = ln - c - 1;
      }
      fs = fold ? sv_upper(f, fl) : sv_new(f, fl);
      cnt = cnt + 1;
      nfv = nfv + 1;
      GROW(fn_vars, fvcap, nfv, long);
      fn_vars[nfv] = vslot(SP(fs), SN(fs));
      sv_dec(fs);
    }
    if (n == 0)
      fn_nargs[fi] = cnt;
    else
      fn_nloc[fi] = cnt;
  }
  sv_dec(nm);
  sv_dec(ent);
}

/* ---- parsing strings at run time (EVAL, CODE) ---- */
static long parse_string_expr(Sv *txt)
{
  long nd_, v;
  const char *sv_src;
  long sv_ps, sv_len;
  int sv_err;
  Sv *sv_msg;
  v = map_get(&evc, SP(txt), SN(txt));
  if (v != 0)
    return v;
  sv_src = src;
  sv_ps = ps;
  sv_len = slen;
  sv_err = perr;
  sv_msg = sv_ref(perr_msg);
  src = SP(txt);
  ps = 1;
  slen = SN(txt);
  perr = 0;
  nd_ = parse_expr(1);
  skipb();
  if (ps <= slen && perr == 0)
    pfail("unexpected text");
  if (perr == 1) {
    Sv *m = sv_cat2("Syntax error in EVAL/CODE string: ", 34,
        SP(perr_msg),
                    SN(perr_msg));
    nd_ = 0;
    snerr(14, SP(m));
    sv_dec(m);
  }
  src = sv_src;
  ps = sv_ps;
  slen = sv_len;
  perr = sv_err;
  sv_dec(perr_msg);
  perr_msg = sv_msg;
  if (nd_ > 0)
    map_set(&evc, SP(txt), SN(txt), nd_);
  return nd_;
}

static long compile_code(Sv *txt)
{
  long first;
  const char *sv_src;
  long sv_ps, sv_len;
  int sv_err;
  Sv *sv_msg;
  int ok;
  sv_src = src;
  sv_ps = ps;
  sv_len = slen;
  sv_err = perr;
  sv_msg = sv_ref(perr_msg);
  first = nst + 1;
  ok = add_statement(SP(txt), SN(txt), 0);
  src = sv_src;
  ps = sv_ps;
  slen = sv_len;
  perr = sv_err;
  sv_dec(perr_msg);
  perr_msg = sv_msg;
  if (ok == 0) {
    nst = first - 1;
    return 0;
  }
  ncd = ncd + 1;
  GROW(cd_start, cdcap, ncd, long);
  cd_start[ncd] = first;
  return ncd;
}
/* ===== s5_bi.c ===== */
/* s5_bi.c - built-in functions, SORT, CONVERT, COPY, I/O association
   */

#define sarg(b, k) ab_str((b) + (k))
#define iarg(b, k) ab_int((b) + (k))
#define ARGNULL(k) (abv[base + (k)].t == 0 \
    && SN(abv[base + (k)].s) == 0)

static int numcmp(long a, long b)
{
  int at;
  long ai;
  double ar, x, y;
  ld(a);
  tonum();
  if (fail == 1)
    return 0;
  at = rt;
  ai = ri;
  ar = rr;
  ld(b);
  tonum();
  if (fail == 1)
    return 0;
  if (at == 1 && rt == 1) {
    if (ai < ri)
      return -1;
    if (ai > ri)
      return 1;
    return 0;
  }
  x = at == 1 ? (double)ai : ar;
  y = rt == 1 ? (double)ri : rr;
  if (x < y)
    return -1;
  if (x > y)
    return 1;
  return 0;
}

/* compare two sort keys held in srt[] (numbers before strings) */
static int srt_cmp(long a, long b)
{
  double x, y;
  if (srt[a].t == 0 && srt[b].t == 0)
    return str_cmp(SP(srt[a].s), SN(srt[a].s), SP(srt[b].s),
        SN(srt[b].s));
  if (srt[a].t == 0)
    return 1;
  if (srt[b].t == 0)
    return -1;
  x = srt[a].t == 1 ? (double)srt[a].i : srt[a].r;
  y = srt[b].t == 1 ? (double)srt[b].i : srt[b].r;
  if (x < y)
    return -1;
  if (x > y)
    return 1;
  return 0;
}

/* sort permutation srt_p[1..n] by srt[] keys (shell sort) */
static void srt_sort(long n, int desc)
{
  long gap, i, j, tmp;
  int ok;
  GROW(srt_p, srtpcap, n + 1, long);
  for (i = 1; i <= n; i++)
    srt_p[i] = i;
  gap = n / 2;
  while (gap > 0) {
    for (i = gap + 1; i <= n; i++) {
      tmp = srt_p[i];
      j = i;
      ok = 1;
      while (ok == 1 && j > gap) {
        if (desc == 0)
          ok = srt_cmp(srt_p[j - gap], tmp) > 0;
        else
          ok = srt_cmp(srt_p[j - gap], tmp) < 0;
        if (ok == 1) {
          srt_p[j] = srt_p[j - gap];
          j = j - gap;
        }
      }
      srt_p[j] = tmp;
    }
    if (gap == 1)
      gap = 0;
    else {
      gap = gap * 5 / 11;
      if (gap < 1)
        gap = 1;
    }
  }
}

/* the registers hold a value; replace it by a copy */
static void copy_val(void)
{
  int t = rt;
  long aid, tot, b, i, x, tid, o, e, na;
  if (t == 4) {
    aid = ri;
    tot = arr_size(aid);
    nar = nar + 1;
    GBEGIN(narcap)
      GA(ar_nd, nar, long)
      GA(ar_d0, nar, long)
      GA(ar_base, nar, long)
      GA(ar_proto, nar, Sv *)
    GEND(narcap)
    ar_nd[nar] = ar_nd[aid];
    ar_d0[nar] = ar_d0[aid];
    ar_proto[nar] = sv_ref(ar_proto[aid]);
    ar_base[nar] = ne + 1;
    b = ar_base[aid];
    GROW(elp, necap, ne + tot, EL);
    for (i = 0; i < tot; i++) {
      ne = ne + 1;
      v_set(&elp[ne].val, &elp[b + i].val);
    }
    setobj(4, nar);
  } else if (t == 5) {
    tid = ri;
    o = make_table();
    e = tb_head[tid];
    na = 0;
    while (e > 0) {
      na = na + 1;
      GROW(srt_v, srtvcap, na, long);
      srt_v[na] = e;
      e = elp[e].nx;
    }
    for (i = na; i >= 1; i--) {
      e = srt_v[i];
      key_load(e);
      x = tbl_find(o, 1);
      v_set(&elp[x].val, &elp[e].val);
    }
    setobj(5, o);
  } else if (t == 9) {
    o = ri;
    nob = nob + 1;
    GBEGIN(obcap)
      GA(ob_ty, nob, long)
      GA(ob_base, nob, long)
    GEND(obcap)
    ob_ty[nob] = ob_ty[o];
    ob_base[nob] = ne + 1;
    GROW(elp, necap, ne + ty_nf[ob_ty[o]], EL);
    for (i = 0; i < ty_nf[ob_ty[o]]; i++) {
      ne = ne + 1;
      v_set(&elp[ne].val, &elp[ob_base[o] + i].val);
    }
    setobj(9, nob);
  }
}

/* table -> srt_v list of elements in insertion order; returns count */
static long table_list(long tid)
{
  long e = tb_head[tid];
  long n = 0;
  long i, j, tmp;
  while (e > 0) {
    n = n + 1;
    GROW(srt_v, srtvcap, n, long);
    srt_v[n] = e;
    e = elp[e].nx;
  }
  i = 1;
  j = n;
  while (i < j) {
    tmp = srt_v[i];
    srt_v[i] = srt_v[j];
    srt_v[j] = tmp;
    i = i + 1;
    j = j - 1;
  }
  return n;
}

static Sv *n_comma_2(long n)
{
  char b[48];
  int k = fmt_long(n, b);
  b[k++] = ',';
  b[k++] = '2';
  return sv_new(b, k);
}

static void do_sort(int desc, long base)
{
  long tid, aid, n, i, e, r, b, tot;
  static const V zero = {0, NULL, 0, 0.0};
  ld(base + 1);
  if (rt == 5) {
    Sv *pr;
    tid = ri;
    n = table_list(tid);
    GROW(srt, srtcap, n + 1, V);
    for (i = 1; i <= n; i++) {
      e = srt_v[i];
      key_load(e);
      v_set(&srt[i], &R);
    }
    srt_sort(n, desc);
    pr = n_comma_2(n);
    r = make_array(pr, &zero);
    sv_dec(pr);
    if (r == 0)
      return;
    b = ar_base[r];
    for (i = 1; i <= n; i++) {
      e = srt_v[srt_p[i]];
      key_load(e);
      v_set(&elp[b + (i - 1) * 2].val, &R);
      v_set(&elp[b + (i - 1) * 2 + 1].val, &elp[e].val);
    }
    setobj(4, r);
    return;
  }
  if (rt != 4) {
    snerr(4, "SORT requires an array or table");
    return;
  }
  aid = ri;
  tot = arr_size(aid);
  b = ar_base[aid];
  GROW(srt, srtcap, tot + 1, V);
  for (i = 1; i <= tot; i++)
    v_set(&srt[i], &elp[b + i - 1].val);
  srt_sort(tot, desc);
  copy_val();
  r = ri;
  b = ar_base[r];
  for (i = 1; i <= tot; i++)
    v_set(&elp[b + i - 1].val, &srt[srt_p[i]]);
}

static int str_is(const Sv *s, const char *z)
{
  long n = (long)strlen(z);
  return SN(s) == n && (n == 0 || memcmp(s->c, z, (size_t)n) == 0);
}

static void do_convert(long base)
{
  Sv *tys;
  Sv *ty;
  int t;
  long k, n, i, r, b, tid, e, x, aid;
  static const V zero = {0, NULL, 0, 0.0};
  const char *sp0;
  long sn0;
  Sv *a2 = sarg(base, 2);
  strip_span(SP(a2), SN(a2), &sp0, &sn0);
  tys = sv_new(sp0, sn0);
  sv_dec(a2);
  ty = sv_upper(SP(tys), SN(tys));
  sv_dec(tys);
  ld(base + 1);
  t = rt;
  if (str_is(ty, "STRING")) {
    if (t <= 2)
      setstr_own(rstr());
    else if (t == 7)
      setnull();
    else
      fail = 1;
  } else if (str_is(ty, "INTEGER")) {
    if (t == 1) {
    } else if (t == 2) {
      if (rr > -9.2e18 && rr < 9.2e18 && rr < (double)LONG_MAX &&
          rr > (double)LONG_MIN)
        setint((long)dtrunc(rr));
      else
        fail = 1;
    } else if (t == 0) {
      k = str2num(SP(rs), SN(rs));
      if (k == 1)
        setint(nv_i);
      else if (k == 2 && nv_r > -9.2e18 && nv_r < 9.2e18 &&
               nv_r < (double)LONG_MAX && nv_r > (double)LONG_MIN)
        setint((long)dtrunc(nv_r));
      else
        fail = 1;
    } else
      fail = 1;
  } else if (str_is(ty, "REAL")) {
    if (t == 2) {
    } else if (t == 1)
      setreal((double)ri);
    else if (t == 0) {
      k = str2num(SP(rs), SN(rs));
      if (k == 1)
        setreal((double)nv_i);
      else if (k == 2)
        setreal(nv_r);
      else
        fail = 1;
    } else
      fail = 1;
  } else if (str_is(ty, "NUMERIC")) {
    if (t == 0) {
      k = str2num(SP(rs), SN(rs));
      if (k == 1)
        setint(nv_i);
      else if (k == 2)
        setreal(nv_r);
      else
        fail = 1;
    } else if (t > 2)
      fail = 1;
  } else if (str_is(ty, "PATTERN")) {
    k = topat();
    if (k > 0)
      setobj(3, k);
    else
      fail = 1;
  } else if (str_is(ty, "EXPRESSION")) {
    if (t == 8) {
    } else if (t <= 2) {
      Sv *tx = rstr();
      long nd_ = parse_string_expr(tx);
      sv_dec(tx);
      if (nd_ > 0)
        setobj(8, nd_);
      else
        fail = 1;
    } else
      fail = 1;
  } else if (str_is(ty, "NAME")) {
    if (t == 6) {
    } else if (t <= 2) {
      Sv *tx = rstr();
      setobj(6, mkname(NM_VAR, vslot_rt(tx)));
      sv_dec(tx);
    } else
      fail = 1;
  } else if (str_is(ty, "CODE")) {
    if (t == 7) {
    } else {
      Sv *tx = rstr();
      k = compile_code(tx);
      sv_dec(tx);
      if (k > 0)
        setobj(7, k);
      else
        fail = 1;
    }
  } else if (str_is(ty, "ARRAY")) {
    if (t == 4) {
    } else if (t == 5) {
      tid = ri;
      n = table_list(tid);
      if (n == 0)
        fail = 1;
      else {
        Sv *pr = n_comma_2(n);
        r = make_array(pr, &zero);
        sv_dec(pr);
        b = ar_base[r];
        for (i = 1; i <= n; i++) {
          e = srt_v[i];
          key_load(e);
          v_set(&elp[b + (i - 1) * 2].val, &R);
          v_set(&elp[b + (i - 1) * 2 + 1].val, &elp[e].val);
        }
        setobj(4, r);
      }
    } else
      fail = 1;
  } else if (str_is(ty, "TABLE")) {
    if (t == 5) {
    } else if (t == 4 && ar_nd[ri] == 2) {
      aid = ri;
      n = dm_hi[ar_d0[aid]] - dm_lo[ar_d0[aid]] + 1;
      tid = make_table();
      b = ar_base[aid];
      for (i = 1; i <= n; i++) {
        v_set(&R, &elp[b + (i - 1) * 2].val);
        x = tbl_find(tid, 1);
        v_set(&elp[x].val, &elp[b + (i - 1) * 2 + 1].val);
      }
      setobj(5, tid);
    } else
      fail = 1;
  } else
    fail = 1;
  sv_dec(ty);
}

/* INPUT / OUTPUT association */
static void io_assoc(int isout, long base, long na)
{
  long slot, u, k;
  Sv *fnm = NULL;
  ld(base + 1);
  if (rt == 6 && nm_k[ri] == NM_VAR)
    slot = nm_a[ri];
  else if (rt == 0 && SN(rs) > 0)
    slot = vslot_rt(rs);
  else {
    snerr(8, "INPUT/OUTPUT: bad variable");
    return;
  }
  if (ARGNULL(2)) {
    if (isout == 1)
      u = 6;
    else
      u = 5;
  } else
    u = ab_int(base + 2);
  if (fail == 1)
    return;
  if (u < 0 || u > 255) {
    snerr(8, "Bad unit");
    return;
  }
  for (k = 3; k <= na; k++) {
    if (abv[base + k].t == 0 && SN(abv[base + k].s) > 0) {
      const char *tp;
      long tn;
      Sv *a = abv[base + k].s;
      strip_span(a->c, a->n, &tp, &tn);
      if (str2num(a->c, a->n) == 0 && !(tn > 0 && tp[0] == '(')) {
        sv_dec(fnm);
        fnm = sv_new(tp, tn);
      }
    }
  }
  if (fnm != NULL) {
    FILE *h = fopen(fnm->c, isout == 1 ? "w" : "r");
    if (h == NULL) {
      sv_dec(fnm);
      fail = 1;
      return;
    }
    ou_h[u] = h;
    ou_m[u] = isout == 1 ? 2 : 1;
    ou_e[u] = 0;
    sv_dec(ou_n[u]);
    ou_n[u] = fnm;
  } else if (ou_m[u] == 0) {
    fail = 1;
    return;
  }
  vrp[slot].fl = isout == 1 ? 1 : 2;
  vrp[slot].unit = u;
}

static void do_dump(void)
{
  long i;
  for (i = 1; i <= nv; i++) {
    if (vrp[i].val.t != 0 || SN(vrp[i].val.s) > 0) {
      v_set(&R, &vrp[i].val);
      if (rt <= 2) {
        Sv *t = rstr();
        Sv *l = sv_cat2(SP(vrp[i].name), SN(vrp[i].name), " = ", 3);
        Sv *m = sv_cat2(SP(l), SN(l), SP(t), SN(t));
        put_line(6, SP(m), SN(m));
        sv_dec(t);
        sv_dec(l);
        sv_dec(m);
      }
    }
  }
}

static void num_fn(long bi)
{
  double x;
  tonum();
  if (fail != 0)
    return;
  x = rt == 1 ? (double)ri : rr;
  if (bi == FN_ATAN)
    setreal(atan(x));
  else if (bi == FN_SIN)
    setreal(sin(x));
  else if (bi == FN_COS)
    setreal(cos(x));
  else if (bi == FN_TAN)
    setreal(tan(x));
  else if (bi == FN_EXP)
    setreal(exp(x));
  else if (bi == FN_LN) {
    if (x <= 0.0)
      snerr(13, "Illegal argument to LN");
    else
      setreal(log(x));
  } else {
    if (x < 0.0)
      snerr(13, "Illegal argument to SQRT");
    else
      setreal(sqrt(x));
  }
}

static long time_ms(void)
{
#ifdef SNO_POSIX
  static long t0s = -1;
  static long t0u = 0;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (t0s < 0) {
    t0s = (long)tv.tv_sec;
    t0u = (long)tv.tv_usec;
  }
  return ((long)tv.tv_sec - t0s) * 1000L + ((long)tv.tv_usec - t0u)
      / 1000L;
#else
  return (long)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
#endif
}

/* arguments are abv[base+1 .. base+na] (padded with null values); the
   result is left in the registers */
static void builtin(long bi, long base, long na)
{
  long n, m, i, k, p, r, e;
  Sv *s = NULL;
  Sv *s2 = NULL;
  Sv *s3 = NULL;
  switch (bi) {
  case FN_ABS:
    ld(base + 1);
    tonum();
    if (fail == 0) {
      if (rt == 1) {
        if (ri < 0 && ri != LONG_MIN)
          ri = 0 - ri;
      } else
        rr = fabs(rr);
    }
    break;
  case FN_ANY:
    s = sarg(base, 1);
    setobj(3, pnode(PT_ANY, 0, 0, s, 0));
    s = NULL;
    break;
  case FN_NOTANY:
    s = sarg(base, 1);
    setobj(3, pnode(PT_NOTANY, 0, 0, s, 0));
    s = NULL;
    break;
  case FN_SPAN:
    s = sarg(base, 1);
    setobj(3, pnode(PT_SPAN, 0, 0, s, 0));
    s = NULL;
    break;
  case FN_BREAK:
    s = sarg(base, 1);
    setobj(3, pnode(PT_BREAK, 0, 0, s, 0));
    s = NULL;
    break;
  case FN_BREAKX:
    s = sarg(base, 1);
    setobj(3, pnode(PT_BREAKX, 0, 0, s, 0));
    s = NULL;
    break;
  case FN_LEN:
    n = iarg(base, 1);
    if (fail == 0)
      setobj(3, pnode(PT_LEN, 0, 0, NULL, n));
    break;
  case FN_POS:
    n = iarg(base, 1);
    if (fail == 0)
      setobj(3, pnode(PT_POS, 0, 0, NULL, n));
    break;
  case FN_RPOS:
    n = iarg(base, 1);
    if (fail == 0)
      setobj(3, pnode(PT_RPOS, 0, 0, NULL, n));
    break;
  case FN_TAB:
    n = iarg(base, 1);
    if (fail == 0)
      setobj(3, pnode(PT_TAB, 0, 0, NULL, n));
    break;
  case FN_RTAB:
    n = iarg(base, 1);
    if (fail == 0)
      setobj(3, pnode(PT_RTAB, 0, 0, NULL, n));
    break;
  case FN_REM:
    setobj(3, pnode(PT_REM, 0, 0, NULL, 0));
    break;
  case FN_ARB:
    setobj(3, pnode(PT_ARB, 0, 0, NULL, 0));
    break;
  case FN_BAL:
    setobj(3, pnode(PT_BAL, 0, 0, NULL, 0));
    break;
  case FN_FAIL:
    fail = 1;
    break;
  case FN_ABORT:
    setobj(3, pnode(PT_ABORT, 0, 0, NULL, 0));
    break;
  case FN_SUCCEED:
    setobj(3, pnode(PT_SUCCEED, 0, 0, NULL, 0));
    break;
  case FN_ARBNO:
    ld(base + 1);
    p = topat();
    if (p > 0)
      setobj(3, pnode(PT_ARBNO, p, 0, NULL, 0));
    break;
  case FN_FENCE:
    if (na == 0)
      setobj(3, pnode(PT_FENCE, 0, 0, NULL, 0));
    else {
      ld(base + 1);
      p = topat();
      if (p > 0)
        setobj(3, pnode(PT_FENCEP, p, 0, NULL, 0));
    }
    break;
  case FN_APPLY:
    s = sarg(base, 1);
    if (fold) {
      Sv *u = sv_upper(SP(s), SN(s));
      sv_dec(s);
      s = u;
    }
    if (na < 1)
      snerr(5, "APPLY: no function");
    else
      invoke(SP(s), SN(s), bfn_index(SP(s), SN(s)), base + 1, na - 1);
    sv_dec(s);
    s = NULL;
    break;
  case FN_ARG:
  case FN_LOCAL:
    s = sarg(base, 1);
    n = iarg(base, 2);
    if (fail == 0) {
      k = map_get(&fnmap, SP(s), SN(s));
      if (k == 0)
        fail = 1;
      else if (bi == FN_ARG) {
        if (n < 1 || n > fn_nargs[k])
          fail = 1;
        else
          setstr(vrp[fn_vars[fn_first[k] + n]].name);
      } else {
        if (n < 1 || n > fn_nloc[k])
          fail = 1;
        else
          setstr(vrp[fn_vars[fn_first[k] + fn_nargs[k] + n]].name);
      }
    }
    sv_dec(s);
    s = NULL;
    break;
  case FN_ARRAY:
    s = sarg(base, 1);
    r = make_array(s, &abv[base + 2]);
    if (r > 0)
      setobj(4, r);
    sv_dec(s);
    s = NULL;
    break;
  case FN_ATAN:
  case FN_SIN:
  case FN_COS:
  case FN_TAN:
  case FN_EXP:
  case FN_LN:
  case FN_SQRT:
    ld(base + 1);
    num_fn(bi);
    break;
  case FN_BACKSPACE:
  case FN_EJECT:
  case FN_DETACH:
  case FN_TRACE:
  case FN_STOPTR:
  case FN_COLLECT:
  case FN_UNLOAD:
    if (bi == FN_COLLECT)
      setint(0);
    break;
  case FN_CHAR:
    n = iarg(base, 1);
    if (fail == 0) {
      if (n < 0 || n > 255)
        snerr(8, "CHAR: argument out of range");
      else {
        char c1 = (char)n;
        setstr_own(sv_new(&c1, 1));
      }
    }
    break;
  case FN_CHOP:
    ld(base + 1);
    tonum();
    if (fail == 0 && rt == 2)
      rr = dtrunc(rr);
    break;
  case FN_CLEAR:
    for (i = 1; i <= nv; i++) {
      v_free(&vrp[i].val);
      vrp[i].val.t = 0;
      vrp[i].val.i = 0;
      vrp[i].val.r = 0.0;
    }
    break;
  case FN_CODE:
    s = sarg(base, 1);
    k = compile_code(s);
    if (k > 0)
      setobj(7, k);
    else
      fail = 1;
    sv_dec(s);
    s = NULL;
    break;
  case FN_CONVERT:
    do_convert(base);
    break;
  case FN_COPY:
    ld(base + 1);
    copy_val();
    break;
  case FN_DATA:
    s = sarg(base, 1);
    define_data(s);
    sv_dec(s);
    s = NULL;
    break;
  case FN_DATATYPE:
    ld(base + 1);
    setstr_own(sv_lit(dtype()));
    break;
  case FN_DATE:
    setstr_own(sv_lit("01/01/2000"));
    break;
  case FN_DEFINE:
    s = sarg(base, 1);
    s2 = sarg(base, 2);
    define_fn(s, s2);
    sv_dec(s);
    sv_dec(s2);
    s = NULL;
    s2 = NULL;
    break;
  case FN_DIFFER:
    if (ident_ab(base + 1, base + 2) == 1)
      fail = 1;
    break;
  case FN_IDENT:
    if (ident_ab(base + 1, base + 2) == 0)
      fail = 1;
    break;
  case FN_DUMP:
    do_dump();
    break;
  case FN_ENDFILE:
    n = iarg(base, 1);
    if (fail == 0 && n >= 0 && n <= 255) {
      if (ou_m[n] != 0 && n > 7) {
        fclose(ou_h[n]);
        ou_m[n] = 0;
        ou_h[n] = NULL;
      }
    }
    break;
  case FN_REWIND:
    n = iarg(base, 1);
    if (fail == 0 && n > 7 && n <= 255 && ou_m[n] != 0) {
      fclose(ou_h[n]);
      ou_h[n] = fopen(SP(ou_n[n]), ou_m[n] == 1 ? "r" : "a");
      ou_e[n] = 0;
    }
    break;
  case FN_EQ:
  case FN_NE:
  case FN_LT:
  case FN_LE:
  case FN_GT:
  case FN_GE:
    k = numcmp(base + 1, base + 2);
    if (fail == 0) {
      if (bi == FN_EQ && k != 0)
        fail = 1;
      if (bi == FN_NE && k == 0)
        fail = 1;
      if (bi == FN_LT && k >= 0)
        fail = 1;
      if (bi == FN_LE && k > 0)
        fail = 1;
      if (bi == FN_GT && k <= 0)
        fail = 1;
      if (bi == FN_GE && k < 0)
        fail = 1;
    }
    break;
  case FN_LEQ:
  case FN_LNE:
  case FN_LLT:
  case FN_LLE:
  case FN_LGT:
  case FN_LGE:
    s = sarg(base, 1);
    s2 = sarg(base, 2);
    k = str_cmp(SP(s), SN(s), SP(s2), SN(s2));
    sv_dec(s);
    sv_dec(s2);
    s = NULL;
    s2 = NULL;
    if (bi == FN_LEQ && k != 0)
      fail = 1;
    if (bi == FN_LNE && k == 0)
      fail = 1;
    if (bi == FN_LLT && k >= 0)
      fail = 1;
    if (bi == FN_LLE && k > 0)
      fail = 1;
    if (bi == FN_LGT && k <= 0)
      fail = 1;
    if (bi == FN_LGE && k < 0)
      fail = 1;
    break;
  case FN_EVAL:
    ld(base + 1);
    if (rt == 8)
      eval_node(ri);
    else if (rt == 0) {
      Sv *tx = sv_ref(rs);
      k = parse_string_expr(tx);
      sv_dec(tx);
      if (k > 0)
        eval_node(k);
      else
        fail = 1;
    } else if (rt == 1 || rt == 2) {
    } else if (rt == 3 || rt == 4 || rt == 5 || rt == 9) {
    } else
      fail = 1;
    break;
  case FN_EXIT:
    n = 0;
    if (na >= 1 && abv[base + 1].t != 0)
      n = iarg(base, 1);
    rc_exit = (int)n;
    halted = 1;
    break;
  case FN_FIELD:
    s = sarg(base, 1);
    n = iarg(base, 2);
    k = map_get(&tymap, SP(s), SN(s));
    if (k == 0)
      fail = 1;
    else if (n < 1 || n > ty_nf[k])
      fail = 1;
    else
      setstr(ty_fld[ty_f0[k] + n - 1]);
    sv_dec(s);
    s = NULL;
    break;
  case FN_HOST:
    setnull();
    break;
  case FN_INPUT:
    io_assoc(0, base, na);
    break;
  case FN_OUTPUT:
    io_assoc(1, base, na);
    break;
  case FN_INTEGER:
    ld(base + 1);
    if (rt == 1) {
    } else if (rt == 0) {
      const char *tp;
      long tn;
      strip_span(SP(rs), SN(rs), &tp, &tn);
      if (str2num(SP(rs), SN(rs)) != 1)
        fail = 1;
      else if (tn == 0)
        fail = 1;
      else
        setnull();
    } else
      fail = 1;
    if (fail == 0)
      setnull();
    break;
  case FN_REAL:
    ld(base + 1);
    if (rt != 2)
      fail = 1;
    else
      setnull();
    break;
  case FN_ITEM:
    ld(base + 1);
    if (rt == 4) {
      e = arr_elem(ri, base + 1, na - 1);
      if (e == 0)
        fail = 1;
      else
        el_get(e);
    } else if (rt == 5) {
      long t = ri;
      ld(base + 2);
      e = tbl_find(t, 0);
      if (e == 0)
        setnull();
      else
        el_get(e);
    } else
      fail = 1;
    break;
  case FN_LPAD:
  case FN_RPAD:
    s = sarg(base, 1);
    n = iarg(base, 2);
    s2 = sarg(base, 3);
    if (fail == 0) {
      char pc = SN(s2) > 0 ? s2->c[0] : ' ';
      if (SN(s) >= n)
        setstr(s);
      else {
        long cnt = n - SN(s);
        Sv *res = sv_new(NULL, n);
        if (bi == FN_LPAD) {
          memset(res->c, pc, (size_t)cnt);
          memcpy(res->c + cnt, SP(s), (size_t)SN(s));
        } else {
          memcpy(res->c, SP(s), (size_t)SN(s));
          memset(res->c + SN(s), pc, (size_t)cnt);
        }
        setstr_own(res);
      }
    }
    sv_dec(s);
    sv_dec(s2);
    s = NULL;
    s2 = NULL;
    break;
  case FN_OPSYN:
    s = sarg(base, 1);
    s2 = sarg(base, 2);
    if (fold) {
      Sv *u = sv_upper(SP(s), SN(s));
      Sv *u2 = sv_upper(SP(s2), SN(s2));
      sv_dec(s);
      sv_dec(s2);
      s = u;
      s2 = u2;
    }
    if (map_get(&fnmap, SP(s2), SN(s2)) != 0)
      map_set(&fnmap, SP(s), SN(s), map_get(&fnmap, SP(s2), SN(s2)));
    else if (bfn_index(SP(s2), SN(s2)) > 0) {
      map_set(&fnmap, SP(s), SN(s), 0);
      map_set(&fnalias, SP(s), SN(s), bfn_index(SP(s2), SN(s2)));
    } else if (map_get(&fnalias, SP(s2), SN(s2)) != 0)
      map_set(&fnalias, SP(s), SN(s),
          map_get(&fnalias, SP(s2), SN(s2)));
    else
      snerr(5, "OPSYN: undefined function");
    sv_dec(s);
    sv_dec(s2);
    s = NULL;
    s2 = NULL;
    break;
  case FN_PROTOTYPE:
    ld(base + 1);
    if (rt == 4)
      setstr(ar_proto[ri]);
    else if (rt == 5)
      setint(tb_n[ri]);
    else
      fail = 1;
    break;
  case FN_RANDOM:
    rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
    n = 0;
    if (na >= 1 && abv[base + 1].t != 0)
      n = iarg(base, 1);
    if (n > 0)
      setint((long)(rnd_state % (unsigned long)n));
    else
      setreal((double)rnd_state / 2147483648.0);
    break;
  case FN_REMDR:
    n = iarg(base, 1);
    m = iarg(base, 2);
    if (fail == 0) {
      if (m == 0)
        snerr(12, "Division by zero");
      else if (m == -1)
        setint(0);
      else
        setint(n % m);
    }
    break;
  case FN_REPLACE:
    s = sarg(base, 1);
    s2 = sarg(base, 2);
    s3 = sarg(base, 3);
    if (SN(s2) != SN(s3))
      snerr(8, "REPLACE: argument lengths differ");
    else {
      Sv *res = sv_new(SP(s), SN(s));
      for (i = 0; i < SN(s); i++) {
        for (k = 0; k < SN(s2); k++)
          if (s2->c[k] == s->c[i]) {
            res->c[i] = s3->c[k];
            break;
          }
      }
      setstr_own(res);
    }
    sv_dec(s);
    sv_dec(s2);
    sv_dec(s3);
    s = NULL;
    s2 = NULL;
    s3 = NULL;
    break;
  case FN_REVERSE:
    s = sarg(base, 1);
    {
      Sv *res = sv_new(SP(s), SN(s));
      for (i = 0; i < SN(s); i++)
        res->c[i] = s->c[SN(s) - 1 - i];
      setstr_own(res);
    }
    sv_dec(s);
    s = NULL;
    break;
  case FN_SIZE:
    ld(base + 1);
    s = rstr();
    setint(SN(s));
    sv_dec(s);
    s = NULL;
    break;
  case FN_SORT:
    do_sort(0, base);
    break;
  case FN_RSORT:
    do_sort(1, base);
    break;
  case FN_SUBSTR:
    s = sarg(base, 1);
    i = iarg(base, 2);
    if (ARGNULL(3))
      n = SN(s) - i + 1;
    else
      n = iarg(base, 3);
    if (fail == 0) {
      if (i < 1 || n < 0 || i + n - 1 > SN(s))
        fail = 1;
      else
        setstr_own(sv_new(SP(s) + i - 1, n));
    }
    sv_dec(s);
    s = NULL;
    break;
  case FN_DUPL:
    s = sarg(base, 1);
    n = iarg(base, 2);
    if (fail == 0) {
      if (n < 0)
        snerr(8, "DUPL: negative count");
      else if (SN(s) == 0 || n == 0)
        setnull();
      else {
        Sv *res;
        if (n > 100000000L / SN(s))
          snerr(8, "DUPL: result too large");
        else {
          res = sv_new(NULL, n * SN(s));
          for (i = 0; i < n; i++)
            memcpy(res->c + i * SN(s), s->c, (size_t)SN(s));
          setstr_own(res);
        }
      }
    }
    sv_dec(s);
    s = NULL;
    break;
  case FN_TABLE:
    setobj(5, make_table());
    break;
  case FN_TIME:
    setint(time_ms());
    break;
  case FN_TRIM:
    s = sarg(base, 1);
    {
      long b = SN(s);
      while (b > 0 && strchr(SPACES, s->c[b - 1]) != NULL
          && s->c[b - 1] != '\0')
        b--;
      setstr_own(sv_new(SP(s), b));
    }
    sv_dec(s);
    s = NULL;
    break;
  case FN_LOAD:
    snerr(8, "LOAD not supported");
    break;
  default:
    snerr(5, "Unimplemented function");
    break;
  }
  if (fail == 0) {
    switch (bi) {
    case FN_EQ:
    case FN_NE:
    case FN_LT:
    case FN_LE:
    case FN_GT:
    case FN_GE:
    case FN_LEQ:
    case FN_LNE:
    case FN_LLT:
    case FN_LLE:
    case FN_LGT:
    case FN_LGE:
    case FN_DEFINE:
    case FN_DATA:
    case FN_ENDFILE:
    case FN_REWIND:
    case FN_OPSYN:
    case FN_CLEAR:
    case FN_INPUT:
    case FN_OUTPUT:
    case FN_IDENT:
    case FN_DIFFER:
    case FN_DUMP:
    case FN_BACKSPACE:
    case FN_EJECT:
    case FN_DETACH:
    case FN_TRACE:
    case FN_STOPTR:
    case FN_UNLOAD:
    case FN_EXIT:
      setnull();
      break;
    default:
      break;
    }
  }
}
/* ===== s6_main.c ===== */
/* s6_main.c - predefined variables, initialisation, main */

static void def_globals(void)
{
  long s;
  ou_h[5] = h_in;
  ou_m[5] = 1;
  ou_e[5] = 0;
  ou_h[6] = h_out;
  ou_m[6] = 2;
  ou_h[7] = h_err;
  ou_m[7] = 2;
  s = vslot("OUTPUT", 6);
  vrp[s].fl = 1;
  vrp[s].unit = 6;
  s = vslot("INPUT", 5);
  vrp[s].fl = 2;
  vrp[s].unit = 5;
  s = vslot("TERMINAL", 8);
  vrp[s].fl = 1;
  vrp[s].unit = 7;
  s = vslot("ARB", 3);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_ARB, 0, 0, NULL, 0);
  s = vslot("BAL", 3);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_BAL, 0, 0, NULL, 0);
  s = vslot("REM", 3);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_REM, 0, 0, NULL, 0);
  s = vslot("FAIL", 4);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_FAIL, 0, 0, NULL, 0);
  s = vslot("FENCE", 5);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_FENCE, 0, 0, NULL, 0);
  s = vslot("ABORT", 5);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_ABORT, 0, 0, NULL, 0);
  s = vslot("SUCCEED", 7);
  vrp[s].val.t = 3;
  vrp[s].val.i = pnode(PT_SUCCEED, 0, 0, NULL, 0);
}

static void usage(void)
{
  printf("usage: snobol [-f] program.sno\n");
}

int main(int argc, char **argv)
{
  const char *fname = NULL;
  int ia;
  int u;
  h_out = stdout;
  h_in = stdin;
  h_err = stderr;
  def_globals();
  for (ia = 1; ia < argc; ia++) {
    const char *oa = argv[ia];
    if (fname == NULL && oa[0] == '-' && oa[1] != '\0') {
      if (strcmp(oa, "-f") == 0 || strcmp(oa, "-F") == 0)
        fold = 1;
      else if (strcmp(oa, "-h") == 0 || strcmp(oa, "-H") == 0) {
        usage();
        return 0;
      } else {
        printf("snobol: unknown option %s\n", oa);
        return 1;
      }
    } else if (fname == NULL)
      fname = oa;
  }
  if (fname == NULL) {
    usage();
    return 1;
  }
  if (load_program(fname) == 0)
    return 1;
  run_program();
  flush_out();
  for (u = 8; u < 256; u++)
    if (ou_m[u] != 0 && ou_h[u] != NULL)
      fclose(ou_h[u]);
  return rc_exit;
}
