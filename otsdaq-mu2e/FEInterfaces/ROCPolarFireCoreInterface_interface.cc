#include "otsdaq-core/Macros/InterfacePluginMacros.h"
#include "otsdaq-mu2e/FEInterfaces/ROCPolarFireCoreInterface.h"

using namespace ots;

#undef __MF_SUBJECT__
#define __MF_SUBJECT__ "FE-ROCPolarFireCoreInterface"

//=========================================================================================
ROCPolarFireCoreInterface::ROCPolarFireCoreInterface(
    const std::string&       rocUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       theConfigurationPath)
    : ROCCoreVInterface(rocUID, theXDAQContextConfigTree, theConfigurationPath)
{
	INIT_MF("ROCPolarFireCoreInterface");

	__MCOUT_INFO__("ROCPolarFireCoreInterface instantiated with link: "
	               << linkID_ << " and EventWindowDelayOffset = " << delay_ << __E__);
}

//==========================================================================================
ROCPolarFireCoreInterface::~ROCPolarFireCoreInterface(void)
{
	// NOTE:: be careful not to call __FE_COUT__ decoration because it uses the
	// tree and it may already be destructed partially
	__COUT__ << FEVInterface::interfaceUID_ << "Destructed." << __E__;
}

//==================================================================================================
int ROCPolarFireCoreInterface::readEmulatorRegister(unsigned address)
{
	__FE_COUT__ << "Calling read emulator ROC register: link number " << std::dec << linkID_
	            << ", address = " << address << __E__;
	if(address == 6)
		return 4860;
	else if(address == 7)
		return delay_;
	return -1;
} //end readEmulatorRegister

//==================================================================================================
void ROCPolarFireCoreInterface::writeROCRegister(unsigned address, unsigned data_to_write)
{
	__FE_COUT__ << "Calling write ROC register: link number " << std::dec << linkID_
	            << ", address = " << address << ", write data = " << data_to_write
	            << __E__;

	thisDTC_->WriteROCRegister(linkID_, address, data_to_write);

	return;
}

//==================================================================================================
int ROCPolarFireCoreInterface::readROCRegister(unsigned address)
{
	__FE_COUT__ << "Calling read ROC register: link number " << std::dec << linkID_
	            << ", address = " << address << __E__;

	int read_data = 0;

	try
	{
		read_data = thisDTC_->ReadROCRegister(linkID_, address, 10);
	}
	catch(...)
	{
		__COUT__ << "DTC failed DCS read" << __E__;
		read_data = -999;
	}

	return read_data;
}

//==================================================================================================
int ROCPolarFireCoreInterface::readTimestamp() { return this->readRegister(12); }

//==================================================================================================
void ROCPolarFireCoreInterface::writeDelay(unsigned delay)
{
	this->writeRegister(21, delay);
	return;
}

//==================================================================================================
int ROCPolarFireCoreInterface::readDelay() { return this->readRegister(7); }

//==================================================================================================
int ROCPolarFireCoreInterface::readDTCLinkLossCounter() { return this->readRegister(8); }

//==================================================================================================
void ROCPolarFireCoreInterface::resetDTCLinkLossCounter()
{
	this->writeRegister(24, 0x1);
	return;
}

//==================================================================================================
void ROCPolarFireCoreInterface::configure(void) try
{

	if(emulatorMode_)
	{
		__FE_COUT__ << "Emulator ROC configuring..." << __E__;
		return;
	}
	// __MCOUT_INFO__("......... Clear DCS FIFOs" << __E__);
	// this->writeRegister(0,1);
	// this->writeRegister(0,0);

	// setup needToResetAlignment using rising edge of register 22
	// (i.e., force synchronization of ROC clock with 40MHz system clock)
	__MCOUT_INFO__("......... setup to synchronize ROC clock with 40 MHz clock edge"
	               << __E__);
	this->writeRegister(22, 0);
	this->writeRegister(22, 1);

	__MCOUT_INFO__("........."
	               << " Set delay = " << delay_ << ", readback = " << this->readDelay()
	               << " ... ");

	this->writeDelay(delay_);



	__FE_COUT__ << "Debugging ROC-DCS" << __E__;

	unsigned int val;

	// read 6 should read back 0x12fc
	for(int i = 0; i < 1; i++)
	{
		val = this->readRegister(6);

		//__MCOUT_INFO__(i << " read register 6 = " << val << __E__);
		if(val != 4860)
		{
			__FE_SS__ << "Bad read not 4860! val = " << val << __E__;
			__FE_SS_THROW__;
		}

		val = this->readDelay();
		//__MCOUT_INFO__(i << " read register 7 = " << val << __E__);
		if(val != delay_)
		{
			__FE_SS__ << "Bad read not " << delay_ << "! val = " << val << __E__;
			__FE_SS_THROW__;
		}
	}

	if(0)  // random intense check
	{
		highRateCheck();
	}

	__MCOUT_INFO__("......... reset DTC link loss counter ... ");
	resetDTCLinkLossCounter();
}
catch(const std::runtime_error& e)
{
	__FE_MOUT__ << "Error caught: " << e.what() << __E__;
	throw;
}
catch(...)
{
	__FE_SS__ << "Unknown error caught. Check printouts!" << __E__;
	__FE_MOUT__ << ss.str();
	__FE_SS_THROW__;
}

//========================================================================================================================
void ROCPolarFireCoreInterface::halt(void) {}

//========================================================================================================================
void ROCPolarFireCoreInterface::pause(void) {}

//========================================================================================================================
void ROCPolarFireCoreInterface::resume(void) {}

//========================================================================================================================
void ROCPolarFireCoreInterface::start(std::string)  // runNumber)
{
}

//========================================================================================================================
void ROCPolarFireCoreInterface::stop(void) {}

//========================================================================================================================
bool ROCPolarFireCoreInterface::running(void) { return false; }

DEFINE_OTS_INTERFACE(ROCPolarFireCoreInterface)