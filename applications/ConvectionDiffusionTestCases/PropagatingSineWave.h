/*
 * PropagatingSineWave.h
 *
 *  Created on: Aug 18, 2016
 *      Author: fehn
 */

#ifndef APPLICATIONS_CONVECTIONDIFFUSIONTESTCASES_PROPAGATINGSINEWAVE_H_
#define APPLICATIONS_CONVECTIONDIFFUSIONTESTCASES_PROPAGATINGSINEWAVE_H_


/**************************************************************************************/
/*                                                                                    */
/*                                 INPUT PARAMETERS                                   */
/*                                                                                    */
/**************************************************************************************/

// test case for purely convective problem
// sine wave that is advected from left to right by a constant velocity field


// set the number of space dimensions: DIMENSION = 2, 3
const unsigned int DIMENSION = 2;

// set the polynomial degree of the shape functions
const unsigned int FE_DEGREE = 2;

// set the number of refine levels for spatial convergence tests
const unsigned int REFINE_STEPS_SPACE_MIN = 3;
const unsigned int REFINE_STEPS_SPACE_MAX = 3;

// set the number of refine levels for temporal convergence tests
const unsigned int REFINE_STEPS_TIME_MIN = 0;
const unsigned int REFINE_STEPS_TIME_MAX = 0;

void InputParametersConvDiff::set_input_parameters()
{
  // MATHEMATICAL MODEL
  equation_type = EquationTypeConvDiff::Convection;
  right_hand_side = false;

  // PHYSICAL QUANTITIES
  start_time = 0.0;
  end_time = 8.0;
  diffusivity = 0.0;

  // TEMPORAL DISCRETIZATION
  order_time_integrator = 4;
  cfl_number = 0.2;
  diffusion_number = 0.01;

  // SPATIAL DISCRETIZATION
  // convective term
  numerical_flux_convective_operator = NumericalFluxConvectiveOperator::LaxFriedrichsFlux;

  // viscous term
  IP_factor = 1.0;

  // NUMERICAL PARAMETERS
  runtime_optimization = false;

  // OUTPUT AND POSTPROCESSING
  print_input_parameters = true;
  write_output = "true";
  output_prefix = "propagating_sine_wave";
  output_start_time = start_time;
  output_interval_time = (end_time-start_time)/20;

  analytical_solution_available = true;
  error_calc_start_time = start_time;
  error_calc_interval_time = output_interval_time;
}


/**************************************************************************************/
/*                                                                                    */
/*    FUNCTIONS (ANALYTICAL SOLUTION, BOUNDARY CONDITIONS, VELOCITY FIELD, etc.)      */
/*                                                                                    */
/**************************************************************************************/

/*
 *  Analytical solution
 */

template<int dim>
class AnalyticalSolution : public Function<dim>
{
public:
  AnalyticalSolution (const unsigned int  n_components = 1,
                      const double        time = 0.)
    :
    Function<dim>(n_components, time)
  {}

  virtual ~AnalyticalSolution(){};

  virtual double value (const Point<dim>   &p,
                        const unsigned int component = 0) const;
};

template<int dim>
double AnalyticalSolution<dim>::value(const Point<dim>    &p,
                                      const unsigned int  /* component */) const
{
  double t = this->get_time();

  double result = std::sin(numbers::PI*(p[0]-t));

  return result;
}

/*
 *  Right-hand side
 */

template<int dim>
class RightHandSide : public Function<dim>
{
public:
  RightHandSide (const unsigned int   n_components = 1,
                 const double         time = 0.)
    :
    Function<dim>(n_components, time)
  {}

  virtual ~RightHandSide(){};

  virtual double value (const Point<dim>    &p,
                       const unsigned int  component = 0) const;
};

template<int dim>
double RightHandSide<dim>::value(const Point<dim>     &p,
                                const unsigned int   /* component */) const
{
  double result = 0.0;
  return result;
}

/*
 *  Neumann boundary condition
 */

