////////////////////////////////////////////////////////////////////////
// Class:       CRTMerger
// Plugin Type: producer (art v2_06_03)
// File:        CRTMerger_module.cc
//
// Generated at Thu Jun 29 23:19:10 2017 by Wesley Ketchum using cetskelgen
// from cetlib version v2_03_00.
//
// Based strongly off of module that Kevin Wiermann and Kolahal B. wrote.
//
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <memory>

#include "bernfebdaq-core/Overlays/BernZMQFragment.hh"
#include "artdaq-core/Data/Fragments.hh"

#include "gallery/Event.h"
#include "gallery/ValidHandle.h"

namespace bernfebdaq {
  class CRTMerger;
}


class bernfebdaq::CRTMerger : public art::EDProducer {
public:
  explicit CRTMerger(fhicl::ParameterSet const & p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  CRTMerger(CRTMerger const &) = delete;
  CRTMerger(CRTMerger &&) = delete;
  CRTMerger & operator = (CRTMerger const &) = delete;
  CRTMerger & operator = (CRTMerger &&) = delete;

  // Required functions.
  void produce(art::Event & e) override;

  // Selected optional functions.
  void beginJob() override;
  void endJob() override;
  void reconfigure(fhicl::ParameterSet const & p) override;

  //void respondToCloseInputFile(art::FileBlock const & fb) override;
  void respondToOpenInputFile(art::FileBlock const & fb) override;

private:

  std::vector<std::string>   fCRTFileNames;
  art::InputTag              fCRTDataLabel;
  gallery::Event             fCRTEvent;

  long int                   fCRTTimeOffsetNanoseconds;

  bool                       fAssumeInputFileListOrdered;
  int                        fVerbosity;
    
  std::vector<artdaq::Fragment> fPreviousFragmentVector;
  
};


bernfebdaq::CRTMerger::CRTMerger(fhicl::ParameterSet const & p)
: fCRTFileNames(p.get< std::vector<std::string> >("CRTFileNames")),
  fCRTDataLabel(p.get< art::InputTag >("CRTDataLabel")),
  fCRTEvent(fCRTFileNames)
{
  this->reconfigure(p);

  this->produces< std::vector<artdaq::Fragment> >();
}

void bernfebdaq::CRTMerger::produce(art::Event & e)
{
  
  std::unique_ptr< std::vector<artdaq::Fragment> >
    FragmentVectorPtr(new std::vector<artdaq::Fragment>);
  std::vector<artdaq::Fragment> & FragmentVector(*FragmentVectorPtr);

  uint32_t time_seconds = e.time().timeHigh();
  uint32_t time_nanosec = e.time().timeLow();

  uint64_t time_tpc = ((uint64_t)time_seconds)*1000000000 + (uint64_t)time_nanosec;
  
  if(fVerbosity>0)
    std::cout << "TPC event time is "
	      << time_tpc/1000000000 << " s, "
	      << time_tpc%1000000000 << " ns."
	      << std::endl;
  
  while(!fCRTEvent.atEnd()){

    if(fVerbosity>0)
      std::cout << "Looping in CRT events..." << std::endl;
    
    auto const& crtdaq_handle = 
      fCRTEvent.getValidHandle< std::vector<artdaq::Fragment> >(fCRTDataLabel);

    auto const& crtdaq_vector(*crtdaq_handle);

    if(crtdaq_vector.size()==0){
      if(fVerbosity>0)
	std::cout << "\tCRTDAQ vector has zero size. Move to next CRT event." << std::endl;
      fCRTEvent.next();
      continue;
    }

    bernfebdaq::BernZMQFragment frag(crtdaq_vector[0]);
    uint64_t time_crt_start = ((uint64_t)(frag.metadata()->time_start_seconds()))*1000000000 +
      (uint64_t)(frag.metadata()->time_start_nanosec());
    uint64_t time_crt_end = ((uint64_t)(frag.metadata()->time_end_seconds()))*1000000000 +
      (uint64_t)(frag.metadata()->time_end_nanosec());

    time_crt_start += fCRTTimeOffsetNanoseconds;
    time_crt_end += fCRTTimeOffsetNanoseconds;
    
    if(fVerbosity>0)
      std::cout << "\tCRT event time is ("
		<< time_crt_start/1000000000 << " s ,"
		<< time_crt_start%1000000000 << " ns)"
		<< " --> ("
		<< time_crt_end/1000000000 << " s ,"
		<< time_crt_end%1000000000 << " ns)"
		<< std::endl;
    
    if( time_tpc>=time_crt_start && time_tpc<=time_crt_end)
      {
	if(fVerbosity>0)
	  std::cout << "\t\tFOUND MATCH!" << std::endl;
	FragmentVector = fPreviousFragmentVector; //prev window
	FragmentVector.insert(FragmentVector.end(),crtdaq_vector.begin(),crtdaq_vector.end()); //this window
	fCRTEvent.next();
	if(!fCRTEvent.atEnd()){
	  auto const& crtdaq_next_handle = 
	    fCRTEvent.getValidHandle< std::vector<artdaq::Fragment> >(fCRTDataLabel);	  
	  auto const& crtdaq_next_vector(*crtdaq_next_handle);
	  FragmentVector.insert(FragmentVector.end(),crtdaq_next_vector.begin(),crtdaq_next_vector.end()); //next window	  
	  fPreviousFragmentVector = crtdaq_vector;
	}
	break;
      }
    else if(time_tpc<time_crt_start)
      {
	if(fVerbosity>1)
	  std::cout << "\t\tCRTEvents too advanced. Go to next TPC event!" << std::endl;
	break;
      }
    else if(time_tpc>time_crt_end)
      {
	if(fVerbosity>1)
	  std::cout << "\t\tCRTEvents too early. Go to next CRT event!" << std::endl;
	fPreviousFragmentVector = crtdaq_vector;
	fCRTEvent.next();
      }
  }

  e.put(std::move(FragmentVectorPtr));

}

void bernfebdaq::CRTMerger::beginJob()
{
}

void bernfebdaq::CRTMerger::endJob()
{
}


void bernfebdaq::CRTMerger::reconfigure(fhicl::ParameterSet const & p)
{
  fCRTFileNames = p.get< std::vector<std::string> >("CRTFileNames");
  fCRTDataLabel = p.get< art::InputTag >("CRTDataLabel");
  fVerbosity    = p.get<int>("Verbosity");
  fAssumeInputFileListOrdered = p.get<bool>("AssumeInputFileListOrdered");
  fCRTTimeOffsetNanoseconds = p.get<long int>("CRTTimeOffsetNanoseconds");
}
/*
void bernfebdaq::CRTMerger::respondToCloseInputFile(art::FileBlock const & fb)
{
}
*/

void bernfebdaq::CRTMerger::respondToOpenInputFile(art::FileBlock const &)
{
  if(!fAssumeInputFileListOrdered)
    fCRTEvent.toBegin();
}


DEFINE_ART_MODULE(bernfebdaq::CRTMerger)
