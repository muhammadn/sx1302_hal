### get external defined data

include ../target.cfg

### Application-specific constants

APP_NAME := meshtasticd
RUNTIME_STUB_APP := meshtastic_runtime_stub

### Environment constants

LGW_PATH ?= ../libloragw
LIB_PATH ?= ../libtools
ARCH ?=
CROSS_COMPILE ?=
MESHTASTIC_PATH ?= .
MESHTASTIC_FIRMWARE_PATH ?= $(MESHTASTIC_PATH)/firmware
ENABLE_MESHTASTIC_RUNTIME_ADAPTER ?= 0
ENABLE_MESHTASTIC_RUNTIME_IPC ?= 1
MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS ?= 1
MESHTASTIC_RUNTIME_INCLUDES ?=

OBJDIR = obj

### External constant definitions

include $(LGW_PATH)/library.cfg
RELEASE_VERSION := `cat ../VERSION`

### Constant symbols

CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++

CFLAGS := -O2 -Wall -Wextra -std=c99 \
	-I. \
	-I$(MESHTASTIC_PATH)/bridge \
	-I../clusterduck/inc \
	-I../clusterduck/inc/paho.mqtt.c \
	-I../libtools/inc \
	-DMESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS=$(MESHTASTIC_ALLOW_WEAK_RUNTIME_STUBS)

CXXFLAGS := -O2 -Wall -Wextra -std=gnu++17 \
	-I$(MESHTASTIC_PATH)/bridge \
	-I$(LGW_PATH)/inc \
	-I../libtools/inc

ADAPTER_OBJS :=
IPC_OBJS :=

ifeq ($(ENABLE_MESHTASTIC_RUNTIME_IPC),1)
IPC_OBJS += $(OBJDIR)/MeshtasticRuntimeIpc.o
endif

ifeq ($(ENABLE_MESHTASTIC_RUNTIME_ADAPTER),1)
ifeq ($(strip $(MESHTASTIC_RUNTIME_INCLUDES)),)
$(error ENABLE_MESHTASTIC_RUNTIME_ADAPTER=1 requires MESHTASTIC_RUNTIME_INCLUDES (Linux daemon/Portduino Meshtastic SDK include paths; this repo alone is not sufficient))
endif
ifeq ($(ENABLE_MESHTASTIC_RUNTIME_IPC),1)
$(error ENABLE_MESHTASTIC_RUNTIME_ADAPTER=1 and ENABLE_MESHTASTIC_RUNTIME_IPC=1 cannot be enabled together)
endif
ADAPTER_OBJS += $(OBJDIR)/MeshtasticRuntimeAdapter.o
endif

VFLAG := -D VERSION_STRING="\"$(RELEASE_VERSION)\""

LIBS := -lloragw -ltinymt32 -lparson -lbase64 -lrt -lpthread -lm -lpaho-mqtt3c

.PHONY: all clean install runtime-stub

all: $(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)
	rm -f $(RUNTIME_STUB_APP)

install:
	@echo "No meshtastic install step defined; built binary is ./$(APP_NAME)"

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/jitqueue.o: ../clusterduck/src/jitqueue.c ../clusterduck/inc/jitqueue.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(LGW_PATH)/inc $< -o $@

$(OBJDIR)/MeshtasticBridge.o: $(MESHTASTIC_PATH)/bridge/MeshtasticBridge.cpp $(MESHTASTIC_PATH)/bridge/MeshtasticBridge.h | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJDIR)/MeshtasticSX126xShim.o: $(MESHTASTIC_PATH)/bridge/MeshtasticSX126xShim.c $(MESHTASTIC_PATH)/bridge/MeshtasticSX126xShim.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/MeshtasticRuntimeIpc.o: $(MESHTASTIC_PATH)/bridge/MeshtasticRuntimeIpc.c $(MESHTASTIC_PATH)/bridge/MeshtasticBridge.h $(MESHTASTIC_PATH)/bridge/MeshtasticSX126xShim.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/MeshtasticRuntimeStub.o: $(MESHTASTIC_PATH)/bridge/MeshtasticRuntimeStub.c | $(OBJDIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/MeshtasticRuntimeAdapter.o: $(MESHTASTIC_PATH)/bridge/MeshtasticRuntimeAdapter.cpp | $(OBJDIR)
	$(CXX) -c $(CXXFLAGS) -I$(MESHTASTIC_FIRMWARE_PATH)/src $(MESHTASTIC_RUNTIME_INCLUDES) $< -o $@

$(OBJDIR)/$(APP_NAME).o: $(APP_NAME).c | $(OBJDIR)
	$(CC) -c $(CFLAGS) $(VFLAG) -I$(LGW_PATH)/inc $< -o $@

$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(OBJDIR)/jitqueue.o $(OBJDIR)/MeshtasticBridge.o $(OBJDIR)/MeshtasticSX126xShim.o $(IPC_OBJS) $(ADAPTER_OBJS) $(LGW_PATH)/libloragw.a
	$(CXX) -L$(LGW_PATH) -L$(LIB_PATH) \
		$(OBJDIR)/$(APP_NAME).o $(OBJDIR)/jitqueue.o $(OBJDIR)/MeshtasticBridge.o $(OBJDIR)/MeshtasticSX126xShim.o $(IPC_OBJS) $(ADAPTER_OBJS) \
		-o $@ $(LIBS)

$(RUNTIME_STUB_APP): $(OBJDIR)/MeshtasticRuntimeStub.o
	$(CC) $(OBJDIR)/MeshtasticRuntimeStub.o -o $@

runtime-stub: $(RUNTIME_STUB_APP)
