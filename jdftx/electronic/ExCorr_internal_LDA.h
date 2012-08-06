/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman, Kendra Letchworth Weaver

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

#ifndef JDFTX_ELECTRONIC_EXCORR_INTERNAL_LDA_H
#define JDFTX_ELECTRONIC_EXCORR_INTERNAL_LDA_H

#include <electronic/ExCorr_internal.h>

//! @file ExCorr_internal_LDA.h
//! Shared CPU-GPU implementation of LDA functionals

//! Available LDA functionals
enum LDA_Variant
{	LDA_X_Slater, //!< LDA exchange (Slater functional)
	LDA_C_PZ, //!< Perdew-Zunger LDA correlation
	LDA_C_PW, //!< Perdew-Wang LDA correlation
	LDA_C_PW_prec, //!< Perdew-Wang LDA correlation (with higher precision constants used in PBE)
	LDA_C_VWN, //!<  Vosko-Wilk-Nusair LDA correlation
	LDA_XC_Teter, //!<  Teter LDA exchange and correlation
	LDA_KE_TF //!< Thomas-Fermi kinetic energy functional
};

//! Common interface to the compute kernels shared by all LDA functionals
class FunctionalLDA : public Functional
{
public:
	FunctionalLDA(LDA_Variant variant, double scaleFac=1.0);
	bool needsSigma() const { return false; }
	bool needsLap() const { return false; }
	bool needsTau() const { return false; }
	bool isKinetic() const
	{	switch(variant)
		{	case LDA_KE_TF:
				return true;
			default:
				return false;
		}
	}
	
	//! The thread launchers and gpu kernels for all LDAs are generated by this function
	//! using the template specializations of LDA_calc and LDA_eval
	//! Note that sigma, lap and taus are unused by LDAs
	void evaluate(int N, std::vector<const double*> n, std::vector<const double*> sigma,
		std::vector<const double*> lap, std::vector<const double*> tau,
		double* E, std::vector<double*> E_n, std::vector<double*> E_sigma,
		std::vector<double*> E_lap, std::vector<double*> E_tau) const;

private:
	LDA_Variant variant;
};

//! Switch a function fTemplate templated over LDA variant and spin count,
//! over all supported functionals with nCount being a compile-time constant
//! (Used by the thread and gpu launchers of FunctionalLDA::evaluate)
//! (This is needed to switch from a run-time variant to a compile-time template argument)
#define SwitchTemplate_LDA(variant,nCount, fTemplate,argList) \
	switch(variant) \
	{	case LDA_X_Slater:  fTemplate< LDA_X_Slater,  nCount> argList; break; \
		case LDA_C_PZ:      fTemplate< LDA_C_PZ,      nCount> argList; break; \
		case LDA_C_PW:      fTemplate< LDA_C_PW,      nCount> argList; break; \
		case LDA_C_PW_prec: fTemplate< LDA_C_PW_prec, nCount> argList; break; \
		case LDA_C_VWN:     fTemplate< LDA_C_VWN,     nCount> argList; break; \
		case LDA_XC_Teter:  fTemplate< LDA_XC_Teter,  nCount> argList; break; \
		case LDA_KE_TF:     fTemplate< LDA_KE_TF,     nCount> argList; break; \
		default: break; \
	}

//! LDA interface inner layer (specialize for each functional):
//! Return energy density given rs and zeta and set gradients w.r.t rs and zeta
template<LDA_Variant variant> __hostanddev__
double LDA_eval(double rs, double zeta, double& e_rs, double& e_zeta);

