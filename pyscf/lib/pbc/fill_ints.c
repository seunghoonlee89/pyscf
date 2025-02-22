/* Copyright 2014-2018,2021 The PySCF Developers. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

 *
 * Author: Qiming Sun <osirpt.sun@gmail.com>
 */

#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <assert.h>
#include "config.h"
#include "cint.h"
#include "gto/gto.h"
#include "np_helper/np_helper.h"
#include "vhf/nr_direct.h"
#include "vhf/fblas.h"
#include "pbc/pbc.h"

#define INTBUFMAX10     8000
#define OF_CMPLX        2

typedef void (*FPtrSort)(double *outR, double *outI, double *bufkkR, double *bufkkI,
                         int *shls, int *ao_loc, BVKEnvs *envs_bvk);
typedef void (*FPtrFill)(int (*intor)(double *, int *, int *, double, CINTEnvVars *, BVKEnvs *),
                         double *outR, double *outI, double *cache, int *cell0_shls,
                         CINTEnvVars *envs_cint, BVKEnvs *envs_bvk);

void PBCminimal_CINTEnvVars(CINTEnvVars *envs, int *atm, int natm, int *bas, int nbas, double *env,
                            CINTOpt *cintopt)
{
        envs->atm = atm;
        envs->bas = bas;
        envs->env = env;
        envs->natm = natm;
        envs->nbas = nbas;
        envs->opt = cintopt;
        envs->ncomp_e1 = 1;
        envs->ncomp_e2 = 1;
        envs->ncomp_tensor = 1;

}

/*
 * contract basis in supmol to basis of bvk-cell
 * sh_loc indicates the boundary of each bvkcell basis
 */
static int _assemble3c(double *out, int *cell0_shls, int *bvk_cells, double cutoff,
                       CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int *atm = envs_cint->atm;
        int *bas = envs_cint->bas;
        double *env = envs_cint->env;
        int natm = envs_cint->natm;
        int nbas = envs_cint->nbas;
        size_t Nbas = nbas;
        int ncomp = envs_bvk->ncomp;
        int nbasp = envs_bvk->nbasp;
        int nbas_bvk = nbasp * envs_bvk->ncells;
        int *cell0_ao_loc = envs_bvk->ao_loc;
        int *sh_loc = envs_bvk->sh_loc;
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        int ksh_cell0 = cell0_shls[2];
        int cell_i = bvk_cells[0];
        int cell_j = bvk_cells[1];
        int ish_bvk = ish_cell0 + cell_i * nbasp;
        int jsh_bvk = jsh_cell0 + cell_j * nbasp;
        int ksh_bvk = ksh_cell0 - nbasp + nbas_bvk;
        int ish0 = sh_loc[ish_bvk];
        int jsh0 = sh_loc[jsh_bvk];
        int ksh0 = sh_loc[ksh_bvk];
        int ish1 = sh_loc[ish_bvk+1];
        int jsh1 = sh_loc[jsh_bvk+1];
        int ksh1 = sh_loc[ksh_bvk+1];
        int i0 = cell0_ao_loc[ish_cell0];
        int j0 = cell0_ao_loc[jsh_cell0];
        int k0 = cell0_ao_loc[ksh_cell0];
        int i1 = cell0_ao_loc[ish_cell0+1];
        int j1 = cell0_ao_loc[jsh_cell0+1];
        int k1 = cell0_ao_loc[ksh_cell0+1];
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dijkc = di * dj * dk * ncomp;
        CINTOpt *cintopt = envs_cint->opt;
        double *bufL = out + dijkc;
        double *cache = bufL + dijkc;
        int shls[3];
        int n, ish, jsh, ksh;
        int8_t *ovlp_mask = envs_bvk->ovlp_mask;
        // aux_mask to skip smooth auxiliary basis if needed
        int *aux_mask = envs_bvk->bas_map;
        double *qcond = envs_bvk->q_cond;
        double *qcond_k;
        double kj_cutoff;
        int (*intor)() = envs_bvk->intor;

        int empty = 1;
        NPdset0(out, dijkc);

        if (ish0 == ish1 || jsh0 == jsh1 || ksh0 == ksh1) {
                return 0;
        }

        if (qcond != NULL) {
                for (ksh = ksh0; ksh < ksh1; ksh++) {
                        // (ksh - nbas) because ksh the basis in auxcell was appended to
                        // the end of supmol basis
                        if (!aux_mask[ksh]) {
                                continue;
                        }
                        qcond_k = qcond + (ksh - Nbas) * Nbas;
                        shls[2] = ksh;
                        for (ish = ish0; ish < ish1; ish++) {
                                kj_cutoff = cutoff / qcond_k[ish];
                                shls[0] = ish;
                                for (jsh = jsh0; jsh < jsh1; jsh++) {
                                        if (!ovlp_mask[ish*Nbas+jsh] ||
                                            qcond_k[jsh] < kj_cutoff) {
                                                continue;
                                        }
                                        shls[1] = jsh;
                                        if ((*intor)(bufL, NULL, shls, atm, natm,
                                                     bas, nbas, env, cintopt, cache)) {
                                                for (n = 0; n < dijkc; n++) {
                                                        out[n] += bufL[n];
                                                }
                                                empty = 0;
                                        }
                                }
                        }
                }
        } else {
                for (ksh = ksh0; ksh < ksh1; ksh++) {
                        // (ksh - nbas) because ksh the basis in auxcell was appended to
                        // the end of supmol basis
                        if (!aux_mask[ksh]) {
                                continue;
                        }
                        shls[2] = ksh;
                        for (ish = ish0; ish < ish1; ish++) {
                                shls[0] = ish;
                                for (jsh = jsh0; jsh < jsh1; jsh++) {
                                        if (!ovlp_mask[ish*Nbas+jsh]) {
                                                continue;
                                        }
                                        shls[1] = jsh;
                                        if ((*intor)(bufL, NULL, shls, atm, natm,
                                                     bas, nbas, env, cintopt, cache)) {
                                                for (n = 0; n < dijkc; n++) {
                                                        out[n] += bufL[n];
                                                }
                                                empty = 0;
                                        }
                                }
                        }
                }
        }
        return !empty;
}

