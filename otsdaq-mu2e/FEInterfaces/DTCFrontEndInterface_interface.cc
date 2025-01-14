#include "otsdaq-mu2e/FEInterfaces/DTCFrontEndInterface.h"
#include "otsdaq/FECore/MakeInterface.h"
#include "otsdaq/Macros/BinaryStringMacros.h"
#include "otsdaq/Macros/InterfacePluginMacros.h"


#include <fstream>


using namespace ots;

#undef __MF_SUBJECT__
#define __MF_SUBJECT__ "DTCFrontEndInterface"

// // some global variables, probably a bad idea. But temporary
// std::string RunDataFN = "";
// std::fstream runDataFile_;
// int FEWriteFile = 0;
// bool artdaqMode_ = true;

//=========================================================================================
DTCFrontEndInterface::DTCFrontEndInterface(
    const std::string&       interfaceUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       interfaceConfigurationPath)
    : CFOandDTCCoreVInterface(
          interfaceUID, theXDAQContextConfigTree, interfaceConfigurationPath)
    , thisDTC_(0)
    , EmulatedCFO_(0)
{
	__FE_COUT__ << "instantiate DTC... " << getInterfaceUID() << " "
	            << theXDAQContextConfigTree << " " << interfaceConfigurationPath << __E__;

	if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV)
	{
		__FE_COUT_INFO__ << "Hardware Dev Mode identified, so forcing CFO emulation mode for DTC" << __E__;
		emulate_cfo_ = true;
	}
	else
		emulate_cfo_ = getSelfNode().getNode("EmulateCFO").getValue<bool>();
	__FE_COUTV__(emulate_cfo_);

	if(emulatorMode_)
	{
		__FE_COUT__ << "Emulator DTC mode starting up..." << __E__;
		createROCs();
		registerFEMacros();
		return;
	}
	// else not emulator mode


	unsigned dtc_class_roc_mask = 0;
	// create roc mask for DTC
	{
		std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
		    Configurable::getSelfNode().getNode("LinkToROCGroupTable").getChildren();

		__FE_COUTV__(rocChildren.size());
		roc_mask_ = 0;

		for(auto& roc : rocChildren)
		{
			__FE_COUT__ << "roc uid " << roc.first << __E__;
			bool enabled = roc.second.getNode("Status").getValue<bool>();
			__FE_COUT__ << "roc enable " << enabled << __E__;

			if(enabled)
			{
				int linkID = roc.second.getNode("linkID").getValue<int>();
				roc_mask_ |= (0x1 << linkID);
				dtc_class_roc_mask |=
				    (0x1 << (linkID * 4));  // the DTC class instantiation expects each
				                            // ROC has its own hex nibble
			}
		}

		__FE_COUT__ << "DTC roc_mask_ = 0x" << std::hex << roc_mask_ << std::dec << __E__;
		__FE_COUT__ << "roc_mask to instantiate DTC class = 0x" << std::hex
		            << dtc_class_roc_mask << std::dec << __E__;

	}  // end create roc mask

	// instantiate DTC with the appropriate ROCs enabled
	std::string         expectedDesignVersion = "";
	DTCLib::DTC_SimMode mode =
	    emulate_cfo_ ? DTCLib::DTC_SimMode_NoCFO : DTCLib::DTC_SimMode_Disabled;
		    

	__FE_COUT__ << "DTC arguments..." << std::endl;
	__FE_COUTV__(mode);
	__FE_COUTV__(deviceIndex_);
	__FE_COUTV__(dtc_class_roc_mask);
	__FE_COUTV__(expectedDesignVersion);
	__FE_COUTV__(skipInit_);
	__FE_COUT__ << "END DTC arguments..." << std::endl;

	thisDTC_ = new DTCLib::DTC(
	    mode, deviceIndex_, dtc_class_roc_mask, expectedDesignVersion, 
		true /* skipInit */, //always skip init and lots ots configure setup
		"" /* simMemoryFile */,
		getInterfaceUID()); 
	
	std::string designVersion = thisDTC_->ReadDesignDate();
	__FE_COUTV__(designVersion);

	createROCs();
	registerFEMacros();

	// DTC-specific info
	dtc_location_in_chain_ =
	    getSelfNode().getNode("LocationInChain").getValue<unsigned int>();
	

	__FE_COUT_INFO__ << "DTC instantiated with name: " << getInterfaceUID()
	                 << " dtc_location_in_chain_ = " << dtc_location_in_chain_
	                 << " talking to /dev/mu2e" << deviceIndex_ << __E__;
}  // end constructor()

//==========================================================================================
DTCFrontEndInterface::~DTCFrontEndInterface(void)
{
	{
		// uint32_t lossOfLockReadData = registerRead(0x93c8);  // read loss-of-lock counter
		__FE_COUTV__(thisDTC_->FormatRXCDRUnlockCountCFOLink());
	}

	// destroy ROCs before DTC destruction
	rocs_.clear();


	if(thisDTC_)
		delete thisDTC_;

	__FE_COUT__ << "Destructed." << __E__;
}  // end destructor()

//==============================================================================
void DTCFrontEndInterface::registerFEMacros(void)
{
	__FE_COUT__ << "Registering DTC FE Macros..." << __E__;

	mapOfFEMacroFunctions_.clear();

	// clang-format off

	registerFEMacroFunction(
		"Get Firmware Version",  // feMacroName
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::GetFirmwareVersion),  // feMacroFunction
					std::vector<std::string>{},
					std::vector<std::string>{"Firmware Version Date"},  // namesOfOutputArgs
					1);//"allUsers:0 | TDAQ:255");
					
	// registerFEMacroFunction(
	// 	"Flash_LEDs",  // feMacroName
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::FlashLEDs),  // feMacroFunction
	// 				std::vector<std::string>{},
	// 				std::vector<std::string>{},  // namesOfOutputArgs
	// 				1);                          // requiredUserPermissions
    
	registerFEMacroFunction(
		"Get Status",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::GetStatus),            // feMacroFunction
					std::vector<std::string>{},  // namesOfInputArgs
					std::vector<std::string>{"Status"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"Get Simple Status",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::GetSimpleStatus),            // feMacroFunction
					std::vector<std::string>{},  // namesOfInputArgs
					std::vector<std::string>{"Status"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"ROC_Write",  // feMacroName
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::WriteROC),  // feMacroFunction
					std::vector<std::string>{"rocLinkIndex", "address", "writeData"},
					std::vector<std::string>{},  // namesOfOutput
					1);                          // requiredUserPermissions

	registerFEMacroFunction(
		"ROC_Read",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ReadROC),                  // feMacroFunction
					std::vector<std::string>{"rocLinkIndex", "address"},  // namesOfInputArgs
					std::vector<std::string>{"readData"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"ROC_BlockRead",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ROCBlockRead),
					std::vector<std::string>{"rocLinkIndex", "numberOfWords", "address", "incrementAddress"},
					std::vector<std::string>{"readData"},
					1);  // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"ROC_Write_ExtRegister",  // feMacroName
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::WriteExternalROCRegister),  // feMacroFunction
	// 				std::vector<std::string>{"rocLinkIndex", "block", "address", "writeData"},
	// 				std::vector<std::string>{},  // namesOfOutputArgs
	// 				1);                          // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"ROC_Read_ExtRegister",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::ReadExternalROCRegister),
	// 				std::vector<std::string>{"rocLinkIndex", "block", "address"},
	// 				std::vector<std::string>{"readData"},
	// 				1);  // requiredUserPermissions			

							

	


	registerFEMacroFunction(
		"Buffer Test",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::BufferTest),                  // feMacroFunction
					std::vector<std::string>{"numberOfEvents", "doNotMatch (bool)", "eventDuration", "doNotReadBack (bool)", "saveBinaryDataToFile (bool)"}, 
					std::vector<std::string>{"response"},
					1);  // requiredUserPermissions
					
	registerFEMacroFunction(
		"DTC_Write",  // feMacroName
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::WriteDTC),  // feMacroFunction
					std::vector<std::string>{"address", "writeData"},
					std::vector<std::string>{},  // namesOfOutput
					1);                          // requiredUserPermissions

	registerFEMacroFunction(
		"DTC_Read",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ReadDTC),                  // feMacroFunction
					std::vector<std::string>{"address"},  // namesOfInputArgs
					std::vector<std::string>{"readData"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"DTC_Reset",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::DTCReset),
					std::vector<std::string>{},
					std::vector<std::string>{},
					1);  // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"DTC_HighRate_DCS_Check",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::DTCHighRateDCSCheck),
	// 				std::vector<std::string>{"rocLinkIndex","loops","baseAddress",
	// 					"correctRegisterValue0","correctRegisterValue1"},
	// 				std::vector<std::string>{},
	// 				1);  // requiredUserPermissions
					
	// registerFEMacroFunction(
	// 	"DTC_HighRate_DCS_Block_Check",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::DTCHighRateBlockCheck),
	// 				std::vector<std::string>{"rocLinkIndex","loops","baseAddress",
	// 					"correctRegisterValue0","correctRegisterValue1"},
	// 				std::vector<std::string>{},
	// 				1);  // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"DTC_SendHeartbeatAndDataRequest",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::DTCSendHeartbeatAndDataRequest),
	// 				std::vector<std::string>{"numberOfRequests","timestampStart","useSWCFOEmulator","rocMask"},
	// 				std::vector<std::string>{"readData"},
	// 				1);  // requiredUserPermissions					
					
			
	registerFEMacroFunction(
		"Read Loss-of-Lock Counter",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ReadLossOfLockCounter),
					std::vector<std::string>{},
					std::vector<std::string>{						
						"Upstream Rx Lock Loss Count"},
					1);  // requiredUserPermissions
	

	registerFEMacroFunction(
		"Reset Loss-of-Lock Counter",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ResetLossOfLockCounter),
					std::vector<std::string>{},
					std::vector<std::string>{						
						"Upstream Rx Lock Loss Count"},
					1);  // requiredUserPermissions
	
	
	registerFEMacroFunction(
		"Get DTC Counters",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::DTCCounters),
					std::vector<std::string>{},
					std::vector<std::string>{						
						"Link Counters",
						"Performance Counters",
						"Packet Counters"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"Get Upstream Rx Control Link Status",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::GetUpstreamControlLinkStatus),
					std::vector<std::string>{},
					std::vector<std::string>{						
						"Upstream Rx Lock Loss Count",
						"Upstream Rx CDR Lock Status",
						"Upstream Rx PLL Lock Status",
						// "Jitter Attenuator Reset",
						"Jitter Attenuator Status",	//"Jitter Attenuator Input Select",
						// "Jitter Attenuator Loss-of-Signal",
						"Reset Done"},
					1 /* requiredUserPermissions */,
					"*" /* allowedCallingFEs */,
					"Get the status of the upstream control link, which is the forwarded synchronization and control sourced from the CFO through DTC daisy-chains." /* feMacroTooltip */
	);

	registerFEMacroFunction(
		"Select Jitter Attenuator Source",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::SelectJitterAttenuatorSource),
				        std::vector<std::string>{"Source (0 is Control Link Rx, 1 is RJ45, 2 is FPGA FMC)", "DoNotSet"},
						std::vector<std::string>{"Register Write Results"},
					1);  // requiredUserPermissions


	
	registerFEMacroFunction(
		"Set Emulated ROC Event Fragment Size",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::SetEmulatedROCEventFragmentSize),
				        std::vector<std::string>{"ROC Fragment Size (11-bits)"},
						std::vector<std::string>{"Size Written"},
					1);  // requiredUserPermissions
	

	registerFEMacroFunction(
		"Configure DTC for HWDevmode",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::configureHardwareDevMode),
				        std::vector<std::string>{},
						std::vector<std::string>{"Setting the CFO emulated, DCS enabled and the retransmission off"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"Read Rx Diag FIFO",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::readRxDiagFIFO),
				        std::vector<std::string>{"LinkIndex"},
						std::vector<std::string>{"Diagnostic RX FIFO"},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"Read Tx Diag FIFO",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::readTxDiagFIFO),
				        std::vector<std::string>{"LinkIndex"},
						std::vector<std::string>{"Diagnostic TX FIFO"},
					1);  // requiredUserPermissions
					
	registerFEMacroFunction(
		"Get Link Errors",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::GetLinkErrors),
				        std::vector<std::string>{""},
						std::vector<std::string>{"Link Errors"},
					1);  // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"ROCDestroy",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::ROCDestroy),
	// 				std::vector<std::string>{},
	// 				std::vector<std::string>{},
	// 				1);  // requiredUserPermissions
	// registerFEMacroFunction(
	// 	"ROCInstantiate",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&DTCFrontEndInterface::ROCInstantiate),
	// 				std::vector<std::string>{},
	// 				std::vector<std::string>{},
	// 				1);  // requiredUserPermissions
	registerFEMacroFunction(
		"DTCInstantiate",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::DTCInstantiate),
					std::vector<std::string>{},
					std::vector<std::string>{},
					1);  // requiredUserPermissions
	registerFEMacroFunction(
		"DMABufferRelease",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::DMABufferRelease),
					std::vector<std::string>{},
					std::vector<std::string>{},
					1);  // requiredUserPermissions
	registerFEMacroFunction(
		"DTC_Reset_Links",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ResetDTCLinks),
					std::vector<std::string>{},
					std::vector<std::string>{},
					1);  // requiredUserPermissions

	registerFEMacroFunction(
		"Headers Format test",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
				&DTCFrontEndInterface::HeaderFormatTest),
				std::vector<std::string>{},
				std::vector<std::string>{"setRegister"},
				1);

	std::stringstream feMacroTooltip;
	feMacroTooltip << "There are " << CONFIG_DTC_TIMING_CHAIN_STEPS <<
		" steps. So choose 1 step at a time, 0-" << CONFIG_DTC_TIMING_CHAIN_STEPS << 
		" or use -1 to run all steps sequentially." << __E__;
		
	registerFEMacroFunction(
		"Configure for Timing Chain",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ConfigureForTimingChain),
					std::vector<std::string>{"StepIndex"},
					std::vector<std::string>{},
					1,"*" /*allowedCallingFEs*/,
					feMacroTooltip.str());  // requiredUserPermissions

	registerFEMacroFunction(
		"Reset ROC link",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::ROCResetLink),
				    std::vector<std::string>{"Link", "Lane"},
					std::vector<std::string>{"readData"},
					1);  // requiredUserPermissions
	
	registerFEMacroFunction(
		"Check Link Loss-of-Light",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&DTCFrontEndInterface::GetLinkLossOfLight),            // feMacroFunction
					std::vector<std::string>{},  // namesOfInputArgs
					std::vector<std::string>{"Link Status"},
					1);  // requiredUserPermissions

	
	std::string value = FEVInterface::getFEMacroConstArgument(std::vector<std::pair<const std::string,std::string>>{
		
	std::make_pair("Link to Shutdown (0-7, 6 is Control)","0")},
	"Link to Shutdown (0-7, 6 is Control)");
			
			__COUTV__(value);


	{ //add ROC FE Macros
		__FE_COUT__ << "Getting children ROC FEMacros..." << __E__;
		rocFEMacroMap_.clear();
		for(auto& roc : rocs_)
		{
			auto feMacros = roc.second->getMapOfFEMacroFunctions();
			for(auto& feMacro:feMacros)
			{
				__FE_COUT__ << roc.first << "::" << feMacro.first << __E__;

				//make DTC FEMacro forwarding to ROC FEMacro				
                std::string macroName =
                                "Link" +
                                std::to_string(roc.second->getLinkID()) +
                                "_" + roc.first + "_" +
                                feMacro.first;
                __FE_COUTV__(macroName);
                std::vector<std::string> inputArgs,outputArgs;
                //inputParams.push_back("ROC_UID");
                //inputParams.push_back("ROC_FEMacroName");
				for(auto& inArg: feMacro.second.namesOfInputArguments_)
					inputArgs.push_back(inArg);
				for(auto& outArg: feMacro.second.namesOfOutputArguments_)
					outputArgs.push_back(outArg);

				__FE_COUTV__(StringMacros::vectorToString(inputArgs));
				__FE_COUTV__(StringMacros::vectorToString(outputArgs));

				rocFEMacroMap_.emplace(std::make_pair(macroName,
						std::make_pair(roc.first,feMacro.first)));

				registerFEMacroFunction(macroName,
						static_cast<FEVInterface::frontEndMacroFunction_t>(
								&DTCFrontEndInterface::RunROCFEMacro),
								inputArgs,
								outputArgs,
								1);  // requiredUserPermissions
			}
		}
	} //end add ROC FE Macros

	// clang-format on

	CFOandDTCCoreVInterface::registerCFOandDTCFEMacros();

}  // end registerFEMacros()

//==============================================================================
void DTCFrontEndInterface::configureSlowControls(void)
{
	__FE_COUT__ << "Configuring slow controls..." << __E__;

	// parent configure adds DTC slow controls channels
	FEVInterface::configureSlowControls();  // also resets DTC-proper channels

	__FE_COUT__ << "DTC '" << getInterfaceUID()
	            << "' slow controls channel count (BEFORE considering ROCs): "
	            << mapOfSlowControlsChannels_.size() << __E__;

	mapOfROCSlowControlsChannels_.clear();  // reset ROC channels

	// now add ROC slow controls channels
	ConfigurationTree ROCLink =
	    Configurable::getSelfNode().getNode("LinkToROCGroupTable");
	if(!ROCLink.isDisconnected())
	{
		std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
		    ROCLink.getChildren();

		unsigned int initialChannelCount;

		for(auto& rocChildPair : rocChildren)
		{
			initialChannelCount = mapOfROCSlowControlsChannels_.size();

			FEVInterface::addSlowControlsChannels(
			    rocChildPair.second.getNode("LinkToSlowControlsChannelTable"),
			    "/" + rocChildPair.first /*subInterfaceID*/,
			    &mapOfROCSlowControlsChannels_);

			__FE_COUT__ << "ROC '" << getInterfaceUID() << "/" << rocChildPair.first
			            << "' slow controls channel count: "
			            << mapOfROCSlowControlsChannels_.size() - initialChannelCount
			            << __E__;

		}  // end ROC children loop

	}      // end ROC channel handling
	else
		__FE_COUT__ << "ROC link disconnected, assuming no ROCs" << __E__;

	__FE_COUT__ << "DTC '" << getInterfaceUID()
	            << "' slow controls channel count (AFTER considering ROCs): "
	            << mapOfSlowControlsChannels_.size() +
	                   mapOfROCSlowControlsChannels_.size()
	            << __E__;

	__FE_COUT__ << "Done configuring slow controls." << __E__;

}  // end configureSlowControls()

