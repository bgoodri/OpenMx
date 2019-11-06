#include <ostream>
#include "omxExpectation.h"
#include "path.h"
#include "EnableWarnings.h"

void PathCalc::prepM(FitContext *fc)
{
	if (!mio) mxThrow("PathCalc::prepM but no PathCalcIO for mean");
	mio->recompute(fc);
	if (ignoreVersion || versionM != mio->getVersion(fc)) {
		mio->refresh(fc);
		versionM = mio->getVersion(fc);
	}
	if (verbose >= 2) mxPrintMat("M", mio->full);
}

void PathCalc::prepS(FitContext *fc)
{
	sio->recompute(fc);
	if (ignoreVersion || versionS != sio->getVersion(fc)) {
		if (!useSparse) {
			sio->refresh(fc);
		} else {
			sio->refreshSparse(fc, 0.0);
		}
		versionS = sio->getVersion(fc);
	}
	if (verbose >= 2) mxPrintMat("S", sio->full);
}

void PathCalc::prepA(FitContext *fc)
{
	aio->recompute(fc);
	if (ignoreVersion || versionIA != aio->getVersion(fc)) {
		refreshA(fc, 1.0);
		versionIA = aio->getVersion(fc);
	}
}

void PathCalc::init1()
{
	if (algoSet) mxThrow("PathCalc::init() but algoSet");
	if (mio) {
		mio->full.resize(numVars, 1);
		//fullM.setZero(); all coeff are copied
	}
	if (!useSparse) {
		aio->full.resize(numVars, numVars);
		aio->full.setZero();
		sio->full.resize(numVars, numVars);
		sio->full.setZero();
	} else {
		aio->sparse.resize(numVars, numVars);
		aio->sparse.reserve(2*numVars);
		aio->sparse.uncompress();
		sio->sparse.resize(numVars, numVars);
		sio->sparse.reserve(2*numVars);
		sio->sparse.uncompress();
		sparseIdent.resize(numVars, numVars);
		sparseIdent.setIdentity();
		sparseIdent.makeCompressed();
	}
	polyRep.resize(numVars);

	obsMap.resize(numVars);
	obsMap.setConstant(-1);
	auto &lf = *latentFilter;
	for (int vx=0, ox=0; vx < numVars; ++vx) {
		if (!lf[vx]) continue;
		obsMap[vx] = ox++;
	}
}

void PathCalc::init2()
{
	algoSet = true;
}

void PathCalc::setAlgo(FitContext *fc, bool _boker2019)
{
	if (!_boker2019 && std::any_of(isProductNode->begin(), isProductNode->end(),
																 [](bool x){ return x; })) {
		mxThrow("Must use Boker2019 when product nodes are present");
	}
	boker2019 = _boker2019;

	init1();

	if (!boker2019) {
		determineShallowDepth(fc);
		if (verbose >= 1) mxLog("PathCalc: depth %d", numIters);
	} else {
		
	}

	init2();
}

void PathCalc::determineShallowDepth(FitContext *fc)
{
	if (!Global->RAMInverseOpt) return;

	int maxDepth = std::min(numVars, 30);
	if (Global->RAMMaxDepth != NA_INTEGER) {
		maxDepth = std::min(maxDepth, Global->RAMMaxDepth);
	}
	refreshA(fc, 1.0);
	if (!useSparse) {
		if ((aio->full.diagonal().array() != 0).any()) mxThrow("A matrix has non-zero diagonal");
		Eigen::MatrixXd curProd = aio->full;
		for (int tx=1; tx <= maxDepth; ++tx) {
			if (false) {
				mxLog("tx=%d", tx);
				mxPrintMat("curProd", curProd);
			}
			curProd *= aio->full.transpose();
			if ((curProd.array() == 0.0).all()) {
				numIters = tx - 1;
				break;
			}
		}
	} else {
		typeof(aio->sparse) curProd = aio->sparse;
		int prev = curProd.nonZeros();
		for (int tx=1; tx <= maxDepth; ++tx) {
			curProd = (curProd * aio->sparse.transpose()).eval();
			if (tx == 1 && prev <= curProd.nonZeros()) break;  // no improvement
			if (curProd.nonZeros() == 0) {
				numIters = tx - 1;
				break;
			}
		}
  }
}