// [kI, KJ, i, j, k, comp] in Fortran order => [kI, kJ, comp, i, j, k] in C order
static void _sort_kks1(double *outR, double *outI, double *bufkkR, double *bufkkI,
                       int *shls, int *ao_loc, BVKEnvs *envs_bvk)
{
        int *shls_slice = envs_bvk->shls_slice;
        int *kpt_ij_idx = envs_bvk->kpt_ij_idx;
        int kpt_ij_size = envs_bvk->kpt_ij_size;
        int nkpts = envs_bvk->nkpts;
        int comp = envs_bvk->ncomp;
        int ish = shls[0];
        int jsh = shls[1];
        int ksh = shls[2];
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int jsh0 = shls_slice[2];
        int jsh1 = shls_slice[3];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int ip0 = ao_loc[ish0];
        int ip1 = ao_loc[ish1];
        int jp0 = ao_loc[jsh0];
        int jp1 = ao_loc[jsh1];
        int kp0 = ao_loc[ksh0];
        int kp1 = ao_loc[ksh1];
        int i0 = ao_loc[ish] - ip0;
        int j0 = ao_loc[jsh] - jp0;
        int k0 = ao_loc[ksh] - kp0;
        int i1 = ao_loc[ish+1] - ip0;
        int j1 = ao_loc[jsh+1] - jp0;
        int k1 = ao_loc[ksh+1] - kp0;
        int ko, kikj, i, j, k, n, ij, ic;
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dij = di * dj;
        int KK = nkpts * nkpts;
        int KKdij = KK * dij;
        size_t naoi = ip1 - ip0;
        size_t naoj = jp1 - jp0;
        size_t naok = kp1 - kp0;
        size_t nao2 = naoi * naoj;
        size_t nao3 = nao2 * naok;
        size_t n3c = nao3 * comp;
        size_t off;
        double *pbufR, *pbufI;

        for (ic = 0; ic < comp; ic++) {
                for (n = 0, j = j0; j < j1; j++) {
                for (i = i0; i < i1; i++, n++) {
                        ij = i * naoj + j;
                        pbufR = bufkkR + n * KK;
                        pbufI = bufkkI + n * KK;
                        for (ko = 0; ko < kpt_ij_size; ko++) {
                                kikj = kpt_ij_idx[ko];
                                off = n3c * ko + ij * naok + k0;
                                for (k = 0; k < dk; k++) {
                                        outR[off+k] = pbufR[k*KKdij+kikj];
                                        outI[off+k] = pbufI[k*KKdij+kikj];
                                }
                        }
                } }
                outR += nao3;
                outI += nao3;
                bufkkR += KKdij * dk;
                bufkkI += KKdij * dk;
        }
}

// [kI, KJ, i, j, k, comp] in Fortran order => [kI, kJ, comp, i>=j, k] in C order
static void _sort_kks2(double *outR, double *outI, double *bufkkR, double *bufkkI,
                       int *shls, int *ao_loc, BVKEnvs *envs_bvk)
{
        int *shls_slice = envs_bvk->shls_slice;
        int *kpt_ij_idx = envs_bvk->kpt_ij_idx;
        int kpt_ij_size = envs_bvk->kpt_ij_size;
        int nkpts = envs_bvk->nkpts;
        int comp = envs_bvk->ncomp;
        int ish = shls[0];
        int jsh = shls[1];
        int ksh = shls[2];
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int ip0 = ao_loc[ish0];
        int ip1 = ao_loc[ish1];
        int kp0 = ao_loc[ksh0];
        int kp1 = ao_loc[ksh1];
        assert(ip1 == jp1 && jp0 == 0);
        int i0 = ao_loc[ish];
        int j0 = ao_loc[jsh];
        int k0 = ao_loc[ksh] - kp0;
        int i1 = ao_loc[ish+1];
        int j1 = ao_loc[jsh+1];
        int k1 = ao_loc[ksh+1] - kp0;
        int ko, kikj, i, j, k, n, ij, ic;
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dij = di * dj;
        int KK = nkpts * nkpts;
        int KKdij = KK * dij;
        size_t ijoff = (size_t)ip0 * (ip0 + 1) / 2;
        size_t nao2 = (size_t)ip1 * (ip1 + 1) / 2 - ijoff;
        size_t naok = kp1 - kp0;
        size_t nao3 = nao2 * naok;
        size_t n3c = nao3 * comp;
        size_t off;
        double *pbufR, *pbufI;

        if (i0 > j0) {
                for (ic = 0; ic < comp; ic++) {
                        for (n = 0, j = j0; j < j1; j++) {
                        for (i = i0; i < i1; i++, n++) {
                                ij = i * (i + 1) / 2 + j - ijoff;
                                pbufR = bufkkR + n * KK;
                                pbufI = bufkkI + n * KK;
                                for (ko = 0; ko < kpt_ij_size; ko++) {
                                        kikj = kpt_ij_idx[ko];
                                        off = n3c * ko + ij * naok + k0;
                                        for (k = 0; k < dk; k++) {
                                                outR[off+k] = pbufR[k*KKdij+kikj];
                                                outI[off+k] = pbufI[k*KKdij+kikj];
                                        }
                                }
                        } }
                        outR += nao3;
                        outI += nao3;
                        bufkkR += KKdij * dk;
                        bufkkI += KKdij * dk;
                }
        } else {
                for (ic = 0; ic < comp; ic++) {
                        for (i = i0; i < i1; i++) {
                        for (j = j0; j <= i; j++) {
                                ij = i * (i + 1) / 2 + j - ijoff;
                                pbufR = bufkkR + ((j - j0) * di + i - i0) * KK;
                                pbufI = bufkkI + ((j - j0) * di + i - i0) * KK;
                                for (ko = 0; ko < kpt_ij_size; ko++) {
                                        kikj = kpt_ij_idx[ko];
                                        off = n3c * ko + ij * naok + k0;
                                        for (k = 0; k < dk; k++) {
                                                outR[off+k] = pbufR[k*KKdij+kikj];
                                                outI[off+k] = pbufI[k*KKdij+kikj];
                                        }
                                }
                        } }
                        outR += nao3;
                        outI += nao3;
                        bufkkR += KKdij * dk;
                        bufkkI += KKdij * dk;
                }
        }
}

