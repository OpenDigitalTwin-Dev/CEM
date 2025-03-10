// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef PALACE_FEM_COEFFICIENT_HPP
#define PALACE_FEM_COEFFICIENT_HPP

#include <complex>
#include <memory>
#include <utility>
#include <vector>
#include <mfem.hpp>
#include "fem/gridfunction.hpp"
#include "models/materialoperator.hpp"

// XX TODO: Add bulk element Eval() overrides to speed up postprocessing (also needed in
//          mfem::DataCollection classes.

namespace palace
{

//
// Derived coefficients which compute single values on internal boundaries where a possibly
// discontinuous function is given as an input grid function. These are all cheap to
// construct by design. All methods assume the provided grid function is ready for parallel
// comm on shared faces after a call to ExchangeFaceNbrData.
//

// Base class for coefficients which need to evaluate a GridFunction in a domain element
// attached to a boundary element, or both domain elements on either side for internal
// boundaries.
class BdrGridFunctionCoefficient
{
protected:
  // XX TODO: For thread-safety (multiple threads evaluating a coefficient simultaneously),
  //          the FET, FET.Elem1, and FET.Elem2 objects cannot be shared
  const mfem::ParMesh &mesh;
  mfem::FaceElementTransformations FET;
  mfem::IsoparametricTransformation T1, T2;

  bool GetBdrElementNeighborTransformations(int i, const mfem::IntegrationPoint &ip)
  {
    // Get the element transformations neighboring the element, and optionally set the
    // integration point too.
    return GetBdrElementNeighborTransformations(i, mesh, FET, T1, T2, &ip);
  }

  static bool UseElem12(const mfem::FaceElementTransformations &FET,
                        const MaterialOperator &mat_op)
  {
    // For interior faces with no way to distinguish on which side to evaluate quantities,
    // evaluate on both (using the average or sum, depending on the application).
    return (FET.Elem2 && mat_op.GetLightSpeedMax(FET.Elem2->Attribute) ==
                             mat_op.GetLightSpeedMax(FET.Elem1->Attribute));
  }

  static bool UseElem2(const mfem::FaceElementTransformations &FET,
                       const MaterialOperator &mat_op, bool side_n_min)
  {
    // For interior faces, compute the value on the side where the speed of light is larger
    // (refractive index is smaller, typically should choose the vacuum side). For cases
    // where the speeds are the same, use element 1.
    return (FET.Elem2 && side_n_min &&
            mat_op.GetLightSpeedMax(FET.Elem2->Attribute) >
                mat_op.GetLightSpeedMax(FET.Elem1->Attribute));
  }

public:
  BdrGridFunctionCoefficient(const mfem::ParMesh &mesh) : mesh(mesh) {}

  // For a boundary element, return the element transformation objects for the neighboring
  // domain elements. FET.Elem2 may be nullptr if the boundary is a true one-sided boundary,
  // but if it is shared with another subdomain then it will be populated. Expects
  // ParMesh::ExchangeFaceNbrData has been called already.
  static bool GetBdrElementNeighborTransformations(
      int i, const mfem::ParMesh &mesh, mfem::FaceElementTransformations &FET,
      mfem::IsoparametricTransformation &T1, mfem::IsoparametricTransformation &T2,
      const mfem::IntegrationPoint *ip = nullptr);

  // Return normal vector to the boundary element at an integration point. For a face
  // element, the normal points out of the element (from element 1 into element 2, if it
  // exists). This convention can be flipped with the optional parameter. It is assumed
  // that the element transformation has already been configured at the integration point
  // of interest.
  static void GetNormal(mfem::ElementTransformation &T, mfem::Vector &normal,
                        bool invert = false)
  {
    MFEM_ASSERT(normal.Size() == T.GetSpaceDim(),
                "Size mismatch for normal vector (space dimension = " << T.GetSpaceDim()
                                                                      << ")!");
    mfem::CalcOrtho(T.Jacobian(), normal);
    normal /= invert ? -normal.Norml2() : normal.Norml2();
  }

