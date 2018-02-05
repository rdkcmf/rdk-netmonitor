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

map<string,NlType> type_map =
{
   {"address",NlType::address},
   {"link", NlType::link},
   {"route",NlType::route},
   {"wifi",NlType::wifi}
};

int main ()
{
    NetLinkIfc* netifc = NetLinkIfc::get_instance();

    string nltype, scriptname;
    ifstream cfgFile("/etc/nlmon.cfg");

    while (cfgFile>>nltype>>scriptname)
    {
       ScriptSubscriber* script = new ScriptSubscriber(type_map[nltype],scriptname);
       netifc->addSubscriber(script);
    }

    cfgFile.close();

    netifc->initialize();
    netifc->run();
}
