#include <ostream>
#include "omxExpectation.h"
#include "polynomial.h"
#include <Eigen/Eigenvalues>
#include "SelfAdjointEigenSolverNosort.h"
#include "EnableWarnings.h"

class povRAMExpectation : public omxExpectation {
	typedef omxExpectation super;
	unsigned Zversion;
	omxMatrix *_Z, *I, *Ax;
	Eigen::VectorXi dataCols;  // composition of F permutation and expectation->dataColumns
	std::vector<const char *> dataColNames;
	std::vector< omxThresholdColumn > thresholds;
	std::vector<bool> isProductNode;

	void appendPolyRep(int nn, std::vector<int> &status,
										 std::vector< Polynomial< double > > &polyRep);

 public:
	std::vector<bool> latentFilter; // false when latent

	povRAMExpectation() : Zversion(0), _Z(0) {};
	virtual ~povRAMExpectation();

	omxMatrix *cov, *means; // observed covariance and means
	omxMatrix *A, *S, *F, *M;

	int verbose;
	int numIters;

	void studyF();

	virtual void init();
	virtual void compute(FitContext *fc, const char *what, const char *how);
	virtual omxMatrix *getComponent(const char*);
	virtual const std::vector<const char *> &getDataColumnNames() const { return dataColNames; };
	virtual const Eigen::Map<DataColumnIndexVector> getDataColumns() {
		return Eigen::Map<DataColumnIndexVector>(dataCols.data(), numDataColumns);
	}
	virtual std::vector< omxThresholdColumn > &getThresholdInfo() { return thresholds; }
};

omxExpectation *povRAMExpectationInit() { return new povRAMExpectation; }

povRAMExpectation::~povRAMExpectation()
{
	omxFreeMatrix(I);
	omxFreeMatrix(_Z);
	omxFreeMatrix(Ax);
	omxFreeMatrix(cov);
	omxFreeMatrix(means);
}

void povRAMExpectation::init()
{
	canDuplicate = true;

	ProtectedSEXP Rverbose(R_do_slot(rObj, Rf_install("verbose")));
	verbose = Rf_asInteger(Rverbose) + OMX_DEBUG;

	ProtectedSEXP Rdepth(R_do_slot(rObj, Rf_install("depth")));
	numIters = Rf_asInteger(Rdepth);

	M = omxNewMatrixFromSlot(rObj, currentState, "M"); // can be optional? TODO
	A = omxNewMatrixFromSlot(rObj, currentState, "A");
	S = omxNewMatrixFromSlot(rObj, currentState, "S");
	F = omxNewMatrixFromSlot(rObj, currentState, "F");

	int k = A->rows;
	I = omxNewIdentityMatrix(k, currentState);
	_Z = omxInitMatrix(k, k, TRUE, currentState);
	Ax = omxInitMatrix(k, k, TRUE, currentState);

	ProtectedSEXP RprodNode(R_do_slot(rObj, Rf_install("isProductNode")));
	if (Rf_length(RprodNode) != A->cols) {
		mxThrow("isProductNode must be same dimension as A matrix");
	}
	isProductNode.assign(A->cols, false);
	for (int px = 0; px < A->cols; ++px) {
		if (INTEGER(RprodNode)[px]) isProductNode[px] = true;
	}

	int l = F->rows;
	cov = 		omxInitMatrix(l, l, TRUE, currentState);
	means = 	omxInitMatrix(1, l, TRUE, currentState);

	studyF();
}

void povRAMExpectation::studyF()
{
	// Permute the data columns such that the manifest
	// part of F is a diagonal matrix. This permits
	// trivial filtering of the latent variables.
	auto dataColumns = super::getDataColumns();
	auto origDataColumnNames = super::getDataColumnNames();
	auto origThresholdInfo = super::getThresholdInfo();
	EigenMatrixAdaptor eF(F);
	latentFilter.assign(eF.cols(), false);
	dataCols.resize(eF.rows());
	dataColNames.resize(eF.rows(), 0);
	if (!eF.rows()) return;  // no manifests
	for (int cx =0, dx=0; cx < eF.cols(); ++cx) {
		int dest;
		double isManifest = eF.col(cx).maxCoeff(&dest);
		latentFilter[cx] = isManifest;
		if (isManifest) {
			dataColNames[dx] = origDataColumnNames[dest];
			int newDest = dataColumns.size()? dataColumns[dest] : dest;
			dataCols[dx] = newDest;
			if (origThresholdInfo.size()) {
				omxThresholdColumn adj = origThresholdInfo[dest];
				adj.dColumn = dx;
				thresholds.push_back(adj);
			}
			dx += 1;
		}
	}
}

omxMatrix* povRAMExpectation::getComponent(const char* component)
{
	omxMatrix* retval = NULL;

	if(strEQ("cov", component)) {
		retval = cov;
	} else if(strEQ("means", component)) {
		retval = means;
	} else if(strEQ("slope", component)) {
		mxThrow("slope not supported");
	} else if(strEQ("pvec", component)) {
		// Once implemented, change compute function and return pvec
	}
	
	return retval;
}

