#!/usr/bin/env python
# Copyright 2014-2020 The PySCF Developers. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Author: Xing Zhang <zhangxing.nju@gmail.com>
#

import numpy as np
from pyscf import __config__
from pyscf import lib
from pyscf.lib import logger
from pyscf.pbc.lib import kpts as libkpts
from pyscf.pbc.scf import khf_ksymm, kuhf
from pyscf.pbc.lib.kpts import KPoints

@lib.with_doc(kuhf.get_occ.__doc__)
def get_occ(mf, mo_energy_kpts=None, mo_coeff_kpts=None):
    if mo_energy_kpts is None: mo_energy_kpts = mf.mo_energy
    kpts = mf.kpts

    nocc_a, nocc_b = mf.nelec
    if isinstance(kpts, KPoints):
        mo_energy_kpts = kpts.transform_mo_energy(mo_energy_kpts)
    mo_energy = np.sort(np.hstack(mo_energy_kpts[0]))
    fermi_a = mo_energy[nocc_a-1]
    mo_occ_kpts = [[], []]
    for mo_e in mo_energy_kpts[0]:
        mo_occ_kpts[0].append((mo_e <= fermi_a).astype(np.double))
    if nocc_a < len(mo_energy):
        logger.info(mf, 'alpha HOMO = %.12g  LUMO = %.12g', fermi_a, mo_energy[nocc_a])
    else:
        logger.info(mf, 'alpha HOMO = %.12g  (no LUMO because of small basis) ', fermi_a)

    if nocc_b > 0:
        mo_energy = np.sort(np.hstack(mo_energy_kpts[1]))
        fermi_b = mo_energy[nocc_b-1]
        for mo_e in mo_energy_kpts[1]:
            mo_occ_kpts[1].append((mo_e <= fermi_b).astype(np.double))
        if nocc_b < len(mo_energy):
            logger.info(mf, 'beta HOMO = %.12g  LUMO = %.12g', fermi_b, mo_energy[nocc_b])
        else:
            logger.info(mf, 'beta HOMO = %.12g  (no LUMO because of small basis) ', fermi_b)
    else:
        for mo_e in mo_energy_kpts[1]:
            mo_occ_kpts[1].append(np.zeros_like(mo_e))

    if mf.verbose >= logger.DEBUG:
        np.set_printoptions(threshold=len(mo_energy))
        logger.debug(mf, '     k-point                  alpha mo_energy')
        for k,kpt in enumerate(mf.cell.get_scaled_kpts(kpts, kpts_in_ibz=False)):
            if (np.count_nonzero(mo_occ_kpts[0][k]) > 0 and
                np.count_nonzero(mo_occ_kpts[0][k] == 0) > 0):
                logger.debug(mf, '  %2d (%6.3f %6.3f %6.3f)   %s %s',
                             k, kpt[0], kpt[1], kpt[2],
                             mo_energy_kpts[0][k][mo_occ_kpts[0][k]> 0],
                             mo_energy_kpts[0][k][mo_occ_kpts[0][k]==0])
            else:
                logger.debug(mf, '  %2d (%6.3f %6.3f %6.3f)   %s',
                             k, kpt[0], kpt[1], kpt[2], mo_energy_kpts[0][k])
        logger.debug(mf, '     k-point                  beta  mo_energy')
        for k,kpt in enumerate(mf.cell.get_scaled_kpts(kpts, kpts_in_ibz=False)):
            if (np.count_nonzero(mo_occ_kpts[1][k]) > 0 and
                np.count_nonzero(mo_occ_kpts[1][k] == 0) > 0):
                logger.debug(mf, '  %2d (%6.3f %6.3f %6.3f)   %s %s',
                             k, kpt[0], kpt[1], kpt[2],
                             mo_energy_kpts[1][k][mo_occ_kpts[1][k]> 0],
                             mo_energy_kpts[1][k][mo_occ_kpts[1][k]==0])
            else:
                logger.debug(mf, '  %2d (%6.3f %6.3f %6.3f)   %s',
                             k, kpt[0], kpt[1], kpt[2], mo_energy_kpts[1][k])
        np.set_printoptions(threshold=1000)

    if isinstance(kpts, KPoints):
        mo_occ_kpts[0] = kpts.check_mo_occ_symmetry(mo_occ_kpts[0], tol=1e-4)
        mo_occ_kpts[1] = kpts.check_mo_occ_symmetry(mo_occ_kpts[1], tol=1e-4)
    return mo_occ_kpts

