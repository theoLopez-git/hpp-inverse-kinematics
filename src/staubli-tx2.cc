//
// Copyright (c) 2025 CNRS
// Authors: Florent Lamiraux
//

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.

#include <hpp/constraints/explicit.hh>
#include <hpp/constraints/generic-transformation.hh>
#include <hpp/constraints/matrix-view.hh>
#include <hpp/inverse-kinematics/staubli-tx2.hh>
#include <hpp/manipulation/device.hh>
#include <hpp/manipulation/handle.hh>
#include <hpp/pinocchio/gripper.hh>
#include <hpp/util/debug.hh>
#include <pinocchio/algorithm/jacobian.hpp>
#include <pinocchio/multibody/data.hpp>
#include <../src/configuration-variables.hh>

namespace hpp {
namespace inverseKinematics {

class InverseKinematics : public DifferentiableFunction {
public:
  typedef shared_ptr<InverseKinematics> Ptr_t;
  typedef weak_ptr<InverseKinematics> WkPtr_t;
  static Ptr_t create(const std::string& name, const DevicePtr_t& robot,
		      const JointConstPtr_t& joint1, const JointConstPtr_t& joint2,
		      const Transform3s& frame1, const Transform3s& frame2,
		      const std::string& baseLinkName, const segments_t inConf,
		      const segments_t outConf, const segments_t inVel) {
    return Ptr_t(new InverseKinematics(name, robot, joint1, joint2, frame1, frame2, baseLinkName,
				       inConf, outConf, inVel));
  }
protected:
  InverseKinematics(const std::string& name, const DevicePtr_t& robot,
		    const JointConstPtr_t& joint1, const JointConstPtr_t& joint2,
		    const Transform3s& frame1, const Transform3s& frame2,
		    const std::string& baseLinkName, const segments_t inConf,
		    const segments_t outConf, const segments_t inVel) :
    DifferentiableFunction(BlockIndex::cardinal(inConf), BlockIndex::cardinal(inVel),
			   LiegroupSpace::Rn(6), name), robot_(robot), joint1_ (joint1),
    joint2_ (joint2), frame1_ (frame1), frame2_ (frame2), baseLinkIdx_(
      robot->model().getFrameId(baseLinkName)),
    rootIdx_(robot->model().frames[baseLinkIdx_].parentJoint),
    inConf_(inConf), inVel_(inVel), outConf_(outConf),
    data_(robot->model()) {
  }

  inline matrix3_t cross(const vector3_t& u) {
    matrix3_t res;
    res.setZero();
    res(0,1) = -u(2);
    res(0,2) =  u(1);
    res(1,0) =  u(2);
    res(1,2) = -u(0);
    res(2,0) = -u(1);
    res(2,1) =  u(0);
    return res;
  }

  virtual void impl_compute(LiegroupElementRef result, vectorIn_t argument) const {
    forwardKinematics(argument);
    // pose of the robot root joint
    Transform3s _0Mr(data_.oMi[rootIdx_]);
    // pose of joint1: J2 * F2 * F1^{-1}
    Transform3s joint1Pose(data_.oMi[joint2_->index()] * frame2_ * frame1_.inverse());
    // pose of last joint in robot arm base_link
    Transform3s Minput(_0Mr.inverse() * joint1Pose);
    hppDout(info, "_0Mr=" << _0Mr);
    hppDout(info, "joint1Pose=" << joint1Pose);
    hppDout(info, "Minput=" << Minput);
    // Compute inverse kinematics here

    result.vector().setZero();
  }
  virtual void impl_jacobian(matrixOut_t jacobian, vectorIn_t arg) const {
    forwardKinematics(arg);
    J_.resize(6, inVel_.nbIndices());
    Transform3s _0M1(data_.oMi[joint1_->index()]);
    Transform3s _0M2(data_.oMi[joint2_->index()]);
    Transform3s _0Mr(data_.oMi[rootIdx_]);
    Transform3s _rM2(_0Mr.inverse() * _0M2);
    matrix3_t _0R1(_0M1.rotation());
    matrix3_t _1R2(_0R1.transpose() * _0M2.rotation());
    matrix3_t _1Rr(_0R1.transpose() * _0Mr.rotation());
    matrix3_t _2th_cross(cross(frame2_.translation()));
    // pose of the robot root joint
    Transform3s _0Mg(_0M1*frame1_);
    Transform3s _1Mg(_0M1.inverse() * _0Mg);
    vecctor3_t _1tg(_1Mg.translation());
    vector3_t _rtg((_0Mr.inverse()*_0Mg).translation());

    J2_.resize(6, robot_->numberDof());
    J2_.setZeros();
    Jr_.resize(6, robot_->numberDof());
    Jr_.setZeros();
    J1_.resize(6, robot_->numberDof());
    J1_.setZeros();
    pinocchio::getJointJacobian(robot_->model(), data_, joint2_->index(), pinocchio::LOCAL, J2_);
    pinocchio::getJointJacobian(robot_->model(), data_, rootIdx_, pinocchio::LOCAL, Jr_);
    J2_in_ = inConf_.rview(J2_);
    Jr_in_ = inConf_.rview(Jr_);
    J.topRows(3) = _1R2*(-_2th_cross*J2_in_.bottomRows(3) + J2_in_.topRows(3)) +
      _1Rr*(cross(_rtg)*Jr_in_.bottomRows(3) - Jr_in_.topRows(3)) +
      _0R1*cross(_1tg)*(_1R2*J2_in_.bottomRows(3) - _1Rr*Jr_in.bottomRows(3));
    J.bottomRows(3) = _rM2.rotation() * J2_in_.bottomRows(3) - Jr_in.bottomRows(3);
    matrix6_t Jout(outConf_.rview(J1_));
    jacobian = Jout_.inverse() * J;
  }
private:
  // Compute forward kinematics with only input variables
  void forwardKinematics(vectorIn_t arg) const {
    qsmall_ = inConf_.rview(robot_->currentConfiguration());
    if (qsmall_ != arg) {
      q_ = robot_->currentConfiguration();
      inConf_.lview(q_) = arg;
      robot_->currentConfiguration(q_);
    }
    robot_->computeForwardKinematics(pinocchio::JOINT_POSITION & pinocchio::JACOBIAN);
  }

