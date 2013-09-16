/******************************************************************************
 * Copyright (C) 2013 by Jerome Maye                                          *
 * jerome.maye@gmail.com                                                      *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify       *
 * it under the terms of the Lesser GNU General Public License as published by*
 * the Free Software Foundation; either version 3 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * Lesser GNU General Public License for more details.                        *
 *                                                                            *
 * You should have received a copy of the Lesser GNU General Public License   *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.       *
 ******************************************************************************/

#include "aslam/calibration/core/IncrementalEstimator.h"

#include <algorithm>
#include <utility>

#include <boost/make_shared.hpp>

#include <aslam/backend/Optimizer2Options.hpp>
#include <aslam/backend/SparseQRLinearSolverOptions.h>
#include <aslam/backend/SparseQrLinearSystemSolver.hpp>
#include <aslam/backend/GaussNewtonTrustRegionPolicy.hpp>
#include <aslam/backend/Optimizer2.hpp>
#include <aslam/backend/CompressedColumnMatrix.hpp>

#include "aslam/calibration/core/IncrementalOptimizationProblem.h"
#include "aslam/calibration/base/Timestamp.h"
#include "aslam/calibration/exceptions/InvalidOperationException.h"
#include "aslam/calibration/algorithms/marginalize.h"

namespace aslam {
  namespace calibration {

/******************************************************************************/
/* Constructors and Destructor                                                */
/******************************************************************************/

    IncrementalEstimator::IncrementalEstimator(size_t groupId,
        const Options& options) :
        _problem(boost::make_shared<IncrementalOptimizationProblem>()),
        _margGroupId(groupId),
        _mi(0),
        _svLogSum(0),
        _options(options),
        _optimizer(boost::make_shared<Optimizer>()) {
      // optimization options
      aslam::backend::Optimizer2Options& optOptions = _optimizer->options();
      optOptions.verbose = _options._verbose;
      optOptions.maxIterations = _options._maxIterations;
      optOptions.linearSystemSolver = boost::make_shared<LinearSolver>();
      optOptions.trustRegionPolicy = boost::make_shared<TrustRegionPolicy>();

      // attach the problem to the optimizer
      _optimizer->setProblem(_problem);
    }

    IncrementalEstimator::~IncrementalEstimator() {
    }

/******************************************************************************/
/* Accessors                                                                  */
/******************************************************************************/

    const IncrementalOptimizationProblem*
        IncrementalEstimator::getProblem() const {
      return _problem.get();
    }

    const IncrementalEstimator::Options&
        IncrementalEstimator::getOptions() const {
      return _options;
    }

    IncrementalEstimator::Options& IncrementalEstimator::getOptions() {
      return _options;
    }

    double IncrementalEstimator::getMutualInformation() const {
      return _mi;
    }

    size_t IncrementalEstimator::getMargGroupId() const {
      return _margGroupId;
    }

    const aslam::backend::CompressedColumnMatrix<ssize_t>&
        IncrementalEstimator::getJacobianTranspose() const {
      return _optimizer->getSolver<LinearSolver>()->getJacobianTranspose();
    }

    size_t IncrementalEstimator::getRank() const {
      return _optimizer->getSolver<LinearSolver>()->getRank();
    }

    double IncrementalEstimator::getQRTol() const {
      return _optimizer->getSolver<LinearSolver>()->getTol();
    }

    size_t IncrementalEstimator::getCholmodMemoryUsage() const {
      return _optimizer->getSolver<LinearSolver>()->getMemoryUsage();
    }

    const Eigen::MatrixXd& IncrementalEstimator::getNullSpace() const {
      return _NS;
    }

    const Eigen::MatrixXd& IncrementalEstimator::getColumnSpace() const {
      return _CS;
    }

    const Eigen::MatrixXd& IncrementalEstimator::getMarginalizedCovariance()
        const {
      return _Sigma;
    }

    const Eigen::MatrixXd&
        IncrementalEstimator::getProjectedMarginalizedCovariance() const {
      return _SigmaP;
    }

/******************************************************************************/
/* Methods                                                                    */
/******************************************************************************/