@lib.with_doc(kuhf.energy_elec.__doc__)
def energy_elec(mf, dm_kpts=None, h1e_kpts=None, vhf_kpts=None):
    if dm_kpts is None: dm_kpts = mf.make_rdm1()
    if h1e_kpts is None: h1e_kpts = mf.get_hcore()
    if vhf_kpts is None: vhf_kpts = mf.get_veff(mf.cell, dm_kpts)
    wtk = mf.kpts.weights_ibz

    e1 = np.einsum('k,kij,kji', wtk, dm_kpts[0], h1e_kpts)
    e1+= np.einsum('k,kij,kji', wtk, dm_kpts[1], h1e_kpts)
    e_coul = np.einsum('k,kij,kji', wtk, dm_kpts[0], vhf_kpts[0]) * 0.5
    e_coul+= np.einsum('k,kij,kji', wtk, dm_kpts[1], vhf_kpts[1]) * 0.5
    mf.scf_summary['e1'] = e1.real
    mf.scf_summary['e2'] = e_coul.real
    logger.debug(mf, 'E1 = %s  E_coul = %s', e1, e_coul)
    if kuhf.CHECK_COULOMB_IMAG and abs(e_coul.imag > mf.cell.precision*10):
        logger.warn(mf, "Coulomb energy has imaginary part %s. "
                    "Coulomb integrals (e-e, e-N) may not converge !",
                    e_coul.imag)
    return (e1+e_coul).real, e_coul.real

get_rho = khf_ksymm.get_rho


class KsymAdaptedKUHF(khf_ksymm.KsymAdaptedKSCF, kuhf.KUHF):
    """
    KUHF with k-point symmetry
    """
    def __init__(self, cell, kpts=libkpts.KPoints(),
                 exxdiv=getattr(__config__, 'pbc_scf_SCF_exxdiv', 'ewald')):
        self._kpts = None
        kuhf.KUHF.__init__(self, cell, kpts, exxdiv)

    @property
    def nelec(self):
        if self._nelec is not None:
            return self._nelec
        else:
            cell = self.cell
            nkpts = self.kpts.nkpts
            ne = cell.tot_electrons(nkpts)
            nalpha = (ne + cell.spin) // 2
            nbeta = nalpha - cell.spin
            if nalpha + nbeta != ne:
                raise RuntimeError('Electron number %d and spin %d are not consistent\n'
                                   'Note cell.spin = 2S = Nalpha - Nbeta, not 2S+1' %
                                   (ne, cell.spin))
            return nalpha, nbeta

    @nelec.setter
    def nelec(self, x):
        self._nelec = x

    def dump_flags(self, verbose=None):
        khf_ksymm.KsymAdaptedKSCF.dump_flags(self, verbose)
        logger.info(self, 'number of electrons per unit cell  '
                    'alpha = %d beta = %d', *self.nelec)
        return self

    def get_init_guess(self, cell=None, key='minao'):
        if cell is None:
            cell = self.cell
        dm_kpts = None
        key = key.lower()
        if key == '1e' or key == 'hcore':
            dm_kpts = self.init_guess_by_1e(cell)
        elif getattr(cell, 'natm', 0) == 0:
            logger.info(self, 'No atom found in cell. Use 1e initial guess')
            dm_kpts = self.init_guess_by_1e(cell)
        elif key == 'atom':
            dm = self.init_guess_by_atom(cell)
        elif key[:3] == 'chk':
            try:
                dm_kpts = self.from_chk()
            except (IOError, KeyError):
                logger.warn(self, 'Fail to read %s. Use MINAO initial guess',
                            self.chkfile)
                dm = self.init_guess_by_minao(cell)
        else:
            dm = self.init_guess_by_minao(cell)

        if dm_kpts is None:
            nkpts = self.kpts.nkpts_ibz
            # dm[spin,nao,nao] at gamma point -> dm_kpts[spin,nkpts,nao,nao]
            dm_kpts = np.repeat(dm[:,None,:,:], nkpts, axis=1)
            dm_kpts[0,:] *= 1.01
            dm_kpts[1,:] *= 0.99  # To slightly break spin symmetry
            assert dm_kpts.shape[0]==2

        ne = np.einsum('k,xkij,kji->x', self.kpts.weights_ibz, dm_kpts, self.get_ovlp(cell)).real
        # FIXME: consider the fractional num_electron or not? This maybe
        # relates to the charged system.
        nkpts = self.kpts.nkpts
        ne *= nkpts
        nelec = np.asarray(self.nelec)
        if np.any(abs(ne - nelec) > 1e-7*nkpts):
            logger.debug(self, 'Big error detected in the electron number '
                         'of initial guess density matrix (Ne/cell = %g)!\n'
                         '  This can cause huge error in Fock matrix and '
                         'lead to instability in SCF for low-dimensional '
                         'systems.\n  DM is normalized wrt the number '
                         'of electrons %s', ne.mean()/nkpts, nelec/nkpts)
            dm_kpts *= (nelec / ne).reshape(2,-1,1,1)
        return dm_kpts

    get_occ = get_occ
    energy_elec = energy_elec
    get_rho = get_rho

KUHF = KsymAdaptedKUHF
