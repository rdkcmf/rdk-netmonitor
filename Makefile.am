##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2018 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

AM_CXXFLAGS = -Wall -std=c++1y $(NL_CFLAGS) $(NLCLI_CFLAGS) $(DEBUG_CXXFLAGS)
ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS =  $(BREAKPAD_CFLAGS)
AM_CPPFLAGS =  $(BREAKPAD_CFLAGS)

bin_PROGRAMS = nlmon

lib_LTLIBRARIES = libnlmonitor.la

libnlmonitor_la_SOURCES = netlinkifc.cpp
libnlmonitor_la_LDFLAGS = $(NLCLI_LIBS) $(NL_LIBS) -lpthread -lbreakpadwrapper


nlmon_SOURCES = main.cpp
nlmon_LDADD = libnlmonitor.la

nlmon_la_includedir = ${includedir}
nlmon_la_include_HEADERS = netlinkifc.h  subscribers.h