static void _fill_kk(int (*intor)(), FPtrSort fsort,
                     double *outR, double *outI, double *cache, int *cell0_shls,
                     CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        char TRANS_N = 'N';
        char TRANS_T = 'T';
        double D0 = 0;
        double D1 = 1;
        double ND1 = -1;

        int bvk_ncells = envs_bvk->ncells;
        int nkpts = envs_bvk->nkpts;
        int *cell0_ao_loc = envs_bvk->ao_loc;
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        int ksh_cell0 = cell0_shls[2];
        int di = cell0_ao_loc[ish_cell0+1] -  cell0_ao_loc[ish_cell0];
        int dj = cell0_ao_loc[jsh_cell0+1] -  cell0_ao_loc[jsh_cell0];
        int dk = cell0_ao_loc[ksh_cell0+1] -  cell0_ao_loc[ksh_cell0];
        int dij = di * dj;
        int ncomp = envs_bvk->ncomp;
        int d3c = dij * dk * ncomp;
        int d3cL = d3c * bvk_ncells;
        int d3ck = d3c * nkpts;
        double *bufkLR = cache;
        double *bufkLI = bufkLR + (size_t)d3cL * nkpts;
        double *bufkkR = bufkLI + (size_t)d3cL * nkpts;
        double *bufkkI = bufkkR + (size_t)d3c * nkpts * nkpts;
        double *bufL = bufkkR;
        double *pbuf = bufL;
        int iL, jL;
        int bvk_cells[2];

        int iLmax = -1;
        int jLmax = -1;
        for (iL = 0; iL < bvk_ncells; iL++) {
        for (jL = 0; jL < bvk_ncells; jL++) {
                bvk_cells[0] = iL;
                bvk_cells[1] = jL;
                if ((*intor)(pbuf, cell0_shls, bvk_cells, envs_bvk->cutoff,
                             envs_cint, envs_bvk)) {
                        iLmax = iL;
                        jLmax = MAX(jL, jLmax);
                }
                pbuf += d3c;
        } }

        int nLi = iLmax + 1;
        int nLj = jLmax + 1;
        double *expLkR = envs_bvk->expLkR;
        double *expLkI = envs_bvk->expLkI;

        if (jLmax >= 0) {  // ensure j3c buf is not empty
                dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3cL, &nLi,
                       &D1, expLkR, &nkpts, bufL, &d3cL,
                       &D0, bufkLR, &nkpts);
                // conj(exp(1j*dot(h,k)))
                dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3cL, &nLi,
                       &ND1, expLkI, &nkpts, bufL, &d3cL,
                       &D0, bufkLI, &nkpts);

                dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3ck, &nLj,
                       &D1, expLkR, &nkpts, bufkLR, &d3ck,
                       &D0, bufkkR, &nkpts);
                dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3ck, &nLj,
                       &ND1, expLkI, &nkpts, bufkLI, &d3ck,
                       &D1, bufkkR, &nkpts);
                dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3ck, &nLj,
                       &D1, expLkR, &nkpts, bufkLI, &d3ck,
                       &D0, bufkkI, &nkpts);
                dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3ck, &nLj,
                       &D1, expLkI, &nkpts, bufkLR, &d3ck,
                       &D1, bufkkI, &nkpts);

                (*fsort)(outR, outI, bufkkR, bufkkI, cell0_shls, cell0_ao_loc, envs_bvk);
        }
}

void PBCfill_nr3c_kks1(int (*intor)(), double *outR, double *outI, double *cache,
                       int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        _fill_kk(intor, _sort_kks1, outR, outI, cache, cell0_shls, envs_cint, envs_bvk);
}

void PBCfill_nr3c_kks2(int (*intor)(), double *outR, double *outI, double *cache,
                       int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        if (ish_cell0 < jsh_cell0) {
                return;
        }
        _fill_kk(intor, _sort_kks2, outR, outI, cache, cell0_shls, envs_cint, envs_bvk);
}

// [kI, i, j, k, comp] in Fortran order => [kI, comp, i, j, k] in C order
static void _sort_ks1(double *outR, double *outI, double *bufkR, double *bufkI,
                      int *shls, int *ao_loc, BVKEnvs *envs_bvk)
{
        int *shls_slice = envs_bvk->shls_slice;
        int nkpts = envs_bvk->nkpts;
        int comp = envs_bvk->ncomp;
        int ish = shls[0];
        int jsh = shls[1];
        int ksh = shls[2];
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int jsh0 = shls_slice[2];
        int jsh1 = shls_slice[3];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int ip0 = ao_loc[ish0];
        int ip1 = ao_loc[ish1];
        int jp0 = ao_loc[jsh0];
        int jp1 = ao_loc[jsh1];
        int kp0 = ao_loc[ksh0];
        int kp1 = ao_loc[ksh1];
        int i0 = ao_loc[ish] - ip0;
        int j0 = ao_loc[jsh] - jp0;
        int k0 = ao_loc[ksh] - kp0;
        int i1 = ao_loc[ish+1] - ip0;
        int j1 = ao_loc[jsh+1] - jp0;
        int k1 = ao_loc[ksh+1] - kp0;
        int ki, i, j, k, n, ij, ic;
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dij = di * dj;
        int Kdij = nkpts * dij;
        size_t naoi = ip1 - ip0;
        size_t naoj = jp1 - jp0;
        size_t naok = kp1 - kp0;
        size_t nao2 = naoi * naoj;
        size_t nao3 = nao2 * naok;
        size_t n3c = nao3 * comp;
        size_t off;
        double *pbufR, *pbufI;

        for (ic = 0; ic < comp; ic++) {
                for (n = 0, j = j0; j < j1; j++) {
                for (i = i0; i < i1; i++, n++) {
                        ij = i * naoj + j;
                        pbufR = bufkR + n * nkpts;
                        pbufI = bufkI + n * nkpts;
                        for (ki = 0; ki < nkpts; ki++) {
                                off = n3c * ki + ij * naok + k0;
                                for (k = 0; k < dk; k++) {
                                        outR[off+k] = pbufR[k*Kdij+ki];
                                        outI[off+k] = pbufI[k*Kdij+ki];
                                }
                        }
                } }
                outR += nao3;
                outI += nao3;
                bufkR += Kdij * dk;
                bufkI += Kdij * dk;
        }
}