//==============================================================================
// virtual in case channels are handled in multiple maps, for example
void DTCFrontEndInterface::resetSlowControlsChannelIterator(void)
{
	// call parent
	FEVInterface::resetSlowControlsChannelIterator();

	currentChannelIsInROC_ = false;
}  // end resetSlowControlsChannelIterator()

//==============================================================================
// virtual in case channels are handled in multiple maps, for example
FESlowControlsChannel* DTCFrontEndInterface::getNextSlowControlsChannel(void)
{
	// if finished with DTC slow controls channels, move on to ROC list
	if(slowControlsChannelsIterator_ == mapOfSlowControlsChannels_.end())
	{
		slowControlsChannelsIterator_ = mapOfROCSlowControlsChannels_.begin();
		currentChannelIsInROC_        = true;  // switch to ROC mode
	}

	// if finished with ROC list, then done
	if(slowControlsChannelsIterator_ == mapOfROCSlowControlsChannels_.end())
		return nullptr;

	if(currentChannelIsInROC_)
	{
		std::vector<std::string> uidParts;
		StringMacros::getVectorFromString(
		    slowControlsChannelsIterator_->second.interfaceUID_,
		    uidParts,
		    {'/'} /*delimiters*/);
		if(uidParts.size() != 2)
		{
			__FE_SS__ << "Illegal ROC slow controls channel name '"
			          << slowControlsChannelsIterator_->second.interfaceUID_
			          << ".' Format should be DTC/ROC." << __E__;
		}
		currentChannelROCUID_ =
		    uidParts[1];  // format DTC/ROC names, take 2nd part as ROC UID
	}
	return &(
	    (slowControlsChannelsIterator_++)->second);  // return iterator, then increment
}  // end getNextSlowControlsChannel()

//==============================================================================
// virtual in case channels are handled in multiple maps, for example
unsigned int DTCFrontEndInterface::getSlowControlsChannelCount(void)
{
	return mapOfSlowControlsChannels_.size() + mapOfROCSlowControlsChannels_.size();
}  // end getSlowControlsChannelCount()

//==============================================================================
// virtual in case read should be different than universalread
void DTCFrontEndInterface::getSlowControlsValue(FESlowControlsChannel& channel,
                                                std::string&           readValue)
{
	__FE_COUTV__(currentChannelIsInROC_);
	__FE_COUTV__(universalDataSize_);
	if(!currentChannelIsInROC_)
	{
		FEVInterface::getSlowControlsValue(channel, readValue);
	}
	else
	{
		__FE_COUTV__(currentChannelROCUID_);
		auto rocIt = rocs_.find(currentChannelROCUID_);
		if(rocIt == rocs_.end())
		{
			__FE_SS__ << "ROC UID '" << currentChannelROCUID_
			          << "' was not found in ROC map." << __E__;
			ss << "Here are the existing ROCs: ";
			bool first = true;
			for(auto& rocPair : rocs_)
				if(!first)
					ss << ", " << rocPair.first;
				else
				{
					ss << rocPair.first;
					first = false;
				}
			ss << __E__;
			__FE_SS_THROW__;
		}
		readValue.resize(universalDataSize_);
		*((uint16_t*)(&readValue[0])) =
		    rocIt->second->readRegister(*((uint16_t*)(&channel.universalAddress_[0])));
	}

	__FE_COUTV__(readValue.size());
}  // end getNextSlowControlsChannel()

//==============================================================================
void DTCFrontEndInterface::createROCs(void)
{
	rocs_.clear();

	std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
	    Configurable::getSelfNode().getNode("LinkToROCGroupTable").getChildren();

	// instantiate vector of ROCs
	for(auto& roc : rocChildren)
		if(roc.second.getNode("Status").getValue<bool>())
		{
			__FE_COUT__
			    << "ROC Plugin Name: "
			    << roc.second.getNode("ROCInterfacePluginName").getValue<std::string>()
			    << std::endl;
			__FE_COUT__ << "ROC Name: " << roc.first << std::endl;

			try
			{
				__COUTV__(theXDAQContextConfigTree_.getValueAsString());
				__COUTV__(
				    roc.second.getNode("ROCInterfacePluginName").getValue<std::string>());

				// Note: FEVInterface makeInterface returns a unique_ptr
				//	and we want to verify that ROCCoreVInterface functionality
				//	is there, so we do an intermediate dynamic_cast to check
				//	before placing in a new unique_ptr of type ROCCoreVInterface.
				std::unique_ptr<FEVInterface> tmpVFE = makeInterface(
				    roc.second.getNode("ROCInterfacePluginName").getValue<std::string>(),
				    roc.first,
				    theXDAQContextConfigTree_,
				    (theConfigurationPath_ + "/LinkToROCGroupTable/" + roc.first));

				// setup parent supervisor of FEVinterface (for backwards compatibility,
				// left out of constructor)
				tmpVFE->parentSupervisor_ = parentSupervisor_;

				ROCCoreVInterface& tmpRoc = dynamic_cast<ROCCoreVInterface&>(
				    *tmpVFE);  // dynamic_cast<ROCCoreVInterface*>(tmpRoc.get());

				// setup other members of ROCCore (for interface plug-in compatibility,
				// left out of constructor)

				__COUTV__(tmpRoc.emulatorMode_);
				tmpRoc.emulatorMode_ = emulatorMode_;
				__COUTV__(tmpRoc.emulatorMode_);

				if(emulatorMode_)
				{
					__FE_COUT__ << "Creating ROC in emulator mode..." << __E__;

					// try
					{
						// all ROCs support emulator mode

						//						// verify ROCCoreVEmulator class
						// functionality  with  dynamic_cast
						// ROCCoreVEmulator&  tmpEmulator =
						// dynamic_cast<ROCCoreVEmulator&>(
						//						    tmpRoc);  //
						// dynamic_cast<ROCCoreVInterface*>(tmpRoc.get());

						// start emulator thread
						std::thread(
						    [](ROCCoreVInterface* rocEmulator) {
							    __COUT__ << "Starting ROC emulator thread..." << __E__;
							    ROCCoreVInterface::emulatorThread(rocEmulator);
						    },
						    &tmpRoc)
						    .detach();
					}
					//					catch(const std::bad_cast& e)
					//					{
					//						__SS__ << "Cast to ROCCoreVEmulator failed!
					// Verify  the  emulator " 						          "plugin
					// inherits
					// from  ROCCoreVEmulator."
					//						       << __E__;
					//						ss << "Failed to instantiate plugin named '"
					//<<  roc.first
					//						   << "' of type '"
					//						   <<
					// roc.second.getNode("ROCInterfacePluginName")
					//						          .getValue<std::string>()
					//						   << "' due to the following error: \n"
					//						   << e.what() << __E__;
					//
					//						__SS_THROW__;
					//					}
				}
				else
				{
					tmpRoc.thisDTC_ = thisDTC_;
				}

				rocs_.emplace(std::pair<std::string, std::unique_ptr<ROCCoreVInterface>>(
				    roc.first, &tmpRoc));
				tmpVFE.release();  // release the FEVInterface unique_ptr, so we are left
				                   // with just one

				__COUTV__(rocs_[roc.first]->emulatorMode_);
			}
			catch(const cet::exception& e)
			{
				__SS__ << "Failed to instantiate plugin named '" << roc.first
				       << "' of type '"
				       << roc.second.getNode("ROCInterfacePluginName")
				              .getValue<std::string>()
				       << "' due to the following error: \n"
				       << e.what() << __E__;
				__FE_SS_THROW__;
			}
			catch(const std::bad_cast& e)
			{
				__SS__ << "Cast to ROCCoreVInterface failed! Verify the plugin inherits "
				          "from ROCCoreVInterface."
				       << __E__;
				ss << "Failed to instantiate plugin named '" << roc.first << "' of type '"
				   << roc.second.getNode("ROCInterfacePluginName").getValue<std::string>()
				   << "' due to the following error: \n"
				   << e.what() << __E__;

				__FE_SS_THROW__;
			}
		}

	__FE_COUT__ << "Done creating " << rocs_.size() << " ROC(s)" << std::endl;
}  // end createROCs()

//==================================================================================================
std::string DTCFrontEndInterface::readStatus(void)
{
	return thisDTC_->FormattedRegDump(20);
} // end readStatus()

// //==================================================================================================
// int DTCFrontEndInterface::getROCLinkStatus(int ROC_link)
// {
// 	int overall_link_status = registerRead(0x9140);

// 	int ROC_link_status = (overall_link_status >> ROC_link) & 0x1;

// 	return ROC_link_status;
// }  // end getROCLinkStatus()

// //==================================================================================================
// int DTCFrontEndInterface::getCFOLinkStatus()
// {
// 	int overall_link_status = registerRead(0x9140);

// 	int CFO_link_status = (overall_link_status >> 6) & 0x1;

// 	return CFO_link_status;
// }  // end getCFOLinkStatus()

// //==================================================================================================
// int DTCFrontEndInterface::checkLinkStatus()
// {
// 	int ROCs_OK = 1;

// 	for(int i = 0; i < 8; i++)
// 	{
// 		//__FE_COUT__ << " check link " << i << " ";

// 		if(ROCActive(i))
// 		{
// 			//__FE_COUT__ << " active... status = " << getROCLinkStatus(i) << __E__;
// 			ROCs_OK &= getROCLinkStatus(i);
// 		}
// 	}

// 	if((getCFOLinkStatus() == 1) && ROCs_OK == 1)
// 	{
// 		//	__FE_COUT__ << "DTC links OK = 0x" << std::hex << registerRead(0x9140)
// 		//<< std::dec << __E__;
// 		//	__MOUT__ << "DTC links OK = 0x" << std::hex << registerRead(0x9140) <<
// 		// std::dec << __E__;

// 		return 1;
// 	}
// 	else
// 	{
// 		//	__FE_COUT__ << "DTC links not OK = 0x" << std::hex <<
// 		// registerRead(0x9140) << std::dec << __E__;
// 		//	__MOUT__ << "DTC links not OK = 0x" << std::hex << registerRead(0x9140)
// 		//<< std::dec << __E__;

// 		return 0;
// 	}
// }  // end checkLinkStatus()

// //==================================================================================================
// bool DTCFrontEndInterface::ROCActive(unsigned ROC_link)
// {
// 	// __FE_COUTV__(roc_mask_);
// 	// __FE_COUTV__(ROC_link);

// 	if(((roc_mask_ >> ROC_link) & 0x01) == 1)
// 	{
// 		return true;
// 	}
// 	else
// 	{
// 		return false;
// 	}
// }  // end ROCActive()

