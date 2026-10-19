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
