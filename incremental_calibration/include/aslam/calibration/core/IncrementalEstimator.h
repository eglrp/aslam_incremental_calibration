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

/** \file IncrementalEstimator.h
    \brief This file defines the IncrementalEstimator class, which implements
           an incremental estimator for robotic calibration problems.
  */

#ifndef ASLAM_CALIBRATION_CORE_INCREMENTAL_ESTIMATOR_H
#define ASLAM_CALIBRATION_CORE_INCREMENTAL_ESTIMATOR_H

#include <cstddef>

#include <aslam-tsvd-solver/aslam-tsvd-solver.h>
#include <aslam/backend/Optimizer2Options.hpp>
#include <boost/shared_ptr.hpp>
#include <Eigen/Core>

namespace sm {
  class ConstPropertyTree;
}

namespace aslam {
  namespace backend {
    class GaussNewtonTrustRegionPolicy;
    class Optimizer2;
    template<typename I> class CompressedColumnMatrix;
  }

  namespace calibration {
    typedef aslam::backend::AslamTruncatedSvdSolver LinearSolver;
    typedef aslam::backend::AslamTruncatedSvdSolver::Options
        LinearSolverOptions;
    class OptimizationProblem;
    class IncrementalOptimizationProblem;

    /** The class IncrementalEstimator implements an incremental estimator
        for robotic calibration problems.
        \brief Incremental estimator
      */
    class IncrementalEstimator {
    public:
      /** \name Types definitions
        @{
        */
      /// Optimization problem type
      typedef OptimizationProblem Batch;
      /// Optimization problem type (shared pointer)
      typedef boost::shared_ptr<OptimizationProblem> BatchSP;
      /// Incremental optimization problem (shared pointer)
      typedef boost::shared_ptr<IncrementalOptimizationProblem>
        IncrementalOptimizationProblemSP;
      /// Self type
      typedef IncrementalEstimator Self;
      /// Optimizer type
      typedef aslam::backend::Optimizer2 Optimizer;
      /// Optimizer options type
      typedef aslam::backend::Optimizer2Options OptimizerOptions;
      /// Optimizer type (shared_ptr)
      typedef boost::shared_ptr<Optimizer> OptimizerSP;
      /// Options for the incremental estimator
      struct Options {
        Options() :
            infoGainDelta(0.2),
            checkValidity(false),
            maxIterationHitIsStillValid(false),
            verbose(false)
        {}
        /// Information gain delta
        double infoGainDelta;
        /// Check validity of the solution
        bool checkValidity;
        /// True means hitting max iterations is still valid
        bool maxIterationHitIsStillValid;
        /// Verbosity of the estimator
        bool verbose;
      };
      /// Return value when adding a batch
      struct ReturnValue {
        /// True if the batch was accepted
        bool batchAccepted;
        /// True if the optimization aborted prior to max iterations and actually decreased the cost
        bool solutionValid;
        /// True if information gain is above the threshold or the theta rank increased
        bool isInformativeBatch;
        /// Information gain
        double informationGain;
        /// Numerical rank of J_psi
        std::ptrdiff_t rankPsi;
        /// Numerical rank deficiency of J_psi
        std::ptrdiff_t rankPsiDeficiency;
        /// Numerical rank of A_theta
        std::ptrdiff_t rankTheta;
        /// Numerical rank deficiency of A_theta
        std::ptrdiff_t rankThetaDeficiency;
        /// SVD tolerance used for this batch
        double svdTolerance;
        /// QR tolerance used for this batch
        double qrTolerance;
        /// Orthonormal basis for the unobservable subspace of theta
        Eigen::MatrixXd nobsBasis;
        /// Orthonormal basis for the unobservable subspace of scaled theta
        Eigen::MatrixXd nobsBasisScaled;
        /// Orthonormal basis for the observable subspace of theta
        Eigen::MatrixXd obsBasis;
        /// Orthonormal basis for the observable subspace of theta
        Eigen::MatrixXd obsBasisScaled;
        /// Covariance of theta
        Eigen::MatrixXd sigma2Theta;
        /// Covariance of scaled theta
        Eigen::MatrixXd sigma2ThetaScaled;
        /// Covariance of theta_obs
        Eigen::MatrixXd sigma2ThetaObs;
        /// Covariance of scaled theta_obs
        Eigen::MatrixXd sigma2ThetaObsScaled;
        /// Singular values of A_theta
        Eigen::VectorXd singularValues;
        /// Singular values of scaled A_theta
        Eigen::VectorXd singularValuesScaled;
        /// Number of iterations
        size_t numIterations;
        /// Cost function at start
        double JStart;
        /// Cost function at end
        double JFinal;
        /// Elapsed time for processing this batch [s]
        double elapsedTime;
        /// The last log2 sum of the singular values
        double svLog2Sum;

