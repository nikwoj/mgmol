// Copyright (c) 2017, Lawrence Livermore National Security, LLC and
// UT-Battelle, LLC.
// Produced at the Lawrence Livermore National Laboratory and the Oak Ridge
// National Laboratory.
// Written by J.-L. Fattebert, D. Osei-Kuffuor and I.S. Dunn.
// LLNL-CODE-743438
// All rights reserved.
// This file is part of MGmol. For details, see https://github.com/llnl/mgmol.
// Please also read this link https://github.com/llnl/mgmol/LICENSE

#include "VariableSizeMatrix.h"
#include "DataDistribution.h"
#include "MGmol_MPI.h"
#include "Table.h"
#include "mputils.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#ifdef USE_MPI
#include <mpi.h>
#endif

using namespace std;

Timer VariableSizeMatrixInterface::AmultSymBdiag_tm_(
    "VariableSizeMatrix::AmultSymBdiag");
Timer VariableSizeMatrixInterface::AmultSymB_ij_tm_(
    "VariableSizeMatrix::AmultSymB_ij");
Timer VariableSizeMatrixInterface::AmultSymBLocal_tm_(
    "VariableSizeMatrix::AmultSymBLocal");
Timer VariableSizeMatrixInterface::AmultSymB_tm_(
    "VariableSizeMatrix::AmultSymB");
Timer VariableSizeMatrixInterface::initialize_tm_(
    "VariableSizeMatrix::Init_with_squareLocMat");
Timer VariableSizeMatrixInterface::updateRow_tm_(
    "VariableSizeMatrix::updateRows");
Timer VariableSizeMatrixInterface::insertRow_tm_(
    "VariableSizeMatrix::insertNewRow");
Timer VariableSizeMatrixInterface::sort_col_tm_(
    "VariableSizeMatrix::sortMatrixColumns");
/*
   Quicksort of the elements in a from low to high. The elements
   in b are permuted according to the sorted a.
*/
///*
void quickSortIR(int* a, double* b, const int lo, const int hi)
{
    // lo is the lower index, hi is the upper index
    //  of the region of array a that is to be sorted

    int i = lo, j = hi;
    int v;
    int mid = (lo + hi) >> 1;
    int x   = ceil(a[mid]);
    double q;
    //  partition
    do
    {
        while (a[i] < x)
            i++;
        while (a[j] > x)
            j--;
        if (i <= j)
        {
            v    = a[i];
            a[i] = a[j];
            a[j] = v;
            q    = b[i];
            b[i] = b[j];
            b[j] = q;
            i++;
            j--;
        }
    } while (i <= j);
    //  recursion
    if (lo < j) quickSortIR(a, b, lo, j);
    if (i < hi) quickSortIR(a, b, i, hi);
}
//*/
/* Binary search algorithm */
///*
// int binarySearchI(const int * const a, const int key, const int start, const
// int end)
//{
//  // test if array is empty
//  if (end < start)
//    // set is empty, so return value showing not found
//    return -1;
//  else
//  {
//    // calculate midpoint to cut set in half
//    int imid = start + ((end-start)>>1); //(int)floor(double((imax -
//    imin)/2));
//
//      // three-way comparison
//      if (a[imid] > key)
//        // key is in lower subset
//        return binarySearchI(a, key, start, imid-1);
//      else if (a[imid] < key)
//        // key is in upper subset
//        return binarySearchI(a, key, imid+1, end);
//      else
//        // key has been found
//        return imid;
//  }
//}
//*/
template <class T>
VariableSizeMatrix<T>::VariableSizeMatrix(
    const string name, const int alloc_size, const MPI_Comm comm)
    : name_(name)
{
    (void)comm; // comm is currently unused
    // reserve some memory for matrix data
    const int size = MIN_MAT_SIZE >= alloc_size ? MIN_MAT_SIZE : alloc_size;
    data_.reserve(size);

    /* Create table object */
    // const int tsize = 1000 < size ? 1000 : size;
    table_  = new Table(alloc_size);
    n_      = 0;
    nzmax_  = 0;
    totnnz_ = 0;
}

template <class T>
VariableSizeMatrix<T>::~VariableSizeMatrix()
{
    TvecIterator row = data_.begin();
    while (row != data_.end())
    {
        delete *row;
        row++;
    }
    delete table_;
}

