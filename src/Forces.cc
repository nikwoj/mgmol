// Copyright (c) 2017, Lawrence Livermore National Security, LLC and
// UT-Battelle, LLC.
// Produced at the Lawrence Livermore National Laboratory and the Oak Ridge
// National Laboratory.
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved.
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE

#include "Control.h"
#include "DataDistribution.h"
#include "Forces.h"
#include "Grid.h"
#include "Ions.h"
#include "KBPsiMatrixSparse.h"
#include "MGmol.h"
#include "MGmol_blas1.h"
#include "MPIdata.h"
#include "LocGridOrbitals.h"
#include "Mesh.h"
#include "Potentials.h"
#include "ProjectedMatrices.h"
#include "ProjectedMatricesSparse.h"
#include "ReplicatedWorkSpace.h"
#include "VariableSizeMatrix.h"
#include "Vector3D.h"
#include "tools.h"

#include <iostream>

#define Ry2Ha 0.5;
double shift_R[3*NPTS][3];

double get_trilinval(const double xc, const double yc, const double zc,
    const double h0, const double h1, const double h2, const Vector3D& ref,
    const Vector3D& lattice, RadialInter& lpot);

#if NPTS > 3
double get_deriv4(double value[4])
{
    double sum = (value[1] - value[0]) * 2. / (3. * DELTAC);
    sum -= (value[3] - value[2]) / (12. * DELTAC);
    return sum;
}
#endif

double get_deriv2(double value[2])
{
    return (value[1] - value[0]) / (2. * DELTAC);
}

template <class T>
void Forces<T>::evaluateShiftedFields(
    Ion& ion, std::vector<std::vector<double>>& var_pot,
    std::vector<std::vector<double>>& var_charge)
{
    evaluateShiftedFields_tm_.start();

    const Species& sp(ion.getSpecies());
    const RadialInter& lpot = ion.getLocalPot();

    auto lambda_radiallpot = [&lpot] (double r) { return lpot.cubint(r); };
    auto lambda_rhocomp = [&sp] (double r) { return sp.getRhoComp(r); };

    Vector3D ref_position(ion.position(0), ion.position(1), ion.position(2));

    // generate shifted atomic positions
    // (NPTS in each direction)
    std::vector<Vector3D> positions;
    for (short ishift = 0; ishift < 3*NPTS; ishift++)
    {
        Vector3D shifted_point(ref_position);
        shifted_point[0] += shift_R[ishift][0];
        shifted_point[1] += shift_R[ishift][1];
        shifted_point[2] += shift_R[ishift][2];

        positions.push_back(shifted_point);
    }

    const double lrad = sp.lradius();

    // evaluate filtered/unfiltered potential on mesh
    // for shifted atomic poistions
    evaluateRadialFunc(positions, lrad, var_pot, lambda_radiallpot);

    // evaluate Gaussian compensating charge on mesh
    // for shifted atomic poistions
    evaluateRadialFunc(positions, lrad, var_charge, lambda_rhocomp);

    evaluateShiftedFields_tm_.stop();
};

template <class T>
void Forces<T>::evaluateRadialFunc(const std::vector<Vector3D>& positions,
                            const double lrad,
                            std::vector<std::vector<double>>& var,
                            std::function<double(double)> const& lambda_radial)
{
    Control& ct     = *(Control::instance());

    Mesh* mymesh           = Mesh::instance();
    const pb::Grid& mygrid = mymesh->grid();

    int offset = 0;

    const int dim0 = mygrid.dim(0);
    const int dim1 = mygrid.dim(1);
    const int dim2 = mygrid.dim(2);

    const double h0 = mygrid.hgrid(0);
    const double h1 = mygrid.hgrid(1);
    const double h2 = mygrid.hgrid(2);

    Vector3D ll(mygrid.ll(0), mygrid.ll(1), mygrid.ll(2));

    Vector3D point( 0., 0., 0. );

    point[0] = mygrid.start(0);

    for (int ix = 0; ix < dim0; ix++)
    {
        point[1] = mygrid.start(1);

        for (int iy = 0; iy < dim1; iy++)
        {
            point[2] = mygrid.start(2);
            for (int iz = 0; iz < dim2; iz++)
            {
                short ishift = 0;

                for (auto& position : positions)
                {
                    const double r
                        = position.minimage(point, ll, ct.bcPoisson);
                    if (r < lrad)
                    {
                        var[ishift][offset] = lambda_radial(r);
                    }
                    else
                    {
                        var[ishift][offset] = 0.;
                    }
                    ishift++;
                }

                offset++;

                point[2] += h2;

            } // end for iz

            point[1] += h1;

        } // end for iy

        point[0] += h0;

    } // end for ix

}

