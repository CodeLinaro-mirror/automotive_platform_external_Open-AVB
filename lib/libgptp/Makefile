.PHONY : clean

CPPFLAGS+= -fpic -g -I../../daemons/gptp/linux/src -I../../daemons/gptp/common
ifeq ($(GPTP_AUTO_START),1)
	CPPFLAGS += -DGPTP_AUTO_START
endif
ifeq ($(LE_GVM),1)
	CPPFLAGS += -DLE_GVM
endif
ifeq ($(AVB_FEATURE_GVM_MODE),1)
	CPPFLAGS += -DAVB_FEATURE_GVM_MODE
endif
ifeq ($(SYSTEMD_SUPPORT_INCLUDED),1)
	CPPFLAGS += -DSYSTEMD
endif
ifeq ($(RGPTP_CLNT_ENABLED),1)
	CPPFLAGS += -DRGPTP_CLNT_ENABLED
endif
LDFLAGS+= -shared -lrt -lpthread

ifeq ($(AVB_FEATURE_GVM_MODE),1)
	LDFLAGS+= -luhab
endif
ifeq ($(GPTP_VFIO),1)
	CPPFLAGS += -DGPTP_VFIO
	LDFLAGS += -lvfioptp
endif

SOURCES= gptp_helper.cpp
OBJECTS= gptp_helper.o
TARGET= libgptp.so 

all: $(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $(OBJECTS) -o $@ $(LDFLAGS)
