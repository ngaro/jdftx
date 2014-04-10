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

#include <commands/command.h>
#include <electronic/Everything.h>

//! @file elec_misc.cpp Miscellaneous properties of the electronic system

struct CommandElecCutoff : public Command
{
	CommandElecCutoff() : Command("elec-cutoff")
	{
		format = "<Ecut> [<EcutRho>=0]";
		comments = "Electronic planewave cutoff in Hartree. Optionally specify charge density cutoff\n"
			"<EcutRho> in hartrees. If unspecified or zero, EcutRho is taken to be 4*Ecut.";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.Ecut, 20., "Ecut");
		pl.get(e.cntrl.EcutRho, 0., "EcutRho");
		if(e.cntrl.EcutRho && e.cntrl.EcutRho < 4*e.cntrl.Ecut)
			throw string("<EcutRho> must be at least 4 <Ecut>");
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%lg", e.cntrl.Ecut);
		if(e.cntrl.EcutRho)
			logPrintf(" %lg", e.cntrl.EcutRho);
	}
}
commandElecCutoff;

//-------------------------------------------------------------------------------------------------

struct CommandElecNbands : public Command
{
	CommandElecNbands() : Command("elec-n-bands")
	{
		format = "<n>";
		comments = "Manually specify the number of bands (Default: set nBands assuming insulator\n"
			"or in the case of fillings, equal to total number of atomic orbitals.)";
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.eInfo.nBands, 0, "n", true);
		if(e.eInfo.nBands<=0) throw string("<n> must be positive.\n");
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%d", e.eInfo.nBands);
	}
}
commandElecNbands;

//-------------------------------------------------------------------------------------------------

struct CommandLCAOparams : public Command
{
	CommandLCAOparams() : Command("lcao-params")
	{
		format = "[<nIter>=-1] [<Ediff>=1e-6] [<kT>=1e-3]";
		comments = "Control LCAO wavefunction initialization:\n"
			" <nIter>: maximum subspace iterations in LCAO (negative => auto-select)\n"
			" <Ediff>: energy-difference convergence threshold for subspace iteration\n"
			" <kT>: Fermi temperature for the subspace iteration for T=0 calculations.\n"
			"    If present, the Fermi temperature from elec-fermi-fillings overrides this.\n";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.eVars.lcaoIter, -1, "nIter");
		pl.get(e.eVars.lcaoTol, 1e-6, "Ediff");
		pl.get(e.eInfo.kT, 1e-3, "kT");
		if(e.eInfo.kT<=0) throw string("<kT> must be positive.\n");
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%d %lg %lg", e.eVars.lcaoIter, e.eVars.lcaoTol, e.eInfo.kT);
	}
}
commandLCAOparams;

//-------------------------------------------------------------------------------------------------

EnumStringMap<SpinType> spinMap
(	SpinNone, "no-spin",
	SpinZ, "z-spin"
);

struct CommandSpinType : public Command
{
	CommandSpinType() : Command("spintype")
	{
		format = "<type>=" + spinMap.optionList();
		comments = "Select spin-polarization type";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.eInfo.spinType, SpinNone, spinMap, "type");
	}

	void printStatus(Everything& e, int iRep)
	{	fputs(spinMap.getString(e.eInfo.spinType), globalLog);
	}
}
commandSpinType;

//-------------------------------------------------------------------------------------------------

struct CommandSpinRestricted : public Command
{
	CommandSpinRestricted() : Command("spin-restricted")
	{
		format = "yes|no";
		comments = "Select whether to perform restricted spin-polarized calculations (default no).\n"
			"Note that computational optimizations are minimal in current restricted implementation.\n"
			"The format of wavefunction files depends on the spin, but is unaffected by this flag.";
		require("spintype");
		forbid("fix-electron-density");
		forbid("fix-electron-potential");
		forbid("electronic-scf");
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.eInfo.spinRestricted, false, boolMap, "restricted");
		if(e.eInfo.spinType==SpinNone && e.eInfo.spinRestricted) throw string("Spin-restricted calculations require spintype set to z-spin");
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%s", boolMap.getString(e.eInfo.spinRestricted));
	}
}
commandSpinRestricted;

//-------------------------------------------------------------------------------------------------

//Base class for fix-electron-density and fix-electron-potential
struct CommandFixElectronHamiltonian : public Command
{	
    CommandFixElectronHamiltonian(string name) : Command("fix-electron-"+name)
	{
		format = "<filenamePattern>";
		comments = "Perform band structure calculations at fixed electron " + name + "\n"
			"(or spin " + name + ") read from the specified <filenamePattern>, which\n"
			"must contain $VAR which will be replaced by the appropriate variable\n"
			"names accounting for spin-polarization (same as used for dump).\n"
			"Meta-GGA calculations will also require the corresponding kinetic " + name + ".";
		
		require("spintype");
		forbid("elec-fermi-fillings");
		forbid("elec-ex-corr-compare");
		forbid("electronic-scf");
		forbid("vibrations");
		forbid("spin-restricted");
	}

	void process(ParamList& pl, Everything& e, string& targetFilenamePattern)
	{	pl.get(targetFilenamePattern, string(), "filenamePattern", true);
		if(targetFilenamePattern.find("$VAR") == string::npos)
			throw string("<filenamePattern> must contain $VAR");
		e.cntrl.fixed_H = true;
	}

