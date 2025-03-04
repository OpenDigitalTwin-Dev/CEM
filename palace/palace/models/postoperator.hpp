// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef PALACE_MODELS_POST_OPERATOR_HPP
#define PALACE_MODELS_POST_OPERATOR_HPP

#include <complex>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mfem.hpp>
#include "fem/gridfunction.hpp"
#include "fem/interpolator.hpp"
#include "linalg/operator.hpp"
#include "linalg/vector.hpp"
#include "models/domainpostoperator.hpp"
#include "models/lumpedportoperator.hpp"
#include "models/surfacepostoperator.hpp"

namespace palace
{

class CurlCurlOperator;
class ErrorIndicator;
class IoData;
class LaplaceOperator;
class MaterialOperator;
class SpaceOperator;
class SurfaceCurrentOperator;
class WavePortOperator;

//
// A class to handle solution postprocessing.
//
class PostOperator
{
private:
  // Reference to material property operator (not owned).
  const MaterialOperator &mat_op;

  // Surface boundary and domain postprocessors.
  const SurfacePostOperator surf_post_op;
  const DomainPostOperator dom_post_op;

  // Objects for grid function postprocessing from the FE solution.
  mutable std::unique_ptr<GridFunction> E, B, V, A;
  std::unique_ptr<mfem::VectorCoefficient> S, E_sr, E_si, B_sr, B_si, A_s, J_sr, J_si;
  std::unique_ptr<mfem::Coefficient> U_e, U_m, V_s, Q_sr, Q_si;

  // Wave port boundary mode field postprocessing.
  struct WavePortFieldData
  {
    std::unique_ptr<mfem::VectorCoefficient> E0r, E0i;
  };
  std::map<int, WavePortFieldData> port_E0;

  // Lumped and wave port voltage and current (R, L, and C branches) caches updated when
  // the grid functions are set.
  struct PortPostData
  {
    std::complex<double> P, V, I[3], S;
  };
  std::map<int, PortPostData> lumped_port_vi, wave_port_vi;
  bool lumped_port_init, wave_port_init;

  // Data collection for writing fields to disk for visualization and sampling points.
  mutable mfem::ParaViewDataCollection paraview, paraview_bdr;
  mutable InterpolationOperator interp_op;
  double mesh_Lc0;
  void InitializeDataCollection(const IoData &iodata);

public:
  PostOperator(const IoData &iodata, SpaceOperator &space_op, const std::string &name);
  PostOperator(const IoData &iodata, LaplaceOperator &laplace_op, const std::string &name);
  PostOperator(const IoData &iodata, CurlCurlOperator &curlcurl_op,
               const std::string &name);

  // Access to surface and domain postprocessing objects.
  const auto &GetSurfacePostOp() const { return surf_post_op; }
  const auto &GetDomainPostOp() const { return dom_post_op; }

  // Return options for postprocessing configuration.
  bool HasE() const { return E != nullptr; }
  bool HasB() const { return B != nullptr; }
  bool HasImag() const { return HasE() ? E->HasImag() : B->HasImag(); }

  // Populate the grid function solutions for the E- and B-field using the solution vectors
  // on the true dofs. For the real-valued overload, the electric scalar potential can be
  // specified too for electrostatic simulations. The output mesh and fields are
  // nondimensionalized consistently (B ~ E (L₀ ω₀ E₀⁻¹)).
  void SetEGridFunction(const ComplexVector &e, bool exchange_face_nbr_data = true);
  void SetBGridFunction(const ComplexVector &b, bool exchange_face_nbr_data = true);
  void SetEGridFunction(const Vector &e, bool exchange_face_nbr_data = true);
  void SetBGridFunction(const Vector &b, bool exchange_face_nbr_data = true);
  void SetVGridFunction(const Vector &v, bool exchange_face_nbr_data = true);
  void SetAGridFunction(const Vector &a, bool exchange_face_nbr_data = true);

  // Access grid functions for field solutions.
  auto &GetEGridFunction()
  {
    MFEM_ASSERT(E, "Missing GridFunction object when accessing from PostOperator!");
    return *E;
  }
  auto &GetBGridFunction()
  {
    MFEM_ASSERT(B, "Missing GridFunction object when accessing from PostOperator!");
    return *B;
  }
  auto &GetVGridFunction()
  {
    MFEM_ASSERT(V, "Missing GridFunction object when accessing from PostOperator!");
    return *V;
  }
  auto &GetAGridFunction()
  {
    MFEM_ASSERT(A, "Missing GridFunction object when accessing from PostOperator!");
    return *A;
  }

