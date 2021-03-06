// $Id$
//==============================================================================
//!
//! \file NewmarkDriver.h
//!
//! \date Jul 9 2013
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Newmark driver for isogeometric analysis of elastodynamics problems.
//!
//==============================================================================

#ifndef _NEWMARK_DRIVER_H
#define _NEWMARK_DRIVER_H

#include "DataExporter.h"
#include "TimeStep.h"
#include "Utilities.h"
#include "tinyxml.h"
#include <fstream>


/*!
  \brief Driver for isogeometric FEM analysis of elastodynamic problems.
*/

template<class Newmark> class NewmarkDriver : public Newmark
{
public:
  //! \brief The constructor forwards to the parent class constructor.
  //! \param sim Reference to the spline FE model
  NewmarkDriver(SIMbase& sim) : Newmark(sim) { doInitAcc = false; }
  //! \brief Empty destructor.
  virtual ~NewmarkDriver() {}

protected:
  //! \brief Parses a data section from an XML document.
  //! \param[in] elem The XML element to parse
  virtual bool parse(const TiXmlElement* elem)
  {
    if (!strcasecmp(elem->Value(),"newmarksolver"))
    {
      utl::getAttribute(elem,"initacc",doInitAcc);
      const TiXmlElement* child = elem->FirstChildElement();
      for (; child; child = child->NextSiblingElement())
        params.parse(child);
    }
    else if (!strcasecmp(elem->Value(),"postprocessing"))
    {
      const TiXmlElement* respts = elem->FirstChildElement("resultpoints");
      if (respts)
        utl::getAttribute(respts,"file",pointfile);
    }

    return this->Newmark::parse(elem);
  }

public:
  //! \brief Invokes the main time stepping simulation loop.
  //! \param writer HDF5 results exporter
  //! \param[in] ztol Truncate norm values smaller than this to zero
  //! \param[in] outPrec Number of digits after the decimal point in norm print
  int solveProblem(DataExporter* writer, utl::LogStream*, double,
                   double ztol = 1.0e-8, std::streamsize outPrec = 0)
  {
    return this->solveProblem(writer,ztol,outPrec);
  }

  //! \brief Invokes the main time stepping simulation loop.
  //! \param writer HDF5 results exporter
  //! \param[in] ztol Truncate norm values smaller than this to zero
  //! \param[in] outPrec Number of digits after the decimal point in norm print
  int solveProblem(DataExporter* writer,
                   double ztol = 1.0e-8, std::streamsize outPrec = 0)
  {
    // Initialize the linear solver
    this->initEqSystem();

    // Calculate initial accelerations
    if (doInitAcc && !this->initAcc(ztol,outPrec))
      return 4;

    SIMoptions::ProjectionMap::const_iterator pi = Newmark::opt.project.begin();
    bool doProject  = pi != Newmark::opt.project.end();
    double nextSave = params.time.t + Newmark::opt.dtSave;

    std::streamsize ptPrec = outPrec > 0 ? outPrec : 3;
    std::ostream* os = &std::cout;
    if (!pointfile.empty())
      os = new std::ofstream(pointfile.c_str());

    // Invoke the time-step loop
    int status = 0;
    for (int iStep = 0; status == 0 && this->advanceStep(params);)
    {
      // Solve the dynamic FE problem at this time step
      if (this->solveStep(params,SIM::DYNAMIC,ztol,outPrec) != SIM::CONVERGED)
      {
        status = 5;
        break;
      }

      if (doProject)
      {
        // Project the secondary results onto the spline basis
        Newmark::model.setMode(SIM::RECOVERY);
        if (!Newmark::model.project(proSol,Newmark::solution.front(),
                                    pi->first,params.time))
          status += 6;
      }

      // Print solution components at the user-defined points
      utl::LogStream log(*os);
      this->dumpResults(params.time.t,log,ptPrec,pointfile.empty());

      if (params.hasReached(nextSave))
      {
        // Save solution variables to VTF
        if (Newmark::opt.format >= 0)
          if (!this->saveStep(++iStep,params.time.t) ||
              !Newmark::model.writeGlvS1(this->getVelocity(),iStep,
                                         Newmark::nBlock,params.time.t,
                                         "velocity",20) ||
              !Newmark::model.writeGlvS1(this->getAcceleration(),iStep,
                                         Newmark::nBlock,params.time.t,
                                         "acceleration",30) ||
              !Newmark::model.writeGlvP(proSol,iStep,Newmark::nBlock,110,
                                        pi->second.c_str()))
            status += 7;

        // Save solution variables to HDF5
        if (writer)
          if (!writer->dumpTimeLevel(&params))
            status += 8;

        nextSave = params.time.t + Newmark::opt.dtSave;
        if (nextSave > params.stopTime)
          nextSave = params.stopTime; // Always save the final step
      }
    }

    if (!pointfile.empty())
      delete os;

    return status;
  }

  //! \brief Accesses the projected solution.
  const Vector& getProjection() const { return proSol; }

  //! \brief Overrides the stop time that was read from the input file.
  void setStopTime(double t) { params.stopTime = t; }

private:
  TimeStep params; //!< Time stepping parameters
  Matrix   proSol; //!< Projected secondary solution

  std::string pointfile; //!< Name of output file for point results
  bool        doInitAcc; //!< If \e true, calculate initial accelerations
};

#endif
