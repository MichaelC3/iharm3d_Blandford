/******************************************************************************
 *                                                                            *
 * DIAG.C                                                                     *
 *                                                                            *
 * DIAGNOSTIC OUTPUT                                                          *
 *                                                                            *
 ******************************************************************************/

#include "decs.h"

void reset_log_variables()
{
  #if RADIATION
  step_tot = step_made = step_abs = step_scatt = step_rec = 0;
  #endif
  mass_added = 0.0;
}

// Evaluate flux based diagnostics; put results in global variables
// Note this is still per-process
void diag_flux(struct FluidFlux *F)
{
  mdot = edot = ldot = 0.;
  mdot_eh = edot_eh = ldot_eh = 0.;
  int iEH = NG + 5;
  if (global_start[0] == 0) {
    #pragma omp parallel for \
      reduction(+:mdot) reduction(+:edot) reduction(+:ldot) \
      reduction(+:mdot_eh) reduction(+:edot_eh) reduction(+:ldot_eh) \
      collapse(2)
    JSLOOP(0, N2 - 1) {
      KSLOOP(0, N3 - 1) {
        mdot += -F->X1[RHO][k][j][NG]*dx[2]*dx[3];
        edot += (F->X1[UU][k][j][NG] - F->X1[RHO][k][j][NG])*dx[2]*dx[3];
        ldot += F->X1[U3][k][j][NG]*dx[2]*dx[3];
        mdot_eh += -F->X1[RHO][k][j][iEH]*dx[2]*dx[3];
        edot_eh += (F->X1[UU][k][j][iEH] - F->X1[RHO][k][j][iEH])*dx[2]*dx[3];
        ldot_eh += F->X1[U3][k][j][iEH]*dx[2]*dx[3];
      }
    }
  }
}

void diag(struct GridGeom *G, struct FluidState *S, int call_code)
{
  static FILE *ener_file;

  if (call_code == DIAG_INIT) {
    // Set things up
    if(mpi_io_proc()) {
      ener_file = fopen("dumps/log.out", "a");
      if (ener_file == NULL) {
        fprintf(stderr,
          "error opening energy output file\n");
        exit(1);
      }
    }
  }

  double pp = 0.;
  double divbmax = 0.;
  double rmed = 0.;
  double e = 0.;
  // Calculate conserved quantities
  if ((call_code == DIAG_INIT || call_code == DIAG_LOG ||
       call_code == DIAG_FINAL) && !failed) {

    get_state_vec(G, S, CENT, 0, N3 - 1, 0, N2 - 1, 0, N1 - 1);
    prim_to_flux_vec(G, S, 0, CENT, 0, N3 - 1, 0, N2 - 1, 0, N1 - 1, S->U);

    //TODO OpenMP this
    ZLOOP {
      rmed += S->U[RHO][k][j][i]*dV;
      pp += S->U[U3][k][j][i]*dV;
      e += S->U[UU][k][j][i]*dV;

      double divb = flux_ct_divb(G, S, i, j, k);

      if (divb > divbmax) {
        divbmax = divb;
      }
    }
  }

  rmed = mpi_reduce(rmed);
  pp = mpi_reduce(pp);
  e = mpi_reduce(e);
  divbmax = mpi_max(divbmax);

  double mass_proc = 0.;
  double egas_proc = 0.;
  double Phi_proc = 0.;
  double jet_EM_flux_proc = 0.;
  double lum_eht_proc = 0.;
  ZLOOP {
    mass_proc += S->U[RHO][k][j][i]*dV;
    egas_proc += S->U[UU][k][j][i]*dV;
    double rho = S->P[RHO][k][j][i];
    double Pg = (gam - 1.)*S->P[UU][k][j][i];
    double bsq = bsq_calc(S, i, j, k);
    double Bmag = sqrt(bsq);
    double C_eht = 0.2;
    double j_eht = pow(rho,3.)*pow(Pg,-2.)*exp(-C_eht*pow(rho*rho/(Bmag*Pg*Pg),1./3.));
    lum_eht_proc += j_eht*dV*G->gdet[CENT][j][i];
    if (global_start[0] + i == 5+NG) {
      Phi_proc += 0.5*fabs(S->P[B1][k][j][i])*dx[2]*dx[3]*G->gdet[CENT][j][i];

      // TODO port properly.  Needs speculative get_state
//      double P_EM[NVAR];
//      PLOOP P_EM[ip] = S->P[ip][k][j][i];
//      P_EM[RHO] = 0.;
//      P_EM[UU] = 0.;
//      get_state(P_EM, &(ggeom[i][j][CENT]), &q);
//      double sig = bsq/S->P[RHO][k][j][i];
//      if (sig > 1.) {
//        primtoflux(P_EM, &q, 1, &(ggeom[i][j][CENT]), U);
//        jet_EM_flux_proc += -U[U1]*dx[2]*dx[3];
//      }
    }
  }
  double mass = mpi_reduce(mass_proc);
  double egas = mpi_reduce(egas_proc);
  double Phi = mpi_reduce(Phi_proc);
  double jet_EM_flux = mpi_reduce(jet_EM_flux_proc);
  double lum_eht = mpi_reduce(lum_eht_proc);

  if ((call_code == DIAG_INIT && !is_restart) ||
    call_code == DIAG_DUMP || call_code == DIAG_FINAL) {
    dump(G, S);
    dump_cnt++;
  }

  if (call_code == DIAG_INIT || call_code == DIAG_LOG ||
      call_code == DIAG_FINAL) {
    double mdot_all = mpi_reduce(mdot);
    double edot_all = mpi_reduce(edot);
    double ldot_all = mpi_reduce(ldot);
    double mdot_eh_all = mpi_reduce(mdot_eh);
    double edot_eh_all = mpi_reduce(edot_eh);
    double ldot_eh_all = mpi_reduce(ldot_eh);

    //mdot will be negative w/scheme above
    double phi = Phi/sqrt(fabs(mdot_all) + SMALL);

    if(mpi_io_proc()) {
      fprintf(stdout, "LOG      t=%g \t divbmax: %g\n",
        t,divbmax);
      fprintf(ener_file, "%10.5g %10.5g %10.5g %10.5g %15.8g %15.8g ",
        t, rmed, pp, e,
        S->P[UU][N3/2][N2/2][N1/2]*pow(S->P[RHO][N3/2][N2/2][N1/2], -gam),
        S->P[UU][N3/2][N2/2][N1/2]);
      fprintf(ener_file, "%15.8g %15.8g %15.8g ", mdot_all, edot_all, ldot_all);
      fprintf(ener_file, "%15.8g %15.8g ", mass, egas);
      fprintf(ener_file, "%15.8g %15.8g %15.8g ", Phi, phi, jet_EM_flux);
      fprintf(ener_file, "%15.8g ", divbmax);
      fprintf(ener_file, "%15.8g ", lum_eht);
      fprintf(ener_file, "%15.8g ", mass_added);
      fprintf(ener_file, "%15.8g %15.8g %15.8g ", mdot_eh_all, edot_eh_all, ldot_eh_all);
      fprintf(ener_file, "\n");
      fflush(ener_file);
    }
  }
}