//==================================================================================================
void DTCFrontEndInterface::configure(void)
try
{
	__FE_COUTV__(getIterationIndex());
	__FE_COUTV__(getSubIterationIndex());

	if(skipInit_) return;

	if(operatingMode_ == "HardwareDevMode")
	{
		__FE_COUT_INFO__ << "Configuring for hardware development mode!" << __E__;
		configureHardwareDevMode();
	}
	else if(operatingMode_ == "EventBuildingMode")
	{
		__FE_COUT_INFO__ << "Configuring for Event Building mode!" << __E__;
		configureEventBuildingMode();
	}
	else if(operatingMode_ == "LoopbackMode")
	{
		__FE_COUT_INFO__ << "Configuring for Loopback mode!" << __E__;
		configureLoopbackMode();
	}
	else
	{
		__FE_SS__ << "Unknown system operating mode: " << operatingMode_ << __E__
		          << " Please specify a valid operating mode in the 'Mu2eGlobalsTable.'"
		          << __E__;
		__FE_SS_THROW__;
	}

	return;

	// /////////////////////////////
	// /////////////////////////////
	// ///////////////////////////// old configure
	// ///////////////////////////// old configure
	// ///////////////////////////// old configure
	// ///////////////////////////// old configure
	// /////////////////////////////
	// /////////////////////////////

	// // if(regWriteMonitorStream_.is_open())
	// // {
	// // 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0)
	// // 	                       << ", \t ---------- Configure step " << getIterationIndex()
	// // 	                       << ":" << getSubIterationIndex() << "\n";
	// // 	regWriteMonitorStream_.flush();
	// // }

	// if(getConfigurationManager()
	//        ->getNode("/Mu2eGlobalsTable/SyncDemoConfig/SkipCFOandDTCConfigureSteps")
	//        .getValue<bool>())
	// {
	// 	__FE_COUT_INFO__ << "Skipping configure steps!" << __E__;
	// 	return;
	// }

	// if(emulatorMode_)
	// {
	// 	__FE_COUT__ << "Emulator DTC configuring... # of ROCs = " << rocs_.size()
	// 	            << __E__;
	// 	for(auto& roc : rocs_)
	// 		roc.second->configure();
	// 	return;
	// }

	// uint32_t dtcEventBuilderReg_DTCID = 0;
	// uint32_t dtcEventBuilderReg_Mode = 0;
	// uint32_t dtcEventBuilderReg_PartitionID = 0;
	// uint32_t dtcEventBuilderReg_MACIndex = 0;
	// // uint32_t dtcEventBuilderReg_DTCInfo = 0;

	// uint32_t dtcEventBuilderReg_NumBuff       = 0;
	// uint32_t dtcEventBuilderReg_StartNode     = 0;
	// uint32_t dtcEventBuilderReg_NumNodes      = 0;
	// // uint32_t dtcEventBuilderReg_Configuration = 0;

	// try
	// {
	// 	dtcEventBuilderReg_DTCID =
	// 	    getSelfNode().getNode("EventBuilderDTCID").getValue<uint32_t>();
	// 	dtcEventBuilderReg_Mode =
	// 	    getSelfNode().getNode("EventBuilderMode").getValue<uint32_t>();
	// 	dtcEventBuilderReg_PartitionID =
	// 	    getSelfNode().getNode("EventBuilderPartitionID").getValue<uint32_t>();
	// 	dtcEventBuilderReg_MACIndex =
	// 	    getSelfNode().getNode("EventBuilderMACIndex").getValue<uint32_t>();

	// 	dtcEventBuilderReg_NumBuff =
	// 	    getSelfNode().getNode("EventBuilderNumBuff").getValue<uint32_t>();
	// 	dtcEventBuilderReg_StartNode =
	// 	    getSelfNode().getNode("EventBuilderStartNode").getValue<uint32_t>();
	// 	dtcEventBuilderReg_NumNodes =
	// 	    getSelfNode().getNode("EventBuilderNumNodes").getValue<uint32_t>();

	// 	__FE_COUTV__(dtcEventBuilderReg_DTCID);
	// 	__FE_COUTV__(dtcEventBuilderReg_Mode);
	// 	__FE_COUTV__(dtcEventBuilderReg_PartitionID);
	// 	__FE_COUTV__(dtcEventBuilderReg_MACIndex);
	// 	__FE_COUTV__(dtcEventBuilderReg_NumBuff);
	// 	__FE_COUTV__(dtcEventBuilderReg_StartNode);
	// 	__FE_COUTV__(dtcEventBuilderReg_NumNodes);

	// 	// Register x9154 is #DTC ID [31-24] / EVB Mode [23-16]/ EVB Partition ID [15-8]/
	// 	// EVB Local MAC Index [7-0]
	// 	thisDTC_->SetEVBInfo(dtcEventBuilderReg_DTCID,
	// 		dtcEventBuilderReg_Mode,
	// 		dtcEventBuilderReg_PartitionID,
	// 		dtcEventBuilderReg_MACIndex);
	// 	// dtcEventBuilderReg_DTCInfo =
	// 	//     dtcEventBuilderReg_DTCID << 24 | dtcEventBuilderReg_Mode << 16 |
	// 	//     dtcEventBuilderReg_PartitionID << 8 | dtcEventBuilderReg_MACIndex;
	// 	// __FE_COUTV__(dtcEventBuilderReg_DTCInfo);
	// 	// registerWrite(0x9154, dtcEventBuilderReg_DTCInfo);

	// 	// Register x9158 is #Num EVB Buffers[22-16], EVB Start Node [14-8], Num Nodes
	// 	// [6-0]
	// 	thisDTC_->SetEVBBufferInfo(dtcEventBuilderReg_NumBuff,
	// 		dtcEventBuilderReg_StartNode,
	// 		dtcEventBuilderReg_NumNodes);
	// 	// dtcEventBuilderReg_Configuration = dtcEventBuilderReg_NumBuff << 16 |
	// 	//                                    dtcEventBuilderReg_StartNode << 8 |
	// 	//                                    dtcEventBuilderReg_NumNodes;
	// 	// __FE_COUTV__(dtcEventBuilderReg_Configuration);
	// 	// registerWrite(0x9158, dtcEventBuilderReg_Configuration);
	// }
	// catch(...)
	// {
	// 	__FE_COUT_INFO__ << "Ignoring missing event building configuration values."
	// 	                 << __E__;
	// }
	// // end of the new code

	// __FE_COUT__ << "DTC configuring... # of ROCs = " << rocs_.size() << __E__;

	// // NOTE: otsdaq/xdaq has a soap reply timeout for state transitions.
	// // Therefore, break up configuration into several steps so as to reply before
	// // the time out As well, there is a specific order in which to configure the
	// // links in the chain of CFO->DTC0->DTC1->...DTCN

	// const int number_of_system_configs =
	//     -1;                    // if < 0, keep trying until links are OK.
	//                            // If > 0, go through configuration steps this many times

	// const int reset_fpga = 1;  // 1 = yes, 0 = no
	// // const bool config_clock             = configure_clock_;  // 1 = yes, 0 = no
	// const bool config_jitter_attenuator = configure_clock_;  // 1 = yes, 0 = no
	// // const int  reset_rx                 = 0;                 // 1 = yes, 0 = no

	// const int number_of_dtc_config_steps = 7;

	// const int max_number_of_tries = 3;  // max number to wait for links OK

	// int number_of_total_config_steps =
	//     number_of_system_configs * number_of_dtc_config_steps;

	// int config_step    = getIterationIndex();
	// int config_substep = getSubIterationIndex();

	// bool isLastTimeThroughConfigure = false;

	// if(number_of_system_configs > 0)
	// {
	// 	if(config_step >= number_of_total_config_steps)  // done, exit system config
	// 		return;
	// }

	// // waiting for link loop
	// if(config_substep > 0 && config_substep < max_number_of_tries)
	// {  // wait a maximum of 30 seconds

	// 	const int number_of_link_checks = 10;

	// 	// int link_ok = 0;

	// 	for(int i = 0; i < number_of_link_checks; i++)
	// 	{
	// 		if(checkLinkStatus() == 1)
	// 		{
	// 			// links OK,  continue with the rest of the configuration
	// 			__FE_COUT__ << device_name_ << " Link Status is OK = 0x" << std::hex
	// 			            << registerRead(0x9140) << std::dec << __E__;

	// 			indicateIterationWork();
	// 			turnOffLED();
	// 			return;
	// 		}
	// 		else if(getCFOLinkStatus() == 0)
	// 		{
	// 			// in this case, links will never get to be OK, stop waiting for them

	// 			__FE_COUT__ << device_name_ << " CFO Link Status is bad = 0x" << std::hex
	// 			            << registerRead(0x9140) << std::dec << __E__;

	// 			// usleep(500000 /*500ms*/);
	// 			sleep(1);

	// 			indicateIterationWork();
	// 			turnOffLED();
	// 			return;
	// 		}
	// 		else
	// 		{
	// 			// links still not OK, keep checking...

	// 			__FE_COUT__ << "Waiting for DTC Link Status = 0x" << std::hex
	// 			            << registerRead(0x9140) << std::dec << __E__;
	// 			// usleep(500000 /*500ms*/);
	// 			sleep(1);
	// 		}
	// 	}

	// 	indicateSubIterationWork();
	// 	return;
	// }
	// else if(config_substep > max_number_of_tries)
	// {
	// 	// wait a maximum of 30 seconds, then stop waiting for them

	// 	__FE_COUT__ << "Links still bad = 0x" << std::hex << registerRead(0x9140)
	// 	            << std::dec << "... continue" << __E__;
	// 	indicateIterationWork();
	// 	turnOffLED();
	// 	return;
	// }

	// turnOnLED();


	// if((config_step % number_of_dtc_config_steps) == 0)
	// {
	// 	__FE_COUTV__(GetFirmwareVersion());
	// 	if(reset_fpga == 1 && config_step < number_of_dtc_config_steps)
	// 	{
	// 		// only reset the FPGA the first time through
	// 		__FE_COUT_INFO__ << "Step " << config_step << ": " << device_name_
	// 		                 << " Reset DTC " << __E__;

	// 		DTCReset();
	// 	}

	// 	// From Rick new code
	// 	uint32_t dtcEventBuilderReg_DTCID       = 0;
	// 	uint32_t dtcEventBuilderReg_Mode        = 0;
	// 	uint32_t dtcEventBuilderReg_PartitionID = 0;
	// 	uint32_t dtcEventBuilderReg_MACIndex    = 0;
	// 	uint32_t dtcEventBuilderReg_DTCInfo     = 0;

	// 	uint32_t dtcEventBuilderReg_NumBuff       = 0;
	// 	uint32_t dtcEventBuilderReg_StartNode     = 0;
	// 	uint32_t dtcEventBuilderReg_NumNodes      = 0;
	// 	uint32_t dtcEventBuilderReg_Configuration = 0;

	// 	try
	// 	{
	// 		__FE_COUT__ << "Configuring DTC registers for the EVB" << rocs_.size()
	// 		            << __E__;

	// 		dtcEventBuilderReg_DTCID =
	// 		    getSelfNode().getNode("EventBuilderDTCID").getValue<uint32_t>();
	// 		dtcEventBuilderReg_Mode =
	// 		    getSelfNode().getNode("EventBuilderMode").getValue<uint32_t>();
	// 		dtcEventBuilderReg_PartitionID =
	// 		    getSelfNode().getNode("EventBuilderPartitionID").getValue<uint32_t>();
	// 		dtcEventBuilderReg_MACIndex =
	// 		    getSelfNode().getNode("EventBuilderMACIndex").getValue<uint32_t>();

	// 		dtcEventBuilderReg_NumBuff =
	// 		    getSelfNode().getNode("EventBuilderNumBuff").getValue<uint32_t>();
	// 		dtcEventBuilderReg_StartNode =
	// 		    getSelfNode().getNode("EventBuilderStartNode").getValue<uint32_t>();
	// 		dtcEventBuilderReg_NumNodes =
	// 		    getSelfNode().getNode("EventBuilderNumNodes").getValue<uint32_t>();

	// 		__FE_COUTV__(dtcEventBuilderReg_DTCID);  // Doesn't work if I use uint8_t
	// 		__FE_COUTV__(dtcEventBuilderReg_Mode);
	// 		__FE_COUTV__(dtcEventBuilderReg_PartitionID);
	// 		__FE_COUTV__(dtcEventBuilderReg_MACIndex);
	// 		__FE_COUTV__(dtcEventBuilderReg_NumBuff);
	// 		__FE_COUTV__(dtcEventBuilderReg_StartNode);
	// 		__FE_COUTV__(dtcEventBuilderReg_NumNodes);

	// 		// Register x9154 is #DTC ID [31-24] / EVB Mode [23-16]/ EVB Partition ID
	// 		// [15-8]/ EVB Local MAC Index [7-0]
	// 		dtcEventBuilderReg_DTCInfo =
	// 		    dtcEventBuilderReg_DTCID << 24 | dtcEventBuilderReg_Mode << 16 |
	// 		    dtcEventBuilderReg_PartitionID << 8 | dtcEventBuilderReg_MACIndex;
	// 		__FE_COUTV__(dtcEventBuilderReg_DTCInfo);
	// 		registerWrite(0x9154, dtcEventBuilderReg_DTCInfo);

	// 		// Register x9158 is #Num EVB Buffers[22-16], EVB Start Node [14-8], Num Nodes
	// 		// [6-0]
	// 		dtcEventBuilderReg_Configuration = dtcEventBuilderReg_NumBuff << 16 |
	// 		                                   dtcEventBuilderReg_StartNode << 8 |
	// 		                                   dtcEventBuilderReg_NumNodes;
	// 		__FE_COUTV__(dtcEventBuilderReg_Configuration);
	// 		registerWrite(0x9158, dtcEventBuilderReg_Configuration);
	// 	}
	// 	catch(...)
	// 	{
	// 		__FE_COUT_INFO__ << "Ignoring missing event building configuration values."
	// 		                 << __E__;
	// 	}
	// 	// end of the new code
	// }
	// else if((config_step % number_of_dtc_config_steps) == 1)
	// {
	// 	__FE_COUT_INFO__ << "Step" << config_step << ": " << device_name_
	// 	                 << "select/setup clock..." << __E__;

	// 	// choose jitter attenuator input select (reg 0x9308, bits 5:4)
	// 	//  0 is Upstream Control Link Rx Recovered Clock
	// 	//  1 is RJ45 Upstream Clock
	// 	//  2 is Timing Card Selectable (SFP+ or FPGA) Input Clock
	// 	{
	// 		uint32_t readData = registerRead(0x9308);
	// 		uint32_t val      = 0;
	// 		try
	// 		{
	// 			val = getSelfNode()
	// 			          .getNode("JitterAttenuatorInputSource")
	// 			          .getValue<uint32_t>();
	// 		}
	// 		catch(...)
	// 		{
	// 			__FE_COUT__ << "Defaulting Jitter Attenuator Input Source to val = "
	// 			            << val << __E__;
	// 		}
	// 		readData &= ~(3 << 4);       // clear the two bits
	// 		readData &= ~(1);            // ensure unreset of jitter attenuator
	// 		readData |= (val & 3) << 4;  // set the two bits to selected value

	// 		registerWrite(0x9308, readData);
	// 		__FE_COUT__
	// 		    << "Jitter Attenuator Input Select: " << val << " ==> "
	// 		    << (val == 0
	// 		            ? "Upstream Control Link Rx Recovered Clock"
	// 		            : (val == 1
	// 		                   ? "RJ45 Upstream Clock"
	// 		                   : "Timing Card Selectable (SFP+ or FPGA) Input Clock"))
	// 		    << __E__;
	// 	}
	// }
	// else if((config_step % number_of_dtc_config_steps) == 2)
	// {
	// 	// configure Jitter Attenuator to recover clock
	// 	if((config_jitter_attenuator == 1 || emulate_cfo_ == 1) &&
	// 	   config_step < number_of_dtc_config_steps)
	// 	{
	// 		__MCOUT_INFO__("Step " << config_step << ": " << device_name_
	// 		                       << " configure Jitter Attenuator..." << __E__);

	// 		// It's needed only after a powercycle
	// 		configureJitterAttenuator();

	// 		usleep(500000 /*500ms*/);
	// 		sleep(1);
	// 	}
	// 	else
	// 	{
	// 		__MCOUT_INFO__("Step " << config_step << ": " << device_name_
	// 		                       << " do NOT configure Jitter Attenuator..." << __E__);
	// 	}
	// }
	// else if((config_step % number_of_dtc_config_steps) == 3)
	// {
	// 	if(emulate_cfo_ == 1)
	// 	{
	// 		__MCOUT_INFO__("Step " << config_step << ": " << device_name_
	// 		                       << " enable CFO emulation and internal clock");

	// 		int dataToWrite = 0x40808404;
	// 		registerWrite(0x9100, dataToWrite);  // This disable retransmission + set the
	// 		                                     // CFO in emulation mode

	// 		// Micol thinks this two lines below are not needed
	// 		//__FE_COUT__ << ".......CFO emulation: time between data requests" << __E__;
	// 		// registerWrite(0x91a8, 0x4e20);
	// 	}
	// 	else
	// 	{
	// 		int dataInReg = registerRead(0x9100);
	// 		int dataToWrite =
	// 		    dataInReg &
	// 		    0xbfff7fff;  // bit 30 = CFO emulation enable; bit 15 CFO emulation mode
	// 		registerWrite(0x9100, dataToWrite);
	// 	}
	// }
	// else if((config_step % number_of_dtc_config_steps) == 4) {}
	// else if((config_step % number_of_dtc_config_steps) == 5)
	// {
	// 	__MCOUT_INFO__("Step " << config_step << ": " << device_name_
	// 	                       << " enable markers, Tx, Rx" << __E__);

	// 	// enable markers, tx and rx

	// 	int data_to_write = (roc_mask_ << 8) | roc_mask_;
	// 	__FE_COUT__ << "CFO enable markers - enabled ROC links 0x" << std::hex
	// 	            << data_to_write << std::dec << __E__;
	// 	registerWrite(0x91f8, data_to_write);

	// 	data_to_write = 0x4040 | (roc_mask_ << 8) | roc_mask_;
	// 	__FE_COUT__ << "DTC enable tx and rx - CFO and enabled ROC links 0x" << std::hex
	// 	            << data_to_write << std::dec << __E__;
	// 	registerWrite(0x9114, data_to_write);

	// 	data_to_write = 0x00014141;  // DMA timeout from chants.
	// 	__FE_COUT__ << "set DMA timeout" << std::hex << data_to_write << std::dec
	// 	            << __E__;
	// 	registerWrite(0x9144, data_to_write);

	// 	// put DTC CFO link output into loopback mode
	// 	__FE_COUT__ << "DTC set CFO link output loopback mode ENABLE" << __E__;

	// 	__MCOUT_INFO__("Step " << config_step << ": " << device_name_ << " configure ROCs"
	// 	                       << __E__);

	// 	bool doConfigureROCs = false;
	// 	try
	// 	{
	// 		doConfigureROCs = Configurable::getSelfNode()
	// 		                      .getNode("EnableROCConfigureStep")
	// 		                      .getValue<bool>();
	// 	}
	// 	catch(...)
	// 	{
	// 	}  // ignore missing field
	// 	if(doConfigureROCs)
	// 		for(auto& roc : rocs_)
	// 			roc.second->configure();

	// 	// usleep(500000 /*500ms*/);
	// 	sleep(1);
	// }
	// else if((config_step % number_of_dtc_config_steps) == 6)
	// {
	// 	if(emulate_cfo_ == 1)
	// 	{
	// 		__MCOUT_INFO__("Step " << config_step
	// 		                       << ": CFO emulation enable Event start characters "
	// 		                          "and event window interval"
	// 		                       << __E__);

	// 		__FE_COUT__ << "CFO emulation:  set Event Window interval" << __E__;
	// 		registerWrite(0x91f0, 0x00000000);  // for NO markers, write these

	// 		__FE_COUT__ << "CFO emulation:  set 40MHz marker interval" << __E__;
	// 		registerWrite(0x91f4, 0x00000000);  // for NO markers, write these

	// 		__FE_COUT__ << "CFO emulation:  set heartbeat interval " << __E__;
	// 	}
	// 	__MCOUT_INFO__("Step " << config_step << ": " << device_name_ << " configured"
	// 	                       << __E__);

	// 	__FE_COUTV__(getIterationIndex());
	// 	__FE_COUTV__(getSubIterationIndex());

	// 	if(checkLinkStatus() == 1)
	// 	{
	// 		__MCOUT_INFO__(device_name_ << " links OK 0x" << std::hex
	// 		                            << registerRead(0x9140) << std::dec << __E__);

	// 		// usleep(500000 ); //500ms/
	// 		sleep(1);
	// 		turnOffLED();

	// 		if(number_of_system_configs < 0)
	// 		{
	// 			isLastTimeThroughConfigure = true;
	// 			// do a final DTC Reset
	// 			//__MCOUT_INFO__("Last step in configuration; doing DTCReset" << __E__);
	// 			// DTCReset();
	// 		}
	// 	}
	// 	else if(config_step > max_number_of_tries)
	// 	{
	// 		isLastTimeThroughConfigure = true;
	// 		__MCOUT_INFO__(device_name_ << " after " << max_number_of_tries
	// 		                            << " tries, links not OK 0x" << std::hex
	// 		                            << registerRead(0x9140) << std::dec << __E__);
	// 	}
	// 	else
	// 	{
	// 		__MCOUT_INFO__(device_name_ << " links not OK 0x" << std::hex
	// 		                            << registerRead(0x9140) << std::dec << __E__);
	// 	}

	// 	__FE_COUTV__(isLastTimeThroughConfigure);
	// 	if(isLastTimeThroughConfigure)
	// 	{
	// 		sleep(2);
	// 		// write anything to reset
	// 		// 0x93c8 is RX CDR Unlock counter (32-bit)
	// 		__MCOUT_INFO__("LAST STEP!! Reset Loss-of-Lock Counter() on DTC");

	// 		registerWrite(0x93c8, 0);

	// 		// Registers to set the EVB
	// 		try
	// 		{
	// 			__FE_COUT__ << "Configuring DTC registers for the EVB" << rocs_.size()
	// 			            << __E__;
	// 			// These registers are needed for the EVB, but I need to check their
	// 			// meaning
	// 			registerWrite(0x9100, 0x800404);
	// 			registerWrite(0x92c0, 0x0);
	// 			registerWrite(0x9114, 0xc1c1);
	// 			registerWrite(0x96C8, 0x555555D5);
	// 			registerWrite(0x96CC, 0x78555555);
	// 		}
	// 		catch(...)
	// 		{
	// 			__FE_COUT_INFO__
	// 			    << "Ignoring missing event building DTC registers values." << __E__;
	// 		}
	// 		// end of the new code

	// 		return;  // links OK, kick out of configure OR link tries complete
	// 	}
	// }

	// readStatus();             // spit out link status at every step

	// indicateIterationWork();  // otherwise, tell state machine to stay in configure
	//                           // state ("come back to me")

	// turnOffLED();

	// return;
}  // end configure()
catch(const std::runtime_error& e)
{
	__FE_SS__ << "Error caught: " << e.what() << __E__;
	__FE_SS_THROW__;
}
catch(const std::exception& e)
{
	__FE_SS__ << "Error caught: " << e.what() << __E__;
	__FE_SS_THROW__;
}
catch(...)
{
	__FE_SS__ << "Unknown error caught. Check the printouts!" << __E__;
	__FE_SS_THROW__;
}

//==============================================================================
void DTCFrontEndInterface::configureHardwareDevMode(void)
{
	__FE_COUT_INFO__ << "configureHardwareDevMode()" << __E__;

	// 0x9100 is the DTC Control Register
	// registerWrite(0x9100, 0x40008404);  // bit 30 = CFO emulation enable, bit 15 = CFO emulation mode
	                  // (set both to 1 to be use the CFO emulated)
	                  // bit 2 = DCS enable
	                  // bit 10 = Sequence Number Disable
	                  // (set to 1 to turns off retrasmission DTC-ROC which isn't working
	                  // right now)

	thisDTC_->ResetDTC(); //put DTC in known state with DTC reset
	thisDTC_->ClearDTCControlRegister();

	if(configure_clock_)
	{
		uint32_t select      = 0;
		try
		{
			select = getSelfNode()
						.getNode("JitterAttenuatorInputSource")
						.getValue<uint32_t>();
		}
		catch(...)
		{
			__FE_COUT__ << "Defaulting Jitter Attenuator Input Source to select = "
						<< select << __E__;
		}

		__FE_COUTV__(select);
		//For DTC - 0 ==> CFO Control Link
		//For DTC - 1 ==> RTF copper clock
		//For DTC - 2 ==> FPGA FMC
		thisDTC_->SetJitterAttenuatorSelect(select); // this call should first check if JA is already locked, JA only needs to be set after a cold start or if input clock changes
	}
	else
		__FE_COUT_INFO__ << "Skipping configure clock." << __E__;



	// thisDTC_->EnableCFOEmulation(); // this will enable sending requests, so do it later after configuring event 
	thisDTC_->SetCFOEmulationMode(); //turn on DTC emulation (ignores any real CFO) 
	thisDTC_->SetSequenceNumberDisable();
	thisDTC_->EnableDCSReception();


	// CFO Event Mode bytes (for the CFO Emulator): 0x91c0 and 0x91c4
	// Data in these registers populates the Event Mode field in the Heartbeat packets the
	// CFO Emulator will send. They are used to identify a Null vs. Non-null heartbeat: If
	// the Event Mode bits are all zero, the DTC interprets it a null heartbeats. The DTC
	// only generates Data Request Packets for non-null heartbeats.  So when these
	// registers are set to zeros, the DTC generates only null heartbeats, and no Data
	// Requests. For testing purposes, it's needed to set the Event Mode Bytes to some
	// non-zero value.
	// registerWrite(0x91c0, 0xffffffff);
	// registerWrite(0x91c4, 0xffff);
	thisDTC_->SetCFOEmulationEventMode(-1);
	__FE_COUTV__(thisDTC_->ReadCFOEmulationEventMode());

	// The DTC's CFO Emulator will generate a number of null packets after the number
	// of requested non-null heartbeats has been output.
	// The number of requests (non-nulls) is set in 0x91AC, and the number of nulls is set
	// in 0x91BC.
	// registerWrite(0x91BC, 0x10);  // Set number of null heartbeats packets
	thisDTC_->SetCFOEmulationNumNullHeartbeats(0x10);
	// registerWrite(0x91AC, 0); // Set number of heartbeats packets


	// check if any ROCs should be DTC-hardware emulated ROCs
	{
		std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
		    Configurable::getSelfNode().getNode("LinkToROCGroupTable").getChildren();

		int dtcHwEmulateROCmask = 0;
		for(auto& roc : rocChildren)
		{
			bool enabled = roc.second.getNode("EmulateInDTCHardware").getValue<bool>();

			if(enabled)
			{
				int linkID = roc.second.getNode("linkID").getValue<int>();
				__FE_COUT__ << "roc uid '" << roc.first << "' at link=" << linkID
				            << " is DTC-hardware emulated!" << __E__;
				dtcHwEmulateROCmask |= (1 << linkID);
			}
		}

		__FE_COUT__ << "Writing DTC-hardware emulation mask: 0x" << std::hex
		            << dtcHwEmulateROCmask << std::dec << __E__;
		thisDTC_->SetROCEmulatorMask(dtcHwEmulateROCmask);
		// registerWrite(0x9110, dtcHwEmulateROCmask);
		__FE_COUT__ << "End check for DTC-hardware emulated ROCs." << __E__;
	}  // end check if any ROCs should be DTC-hardware emulated ROCs

	//enable ROC links (do not forget CFO link is off in HW dev mode)
	__FE_COUT__ << "Enabling/Disabling DTC links with ROC mask = " << roc_mask_ << __E__;
	thisDTC_->DisableLink(DTCLib::DTC_Link_CFO);
	thisDTC_->DisableLink(DTCLib::DTC_Link_EVB);
	for(size_t i=0;i<DTCLib::DTC_Links.size();++i)
		if((roc_mask_ >> i) & 1)
			thisDTC_->EnableLink(DTCLib::DTC_Links[i]);
		else
			thisDTC_->DisableLink(DTCLib::DTC_Links[i]);
	thisDTC_->SetROCDCSResponseTimer(1000); //set ROC DCS timeout (if 0, the DTC will hang forever when a ROC does not respond)
	thisDTC_->SetDMATimeoutPreset(0x00014141);  // DMA timeout from chants (default is 0x800)

}  // end configureHardwareDevMode()

