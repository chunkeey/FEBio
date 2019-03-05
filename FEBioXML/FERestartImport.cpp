// FERestartImport.cpp: implementation of the FERestartImport class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "FERestartImport.h"
#include "FECore/FESolver.h"
#include "FECore/FEAnalysis.h"
#include "FECore/FEModel.h"
#include "FECore/DumpFile.h"
#include "FEBioLoadDataSection.h"
#include "FEBioStepSection.h"

void FERestartControlSection::Parse(XMLTag& tag)
{
	FEModel& fem = *GetFEModel();
	FEAnalysis* pstep = fem.GetCurrentStep();

	++tag;
	do
	{
		if      (tag == "time_steps"        ) tag.value(pstep->m_ntime);
		else if (tag == "final_time"        ) tag.value(pstep->m_final_time);
		else if (tag == "step_size"         ) tag.value(pstep->m_dt0);
		else if (tag == "time_stepper")
		{
			pstep->m_bautostep = true;
			FETimeStepController& tc = pstep->m_timeController;
			++tag;
			do
			{
				if      (tag == "max_retries") tag.value(tc.m_maxretries);
				else if (tag == "opt_iter"   ) tag.value(tc.m_iteopt);
				else if (tag == "dtmin"      ) tag.value(tc.m_dtmin);
				else throw XMLReader::InvalidTag(tag);

				++tag;
			}
			while (!tag.isend());
		}
		else if (tag == "plot_level")
		{
			char szval[256];
			tag.value(szval);
			if      (strcmp(szval, "PLOT_NEVER"        ) == 0) pstep->SetPlotLevel(FE_PLOT_NEVER);
			else if (strcmp(szval, "PLOT_MAJOR_ITRS"   ) == 0) pstep->SetPlotLevel(FE_PLOT_MAJOR_ITRS);
			else if (strcmp(szval, "PLOT_MINOR_ITRS"   ) == 0) pstep->SetPlotLevel(FE_PLOT_MINOR_ITRS);
			else if (strcmp(szval, "PLOT_MUST_POINTS"  ) == 0) pstep->SetPlotLevel(FE_PLOT_MUST_POINTS);
			else if (strcmp(szval, "PLOT_FINAL"        ) == 0) pstep->SetPlotLevel(FE_PLOT_FINAL);
			else if (strcmp(szval, "PLOT_STEP_FINAL"   ) == 0) pstep->SetPlotLevel(FE_PLOT_STEP_FINAL);
			else if (strcmp(szval, "PLOT_AUGMENTATIONS") == 0) pstep->SetPlotLevel(FE_PLOT_AUGMENTATIONS);
			else throw XMLReader::InvalidValue(tag);
		}
		else throw XMLReader::InvalidTag(tag);

		++tag;
	}
	while (!tag.isend());

	// we need to reevaluate the time step size and end time
	fem.GetTime().timeIncrement = pstep->m_dt0;
	pstep->m_tend = pstep->m_tstart = pstep->m_ntime*pstep->m_dt0;

}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

FERestartImport::FERestartImport()
{
	
}

FERestartImport::~FERestartImport()
{
	
}

//-----------------------------------------------------------------------------

bool FERestartImport::Load(FEModel& fem, const char* szfile)
{
	// open the XML file
	if (m_xml.Open(szfile) == false) return errf("FATAL ERROR: Failed opening restart file %s\n", szfile);

	m_fem = &fem;
	m_builder = new FEModelBuilder(fem);

	m_szdmp[0] = 0;

	m_map["Control" ] = new FERestartControlSection(this);

	// make sure we can redefine curves in the LoadData section
	FEBioLoadDataSection* lcSection = new FEBioLoadDataSection(this);
	lcSection->SetRedefineCurvesFlag(true);

	m_map["LoadData"] = lcSection;

	// set the file version to make sure we are using the correct format
	SetFileVerion(0x0205);

	// loop over child tags
	bool ret = true;
	try
	{
		// find the root element
		XMLTag tag;
		if (m_xml.FindTag("febio_restart", tag) == false) return errf("FATAL ERROR: File does not contain restart data.\n");

		// check the version number
		const char* szversion = tag.m_att[0].m_szatv;
		int nversion = -1;
		if      (strcmp(szversion, "1.0") == 0) nversion = 1;
		else if (strcmp(szversion, "2.0") == 0) nversion = 2;

		if (nversion == -1) return errf("FATAL ERROR: Incorrect restart file version\n");

		// Add the Step section for version 2
		if (nversion == 2)
		{
			m_map["Step"] = new FEBioStepSection25(this);
		}

		// the first section has to be the archive
		++tag;
		if (tag != "Archive") return errf("FATAL ERROR: The first element must be the archive name\n");
		char szar[256];
		tag.value(szar);

		// open the archive
		DumpFile ar(fem);
		if (ar.Open(szar) == false) return errf("FATAL ERROR: failed opening restart archive\n");

		// read the archive
		fem.Serialize(ar);

		// read the rest of the restart input file
		ret = ParseFile(tag);
	}
	catch (XMLReader::Error& e)
	{
		fprintf(stderr, "FATAL ERROR: %s (line %d)\n", e.GetErrorString(), m_xml.GetCurrentLine());
		ret = false;
	}
	catch (...)
	{
		fprintf(stderr, "FATAL ERROR: unrecoverable error (line %d)\n", m_xml.GetCurrentLine());
		ret = false;
	}

	// close the XML file
	m_xml.Close();

	// we're done!
	return ret;
}
