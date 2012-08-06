/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman

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

#ifndef JDFTX_ELECTRONIC_WANNIER_H
#define JDFTX_ELECTRONIC_WANNIER_H

#include <core/MinimizeParams.h>
#include <electronic/common.h>
#include <core/vector3.h>

//! Compute Maximally-Localized Wannier Functions
class Wannier
{
public:
	Wannier();
	~Wannier();
	void setup(const Everything& everything);
	
	struct Center
	{	int band; //!< source band
		vector3<> r; //!< guess for center of localized wannier function
		double width; //!< guess width for localized wannier function
	};
	
	std::vector< std::vector<Center> > group; //!< list of groups of centers (the bands in each group are linearly combined)
	vector3<int> supercell; //!< number of unit cells per dimension in supercell
	bool convertReal; //!< whether to convert wannier functions to real ones by phase removal
	
	void saveMLWF(); //!< Output the Maximally-Localized Wannier Functions from current wavefunctions
	
private:
	const Everything* e;
	MinimizeParams minParams;
	bool verboseLog;
	class WannierEval* eval; //!< opaque struct to evaluator
	friend class WannierEval;
	friend class CommandDebug;
	friend class CommandWannierMinimize;
};

#endif // JDFTX_ELECTRONIC_WANNIER_H