template <class T>
VariableSizeMatrix<T>& VariableSizeMatrix<T>::operator=(
    const VariableSizeMatrix<T>& src)
{
    if (this == &src) return *this;

    const int n = src.n();
    copyData(src, n);

    return *this;
}

template <class T>
VariableSizeMatrix<T>::VariableSizeMatrix(
    const VariableSizeMatrix<T>& A, const bool copy_table)
    : name_(A.name_ + "_copy"),
      n_(A.n_),
      nzmax_(A.nzmax_),
      totnnz_(A.totnnz_),
      lvars_(A.lvars_)
{
    const int n = A.n_;
    if (copy_table)
    {
        // const int tsize = 1000 < n ? 1000 : n;
        table_ = new Table(n);
        data_.reserve(n);
        int j = 0;
        for (const_TvecIterator arow = A.data_.begin(); arow != A.data_.end();
             arow++)
        {
            T* new_row = new T(**arow);
            data_.push_back(new_row);
            /* copy table */
            (*table_).insert(lvars_[j]);
            j++;
        }
    }
    else
    {
        /* copy matrix data only */
        table_ = 0;

        data_.reserve(n);
        for (const_TvecIterator arow = A.data_.begin(); arow != A.data_.end();
             arow++)
        {
            T* new_row = new T(**arow);
            data_.push_back(new_row);
        }
    }
    return;
}

template <class T>
template <class T2>
VariableSizeMatrix<T>::VariableSizeMatrix(
    const VariableSizeMatrix<T2>& A, const bool copy_table)
{
    typedef typename std::vector<T2*>::const_iterator const_T2vecIterator;
    n_      = A.n();
    nzmax_  = A.nzmax();
    totnnz_ = A.nnzmat();
    lvars_  = A.lvars();

    const int n = n_;
    if (copy_table)
    {
        // const int tsize = 1000 < n ? 1000 : n;
        table_ = new Table(n);
        data_.reserve(n);
        int j = 0;
        for (int i = 0; i < n; i++) //(const_T2vecIterator arow=A.data_.begin();
                                    //arow!=A.data_.end(); arow++)
        {
            T* new_row = new T(A.getRow(i)); //(**arow);
            data_.push_back(new_row);
            /* copy table */
            (*table_).insert(lvars_[j]);
            j++;
        }
    }
    else
    {
        /* copy matrix data only */
        table_ = 0;

        data_.reserve(n);
        for (int i = 0; i < n; i++) //(const_T2vecIterator arow=A.data_.begin();
                                    //arow!=A.data_.end(); arow++)
        {
            T* new_row = new T(A.getRow(i)); //(**arow);
            data_.push_back(new_row);
        }
    }
    return;
}

/* Copy data from matrix A. Copies m rows of A */
template <class T>
void VariableSizeMatrix<T>::copyData(
    const VariableSizeMatrix<T>& A, const int m)
{
    if (n_ != 0) reset();

    for (int j = 0; j < m; j++)
    {
        T* new_row = new T(*(A.data_[j]));
        data_.push_back(new_row);

        totnnz_ += (int)A.data_[j]->nnz();
    }

    /* copy data vectors */
    lvars_ = A.lvars_;

    /* copy table */
    for (int i = 0; i < m; i++)
        (*table_).insert(lvars_[i]);

    n_ = m;

    return;
}

/* Augment current matrix by inserting a new row
 * NOTE: Zero rows are OK
 */
template <class T>
void VariableSizeMatrix<T>::insertNewRow(const int ncols, const int row,
    const int* cols, const double* vals, const bool append)
{
    //    insertRow_tm_.start();

    /* begin */
    //    const int lrindex = (*table_).get_size();

    if (append == false) return;

    // update
    T* new_row = new T();
    new_row->assign(ncols, cols, vals);
    //#ifdef _OPENMP
    //#pragma omp critical
    //#endif
    //    {
    data_.push_back(new_row);
    /* insert row into table */
    updateTableValue(row);
    /* update nzmax and totnnz */
    totnnz_ += ncols;
    /* update matrix row size */
    n_++;
    /* update local variable index array */
    lvars_.push_back(row);
    //    }

    //    insertRow_tm_.stop();
    return;
}

