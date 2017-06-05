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

  typedef std::vector< std::vector<BernZMQEvent> > FEBBuffer_t;
  typedef std::unordered_map< uint32_t,uint32_t >  FEBStuckEventMap_t;
  
  size_t                                               fNFEBBuffers;
  std::unordered_map< uint64_t, FEBBuffer_t  >         fFEBBuffers;
  std::unordered_map< uint64_t, uint32_t  >            fPollLastEvent;
  std::unordered_map< uint64_t, size_t  >              fFEBCurrentBuffer;
  std::unordered_map< uint64_t, FEBStuckEventMap_t  >  fFEBStuckEventMaps;


  std::unordered_map< uint64_t,
		      std::unique_ptr<std::vector<artdaq::Fragment> > > fFragMap;
  
  //from the slf machines
  struct mytimeb
  {
    long int time;                /* Seconds since epoch, as from `time'.  */
    unsigned short int millitm; /* Additional milliseconds.  */
    short int timezone;         /* Minutes west of GMT.  */
    short int dstflag;          /* Nonzero if Daylight Savings Time used.  */
  };

  art::Timestamp fLastTimeStarted;
  art::Timestamp fLastTimeFinished;

  art::Timestamp GetNextPollTime();

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
    fNFEBBuffers(ps.get<unsigned int>("NFEBBuffers",2))
{  
  helper.reconstitutes< std::vector<artdaq::Fragment>, art::InEvent >(fModuleLabel,fInstanceLabel);
}


void bernfebdaq::BernZMQBinaryInputDetail::readFile(std::string const & filename, art::FileBlock*& fb)
{
  fb = new art::FileBlock(art::FileFormatVersion(1, "RawEvent2011"), filename);

  fInputStream.open(filename.c_str(),std::ios_base::in | std::ios_base::binary);
}

void bernfebdaq::BernZMQBinaryInputDetail::closeCurrentFile()
{
  fInputStream.close();
}

art::Timestamp bernfebdaq::BernZMQBinaryInputDetail::GetNextPollTime()
{
  bernfebdaq::BernZMQEvent zmq_ev;
  zmq_ev.mac5 = 0x1234;
  int current_position = fInputStream.tellg();

  while(true){
  
    fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
    
    if(!fInputStream)
      return art::Timestamp(0);
    
    //std::cout << "Reading event " << fEventNumber << std::endl;
    //std::cout << zmq_ev << std::endl;
    
    if(zmq_ev.MAC5()==0xffff) break;
  }

  mytimeb time_poll_started  = *((mytimeb*)((char*)(zmq_ev.adc)+sizeof(int)));
  art::Timestamp time =  ( ( (uint64_t)(time_poll_started.time) << 32 ) +
			   ( (uint64_t)(time_poll_started.millitm)*1000000 ) );

  fInputStream.seekg(current_position, fInputStream.beg);

  return time;
}

