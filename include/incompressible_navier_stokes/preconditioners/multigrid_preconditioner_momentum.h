/*
 * multigrid_preconditioner.h
 *
 *  Created on: Nov 23, 2016
 *      Author: fehn
 */

#ifndef INCLUDE_INCOMPRESSIBLE_NAVIER_STOKES_PRECONDITIONERS_MULTIGRID_PRECONDITIONER_MOMENTUM_H_
#define INCLUDE_INCOMPRESSIBLE_NAVIER_STOKES_PRECONDITIONERS_MULTIGRID_PRECONDITIONER_MOMENTUM_H_


#include "../../operators/multigrid_operator.h"
#include "../../solvers_and_preconditioners/multigrid/multigrid_preconditioner_base.h"
#include "../spatial_discretization/operators/momentum_operator.h"

namespace IncNS
{
/*
 * Multigrid preconditioner for momentum operator of the incompressible Navier-Stokes equations.
 */
template<int dim, typename Number, typename MultigridNumber>
class MultigridPreconditioner : public MultigridPreconditionerBase<dim, Number, MultigridNumber>
{
private:
  typedef MomentumOperator<dim, Number>                          PDEOperator;
  typedef MomentumOperator<dim, MultigridNumber>                 PDEOperatorMG;
  typedef MultigridOperatorBase<dim, MultigridNumber>            MGOperatorBase;
  typedef MultigridOperator<dim, MultigridNumber, PDEOperatorMG> MGOperator;

  typedef MultigridPreconditionerBase<dim, Number, MultigridNumber> Base;

  typedef typename Base::Map               Map;
  typedef typename Base::PeriodicFacePairs PeriodicFacePairs;
  typedef typename Base::VectorType        VectorType;
  typedef typename Base::VectorTypeMG      VectorTypeMG;

public:
  MultigridPreconditioner(MPI_Comm const & comm)
    : Base(comm),
      pde_operator(nullptr),
      mg_operator_type(MultigridOperatorType::ReactionDiffusion),
      mesh_is_moving(false)
  {
  }

  void
  initialize(MultigridData const &                    mg_data,
             parallel::TriangulationBase<dim> const * tria,
             FiniteElement<dim> const &               fe,
             Mapping<dim> const &                     mapping,
             PDEOperator const &                      pde_operator,
             MultigridOperatorType const &            mg_operator_type,
             bool const                               mesh_is_moving,
             Map const *                              dirichlet_bc        = nullptr,
             PeriodicFacePairs *                      periodic_face_pairs = nullptr)
  {
    this->pde_operator = &pde_operator;

    this->mg_operator_type = mg_operator_type;

    this->mesh_is_moving = mesh_is_moving;

    data = this->pde_operator->get_data();

    // When solving the reaction-convection-diffusion problem, it might be possible
    // that one wants to apply the multigrid preconditioner only to the reaction-diffusion
    // operator (which is symmetric, Chebyshev smoother, etc.) instead of the non-symmetric
    // reaction-convection-diffusion operator. Accordingly, we have to reset which
    // operators should be "active" for the multigrid preconditioner, independently of
    // the actual equation type that is solved.
    AssertThrow(this->mg_operator_type != MultigridOperatorType::Undefined,
                ExcMessage("Invalid parameter mg_operator_type."));

    if(this->mg_operator_type == MultigridOperatorType::ReactionDiffusion)
    {
      // deactivate convective term for multigrid preconditioner
      data.convective_problem = false;
    }
    else if(this->mg_operator_type == MultigridOperatorType::ReactionConvectionDiffusion)
    {
      AssertThrow(data.convective_problem == true, ExcMessage("Invalid parameter."));
    }
    else
    {
      AssertThrow(false, ExcMessage("Not implemented."));
    }

    Base::initialize(mg_data,
                     tria,
                     fe,
                     mapping,
                     false /*operator_is_singular*/,
                     dirichlet_bc,
                     periodic_face_pairs);
  }

  /*
   * This function updates the multigrid preconditioner.
   */
  void
  update() override
  {
    if(mesh_is_moving)
    {
      this->update_matrix_free();
    }

    update_operators();

    this->update_smoothers();

    // singular operators do not occur for this operator
    this->update_coarse_solver(false /* operator_is_singular */);
  }

private:
  void
  fill_matrix_free_data(MatrixFreeData<dim, MultigridNumber> & matrix_free_data,
                        unsigned int const                     level)
  {
    matrix_free_data.data.mg_level = this->level_info[level].h_level();
    matrix_free_data.data.tasks_parallel_scheme =
      MatrixFree<dim, MultigridNumber>::AdditionalData::none;

    if(data.unsteady_problem)
      matrix_free_data.append_mapping_flags(MassMatrixKernel<dim, Number>::get_mapping_flags());
    if(data.convective_problem)
      matrix_free_data.append_mapping_flags(
        Operators::ConvectiveKernel<dim, Number>::get_mapping_flags());
    if(data.viscous_problem)
      matrix_free_data.append_mapping_flags(
        Operators::ViscousKernel<dim, Number>::get_mapping_flags(this->level_info[level].is_dg(),
                                                                 this->level_info[level].is_dg()));

    if(data.use_cell_based_loops && this->level_info[level].is_dg())
    {
      auto tria = dynamic_cast<parallel::distributed::Triangulation<dim> const *>(
        &this->dof_handlers[level]->get_triangulation());
      Categorization::do_cell_based_loops(*tria,
                                          matrix_free_data.data,
                                          this->level_info[level].h_level());
    }

    matrix_free_data.insert_dof_handler(&(*this->dof_handlers[level]), "std_dof_handler");
    matrix_free_data.insert_constraint(&(*this->constraints[level]), "std_dof_handler");
    matrix_free_data.insert_quadrature(QGauss<1>(this->level_info[level].degree() + 1),
                                       "std_quadrature");
  }