  // 3D cross product.
  static void Cross3(const mfem::Vector &A, const mfem::Vector &B, mfem::Vector &C,
                     bool add = false)
  {
    MFEM_ASSERT(A.Size() == B.Size() && A.Size() == C.Size() && A.Size() == 3,
                "BdrGridFunctionCoefficient cross product expects a mesh in 3D space!");
    if (add)
    {
      C(0) += A(1) * B(2) - A(2) * B(1);
      C(1) += A(2) * B(0) - A(0) * B(2);
      C(2) += A(0) * B(1) - A(1) * B(0);
    }
    else
    {
      C(0) = A(1) * B(2) - A(2) * B(1);
      C(1) = A(2) * B(0) - A(0) * B(2);
      C(2) = A(0) * B(1) - A(1) * B(0);
    }
  }
};

// Computes surface current Jₛ = n x H = n x μ⁻¹ B on boundaries from B as a vector grid
// function where n is an inward normal (computes -n x H for outward normal n). For a
// two-sided internal boundary, the contributions from both sides add.
class BdrSurfaceCurrentVectorCoefficient : public mfem::VectorCoefficient,
                                           public BdrGridFunctionCoefficient
{
private:
  const mfem::ParGridFunction &B;
  const MaterialOperator &mat_op;

public:
  BdrSurfaceCurrentVectorCoefficient(const mfem::ParGridFunction &B,
                                     const MaterialOperator &mat_op)
    : mfem::VectorCoefficient(mat_op.SpaceDimension()),
      BdrGridFunctionCoefficient(*B.ParFESpace()->GetParMesh()), B(B), mat_op(mat_op)
  {
  }

  using mfem::VectorCoefficient::Eval;
  void Eval(mfem::Vector &V, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    // Get neighboring elements.
    MFEM_ASSERT(T.ElementType == mfem::ElementTransformation::BDR_ELEMENT,
                "Unexpected element type in BdrSurfaceCurrentVectorCoefficient!");
    bool ori = GetBdrElementNeighborTransformations(T.ElementNo, ip);

    // For interior faces, compute Jₛ = n x H = n x μ⁻¹ (B1 - B2), where B1 (B2) is B in
    // element 1 (element 2) and n points into element 1.
    double W_data[3], VU_data[3];
    mfem::Vector W(W_data, vdim), VU(VU_data, vdim);
    B.GetVectorValue(*FET.Elem1, FET.Elem1->GetIntPoint(), W);
    mat_op.GetInvPermeability(FET.Elem1->Attribute).Mult(W, VU);
    if (FET.Elem2)
    {
      // Double-sided, not a true boundary. Add result with opposite normal.
      double VL_data[3];
      mfem::Vector VL(VL_data, vdim);
      B.GetVectorValue(*FET.Elem2, FET.Elem2->GetIntPoint(), W);
      mat_op.GetInvPermeability(FET.Elem2->Attribute).Mult(W, VL);
      VU -= VL;
    }

    // Orient with normal pointing into element 1.
    double normal_data[3];
    mfem::Vector normal(normal_data, vdim);
    GetNormal(T, normal, ori);
    V.SetSize(vdim);
    Cross3(normal, VU, V);
  }
};

// Helper for BdrSurfaceFluxCoefficient.
enum class SurfaceFluxType
{
  ELECTRIC,
  MAGNETIC,
  POWER
};

// Computes the flux Φₛ = F ⋅ n with F = B or ε D on interior boundary elements using B or
// E given as a vector grid function. For a two-sided internal boundary, the contributions
// from both sides can either add or be averaged.
template <SurfaceFluxType Type>
class BdrSurfaceFluxCoefficient : public mfem::Coefficient,
                                  public BdrGridFunctionCoefficient
{
private:
  const mfem::ParGridFunction *E, *B;
  const MaterialOperator &mat_op;
  bool two_sided;
  const mfem::Vector &x0;

  void GetLocalFlux(mfem::ElementTransformation &T, mfem::Vector &V) const;

public:
  BdrSurfaceFluxCoefficient(const mfem::ParGridFunction *E, const mfem::ParGridFunction *B,
                            const MaterialOperator &mat_op, bool two_sided,
                            const mfem::Vector &x0)
    : mfem::Coefficient(), BdrGridFunctionCoefficient(E ? *E->ParFESpace()->GetParMesh()
                                                        : *B->ParFESpace()->GetParMesh()),
      E(E), B(B), mat_op(mat_op), two_sided(two_sided), x0(x0)
  {
    MFEM_VERIFY(
        (E || (Type != SurfaceFluxType::ELECTRIC && Type != SurfaceFluxType::POWER)) &&
            (B || (Type != SurfaceFluxType::MAGNETIC && Type != SurfaceFluxType::POWER)),
        "Missing E or B field grid function for surface flux coefficient!");
  }

  double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip) override
  {
    // Get neighboring elements.
    MFEM_ASSERT(T.ElementType == mfem::ElementTransformation::BDR_ELEMENT,
                "Unexpected element type in BdrSurfaceFluxCoefficient!");
    bool ori = GetBdrElementNeighborTransformations(T.ElementNo, ip);

    // For interior faces, compute either F ⋅ n as the average or by adding the
    // contributions from opposite sides with opposite normals.
    const int vdim = T.GetSpaceDim();
    double VU_data[3];
    mfem::Vector VU(VU_data, vdim);
    GetLocalFlux(*FET.Elem1, VU);
    if (FET.Elem2)
    {
      // Double-sided, not a true boundary.
      double VL_data[3];
      mfem::Vector VL(VL_data, vdim);
      GetLocalFlux(*FET.Elem2, VL);
      if (two_sided)
      {
        // Add result with opposite normal.
        VU -= VL;
      }
      else
      {
        // Take the average of the values on both sides.
        VU += VL;
        VU *= 0.5;
      }
    }

    // Dot with normal direction and assign appropriate sign. The normal is oriented to
    // point into element 1.
    double normal_data[3];
    mfem::Vector normal(normal_data, vdim);
    GetNormal(T, normal, ori);
    double flux = VU * normal;
    if (two_sided)
    {
      return flux;
    }
    else
    {
      // Orient outward from the surface with the given center.
      double x_data[3];
      mfem::Vector x(x_data, vdim);
      T.Transform(ip, x);
      x -= x0;
      return (x * normal < 0.0) ? -flux : flux;
    }
  }
};

