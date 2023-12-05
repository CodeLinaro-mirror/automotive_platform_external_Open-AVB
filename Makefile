descend = \
	+mkdir -p $(OUTPUT)$(1) && \
	$(MAKE) $(COMMAND_O) subdir=$(if $(subdir),$(subdir)/$(1),$(1)) $(PRINT_DIR) -C $(1) $(2)

help:
	@echo 'Possible targets:'
	@echo ''
	@echo '  ntn               - neutrino library'
	@echo ''
	@echo '  daemons_all       - build all daemons (mrpd gptp maap)'
	@echo '  mrpd              - mrpd daemon'
	@echo '  gptp              - gptp daemon for linux'
	@echo '  maap              - maap daemon'
	@echo ''
	@echo '  avtp_pipeline     - AVTP pipeline'
	@echo '  avtp_pipeline_doc - AVTP pipeline doc'
	@echo ''
	@echo '  examples_all      - build all examples (simple_talker simple_listener mrp_client live_stream jackd-talker jackd-listener)'
	@echo '  simple_talker     - simple_talker application'
	@echo '  simple_listener   - simple_listener application'
	@echo '  mrp_client        - mrp_client application'
	@echo '  jackd-talker      - jackd-talker application'
	@echo '  jackd-listener    - jackd-listener application'
	@echo '  live_stream       - live_stream application'
	@echo ''
	@echo 'Cleaning targets:'
	@echo ''
	@echo '  all of the above with the "_clean" string appended cleans'
	@echo '    the respective build directory.'
	@echo '  clean: a summary clean target to clean _all_ folders'
	@echo ''

ntn: FORCE
	$(call descend,lib/neutrino)
ntn_clean:
	$(call descend,lib/neutrino/,clean)

emac: FORCE
	$(call descend,lib/emac)

emac_clean:
	$(call descend,lib/emac/,clean)

mrpd:
	$(call descend,daemons/$@)

mrpd_clean:
	$(call descend,daemons/mrpd/,clean)

gptp: FORCE
	$(call descend,daemons/$@/linux/build/)

gptp_clean:
	$(call descend,daemons/gptp/linux/build/,clean)

gptp_install: FORCE
ifeq ($(ENABLE_GPTP),1)
	mkdir -p $(DESTDIR)$(bindir)
	install -m 0755 daemons/gptp/linux/build/obj/daemon_cl $(DESTDIR)$(bindir)
endif

maap:
	$(call descend,daemons/$@/linux/)

maap_clean:
	$(call descend,daemons/maap/linux/,clean)

daemons_all: mrpd maap gptp

daemons_all_clean: mrpd_clean gptp_clean maap_clean

simple_talker:
	$(call descend,examples/$@)

simple_talker_clean:
	$(call descend,examples/simple_talker/,clean)


simple_listener:
	$(call descend,examples/$@)

simple_listener_clean:
	$(call descend,examples/simple_listener/,clean)

mrp_client:
	$(call descend,examples/$@)

mrp_client_clean:
	$(call descend,examples/mrp_client/,clean)

jackd-talker:
	$(call descend,examples/$@)

jackd-talker_clean:
	$(call descend,examples/jackd-talker/,clean)

jackd-listener:
	$(call descend,examples/$@)

jackd-listener_clean:
	$(call descend,examples/jackd-listener/,clean)

live_stream:
	$(call descend,examples/$@)

live_stream_clean:
	$(call descend,examples/live_stream/,clean)

avtp_pipeline: ntn emac
	$(MAKE) -s -C lib/avtp_pipeline -f avtp_pipeline.mk

avtp_pipeline_clean:
	$(MAKE) -s -C lib/avtp_pipeline -f avtp_pipeline.mk clean

avtp_pipeline_doc: ntn
	$(MAKE) -s -C lib/avtp_pipeline -f avtp_pipeline.mk doc

libgptp:
	$(call descend,lib/libgptp)

libgptp_clean:
	$(call descend,lib/libgptp/,clean)

libgptp_install: FORCE
ifeq ($(ENABLE_LIBGPTP),1)
	mkdir -p $(DESTDIR)$(libdir)
	mkdir -p $(DESTDIR)$(includedir)
	install -m 0755 lib/libgptp/*.so $(DESTDIR)$(libdir)
	install -m 0644 lib/libgptp/gptp_helper.h ${DESTDIR}${includedir}
endif

libgptp_test:
	$(call descend,examples/$@)

libgptp_test_clean:
	$(call descend,examples/libgptp_test/,clean)

libgptp_test_install: FORCE
ifeq ($(ENABLE_LIBGPTP_TEST),1)
	mkdir -p $(DESTDIR)$(bindir)
	install -m 0755 examples/libgptp_test/libgptp_test $(DESTDIR)$(bindir)
endif

examples_all: simple_talker simple_listener mrp_client live_stream jackd-talker \
	jackd-listener

examples_all_clean: simple_talker_clean simple_listener_clean mrp_client_clean \
	jackd-talker_clean jackd-listener_clean live_stream_clean

all: daemons_all avtp_pipeline libgptp libgptp_test

install: gptp_install libgptp_install libgptp_test_install

clean: daemons_all_clean avtp_pipeline_clean libgptp_clean libgptp_test_clean

.PHONY: FORCE
