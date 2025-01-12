// Copyright (c) 2017, Lawrence Livermore National Security, LLC and
// UT-Battelle, LLC.
// Produced at the Lawrence Livermore National Laboratory and the Oak Ridge
// National Laboratory.
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved.
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE
#include "DistMatrix.h"
#include "DistVector.h"
#include "BlacsContext.h"
#include "MGmol_MPI.h"
#include "Power.h"

#include <iostream>

int main(int argc, char** argv)
{
    int mpirc = MPI_Init(&argc, &argv);
    int myrank;
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

    if (myrank == 0)
       std::cout << "Test condition DistMatrix using power method" << std::endl;

    {
        MGmol_MPI::setup(MPI_COMM_WORLD, std::cout);

        int nprow=2;
        int npcol=2;
        dist_matrix::BlacsContext bc(MPI_COMM_WORLD, nprow,npcol);

        const int n = 51;
        const double shift = 0.2;

        //block size
        const int nb=7;

        //construct diagonal 10x10 matrix
        dist_matrix::DistMatrix<double> S("S",bc,n,n,nb,nb);
        for(int i = 0; i < n; i++)
            S.setVal(i, i, 0.1*i+shift);

        const double expected_cond = (0.1*(n-1)+shift)/(shift);

        //
        // Now use power method to estimate extreme eigenvalues
        // and approximate condition number
        //

        // DistVector will be generated in Power
        // and need default values
        dist_matrix::DistMatrix<DISTMATDTYPE>::setDefaultBlacsContext(&bc);
        double emin;
        double emax;
        Power<dist_matrix::DistVector<double>,
              dist_matrix::DistMatrix<double>> power(n);

        power.computeEigenInterval(S, emin, emax, 1.e-3, (myrank == 0));
        std::cout << "emax = "<<emax<<std::endl;
        std::cout << "emin = "<<emin<<std::endl;

        const double cond = emax/emin;
        std::cout<<"Condition number calculated with Power method = "<<cond<<std::endl;
        std::cout<<"Expected condition number   = "<<expected_cond<<std::endl;
        const double tolpower = 0.06;
        const double rdiff = std::abs(cond-expected_cond)/cond;
        std::cout << "Calculated and expected condition number differ by "
                      <<rdiff*100.<<"%" << std::endl;
        if (rdiff > tolpower)
        {
            std::cerr << "Error larger than tol = "<<tolpower*100.<<"%" << std::endl;
            return 1;
        }

    }

    mpirc = MPI_Finalize();

    // return 0 for SUCCESS
    return 0;
}