template <>
inline void BdrSurfaceFluxCoefficient<SurfaceFluxType::ELECTRIC>::GetLocalFlux(
    mfem::ElementTransformation &T, mfem::Vector &V) const
{
  // Flux D.
  double W_data[3];
  mfem::Vector W(W_data, T.GetSpaceDim());
  E->GetVectorValue(T, T.GetIntPoint(), W);
  mat_op.GetPermittivityReal(T.Attribute).Mult(W, V);
}

template <>
inline void BdrSurfaceFluxCoefficient<SurfaceFluxType::MAGNETIC>::GetLocalFlux(
    mfem::ElementTransformation &T, mfem::Vector &V) const
{
  // Flux B.
  B->GetVectorValue(T, T.GetIntPoint(), V);
}

template <>
inline void BdrSurfaceFluxCoefficient<SurfaceFluxType::POWER>::GetLocalFlux(
    mfem::ElementTransformation &T, mfem::Vector &V) const
{
  // Flux E x H = E x μ⁻¹ B.
  double W1_data[3], W2_data[3];
  mfem::Vector W1(W1_data, T.GetSpaceDim()), W2(W2_data, T.GetSpaceDim());
  B->GetVectorValue(T, T.GetIntPoint(), W1);
  mat_op.GetInvPermeability(T.Attribute).Mult(W1, W2);
  E->GetVectorValue(T, T.GetIntPoint(), W1);
  V.SetSize(W1.Size());
  Cross3(W1, W2, V);
}

// Helper for InterfaceDielectricCoefficient.
enum class InterfaceDielectricType
{
  DEFAULT,
  MA,
  MS,
  SA
};

