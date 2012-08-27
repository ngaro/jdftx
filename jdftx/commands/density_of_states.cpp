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
#include <electronic/DOS.h>

EnumStringMap<DOS::Weight::Type> weightTypeMap
(	DOS::Weight::Total, "Total",
	DOS::Weight::Slice, "Slice",
	DOS::Weight::Sphere, "Sphere",
	DOS::Weight::AtomSlice, "AtomSlice",
	DOS::Weight::AtomSphere, "AtomSphere",
	DOS::Weight::File, "File",
	DOS::Weight::Orbital, "Orbital",
	DOS::Weight::OrthoOrbital, "OrthoOrbital"
);

struct CommandDensityOfStates : public Command
{
	CommandDensityOfStates() : Command("density-of-states")
	{
		format = "[<key1> ...] [<key2> ...] [<key3> ...] ... ";
		comments =
			"Compute density of states. The results are printed to a text file\n"
			"with name corresponding to variable name 'dos' (see dump-name).\n"
			"(Spin polarized calculations output variables 'dosUp' and 'dosDn'.)\n"
			"Density of states with different weight functions may be computed\n"
			"simultaneously, and they are all output as columns in the same file\n"
			"in the same order that they appear in this command, with the energy\n"
			"in the first column. The energy is in Hartrees, and the density of\n"
			"states is in electrons/UnitCell/Hartree.\n"
			"   This command is organized into subcommands, each with a keyword\n"
			"followed by subcommand-specific arguments. The keywords that lead to a\n"
			"column in the output file (various weighting modes) and arguments are:\n"
			"   Total\n"
			"      Compute the total density of states (no arguments)\n"
			"   Slice  <c0> <c1> <c2>   <r>   <i0> <i1> <i2>\n"
			"      Density of states in a planar slab centered at (<c0>,<c1>,<c2>)\n"
			"      in the coordinate system selected by coords-type, parallel to\n"
			"      the lattice plane specified by Miller indices (<i0>,<i1>,<i2>),\n"
			"      with half-width <r> bohrs normal to the lattice plane.\n"
			"   Sphere  <c0> <c1> <c2>   <r>\n"
			"      Density of states in a sphere of radius <r> bohrs centered at\n"
			"      (<c0>,<c1>,<c2>) in the coordinate system selected by coords-type.\n"
			"   AtomSlice  <species> <atomIndex>   <r>   <i0> <i1> <i2>\n"
			"      Like Slice mode, with center located at atom number <atomIndex>\n"
			"      (1-based index, in input file order) of species name <species>.\n"
			"   AtomSphere  <species> <atomIndex>   <r>\n"
			"      Like Sphere mode, but centered on an atom (specified as in AtomSlice)\n"
			"   File <filename>\n"
			"      Arbitrary real-space weight function read from file <filename>.\n"
			"      (double-precision binary, same format as electron density output)\n"
			"      A file with all 1.0's would yield the same result as mode Total.\n"
			"   Orbital  <species> <atomIndex>   <orbDesc>\n"
			"      Atomic-orbital projected density of states. The target atom is\n"
			"      selected as in AtomSphere mode. <orbDesc> selects the atomic orbital\n"
			"      used for projection, from those available in the pseudopotential.\n"
			"      s, p, d or f select the total projection in that angular momentum,\n"
			"      and px, py, pz, dxy, dyz, dz2, dxz, dx2-y2, d, fy(3x2-y2) fxyz, fyz2,\n"
			"      fz3, fxz2, fz(x2-y2) or fx(x2-3y2) select a specific orbital, where\n"
			"      (x,y,z) are cartesian directions. The orbital code may be prefixed\n"
			"      by the psuedo-atom principal quantum number in the case of multiple\n"
			"      orbitals per angular momentum eg. '2px' selects the second px orbital\n"
			"      in a psuedopotential with 2 l=1 orbitals, while '1px' or 'px' select\n"
			"      the first of the two.\n"
			"   OrthoOrbital  <species> <atomIndex>   <orbDesc>\n"
			"      Similar to Orbital, except the projectors are Lowdin-orthonormalized\n"
			"      atomic orbitals. This orthonormalization ensures that the sum of DOS\n"
			"      projected on all OrthoOrbitals is <= the total DOS.\n"
			"Any number of weight functions may be specified; only the total density\n"
			"of states is output if no weight functions are specified. Other flags\n"
			"that control aspects of the density of states computation are:\n"
			"   Etol <Etol>\n"
			"      Resolution in energy within which eigenvalues are identified,\n"
			"      and is used as the band width for Gamma-point only calculations.\n"
			"      This flag affects all columns of output, and is 1e-6 by default.\n"
			"   Occupied\n"
			"      All subsequent columns are occupied density of states, that is\n"
			"      they are weighted by the band fillings.\n"
			"   Complete\n"
			"      All subsequent columns are complete density of states, that is\n"
			"      they do not depend on band fillings: this is the default mode.\n"
			"This command adds DOS to dump-frequency End, but this may be altered\n"
			"within a dump command of appropriate frequency (see command dump).";
		hasDefault = false;
		
		require("ion"); //This ensures that this command is processed after all ion commands 
		// (which in turn are processed after lattice and all ion-species commands)
	}

