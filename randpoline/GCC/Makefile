#
# Intel provides this code “as-is” and disclaims all express and implied warranties, including without
# limitation, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement,
# as well as any warranty arising from course of performance, course of dealing, or usage in trade. No license
# (express or implied, by estoppel or otherwise) to any intellectual property rights is granted by Intel providing
# this code.
# This code is preliminary, may contain errors and is subject to change without notice.
# Intel technologies' features and benefits depend on system configuration and may require enabled hardware,
# software or service activation. Performance varies depending on system configuration.  Any differences in your
# system hardware, software or configuration may affect your actual performance.  No product or component can be
# absolutely secure.
# Intel and the Intel logo are trademarks of Intel Corporation in the United States and other countries.
# *Other names and brands may be claimed as the property of others.
# © Intel Corporation
#

CXX		= /usr/bin/g++
GCCPLUGINS_DIR	= $(shell $(CXX) -print-file-name=plugin)
CXXFLAGS	= -I$(GCCPLUGINS_DIR)/include -shared -fPIC -O2 -fno-rtti -Wall -std=c++11 -fpermissive


SRC		= postvartrackpass.cpp
SHARED_OBJ	= randpoline.so

.PHONY: all clean

all: $(SHARED_OBJ) 

$(SHARED_OBJ): $(SRC)  
	$(CXX) $(CXXFLAGS) $(SRC) -o $@



# clean
clean:
	rm -rf $(SHARED_OBJ) 
