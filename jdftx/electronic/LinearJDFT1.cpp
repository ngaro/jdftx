/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman, Kendra Letchworth Weaver

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

#include <electronic/Everything.h>
#include <electronic/LinearJDFT1.h>
#include <electronic/JDFT1_shapeFunc.h>
#include <core/DataIO.h>
#include <core/DataMultiplet.h>
#include <core/Thread.h>

//----------------------- The JDFT `shape function' and gradient ------------------

void JDFT1_shapeFunc(const DataRptr& nCavity, DataRptr& shape, const double nc, const double sigma)
{
	nullToZero(shape, nCavity->gInfo);
	#ifdef GPU_ENABLED
	JDFT1_shapeFunc_gpu(nCavity->gInfo.nr, nCavity->dataGpu(), shape->dataGpu(), nc, sigma);
	#else
	threadedLoop(JDFT1_shapeFunc_sub, nCavity->gInfo.nr, nCavity->data(), shape->data(), nc, sigma);
	#endif
}

void JDFT1_shapeFunc_grad(const DataRptr& nCavity, const DataRptr& grad_shape, DataRptr& grad_nCavity, const double nc, const double sigma)
{
	nullToZero(grad_nCavity, nCavity->gInfo);
	#ifdef GPU_ENABLED
	JDFT1_shapeFunc_grad_gpu(nCavity->gInfo.nr, nCavity->dataGpu(), grad_shape->dataGpu(), grad_nCavity->dataGpu(), nc, sigma);
	#else
	threadedLoop(JDFT1_shapeFunc_grad_sub, nCavity->gInfo.nr, nCavity->data(), grad_shape->data(), grad_nCavity->data(), nc, sigma);
	#endif
}



//------------------ Linear JDFT1 solver interface -----------------------

LinearJDFT1::LinearJDFT1(const Everything& e, const FluidSolverParams& fsp)
: FluidSolver(e), params(fsp), Kkernel(e.gInfo)
{	//Initialize extra parameters:
	params.k2factor = (8*M_PI/params.T) * params.ionicConcentration * pow(params.ionicZelectrolyte,2);
}

DataGptr LinearJDFT1::hessian(const DataGptr& phiTilde)
{	DataGptr rhoTilde = divergence(J(epsilon * I(gradient(phiTilde)))); //poisson term
	if(kappaSq) rhoTilde -= J(kappaSq*I(phiTilde)); // screening term
	return rhoTilde;
}

DataGptr LinearJDFT1::precondition(const DataGptr& rTilde)
{	return Kkernel*(J(epsInv*I(Kkernel*rTilde)));
}

//Initialize Kkernel to square-root of the inverse kinetic operator
inline void setPreconditionerKernel(int i, double G2, double* Kkernel, double kRMS)
{	if(i==0) Kkernel[i] = kRMS ? 1.0/kRMS : 0.0;
	else Kkernel[i] = 1.0/sqrt(G2 + pow(kRMS,2));
}

void LinearJDFT1::set(const DataGptr& rhoExplicitTilde, const DataGptr& nCavityTilde)
{
	this->rhoExplicitTilde = clone(rhoExplicitTilde); zeroNyquist(this->rhoExplicitTilde);
	this->nCavity = I(nCavityTilde);

	//Compute cavity shape function (0 to 1)
	DataRptr shape(DataR::alloc(e.gInfo,isGpuEnabled()));
	JDFT1_shapeFunc(nCavity, shape, params.nc, params.sigma);
	
	//Compute epsilon and kappaSq:
	epsilon = 1 + (params.epsilonBulk-1)*shape;
	kappaSq = params.ionicConcentration ? params.k2factor*shape : 0; //set kappaSq to null pointer if no screening

	//Info:
	logPrintf("\tLinear fluid (dielectric constant: %g", params.epsilonBulk);
	if(params.ionicConcentration) logPrintf(", screening length: %g Bohr", sqrt(params.epsilonBulk/params.k2factor));
	logPrintf(") occupying %lf of unit cell:", integral(shape)/e.gInfo.detR); logFlush();

	//Update the preconditioner
	epsInv = inv(epsilon);
	double kRMS = (kappaSq ? sqrt(sum(kappaSq)/sum(epsilon)) : 0.0);
	applyFuncGsq(Kkernel.gInfo, setPreconditionerKernel, Kkernel.data, kRMS);
	Kkernel.set();
	
	//Initialize the state if it hasn't been loaded:
	if(!state) nullToZero(state, e.gInfo);
}


void LinearJDFT1::minimizeFluid()
{
	fprintf(e.fluidMinParams.fpLog, "\n\tWill stop at %d iterations, or sqrt(|r.z|)<%le\n", e.fluidMinParams.nIterations, e.fluidMinParams.knormThreshold);
	int nIter = solve((-4*M_PI)*rhoExplicitTilde, e.fluidMinParams);
	logPrintf("\tCompleted after %d iterations.\n", nIter);
}

double LinearJDFT1::get_Adiel_and_grad(DataGptr& grad_rhoExplicitTilde, DataGptr& grad_nCavityTilde)
{
	DataGptr& phi = state; // that's what we solved for in minimize

	//The "electrostatic" gradient is the potential due to the bound charge alone:
	grad_rhoExplicitTilde = phi - (-4*M_PI)*Linv(O(rhoExplicitTilde));

	//The "cavity" gradient is computed by chain rule via the gradient w.r.t to the shape function:
	DataRptrVec gradPhi = I(gradient(phi));
	DataRptr gradPhiSq = gradPhi[0]*gradPhi[0] + gradPhi[1]*gradPhi[1] + gradPhi[2]*gradPhi[2];
	DataRptr grad_shape = (-(params.epsilonBulk-1)/(8*M_PI)) * gradPhiSq; //dielectric part
	if(params.ionicConcentration)
	{	DataRptr Iphi = I(phi); //potential in real space
		grad_shape += (params.k2factor/(8*M_PI)) * (Iphi*Iphi); //screening part
	}
	DataRptr grad_nCavity(DataR::alloc(e.gInfo));
	JDFT1_shapeFunc_grad(nCavity, grad_shape, grad_nCavity, params.nc, params.sigma);
	grad_nCavityTilde = J(grad_nCavity);

	//Compute and return A_diel:
	return 0.5*dot(grad_rhoExplicitTilde, O(rhoExplicitTilde));
}

void LinearJDFT1::loadState(const char* filename)
{	DataRptr Istate(DataR::alloc(e.gInfo));
	loadRawBinary(Istate, filename); //saved data is in real space
	state = J(Istate);
}

void LinearJDFT1::saveState(const char* filename) const
{	saveRawBinary(I(state), filename); //saved data is in real space
}

void LinearJDFT1::dumpDensities(const char* filenamePattern) const
{	string filename(filenamePattern);
	filename.replace(filename.find("%s"), 2, "Epsilon");
	logPrintf("Dumping '%s'... ", filename.c_str());  logFlush();
	saveRawBinary(epsilon, filename.c_str());
	logPrintf("done.\n"); logFlush();
}

