#include "Hamiltonian.h"
#include "ProjectedMatricesInterface.h"
#include "MGmol.h"
#include "Ions.h"
#include "OrbitalsStepper.h"

#include <iostream>

template <class T>
class GrassmanCGFactory
{
public:
    static OrbitalsStepper<T>* create(Hamiltonian<T>* hamiltonian,
        ProjectedMatricesInterface* proj_matrices, MGmol<T>* mgmol_strategy,
        Ions& ions, std::ostream& os, const bool short_sighted);

};