    aslam::backend::SolutionReturnValue IncrementalEstimator::optimize() {
      // init the linear solver
      initLinearSolver();

      // optimize
      return _optimizer->optimize();
    }

    IncrementalEstimator::ReturnValue IncrementalEstimator::reoptimize() {
      // ensure marginalized design variables are well located
      orderMarginalizedDesignVariables();

      // optimize
      aslam::backend::SolutionReturnValue srv = optimize();

      // return value
      ReturnValue ret;

      // analyze marginalized system
      const size_t dim = _problem->getGroupDim(_margGroupId);
      const size_t numCols = getJacobianTranspose().rows();
      _svLogSum =  marginalize(getJacobianTranspose(), numCols - dim, ret._NS,
        ret._CS, ret._Sigma, ret._SigmaP, ret._Omega);
      _NS = ret._NS;
      _CS = ret._CS;
      _Sigma = ret._Sigma;
      _SigmaP = ret._SigmaP;

      // update output structure
      ret._batchAccepted = false;
      ret._mi = 0.0;
      ret._rank = getRank();
      ret._qrTol = getQRTol();
      ret._numIterations = srv.iterations;
      ret._JStart = srv.JStart;
      ret._JFinal = srv.JFinal;

      return ret;
    }

    IncrementalEstimator::ReturnValue
        IncrementalEstimator::addBatch(const BatchSP& problem, bool force) {
      // query the time
      const double timeStart = Timestamp::now();

      // insert new batch in the problem
      _problem->add(problem);

      // ensure marginalized design variables are well located
      orderMarginalizedDesignVariables();

      // save design variables in case the batch is rejected
      _problem->saveDesignVariables();

      // optimize
      aslam::backend::SolutionReturnValue srv = optimize();

      // check if the solution is valid
      bool solutionValid = true;
      if (srv.iterations == _optimizer->options().maxIterations ||
          srv.JFinal >= srv.JStart)
        solutionValid = false;

      // batch is kept?
      bool keepBatch = false;

      // return value
      ReturnValue ret;

      // analyze marginalized system
      const size_t dim = _problem->getGroupDim(_margGroupId);
      const size_t numCols = getJacobianTranspose().rows();
      const double svLogSum = marginalize(getJacobianTranspose(), numCols - dim,
        ret._NS, ret._CS, ret._Sigma, ret._SigmaP, ret._Omega);

      // first round of estimation?
      if (!_svLogSum && solutionValid) {
        _svLogSum = svLogSum;
        keepBatch = true;
        ret._mi = 0;
        _NS = ret._NS;
        _CS = ret._CS;
        _Sigma = ret._Sigma;
        _SigmaP = ret._SigmaP;
        _Omega = ret._Omega;
      }
      else {
        // compute MI
        const double mi = 0.5 * (svLogSum - _svLogSum);
        ret._mi = mi;

        // warning for rank going down
        if (ret._CS.cols() < _CS.cols() && _options._verbose)
          std::cerr << "WARNING: RANK GOING DOWN!" << std::endl;

        // MI improvement or rank goes up
        if ((mi > _options._miTol || ret._CS.cols() > _CS.cols())
            && solutionValid) {
          _svLogSum = svLogSum;
          keepBatch = true;
          _mi = mi;
          _NS = ret._NS;
          _CS = ret._CS;
          _Sigma = ret._Sigma;
          _SigmaP = ret._SigmaP;
          _Omega = ret._Omega;
        }
      }

      // update output structure
      ret._batchAccepted = keepBatch || force;
      ret._rank = getRank();
      ret._qrTol = getQRTol();
      ret._numIterations = srv.iterations;
      ret._JStart = srv.JStart;
      ret._JFinal = srv.JFinal;

      // remove batch if necessary
      if (!keepBatch && !force) {
        // kick out the problem from the container
        _problem->remove(problem);

        // restore variables
        _problem->restoreDesignVariables();

        // restore the linear solver
        restoreLinearSolver();
      }

      // insert elapsed time
      ret._elapsedTime = Timestamp::now() - timeStart;

      // insert memory usage
      ret._cholmodMemoryUsage = getCholmodMemoryUsage();

      // output informations
      return ret;
    }