// [kI, i, j, k, comp] in Fortran order => [kI, comp, i>=j, k] in C order
static void _sort_ks2(double *outR, double *outI, double *bufkR, double *bufkI,
                      int *shls, int *ao_loc, BVKEnvs *envs_bvk)
{
        int *shls_slice = envs_bvk->shls_slice;
        int nkpts = envs_bvk->nkpts;
        int comp = envs_bvk->ncomp;
        int ish = shls[0];
        int jsh = shls[1];
        int ksh = shls[2];
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int ip0 = ao_loc[ish0];
        int ip1 = ao_loc[ish1];
        int kp0 = ao_loc[ksh0];
        int kp1 = ao_loc[ksh1];
        assert(ip1 == jp1 && jp0 == 0);
        int i0 = ao_loc[ish];
        int j0 = ao_loc[jsh];
        int k0 = ao_loc[ksh] - kp0;
        int i1 = ao_loc[ish+1];
        int j1 = ao_loc[jsh+1];
        int k1 = ao_loc[ksh+1] - kp0;
        int ki, i, j, k, n, ij, ic;
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dij = di * dj;
        int Kdij = nkpts * dij;
        size_t ijoff = (size_t)ip0 * (ip0 + 1) / 2;
        size_t nao2 = (size_t)ip1 * (ip1 + 1) / 2 - ijoff;
        size_t naok = kp1 - kp0;
        size_t nao3 = nao2 * naok;
        size_t n3c = nao3 * comp;
        size_t off;
        double *pbufR, *pbufI;

        if (i0 > j0) {
                for (ic = 0; ic < comp; ic++) {
                        for (n = 0, j = j0; j < j1; j++) {
                        for (i = i0; i < i1; i++, n++) {
                                ij = i * (i + 1) / 2 + j - ijoff;
                                pbufR = bufkR + n * nkpts;
                                pbufI = bufkI + n * nkpts;
                                for (ki = 0; ki < nkpts; ki++) {
                                        off = n3c * ki + ij * naok + k0;
                                        for (k = 0; k < dk; k++) {
                                                outR[off+k] = pbufR[k*Kdij+ki];
                                                outI[off+k] = pbufI[k*Kdij+ki];
                                        }
                                }
                        } }
                        outR += nao3;
                        outI += nao3;
                        bufkR += Kdij * dk;
                        bufkI += Kdij * dk;
                }
        } else {
                for (ic = 0; ic < comp; ic++) {
                        for (i = i0; i < i1; i++) {
                        for (j = j0; j <= i; j++) {
                                ij = i * (i + 1) / 2 + j - ijoff;
                                pbufR = bufkR + ((j - j0) * di + i - i0) * nkpts;
                                pbufI = bufkI + ((j - j0) * di + i - i0) * nkpts;
                                for (ki = 0; ki < nkpts; ki++) {
                                        off = n3c * ki + ij * naok + k0;
                                        for (k = 0; k < dk; k++) {
                                                outR[off+k] = pbufR[k*Kdij+ki];
                                                outI[off+k] = pbufI[k*Kdij+ki];
                                        }
                                }
                        } }
                        outR += nao3;
                        outI += nao3;
                        bufkR += Kdij * dk;
                        bufkI += Kdij * dk;
                }
        }
}

static void _fill_k(int (*intor)(), FPtrSort fsort,
                    double *outR, double *outI, double *cache,
                    int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        char TRANS_N = 'N';
        char TRANS_T = 'T';
        double D0 = 0;
        double D1 = 1;

        int bvk_ncells = envs_bvk->ncells;
        int nkpts = envs_bvk->nkpts;
        int *cell0_ao_loc = envs_bvk->ao_loc;
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        int ksh_cell0 = cell0_shls[2];
        int di = cell0_ao_loc[ish_cell0+1] -  cell0_ao_loc[ish_cell0];
        int dj = cell0_ao_loc[jsh_cell0+1] -  cell0_ao_loc[jsh_cell0];
        int dk = cell0_ao_loc[ksh_cell0+1] -  cell0_ao_loc[ksh_cell0];
        int dij = di * dj;
        int ncomp = envs_bvk->ncomp;
        int d3c = dij * dk * ncomp;
        int d3cL = d3c * bvk_ncells;
        int d3ck = d3c * nkpts;
        double *bufkR = cache;
        double *bufkI = bufkR + d3ck;
        double *bufL = bufkI + d3ck;
        double *bufLkR = bufL + d3cL;
        double *bufLkI = bufLkR + d3ck;
        double *pbuf;
        int iL, jL, jLmax, nLj, i, k;
        int bvk_cells[2];
        double *expLkR = envs_bvk->expLkR;
        double *expLkI = envs_bvk->expLkI;

        int empty = 1;
        NPdset0(bufkR, d3ck);
        NPdset0(bufkI, d3ck);

        for (iL = 0; iL < bvk_ncells; iL++) {
                jLmax = -1;
                pbuf = bufL;
                for (jL = 0; jL < bvk_ncells; jL++) {
                        bvk_cells[0] = iL;
                        bvk_cells[1] = jL;
                        if ((*intor)(pbuf, cell0_shls, bvk_cells, envs_bvk->cutoff,
                                     envs_cint, envs_bvk)) {
                                jLmax = MAX(jL, jLmax);
                        }
                        pbuf += d3c;
                }
                if (jLmax >= 0) {  // ensure j3c buf is not empty
                        nLj = jLmax + 1;
                        dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3c, &nLj,
                               &D1, expLkR, &nkpts, bufL, &d3c,
                               &D0, bufLkR, &nkpts);
                        dgemm_(&TRANS_N, &TRANS_T, &nkpts, &d3c, &nLj,
                               &D1, expLkI, &nkpts, bufL, &d3c,
                               &D0, bufLkI, &nkpts);

                        for (i = 0; i < d3c; i++) {
                        for (k = 0; k < nkpts; k++) {
                                bufkR[i*nkpts+k] += bufLkR[i*nkpts+k] * expLkR[iL*nkpts+k]
                                                  + bufLkI[i*nkpts+k] * expLkI[iL*nkpts+k];
                                bufkI[i*nkpts+k] += bufLkI[i*nkpts+k] * expLkR[iL*nkpts+k]
                                                  - bufLkR[i*nkpts+k] * expLkI[iL*nkpts+k];
                        } }
                        empty = 0;
                }
        }
        if (!empty) {
                (*fsort)(outR, outI, bufkR, bufkI, cell0_shls, cell0_ao_loc, envs_bvk);
        }
}