//==============================================================================
void DTCFrontEndInterface::configureEventBuildingMode(int step)
{
	if(step == -1)
		step = getIterationIndex();

	__FE_COUT_INFO__ << "configureEventBuildingMode() " << step << __E__;
	
	if(emulate_cfo_) //when no CFO, what is event building mode?
	{
		__FE_SS__ << "There is no CFO! Event Building Mode is invalid." << __E__;
		__SS_THROW__;
	}

	if(step < CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX)
	{
		__FE_COUT__ << "Do nothing while CFO configures for timing chain." << __E__;
		indicateIterationWork();
	}
	else if(step < CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX + 
		CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	{
		configureForTimingChain(getIterationIndex() - 
			CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX /* start case index!! */);
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX + 
		CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	{
		__FE_COUT__ << "Do nothing while CFO reset its Tx..." << __E__;
		indicateIterationWork();
	}
	else if(step == 1 + CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX + 
		CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	{ 
		__FE_COUT__ << "Setup EVB parameters..." << __E__;
		uint32_t dtcEventBuilderReg_DTCID = 0;
		uint32_t dtcEventBuilderReg_Mode = 0;
		uint32_t dtcEventBuilderReg_PartitionID = 0;
		uint32_t dtcEventBuilderReg_MACIndex = 0;
		// uint32_t dtcEventBuilderReg_DTCInfo = 0;

		uint32_t dtcEventBuilderReg_NumBuff       = 0;
		uint32_t dtcEventBuilderReg_StartNode     = 0;
		uint32_t dtcEventBuilderReg_NumNodes      = 0;
		// uint32_t dtcEventBuilderReg_Configuration = 0;

		try
		{
			dtcEventBuilderReg_DTCID =
				getSelfNode().getNode("EventBuilderDTCID").getValue<uint32_t>();
			dtcEventBuilderReg_Mode =
				getSelfNode().getNode("EventBuilderMode").getValue<uint32_t>();
			dtcEventBuilderReg_PartitionID =
				getSelfNode().getNode("EventBuilderPartitionID").getValue<uint32_t>();
			dtcEventBuilderReg_MACIndex =
				getSelfNode().getNode("EventBuilderMACIndex").getValue<uint32_t>();

			dtcEventBuilderReg_NumBuff =
				getSelfNode().getNode("EventBuilderNumBuff").getValue<uint32_t>();
			dtcEventBuilderReg_StartNode =
				getSelfNode().getNode("EventBuilderStartNode").getValue<uint32_t>();
			dtcEventBuilderReg_NumNodes =
				getSelfNode().getNode("EventBuilderNumNodes").getValue<uint32_t>();

			__FE_COUTV__(dtcEventBuilderReg_DTCID);
			__FE_COUTV__(dtcEventBuilderReg_Mode);
			__FE_COUTV__(dtcEventBuilderReg_PartitionID);
			__FE_COUTV__(dtcEventBuilderReg_MACIndex);
			__FE_COUTV__(dtcEventBuilderReg_NumBuff);
			__FE_COUTV__(dtcEventBuilderReg_StartNode);
			__FE_COUTV__(dtcEventBuilderReg_NumNodes);

			// Register x9154 is #DTC ID [31-24] / EVB Mode [23-16]/ EVB Partition ID [15-8]/
			// EVB Local MAC Index [7-0]
			thisDTC_->SetEVBInfo(dtcEventBuilderReg_DTCID,
				dtcEventBuilderReg_Mode,
				dtcEventBuilderReg_PartitionID,
				dtcEventBuilderReg_MACIndex);
			// dtcEventBuilderReg_DTCInfo =
			//     dtcEventBuilderReg_DTCID << 24 | dtcEventBuilderReg_Mode << 16 |
			//     dtcEventBuilderReg_PartitionID << 8 | dtcEventBuilderReg_MACIndex;
			// __FE_COUTV__(dtcEventBuilderReg_DTCInfo);
			// registerWrite(0x9154, dtcEventBuilderReg_DTCInfo);

			// Register x9158 is #Num EVB Buffers[22-16], EVB Start Node [14-8], Num Nodes
			// [6-0]
			thisDTC_->SetEVBBufferInfo(dtcEventBuilderReg_NumBuff,
				dtcEventBuilderReg_StartNode,
				dtcEventBuilderReg_NumNodes);
			// dtcEventBuilderReg_Configuration = dtcEventBuilderReg_NumBuff << 16 |
			//                                    dtcEventBuilderReg_StartNode << 8 |
			//                                    dtcEventBuilderReg_NumNodes;
			// __FE_COUTV__(dtcEventBuilderReg_Configuration);
			// registerWrite(0x9158, dtcEventBuilderReg_Configuration);
		}
		catch(...)
		{
			__FE_COUT_INFO__ << "Ignoring missing event building configuration values."
								<< __E__;
		}
		
		// These registers are needed for the EVB, but I need to check their meaning
		// registerWrite(0x9100, 0x800404);
		thisDTC_->EnableAutogenDRP(); //bit 23
		thisDTC_->SetSequenceNumberDisable(); //bit 10

		// registerWrite(0x92c0, 0x0);
		thisDTC_->ClearEventModeTableEnable();
		thisDTC_->SetEventModeLookupByteSelect(0);

		// registerWrite(0x9114, 0xc1c1);
		thisDTC_->EnableLink(DTCLib::DTC_Link_EVB);

		// registerWrite(0x96C8, 0x555555D5); 	//10G configurable preamble world 
		// registerWrite(0x96CC, 0x78555555);	//10G configurable idle world 

		__FE_COUT__ << "Setup EVB parameters done." << __E__;
	}
	else
		__FE_COUT__ << "Do nothing while other configurable entities finish..." << __E__;
	

}  // end configureEventBuildingMode()

//==============================================================================
void DTCFrontEndInterface::configureLoopbackMode(int step)
{
	if(step == -1)
		step = getIterationIndex();

	__FE_COUT_INFO__ << "configureLoopbackMode() " << step << __E__;

	if(emulate_cfo_) //when no CFO, what is loopback mode?
	{
		__FE_SS__ << "There is no CFO! Loopback Mode is invalid." << __E__;
		__SS_THROW__;
	}

	if(step < CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX)
	{
		__FE_COUT__ << "Do nothing while CFO configures for timing chain." << __E__;
		indicateIterationWork();
	}
	else if(step < CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX + 
		CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	{
		configureForTimingChain(getIterationIndex() - 
			CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX /* start case index!! */);
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX + 
		CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	{
		__FE_COUT__ << "Do nothing while CFO reset its Tx..." << __E__;
		// indicateIterationWork();
	}
	else
		__FE_COUT__ << "Do nothing while other configurable entities finish..." << __E__;

}  // end configureLoopbackMode()

//==============================================================================
void DTCFrontEndInterface::configureForTimingChain(int step)
{
	__FE_COUT_INFO__ << "configureForTimingChain() " << step << __E__;
	
	//Jun/18/2023 14:00 raw-data: 0x23061814
	switch(step)
	{
		case 0:
			thisDTC_->ResetDTC();
			thisDTC_->ClearDTCControlRegister();
			indicateIterationWork();
			break;
		case 1:
			{
	
				if(configure_clock_)
				{
					uint32_t select      = 0;
					try
					{
						select = getSelfNode()
									.getNode("JitterAttenuatorInputSource")
									.getValue<uint32_t>();
					}
					catch(...)
					{
						__FE_COUT__ << "Defaulting Jitter Attenuator Input Source to select = "
									<< select << __E__;
					}

					__FE_COUTV__(select);
					//For DTC - 0 ==> CFO Control Link
					//For DTC - 1 ==> RTF copper clock
					//For DTC - 2 ==> FPGA FMC
					thisDTC_->SetJitterAttenuatorSelect(select);
				}
				else
					__FE_COUT_INFO__ << "Skipping configure clock." << __E__;
			}
			indicateIterationWork();
			break;
		case 2:
			// __FE_COUT__ << "DTC reset links" << __E__;
			// thisDTC_->ResetSERDESTX(DTCLib::DTC_Link_ID::DTC_Link_ALL);
			// thisDTC_->ResetSERDESRX(DTCLib::DTC_Link_ID::DTC_Link_ALL);
			// thisDTC_->ResetSERDES(DTCLib::DTC_Link_ID::DTC_Link_ALL);

			// check if any ROCs should be DTC-hardware emulated ROCs
			{
				std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
					Configurable::getSelfNode().getNode("LinkToROCGroupTable").getChildren();

				int dtcHwEmulateROCmask = 0;
				for(auto& roc : rocChildren)
				{
					bool enabled = roc.second.getNode("EmulateInDTCHardware").getValue<bool>();

					if(enabled)
					{
						int linkID = roc.second.getNode("linkID").getValue<int>();
						__FE_COUT__ << "roc uid '" << roc.first << "' at link=" << linkID
									<< " is DTC-hardware emulated!" << __E__;
						dtcHwEmulateROCmask |= (1 << linkID);
					}
				}

				__FE_COUT__ << "Writing DTC-hardware emulation mask: 0x" << std::hex
							<< dtcHwEmulateROCmask << std::dec << __E__;
				thisDTC_->SetROCEmulatorMask(dtcHwEmulateROCmask);
				// registerWrite(0x9110, dtcHwEmulateROCmask);
				__FE_COUT__ << "End check for DTC-hardware emulated ROCs." << __E__;
			}  // end check if any ROCs should be DTC-hardware emulated ROCs
			
			//enable ROC links w/CFO link
			__FE_COUT__ << "Enabling/Disabling DTC links with ROC mask = " << roc_mask_ << __E__;
			thisDTC_->EnableLink(DTCLib::DTC_Link_CFO);
			thisDTC_->DisableLink(DTCLib::DTC_Link_EVB);
			for(size_t i=0;i<DTCLib::DTC_Links.size();++i)
				if((roc_mask_ >> i) & 1)
					thisDTC_->EnableLink(DTCLib::DTC_Links[i]);
				else
					thisDTC_->DisableLink(DTCLib::DTC_Links[i]);

			thisDTC_->SetROCDCSResponseTimer(1000); //set ROC DCS timeout (if 0, the DTC will hang forever when a ROC does not respond)
			thisDTC_->EnableDCSReception();
			break;
		default:
			__FE_COUT__ << "Do nothing while other configurable entities finish..." << __E__;
	}	


}  // end configureForTimingChain()

//==============================================================================
void DTCFrontEndInterface::halt(void)
{
	// if(regWriteMonitorStream_.is_open())
	// {
	// 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0)
	// 	                       << ", \t ---------- Halting..."
	// 	                       << "\n";
	// 	regWriteMonitorStream_.flush();
	// }

	__FE_COUT__ << "Halting..." << __E__;

	for(auto& roc : rocs_)  // halt "as usual"
	{
		roc.second->halt();
	}

	rocs_.clear();

	__FE_COUT__ << "Halted." << __E__;

	// if(device_name_ == "DTC8")
	// {
	// 	__FE_COUT__ << "Forcing abort" << __E__;
	// 	abort();
	// }

	//	__FE_COUT__ << "HALT: DTC status" << __E__;
	//	readStatus();

	// if(runDataFile_.is_open())
	// 	runDataFile_.close();
}  // end halt()

//==============================================================================
void DTCFrontEndInterface::pause(void)
{
	// if(regWriteMonitorStream_.is_open())
	// {
	// 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0)
	// 	                       << ", \t ---------- Pausing..."
	// 	                       << "\n";
	// 	regWriteMonitorStream_.flush();
	// }

	__FE_COUT__ << "Pausing..." << __E__;
	for(auto& roc : rocs_)  // pause "as usual"
	{
		roc.second->pause();
	}

	//	__FE_COUT__ << "PAUSE: DTC status" << __E__;
	//	readStatus();

	__FE_COUT__ << "Paused." << __E__;
}

//==============================================================================
void DTCFrontEndInterface::resume(void)
{
	// if(regWriteMonitorStream_.is_open())
	// {
	// 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0)
	// 	                       << ", \t ---------- Resuming..."
	// 	                       << "\n";
	// 	regWriteMonitorStream_.flush();
	// }

	__FE_COUT__ << "Resuming..." << __E__;
	for(auto& roc : rocs_)  // resume "as usual"
	{
		roc.second->resume();
	}

	//	__FE_COUT__ << "RESUME: DTC status" << __E__;
	//	readStatus();

	__FE_COUT__ << "Resumed." << __E__;
}  // end resume()

//==============================================================================
void DTCFrontEndInterface::start(std::string runNumber)
{
	__FE_COUTV__(operatingMode_);
	__FE_COUTV__(emulatorMode_);

	if(operatingMode_ == "HardwareDevMode")
	{
		__FE_COUT_INFO__ << "Starting for hardware development mode!" << __E__;
	}
	else if(operatingMode_ == "EventBuildingMode")
	{
		__FE_COUT_INFO__ << "Starting for Event Building mode!" << __E__;
	}
	else if(operatingMode_ == "LoopbackMode")
	{
		__FE_COUT_INFO__ << "Starting for Loopback mode!" << __E__;
	}
	else
	{
		__FE_SS__ << "Unknown system operating mode: " << operatingMode_ << __E__
		          << " Please specify a valid operating mode in the 'Mu2eGlobalsTable.'"
		          << __E__;
		__FE_SS_THROW__;
	}

	return;

	// /////////////////////////////
	// /////////////////////////////
	// ///////////////////////////// old start
	// ///////////////////////////// old start
	// ///////////////////////////// old start
	// ///////////////////////////// old start
	// /////////////////////////////
	// /////////////////////////////

	// // if(regWriteMonitorStream_.is_open())
	// // {
	// // 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0)
	// // 	                       << ", \t ---------- Starting..."
	// // 	                       << "\n";
	// // 	regWriteMonitorStream_.flush();
	// // }

	// // open a file for this run number to write data to, if it hasn't been opened yet
	// // define a data file
	// if(!artdaqMode_)
	// {
	// 	std::string runDataFilename = std::string(__ENV__("OTSDAQ_DATA")) + "/RunData_" +
	// 	                              runNumber + "_" + device_name_ + ".dat";

	// 	__FE_COUTV__(runDataFilename);
	// 	if(runDataFile_.is_open())
	// 	{
	// 		__SS__
	// 		    << "File was left open! How was this possible -  open data file RunData: "
	// 		    << runDataFilename << __E__;
	// 		__SS_THROW__;
	// 	}

	// 	runDataFile_.open(runDataFilename, std::ios::out | std::ios::app);

	// 	if(runDataFile_.fail())
	// 	{
	// 		__SS__ << "FAILED to open data file RunData: " << runDataFilename << __E__;
	// 		__SS_THROW__;
	// 	}

	// }  // end local run file creation

	// if(emulatorMode_)
	// {
	// 	__FE_COUT__ << "Emulator DTC starting... # of ROCs = " << rocs_.size() << __E__;
	// 	for(auto& roc : rocs_)
	// 	{
	// 		__FE_COUT__ << "Starting ROC ";
	// 		roc.second->start(runNumber);
	// 		__FE_COUT__ << "Done starting ROC";
	// 	}
	// 	return;
	// }

	// int numberOfLoopbacks =
	//     getConfigurationManager()
	//         ->getNode("/Mu2eGlobalsTable/SyncDemoConfig/NumberOfLoopbacks")
	//         .getValue<unsigned int>();

	// __FE_COUTV__(numberOfLoopbacks);

	// // int stopIndex = getIterationIndex();

	// if(numberOfLoopbacks == 0)
	// {
	// 	for(auto& roc : rocs_)  // start "as usual"
	// 	{
	// 		roc.second->start(runNumber);
	// 	}
	// 	return;
	// }

	// __MCOUT_INFO__(device_name_ << " Ignoring loopback for now..." << __E__);
	// return;  // for now ignore loopback mode

	// const int numberOfChains = 1;
	// // int       link[numberOfChains] = {0};

	// const int numberOfDTCsPerChain = 1;

	// const int numberOfROCsPerDTC = 1;  // assume these are ROC0 and ROC1

	// // To do loopbacks on all CFOs, first have to setup all DTCs, then the CFO
	// // (this method) work per iteration.  Loop back done on all chains (in this
	// // method), assuming the following order: i  DTC0  DTC1  ...  DTCN 0  ROC0
	// // none  ...  none 1  ROC1  none  ...  none 2  none  ROC0  ...  none 3  none
	// // ROC1  ...  none
	// // ...
	// // N-1  none  none  ...  ROC0
	// // N  none  none  ...  ROC1

	// int totalNumberOfMeasurements =
	//     numberOfChains * numberOfDTCsPerChain * numberOfROCsPerDTC;

	// int loopbackIndex = getIterationIndex();

	// if(loopbackIndex == 0)  // start
	// {
	// 	initial_9100_ = registerRead(0x9100);
	// 	initial_9114_ = registerRead(0x9114);
	// 	indicateIterationWork();
	// 	return;
	// }

	// if(loopbackIndex > totalNumberOfMeasurements)  // finish
	// {
	// 	__MCOUT_INFO__(device_name_ << " loopback DONE" << __E__);

	// 	if(checkLinkStatus() == 1)
	// 	{
	// 		//      __MCOUT_INFO__(device_name_ << " links OK 0x" << std::hex <<
	// 		//      registerRead(0x9140) << std::dec << __E__);
	// 	}
	// 	else
	// 	{
	// 		//      __MCOUT_INFO__(device_name_ << " links not OK 0x" << std::hex <<
	// 		//      registerRead(0x9140) << std::dec << __E__);
	// 	}

	// 	if(0)
	// 		for(auto& roc : rocs_)
	// 		{
	// 			__MCOUT_INFO__(".... ROC" << roc.second->getLinkID() << "-DTC link lost "
	// 			                          << roc.second->readDTCLinkLossCounter()
	// 			                          << " times");
	// 		}

	// 	registerWrite(0x9100, initial_9100_);
	// 	registerWrite(0x9114, initial_9114_);

	// 	return;
	// }

	// //=========== Perform loopback=============

	// // where are we in the procedure?
	// unsigned int activeROC = (loopbackIndex - 1) % numberOfROCsPerDTC;

	// int activeDTC = -1;

	// for(int nDTC = 0; nDTC < numberOfDTCsPerChain; nDTC++)
	// {
	// 	if((loopbackIndex - 1) >= (nDTC * numberOfROCsPerDTC) &&
	// 	   (loopbackIndex - 1) < ((nDTC + 1) * numberOfROCsPerDTC))
	// 	{
	// 		activeDTC = nDTC;
	// 	}
	// }

	// // __FE_COUT__ << "loopback index = " << loopbackIndex
	// // 	<< " activeDTC = " << activeDTC
	// //	 	<< " activeROC = " << activeROC
	// //	 	<< __E__;

	// if(activeDTC == dtc_location_in_chain_)
	// {
	// 	__FE_COUT__ << "DTC" << activeDTC << "loopback mode ENABLE" << __E__;
	// 	int dataInReg   = registerRead(0x9100);
	// 	int dataToWrite = dataInReg & 0xefffffff;  // bit 28 = 0
	// 	registerWrite(0x9100, dataToWrite);
	// }
	// else
	// {
	// 	// this DTC is lower in chain than the one being looped.  Pass the loopback
	// 	// signal through
	// 	__FE_COUT__ << "active DTC = " << activeDTC
	// 	            << " is NOT this DTC = " << dtc_location_in_chain_
	// 	            << "... pass signal through" << __E__;

	// 	int dataInReg   = registerRead(0x9100);
	// 	int dataToWrite = dataInReg | 0x10000000;  // bit 28 = 1
	// 	registerWrite(0x9100, dataToWrite);
	// }

	// int ROCToEnable =
	//     0x00004040 |
	//     (0x101 << activeROC);  // enables TX and Rx to CFO (bit 6) and appropriate ROC
	// __FE_COUT__ << "enable ROC " << activeROC << " --> 0x" << std::hex << ROCToEnable
	//             << std::dec << __E__;

	// registerWrite(0x9114, ROCToEnable);

	// indicateIterationWork();  // FIXME -- go back to including the ROC (could not 'read'
	//                           // for some reason)
	// return;
	// // Re-align the links for the activeROC
	// for(auto& roc : rocs_)
	// {
	// 	if(roc.second->getLinkID() == activeROC)
	// 	{
	// 		__FE_COUT__ << "... ROC realign link... " << __E__;
	// 		roc.second->writeRegister(22, 0);
	// 		roc.second->writeRegister(22, 1);
	// 	}
	// }

	// indicateIterationWork();
	// return;
}  // end start()

//==============================================================================
void DTCFrontEndInterface::stop(void)
{
	// if(regWriteMonitorStream_.is_open())
	// {
	// 	regWriteMonitorStream_ << "---------- Stopping..."
	// 	                       << "\n";
	// 	regWriteMonitorStream_.flush();
	// }

	// must close data file on each possible return with call 'if(runDataFile_.is_open())
	// runDataFile_.close();'

	if(emulatorMode_)
	{
		__FE_COUT__ << "Emulator DTC stopping... # of ROCs = " << rocs_.size() << __E__;
		for(auto& roc : rocs_)
			roc.second->stop();

		// if(runDataFile_.is_open())
		// 	runDataFile_.close();
		// return;
	}

	int numberOfCAPTANPulses =
	    getConfigurationManager()
	        ->getNode("/Mu2eGlobalsTable/SyncDemoConfig/NumberOfCAPTANPulses")
	        .getValue<unsigned int>();

	__FE_COUTV__(numberOfCAPTANPulses);

	// int stopIndex = getIterationIndex();

	if(numberOfCAPTANPulses == 0)
	{
		for(auto& roc : rocs_)  // stop "as usual"
		{
			roc.second->stop();
		}
		// if(runDataFile_.is_open())
		// 	runDataFile_.close();
		// return;
	}

	// if(stopIndex == 0)
	// {
	// 	//		int i = 0;
	// 	for(auto& roc : rocs_)
	// 	{
	// 		// re-align link
	// 		roc.second->writeRegister(22, 0);
	// 		roc.second->writeRegister(22, 1);

	// 		// std::stringstream filename;
	// 		// filename << "/home/mu2edaq/sync_demo/ots/" << device_name_ << "_ROC"
	// 		//          << roc.second->getLinkID() << "data.txt";
	// 		// std::string filenamestring = filename.str();
	// 		// datafile_[i].open(filenamestring);
	// 		//	i++;
	// 	}
	// }

	// if(stopIndex > numberOfCAPTANPulses)
	// {
	// 	int i = 0;
	// 	for(auto& roc : rocs_)
	// 	{
	// 		__MCOUT_INFO__(".... ROC" << roc.second->getLinkID() << "-DTC link lost "
	// 		                          << roc.second->readDTCLinkLossCounter()
	// 		                          << " times");
	// 		datafile_[i].close();
	// 		i++;
	// 	}
	// 	if(runDataFile_.is_open())
	// 		runDataFile_.close();
	// 	return;
	// }

	// int i = 0;
	// __FE_COUT__ << "Entering read timestamp loop..." << __E__;
	// for(auto& roc : rocs_)
	// {
	// 	int timestamp_data = roc.second->readTimestamp();

	// 	__FE_COUT__ << "Read " << stopIndex << " -> " << device_name_ << " timestamp "
	// 	            << timestamp_data << __E__;

	// 	datafile_[i] << stopIndex << " " << timestamp_data << std::endl;
	// 	i++;
	// }

	// indicateIterationWork();
	// return;
}  // end stop()

//==============================================================================
// return true to keep running
bool DTCFrontEndInterface::running(void)
{
	__FE_COUTV__(operatingMode_);
	__FE_COUTV__(emulatorMode_);

	if(operatingMode_ == "HardwareDevMode")
	{
		__FE_COUT_INFO__ << "Running for hardware development mode!" << __E__;
	}
	else if(operatingMode_ == "EventBuildingMode")
	{
		__FE_COUT_INFO__ << "Running for Event Building mode!" << __E__;
	}
	else if(operatingMode_ == "LoopbackMode")
	{
		__FE_COUT_INFO__ << "Running for Loopback mode!" << __E__;
	}
	else
	{
		__FE_SS__ << "Unknown system operating mode: " << operatingMode_ << __E__
		          << " Please specify a valid operating mode in the 'Mu2eGlobalsTable.'"
		          << __E__;
		__FE_SS_THROW__;
	}

	return false;

	// /////////////////////////////
	// /////////////////////////////
	// ///////////////////////////// old running
	// ///////////////////////////// old running
	// ///////////////////////////// old running
	// ///////////////////////////// old running
	// /////////////////////////////
	// /////////////////////////////

	// //   if(artdaqMode_) {
	// //     __FE_COUT__ << "Running in artdaqmode" << __E__;
	// return true;
	// //   }
	// if(emulatorMode_)
	// {
	// 	__FE_COUT__ << "Emulator DTC running... # of ROCs = " << rocs_.size() << __E__;
	// 	bool stillRunning = false;
	// 	for(auto& roc : rocs_)
	// 		stillRunning = stillRunning || roc.second->running();

	// 	return stillRunning;
	// }

	// // first setup DTC and CFO.  This is stolen from "getheartbeatanddatarequest"

	// //	auto start = DTCLib::DTC_EventWindowTag(static_cast<uint64_t>(timestampStart));

	// //    std::time_t current_time;

	// bool incrementTimestamp = true;

	// uint32_t cfodelay = 10000;  // have no idea what this is, but 1000 didn't work (don't
	//                             // know if 10000 works, either)
	// int          requestsAhead  = 0;
	// unsigned int number         = -1;  // largest number of events?
	// unsigned int timestampStart = 0;

	// auto device = thisDTC_->GetDevice();
	// // auto initTime = device->GetDeviceTime();
	// device->ResetDeviceTime();
	// // auto afterInit = std::chrono::steady_clock::now();

	// if(emulate_cfo_ == 1)
	// {
	// 	registerWrite(
	// 	    0x9100, 0x40008404);  // bit 30 = CFO emulation enable, bit 15 = CFO
	// 	                          // emulation mode, bit 2 = DCS enable
	// 	                          // bit 10 turns off retry which isn't working right now
	// 	sleep(1);

	// 	// set number of null heartbeats
	// 	// registerWrite(0x91BC, 0x0);
	// 	registerWrite(0x91BC, 0x10);  // new incantaton from Rick K. 12/18/2019
	// 	//	  sleep(1);

	// 	// # Send data
	// 	// #disable 40mhz marker
	// 	registerWrite(0x91f4, 0x0);
	// 	//	  sleep(1);

	// 	// #set num dtcs
	// 	registerWrite(0x9158, 0x1);
	// 	//	  sleep(1);

	// 	bool     useSWCFOEmulator = true;
	// 	uint16_t debugPacketCount = 0;
	// 	auto     debugType        = DTCLib::DTC_DebugType_SpecialSequence;
	// 	bool     stickyDebugType  = true;
	// 	bool     quiet            = false;
	// 	bool     asyncRR          = false;
	// 	bool     forceNoDebugMode = true;

	// 	DTCLib::DTCSoftwareCFO* EmulatedCFO_ =
	// 	    new DTCLib::DTCSoftwareCFO(thisDTC_,
	// 	                               useSWCFOEmulator,
	// 	                               debugPacketCount,
	// 	                               debugType,
	// 	                               stickyDebugType,
	// 	                               quiet,
	// 	                               asyncRR,
	// 	                               forceNoDebugMode);

	// 	EmulatedCFO_->SendRequestsForRange(
	// 	    number,
	// 	    DTCLib::DTC_EventWindowTag(static_cast<uint64_t>(timestampStart)),
	// 	    incrementTimestamp,
	// 	    cfodelay,
	// 	    requestsAhead);

	// 	// auto readoutRequestTime = device->GetDeviceTime();
	// 	device->ResetDeviceTime();
	// 	// auto afterRequests = std::chrono::steady_clock::now();
	// }

	// while(WorkLoop::continueWorkLoop_)
	// {
	// 	registerWrite(
	// 	    0x9100, 0x40008404);  // bit 30 = CFO emulation enable, bit 15 = CFO
	// 	                          // emulation mode, bit 2 = DCS enable
	// 	                          // bit 10 turns off retry which isn't working right now
	// 	sleep(1);

	// 	// set number of null heartbeats
	// 	// registerWrite(0x91BC, 0x0);
	// 	registerWrite(0x91BC, 0x10);  // new incantaton from Rick K. 12/18/2019
	// 	//	  sleep(1);

	// 	// # Send data
	// 	// #disable 40mhz marker
	// 	registerWrite(0x91f4, 0x0);
	// 	//	  sleep(1);

	// 	// #set num dtcs
	// 	registerWrite(0x9158, 0x1);
	// 	//	  sleep(1);

	// 	bool     useSWCFOEmulator = true;
	// 	uint16_t debugPacketCount = 0;
	// 	auto     debugType        = DTCLib::DTC_DebugType_SpecialSequence;
	// 	bool     stickyDebugType  = true;
	// 	bool     quiet            = false;
	// 	bool     asyncRR          = false;
	// 	bool     forceNoDebugMode = true;

	// 	DTCLib::DTCSoftwareCFO* EmulatedCFO_ =
	// 	    new DTCLib::DTCSoftwareCFO(thisDTC_,
	// 	                               useSWCFOEmulator,
	// 	                               debugPacketCount,
	// 	                               debugType,
	// 	                               stickyDebugType,
	// 	                               quiet,
	// 	                               asyncRR,
	// 	                               forceNoDebugMode);

	// 	EmulatedCFO_->SendRequestsForRange(
	// 	    number,
	// 	    DTCLib::DTC_EventWindowTag(static_cast<uint64_t>(timestampStart)),
	// 	    incrementTimestamp,
	// 	    cfodelay,
	// 	    requestsAhead);

	// 	// auto readoutRequestTime = device->GetDeviceTime();
	// 	device->ResetDeviceTime();
	// 	// auto afterRequests = std::chrono::steady_clock::now();
	// }

	// while(WorkLoop::continueWorkLoop_)
	// {
	// 	for(auto& roc : rocs_)
	// 	{
	// 		roc.second->running();
	// 	}

	// 	// print out stuff
	// 	unsigned quietCount = 20;
	// 	bool     quiet      = false;

	// 	std::stringstream ostr;
	// 	ostr << std::endl;

	// 	//		std::cout << "Buffer Read " << std::dec << ii << std::endl;
	// 	mu2e_databuff_t* buffer;
	// 	auto             tmo_ms = 1500;
	// 	__FE_COUT__ << "util - before read for DAQ in running";
	// 	auto sts = device->read_data(
	// 	    DTC_DMA_Engine_DAQ, reinterpret_cast<void**>(&buffer), tmo_ms);
	// 	__FE_COUT__ << "util - after read for DAQ in running "
	// 	            << " sts=" << sts << ", buffer=" << (void*)buffer;

	// 	if(sts > 0)
	// 	{
	// 		void* readPtr = &buffer[0];
	// 		auto  bufSize = static_cast<uint16_t>(*static_cast<uint64_t*>(readPtr));
	// 		readPtr       = static_cast<uint8_t*>(readPtr) + 8;

	// 		__FE_COUT__ << "Buffer reports DMA size of " << std::dec << bufSize
	// 		            << " bytes. Device driver reports read of " << sts << " bytes,"
	// 		            << std::endl;

	// 		__FE_COUT__ << "util - bufSize is " << bufSize;
	// 		outputStream.write(static_cast<char*>(readPtr), sts - 8);
	// 		auto maxLine = static_cast<unsigned>(ceil((sts - 8) / 16.0));
	// 		__FE_COUT__ << "maxLine " << maxLine;
	// 		for(unsigned line = 0; line < maxLine; ++line)
	// 		{
	// 			ostr << "0x" << std::hex << std::setw(5) << std::setfill('0') << line
	// 			     << "0: ";
	// 			for(unsigned byte = 0; byte < 8; ++byte)
	// 			{
	// 				if(line * 16 + 2 * byte < sts - 8u)
	// 				{
	// 					auto thisWord =
	// 					    reinterpret_cast<uint16_t*>(buffer)[4 + line * 8 + byte];
	// 					ostr << std::setw(4) << static_cast<int>(thisWord) << " ";
	// 				}
	// 			}

	// 			ostr << std::endl;
	// 			//	std::cout << ostr.str();

	// 			//     __SET_ARG_OUT__("readData", ostr.str());  // write to data file

	// 			// don't write data to the log file, only the data file
	// 			// __FE_COUT__ << ostr.str();

	// 			if(maxLine > quietCount * 2 && quiet && line == (quietCount - 1))
	// 			{
	// 				line =
	// 				    static_cast<unsigned>(ceil((sts - 8) / 16.0)) - (1 + quietCount);
	// 			}
	// 		}
	// 	}
	// 	device->read_release(DTC_DMA_Engine_DAQ, 1);

	// 	ostr << std::endl;

	// 	if(runDataFile_.is_open())
	// 	{
	// 		runDataFile_ << ostr.str();
	// 		runDataFile_.flush();  // flush to disk
	// 	}
	// 	//__FE_COUT__ << ostr.str();

	// 	delete EmulatedCFO_;

	// 	break;
	// }
	// return true;
}  // end running()

//==============================================================================
// rocRead
void DTCFrontEndInterface::ReadROC(__ARGS__)
{
	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;
	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	DTCLib::DTC_Link_ID rocLinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("rocLinkIndex", uint8_t));
	uint8_t address = __GET_ARG_IN__("address", uint8_t);
	__FE_COUTV__(rocLinkIndex);
	__FE_COUTV__((unsigned int)address);

	uint16_t readData = -999;

	for(auto& roc : rocs_)
	{
		__FE_COUT__ << "Found link ID " << roc.second->getLinkID() << " looking for "
		            << rocLinkIndex << __E__;

		if(rocLinkIndex == roc.second->getLinkID())
		{
			// readData = roc.second->readRegister(address);

			readData = thisDTC_->ReadROCRegister(rocLinkIndex, address, 100);

			char readDataStr[100];
			sprintf(readDataStr, "0x%X", readData);
			__SET_ARG_OUT__("readData", readDataStr);
			//__SET_ARG_OUT__("readData", readData);

			// for(auto &argOut:argsOut)
			__FE_COUT__ << "readData"
			            << ": " << std::hex << readData << std::dec << __E__;
			__FE_COUT__ << "End of Data";
			return;
		}
	}

	__FE_SS__ << "ROC link ID " << rocLinkIndex << " not found!" << __E__;
	__FE_SS_THROW__;

}  // end ReadROC()

//==============================================================================
// DTCStatus
//	FEMacro 'DTCStatus' generated, Oct-22-2018 03:16:46, by 'admin' using
// MacroMaker. 	Macro Notes:
void DTCFrontEndInterface::WriteROC(__ARGS__)
{
	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;
	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	DTCLib::DTC_Link_ID rocLinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("rocLinkIndex", uint8_t));
	uint8_t  address   = __GET_ARG_IN__("address", uint8_t);
	uint16_t writeData = __GET_ARG_IN__("writeData", uint16_t);

	__FE_COUTV__(rocLinkIndex);
	__FE_COUTV__((unsigned int)address);
	__FE_COUTV__(writeData);

	__FE_COUT__ << "ROCs size = " << rocs_.size() << __E__;

	for(auto& roc : rocs_)
	{
		__FE_COUT__ << "Found link ID " << roc.second->getLinkID() << " looking for "
		            << rocLinkIndex << __E__;

		if(rocLinkIndex == roc.second->getLinkID())
		{
			roc.second->writeRegister(address, writeData);

			for(auto& argOut : argsOut)
				__FE_COUT__ << argOut.first << ": " << argOut.second << __E__;

			return;
		}
	}

	__FE_SS__ << "ROC link ID " << rocLinkIndex << " not found!" << __E__;
	__FE_SS_THROW__;
}  // end WriteROC()

//==============================================================================
void DTCFrontEndInterface::WriteExternalROCRegister(__ARGS__)
{
	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;
	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	// macro commands section

	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;

	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	DTCLib::DTC_Link_ID rocLinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("rocLinkIndex", uint8_t));
	uint8_t  address   = __GET_ARG_IN__("address", uint8_t);
	uint16_t writeData = __GET_ARG_IN__("writeData", uint16_t);
	uint8_t  block     = __GET_ARG_IN__("block", uint8_t);
	__FE_COUTV__(rocLinkIndex);
	__FE_COUT__ << "block = " << std::dec << (unsigned int)block << __E__;
	__FE_COUT__ << "address = 0x" << std::hex << (unsigned int)address << std::dec
	            << __E__;
	__FE_COUT__ << "writeData = 0x" << std::hex << writeData << std::dec << __E__;

	bool acknowledge_request = false;

	thisDTC_->WriteExtROCRegister(
	    rocLinkIndex, block, address, writeData, acknowledge_request, 0);

	for(auto& argOut : argsOut)
		__FE_COUT__ << argOut.first << ": " << argOut.second << __E__;
}  // end WriteExternalROCRegister()

//==============================================================================
void DTCFrontEndInterface::ReadExternalROCRegister(__ARGS__)
{
	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;
	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	DTCLib::DTC_Link_ID rocLinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("rocLinkIndex", uint8_t));
	uint8_t address = __GET_ARG_IN__("address", uint8_t);
	uint8_t block   = __GET_ARG_IN__("block", uint8_t);
	__FE_COUTV__(rocLinkIndex);
	__FE_COUT__ << "block = " << std::dec << (unsigned int)block << __E__;
	__FE_COUT__ << "address = 0x" << std::hex << (unsigned int)address << std::dec
	            << __E__;

	// bool acknowledge_request = false;

	for(auto& roc : rocs_)
	{
		__FE_COUT__ << "At ROC link ID " << roc.second->getLinkID() << ", looking for "
		            << rocLinkIndex << __E__;

		if(rocLinkIndex == roc.second->getLinkID())
		{
			uint16_t readData;

			readData = thisDTC_->ReadExtROCRegister(rocLinkIndex, block, address);

			std::string readDataString = "";
			readDataString = BinaryStringMacros::binaryNumberToHexString(readData);

			// StringMacros::vectorToString(readData);

			__SET_ARG_OUT__("readData", readDataString);

			// for(auto &argOut:argsOut)
			__FE_COUT__ << "readData"
			            << ": " << readDataString << __E__;
			return;
		}
	}

	__FE_SS__ << "ROC link ID " << rocLinkIndex << " not found!" << __E__;
	__FE_SS_THROW__;
}  // end ReadExternalROCRegister()

//========================================================================
void DTCFrontEndInterface::ROCBlockRead(__ARGS__)
{
	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;
	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	// macro commands section
	__FE_COUT__ << "# of input args = " << argsIn.size() << __E__;
	__FE_COUT__ << "# of output args = " << argsOut.size() << __E__;

	for(auto& argIn : argsIn)
		__FE_COUT__ << argIn.first << ": " << argIn.second << __E__;

	DTCLib::DTC_Link_ID rocLinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("rocLinkIndex", uint8_t));
	uint16_t address          = __GET_ARG_IN__("address", uint16_t);
	uint16_t wordCount        = __GET_ARG_IN__("numberOfWords", uint16_t);
	bool     incrementAddress = __GET_ARG_IN__("incrementAddress", bool);

	__FE_COUTV__(rocLinkIndex);
	__FE_COUT__ << "address = 0x" << std::hex << (unsigned int)address << std::dec
	            << __E__;
	__FE_COUT__ << "numberOfWords = " << std::dec << (unsigned int)wordCount << __E__;
	__FE_COUTV__(incrementAddress);

	for(auto& roc : rocs_)
	{
		__FE_COUT__ << "At ROC link ID " << roc.second->getLinkID() << ", looking for "
		            << rocLinkIndex << __E__;

		if(rocLinkIndex == roc.second->getLinkID())
		{
			std::vector<uint16_t> readData;

			roc.second->readBlock(readData, address, wordCount, incrementAddress);

			std::string readDataString = "";
			{
				bool first = true;
				for(const auto& data : readData)
				{
					if(!first)
						readDataString += ", ";
					else
						first = false;
					readDataString += BinaryStringMacros::binaryNumberToHexString(data);
				}
			}
			// StringMacros::vectorToString(readData);

			__SET_ARG_OUT__("readData", readDataString);

			// for(auto &argOut:argsOut)
			__FE_COUT__ << "readData"
			            << ": " << readDataString << __E__;
			return;
		}
	}

	__FE_SS__ << "ROC link ID " << rocLinkIndex << " not found!" << __E__;
	__FE_SS_THROW__;

}  // end ROCBlockRead()

