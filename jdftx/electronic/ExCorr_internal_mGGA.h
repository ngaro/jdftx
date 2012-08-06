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

#ifndef JDFTX_ELECTRONIC_EXCORR_INTERNAL_MGGA_H
#define JDFTX_ELECTRONIC_EXCORR_INTERNAL_MGGA_H

#include <electronic/ExCorr_internal_GGA.h>

//! @file ExCorr_internal_mGGA.h
//! Shared CPU-GPU implementation of meta GGA functionals

//! Available mGGA functionals 
enum mGGA_Variant
{	mGGA_X_TPSS, //!< TPSS mGGA exchange
	mGGA_C_TPSS, //!< TPSS mGGA correlation
	mGGA_X_revTPSS, //!< revTPSS mGGA exchange
	mGGA_C_revTPSS //!< revTPSS mGGA correlation
};

//! Common interface to the compute kernels for mGGA-like functionals
class FunctionalMGGA : public Functional
{
public:
	FunctionalMGGA(mGGA_Variant variant, double scaleFac=1.0);
	bool needsSigma() const { return true; }
	bool needsLap() const
	{	switch(variant)
		{	case mGGA_X_TPSS:
			case mGGA_C_TPSS:
			case mGGA_X_revTPSS:
			case mGGA_C_revTPSS:
				return false;
			default:
				return true;
		}
	}
	bool needsTau() const { return true; }
	bool isKinetic() const { return false; }
	
	//! The thread launchers and gpu kernels for all mGGAs are generated by this function
	//! using the template specializations of mGGA_calc, mGGA_eval
	//! Note that lap and taus are unused by mGGAs
	void evaluate(int N, std::vector<const double*> n, std::vector<const double*> sigma,
		std::vector<const double*> lap, std::vector<const double*> tau,
		double* E, std::vector<double*> E_n, std::vector<double*> E_sigma,
		std::vector<double*> E_lap, std::vector<double*> E_tau) const;

private:
	mGGA_Variant variant;
};

//! Switch a function fTemplate templated over mGGA variant, spin scaling behavior and spin count,
//! over all supported functionals with nCount being a compile-time constant
//! NOTE: The second argument to fTemplate must correspond to the spin-scaling behavior of each functional
//! (Used by the thread and gpu launchers of FunctionalMGGA::evaluate)
//! (This is needed to switch from a run-time variant to a compile-time template argument)
#define SwitchTemplate_mGGA(variant,nCount, fTemplate,argList) \
	switch(variant) \
	{	case mGGA_X_TPSS:    fTemplate< mGGA_X_TPSS,     true, nCount> argList; break; \
		case mGGA_C_TPSS:    fTemplate< mGGA_C_TPSS,    false, nCount> argList; break; \
		case mGGA_X_revTPSS: fTemplate< mGGA_X_revTPSS,  true, nCount> argList; break; \
		case mGGA_C_revTPSS: fTemplate< mGGA_C_revTPSS, false, nCount> argList; break; \
		default: break; \
	}

//! GGA interface inner layer for spin-scaling functionals (specialized for each such functional):
//! Return energy density given dimensionless quantities rs, s2, q and z, and set gradients w.r.t them
//! See the PBE / TPSS refs for definitions of these quantities
template<mGGA_Variant variant> __hostanddev__
double mGGA_eval(double rs, double s2, double q, double z,
	double& e_rs, double& e_s2, double& e_q, double& e_z);

//! GGA interface inner layer for functionals that do not spin-scale (specialized for each such functional):
//! Return energy density given rs, zeta, g, t2 (see PW91 ref for definitions of these),
//! t2up, t2dn (the individual spin versions of t2)
//! zi2 (the dimensionless grad zeta squared term) and z = tauW/tau (totals, not spin resolved)
//! and set gradients w.r.t them
template<mGGA_Variant variant> __hostanddev__
double mGGA_eval(double rs, double zeta, double g, double t2,
	double t2up, double t2dn, double zi2, double z,
	double& e_rs, double& e_zeta, double& e_g, double& e_t2,
	double& e_t2up, double& e_t2dn, double& e_zi2, double& e_z);