  std::shared_ptr<MGOperatorBase>
  initialize_operator(unsigned int const level)
  {
    // initialize pde_operator in a first step
    std::shared_ptr<PDEOperatorMG> pde_operator_level(new PDEOperatorMG());

    data.dof_index  = this->matrix_free_data_objects[level]->get_dof_index("std_dof_handler");
    data.quad_index = this->matrix_free_data_objects[level]->get_quad_index("std_quadrature");

    pde_operator_level->initialize(*this->matrix_free_objects[level],
                                   *this->constraints[level],
                                   data);

    // make sure that scaling factor of time derivative term has been set before the smoothers are
    // initialized
    pde_operator_level->set_scaling_factor_mass_matrix(
      pde_operator->get_scaling_factor_mass_matrix());

    // initialize MGOperator which is a wrapper around the PDEOperatorMG
    std::shared_ptr<MGOperator> mg_operator(new MGOperator(pde_operator_level));

    return mg_operator;
  }

  /*
   * This function updates the multigrid operators for all levels
   */
  void
  update_operators()
  {
    if(mesh_is_moving)
    {
      update_operators_after_mesh_movement();
    }

    if(data.unsteady_problem)
    {
      set_time(pde_operator->get_time());
      set_scaling_factor_time_derivative_term(pde_operator->get_scaling_factor_mass_matrix());
    }

    if(mg_operator_type == MultigridOperatorType::ReactionConvectionDiffusion)
    {
      VectorType const & vector_linearization = pde_operator->get_velocity();

      // convert Number --> MultigridNumber, e.g., double --> float, but only if necessary
      VectorTypeMG         vector_multigrid_type_copy;
      VectorTypeMG const * vector_multigrid_type_ptr;
      if(std::is_same<MultigridNumber, Number>::value)
      {
        vector_multigrid_type_ptr = reinterpret_cast<VectorTypeMG const *>(&vector_linearization);
      }
      else
      {
        vector_multigrid_type_copy = vector_linearization;
        vector_multigrid_type_ptr  = &vector_multigrid_type_copy;
      }

      set_vector_linearization(*vector_multigrid_type_ptr);
    }
  }

  /*
   * This function updates vector_linearization.
   * In order to update operators[level] this function has to be called.
   */
  void
  set_vector_linearization(VectorTypeMG const & vector_linearization)
  {
    // copy velocity to finest level
    this->get_operator(this->fine_level)->set_velocity_copy(vector_linearization);

    // interpolate velocity from fine to coarse level
    for(unsigned int level = this->fine_level; level > this->coarse_level; --level)
    {
      auto & vector_fine_level   = this->get_operator(level - 0)->get_velocity();
      auto   vector_coarse_level = this->get_operator(level - 1)->get_velocity();
      this->transfers.interpolate(level, vector_coarse_level, vector_fine_level);
      this->get_operator(level - 1)->set_velocity_copy(vector_coarse_level);
    }
  }

  /*
   * This function updates the evaluation time. In order to update the operators this function
   * has to be called. (This is due to the fact that the linearized convective term does not only
   * depend on the linearized velocity field but also on Dirichlet boundary data which itself
   * depends on the current time.)
   */
  void
  set_time(double const & time)
  {
    for(unsigned int level = this->coarse_level; level <= this->fine_level; ++level)
    {
      get_operator(level)->set_time(time);
    }
  }

  /*
   * This function performs the updates that are necessary after the mesh has been moved
   * and after matrix_free has been updated.
   */
  void
  update_operators_after_mesh_movement()
  {
    for(unsigned int level = this->coarse_level; level <= this->fine_level; ++level)
    {
      get_operator(level)->update_after_mesh_movement();
    }
  }

  /*
   * This function updates scaling_factor_time_derivative_term. In order to update the
   * operators this function has to be called. This is necessary if adaptive time stepping
   * is used where the scaling factor of the derivative term is variable.
   */
  void
  set_scaling_factor_time_derivative_term(double const & scaling_factor_time_derivative_term)
  {
    for(unsigned int level = this->coarse_level; level <= this->fine_level; ++level)
    {
      get_operator(level)->set_scaling_factor_mass_matrix(scaling_factor_time_derivative_term);
    }
  }

  std::shared_ptr<PDEOperatorMG>
  get_operator(unsigned int level)
  {
    std::shared_ptr<MGOperator> mg_operator =
      std::dynamic_pointer_cast<MGOperator>(this->operators[level]);

    return mg_operator->get_pde_operator();
  }

  MomentumOperatorData<dim> data;

  PDEOperator const * pde_operator;

  MultigridOperatorType mg_operator_type;

  bool mesh_is_moving;
};

} // namespace IncNS

#endif /* INCLUDE_INCOMPRESSIBLE_NAVIER_STOKES_PRECONDITIONERS_MULTIGRID_PRECONDITIONER_MOMENTUM_H_ \
        */
