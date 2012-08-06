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

#ifndef JDFTX_ELECTRONIC_EXACTEXCHANGE_INTERNAL_H
#define JDFTX_ELECTRONIC_EXACTEXCHANGE_INTERNAL_H

#include <core/matrix3.h>

#define GzeroTOL 1e-8

__hostanddev__
double erfcTilde(double Gsq, double omegaSq)
{	return (omegaSq ? (1.-exp(-0.25*Gsq/omegaSq)) : 1.) / Gsq;
}

__hostanddev__
double screenedCoulombK_calc(const vector3<int>& iG, const matrix3<>& GGT,
	const vector3<>& kDiff, double weightedVzero, double omegaSq)
{
	double kpGsq = GGT.metric_length_squared(kDiff + iG);
	if(kpGsq>GzeroTOL) return erfcTilde(kpGsq, omegaSq);
	else return weightedVzero;
}

#endif //JDFTX_ELECTRONIC_EXACTEXCHANGE_INTERNAL_H
