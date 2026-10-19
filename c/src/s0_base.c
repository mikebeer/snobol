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

/* ---- literal character sets (no code point ranges: EBCDIC safe) ---- */
static const char LOWER[] = "abcdefghijklmnopqrstuvwxyz";
static const char UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char DIGITS[] = "0123456789";
static const char SPACES[] = " \t\n\v\f\r";

/* ---- strings: reference counted, NUL terminated, NULL means "" ---- */
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

/* strict string comparison (REXX "<<"): bytewise, shorter prefix is less */
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
  return c != '\0' && (strchr(LOWER, c) != NULL || strchr(UPPER, c) != NULL);
}

static int is_namech(char c)
{
  return c == '.' || c == '_' || is_digit_c(c) || is_alpha_c(c);
}

/* REXX strip(): blanks and white space at both ends; returns span */
static void strip_span(const char *s, long n, const char **ps, long *pn)
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
    if (SN(m->k[i]) == n && (n == 0 || memcmp(m->k[i]->c, p, (size_t)n) == 0))
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

/* ---- integers: overflow is detected (CREXX leaves the old value) ---- */
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

/* parse an optionally signed decimal integer (whole span); 0 if bad/overflow */
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

/* ---- real numbers: CREXX prints 15 significant digits, plain notation
   up to exponent 17, else d.ddde+XX (see README) ---- */
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
  } else if (strncmp(abs_start, "0.", 2) == 0 && abs_start[2] != '\0') {
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

/* fmtreal of the CREXX source: E upper case, a "." forced into the mantissa */
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

/* ---- continuation cells, choice points, conditional assignments ---- */
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

static long kw_anchor = 0, kw_trim = 0, kw_fullscan = 0, kw_errlimit = 0;
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

