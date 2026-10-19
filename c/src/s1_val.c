/* s1_val.c - value registers, errors, numbers, variables, arrays, tables */

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
    fprintf(h_err, "snobol: error %ld at %s: %s\n", code, where_am_i(w), msg);
    fflush(h_err);
  }
  halted = 1;
  rc_exit = 1;
}

/* the text of a number given as C text: "1.5", ".5", "5.", "1.E5" -> double */
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

/* read one line from unit u into the registers; 1 on success, 0 at EOF */
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

/* pool index of the element addressed by ab[base+1 .. base+na], 0 if out of
   range */
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
    t = t * (dm_hi[ar_d0[aid] + d - 1] - dm_lo[ar_d0[aid] + d - 1] + 1);
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

/* find (or create) the element for the key in the registers; 0 if absent */
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
