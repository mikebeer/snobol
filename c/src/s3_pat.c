/* s3_pat.c - pattern nodes, the backtracking matcher, statement execution */

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

/* next balanced end after prev, for a BAL match starting at start (0 if
   none) */
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

/* try to match the pattern at cursor s0: 1 on success (end cursor in mend) */
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
      else if (pos + n <= mlen && memcmp(ms + pos, pnp[node].s->c, (size_t)n) == 0) {
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
