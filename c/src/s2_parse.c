/* s2_parse.c - loading the program text and parsing statements */

static long parse_expr(long minp);
static long parse_operand(void);
static long parse_primary(void);
static long postfix(long base);
static long parse_call(const char *nme, long nl);
static int parse_statement(const char *txt, long n, long li);
static int add_statement(const char *txt, long n, long li);

/* REXX wordpos(phrase, bfn): index of the phrase in the builtin word list */
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
      if ((long)strlen(b) != wl[j] || memcmp(b, w[j], (size_t)wl[j]) != 0)
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
    if ((long)strlen(names[k]) == n && memcmp(names[k], s, (size_t)n) == 0)
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
    if (ln >= 3 && (memcmp(lp, "END", 3) == 0 || memcmp(lp, "end", 3) == 0)) {
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

/* binary operator at position p?  returns length (0 if none) and the code */
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
  if (c == '+' || c == '-' || c == '*' || c == '.' || c == '$' || c == '?') {
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
    if (c == ')' || c == ',' || c == '>' || c == ']' || c == ':' || c == '=' ||
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
    nm0 = fold ? sv_upper(src + ps - 1, j - ps) : sv_new(src + ps - 1, j - ps);
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
