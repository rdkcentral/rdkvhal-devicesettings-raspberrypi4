##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2017 RDK Management
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

RM          := rm -rf
CXXFLAGS    = -std=c++1y  -g -fPIC -D_REENTRANT -Wall -DALSA_AUDIO_MASTER_CONTROL_ENABLE -Wall -Werror
LIBNAME     := ds-hal
LIBNAMEFULL := lib$(LIBNAME).so
OBJS        := $(patsubst %.c,%.o,$(wildcard *.c))
DSHAL_API_MAJOR_VERSION := '0'
DSHAL_API_MINOR_VERSION := '0'
VERSION     := $(DSHAL_API_MAJOR_VERSION).$(DSHAL_API_MINOR_VERSION)
LIBSOM = $(LIBNAMEFULL).$(DSHAL_API_MAJOR_VERSION)
LIBSOV = $(LIBNAMEFULL).$(VERSION)


$(LIBNAMEFULL): $(LIBSOV)
	ln -sf $(LIBSOV) $(LIBNAMEFULL)
	ln -sf $(LIBSOV) $(LIBSOM)

$(LIBSOV): $(OBJS)
	@echo "Building $(LIBSOV) ...."
	$(CXX) $(OBJS) -shared -Wl,-soname,$(LIBSOM) -o $(LIBSOV) -lvchostif -lvchiq_arm -lvcos -lasound

%.o: %.c
	@echo "Building $@ ...."
	$(CXX) -c $<  $(CXXFLAGS)  -DALSA_AUDIO_MASTER_CONTROL_ENABLE -I=/usr/include/interface/vmcs_host/linux $(CFLAGS) -o $@

install: $(LIBSOV)
	@echo "Installing files in $(DESTDIR) ..."
	install -d $(DESTDIR)
	install -m 0755 $< $(DESTDIR)
.PHONY: clean
clean:
	$(RM) *.so*
	$(RM) *.o
