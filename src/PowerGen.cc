// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory.
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved.
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE

#include "PowerGen.h"

#include "GramMatrix.h"
#include "mputils.h"
#include "random.h"
#include "DistVector.h"

#include <vector>

Timer PowerGen::compute_tm_("PowerGen::compute");


/* Use the power method to compute the extents of the spectrum of the
 * generalized eigenproblem. In order to use a residual-based convergence
 * criterion in an efficient way, we delay normalization of the vectors to avoid
 * multiple matvecs. NOTE: We are only interested in the eigenvalues, so the
 * final eigenvector may not be normalized.
 */
void PowerGen::computeGenEigenInterval(dist_matrix::DistMatrix<double>& mat,
    GramMatrix& gm,
    std::vector<double>& interval, const int maxits,
    const double pad)
{
    srand(13579);

    compute_tm_.start();

    interval.clear();

    dist_matrix::DistMatrix<double> smat(gm.getMatrix());

    // use the power method to get the eigenvalue interval
    const int m      = mat.m(); // number of global rows
    const int mloc   = mat.mloc(); // number of local rows
    const double one = 1., zero = 0.;

    // shift
    mat.axpy(shift_, smat);

    // initialize solution data
    // initial guess
    dist_matrix::DistVector<double> sol("sol", m);
    sol.assign(vec1_); // initialize local solution data
    // new solution
    dist_matrix::DistVector<double> new_sol("new_sol", m);
    std::vector<double> vec(mloc, 0.);
    new_sol.assign(vec);

    // get norm of initial sol
    double alpha = sol.nrm2();
    double gamma = 1. / alpha;
    std::cout << "e1:: ITER 0:: = " << alpha << " shift = " << shift_ << std::endl;

    // residual
    dist_matrix::DistVector<double> res(new_sol);
    // initial eigenvalue estimate (for shifted system)
    double beta = sol.dot(new_sol);

    // compute first extent
    int iter1 = 0;
    // loop
    for (int i = 0; i < maxits; i++)
    {
        iter1++;

        // First compute residual for convergence check
        res.gemv('N', one, mat, sol, zero);
        // store matvec result appropriately scaled for later reuse
        new_sol.clear();
        new_sol.axpy(gamma, res);
        // Compute residual: res = beta*S*x - mat*x
        res.gemm('N', 'N', beta, smat, sol, -1.);
        // compute residual norm
        double resnorm = res.nrm2();
        // check for convergence
        if (resnorm < 1.0e-2) break;

        // apply inverse to new_sol to update solution
        // No need to do matvec with scaled copy of sol.
        // Reuse previously stored matvec from residual calculation
        gm.applyInv(new_sol); // can also do gemv with gm_->getInverse()

        // compute 'shifted' eigenvalue
        beta = sol.dot(new_sol);
        // scale beta by gamma to account for normalizing sol
        beta *= gamma;
        // update solution data
        sol = new_sol;
        // compute norm and update gamma
        alpha = sol.nrm2();
        gamma = 1. / alpha;
    }
    // compute first extent (eigenvalue)
    double e1 = beta - shift_;
    sol.copyDataToVector(vec1_);

    // shift matrix by beta and compute second extent
    // store shift
    double shft_e1 = -beta;
    mat.axpy(shft_e1, smat);

    // reset data and begin loop
    sol.assign(vec2_);
    new_sol.assign(vec);
    alpha = sol.nrm2();
    gamma = 1. / alpha;
    beta  = sol.dot(new_sol);

    // loop
    std::cout << "e2:: ITER 0:: = " << beta << std::endl;
    int iter2 = 0;
    for (int i = 0; i < maxits; i++)
    {
        iter2++;

        // First compute residual for convergence check
        res.gemv('N', one, mat, sol, zero);
        // store matvec result appropriately scaled for later reuse
        new_sol.clear();
        new_sol.axpy(gamma, res);
        // Compute residual: res = beta*S*x - mat*x
        res.gemm('N', 'N', beta, smat, sol, -1.);
        // compute residual norm
        double resnorm = res.nrm2();
        // check for convergence
        if (resnorm < 1.0e-2) break;

        // apply inverse to new_sol to update solution
        // No need to do matvec with scaled copy of sol.
        // Reuse previously stored matvec from residual calculation
        gm.applyInv(new_sol); // can also do gemv with gm_->getInverse()

        // compute 'shifted' eigenvalue
        beta = sol.dot(new_sol);
        // scale beta by gamma to account for not normalizing sol
        beta *= gamma;
        // update solution data
        sol = new_sol;
        // compute norm and update gamma
        alpha = sol.nrm2();
        gamma = 1. / alpha;
    }
    // compute second extent
    double e2 = beta - shft_e1 - shift_;
    sol.copyDataToVector(vec2_);

    // save results
    double tmp     = e1;
    e1             = std::min(tmp, e2);
    e2             = std::max(tmp, e2);
    double padding = pad * (e2 - e1);

    std::cout << "Power method Eigen intervals********************  = ( " << e1
           << ", " << e2 << ")\n"
           << "iter1 = " << iter1 << ", iter2 = " << iter2 << std::endl;

    e1 -= padding;
    e2 += padding;
    interval.push_back(e1);
    interval.push_back(e2);

    // update shft
    shift_ = std::max(fabs(e1), fabs(e2));

    compute_tm_.stop();
}