        size_t peakMemoryUsage;
        size_t memoryUsage;
        size_t numFlops;
      };
      /** @}
        */

      /** \name Constructors/destructor
        @{
        */
      /// Constructs estimator with group to marginalize and options
      IncrementalEstimator(size_t margGroupId, const Options& options = Options(),
        const LinearSolverOptions& linearSolverOptions = LinearSolverOptions(),
        const OptimizerOptions& optimizerOptions = OptimizerOptions());
      /// Constructs estimator with configuration in property tree
      IncrementalEstimator(const sm::ConstPropertyTree& config);
      /// Copy constructor
      IncrementalEstimator(const Self& other) = delete;
      /// Copy assignment operator
      IncrementalEstimator& operator = (const Self& other) = delete;
      /// Move constructor
      IncrementalEstimator(Self&& other) = delete;
      /// Move assignment operator
      IncrementalEstimator& operator = (Self&& other) = delete;
      /// Destructor
      virtual ~IncrementalEstimator();
      /** @}
        */

      /** \name Methods
        @{
        */
      class TryBatchResult {
       public:
        void accept() { _estimator->acceptBatch(*this); unset(); }
        void reject(bool restoreDesignVariables) { _estimator->rejectBatch(*this, restoreDesignVariables); unset(); }

        const ReturnValue & getReturnValue() const { return *_ret; }
        friend IncrementalEstimator;
        TryBatchResult & operator = (TryBatchResult && tr) = default;
        TryBatchResult(TryBatchResult && tr) = default;
        TryBatchResult() : _estimator(nullptr), _batch(nullptr){};
        bool isSet() { return _batch != nullptr; }
       private:
        void unset() { _batch.reset(); }
        TryBatchResult(IncrementalEstimator & estimator, const BatchSP & batch) : _estimator(&estimator), _batch(batch), _ret(new ReturnValue()){}
        IncrementalEstimator * _estimator;
        BatchSP _batch;
        std::unique_ptr<ReturnValue> _ret;
      };
      friend TryBatchResult;

      /// Try a measurement batch as candidate
      TryBatchResult tryBatch(const BatchSP& batch, bool firstStoreDesignVariables);

      /// Adds a measurement batch to the estimator
      /**
       * This is a convenience function invoking first
       * tryBatch and then accepting or rejecting it
       * exactly when force or TryResult.getResult().isInformativeBatch
       */
      inline ReturnValue addBatch(const BatchSP& batch, bool force = false){
        TryBatchResult tr = tryBatch(batch, !force);
        if (force || tr.getReturnValue().isInformativeBatch){
          tr.accept();
        } else {
          tr.reject(true);
        }
        return tr.getReturnValue();
      }

      /// Removes a measurement batch from the estimator
      void removeBatch(size_t idx);
      /// Removes a measurement batch from the estimator
      void removeBatch(const BatchSP& batch);
      /// Re-runs the optimizer
      ReturnValue reoptimize();
      /** @}
        */

      /** \name Accessors
        @{
        */
      /// Returns the number of batches
      size_t getNumBatches() const;
      /// Returns the incremental optimization problem
      const IncrementalOptimizationProblem* getProblem() const;
      /// Returns the current options
      const Options& getOptions() const;
      /// Returns the current options
      Options& getOptions();
      /// Returns the linear solver options
      const LinearSolverOptions& getLinearSolverOptions() const;
      /// Returns the linear solver options
      LinearSolverOptions& getLinearSolverOptions();
      /// Returns the optimizer options
      const OptimizerOptions& getOptimizerOptions() const;
      /// Returns the optimizer options
      OptimizerOptions& getOptimizerOptions();
      /// Return the marginalized group ID
      size_t getMargGroupId() const;
      /// Returns the last information gain
      double getInformationGain() const;
      /// Returns the current Jacobian transpose if available
      const aslam::backend::CompressedColumnMatrix<std::ptrdiff_t>&
        getJacobianTranspose() const;
      /// Returns the current estimated numerical rank of J_psi
      std::ptrdiff_t getRankPsi() const;
      /// Returns the current estimated numerical rank deficiency of J_psi
      std::ptrdiff_t getRankPsiDeficiency() const;
      /// Returns the current estimated numerical rank of A_theta
      std::ptrdiff_t getRankTheta() const;
      /// Returns the current estimated numerical rank deficiency of A_theta
      std::ptrdiff_t getRankThetaDeficiency() const;
      /// Returns the current tolerance for the SVD decomposition
      double getSVDTolerance() const;
      /// Returns the current tolerance for the QR decomposition
      double getQRTolerance() const;
      /// Returns the orthonormal basis for the unobservable subspace of theta
      const Eigen::MatrixXd& getNobsBasis(bool scaled = false) const;
      /// Returns the orthonormal basis for the observable subspace of theta
      const Eigen::MatrixXd& getObsBasis(bool scaled = false) const;
      /// Returns the covariance of theta
      const Eigen::MatrixXd& getSigma2Theta(bool scaled = false) const;
      /// Returns the covariance of theta_obs
      const Eigen::MatrixXd& getSigma2ThetaObs(bool scaled = false) const;
      /// Returns the singular values of A_theta
      const Eigen::VectorXd& getSingularValues(bool scaled = false) const;
      /// Returns the peak memory usage in bytes
      size_t getPeakMemoryUsage() const;
      /// Returns the current memory usage in bytes
      size_t getMemoryUsage() const;
      /// Returns the number of flops of the linear solver
      double getNumFlops() const;
      /// Returns the current initial cost for the estimator
      double getInitialCost() const;
      /// Returns the current final cost for the estimator
      double getFinalCost() const;