template <class T>
void Forces<T>::get_loc_proj(RHODTYPE* rho,
    std::vector<std::vector<double>>& var_pot,
    std::vector<std::vector<double>>& var_charge,
    std::vector<double>& loc_proj)
{
    get_loc_proj_tm_.start();

    Mesh* mymesh    = Mesh::instance();
    const int numpt = mymesh->numpt();

    Potentials& pot = hamiltonian_->potential();

    for (short dir = 0; dir < 3; dir++)
    {
        double* lproj = &(loc_proj[dir * NPTS]);

        // pseudopotential * rho
        // - delta rhoc * vh
        for (short ishift = 0; ishift < NPTS; ishift++)
        {
            const std::vector<double>& vpot      = var_pot[NPTS*dir+ishift];
            const std::vector<double>& drhoc_ptr = var_charge[NPTS*dir+ishift];

            for (int idx = 0; idx < numpt; idx++)
            {
                lproj[ishift] += vpot[idx] * rho[idx];
                lproj[ishift] -= drhoc_ptr[idx] * pot.vh_rho(idx);
            }
        }
    }

    get_loc_proj_tm_.stop();
}

template <class T>
void Forces<T>::lforce_ion(Ion& ion, RHODTYPE* rho, std::vector<double>& loc_proj)
{
    Mesh* mymesh    = Mesh::instance();
    const int numpt = mymesh->numpt();

    std::vector<std::vector<double>> var_pot;
    std::vector<std::vector<double>> var_charge;

    var_pot.resize(3*NPTS);
    var_charge.resize(3*NPTS);
    for(short i = 0; i < 3*NPTS; i++)
    {
        var_pot[i].resize(numpt);
        var_charge[i].resize(numpt);
    }

    // generate var_pot and var_charge for this ion
    evaluateShiftedFields(ion, var_pot, var_charge);

    get_loc_proj(rho, var_pot, var_charge, loc_proj);
}