  // Postprocess the total electric and magnetic field energies in the electric and magnetic
  // fields.
  double GetEFieldEnergy() const;
  double GetHFieldEnergy() const;

  // Postprocess the electric and magnetic field energies in the domain with the given
  // index.
  double GetEFieldEnergy(int idx) const;
  double GetHFieldEnergy(int idx) const;

  // Postprocess the electric or magnetic field flux for a surface index using the computed
  // electcric field and/or magnetic flux density field solutions.
  std::complex<double> GetSurfaceFlux(int idx) const;

  // Postprocess the partitipation ratio for interface lossy dielectric losses in the
  // electric field mode.
  double GetInterfaceParticipation(int idx, double E_m) const;

  // Update cached port voltages and currents for lumped and wave port operators.
  void UpdatePorts(const LumpedPortOperator &lumped_port_op,
                   const WavePortOperator &wave_port_op, double omega = 0.0)
  {
    UpdatePorts(lumped_port_op, omega);
    UpdatePorts(wave_port_op, omega);
  }
  void UpdatePorts(const LumpedPortOperator &lumped_port_op, double omega = 0.0);
  void UpdatePorts(const WavePortOperator &wave_port_op, double omega = 0.0);

  // Postprocess the energy in lumped capacitor or inductor port boundaries with index in
  // the provided set.
  double GetLumpedInductorEnergy(const LumpedPortOperator &lumped_port_op) const;
  double GetLumpedCapacitorEnergy(const LumpedPortOperator &lumped_port_op) const;

  // Postprocess the S-parameter for recieving lumped or wave port index using the electric
  // field solution.
  std::complex<double> GetSParameter(const LumpedPortOperator &lumped_port_op, int idx,
                                     int source_idx) const;
  std::complex<double> GetSParameter(const WavePortOperator &wave_port_op, int idx,
                                     int source_idx) const;

  // Postprocess the circuit voltage and current across lumped port index using the electric
  // field solution. When the internal grid functions are real-valued, the returned voltage
  // has only a nonzero real part.
  std::complex<double> GetPortPower(const LumpedPortOperator &lumped_port_op,
                                    int idx) const;
  std::complex<double> GetPortPower(const WavePortOperator &wave_port_op, int idx) const;
  std::complex<double> GetPortVoltage(const LumpedPortOperator &lumped_port_op,
                                      int idx) const;
  std::complex<double> GetPortVoltage(const WavePortOperator &wave_port_op, int idx) const;
  std::complex<double>
  GetPortCurrent(const LumpedPortOperator &lumped_port_op, int idx,
                 LumpedPortData::Branch branch = LumpedPortData::Branch::TOTAL) const;
  std::complex<double> GetPortCurrent(const WavePortOperator &wave_port_op, int idx) const;

  // Postprocess the EPR for the electric field solution and lumped port index.
  double GetInductorParticipation(const LumpedPortOperator &lumped_port_op, int idx,
                                  double E_m) const;

  // Postprocess the coupling rate for radiative loss to the given I-O port index.
  double GetExternalKappa(const LumpedPortOperator &lumped_port_op, int idx,
                          double E_m) const;

  // Write to disk the E- and B-fields extracted from the solution vectors. Note that fields
  // are not redimensionalized, to do so one needs to compute: B <= B * (μ₀ H₀), E <= E *
  // (Z₀ H₀), V <= V * (Z₀ H₀ L₀), etc.
  void WriteFields(int step, double time) const;
  void WriteFieldsFinal(const ErrorIndicator *indicator = nullptr) const;

  // Probe the E- and B-fields for their vector-values at speceified locations in space.
  // Locations of probes are set up in constructor from configuration file data. If
  // the internal grid functions are real-valued, the returned fields have only nonzero real
  // parts. Output vectors are ordered by vector dimension, that is [v1x, v1y, v1z, v2x,
  // v2y, v2z, ...].
  const auto &GetProbes() const { return interp_op.GetProbes(); }
  std::vector<std::complex<double>> ProbeEField() const;
  std::vector<std::complex<double>> ProbeBField() const;

  // Get the associated MPI communicator.
  MPI_Comm GetComm() const
  {
    return (E) ? E->ParFESpace()->GetComm() : B->ParFESpace()->GetComm();
  }
};

}  // namespace palace

#endif  // PALACE_MODELS_POST_OPERATOR_HPP
