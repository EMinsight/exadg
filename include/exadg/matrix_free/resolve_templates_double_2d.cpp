/*
 * resolve_templates_double_2d.cpp
 *
 *  Created on: May 2, 2019
 *      Author: fehn
 */

#include <exadg/matrix_free/evaluation_template_factory.templates.h>

template struct dealii::internal::FEEvaluationFactory<2, double>;

template struct dealii::internal::FEFaceEvaluationFactory<2, double>;

template struct dealii::internal::CellwiseInverseMassFactory<2, double>;