template <class T>
void Forces<T>::lforce(Ions& ions, RHODTYPE* rho)
{
    Mesh* mymesh           = Mesh::instance();
    const pb::Grid& mygrid = mymesh->grid();
    //    Control& ct = *(Control::instance());

    lforce_tm_.start();

    //    if(ct.short_sighted)
    //    {
    // cout<<"max Vl radius = "<<ions.getMaxVlRadius()<<endl;

    int buffer_size  = 3 * NPTS;
    std::vector<double> loc_proj(buffer_size);

    lforce_local_tm_.start();

    int cols[buffer_size];
    for (int i = 0; i < buffer_size; i++)
        cols[i] = i;

    VariableSizeMatrix<sparserow> loc_proj_mat(
        "locProj", ions.overlappingVL_ions().size());
    // Loop over ions with potential overlaping with local subdomain
    for (auto& ion : ions.overlappingVL_ions())
    {
        int index = ion->index();
        for (short dir = 0; dir < 3 * NPTS; dir++)
            loc_proj[dir] = 0.;
        lforce_ion(*ion, rho, loc_proj);

        /* insert row into 2D matrix */
        loc_proj_mat.insertNewRow(buffer_size, index, &cols[0], &loc_proj[0],
                                  true);
    }

    lforce_local_tm_.stop();
    consolidate_data_.start();

    /* Distribute/ gather data */
    /* ions have rectangular domain and we only care about gathering on
     * ions centered on the local domain, hence we set append=false
     */
    const pb::PEenv& myPEenv = mymesh->peenv();
    double domain[3]         = { mygrid.ll(0), mygrid.ll(1), mygrid.ll(2) };
    DataDistribution distributor(
        "lforce", ions.getMaxVlRadius(), myPEenv, domain);
    // the first time through, we may need to recompute the buffer size for
    // sparse matrix communications since we are dealing with a matrix not seen
    // before
    static bool first_time = true;
    if (first_time)
    {
        DataDistribution::enforceComputeMaxDataSize();
        first_time = false;
    }
    distributor.augmentLocalData(loc_proj_mat, false);

    consolidate_data_.stop();

    for(auto& lion : ions.local_ions())
    {
        // Forces opposed to the gradient
        int index   = lion->index();
        int* rindex = (int*)loc_proj_mat.getTableValue(index);
        assert(rindex != NULL);
        std::fill(loc_proj.begin(), loc_proj.end(), 0.);
        loc_proj_mat.row_daxpy(*rindex, buffer_size, mygrid.vel(),
                               &loc_proj[0]);

        lion->add_force(-get_deriv2(&loc_proj[0]),
            -get_deriv2(&(loc_proj[NPTS])), 
            -get_deriv2(&(loc_proj[2 * NPTS])));
    }
//    }
//    else
/*
    {
       double  **loc_proj;

       DIM2(loc_proj, ions.getNumIons(), 3*NPTS, double);

       init_loc_proj(loc_proj,ions.getNumIons());

       // Loop over ions
       std::vector<Ion*>::const_iterator ion=ions.overlappingVL_ions().begin();
       while(ion!=ions.overlappingVL_ions().end()){

           int index=(*ion)->index();
           lforce_ion(**ion, rho, &loc_proj[index]);

           ion++;
       }

       int      n = 3*NPTS*ions.getNumIons();
       pb::my_dscal(n,mygrid.vel(),*loc_proj);
       global_sums_double(*loc_proj, n);

       std::vector<Ion*>::iterator lion=ions.local_ions().begin();
       while(lion!=ions.local_ions().end()){
           // Forces opposed to the gradient
           int index=(*lion)->index();
           (*lion)->add_force(-get_deriv2(&loc_proj[index][0]),
                              -get_deriv2(&loc_proj[index][1*NPTS]),
                              -get_deriv2(&loc_proj[index][2*NPTS]) );

           lion++;
       }
       D2FREE(loc_proj);
    }
*/
#ifdef HAVE_TRICUBIC
    Potentials& pot = hamiltonian_->potential();
    if (pot.withVext())
    {
        double position[3];
        double grad[3];
        for (auto& ion : ions.local_ions())
        {
            ion->getPosition(position);

            pot.getGradVext(position, grad);

            const double charge = (*ion)->getZion();

            ion->add_force(
                grad[0] * charge, grad[1] * charge, grad[2] * charge);
            // if( onpe0 )
            //(*MPIdata::sout)<<"External force on Ion "<<ion->name()<<": "
            //                              <<grad[0]*charge<<","
            //                              <<grad[1]*charge<<","
            //                              <<grad[2]*charge<<endl;
            ion++;
        }
    }
#endif

    lforce_tm_.stop();
}

// Get the nl energy as the trace of loc_kbpsi*mat_X for several loc_kbpsi
// result added to erg