//! GGA interface outer layer: Accumulate GGA energy density (per unit volume)
//! and its derivatives w.r.t. density and sigma (gradient contractions)
//! Uses template specializations of the appropriate version of GGA_eval
template<mGGA_Variant variant, bool spinScaling, int nCount> struct mGGA_calc;

//! Specialization of mGGA_calc for spin-scaling functionals (exchange)
template<mGGA_Variant variant, int nCount>
struct mGGA_calc <variant, true, nCount>
{	__hostanddev__ static
	void compute(int i, array<const double*,nCount> n, array<const double*,2*nCount-1> sigma,
		array<const double*,nCount> lap, array<const double*,nCount> tau,
		double* E, array<double*,nCount> E_n, array<double*,2*nCount-1> E_sigma,
		array<double*,nCount> E_lap, array<double*,nCount> E_tau, double scaleFac)
	{	//Each spin component is computed separately:
		for(int s=0; s<nCount; s++)
		{	//Scale up s-density and gradient:
 			double ns = n[s][i] * nCount;
 			if(ns < nCutoff) continue;
			//Compute dimensionless quantities rs, s2, q and z: (see TPSS reference)
			double rs = pow((4.*M_PI/3.)*ns, (-1./3));
			double s2_sigma = pow(ns, -8./3) * ((0.25*nCount*nCount) * pow(3.*M_PI*M_PI, -2./3));
			double s2 = s2_sigma * sigma[2*s][i];
			double q_lap = pow(ns, -5./3) * ((0.25*nCount) * pow(3.*M_PI*M_PI, -2./3));
			double q = q_lap * (lap[s] ? lap[s][i] : 0.);
			if(tau[s] && tau[s][i]<nCutoff) continue;
			double z_sigma = tau[s] ? (0.125*nCount)/(ns * tau[s][i]) : 0.;
			double z = z_sigma * sigma[2*s][i];
			bool zOffRange = false;
			if(z>1.) { z=1.; zOffRange = true; }
			//Compute energy density and its gradients using GGA_eval:
			double e_rs, e_s2, e_q, e_z, e = mGGA_eval<variant>(rs, s2, q, z, e_rs, e_s2, e_q, e_z);
			if(zOffRange) e_z = 0;
			//Compute gradients if required:
			if(E_n[0])
			{	//Propagate rs, s2, q, z gradients to n, sigma, lap, tau:
				double e_n = -(e_rs*rs + 8*e_s2*s2 + 5*e_q*q + 3*e_z*z) / (3. * n[s][i]);
				double e_sigma = e_s2 * s2_sigma + e_z * z_sigma;
				double e_lap = e_q * q_lap;
				double e_tau = tau[s] ? -e_z*z/tau[s][i] : 0.;
				//Convert form per-particle to per volume:
				E_n[s][i] += scaleFac*( n[s][i] * e_n + e );
				E_sigma[2*s][i] += scaleFac*( n[s][i] * e_sigma );
				if(lap[s]) E_lap[s][i] += scaleFac*( n[s][i] * e_lap );
				if(tau[s]) E_tau[s][i] += scaleFac*( n[s][i] * e_tau );
			}
			E[i] += scaleFac*( n[s][i] * e );
		}
	}
};