	void printStatus(Everything& e, int iRep, const string& targetFilenamePattern)
	{	logPrintf("%s", targetFilenamePattern.c_str());
	}
};

struct CommandFixElectronDensity : public CommandFixElectronHamiltonian
{   CommandFixElectronDensity() : CommandFixElectronHamiltonian("density") { forbid("fix-electron-potential"); }
	void process(ParamList& pl, Everything& e) { CommandFixElectronHamiltonian::process(pl, e, e.eVars.nFilenamePattern); }
	void printStatus(Everything& e, int iRep) { CommandFixElectronHamiltonian::printStatus(e, iRep, e.eVars.nFilenamePattern); }
}
commandFixElectronDensity;

struct CommandFixElectronPotential : public CommandFixElectronHamiltonian
{   CommandFixElectronPotential() : CommandFixElectronHamiltonian("potential") { forbid("fix-electron-density"); }
	void process(ParamList& pl, Everything& e) { CommandFixElectronHamiltonian::process(pl, e, e.eVars.VFilenamePattern); }
	void printStatus(Everything& e, int iRep) { CommandFixElectronHamiltonian::printStatus(e, iRep, e.eVars.VFilenamePattern); }
}
commandFixElectronPotential;

//-------------------------------------------------------------------------------------------------

struct CommandFixOccupied : public Command
{
	CommandFixOccupied() : Command("fix-occupied")
	{
		format = "[<fThreshold>=0]";
		comments = "Fix orbitals with fillings larger than <fThreshold> in band-structure calculations\n"
			"The occupied orbitals must be read in using the wavefunction / initial-state commands.\n";
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.occupiedThreshold, 0., "fThreshold");
		if(e.cntrl.occupiedThreshold<0) throw string("fThreshold must be >= 0");
		e.cntrl.fixOccupied = true;
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%lg", e.cntrl.occupiedThreshold);
	}
}
commandFixOccupied;

//-------------------------------------------------------------------------------------------------

struct CommandReorthogonalizeOrbitals : public Command
{
	CommandReorthogonalizeOrbitals() : Command("reorthogonalize-orbitals")
	{
		format = "[<interval=20> [<threshold>=1.5]";
		comments =
			"Every <interval> electronic steps, re-orthogonalize analytically-continued\n"
			"orbitals if the condition number of their overlap matrix crosses <threshold>.\n"
			"Set <interval> = 0 to disable this check.";
		
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.overlapCheckInterval, 20, "interval");
		pl.get(e.cntrl.overlapConditionThreshold, 1.5, "threshold");
		if(e.cntrl.overlapCheckInterval<0) throw string("<interval> must be non-negative");
		if(e.cntrl.overlapConditionThreshold<=1.) throw string("<threshold> must be > 1");
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%d %lg", e.cntrl.overlapCheckInterval, e.cntrl.overlapConditionThreshold);
	}
}
commandReorthogonalizeOrbitals;

//-------------------------------------------------------------------------------------------------

struct CommandWavefunctionDrag : public Command
{
	CommandWavefunctionDrag() : Command("wavefunction-drag")
	{
		format = "yes|no";
		comments =
			"Drag wavefunctions when ions are moved using atomic orbital projections (yes by default).";
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.dragWavefunctions, true, boolMap, "shouldDrag", true);
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%s", boolMap.getString(e.cntrl.dragWavefunctions));
	}
}
commandWavefunctionDrag;

//-------------------------------------------------------------------------------------------------

struct CommandCacheProjectors : public Command
{
	CommandCacheProjectors() : Command("cache-projectors")
	{
		format = "yes|no";
		comments =
			"Cache nonlocal-pseudopotential projectors (yes by default); turn off to save memory.";
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.cacheProjectors, true, boolMap, "shouldCache", true);
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%s", boolMap.getString(e.cntrl.cacheProjectors));
	}
}
commandCacheProjectors;

//-------------------------------------------------------------------------------------------------

struct CommandBasis : public Command
{
	CommandBasis() : Command("basis")
	{
		format = "<kdep>=" + kdepMap.optionList();
		comments = "Basis set at each k-point (default), or single basis set at gamma point";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.basisKdep, BasisKpointDep, kdepMap, "kdep");
	}

	void printStatus(Everything& e, int iRep)
	{	fputs(kdepMap.getString(e.cntrl.basisKdep), globalLog);
	}
}
commandBasis;


//-------------------------------------------------------------------------------------------------

static EnumStringMap<ElecEigenAlgo> elecEigenMap(ElecEigenCG, "CG", ElecEigenDavidson, "Davidson");

struct CommandElecEigenAlgo : public Command
{
    CommandElecEigenAlgo() : Command("elec-eigen-algo")
	{
		format = "<algo>=" + elecEigenMap.optionList();
		comments = "Selects eigenvalue algorithm for band-structure calculations or inner loop of SCF.";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.elecEigenAlgo, ElecEigenDavidson, elecEigenMap, "algo");
	}

	void printStatus(Everything& e, int iRep)
	{	fputs(elecEigenMap.getString(e.cntrl.elecEigenAlgo), globalLog);
	}
}
commandElecEigenAlgo;

