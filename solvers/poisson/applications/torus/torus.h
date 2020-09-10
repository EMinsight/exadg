/*
 * torus.h
 *
 *  Created on: May, 2019
 *      Author: fehn
 */

#ifndef APPLICATIONS_POISSON_TEST_CASES_TORUS_H_
#define APPLICATIONS_POISSON_TEST_CASES_TORUS_H_

namespace ExaDG
{
namespace Poisson
{
namespace Torus
{
void do_create_grid(
  std::shared_ptr<parallel::TriangulationBase<2>> /*triangulation*/,
  unsigned int const /*n_refine_space*/,
  std::vector<
    GridTools::PeriodicFacePair<typename Triangulation<2>::cell_iterator>> & /*periodic_faces*/)
{
  AssertThrow(false, ExcMessage("This test case is only implemented for dim=3."));
}

template<int dim>
void
do_create_grid(
  std::shared_ptr<parallel::TriangulationBase<dim>> triangulation,
  unsigned int const                                n_refine_space,
  std::vector<
    GridTools::PeriodicFacePair<typename Triangulation<dim>::cell_iterator>> & /*periodic_faces*/)
{
  double const r = 0.5, R = 1.5;
  GridGenerator::torus(*triangulation, R, r);
  //  GridGenerator::torus(*triangulation, R, r, 4, 1.5*numbers::PI); // open torus

  triangulation->refine_global(n_refine_space);
}

template<int dim, typename Number>
class Application : public ApplicationBase<dim, Number>
{
public:
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
    param.right_hand_side = true;

    // SPATIAL DISCRETIZATION
    param.triangulation_type     = TriangulationType::Distributed;
    param.mapping                = MappingType::Isoparametric;
    param.spatial_discretization = SpatialDiscretization::DG;
    param.IP_factor              = 1.0;

    // SOLVER
    param.solver                                       = Solver::CG;
    param.solver_data                                  = SolverData(1e4, 1.e-20, 1.e-8);
    param.compute_performance_metrics                  = true;
    param.preconditioner                               = Preconditioner::Multigrid;
    param.multigrid_data.type                          = MultigridType::cphMG;
    param.multigrid_data.smoother_data.smoother        = MultigridSmoother::Chebyshev;
    param.multigrid_data.coarse_problem.solver         = MultigridCoarseGridSolver::CG;
    param.multigrid_data.coarse_problem.preconditioner = MultigridCoarseGridPreconditioner::AMG;
  }

  void
  create_grid(std::shared_ptr<parallel::TriangulationBase<dim>> triangulation,
              unsigned int const                                n_refine_space,
              std::vector<GridTools::PeriodicFacePair<typename Triangulation<dim>::cell_iterator>> &
                periodic_faces)
  {
    do_create_grid(triangulation, n_refine_space, periodic_faces);
  }

  void set_boundary_conditions(std::shared_ptr<BoundaryDescriptor<0, dim>> boundary_descriptor)
  {
    typedef typename std::pair<types::boundary_id, std::shared_ptr<Function<dim>>> pair;

    boundary_descriptor->dirichlet_bc.insert(pair(0, new Functions::ZeroFunction<dim>(1)));
  }

  void
  set_field_functions(std::shared_ptr<FieldFunctions<dim>> field_functions)
  {
    field_functions->initial_solution.reset(new Functions::ZeroFunction<dim>(1));
    field_functions->right_hand_side.reset(new Functions::ConstantFunction<dim>(1.0, 1));
  }

  std::shared_ptr<ConvDiff::PostProcessorBase<dim, Number>>
  construct_postprocessor(unsigned int const degree, MPI_Comm const & mpi_comm)
  {
    ConvDiff::PostProcessorData<dim> pp_data;
    pp_data.output_data.write_output       = this->write_output;
    pp_data.output_data.output_folder      = this->output_directory + "vtu/";
    pp_data.output_data.output_name        = this->output_name;
    pp_data.output_data.write_higher_order = true;
    pp_data.output_data.degree             = degree;

    std::shared_ptr<ConvDiff::PostProcessorBase<dim, Number>> pp;
    pp.reset(new ConvDiff::PostProcessor<dim, Number>(pp_data, mpi_comm));

    return pp;
  }
};

} // namespace Torus
} // namespace Poisson
} // namespace ExaDG

#endif