//! Specialization of mGGA_calc for functionals that do not spin-scale (correlation)
//! The current structure is designed for TPSS-like correlation functionals;
//! this could be generalized in the future by replacing the bool template parameter
//! spinScaling with an enum mGGAtype to provide multiple interfaces for correlation
template<mGGA_Variant variant, int nCount>
struct mGGA_calc <variant, false, nCount>
{	__hostanddev__ static
	void compute(int i, array<const double*,nCount> n, array<const double*,2*nCount-1> sigma,
		array<const double*,nCount> lap, array<const double*,nCount> tau,
		double* E, array<double*,nCount> E_n, array<double*,2*nCount-1> E_sigma,
		array<double*,nCount> E_lap, array<double*,nCount> E_tau, double scaleFac)
	{
		//Compute nTot and rs, and ignore tiny densities:
		double nTot = (nCount==1) ? n[0][i] : n[0][i]+n[1][i];
		if(nTot<nCutoff) return;
		double rs = pow((4.*M_PI/3.)*nTot, (-1./3));
		//Compute zeta, g(zeta)
		double zeta = (nCount==1) ? 0. : (n[0][i] - n[1][i])/nTot;
		double g = 0.5*(pow(1+zeta, 2./3) + pow(1-zeta, 2./3));
		//Compute dimensionless gradient squared t2 (and t2up/t2dn):
		double t2_sigma = (pow(M_PI/3, 1./3)/16.) * pow(nTot,-7./3) / (g*g);
		double sigmaTot = (nCount==1) ? sigma[0][i] : sigma[0][i]+2*sigma[1][i]+sigma[2][i];
		double t2 = t2_sigma * sigmaTot;
		double t2up_sigmaUp, t2dn_sigmaDn, t2up, t2dn;
		if(nCount==1) t2up = t2dn = 2*t2;
		else
		{	if(n[0][i]<nCutoff || n[1][i]<nCutoff) return;
			t2up_sigmaUp = (pow(4*M_PI/3, 1./3)/16.) * pow(n[0][i],-7./3);
			t2dn_sigmaDn = (pow(4*M_PI/3, 1./3)/16.) * pow(n[1][i],-7./3);
			t2up = t2up_sigmaUp * sigma[0][i];
			t2dn = t2dn_sigmaDn * sigma[2][i];
		}
		//Compute dimensionless gradient squared zi2:
		double zi2_sigmaDiff = pow(nTot,-14./3) * pow(3*M_PI*M_PI,-2./3);
		double sigmaDiff = (nCount==1) ? 0. :
			n[1][i]*n[1][i]*sigma[0][i] - 2*n[0][i]*n[1][i]*sigma[1][i] + n[0][i]*n[0][i]*sigma[2][i];
		double zi2 = zi2_sigmaDiff * sigmaDiff;
		//Compute reduced KE density, z = tauW/tau
		double tauTot = (nCount==1) ? tau[0][i] : tau[0][i]+tau[1][i];
		if(tauTot<nCutoff) return;
		double z_sigma = 0.125/(nTot * tauTot);
		double z = z_sigma * sigmaTot;
		bool zOffRange = false;
		if(z>1.) { z=1.; zOffRange = true; }
		
		//Compute per-particle energy and derivatives:
		double e_rs, e_zeta, e_g, e_t2, e_t2up, e_t2dn, e_zi2, e_z;
		double e = mGGA_eval<variant>(rs, zeta, g, t2, t2up, t2dn, zi2, z,
			e_rs, e_zeta, e_g, e_t2, e_t2up, e_t2dn, e_zi2, e_z);
		if(zOffRange) e_z = 0;
		
		//Compute and store final n/sigma derivatives if required
		if(E_n[0])
		{	if(nCount==1) e_t2 += 2*(e_t2up+e_t2dn);
			else
			{	double E_t2up = scaleFac * nTot * e_t2up;
				double E_t2dn = scaleFac * nTot * e_t2dn;
				E_sigma[0][i] += E_t2up * t2up_sigmaUp;
				E_sigma[2][i] += E_t2dn * t2dn_sigmaDn;
				E_n[0][i] += (-7./3) * E_t2up * t2up / n[0][i];
				E_n[1][i] += (-7./3) * E_t2dn * t2dn / n[1][i];
			}
			double e_nTot = -(e_rs*rs + 7.*e_t2*t2 + 14.*e_zi2*zi2 + 3*e_z*z) / (3.*nTot);
			double e_sigma = e_t2 * t2_sigma + e_z * z_sigma; //derivative w.r.t |DnTot|^2
			double e_tau = -e_z*z/tauTot;
			
			double g_zeta = (1./3) * //Avoid singularities at zeta = +/- 1:
				( (1+zeta > nCutoff ? pow(1+zeta, -1./3) : 0.)
				- (1-zeta > nCutoff ? pow(1-zeta, -1./3) : 0.) );
			e_zeta += (e_g - 2. * e_t2*t2 / g) * g_zeta;
			
			double E_nTot = e + nTot * e_nTot;
			E_n[0][i] += scaleFac*( E_nTot - e_zeta * (zeta-1) );
			E_sigma[0][i] += scaleFac*( (nTot * e_sigma) );
			E_tau[0][i] += scaleFac*( nTot * e_tau );
			if(nCount>1)
			{	E_n[1][i] += scaleFac*( E_nTot - e_zeta * (zeta+1) );
				E_sigma[1][i] += scaleFac*( (nTot * e_sigma) * 2 );
				E_sigma[2][i] += scaleFac*( (nTot * e_sigma) );
				E_tau[1][i] += scaleFac*( nTot * e_tau );
				//Propagate gradients from zi2 to n, sigma
				double E_sigmaDiff = scaleFac*(nTot* (e_zi2 * zi2_sigmaDiff));
				E_sigma[0][i] += n[1][i]*n[1][i] * E_sigmaDiff;
				E_sigma[1][i] -= 2*n[0][i]*n[1][i] * E_sigmaDiff;
				E_sigma[2][i] += n[0][i]*n[0][i] * E_sigmaDiff;
				E_n[0][i] += 2*(sigma[2][i]*n[0][i] - sigma[1][i]*n[1][i]) * E_sigmaDiff;
				E_n[1][i] += 2*(sigma[0][i]*n[1][i] - sigma[1][i]*n[0][i]) * E_sigmaDiff;
			}
		}
		E[i] += scaleFac*( nTot * e ); //energy density per volume
	}
};

