// Copyright (c) 2017, Lawrence Livermore National Security, LLC and
// UT-Battelle, LLC.
// Produced at the Lawrence Livermore National Laboratory and the Oak Ridge
// National Laboratory.
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved.
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE

#include "ABPG.h"
#include "OrthoAndersonMix.h"
#include "Control.h"
#include "Ions.h"
#include "MGmol.h"
#include "Potentials.h"
#include "ProjectedMatricesInterface.h"
#include "LocGridOrbitals.h"

#include <iostream>
using namespace std;

template <class T>
void ABPG<T>::setup(T& orbitals)
{
    Control& ct = *(Control::instance());

    if (ct.wf_dyn == 1) // use Anderson extrapolation
    {
        if( ct.getOrbitalsType() == OrbitalsType::Orthonormal )
        wf_mix_ = new OrthoAndersonMix<T>(
            ct.wf_m, ct.betaAnderson, orbitals);
        else
            wf_mix_ = new AndersonMix<T>(
                ct.wf_m, ct.betaAnderson, orbitals);
    }
}

//
// Performs a single wave functions update step.
//
// orthof=true: wants orthonormalized updated wave functions
template <class T>
int ABPG<T>::update(T& orbitals, Ions& ions,
    const double precond_factor, const bool orthof,
    T& work_orbitals, const bool accelerate, const bool print_res,
    const double atol)
{
    abpg_nl_update_tm_.start();

    Control& ct = *(Control::instance());
    if (onpe0 && ct.verbose > 2) os_ << "ABPG::update()..." << endl;

    // temporary Orbitals to hold residual
    T res("Residual", orbitals, false);

    const bool check_res = (atol > 0.);
    double normRes       = mgmol_strategy_->computeResidual(
        orbitals, work_orbitals, res, (print_res || check_res), check_res);
    if (normRes < atol && check_res)
    {
        abpg_nl_update_tm_.stop();
        return 0;
    }

    // update wavefunctions
    update_states(orbitals, res, work_orbitals, precond_factor, accelerate);

    bool flag_ortho = false;
    if (ct.getOrbitalsType() == OrbitalsType::Eigenfunctions || orthof)
    {
        if (ct.isLocMode())
        {
            // orbitals.ortho_norm_local();
            orbitals.normalize();
        }
        else
        {
            orbitals.orthonormalizeLoewdin();
            flag_ortho = true;
        }
        if (ct.wf_dyn) // restart mixing after (ortho-)normalization
        {
            if (wf_mix_ != 0) wf_mix_->restart();
        }
    }
    else if (ct.getOrbitalsType() != OrbitalsType::Orthonormal)
    {
        orbitals.normalize();
    }

    // if orthonorm() not called, recompute overlap
    if (!flag_ortho && ct.getOrbitalsType() != OrbitalsType::Orthonormal)
    {
        orbitals.computeGramAndInvS();
    }

#if 0
    int msize=ct.numst;
    if( ct.loc_mode ){
        // work matrices
        dist_matrix::DistMatrix<DISTMATDTYPE>  u_dis("U", msize, msize);
        u_dis=proj_matrices_->matS();
        dist_matrix::DistMatrix<DISTMATDTYPE>  w_dis("W", msize, msize);
        w_dis.identity();
        u_dis.axpy(-1.,w_dis);
        const double inorm=u_dis.norm('i');
        if( onpe0 && inorm>1.e-8 )
            os_<<setprecision(2)<<scientific
                <<"Largest non diagonal element of S = "<<inorm<<endl;
    }
#endif

    abpg_nl_update_tm_.stop();

    return 1;
}

//////////////////////////////////////////////////////////////////////////////
// Update orbitals using MG preconditioning and Anderson extrapolation
template <class T>
void ABPG<T>::update_states(T& orbitals, T& res,
    T& work_orbitals, const double precond_factor,
    const bool accelerate)
{
    assert(orbitals.getIterativeIndex() >= 0);

    update_states_tm_.start();

    // if(onpe0) os_<<"Update wave functions"<<endl;

    Control& ct = *(Control::instance());

    if ((ct.getPrecondType() % 10) == 0 && ct.getMGlevels() >= 0)
    {
        // PRECONDITIONING
        // compute the preconditioned steepest descent direction
        // -> res
        mgmol_strategy_->precond_mg(res);
    }

    // non-energy compatible spread penalties are added after
    // preconditioning is applied
    if (ct.isSpreadFunctionalActive() && !ct.isSpreadFunctionalEnergy())
        mgmol_strategy_->addResidualSpreadPenalty(orbitals, res);

    // apply AOMM projector to have a gradient orthogonal to kernel functions
    if (ct.use_kernel_functions)
    {
        mgmol_strategy_->projectOutKernel(res);

        // just reapply mask for now...
        res.applyMask();
    }

    if (ct.project_out_psd)
    {
        if (onpe0) os_ << "Project out preconditioned gradient" << endl;
        orbitals.projectOut(res);
    }

    const double alpha = 0.5 * precond_factor;

    // Update wavefuntion
#ifdef PRINT_OPERATIONS
    if (onpe0) os_ << "Update states" << endl;
#else
    if (onpe0 && ct.verbose > 2) os_ << "Update states" << endl;
#endif
    if (accelerate)
    {
        res.scal(alpha);
        // Extrapolation scheme
        assert(wf_mix_ != 0);
        ostream os(nullptr);
        if(onpe0)os.rdbuf(cout.rdbuf());
        wf_mix_->update(res, work_orbitals, os, (ct.verbose>0));
    }
    else
    {
        // Preconditioned Power Method
        orbitals.axpy(alpha, res);

        if (ct.getOrbitalsType() == OrbitalsType::Orthonormal)
            orbitals.orthonormalizeLoewdin(false);
    }
    orbitals.incrementIterativeIndex();

    update_states_tm_.stop();

    assert(orbitals.getIterativeIndex() >= 0);
}

template <class T>
void ABPG<T>::printTimers(ostream& os)
{
    abpg_tm_.print(os);
    abpg_nl_update_tm_.print(os);
    comp_res_tm_.print(os);
    update_states_tm_.print(os);
}

template class ABPG<LocGridOrbitals>;
template class ABPG<ExtendedGridOrbitals>;