// Computes a single-valued α Eᵀ E on boundaries from E given as a vector grid function.
// Uses the neighbor element on a user specified side to compute a single-sided value for
// potentially discontinuous solutions for an interior boundary element. The four cases
// correspond to a generic interface vs. specializations for metal-air, metal-substrate,
// and subtrate-air interfaces following:
//   J. Wenner et al., Surface loss simulations of superconducting coplanar waveguide
//     resonators, Appl. Phys. Lett. (2011).
template <InterfaceDielectricType Type>
class InterfaceDielectricCoefficient : public mfem::Coefficient,
                                       public BdrGridFunctionCoefficient
{
private:
  const GridFunction &E;
  const MaterialOperator &mat_op;
  const double t_i, epsilon_i;
  bool side_n_min;

  void Initialize(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip,
                  mfem::Vector *normal)
  {
    // Get neighboring elements and the normal vector, oriented to point into element 1.
    MFEM_ASSERT(T.ElementType == mfem::ElementTransformation::BDR_ELEMENT,
                "Unexpected element type in InterfaceDielectricCoefficient!");
    bool ori = GetBdrElementNeighborTransformations(T.ElementNo, ip);
    if (normal)
    {
      GetNormal(T, *normal, ori);
    }
  }

  int GetLocalVectorValue(const mfem::ParGridFunction &U, mfem::Vector &V) const
  {
    if (UseElem12(FET, mat_op))
    {
      // Doesn't make much sense to have a result from both sides here, so just take the
      // side with the larger solution (in most cases, one might be zero).
      double W_data[3];
      U.GetVectorValue(*FET.Elem1, FET.Elem1->GetIntPoint(), V);
      mfem::Vector W(W_data, V.Size());
      U.GetVectorValue(*FET.Elem2, FET.Elem2->GetIntPoint(), W);
      if (V * V < W * W)
      {
        V = W;
        return FET.Elem2->Attribute;
      }
      else
      {
        return FET.Elem1->Attribute;
      }
    }
    else if (UseElem2(FET, mat_op, side_n_min))
    {
      U.GetVectorValue(*FET.Elem2, FET.Elem2->GetIntPoint(), V);
      return FET.Elem2->Attribute;
    }
    else
    {
      U.GetVectorValue(*FET.Elem1, FET.Elem1->GetIntPoint(), V);
      return FET.Elem1->Attribute;
    }
  }

public:
  InterfaceDielectricCoefficient(const GridFunction &E, const MaterialOperator &mat_op,
                                 double t_i, double epsilon_i, bool side_n_min)
    : mfem::Coefficient(), BdrGridFunctionCoefficient(*E.ParFESpace()->GetParMesh()), E(E),
      mat_op(mat_op), t_i(t_i), epsilon_i(epsilon_i), side_n_min(side_n_min)
  {
  }

  double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip) override;
};

template <>
inline double InterfaceDielectricCoefficient<InterfaceDielectricType::DEFAULT>::Eval(
    mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip)
{
  // Get single-sided solution and neighboring element attribute.
  double V_data[3];
  mfem::Vector V(V_data, T.GetSpaceDim());
  Initialize(T, ip, nullptr);
  GetLocalVectorValue(E.Real(), V);
  double V2 = V * V;
  if (E.HasImag())
  {
    GetLocalVectorValue(E.Imag(), V);
    V2 += V * V;
  }

  // No specific interface, use full field evaluation: 0.5 * t * ε * |E|² .
  return 0.5 * t_i * epsilon_i * V2;
}

template <>
inline double InterfaceDielectricCoefficient<InterfaceDielectricType::MA>::Eval(
    mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip)
{
  // Get single-sided solution and neighboring element attribute.
  double V_data[3], normal_data[3];
  mfem::Vector V(V_data, T.GetSpaceDim()), normal(normal_data, T.GetSpaceDim());
  Initialize(T, ip, &normal);
  GetLocalVectorValue(E.Real(), V);
  double Vn = V * normal;
  double Vn2 = Vn * Vn;
  if (E.HasImag())
  {
    GetLocalVectorValue(E.Imag(), V);
    Vn = V * normal;
    Vn2 += Vn * Vn;
  }

  // Metal-air interface: 0.5 * t / ε_MA * |E_n|² .
  return 0.5 * t_i / epsilon_i * Vn2;
}

template <>
inline double InterfaceDielectricCoefficient<InterfaceDielectricType::MS>::Eval(
    mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip)
{
  // Get single-sided solution and neighboring element attribute.
  double V_data[3], W_data[3], normal_data[3];
  mfem::Vector V(V_data, T.GetSpaceDim()), W(W_data, T.GetSpaceDim()),
      normal(normal_data, T.GetSpaceDim());
  Initialize(T, ip, &normal);
  int attr = GetLocalVectorValue(E.Real(), V);
  mat_op.GetPermittivityReal(attr).Mult(V, W);
  double Vn = W * normal;
  double Vn2 = Vn * Vn;
  if (E.HasImag())
  {
    GetLocalVectorValue(E.Imag(), V);
    mat_op.GetPermittivityReal(attr).Mult(V, W);
    Vn = W * normal;
    Vn2 += Vn * Vn;
  }

  // Metal-substrate interface: 0.5 * t / ε_MS * |(ε_S E)_n|² .
  return 0.5 * t_i / epsilon_i * Vn2;
}