	void process(ParamList& pl, Everything& e)
	{	e.dump.insert(std::make_pair(DumpFreq_End, DumpDOS)); //Add DOS to dump-ferequency End
		e.dump.dos = std::make_shared<DOS>();
		DOS& dos = *(e.dump.dos);
		DOS::Weight::FillingMode fillingMode = DOS::Weight::Complete;
		//Process subcommands:
		while(true)
		{	//Get the keyword:
			string key; pl.get(key, string(), "key");
			if(!key.length())
				break; //end of command
			
			//Check if it is a flag:
			if(key == "Etol") { pl.get(dos.Etol, 0., "Etol", true); continue; }
			if(key == "Occupied") { fillingMode = DOS::Weight::Occupied; continue; }
			if(key == "Complete") { fillingMode = DOS::Weight::Complete; continue; }
			
			//Otherwise it should be a weight function:
			DOS::Weight weight;
			weight.fillingMode = fillingMode;
			if(!weightTypeMap.getEnum(key.c_str(), weight.type))
				throw "'"+key+"' is not a valid subcommand of density-of-states.";
			
			//Get center coordinates for Slice or Sphere mode:
			if(weight.type==DOS::Weight::Slice || weight.type==DOS::Weight::Sphere)
			{	vector3<> center;
				pl.get(center[0], 0., "c0", true);
				pl.get(center[1], 0., "c1", true);
				pl.get(center[2], 0., "c2", true);
				weight.center = (e.iInfo.coordsType==CoordsCartesian) ? inv(e.gInfo.R) * center : center; //internally store in lattice coordinates
			}
			//Get species and atom index for all atom-centered modes:
			if(weight.type==DOS::Weight::AtomSlice || weight.type==DOS::Weight::AtomSphere
				|| weight.type==DOS::Weight::Orbital || weight.type==DOS::Weight::OrthoOrbital)
			{	//Find specie:
				string spName; pl.get(spName, string(), "species", true);
				bool spFound = false;
				for(size_t sp=0; sp<e.iInfo.species.size(); sp++)
					if(e.iInfo.species[sp]->name == spName)
					{	weight.specieIndex = sp;
						spFound = true;
						break;
					}
				if(!spFound)
					throw "Could not find species with name '" + spName + "'";
				//Get atom index:
				pl.get(weight.atomIndex, size_t(0), "atomIndex", true);
				if(!weight.atomIndex)
					throw string("Atom index should be a positive integer");
				if(weight.atomIndex > e.iInfo.species[weight.specieIndex]->atpos.size())
					throw "Atom index exceeds number of atoms for species '" + spName + "'";
				weight.atomIndex--; //store 0-based index internally
			}
			//Get radius / half-width for all sphere and slice modes
			if(weight.type==DOS::Weight::Slice || weight.type==DOS::Weight::Sphere
				|| weight.type==DOS::Weight::AtomSlice || weight.type==DOS::Weight::AtomSphere)
			{	pl.get(weight.radius, 0., "r", true);
				if(weight.radius <= 0)
					throw string("Radius / half-width of weight function must be > 0");
			}
			//Get lattice plane direction for slice modes
			if(weight.type==DOS::Weight::Slice || weight.type==DOS::Weight::AtomSlice)
			{	pl.get(weight.direction[0], 0, "i0", true);
				pl.get(weight.direction[1], 0, "i1", true);
				pl.get(weight.direction[2], 0, "i2", true);
				if(!weight.direction.length_squared())
					throw string("Lattice plane direction (0,0,0) is invalid");
			}
			//Get filename for File mode:
			if(weight.type==DOS::Weight::File)
			{	pl.get(weight.filename, string(), "filename", true);
				//Check if file exists and is readable:
				FILE* fp = fopen(weight.filename.c_str(), "r");
				if(!fp) throw "File '"+weight.filename+"' cannot be opened for reading.\n";
				fclose(fp);
			}
			//Get orbital description for Orbital modes:
			if(weight.type==DOS::Weight::Orbital || weight.type==DOS::Weight::OrthoOrbital)
			{	string orbDesc; pl.get(orbDesc, string(), "orbDesc", true);
				weight.orbitalDesc.parse(orbDesc);
			}
			dos.weights.push_back(weight);
		}
	}

