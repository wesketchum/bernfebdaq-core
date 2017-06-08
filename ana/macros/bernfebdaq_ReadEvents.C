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
 * Wesley Ketchum (wketchum@fnal.gov), Aug28, 2016
 * 
 *************************************************************/

#include "../include/bernfebdaq_ReadEvents.h"

void bernfebdaq_ReadEvents(std::string filename, int max_events=-1) {
  
  std::vector<std::string> filenames;
  filenames.push_back(filename);

  art::InputTag daqTag("daq:crt04");
  
  for (gallery::Event ev(filenames) ; !ev.atEnd(); ev.next()) {

    if(max_events==0) break;

    std::cout << "Processing "
	      << "Run " << ev.eventAuxiliary().run() << ", "
	      << "Event " << ev.eventAuxiliary().event() << ", "
	      << "Time " << ev.eventAuxiliary().time().timeHigh() << std::endl;
    

    auto const& daq_handle = ev.getValidHandle<vector<artdaq::Fragment>>(daqTag);

    for(auto const& rawfrag : *daq_handle){

      bernfebdaq::BernZMQFragment frag(rawfrag);
      // Now do all the stuff you want to do with the fragments ....
      //std::cout << frag << std::endl;

    }
    
    max_events--;
    
  } //end loop over events!


}