template <>
inline double InterfaceDielectricCoefficient<InterfaceDielectricType::SA>::Eval(
    mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip)
{
  // Get single-sided solution and neighboring element attribute.
  double V_data[3], normal_data[3];
  mfem::Vector V(V_data, T.GetSpaceDim()), normal(normal_data, T.GetSpaceDim());
  Initialize(T, ip, &normal);
  GetLocalVectorValue(E.Real(), V);
  double Vn = V * normal;
  V.Add(-Vn, normal);
  double Vn2 = Vn * Vn;
  double Vt2 = V * V;
  if (E.HasImag())
  {
    GetLocalVectorValue(E.Imag(), V);
    Vn = V * normal;
    V.Add(-Vn, normal);
    Vn2 += Vn * Vn;
    Vt2 += V * V;
  }

  // Substrate-air interface: 0.5 * t * (ε_SA * |E_t|² + 1 / ε_SA * |E_n|²) .
  return 0.5 * t_i * (epsilon_i * Vt2 + Vn2 / epsilon_i);
}

// Helper for EnergyDensityCoefficient.
enum class EnergyDensityType
{
  ELECTRIC,
  MAGNETIC
};

// Returns the local energy density evaluated as 1/2 Dᴴ E or 1/2 Hᴴ B for real-valued
// material coefficients. For internal boundary elements, the solution is taken on the side
// of the element with the larger-valued speed of light.
template <EnergyDensityType Type>
class EnergyDensityCoefficient : public mfem::Coefficient, public BdrGridFunctionCoefficient
{
private:
  const GridFunction &U;
  const MaterialOperator &mat_op;
  bool side_n_min;

  double GetLocalEnergyDensity(mfem::ElementTransformation &T) const;

public:
  EnergyDensityCoefficient(const GridFunction &U, const MaterialOperator &mat_op,
                           bool side_n_min)
    : mfem::Coefficient(), BdrGridFunctionCoefficient(*U.ParFESpace()->GetParMesh()), U(U),
      mat_op(mat_op), side_n_min(side_n_min)
  {
  }

  double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip) override
  {
    if (T.ElementType == mfem::ElementTransformation::ELEMENT)
    {
      return GetLocalEnergyDensity(T);
    }
    else if (T.ElementType == mfem::ElementTransformation::BDR_ELEMENT)
    {
      // Get neighboring elements.
      GetBdrElementNeighborTransformations(T.ElementNo, ip);

      // For interior faces, compute the value on the desired side.
      if (UseElem12(FET, mat_op))
      {
        return std::max(GetLocalEnergyDensity(*FET.Elem1),
                        GetLocalEnergyDensity(*FET.Elem2));
      }
      else if (UseElem2(FET, mat_op, side_n_min))
      {
        return GetLocalEnergyDensity(*FET.Elem2);
      }
      else
      {
        return GetLocalEnergyDensity(*FET.Elem1);
      }
    }
    MFEM_ABORT("Unsupported element type in EnergyDensityCoefficient!");
    return 0.0;
  }
};

template <>
inline double EnergyDensityCoefficient<EnergyDensityType::ELECTRIC>::GetLocalEnergyDensity(
    mfem::ElementTransformation &T) const
{
  // Only the real part of the permittivity contributes to the energy (imaginary part
  // cancels out in the inner product due to symmetry).
  double V_data[3];
  mfem::Vector V(V_data, T.GetSpaceDim());
  U.Real().GetVectorValue(T, T.GetIntPoint(), V);
  double dot = mat_op.GetPermittivityReal(T.Attribute).InnerProduct(V, V);
  if (U.HasImag())
  {
    U.Imag().GetVectorValue(T, T.GetIntPoint(), V);
    dot += mat_op.GetPermittivityReal(T.Attribute).InnerProduct(V, V);
  }
  return 0.5 * dot;
}

template <>
inline double EnergyDensityCoefficient<EnergyDensityType::MAGNETIC>::GetLocalEnergyDensity(
    mfem::ElementTransformation &T) const
{
  double V_data[3];
  mfem::Vector V(V_data, T.GetSpaceDim());
  U.Real().GetVectorValue(T, T.GetIntPoint(), V);
  double dot = mat_op.GetInvPermeability(T.Attribute).InnerProduct(V, V);
  if (U.HasImag())
  {
    U.Imag().GetVectorValue(T, T.GetIntPoint(), V);
    dot += mat_op.GetInvPermeability(T.Attribute).InnerProduct(V, V);
  }
  return 0.5 * dot;
}

