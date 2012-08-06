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

#include <electronic/SpeciesInfo.h>
#include <electronic/Everything.h>
#include <electronic/matrix.h>

namespace FhiFile
{	//! Read line from Cpi/Fhi file (basically a raw getline())
	string getLine(istream& in)
	{	string line; getline(in, line); return line;
	}

	//! Logarithmic grid angular momentum channel (r, R, V) read in from CPI file
	struct LogGridChannel
	{	double dlogr; //!< logarithmic grid spacing
		std::vector<double> r; //!< radial coordinate
		std::vector<double> R; //!< radial part of wavefunction R(r)
		std::vector<double> V; //!< potential
		
		void read(istream& in)
		{	int nPoints; double rRatio;
			istringstream(getLine(in)) >> nPoints >> rRatio;
			dlogr = log(rRatio);
			r.resize(nPoints);
			R.resize(nPoints);
			V.resize(nPoints);
			logPrintf("%d samples at logarithmic spacing %lg.\n", nPoints, dlogr);
			int iDump; //unused integer at start of every line
			for(int i=0; i<nPoints; i++)
			{	istringstream(getLine(in)) >> iDump >> r[i] >> R[i] >> V[i];
				R[i] /= r[i]; //file stores u(r) = r*R(r)
			}
		}
		
		//! Generate the full local potential (V + Z/r)
		RadialFunctionR VplusZbyr(double Z) const
		{	RadialFunctionR Vloc(r, dlogr);
			for(unsigned i=0; i<r.size(); i++)
				Vloc.f[i] = V[i] + Z/r[i];
			return Vloc;
		}
		
		//! Subtract the local potential and multiply by wavefunction to get non-local part:
		RadialFunctionR getProjector(const LogGridChannel& loc) const
		{	RadialFunctionR RdV(r, dlogr);
			for(unsigned i=0; i<r.size(); i++)
				RdV.f[i] = R[i] * (V[i] - loc.V[i]);
			return RdV;
		}
		
		//! Return the matrix element < R | V-Vloc| R > for the projector
		double projectorM(const LogGridChannel& loc) const
		{	RadialFunctionR RVR(r, dlogr);
			for(unsigned i=0; i<r.size(); i++)
				RVR.f[i] = R[i] * (V[i] - loc.V[i]) * R[i];
			return RVR.transform(0, 0.)/(4*M_PI); //the l=0, G=0 bessel transform is just the integral
		}
		
		//!Get the radial wavefunction on the logPrintf grid:
		RadialFunctionR getPsi() const
		{	RadialFunctionR psi(r, dlogr);
			psi.f = R;
			return psi;
		}
		//!Check if this channel has a wavefunction (custom local channel won't)
		bool hasPsi() const
		{	return (cblas_dnrm2(R.size(), &R[0], 1) > 1e-10);
		}
	};
};

// Read a FHI98 format .cpi file 
void SpeciesInfo::readCpi(istream& in)
{
	using namespace FhiFile;
	
	//-------------------- CPI file read ---------------------
	
	//Valence charge and number of angular momentum channels
	double Zvalence; int lCount;
	istringstream(getLine(in)) >> Zvalence >> lCount;
	if(Zvalence != Z) die("Valence charge in pseudopotential = %lg != %lg (specified Z).\n", Zvalence, Z);

	//Skip 10 unused lines
	for(int i=0; i<10; i++) getLine(in);
	
	//Read all angular momentum channels
	std::vector<LogGridChannel> channels(lCount);
	for(int l=0; l<lCount; l++)
	{	logPrintf("  l=%d:  ", l);
		channels[l].read(in);
	}
	
	//Read core density (if available):
	RadialFunctionR nCoreLog(channels[0].r, channels[0].dlogr);
	logPrintf("  Core density:  ");
	for(unsigned i=0; i<nCoreLog.r.size(); i++)
	{	if(in.eof()) { nCoreLog=RadialFunctionR(); break; }
		istringstream(getLine(in)) >> nCoreLog.r[i] >> nCoreLog.f[i];
		nCoreLog.f[i] *= (1.0/(4*M_PI)); //weird scale factor in input
	}
	if(nCoreLog.r.size()) logPrintf("%lu samples at logarithmic spacing %lg.\n", nCoreLog.r.size(), channels[0].dlogr);
	else logPrintf("not found.\n"); 
	
	//---------------- Log R Grid -> Uniform G grid transformations -----------------
	
	int nGridLoc = int(ceil(e->iInfo.GmaxLoc/dGloc))+5;
	//Core density:
	if(nCoreLog.f.size())
		setCore(nCoreLog);
	//Local potential:
	int lLoc = lLocCpi>=0 ? lLocCpi : (lCount-1); //specified channel, or last channel if unspecified
	if(lLoc>=lCount) die("Local channel l=%d is invalid (max l=%d in file).\n", lLoc, lCount);
	logPrintf("  Transforming local potential (l=%d) to a uniform radial grid of dG=%lg with %d points.\n", lLoc, dGloc, nGridLoc);
	channels[lLoc].VplusZbyr(Z).transform(0, dGloc, nGridLoc, VlocRadial);
	
	//Non-local potentials
	if(lLoc==lCount-1) lCount--; //projetcor array shortens if last channel is local
	VnlRadial.resize(lCount);
	Mnl.resize(lCount);
	int nGridNL = int(ceil(e->iInfo.GmaxNL/dGnl))+5;
	if(lCount)
	{	logPrintf("  Transforming nonlocal projectors to a uniform radial grid of dG=%lg with %d points.\n", dGnl, nGridNL);
		for(int l=0; l<lCount; l++)
			if(l != lLoc)
			{	if(l>3) die("Nonlocal projectors with l>3 not implemented.\n");
				double Minv = channels[l].projectorM(channels[lLoc]);
				if(Minv) //to handle the special case when custom local channel happens to equal one of the l's!
				{	VnlRadial[l].resize(1); //single projector per angular momentum
					channels[l].getProjector(channels[lLoc]).transform(l, dGnl, nGridNL, VnlRadial[l][0]);
					Mnl[l] = eye(1) * (1./Minv);
				}
			}
	}
	
	//Radial wavefunctions:
	logPrintf("  Transforming atomic orbitals to a uniform radial grid of dG=%lg with %d points.\n", dGnl, nGridNL);
	psiRadial.resize(channels.size());
	for(unsigned l=0; l<channels.size(); l++)
	{	if(channels[l].hasPsi())
		{	if(l>3) die("Atomic orbitals with l>3 not implemented.\n");
			psiRadial[l].push_back(RadialFunctionG());
			channels[l].getPsi().transform(l, dGnl, nGridNL, psiRadial[l].back());
		}
		else
		{	psiRadial.resize(l); //no more wavefunctions
			break;
		}
	}
}

void SpeciesInfo::readFhi(istream& in)
{	
	using namespace FhiFile;
	
	//Read the FHI header:
	int iDump; double dDump; //for unused params
	logPrintf("  Title: %s.\n", getLine(in).c_str()); //line 1
	istringstream(getLine(in)) >> dDump >> Z; //line 2
	istringstream(getLine(in)) >> iDump >> iDump >> iDump >> lLocCpi; //line 3
	for(int line=4; line<=7; line++)
		getLine(in); //Ignore lines 4 through 7
	//The remainder of the file is a .cpi:
	readCpi(in);
}

