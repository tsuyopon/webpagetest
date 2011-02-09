#include "StdAfx.h"
#include "URLBlaster.h"
#include <process.h>
#include <shlwapi.h>
#include <Userenv.h>
#include <Aclapi.h>
#include <Lm.h>
#include <WtsApi32.h>
#include "TraceRoute.h"

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
CURLBlaster::CURLBlaster(HWND hWnd, CLog &logRef)
: userName(_T(""))
, hLogonToken(NULL)
, hProfile(NULL)
, password(_T("2dialit"))
, accountBase(_T("user"))
, hThread(NULL)
, index(0)
, count(0)
, hDlg(hWnd)
, urlManager(NULL)
, testType(0)
, labID(0)
, dialerID(0)
, connectionType(0)
, timeout(60)
, experimental(0)
, sequentialErrors(0)
, screenShotErrors(0)
, browserPID(0)
, userSID(NULL)
, log(logRef)
, pipeIn(0)
, pipeOut(0)
, hDynaTrace(NULL)
, useBitBlt(0)
, winpcap(logRef)
{
	InitializeCriticalSection(&cs);
	hMustExit = CreateEvent(0, TRUE, FALSE, NULL );
	hClearedCache = CreateEvent(0, TRUE, FALSE, NULL );
	hRun = CreateEvent(0, TRUE, FALSE, NULL );
	srand(GetTickCount());
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
CURLBlaster::~CURLBlaster(void)
{
	SetEvent(hMustExit);
	
	// wait for the thread to exit (up to double the timeout value)
	if( hThread )
	{
		if( WaitForSingleObject(hThread, timeout * 2 * 1000) == WAIT_TIMEOUT )
			TerminateThread(hThread, 0);
		CloseHandle(hThread);
	}
	
	if( hLogonToken )
	{
		if( hProfile )
			UnloadUserProfile( hLogonToken, hProfile );

		CloseHandle( hLogonToken );
	}
		
	CloseHandle( hMustExit );
	CloseHandle( hClearedCache );
	CloseHandle( hRun );
	EnterCriticalSection(&cs);
	if( userSID )
	{
		HeapFree(GetProcessHeap(), 0, (LPVOID)userSID);
		userSID = 0;
	}
	LeaveCriticalSection(&cs);
	DeleteCriticalSection(&cs);
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
static unsigned __stdcall ThreadProc( void* arg )
{
	CURLBlaster * blaster = (CURLBlaster *)arg;
	if( blaster )
		blaster->ThreadProc();
		
	return 0;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CURLBlaster::Start(int userIndex)
{
	bool ret = false;
	
	// store off which user this worker belongs to
	index = userIndex;
	userName.Format(_T("%s%d"), (LPCTSTR)accountBase, index);
	
	info.userName = userName;
	
	// default directories
	TCHAR path[MAX_PATH];
	DWORD len = _countof(path);
	CString profiles = _T("C:\\Documents and Settings");
	if( GetProfilesDirectory(path, &len) )
		profiles = path;
	profiles += _T("\\");
		
	profile = profiles + userName;
	cookies = profile + _T("\\Cookies");
	history = profile + _T("\\Local Settings\\History");
	tempFiles = profile + _T("\\Local Settings\\Temporary Internet Files");
	silverlight = profile + _T("\\Local Settings\\Application Data\\Microsoft\\Silverlight");
	flash = profile + _T("\\Application Data\\Macromedia\\Flash Player\\#SharedObjects");

  // Get WinPCap ready (install it if necessary)
  winpcap.Initialize();

	// spawn the worker thread
	ResetEvent(hMustExit);
	hThread = (HANDLE)_beginthreadex(0, 0, ::ThreadProc, this, 0, 0);
	if( hThread )
		ret = true;
	
	return ret;
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
void CURLBlaster::Stop(void)
{
	SetEvent(hMustExit);
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
void CURLBlaster::ThreadProc(void)
{
	if( DoUserLogon() )
	{
		sequentialErrors = 0;
		
		log.Trace(_T("Waiting for startup synchronization"));

		// synchronize all of the threads and don't start testing until they have all cleared out the caches (heavy disk activity)
		SetEvent(hClearedCache);
		WaitForSingleObject(hRun, INFINITE);

		log.Trace(_T("Running..."));

		// start off with IPFW in a clean state
		ResetIpfw();

		while( WaitForSingleObject(hMustExit,0) == WAIT_TIMEOUT )
		{
			// get the url to test
			if(	GetUrl() )
			{
        if( info.testType.GetLength() )
        {
          // running a custom test
          do
          {
            if( !info.testType.CompareNoCase(_T("traceroute")) )
            {
              CTraceRoute tracert(info);
              tracert.Run();
            }

            urlManager->UrlFinished(info);
          }while( !info.done );
        }
        else if( !info.zipFileDir.IsEmpty() )
				{
					EncodeVideo();
					urlManager->UrlFinished(info);
				}
				else
				{
					// loop for as many runs as are needed for the current request
					do
					{
						ClearCache();

						if( Launch(preLaunch) )
						{
							LaunchBrowser();

							// record the cleared cache view
							if( urlManager->RunRepeatView(info) )
								LaunchBrowser();

							Launch(postLaunch);
						}

						urlManager->UrlFinished(info);
					}while( !info.done );
				}
			}
			else
				Sleep(500 + (rand() % 500));
		}
	}
}

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
bool CURLBlaster::DoUserLogon(void)
{
	bool ret = false;
	
	log.Trace(_T("Logging on user:%s, password:%s"), (LPCTSTR)userName, (LPCTSTR)password);
	
	// create the account if it doesn't exist
	USER_INFO_0 * userInfo = NULL;
	if( !NetUserGetInfo( NULL, userName, 0, (LPBYTE *)&userInfo ) )
		NetApiBufferFree(userInfo);
	else
	{
		USER_INFO_1 info;
		memset(&info, 0, sizeof(info));
		wchar_t name[1000];
		wchar_t pw[PWLEN];
		lstrcpyW(name, CT2W(userName));
		lstrcpyW(pw, CT2W(password));
		
		info.usri1_name = name;
		info.usri1_password = pw;
		info.usri1_priv = USER_PRIV_USER;
		info.usri1_comment = L"UrlBlast testing user account";
		info.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;

		if( !NetUserAdd(NULL, 1, (LPBYTE)&info, NULL) )
		{
			CString msg;
			msg.Format(_T("Created user account '%s'"), (LPCTSTR)userName);
			log.LogEvent(event_Info, 0, msg);
			
			// hide the account from the welcome screen
			HKEY hKey;
			if( RegCreateKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\SpecialAccounts\\UserList"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
			{
				DWORD val = 0;
				RegSetValueEx(hKey, userName, 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
				RegCloseKey(hKey);
			}
		}
		else
		{
			CString msg;
			msg.Format(_T("Failed to create user account '%s'"), (LPCTSTR)userName);
			log.LogEvent(event_Error, 0, msg);
		}
	}
	
	// log the user on
	if( LogonUser(userName, NULL, password, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hLogonToken) )
	{
		TCHAR szUserName[100];
		lstrcpy( szUserName, (LPCTSTR)userName);

		// get the SID for the account
		EnterCriticalSection(&cs);
		TOKEN_USER * user = NULL;
		DWORD userLen = 0;
		DWORD len = 0;
		GetTokenInformation(hLogonToken, TokenUser, &user, userLen, &len);
		if( len )
		{
			user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
			userLen = len;
			if( user )
			{
				if( GetTokenInformation(hLogonToken, TokenUser, user, userLen, &len) )
				{
					if( user->User.Sid && IsValidSid(user->User.Sid) )
					{
						len = GetLengthSid(user->User.Sid);
						userSID = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
						if( userSID )
							CopySid(len, userSID, user->User.Sid);
					}
				}
				HeapFree(GetProcessHeap(), 0, (LPVOID)user);

			}
		}
		LeaveCriticalSection(&cs);
		
		log.Trace(_T("Logon ok, loading user profile"));
		
		// load their profile
		PROFILEINFO userProfile;
		memset( &userProfile, 0, sizeof(userProfile) );
		userProfile.dwSize = sizeof(userProfile);
		userProfile.lpUserName = szUserName;

		if( LoadUserProfile( hLogonToken, &userProfile ) )
		{
			hProfile = userProfile.hProfile;
			
			log.Trace(_T("Profile loaded, locating profile directory"));
			
			// close the IE settings from the main OS user to the URLBlast user
			CloneIESettings();

			// figure out where their directories are
			TCHAR path[MAX_PATH];
			DWORD len = _countof(path);
			if( GetUserProfileDirectory(hLogonToken, path, &len) )
			{
				profile = path;
				
				HKEY hKey;
				if( SUCCEEDED(RegOpenKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders"), 0, KEY_READ, &hKey)) )
				{
					len = _countof(path);
					if( SUCCEEDED(RegQueryValueEx(hKey, _T("Cookies"), 0, 0, (LPBYTE)path, &len)) )
						cookies = path;

					len = _countof(path);
					if( SUCCEEDED(RegQueryValueEx(hKey, _T("History"), 0, 0, (LPBYTE)path, &len)) )
						history = path;

					len = _countof(path);
					if( SUCCEEDED(RegQueryValueEx(hKey, _T("Cache"), 0, 0, (LPBYTE)path, &len)) )
						tempFiles = path;

					len = _countof(path);
					if( SUCCEEDED(RegQueryValueEx(hKey, _T("Local AppData"), 0, 0, (LPBYTE)path, &len)) )
					{
						silverlight = path;
						silverlight += _T("\\Microsoft\\Silverlight");
					}

					len = _countof(path);
					if( SUCCEEDED(RegQueryValueEx(hKey, _T("AppData"), 0, 0, (LPBYTE)path, &len)) )
					{
						flash = path;
						flash += _T("\\Macromedia\\Flash Player\\#SharedObjects");
					}

					cookies.Replace(_T("%USERPROFILE%"), profile);
					history.Replace(_T("%USERPROFILE%"), profile);
					tempFiles.Replace(_T("%USERPROFILE%"), profile);
					silverlight.Replace(_T("%USERPROFILE%"), profile);
					flash.Replace(_T("%USERPROFILE%"), profile);
					
					RegCloseKey(hKey);
				}
			}
			
			ret = true;
		}
	}
	else
	{
		log.Trace(_T("Logon failed: %d"), GetLastError());
		CString msg;
		msg.Format(_T("Logon failed for '%s'"), (LPCTSTR)userName);
		log.LogEvent(event_Error, 0, msg);
	}

	if( ret )
		log.Trace(_T("DoUserLogon successful for %s"), (LPCTSTR)userName);
	else
		log.Trace(_T("DoUserLogon failed for %s"), (LPCTSTR)userName);
	
	return ret;
}

int cacheCount;

/*-----------------------------------------------------------------------------
	Launch a process in the given user space that will delete the appropriate folders
-----------------------------------------------------------------------------*/
void CURLBlaster::ClearCache(void)
{
	// delete the cookies, history and temporary internet files for this user
	DeleteDirectory( cookies );
	DeleteDirectory( history );
	cacheCount = 0;
	DeleteDirectory( tempFiles );
	CString buff;
	buff.Format(_T("%d files found in cache\n"), cacheCount);
	OutputDebugString(buff);
	DeleteDirectory( silverlight );
	DeleteDirectory( flash, false );
	
	cached = false;
}

/*-----------------------------------------------------------------------------
	recursively delete the given directory
-----------------------------------------------------------------------------*/
void DeleteDirectory( LPCTSTR inPath, bool remove )
{
	if( lstrlen(inPath) )
	{
		TCHAR * path = new TCHAR[MAX_PATH];	// allocate off of the heap so we don't blow the stack
		lstrcpy( path, inPath );
		PathAppend( path, _T("*.*") );
		
		WIN32_FIND_DATA fd;
		HANDLE hFind = FindFirstFile(path, &fd);
		if( hFind != INVALID_HANDLE_VALUE )
		{
			do
			{
				if( lstrcmp(fd.cFileName, _T(".")) && lstrcmp(fd.cFileName, _T("..")) )
				{
					lstrcpy( path, inPath );
					PathAppend( path, fd.cFileName );
					
					if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
						DeleteDirectory(path);
					else
					{
						cacheCount++;
						if( !DeleteFile(path) )
						{
							CString buff;
							buff.Format(_T("Failed to delete '%s'\n"), (LPCTSTR)path);
							OutputDebugString(buff);
						}
					}
				}
			}while(FindNextFile(hFind, &fd));
			
			FindClose(hFind);
		}
		
		delete [] path;
		
		// remove the actual directory
		if( remove )
			RemoveDirectory(inPath);
	}
}

/*-----------------------------------------------------------------------------
	Launch the browser and wait for it to exit
-----------------------------------------------------------------------------*/
bool CURLBlaster::LaunchBrowser(void)
{
	bool ret = false;
	info.testResult = -1;

	// flush the DNS cache
	FlushDNS();
	
	if( !info.url.IsEmpty() )
	{
		STARTUPINFOW si;
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);

		if( ConfigureIpfw() )
		{
			if( ConfigureIE() )
			{
				ConfigurePagetest();

        CString browser;
        if( info.browser.GetLength() )
	      {
		      // check to see if the browser is in the same directory - otherwise let the path find it
		      browser = info.browser;
          TCHAR buff[MAX_PATH];
		      if( GetModuleFileName(NULL, buff, _countof(buff)) )
		      {
			      lstrcpy( PathFindFileName(buff), PathFindFileName(browser) );
			      if( GetFileAttributes(buff) != INVALID_FILE_ATTRIBUTES )
				      browser = buff;
		      }
	      }
				
				// build the launch command for IE
				TCHAR exe[MAX_PATH];
				TCHAR commandLine[MAX_PATH + 1024];
				if( !info.url.Left(6).CompareNoCase(_T("run://")) )
				{
					// we're launching a custom exe
					CString cmd = info.url.Mid(6);
					CString options;
					int index = cmd.Find(' ');
					if( index > 0 )
					{
						options = cmd.Mid(index + 1);
						cmd = cmd.Left(index);
					}
					cmd.Trim();
					options.Trim();
					
					// get the full path for the exe
					lstrcpy(exe, cmd);

					// build the command line
					lstrcpy( commandLine, _T("\"") );
					lstrcat( commandLine, exe );
					lstrcat( commandLine, _T("\"") );
					if( options.GetLength() )
						lstrcat( commandLine, CString(" ") + options );
				}
        else if( browser.GetLength() )
				{
					// custom browser
					lstrcpy( exe, browser );

					// build the command line
					lstrcpy( commandLine, _T("\"") );
					lstrcat( commandLine, exe );
					lstrcat( commandLine, _T("\"") );
				}
				else
				{
					// we're launching IE
					SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, 0, SHGFP_TYPE_CURRENT, exe);
					PathAppend(exe, _T("Internet Explorer\\iexplore.exe"));
					
					// give it an about:blank command line for launch
					lstrcpy( commandLine, _T("\"") );
					lstrcat( commandLine, exe );
					lstrcat( commandLine, _T("\" about:blank") );

					// see if we need to launch dynaTrace
					LaunchDynaTrace();
				}

        // start a packet capture if we need to
        if( !info.tcpdumpFile.IsEmpty() )
          winpcap.StartCapture(info.tcpdumpFile);
				
				PROCESS_INFORMATION pi;
				
				log.Trace(_T("Launching... user='%s', path='%s', command line='%s'"), (LPCTSTR)userName, (LPCTSTR)exe, (LPCTSTR)commandLine);
				
				// launch internet explorer as our user
				EnterCriticalSection(&cs);
				if( CreateProcessWithLogonW(CT2W((LPCTSTR)userName), NULL, CT2W((LPCTSTR)password), 0, CT2W(exe), CT2W(commandLine), 0, NULL, NULL, &si, &pi) )
				{
					// keep track of the process ID for the browser we care about
					browserPID = pi.dwProcessId;
					LeaveCriticalSection(&cs);
					
					// boost the browser priority
					SetPriorityClass(pi.hProcess, ABOVE_NORMAL_PRIORITY_CLASS);
					
					log.LogEvent(event_BrowserLaunch, 0, (LPCTSTR)eventName.Left(1000));
					
					// wait for it to exit - give it up to double the timeout value
					// TODO:  have urlManager specify the timeout
					int multiple = 2;
          if( info.runningScript || info.aft )
						multiple = 10;
					if( WaitForSingleObject(pi.hProcess, timeout * multiple * 1000) == WAIT_OBJECT_0 )
					{
						count++;
						cached = true;
						ret = true;
						if( hDlg )
							PostMessage(hDlg, MSG_UPDATE_UI, 0, 0);
					}
					else
					{
						log.LogEvent(event_TerminatedBrowser, 0, (LPCTSTR)eventName.Left(1000));
						TerminateProcess(pi.hProcess, 0);	// kill the browser if it didn't exit on it's own
					}
					
					EnterCriticalSection(&cs);
					browserPID = 0;
					LeaveCriticalSection(&cs);
					
					CloseHandle(pi.hThread);
					CloseHandle(pi.hProcess);
					
					// get the result
					HKEY hKey;
					if( RegCreateKeyEx((HKEY)hProfile, _T("SOFTWARE\\AOL\\ieWatch"), 0, 0, 0, KEY_READ | KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
					{
						DWORD len = sizeof(info.testResult);
						if( RegQueryValueEx(hKey, _T("Result"), 0, 0, (LPBYTE)&info.testResult, &len) == ERROR_SUCCESS )
						{
							// only track sequential errors when not running a script
							if( !info.runningScript )
							{
								if( info.testResult & 0x80000000 )
									sequentialErrors++;
								else
									sequentialErrors = 0;
							}
						}
								
						RegDeleteValue(hKey, _T("Result"));
						
						// delete a few other keys we don't want to persist
						RegDeleteValue(hKey, _T("Use Address"));
						RegDeleteValue(hKey, _T("DNS Latency"));
						
						RegCloseKey(hKey);
					}

					// clean up any processes that may have been spawned
					KillProcs();

					// see if we are running in crawler mode and collected links
					if( info.harvestLinks )
						urlManager->HarvestedLinks(info);
				}
				else
				{
					LeaveCriticalSection(&cs);
					LPVOID lpvMessageBuffer;
					FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),  (LPTSTR)&lpvMessageBuffer, 0, NULL);
					LPCTSTR szMsg = (LPCTSTR)lpvMessageBuffer;
					LocalFree(lpvMessageBuffer);

					CString msg;
					msg.Format(_T("Failed to launch browser '%s' - %s"), (LPCTSTR)szMsg, (LPCTSTR)exe);
					log.LogEvent(event_Error, 0, msg);

					SetEvent(hMustExit);	// something went horribly wrong, this should never happen but don't get stuck in a loop
				}

        // stop the tcpdump if we started one
        if( !info.tcpdumpFile.IsEmpty() )
          winpcap.StopCapture();

				CloseDynaTrace();
			}

			ResetIpfw();
		}
	}
	
	return ret;
}

/*-----------------------------------------------------------------------------
	Get the next url to test
-----------------------------------------------------------------------------*/
bool CURLBlaster::GetUrl(void)
{
	bool ret = false;
	info.Reset();

	// get a new url from the central url manager
	if( urlManager->GetNextUrl(info) )
	{
		info.eventText += customEventText;
		
		ret = true;
	}
	
	return ret;
}

/*-----------------------------------------------------------------------------
	Store the stuff pagetest needs in the registry
-----------------------------------------------------------------------------*/
void CURLBlaster::ConfigurePagetest(void)
{
	if( hProfile )
	{
		// tell it what url to test
		HKEY hKey;
		if( RegCreateKeyEx((HKEY)hProfile, _T("SOFTWARE\\AOL\\ieWatch"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			RegSetValueEx(hKey, _T("url"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.url, (info.url.GetLength() + 1) * sizeof(TCHAR));
			DWORD block = 1;
			RegSetValueEx(hKey, _T("Block All Popups"), 0, REG_DWORD, (const LPBYTE)&block, sizeof(block));
			RegSetValueEx(hKey, _T("Timeout"), 0, REG_DWORD, (const LPBYTE)&timeout, sizeof(timeout));
			
			// tell ieWatch where to place the browser window
			DWORD val = 0;
			RegSetValueEx(hKey, _T("Window Left"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("Window Top"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			val = pos.right;
			RegSetValueEx(hKey, _T("Window Width"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			val = pos.bottom;
			RegSetValueEx(hKey, _T("Window Height"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			// pass it the IP address to use
			RegDeleteValue(hKey, _T("Use Address"));
			if( !ipAddress.IsEmpty() )
				RegSetValueEx(hKey, _T("Use Address"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)ipAddress, (ipAddress.GetLength() + 1) * sizeof(TCHAR));

			// delete any old results from the reg key
			RegDeleteValue(hKey, _T("Result"));

			RegCloseKey(hKey);
		}
		
		// create the event name
		DWORD isCached = 0;
		CString cachedString;
		if( cached )
		{
			cachedString = _T("Cached-");
			isCached = 1;
		}
		else
		{
			cachedString = _T("Cleared Cache-");
		}

		eventName = cachedString + info.eventText + _T("^");
		if( info.runningScript )
		{
			TCHAR script[MAX_PATH];
			lstrcpy(script, info.url.Right(info.url.GetLength() - 9));
			eventName += PathFindFileName(script);
		}
		else
			eventName += info.url;
		
		// give it the event name and log file location
		if( RegCreateKeyEx((HKEY)hProfile, _T("SOFTWARE\\America Online\\SOM"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			RegDeleteValue(hKey, _T("IEWatchLog"));
			if( !info.logFile.IsEmpty() )
				RegSetValueEx(hKey, _T("IEWatchLog"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.logFile, (info.logFile.GetLength() + 1) * sizeof(TCHAR));
			
			RegDeleteValue(hKey, _T("Links File"));
			if( info.harvestLinks && !info.linksFile.IsEmpty() )
			{
				DeleteFile(info.linksFile);
				RegSetValueEx(hKey, _T("Links File"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.linksFile, (info.linksFile.GetLength() + 1) * sizeof(TCHAR));			
			}
				
			RegDeleteValue(hKey, _T("404 File"));
			if( !info.s404File.IsEmpty() )
				RegSetValueEx(hKey, _T("404 File"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.s404File, (info.s404File.GetLength() + 1) * sizeof(TCHAR));			

			RegDeleteValue(hKey, _T("HTML File"));
			if( !info.htmlFile.IsEmpty() )
				RegSetValueEx(hKey, _T("HTML File"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.htmlFile, (info.htmlFile.GetLength() + 1) * sizeof(TCHAR));			

			RegDeleteValue(hKey, _T("Cookies File"));
			if( !info.cookiesFile.IsEmpty() )
				RegSetValueEx(hKey, _T("Cookies File"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.cookiesFile, (info.cookiesFile.GetLength() + 1) * sizeof(TCHAR));			

			RegSetValueEx(hKey, _T("EventName"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)eventName, (eventName.GetLength() + 1) * sizeof(TCHAR));
			RegSetValueEx(hKey, _T("LabID"), 0, REG_DWORD, (const LPBYTE)&labID, sizeof(labID));
			RegSetValueEx(hKey, _T("DialerID"), 0, REG_DWORD, (const LPBYTE)&dialerID, sizeof(dialerID));
			RegSetValueEx(hKey, _T("ConnectionType"), 0, REG_DWORD, (const LPBYTE)&connectionType, sizeof(connectionType));
			RegSetValueEx(hKey, _T("Cached"), 0, REG_DWORD, (const LPBYTE)&isCached, sizeof(isCached));
			RegSetValueEx(hKey, _T("URL"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.url, (info.url.GetLength() + 1) * sizeof(TCHAR));
			RegSetValueEx(hKey, _T("DOM Element ID"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.domElement, (info.domElement.GetLength() + 1) * sizeof(TCHAR));
			RegSetValueEx(hKey, _T("Experimental"), 0, REG_DWORD, (const LPBYTE)&experimental, sizeof(experimental));
			RegSetValueEx(hKey, _T("Screen Shot Errors"), 0, REG_DWORD, (const LPBYTE)&screenShotErrors, sizeof(screenShotErrors));
			RegSetValueEx(hKey, _T("Check Optimizations"), 0, REG_DWORD, (const LPBYTE)&info.checkOpt, sizeof(info.checkOpt));
			
			RegSetValueEx(hKey, _T("Include Object Data"), 0, REG_DWORD, (const LPBYTE)&info.includeObjectData, sizeof(info.includeObjectData));


			RegSetValueEx(hKey, _T("ignoreSSL"), 0, REG_DWORD, (const LPBYTE)&info.ignoreSSL, sizeof(info.ignoreSSL));
			RegSetValueEx(hKey, _T("useBitBlt"), 0, REG_DWORD, (const LPBYTE)&useBitBlt, sizeof(useBitBlt));
			
			CString descriptor = _T("Launch");
			RegSetValueEx(hKey, _T("Descriptor"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)descriptor, (descriptor.GetLength() + 1) * sizeof(TCHAR));

			DWORD abm = 2;
			if( info.urlType == 1 )
				abm = 0;
			else if( info.urlType == 2 )
				abm = 1;
			RegSetValueEx(hKey, _T("ABM"), 0, REG_DWORD, (const LPBYTE)&abm, sizeof(abm));

			DWORD val = 0;
			if( info.saveEverything )
				val = 1;
			RegSetValueEx(hKey, _T("Save Everything"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			val = 0;
			if( info.captureVideo )
				val = 1;
			RegSetValueEx(hKey, _T("Capture Video"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

		  RegSetValueEx(hKey, _T("AFT"), 0, REG_DWORD, (const LPBYTE)&info.aft, sizeof(info.aft));

			RegDeleteValue(hKey, _T("Block"));
			if( !info.block.IsEmpty() )
				RegSetValueEx(hKey, _T("Block"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.block, (info.block.GetLength() + 1) * sizeof(TCHAR));
				
			RegDeleteValue(hKey, _T("Basic Auth"));
			if( !info.basicAuth.IsEmpty() )
				RegSetValueEx(hKey, _T("Basic Auth"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.basicAuth, (info.basicAuth.GetLength() + 1) * sizeof(TCHAR));

			RegDeleteValue(hKey, _T("Host"));
			if( !info.host.IsEmpty() )
				RegSetValueEx(hKey, _T("Host"), 0, REG_SZ, (const LPBYTE)(LPCTSTR)info.host, (info.host.GetLength() + 1) * sizeof(TCHAR));

			RegCloseKey(hKey);
		}
		
	}
}

/*-----------------------------------------------------------------------------
	Setup the IE settings so we don't get a bunch of dialogs
-----------------------------------------------------------------------------*/
bool CURLBlaster::ConfigureIE(void)
{
	bool ret = false;

	if( hProfile )
	{
		ret = true;

		// Set some basic IE options
		HKEY hKey;
		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Internet Explorer\\Main"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			LPCTSTR szVal = _T("yes");
			RegSetValueEx(hKey, _T("DisableScriptDebuggerIE"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));

			szVal = _T("no");
			RegSetValueEx(hKey, _T("FormSuggest PW Ask"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));
			RegSetValueEx(hKey, _T("Friendly http errors"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));
			RegSetValueEx(hKey, _T("Use FormSuggest"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));

			DWORD val = 1;
			RegSetValueEx(hKey, _T("NoUpdateCheck"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("NoJITSetup"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("NoWebJITSetup"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			RegCloseKey(hKey);
		}
		
		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Internet Explorer\\InformationBar"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			DWORD val = 0;
			RegSetValueEx(hKey, _T("FirstTime"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			RegCloseKey(hKey);
		}

		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Internet Explorer\\IntelliForms"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			DWORD val = 0;
			RegSetValueEx(hKey, _T("AskUser"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			RegCloseKey(hKey);
		}

		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Internet Explorer\\Security"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			LPCTSTR szVal = _T("Query");
			RegSetValueEx(hKey, _T("Safety Warning Level"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));

			szVal = _T("Medium");
			RegSetValueEx(hKey, _T("Sending_Security"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));

			szVal = _T("Low");
			RegSetValueEx(hKey, _T("Viewing_Security"), 0, REG_SZ, (const LPBYTE)szVal, (lstrlen(szVal) + 1) * sizeof(TCHAR));

			RegCloseKey(hKey);
		}

		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			DWORD val = 1;
			RegSetValueEx(hKey, _T("AllowCookies"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("EnableHttp1_1"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("ProxyHttp1.1"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("EnableNegotiate"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			val = 0;
			RegSetValueEx(hKey, _T("WarnAlwaysOnPost"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("WarnonBadCertRecving"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("WarnOnPost"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("WarnOnPostRedirect"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegSetValueEx(hKey, _T("WarnOnZoneCrossing"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			//RegSetValueEx(hKey, _T("ProxyEnable"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			
			RegCloseKey(hKey);
		}

		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\5.0\\Cache\\Content"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			DWORD val = 131072;
			RegSetValueEx(hKey, _T("CacheLimit"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegCloseKey(hKey);
		}

		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Cache\\Content"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			DWORD val = 131072;
			RegSetValueEx(hKey, _T("CacheLimit"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));
			RegCloseKey(hKey);
		}

		// reset the toolbar layout (to make sure the sidebar isn't open)		
		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Internet Explorer\\Toolbar\\WebBrowser"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			RegDeleteValue(hKey, _T("ITBarLayout"));
			RegCloseKey(hKey);
		}
		
		// Tweak the security zone to eliminate some warnings
		if( RegCreateKeyEx((HKEY)hProfile, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\Zones\\3"), 0, 0, 0, KEY_WRITE, 0, &hKey, 0) == ERROR_SUCCESS )
		{
			DWORD val = 0;
			
			// don't warn about posting data
			RegSetValueEx(hKey, _T("1601"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			// don't warn about mixed content
			RegSetValueEx(hKey, _T("1609"), 0, REG_DWORD, (const LPBYTE)&val, sizeof(val));

			RegCloseKey(hKey);
		}
	}

	return ret;
}

/*-----------------------------------------------------------------------------
	Recusrively copy the IE settings
-----------------------------------------------------------------------------*/
void CURLBlaster::CloneIESettings(void)
{
	CloneRegKey( HKEY_CURRENT_USER, (HKEY)hProfile, _T("Software\\Microsoft\\Internet Explorer") );
	CloneRegKey( HKEY_CURRENT_USER, (HKEY)hProfile, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings") );
}

/*-----------------------------------------------------------------------------
	Recusrively copy a registry key
-----------------------------------------------------------------------------*/
void CURLBlaster::CloneRegKey(HKEY hSrc, HKEY hDest, LPCTSTR subKey)
{
	HKEY src;
	if( RegOpenKeyEx(hSrc, subKey, 0, KEY_READ, &src) == ERROR_SUCCESS )
	{
		HKEY dest;
		if( RegCreateKeyEx(hDest, subKey, 0, 0, 0, KEY_WRITE, 0, &dest, 0) == ERROR_SUCCESS )
		{
			// copy all of the values over
			DWORD nameSize = 16384;
			DWORD valSize = 32767;
			
			TCHAR * name = new TCHAR[nameSize];
			LPBYTE data = new BYTE[valSize];
			DWORD nameLen = nameSize;
			DWORD dataLen = valSize;

			DWORD type;
			DWORD index = 0;
			while( RegEnumValue(src, index, name, &nameLen, 0, &type, data, &dataLen) == ERROR_SUCCESS )
			{
				RegSetValueEx(dest, name, 0, type, data, dataLen);
				
				index++;
				nameLen = nameSize;
				dataLen = valSize;
			}
			
			// copy all of the sub-keys over
			index = 0;
			nameLen = nameSize;
			while( RegEnumKeyEx(src, index, name, &nameLen, 0, 0, 0, 0) == ERROR_SUCCESS )
			{
        // don't copy the search providers key, this can triggere IE messages
        if( _tcsicmp(name, _T("SearchScopes")) )
				  CloneRegKey(src, dest, name);
				
				index++;
				nameLen = nameSize;
			}

			delete [] name;
			delete [] data;
			
			RegCloseKey(dest);
		}
		
		RegCloseKey(src);
	}
}

/*-----------------------------------------------------------------------------
	Encode a video job
-----------------------------------------------------------------------------*/
void CURLBlaster::EncodeVideo(void)
{
	TCHAR path[MAX_PATH];
	if( GetModuleFileName(NULL, path, _countof(path)) )
	{
		lstrcpy(PathFindFileName(path), _T("x264.exe"));
		CString exe(path);
		CString cmd = CString(_T("\"")) + exe + _T("\" --crf 24 --threads 1 --keyint 10 --min-keyint 1 -o video.mp4 video.avs");

		PROCESS_INFORMATION pi;
		STARTUPINFO si;
		memset( &si, 0, sizeof(si) );
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		log.Trace(_T("Executing '%s' in '%s'"), (LPCTSTR)cmd, (LPCTSTR)info.zipFileDir);
		if( CreateProcess((LPCTSTR)exe, (LPTSTR)(LPCTSTR)cmd, 0, 0, FALSE, IDLE_PRIORITY_CLASS , 0, (LPCTSTR)info.zipFileDir, &si, &pi) )
		{
			WaitForSingleObject(pi.hProcess, 60 * 60 * 1000);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			log.Trace(_T("Successfully ran '%s'"), (LPCTSTR)cmd);
		}
		else
			log.Trace(_T("Execution failed '%s'"), (LPCTSTR)cmd);
	}
}

/*-----------------------------------------------------------------------------
	Launch the given exe and ensure that we get a clean return code
-----------------------------------------------------------------------------*/
bool CURLBlaster::Launch(CString cmd, HANDLE * phProc)
{
	bool ret = false;

	if( cmd.GetLength() )
	{
		PROCESS_INFORMATION pi;
		STARTUPINFO si;
		memset( &si, 0, sizeof(si) );
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		log.Trace(_T("Executing '%s'"), (LPCTSTR)cmd);
		if( CreateProcess(NULL, (LPTSTR)(LPCTSTR)cmd, 0, 0, FALSE, NORMAL_PRIORITY_CLASS , 0, NULL, &si, &pi) )
		{
			if( phProc )
			{
				*phProc = pi.hProcess;
				CloseHandle(pi.hThread);
			}
			else
			{
				WaitForSingleObject(pi.hProcess, 60 * 60 * 1000);

				DWORD code;
				if( GetExitCodeProcess(pi.hProcess, &code) && code == 0 )
					ret = true;

				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
				log.Trace(_T("Successfully ran '%s'"), (LPCTSTR)cmd);
			}
		}
		else
			log.Trace(_T("Execution failed '%s'"), (LPCTSTR)cmd);
	}
	else
		ret = true;

	return ret;
}

/*-----------------------------------------------------------------------------
	Set up bandwidth throttling
-----------------------------------------------------------------------------*/
bool CURLBlaster::ConfigureIpfw(void)
{
	bool ret = false;

	if( pipeIn && pipeOut && info.ipfw && info.bwIn && info.bwOut )
	{
		// split the latency across directions
		DWORD latency = info.latency / 2;

		CString buff;
		buff.Format(_T("[urlblast] - Throttling: %d Kbps in, %d Kbps out, %d ms latency, %0.2f plr"), info.bwIn, info.bwOut, info.latency, info.plr );
		OutputDebugString(buff);

		// create the inbound pipe
		if( ipfw.CreatePipe(pipeIn, info.bwIn * 1000, latency, info.plr / 100.0) )
		{
			// make up for odd values
			if( info.latency % 2 )
				latency++;

			// create the outbound pipe
			if( ipfw.CreatePipe(pipeOut, info.bwOut * 1000, latency, info.plr / 100.0) )
				ret = true;
			else
				ipfw.CreatePipe(pipeIn, 0, 0, 0);
		}
	}
	else
		ret = true;

	return ret;
}

/*-----------------------------------------------------------------------------
	Remove the bandwidth throttling
-----------------------------------------------------------------------------*/
void CURLBlaster::ResetIpfw(void)
{
	if( pipeIn )
		ipfw.CreatePipe(pipeIn, 0, 0, 0);
	if( pipeOut )
		ipfw.CreatePipe(pipeOut, 0, 0, 0);
}

/*-----------------------------------------------------------------------------
	Terminate any procs that are running under our test user account
	in case something got spawned while testing
-----------------------------------------------------------------------------*/
void CURLBlaster::KillProcs()
{
	#ifndef _DEBUG
	
	WTS_PROCESS_INFO * proc = NULL;
	DWORD count = 0;
	if( WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &proc, &count) )
	{
		for( DWORD i = 0; i < count; i++ )
		{
			// see if the SID matches
			if( userSID && proc[i].pUserSid && IsValidSid(userSID) && IsValidSid(proc[i].pUserSid) )
			{
				if( EqualSid(proc[i].pUserSid, userSID ) )
				{
					HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, proc[i].ProcessId);
					if( hProc )
					{
						TerminateProcess(hProc, 0);
						CloseHandle(hProc);
					}
				}
			}
		}
		
		WTSFreeMemory(proc);
	}
	#endif
}

typedef int (CALLBACK* DNSFLUSHPROC)();

/*-----------------------------------------------------------------------------
	Flush the OS DNS cache
-----------------------------------------------------------------------------*/
void CURLBlaster::FlushDNS()
{
	bool flushed = false;
	HINSTANCE		hDnsDll;

	log.Trace(_T("Flushing DNS cache"));

	hDnsDll = LoadLibrary(_T("dnsapi.dll"));
	if( hDnsDll )
	{
		DNSFLUSHPROC pDnsFlushProc = (DNSFLUSHPROC)GetProcAddress(hDnsDll, "DnsFlushResolverCache");
		if( pDnsFlushProc )
		{
			int ret = pDnsFlushProc();
			if( ret == ERROR_SUCCESS)
			{
				flushed = true;
				log.Trace(_T("Successfully flushed the DNS resolved cache"));
			}
			else
				log.Trace(_T("DnsFlushResolverCache returned %d"), ret);
		}
		else
			log.Trace(_T("Failed to load dnsapi.dll"));

		FreeLibrary(hDnsDll);
	}
	else
		log.Trace(_T("Failed to load dnsapi.dll"));

	if( !flushed )
		Launch(_T("ipconfig.exe /flushdns"));
}

/*-----------------------------------------------------------------------------
	Launch Dynatrace (if necessary)
-----------------------------------------------------------------------------*/
void CURLBlaster::LaunchDynaTrace()
{
	if( !dynaTrace.IsEmpty() )
	{
		Launch(dynaTrace, &hDynaTrace);
		WaitForInputIdle(hDynaTrace, 30000);
	}
}

/*-----------------------------------------------------------------------------
	Close Dynatrace (if necessary)
-----------------------------------------------------------------------------*/
void CURLBlaster::CloseDynaTrace()
{
	if( hDynaTrace )
	{
		HWND hWnd = FindWindow(NULL, _T("dynaTrace AJAX Edition"));
		PostMessage(hWnd, WM_CLOSE, 0, 0);
		if( WaitForSingleObject(hDynaTrace, 60000) == WAIT_TIMEOUT )
			TerminateProcess(hDynaTrace,0);
		CloseHandle(hDynaTrace);

		// zip up the profile data to our test results folder
		TCHAR path[MAX_PATH];
		DWORD len = _countof(path);
		lstrcpy(path, _T("C:\\Documents and Settings"));
		GetProfilesDirectory(path, &len);
		TCHAR name[MAX_PATH];
		len = _countof(name);
		if( GetUserName(name, &len) )
		{
			lstrcat(path, _T("\\"));
			lstrcat(path, name);
			lstrcat(path, _T("\\.dynaTrace\\ajax\\browser\\iesessions"));
			ZipDir(path, info.logFile + _T("_dynaTrace.dtas"), _T(""), NULL);
		}
	}
}

/*-----------------------------------------------------------------------------
	Archive (and delete) the given directory
-----------------------------------------------------------------------------*/
void CURLBlaster::ZipDir(CString dir, CString dest, CString depth, zipFile file)
{
	bool top = false;

	// start by creating an empty zip file
	if( !file )
	{
		file = zipOpen(CT2A(dest), APPEND_STATUS_CREATE);
		top = true;
	}

	if( file )
	{
		WIN32_FIND_DATA fd;
		HANDLE hFind = FindFirstFile( dir + _T("\\*.*"), &fd );
		if( hFind != INVALID_HANDLE_VALUE )
		{
			do
			{
				// skip over . and ..
				if( lstrcmp(fd.cFileName, _T(".")) && lstrcmp(fd.cFileName, _T("..")) )
				{
					if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
					{
						CString d = fd.cFileName;
						if( depth.GetLength() )
							d = depth + CString(_T("\\")) + fd.cFileName;

						ZipDir( dir + CString(_T("\\")) + fd.cFileName, dest, d, file);
						RemoveDirectory(dir + CString(_T("\\")) + fd.cFileName);
					}
					else
					{
						CString archiveFile;
						if( depth.GetLength() )
							archiveFile = depth + CString(_T("/"));
						archiveFile += fd.cFileName;

						CString filePath = dir + CString(_T("\\")) + fd.cFileName;

						// add the file to the zip archive
						HANDLE hFile = CreateFile( filePath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
						if( hFile != INVALID_HANDLE_VALUE )
						{
							DWORD size = GetFileSize(hFile, 0);
							if( size )
							{
								BYTE * mem = (BYTE *)malloc(size);
								if( mem )
								{
									DWORD bytes;
									if( ReadFile(hFile, mem, size, &bytes, 0) && size == bytes )
									{
										// add the file to the archive
										if( !zipOpenNewFileInZip( file, CT2A(archiveFile), 0, 0, 0, 0, 0, 0, Z_DEFLATED, Z_BEST_COMPRESSION ) )
										{
											// write the file to the archive
											zipWriteInFileInZip( file, mem, size );
											zipCloseFileInZip( file );
										}
									}
									
									free(mem);
								}
							}
							
							CloseHandle( hFile );
						}

						DeleteFile(filePath);
					}
				}
			}while( FindNextFile(hFind, &fd) );
			FindClose(hFind);
		}
	}

	// if we're done with the root, delete everything
	if( top && file )
		zipClose(file, 0);
}