// Compute time-averaged Poynting vector Re{E x H⋆}, without the typical factor of 1/2. For
// internal boundary elements, the solution is taken on the side of the element with the
// larger-valued speed of light.
class PoyntingVectorCoefficient : public mfem::VectorCoefficient,
                                  public BdrGridFunctionCoefficient
{
private:
  const GridFunction &E, &B;
  const MaterialOperator &mat_op;
  bool side_n_min;

  void GetLocalPower(mfem::ElementTransformation &T, mfem::Vector &V) const
  {
    double W1_data[3], W2_data[3];
    mfem::Vector W1(W1_data, T.GetSpaceDim()), W2(W2_data, T.GetSpaceDim());
    B.Real().GetVectorValue(T, T.GetIntPoint(), W1);
    mat_op.GetInvPermeability(T.Attribute).Mult(W1, W2);
    E.Real().GetVectorValue(T, T.GetIntPoint(), W1);
    V.SetSize(vdim);
    Cross3(W1, W2, V);
    if (E.HasImag())
    {
      B.Imag().GetVectorValue(T, T.GetIntPoint(), W1);
      mat_op.GetInvPermeability(T.Attribute).Mult(W1, W2);
      E.Imag().GetVectorValue(T, T.GetIntPoint(), W1);
      Cross3(W1, W2, V, true);
    }
  }

public:
  PoyntingVectorCoefficient(const GridFunction &E, const GridFunction &B,
                            const MaterialOperator &mat_op, bool side_n_min)
    : mfem::VectorCoefficient(mat_op.SpaceDimension()),
      BdrGridFunctionCoefficient(*E.ParFESpace()->GetParMesh()), E(E), B(B), mat_op(mat_op),
      side_n_min(side_n_min)
  {
  }

  using mfem::VectorCoefficient::Eval;
  void Eval(mfem::Vector &V, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    if (T.ElementType == mfem::ElementTransformation::ELEMENT)
    {
      GetLocalPower(T, V);
      return;
    }
    else if (T.ElementType == mfem::ElementTransformation::BDR_ELEMENT)
    {
      // Get neighboring elements.
      GetBdrElementNeighborTransformations(T.ElementNo, ip);

      // For interior faces, compute the value on the desired side.
      if (UseElem12(FET, mat_op))
      {
        GetLocalPower(*FET.Elem1, V);
        double W_data[3];
        mfem::Vector W(W_data, V.Size());
        GetLocalPower(*FET.Elem2, W);
        if (V * V < W * W)
        {
          V = W;
        }
      }
      else if (UseElem2(FET, mat_op, side_n_min))
      {
        GetLocalPower(*FET.Elem2, V);
      }
      else
      {
        GetLocalPower(*FET.Elem1, V);
      }
      return;
    }
    MFEM_ABORT("Unsupported element type in PoyntingVectorCoefficient!");
  }
};

// Returns the local vector field evaluated on a boundary element. For internal boundary
// elements, the solution is taken on the side of the element with the larger-valued speed
// of light.
class BdrFieldVectorCoefficient : public mfem::VectorCoefficient,
                                  public BdrGridFunctionCoefficient
{
private:
  const mfem::ParGridFunction &U;
  const MaterialOperator &mat_op;
  bool side_n_min;

public:
  BdrFieldVectorCoefficient(const mfem::ParGridFunction &U, const MaterialOperator &mat_op,
                            bool side_n_min)
    : mfem::VectorCoefficient(mat_op.SpaceDimension()),
      BdrGridFunctionCoefficient(*U.ParFESpace()->GetParMesh()), U(U), mat_op(mat_op),
      side_n_min(side_n_min)
  {
  }

  using mfem::VectorCoefficient::Eval;
  void Eval(mfem::Vector &V, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    // Get neighboring elements.
    MFEM_ASSERT(T.ElementType == mfem::ElementTransformation::BDR_ELEMENT,
                "Unexpected element type in BdrFieldVectorCoefficient!");
    GetBdrElementNeighborTransformations(T.ElementNo, ip);

    // For interior faces, compute the value on the desired side.
    if (UseElem12(FET, mat_op))
    {
      U.GetVectorValue(*FET.Elem1, FET.Elem1->GetIntPoint(), V);
      double W_data[3];
      mfem::Vector W(W_data, V.Size());
      U.GetVectorValue(*FET.Elem2, FET.Elem2->GetIntPoint(), W);
      if (V * V < W * W)
      {
        V = W;
      }
    }
    else if (UseElem2(FET, mat_op, side_n_min))
    {
      U.GetVectorValue(*FET.Elem2, FET.Elem2->GetIntPoint(), V);
    }
    else
    {
      U.GetVectorValue(*FET.Elem1, FET.Elem1->GetIntPoint(), V);
    }
  }
};