void PathCalc::evaluate(FitContext *fc, bool doFilter)
{
	if (boker2019) mxThrow("PathCalc::evaluate but boker2019=TRUE");

	aio->recompute(fc);
	const unsigned fver = 0xb01dface;
	if (!ignoreVersion && versionIA == aio->getVersion(fc) + doFilter*fver) {
		//mxLog("PathCalc<avft>::evaluate() in cache");
		return;
	}
	versionIA = aio->getVersion(fc) + doFilter*fver;

	if (numIters >= 0) {
		refreshA(fc, 1.0);
		// could further optimize using std::swap? (see old code)
		if (!useSparse) {
			IA = aio->full;
			IA.diagonal().array() += 1;
			for (int iter=1; iter <= numIters; ++iter) {
				IA *= aio->full;
				IA.diagonal().array() += 1;
				//{ Eigen::MatrixXd tmp = out; mxPrintMat("out", tmp); }
			}
			if (verbose >= 2) mxPrintMat("IA", IA);
		} else {
			sparseIA = aio->sparse;
			for (int vx=0; vx < numVars; ++vx) sparseIA.coeffRef(vx,vx) += 1;
			for (int iter=1; iter <= numIters; ++iter) {
				sparseIA = (sparseIA * aio->sparse).eval();
				for (int vx=0; vx < numVars; ++vx) sparseIA.coeffRef(vx,vx) += 1;
			}
			if (verbose >= 2) {
				IA = sparseIA;
				mxPrintMat("IA", IA);
			}
		}
	} else {
		refreshA(fc, -1.0);
		if (!useSparse) {
			aio->full.diagonal().array() = 1;
			Eigen::FullPivLU< Eigen::MatrixXd > lu(aio->full);
			IA.resize(numVars, numVars);
			IA.setIdentity();
			IA = lu.solve(IA);
			if (verbose >= 2) mxPrintMat("IA", IA);
		} else {
			for (int vx=0; vx < numVars; ++vx) aio->sparse.coeffRef(vx,vx) = 1.0;
			aio->sparse.makeCompressed();
			if (!sparseLUanal) {
				sparseLUanal = true;
				sparseLU.analyzePattern(aio->sparse);
			}
			sparseLU.factorize(aio->sparse);
			if (sparseLU.info() != Eigen::Success) {
				mxThrow("Failed to invert A matrix; %s",
								sparseLU.lastErrorMessage().c_str());
			}
			sparseIA = sparseLU.solve(sparseIdent);
			if (verbose >= 2) {
				IA = sparseIA;
				mxPrintMat("IA", IA);
			}
		}
	}

	if (doFilter) {
		// We built A transposed so we can quickly filter columns
		auto &lf = *latentFilter;
		if (!useSparse) {
			for (int rx=0, dx=0; rx < IA.rows(); ++rx) {
				if (!lf[rx]) continue;
				IA.col(dx) = IA.col(rx);
				dx += 1;
			}
			IA.conservativeResize(numVars, numObs);
			if (verbose >= 2) mxPrintMat("IAF", IA);
		} else {
			// Switch to filterOuter http://eigen.tuxfamily.org/bz/show_bug.cgi?id=1130 TODO
			sparseIA.uncompress();
			Eigen::SparseMatrix<double>::Index *op = sparseIA.outerIndexPtr();
			Eigen::SparseMatrix<double>::Index *nzp = sparseIA.innerNonZeroPtr();
			int dx = 0;
			for (int cx=0; cx < numVars; ++cx) {
				if (!lf[cx]) continue;
				op[dx] = op[cx];
				nzp[dx] = nzp[cx];
				++dx;
			}
			op[dx] = op[numVars];
			sparseIA.conservativeResize(numVars, numObs);
			if (verbose >= 2) {
				IA = sparseIA;
				mxPrintMat("IAF", IA);
			}
		}
	}
}

