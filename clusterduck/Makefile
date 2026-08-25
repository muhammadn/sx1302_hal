 url=https://github.com/muhammadn/sx1302_hal/blob/4b42025d1751e04632c0b04160e0d29dbbb222a5/packet_forwarder/Makefile
### get external defined data

include ../target.cfg

### Application-specific constants

APP_NAME := clusterduckd

### Environment constants

LGW_PATH ?= ../libloragw
LIB_PATH ?= ../libtools
ARCH ?=
CROSS_COMPILE ?=
# Path to ClusterDuck source (contains duck_bridge.h / duck_bridge.cpp).
# Adjust if ClusterDuck repo is located elsewhere relative to clusterduckd.
CLUSTERDUCK_PATH ?= .

OBJDIR = obj
INCLUDES = $(wildcard inc/*.h)

### External constant definitions
# must get library build option to know if mpsse must be linked or not

include $(LGW_PATH)/library.cfg
RELEASE_VERSION := `cat ../VERSION`

### Constant symbols

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
CXX := $(CROSS_COMPILE)g++
# C compiler flags (add ClusterDuck include path so duck_bridge.h is found)
CFLAGS := -O2 -Wall -Wextra -std=c99 -Iinc -I. -I../libtools/inc -I$(CLUSTERDUCK_PATH)/src -I$(CLUSTERDUCK_PATH)/inc/paho.mqtt.c
# C++ compiler flags for duck_bridge.cpp
CXXFLAGS := -O2 -Wall -Wextra -std=gnu++17 -I$(CLUSTERDUCK_PATH)/src -I$(LGW_PATH)/inc -I../libtools/inc

VFLAG := -D VERSION_STRING="\"$(RELEASE_VERSION)\""

### Constants for Lora concentrator HAL library
# List the library sub-modules that are used by the application

LGW_INC =
ifneq ($(wildcard $(LGW_PATH)/inc/config.h),)
  # only for HAL version 1.3 and beyond
  LGW_INC += $(LGW_PATH)/inc/config.h
endif
LGW_INC += $(LGW_PATH)/inc/loragw_hal.h

### Linking options

LIBS := -lloragw -ltinymt32 -lparson -lbase64 -lrt -lpthread -lm -lpaho-mqtt3c -lprotobuf -latomic

### General build targets

all: $(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)

ifneq ($(strip $(TARGET_IP)),)
 ifneq ($(strip $(TARGET_DIR)),)
  ifneq ($(strip $(TARGET_USR)),)
install:
	@echo "---- Copying clusterduckd files to $(TARGET_IP):$(TARGET_DIR)"
	@ssh $(TARGET_USR)@$(TARGET_IP) "mkdir -p $(TARGET_DIR)"
	@scp clusterduckd $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
install_conf:
	@echo "---- Copying clusterduckd conf files to $(TARGET_IP):$(TARGET_DIR)"
	@ssh $(TARGET_USR)@$(TARGET_IP) "mkdir -p $(TARGET_DIR)"
	@scp global_conf.json.sx1250.* $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
	@scp global_conf.json.sx1255.* $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
	@scp global_conf.json.sx1257.* $(TARGET_USR)@$(TARGET_IP):$(TARGET_DIR)
  else
	@echo "ERROR: TARGET_USR is not configured in target.cfg"
  endif
 else
	@echo "ERROR: TARGET_DIR is not configured in target.cfg"
 endif
else
	@echo "ERROR: TARGET_IP is not configured in target.cfg"
endif

### Sub-modules compilation

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(LGW_PATH)/inc $< -o $@

### Rule to compile ClusterDuck unified bridge into an object file
# This brings the new unified bridge into the binary, replacing old duck_bridge and DuckForwarderBridge
$(OBJDIR)/ClusterDuckBridge.o: $(CLUSTERDUCK_PATH)/src/bridge/ClusterDuckBridge.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/CdpPacket.o: $(CLUSTERDUCK_PATH)/src/CdpPacket.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/bloomfilter.o: $(CLUSTERDUCK_PATH)/src/routing/bloomfilter.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/DuckRouter.o: $(CLUSTERDUCK_PATH)/src/routing/DuckRouter.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/DuckLoRa.o: $(CLUSTERDUCK_PATH)/src/radio/DuckLoRa.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/Init.o: $(CLUSTERDUCK_PATH)/src/Init.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/CRC32.o: $(CLUSTERDUCK_PATH)/src/utils/CRC32.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/DuckUtils.o: $(CLUSTERDUCK_PATH)/src/utils/DuckUtils.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/safe_base64.o: $(CLUSTERDUCK_PATH)/src/utils/safe_base64.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/duck_payloads.pb.o: $(CLUSTERDUCK_PATH)/src/payloads/duck_payloads.pb.cc | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@
$(OBJDIR)/DuckPayloads.o: $(CLUSTERDUCK_PATH)/src/payloads/DuckPayloads.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@

### Main program compilation and assembly

$(OBJDIR)/$(APP_NAME).o: src/$(APP_NAME).c $(LGW_INC) $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) $(VFLAG) -I$(LGW_PATH)/inc $< -o $@

# Link with g++ to ensure proper linking of C++ runtime
# Using unified ClusterDuckBridge.o instead of old duck_bridge.o and DuckForwarderBridge.o
$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(LGW_PATH)/libloragw.a $(OBJDIR)/jitqueue.o $(OBJDIR)/ClusterDuckBridge.o $(OBJDIR)/CdpPacket.o $(OBJDIR)/bloomfilter.o $(OBJDIR)/DuckRouter.o $(OBJDIR)/DuckLoRa.o $(OBJDIR)/Init.o $(OBJDIR)/CRC32.o $(OBJDIR)/DuckUtils.o $(OBJDIR)/safe_base64.o $(OBJDIR)/duck_payloads.pb.o $(OBJDIR)/DuckPayloads.o
	$(CXX) -static -L$(LGW_PATH) -L$(LIB_PATH) $< $(OBJDIR)/jitqueue.o $(OBJDIR)/ClusterDuckBridge.o $(OBJDIR)/CdpPacket.o $(OBJDIR)/bloomfilter.o $(OBJDIR)/DuckRouter.o $(OBJDIR)/DuckLoRa.o $(OBJDIR)/Init.o $(OBJDIR)/CRC32.o $(OBJDIR)/DuckUtils.o $(OBJDIR)/safe_base64.o $(OBJDIR)/duck_payloads.pb.o $(OBJDIR)/DuckPayloads.o $(MQTT_OBJS) -o $@ $(LIBS)

### EOF