//-------------------- meta-GGA exchange implementations -------------------------

//! TPSS or revTPSS Exchange depending on revised = false/true
//! J.P. Perdew et al, Phys. Rev. Lett. 91, 146401 (2003) [TPSS]
//! J.P. Perdew et al, Phys. Rev. Lett. 103, 026403 (2009) [revTPSS]
template<bool revised> __hostanddev__
double mGGA_TPSS_Exchange(double rs, double s2, double q, double z,
	double& e_rs, double& e_s2, double& e_q, double& e_z)
{	//Eqn. (7) of ref and its gradient:
	const double b = 0.40;
	double alphazmz = (5./3)*s2*(1-z) - z; //(alpha-1)*z (rearranging eqn (8) to avoid z=0 issues)
	double alphazmz_z = -(5./3)*s2 - 1.;
	double alphazmz_s2 = (5./3)*(1-z);
	double qbDen = 1./sqrt(z*z + b*alphazmz*(alphazmz+z));
	double qbDenPrime = -0.5*qbDen*qbDen*qbDen;
	double qbDen_z = qbDenPrime*( 2*z + b*alphazmz + b*(2*alphazmz+z)*alphazmz_z );
	double qbDen_s2 = qbDenPrime*( b*(2*alphazmz+z)*alphazmz_s2 );
	double qb = 0.45*alphazmz*qbDen + (2./3) * s2;
	double qb_z = 0.45*(alphazmz_z*qbDen + alphazmz*qbDen_z);
	double qb_s2 = 0.45*(alphazmz_s2*qbDen + alphazmz*qbDen_s2) + (2./3);
	//Eqn. (10) of ref and its gradient:
	const double kappa = 0.804;
	const double mu = revised ? 0.14 : 0.21951;
	const double c = revised ? 2.35204 : 1.59096;
	const double e = revised ? 2.1677 : 1.537;
	double z2 = z*z, s4=s2*s2;
	//--- Term 1 of numerator:
	double xNumTerm1_s2 = 10./81 + c*(revised ? z2*z : z2)/((1+z2)*(1+z2));
	double xNumTerm1 = xNumTerm1_s2 * s2;
	double xNumTerm1_z = s2 * c*(revised ? z2*(3-z2) : 2*z*(1-z2))/((1+z2)*(1+z2)*(1+z2));
	//--- Term 3 of numerator
	double xNumTerm3arg = 0.18*z2+0.5*s4;
	double xNumTerm3_qb = (-73./405)*sqrt(xNumTerm3arg);
	double xNumTerm3 = xNumTerm3_qb * qb;
	double xNumTerm3_z = 0.18*z*(xNumTerm3/xNumTerm3arg);
	double xNumTerm3_s2 = 0.5*s2*(xNumTerm3/xNumTerm3arg);
	//--- Numerator
	double xNum = xNumTerm1 + (146./2025)*qb*qb + xNumTerm3
		+ (100./(6561*kappa))*s4 + (4.*sqrt(e)/45)*z2 + (e*mu)*s4*s2;
	double xNum_qb = (146./2025)*2*qb +xNumTerm3_qb;
	double xNum_z = xNumTerm1_z + xNumTerm3_z + (4.*sqrt(e)/45)*2*z;
	double xNum_s2 = xNumTerm1_s2 + xNumTerm3_s2 + (100./(6561*kappa))*2*s2 + (e*mu)*3*s4;
	//--- Denominator
	double xDenSqrt = 1./(1+sqrt(e)*s2);
	double xDen = xDenSqrt*xDenSqrt;
	double xDen_s2 = -2*sqrt(e)*xDen*xDenSqrt;
	//--- Eqn (10) for x:
	double x = xNum*xDen;
	double x_s2 = (xNum_s2 + xNum_qb*qb_s2)*xDen + xNum*xDen_s2;
	double x_z = (xNum_z + xNum_qb*qb_z)*xDen;
	//TPSS Enhancement factor:
	double F = 1+kappa - (kappa*kappa)/(kappa+x);
	double F_x =  (kappa*kappa)/((kappa+x)*(kappa+x));
	//TPSS Exchange energy per particle:
	double eSlater_rs, eSlater = slaterExchange(rs, eSlater_rs);
	e_rs = eSlater_rs * F;
	e_s2 = eSlater * F_x * x_s2;
	e_q = 0.;
	e_z = eSlater * F_x * x_z;
	return eSlater * F;
}