void PBCfill_nr3c_ks1(int (*intor)(), double *outR, double *outI, double *cache,
                      int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        _fill_k(intor, _sort_ks1, outR, outI, cache, cell0_shls, envs_cint, envs_bvk);
}

void PBCfill_nr3c_ks2(int (*intor)(), double *outR, double *outI, double *cache,
                      int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        if (ish_cell0 < jsh_cell0) {
                return;
        }
        _fill_k(intor, _sort_ks2, outR, outI, cache, cell0_shls, envs_cint, envs_bvk);
}

// [i, j, k, comp] in Fortran order => [comp, i, j, k] in C order
// Note: just need to copy real part. imaginary part may be NULL pointer
static void _sort_gs1(double *outR, double *outI, double *bufR, double *bufI,
                      int *shls, int *ao_loc, BVKEnvs *envs_bvk)
{
        int *shls_slice = envs_bvk->shls_slice;
        int comp = envs_bvk->ncomp;
        int ish = shls[0];
        int jsh = shls[1];
        int ksh = shls[2];
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int jsh0 = shls_slice[2];
        int jsh1 = shls_slice[3];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int ip0 = ao_loc[ish0];
        int ip1 = ao_loc[ish1];
        int jp0 = ao_loc[jsh0];
        int jp1 = ao_loc[jsh1];
        int kp0 = ao_loc[ksh0];
        int kp1 = ao_loc[ksh1];
        int i0 = ao_loc[ish] - ip0;
        int j0 = ao_loc[jsh] - jp0;
        int k0 = ao_loc[ksh] - kp0;
        int i1 = ao_loc[ish+1] - ip0;
        int j1 = ao_loc[jsh+1] - jp0;
        int k1 = ao_loc[ksh+1] - kp0;
        int i, j, k, n, ic;
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dij = di * dj;
        int dijk = dij * dk;
        size_t naoi = ip1 - ip0;
        size_t naoj = jp1 - jp0;
        size_t naok = kp1 - kp0;
        size_t nao2 = naoi * naoj;
        size_t nao3 = nao2 * naok;
        size_t off;

        for (ic = 0; ic < comp; ic++) {
                for (n = 0, j = j0; j < j1; j++) {
                for (i = i0; i < i1; i++, n++) {
                        off = (i * naoj + j) * naok + k0;
                        for (k = 0; k < dk; k++) {
                                outR[off+k] = bufR[n+k*dij];
                        }
                } }
                outR += nao3;
                bufR += dijk;
        }
}

// [i, j, k, comp] in Fortran order => [comp, i>=j, k] in C order
// Note: just need to copy real part. imaginary part may be NULL pointer
static void _sort_gs2(double *outR, double *outI, double *bufR, double *bufI,
                      int *shls, int *ao_loc, BVKEnvs *envs_bvk)
{
        int *shls_slice = envs_bvk->shls_slice;
        int comp = envs_bvk->ncomp;
        int ish = shls[0];
        int jsh = shls[1];
        int ksh = shls[2];
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int ip0 = ao_loc[ish0];
        int ip1 = ao_loc[ish1];
        int kp0 = ao_loc[ksh0];
        int kp1 = ao_loc[ksh1];
        assert(ip1 == jp1 && jp0 == 0);
        int i0 = ao_loc[ish];
        int j0 = ao_loc[jsh];
        int k0 = ao_loc[ksh] - kp0;
        int i1 = ao_loc[ish+1];
        int j1 = ao_loc[jsh+1];
        int k1 = ao_loc[ksh+1] - kp0;
        int i, j, k, n, ic;
        int di = i1 - i0;
        int dj = j1 - j0;
        int dk = k1 - k0;
        int dij = di * dj;
        int dijk = dij * dk;
        size_t ijoff = (size_t)ip0 * (ip0 + 1) / 2;
        size_t nao2 = (size_t)ip1 * (ip1 + 1) / 2 - ijoff;
        size_t naok = kp1 - kp0;
        size_t nao3 = nao2 * naok;
        size_t off;

        if (i0 > j0) {
                for (ic = 0; ic < comp; ic++) {
                        for (n = 0, j = j0; j < j1; j++) {
                        for (i = i0; i < i1; i++, n++) {
                                off = (i * (i + 1) / 2 + j - ijoff) * naok + k0;
                                for (k = 0; k < dk; k++) {
                                        outR[off+k] = bufR[k*dij+n];
                                }
                        } }
                        outR += nao3;
                        bufR += dijk;
                }
        } else {
                for (ic = 0; ic < comp; ic++) {
                        for (i = i0; i < i1; i++) {
                        for (j = j0; j <= i; j++) {
                                n = ((j - j0) * di + i - i0);
                                off = (i * (i + 1) / 2 + j - ijoff) * naok + k0;
                                for (k = 0; k < dk; k++) {
                                        outR[off+k] = bufR[k*dij+n];
                                }
                        } }
                        outR += nao3;
                        bufR += dijk;
                }
        }
}

static void _fill_nk1(int (*intor)(), FPtrSort fsort,
                      double *outR, double *outI, double *cache,
                      int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int bvk_ncells = envs_bvk->ncells;
        int *cell0_ao_loc = envs_bvk->ao_loc;
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        int ksh_cell0 = cell0_shls[2];
        int di = cell0_ao_loc[ish_cell0+1] -  cell0_ao_loc[ish_cell0];
        int dj = cell0_ao_loc[jsh_cell0+1] -  cell0_ao_loc[jsh_cell0];
        int dk = cell0_ao_loc[ksh_cell0+1] -  cell0_ao_loc[ksh_cell0];
        int dij = di * dj;
        int ncomp = envs_bvk->ncomp;
        int d3c = dij * dk * ncomp;
        double *bufR = cache;
        double *bufI = bufR + d3c;
        double *bufL = bufI + d3c;
        int iL, jL, n;
        int bvk_cells[2];
        double *expLkR = envs_bvk->expLkR;
        double *expLkI = envs_bvk->expLkI;
        double facR, facI;

        int empty = 1;
        NPdset0(bufR, d3c);
        NPdset0(bufI, d3c);
        for (iL = 0; iL < bvk_ncells; iL++) {
        for (jL = 0; jL < bvk_ncells; jL++) {
                bvk_cells[0] = iL;
                bvk_cells[1] = jL;
                if ((*intor)(bufL, cell0_shls, bvk_cells, envs_bvk->cutoff,
                             envs_cint, envs_bvk)) {
                        empty = 0;
                        facR = expLkR[iL] * expLkR[jL] + expLkI[iL] * expLkI[jL];
                        facI = expLkR[iL] * expLkI[jL] - expLkI[iL] * expLkR[jL];
                        for (n = 0; n < d3c; n++) {
                                bufR[n] += bufL[n] * facR;
                                bufI[n] += bufL[n] * facI;
                        }
                }
        } }

        if (!empty) {
                (*fsort)(outR, NULL, bufR, NULL, cell0_shls, cell0_ao_loc, envs_bvk);
                (*fsort)(outI, NULL, bufI, NULL, cell0_shls, cell0_ao_loc, envs_bvk);
        }
}

