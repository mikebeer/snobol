/* s4_eval.c - arithmetic, lvalues, expression evaluation, calls, DEFINE */

static void builtin(long bi, long base, long na);
static void call_user(long fi, long base, long na);
static void invoke(const char *nme, long nl, long fi, long base, long na);

static void v_free(V *v)
{
  Sv *o = v->s;
  v->s = NULL;
  sv_dec(o);
}

/* CREXX leaves the previous result of the same operation when an integer
   operation overflows (initially 0); these emulate that */
static long last_add = 0;
static long last_sub = 0;
static long last_mul = 0;
static long last_div = 0;

/* ---- arithmetic: op 1 + 2 - 3 * 4 / 5 **; left operand lft, right in the
   registers ---- */
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
    invoke(SP(ndp[nd_].s), SN(ndp[nd_].s), ndp[nd_].i, base, ndp[nd_].b);
    ab_cnt = base;
    break;
  default:
    snerr(5, "Bad expression node");
    break;
  }
}

/* ---- function dispatch ---- */
static void invoke(const char *nme, long nl, long fi, long base, long na)
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
    Sv *m = sv_cat2("Undefined function entry label ", 31, SP(lab), SN(lab));
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
    Sv *m = sv_cat2("Syntax error in EVAL/CODE string: ", 34, SP(perr_msg),
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
