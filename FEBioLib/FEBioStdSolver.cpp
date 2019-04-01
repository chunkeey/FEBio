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
#include "FEBioStdSolver.h"
#include <FEBioLib/FEBioModel.h>
#include <FECore/log.h>
#include <FEBioXML/FERestartImport.h>
#include <FECore/DumpFile.h>

//-----------------------------------------------------------------------------
FEBioStdSolver::FEBioStdSolver(FEModel* pfem) : FECoreTask(pfem) {}

//-----------------------------------------------------------------------------
// This simply calls the FEModel::Init
bool FEBioStdSolver::Init(const char* szfile)
{
	return (GetFEModel() ? GetFEModel()->Init() : false);
}

//-----------------------------------------------------------------------------
// This simply calls the FEM::Solve function which will solve the FE problem.
bool FEBioStdSolver::Run()
{
	// Solve the problem and return error code
	return (GetFEModel() ? GetFEModel()->Solve() : false);
}

//-----------------------------------------------------------------------------
bool FEBioRestart::Init(const char *szfile)
{
	FEBioModel& fem = static_cast<FEBioModel&>(*GetFEModel());

	// check the extension of the file
	// if the extension is .dmp or not given it is assumed the file
	// is a bindary archive (dump file). Otherwise it is assumed the
	// file is a restart input file.
	const char* ch = strrchr(szfile, '.');
	if ((ch == 0) || (strcmp(ch, ".dmp") == 0) || (strcmp(ch, ".DMP") == 0))
	{
		// the file is binary so just read the dump file and return

		// open the archive
		DumpFile ar(fem);
		if (ar.Open(szfile) == false) { fprintf(stderr, "FATAL ERROR: failed opening restart archive\n"); return false; }

		// read the archive
		try
		{
			fem.Serialize(ar);
		}
		catch (std::exception e)
		{
			fprintf(stderr, "FATAL ERROR: Exception occured while reading restart archive %s\n%s\n", szfile, e.what());
			return false;
		}
		catch (...)
		{
			fprintf(stderr, "FATAL ERROR: failed reading restart data from archive %s\n", szfile); 
			return false;
		}
	}
	else
	{
		// the file is assumed to be a xml-text input file
		FERestartImport file;
		if (file.Load(fem, szfile) == false)
		{
			char szerr[256];
			file.GetErrorMessage(szerr);
			fprintf(stderr, "%s", szerr);
			return false;
		}

		// see if user redefined restart file name
		if (file.m_szdmp[0]) fem.SetDumpFilename(file.m_szdmp);
	}

	// Open the log file for appending
	const char* szlog = fem.GetLogfileName();
	Logfile& felog = fem.GetLogFile();
	if (felog.append(szlog) == false)
	{
		printf("WARNING: Could not reopen log file. A new log file is created\n");
		felog.open(szlog);
		return false;
	}

	// inform the user from where the problem is restarted
	felog.printbox(" - R E S T A R T -", "Restarting from time %lg.\n", fem.GetCurrentTime());

	return true;
}

//-----------------------------------------------------------------------------
bool FEBioRestart::Run()
{
	// continue the analysis
	return (GetFEModel() ? GetFEModel()->Solve() : false);
}