void PBCfill_nr3c_nk1s1(int (*intor)(), double *outR, double *outI, double *cache,
                        int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        _fill_nk1(intor, _sort_gs1, outR, outI, cache, cell0_shls, envs_cint, envs_bvk);
}

void PBCfill_nr3c_nk1s2(int (*intor)(), double *outR, double *outI, double *cache,
                        int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        if (ish_cell0 < jsh_cell0) {
                return;
        }
        _fill_nk1(intor, _sort_gs2, outR, outI, cache, cell0_shls, envs_cint, envs_bvk);
}


static void _fill_g(int (*intor)(), FPtrSort fsort,
                    double *outR, double *outI, double *cache,
                    int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int *cell0_ao_loc = envs_bvk->ao_loc;
        double *buf = cache;
        int bvk_cells[2] = {0, 0};

        if ((*intor)(buf, cell0_shls, bvk_cells, envs_bvk->cutoff,
                     envs_cint, envs_bvk)) {
                (*fsort)(outR, NULL, buf, NULL, cell0_shls, cell0_ao_loc, envs_bvk);
        }
}

void PBCfill_nr3c_gs1(int (*intor)(), double *outR, double *outI, double *cache,
                      int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        _fill_g(intor, _sort_gs1, outR, NULL, cache, cell0_shls, envs_cint, envs_bvk);
}

void PBCfill_nr3c_gs2(int (*intor)(), double *outR, double *outI, double *cache,
                      int *cell0_shls, CINTEnvVars *envs_cint, BVKEnvs *envs_bvk)
{
        int ish_cell0 = cell0_shls[0];
        int jsh_cell0 = cell0_shls[1];
        if (ish_cell0 < jsh_cell0) {
                return;
        }
        _fill_g(intor, _sort_gs2, outR, NULL, cache, cell0_shls, envs_cint, envs_bvk);
}

void PBCfill_nr3c_drv(int (*intor)(), FPtrFill fill, int is_pbcintor,
                      double *eriR, double *eriI, double *expLkR, double *expLkI,
                      int *kpt_ij_idx, int kpt_ij_size, int bvk_ncells, int nimgs,
                      int nkpts, int nbasp, int comp,
                      int *sh_loc, int *cell0_ao_loc, int *shls_slice,
                      int8_t *ovlp_mask, int8_t *cell0_ovlp_mask, int *bas_map,
                      double *q_cond, double cutoff, CINTOpt *cintopt, int cache_size,
                      int *atm, int natm, int *bas, int nbas, double *env)
{
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int jsh0 = shls_slice[2];
        int jsh1 = shls_slice[3];
        int ksh0 = shls_slice[4];
        int ksh1 = shls_slice[5];
        int nish = ish1 - ish0;
        int njsh = jsh1 - jsh0;
        int nksh = ksh1 - ksh0;
        size_t nij = nish * njsh;
        size_t nijk = nij * nksh;
        // int di = GTOmax_shell_dim(bvk_ao_loc, shls_slice, 1);

        BVKEnvs envs_bvk = {bvk_ncells, nimgs,
                nkpts, nkpts, nbasp, comp, 0, kpt_ij_size,
                sh_loc, cell0_ao_loc, bas_map, shls_slice, kpt_ij_idx,
                expLkR, expLkI, ovlp_mask, q_cond, cutoff};

        // if intor is a regular molecular integral function, calling the
        // general assemble3c function
        if (!is_pbcintor) {
                envs_bvk.intor = intor;
                intor = &_assemble3c;
        }

#pragma omp parallel
{
        CINTEnvVars envs_cint;
        PBCminimal_CINTEnvVars(&envs_cint, atm, natm, bas, nbas, env, cintopt);

        size_t n, ij;
        int ish, jsh, ksh;
        int cell0_shls[3];
        double *cache = malloc(sizeof(double) * cache_size);
#pragma omp for schedule(dynamic)
        for (n = 0; n < nijk; n++) {
                ksh = n / nij + ksh0;
                ij = n % nij;
                ish = ij / njsh + ish0;
                jsh = ij % njsh + jsh0;
                if (!cell0_ovlp_mask[ish*nbasp+jsh]) {
                        continue;
                }
                cell0_shls[0] = ish;
                cell0_shls[1] = jsh;
                cell0_shls[2] = ksh;
                (*fill)(intor, eriR, eriI, cache, cell0_shls, &envs_cint, &envs_bvk);
        }
        free(cache);
}
}

void PBC_nr3c_q_cond(int (*intor)(), CINTOpt *cintopt,
                     double *q_cond, int *shls_slice, int *ao_loc,
                     int *atm, int natm, int *bas, int nbas, double *env)
{
        int ish0 = shls_slice[0];
        int ish1 = shls_slice[1];
        int ksh0 = shls_slice[2];
        int ksh1 = shls_slice[3];
        int nish = ish1 - ish0;
        int nksh = ksh1 - ksh0;
        const int cache_size = GTOmax_cache_size(intor, shls_slice, 2,
                                                 atm, natm, bas, nbas, env);
#pragma omp parallel
{
        double qtmp, tmp;
        size_t ik, i, k, di, dk;
        int ish, ksh;
        int shls[3];
        di = 0;
        for (ish = MIN(ish0, ksh0); ish < MAX(ish1, ksh1); ish++) {
                dk = ao_loc[ish+1] - ao_loc[ish];
                di = MAX(di, dk);
        }
        double *buf = malloc(sizeof(double) * (di*di*di + cache_size));
        double *cache = buf + di * di * di;
#pragma omp for schedule(dynamic, 4)
        for (ik = 0; ik < nish*nksh; ik++) {
                ksh = ik / nish + ksh0;
                ish = ik % nish;
                di = ao_loc[ish+1] - ao_loc[ish];
                dk = ao_loc[ksh+1] - ao_loc[ksh];
                shls[0] = ish;
                shls[1] = ish;
                shls[2] = ksh;
                qtmp = 1e-200;
                if (0 != (*intor)(buf, NULL, shls, atm, natm, bas, nbas, env,
                                  cintopt, cache)) {
                        for (k = 0; k < dk; k++) {
                        for (i = 0; i < di; i++) {
                                tmp = fabs(buf[i+di*i+di*di*k]);
                                qtmp = MAX(qtmp, tmp);
                        } }
                        qtmp = sqrt(qtmp);
                }
                q_cond[ik] = qtmp;
        }
        free(buf);
}
}

