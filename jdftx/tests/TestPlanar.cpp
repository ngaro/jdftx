/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#include <fluid/FluidMixture.h>
#include <fluid/IdealGasPsiAlpha.h>
#include <fluid/IdealGasPomega.h>
#include <fluid/Fex_H2O_ScalarEOS.h>

void setPhi(int i, vector3<> r, double* phiApplied, double* phiWall,
	double gridLength, double Dfield, double zWall)
{
	register double zEff;
	if(r[2]<0.5*gridLength) zEff = r[2] - 0.25*gridLength;
	else zEff = 0.75*gridLength - r[2];
	phiApplied[i] = -Dfield * zEff;
	phiWall[i] = ((fabs(zEff)>0.25*gridLength-zWall) ? 1.0 : 0.0);
}

int main(int argc, char** argv)
{	initSystem(argc, argv);

	//Parse command-line
	S2quadType quadType = QuadEuler;
	int nBeta = 12;
	if(argc > 1)
	{	if(!S2quadTypeMap.getEnum(argv[1], quadType))
			die("<quad> must be one of %s\n", S2quadTypeMap.optionList().c_str());
		if(quadType==QuadEuler)
		{	if(argc < 3) die("<nBeta> must be specified for Euler quadratures.\n")
			nBeta = atoi(argv[2]);
			if(nBeta <= 0) die("<nBeta> must be non-negative.")
		}
	}
	
	//Setup simulation grid:
	GridInfo gInfo;
	gInfo.S = vector3<int>(1, 1, 4096);
	const double hGrid = 0.0625;
	gInfo.R = Diag(hGrid * gInfo.S);
	gInfo.initialize();

	//Setup fluid:
	SO3quad quad(quadType, 2, nBeta);
	//TranslationOperatorFourier trans(gInfo);
	TranslationOperatorSpline trans(gInfo, TranslationOperatorSpline::Linear);
	FluidMixture fluidMixture(gInfo, 298*Kelvin);
	Fex_H2O_ScalarEOS fex(fluidMixture);
	IdealGasPomega idgas(&fex, 1.0, quad, trans);
	double p = 1.01325*Bar;
	printf("pV = %le\n", p*gInfo.detR);
	fluidMixture.setPressure(p);

	//Initialize external potential (repel O from a cube)
	double Dfield = 1.0 * eV/Angstrom;
	const double zWall = 8.0 - 1e-3;
	const double& gridLength = gInfo.R(2,2);
	DataRptr phiApplied(DataR::alloc(gInfo)), phiWall(DataR::alloc(gInfo));
	applyFunc_r(gInfo, setPhi, phiApplied->data(), phiWall->data(), gridLength, Dfield, zWall);
	const double ZO = fex.getMolecule()->site[0].prop->chargeZ;
	idgas.V[0] = ZO * phiApplied + phiWall;
	idgas.V[1] = -0.5*ZO * phiApplied + phiWall;

	//----- Initialize state -----
	fluidMixture.initState(0.01);

	//----- FDtest and CG -----
	MinimizeParams mp;
	mp.alphaTstart = 3e4;
	mp.nDim = gInfo.nr * fluidMixture.get_nIndep();
	mp.energyLabel = "Phi";
	mp.nIterations=1500;
	mp.energyDiffThreshold=1e-16;
	
	fluidMixture.minimize(mp);
	
	//------ Outputs ---------
	ostringstream quadName;
	quadName << S2quadTypeMap.getString(quadType);
	if(quadType == QuadEuler) quadName << nBeta;
	
	DataRptrCollection N;
	fluidMixture.getFreeEnergy(FluidMixture::Outputs(&N));
	FILE* fp = fopen((quadName.str()+".Nplanar").c_str(), "w");
	double* NOdata = N[0]->data();
	double* NHdata = N[1]->data();
	double nlInv = 1./idgas.get_Nbulk();
	for(int i=0; i<gInfo.S[2]/2; i++)
		fprintf(fp, "%le\t%le\t%le\n", i*hGrid, nlInv*NOdata[i], 0.5*nlInv*NHdata[i]);
	fclose(fp);
	
	return 0;
}