void povRAMExpectation::appendPolyRep(int nn, std::vector<int> &status,
																			std::vector< Polynomial< double > > &polyRep)
{
	EigenMatrixAdaptor eA(A);
	if (status[nn] == 2) return;
	if (status[nn] == 1) mxThrow("Asymmetric matrix is cyclic");
	status[nn] = 1;
	
	for (int ii=0; ii < A->rows; ++ii) {
		if (ii == nn || status[ii] == 2 || eA(nn,ii) == 0) continue;
		appendPolyRep(ii, status, polyRep);
	}
	for (int ii=0; ii < A->rows; ++ii) {
		if (ii == nn || eA(nn,ii) == 0) continue;
		Polynomial< double > term(eA(nn,ii));
		//mxLog("A %d %d %f", ii,nn,eA(nn,ii));
		//std::cout << std::string(polyRep[ii]) << "\n";
		term *= polyRep[ii];
		//std::cout << std::string(polyRep[nn]) << "OP " << isProductNode[nn] << " " << std::string(term) << "\n";
		if (isProductNode[nn]) {
			polyRep[nn] *= term;
		} else {
			polyRep[nn] += term;
		}
		//std::cout << "result: " << std::string(polyRep[nn]) << "\n";
	}

	status[nn] = 2;
}

template <typename T>
double polynomialToMoment(Polynomial< double > &polyRep, T& symEv)
{
	double erg = 0;
	for (auto &monom : polyRep.monomials) {
		double zwerg = monom.coeff;
		for (size_t ii=0; ii < monom.exponent.size(); ++ii) {
			if (monom.exponent[ii] % 2 == 1) { zwerg = 0; break; }
			for (int jj=0; jj <= (monom.exponent[ii]/2)-1; ++jj) zwerg *= 2*jj+1;
			zwerg *= pow(symEv[ii], monom.exponent[ii]/2.0);
		}
		erg += zwerg;
	}
	//std::cout << std::string(polyRep) << "\n=" << erg << "\n";
	return erg;
}

void povRAMExpectation::compute(FitContext *fc, const char *what, const char *how)
{
	if (F->rows == 0) return;

	omxRecompute(A, fc);
	omxRecompute(S, fc);
	omxRecompute(F, fc);
	if (M) omxRecompute(M, fc);  // currently required TODO

	// if (Zversion != omxGetMatrixVersion(A)) {
	// 	omxShallowInverse(fc, numIters, A, _Z, Ax, I);  // Z = (I-A)^{-1}
	// 	Zversion = omxGetMatrixVersion(A);
	// }

	//EigenMatrixAdaptor eZ(_Z);
	EigenMatrixAdaptor eS(S);
	EigenVectorAdaptor eM(M);
	//mxPrintMat("S", eS);

	std::vector< Polynomial< double > > polyRep(S->rows);
	for (int ii=0; ii < S->rows; ++ii) {
		if (isProductNode[ii] && eM(ii) == 0.0) {
			// I think this should be set up by the frontend TODO
			polyRep[ii].addMonomial(Monomial< double >(1.0));
		}
		else polyRep[ii].addMonomial(Monomial< double >(eM(ii)));
	}

	Eigen::SelfAdjointEigenSolverNosort< Eigen::MatrixXd > sym(eS);
	auto &symEv = sym.eigenvalues();
	auto &symVec = sym.eigenvectors();
	for (int jj=0; jj < S->rows; ++jj) {
		if (symEv(jj) == 0) continue;
		for (int ii=0; ii < S->rows; ++ii) {
			if (symVec(ii,jj) == 0) continue;
			polyRep[ii].addMonomial(symVec(ii,jj), jj);
		}
	}

	// for (int ii=0; ii < S->rows; ++ii) {
	// 	std::cout << ii << ":" << std::string(polyRep[ii]) << "\n";
	// }

	std::vector<int> status(S->rows, 0);
	for (int ii=0; ii<S->rows; ii++) {
		appendPolyRep(ii, status, polyRep);
	}

	// mxPrintMat("vec", symVec);
	// for (int ii=0; ii < S->rows; ++ii) {
	// 	std::cout << ii << " " << symEv[ii] << ":" << std::string(polyRep[ii]) << "\n";
	// }

	EigenMatrixAdaptor sigmaBig(Ax);
	Eigen::VectorXd fullMean(S->rows);
	for (int ii=0; ii<S->rows; ++ii) {
		fullMean[ii] = polynomialToMoment(polyRep[ii], symEv);
	}
	for (int ii=0; ii<S->rows; ii++) {
		for (int jj=ii; jj<S->rows; jj++) {
			auto polyProd = polyRep[ii] * polyRep[jj];
			sigmaBig(ii,jj) = polynomialToMoment(polyProd, symEv) - fullMean[ii]*fullMean[jj];
		}
	}
	sigmaBig.derived() = sigmaBig.selfadjointView<Eigen::Upper>();

  //mxPrintMat("full cov", sigmaBig);
	//mxPrintMat("full mean", fullMean);

	//mxThrow("stop");

	EigenMatrixAdaptor eCov(cov);
	EigenVectorAdaptor eMeans(means);
	subsetCovariance(sigmaBig, [&](int x){ return latentFilter[x]; }, F->rows, eCov);
	subsetVector(fullMean, [&](int x){ return latentFilter[x]; }, F->rows, eMeans);
	//mxPrintMat("cov", eCov);
	//mxPrintMat("mean", eMeans);
}