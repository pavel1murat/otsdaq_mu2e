#ifndef _ots_ROCCoreVInterface_h_
#define _ots_ROCCoreVInterface_h_

#include <sstream>
#include <string>
#include "dtcInterfaceLib/DTC.h"
#include "otsdaq-core/FECore/FEVInterface.h"

namespace ots
{
class ROCCoreVInterface : public FEVInterface
{
  public:
	ROCCoreVInterface(const std::string&       rocUID,
	                  const ConfigurationTree& theXDAQContextConfigTree,
	                  const std::string&       interfaceConfigurationPath);

	~ROCCoreVInterface(void);

	virtual std::string getInterfaceType(void) const override
	{
		return theXDAQContextConfigTree_.getBackNode(theConfigurationPath_)
			.getNode(FEVInterface::interfaceUID_)
		    .getNode("ROCInterfacePluginName")
		    .getValue<std::string>();
	}

	// state machine
	//----------------
	void configure(void) override;
	void halt(void) override;
	void pause(void) override;
	void resume(void) override;
	void start(std::string runNumber) override;
	void stop(void) override;
	bool running(void) override;

	//----------------
	// just to keep FEVInterface, defining universal read..
	void universalRead(char* address, char* readValue) override
	{
		__SS__ << "Not defined. Should never be called. Parent should be "
		          "DTCFrontEndInterface, not "
		          "FESupervisor."
		       << __E__;
		__SS_THROW__;
	}
	void universalWrite(char* address, char* writeValue) override
	{
		__SS__ << "Not defined. Should never be called. Parent should be "
		          "DTCFrontEndInterface, not "
		          "FESupervisor."
		       << __E__;
		__SS_THROW__;
	}
	//----------------

	// write and read to registers
	//	Philosophy: call writeRegister/readRegister and implement once
	//		writeROCRegister/writeEmulatorRegister/readROCRegister/readEmulatorRegister
	void         writeRegister(unsigned address,
	                           unsigned writeData);  // chooses ROC or Emulator version
	int          readRegister(unsigned address);     // chooses ROC or Emulator version
	virtual void writeROCRegister(
	    unsigned address,
	    unsigned writeData) = 0;  // pure virtual, must define in inheriting children
	virtual int readROCRegister(
	    unsigned address) = 0;  // pure virtual, must define in inheriting children
	virtual void writeEmulatorRegister(
	    unsigned address,
	    unsigned writeData) = 0;  // pure virtual, must define in inheriting children
	virtual int readEmulatorRegister(
	    unsigned address) = 0;  // pure virtual, must define in inheriting children

	// pure virtual specific ROC functions
	virtual int  readTimestamp() = 0;  // pure virtual, must define in inheriting children
	virtual void writeDelay(unsigned delay) = 0;  // 5ns steps // pure virtual, must
	                                              // define in inheriting children
	virtual int
	readDelay() = 0;  // 5ns steps // pure virtual, must define in inheriting children

	virtual int
	readDTCLinkLossCounter() = 0;  // pure virtual, must define in inheriting children
	virtual void
	resetDTCLinkLossCounter() = 0;  // pure virtual, must define in inheriting children

	// ROC debugging functions
	void        highRateCheck(void);
	static void highRateCheckThread(ROCCoreVInterface* roc);

	inline int getLinkID() { return linkID_; }

	bool         emulatorMode_;
	DTCLib::DTC* thisDTC_;

  protected:
	DTCLib::DTC_Link_ID linkID_;
	const unsigned int  delay_;

	//----------------- Emulator members
	// return false when done with workloop
  public:
	virtual bool emulatorWorkLoop(void)
	{
		__SS__ << "This is an empty emulator! this function should be overridden "
		          "by the derived class."
		       << __E__;
		__SS_THROW__;
		return false;
	}

	static void emulatorThread(ROCCoreVInterface* roc)
	{
		roc->workloopRunning_ = true;

		bool stillWorking = true;
		while(!roc->workloopExit_ && stillWorking)
		{
			__COUT__ << "Calling emulator WorkLoop..." << __E__;

			// lockout member variables for the remainder of the scope
			// this guarantees the emulator thread can safely access the members
			//	Note: other functions (e.g. write and read) must also lock for
			// this to work!
			std::lock_guard<std::mutex> lock(roc->workloopMutex_);
			stillWorking = roc->emulatorWorkLoop();
		}
		__COUT__ << "Exited emulator WorkLoop." << __E__;

		roc->workloopRunning_ = false;
	}  // end emulatorThread()

  protected:
	volatile bool workloopExit_;
	volatile bool workloopRunning_;

	std::mutex workloopMutex_;

	//----------------- end Emulator members

};  // end ROCCoreVInterface declaration

}  // namespace ots

#endif