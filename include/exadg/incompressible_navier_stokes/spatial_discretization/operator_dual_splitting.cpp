/*  ______________________________________________________________________
 *
 *  ExaDG - High-Order Discontinuous Galerkin for the Exa-Scale
 *
 *  Copyright (C) 2021 by the ExaDG authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *  ______________________________________________________________________
 */

#include <exadg/incompressible_navier_stokes/preconditioners/multigrid_preconditioner_momentum.h>
#include <exadg/incompressible_navier_stokes/spatial_discretization/operator_dual_splitting.h>
#include <exadg/solvers_and_preconditioners/preconditioners/block_jacobi_preconditioner.h>
#include <exadg/solvers_and_preconditioners/preconditioners/inverse_mass_preconditioner.h>
#include <exadg/solvers_and_preconditioners/preconditioners/jacobi_preconditioner.h>

namespace ExaDG
{
namespace IncNS
{
template<int dim, typename Number>
OperatorDualSplitting<dim, Number>::OperatorDualSplitting(
  std::shared_ptr<Grid<dim> const>                      grid_in,
  std::shared_ptr<dealii::Mapping<dim> const>           mapping_in,
  std::shared_ptr<MultigridMappings<dim, Number>> const multigrid_mappings_in,
  std::shared_ptr<BoundaryDescriptor<dim> const>        boundary_descriptor_in,
  std::shared_ptr<FieldFunctions<dim> const>            field_functions_in,
  Parameters const &                                    parameters_in,
  std::string const &                                   field_in,
  MPI_Comm const &                                      mpi_comm_in)
  : ProjectionBase(grid_in,
                   mapping_in,
                   multigrid_mappings_in,
                   boundary_descriptor_in,
                   field_functions_in,
                   parameters_in,
                   field_in,
                   mpi_comm_in)
{
}

template<int dim, typename Number>
OperatorDualSplitting<dim, Number>::~OperatorDualSplitting()
{
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::apply_velocity_divergence_term(VectorType &       dst,
                                                                   VectorType const & src) const
{
  this->divergence_operator.apply(dst, src);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_div_term_body_forces_add(VectorType &   dst,
                                                                     double const & time) const
{
  this->evaluation_time = time;

  VectorType src_dummy;
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_rhs_ppe_div_term_body_forces_boundary_face,
                               this,
                               dst,
                               src_dummy);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_rhs_ppe_div_term_body_forces_boundary_face(
  dealii::MatrixFree<dim, Number> const & matrix_free,
  VectorType &                            dst,
  VectorType const &,
  Range const & face_range) const
{
  unsigned int dof_index_pressure  = this->get_dof_index_pressure();
  unsigned int quad_index_pressure = this->get_quad_index_pressure();

  FaceIntegratorP integrator(matrix_free, true, dof_index_pressure, quad_index_pressure);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    integrator.reinit(face);

    BoundaryTypeU boundary_type =
      this->boundary_descriptor->velocity->get_boundary_type(matrix_free.get_boundary_id(face));

    for(unsigned int q = 0; q < integrator.n_q_points; ++q)
    {
      if(boundary_type == BoundaryTypeU::Dirichlet or
         boundary_type == BoundaryTypeU::DirichletCached)
      {
        dealii::Point<dim, scalar> q_points = integrator.quadrature_point(q);

        // evaluate right-hand side
        vector rhs =
          FunctionEvaluator<1, dim, Number>::value(*(this->field_functions->right_hand_side),
                                                   q_points,
                                                   this->evaluation_time);

        scalar flux_times_normal = rhs * integrator.get_normal_vector(q);
        // minus sign is introduced here which allows to call a function of type ...add()
        // and avoids a scaling of the resulting vector by the factor -1.0
        integrator.submit_value(-flux_times_normal, q);
      }
      else if(boundary_type == BoundaryTypeU::Neumann or boundary_type == BoundaryTypeU::Symmetry)
      {
        // Do nothing on Neumann and symmetry boundaries.
        // Remark: On symmetry boundaries it follows from g_u * n = 0 that also g_{u_hat} * n = 0.
        // Hence, a symmetry boundary for u is also a symmetry boundary for u_hat. Hence, there
        // are no inhomogeneous contributions on symmetry boundaries.
        scalar zero = dealii::make_vectorized_array<Number>(0.0);
        integrator.submit_value(zero, q);
      }
      else
      {
        AssertThrow(false,
                    dealii::ExcMessage("Boundary type of face is invalid or not implemented."));
      }
    }
    integrator.integrate(dealii::EvaluationFlags::values);
    integrator.distribute_local_to_global(dst);
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_velocity_divergence_term_dirichlet_bc_from_dof_vector(
  VectorType &       dst,
  VectorType const & velocity) const
{
  this->divergence_operator.rhs_bc_from_dof_vector(dst, velocity);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_div_term_convective_term_add(
  VectorType &       dst,
  VectorType const & src) const
{
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_rhs_ppe_div_term_convective_term_boundary_face,
                               this,
                               dst,
                               src);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_rhs_ppe_div_term_convective_term_boundary_face(
  dealii::MatrixFree<dim, Number> const & matrix_free,
  VectorType &                            dst,
  VectorType const &                      src,
  Range const &                           face_range) const
{
  unsigned int const dof_index_velocity = this->get_dof_index_velocity();
  unsigned int const dof_index_pressure = this->get_dof_index_pressure();
  unsigned int const quad_index         = this->get_quad_index_velocity_overintegration();

  FaceIntegratorU velocity(matrix_free, true, dof_index_velocity, quad_index);
  FaceIntegratorP pressure(matrix_free, true, dof_index_pressure, quad_index);

  FaceIntegratorU grid_velocity(matrix_free, true, dof_index_velocity, quad_index);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    velocity.reinit(face);
    velocity.gather_evaluate(src,
                             dealii::EvaluationFlags::values | dealii::EvaluationFlags::gradients);

    if(this->param.ale_formulation)
    {
      grid_velocity.reinit(face);
      grid_velocity.gather_evaluate(this->convective_kernel->get_grid_velocity(),
                                    dealii::EvaluationFlags::values);
    }

    pressure.reinit(face);

    BoundaryTypeU boundary_type =
      this->boundary_descriptor->velocity->get_boundary_type(matrix_free.get_boundary_id(face));

    for(unsigned int q = 0; q < pressure.n_q_points; ++q)
    {
      if(boundary_type == BoundaryTypeU::Dirichlet or
         boundary_type == BoundaryTypeU::DirichletCached)
      {
        vector normal = pressure.get_normal_vector(q);

        vector u      = velocity.get_value(q);
        tensor grad_u = velocity.get_gradient(q);

        vector flux;
        if(this->param.formulation_convective_term_bc ==
           FormulationConvectiveTerm::DivergenceFormulation)
        {
          scalar div_u = velocity.get_divergence(q);
          flux         = grad_u * u + div_u * u;
        }
        else if(this->param.formulation_convective_term_bc ==
                FormulationConvectiveTerm::ConvectiveFormulation)
        {
          flux = grad_u * u;
        }
        else
        {
          AssertThrow(false, dealii::ExcMessage("Not implemented."));
        }

        if(this->param.ale_formulation)
        {
          flux -= grad_u * grid_velocity.get_value(q);
        }

        scalar flux_times_normal = flux * normal;

        pressure.submit_value(flux_times_normal, q);
      }
      else if(boundary_type == BoundaryTypeU::Neumann or boundary_type == BoundaryTypeU::Symmetry)
      {
        // Do nothing on Neumann and symmetry boundaries.
        // Remark: On symmetry boundaries it follows from g_u * n = 0 that also g_{u_hat} * n = 0.
        // Hence, a symmetry boundary for u is also a symmetry boundary for u_hat. Hence, there
        // are no inhomogeneous contributions on symmetry boundaries.
        scalar zero = dealii::make_vectorized_array<Number>(0.0);
        pressure.submit_value(zero, q);
      }
      else
      {
        AssertThrow(false,
                    dealii::ExcMessage("Boundary type of face is invalid or not implemented."));
      }
    }
    pressure.integrate_scatter(dealii::EvaluationFlags::values, dst);
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_nbc_numerical_time_derivative_add(
  VectorType &       dst,
  VectorType const & acceleration) const
{
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_rhs_ppe_nbc_numerical_time_derivative_add_boundary_face,
                               this,
                               dst,
                               acceleration);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_rhs_ppe_nbc_numerical_time_derivative_add_boundary_face(
  dealii::MatrixFree<dim, Number> const & data,
  VectorType &                            dst,
  VectorType const &                      acceleration,
  Range const &                           face_range) const
{
  unsigned int dof_index_velocity  = this->get_dof_index_velocity();
  unsigned int dof_index_pressure  = this->get_dof_index_pressure();
  unsigned int quad_index_velocity = this->get_quad_index_velocity_standard();

  FaceIntegratorU integrator_velocity(data, true, dof_index_velocity, quad_index_velocity);
  FaceIntegratorP integrator_pressure(data, true, dof_index_pressure, quad_index_velocity);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    integrator_velocity.reinit(face);
    integrator_velocity.gather_evaluate(acceleration, dealii::EvaluationFlags::values);

    integrator_pressure.reinit(face);

    dealii::types::boundary_id boundary_id = data.get_boundary_id(face);
    BoundaryTypeP              boundary_type =
      this->boundary_descriptor->pressure->get_boundary_type(boundary_id);

    for(unsigned int q = 0; q < integrator_pressure.n_q_points; ++q)
    {
      if(boundary_type == BoundaryTypeP::Neumann)
      {
        vector normal = integrator_velocity.get_normal_vector(q);
        vector dudt   = integrator_velocity.get_value(q);
        scalar h      = -normal * dudt;

        integrator_pressure.submit_value(h, q);
      }
      else if(boundary_type == BoundaryTypeP::Dirichlet)
      {
        scalar zero = dealii::make_vectorized_array<Number>(0.0);
        integrator_pressure.submit_value(zero, q);
      }
      else
      {
        AssertThrow(false,
                    dealii::ExcMessage("Boundary type of face is invalid or not implemented."));
      }
    }

    integrator_pressure.integrate(dealii::EvaluationFlags::values);
    integrator_pressure.distribute_local_to_global(dst);
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_nbc_body_force_term_add(VectorType &   dst,
                                                                    double const & time) const
{
  this->evaluation_time = time;

  VectorType src_dummy;
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_rhs_ppe_nbc_body_force_term_add_boundary_face,
                               this,
                               dst,
                               src_dummy);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_rhs_ppe_nbc_body_force_term_add_boundary_face(
  dealii::MatrixFree<dim, Number> const & data,
  VectorType &                            dst,
  VectorType const &,
  Range const & face_range) const
{
  unsigned int dof_index_pressure  = this->get_dof_index_pressure();
  unsigned int quad_index_pressure = this->get_quad_index_pressure();

  FaceIntegratorP integrator(data, true, dof_index_pressure, quad_index_pressure);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    integrator.reinit(face);

    dealii::types::boundary_id boundary_id = data.get_boundary_id(face);
    BoundaryTypeP              boundary_type =
      this->boundary_descriptor->pressure->get_boundary_type(boundary_id);

    for(unsigned int q = 0; q < integrator.n_q_points; ++q)
    {
      if(boundary_type == BoundaryTypeP::Neumann)
      {
        dealii::Point<dim, scalar> q_points = integrator.quadrature_point(q);

        // evaluate right-hand side
        vector rhs =
          FunctionEvaluator<1, dim, Number>::value(*(this->field_functions->right_hand_side),
                                                   q_points,
                                                   this->evaluation_time);

        vector normal = integrator.get_normal_vector(q);

        scalar h = normal * rhs;

        integrator.submit_value(h, q);
      }
      else if(boundary_type == BoundaryTypeP::Dirichlet)
      {
        scalar zero = dealii::make_vectorized_array<Number>(0.0);
        integrator.submit_value(zero, q);
      }
      else
      {
        AssertThrow(false,
                    dealii::ExcMessage("Boundary type of face is invalid or not implemented."));
      }
    }
    integrator.integrate(dealii::EvaluationFlags::values);
    integrator.distribute_local_to_global(dst);
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_nbc_convective_add(VectorType &       dst,
                                                               VectorType const & src) const
{
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_rhs_ppe_nbc_convective_add_boundary_face,
                               this,
                               dst,
                               src);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_rhs_ppe_nbc_convective_add_boundary_face(
  dealii::MatrixFree<dim, Number> const & matrix_free,
  VectorType &                            dst,
  VectorType const &                      src,
  Range const &                           face_range) const
{
  unsigned int const dof_index_velocity = this->get_dof_index_velocity();
  unsigned int const dof_index_pressure = this->get_dof_index_pressure();
  unsigned int const quad_index         = this->get_quad_index_velocity_overintegration();

  FaceIntegratorU velocity(matrix_free, true, dof_index_velocity, quad_index);
  FaceIntegratorP pressure(matrix_free, true, dof_index_pressure, quad_index);
  FaceIntegratorU grid_velocity(matrix_free, true, dof_index_velocity, quad_index);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    velocity.reinit(face);
    velocity.gather_evaluate(src,
                             dealii::EvaluationFlags::values | dealii::EvaluationFlags::gradients);

    if(this->param.ale_formulation)
    {
      grid_velocity.reinit(face);
      grid_velocity.gather_evaluate(this->convective_kernel->get_grid_velocity(),
                                    dealii::EvaluationFlags::values);
    }

    pressure.reinit(face);

    BoundaryTypeP boundary_type =
      this->boundary_descriptor->pressure->get_boundary_type(matrix_free.get_boundary_id(face));

    for(unsigned int q = 0; q < pressure.n_q_points; ++q)
    {
      if(boundary_type == BoundaryTypeP::Neumann)
      {
        vector normal = pressure.get_normal_vector(q);

        vector u      = velocity.get_value(q);
        tensor grad_u = velocity.get_gradient(q);

        vector flux;
        if(this->param.formulation_convective_term_bc ==
           FormulationConvectiveTerm::DivergenceFormulation)
        {
          scalar div_u = velocity.get_divergence(q);
          flux         = grad_u * u + div_u * u;
        }
        else if(this->param.formulation_convective_term_bc ==
                FormulationConvectiveTerm::ConvectiveFormulation)
        {
          flux = grad_u * u;
        }
        else
        {
          AssertThrow(false, dealii::ExcMessage("Not implemented."));
        }

        if(this->param.ale_formulation)
        {
          flux -= grad_u * grid_velocity.get_value(q);
        }

        pressure.submit_value(-normal * flux, q);
      }
      else if(boundary_type == BoundaryTypeP::Dirichlet)
      {
        pressure.submit_value(dealii::make_vectorized_array<Number>(0.0), q);
      }
      else
      {
        AssertThrow(false,
                    dealii::ExcMessage("Boundary type of face is invalid or not implemented."));
      }
    }

    pressure.integrate_scatter(dealii::EvaluationFlags::values, dst);
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_nbc_viscous_add(VectorType &       dst,
                                                            VectorType const & src) const
{
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_rhs_ppe_nbc_viscous_add_boundary_face,
                               this,
                               dst,
                               src);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_rhs_ppe_nbc_viscous_add_boundary_face(
  dealii::MatrixFree<dim, Number> const & matrix_free,
  VectorType &                            dst,
  VectorType const &                      src,
  Range const &                           face_range) const
{
  unsigned int const dof_index_velocity = this->get_dof_index_velocity();
  unsigned int const dof_index_pressure = this->get_quad_index_pressure();
  unsigned int const quad_index         = this->get_quad_index_velocity_standard();

  FaceIntegratorU omega(matrix_free, true, dof_index_velocity, quad_index);

  FaceIntegratorP pressure(matrix_free, true, dof_index_pressure, quad_index);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    pressure.reinit(face);

    omega.reinit(face);
    omega.gather_evaluate(src, dealii::EvaluationFlags::gradients);

    BoundaryTypeP boundary_type =
      this->boundary_descriptor->pressure->get_boundary_type(matrix_free.get_boundary_id(face));

    for(unsigned int q = 0; q < pressure.n_q_points; ++q)
    {
      scalar viscosity = this->get_viscosity_boundary_face(face, q);

      if(boundary_type == BoundaryTypeP::Neumann)
      {
        scalar h = dealii::make_vectorized_array<Number>(0.0);

        vector normal = pressure.get_normal_vector(q);

        vector curl_omega = CurlCompute<dim, FaceIntegratorU>::compute(omega, q);

        h = -normal * (viscosity * curl_omega);

        pressure.submit_value(h, q);
      }
      else if(boundary_type == BoundaryTypeP::Dirichlet)
      {
        pressure.submit_value(dealii::make_vectorized_array<Number>(0.0), q);
      }
      else
      {
        AssertThrow(false,
                    dealii::ExcMessage("Boundary type of face is invalid or not implemented."));
      }
    }
    pressure.integrate_scatter(dealii::EvaluationFlags::values, dst);
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::rhs_ppe_laplace_add(VectorType &   dst,
                                                        double const & evaluation_time) const
{
  ProjectionBase::do_rhs_ppe_laplace_add(dst, evaluation_time);
}

template<int dim, typename Number>
unsigned int
OperatorDualSplitting<dim, Number>::solve_pressure(VectorType &       dst,
                                                   VectorType const & src,
                                                   bool const         update_preconditioner) const
{
  return ProjectionBase::do_solve_pressure(dst, src, update_preconditioner);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::interpolate_velocity_dirichlet_bc(VectorType &   dst,
                                                                      double const & time) const
{
  this->evaluation_time = time;

  dst = 0.0;

  VectorType src_dummy;
  this->get_matrix_free().loop(&This::cell_loop_empty,
                               &This::face_loop_empty,
                               &This::local_interpolate_velocity_dirichlet_bc_boundary_face,
                               this,
                               dst,
                               src_dummy);
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::local_interpolate_velocity_dirichlet_bc_boundary_face(
  dealii::MatrixFree<dim, Number> const & matrix_free,
  VectorType &                            dst,
  VectorType const &,
  Range const & face_range) const
{
  unsigned int const dof_index  = this->get_dof_index_velocity();
  unsigned int const quad_index = this->get_quad_index_velocity_nodal_points();

  FaceIntegratorU integrator(matrix_free, true, dof_index, quad_index);

  for(unsigned int face = face_range.first; face < face_range.second; face++)
  {
    dealii::types::boundary_id const boundary_id = matrix_free.get_boundary_id(face);

    BoundaryTypeU const boundary_type =
      this->boundary_descriptor->velocity->get_boundary_type(boundary_id);

    if(boundary_type == BoundaryTypeU::Dirichlet or boundary_type == BoundaryTypeU::DirichletCached)
    {
      integrator.reinit(face);
      integrator.read_dof_values(dst);

      for(unsigned int q = 0; q < integrator.n_q_points; ++q)
      {
        unsigned int const local_face_number = matrix_free.get_face_info(face).interior_face_no;

        unsigned int const index = matrix_free.get_shape_info(dof_index, quad_index)
                                     .face_to_cell_index_nodal[local_face_number][q];

        vector g = vector();

        if(boundary_type == BoundaryTypeU::Dirichlet)
        {
          auto bc = this->boundary_descriptor->velocity->dirichlet_bc.find(boundary_id)->second;
          auto q_points = integrator.quadrature_point(q);

          g = FunctionEvaluator<1, dim, Number>::value(*bc, q_points, this->evaluation_time);
        }
        else if(boundary_type == BoundaryTypeU::DirichletCached)
        {
          auto bc = this->boundary_descriptor->velocity->get_dirichlet_cached_data();

          g = FunctionEvaluator<1, dim, Number>::value(*bc, face, q, quad_index);
        }
        else
        {
          AssertThrow(false, dealii::ExcMessage("Not implemented."));
        }

        integrator.submit_dof_value(g, index);
      }

      integrator.set_dof_values(dst);
    }
    else
    {
      AssertThrow(boundary_type == BoundaryTypeU::Neumann or
                    boundary_type == BoundaryTypeU::Symmetry,
                  dealii::ExcMessage("BoundaryTypeU not implemented."));
    }
  }
}

template<int dim, typename Number>
void
OperatorDualSplitting<dim, Number>::apply_helmholtz_operator(VectorType &       dst,
                                                             VectorType const & src) const
{
  this->momentum_operator.vmult(dst, src);
}

template class OperatorDualSplitting<2, float>;
template class OperatorDualSplitting<2, double>;

template class OperatorDualSplitting<3, float>;
template class OperatorDualSplitting<3, double>;

} // namespace IncNS
} // namespace ExaDG
