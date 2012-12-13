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

#ifndef JDFTX_CORE_WIGNERSEITZ_H
#define JDFTX_CORE_WIGNERSEITZ_H

//! @file WignerSeitz.h

#include <core/matrix3.h>
#include <float.h>
#include <array>
#include <list>
#include <set>

//! Wigner-Seitz construction for a 3D lattice (2D lattice may be handled with orthogonal 3rd direction)
class WignerSeitz
{
public:
	WignerSeitz(const matrix3<>& R); //!< Construct Wigner-Seitz cell given lattice vectors
	~WignerSeitz();

	//! Find the point within the Wigner-Seitz cell equivalent to x (lattice coordinates)
	inline vector3<> restrict(const vector3<>& x) const
	{	static const double tol = 1e-8;
		vector3<> xWS = x;
		bool changed = true;
		while(changed)
		{	changed = false;
			for(const Face* f: faceHalf)
			{	double d = 0.5 * (1. + dot(f->eqn, xWS));
				if(d<-tol || d>1.+tol) //not in fundamental zone
				{	xWS -= floor(d) * f->img;
					changed = true;
				}
			}
		}
		return xWS;
	}
	
	//! Radius of largest sphere centered at origin contained within the Wigner-Seitz cell
	//! Ignore direction iDir to obtain 2D behavior, if iDir >= 0
	double inRadius(int iDir=-1) const;
	
	//! Radius of smallest sphere centered at origin that contains the Wigner-Seitz cell
	//! Ignore direction iDir to obtain 2D behavior, if iDir >= 0
	double circumRadius(int iDir=-1) const;
	
	//! Write a wireframe plot to file (for gnuplot)
	void writeWireframePlot(const char* filename) const;
	
	//! Write a wireframe plot for Data Explorer (.dx)
	void writeWireframeDX(const char* filename) const;

	//! Check if the data structure is valid (all links are reciprocated etc.)
	void checkGraph() const;
	
	//! Output vertex, edge and face connectivity info:
	void writeGraph(FILE* fp=stdout) const;

	static const double geomRelTol; //!< relative tolerance for orthogonality and volume checks
	
	//! Check whether lattice vectors are orthogonal (within relative tolerance geomRelTol)
	static bool isOrthogonal(const vector3<>&, const vector3<>&);
	
private:
	struct Vertex; //!< Point
	struct Edge; //!< Line segment
	struct Face; //!<Polygonal facet

	matrix3<> R, invR, RTR; //!< matrix of lattice vectors, and its combinations
	double minDistSq; //!< threshold on distance squared for welding vertices
	std::list<Vertex*> vertex; //!< set of all vertices
	std::set<Edge*> edge; //!< set of all edges
	std::set<Face*> face; //!< set of all faces
	std::vector<Face*> faceHalf; //!< array of half the faces, one from each inversion symmetry pair
	
	//! Point
	struct Vertex
	{	vector3<> pos; //!< position in lattice coordinates
		std::list<Edge*> edge; //!< edges bounded by this vertex
	};
	
	//! Line segment
	struct Edge
	{	std::array<Vertex*,2> vertex; //!< vertices bounding this edge
		std::array<Face*,2> face; //!< faces bounded by this edge
	};
	
	//! Polygonal facet
	struct Face
	{	vector3<int> img; //!< image of origin under the plane containing this face (lattice coordinates)
		vector3<> eqn; //!< equation of plane given by eqn.x==1 (x in lattice coordinates)
		std::list<Edge*> edge; //!< edges bounding this face
	};
	
	//! Slice the current polyhedron by the perpendicular bisector of 0->a (a in lattice coordinates)
	void addPlane(const vector3<int>& a);
	
	//! Add a edge from vStart towards vEnd in face f
	//! Note: edges are added to the end of the face list by default (therefore must call in order)
	//! However if or if there is only one missing edge (lastEdge=true), insertion will be at correct location
	void addEdge(Face* f, Vertex* vStart, Vertex* vEnd, bool lastEdge=false);
};

#endif // JDFTX_CORE_WIGNERSEITZ_H