//! LDA interface outer layer: Accumulate LDA energy density (per unit volume) and its density derivatives
//! Uses template specializations of LDA_eval for each functional written in terms of rs and zeta
//! This layer may be directly specialized for simpler functionals (eg. Slater exchange, Thomas-Fermi KE)
template<LDA_Variant variant, int nCount> struct LDA_calc
{	__hostanddev__ static
	void compute(int i, array<const double*,nCount> n, double* E, array<double*,nCount> E_n, double scaleFac)
	{	//Compute nTot and rs, and ignore tiny densities:
		double nTot = (nCount==1) ? n[0][i] : n[0][i]+n[1][i];
		if(nTot<nCutoff) return;
		double rs = pow((4.*M_PI/3.)*nTot, (-1./3));
		
		//Compute the per particle energy and its derivatives:
		double zeta = (nCount==1) ? 0. : (n[0][i] - n[1][i])/nTot;
		double e_rs, e_zeta, e = LDA_eval<variant>(rs, zeta, e_rs, e_zeta);
		
		//Compute and store final n derivatives if required
		if(E_n[0]) //if this pointer is non-null, all the rest are assumed non-null as well
		{	double e_nTot = -e_rs * rs / (3. * nTot); //propagate rs derivative to nTot;
			double E_nTot = e + nTot * e_nTot; //derivative of energy density per volume
			E_n[0][i] += scaleFac*( E_nTot - e_zeta * (zeta-1) );
			if(nCount>1) E_n[1][i] += scaleFac*( E_nTot - e_zeta * (zeta+1) );
		}
		E[i] += scaleFac*( nTot * e ); //energy density per volume
	}
};

//! Specialization of LDA_calc for Thomas-Fermi kinetic energy (compute directly in n[s])
template<int nCount> struct LDA_calc <LDA_KE_TF, nCount>
{	__hostanddev__ static
	void compute(int i, array<const double*,nCount> n, double* E, array<double*,nCount> E_n, double scaleFac)
	{	//Kinetic Energy is computed for each spin density, independently of the other
		const double KEprefac = (0.3/nCount)*pow(3*M_PI*M_PI, 2./3.);
		for(int s=0; s<nCount; s++)
		{	double ns = n[s][i] * nCount;
			double nsTo23 = pow(ns, 2./3.);
			E[i] += scaleFac*( KEprefac * nsTo23 * ns ); // KEprefac * ns^(5/3)
			if(E_n[s])
				E_n[s][i] += scaleFac*( (nCount * KEprefac * 5./3.) * nsTo23 );
		}
	}
};

//! Specialization of LDA_calc for Slater exchange (compute directly in n[s]; zeta not required)
template<int nCount> struct LDA_calc <LDA_X_Slater, nCount>
{	__hostanddev__ static
	void compute(int i, array<const double*,nCount> n, double* E, array<double*,nCount> E_n, double scaleFac)
	{	//Exchange is computed for each spin density, independently of the other
		const double Xprefac = (-0.75/nCount) * pow(3./M_PI, 1./3);
		for(int s=0; s<nCount; s++)
		{	double ns = n[s][i] * nCount;
			double nsCbrt = pow(ns, 1./3);
			E[i] += scaleFac*( Xprefac * nsCbrt * ns ); // Xprefac * ns^(4/3)
			if(E_n[s])
				E_n[s][i] += scaleFac*( (nCount * Xprefac * 4./3) * nsCbrt );
		}
	}
};



//! Functor for Perdew-Zunger correlation [Phys. Rev. B 23, 5048 (1981)]
//! @tparam para Compute paramagnetic state if true, and ferromagnetic if false
template<bool para> struct LDA_eval_C_PZ
{	__hostanddev__ double operator()(double rs, double& e_rs) const
	{	if(rs<1.)
		{	const double a     = para ?  0.0311 :  0.01555;
			const double b     = para ? -0.0480 : -0.0269;
			const double c     = para ?  0.0020 :  0.0007;
			const double d     = para ? -0.0116 : -0.0048;
			e_rs = a/rs + c*(1+log(rs)) + d;
			return (a + c*rs) * log(rs) + b + d*rs;
		}
		else
		{	const double gamma = para ? -0.1423 : -0.0843;
			const double beta1 = para ?  1.0529 :  1.3981;
			const double beta2 = para ?  0.3334 :  0.2611;
			double denInv = 1./(1. + beta1*sqrt(rs) + beta2*rs);
			double denPrime = beta1/(2.*sqrt(rs)) + beta2;
			e_rs = gamma * (-denInv*denInv) * denPrime;
			return gamma * denInv;
		}
	}
};
//! Perdew-Zunger correlation
template<> __hostanddev__
double LDA_eval<LDA_C_PZ>(double rs, double zeta, double& e_rs, double& e_zeta)
{	return spinInterpolate(rs, zeta, e_rs, e_zeta, LDA_eval_C_PZ<true>(), LDA_eval_C_PZ<false>());
}

