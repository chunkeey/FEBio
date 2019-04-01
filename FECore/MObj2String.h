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
#include "MathObject.h"
#include <string>
using namespace std;

// this class converts a MathObject to a string
class MObj2String
{
public:
	string Convert(const MathObject& o);

protected:
	string Convert(const MItem* pi);

	string Constant (const MConstant* pc);
	string Fraction (const MFraction* pc);
	string NamedCt  (const MNamedCt*  pc);
	string Variable (const MVarRef*   pv);
	string OpNeg    (const MNeg*      po);
	string OpAdd    (const MAdd*      po);
	string OpSub    (const MSub*      po);
	string OpMul    (const MMul*      po);
	string OpDiv    (const MDiv*      po);
	string OpPow    (const MPow*      po);
	string OpEqual  (const MEquation* po);
	string OpFnc1D  (const MFunc1D*   po);
	string OpFnc2D  (const MFunc2D*   po);
	string OpFncND  (const MFuncND*   po);
	string OpSFnc   (const MSFuncND*  po);
};