bool bernfebdaq::BernZMQBinaryInputDetail::readNext(art::RunPrincipal const* const,
						    art::SubRunPrincipal const* const,
						    art::RunPrincipal*& outR,
						    art::SubRunPrincipal*& outSR,
						    art::EventPrincipal*& outE)
{

  if(!fInputStream) return false;
  
  //std::unique_ptr< std::vector<artdaq::Fragment> > frags( new std::vector<artdaq::Fragment>);
  uint64_t frag_time;
  
  bernfebdaq::BernZMQEvent zmq_ev;
  size_t pull_counter=0;

  bernfebdaq::BernZMQFragmentMetadata metadata;
  
  while(fInputStream){
    fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
    pull_counter++;
    
    //std::cout << "Reading event " << fEventNumber << std::endl;
    //std::cout << zmq_ev << std::endl;
    
    
    //special event handling
    if(zmq_ev.MAC5()==0xffff){
      
      mytimeb time_poll_started  = *((mytimeb*)((char*)(zmq_ev.adc)+sizeof(int)));
      mytimeb time_poll_finished = *((mytimeb*)((char*)(zmq_ev.adc)+sizeof(int)+sizeof(struct mytimeb)));
      
      fLastTimeStarted  = ( ( (uint64_t)(time_poll_started.time) << 32 ) +
			    ( (uint64_t)(time_poll_started.millitm)*1000000 ) );
      fLastTimeFinished = ( ( (uint64_t)(time_poll_finished.time) << 32 ) +
			    ( (uint64_t)(time_poll_finished.millitm)*1000000 ) );

      std::cout << "--Pulled " << pull_counter-1 << " events from " << fLastTimeStarted.timeHigh() << " " << fLastTimeStarted.timeLow()
		<< " to " << fLastTimeFinished.timeHigh() << " " << fLastTimeFinished.timeLow() << std::endl;
      pull_counter=0;
      //fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));

      std::cout << "\t Buffer 11 last content = " << fFEBBuffers[0xb][fFEBCurrentBuffer[0xb]].back().hdr_str() << std::endl;

      for(auto const& buffer : fFEBBuffers){
	auto mac = buffer.first;
	fPollLastEvent[mac] = buffer.second[fFEBCurrentBuffer[mac]].back().Time_TS0();
      }
      
      continue;
    }

    if(fFEBBuffers.count(zmq_ev.MAC5())==0){
      fFEBCurrentBuffer[zmq_ev.MAC5()]=0;
      fFEBBuffers[zmq_ev.MAC5()].resize(fNFEBBuffers);
    }
    
    size_t i_buffer = fFEBCurrentBuffer[zmq_ev.MAC5()];
    auto & myFEBBuffers = fFEBBuffers[zmq_ev.MAC5()];
    auto & myFEBBuffer = myFEBBuffers[i_buffer];
    auto const& last_poll_event = fPollLastEvent[zmq_ev.MAC5()];
    
    uint32_t prev1_ts0 = 0, prev1_ts1 = 0;
    uint32_t prev2_ts0 = 0, prev2_ts1 = 0;
    //uint32_t prev3_ts0 = 0, prev3_ts1 = 0;
    //uint32_t prev4_ts0 = 0, prev4_ts1 = 0;
    bool prev_gps = false;
    bool last_poll_found = false;
    if(myFEBBuffer.size()>3){
      prev1_ts0 = myFEBBuffer.back().Time_TS0();
      prev1_ts1 = myFEBBuffer.back().Time_TS1();
      prev2_ts0 = myFEBBuffer[myFEBBuffer.size()-2].Time_TS0();
      prev2_ts1 = myFEBBuffer[myFEBBuffer.size()-2].Time_TS1();
      prev_gps =  myFEBBuffer.back().IsReference_TS0();
      //prev3_ts0 = myFEBBuffer[myFEBBuffer.size()-3].Time_TS0();
      //prev3_ts1 = myFEBBuffer[myFEBBuffer.size()-3].Time_TS1();
      //prev4_ts0 = myFEBBuffer[myFEBBuffer.size()-4].Time_TS0();
      //prev4_ts1 = myFEBBuffer[myFEBBuffer.size()-4].Time_TS1();
    }

    if(zmq_ev.Time_TS0()<prev1_ts0 && !prev_gps)
      {
	if(fFEBStuckEventMaps[zmq_ev.MAC5()][zmq_ev.Time_TS0()]==zmq_ev.Time_TS1()){
	  //std::cout << "FOUND PREV OUT OF TIME. HANDLED" << std::endl;
	  //fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
	  continue;
	}

	uint32_t diff_0_1_ts0 = (1000000000 + zmq_ev.Time_TS0() - prev1_ts0);
	uint32_t diff_0_1_ts1 = (0x40000000 + zmq_ev.Time_TS1() - prev1_ts1);
	if(zmq_ev.Time_TS1() > prev1_ts1) diff_0_1_ts1 = zmq_ev.Time_TS1() - prev1_ts1;

	uint32_t diff_0_2_ts0 = (1000000000 + zmq_ev.Time_TS0() - prev2_ts0);
	uint32_t diff_0_2_ts1 = (0x40000000 + zmq_ev.Time_TS1() - prev2_ts1);
	if(zmq_ev.Time_TS0() > prev2_ts0) diff_0_2_ts0 = zmq_ev.Time_TS0() - prev2_ts0;
	if(zmq_ev.Time_TS1() > prev2_ts1) diff_0_2_ts1 = zmq_ev.Time_TS1() - prev2_ts1;
	/*
	uint32_t diff_0_3_ts0 = (1000000000 + zmq_ev.Time_TS0() - prev3_ts0);
	uint32_t diff_0_3_ts1 = (0x40000000 + zmq_ev.Time_TS1() - prev3_ts1);
	if(zmq_ev.Time_TS0() > prev3_ts0) diff_0_3_ts0 = zmq_ev.Time_TS0() - prev3_ts0;
	if(zmq_ev.Time_TS1() > prev3_ts1) diff_0_3_ts1 = zmq_ev.Time_TS1() - prev3_ts1;

	uint32_t diff_0_4_ts0 = (1000000000 + zmq_ev.Time_TS0() - prev4_ts0);
	uint32_t diff_0_4_ts1 = (0x40000000 + zmq_ev.Time_TS1() - prev4_ts1);
	if(zmq_ev.Time_TS0() > prev4_ts0) diff_0_4_ts0 = zmq_ev.Time_TS0() - prev4_ts0;
	if(zmq_ev.Time_TS1() > prev4_ts1) diff_0_4_ts1 = zmq_ev.Time_TS1() - prev4_ts1;
	*/
	uint32_t diff_1_2_ts0 = (1000000000 + prev1_ts0 - prev2_ts0);
	uint32_t diff_1_2_ts1 = (0x40000000 + prev1_ts1 - prev2_ts1);
	if(prev1_ts0 > prev2_ts0) diff_1_2_ts0 = prev1_ts0 - prev2_ts0;
	if(prev1_ts1 > prev2_ts1) diff_1_2_ts1 = prev1_ts1 - prev2_ts1;

	int diff_diff_1_2 = (long int)(diff_1_2_ts0) - (long int)(diff_1_2_ts1);
	int diff_diff_0_2 = (long int)(diff_0_2_ts0) - (long int)(diff_0_2_ts1);
	int diff_diff_0_1 = (long int)(diff_0_1_ts0) - (long int)(diff_0_1_ts1);
	//int diff_diff_0_3 = (long int)(diff_0_3_ts0) - (long int)(diff_0_3_ts1);
	//int diff_diff_0_4 = (long int)(diff_0_4_ts0) - (long int)(diff_0_4_ts1);
	
	std::cout << "\tROLLOVER CHECK: (buf_size=" << myFEBBuffer.size() << ")"
	  //<< "\n\t" << prev4_ts0 << " " << prev4_ts1
	  //<< "\n\t" << prev3_ts0 << " " << prev3_ts1
		  << "\n\t" << prev2_ts0 << " " << prev2_ts1
		  << "\n\t" << prev1_ts0 << " " << prev1_ts1
		  << "\n\t" << zmq_ev.Time_TS0() << " " << zmq_ev.Time_TS1()
		  << "\n\t"
		  << " " << diff_0_2_ts0 << " " << diff_0_2_ts1 << " " << diff_diff_0_2
		  << " " << diff_1_2_ts0 << " " << diff_1_2_ts1 << " " << diff_diff_1_2
		  << " " << diff_0_1_ts0 << " " << diff_0_1_ts1 << " " << diff_diff_0_1
		  << std::endl;
	
	//the previous event (last in buffer) is bad
	if( std::abs(diff_diff_0_1)>20 && std::abs(diff_diff_1_2)>20 && std::abs(diff_diff_0_2)<=20){
	  
	  fFEBStuckEventMaps[zmq_ev.MAC5()][prev1_ts0] = prev1_ts1;
	  std::cout << "\t\tInserting -1 (" << prev1_ts0 << "," << prev1_ts1
	  	    << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	  
	  //std::cout << "FOUND NEW OUT OF TIME. HANDLED" << std::endl;
	  //fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
	  //continue;
	  myFEBBuffer.pop_back();
	  prev1_ts0 = myFEBBuffer.back().Time_TS0();
	  prev1_ts1 = myFEBBuffer.back().Time_TS1();
	  prev_gps =  myFEBBuffer.back().IsReference_TS0();
	}
	//this (new) event is the bad one ... (maybe...)
	else if(std::abs(diff_diff_1_2)<=20 && std::abs(diff_diff_0_2)>20 && std::abs(diff_diff_0_1)>20){
	  //check if we have multiple stuck events!
	  size_t max_multi_stuck_events = 20;
	  if(myFEBBuffer.size()<max_multi_stuck_events)
	    max_multi_stuck_events = myFEBBuffer.size();
	  bool current_is_bad=true;
	  for(size_t i_e=1; i_e<max_multi_stuck_events; ++i_e){
	    uint32_t prev_ts0 = myFEBBuffer[myFEBBuffer.size()-i_e].Time_TS0();
	    uint32_t prev_ts1 = myFEBBuffer[myFEBBuffer.size()-i_e].Time_TS1();

	    if(prev_ts0==last_poll_event){
	      last_poll_found = true;
	      std::cout << "\tFOUND LAST POLL EVENT." << std::endl;
	    }
	    uint32_t diff_ts0 = (1000000000 + zmq_ev.Time_TS0() - prev_ts0);
	    uint32_t diff_ts1 = (0x40000000 + zmq_ev.Time_TS1() - prev_ts1);
	    if(zmq_ev.Time_TS0() > prev_ts0) diff_ts0 = zmq_ev.Time_TS0() - prev_ts0;
	    if(zmq_ev.Time_TS1() > prev_ts1) diff_ts1 = zmq_ev.Time_TS1() - prev_ts1;
	    int diff_diff = (long int)(diff_ts0) - (long int)(diff_ts1);
	    //OK, so, if we find a matching one ...
	    if(std::abs(diff_diff)<=20){
	      current_is_bad=false;
	      for(size_t i_r=1; i_r<i_e; ++i_r){
		uint32_t pts0 = myFEBBuffer.back().Time_TS0();
		uint32_t pts1 = myFEBBuffer.back().Time_TS1();
		fFEBStuckEventMaps[zmq_ev.MAC5()][pts0] = pts1;
		std::cout << "\t\tInserting -" << i_r << " (" << pts0 << "," << pts1
			  << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
		myFEBBuffer.pop_back();
		prev1_ts0 = myFEBBuffer.back().Time_TS0();
		prev1_ts1 = myFEBBuffer.back().Time_TS1();
		prev_gps =  myFEBBuffer.back().IsReference_TS0();
	      }
	      break;
	    }
	  }
	  /*
	  if(std::abs(diff_diff_0_3)<=20){
	    fFEBStuckEventMaps[zmq_ev.MAC5()][prev1_ts0] = prev1_ts1;
	    fFEBStuckEventMaps[zmq_ev.MAC5()][prev2_ts0] = prev2_ts1;
	    std::cout << "\t\tInserting -1 (" << prev1_ts0 << "," << prev1_ts1
		      << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	    std::cout << "\t\tInserting -2 (" << prev2_ts0 << "," << prev2_ts1
		      << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	    myFEBBuffer.erase(myFEBBuffer.end()-2);
	    myFEBBuffer.erase(myFEBBuffer.end()-1);
	    prev1_ts0 = myFEBBuffer.back().Time_TS0();
	    prev1_ts1 = myFEBBuffer.back().Time_TS1();
	  }
	  //check if we have three stuck events!
	  else if(std::abs(diff_diff_0_4)<=20){
	    fFEBStuckEventMaps[zmq_ev.MAC5()][prev1_ts0] = prev1_ts1;
	    fFEBStuckEventMaps[zmq_ev.MAC5()][prev2_ts0] = prev2_ts1;
	    fFEBStuckEventMaps[zmq_ev.MAC5()][prev3_ts0] = prev3_ts1;
	    std::cout << "\t\tInserting -1 (" << prev1_ts0 << "," << prev1_ts1
		      << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	    std::cout << "\t\tInserting -2 (" << prev2_ts0 << "," << prev2_ts1
		      << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	    std::cout << "\t\tInserting -3 (" << prev3_ts0 << "," << prev2_ts1
		      << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	    myFEBBuffer.erase(myFEBBuffer.end()-3);
	    myFEBBuffer.erase(myFEBBuffer.end()-2);
	    myFEBBuffer.erase(myFEBBuffer.end()-1);
	    prev1_ts0 = myFEBBuffer.back().Time_TS0();
	    prev1_ts1 = myFEBBuffer.back().Time_TS1();
	  }
	  */
	  //nope! current event is the bad one
	  if(current_is_bad){

	    fFEBStuckEventMaps[zmq_ev.MAC5()][zmq_ev.Time_TS0()] = zmq_ev.Time_TS1();
	    //std::cout << "\t\tInserting 0 (" << zmq_ev.Time_TS0() << "," << zmq_ev.Time_TS1()
	    //	      << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	    
	    //std::cout << "FOUND NEW OUT OF TIME. HANDLED" << std::endl;
	    //fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
	    continue;
	    //myFEBBuffer.pop_back();
	  }
	}
	//umm ... two events ago is bad?
	else if(std::abs(diff_diff_1_2)>20 && std::abs(diff_diff_0_2)>20 && std::abs(diff_diff_0_1)<=20){
	  fFEBStuckEventMaps[zmq_ev.MAC5()][prev2_ts0] = prev2_ts1;
	  //std::cout << "\t\tInserting -2 (" << prev2_ts0 << "," << prev2_ts1
	  //	    << ") as stuck event in " << zmq_ev.MAC5() << std::endl;
	  
	  //std::cout << "FOUND NEW OUT OF TIME. HANDLED" << std::endl;
	  //fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
	  //continue;
	  //myFEBBuffer.pop_back();
	  myFEBBuffer.erase(myFEBBuffer.end()-2);
	  prev1_ts0 = myFEBBuffer.back().Time_TS0();
	  prev1_ts1 = myFEBBuffer.back().Time_TS1();
	  prev_gps =  myFEBBuffer.back().IsReference_TS0();
	}
	else{
	  size_t max_multi_stuck_events = 20;
	  if(myFEBBuffer.size()<max_multi_stuck_events)
	    max_multi_stuck_events = myFEBBuffer.size();
	  for(size_t i_e=1; i_e<max_multi_stuck_events; ++i_e){
	    uint32_t prev_ts0 = myFEBBuffer[myFEBBuffer.size()-i_e].Time_TS0();
	    if(prev_ts0==last_poll_event){
	      last_poll_found = true;
	      std::cout << "\tFOUND LAST POLL EVENT." << std::endl;
	      break;
	    }
	  }
	}
      }
    
    /*    
    if(myFEBBuffer.size()>0)
      prev1_ts0 = myFEBBuffer.back().Time_TS0();
    else
      prev1_ts0 = 0;
    */

    myFEBBuffer.push_back(zmq_ev);
    
    if(zmq_ev.IsReference_TS0() || (zmq_ev.Time_TS0()<prev1_ts0 && !prev_gps && !last_poll_found) )
      {
	art::Timestamp mytime = GetNextPollTime();

	if(zmq_ev.IsReference_TS0()) std::cout << "FOUND GPS PULSE FOR FEB ";
	else if(zmq_ev.Time_TS0()<prev1_ts0 && !prev_gps) std::cout << "FOUND GPS ROLLOVER FOR FEB ";
	
	std:: cout << zmq_ev.MAC5() << " : "
		   << mytime.timeHigh() << " " << mytime.timeLow()
		   << " ( " << zmq_ev.Time_TS0() << ", prev=" << prev1_ts0 << ", " << zmq_ev.Time_TS1() << " )"
		   << std::endl;

	uint32_t sec_strt, sec_end;
	if(mytime.timeLow() < 0.5e9){
	  sec_strt = mytime.timeHigh()-2;
	  sec_end = mytime.timeHigh()-1;
	}
	else{
	  sec_strt = mytime.timeHigh()-1;
	  sec_end = mytime.timeHigh();
	}

	int time_correction_diff = 0;
	if(zmq_ev.IsReference_TS0())
	  time_correction_diff = (int)(zmq_ev.Time_TS0()) - 1000000000;
	
	metadata = BernZMQFragmentMetadata(sec_strt,0,
					   sec_end,0,
					   time_correction_diff, 0,
					   0,0,
					   zmq_ev.MAC5(),0,
					   32,12);

	frag_time =  ( ((uint64_t)metadata.time_start_seconds() << 32 ) + metadata.time_start_nanosec() );

	size_t i_next_buffer=i_buffer+1;	    
	if(i_next_buffer==fNFEBBuffers) i_next_buffer=0;
	myFEBBuffers[i_next_buffer].clear();

	if(!zmq_ev.IsReference_TS0()){
	  myFEBBuffers[i_next_buffer].push_back(zmq_ev);
	  myFEBBuffer.pop_back();
	  fFEBCurrentBuffer[zmq_ev.MAC5()] = i_next_buffer;	  
	}

	std::unique_ptr<artdaq::Fragment> myfragptr =
	  artdaq::Fragment::FragmentBytes(metadata.n_events()*sizeof(BernZMQEvent),
					  fEventNumber,metadata.feb_id(),
					  bernfebdaq::detail::FragmentType::BernZMQ, metadata,
					  metadata.time_start_seconds());
	artdaq::Fragment myfrag(*myfragptr);

	std::cout << " There are " << fFragMap.count(frag_time) << " fragment collections at " << frag_time << std::endl;
	
	if(fFragMap.count(frag_time)==0){
	  std::cout << "\tCreating new frag list ..." << std::endl;
	  fFragMap[frag_time].reset( new std::vector<artdaq::Fragment>);
	}

	std::cout << " There are " << fFragMap[frag_time]->size() << " fragments in that collection." << std::endl;
	
	fFragMap[frag_time]->emplace_back( myfrag );
	std::copy((char*)(&myFEBBuffer[0]),
		  (char*)(&myFEBBuffer[0])+metadata.n_events()*sizeof(BernZMQEvent),
		  (char*)(fFragMap[frag_time]->back().dataBegin()));

	std::cout << " Now there are " << fFragMap[frag_time]->size() << " fragments in that collection." << std::endl;
	
	if(fFragMap[frag_time]->size()==9)
	  break;
      }
	
    //fInputStream.read((char*)(&zmq_ev),sizeof(bernfebdaq::BernZMQEvent));
  }

  size_t run_num = 1;
  size_t subrun_num = 1;

  std::cout << "Making principals..." << std::endl;

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

  /*
  std::cout << "Making fragment ..." << std::endl;
  
  //frags->resize(1);
  std::unique_ptr<artdaq::Fragment> myfragptr =
    artdaq::Fragment::FragmentBytes(metadata.n_events()*sizeof(BernZMQEvent),
				    fEventNumber,metadata.feb_id(),
				    bernfebdaq::detail::FragmentType::BernZMQ, metadata,
				    metadata.time_start_seconds());
  artdaq::Fragment myfrag(*myfragptr);
  frags->emplace_back( myfrag );
  std::copy((char*)(&myFEBBuffer[0]),
	    (char*)(&myFEBBuffer[0])+metadata.n_events()*sizeof(BernZMQEvent),
	    (char*)(frags->back().dataBegin()));
  */
  std::cout << "Made fragment, now put onto event." << std::endl;
  
  art::put_product_in_principal(std::move(fFragMap[frag_time]),
				*outE,
				fModuleLabel, fInstanceLabel);

  fFragMap.erase(frag_time);
  
  return true;
}



 
// Optional typedef.
namespace bernfebdaq {
  using BernZMQBinaryInputSource = art::Source<BernZMQBinaryInputDetail>;
}

// Define the input source to the art system.
DEFINE_ART_INPUT_SOURCE(bernfebdaq::BernZMQBinaryInputSource)