//! TPSS Exchange: J.P. Perdew et al, Phys. Rev. Lett. 91, 146401 (2003)
template<> __hostanddev__ double mGGA_eval<mGGA_X_TPSS>(double rs, double s2, double q, double z,
	double& e_rs, double& e_s2, double& e_q, double& e_z)
{	return mGGA_TPSS_Exchange<false>(rs, s2, q, z, e_rs, e_s2, e_q, e_z);
}

//! revTPSS Exchange: J.P. Perdew et al, Phys. Rev. Lett. 103, 026403 (2009)
template<> __hostanddev__ double mGGA_eval<mGGA_X_revTPSS>(double rs, double s2, double q, double z,
	double& e_rs, double& e_s2, double& e_q, double& e_z)
{	return mGGA_TPSS_Exchange<true>(rs, s2, q, z, e_rs, e_s2, e_q, e_z);
}


//-------------------- meta-GGA correlation implementations -------------------------

//! Compute beta(rs) for the TPSS/revTPSS correlation functionals
template<bool revised> __hostanddev__
double betaTPSS(double rs, double& beta_rs)
{	if(revised==false)
	{	beta_rs = 0.; //The constant value used in PBE:
		return 0.06672455060314922; 
	}
	else
	{	//Eqn. (3) of the revTPSS ref
		const double num_rs=0.1, num = 1+num_rs*rs;
		const double den_rs=0.1778, den = 1+den_rs*rs;
		const double beta0 = 0.066725;
		beta_rs = beta0*(num_rs*den-num*den_rs)/(den*den);
		return beta0*num/den;
	}
}

