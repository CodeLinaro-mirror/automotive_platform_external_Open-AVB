/*************************************************************************************************************
Copyright (c) 2015, Coveloz Consulting Ltda
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS LISTED "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS LISTED BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Attributions: The inih library portion of the source code is licensed from
Brush Technology and Ben Hoyt - Copyright (c) 2009, Brush Technology and Copyright (c) 2009, Ben Hoyt.
Complete license and copyright information can be found at
https://github.com/benhoyt/inih/commit/74d2ca064fb293bc60a77b0bd068075b293cf175.
*************************************************************************************************************/

/**
 * @file
 * MODULE SUMMARY : Reads the .ini file and parses it into information
 * to be used on daemon_cl
 */

#include <string>

#include "ini.h"
#include <limits.h>
#include <common_port.hpp>

const uint32_t LINKSPEED_10G =		10000000;
const uint32_t LINKSPEED_2_5G =		2500000;
const uint32_t LINKSPEED_1G =		1000000;
const uint32_t LINKSPEED_100MB =	100000;
const uint32_t INVALID_LINKSPEED =	UINT_MAX;

/**
 * @brief Returns name given numeric link speed
 * @return NULL if speed/name isn't found
 */
const char *findNameBySpeed( uint32_t speed );

/**
 * @brief Provides the gptp interface for
 * the iniParser external module
 */
class GptpIniParser
{
    public:

        /**
         * @brief Container with the information to get from the .ini file
         */
        typedef struct
        {
            /*ptp data set*/
            uint8_t priority1;
            uint8_t priority2;
            uint8_t clockClass;

            /*log section*/
            uint8_t logmode;
            uint8_t logDiagnosticCounters;
            uint8_t logExceptions;
            char    *gptp_diagnostic_counter_file;
            char    *gptp_exception_file;

            /*port data set*/
            unsigned int announceReceiptTimeout;
            unsigned int syncReceiptTimeout;
            unsigned int syncReceiptThresh;		//!< Number of wrong sync messages that will trigger a switch to master
            int64_t neighborPropDelayThresh;
            unsigned int seqIdAsCapableThresh;
            uint16_t lostPdelayRespThresh;
			int8_t initialLogSyncInterval;
			int8_t initialLogPdelayReqInterval;
			int8_t operLogSyncInterval;
			int8_t operLogPdelayReqInterval;
			bool automotive_profile;
			bool asCapable;
			bool isGm;
            PortState port_state;

            /*ethernet adapter data set*/
	    std::string ifname;
		phy_delay_map_t phy_delay;
        } gptp_cfg_t;

        /*public methods*/
        GptpIniParser(std::string ini_path);
        ~GptpIniParser();

        /**
         * @brief  Reads the parser Error value
         * @param  void
         * @return Parser Error
         */
        int parserError(void);

        /**
         * @brief  Reads priority1 config value
         * @param  void
         * @return priority1
         */
        uint8_t getPriority1(void)
        {
            return _config.priority1;
        }
        /**
         * @brief  Reads priority2 config value
         * @param  void
         * @return priority2
         */
        uint8_t getPriority2(void)
        {
            return _config.priority2;
        }
        /**
         * @brief  Reads clockClass config value
         * @param  void
         * @return clockclass
         */
        uint8_t getclockClass(void)
        {
            return _config.clockClass;
        }

        /**
         * @brief  Reads the announceReceiptTimeout configuration value
         * @param  void
         * @return announceRecepitTimeout value from .ini file
         */
        unsigned int getAnnounceReceiptTimeout(void)
        {
            return _config.announceReceiptTimeout;
        }

        /**
         * @brief  Reads the syncRecepitTimeout configuration value
         * @param  void
         * @return syncRecepitTimeout value from the .ini file
         */
        unsigned int getSyncReceiptTimeout(void)
        {
            return _config.syncReceiptTimeout;
        }

        /**
         * @brief  Reads the PHY DELAY values from the configuration file
         * @param  void
         * @return PHY delay map structure
         */
        const phy_delay_map_t getPhyDelay(void)
        {
            return _config.phy_delay;
        }

        /**
         * @brief  Reads the neighbohr propagation delay threshold from the configuration file
         * @param  void
         * @return neighborPropDelayThresh value from the .ini file
         */
        int64_t getNeighborPropDelayThresh(void)
        {
            return _config.neighborPropDelayThresh;
        }

        /**
         * @brief  Reads the sync receipt threshold from the configuration file
         * @return syncRecepitThresh value from the .ini file
         */
        unsigned int getSyncReceiptThresh(void)
        {
            return _config.syncReceiptThresh;
        }
        /**
         * @brief  Reads the initial log sync interval from the configuration file
         * @return initialLogSyncInterval value from the .ini file
         */
        int8_t getInitialLogSyncInterval(void)
        {
            return _config.initialLogSyncInterval;
        }
        /**
         * @brief  Reads the initial log pdelay interval from the configuration file
         * @return initialLogPdelayReqInterval value from the .ini file
         */
        int8_t getInitialLogPdelayReqInterval(void)
        {
            return _config.initialLogPdelayReqInterval;
        }
        /**
         * @brief  Reads the operational log sync interval from the configuration file
         * @return operLogSyncInterval value from the .ini file
         */
        int8_t getOperLogSyncInterval(void)
        {
            return _config.operLogSyncInterval;
        }
        /**
         * @brief  Reads the operational log pdelay req interval from the configuration file
         * @return operlogpdelayreqInterval value from the .ini file
         */
        int8_t getOperLogPdelayReqInterval(void)
        {
            return _config.operLogPdelayReqInterval;
        }

		bool getIsGM(void)
		{
			return _config.isGm;
		}

		bool getAsCapable(void)
		{
			return _config.asCapable;
		}
		bool getAutomotiveProfile(void)
		{
			return _config.automotive_profile;
		}

		PortState getPortState(void)
		{
			return _config.port_state;
		}

		/* log section */

		/**
		* @brief  Reads the log mode from the configuration file
		* @return log mode value from the .ini file
		*/
		uint8_t getlogmode(void)
		{
			return _config.logmode;
		}

		/**
		 * @brief  Reads the logDiagnosticCounters from the configuration file
		* @return logDiagnosticCounters value from the .ini file
		*/
		uint8_t getlogDiagnosticCounters(void)
		{
			return _config.logDiagnosticCounters;
		}

		/**
		 * @brief  Reads the logExceptions from the configuration file
		* @return logExceptions value from the .ini file
		*/
		uint8_t getlogExceptions(void)
		{
			return _config.logExceptions;
		}

		/**
		 * @brief  Reads the DiagnosticCountersFileName from the configuration file
		* @return gptp_diagnostic_counter_file value from the .ini file
		*/
		char* getDiagnosticCountersFileName(void)
		{
			return _config.gptp_diagnostic_counter_file;
		}

		/**
		 * @brief  Reads the ExceptionsFileName from the configuration file
		* @return gptp_exception_file value from the .ini file
		*/
		char* getExceptionsFileName(void)
		{
			return _config.gptp_exception_file;
		}

	/**
	 * @brief Dump PHY delays to screen
	 */
	void print_phy_delay( void );

    private:
        int _error;
        gptp_cfg_t _config;

        static int iniCallBack(void *user, const char *section, const char *name, const char *value);
        static bool parseMatch(const char *s1, const char *s2);
};

