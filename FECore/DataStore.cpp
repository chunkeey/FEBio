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

#include "stdafx.h"
#include "DataStore.h"
#include "log.h"
#include "FEModel.h"
#include "FEAnalysis.h"

//-----------------------------------------------------------------------------
DataStore::DataStore()
{
}

//-----------------------------------------------------------------------------
DataStore::~DataStore()
{
}

//-----------------------------------------------------------------------------
void DataStore::Clear()
{
	for (size_t i=0; i<m_data.size(); ++i) delete m_data[i];
	m_data.clear();
}

//-----------------------------------------------------------------------------

void DataStore::Write()
{
	for (size_t i=0; i<m_data.size(); ++i)
	{
		DataRecord& DR = *m_data[i];
		DR.Write();
	}
}

//-----------------------------------------------------------------------------

void DataStore::AddRecord(DataRecord* prec)
{
	prec->m_nid = (int) m_data.size() + 1;
	m_data.push_back(prec);
}