	void printStatus(Everything& e, int iRep)
	{	assert(e.dump.dos);
		DOS& dos = *(e.dump.dos);
		DOS::Weight::FillingMode fillingMode = DOS::Weight::Complete;
		logPrintf("Etol %le", dos.Etol);
		for(unsigned iWeight=0; iWeight<dos.weights.size(); iWeight++)
		{	const DOS::Weight& weight = dos.weights[iWeight];
			//Check for changed filling mode:
			if(iWeight==0 || weight.fillingMode != fillingMode)
			{	fillingMode = weight.fillingMode;
				logPrintf(" \\\n\t\t%s", fillingMode==DOS::Weight::Complete ? "Complete" : "Occupied");
			}
			//Output weight function subcommand:
			logPrintf(" \\\n\t%s", weightTypeMap.getString(weight.type));
			switch(weight.type)
			{	case DOS::Weight::Total:
					break; //no arguments
				case DOS::Weight::Slice:
				case DOS::Weight::Sphere:
				{	vector3<> center = (e.iInfo.coordsType==CoordsCartesian) ? e.gInfo.R * weight.center : weight.center;
					logPrintf(" %lg %lg %lg   %lg", center[0], center[1], center[2], weight.radius);
					if(weight.type == DOS::Weight::Slice)
						logPrintf("   %d %d %d", weight.direction[0], weight.direction[1], weight.direction[2]);
					break;
				}
				case DOS::Weight::AtomSlice:
				case DOS::Weight::AtomSphere:
				case DOS::Weight::Orbital:
				case DOS::Weight::OrthoOrbital:
				{	logPrintf(" %s %lu", e.iInfo.species[weight.specieIndex]->name.c_str(), weight.atomIndex+1);
					if(weight.type == DOS::Weight::AtomSlice)
						logPrintf("   %lg   %d %d %d", weight.radius, weight.direction[0], weight.direction[1], weight.direction[2]);
					if(weight.type == DOS::Weight::AtomSphere)
						logPrintf("   %lg", weight.radius);
					if(weight.type == DOS::Weight::Orbital || weight.type == DOS::Weight::OrthoOrbital)
						logPrintf("   %s", string(weight.orbitalDesc).c_str());
					break;
				}
				case DOS::Weight::File:
					logPrintf(" %s", weight.filename.c_str());
					break;
			}
		}
	}
}
commandDensityOfStates;