template <class T>
void Forces<T>::nlforceSparse(T& orbitals, Ions& ions)
{
    if (ions.getNumIons() == 0) return;

    Control& ct     = *(Control::instance());
    MGmol_MPI& mmpi = *(MGmol_MPI::instance());

    // first check if any NL forces computation necessary
    if (!ions.hasNLprojectors())
    {
        if (onpe0) cout << "No nl forces!!" << endl;
        return;
    }

    nlforce_tm_.start();

    kbpsi_tm_.start();
    KBPsiMatrixSparse*** kbpsi = new KBPsiMatrixSparse**[3];

    // compute all kbpsi matrices for all shifts
    for (short dir = 0; dir < 3; dir++)
    {
        kbpsi[dir] = new KBPsiMatrixSparse*[NPTS];
        for (int npt = 0; npt < NPTS; npt++)
        {
            kbpsi[dir][npt] = new KBPsiMatrixSparse(NULL, false);

            double shift[3] = { 0., 0., 0. };
            shift[dir]      = shift_R[dir*NPTS+npt][dir];
            Ions shifted_ions(ions, shift); ///***

            kbpsi[dir][npt]->setup(shifted_ions, orbitals);
            kbpsi[dir][npt]->computeAll(shifted_ions, orbitals);
        }
    }
    kbpsi_tm_.stop();

    energy_tm_.start();
    map<int, double*> erg;
    if (ct.short_sighted)
    {
        ProjectedMatricesSparse* projmatrices
            = dynamic_cast<ProjectedMatricesSparse*>(proj_matrices_);
        assert(projmatrices);
        DensityMatrixSparse& dm(projmatrices->getDM());

        // loop over all the ions
        // parallelization over ions by including only those centered in
        // subdomain
        for (auto& ion : ions.local_ions())
        {
            std::vector<int> gids;
            ion->getGidsNLprojs(gids);
            std::vector<short> kbsigns;
            ion->getKBsigns(kbsigns);

            const short nprojs = (short)gids.size();
            for (short i = 0; i < nprojs; i++)
            {
                const int gid = gids[i];

                double* zeros = new double[3 * NPTS];
                memset(zeros, 0, 3 * NPTS * sizeof(double));
                erg.insert(pair<int, double*>(gid, zeros));
            }

#ifdef _OPENMP
#pragma omp parallel for
#endif
            for (short ii = 0; ii < nprojs * 3 * NPTS; ii++)
            {
                short ip            = ii / (3 * NPTS);
                const int gid       = gids[ip];
                const double kbmult = (double)kbsigns[ip];

                short it = ii % (3 * NPTS);
                // for(short it=0;it<3*NPTS;it++)
                {
                    short dir    = it / NPTS;
                    short ishift = it % NPTS;
                    double alpha = kbpsi[dir][ishift]->getTraceDM(gid, dm);
                    erg[gid][NPTS * dir + ishift] = alpha * kbmult;
                }
            }
        }
    }
    else
    {
        ReplicatedWorkSpace<DISTMATDTYPE>& wspace(ReplicatedWorkSpace<DISTMATDTYPE>::instance());
        const int ndim               = wspace.getDim();
        DISTMATDTYPE* work_DM_matrix = wspace.square_matrix();

        assert(dynamic_cast<ProjectedMatrices*>(proj_matrices_));
        ProjectedMatrices* projmatrices
            = dynamic_cast<ProjectedMatrices*>(proj_matrices_);
        projmatrices->getReplicatedDM(work_DM_matrix);

        // loop over all the ions
        // parallelization over ions by including only those centered in
        // subdomain
        for (auto& ion : ions.local_ions())
        {
            std::vector<int> gids;
            ion->getGidsNLprojs(gids);
            std::vector<short> kbsigns;
            ion->getKBsigns(kbsigns);

            const short nprojs = (short)gids.size();
            for (short i = 0; i < nprojs; i++)
            {
                const int gid       = gids[i];
                const double kbmult = (double)kbsigns[i];

                double* zeros = new double[3 * NPTS];
                memset(zeros, 0, 3 * NPTS * sizeof(double));
                erg.insert(pair<int, double*>(gid, zeros));

                for (short dir = 0; dir < 3; dir++)
                    for (short ishift = 0; ishift < NPTS; ishift++)
                    {
                        double alpha = kbpsi[dir][ishift]->getTraceDM(
                            gid, work_DM_matrix, ndim);
                        erg[gid][NPTS * dir + ishift] = alpha * kbmult;
                    }
            }
        }
    }
    energy_tm_.stop();

    // release memory
    for (short dir = 0; dir < 3; dir++)
    {
        for (short npt = 0; npt < NPTS; npt++)
        {
            delete kbpsi[dir][npt];
        }
        delete[] kbpsi[dir];
    }
    delete[] kbpsi;

    // compute forces on each ion by finite differences
    const double factor                     = -1. * Ry2Ha;
    for (auto& ion : ions.local_ions())
    {
        std::vector<int> gids;
        ion->getGidsNLprojs(gids);

        const short nprojs = (short)gids.size();
        for (short i = 0; i < nprojs; i++)
        {
            const int gid = gids[i];

            double ff[3] = { get_deriv2(&erg[gid][NPTS * 0]) * factor,
                get_deriv2(&erg[gid][NPTS * 1]) * factor,
                get_deriv2(&erg[gid][NPTS * 2]) * factor };

            if (mmpi.nspin() == 2)
            {
                double sum[3] = { 0., 0., 0. };
                mmpi.allreduceSpin(&ff[0], &sum[0], 3, MPI_SUM);
                for (short dir = 0; dir < 3; dir++)
                    ff[dir] = sum[dir];
            }

            ion->add_force(ff[0], ff[1], ff[2]);
        }

    }

    map<int, double*>::iterator ierg = erg.begin();
    while (ierg != erg.end())
    {
        delete[] ierg->second;
        ++ierg;
    }

    nlforce_tm_.stop();
}

