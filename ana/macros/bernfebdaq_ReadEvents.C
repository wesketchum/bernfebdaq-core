/*************************************************************
 * 
 * bernfebdaq_ReadEvents() macro
 * 
 * This is a simple demonstration of reading data from the CRT DAQ
 * in a ROOT Macro
 *
 * Prior to loading the macro, you need to load the library:
 * root [0] .L $BERNFEBDAQ_CORE_LIB/libbernfebdaq-core_Overlays.dylib (or .so)
 *
 * Wesley Ketchum (wketchum@fnal.gov), June08, 2017
 * 
 *************************************************************/

#include "../include/bernfebdaq_ReadEvents.h"

void bernfebdaq_ReadEvents(std::string filename, std::string output_file="output.root", int max_events=-1) {
  
  std::vector<std::string> filenames;
  filenames.push_back(filename);

  //see this by doing an event dump 'lar -c eventdump.fcl -s <file_name> -n 1'
  //should be "daq" for the files with all servers
  art::InputTag daqTag("crtdaq");

  //setup output file ...
  TFile f_out(output_file.c_str(),"RECREATE");
  
  //setup a tree for the crt events
  bernfebdaq::BernZMQEvent crt_event;
  double crt_t0_corr;
  TTree tr_crtevt("tr_crtevt","CRT Event Tree");
  tr_crtevt.Branch("mac5",&crt_event.mac5,"mac5/s");
  tr_crtevt.Branch("flags",&crt_event.flags,"flags/s");  
  tr_crtevt.Branch("ts0",&crt_event.ts0,"ts0/i");
  tr_crtevt.Branch("ts1",&crt_event.ts1,"ts1/i");
  tr_crtevt.Branch("t0_corr",&crt_t0_corr,"t0_corr/D");
  tr_crtevt.Branch("adc",&crt_event.adc,"adc[32]/s");

  //setup a tree for the crt fragments
  uint32_t t_strt_s,t_strt_ns,t_end_s,t_end_ns;
  uint64_t feb_id;
  uint32_t n_events;
  TTree tr_crtfrag("tr_crtfrag","CRT Fragment Tree");
  tr_crtfrag.Branch("t_strt_s",&t_strt_s,"t_strt_s/i");
  tr_crtfrag.Branch("t_strt_ns",&t_strt_ns,"t_strt_ns/i");
  tr_crtfrag.Branch("t_end_s",&t_end_s,"t_end_s/i");
  tr_crtfrag.Branch("t_end_ns",&t_end_ns,"t_end_ns/i");
  tr_crtfrag.Branch("feb_id",&feb_id,"feb_id/l");
  tr_crtfrag.Branch("n_events",&n_events,"n_events/i");
  
  //loop over the "art" events.
  for (gallery::Event ev(filenames) ; !ev.atEnd(); ev.next()) {

    if(max_events==0) break;

    std::cout << "Processing "
	      << "Run " << ev.eventAuxiliary().run() << ", "
	      << "Event " << ev.eventAuxiliary().event() << ", "
	      << "Time " << ev.eventAuxiliary().time().timeHigh() << std::endl;
    
    //this gets the vector of artdaq fragments
    auto const& daq_handle = ev.getValidHandle<std::vector<artdaq::Fragment>>(daqTag);
    
    //this loops over the fragments
    for(auto const& rawfrag : *daq_handle){

      //this applies the overlay. See bernfebdaq-core/Overlays/BernZMQFragment.hh"

      bernfebdaq::BernZMQFragment frag(rawfrag);

      // Now do all the stuff you want to do with the fragments ....

      //There's a streamer...
      std::cout << frag << std::endl;

      //get a pointer to the metadata class
      //the pointer keeps us from making a copy of the data...
      bernfebdaq::BernZMQFragmentMetadata const *metadata = frag.metadata();

      //fill in our crt fragment tree...
      t_strt_s = metadata->time_start_seconds();
      t_strt_ns = metadata->time_start_nanosec();
      t_end_s = metadata->time_end_seconds();
      t_end_ns = metadata->time_end_nanosec();
      feb_id = metadata->feb_id();
      n_events = metadata->n_events();
      tr_crtfrag.Fill();
      

      //do a loop over all the CRT event objects
      size_t n_hits = metadata->n_events();
      for(size_t i_h=0; i_h < n_hits; ++i_h){

	//this copies the event object
	crt_event = frag.Event(i_h);
	crt_t0_corr = GetCorrectedTime(crt_event.Time_TS0(),*metadata);
	tr_crtevt.Fill();
	
      }
      
    }
    
    max_events--;
    
  } //end loop over events!


  f_out.Write();
  f_out.Close();

}