//! Functor for Perdew-Wang correlation [JP Perdew and Y Wang, Phys. Rev. B 45, 13244 (1992)]
//! @tparam spinID Compute paramagnetic for spinID=0, ferromagnetic for spinID=1 and spin-stiffness for spinID=2
//! @tparam prec false for original PW coefficients, true for higher precision version used in PBE
template<int spinID, bool prec=true> struct LDA_eval_C_PW
{	__hostanddev__ double operator()(double rs, double& e_rs) const
	{	//PW fit parameters for          paramagnetic            ferromagnetic    zeta-derivative
		const double A     = prec
		                 ? ( (spinID==0) ? 0.0310907 : ((spinID==1) ? 0.01554535 : 0.0168869) )
		                 : ( (spinID==0) ? 0.031091  : ((spinID==1) ? 0.015545   : 0.016887) );
		const double alpha = (spinID==0) ? 0.21370   : ((spinID==1) ? 0.20548    : 0.11125);
		const double beta1 = (spinID==0) ? 7.5957    : ((spinID==1) ? 14.1189    : 10.357);
		const double beta2 = (spinID==0) ? 3.5876    : ((spinID==1) ? 6.1977     : 3.6231);
		const double beta3 = (spinID==0) ? 1.6382    : ((spinID==1) ? 3.3662     : 0.88026);
		const double beta4 = (spinID==0) ? 0.49294   : ((spinID==1) ? 0.62517    : 0.49671);
		//Denominator of rational function inside the log of equation (10):
		double x = sqrt(rs);
		double den   = (2*A)*x*(beta1 + x*(beta2 + x*(beta3 + x*(beta4))));
		double den_x = (2*A)*(beta1 + x*(2*beta2 + x*(3*beta3 + x*(4*beta4))));
		double den_rs = den_x * 0.5/x; //propagate x derivative to rs derivative
		//The log term of equation (10):
		double logTerm    = log(1.+1./den);
		double logTerm_rs = -den_rs/(den*(1.+den));
		//Equation (10) and its derivative:
		e_rs = -(2*A) * (alpha * logTerm + (1+alpha*rs) * logTerm_rs);
		return -(2*A) * (1+alpha*rs) * logTerm;
	}
};
//! Perdew-Wang correlation (original version, for numerical compatibility with LibXC's PW91)
template<> __hostanddev__
double LDA_eval<LDA_C_PW>(double rs, double zeta, double& e_rs, double& e_zeta)
{	return spinInterpolate(rs, zeta, e_rs, e_zeta,
		LDA_eval_C_PW<0,false>(), LDA_eval_C_PW<1,false>(), LDA_eval_C_PW<2,false>(),
		1.709921); //trunctaion of 4./(9*(2^(1./3) - 1)) at ~ single precision
}
//! Perdew-Wang correlation (extended precision version, for numerical compatibility with LibXC's PBE)
template<> __hostanddev__
double LDA_eval<LDA_C_PW_prec>(double rs, double zeta, double& e_rs, double& e_zeta)
{	return spinInterpolate(rs, zeta, e_rs, e_zeta,
		LDA_eval_C_PW<0>(), LDA_eval_C_PW<1>(), LDA_eval_C_PW<2>()); //defaults are high-prec versions
}


