/*  ______________________________________________________________________
 *
 *  ExaDG - High-Order Discontinuous Galerkin for the Exa-Scale
 *
 *  Copyright (C) 2022 by the ExaDG authors
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

#pragma once

#include <deal.II/matrix_free/fe_evaluation.h>

#include <exadg/fluid_structure_interaction/precice/coupling_surface.h>
#include <exadg/fluid_structure_interaction/precice/interface_coupling.h>

namespace ExaDG
{
namespace preCICE
{
/**
 * Derived class of the CouplingSurface: shallow wrapper,
 * where the participant defines a vector of points and
 * the interface handles only the dealii::Exchange with preCICE.
 */
template<int dim, int data_dim, typename VectorizedArrayType>
class ExaDGSurface : public CouplingSurface<dim, data_dim, VectorizedArrayType>
{
public:
  ExaDGSurface(std::shared_ptr<const dealii::MatrixFree<dim, double, VectorizedArrayType>> data,
               std::shared_ptr<precice::SolverInterface>                                   precice,
               const std::string                mesh_name,
               const dealii::types::boundary_id surface_id = dealii::numbers::invalid_unsigned_int)
    : CouplingSurface<dim, data_dim, VectorizedArrayType>(data, precice, mesh_name, surface_id)
  {
  }

  /**
   * @brief define_mesh_vertices Define a vertex coupling mesh for preCICE
   *        coupling the classical preCICE way
   */
  virtual void
  define_coupling_mesh(const std::vector<dealii::Point<dim>> & vec) override;

  /**
   * @brief write_data
   *
   * @param[in] data_vector The data to be passed to preCICE (absolute
   *            displacement for FSI). Note that the data_vector needs to
   *            contain valid ghost values for parallel runs, i.e.
   *            update_ghost_values must be calles before
   */
  virtual void
  write_data(const dealii::LinearAlgebra::distributed::Vector<double> & data_vector,
             const std::string &                                        data_name) override;

  virtual void
  read_block_data(const std::string & data_name) const override;

  void
  set_data_pointer(
    std::shared_ptr<ExaDG::preCICE::InterfaceCoupling<dim, dim, double>> exadg_terminal_);

private:
  /// Accessor for ExaDG data structures
  std::shared_ptr<ExaDG::preCICE::InterfaceCoupling<dim, dim, double>> exadg_terminal;
  /// The preCICE IDs
  std::vector<int> coupling_nodes_ids;

  virtual std::string
  get_surface_type() const override;
};



template<int dim, int data_dim, typename VectorizedArrayType>
void
ExaDGSurface<dim, data_dim, VectorizedArrayType>::define_coupling_mesh(
  const std::vector<dealii::Point<dim>> & vec)
{
  Assert(this->mesh_id != -1, dealii::ExcNotInitialized());

  // In order to avoid that we define the surface multiple times when reader
  // and writer refer to the same object
  if(coupling_nodes_ids.size() > 0)
    return;

  // Initial guess: half of the boundary is part of the coupling surface
  coupling_nodes_ids.resize(vec.size());

  this->precice->setMeshVertices(this->mesh_id, vec.size(), &vec[0][0], coupling_nodes_ids.data());

  if(this->read_data_map.size() > 0)
    this->print_info(true, this->precice->getMeshVertexSize(this->mesh_id));
  if(this->write_data_map.size() > 0)
    this->print_info(false, this->precice->getMeshVertexSize(this->mesh_id));
}


template<int dim, int data_dim, typename VectorizedArrayType>
void
ExaDGSurface<dim, data_dim, VectorizedArrayType>::read_block_data(
  const std::string & data_name) const
{
  const int read_data_id = this->read_data_map.at(data_name);

  std::vector<dealii::Tensor<1, dim>> values(coupling_nodes_ids.size());
  if constexpr(data_dim > 1)
  {
    this->precice->readBlockVectorData(read_data_id,
                                       coupling_nodes_ids.size(),
                                       coupling_nodes_ids.data(),
                                       &values[0][0]);
  }
  else
  {
    AssertThrow(false, dealii::ExcNotImplemented());
  }
  Assert(exadg_terminal.get() != nullptr, dealii::ExcNotInitialized());
  exadg_terminal->update_data(values);
}



template<int dim, int data_dim, typename VectorizedArrayType>
void
ExaDGSurface<dim, data_dim, VectorizedArrayType>::set_data_pointer(
  std::shared_ptr<ExaDG::preCICE::InterfaceCoupling<dim, dim, double>> exadg_terminal_)
{
  exadg_terminal = exadg_terminal_;
}



template<int dim, int data_dim, typename VectorizedArrayType>
void
ExaDGSurface<dim, data_dim, VectorizedArrayType>::write_data(
  const dealii::LinearAlgebra::distributed::Vector<double> &,
  const std::string &)
{
  AssertThrow(false, dealii::ExcNotImplemented());
}


template<int dim, int data_dim, typename VectorizedArrayType>
std::string
ExaDGSurface<dim, data_dim, VectorizedArrayType>::get_surface_type() const
{
  return "exadg shallow wrapper";
}

} // namespace preCICE
} // namespace ExaDG