//! TPSS or revTPSS Correlation depending on revised = false/true
//! J.P. Perdew et al, Phys. Rev. Lett. 91, 146401 (2003) [TPSS]
//! J.P. Perdew et al, Phys. Rev. Lett. 103, 026403 (2009) [revTPSS]
template<bool revised> __hostanddev__
double mGGA_TPSS_Correlation(
	double rs, double zeta, double g, double t2,
	double t2up, double t2dn, double zi2, double z,
	double& e_rs, double& e_zeta, double& e_g, double& e_t2,
	double& e_t2up, double& e_t2dn, double& e_zi2, double& e_z)
{	
	//Compute C(zeta,0) and its derivatives (eqn (13))
	const double C0 = revised ?  0.59  : 0.53;
	const double C1 = revised ? 0.9269 : 0.87;
	const double C2 = revised ? 0.6225 : 0.50;
	const double C3 = revised ? 2.1540 : 2.26;
	double zeta2 = zeta*zeta;
	double Czeta0 = C0 + zeta2*(C1 + zeta2*(C2 + zeta2*(C3)));
	double Czeta0_zeta = zeta*(2*C1 + zeta2*(4*C2 + zeta2*(6*C3)));
	//Compute C(zeta,zi) and its derivatives (eqn (14))
	double zetapCbrt = pow(1+zeta, 1./3);
	double zetamCbrt = pow(1-zeta, 1./3);
	double Cnum = (1+zeta)*zetapCbrt * (1-zeta)*zetamCbrt; //bring (1+/-zeta)^-4/3 from denominator to numerator
	double Cnum_zeta = (-8./3)*zeta*zetapCbrt*zetamCbrt;
	double Cden_zi2 = 0.5*((1+zeta)*zetapCbrt + (1-zeta)*zetamCbrt);
	double Cden = Cnum + zi2*Cden_zi2;
	double Cden_zeta = Cnum_zeta + (2./3)*zi2*(zetapCbrt - zetamCbrt);
	double Cratio = Cnum/Cden, Cratio2 = Cratio*Cratio, Cratio4 = Cratio2*Cratio2;
	double C = Czeta0*Cratio4;
	double C_zeta = Czeta0_zeta*Cratio4 + 4*Czeta0*Cratio2*Cratio*(Cnum_zeta/Cden - Cratio*Cden_zeta/Cden);
	double C_zi2 = -4*Czeta0*Cratio4*Cden_zi2/Cden;
	if(!Cnum && !Cden) { C=Czeta0; C_zeta=0.; C_zi2=0.; } //Avoid 0/0 error
	
	//Ingredients for eqn (12):
	//PBE correlation at target spin densities:
	double ec_rs, ec_zeta, ec_g, ec_t2;
	double beta_rs, beta = betaTPSS<revised>(rs, beta_rs);
	double ec = GGA_PBE_correlation(beta, beta_rs, rs, zeta, g, t2, ec_rs, ec_zeta, ec_g, ec_t2);
	const double gPol = pow(2., -1./3); //g for a fully polarized density
	//PBE correlation with up-spins alone:
	double ecUp, ecUp_rs, ecUp_zeta, ecUp_t2up;
	{	double ecUp_rsUp, ecUp_zetaPol, ecUp_gPol;
		double rsUp = rs/(zetapCbrt*gPol);
		double betaUp_rsUp, betaUp = betaTPSS<revised>(rsUp, betaUp_rsUp);
		ecUp = GGA_PBE_correlation(betaUp, betaUp_rsUp,
			rsUp, 1., gPol, t2up, ecUp_rsUp, ecUp_zetaPol, ecUp_gPol, ecUp_t2up);
		ecUp_rs = ecUp_rsUp * rsUp / rs;
		ecUp_zeta = ecUp_rsUp * rsUp * (1+zeta>nCutoff ? (-1./3)/(1+zeta) : 0.);
	}
	//PBE correlation with down-spins alone:
	double ecDn, ecDn_rs, ecDn_zeta, ecDn_t2dn;
	if(!zeta && t2up==t2dn)
	{	ecDn = ecUp;
		ecDn_rs = ecUp_rs;
		ecDn_zeta = -ecUp_zeta;
		ecDn_t2dn = ecUp_t2up;
	}
	else
	{	double ecDn_rsDn, ecDn_zetaPol, ecDn_gPol;
		double rsDn = rs/(zetamCbrt*gPol);
		double betaDn_rsDn, betaDn = betaTPSS<revised>(rsDn, betaDn_rsDn);
		ecDn = GGA_PBE_correlation(betaDn, betaDn_rsDn,
			rsDn, 1., gPol, t2dn, ecDn_rsDn, ecDn_zetaPol, ecDn_gPol, ecDn_t2dn);
		ecDn_rs = ecDn_rsDn * rsDn / rs;
		ecDn_zeta = ecDn_rsDn * rsDn * (1-zeta>nCutoff ? (1./3)/(1-zeta) : 0.);
	}
	//Compute ecTilde = 0.5*(1+zeta) max(ec, ecUp) + 0.5*(1-zeta) max(ec, ecDn):
	double ecTilde=0., ecTilde_rs=0., ecTilde_zeta=0., ecTilde_g=0.;
	double ecTilde_t2=0., ecTilde_t2up=0., ecTilde_t2dn=0.;
	if(ec > ecUp)
	{	double scale = 0.5*(1+zeta);
		ecTilde      += scale*ec;
		ecTilde_rs   += scale*ec_rs;
		ecTilde_zeta += scale*ec_zeta + 0.5*ec;
		ecTilde_g    += scale*ec_g;
		ecTilde_t2   += scale*ec_t2;
	}
	else
	{	double scale = 0.5*(1+zeta);
		ecTilde      += scale*ecUp;
		ecTilde_rs   += scale*ecUp_rs;
		ecTilde_zeta += scale*ecUp_zeta + 0.5*ecUp;
		ecTilde_t2up += scale*ecUp_t2up;
	}
	if(ec > ecDn)
	{	double scale = 0.5*(1-zeta);
		ecTilde      += scale*ec;
		ecTilde_rs   += scale*ec_rs;
		ecTilde_zeta += scale*ec_zeta - 0.5*ec;
		ecTilde_g    += scale*ec_g;
		ecTilde_t2   += scale*ec_t2;
	}
	else
	{	double scale = 0.5*(1-zeta);
		ecTilde      += scale*ecDn;
		ecTilde_rs   += scale*ecDn_rs;
		ecTilde_zeta += scale*ecDn_zeta - 0.5*ecDn;
		ecTilde_t2dn += scale*ecDn_t2dn;
	}
	//Put together eqn. (12):
	double z2=z*z, z3=z2*z;
	double ecPKZB_ec = (1+C*z2);
	double ecPKZB_ecTilde = -(1+C)*z2;
	double ecPKZB = ecPKZB_ec*ec + ecPKZB_ecTilde*ecTilde;
	double ecPKZB_C = z2*ec - z2*ecTilde;
	double ecPKZB_z = 2*z*(C*ec - (1+C)*ecTilde);
	//Put together the final correlation energy (eqn. (11)):
	const double d = 2.8;
	double e = ecPKZB * (1 + d*ecPKZB*z3);
	double e_ecPKZB = 1 + 2*d*ecPKZB*z3;
	e_rs = e_ecPKZB*(ecPKZB_ec*ec_rs + ecPKZB_ecTilde*ecTilde_rs);
	e_zeta = e_ecPKZB*(ecPKZB_C*C_zeta + ecPKZB_ec*ec_zeta + ecPKZB_ecTilde*ecTilde_zeta);
	e_g = e_ecPKZB*(ecPKZB_ec*ec_g + ecPKZB_ecTilde*ecTilde_g);
	e_t2 = e_ecPKZB*(ecPKZB_ec*ec_t2 + ecPKZB_ecTilde*ecTilde_t2);
	e_t2up = e_ecPKZB*ecPKZB_ecTilde*ecTilde_t2up;
	e_t2dn = e_ecPKZB*ecPKZB_ecTilde*ecTilde_t2dn;
	e_zi2 = e_ecPKZB*ecPKZB_C*C_zi2;
	e_z = e_ecPKZB*ecPKZB_z + 3*d*ecPKZB*ecPKZB*z2;
	return e;
}