template<int dim>
class NeumannBoundary : public Function<dim>
{
public:
  NeumannBoundary (const unsigned int n_components = 1,
                   const double       time = 0.)
    :
    Function<dim>(n_components, time)
  {}

  virtual ~NeumannBoundary(){};

  virtual double value (const Point<dim>    &p,
                        const unsigned int  component = 0) const;
};

template<int dim>
double NeumannBoundary<dim>::value(const Point<dim>   &p,
                                   const unsigned int /* component */) const
{
  double result = 0.0;
  return result;
}

/*
 *  Velocity field
 */

template<int dim>
class VelocityField : public Function<dim>
{
public:
  VelocityField (const unsigned int n_components = dim,
                 const double       time = 0.)
    :
    Function<dim>(n_components, time)
  {}

  virtual ~VelocityField(){};

  virtual double value (const Point<dim>    &p,
                        const unsigned int  component = 0) const;
};

template<int dim>
double VelocityField<dim>::value(const Point<dim>   &point,
                                 const unsigned int component) const
{
  double value = 0.0;

  if(component == 0)
    value = 1.0;

  return value;
}

/**************************************************************************************/
/*                                                                                    */
/*         GENERATE GRID, SET BOUNDARY INDICATORS AND FILL BOUNDARY DESCRIPTOR        */
/*                                                                                    */
/**************************************************************************************/

template<int dim>
void create_grid_and_set_boundary_conditions(parallel::distributed::Triangulation<dim>               &triangulation,
                                             unsigned int const                                      n_refine_space,
                                             std_cxx11::shared_ptr<BoundaryDescriptorConvDiff<dim> > boundary_descriptor)
{
  // hypercube: line in 1D, square in 2D, etc., hypercube volume is [left,right]^dim
  const double left = -1.0, right = 1.0;
  GridGenerator::hyper_cube(triangulation,left,right);

  // set boundary indicator
  typename Triangulation<dim>::cell_iterator cell = triangulation.begin(), endc = triangulation.end();
  for(;cell!=endc;++cell)
  {
    for(unsigned int face_number=0;face_number < GeometryInfo<dim>::faces_per_cell;++face_number)
    {
      //use outflow boundary at right boundary
      if ((std::fabs(cell->face(face_number)->center()(0) - right) < 1e-12))
       cell->face(face_number)->set_boundary_id(1);
    }
  }
  triangulation.refine_global(n_refine_space);

  std_cxx11::shared_ptr<Function<dim> > analytical_solution;
  analytical_solution.reset(new AnalyticalSolution<dim>());
  boundary_descriptor->dirichlet_bc.insert(std::pair<types::boundary_id,std_cxx11::shared_ptr<Function<dim> > >(0,analytical_solution));
  std_cxx11::shared_ptr<Function<dim> > neumann_bc;
  neumann_bc.reset(new NeumannBoundary<dim>());
  boundary_descriptor->neumann_bc.insert(std::pair<types::boundary_id,std_cxx11::shared_ptr<Function<dim> > >(1,neumann_bc));
}

template<int dim>
void set_field_functions(std_cxx11::shared_ptr<FieldFunctionsConvDiff<dim> > field_functions)
{
  // initialize functions (analytical solution, rhs, boundary conditions)
  std_cxx11::shared_ptr<Function<dim> > analytical_solution;
  analytical_solution.reset(new AnalyticalSolution<dim>());

  std_cxx11::shared_ptr<Function<dim> > right_hand_side;
  right_hand_side.reset(new RightHandSide<dim>());

  std_cxx11::shared_ptr<Function<dim> > velocity;
  velocity.reset(new VelocityField<dim>());

  field_functions->analytical_solution = analytical_solution;
  field_functions->right_hand_side = right_hand_side;
  field_functions->velocity = velocity;
}



#endif /* APPLICATIONS_CONVECTIONDIFFUSIONTESTCASES_PROPAGATINGSINEWAVE_H_ */
