#include <vector>
#include <array>
#include <cmath>
#include "superSamplingClass.h"

double sincFunction(double x) { return (std::abs(x)<1e-14 ? 1 : std::sin(x*pi)/(x*pi));}

std::array<double,3> computeLowGridBd(
        const std::array<double,3> center,
        const std::array<double,3> gridSpace, 
        const double radius) {
    std::array<double,3> lowGridBd;
    int nX = std::round(radius / gridSpace[0]);
    int nY = std::round(radius / gridSpace[1]);
    int nZ = std::round(radius / gridSpace[2]);
    lowGridBd[0] = center[0] - nX*gridSpace[0];
    lowGridBd[1] = center[1] - nY*gridSpace[1];
    lowGridBd[2] = center[2] - nZ*gridSpace[2];
    return lowGridBd;
}


std::vector<double> computeWeights(
                const int numExtraPts,
                const std::array<double,3> fineGridSpace,
                const std::array<double,3> coarGridSpace,
                double filter(double)) {
    int offset=0;
    int convNumPts = 2*numExtraPts + 1;
    std::vector<double> weights(convNumPts*convNumPts*convNumPts);
    //std::array<double,3> lowGridBd = computeLowGridBd(
    //                {0.,0.,0.}, fineGridSpace, cutOffRadius);
    std::array<double,3> lowGridBd={-numExtraPts*fineGridSpace[0],
                                    -numExtraPts*fineGridSpace[1],
                                    -numExtraPts*fineGridSpace[2]};
    
    double idx=lowGridBd[0];
    for(int i=0; i < convNumPts; ++i) {
        double idy=lowGridBd[1];
        double filterX = filter(idx / coarGridSpace[0]);
        
        for(int j=0; j < convNumPts; ++j) {
            double idz=lowGridBd[2];
            double filterY = filter(idy / coarGridSpace[1]);
            
            for(int k=0; k < convNumPts; ++k) {
                double filterZ = filter(idz / coarGridSpace[2]);
                weights[offset] = filterX * filterY * filterZ;
                idz += fineGridSpace[2];
                offset++;
            }
            idy += fineGridSpace[1];
        }
        idx += fineGridSpace[0];
    }
    return weights;
}

template <int lMax>
void superSampling<lMax>::setup(
            int sampleRate,
            int numExtraPts,
            std::array<double,3> coarGridSpace,
            double filter(double)
            ) {
    sampleRate_ = sampleRate;
    //cutOffRadius_ = cutOffRadius;
    numExtraPts_ = numExtraPts;
    coarGridSpace_ = coarGridSpace;
    fineGridSpace_[0] = coarGridSpace_[0] / sampleRate_;
    fineGridSpace_[1] = coarGridSpace_[1] / sampleRate_;
    fineGridSpace_[2] = coarGridSpace_[2] / sampleRate_;
    //int convNumPts = 2 * std::round(cutOffRadius_ / fineGridSpace_[0]) + 1;
    int convNumPts = 2 * numExtraPts + 1;
    weights_ = computeWeights(
                    numExtraPts,
                    fineGridSpace_,
                    coarGridSpace_,
                    filter);
}
 
template <int lMax>
std::array<std::vector<double>,2*lMax + 1> superSampling<lMax>::getFuncValues(std::function<double(const double)> Func) {
//std::array<std::vector<double>,2*lMax + 1> superSampling<lMax>::getFuncValues(const RadialInter& objectFunc) {
    int offset = 0;
    std::array<std::vector<double>,2*lMax + 1> FuncValues;
    for(int ll=0; ll<2*lMax+1; ++ll) {
            FuncValues[ll].resize(funcNumPts_[0]*funcNumPts_[1]*funcNumPts_[2]);
    }
    //int extraPoints = std::round(cutOffRadius_/fineGridSpace_[0]);
    double idx = botMeshCorner_[0] - numExtraPts_*fineGridSpace_[0] - atomicCenter_[0];
    //double idx = atomicCenter_[0] - (botMeshCorner_[0] - extraPoints*fineGridSpace_[0]);

    for(int i=0; i<funcNumPts_[0]; ++i) {
        double dx2 = idx*idx;
        double idy = botMeshCorner_[1] - numExtraPts_*fineGridSpace_[1] - atomicCenter_[1];
        
        for(int j=0; j<funcNumPts_[1]; ++j) {
            double dy2 = idy*idy;
            double idz = botMeshCorner_[2] - numExtraPts_*fineGridSpace_[2] - atomicCenter_[2];
            
            for(int k=0; k<funcNumPts_[2]; ++k) {
                double dz2 = idz*idz;
                double radius = std::sqrt(dx2 + dy2 + dz2);
                double currentFuncValue = Func(radius);
                //double currentFuncValue = objectFunc.cubint(radius);
                if(harmonics_) {
                    if(lMax==0) {
                        FuncValues[0][offset] = setSProjector(
                                            currentFuncValue,
                                            constLMAX0_);
                    } if(lMax==1) {
                        std::array<double,3> values = setPProjector(
                                            currentFuncValue,
                                            radius,
                                            constLMAX1_,
                                            {idx,idy,idz});
                        FuncValues[0][offset] = values[0];
                        FuncValues[1][offset] = values[1];
                        FuncValues[2][offset] = values[2];
                    } if(lMax==2) {
                        std::array<double,5> values = setDProjector(
                                            currentFuncValue,
                                            radius,
                                            constLMAX2_,
                                            {idx,idy,idz},
                                            {dx2,dy2,dz2});
                        FuncValues[0][offset] = values[0];
                        FuncValues[1][offset] = values[1];
                        FuncValues[2][offset] = values[2];
                        FuncValues[3][offset] = values[3];
                        FuncValues[4][offset] = values[4];
                    } if(lMax==3) {
                        std::array<double,7> values = setFProjector(
                                            currentFuncValue,
                                            radius,
                                            constLMAX3_,
                                            {idx,idy,idz},
                                            {dx2,dy2,dz2});
                        FuncValues[0][offset] = values[0];
                        FuncValues[1][offset] = values[1];
                        FuncValues[2][offset] = values[2];
                        FuncValues[3][offset] = values[3];
                        FuncValues[4][offset] = values[4];
                        FuncValues[5][offset] = values[5];
                        FuncValues[6][offset] = values[6];
                    }
                } else {
                    FuncValues[0][offset] = currentFuncValue;
                }
                
                offset++;
                idz += fineGridSpace_[2];
            }
            idy += fineGridSpace_[1];
        }
        idx += fineGridSpace_[0];
    }
    return FuncValues;
}

