/* s5_bi.c - built-in functions, SORT, CONVERT, COPY, I/O association */

#define sarg(b, k) ab_str((b) + (k))
#define iarg(b, k) ab_int((b) + (k))
#define ARGNULL(k) (abv[base + (k)].t == 0 && SN(abv[base + (k)].s) == 0)

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
    return str_cmp(SP(srt[a].s), SN(srt[a].s), SP(srt[b].s), SN(srt[b].s));
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
  return ((long)tv.tv_sec - t0s) * 1000L + ((long)tv.tv_usec - t0u) / 1000L;
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
      map_set(&fnalias, SP(s), SN(s), map_get(&fnalias, SP(s2), SN(s2)));
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
      while (b > 0 && strchr(SPACES, s->c[b - 1]) != NULL && s->c[b - 1] != '\0')
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