// //========================================================================
// void DTCFrontEndInterface::DTCHighRateBlockCheck(__ARGS__)
// {
// 	unsigned int linkIndex   = __GET_ARG_IN__("rocLinkIndex", unsigned int);
// 	unsigned int loops       = __GET_ARG_IN__("loops", unsigned int);
// 	unsigned int baseAddress = __GET_ARG_IN__("baseAddress", unsigned int);
// 	unsigned int correctRegisterValue0 =
// 	    __GET_ARG_IN__("correctRegisterValue0", unsigned int);
// 	unsigned int correctRegisterValue1 =
// 	    __GET_ARG_IN__("correctRegisterValue1", unsigned int);

// 	__FE_COUTV__(linkIndex);
// 	__FE_COUTV__(loops);
// 	__FE_COUTV__(baseAddress);
// 	__FE_COUTV__(correctRegisterValue0);
// 	__FE_COUTV__(correctRegisterValue1);

// 	for(auto& roc : rocs_)
// 		if(roc.second->getLinkID() == linkIndex)
// 		{
// 			roc.second->highRateBlockCheck(
// 			    loops, baseAddress, correctRegisterValue0, correctRegisterValue1);
// 			return;
// 		}

// 	__FE_SS__ << "Error! Could not find ROC at link index " << linkIndex << __E__;
// 	__FE_SS_THROW__;