//! Functor for Vosko-Wilk-Nusair correlation [Can. J. Phys. 58, 1200 (1980)]
//! @tparam spinID Compute paramagnetic for spinID=0, ferromagnetic for spinID=1 and spin-stiffness for spinID=2
template<int spinID> struct LDA_eval_C_VWN
{	__hostanddev__ double operator()(double rs, double& e_rs) const
	{	//VWN fit parameters for       paramagnetic              ferromagnetic    zeta-derivative
		const double A  = (spinID==0) ? 0.0310907 : ((spinID==1) ? 0.01554535 :  1./(6.*M_PI*M_PI));
		const double b  = (spinID==0) ? 3.72744   : ((spinID==1) ? 7.06042    : 1.13107);
		const double c  = (spinID==0) ? 12.9352   : ((spinID==1) ? 18.0578    : 13.0045);
		const double x0 = (spinID==0) ? -0.10498  : ((spinID==1) ? -0.32500   : -0.0047584);
		const double X0 = c + x0*(b + x0);
		const double Q = sqrt(4.*c - b*b);
		double x = sqrt(rs);
		double X = c + x*(b + x);
		double X_x = 2*x + b;
		//Three transcendental terms and their derivatives w.r.t x:
		double atanTerm = (2./Q) * atan(Q/X_x),  atanTerm_x = -4./(Q*Q + X_x*X_x);
		double logTerm1 = log(x*x/X),            logTerm1_x = 2./x - X_x/X;
		double logTerm2 = log((x-x0)*(x-x0)/X),  logTerm2_x = 2./(x-x0) - X_x/X;
		//Correlation energy density and its derivatives:
		double e   = A*(logTerm1 + b * (atanTerm - (x0/X0)*(logTerm2 + (b+2*x0) * atanTerm)));
		double e_x = A*(logTerm1_x + b * (atanTerm_x - (x0/X0)*(logTerm2_x + (b+2*x0) * atanTerm_x)));
		e_rs = e_x * 0.5/x; //propagate x derivative to rs derivative
		return e;
	}
};
//! Vosko-Wilk-Nusair correlation
template<> __hostanddev__
double LDA_eval<LDA_C_VWN>(double rs, double zeta, double& e_rs, double& e_zeta)
{	return spinInterpolate(rs, zeta, e_rs, e_zeta,
		LDA_eval_C_VWN<0>(), LDA_eval_C_VWN<1>(), LDA_eval_C_VWN<2>());
}


//! Teter LSD exchange & correlation [Phys. Rev. B 54, 1703 (1996)]
template<> __hostanddev__
double LDA_eval<LDA_XC_Teter>(double rs, double zeta, double& e_rs, double& e_zeta)
{	//Value of pade coefficients at para,   change in going to ferro 
	const double pa0 = 0.4581652932831429 , da0 = 0.119086804055547;
	const double pa1 = 2.217058676663745  , da1 = 0.6157402568883345;
	const double pa2 = 0.7405551735357053 , da2 = 0.1574201515892867;
	const double pa3 = 0.01968227878617998, da3 = 0.003532336663397157;
	const double pb2 = 4.504130959426697  , db2 = 0.2673612973836267;
	const double pb3 = 1.110667363742916  , db3 = 0.2052004607777787;
	const double pb4 = 0.02359291751427506, db4 = 0.004200005045691381;
	//spin-interpolate coefficients to current zeta:
	double f_zeta, f = spinInterpolation(zeta, f_zeta);
	double a0 = pa0 + f * da0;
	double a1 = pa1 + f * da1;
	double a2 = pa2 + f * da2;
	double a3 = pa3 + f * da3;
	double b2 = pb2 + f * db2;
	double b3 = pb3 + f * db3;
	double b4 = pb4 + f * db4;
	//Pade approximant:
	double num = a0 + rs*(a1 + rs*(a2 + rs*(a3))); //numerator
	double den = rs*(1. + rs*(b2 + rs*(b3 + rs*(b4)))); //denominator
	double num_rs = a1 + rs*(2*a2 + rs*(3*a3)); //derivative of numerator w.r.t rs
	double den_rs = 1. + rs*(2*b2 + rs*(3*b3 + rs*(4*b4))); //derivative of denominator w.r.t rs
	double num_f = da0 + rs*(da1 + rs*(da2 + rs*(da3))); //derivative of numerator w.r.t f(zeta)
	double den_f = rs*rs*(db2 + rs*(db3 + rs*(db4))); //derivative of denominator w.r.t f(zeta)
	e_rs = (num*den_rs - den*num_rs)/(den*den);
	e_zeta = (num*den_f - den*num_f)*f_zeta/(den*den);
	return -num/den;
};

#endif // JDFTX_ELECTRONIC_EXCORR_INTERNAL_LDA_H
