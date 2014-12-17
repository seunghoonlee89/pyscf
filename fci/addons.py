#!/usr/bin/env python

import sys
import numpy
import cistring

def large_ci(ci, norb, nelec, tol=.1):
    if isinstance(nelec, int):
        neleca = nelecb = nelec/2
    else:
        neleca, nelecb = nelec
    idx = numpy.argwhere(abs(ci) > tol)
    res = []
    for i,j in idx:
        res.append((ci[i,j], \
                    bin(cistring.addr2str(norb, neleca, i)), \
                    bin(cistring.addr2str(norb, nelecb, j))))
    return res

def initguess_triplet(norb, nelec, binstring):
    if isinstance(nelec, int):
        neleca = nelecb = nelec/2
    else:
        neleca, nelecb = nelec
    na = cistring.num_strings(norb, neleca)
    nb = cistring.num_strings(norb, nelecb)
    addr = cistring.str2addr(norb, neleca, int(binstring,2))
    ci0 = numpy.zeros((na,nb))
    ci0[addr,0] = numpy.sqrt(.5)
    ci0[0,addr] =-numpy.sqrt(.5)
    return ci0


# construct (N-1)-electron wavefunction by removing an alpha electron from
# N-electron wavefunction:
# |N-1> = a_p |N>
def des_a(ci0, norb, nelec, ap_id):
    if isinstance(nelec, int):
        neleca = nelecb = nelec / 2
    else:
        neleca, nelecb = nelec

    des_index = cistring.gen_des_str_index(range(norb), neleca)
    na_ci1 = cistring.num_strings(norb, neleca-1)
    ci1 = numpy.zeros((na_ci1, ci0.shape[1]))

    entry_has_ap = (des_index[:,:,0] == ap_id)
    addr_ci0 = numpy.any(entry_has_ap, axis=1)
    addr_ci1 = des_index[entry_has_ap,2]
    sign = des_index[entry_has_ap,3]
    #print(addr_ci0)
    #print(addr_ci1)
    ci1[addr_ci1] = sign.reshape(-1,1) * ci0[addr_ci0]
    return ci1

# construct (N-1)-electron wavefunction by removing a beta electron from
# N-electron wavefunction:
def des_b(ci0, norb, nelec, ap_id):
    if isinstance(nelec, int):
        neleca = nelecb = nelec / 2
    else:
        neleca, nelecb = nelec
    des_index = cistring.gen_des_str_index(range(norb), nelecb)
    nb_ci1 = cistring.num_strings(norb, nelecb-1)
    ci1 = numpy.zeros((ci0.shape[0], nb_ci1))

    entry_has_ap = (des_index[:,:,0] == ap_id)
    addr_ci0 = numpy.any(entry_has_ap, axis=1)
    addr_ci1 = des_index[entry_has_ap,2]
    sign = des_index[entry_has_ap,3]
    ci1[:,addr_ci1] = ci0[:,addr_ci0] * sign
    return ci1

# construct (N+1)-electron wavefunction by adding an alpha electron to
# N-electron wavefunction:
# |N+1> = a_p^+ |N>
def cre_a(ci0, norb, nelec, ap_id):
    if isinstance(nelec, int):
        neleca = nelecb = nelec / 2
    else:
        neleca, nelecb = nelec
    cre_index = cistring.gen_cre_str_index(range(norb), neleca)
    na_ci1 = cistring.num_strings(norb, neleca+1)
    ci1 = numpy.zeros((na_ci1, ci0.shape[1]))

    entry_has_ap = (cre_index[:,:,0] == ap_id)
    addr_ci0 = numpy.any(entry_has_ap, axis=1)
    addr_ci1 = cre_index[entry_has_ap,2]
    sign = cre_index[entry_has_ap,3]
    ci1[addr_ci1] = sign.reshape(-1,1) * ci0[addr_ci0]
    return ci1

# construct (N+1)-electron wavefunction by adding a beta electron to
# N-electron wavefunction:
def cre_b(ci0, norb, nelec, ap_id):
    if isinstance(nelec, int):
        neleca = nelecb = nelec / 2
    else:
        neleca, nelecb = nelec
    cre_index = cistring.gen_cre_str_index(range(norb), nelecb)
    nb_ci1 = cistring.num_strings(norb, nelecb-1)
    ci1 = numpy.zeros((ci0.shape[0], nb_ci1))

    entry_has_ap = (cre_index[:,:,0] == ap_id)
    addr_ci0 = numpy.any(entry_has_ap, axis=1)
    addr_ci1 = cre_index[entry_has_ap,2]
    sign = cre_index[entry_has_ap,3]
    ci1[:,addr_ci1] = ci0[:,addr_ci0] * sign
    return ci1

if __name__ == '__main__':
    a4 = 10*numpy.arange(4)[:,None]
    a6 = 10*numpy.arange(6)[:,None]
    b4 = numpy.arange(4)
    b6 = numpy.arange(6)
    print(map(bin, cistring.gen_strings4orblist(range(4), 3)))
    print(map(bin, cistring.gen_strings4orblist(range(4), 2)))
    print(desa(a4+b4, 4, 6, 0))
    print(desa(a4+b4, 4, 6, 1))
    print(desa(a4+b4, 4, 6, 2))
    print(desa(a4+b4, 4, 6, 3))
    print('-------------')
    print(desb(a6+b4, 4, (2,3), 0))
    print(desb(a6+b4, 4, (2,3), 1))
    print(desb(a6+b4, 4, (2,3), 2))
    print(desb(a6+b4, 4, (2,3), 3))
    print('-------------')
    print(crea(a6+b4, 4, (2,3), 0))
    print(crea(a6+b4, 4, (2,3), 1))
    print(crea(a6+b4, 4, (2,3), 2))
    print(crea(a6+b4, 4, (2,3), 3))
    print('-------------')
    print(creb(a6+b6, 4, 4, 0))
    print(creb(a6+b6, 4, 4, 1))
    print(creb(a6+b6, 4, 4, 2))
    print(creb(a6+b6, 4, 4, 3))
