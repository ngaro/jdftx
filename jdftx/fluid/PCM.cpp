/*-------------------------------------------------------------------
Copyright 2013 Ravishankar Sundararaman, Deniz Gunceler

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

#include <fluid/PCM.h>
#include <fluid/PCM_internal.h>
#include <electronic/Everything.h>
#include <electronic/SphericalHarmonics.h>
#include <electronic/operators.h>
#include <electronic/VanDerWaals.h>
#include <core/DataMultiplet.h>
#include <core/DataIO.h>
#include <core/Units.h>

double wExpand_calc(double G, double R)
{	return (2./3)*(bessel_jl(0, G*R) + bessel_jl(2, G*R)); //corresponds to theta(R-r)/(2*pi*R^3)
}

double wCavity_calc(double G, double d)
{	return bessel_jl(0, G*d); //corresponds to delta(d-r)
}

//Spherically-averaged structure factor
double Sf_calc(double G, const std::vector<double>* rArr)
{	double Sf = 0.;
	for(double r: *rArr) Sf += bessel_jl(0, G*r);
	return Sf;
}


PCM::PCM(const Everything& e, const FluidSolverParams& fsp): FluidSolver(e,fsp)
{
	if(fsp.solvents.size() < 1) die("PCMs require exactly one solvent component - none specified.\n");
	if(fsp.solvents.size() > 1) die("PCMs require exactly one solvent component - more than one specified.\n");
	const auto& solvent = fsp.solvents[0];
	const double dG = 0.02;
	
	//Print common info and add relevant citations:
	logPrintf("   Cavity determined by nc: %lg and sigma: %lg\n", fsp.nc, fsp.sigma);
	switch(fsp.pcmVariant)
	{	case PCM_SaLSA:
		case PCM_Nonlocal: //Nonlocal PCMs
		case PCM_SGA13: //and local PCM that uses weighted-density cavitation+dispersion
		{	if(fsp.pcmVariant==PCM_SaLSA)
				Citations::add("Spherically-averaged liquid susceptibility ansatz (SaLSA) nonlocal fluid model",
					"R. Sundararaman, K.A. Schwarz, K. Letchworth-Weaver, D. Gunceler, and T.A. Arias, (under preparation)");
			else
			{	Citations::add("Linear/nonlinear dielectric/ionic fluid model with weighted-density cavitation and dispersion",
					"R. Sundararaman, D. Gunceler, and T.A. Arias, (under preparation)");
				Rex[0] = solvent->Rvdw - solvent->Res;
				Rex[1] = solvent->Rvdw;
				logPrintf("   Electrostatic cavity expanded by Rvdw-Res: %lg bohr, and cavitation/dispersion cavity by Rvdw: %lg bohr.\n", Rex[0], Rex[1]);
				//Initialize cavity expansion weight functions:
				for(int i=0; i<2; i++)
					wExpand[i].init(0, dG, e.gInfo.GmaxGrid, wExpand_calc, Rex[i]);
			}
			wCavity.init(0, dG, e.gInfo.GmaxGrid, wCavity_calc, 2.*solvent->Rvdw); //Initialize nonlocal cavitation weight function
			logPrintf("   Weighted density cavitation model constrained by Nbulk: %lg bohr^-3, Pvap: %lg kPa, Rvdw: %lg bohr and sigmaBulk: %lg Eh/bohr^2 at T: %lg K.\n", solvent->Nbulk, solvent->Pvap/KPascal, solvent->Rvdw, solvent->sigmaBulk, fsp.T/Kelvin);
			logPrintf("   Weighted density dispersion model using vdW pair potentials.\n");
			//Initialize structure factors for dispersion:
			if(fsp.pcmVariant == PCM_Nonlocal)
			{	Sf.resize(1);  //simplified model: use single site rather than explicit molecule geometry
				//TODO: Init Sf functional form
				atomicNumbers.assign(1, VanDerWaals::unitParticle);
			}
			else
			{	if(!solvent->molecule.sites.size()) die("Nonlocal dispersion model requires solvent molecule geometry, which is not yet implemented for selected solvent\n");
				Sf.resize(solvent->molecule.sites.size());
				atomicNumbers.resize(solvent->molecule.sites.size());
				for(unsigned i=0; i<Sf.size(); i++)
				{	std::vector<double> r; //radial distances of solvent sites from center
					for(vector3<> pos: solvent->molecule.sites[i]->positions) r.push_back(pos.length());
					Sf[i].init(0, dG, e.gInfo.GmaxGrid, Sf_calc, &r);
					atomicNumbers[i] = solvent->molecule.sites[i]->atomicNumber;
				}
			}
			vdwForces = std::make_shared<IonicGradient>();
			break;
		}
		case PCM_SG14:
		case PCM_SG14tau:
		case PCM_SG14tauVW:
		{	wCavity.init(0, dG, e.gInfo.GmaxGrid, wCavity_calc, 2.*solvent->Rvdw); //Initialize nonlocal cavitation weight function
			logPrintf("   Effective weighted-cavity tension: %lg Eh/molecule with Rvdw: %lg bohr to account for cavitation and dispersion.\n", fsp.cavityTension, solvent->Rvdw);
			break;
		}
		case PCM_GLSSA13:
		{	Citations::add("Linear/nonlinear dielectric/ionic fluid model with effective cavity tension",
				"D. Gunceler, K. Letchworth-Weaver, R. Sundararaman, K.A. Schwarz and T.A. Arias, Modelling Simul. Mater. Sci. Eng. 21 074005 (2013)");
			logPrintf("   Effective cavity tension: %lg Eh/bohr^2 to account for cavitation and dispersion.\n", fsp.cavityTension);
			break;
		}
		case PCM_LA12:
		case PCM_PRA05:
		{	if(k2factor)
				Citations::add("Linear dielectric fluid model with ionic screening",
					"K. Letchworth-Weaver and T.A. Arias, Phys. Rev. B 86, 075140 (2012)");
			else
				Citations::add("Linear dielectric fluid model",
					"S.A. Petrosyan SA, A.A. Rigos and T.A. Arias, J Phys Chem B. 109, 15436 (2005)");
			logPrintf("   No cavitation model.\n");
			break;
		}
	}
}

PCM::~PCM()
{	for(int i=0; i<2; i++) wExpand[i].free();
	wCavity.free();
	for(unsigned i=0; i<Sf.size(); i++) Sf[i].free();
}

void PCM::updateCavity()
{
	//Cavities from expanded densities for SGA13 variant:
	if(fsp.pcmVariant == PCM_SGA13)
	{	DataRptr* shapeEx[2] = { &shape, &shapeVdw };
		for(int i=0; i<2; i++)
		{	ShapeFunction::expandDensity(wExpand[i], Rex[i], nCavity, nCavityEx[i]);
			ShapeFunction::compute(nCavityEx[i], *(shapeEx[i]), fsp.nc, fsp.sigma);
		}
	}
	else if(fsp.pcmVariant == PCM_SG14tauVW)
	{	ShapeFunction::tauVW(nCavity, tauCavity);
		ShapeFunction::compute(tauCavity, shape, fsp.nc, fsp.sigma);
	}
	else //Compute directly from nCavity (which is a density product for SaLSA):
		ShapeFunction::compute(nCavity, shape, fsp.nc, fsp.sigma);
	
	//Compute and cache cavitation energy and gradients:
	const auto& solvent = fsp.solvents[0];
	switch(fsp.pcmVariant)
	{	case PCM_SaLSA:
		case PCM_Nonlocal:
		case PCM_SGA13:
		{	//Select relevant shape function:
			const DataGptr sTilde = J(fsp.pcmVariant==PCM_SGA13 ? shapeVdw : shape);
			DataGptr A_sTilde;
			//Cavitation:
			const double nlT = solvent->Nbulk * fsp.T;
			const double Gamma = log(nlT/solvent->Pvap) - 1.;
			const double Cp = 15. * (solvent->sigmaBulk/(2*solvent->Rvdw * nlT) - (1+Gamma)/6);
			const double coeff2 = 1. + Cp - 2.*Gamma;
			const double coeff3 = Gamma - 1. -2.*Cp;
			DataRptr sbar = I(wCavity*sTilde);
			Adiel["Cavitation"] = nlT * integral(sbar*(Gamma + sbar*(coeff2 + sbar*(coeff3 + sbar*Cp))));
			A_sTilde += wCavity*Idag(nlT * (Gamma + sbar*(2.*coeff2 + sbar*(3.*coeff3 + sbar*(4.*Cp)))));
			//Dispersion:
			DataGptrCollection Ntilde(Sf.size()), A_Ntilde(Sf.size()); //effective nuclear densities in spherical-averaged ansatz
			for(unsigned i=0; i<Sf.size(); i++)
				Ntilde[i] = solvent->Nbulk * (Sf[i] * sTilde);
			vdwForces->init(e.iInfo);
			const double vdwScaleEff = (fsp.pcmVariant==PCM_Nonlocal) ? fsp.sqrtC6eff : fsp.vdwScale;
			Adiel["Dispersion"] = e.vanDerWaals->energyAndGrad(Ntilde, atomicNumbers, vdwScaleEff, &A_Ntilde, &(*vdwForces));
			A_vdwScale = Adiel["Dispersion"]/vdwScaleEff;
			for(unsigned i=0; i<Sf.size(); i++)
				if(A_Ntilde[i])
					A_sTilde += solvent->Nbulk * (Sf[i] * A_Ntilde[i]);
			//Propagate gradients to appropriate shape function:
			(fsp.pcmVariant==PCM_SGA13 ? Acavity_shapeVdw : Acavity_shape) = Jdag(A_sTilde);
			break;
		}
		case PCM_SG14:
		case PCM_SG14tau:
		case PCM_SG14tauVW:
		{	DataRptr sbar = I(wCavity*J(shape));
			A_tension = integral(sbar*(1.-sbar)) * solvent->Nbulk;
			Adiel["CavityTension"] = A_tension * fsp.cavityTension;
			Acavity_shape =  Jdag(wCavity*Idag((fsp.cavityTension*solvent->Nbulk) * (1.-2.*sbar)));
			break;
		}
		case PCM_GLSSA13:
		{	DataRptrVec Dshape = gradient(shape);
			DataRptr surfaceDensity = sqrt(lengthSquared(Dshape));
			DataRptr invSurfaceDensity = inv(surfaceDensity);
			A_tension = integral(surfaceDensity);
			Adiel["CavityTension"] = A_tension * fsp.cavityTension;
			Acavity_shape = (-fsp.cavityTension)*divergence(Dshape*invSurfaceDensity);
			break;
		}
		case PCM_LA12:
		case PCM_PRA05:
			break; //no contribution
	}
}

void PCM::propagateCavityGradients(const DataRptr& A_shape, DataRptr& A_nCavity) const
{	if(fsp.pcmVariant == PCM_SGA13)
	{	//Propagate gradient w.r.t expanded cavities to nCavity:
		A_nCavity = 0;
		((PCM*)this)->A_nc = 0;
		const DataRptr* A_shapeEx[2] = { &A_shape, &Acavity_shapeVdw };
		for(int i=0; i<2; i++)
		{	//First compute derivative w.r.t expanded electron density:
			DataRptr A_nCavityEx;
			ShapeFunction::propagateGradient(nCavityEx[i], *(A_shapeEx[i]), A_nCavityEx, fsp.nc, fsp.sigma);
			((PCM*)this)->A_nc += (-1./fsp.nc) * integral(A_nCavityEx*nCavityEx[i]);
			//then propagate to original electron density:
			DataRptr nCavityExUnused; //unused return value below
			ShapeFunction::expandDensity(wExpand[i], Rex[i], nCavity, nCavityExUnused, &A_nCavityEx, &A_nCavity);
		}
	}
	else if(fsp.pcmVariant == PCM_SG14tauVW)
	{	//First compute derivative w.r.t tauVW:
		DataRptr A_tauCavity;
		ShapeFunction::propagateGradient(tauCavity, A_shape + Acavity_shape, A_tauCavity, fsp.nc, fsp.sigma);
		//then propagate to nCavity
		DataRptr tauCavityUnused;
		A_nCavity = 0;
		ShapeFunction::tauVW(nCavity, tauCavityUnused, &A_tauCavity, &A_nCavity);
	}
	else //All gradients are w.r.t the same shape function - propagate them to nCavity (which is defined as a density product for SaLSA)
	{	A_nCavity = 0;
		ShapeFunction::propagateGradient(nCavity, A_shape + Acavity_shape, A_nCavity, fsp.nc, fsp.sigma);
		((PCM*)this)->A_nc = (-1./fsp.nc) * integral(A_nCavity*nCavity);
	}
}

void PCM::dumpDensities(const char* filenamePattern) const
{	string filename;
	FLUID_DUMP(shape, "Shape");
    if(fsp.pcmVariant == PCM_SGA13)
	{	FLUID_DUMP(shapeVdw, "ShapeVdw");
	}
}

void PCM::dumpDebug(const char* filenamePattern) const
{	string filename(filenamePattern);
	filename.replace(filename.find("%s"), 2, "Debug");
	logPrintf("Dumping '%s' ... ", filename.c_str());  logFlush();
	FILE* fp = mpiUtil->isHead() ? fopen(filename.c_str(), "w") : nullLog;
	if(!fp) die("Error opening %s for writing.\n", filename.c_str());

	fprintf(fp, "Cavity volume = %f\n", integral(1.-shape));
	fprintf(fp, "Cavity surface area = %f\n", integral(sqrt(lengthSquared(gradient(shape)))));
	if(fsp.pcmVariant == PCM_SGA13)
	{	fprintf(fp, "Expanded cavity volume = %f\n", integral(1.-shapeVdw));
		fprintf(fp, "Expanded cavity surface area = %f\n", integral(sqrt(lengthSquared(gradient(shapeVdw)))));
	}
	
	fprintf(fp, "\nComponents of Adiel:\n");
	Adiel.print(fp, true, "   %13s = %25.16lf\n");	
	
	fprintf(fp, "\n\nGradients wrt fit parameters:\n");
	fprintf(fp, "   E_nc = %f\n", A_nc);
	switch(fsp.pcmVariant)
	{	case PCM_SaLSA:
		case PCM_SGA13:
			fprintf(fp, "   E_vdwScale = %f\n", A_vdwScale);
			break;
		case PCM_Nonlocal:
			fprintf(fp, "   E_sqrtC6eff = %f\n", A_vdwScale);
			break;
		case PCM_SG14:
		case PCM_SG14tau:
		case PCM_SG14tauVW:
		case PCM_GLSSA13:
			fprintf(fp, "   E_t = %f\n", A_tension);
			break;
		case PCM_LA12:
		case PCM_PRA05:
			break;
	}
	
	printDebug(fp);
	
	if(mpiUtil->isHead()) fclose(fp);
	logPrintf("done\n"); logFlush();
	
	{ //scope for overriding filename
		char filename[256];	ostringstream oss;
		oss << "Nspherical";
		sprintf(filename, filenamePattern, oss.str().c_str());
		logPrintf("Dumping '%s' ... ", filename); logFlush();
		if(mpiUtil->isHead()) saveSphericalized(&shape, 1, filename);
		logPrintf("done\n"); logFlush();
	}
	
	if(fsp.pcmVariant==PCM_SGA13)
	{
		char filename[256];	ostringstream oss;
		oss << "NvdWspherical";
		sprintf(filename, filenamePattern, oss.str().c_str());
		logPrintf("Dumping '%s' ... ", filename); logFlush();
		if(mpiUtil->isHead()) saveSphericalized(&shapeVdw,1, filename);
		logPrintf("done\n"); logFlush();
	}
}