void PathCalc::appendPolyRep(int nn, std::vector<int> &status)
{
	if (status[nn] == 2) return;
	if (status[nn] == 1) mxThrow("Asymmetric matrix is cyclic");
	status[nn] = 1;
	
	for (int ii=0; ii < aio->full.rows(); ++ii) {
		if (ii == nn || status[ii] == 2 || aio->full(nn,ii) == 0) continue;
		appendPolyRep(ii, status);
	}
	for (int ii=0; ii < aio->full.rows(); ++ii) {
		if (ii == nn || aio->full(nn,ii) == 0) continue;
		Polynomial< double > term(aio->full(nn,ii));
		//mxLog("A %d %d %f", ii,nn,aio->full(nn,ii));
		//std::cout << std::string(polyRep[ii]) << "\n";
		term *= polyRep[ii];
		//std::cout << std::string(polyRep[nn]) << "OP " << isProductNode[nn] << " " << std::string(term) << "\n";
		if ((*isProductNode)[nn]) {
			polyRep[nn] *= term;
		} else {
			polyRep[nn] += term;
		}
		//std::cout << "result: " << std::string(polyRep[nn]) << "\n";
	}

	status[nn] = 2;
}

void PathCalc::buildPolynomial(FitContext *fc)
{
	prepS(fc);
	prepA(fc);
	unsigned curV = versionS + versionIA;
	if (mio) {
		prepM(fc);
		curV += versionM;
	}
	if (!ignoreVersion && versionPoly == curV) return;
	versionPoly = curV;

	for (auto &p1 : polyRep) p1.clear();

	if (mio) {
		for (int ii=0; ii < numVars; ++ii) {
			polyRep[ii].addMonomial(Monomial< double >(mio->full(ii)));
		}
	}

	/* old version with Cholesky, doesn't work with non-positive definite S, but probably easier to extend to derivatives. Also, 
	 * polynomials are more sparse with the Cholesky variant. An alternative could be to use a sign switch in the Cholesky decomposition,
	 * thereby working with normally distributed variables of variance either 1 or -1. 
	 double[][] cholesky = null;
	 try {
	 cholesky = Statik.choleskyDecompose(symVal,0.001);
	 } catch (Exception e) {
	 throw new RuntimeException("Polynomial representation is impossible because symmetric matrix is not positive definite: "+e);
	 }

	 int[] x = new int[anzFac]; int k = 0; for (int i=0; i<anzFac; i++) if (cholesky[i][i]!=0.0) x[i] = k++;
	 for (int i=0; i<anzFac; i++) for (int j=0; j<=i; j++) if (cholesky[i][j] != 0.0) polynomialRepresentation[i].addMonomial(cholesky[i][j], x[j]);
	 polynomialRepresentationVariances = Statik.ensureSize(polynomialRepresentationVariances, k);
	 for (int i=0; i<polynomialRepresentationVariances.length; i++) polynomialRepresentationVariances[i] = 1.0;
	*/

	// Add option to use Cholesky with fallback to SelfAdjointEigenSolver TODO
	symSolver.compute(sio->full);
	auto &symEv = symSolver.eigenvalues();
	auto &symVec = symSolver.eigenvectors();
	for (int jj=0; jj < numVars; ++jj) {
		if (symEv(jj) == 0) continue;
		for (int ii=0; ii < numVars; ++ii) {
			if (symVec(ii,jj) == 0) continue;
			polyRep[ii].addMonomial(symVec(ii,jj), jj);
		}
	}

	// for (int ii=0; ii < numVars; ++ii) {
	// 	std::cout << ii << ":" << std::string(polyRep[ii]) << "\n";
	// }

	std::vector<int> status(numVars, 0);
	for (int ii=0; ii<numVars; ii++) {
		appendPolyRep(ii, status);
	}

	// mxPrintMat("vec", symVec);
	if (false) {
		for (int ii=0; ii < numVars; ++ii) {
			std::cout << ii << " " << symEv[ii] << ":" << std::string(polyRep[ii]) << "\n";
		}
	}

	// Could be smarter and avoid latents except when needed TODO
	meanOut.resize(numVars);
	for (int ii=0; ii<numVars; ++ii) {
		meanOut[ii] = polynomialToMoment(polyRep[ii], symEv);
	}
}
