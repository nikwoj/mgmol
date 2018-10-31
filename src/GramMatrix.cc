// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. 
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved. 
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE

#include <iostream>
#include <iomanip>
using namespace std;

#include "GramMatrix.h"
#include "MGmol_MPI.h"

void rotateSym(dist_matrix::DistMatrix<DISTMATDTYPE>& mat,
               const dist_matrix::DistMatrix<DISTMATDTYPE>& rotation_matrix,
               dist_matrix::DistMatrix<DISTMATDTYPE>& work);


GramMatrix::GramMatrix(const int ndim)
{
    dim_=ndim;

    if( dim_>0 ){
        matS_  =new dist_matrix::DistMatrix<DISTMATDTYPE>("S",    ndim, ndim);
        ls_    =new dist_matrix::DistMatrix<DISTMATDTYPE>("LS",   ndim, ndim);
        invS_  =new dist_matrix::DistMatrix<DISTMATDTYPE>("invS", ndim, ndim);

        work_  =new dist_matrix::DistMatrix<DISTMATDTYPE>("work", ndim, ndim);
    }else{
        matS_  =0;
        ls_    =0;
        invS_  =0;
        work_  =0;
    }
    
    orbitals_index_=-1;
    isLSuptodate_  =false;
    isInvSuptodate_=false;
}

GramMatrix::GramMatrix(const GramMatrix& gm)
{
    dim_=gm.dim_;

    if( dim_>0 ){
        matS_  =new dist_matrix::DistMatrix<DISTMATDTYPE>(*gm.matS_);
        ls_    =new dist_matrix::DistMatrix<DISTMATDTYPE>(*gm.ls_);
        invS_  =new dist_matrix::DistMatrix<DISTMATDTYPE>(*gm.invS_);

        work_  =new dist_matrix::DistMatrix<DISTMATDTYPE>(*gm.work_);
    }else{
        matS_  =0;
        ls_    =0;
        invS_  =0;
        work_  =0;
    }
    
    orbitals_index_=gm.orbitals_index_;
    isLSuptodate_  =gm.isLSuptodate_;
    isInvSuptodate_=gm.isInvSuptodate_;
}

GramMatrix& GramMatrix::operator=(const GramMatrix& gm)
{
    if(this==&gm)return *this;
    
    dim_=gm.dim_;

    if( dim_>0 ){
        *matS_  =*gm.matS_;
        *ls_    =*gm.ls_;
        *invS_  =*gm.invS_;
        *work_  =*gm.work_;
    }else{
        matS_  =0;
        ls_    =0;
        invS_  =0;
        work_  =0;
    }
    
    orbitals_index_=gm.orbitals_index_;
    isLSuptodate_  =gm.isLSuptodate_;
    isInvSuptodate_=gm.isInvSuptodate_;
    
    return *this;
}

GramMatrix::~GramMatrix()
{
    if( dim_>0 ){
        delete matS_;
        delete invS_;
        delete ls_;
        delete work_;
    }
}

void GramMatrix::computeInverse()
{
    assert( isLSuptodate_ );
    
    dist_matrix::DistMatrix<DISTMATDTYPE> zz(*ls_);
    zz.potri('l');
    work_->identity();
    invS_->symm('l','l',1.,zz,*work_,0.);
    
    isInvSuptodate_=true;
}

void GramMatrix::transformLTML(dist_matrix::DistMatrix<DISTMATDTYPE>& mat, const DISTMATDTYPE alpha)const
{
    // mat = alpha* L**T * mat
    mat.trmm('l','l','t','n',alpha,*ls_); 
    // mat=mat*L
    mat.trmm('r','l','n','n',1.,*ls_);
}

// Solve Z<-L**(-T)*Z 
void GramMatrix::solveLST(dist_matrix::DistMatrix<DISTMATDTYPE>& z)const
{
    ls_->trtrs('l', 't', 'n', z);
}

double GramMatrix::computeCond()
{
    double anorm=matS_->norm('1');
    double invcond=ls_->pocon('l',anorm);
    double cond=0.;
    if( onpe0 ) cond=1./invcond;
#ifdef USE_MPI
    MGmol_MPI& mmpi = *(MGmol_MPI::instance());
    mmpi.bcast(&cond,   1);
#endif

    return cond;
}

// mat is overwritten by inv(ls)*mat*inv(ls**T)
void GramMatrix::sygst(dist_matrix::DistMatrix<DISTMATDTYPE>& mat)
{
    mat.sygst(1, 'l', *ls_);
}

void GramMatrix::rotateAll(const dist_matrix::DistMatrix<DISTMATDTYPE>& matU)
{
    rotateSym(*matS_, matU, *work_);
    
    updateLS();

    computeInverse();
}

void GramMatrix::setMatrix(const dist_matrix::DistMatrix<DISTMATDTYPE>& mat, const int orbitals_index)
{
    assert( matS_!=0 );
    
    *matS_=mat;
    orbitals_index_=orbitals_index;
    
    updateLS();
}
//void GramMatrix::setMatrix(const double* const val, const int orbitals_index)
//{
//    matS_->init(val, dim_);
//    orbitals_index_=orbitals_index;
//    
//    updateLS();
//}

double GramMatrix::getLinDependent2states(int& st1, int& st2)const
{
    vector<DISTMATDTYPE> eigenvalues(dim_);
    dist_matrix::DistMatrix<DISTMATDTYPE> u("u", dim_, dim_);
    // solve a standard symmetric eigenvalue problem
    dist_matrix::DistMatrix<DISTMATDTYPE> mat(*matS_);
    mat.syev('v', 'l', eigenvalues, u);
    // get the index of the two largest components of column 0 of u
    DISTMATDTYPE val1;
    st1=u.iamax(0,val1);
    u.setGlobalVal(st1,0,0.);
    if(onpe0)cout<<"GramMatrix::getLinDependent2states... element val="<<val1<<endl;
    DISTMATDTYPE val2;
    st2=u.iamax(0,val2);
    if(onpe0)cout<<"GramMatrix::getLinDependent2states... element val="<<val2<<endl;

    // look for 2nd largest coefficient with different sign
    while( val1*val2>0. )
    {
        u.setGlobalVal(st2,0,0.);
        st2=u.iamax(0,val2);
        if(onpe0)cout<<"GramMatrix::getLinDependent2states... element val="<<val2<<endl;
    }

    return (double)eigenvalues[0];
}


double GramMatrix::getLinDependent2states(int& st1, int& st2, int& st3)const
{
    vector<DISTMATDTYPE> eigenvalues(dim_);
    dist_matrix::DistMatrix<DISTMATDTYPE> u("u", dim_, dim_);
    // solve a standard symmetric eigenvalue problem
    dist_matrix::DistMatrix<DISTMATDTYPE> mat(*matS_);
    mat.syev('v', 'l', eigenvalues, u);
    // get the index of the two largest components of column 0 of u
    DISTMATDTYPE val1;
    st1=u.iamax(0,val1);
    u.setGlobalVal(st1,0,0.);

    st2=u.iamax(0,val1);
    u.setGlobalVal(st2,0,0.);

    st3=u.iamax(0,val1);
    
   return (double)eigenvalues[0];
}