//! TPSS Correlation: J.P. Perdew et al, Phys. Rev. Lett. 91, 146401 (2003)
template<> __hostanddev__ double mGGA_eval<mGGA_C_TPSS>(
	double rs, double zeta, double g, double t2,
	double t2up, double t2dn, double zi2, double z,
	double& e_rs, double& e_zeta, double& e_g, double& e_t2,
	double& e_t2up, double& e_t2dn, double& e_zi2, double& e_z)
{
	return mGGA_TPSS_Correlation<false>(rs, zeta, g, t2, t2up, t2dn, zi2, z,
		e_rs, e_zeta, e_g, e_t2, e_t2up, e_t2dn, e_zi2, e_z);
}

//! revTPSS Correlation: J.P. Perdew et al, Phys. Rev. Lett. 103, 026403 (2009)
template<> __hostanddev__ double mGGA_eval<mGGA_C_revTPSS>(
	double rs, double zeta, double g, double t2,
	double t2up, double t2dn, double zi2, double z,
	double& e_rs, double& e_zeta, double& e_g, double& e_t2,
	double& e_t2up, double& e_t2dn, double& e_zi2, double& e_z)
{
	return mGGA_TPSS_Correlation<true>(rs, zeta, g, t2, t2up, t2dn, zi2, z,
		e_rs, e_zeta, e_g, e_t2, e_t2up, e_t2dn, e_zi2, e_z);
}

#endif // JDFTX_ELECTRONIC_EXCORR_INTERNAL_MGGA_H