// Returns the local scalar field evaluated on a boundary element. For internal boundary
// elements, the solution is taken on the side of the element with the larger-valued speed
// of light.
class BdrFieldCoefficient : public mfem::Coefficient, public BdrGridFunctionCoefficient
{
private:
  const mfem::ParGridFunction &U;
  const MaterialOperator &mat_op;
  bool side_n_min;

public:
  BdrFieldCoefficient(const mfem::ParGridFunction &U, const MaterialOperator &mat_op,
                      bool side_n_min)
    : mfem::Coefficient(), BdrGridFunctionCoefficient(*U.ParFESpace()->GetParMesh()), U(U),
      mat_op(mat_op), side_n_min(side_n_min)
  {
  }

  double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip) override
  {
    // Get neighboring elements.
    MFEM_ASSERT(T.ElementType == mfem::ElementTransformation::BDR_ELEMENT,
                "Unexpected element type in BdrFieldCoefficient!");
    GetBdrElementNeighborTransformations(T.ElementNo, ip);

    // For interior faces, compute the value on the desired side.
    if (UseElem12(FET, mat_op))
    {
      return std::max(U.GetValue(*FET.Elem1, FET.Elem1->GetIntPoint()),
                      U.GetValue(*FET.Elem2, FET.Elem2->GetIntPoint()));
    }
    else if (UseElem2(FET, mat_op, side_n_min))
    {
      return U.GetValue(*FET.Elem2, FET.Elem2->GetIntPoint());
    }
    else
    {
      return U.GetValue(*FET.Elem1, FET.Elem1->GetIntPoint());
    }
  }
};

//
// More helpful coefficient types. Wrapper coefficients allow additions of scalar and vector
// or matrix coefficients. Restricted coefficients only compute the coefficient if for the
// given list of attributes. Sum coefficients own a list of coefficients to add.
//

class VectorWrappedCoefficient : public mfem::VectorCoefficient
{
private:
  std::unique_ptr<mfem::Coefficient> coeff;

public:
  VectorWrappedCoefficient(int dim, std::unique_ptr<mfem::Coefficient> &&coeff)
    : mfem::VectorCoefficient(dim), coeff(std::move(coeff))
  {
  }

  using mfem::VectorCoefficient::Eval;
  void Eval(mfem::Vector &V, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    V.SetSize(vdim);
    V = coeff->Eval(T, ip);
  }
};

class MatrixWrappedCoefficient : public mfem::MatrixCoefficient
{
private:
  std::unique_ptr<mfem::Coefficient> coeff;

public:
  MatrixWrappedCoefficient(int dim, std::unique_ptr<mfem::Coefficient> &&coeff)
    : mfem::MatrixCoefficient(dim), coeff(std::move(coeff))
  {
  }

  void Eval(mfem::DenseMatrix &K, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    K.Diag(coeff->Eval(T, ip), height);
  }
};

template <typename Coefficient>
class RestrictedCoefficient : public Coefficient
{
private:
  const mfem::Array<int> &attr;

public:
  template <typename... T>
  RestrictedCoefficient(const mfem::Array<int> &attr, T &&...args)
    : Coefficient(std::forward<T>(args)...), attr(attr)
  {
  }

  double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip) override
  {
    return (attr.Find(T.Attribute) < 0) ? 0.0 : Coefficient::Eval(T, ip);
  }
};

template <typename Coefficient>
class RestrictedVectorCoefficient : public Coefficient
{
private:
  const mfem::Array<int> &attr;

public:
  template <typename... T>
  RestrictedVectorCoefficient(const mfem::Array<int> &attr, T &&...args)
    : Coefficient(std::forward<T>(args)...), attr(attr)
  {
  }

