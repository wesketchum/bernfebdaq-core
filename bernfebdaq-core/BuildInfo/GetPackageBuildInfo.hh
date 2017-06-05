#ifndef artdaq_core_demo_BuildInfo_GetPackageBuildInfo_hh
#define artdaq_core_demo_BuildInfo_GetPackageBuildInfo_hh

#include "artdaq-core/Data/PackageBuildInfo.hh"

#include <string>

namespace coredemo {

  struct GetPackageBuildInfo {

    static artdaq::PackageBuildInfo getPackageBuildInfo();
  };

}

#endif /* artdaq_core_demo_BuildInfo_GetPackageBuildInfo_hh */
