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
#ifndef _SUBSCRIBERS_H_
#define _SUBSCRIBERS_H_

#include <string>
#include <iostream>
#include <algorithm>

#ifdef ENABLE_RDKLOGGER
#include "rdk_debug.h"
#endif

enum class NlType { address, link, route, wifi, dfltroute, unknown };

using namespace std;
class Subscriber
{
protected:
   NlType m_type;
   
public:
   virtual void invoke(std::string args) =0;
   Subscriber (NlType type):m_type(type) {}
   virtual ~Subscriber() {}
   bool isSameType(NlType type) {return m_type == type;}
};

class ScriptSubscriber: public Subscriber
{
private:
   std::string script;
public:
  ScriptSubscriber(NlType type,std::string scrname):Subscriber(type),script(scrname){}
  ~ScriptSubscriber() {}
  void invoke(std::string args)
  {
      std::string cmd = script + " " +args;
#ifdef ENABLE_RDKLOGGER
      RDK_LOG( RDK_LOG_INFO,"LOG.RDK.NLMON","%s(%d):COMMAND = %s \n", __FILE__, __LINE__ ,cmd.c_str());
#else
      std::cout<<"COMMAND = "<<cmd<<std::endl;
#endif
      cmd.erase(std::remove_if(cmd.begin(),cmd.end(),[](char c) {
          return !(isalnum(c) || (c == '/') || (c == ' ') || (c == ':') || (c == '-') || (c == '.') || (c == '@') || (c == '_') || (c == '[') || (c == ']'));}),cmd.end());

      cmd += " &";
#ifdef ENABLE_RDKLOGGER
      RDK_LOG( RDK_LOG_INFO,"LOG.RDK.NLMON","%s(%d):COMMAND After Sanitizing= %s \n", __FILE__, __LINE__ ,cmd.c_str());
#else
      std::cout<<"COMMAND After Sanitizing  = "<<cmd<<std::endl;
#endif
      system(cmd.c_str());
  }
};

class FunctionSubscriber: public Subscriber
{
private:
   typedef void (*function_ptr)(std::string str);
   function_ptr m_funcPtr;
public:
  FunctionSubscriber(NlType type,function_ptr fPtr):Subscriber(type),m_funcPtr(fPtr){}
  ~FunctionSubscriber() {}
  void invoke(std::string args)
  {
#ifdef ENABLE_RDKLOGGER
      RDK_LOG( RDK_LOG_INFO,"LOG.RDK.NLMON","%s(%d):ARGS = %s \n", __FILE__, __LINE__ ,args.c_str());
#else
      std::cout<<"ARGS  = "<<args<<std::endl;
#endif
      args.erase(std::remove_if(args.begin(),args.end(),[](char c) {
          return !(isalnum(c) || (c == '/') || (c == ' ') || (c == ':') || (c == '-') || (c == '.') || (c == '@') || (c == '_') || (c == '[') || (c == ']'));}),args.end());
      m_funcPtr(args);
  }
};
#endif //_SUBSCRIBERS_H_