double setSProjector(
                const double currentValue,
                const double constant) {
    return currentValue * constant;
}

std::array<double,3> setPProjector(
                const double currentValue,
                const double radius,
                const double constant,
                const std::array<double,3> xyz) {
    const double cL1 = currentValue * constant / radius;
    return {cL1*xyz[1], cL1*xyz[2], cL1*xyz[0]};
}

std::array<double,5> setDProjector(
                const double currentValue,
                const double radius,
                const std::array<double,3> constants,
                const std::array<double,3> xyz,
                const std::array<double,3> xyz2) {
    const double r2 = radius*radius;
    const double cL2_0 = currentValue * constants[0] / r2;
    const double cL2_1 = currentValue * constants[1] / r2;
    const double cL2_2 = currentValue * constants[2] / r2;
    double firstValue = cL2_0*xyz[0]*xyz[1];
    double seconValue = cL2_0*xyz[1]*xyz[2];
    double thirdValue = cL2_1*(-xyz2[0] - xyz2[1] + 2*xyz2[2]);
    double fourtValue = cL2_0*xyz[2]*xyz[0];
    double fifthValue = cL2_2*(xyz2[0] - xyz2[1]);
    return {firstValue,seconValue,thirdValue,fourtValue,fifthValue};
}

std::array<double,7> setFProjector(
                const double currentValue,
                const double radius,
                const std::array<double,5> constants,
                const std::array<double,3> xyz,
                const std::array<double,3> xyz2) {
    const double r3 = radius*radius*radius;
    const double cL3_0 = currentValue * constants[0] / r3;
    const double cL3_1 = currentValue * constants[1] / r3;
    const double cL3_2 = currentValue * constants[2] / r3;
    const double cL3_3 = currentValue * constants[3] / r3;
    const double cL3_4 = currentValue * constants[4] / r3;
    double firstValue = cL3_0 * (3.0 * xyz2[0] - xyz2[1]) * xyz[1];
    double seconValue = cL3_1 * xyz[0]*xyz[1]*xyz[2];
    double thirdValue = cL3_2 * (-xyz2[0] - xyz2[1] + 5*xyz2[2]) * xyz[1];
    double fourtValue = cL3_3 * (-3.0 * xyz2[0] - 3*xyz2[1] + 5*xyz2[2]) * xyz[2];
    double fifthValue = cL3_2 * (-xyz2[0] - xyz2[1] + 5*xyz2[2])*xyz[0];
    double sixthValue = cL3_4 * (xyz2[0] - xyz2[1]) * xyz[2];
    double sevenValue = cL3_0 * (xyz2[0] - 3.0 * xyz2[1]) * xyz[0];
    return {firstValue,seconValue,thirdValue,fourtValue,fifthValue,sixthValue,sevenValue};
}



template <int lMax>
std::vector<double> superSampling<lMax>::computeSuperSampling(
                const std::vector<double>& fineMeshFuncValues
        ) {
    int offset = 0;
    std::vector<double> superSampled(coarNumPts_[0]*coarNumPts_[1]*coarNumPts_[2]);
    
    for(int i=0; i < coarNumPts_[0]; ++i) {
        for(int j=0; j < coarNumPts_[1]; ++j) {
            for(int k=0; k < coarNumPts_[2]; ++k) {
                superSampled[offset] = superSampling<lMax>::computeFiltering(
                                {i,j,k},
                                fineMeshFuncValues);
                offset += 1;
            }
        }
    }
    return superSampled;
}

template <int lMax>
double superSampling<lMax>::computeFiltering(
        const std::array<int,3> sampleIndex,
        const std::vector<double>& fineMeshFuncValues
        ) {
    double samplePoint=0;
    int offset = 0;
    
    int funcOffsetX = sampleIndex[0]*funcNumPts_[1]*funcNumPts_[2]*sampleRate_;
    for(int i=0; i < convNumPts_; ++i) {
        int funcOffsetY = sampleIndex[1]*funcNumPts_[2]*sampleRate_;
        for(int j=0; j < convNumPts_; ++j) {
            int funcOffsetZ = sampleIndex[2]*sampleRate_;
            for(int k=0; k < convNumPts_; ++k) {
                samplePoint += fineMeshFuncValues[funcOffsetX + funcOffsetY + funcOffsetZ] * weights_[offset];
                offset++;
                funcOffsetZ++;
            }
            funcOffsetY += funcNumPts_[2];
        }
        funcOffsetX += funcNumPts_[1]*funcNumPts_[2];
    }
    return samplePoint / (sampleRate_*sampleRate_*sampleRate_);
}


