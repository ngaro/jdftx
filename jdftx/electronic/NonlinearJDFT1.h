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


#ifndef JDFTX_ELECTRONIC_NONLINEARJDFT1_H
#define JDFTX_ELECTRONIC_NONLINEARJDFT1_H

#include <core/GridInfo.h>
#include <core/DataMultiplet.h>
#include <core/Minimize.h>
#include <electronic/FluidJDFTx.h>
#include <cstdio>

typedef DataMultiplet<DataR,4> DataRMuEps;

//Some extra quantities specific to NonlinearJDFT1 added here
struct NonlinearJDFT1params : public FluidSolverParams
{
	double Nbulk; //!< Bulk density of water molecules (currently a constant = 4.9383e-3)
	double pMol; //!< Dipole moment of water molecule (currently a constant = the SPC/E value)
	double Kdip; //! dipole correlation prefactor (adjusted to obtain bulk dielectric constant for a given temperature)
	double k2factor; //! inverse bulk ionic screening length squared

	//! A copy constructor to set base-class variables
	NonlinearJDFT1params(const FluidSolverParams& p) : FluidSolverParams(p) {}
};



class NonlinearJDFT1 : public FluidSolver, public Minimizable<DataRMuEps>
{
public:
	DataRMuEps state; //!< State of the solver = the legendre multipliers to the polarization field (and ionic concentrations)

	//! See createFluidSolver()
	NonlinearJDFT1(const Everything& e, const FluidSolverParams& params);
	
	bool needsGummel() { return true; }

	//! Set the explicit system charge density and effective cavity-formation electron density:
	void set(const DataGptr& rhoExplicitTilde, const DataGptr& nCavityTilde);

	void loadState(const char* filename); //!< Load state from file
	void saveState(const char* filename) const; //!< Save state to file

	void minimizeFluid(); //!< Converge using nonlinear conjugate gradients

	double get_Adiel_and_grad(DataGptr& grad_rhoExplicitTilde, DataGptr& grad_nCavityTilde); //!< Get the minimized free energy and the n-gradient

	//! Compute gradient and free energy (used for the CG)  (optionally gradient w.r.t nElectronic)
	double operator()( const DataRMuEps& state, DataRMuEps& grad_state, DataGptr* grad_rhoExplicitTilde = 0, DataGptr* grad_nCavityTilde = 0) const;

	// Interface for Minimizable:
	void step(const DataRMuEps& dir, double alpha);
	double compute(DataRMuEps* grad);
	DataRMuEps precondition(const DataRMuEps& in) const;
	
private:
	DataRptr nCavity, shape;
	DataGptr rhoExplicitTilde;
	NonlinearJDFT1params params;
	RealKernel MuKernel;
};


#endif // JDFTX_ELECTRONIC_NONLINEARJDFT1_H