// Add matrix elements corresponding to subdomains at their right place
// from a square local matrix object.
// Important Note: Neglect contributions smaller than tol!
// (may lead to results dependent on number of CPUs)
template <class T>
void VariableSizeMatrix<T>::initializeMatrixElements(
    const LocalMatrices<MATDTYPE>& ss,
    const vector<vector<int>>& global_indexes, const int numst,
    const double tol)
{
    assert(global_indexes.size() == ss.subdiv());

    short subdiv           = (short)global_indexes.size();
    short chromatic_number = (short)global_indexes[0].size();

    const int n = ss.n();

    int* ist    = new int[2 * subdiv];
    double* val = new double[subdiv];

    // double loop over colors
    initialize_tm_.start();
    //#pragma omp parallel for
    for (int icolor = 0; icolor < chromatic_number; icolor++)
    {

        for (int jcolor = 0; jcolor < n; jcolor++)
        {

            long pst_old = -1;
            int valindex = -1;

            // loop over subdomains
            for (short iloc = 0; iloc < subdiv; iloc++)
            {
                const int st1 = global_indexes[iloc][icolor];
                if (st1 != -1)
                {
                    const int st2 = global_indexes[iloc][jcolor];
                    if (st2 != -1)
                    {

                        // unique id for current pair
                        const long pst = (long)st2 * (long)numst + (long)st1;
                        const MATDTYPE* const ssiloc = ss.getSubMatrix(iloc);
                        const double tmp
                            = ssiloc[jcolor * chromatic_number + icolor];

                        // accumulate values
                        if (fabs(tmp) > tol)
                        {
                            if (pst == pst_old)
                            {
                                assert(valindex >= 0);
                                val[valindex] += tmp;
                            }
                            else
                            {
                                valindex++;
                                val[valindex]         = tmp;
                                ist[2 * valindex]     = st1;
                                ist[2 * valindex + 1] = st2;
                                pst_old               = pst;
                            }
                        }
                    }
                }

            } // iloc

            // assign values
            for (int i = 0; i <= valindex; i++)
                insertMatrixElement(
                    ist[2 * i], ist[2 * i + 1], val[i], ADD, true);
        } // jcolor
    } // icolor

    initialize_tm_.stop();
    delete[] val;
    delete[] ist;
}

/* print a few rows and cols of a square matrix - mainly used for diagnostics */
template <class T>
void VariableSizeMatrix<T>::print(
    ostream& os, const std::vector<int>& locfcns, int n) const
{
    int nrows = n < MAX_PRINT_ROWS ? n : MAX_PRINT_ROWS;
    int ncols = nrows;

    MGmol_MPI& mmpi = *(MGmol_MPI::instance());
    MPI_Comm comm   = mmpi.commSameSpin();
    MPI_Status status;
    int myid;
    MPI_Comm_rank(comm, &myid);
    double* buf = new double[ncols];
    bool found;

    /* begin */
    os << setprecision(5);
#if 1
    for (int row = 0; row < nrows; row++)
    {
        found = false;
        for (std::vector<int>::const_iterator it = locfcns.begin();
             it != locfcns.end(); ++it)
        {
            /* check if row is centered on local PE*/
            if (*it == row)
            {
                /* only one PE sends data */
                found = true;
                for (int col = 0; col < ncols; col++)
                {
                    buf[col] = get_value(row, col);
                }
                if (myid != 0) MPI_Send(buf, ncols, MPI_DOUBLE, 0, myid, comm);
                break;
            }
        }
        if (myid == 0)
        {
            if (!found)
                MPI_Recv(buf, ncols, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG,
                    comm, &status);

            for (int i = 0; i < ncols; i++)
                os << scientific << buf[i] << "\t";

            os << endl;
            os.flush();
        }
        mmpi.barrier();
    }
#endif
    delete[] buf;
    return;
}

/* Print the CSR matrix */
template <class T>
void VariableSizeMatrix<T>::printMatCSR(const char* fname)
{
    FILE* outfile = fopen(fname, "w");

    const int n = n_;
    for (int i = 0; i < n; i++)
    {
        const int nnzrow_i = nnzrow(i);
        for (int j = 0; j < nnzrow_i; j++)
        {
            fprintf(outfile, "%d %d %2.8e \n", lvars_[i],
                data_[i]->getColumnIndex(j), data_[i]->getEntryFromPosition(j));
        }
    }

    fclose(outfile);
}