// }  // end DTCHighRateBlockCheck()

// //========================================================================
// void DTCFrontEndInterface::DTCHighRateDCSCheck(__ARGS__)
// {
// 	unsigned int linkIndex   = __GET_ARG_IN__("rocLinkIndex", unsigned int);
// 	unsigned int loops       = __GET_ARG_IN__("loops", unsigned int);
// 	unsigned int baseAddress = __GET_ARG_IN__("baseAddress", unsigned int);
// 	unsigned int correctRegisterValue0 =
// 	    __GET_ARG_IN__("correctRegisterValue0", unsigned int);
// 	unsigned int correctRegisterValue1 =
// 	    __GET_ARG_IN__("correctRegisterValue1", unsigned int);

// 	__FE_COUTV__(linkIndex);
// 	__FE_COUTV__(loops);
// 	__FE_COUTV__(baseAddress);
// 	__FE_COUTV__(correctRegisterValue0);
// 	__FE_COUTV__(correctRegisterValue1);

// 	for(auto& roc : rocs_)
// 		if(roc.second->getLinkID() == linkIndex)
// 		{
// 			roc.second->highRateCheck(
// 			    loops, baseAddress, correctRegisterValue0, correctRegisterValue1);
// 			return;
// 		}

// 	__FE_SS__ << "Error! Could not find ROC at link index " << linkIndex << __E__;
// 	__FE_SS_THROW__;

// }  // end DTCHighRateDCSCheck()

// //========================================================================
// void DTCFrontEndInterface::DTCSendHeartbeatAndDataRequest(__ARGS__)
// {
// 	unsigned int number           = __GET_ARG_IN__("numberOfRequests", unsigned int);
// 	unsigned int timestampStart   = __GET_ARG_IN__("timestampStart", unsigned int);
// 	bool         useSWCFOEmulator = __GET_ARG_IN__("useSWCFOEmulator", bool);
// 	unsigned int rocMask          = __GET_ARG_IN__("rocMask", unsigned int);

// 	//	auto start = DTCLib::DTC_EventWindowTag(static_cast<uint64_t>(timestampStart));

// 	bool     incrementTimestamp = true;
// 	uint32_t cfodelay = 20000;  // have no idea what this is, but 1000 didn't work (don't
// 	                            // know if 10000 works, either)
// 	int requestsAhead    = 0;
// 	int heartbeatPackets = 32;

// 	__FE_COUTV__(number);
// 	__FE_COUTV__(timestampStart);
// 	__FE_COUTV__(useSWCFOEmulator);
// 	__FE_COUTV__(rocMask);

// 	if(thisDTC_)
// 		delete thisDTC_;
// 	thisDTC_    = new DTCLib::DTC(DTCLib::DTC_SimMode_NoCFO, deviceIndex_, rocMask, "");
// 	auto device = thisDTC_->GetDevice();

// 	// auto initTime = device->GetDeviceTime();
// 	device->ResetDeviceTime();
// 	// auto afterInit = std::chrono::steady_clock::now();

// 	if(emulate_cfo_ == 1)
// 	{
// 		thisDTC_->SetSequenceNumberDisable();  // Set 9100 bit 10
// 		// registerWrite(0x9100, 0x40808404);  // bit 30 = CFO emulation enable, bit 15 =
// 		// CFO
// 		//  emulation mode, bit 2 = DCS enable
// 		//  bit 10 turns off retry which isn't working right now
// 		//	sleep(1);

// 		// set number of null heartbeats
// 		// registerWrite(0x91BC, 0x0);
// 		//	registerWrite(0x91BC, 0x10);  // new incantaton from Rick K. 12/18/2019
// 		//	  sleep(1);

// 		// # Send data
// 		// #disable 40mhz marker
// 		thisDTC_->SetCFOEmulation40MHzMarkerInterval(0);
// 		// registerWrite(0x91f4, 0x0);
// 		//	  sleep(1);

// 		// #set num dtcs
// 		thisDTC_->SetEVBNumberOfDestinationNodes(1);
// 		// registerWrite(0x9158, 0x1);
// 		//	  sleep(1);

// 		uint16_t debugPacketCount = 0;
// 		auto     debugType        = DTCLib::DTC_DebugType_SpecialSequence;
// 		bool     stickyDebugType  = true;
// 		bool     quiet            = false;
// 		bool     asyncRR          = false;
// 		bool     forceNoDebugMode = true;

// 		DTCLib::DTCSoftwareCFO* EmulatedCFO_ =
// 		    new DTCLib::DTCSoftwareCFO(thisDTC_,
// 		                               useSWCFOEmulator,
// 		                               debugPacketCount,
// 		                               debugType,
// 		                               stickyDebugType,
// 		                               quiet,
// 		                               asyncRR,
// 		                               forceNoDebugMode);

// 		EmulatedCFO_->SendRequestsForRange(
// 		    number,
// 		    DTCLib::DTC_EventWindowTag(static_cast<uint64_t>(timestampStart)),
// 		    incrementTimestamp,
// 		    cfodelay,
// 		    requestsAhead,
// 		    heartbeatPackets);

// 		// auto readoutRequestTime = device->GetDeviceTime();
// 		device->ResetDeviceTime();
// 		// auto afterRequests = std::chrono::steady_clock::now();

// 		// print out stuff
// 		unsigned quietCount = 20;
// 		quiet               = false;

// 		std::stringstream ostr;
// 		ostr << std::endl;

// 		for(unsigned ii = 0; ii < number; ++ii)
// 		{
// 			__FE_COUT__ << "Buffer Read " << std::dec << ii << std::endl;
// 			mu2e_databuff_t* buffer;
// 			auto             tmo_ms = 1500;
// 			__FE_COUT__ << "util - before read for DAQ - ii=" << ii;
// 			auto sts = device->read_data(
// 			    DTC_DMA_Engine_DAQ, reinterpret_cast<void**>(&buffer), tmo_ms);
// 			__FE_COUT__ << "util - after read for DAQ - ii=" << ii << ", sts=" << sts
// 			            << ", buffer=" << (void*)buffer;

// 			if(sts > 0)
// 			{
// 				void* readPtr = &buffer[0];
// 				auto  bufSize = static_cast<uint16_t>(*static_cast<uint64_t*>(readPtr));
// 				readPtr       = static_cast<uint8_t*>(readPtr) + 8;

// 				std::cout << "Buffer reports DMA size of " << std::dec << bufSize
// 				          << " bytes. Device driver reports read of " << sts << " bytes,"
// 				          << std::endl;

// 				std::cout << "util - bufSize is " << bufSize;
// 				//	__SET_ARG_OUT__("bufSize", bufSize);

// 				//	__FE_COUT__ << "bufSize" << bufSize;
// 				outputStream.write(static_cast<char*>(readPtr), sts - 8);

// 				auto maxLine = static_cast<unsigned>(ceil((sts - 8) / 16.0));
// 				std::cout << "maxLine " << maxLine;
// 				for(unsigned line = 0; line < maxLine; ++line)
// 				{
// 					ostr << "0x" << std::hex << std::setw(5) << std::setfill('0') << line
// 					     << "0: ";
// 					for(unsigned byte = 0; byte < 8; ++byte)
// 					{
// 						if(line * 16 + 2 * byte < sts - 8u)
// 						{
// 							auto thisWord =
// 							    reinterpret_cast<uint16_t*>(buffer)[4 + line * 8 + byte];
// 							ostr << std::setw(4) << static_cast<int>(thisWord) << " ";
// 						}
// 					}

// 					ostr << std::endl;
// 					//	std::cout << ostr.str();

// 					//__SET_ARG_OUT__("readData", ostr.str());  // write to data file

// 					__FE_COUT__ << ostr.str();  // write to log file

// 					if(maxLine > quietCount * 2 && quiet && line == (quietCount - 1))
// 					{
// 						line = static_cast<unsigned>(ceil((sts - 8) / 16.0)) -
// 						       (1 + quietCount);
// 					}
// 				}
// 			}
// 			device->read_release(DTC_DMA_Engine_DAQ, 1);
// 		}
// 		ostr << std::endl;

// 		__SET_ARG_OUT__("readData", ostr.str());  // write to data file

// 		__FE_COUT__ << ostr.str();                // write to log file

// 		delete EmulatedCFO_;
// 	}
// 	else
// 	{
// 		__FE_SS__ << "Error! DTC must be in CFOEmulate mode" << __E__;
// 		__FE_SS_THROW__;
// 	}

// }  // end DTCSendHeartbeatAndDataRequest()

//========================================================================
void DTCFrontEndInterface::ResetLossOfLockCounter(__ARGS__)
{
	// write anything to reset
	// 0x93c8 is RX CDR Unlock counter (32-bit)
	thisDTC_->ClearRXCDRUnlockCount(DTCLib::DTC_Link_ID::DTC_Link_CFO);
	// registerWrite(0x93c8, 0);

	// now check
	// uint32_t readData = registerRead(0x93c8);

	// char readDataStr[100];
	// sprintf(readDataStr, "%d", readData);
	// __SET_ARG_OUT__("Upstream Rx Lock Loss Count", readDataStr);
	__SET_ARG_OUT__("Upstream Rx Lock Loss Count", 
		thisDTC_->FormatRXCDRUnlockCountCFOLink());
}  // end ResetLossOfLockCounter()

//========================================================================
void DTCFrontEndInterface::ReadLossOfLockCounter(__ARGS__)
{
	// 0x93c8 is RX CDR Unlock counter (32-bit)
	uint32_t readData = //registerRead(0x93c8);
		thisDTC_->ReadRXCDRUnlockCount(DTCLib::DTC_Link_ID::DTC_Link_CFO);

	char readDataStr[100];
	sprintf(readDataStr, "%d", readData);

	// 0x9140 bit-6 is RX CDR is locked

	bool isUpstreamLocked = 1;
	for(int i = 0; i < 5; ++i) //read 5x for multiple samples in case of instability
	{
		isUpstreamLocked &= thisDTC_->ReadSERDESRXCDRLock(DTCLib::DTC_Link_ID::DTC_Link_CFO);
		// readData = registerRead(0x9140);
		// isUpstreamLocked &=
		//     (readData >> 6) & 1;  //& to force unlocked for any unlocked reading
	}
	//__SET_ARG_OUT__("Upstream Rx CDR Lock Status",isUpstreamLocked?"LOCKED":"Not
	//Locked");

	// 0x9128 bit-6 is RX PLL
	// readData                 = registerRead(0x9128);
	bool isUpstreamPLLLocked = //(readData >> 6) & 1;
		thisDTC_->ReadSERDESPLLLocked(DTCLib::DTC_Link_ID::DTC_Link_CFO);

	// Jitter attenuator has configurable "Free Running" mode
	// LOL == Loss of Lock, LOS == Loss of Signal (4-inputs to jitter attenuator)
	// 0x9308 bit-0 is reset, input select bit-5:4, bit-8 is LOL, bit-11:9 (input LOS)
	// readData          = //registerRead(0x9308);
		
	uint32_t    val   = thisDTC_->ReadJitterAttenuatorSelect().to_ulong();//(readData >> 4) & 3;
	std::string JAsrc = val == 0 ? "Control Link" : (val == 1 ? "RJ45" : "FMC/SFP+");

	__SET_ARG_OUT__(
	    "Upstream Rx Lock Loss Count",
	    std::string(readDataStr) +
	        "... CDR = " + std::string(isUpstreamLocked ? " LOCKED" : " Not Locked") +
	        "... PLL = " + std::string(isUpstreamPLLLocked ? " LOCKED" : " Not Locked") +
	        "... JA = " + JAsrc);

}  // end ReadLossOfLockCounter()