  DevicePtr_t robot_;
  JointConstPtr_t joint1_, joint2_;
  Transform3s frame1_, frame2_;
  FrameIndex baseLinkIdx_;
  JointIndex rootIdx_;
  Eigen::RowBlockIndices inConf_;
  Eigen::ColBlockIndices inVel_;
  Eigen::RowBlockIndices outConf_;

  // Local members to avoid memory allocation
  mutable vector_t qsmall_, q_;
  mutable ::pinocchio::Data data_;
  mutable matrix_t J_, J1_, J2_, J2_in_, Jr_, Jr_in_;
}; // class InverseKinematics

class StaubliTX2 : public constraints::Explicit {
public:
  typedef shared_ptr<StaubliTX2> Ptr_t;
  typedef weak_ptr<StaubliTX2> WkPtr_t;
  /// Copy object and return shared pointer to copy
  virtual ImplicitPtr_t copy() const{ return createCopy(weak_.lock()); }
  /// Create instance and return shared pointer
  ///
  /// \param name the name of the constraints,
  /// \param robot the robot the constraints is applied to,
  /// \param joint1 the first joint the transformation of which is
  ///               constrained,
  /// \param joint2 the second joint the transformation of which is
  ///               constrained,
  /// \param frame1 position of a fixed frame in joint 1,
  /// \param frame2 position of a fixed frame in joint 2,
  /// \param baseLinkName name of the robot base_link (origin of the robot frame),
  /// \param extraDof extra degree of freedom used to store an integer. This integrer
  ///        determines which inverse kinematics solution to return when several exist.
  /// \note if joint1 is 0x0, joint 1 frame is considered to be the global
  ///       frame.
  static Ptr_t create(
      const std::string& name, const DevicePtr_t& robot,
      const JointConstPtr_t& joint1, const JointConstPtr_t& joint2, const Transform3s& frame1,
      const Transform3s& frame2, const std::string& baseLinkName, size_type extraDof) {
    StaubliTX2* ptr(new StaubliTX2(name, robot, joint1, joint2, frame1, frame2, baseLinkName,
				   extraDof));
    Ptr_t shPtr(ptr);
    WkPtr_t wkPtr(shPtr);
    ptr->init(wkPtr);
    return shPtr;
  }


  static Ptr_t createCopy(const Ptr_t& other) {
    StaubliTX2* ptr(new StaubliTX2(*other));
    Ptr_t shPtr(ptr);
    WkPtr_t wkPtr(shPtr);
    ptr->init(wkPtr);
    return shPtr;
  }

  /// Compute output value assuming right hand side is 0.
  void outputValue(LiegroupElementRef result, vectorIn_t qin, LiegroupElementConstRef rhs) const
  {
    assert(rhs == rhs.space()->neutral());
    explicitFunction()->value(result, qin);
  }
  /// Compute Jacobian value assuming right hand side is 0.
  void jacobianOutputValue(vectorIn_t qin, LiegroupElementConstRef g_value,
			   LiegroupElementConstRef rhs, matrixOut_t jacobian) const
  {
    assert(rhs == rhs.space()->neutral());
    explicitFunction()->jacobian(jacobian, qin);
  }

