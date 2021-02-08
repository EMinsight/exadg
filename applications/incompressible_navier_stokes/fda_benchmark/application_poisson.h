/*
 * nozzle.h
 *
 *  Created on: May, 2019
 *      Author: fehn
 */

#ifndef APPLICATIONS_POISSON_TEST_CASES_NOZZLE_H_
#define APPLICATIONS_POISSON_TEST_CASES_NOZZLE_H_

#include "include/grid.h"

namespace ExaDG
{
namespace Poisson
{
using namespace dealii;

template<int dim, typename Number>
class Application : public ApplicationBase<dim, Number>
{
public:
  typedef typename ApplicationBase<dim, Number>::PeriodicFaces PeriodicFaces;

  Application(std::string input_file) : ApplicationBase<dim, Number>(input_file)
  {
    // parse application-specific parameters
    ParameterHandler prm;
    this->add_parameters(prm);
    prm.parse_input(input_file, "", true, true);
  }

  void
  set_input_parameters(InputParameters & param)
  {
    // MATHEMATICAL MODEL
    param.right_hand_side = false;

    // SPATIAL DISCRETIZATION
    param.triangulation_type     = TriangulationType::Distributed;
    param.mapping                = MappingType::Cubic; // Isoparametric;
    param.spatial_discretization = SpatialDiscretization::DG;
    param.IP_factor              = 1.0e0;

    // SOLVER
    param.solver                      = Poisson::Solver::CG;
    param.solver_data.abs_tol         = 1.e-20;
    param.solver_data.rel_tol         = 1.e-10;
    param.solver_data.max_iter        = 1e4;
    param.compute_performance_metrics = true;
    param.preconditioner              = Preconditioner::Multigrid;
    param.multigrid_data.type         = MultigridType::cphMG;
    param.multigrid_data.p_sequence   = PSequenceType::Bisect;
    // MG smoother
    param.multigrid_data.smoother_data.smoother   = MultigridSmoother::Chebyshev;
    param.multigrid_data.smoother_data.iterations = 5;
    // MG coarse grid solver
    param.multigrid_data.coarse_problem.solver         = MultigridCoarseGridSolver::CG;
    param.multigrid_data.coarse_problem.preconditioner = MultigridCoarseGridPreconditioner::AMG;
    param.multigrid_data.coarse_problem.solver_data.rel_tol = 1.e-3;
  }

  void
  create_grid(std::shared_ptr<parallel::TriangulationBase<dim>> triangulation,
              PeriodicFaces &                                   periodic_faces,
              unsigned int const                                n_refine_space,
              std::shared_ptr<Mapping<dim>> &                   mapping,
              unsigned int const                                mapping_degree)
  {
    FDANozzle::create_grid_and_set_boundary_ids_nozzle(triangulation,
                                                       n_refine_space,
                                                       periodic_faces);

    mapping.reset(new MappingQGeneric<dim>(mapping_degree));
  }

  void set_boundary_conditions(std::shared_ptr<BoundaryDescriptor<0, dim>> boundary_descriptor)
  {
    typedef typename std::pair<types::boundary_id, std::shared_ptr<Function<dim>>> pair;

    // inflow
    boundary_descriptor->dirichlet_bc.insert(pair(1, new Functions::ConstantFunction<dim>(1.0, 1)));

    // outflow
    boundary_descriptor->dirichlet_bc.insert(pair(2, new Functions::ZeroFunction<dim>(1)));

    // walls
    boundary_descriptor->neumann_bc.insert(pair(0, new Functions::ZeroFunction<dim>(1)));
  }

  void
  set_field_functions(std::shared_ptr<FieldFunctions<dim>> field_functions)
  {
    field_functions->initial_solution.reset(new Functions::ZeroFunction<dim>(1));
    field_functions->right_hand_side.reset(new Functions::ZeroFunction<dim>(1));
  }

  std::shared_ptr<PostProcessorBase<dim, Number>>
  construct_postprocessor(unsigned int const degree, MPI_Comm const & mpi_comm)
  {
    PostProcessorData<dim> pp_data;
    pp_data.output_data.write_output       = this->write_output;
    pp_data.output_data.output_folder      = this->output_directory + "vtu/";
    pp_data.output_data.output_name        = this->output_name;
    pp_data.output_data.write_higher_order = true;
    pp_data.output_data.degree             = degree;

    std::shared_ptr<PostProcessorBase<dim, Number>> pp;
    pp.reset(new PostProcessor<dim, Number>(pp_data, mpi_comm));

    return pp;
  }
};

} // namespace Poisson

template<int dim, typename Number>
std::shared_ptr<Poisson::ApplicationBase<dim, Number>>
get_application(std::string input_file)
{
  return std::make_shared<Poisson::Application<dim, Number>>(input_file);
}

} // namespace ExaDG

#endif