//========================================================================
void DTCFrontEndInterface::GetUpstreamControlLinkStatus(__ARGS__)
{
	// 0x93c8 is RX CDR Unlock counter (32-bit)
	uint32_t readData = //registerRead(0x93c8);
		thisDTC_->ReadRXCDRUnlockCount(DTCLib::DTC_Link_ID::DTC_Link_CFO);

	char readDataStr[100];
	sprintf(readDataStr, "%d", readData);
	__SET_ARG_OUT__("Upstream Rx Lock Loss Count", readDataStr);

	// 0x9140 bit-6 is RX CDR is locked
	bool isUpstreamLocked = 1;
	for(int i = 0; i < 5; ++i)
	{
		isUpstreamLocked &= thisDTC_->ReadSERDESRXCDRLock(DTCLib::DTC_Link_ID::DTC_Link_CFO);
		// readData = registerRead(0x9140);
		// isUpstreamLocked &=
		//     (readData >> 6) & 1;  //& to force unlocked for any unlocked reading
	}
	__SET_ARG_OUT__("Upstream Rx CDR Lock Status",
	                isUpstreamLocked ? "LOCKED" : "Not Locked");

	// 0x9128 bit-6 is RX PLL
	// readData                 = registerRead(0x9128);
	bool isUpstreamPLLLocked = //(readData >> 6) & 1;
		thisDTC_->ReadSERDESPLLLocked(DTCLib::DTC_Link_ID::DTC_Link_CFO);
	__SET_ARG_OUT__("Upstream Rx PLL Lock Status",
	                isUpstreamPLLLocked ? "LOCKED" : "Not Locked");

	__SET_ARG_OUT__("Jitter Attenuator Status",
		thisDTC_->FormatJitterAttenuatorCSR());
	// Jitter attenuator has configurable "Free Running" mode
	// LOL == Loss of Lock, LOS == Loss of Signal (4-inputs to jitter attenuator)
	// 0x9308 bit-0 is reset, input select bit-5:4, bit-8 is LOL, bit-11:9 (input LOS)
	// readData     = registerRead(0x9308);
	// uint32_t val = thisDTC_->ReadJitterAttenuatorReset();// (readData >> 0) & 1;
	// __SET_ARG_OUT__("Jitter Attenuator Reset",
	//                 val ? "Held in RESET" : "OK");  // OK = not in reset
	// val = thisDTC_->ReadJitterAttenuatorSelect().to_ulong();//(readData >> 4) & 3;
	// __SET_ARG_OUT__(
	//     "Jitter Attenuator Input Select",
	//     val == 0 ? "Upstream Control Link Rx Recovered Clock"
	//              : (val == 1 ? "RJ45 Upstream Clock"
	//                          : "Timing Card Selectable (SFP+ or FPGA) Input Clock"));

	// std::stringstream los;
	// val = (readData >> 9) & 7;
	// los << "...below <br><br>Loss-of-Lock: "
	//     << (((readData >> 8) & 1) ? "Not Locked" : "LOCKED");
	// sprintf(readDataStr, "%X", ((readData >> 8) & 0x0FF));
	// los << "<br>  Raw data: 0x" << std::hex << readDataStr << " = "
	//     << ((readData >> 8) & 0x0FF) << std::dec << " ...";
	// los << "<br><br>  Upstream Control Link Rx Recovered Clock ("
	//     << (((val >> 0) & 1) ? "MISSING" : "OK");
	// los << "), \nRJ45 Upstream Rx Clock (" << (((val >> 1) & 1) ? "MISSING" : "OK");
	// los << "), \nTiming Card Selectable, SFP+ or FPGA, Input Clock ("
	//     << (((val >> 2) & 1) ? "MISSING" : "OK");
	// los << ")";
	// __SET_ARG_OUT__("Jitter Attenuator Loss-of-Signal", los.str());

	// 0x9138 Reset Done register
	//	TX reset done bit-7:0,
	//	TX FSM IP reset done bit-15:8,
	//	RX reset done bit-23:16,
	//	RX FSM IP reset done bit-31:24
	// val = registerRead(0x9138);
	// std::stringstream rd;
	// rd << "TX reset done (" << (((val >> 6) & 1) ? "DONE" : "Not done");
	// rd << "), \nTX FSM IP reset done (" << (((val >> 14) & 1) ? "DONE" : "Not done");
	// rd << "), \nRX reset done (" << (((val >> 22) & 1) ? "DONE" : "Not done");
	// rd << "), \nRX FSM IP reset done (" << (((val >> 30) & 1) ? "DONE" : "Not done");
	// rd << ")";
	__SET_ARG_OUT__("Reset Done", thisDTC_->FormatSERDESResetDone()); //rd.str());

}  // end GetUpstreamControlLinkStatus()

//========================================================================
void DTCFrontEndInterface::SelectJitterAttenuatorSource(__ARGS__)
{
	uint32_t select = __GET_ARG_IN__(
	    "Source (0 is Control Link Rx, 1 is RJ45, 2 is FPGA FMC)", uint32_t);
	select %= 4;
	__FE_COUTV__((unsigned int)select);

	// choose jitter attenuator input select (reg 0x9308, bits 5:4)
	//  0 is Upstream Control Link Rx Recovered Clock
	//  1 is RJ45 Upstream Clock
	//  2 is Timing Card Selectable (SFP+ or FPGA) Input Clock

	// char              reg_0x9308[100];
	// uint32_t          val = registerRead(0x9308);
	// std::stringstream results;

	// sprintf(reg_0x9308, "0x%8.8X", val);
	// __FE_COUTV__(reg_0x9308);
	// results << "reg_0x9118 Starting Value: " << reg_0x9308 << __E__;

	// val &= ~(3 << 4);          // clear the two bits
	// val &= ~(1);               // ensure unreset of jitter attenuator
	// val |= (select & 3) << 4;  // set the two bits to selected value

	// sprintf(reg_0x9308, "0x%8.8X", val);
	// __FE_COUTV__(reg_0x9308);
	// results << "reg_0x9118 Select Value: " << reg_0x9308 << __E__;

	// registerWrite(0x9308, val);  // write select value

	if(!__GET_ARG_IN__(
	    "DoNotSet", bool))
	{
		thisDTC_->SetJitterAttenuatorSelect(select);
		sleep(1);
	}


	// __SET_ARG_OUT__("Register Write Results", results.str());
	__FE_COUT__ << "Done with jitter attenuator source select: " << select << __E__;

	__SET_ARG_OUT__("Register Write Results", thisDTC_->FormatJitterAttenuatorCSR());	

}  // end SelectJitterAttenuatorSource()

//========================================================================
void DTCFrontEndInterface::WriteDTC(__ARGS__)
{
	uint32_t address   = __GET_ARG_IN__("address", uint32_t);
	uint32_t writeData = __GET_ARG_IN__("writeData", uint32_t);
	__FE_COUTV__((unsigned int)address);
	__FE_COUTV__((unsigned int)writeData);

	int errorCode = getDevice()->write_register( address, 100, writeData);
	if (errorCode != 0)
	{
		__FE_SS__ << "Error writing register 0x" << std::hex << static_cast<uint32_t>(address) << " " << errorCode;
		__SS_THROW__;
	}

	// registerWrite(address, writeData);
}  // end WriteDTC()

//========================================================================
void DTCFrontEndInterface::ReadDTC(__ARGS__)
{
	uint32_t address = __GET_ARG_IN__("address", uint32_t);
	__FE_COUTV__((unsigned int)address);
	uint32_t readData;// = registerRead(address);

	int errorCode = getDevice()->read_register(address, 100, &readData);
	if (errorCode != 0)
	{
		__FE_SS__ << "Error reading register 0x" << std::hex << static_cast<uint32_t>(address) << " " << errorCode;
		__SS_THROW__;
	}	

	// char readDataStr[100];
	// sprintf(readDataStr,"0x%X",readData);
	// converted to dec and hex display in FEVInterfacesManager handling of FE Macros
	__SET_ARG_OUT__("readData", readData);  // readDataStr);
}  // end ReadDTC()

//========================================================================
void DTCFrontEndInterface::DTCReset(__ARGS__) { thisDTC_->ResetDTC(); }

// //========================================================================
// void DTCFrontEndInterface::DTCReset()
// {
// 	__FE_COUT__ << "Starting DTC Reset: reset the DTC FPGA and SERDES" << __E__;

// 	// 0x9100 is the DTC Control Register
// 	thisDTC_->ResetDTC();
// 	// registerWrite(0x9100, //registerRead(0x9100) |
// 	//  	(1 << 31));  // bit 31 = DTC Reset FPGA

// 	// usleep(500000 /*500ms*/);
// 	// sleep(3);

// 	// reset DTC serdes osc
// 	// thisDTC_->SetCFOEmulationMode(); //not needed after configureHardwareDevMode 



// 	// registerWrite(0x9100, //registerRead(0x9100) | 
// 	// 	(1 << 15));  // Turn on CFO Emulation Mode for Serdes Reset
// 	// registerWrite(0x9118, 0xffff00ff);      // SERDES resets
// 	// registerWrite(0x9118, 0x00000000);      // clear SERDES resets
	
// 	//Rick says he never resets links unless there is a problem:
// 	// thisDTC_->ResetSERDESTX(DTCLib::DTC_Link_ID::DTC_Link_ALL);
// 	// thisDTC_->ResetSERDESRX(DTCLib::DTC_Link_ID::DTC_Link_ALL);
// 	// thisDTC_->ResetSERDES(DTCLib::DTC_Link_ID::DTC_Link_ALL);
// 	//there is also bit-8 of the control register, that resets all the links


// 	// if(emulate_cfo_ == 1)
// 	// {
// 	// 	// registerWrite(0x9100, registerRead(0x9100) & ~(1 << 15)); // Turn off CFO Emulation Mode 
// 	// 	thisDTC_->ClearCFOEmulationMode();
// 	// }

// 	// usleep(500000 /*500ms*/);
// 	// sleep(2);

// 	__FE_COUT__ << "Done with DTC Reset." << __E__;
// }  // end DTCReset()

//========================================================================
void DTCFrontEndInterface::RunROCFEMacro(__ARGS__)
{
	//	std::string feMacroName = __GET_ARG_IN__("ROC_FEMacroName", std::string);
	//	std::string rocUID = __GET_ARG_IN__("ROC_UID", std::string);
	//
	//	__FE_COUTV__(feMacroName);
	//	__FE_COUTV__(rocUID);

	auto feMacroIt = rocFEMacroMap_.find(feMacroStruct.feMacroName_);
	if(feMacroIt == rocFEMacroMap_.end())
	{
		__FE_SS__ << "Fatal error - ROC FE Macro name '" << feMacroStruct.feMacroName_
		          << "' not found in DTC's map!" << __E__;
		__FE_SS_THROW__;
	}

	const std::string& rocUID         = feMacroIt->second.first;
	const std::string& rocFEMacroName = feMacroIt->second.second;

	__FE_COUTV__(rocUID);
	__FE_COUTV__(rocFEMacroName);

	auto rocIt = rocs_.find(rocUID);
	if(rocIt == rocs_.end())
	{
		__FE_SS__ << "Fatal error - ROC name '" << rocUID << "' not found in DTC's map!"
		          << __E__;
		__FE_SS_THROW__;
	}

	rocIt->second->runSelfFrontEndMacro(rocFEMacroName, argsIn, argsOut);

}  // end RunROCFEMacro()

//========================================================================
void DTCFrontEndInterface::SetEmulatedROCEventFragmentSize(__ARGS__)
{
	// To change the size of the event, need to write to each ROC emulator
	// 0x91B0 (b0-10 roc0, b16-26 roc1), 0x91B4 (b0-10 roc2, b16-26 roc3), 0x91B8 (b0-10
	// roc4, b16-26 roc5)

	uint32_t size  = __GET_ARG_IN__("size", uint32_t);
	uint32_t wsize = size & 0x03FF;  // only allow 11 bits

	__FE_COUTV__((unsigned int)size);
	__FE_COUTV__((unsigned int)wsize);

	// uint32_t wword = (wsize << 16) | wsize;  // create word for both ROC bit positions

	for(uint8_t i=0;i<DTCLib::DTC_Links.size();++i)
		thisDTC_->SetCFOEmulationNumPackets(DTCLib::DTC_Links[i],wsize);
	// registerWrite(0x91B0, wword);
	// registerWrite(0x91B4, wword);
	// registerWrite(0x91B8, wword);

	__SET_ARG_OUT__("Size Written", wsize);

}  // end SetEmulatedROCEventFragmentSize()

//========================================================================
void DTCFrontEndInterface::configureHardwareDevMode(__ARGS__)
{
	configureHardwareDevMode();
} // end configureHardwareDevMode()

//========================================================================
// TODO: print the packet with the correct event tag
void DTCFrontEndInterface::BufferTest(__ARGS__)
{
	__FE_COUT__ << "Operation \"buffer_test\"" << std::endl;

	// reset the dtc
	// DTCReset();
	// configureHardwareDevMode();

	// release of all the buffers
	getDevice()->read_release(DTC_DMA_Engine_DAQ, 100);

	// stream to print the output
	std::stringstream ostr;
	ostr << std::endl;

	// arguments
	unsigned int numberOfEvents = __GET_ARG_IN__("numberOfEvents", uint32_t);
	bool activeMatch = !__GET_ARG_IN__("doNotMatch (bool)", bool);
	bool saveBinaryDataToFile = __GET_ARG_IN__("saveBinaryDataToFile (bool)", bool);
	__FE_COUTV__(numberOfEvents);
	__FE_COUTV__(activeMatch);
	__FE_COUTV__(saveBinaryDataToFile);

	// parameters
	uint16_t debugPacketCount = 0; 
	uint32_t cfoDelay = __GET_ARG_IN__("eventDuration", uint32_t);	//400 -- delay in the frequency of the emulated CFO
	bool doNotReadBack = __GET_ARG_IN__("doNotReadBack (bool)", bool);
	// uint32_t requestDelay = 0;
	bool incrementTimestamp = true;		// this parameter is not working with emulated CFO
	bool useSWCFOEmulator = true;
	bool stickyDebugType = true;
	bool quiet = false;
	bool asyncRR = false;
	bool forceNoDebugMode = true;
	int requestsAhead = 0;
	unsigned int timestampStart = 10;	// no 0, because it's a get all (special thing)
	auto debugType = DTCLib::DTC_DebugType_SpecialSequence;	// enum (0)

	// event window Tag used to bind the request to the response
	DTCLib::DTC_EventWindowTag eventTag = DTCLib::DTC_EventWindowTag(static_cast<uint64_t>(timestampStart));

	// create the emulated CFO instance
	DTCLib::DTCSoftwareCFO* cfo = new DTCLib::DTCSoftwareCFO(thisDTC_,
																useSWCFOEmulator,
																debugPacketCount,
																debugType,
																stickyDebugType,
																quiet,
																asyncRR,
																forceNoDebugMode);
	// send the request for a range of events
	cfo -> SendRequestsForRange(numberOfEvents,
								eventTag,
								incrementTimestamp,
								cfoDelay,
								requestsAhead);


	std::string filename = "/macroOutput_" + std::to_string(time(0)) + "_" +
			                       std::to_string(clock()) + ".bin";
	FILE *fp = nullptr;
	if(saveBinaryDataToFile)
	{
		filename = std::string(__ENV__("OTSDAQ_DATA")) + "/" + filename;
		__FE_COUTV__(filename);
		fp = fopen(filename.c_str(), "wb");
		if(!fp)
		{
			__FE_SS__ << "Failed to open file to save macro output '"
						<< filename << "'..." << __E__;
			__FE_SS_THROW__;
		}
	}
	
	// get the data requested
	for(unsigned int ii = 0; !doNotReadBack &&  ii < numberOfEvents; ++ii)
	{ 
		// get the data
		std::vector<std::unique_ptr<DTCLib::DTC_Event>> events = thisDTC_->GetData(eventTag + ii, activeMatch);
		ostr << "Read " << ii << ": Events returned by the DTC: " << events.size() << std::endl;
		if (!events.empty()) 
		{
			for(auto& eventPtr : events) 
			{
				if (eventPtr == nullptr) 
				{
					ostr << "Error: Null pointer!" << std::endl;
					continue;
				}

				// get the event
				auto event = eventPtr.get();

				// check the event tag window
				ostr << "Request event tag:\t" << "0x" << std::hex << std::setw(4) << std::setfill('0') << std::to_string(eventTag.GetEventWindowTag(true) + ii) << std::endl;
				ostr << "Response event tag:\t" << "0x" << std::hex << std::setw(4) << std::setfill('0') << event->GetEventWindowTag().GetEventWindowTag(true) << std::endl;

				// get the event and the relative sub events
				DTCLib::DTC_EventHeader *eventHeader = event->GetHeader();
				std::vector<DTCLib::DTC_SubEvent> subevents = event->GetSubEvents();

				// print the event header
				ostr << eventHeader->toJson() << std::endl
						<< "Subevents count: " << event->GetSubEventCount() << std::endl;
				
				// iterate over the subevents
				for (unsigned int i = 0; i < subevents.size(); ++i)
				{
					// print the subevents header
					DTCLib::DTC_SubEvent subevent = subevents[i];
					ostr << "Subevent [" << i << "]:" << std::endl;
					ostr << subevent.GetHeader()->toJson() << std::endl;

					// check if there is an error on the link
					if (subevent.GetHeader()->link0_status > 0)
					{
						ostr << "Error: " << std::endl;
						std::bitset<8> link0_status(subevent.GetHeader()->link0_status);
						if (link0_status.test(0)) 
						{
							ostr << "ROC Timeout Error!" << std::endl;
						}
						if (link0_status.test(2)) 
						{
							ostr << "Packet sequence number Error!" << std::endl;
						}
						if (link0_status.test(3)) 
						{
							ostr << "CRC Error!" << std::endl;
						}
						if (link0_status.test(6)) 
						{
							ostr << "Fatal Error!" << std::endl;
						}
						
						continue;
					}

					// print the number of data blocks
					ostr << "Number of Data Block: " << subevent.GetDataBlockCount() << std::endl;
					
					// iterate over the data blocks
					std::vector<DTCLib::DTC_DataBlock> dataBlocks = subevent.GetDataBlocks();
					for (unsigned int j = 0; j < dataBlocks.size(); ++j)
					{
						ostr << "Data block [" << j << "]:" << std::endl;
						// print the data block header
						DTCLib::DTC_DataHeaderPacket *dataHeader = dataBlocks[j].GetHeader().get();
						ostr << dataHeader->toJSON() << std::endl;	

						// print the data block payload
						const void* dataPtr = dataBlocks[j].GetData();
						ostr << "Data payload:" << std::endl;
						for (int l = 0; l < dataHeader->GetByteCount() - 16; l+=2)
						{
							auto thisWord = reinterpret_cast<const uint16_t*>(dataPtr)[l];
							if(fp) fwrite(&thisWord,sizeof(uint16_t), 1, fp);
							ostr << "\t0x" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(thisWord) << std::endl;
						}

					}
				}
				ostr << std::endl << std::endl;
			}
		}
	
	}

	if(fp) fclose(fp); //close binary file

	// print the result
	std::stringstream outSs;
	outSs << "Number of events  requested: " + std::to_string(numberOfEvents) << __E__;
	outSs << "Active Event Match: " << (activeMatch?"true":"false") << __E__;
	outSs << "Event Duration: " << cfoDelay << " = " << cfoDelay*25 << " ns" << __E__;
	outSs << "Reading back: " << (doNotReadBack?"false":"true") << __E__;
	if(fp) outSs << "Binary data file saved at: " << filename << __E__;
	outSs << ostr.str();

	__SET_ARG_OUT__("response", outSs.str());
	delete cfo;
}

