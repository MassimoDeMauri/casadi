/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


      #include "newton_implicit_internal.hpp"
      #include <string>

      const std::string casadi::NewtonImplicitInternal::meta_doc=
      "\n"
"Implements simple newton iterations to solve an implicit function.\n"
"\n"
"\n"
">List of available options\n"
"+-----------------+-----------------+-----------------+-----------------+\n"
"|       Id        |      Type       |     Default     |   Description   |\n"
"+=================+=================+=================+=================+\n"
"| abstol          | OT_REAL         | 0.000           | Stopping        |\n"
"|                 |                 |                 | criterion       |\n"
"|                 |                 |                 | tolerance on    |\n"
"|                 |                 |                 | max(|F|)        |\n"
"+-----------------+-----------------+-----------------+-----------------+\n"
"| abstolStep      | OT_REAL         | 0.000           | Stopping        |\n"
"|                 |                 |                 | criterion       |\n"
"|                 |                 |                 | tolerance on    |\n"
"|                 |                 |                 | step size       |\n"
"+-----------------+-----------------+-----------------+-----------------+\n"
"| max_iter        | OT_INTEGER      | 1000            | Maximum number  |\n"
"|                 |                 |                 | of Newton       |\n"
"|                 |                 |                 | iterations to   |\n"
"|                 |                 |                 | perform before  |\n"
"|                 |                 |                 | returning.      |\n"
"+-----------------+-----------------+-----------------+-----------------+\n"
"\n"
"\n"
">List of available monitors\n"
"+----------+\n"
"|    Id    |\n"
"+==========+\n"
"| F        |\n"
"+----------+\n"
"| J        |\n"
"+----------+\n"
"| normF    |\n"
"+----------+\n"
"| step     |\n"
"+----------+\n"
"| stepsize |\n"
"+----------+\n"
"\n"
"\n"
">List of available stats\n"
"+---------------+\n"
"|      Id       |\n"
"+===============+\n"
"| iter          |\n"
"+---------------+\n"
"| return_status |\n"
"+---------------+\n"
"\n"
"\n"
"\n"
"\n"
;