/* Print 2x2 block*/
template <class T>
void VariableSizeMatrix<T>::printMatBlock2(
    const int gid0, const int gid1, ostream& os)
{
    for (int i = 0; i < n_; i++)
    {
        if (lvars_[i] == gid0 || lvars_[i] == gid1)
        {
            const int nnzrow_i = nnzrow(i);
            for (int j = 0; j < nnzrow_i; j++)
            {
                int jindex = data_[i]->getColumnIndex(j);
                if (jindex == gid0 || jindex == gid1)
                    os << "S(" << lvars_[i] << ","
                       << data_[i]->getColumnIndex(j)
                       << ")=" << data_[i]->getEntryFromPosition(j) << endl;
            }
        }
    }
}

/* Print the MM matrix */
/*
void VariableSizeMatrix<T>::printMatMM(ofstream& outfile)
{


     if( onpe0 )
     {
        os<<n_<<" "<<n_<<" "<<nnzmat()<<endl;
        print(outfile, const std::vector<int>&locfcns,  int nrows=NUM_PRINT_ROWS
)

     return;
}
*/
/* reset the CSR matrix to be reused */
template <class T>
void VariableSizeMatrix<T>::reset()
{
    lvars_.clear();
    n_      = 0;
    totnnz_ = 0;

    /* Reset table object */
    if (table_ != 0) (*table_).reset();

    // reset matrix data
    TvecIterator rowptr = data_.begin();
    while (rowptr != data_.end())
    {
        delete *rowptr;
        rowptr++;
    }
    data_.clear();
}

// clear data, keep size, rows, ...
template <class T>
void VariableSizeMatrix<T>::clear()
{
    totnnz_ = 0;

    TvecIterator rowptr = data_.begin();
    while (rowptr != data_.end())
    {
        (*rowptr)->reset();
        rowptr++;
    }
}

/* reset/ initialize matrix with sparse rows */
template <class T>
void VariableSizeMatrix<T>::setupSparseRows(const std::vector<int>& rows)
{
    int nrows = (int)rows.size();

    /* begin -- first reset if needed */
    if (n_ != 0) reset();

    int i                               = 0;
    std::vector<int>::const_iterator it = rows.begin();
    while (it != rows.end())
    {
        updateTableValue(*it);
        T* new_row = new T();
        data_.push_back(new_row);
        i++;
        it++;
    }

    n_     = nrows;
    lvars_ = rows;

    return;
}

/* set a non-zero matrix to identity */
template <class T>
void VariableSizeMatrix<T>::set2Identity()
{
    assert(n_ > 0);
    assert(n_ == (int)(*table_).get_size());

    totnnz_ = n_;

    TvecIterator rowptr = data_.begin();
    int i               = 0;
    while (rowptr != data_.end())
    {
        (*rowptr)->reset();
        (*rowptr)->insertEntry(lvars_[i], 1.0);

        i++;
        rowptr++;
    }

    return;
}

/* Print select rows of the CSR matrix */
template <class T>
void VariableSizeMatrix<T>::printMat(const char* fname, vector<int>& lvec)
{
    FILE* outfile = fopen(fname, "w");

    const int nrows = (int)lvec.size();
    for (int row = 0; row < nrows; row++)
    {
        int* rindex = (int*)getTableValue(lvec[row]);
        if (rindex == NULL) continue;
        const int i        = *rindex;
        const int nnzrow_i = nnzrow(i);
        for (int j = 0; j < nnzrow_i; j++)
        {
            fprintf(outfile, "%d %d %2.20e \n", lvars_[i],
                data_[i]->getColumnIndex(j), data_[i]->getEntryFromPosition(j));
        }
    }
    fclose(outfile);
}

/* Computes the i-th diagonal entry of the matrix product
 * C = A * B, where A is the current matrix object.
 * Assume B is a symmetric square matrix.
 */
