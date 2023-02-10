.PHONY : clean

BUILD_DIR=.
OPT=-g
CFLAGS=$(OPT) -Wall -Wextra -Wno-parentheses
INCFLAGS=-I../../lib/libgptp
LDLIBS= -lrt -lm -lpthread -lgptp
LDFLAGS=-L../../lib/libgptp

ifeq ($(GPTP_AUTO_START),1)
	CFLAGS += -DGPTP_AUTO_START
	CPPFLAGS += -DGPTP_AUTO_START
endif

ifeq ($(AVB_FEATURE_GVM_MODE),1)
	CFLAGS += -DAVB_FEATURE_GVM_MODE
	CPPFLAGS += -DAVB_FEATURE_GVM_MODE
endif

ifeq ($(AVB_FEATURE_GVM_MODE),1)
	LDLIBS+= -luhab
endif

ifeq ($(RGPTP_CLNT_ENABLED),1)
	CFLAGS += -DRGPTP_CLNT_ENABLED
	CPPFLAGS += -DRGPTP_CLNT_ENABLED
endif

all: libgptp_test
libgptp_test: libgptp_test.cpp
	$(CC) -o $(BUILD_DIR)/libgptp_test $(INCFLAGS) libgptp_test.cpp $(CFLAGS) $(LDLIBS) $(LDFLAGS)

clean:
	$(RM) libgptp_test
	$(RM) `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core -o -name "*.orig"`