 protected:
  /// Constructor
  ///
  /// \param name the name of the constraints,
  /// \param robot the robot the constraints is applied to,
  /// \param joint1 the first joint the transformation of which is
  ///               constrained,
  /// \param joint2 the second joint the transformation of which is
  ///               constrained,
  /// \param frame1 position of a fixed frame in joint 1,
  /// \param frame2 position of a fixed frame in joint 2,
  /// \note if joint1 is 0x0, joint 1 frame is considered to be the global
  ///       frame.
  StaubliTX2(const std::string& name, const DevicePtr_t& robot,
	     const JointConstPtr_t& joint1, const JointConstPtr_t& joint2,
	     const Transform3s& frame1, const Transform3s& frame2, const std::string& baseLinkName,
	     size_type extraDof)
    : Explicit(RelativeTransformationR3xSO3::create(name, robot, joint1, joint2,
                                                    frame1, frame2,
                                                    std::vector<bool>(6, true)),
               InverseKinematics::create(name, robot, joint1, joint2,
	           frame1, frame2, baseLinkName, inputConfVariables(robot, joint1, joint2,
								    extraDof),
	           outputConfVariables(joint1), inputVelVariables(robot, joint1, joint2, extraDof)),
               inputConfVariables(robot, joint1, joint2, extraDof), outputConfVariables(joint1),
               inputVelVariables(robot, joint1, joint2, extraDof), outputVelVariables(joint1),
	       ComparisonTypes_t(6*constraints::EqualToZero), std::vector<bool>(6, true)),
      joint1_(joint1),
      joint2_(joint2),
      frame1_(frame1),
      frame2_(frame2),
      extraDof_(extraDof) {
    // Check that extra-dof is actually an extra configuration space variable, i.e. not a
    // configuration variable of pinocchio model
    if (extraDof_ < robot->model().nq) {
      std::ostringstream os;
      os << "Extra degree of freedom (" << extraDof << ") should not be a configuration variable "
	"of the kinematic chain, i.e. should be at least " << robot->model().nq;
      throw std::logic_error(os.str().c_str());
    }
    if (extraDof_ >= robot->configSize()) {
      std::ostringstream os;
      os << "Extra degree of freedom (" << extraDof << ") should be smaller than dimension of "
	"robot configuration space (" << robot->configSize() << ")";
      throw std::logic_error(os.str().c_str());
    }
  }


  /// Copy constructor
  StaubliTX2(const StaubliTX2& other)
    : Explicit(other),
      joint1_(other.joint1_),
      joint2_(other.joint2_),
      frame1_(other.frame1_),
      frame2_(other.frame2_) {}


  /// Store weak pointer to itself
  void init(WkPtr_t weak) {
    Explicit::init(weak);
    weak_ = weak;
  }

 private:
  // Create LiegroupSpace instances to avoid useless allocation.
  JointConstPtr_t joint1_, joint2_;
  Transform3s frame1_;
  Transform3s frame2_;
  size_type extraDof_;
  WkPtr_t weak_;

}; // class StaubliTX2

namespace {
static const matrix3_t I3 = matrix3_t::Identity();

struct NoOutputFunction : public DifferentiableFunction {
  NoOutputFunction(size_type sIn, size_type sInD,
               std::string name = std::string("Empty function"))
      : DifferentiableFunction(sIn, sInD, LiegroupSpace::empty(), name) {
    context("Grasp complement");
  }

  inline void impl_compute(pinocchio::LiegroupElementRef, vectorIn_t) const {}
  inline void impl_jacobian(matrixOut_t, vectorIn_t) const {}
};
}  // namespace

ImplicitPtr_t createGrasp(const GripperPtr_t& gripper, const HandleConstPtr_t& handle,
                          size_type extraDof, const std::string& baseLinkName, std::string n)
{
  if (n.empty()) {
    n = gripper->name() + "_grasps_" + handle->name() + "_(explicit)";
  }

  return StaubliTX2::create(n, gripper->joint()->robot(), gripper->joint(), handle->joint(),
      gripper->objectPositionInJoint(), handle->localPosition(), baseLinkName, extraDof);
}
/// Create a trivial constraint
ImplicitPtr_t createGraspComplement(const GripperPtr_t& gripper, const HandleConstPtr_t& handle,
				    std::string n)
{
  if (n.empty()) {
    n = gripper->name() + "_grasps_" + handle->name() + "/complement_" + "_(empty)";
  }
  DevicePtr_t r = handle->robot();
  return Implicit::create(shared_ptr<NoOutputFunction>(new NoOutputFunction(r->configSize(),
      r->numberDof(), n)), ComparisonTypes_t());
}
/// Create a 6D pregrasp explicit constraint
ImplicitPtr_t createPreGrasp(const GripperPtr_t& gripper, const HandleConstPtr_t& handle,
    size_type extraDof, const value_type& shift, const std::string& baseLinkName, std::string n)
{
  Transform3s M = gripper->objectPositionInJoint() *
                  Transform3s(I3, vector3_t(shift, 0, 0));
  if (n.empty())
    n = "Pregrasp_(explicit)_" + handle->name() + "_" + gripper->name();
  return StaubliTX2::create(n, gripper->joint()->robot(), gripper->joint(), handle->joint(),
			    M, handle->localPosition(), baseLinkName, extraDof);
}
} // namespace inverseKinematics
} // namespace hpp