template <class T>
template <typename T2>
double VariableSizeMatrix<T>::AmultSymBdiag(
    VariableSizeMatrix<T2>* B, const int row)
{
    double val = 0.0;

    assert(n_ > 0);

    AmultSymBdiag_tm_.start();

    int* rindex = (int*)getTableValue(row);
    int* cindex = (int*)(*B).getTableValue(row);

    /* return zero if row/col does not exist */
    if ((cindex == NULL) || (rindex == NULL))
    {
        AmultSymBdiag_tm_.stop();
        return 0.;
    }

    const int lcindex        = *cindex;
    const int nzb            = (*B).nnzrow(lcindex);
    const int lrindex        = *rindex;
    const int nnzrow_lrindex = data_[lrindex]->nnz();

    /* Loop over matrix with fewer non-zeros per row */
    const int nzcols = nzb < nnzrow_lrindex ? nzb : nnzrow_lrindex;
    vector<int> colindexes;
    colindexes.reserve(nzcols);
    vector<double> vvals;
    vvals.reserve(nzcols);
    vector<double> bvals;
    bvals.reserve(nzcols);

    if (nzcols == nzb)
    {
        (*B).getColumnIndexes(lcindex, colindexes);
        (*B).getRowEntries(lcindex, bvals);
        getLocalRowValues(lrindex, colindexes, vvals);
    }
    else
    {
        colindexes = data_[lrindex]->getColumnIndexes();
        vvals      = data_[lrindex]->getColumnEntries();
        (*B).getLocalRowValues(lcindex, colindexes, bvals);
    }
    val = MPdot(nzcols, &vvals[0], &bvals[0]);

    AmultSymBdiag_tm_.stop();

    return val;
}

/* Computes the i-j-th entry of the matrix product
 * C = A * B, where A is the current matrix object.
 * Assume B is a symmetric square matrix.
 */
template <class T>
double VariableSizeMatrix<T>::AmultSymB_ij(
    VariableSizeMatrix<T>* B, const int row, const int col, Table& indexT)
{
    double val = 0.0;

    assert(n_ > 0);

    AmultSymB_ij_tm_.start();

    int* rindex = (int*)getTableValue(row);
    int* cindex = (int*)(*B).getTableValue(col);

    /* return zero if row/col does not exist */
    if ((cindex == NULL) || (rindex == NULL))
    {
        AmultSymB_ij_tm_.stop();
        return 0.;
    }

    const int lcindex        = *cindex;
    const int nzb            = (*B).nnzrow(lcindex);
    const int lrindex        = *rindex;
    const int nnzrow_lrindex = data_[lrindex]->nnz();

    /* Loop over matrix with fewer non-zeros per row */
    if (nzb < nnzrow_lrindex)
    {
        val = 0.0;
        //#pragma omp parallel for reduction(+:val)
        for (int k = 0; k < nzb; k++)
        {
            const int jrow = (*B).getColumnIndex(lcindex, k);
            /*
                      const int *pos = (int *)indexT.get_value(jrow);

                      if(pos != NULL)
                         val += data_[lrindex]->getEntryFromPosition(*pos) *
               (*B).getRowEntry(lcindex, k);
            */
            val += data_[lrindex]->getColumnEntry(jrow)
                   * (*B).getRowEntry(lcindex, k);
        }
    }
    else
    {
        val = 0.0;
        //#pragma omp parallel for reduction(+:val)
        for (int k = 0; k < nnzrow_lrindex; k++)
        {
            const int jcol = data_[lrindex]->getColumnIndex(k);

            const int pos = (*B).getColumnPos(lcindex, jcol);
            if (pos != -1)
            {
                val += data_[lrindex]->getEntryFromPosition(k)
                       * (*B).getRowEntry(lcindex, pos);
            }
        }
    }
    AmultSymB_ij_tm_.stop();

    return val;
}

/* compute the trace of selected rows of the matrix */
template <class T>
double VariableSizeMatrix<T>::trace(const std::vector<int>& rows)
{
    double trace = 0.;
    //#pragma omp parallel for reduction(+:trace)
    for (std::vector<int>::const_iterator i = rows.begin(); i != rows.end();
         ++i)
        trace += data_[*i]->getColumnEntry(lvars_[*i]);

    return trace;
}

/* compute trace */
template <class T>
double VariableSizeMatrix<T>::trace()
{
    double trace = 0.;
    const int n  = n_;
    //#pragma omp parallel for reduction(+:trace)
    for (int i = 0; i < n; i++)
        trace += data_[i]->getColumnEntry(lvars_[i]);

    return trace;
}