  void Eval(mfem::Vector &V, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    if (attr.Find(T.Attribute) < 0)
    {
      V.SetSize(this->vdim);
      V = 0.0;
    }
    else
    {
      Coefficient::Eval(V, T, ip);
    }
  }
};

template <typename Coefficient>
class RestrictedMatrixCoefficient : public Coefficient
{
private:
  const mfem::Array<int> &attr;

public:
  template <typename... T>
  RestrictedMatrixCoefficient(const mfem::Array<int> &attr, T &&...args)
    : Coefficient(std::forward<T>(args)...), attr(attr)
  {
  }

  void Eval(mfem::DenseMatrix &K, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    if (attr.Find(T.Attribute) < 0)
    {
      K.SetSize(this->height, this->width);
      K = 0.0;
    }
    else
    {
      Coefficient::Eval(K, T, ip);
    }
  }
};

class SumCoefficient : public mfem::Coefficient
{
private:
  std::vector<std::pair<std::unique_ptr<mfem::Coefficient>, double>> c;

public:
  SumCoefficient() : mfem::Coefficient() {}

  bool empty() const { return c.empty(); }

  void AddCoefficient(std::unique_ptr<mfem::Coefficient> &&coeff, double a = 1.0)
  {
    c.emplace_back(std::move(coeff), a);
  }

  double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip) override
  {
    double val = 0.0;
    for (auto &[coeff, a] : c)
    {
      val += a * coeff->Eval(T, ip);
    }
    return val;
  }
};

class SumVectorCoefficient : public mfem::VectorCoefficient
{
private:
  std::vector<std::pair<std::unique_ptr<mfem::VectorCoefficient>, double>> c;

public:
  SumVectorCoefficient(int d) : mfem::VectorCoefficient(d) {}

  bool empty() const { return c.empty(); }

  void AddCoefficient(std::unique_ptr<mfem::VectorCoefficient> &&coeff, double a = 1.0)
  {
    MFEM_VERIFY(coeff->GetVDim() == vdim,
                "Invalid VectorCoefficient dimensions for SumVectorCoefficient!");
    c.emplace_back(std::move(coeff), a);
  }

  void AddCoefficient(std::unique_ptr<mfem::Coefficient> &&coeff, double a = 1.0)
  {
    c.emplace_back(std::make_unique<VectorWrappedCoefficient>(vdim, std::move(coeff)), a);
  }

  using mfem::VectorCoefficient::Eval;
  void Eval(mfem::Vector &V, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    double U_data[3];
    mfem::Vector U(U_data, vdim);
    V.SetSize(vdim);
    V = 0.0;
    for (auto &[coeff, a] : c)
    {
      coeff->Eval(U, T, ip);
      V.Add(a, U);
    }
  }
};

class SumMatrixCoefficient : public mfem::MatrixCoefficient
{
private:
  std::vector<std::pair<std::unique_ptr<mfem::MatrixCoefficient>, double>> c;

public:
  SumMatrixCoefficient(int d) : mfem::MatrixCoefficient(d) {}
  SumMatrixCoefficient(int h, int w) : mfem::MatrixCoefficient(h, w) {}

  bool empty() const { return c.empty(); }

  void AddCoefficient(std::unique_ptr<mfem::MatrixCoefficient> &&coeff, double a)
  {
    MFEM_VERIFY(coeff->GetHeight() == height && coeff->GetWidth() == width,
                "Invalid MatrixCoefficient dimensions for SumMatrixCoefficient!");
    c.emplace_back(std::move(coeff), a);
  }

  void AddCoefficient(std::unique_ptr<mfem::Coefficient> &&coeff, double a)
  {
    MFEM_VERIFY(width == height, "MatrixWrappedCoefficient can only be constructed for "
                                 "square MatrixCoefficient objects!");
    c.emplace_back(std::make_unique<MatrixWrappedCoefficient>(height, std::move(coeff)), a);
  }

  void Eval(mfem::DenseMatrix &K, mfem::ElementTransformation &T,
            const mfem::IntegrationPoint &ip) override
  {
    double M_data[9];
    mfem::DenseMatrix M(M_data, height, width);
    K.SetSize(height, width);
    K = 0.0;
    for (auto &[coeff, a] : c)
    {
      coeff->Eval(M, T, ip);
      K.Add(a, M);
    }
  }
};

}  // namespace palace

#endif  // PALACE_FEM_COEFFICIENT_HPP
