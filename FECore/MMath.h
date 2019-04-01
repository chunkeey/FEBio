/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2019 University of Utah, Columbia University, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/

#pragma once
#include "MItem.h"

// Take the derivative with respect of x
MITEM MDerive(const MITEM& e, const MVariable& x);

// Take the nth-derivative with respect of x
MITEM MDerive(const MITEM& e, const MVariable& x, int n);

// Take the partial derivative with respect to x, y, z, ...
MITEM MDerive(const MITEM& e, const MSequence& x);

// Replace an expression with an expression
MITEM MReplace(const MITEM& e, const MVariable& x, const MITEM& s);

// Replace an expression with an expression (assuming x is an equality)
MITEM MReplace(const MITEM& e, const MITEM& x);

// Replace an expression with an expression
MITEM MReplace(const MITEM& e, const MITEM& x, const MITEM& s);

// replace multiple expressions with other expressions
MITEM MReplace(const MITEM& e, const MSequence& x, const MSequence& s);

// Calculate the taylor expansion of an expression
MITEM MTaylor(const MITEM& e, const MVariable& x, double z, int n);

// Calculate the indefinite integral (without constant)
MITEM MIntegral(const MITEM& e, const MVariable& x);

// Calculate the definite integral
MITEM MIntegral(const MITEM& e, const MVariable& x, const MITEM& a, const MITEM& b);

// Expand an expression
MITEM MExpand(const MITEM& e);

// Expand an expression but don't expand the second argument
MITEM MExpand(const MITEM& e, const MITEM& s);

// solve an expression
MITEM MSolve(const MITEM& e, const MITEM& v);

// collect terms
MITEM MCollect(const MITEM& e, const MITEM& x);

// simplify an expression
MITEM MSimplify(const MITEM& e);