template <class T>
void Forces<T>::force(T& orbitals, Ions& ions)
{
#ifdef USE_BARRIERS
    MGmol_MPI& mmpi = *(MGmol_MPI::instance());
    mmpi.barrier();
#endif

    total_tm_.start();

    const int numpt = rho_->rho_[0].size();
    double one      = 1.;

    std::vector<RHODTYPE> rho_tmp;
    if (rho_->rho_.size() > 1)
    {
        rho_tmp.resize(numpt);

        memcpy(&rho_tmp[0], &(rho_->rho_[0][0]), numpt * sizeof(RHODTYPE));
        MPaxpy(numpt, one, &rho_->rho_[1][0], &rho_tmp[0]);
    }

    std::vector<RHODTYPE>& rho = (rho_->rho_.size() > 1) ? rho_tmp : rho_->rho_[0];

    for(int i = 0; i<3*NPTS; i++)
    {
        for(int j = 0; j<3; j++) shift_R[i][j] = 0.;
    }
    for(int i = 0; i<3; i++)
    {
        shift_R[NPTS*i+0][i] = -DELTAC;
        shift_R[NPTS*i+1][i] = DELTAC;
#if NPTS > 3
        shift_R[NPTS*i+2][i] = -2. * DELTAC;
        shift_R[NPTS*i+3][i] = 2. * DELTAC;
#endif
    }
    Control& ct = *(Control::instance());

    // Zero out forces
    ions.resetForces();

    // Get the ion-ion component and store.
    ions.iiforce(ct.bcPoisson);

    // Add the non-local forces
#ifdef USE_BARRIERS
    mmpi.barrier();
#endif
    //    if(ct.short_sighted)
    nlforceSparse(orbitals, ions);
    //    else
    //        nlforce(orbitals,ions);

    // Add the local forces
#ifdef USE_BARRIERS
    mmpi.barrier();
#endif
    lforce(ions, &rho[0]);

#ifdef USE_BARRIERS
    mmpi.barrier();
#endif

    total_tm_.stop();
}

template class Forces<LocGridOrbitals>;
template class Forces<ExtendedGridOrbitals>;