void PBCnr3c_fuse_dd_s1(double *j3c, double *j3c_dd,
                        int *ao_idx, int *orig_slice, int *dd_slice,
                        int nao, int naod, int naux)
{
        size_t Naux = naux;
        int ip0 = orig_slice[0];
        int jp0 = orig_slice[2];
        int i0 = dd_slice[0];
        int i1 = dd_slice[1];
        int j0 = dd_slice[2];
        int j1 = dd_slice[3];
        int off_o = ip0 * nao + jp0;
        int off_i = i0 * naod + j0;
        int i, j, ip, jp, n;
        double *pj3c, *pj3c_dd;
        for (i = i0; i < i1; i++) {
        for (j = j0; j < j1; j++) {
                ip = ao_idx[i];
                jp = ao_idx[j];
                pj3c = j3c + Naux * (ip * nao + jp - off_o);
                pj3c_dd = j3c_dd + Naux * (i * naod + j - off_i);
                for (n = 0; n < naux; n++) {
                        pj3c[n] += pj3c_dd[n];
                }
        } }
}

void PBCnr3c_fuse_dd_s2(double *j3c, double *j3c_dd,
                        int *ao_idx, int *orig_slice, int *dd_slice,
                        int nao, int naod, int naux)
{
        size_t Naux = naux;
        int ip0 = orig_slice[0];
        int jp0 = orig_slice[2];
        int i0 = dd_slice[0];
        int j0 = dd_slice[2];
        int i1 = dd_slice[1];
        int off_o = ip0 * (ip0 + 1) / 2 + jp0;
        int off_i = i0 * naod + j0;
        int i, j, ip, jp, n;
        double *pj3c, *pj3c_dd;
        for (i = i0; i < i1; i++) {
        for (j = 0; j <= i; j++) {
                ip = ao_idx[i];
                jp = ao_idx[j];
                pj3c = j3c + Naux * (ip * (ip + 1) / 2 + jp - off_o);
                pj3c_dd = j3c_dd + Naux * (i * naod + j - off_i);
                for (n = 0; n < naux; n++) {
                        pj3c[n] += pj3c_dd[n];
                }
        } }
}


static int shloc_partition(int *kshloc, int *ao_loc, int ksh0, int ksh1, int dkmax)
{
        int ksh;
        int nloc = 0;
        int loclast = ao_loc[ksh0];
        kshloc[0] = ksh0;
        for (ksh = ksh0+1; ksh < ksh1; ksh++) {
                assert(ao_loc[ksh+1] - ao_loc[ksh] < dkmax);
                if (ao_loc[ksh+1] - loclast > dkmax) {
                        nloc += 1;
                        kshloc[nloc] = ksh;
                        loclast = ao_loc[ksh];
                }
        }
        nloc += 1;
        kshloc[nloc] = ksh1;
        return nloc;
}

static void shift_bas(double *env_loc, double *env, double *Ls, int ptr, int iL)
{
        env_loc[ptr+0] = env[ptr+0] + Ls[iL*3+0];
        env_loc[ptr+1] = env[ptr+1] + Ls[iL*3+1];
        env_loc[ptr+2] = env[ptr+2] + Ls[iL*3+2];
}

static void sort2c_ks1(double complex *out, double *bufr, double *bufi,
                       int *shls_slice, int *ao_loc, int nkpts, int comp,
                       int jsh, int msh0, int msh1)
{
        const int ish0 = shls_slice[0];
        const int ish1 = shls_slice[1];
        const int jsh0 = shls_slice[2];
        const int jsh1 = shls_slice[3];
        const size_t naoi = ao_loc[ish1] - ao_loc[ish0];
        const size_t naoj = ao_loc[jsh1] - ao_loc[jsh0];
        const size_t nij = naoi * naoj;

        const int dj = ao_loc[jsh+1] - ao_loc[jsh];
        const int jp = ao_loc[jsh] - ao_loc[jsh0];
        const int dimax = ao_loc[msh1] - ao_loc[msh0];
        const size_t dmjc = dimax * dj * comp;
        out += jp;

        int i, j, kk, ish, ic, di, dij;
        size_t off;
        double *pbr, *pbi;
        double complex *pout;

        for (kk = 0; kk < nkpts; kk++) {
                off = kk * dmjc;
                for (ish = msh0; ish < msh1; ish++) {
                        di = ao_loc[ish+1] - ao_loc[ish];
                        dij = di * dj;
                        for (ic = 0; ic < comp; ic++) {
                                pout = out + nij*ic + naoj*(ao_loc[ish]-ao_loc[ish0]);
                                pbr = bufr + off + dij*ic;
                                pbi = bufi + off + dij*ic;
        for (j = 0; j < dj; j++) {
                for (i = 0; i < di; i++) {
                        pout[i*naoj+j] = pbr[j*di+i] + pbi[j*di+i]*_Complex_I;
                }
        }
                        }
                        off += dij * comp;
                }
                out += nij * comp;
        }
}
static int _nr2c_fill(int (*intor)(), double complex *out,
                      int nkpts, int comp, int nimgs, int jsh, int ish0,
                      double *buf, double *env_loc, double *Ls,
                      double *expkL_r, double *expkL_i,
                      int *shls_slice, int *ao_loc, CINTOpt *cintopt,
                      int *atm, int natm, int *bas, int nbas, double *env)
{
        const int ish1 = shls_slice[1];
        const int jsh0 = shls_slice[2];

        const char TRANS_N = 'N';
        const double D1 = 1;
        const double D0 = 0;

        ish0 += shls_slice[0];
        jsh += jsh0;
        int jptrxyz = atm[PTR_COORD+bas[ATOM_OF+jsh*BAS_SLOTS]*ATM_SLOTS];
        const int dj = ao_loc[jsh+1] - ao_loc[jsh];
        int dimax = INTBUFMAX10 / dj;
        int ishloc[ish1-ish0+1];
        int nishloc = shloc_partition(ishloc, ao_loc, ish0, ish1, dimax);

        int m, msh0, msh1, dmjc, ish, di, empty;
        int jL;
        int shls[2];
        double *bufk_r = buf;
        double *bufk_i, *bufL, *pbuf, *cache;

        shls[1] = jsh;
        for (m = 0; m < nishloc; m++) {
                msh0 = ishloc[m];
                msh1 = ishloc[m+1];
                dimax = ao_loc[msh1] - ao_loc[msh0];
                dmjc = dj * dimax * comp;
                bufk_i = bufk_r + dmjc * nkpts;
                bufL   = bufk_i + dmjc * nkpts;
                cache  = bufL   + dmjc * nimgs;

                pbuf = bufL;
                for (jL = 0; jL < nimgs; jL++) {
                        shift_bas(env_loc, env, Ls, jptrxyz, jL);
                        for (ish = msh0; ish < msh1; ish++) {
                                shls[0] = ish;
                                di = ao_loc[ish+1] - ao_loc[ish];
                                if ((*intor)(pbuf, NULL, shls, atm, natm, bas, nbas,
                                             env_loc, cintopt, cache)) {
                                        empty = 0;
                                }
                                pbuf += di * dj * comp;
                        }
                }
                dgemm_(&TRANS_N, &TRANS_N, &dmjc, &nkpts, &nimgs,
                       &D1, bufL, &dmjc, expkL_r, &nimgs, &D0, bufk_r, &dmjc);
                dgemm_(&TRANS_N, &TRANS_N, &dmjc, &nkpts, &nimgs,
                       &D1, bufL, &dmjc, expkL_i, &nimgs, &D0, bufk_i, &dmjc);

                sort2c_ks1(out, bufk_r, bufk_i, shls_slice, ao_loc,
                           nkpts, comp, jsh, msh0, msh1);
        }
        return !empty;
}

