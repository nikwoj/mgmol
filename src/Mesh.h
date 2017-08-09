// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. 
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved. 
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE

// $Id$
#ifndef MESH_H
#define MESH_H

#include "GridFactory.h"
#include "PEenv.h"
#include "Control.h"
#include <ostream>
#include <cassert>

#ifdef USE_MPI
#include <mpi.h>
#endif

// Main Mesh structure
class Mesh
{
    static Mesh* pinstance_;
    
    static MPI_Comm comm_;
    
    static int    ngpts_[3];
    static double origin_[3];
    static double lattice_[3];
    static int    lap_type_;

    // nb. subdivision of grid in direction 0
    static int subdivx_;
    static int numpt_;
    static int loc_numpt_;

    pb::Grid*  myGrid_;
    pb::PEenv* myPEenv_;

    Mesh()
    {
        myPEenv_ = new pb::PEenv(comm_,
                                 ngpts_[0],ngpts_[1],ngpts_[2], 1);

        myGrid_  = GridFactory::createGrid(
                      ngpts_,origin_,lattice_,
                      lap_type_,false,*myPEenv_);
        numpt_   = myGrid_->size();
        loc_numpt_ = numpt_;
    };
    
    ~Mesh()
    {
        delete myGrid_;
        delete myPEenv_;
    };
    Mesh(const Mesh&){};
    
public:
    static Mesh* instance()
    {
        assert( lap_type_>=0 );
        if( pinstance_==0 )
        {
            pinstance_ = new Mesh();
        }
        return pinstance_;
    }
    
    static void setup(MPI_Comm comm,
               const int ngpts[3],
               const double origin[3], 
               const double lattice[3], 
               const int lap_type)
    {
        assert( pinstance_==NULL );
        assert( ngpts[0]>0 );
        assert( ngpts[1]>0 );
        assert( ngpts[2]>0 );
        assert( lattice[0]>1.e-3 );
        assert( lattice[1]>1.e-3 );
        assert( lattice[2]>1.e-3 );
        assert( lap_type>=0 );
        if( !( lap_type==0
            || lap_type==1
            || lap_type==2
            || lap_type==3
            || lap_type==4
            || lap_type==10 ) ){
            (*MPIdata::sout)<<"Mesh::setup() -> Invalid Laplacian type"<<std::endl;
        }
        comm_=comm;
        for(int i=0;i<3;i++){
            ngpts_[i]  =ngpts[i];
            origin_[i] =origin[i];
            lattice_[i]=lattice[i];
        }
        lap_type_=lap_type;
    }
    
    void print(std::ostream&)const;

    void subdivGridx(const int nlevels=1);
    
    const pb::Grid& grid()const
    {
        return *myGrid_;
    }
    const pb::PEenv& peenv()const
    {
        return *myPEenv_;
    }
    int subdivx()const
    {
        return subdivx_;
    }
    int numpt()const
    {
        assert( numpt_>0 );
        assert( numpt_<1000000000 );
        return numpt_;
    }
    int locNumpt()const
    {
        assert( loc_numpt_>0 );
        assert( numpt_<1000000000 );
        return loc_numpt_;
    }
};

#endif