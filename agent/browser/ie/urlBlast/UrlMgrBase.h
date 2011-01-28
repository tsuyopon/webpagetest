#pragma once
#include "log.h"

class CTestInfo
{
public:
	CTestInfo(){Reset();}
	void Reset(void)
	{
		zipFileDir.Empty();
		logFile.Empty();
		url.Empty();
		eventText.Empty();
		domElement.Empty();
		urlType = 0;
		includeObjectData = 1;
		harvestLinks = false;
		saveEverything = false;
		captureVideo = false;
		checkOpt = 1;
		connections = 0;
		reserved = 0;
		context = NULL;
		testResult = 0;
		runningScript = false;
		speed = 0;
		block.Empty();
		basicAuth.Empty();
		done = true;
		ignoreSSL = 0;
		bwIn = 0;
		bwOut = 0;
		latency = 0;
		plr = 0;
		ipfw = false;
		host.Empty();
    browser.Empty();
    tcpdump = false;
    tcpdumpFile.Empty();
	}
	
	CString zipFileDir;			// If we got a custom job (video rendering only currently)
	CString userName;			// account name tied to this test
	CString logFile;			// where the results should be stored
	CString url;				// URL to be tested
	CString eventText;			// Friendly text
	CString	domElement;			// DOM element to look for
	DWORD	urlType;			// web 1.0, 2.0 or automatic?
	DWORD	includeObjectData;	// should object data be included?
	bool	harvestLinks;		// should the links be harvested from the page (crawler)?
	CString linksFile;			// where the harvested links should be stored
	CString s404File;			// where the 404's should be logged
	CString htmlFile;			// where the base page HTML be stored
	CString cookiesFile;			// where the cookies should be stored
	bool	saveEverything;		// should everything be saved out (images, etc)?
	bool	captureVideo;		// do we save out the images necessary to construct a video?
	DWORD	checkOpt;			// should optimizations be checked?
	DWORD	connections;		// number of parallel browser connections
	bool	runningScript;		// are we runnning a script?
	CString	scriptFile;			// location of the script file
	DWORD	speed;				// which speed (if any) to test?
	CString block;				// Requests to block
	CString	basicAuth;			// Auth string (user name:password)
	bool	done;				// done after this run
	DWORD	ignoreSSL;			// ignore SSL errors?
	CString	host;				// custom host header
  CString browser;  // custom browser?

	DWORD	bwIn;				// bandwidth in
	DWORD	bwOut;				// bandwidth out
	DWORD	latency;			// latency
	double	plr;				// packet loss
	bool	ipfw;				// do we need to do a custom bandwidth?
  DWORD  tcpdump;   // packet capture?
  CString tcpdumpFile;

	DWORD	reserved;			// reserved for internal use
	void *	context;			// contect information for internal use
	
	DWORD	testResult;			// result code from the test
};

class CUrlMgrBase
{
public:
	CUrlMgrBase(CLog &logRef);
	virtual ~CUrlMgrBase(void);

	virtual void Start(void){}
	virtual void Stop(void){}

	virtual bool GetNextUrl(CTestInfo &info) = 0;
	virtual void HarvestedLinks(CTestInfo &info){}
	virtual bool RunRepeatView(CTestInfo &info){return true;}
	virtual void UrlFinished(CTestInfo &info){}
	virtual void GetStatus(CString &status){}
	virtual bool NeedReboot(){return false;}

protected:
	CLog &log;
	SECURITY_ATTRIBUTES nullDacl;
	SECURITY_DESCRIPTOR SD;
};