// Diagnostic routines
void fail(struct GridGeom *G, struct FluidState *S, int fail_type, int i, int j, int k)
{
  failed = 1;

  fprintf(stderr, "\n\n[%d %d %d] FAIL: %d\n", i, j, k, fail_type);

  area_map(i, j, k, S->P);

  diag(G, S, DIAG_FINAL);

  exit(0);
}

// Map out region around failure point
void area_map(int i, int j, int k, GridPrim prim)
{
  fprintf(stderr, "*** AREA MAP ***\n");

  PLOOP {
    fprintf(stderr, "variable %d \n", ip);
    fprintf(stderr, "i = \t %12d %12d %12d\n", i - 1, i,
      i + 1);
    fprintf(stderr, "j = %d \t %12.5g %12.5g %12.5g\n", j + 1,
      prim[ip][k][j + 1][i - 1], prim[ip][k][j + 1][i],
      prim[ip][k][j + 1][i + 1]);
    fprintf(stderr, "j = %d \t %12.5g %12.5g %12.5g\n", j,
      prim[ip][k][j][i - 1], prim[ip][k][j][i],
      prim[ip][k][j][i + 1]);
    fprintf(stderr, "j = %d \t %12.5g %12.5g %12.5g\n", j - 1,
      prim[ip][k][j - 1][i - 1], prim[ip][k][j - 1][i],
      prim[ip][k][j - 1][i + 1]);
  }

  fprintf(stderr, "****************\n");
}

double flux_ct_divb(struct GridGeom *G, struct FluidState *S, int i, int j,
  int k)
{
  #if N3 > 1
  if(i > 0 + NG && j > 0 + NG && k > 0 + NG &&
     i < N1 + NG && j < N2 + NG && k < N3 + NG) {
  #elif N2 > 1
  if(i > 0 + NG && j > 0 + NG &&
     i < N1 + NG && j < N2 + NG) {
  #elif N1 > 1
  if(i > 0 + NG &&
     i < N1 + NG) {
  #else
  if (0) {
  #endif
    return fabs(0.25*(
      S->P[B1][k][j][i]*G->gdet[CENT][j][i]
      + S->P[B1][k][j-1][i]*G->gdet[CENT][j-1][i]
      + S->P[B1][k-1][j][i]*G->gdet[CENT][j][i]
      + S->P[B1][k-1][j-1][i]*G->gdet[CENT][j-1][i]
      - S->P[B1][k][j][i-1]*G->gdet[CENT][j][i-1]
      - S->P[B1][k][j-1][i-1]*G->gdet[CENT][j-1][i-1]
      - S->P[B1][k-1][j][i-1]*G->gdet[CENT][j][i-1]
      - S->P[B1][k-1][j-1][i-1]*G->gdet[CENT][j-1][i-1]
      )/dx[1] +
      0.25*(
      S->P[B2][k][j][i]*G->gdet[CENT][j][i]
      + S->P[B2][k][j][i-1]*G->gdet[CENT][j][i-1]
      + S->P[B2][k-1][j][i]*G->gdet[CENT][j][i]
      + S->P[B2][k-1][j][i-1]*G->gdet[CENT][j][i-1]
      - S->P[B2][k][j-1][i]*G->gdet[CENT][j-1][i]
      - S->P[B2][k][j-1][i-1]*G->gdet[CENT][j-1][i-1]
      - S->P[B2][k-1][j-1][i]*G->gdet[CENT][j-1][i]
      - S->P[B2][k-1][j-1][i-1]*G->gdet[CENT][j-1][i-1]
      )/dx[2] +
      0.25*(
      S->P[B3][k][j][i]*G->gdet[CENT][j][i]
      + S->P[B3][k][j-1][i]*G->gdet[CENT][j-1][i]
      + S->P[B3][k][j][i-1]*G->gdet[CENT][j][i-1]
      + S->P[B3][k][j-1][i-1]*G->gdet[CENT][j-1][i-1]
      - S->P[B3][k-1][j][i]*G->gdet[CENT][j][i]
      - S->P[B3][k-1][j-1][i]*G->gdet[CENT][j-1][i]
      - S->P[B3][k-1][j][i-1]*G->gdet[CENT][j][i-1]
      - S->P[B3][k-1][j-1][i-1]*G->gdet[CENT][j-1][i-1]
      )/dx[3]);
  } else {
    return 0.;
  }
}