/* ('...M,kL->...k', int3c, exp_kL, exp_kL) */
void PBCnr2c_fill_ks1(int (*intor)(), double complex *out,
                      int nkpts, int comp, int nimgs, int jsh,
                      double *buf, double *env_loc, double *Ls,
                      double *expkL_r, double *expkL_i,
                      int *shls_slice, int *ao_loc, CINTOpt *cintopt,
                      int *atm, int natm, int *bas, int nbas, double *env)
{
        _nr2c_fill(intor, out, nkpts, comp, nimgs, jsh, 0,
                   buf, env_loc, Ls, expkL_r, expkL_i, shls_slice, ao_loc,
                   cintopt, atm, natm, bas, nbas, env);
}

void PBCnr2c_fill_ks2(int (*intor)(), double complex *out,
                      int nkpts, int comp, int nimgs, int jsh,
                      double *buf, double *env_loc, double *Ls,
                      double *expkL_r, double *expkL_i,
                      int *shls_slice, int *ao_loc, CINTOpt *cintopt,
                      int *atm, int natm, int *bas, int nbas, double *env)
{
        _nr2c_fill(intor, out, nkpts, comp, nimgs, jsh, jsh,
                   buf, env_loc, Ls, expkL_r, expkL_i, shls_slice, ao_loc,
                   cintopt, atm, natm, bas, nbas, env);
}

void PBCnr2c_drv(int (*intor)(), void (*fill)(), double complex *out,
                 int nkpts, int comp, int nimgs,
                 double *Ls, double complex *expkL,
                 int *shls_slice, int *ao_loc, CINTOpt *cintopt,
                 int *atm, int natm, int *bas, int nbas, double *env, int nenv)
{
        const int jsh0 = shls_slice[2];
        const int jsh1 = shls_slice[3];
        const int njsh = jsh1 - jsh0;
        double *expkL_r = malloc(sizeof(double) * nimgs*nkpts * OF_CMPLX);
        double *expkL_i = expkL_r + nimgs*nkpts;
        int i;
        for (i = 0; i < nimgs*nkpts; i++) {
                expkL_r[i] = creal(expkL[i]);
                expkL_i[i] = cimag(expkL[i]);
        }
        const int cache_size = GTOmax_cache_size(intor, shls_slice, 2,
                                                 atm, natm, bas, nbas, env);

#pragma omp parallel
{
        int jsh;
        double *env_loc = malloc(sizeof(double)*nenv);
        NPdcopy(env_loc, env, nenv);
        size_t count = nkpts * OF_CMPLX + nimgs;
        double *buf = malloc(sizeof(double)*(count*INTBUFMAX10*comp+cache_size));
#pragma omp for schedule(dynamic)
        for (jsh = 0; jsh < njsh; jsh++) {
                (*fill)(intor, out, nkpts, comp, nimgs, jsh,
                        buf, env_loc, Ls, expkL_r, expkL_i,
                        shls_slice, ao_loc, cintopt, atm, natm, bas, nbas, env);
        }
        free(buf);
        free(env_loc);
}
        free(expkL_r);
}

/*
 * Put tril to the lower triangular part of out, conj(triu) to the upper
 * triangular part of out
 */
void PBCunpack_tril_triu(double complex *out, double complex *tril,
                         double complex *triu, int naux, int nao)
{
#pragma omp parallel
{
        int i, j, k, ij;
        size_t nao2 = nao * nao;
        size_t nao_pair = nao * (nao + 1) / 2;
        double complex *pout, *ptril, *ptriu;
#pragma omp for schedule(dynamic)
        for (k = 0; k < naux; k++) {
                pout = out + nao2 * k;
                ptril = tril + nao_pair * k;
                ptriu = triu + nao_pair * k;
                for (ij = 0, i = 0; i < nao; i++) {
                        for (j = 0; j < i; j++, ij++) {
                                pout[i*nao+j] = ptril[ij];
                                pout[j*nao+i] = conj(ptriu[ij]);
                        }
                        pout[i*nao+i] = ptril[ij];
                        ij++;
                }
        }
}
}