//========================================================================
void DTCFrontEndInterface::ROCResetLink(__ARGS__)
{
	__FE_COUT__ << "Operation \"link_config\"" << std::endl;

	// default values
	unsigned int link = 0;
	uint16_t lane = 0;

	std::string str_link = __GET_ARG_IN__("Link", std::string);
	std::string str_lane = __GET_ARG_IN__("Lane", std::string);

	std::stringstream ostr;
	ostr << std::endl;
	
	if (str_link.compare("Default") != 0)
	{
		// check if the input is a digit
		for (char const &ch: str_link) 
		{
			if (std::isdigit(ch) == 0)
			{
				ostr << "Error: not valid link! " << std::endl;
				return;
			}	
		}
		// cast to int
		link = std::stoi(str_link);
	}
	if (str_lane.compare("Default") != 0)
	{
		// check if the input is a digit
		for (char const &ch: str_lane) 
		{
			if (std::isdigit(ch) == 0)
			{
				ostr << "Error: not valid lane! " << std::endl;
				return;
			}	
		}
		// cast to int
		lane = std::stoi(str_lane);
	}
	__FE_COUTV__(link);
	__FE_COUTV__(lane);

	auto dtc_link = static_cast<DTCLib::DTC_Link_ID>(link);

	// parameter
	unsigned tmo_ms = 10;

	try
	{
		__FE_COUT__ << "Resetting and configuring link " << dtc_link;

		// reset the link and reconfigure
		thisDTC_->WriteROCRegister(dtc_link, 14, 1, false, tmo_ms);
		usleep(1000000);
		thisDTC_->WriteROCRegister(dtc_link, 13, 1, false, tmo_ms);
		usleep(1000000);
		thisDTC_->WriteROCRegister(dtc_link, 13, 0, false, tmo_ms);
		usleep(1000000);

		if (lane == 1) 
		{
			__FE_COUT__ << " to receive data only from CAL lane 0" << std::endl;
			ostr << " to receive data only from CAL lane 0" << std::endl;
		}
		else if (lane == 5) 
		{
			__FE_COUT__ << " to receive data from both CAL lanes" << std::endl;
			ostr << " to receive data from both CAL lanes" << std::endl;
		}
		else if (lane == 15)
		{
			__FE_COUT__ << " to receive data from all 4 lanes" << std::endl;
			ostr << " to receive data from all 4 lanes" << std::endl;
		}

		// after adding external clock and evmarker control to the ROC,
		// one needs to write bit(8)=1 and bit(9)=1 on register 8, ie 0x300 (0r 768)
		uint16_t set_lane = lane + 768;
		__FE_COUTV__(lane);
		thisDTC_->WriteROCRegister(dtc_link, 8, set_lane, false, tmo_ms);

	}
	catch (std::runtime_error& err)
	{
		TLOG(TLVL_ERROR) << "Error writing to ROC: " << err.what();
		ostr << "Error writing to ROC: " << err.what();
	}

	std::string outStr = "Resetting the ROC \nlink: " + std::to_string(link) + "\n" 
							+ "lane: " + std::to_string(lane) + "\n" 
							+ ostr.str();
	__SET_ARG_OUT__("readData", outStr);
}
//========================================================================
void DTCFrontEndInterface::HeaderFormatTest(__ARGS__) 
{
	__FE_COUT__ << "Operation \"heder_format\"" << std::endl;

	std::stringstream ostr;
	ostr << std::endl;

	auto device = thisDTC_->GetDevice();

	device->write_register(37316, 100, 170);	// 0x91c4 <- 0xAA
	TLOG(TLVL_ERROR) << "Write " << std::hex << 170 << " in 0x91C4" << std::endl;
	ostr << "Write " << std::hex << 170 << " in 0x91C4" << std::endl;

	device->write_register(37312, 100, 47802);	// 0x91c0 <- 0xBABA
	TLOG(TLVL_ERROR) << "Write " << std::hex << 47802 << " in 0x91C0" << std::endl;
	ostr << "Write " << std::hex << 47802 << " in 0x91C0" << std::endl;

	device->write_register(37204, 100, 205);	// 0x9154 <- 0xCD
	TLOG(TLVL_ERROR) << "Write " << std::hex << 205 << " in 0x9154" << std::endl;
	ostr << "Write " << std::hex << 205 << " in 0x9154" << std::endl;

	std::string outStr = "Test packet headers \n" + ostr.str();
	__SET_ARG_OUT__("setRegister", outStr);
}
//========================================================================
void DTCFrontEndInterface::DTCCounters(__ARGS__)
{	
	__SET_ARG_OUT__("Link Counters", thisDTC_->LinkCountersRegDump(20));
	__SET_ARG_OUT__("Performance Counters", thisDTC_->PerformanceCountersRegDump(20));
	__SET_ARG_OUT__("Packet Counters", thisDTC_->PacketCountersRegDump(20));
}  // end DTCCounters()

// //========================================================================
// void DTCFrontEndInterface::FlashLEDs(__ARGS__)
// {	
// 	thisDTC_->FlashLEDs();
// } //end FlashLEDs()

//==============================================================================
// GetFirmwareVersion
 void DTCFrontEndInterface::GetFirmwareVersion(__ARGS__)
{	
	__SET_ARG_OUT__("Firmware Version Date", thisDTC_->ReadDesignDate());
}  // end GetFirmwareVersion()

//========================================================================
void DTCFrontEndInterface::GetStatus(__ARGS__)
{	
	__SET_ARG_OUT__("Status", thisDTC_->FormattedRegDump(20));
} //end GetStatus()

//========================================================================
void DTCFrontEndInterface::GetSimpleStatus(__ARGS__)
{	
	__SET_ARG_OUT__("Status", thisDTC_->FormattedRegDump(20, &(thisDTC_->formattedSimpleDumpFunctions_)));
} //end GetSimpleStatus()

//========================================================================
void DTCFrontEndInterface::readRxDiagFIFO(__ARGS__)
{	
	DTCLib::DTC_Link_ID LinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("LinkIndex", uint8_t));

	__SET_ARG_OUT__("Diagnostic RX FIFO", thisDTC_->FormatRXDiagFifo(DTCLib::DTC_Links[LinkIndex]));

	
} //end readRxDiagFIFO()

//========================================================================
void DTCFrontEndInterface::readTxDiagFIFO(__ARGS__)
{	
	DTCLib::DTC_Link_ID LinkIndex =
	    DTCLib::DTC_Link_ID(__GET_ARG_IN__("LinkIndex", uint8_t));

	__SET_ARG_OUT__("Diagnostic TX FIFO", thisDTC_->FormatTXDiagFifo(DTCLib::DTC_Links[LinkIndex]));

	
} //end readTxDiagFIFO()

//========================================================================
void DTCFrontEndInterface::GetLinkErrors(__ARGS__)
{	
	__SET_ARG_OUT__("Link Errors", thisDTC_->SERDESErrorsRegDump(20));
} //end GetLinkErrors()

// //========================================================================
// void DTCFrontEndInterface::ROCDestroy(__ARGS__)
// {	
	
// 	rocs_.clear();

// } //end ROCDestroy()
// //========================================================================
// void DTCFrontEndInterface::ROCInstantiate(__ARGS__)
// {	
// 	createROCs();
	
// } //end ROCInstantiate()
//========================================================================
void DTCFrontEndInterface::DTCInstantiate(__ARGS__)
{	
	if(thisDTC_)
		delete thisDTC_;
	thisDTC_ = nullptr;

	DTCLib::DTC_SimMode mode =
	    emulate_cfo_ ? DTCLib::DTC_SimMode_NoCFO : DTCLib::DTC_SimMode_Disabled;
	
	unsigned dtc_class_roc_mask = 0;
	// create roc mask for DTC
	{
		std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
		    Configurable::getSelfNode().getNode("LinkToROCGroupTable").getChildren();

		__FE_COUTV__(rocChildren.size());
		roc_mask_ = 0;

		for(auto& roc : rocChildren)
		{
			__FE_COUT__ << "roc uid " << roc.first << __E__;
			bool enabled = roc.second.getNode("Status").getValue<bool>();
			__FE_COUT__ << "roc enable " << enabled << __E__;

			if(enabled)
			{
				int linkID = roc.second.getNode("linkID").getValue<int>();
				roc_mask_ |= (0x1 << linkID);
				dtc_class_roc_mask |=
				    (0x1 << (linkID * 4));  // the DTC class instantiation expects each
				                            // ROC has its own hex nibble
			}
		}

		__FE_COUT__ << "DTC roc_mask_ = 0x" << std::hex << roc_mask_ << std::dec << __E__;
		__FE_COUT__ << "roc_mask to instantiate DTC class = 0x" << std::hex
		            << dtc_class_roc_mask << std::dec << __E__;

	}  // end create roc mask

	std::string         expectedDesignVersion = "";

	__COUT__ << "DTC arguments..." << std::endl;
	__COUTV__(mode);
	__COUTV__(deviceIndex_);
	__COUTV__(dtc_class_roc_mask);
	__COUTV__(expectedDesignVersion);
	__COUTV__(skipInit_);
	__COUT__ << "END DTC arguments..." << std::endl;

	thisDTC_ = new DTCLib::DTC(
	    mode, deviceIndex_, dtc_class_roc_mask, expectedDesignVersion, 
		true /* skipInit */, //always skip init and lots ots configure setup
		"" /* simMemoryFile */,
		getInterfaceUID());
	
} //end DTCInstantiate()


//========================================================================
void DTCFrontEndInterface::DMABufferRelease(__ARGS__)
{	
	
	{
		//MU2E_NUM_RECV_BUFFS = 100 in mu2e_pcie_utils/mu2e_driver/mu2e_proto_globals.h
		auto stsDCS = getDevice()->read_release(DTC_DMA_Engine_DCS, 100);
		auto stsDAQ = getDevice()->read_release(DTC_DMA_Engine_DAQ, 100);
		TLOG(TLVL_DEBUG + 7) << "DMABufferRelease - stsDCS=" << stsDCS << ", stsDAQ=" << stsDAQ;
		/*
		for (unsigned ii = 0; ii < 100; ++ii)
			{
				void* buffer;
				uto tmo_ms = 0;
				auto stsRD = getDevice()->read_data(DTC_DMA_Engine_DCS, &buffer, tmo_ms);			auto stsDCS = getDevice()->read_release(DTC_DMA_Engine_DCS, 100);
				auto stsRL = getDevice()->read_release(DTC_DMA_Engine_DCS, 1);
				TLOG(TLVL_DEBUG + 7) << "Reset - release/read for DCS ii=" << ii 
				<< ", stsRD=" << stsRD << ", stsRL=" << stsRL << ", buffer=" << buffer;
				//if (delay > 0) usleep(delay);
			}
		for (unsigned ii = 0; ii < 100; ++ii)
		{
			void* buffer;
			auto tmo_ms = 0;
			// auto stsRD = getDevice()->read_data(DTC_DMA_Engine_DAQ, &buffer, tmo_ms);
			auto stsRL = getDevice()->read_release(DTC_DMA_Engine_DAQ, 1);
			TLOG(TLVL_DEBUG + 7) << "Reset - release/read for DAQ ii=" << ii 
			//<< ", stsRD=" << stsRD 
			<< ", stsRL=" << stsRL << ", buffer=" << buffer;
			//if (delay > 0) usleep(delay);
		}*/
	}

	
} //end DMABufferRelease()

//========================================================================
void DTCFrontEndInterface::ResetDTCLinks(__ARGS__)
{	
	thisDTC_->ResetSERDESTX(DTCLib::DTC_Link_ID::DTC_Link_ALL);
	thisDTC_->ResetSERDESRX(DTCLib::DTC_Link_ID::DTC_Link_ALL);
	thisDTC_->ResetSERDES(DTCLib::DTC_Link_ID::DTC_Link_ALL);
} //end ResetDTCLinks()

//========================================================================
void DTCFrontEndInterface::ConfigureForTimingChain(__ARGS__)
{	
	//call virtual readStatus

	int stepIndex = __GET_ARG_IN__("StepIndex", int);

	// do 0, then 1
	if(stepIndex == -1)
	{
		for(int i=0;i<CONFIG_DTC_TIMING_CHAIN_STEPS;++i)
		{
			configureForTimingChain(i);
			usleep(1000);
		}
	}
	else
		configureForTimingChain(stepIndex);
	

} //end ConfigureForTimingChain()

//========================================================================
void DTCFrontEndInterface::GetLinkLossOfLight(__ARGS__)
{	
	std::stringstream rd;


	//do initial set of writes to get the live read of loss-of-light status (because it is latched value from last read)
/*
	// #Read Firefly RX LOS registers
	// #enable IIC on Firefly
	// my_cntl write 0x93a0 0x00000200
	registerWrite(0x93a0,0x00000200);
	// #Device address, register address, null, null
	// my_cntl write 0x9298 0x54080000
	registerWrite(0x9298,0x54080000);
	// #read enable
	// my_cntl write 0x929c 0x00000002
	registerWrite(0x929c,0x00000002);
	// #disable IIC on Firefly
	// my_cntl write 0x93a0 0x00000000
	registerWrite(0x93a0,0x00000000);
	// #read data: Device address, register address, null, value
	// my_cntl read 0x9298
*/
	thisDTC_->SetTXRXFireflySelect(true);
	thisDTC_->WriteFireflyRXIICInterface(0x54 /*device*/, 0x08 /*address*/, 0 /*data*/);
	thisDTC_->SetTXRXFireflySelect(false);

	// #{EVB, ROC4, ROC1, CFO, unused, ROC5, unused, unused}
	usleep(1000*100);

/*
	// #Read Firefly RX LOS registers
	// my_cntl write 0x93a0 0x00000200
	registerWrite(0x93a0,0x00000200);
	// my_cntl write 0x9298 0x54070000
	registerWrite(0x9298,0x54070000);
	// my_cntl write 0x929c 0x00000002
	registerWrite(0x929c,0x00000002);
	// my_cntl write 0x93a0 0x00000000
	registerWrite(0x93a0,0x00000000);
	// my_cntl read 0x9298
*/

	thisDTC_->SetTXRXFireflySelect(true);
	thisDTC_->WriteFireflyRXIICInterface(0x54 /*device*/, 0x07 /*address*/, 0 /*data*/);
	thisDTC_->SetTXRXFireflySelect(false);

	//END do initial set of writes to get the live read of loss-of-light status (because it is latched value from last read)

	dtc_data_t val=0, val2=0;
	for(int i=0;i<5;++i)
	{
		usleep(1000*100 /* 100 ms */);
		thisDTC_->SetTXRXFireflySelect(true);
		//OR := if ever 1, mark dead
		val |= thisDTC_->ReadFireflyRXIICInterface(0x54 /*device*/, 0x08 /*address*/);
		thisDTC_->SetTXRXFireflySelect(false);
			
		
		usleep(1000*100 /* 100 ms */);
		thisDTC_->SetTXRXFireflySelect(true);
		//OR := if ever 1, mark dead
		val2 |= thisDTC_->ReadFireflyRXIICInterface(0x54 /*device*/, 0x07 /*address*/);
		thisDTC_->SetTXRXFireflySelect(false);
	} //end multi-read to check for strange value changing

	// #ROC0 bit 3
	rd << "{0:" << (((val2>>(0+3))&1)?"DEAD":"OK");
	// #ROC1 bit 5
	rd << ", 1: " << (((val>>(0+5))&1)?"DEAD":"OK");
	// #ROC2 bit 2
	rd << ", 2:" << (((val2>>(0+2))&1)?"DEAD":"OK");
	// #ROC3 bit 0
	rd << ", 3:" << (((val2>>(0+0))&1)?"DEAD":"OK");
	// #ROC4 bit 6
	rd << ", 4: " << (((val>>(0+6))&1)?"DEAD":"OK");
	// #ROC5 bit 1
	rd << ", 5: " << (((val>>(0+1))&1)?"DEAD":"OK");
	// #CFO bit 4
	rd << ", 6/CFO: " << (((val>>(0+4))&1)?"DEAD":"OK");
	// #EVB bit 7  Are EVB and CFO reversed?
	rd << ", 7/EVB: " << (((val>>(0+7))&1)?"DEAD":"OK") << "}";



	__SET_ARG_OUT__("Link Status",rd.str());
} //end GetLinkLossOfLight()

DEFINE_OTS_INTERFACE(DTCFrontEndInterface)
