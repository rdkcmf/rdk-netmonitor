/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
#include "netlinkifc.h"
#include <fstream>
#include <map>
#include <unistd.h>

#ifdef INCLUDE_BREAKPAD
#include "breakpad_wrapper.h"
#endif

#ifdef ENABLE_RDKLOGGER
#include "rdk_debug.h"
#endif

map<string,NlType> type_map =
{
   {"address",NlType::address},
   {"link", NlType::link},
   {"route",NlType::route},
   {"wifi",NlType::wifi},
   {"dfltroute",NlType::dfltroute}
};

int main ()
{
#ifdef ENABLE_RDKLOGGER
    rdk_logger_init("/etc/debug.ini");
#endif
    NetLinkIfc* netifc = NetLinkIfc::get_instance();

    string nltype, scriptname;
    ifstream cfgFile("/etc/nlmon.cfg");

#ifdef INCLUDE_BREAKPAD
breakpad_ExceptionHandler();
#endif

    while (cfgFile>>nltype>>scriptname)
    {
       ScriptSubscriber* script = new ScriptSubscriber(type_map[nltype],scriptname);
       netifc->addSubscriber(script);
    }

    cfgFile.close();

    ofstream pidfile("/run/nlmon.pid",ios::out);
    if (pidfile.is_open())
    {
        pidfile<<getpid()<<"\n";
        pidfile.close();
    }

    netifc->initialize();
    netifc->run();
}
