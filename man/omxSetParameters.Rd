%
%   Copyright 2007-2018 by the individuals mentioned in the source code history
%
%   Licensed under the Apache License, Version 2.0 (the "License");
%   you may not use this file except in compliance with the License.
%   You may obtain a copy of the License at
% 
%        http://www.apache.org/licenses/LICENSE-2.0
% 
%   Unless required by applicable law or agreed to in writing, software
%   distributed under the License is distributed on an "AS IS" BASIS,
%   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%   See the License for the specific language governing permissions and
%   limitations under the License.

\name{omxSetParameters}
\alias{omxSetParameters}

\title{Assign Model Parameters}

\description{
    Modify the attributes of parameters in a model.  This function cannot modify parameters that have NA labels.
	Often you will want to call \code{\link{omxAssignFirstParameters}} after using this, to force the starting
	values of equated parameters to the same value (otherwise the model cannot begin to be evaluated)
}

\usage{
omxSetParameters(model, labels, free = NULL, values = NULL,
    newlabels = NULL, lbound = NULL, ubound = NULL, indep = FALSE,
    strict = TRUE, name = NULL)
}

\arguments{
   \item{model}{an MxModel object.}
   \item{labels}{a character vector of target parameter names.}
   \item{free}{a boolean vector of parameter free/fixed designations.} 
   \item{values}{a numeric vector of parameter values.}
   \item{newlabels}{a character vector of new parameter names.} 
   \item{lbound}{a numeric vector of lower bound values.} 
   \item{ubound}{a numeric vector of upper bound values.} 
   \item{indep}{boolean. set parameters in independent submodels.}
   \item{strict}{boolean. If TRUE then throw an error when a label does not appear in the model.}
   \item{name}{character string. (optional) a new name for the model.}
}

\seealso{
\code{\link{omxGetParameters}}, \code{\link{omxAssignFirstParameters}} 
}

\examples{

A <- mxMatrix('Full', 3, 3, labels = c('a','b', NA), free = TRUE, name = 'A')
model <- mxModel(model="testModel7", A, name = 'model')

# set value of cells labelled "a" and "b" to 1 and 2 respectively
model <- omxSetParameters(model, c('a', 'b'), values = c(1, 2))

# set label of cell labelled "a" to "b" and vice versa
model <- omxSetParameters(model, c('a', 'b'), newlabels = c('b', 'a'))

# set label of cells labelled "a" to "b"
model <- omxSetParameters(model, c('a'), newlabels = 'b')

# ensure initial values are the same for each instance of a labeled parameter
model <- omxAssignFirstParameters(model)
}
