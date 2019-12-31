.PHONY : clean

CPPFLAGS= -fpic -g -I../../daemons/gptp/linux/src -I../../daemons/gptp/common
ifeq ($(GPTP_AUTO_START),1)
	CPPFLAGS += -DGPTP_AUTO_START
endif
ifeq ($(SYSTEMD_SUPPORT_INCLUDED),1)
	CPPFLAGS += -DSYSTEMD
endif
LDFLAGS= -shared -lrt

SOURCES= gptp_helper.cpp
OBJECTS= gptp_helper.o
TARGET= libgptp.so 

all: $(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(OBJECTS) -o $@ $(LDFLAGS)