    void IncrementalEstimator::removeBatch(size_t idx) {
      // remove the batch
      _problem->remove(idx);

      // ensure marginalized design variables are well located
      orderMarginalizedDesignVariables();

      // optimize back
      optimize();

      // update mutual information
      const size_t dim = _problem->getGroupDim(_margGroupId);
      const size_t numCols = getJacobianTranspose().rows();
      const double svLogSum = marginalize(getJacobianTranspose(), numCols - dim,
        _NS, _CS, _Sigma, _SigmaP, _Omega);
      _mi = svLogSum - _svLogSum;
      _svLogSum = svLogSum;
    }

    void IncrementalEstimator::removeBatch(const BatchSP& batch) {
      auto it = _problem->getOptimizationProblem(batch);
      if (it != _problem->getOptimizationProblemEnd())
        removeBatch(std::distance(_problem->getOptimizationProblemBegin(), it));
    }

    size_t IncrementalEstimator::getNumBatches() const
    {
      return _problem->getNumOptimizationProblems();
    }

    void IncrementalEstimator::orderMarginalizedDesignVariables() {
      auto groupsOrdering = _problem->getGroupsOrdering();
      auto margGroupIt = std::find(groupsOrdering.begin(),
        groupsOrdering.end(), _margGroupId);
      if (margGroupIt == groupsOrdering.end())
        throw InvalidOperationException(
          "IncrementalEstimator::orderMarginalizedDesignVariables(): "
          "marginalized group ID should appear in the problem");
      else {
        if (*margGroupIt != groupsOrdering.back()) {
          std::swap(*margGroupIt, groupsOrdering.back());
          _problem->setGroupsOrdering(groupsOrdering);
        }
      }
    }

    void IncrementalEstimator::initLinearSolver() {
      // linear solver options
      aslam::backend::SparseQRLinearSolverOptions linearSolverOptions;
      linearSolverOptions.colNorm = _options._colNorm;
      linearSolverOptions.qrTol = _options._qrTol;

      // reset the linear solver
      aslam::backend::Optimizer2Options& optOptions = _optimizer->options();
      optOptions.verbose = _options._verbose;
      optOptions.maxIterations = _options._maxIterations;
      optOptions.linearSystemSolver = boost::make_shared<LinearSolver>();
      optOptions.trustRegionPolicy = boost::make_shared<TrustRegionPolicy>();
      _optimizer->initializeLinearSolver();
      _optimizer->initializeTrustRegionPolicy();

      // set options to the linear solver
      _optimizer->getSolver<LinearSolver>()->setOptions(linearSolverOptions);
    }

    void IncrementalEstimator::restoreLinearSolver() {
      // init the solver
      initLinearSolver();

      // init the matrix structure
      std::vector<aslam::backend::DesignVariable*> dvs;
      const size_t numDVS = _problem->numDesignVariables();
      dvs.reserve(numDVS);
      size_t columnBase = 0;
      for (size_t i = 0; i < numDVS; ++i) {
        aslam::backend::DesignVariable* dv = _problem->designVariable(i);
        if (dv->isActive()) {
          dvs.push_back(dv);
          dv->setBlockIndex(dvs.size() - 1);
          dv->setColumnBase(columnBase);
          columnBase += dv->minimalDimensions();
        }
      }
      std::vector<aslam::backend::ErrorTerm*> ets;
      const size_t numETS = _problem->numErrorTerms();
      ets.reserve(numETS);
      size_t dim = 0;
      for (size_t i = 0; i < numETS; ++i) {
        aslam::backend::ErrorTerm* et = _problem->errorTerm(i);
        et->setRowBase(dim);
        dim += et->dimension();
        ets.push_back(et);
      }
      _optimizer->getSolver<LinearSolver>()->initMatrixStructure(dvs, ets,
        false);

      // build the system
      _optimizer->getSolver<LinearSolver>()->buildSystem(
        _optimizer->options().nThreads, true);

      // run the QR analysis
      _optimizer->getSolver<LinearSolver>()->analyzeSystem();
    }

  }
}
