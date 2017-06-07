#include "art/Framework/Core/InputSourceMacros.h" 
#include "art/Framework/IO/Sources/Source.h" 
#include "art/Framework/IO/Sources/SourceTraits.h" 

#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Core/ProductRegistryHelper.h"
#include "art/Framework/IO/Sources/SourceHelper.h"
#include "art/Framework/Core/FileBlock.h"
#include "art/Framework/Principal/RunPrincipal.h"
#include "art/Framework/Principal/SubRunPrincipal.h"
#include "art/Framework/Principal/EventPrincipal.h"
#include "canvas/Persistency/Provenance/SubRunID.h"
#include "art/Framework/IO/Sources/put_product_in_principal.h"

#include "bernfebdaq-core/Overlays/BernZMQFragment.hh"
#include "bernfebdaq-core/Overlays/FragmentType.hh"
#include "artdaq-core/Data/Fragment.hh"

#include "BernZMQBinaryInputStreamReader.hh"

#include <fstream>
#include <iostream>
#include <vector>

namespace bernfebdaq {
  // Forward declaration of detail class.
  class BernZMQBinaryInputDetail;
}

////////////////////////////////////////////////////////////////////////
// Any declarations of specializations of Source traits (see
// art/Framework/IO/Sources/SourceTraits.h) should go here.
//
// ...
//
////////////////////////////////////////////////////////////////////////

// Definition of detail class.
class bernfebdaq::BernZMQBinaryInputDetail {

public: 
  BernZMQBinaryInputDetail(fhicl::ParameterSet const &pset,
			   art::ProductRegistryHelper &helper,
			   art::SourceHelper const &src_hlpr);
  
  void readFile(std::string const & filename, art::FileBlock*& fb);

  bool readNext(art::RunPrincipal const* const inR,
		art::SubRunPrincipal const* const inSR,
		art::RunPrincipal*& outR,
		art::SubRunPrincipal*& outSR,
		art::EventPrincipal*& outE);
  void closeCurrentFile();



private:
  
  art::SourceHelper  fSourceHelper;

  std::string        fModuleLabel;
  std::string        fInstanceLabel;

  std::ifstream      fInputStream;
  
  size_t             fEventNumber;
  size_t             fPrevRunNumber;
  size_t             fPrevSubRunNumber;

  size_t             fFragsPerEvent;
  
  BernZMQBinaryInputStreamReader fStreamReader;
  FragMap_t          fFragMap;

  void writeEvent(FragMap_t::value_type & e,
		  art::RunPrincipal const* const,
		  art::SubRunPrincipal const* const,
		  art::RunPrincipal*& outR,
		  art::SubRunPrincipal*& outSR,
		  art::EventPrincipal*& outE);
  
};

bernfebdaq::BernZMQBinaryInputDetail::BernZMQBinaryInputDetail(fhicl::ParameterSet const & ps,
							       art::ProductRegistryHelper & helper,
							       art::SourceHelper const & src_hlpr)
  : fSourceHelper(src_hlpr),
    fModuleLabel(ps.get<std::string>("ModuleLabel")),
    fInstanceLabel(ps.get<std::string>("InstanceLabel")),
    fEventNumber(0),
    fPrevRunNumber(0),
    fPrevSubRunNumber(0),
    fFragsPerEvent(ps.get<size_t>("FragsPerEvent")),
    fStreamReader(ps)
{  
  helper.reconstitutes< std::vector<artdaq::Fragment>, art::InEvent >(fModuleLabel,fInstanceLabel);
}


void bernfebdaq::BernZMQBinaryInputDetail::readFile(std::string const & filename, art::FileBlock*& fb)
{
  fb = new art::FileBlock(art::FileFormatVersion(1, "RawEvent2011"), filename);

  fInputStream.open(filename.c_str(),std::ios_base::in | std::ios_base::binary);
  fStreamReader.SetInputStream(fInputStream);
}

void bernfebdaq::BernZMQBinaryInputDetail::closeCurrentFile()
{
  fInputStream.close();
}

void bernfebdaq::BernZMQBinaryInputDetail::writeEvent(FragMap_t::value_type & e,
						      art::RunPrincipal const* const,
						      art::SubRunPrincipal const* const,
						      art::RunPrincipal*& outR,
						      art::SubRunPrincipal*& outSR,
						      art::EventPrincipal*& outE)
{
  uint64_t const& frag_time = e.first;
  
  size_t run_num = 1;
  size_t subrun_num = 1;
    
  if(run_num!=fPrevRunNumber){
    outR = fSourceHelper.makeRunPrincipal(run_num,frag_time);
    outSR = fSourceHelper.makeSubRunPrincipal(run_num,subrun_num,frag_time);
    fPrevRunNumber = run_num;
    fPrevSubRunNumber =subrun_num;
  }
  else if(subrun_num!=fPrevSubRunNumber){
    outSR = fSourceHelper.makeSubRunPrincipal(run_num,subrun_num,frag_time);
    fPrevSubRunNumber =subrun_num;
  }
  
  outE = fSourceHelper.makeEventPrincipal(run_num,
					  subrun_num,
					  fEventNumber++,
					  frag_time);
  
  art::put_product_in_principal(std::move(e.second),
				*outE,
				fModuleLabel, fInstanceLabel);
}

bool bernfebdaq::BernZMQBinaryInputDetail::readNext(art::RunPrincipal const* const inR,
						    art::SubRunPrincipal const* const inSR,
						    art::RunPrincipal*& outR,
						    art::SubRunPrincipal*& outSR,
						    art::EventPrincipal*& outE)
{

  if(!fInputStream) {
    for(FragMap_t::iterator i_fm=fFragMap.begin(); i_fm!=fFragMap.end(); i_fm++){
      auto & e = *(fFragMap.begin());
      writeEvent(e,inR,inSR,outR,outSR,outE);      
      fFragMap.erase(fFragMap.begin());
      return true;
    }
    return false;
  }

  while(fInputStream){

    //std::cout << "Total of " << fFragMap.size() << " fragment events registered." << std::endl;
    
    for(FragMap_t::iterator i_fm=fFragMap.begin(); i_fm!=fFragMap.end(); i_fm++){
      
      if(i_fm->second->size()<fFragsPerEvent) continue;

      auto & e = *(fFragMap.begin());
      writeEvent(e,inR,inSR,outR,outSR,outE);      
      fFragMap.erase(fFragMap.begin());
      return true;
    }

    auto ts = fStreamReader.ReadUntilSpecialEvent(fFragMap);
    std::cout << "Reached pull event ... " << ts.timeHigh() << ", " << ts.timeLow()
	      << " (" << fFragMap.size() << " fragment events registered)"
	      << std::endl;
    
  }

  for(FragMap_t::iterator i_fm=fFragMap.begin(); i_fm!=fFragMap.end(); i_fm++){
    auto & e = *(fFragMap.begin());
    writeEvent(e,inR,inSR,outR,outSR,outE);      
    fFragMap.erase(fFragMap.begin());
    return true;
  }
  return false;
}



 
// Optional typedef.
namespace bernfebdaq {
  using BernZMQBinaryInputSource = art::Source<BernZMQBinaryInputDetail>;
}

// Define the input source to the art system.
DEFINE_ART_INPUT_SOURCE(bernfebdaq::BernZMQBinaryInputSource)