      const Optimizer& getOptimizer() const {
        return *_optimizer;
      }
      Optimizer & getOptimizer() {
        return * _optimizer;
      }

      /// True iff it is using a observability aware linear solver
      bool isObservabilityAware() { return _isObservabilityAware; }

      /** @}
        */

    protected:
      /** \name Protected methods
        @{
        */
      /// Ensures the marginalized variables are well located
      void orderMarginalizedDesignVariables();
      /// Restores the linear solver
      void restoreLinearSolver();

      /// update internal result variables base on given return value
      void updateInternalVariables(const ReturnValue& ret);

      void acceptBatch(TryBatchResult & tryBatchResult);
      void rejectBatch(TryBatchResult & tryBatchResult, bool restoreDesignVariables);

      /** @}
        */

      /** \name Protected members
        @{
        */
      /// Options
      Options _options;
      /// Group ID to marginalize
      size_t _margGroupId;
      /// Underlying optimizer
      OptimizerSP _optimizer;
      /// Underlying optimization problem
      IncrementalOptimizationProblemSP _problem;
      /// Information gain
      double _informationGain;
      /// Sum of the log2 of the singular values of A_theta (up to rankTheta)
      double _svLog2Sum;
      /// Orthonormal basis for the unobservable subspace of theta
      Eigen::MatrixXd _nobsBasis;
      /// Orthonormal basis for the unobservable subspace of scaled theta
      Eigen::MatrixXd _nobsBasisScaled;
      /// Orthonormal basis for the observable subspace of theta
      Eigen::MatrixXd _obsBasis;
      /// Orthonormal basis for the observable subspace of scaled theta
      Eigen::MatrixXd _obsBasisScaled;
      /// Covariance of theta
      Eigen::MatrixXd _sigma2Theta;
      /// Covariance of scaled theta
      Eigen::MatrixXd _sigma2ThetaScaled;
      /// Covariance of theta_obs
      Eigen::MatrixXd _sigma2ThetaObs;
      /// Covariance of scaled theta_obs
      Eigen::MatrixXd _sigma2ThetaObsScaled;
      /// Singular values of A_theta
      Eigen::VectorXd _singularValues;
      /// Singular values of scaled A_theta
      Eigen::VectorXd _singularValuesScaled;
      /// Tolerance for SVD
      double _svdTolerance;
      /// Tolerance for QR
      double _qrTolerance;
      /// Estimated numerical rank of A_theta
      std::ptrdiff_t _rankTheta;
      /// Estimated numerical rank deficiency of A_theta
      std::ptrdiff_t _rankThetaDeficiency;
      /// Estimated numerical rank of J_psi
      std::ptrdiff_t _rankPsi;
      /// Estimated numerical rank deficiency of J_psi
      std::ptrdiff_t _rankPsiDeficiency;
      /// Peak memory usage
      size_t _peakMemoryUsage;
      /// Memory usage
      size_t _memoryUsage;
      /// Number of flops
      double _numFlops;
      /// Initial cost
      double _initialCost;
      /// Final cost
      double _finalCost;

      /// True iff it is using a observability aware linear solver
      bool _isObservabilityAware;
      /** @}
        */

    };

  }
}

#endif // ASLAM_CALIBRATION_CORE_INCREMENTAL_ESTIMATOR_H
