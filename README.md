# bernfebdaq-core

Contact: Wesley Ketchum, wketchum@fnal.gov

README last modified: 8 June 2017

This package is a combination of three basic entities: the core Overlay classes for the Bern FEB Fagments, to be used with the artdaq system; a art input source module, that can read in "raw" BernZMQ formatted data and combine them into a singular art event; and, a collection of analysis code/modules based on "gallery" to interpret the raw data.

Note, there are some known issues in the input source code, so please do not expect it to work. Those will be outlined below.

<h1>(1) Overlay classes</h1>
The overlay classes are defined in bernfebdaq-core/Overlays. The main one of use right now is the BernZMQFragment, which is the formatting for the data as is delivered by the ZeroMQ version of the FEB drivers (which is what is currently being stored to disk for MicroBooNE). Note that in the artdaq system, all data fragments are collected into artdaq::Fragment objects, which contain some header words, a user-defined metadata class (filled with user-defined information, of course), and a data payload. The data payload has no specific internal structure in artdaq, which leads to the use of user-defined "overlay" classes to interpret the data inside.

For the BernFEB DAQ system, fragments are the collection of raw CRT hits in a definite period of time from one FEB. Each fragment, conceptually, has a start and end time, a collection of CRT hits, and some additional metadata information (like, a feb ID, event counters, and etc.). Additionally, time calibration information, based on the observance of a GPS PPS pulse, is included in the metadata to allow for corrections of the raw CRT hit times. A utility function, GetCorrectedTime(uint32_t raw_time, BernZMQFragmentMetadata const& metadata_obj) is provided for ease of computing this corrected time for a given raw time in a fragment's period of validity. Fragments should, generally, not be longer than 1 second in time, and realistically not shorter than ~1 ms lest header/metadata information far outweigh the actual data volume.