/* compute sum_i ( ddiag[i]*Mat[i][i] ) */
template <class T>
double VariableSizeMatrix<T>::getTraceDiagProductWithMat(
    const vector<double>& ddiag)
{
    double trace = 0.;
    const int n  = n_;
    //#pragma omp parallel for reduction(+:trace)
    for (int i = 0; i < n; i++)
        trace += ddiag[i] * data_[i]->getColumnEntry(lvars_[i]);

    return trace;
}

template <class T>
void VariableSizeMatrix<T>::copyDataToArray(
    int* locvars, int* colidx, double* colvals)
{
    // copy lvars_
    memcpy(&locvars[0], &lvars_[0], n_ * sizeof(int));
    // copy row data
    for (TvecIterator row = data_.begin(); row != data_.end(); ++row)
    {
        const int nnzrow = (*row)->nnz();
        (*row)->copyDataToArray(colidx, colvals);
        colidx += nnzrow;
        colvals += nnzrow;
    }
}

/* Matrix multiplication:
 * perform the matrix multiplication C = A*B, where A
 * is the current matrix object.
 * Assume B is a symmetric square matrix. Only contributions on rows
 * corresponding to centered functions are computed
 *
 ** Current implementation assumes the rows of the pattern matrix
 ** span the column indexes of matrix A.
 * The pattern of result matrix C matches 'pattern' matrix
 */
template <class T>
void VariableSizeMatrix<T>::AmultSymBLocal(VariableSizeMatrix<T>* B,
    VariableSizeMatrix<T>& C, const std::vector<int>& locfcns,
    VariableSizeMatrix<SparseRowAndTable>& pattern, bool flag)
{
    const int nb = (*B).n();

    assert((int)locfcns.size() >= 0);
    assert(nb > 0);
    AmultSymBLocal_tm_.start();

    /* Loop over rows of this matrix*/
    for (std::vector<int>::const_iterator row = locfcns.begin();
         row != locfcns.end(); ++row)
    {
        int* rindex = (int*)getTableValue(*row);
        const int i = *rindex;
        int* pidx   = (int*)pattern.getTableValue(*row);

        //      #pragma omp parallel for
        for (int j = 0; j < nb; j++)
        {
            const int col = (*B).getLocalVariableGlobalIndex(j);
            const int idx = pattern.getColumnPos(*pidx, col);
            if (flag && (idx == -1)) continue;

            double val       = 0.0;
            const int nnzrow = (*B).nnzrow(j);
            for (int k = 0; k < nnzrow; k++)
            {
                const int jrow = (*B).getColumnIndex(j, k);

                const int* pos = (int*)pattern.getTableValue(
                    jrow); // check if corresponding column entry exists.
                if (pos != NULL)
                    val += data_[i]->getEntryFromPosition(*pos)
                           * (*B).getRowEntry(j, k);
            }
            C.data_[i]->insertEntry(col, val);
            C.totnnz_++;
        }
    }

    AmultSymBLocal_tm_.stop();
    return;
}

/* Matrix multiplication:
 * perform the matrix multiplication C = A*B, where A
 * is the current matrix object.
 * Assume B is a symmetric square matrix.
 * The pattern of result matrix C matches 'pattern' matrix
 */
template <class T>
void VariableSizeMatrix<T>::AmultSymB(VariableSizeMatrix<T>* B,
    VariableSizeMatrix<T>& C, VariableSizeMatrix<SparseRowAndTable>& pattern,
    bool flag)
{
    const int nb = (*B).n();
    assert(nb > 0);

    AmultSymB_tm_.start();

    for (int i = 0; i < n_; i++)
    {
        //#pragma omp parallel for
        for (int j = 0; j < nb; j++)
        {
            const int col = (*B).getLocalVariableGlobalIndex(j);
            int* pidx     = (int*)pattern.getTableValue(lvars_[i]);
            const int idx = pattern.getColumnPos(*pidx, col);
            if (flag && (idx == -1)) continue;

            double val       = 0.0;
            const int nnzrow = (*B).nnzrow(j);
            for (int k = 0; k < nnzrow; k++)
            {
                const int jrow = (*B).getColumnIndex(j, k);
                const int pos  = getColumnPos(i, jrow);
                if (pos != -1)
                {
                    val += data_[i]->getEntryFromPosition(pos)
                           * (*B).getRowEntry(j, k);
                }
            }
            C.data_[i]->insertEntry(col, val);
            C.totnnz_++;
        }
    }

    AmultSymB_tm_.stop();
    return;
}