In looking at BernZMQFragment.hh, you should find:
<ul>
<li>a BernZMQFragment class which takes in its constructer an artdaq::Fragment, and contains accessors to the underlying metdata and data-data; 
<li> a BernZMQFragmentMetadata class, which is partially described above; and, 
<li> a BernZMQEvent struct, which defines the raw structure of the data in the data payload portion of the artdaq fragment.
</ul>
The BernZMQFragment has methods for providing pointers to the metadata block and to each individual CRT Hit inside the data (sometimes called CRT Events, so don't be confused). Each fragment also as an output streamer, so one can print the entire contents of the data fragment easily.

<h1>(2) BernZMQBinaryInput Source</h1>
There is a dedicated input source for reading the raw CRT DAQ files. Note that the input source needs to serve the following functions:
<ul>
<li> it must read data from all CRT DAQ servers (there is typically one file per server);
<li> it must correctly identify the absolute global time of each CRT hit, and assign it to the proper artdaq::Fragment; and, 
<li> it must properly combine all fragments (all FEBs) of the same time period together and write them to the art event.
</ul>

While simple in concept, this is not simple in practice as assigning the proper time to every CRT hit is made difficult by a number of issues. Not all GPS PPS pulses are seen as data in the output stream. "Stuck events"---events from previous points in the run that do not get properly cleared from the data buffers---make difficult the determination of a rollover of the GPS clock. And, the latency for when a GPS PPS data event arrives at the server is not precisely known, but appears to be on the "long" side, close to 800 ms (with some potentially significant deviations). There are a number of workarounds and mitigating code to help deal with these issues, but in the end more careful study may be necessary to recover data, and/or we will take an efficiency hit. It is likely that data hits in one module that get matched as coincident in time with another module are true hits, so this should be done prior to using the raw data for physics analysis.

A sample fcl file for running the input source is provided (read_bernzmq.fcl). There are parameters for the InputSource itself, and for the utility class "BernZMQBinaryInputStreamReader". Parameters are described below:
<h3>BernZMQBinaryInput parameters</h3>
(These are at the top level in the source configuration.)
<ul>
<li> module_type: is "BernZMQBinaryInput", and you don't change that
<li> fileNames: the list of input file names to run over. Each file provided here should be a text file that in turn contain a list of CRT raw data files: one from each server, and corresponding to the same start/end times. One can specify the input file(s) using the normal '-s' or '-S' options in art.
<li> ModuleLabel/InstanceLabel: the modules/instance label to assign to the data. Instance labels are good if you may try to run with files separately, but they're not absolutely necessary. 'daq' is a good module label, or 'crtdaq'
<li> FragsPerEvent: this is the number of expected fragments per "event" (where each event is a period of time)---that is, the number of expected FEBs. It is important that this number be correct. The source module will loop over all files and all data from febs in those files, and store a map of fragments keyed by the fragment start time. Data will only be written to an art event once an element in that map has the expected number of fragments per event. When that condition is satisfied, ALL "events" before that event (that is, all fragment collections before the one that has been "completed") will be written into art events one by one, thus ensuring events are always increasing in time, and flushing out any events with missing FEBs. For MicroBooNE, there are 73 total FEBs including the top panel.
</ul>
<h3>BernZMQBinaryInputStreamReaderConfig</h3>
This is a dedicated parameter set underneath the top level. For each CRT server (that is, each raw data file), a BernZMQBinaryInputStreamReader is created. All StreamReaders have the same configuration (they just look at different files). Parameters are as follows.
<ul>
<li> NFEBBuffers (default=2): For each FEB, a buffer is created to store all the data corresponding to 1 second (as determined by seeing a PPS or seeing the T0 timestamp rollover). To aid in debugging and checking time consistency, one can keep older data around a little longer, basically with a double (or n-buffer) configuration.
<li> Verbosity (default=-1): If Verbosity=1, a number of cout statements are written to aid in debugging. With Verbosity=2 or higher, contents of all the FEB buffers are printed frequently. If <=0, no additional statments are printed out.
<li> MaxTSDiff (default=20): To check for stuck events, we compare time differences between events using both TS0 and TS1. These typically will not match after any significant period of time, but the level of matching is not simple. We say if the difference in the difference in time stamps is less than MaxTSDiff, they match.
<li> MaxStuckEvents (default 20): stuck events can appear many times in a row, and so they are searched for in a loop. This is the maximum number of events to go back and search for stuck events. If there are more stuck events than this threshold, the code will likely throw away the current event and not successfully remove the stuck events.
<li> PullTimeLatency (default 500 [ms] ): this is the minimum time in milliseconds that it is expected to take before the GPS PPS data event will be seen in the server. Every incoming data packet is tagged with an NTP time from the server, and the presence of the GPS PPS (or T0 rollover) is then matched to this NTP time, but with the PullTimeLatency subtracted. Typical times are 700-800 ms: 500 ms should work ok.
<li> FragsPerSecond (default 200): this is the configurable number of fragments to create per second, or rather is a way to establishthe total time per fragment. With 200, we have 5 ms fragments. This should not be less than 1, nor greater than 1000.
</ul>

In another document or at another time I can detail the source module, but basically we search through each raw data file and then search through each "pulled" data packet in that stream, sorting events by time and FEB as we see them and identifying when a GPS PPS (or rollover on the T0 clock) has occurred. When that happens, we go through the data in that FEBs buffer and create FragsPerSecond fragments from the data, and store it in a fragment map, which is periodically checked for when all fragments expected for an event have been recorded. We keep track of the time of data from each file, and rotate between files to make sure they stay relatively synchronous in time (thus avoiding large memory buildups).

<h3>Current issues</h3>
Right now, there is currently a problem with reading one of the input files and getting an error flag set. The code generally interprets this to mean the end of the file has been reached, and thus data stops being read from that file. Further protections need to be build in to protect against this issue.

<h1>(3) Analysis scripts/macros/code</h1>
In the ana/ folder, there are some sample analysis scripts that show how to get the data from event and interpret the artdaq::Fragments properly using the BernZMQFragment Overlay class. The most detailed one is the bernfebdaq_ReadEvents macro in the macros folder. There is also a jupyter python notebook that gets the data from a python interface (note this does not work on MAC OS X).

To setup:
<ol>
<li> Setup a products area (or multiple products areas) that contain the gallery and bernfebdaq-core ups products. This is naturally done if you have built bernfebdaq-core using mrb (see below) and are on a system that has the normal suite of larsoft libraries installed.
<li> Run `source setup` from the ana/ directory. In the future, you may need to change the version numbers of gallery and bernfebdaq-core used.
<li> If using the jupyter python notebook, you need to make sure that you have jupyter installed. To do that with recent versions of larsoft, you should (1) define a python install area for personal products `export PYTHONUSERBASE=/uboone/app/users/<uid>/python_install`; (2) create that area if it's not already there via `mkdir -p $PYTHONUSERBASE`; (3) install jupyer into your local area by doing `pip install --user jupyter`; (4) append to your path this new area with `export PATH=$PYTHONUSERBASE/bin:$PATH`. And then you can start jupyter via `jupyter notebook`. See web resources for port forwarding and etc.
</ol>

For the jupyer notebook, one can then open the notebook and run it (change the art InputTag and file names appropriately of course). For the ROOT macro, to run you should do:
<ol>
<li> `root`
<li> `root[0]: .L $BERNFEBDAQ_CORE_LIB/libbernfebdaq-core_Overlays.*` (extension is .so for linux, .dylib for MAC OS X)
<li> `root[1]: .L bernfebdaq_ReadEvents.C++`
<li> `root[2]: bernfebdaq_ReadEvents("input_art_file_name.root","output_file_name.root",max_events_to_read)`
</ol>
This will create a per fragment and per CRT hit tree in the output file.

Currently there are no standalone C++ modules created, but there likely will be in the future (and documentation will follow).

<h1>(4) Building the code</h1>
To build the Overlay and source module code, one should be able to use the normal mrb environment.
<ol>
<li> Do your usual setup of larsoft products.
<li> `setup uboonecode v06_36_00 -qe14:nu:prof` (or one could setup larsoft)
<li> `mkdir -p bernfebdaq_dev_area`
<li> `cd bernfebdaq_dev_area`
<li> `export MRB_PROJECT=uboonecode` (or larsoft ... doesn't matter)
<li> `mrb newDev`
<li> `source localProducts*/setup`
<li> `cd srcs/`
<li> `mrb g https://github.com/wesketchum/bernfebdaq-core.git` (you should be on master branch)
<li> `cd $MRB_BUILD_DIR`
<li> `mrbsetenv`
<li> `mrb i -j8`
<li> `mrbslp` (assuming it all built properly)
</ol>