/* initialize local rows of the local matrix by copying in rows of matrix A */
/* Assumes nnzrow is initially zero - matrix has been reset */
template <class T>
void VariableSizeMatrix<T>::sparsify(const std::vector<int>& pattern)
{
    assert((int)pattern.size() == n_);

    int row      = 0;
    int maxnzrow = 0;
    totnnz_      = 0;
    for (std::vector<int>::const_iterator rp = pattern.begin();
         rp != pattern.end(); ++rp)
    {
        if (*rp)
        {
            maxnzrow
                = maxnzrow > data_[row]->nnz() ? maxnzrow : data_[row]->nnz();
            totnnz_ += data_[row]->nnz();
        }
        else
        {
            data_[row]->reset();
        }
        row++;
    }
    nzmax_ = maxnzrow;

    return;
}

template <class T>
int VariableSizeMatrix<T>::getMaxAbsOffDiagonalRowEntry(
    const int gid, double& value) const
{
    double maxVal = 0.;
    int maxj      = -1;

    bool found = false;

    // loop over rows until row gid is found
    for (int i = 0; i < n_; i++)
    {
        if (lvars_[i] == gid)
        {
            T* data = data_[i];

            // loop over columns
            const int nnzrow_i = nnzrow(i);
            for (int j = 0; j < nnzrow_i; j++)
            {
                int jindex = data->getColumnIndex(j);
                if (jindex != gid) // skip diagonal element
                {
                    double val = fabs(data->getEntryFromPosition(j));
                    if (val > maxVal)
                    {
                        maxVal = val;
                        maxj   = jindex;
                    }
                }
            }
            found = true;
            break;
        }
    }

    assert(found);

    value = maxVal;
    assert(maxj >= 0);
    return maxj;
}

template <class T>
void VariableSizeMatrix<T>::axpy(
    const double alpha, const VariableSizeMatrix<T>& B)
{
    assert(n_ == B.n());
    const int n = B.n();

    for (int i = 0; i < n; i++)
    {
        data_[i]->axpy(B.getRow(i), alpha);
        i++;
    }
}

// perform y:= alpha*this*x + beta*y
template <class T>
void VariableSizeMatrix<T>::gemv(const double alpha,
    const std::vector<double>& x, const double beta, std::vector<double>& y)
{
    assert((int)y.size() == (int)x.size());
    assert((int)y.size() == n_);

    const_TvecIterator row = data_.begin();

    for (int k = 0; row != data_.end(); row++, k++)
    {
        double val = (*row)->dotVec(x);
        y[k]       = beta * y[k] + alpha * val;
    }
}

template <class T>
void VariableSizeMatrix<T>::consolidate(
    const vector<int>& gids, DataDistribution& distributor)
{
    // cout<<"Call VariableSizeMatrix<T>::consolidate()...\n";
    vector<int> pattern(n_, 0);
    for (std::vector<int>::const_iterator it = gids.begin(); it != gids.end();
         ++it)
    {
        const int* rindex = (int*)(table_->get_value(*it));
        pattern[*rindex]  = 1;
    }
    sparsify(pattern);

    // gather/ distribute data from neighbors whose centered functions
    // overlap with functions centered on local subdomain
    distributor.updateLocalRows(*this, true);
}

template class VariableSizeMatrix<SparseRow>;
template class VariableSizeMatrix<SparseRowAndTable>;
template VariableSizeMatrix<SparseRow>::VariableSizeMatrix(
    const VariableSizeMatrix<SparseRowAndTable>& A, const bool copy_table);
template VariableSizeMatrix<SparseRowAndTable>::VariableSizeMatrix(
    const VariableSizeMatrix<SparseRow>& A, const bool copy_table);
template double VariableSizeMatrix<SparseRow>::AmultSymBdiag(
    VariableSizeMatrix<SparseRow>* B, const int row);
template double VariableSizeMatrix<SparseRow>::AmultSymBdiag(
    VariableSizeMatrix<SparseRowAndTable>* B, const int row);
// template double
// VariableSizeMatrix<SparseRow>::AmultSymB_ij(VariableSizeMatrix<SparseRowAndTable>
// *B, const int row, const int col, Table& indexT);
